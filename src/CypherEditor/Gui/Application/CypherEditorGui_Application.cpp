//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Application.cpp
//  Purpose: Implements the editor application context.
//  Details: The framework's resources live in a static library, so they are
//           registered explicitly (Q_INIT_RESOURCE) before first use; static
//           initialisers of a static library are not guaranteed to run.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Application.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <iterator>
#include <new>

// Q_INIT_RESOURCE must be used outside any namespace.
static void EnsureEditorResourcesRegistered()
{
    static const bool bRegistered = [] {
        Q_INIT_RESOURCE( CypherEditorResources );
        return true;
    }();
    ( void )bRegistered;
}

namespace cypher::editor::gui
{

using namespace cypher::common;

editor_gui_t::~editor_gui_t() noexcept
{
    EditorGui_Shutdown( this );
}

void EditorGui_RegisterResources()
{
    EnsureEditorResourcesRegistered();
}

editor_gui_status_t EditorGui_LoadResource( const char *pResourcePath, settings_document_t *pStore )
{
    CY_ASSERT( pResourcePath != nullptr && pStore != nullptr );
    EnsureEditorResourcesRegistered();
    QFile file( QString::fromUtf8( pResourcePath ) );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        CY_LOG_WRITE( Error, Gui, "A built-in editor resource is missing from the binary" );
        return editor_gui_status_t::RESOURCE_MISSING;
    }
    const QByteArray text = file.readAll();
    const settings_document_load_result_t loaded =
        SettingsDocument_Load( pStore, { text.constData(), static_cast<usize>( text.size() ) } );
    if ( loaded.status != settings_document_status_t::OK ) {
        CY_LOG_WRITE( Error, Gui, "A built-in editor resource does not parse" );
        return editor_gui_status_t::RESOURCE_INVALID;
    }
    return editor_gui_status_t::OK;
}

editor_gui_status_t EditorGui_LoadLayoutResource( const char *pResourcePath, layout_t *pLayout, const allocator_t *pAllocator )
{
    CY_ASSERT( pLayout != nullptr && pAllocator != nullptr );
    settings_document_t store{};
    if ( SettingsDocument_Init( &store, pAllocator, EditorLayout_Identity() ) != settings_document_status_t::OK ) {
        return editor_gui_status_t::OUT_OF_MEMORY;
    }
    const editor_gui_status_t status = EditorGui_LoadResource( pResourcePath, &store );
    if ( status != editor_gui_status_t::OK ) { return status; }
    const layout_status_t decoded = EditorLayout_Decode( store.pDocument, pLayout );
    if ( decoded != layout_status_t::OK ) {
        CY_LOG_WRITE( Error, Gui, "A built-in layout failed to decode" );
        return decoded == layout_status_t::OUT_OF_MEMORY ? editor_gui_status_t::OUT_OF_MEMORY : editor_gui_status_t::RESOURCE_INVALID;
    }
    return editor_gui_status_t::OK;
}

editor_gui_status_t EditorGui_Init( editor_gui_t *pGui, QApplication *pApplication, const allocator_t *pAllocator )
{
    if ( pGui == nullptr || pApplication == nullptr || pAllocator == nullptr || pGui->bInitialized ) {
        return editor_gui_status_t::INVALID_ARGUMENT;
    }
    // The log store goes first, so the console shows start-up from its first
    // line, like a game console's boot log.
    EditorLog_Init( &pGui->log );
    EditorLog_Install( &pGui->log );
    EnsureEditorResourcesRegistered();
    pGui->pAllocator = pAllocator;
    usize nSettings = 0u;
    const setting_descriptor_t *pSettings = EditorSettings_FrameworkCatalogue( &nSettings );
    const bool bOk = EditorCommands_Init( &pGui->commands, pAllocator ) == command_registry_status_t::OK &&
                     EditorSettings_Init( &pGui->settings, pAllocator ) == settings_registry_status_t::OK &&
                     EditorSettings_Register( &pGui->settings, pSettings, nSettings ) == settings_registry_status_t::OK &&
                     Vector_Init( &pGui->themes, pAllocator ) &&
                     Vector_Init( &pGui->keymaps, pAllocator ) &&
                     Vector_Init( &pGui->keymapLibrary, pAllocator ) &&
                     EditorThemeRegistry_Init( &pGui->themeTokens, pAllocator ) == theme_status_t::OK &&
                     EditorThemeLibrary_Init( &pGui->themeLibrary, pAllocator ) == theme_status_t::OK &&
                     SettingsDocument_Init( &pGui->builtinTheme, pAllocator, EditorTheme_Identity() ) == settings_document_status_t::OK &&
                     SettingsDocument_Init( &pGui->builtinKeymap, pAllocator, EditorKeymap_Identity() ) == settings_document_status_t::OK &&
                     EditorStyle_RegisterTokens( &pGui->themeTokens ) == theme_status_t::OK;
    pGui->bInitialized = CY_TRUE; // Shutdown must release whatever succeeded.
    if ( !bOk ) {
        EditorGui_Shutdown( pGui );
        return editor_gui_status_t::OUT_OF_MEMORY;
    }
    editor_gui_status_t status = EditorGui_LoadResource( EDITOR_BUILTIN_THEME_RESOURCE, &pGui->builtinTheme );
    if ( status == editor_gui_status_t::OK ) { status = EditorGui_LoadResource( EDITOR_BUILTIN_KEYMAP_RESOURCE, &pGui->builtinKeymap ); }
    if ( status != editor_gui_status_t::OK ) {
        EditorGui_Shutdown( pGui );
        return status;
    }
    const key_value_t *pThemeRoot = SettingsDocument_Root( &pGui->builtinTheme );
    if ( EditorThemeLibrary_Add( &pGui->themeLibrary, pThemeRoot ) != theme_status_t::OK ) {
        EditorGui_Shutdown( pGui );
        return editor_gui_status_t::OUT_OF_MEMORY;
    }
    bool_t bComplete = CY_FALSE;
    pGui->nThemeChain = EditorTheme_BuildChain( &pGui->themeLibrary, pThemeRoot, pGui->themeChain, std::size( pGui->themeChain ), &bComplete );
    CY_ASSERT_MSG( bComplete, "The built-in theme is a root theme" );
    // Presets build on the built-in theme, so they load after it; user
    // theme folders load later still and shadow a preset of the same ID.
    ( void )EditorGui_LoadThemeFolder( pGui, QString::fromLatin1( EDITOR_PRESET_THEME_FOLDER ) );
    const key_value_t *pKeymapRoot = SettingsDocument_Root( &pGui->builtinKeymap );
    if ( !Vector_PushBack( &pGui->keymapLibrary, pKeymapRoot ) ) {
        EditorGui_Shutdown( pGui );
        return editor_gui_status_t::OUT_OF_MEMORY;
    }
    pGui->nKeymapChain = EditorKeymap_BuildChain( pGui->keymapLibrary.pData, pGui->keymapLibrary.nCount, pKeymapRoot, pGui->keymapChain,
                                                  std::size( pGui->keymapChain ), &bComplete );
    CY_ASSERT_MSG( bComplete && pGui->nKeymapChain == 1u, "The built-in keymap is a root keymap" );

    // The built-in theme is audited so a token renamed in code but not in
    // the resource is caught at the first launch of a debug build.
    theme_problem_t problems[8]{};
    const usize nProblems = EditorTheme_Audit( &pGui->themeTokens, pThemeRoot, problems, std::size( problems ) );
    if ( nProblems != 0u ) { CY_LOG_WRITE( Warning, Gui, "The built-in theme names tokens or values the framework does not accept" ); }

    status = EditorGui_ApplyStyle( pGui, pApplication );
    if ( status != editor_gui_status_t::OK ) {
        EditorGui_Shutdown( pGui );
        return status;
    }
    const auto logLine = []( const QString &text ) {
        const QByteArray utf8 = text.toUtf8();
        Cy_LogWriteAt( log_level_t::Info, log_channel_t::Gui, utf8.constData(), CY_SOURCE_LOCATION );
    };
    const auto idText = []( string_view_t view ) { return QString::fromUtf8( view.pData, static_cast<qsizetype>( view.cchLength ) ); };
    logLine( QStringLiteral( "Theme \"%1\": %2 tokens registered" ).arg( idText( EditorGui_ActiveThemeId( pGui ) ) ).arg( pGui->themeTokens.tokens.nCount ) );
    logLine( QStringLiteral( "Keymap \"%1\" loaded" ).arg( idText( EditorGui_ActiveKeymapId( pGui ) ) ) );
    logLine( QStringLiteral( "Settings registry: %1 framework settings" ).arg( EditorSettings_Count( &pGui->settings ) ) );
    CY_LOG_WRITE( Info, Gui, "Editor framework initialised" );
    return editor_gui_status_t::OK;
}

void EditorGui_Shutdown( editor_gui_t *pGui ) noexcept
{
    if ( pGui == nullptr || !pGui->bInitialized ) { return; }
    EditorLog_Uninstall( &pGui->log );
    pGui->nThemeChain = 0u;
    pGui->nKeymapChain = 0u;
    pGui->nStyleListeners = 0u;
    pGui->nKeymapListeners = 0u;
    for ( usize i = 0u; i < pGui->keymaps.nCount; ++i ) {
        SettingsDocument_Shutdown( pGui->keymaps.pData[i] );
        delete pGui->keymaps.pData[i];
    }
    Vector_Shutdown( &pGui->keymaps );
    Vector_Shutdown( &pGui->keymapLibrary );
    for ( usize i = 0u; i < pGui->themes.nCount; ++i ) {
        SettingsDocument_Shutdown( pGui->themes.pData[i] );
        delete pGui->themes.pData[i];
    }
    Vector_Shutdown( &pGui->themes );
    EditorThemeLibrary_Shutdown( &pGui->themeLibrary );
    EditorThemeRegistry_Shutdown( &pGui->themeTokens );
    EditorCommands_Shutdown( &pGui->commands );
    EditorSettings_Shutdown( &pGui->settings );
    SettingsDocument_Shutdown( &pGui->builtinKeymap );
    SettingsDocument_Shutdown( &pGui->builtinTheme );
    pGui->bInitialized = CY_FALSE;
}

editor_gui_status_t EditorGui_ApplyStyle( editor_gui_t *pGui, QApplication *pApplication )
{
    if ( pGui == nullptr || pApplication == nullptr || !pGui->bInitialized ) { return editor_gui_status_t::INVALID_ARGUMENT; }
    pGui->style = EditorStyle_Resolve( &pGui->themeTokens, pGui->themeChain, pGui->nThemeChain );
    if ( pGui->style.nInvalidSkipped != 0u ) { CY_LOG_WRITE( Warning, Gui, "The active theme has invalid values; defaults were used for them" ); }
    if ( !EditorStyle_Apply( pApplication, pGui->style ) ) { return editor_gui_status_t::STYLE_FAILED; }
    // A listener may unsubscribe while being told.
    editor_style_listener_t snapshot[EDITOR_GUI_MAX_STYLE_LISTENERS];
    const usize nListeners = pGui->nStyleListeners;
    std::copy( pGui->styleListeners, pGui->styleListeners + nListeners, snapshot );
    for ( usize i = 0u; i < nListeners; ++i ) { snapshot[i].pfnChanged( snapshot[i].pContext ); }
    return editor_gui_status_t::OK;
}

namespace
{

// Library order is lookup order: loaded themes newest first, so a user theme
// shadows a project theme of the same ID, and the built-in theme last.
CYPHER_NODISCARD bool RebuildLibrary( editor_gui_t *pGui ) noexcept
{
    Vector_Clear( &pGui->themeLibrary.roots );
    for ( usize i = pGui->themes.nCount; i-- > 0u; ) {
        if ( EditorThemeLibrary_Add( &pGui->themeLibrary, SettingsDocument_Root( pGui->themes.pData[i] ) ) != theme_status_t::OK ) { return false; }
    }
    return EditorThemeLibrary_Add( &pGui->themeLibrary, SettingsDocument_Root( &pGui->builtinTheme ) ) == theme_status_t::OK;
}

CYPHER_NODISCARD editor_gui_status_t ApplyChainFrom( editor_gui_t *pGui, QApplication *pApplication, const key_value_t *pThemeRoot )
{
    bool_t bComplete = CY_FALSE;
    pGui->nThemeChain = EditorTheme_BuildChain( &pGui->themeLibrary, pThemeRoot, pGui->themeChain, std::size( pGui->themeChain ), &bComplete );
    if ( !bComplete ) { CY_LOG_WRITE( Warning, Gui, "Theme names a base that is missing or forms a cycle; the chain stops there" ); }
    return EditorGui_ApplyStyle( pGui, pApplication );
}

} // namespace

editor_gui_status_t EditorGui_AddTheme( editor_gui_t *pGui, const QString &text, QString *pIdOut )
{
    if ( pGui == nullptr || !pGui->bInitialized ) { return editor_gui_status_t::INVALID_ARGUMENT; }
    auto *pStore = new ( std::nothrow ) settings_document_t{};
    if ( pStore == nullptr || SettingsDocument_Init( pStore, pGui->pAllocator, EditorTheme_Identity() ) != settings_document_status_t::OK ) {
        delete pStore;
        return editor_gui_status_t::OUT_OF_MEMORY;
    }
    const QByteArray utf8 = text.toUtf8();
    const settings_document_load_result_t loaded =
        SettingsDocument_Load( pStore, string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) } );
    const theme_header_t header = EditorTheme_Header( SettingsDocument_Root( pStore ) );
    if ( loaded.status != settings_document_status_t::OK || header.id.cchLength == 0u ) {
        SettingsDocument_Shutdown( pStore );
        delete pStore;
        return editor_gui_status_t::INVALID_ARGUMENT;
    }
    const QString id = QString::fromUtf8( header.id.pData, static_cast<qsizetype>( header.id.cchLength ) );
    const QByteArray activeId = pGui->nThemeChain != 0u ? QByteArray( EditorGui_ActiveThemeId( pGui ).pData,
                                                                      static_cast<qsizetype>( EditorGui_ActiveThemeId( pGui ).cchLength ) )
                                                        : QByteArray();
    // Replace an earlier theme with the same ID; its document goes away, so
    // the active chain is rebuilt below if it pointed into it.
    for ( usize i = 0u; i < pGui->themes.nCount; ++i ) {
        const theme_header_t other = EditorTheme_Header( SettingsDocument_Root( pGui->themes.pData[i] ) );
        if ( !StringView_Equals( other.id, header.id ) ) { continue; }
        for ( usize iChain = 0u; iChain < pGui->nThemeChain; ++iChain ) {
            if ( pGui->themeChain[iChain] == SettingsDocument_Root( pGui->themes.pData[i] ) ) { pGui->nThemeChain = 0u; }
        }
        SettingsDocument_Shutdown( pGui->themes.pData[i] );
        delete pGui->themes.pData[i];
        Vector_Erase( &pGui->themes, i );
        break;
    }
    if ( !Vector_PushBack( &pGui->themes, pStore ) ) {
        SettingsDocument_Shutdown( pStore );
        delete pStore;
        return editor_gui_status_t::OUT_OF_MEMORY;
    }
    if ( !RebuildLibrary( pGui ) ) { return editor_gui_status_t::OUT_OF_MEMORY; }
    if ( pGui->nThemeChain == 0u && !activeId.isEmpty() ) {
        // The chain lost its storage: select the same ID again (now the new
        // document) so the style never points at freed memory.
        for ( usize i = 0u; i < pGui->themeLibrary.roots.nCount; ++i ) {
            const key_value_t *pRoot = pGui->themeLibrary.roots.pData[i];
            if ( StringView_Equals( EditorTheme_Header( pRoot ).id, string_view_t{ activeId.constData(), static_cast<usize>( activeId.size() ) } ) ) {
                bool_t bComplete = CY_FALSE;
                pGui->nThemeChain = EditorTheme_BuildChain( &pGui->themeLibrary, pRoot, pGui->themeChain, std::size( pGui->themeChain ), &bComplete );
                break;
            }
        }
    }
    if ( pIdOut != nullptr ) { *pIdOut = id; }
    return editor_gui_status_t::OK;
}

usize EditorGui_LoadThemeFolder( editor_gui_t *pGui, const QString &folder )
{
    usize nLoaded = 0u;
    const QDir directory( folder );
    for ( const QString &name : directory.entryList( { QStringLiteral( "*.cytheme" ) }, QDir::Files, QDir::Name ) ) {
        QFile file( directory.filePath( name ) );
        if ( !file.open( QIODevice::ReadOnly ) || EditorGui_AddTheme( pGui, QString::fromUtf8( file.readAll() ) ) != editor_gui_status_t::OK ) {
            const QByteArray message = QStringLiteral( "Theme file skipped: %1" ).arg( file.fileName() ).toUtf8();
            Cy_LogWriteAt( log_level_t::Warning, log_channel_t::Gui, message.constData(), CY_SOURCE_LOCATION );
            continue;
        }
        ++nLoaded;
    }
    return nLoaded;
}

editor_gui_status_t EditorGui_RemoveTheme( editor_gui_t *pGui, QApplication *pApplication, string_view_t id )
{
    if ( pGui == nullptr || pApplication == nullptr || !pGui->bInitialized ) { return editor_gui_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < pGui->themes.nCount; ++i ) {
        const key_value_t *pRoot = SettingsDocument_Root( pGui->themes.pData[i] );
        if ( !StringView_Equals( EditorTheme_Header( pRoot ).id, id ) ) { continue; }
        // Leave the theme (or a theme built on it) before its document goes,
        // so the style never resolves from freed memory.
        bool bInChain = false;
        for ( usize iChain = 0u; iChain < pGui->nThemeChain; ++iChain ) { bInChain = bInChain || pGui->themeChain[iChain] == pRoot; }
        if ( bInChain ) {
            const editor_gui_status_t status = ApplyChainFrom( pGui, pApplication, SettingsDocument_Root( &pGui->builtinTheme ) );
            if ( status != editor_gui_status_t::OK ) { return status; }
        }
        SettingsDocument_Shutdown( pGui->themes.pData[i] );
        delete pGui->themes.pData[i];
        Vector_Erase( &pGui->themes, i );
        return RebuildLibrary( pGui ) ? editor_gui_status_t::OK : editor_gui_status_t::OUT_OF_MEMORY;
    }
    return editor_gui_status_t::RESOURCE_MISSING;
}

editor_gui_status_t EditorGui_SelectTheme( editor_gui_t *pGui, QApplication *pApplication, string_view_t id )
{
    if ( pGui == nullptr || pApplication == nullptr || !pGui->bInitialized ) { return editor_gui_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < pGui->themeLibrary.roots.nCount; ++i ) {
        const key_value_t *pRoot = pGui->themeLibrary.roots.pData[i];
        if ( StringView_Equals( EditorTheme_Header( pRoot ).id, id ) ) { return ApplyChainFrom( pGui, pApplication, pRoot ); }
    }
    return editor_gui_status_t::RESOURCE_MISSING;
}

editor_gui_status_t EditorGui_PreviewTheme( editor_gui_t *pGui, QApplication *pApplication, const key_value_t *pThemeRoot )
{
    if ( pGui == nullptr || pApplication == nullptr || pThemeRoot == nullptr || !pGui->bInitialized ) { return editor_gui_status_t::INVALID_ARGUMENT; }
    return ApplyChainFrom( pGui, pApplication, pThemeRoot );
}

string_view_t EditorGui_ActiveThemeId( const editor_gui_t *pGui ) noexcept
{
    if ( pGui == nullptr || pGui->nThemeChain == 0u ) { return {}; }
    return EditorTheme_Header( pGui->themeChain[0] ).id;
}

QStringList EditorGui_ThemeIds( const editor_gui_t *pGui )
{
    QStringList ids;
    for ( usize i = 0u; pGui != nullptr && i < pGui->themeLibrary.roots.nCount; ++i ) {
        const string_view_t id = EditorTheme_Header( pGui->themeLibrary.roots.pData[i] ).id;
        const QString text = QString::fromUtf8( id.pData, static_cast<qsizetype>( id.cchLength ) );
        if ( !ids.contains( text ) ) { ids.append( text ); }
    }
    return ids;
}

bool_t EditorGui_AddStyleListener( editor_gui_t *pGui, editor_style_listener_fn pfnChanged, void *pContext ) noexcept
{
    CY_ASSERT( pGui != nullptr && pfnChanged != nullptr );
    if ( pGui == nullptr || pfnChanged == nullptr || pGui->nStyleListeners >= EDITOR_GUI_MAX_STYLE_LISTENERS ) { return CY_FALSE; }
    pGui->styleListeners[pGui->nStyleListeners++] = editor_style_listener_t{ pfnChanged, pContext };
    return CY_TRUE;
}

void EditorGui_RemoveStyleListener( editor_gui_t *pGui, editor_style_listener_fn pfnChanged, void *pContext ) noexcept
{
    if ( pGui == nullptr ) { return; }
    for ( usize i = 0u; i < pGui->nStyleListeners; ++i ) {
        if ( pGui->styleListeners[i].pfnChanged != pfnChanged || pGui->styleListeners[i].pContext != pContext ) { continue; }
        for ( usize j = i + 1u; j < pGui->nStyleListeners; ++j ) { pGui->styleListeners[j - 1u] = pGui->styleListeners[j]; }
        --pGui->nStyleListeners;
        return;
    }
}

namespace
{

void NotifyKeymapListeners( editor_gui_t *pGui ) noexcept
{
    // A listener may unsubscribe while being told.
    editor_keymap_listener_t snapshot[EDITOR_GUI_MAX_STYLE_LISTENERS];
    const usize nListeners = pGui->nKeymapListeners;
    std::copy( pGui->keymapListeners, pGui->keymapListeners + nListeners, snapshot );
    for ( usize i = 0u; i < nListeners; ++i ) { snapshot[i].pfnChanged( snapshot[i].pContext ); }
}

editor_gui_status_t ApplyKeymapFrom( editor_gui_t *pGui, const key_value_t *pKeymapRoot ) noexcept
{
    bool_t bComplete = CY_FALSE;
    pGui->nKeymapChain = EditorKeymap_BuildChain( pGui->keymapLibrary.pData, pGui->keymapLibrary.nCount, pKeymapRoot, pGui->keymapChain,
                                                  std::size( pGui->keymapChain ), &bComplete );
    if ( !bComplete ) { CY_LOG_WRITE( Warning, Gui, "Keymap names a base that is missing or forms a cycle; the chain stops there" ); }
    NotifyKeymapListeners( pGui );
    return editor_gui_status_t::OK;
}

struct prepared_keymap_t {
    settings_document_t *pStore{ nullptr }; // Owned until publication.
    settings_document_t *pReplaced{ nullptr }; // Borrowed until publication.
    vector_t<settings_document_t *> stores{};
    vector_t<const key_value_t *> library{};
    const key_value_t *chain[EDITOR_KEYMAP_MAX_DEPTH]{};
    usize nChain{ 0u };
    bool bApply{ false };
    ~prepared_keymap_t() noexcept {
        if ( pStore != nullptr ) { SettingsDocument_Shutdown( pStore ); delete pStore; }
    }
};

editor_gui_status_t PrepareKeymap( editor_gui_t *pGui, const QString &text, prepared_keymap_t &prepared, bool bActivate )
{
    prepared.pStore = new ( std::nothrow ) settings_document_t{};
    if ( prepared.pStore == nullptr || SettingsDocument_Init( prepared.pStore, pGui->pAllocator, EditorKeymap_Identity() ) != settings_document_status_t::OK ) {
        return editor_gui_status_t::OUT_OF_MEMORY;
    }
    const QByteArray utf8 = text.toUtf8();
    const auto loaded = SettingsDocument_Load( prepared.pStore, string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) } );
    if ( loaded.status != settings_document_status_t::OK ) {
        return loaded.status == settings_document_status_t::OUT_OF_MEMORY ? editor_gui_status_t::OUT_OF_MEMORY : editor_gui_status_t::INVALID_ARGUMENT;
    }
    const auto header = EditorKeymap_Header( SettingsDocument_Root( prepared.pStore ) );
    if ( header.id.cchLength == 0u ) { return editor_gui_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &prepared.stores, pGui->pAllocator, pGui->keymaps.nCount + 1u ) ||
         !Vector_Init( &prepared.library, pGui->pAllocator, pGui->keymaps.nCount + 2u ) ) { return editor_gui_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < pGui->keymaps.nCount; ++i ) {
        auto *pStore = pGui->keymaps.pData[i];
        const auto *pRoot = SettingsDocument_Root( pStore );
        if ( StringView_Equals( EditorKeymap_Header( pRoot ).id, header.id ) ) {
            prepared.pReplaced = pStore;
            for ( usize j = 0u; j < pGui->nKeymapChain; ++j ) { prepared.bApply = prepared.bApply || pGui->keymapChain[j] == pRoot; }
        } else if ( !Vector_PushBack( &prepared.stores, pStore ) ) { return editor_gui_status_t::OUT_OF_MEMORY; }
    }
    if ( !Vector_PushBack( &prepared.stores, prepared.pStore ) ) { return editor_gui_status_t::OUT_OF_MEMORY; }
    for ( usize i = prepared.stores.nCount; i-- > 0u; ) {
        if ( !Vector_PushBack( &prepared.library, SettingsDocument_Root( prepared.stores.pData[i] ) ) ) { return editor_gui_status_t::OUT_OF_MEMORY; }
    }
    if ( !Vector_PushBack( &prepared.library, SettingsDocument_Root( &pGui->builtinKeymap ) ) ) { return editor_gui_status_t::OUT_OF_MEMORY; }
    prepared.bApply = prepared.bApply || bActivate;
    if ( prepared.bApply ) {
        const key_value_t *pActive = !bActivate && pGui->nKeymapChain > 0u ? pGui->keymapChain[0] : nullptr;
        const bool bRootReplaced = prepared.pReplaced != nullptr && pActive == SettingsDocument_Root( prepared.pReplaced );
        // A live preview root is borrowed and need not be in the library.
        // Replacing one of its bases must retain the actual draft, even if
        // that draft has the same ID as a different library document.
        if ( bActivate || bRootReplaced ) {
            const auto activeId = bActivate ? header.id : EditorGui_ActiveKeymapId( pGui );
            pActive = nullptr;
            for ( usize i = 0u; i < prepared.library.nCount; ++i ) {
                if ( StringView_Equals( EditorKeymap_Header( prepared.library.pData[i] ).id, activeId ) ) { pActive = prepared.library.pData[i]; break; }
            }
        }
        bool_t bComplete = CY_FALSE;
        prepared.nChain = EditorKeymap_BuildChain( prepared.library.pData, prepared.library.nCount, pActive, prepared.chain,
                                                 std::size( prepared.chain ), &bComplete );
        if ( !bComplete && bActivate ) { return editor_gui_status_t::INVALID_ARGUMENT; }
        if ( !bComplete ) { CY_LOG_WRITE( Warning, Gui, "Keymap names a base that is missing or forms a cycle; the chain stops there" ); }
    }
    return editor_gui_status_t::OK;
}

template <typename T> void SwapKeymapStorage( vector_t<T> &a, vector_t<T> &b ) noexcept
{
    std::swap( a.pData, b.pData ); std::swap( a.nCount, b.nCount );
    std::swap( a.nCapacity, b.nCapacity ); std::swap( a.pAllocator, b.pAllocator );
}

void PublishKeymap( editor_gui_t *pGui, prepared_keymap_t &prepared ) noexcept
{
    SwapKeymapStorage( pGui->keymaps, prepared.stores );
    SwapKeymapStorage( pGui->keymapLibrary, prepared.library );
    prepared.pStore = nullptr; // The application's owned table now holds it.
    if ( prepared.bApply ) {
        pGui->nKeymapChain = prepared.nChain;
        std::copy( prepared.chain, prepared.chain + prepared.nChain, pGui->keymapChain );
        NotifyKeymapListeners( pGui );
    }
    if ( prepared.pReplaced != nullptr ) { SettingsDocument_Shutdown( prepared.pReplaced ); delete prepared.pReplaced; prepared.pReplaced = nullptr; }
}

} // namespace

editor_gui_status_t EditorGui_AddKeymap( editor_gui_t *pGui, const QString &text, QString *pIdOut )
{
    if ( pGui == nullptr || !pGui->bInitialized ) { return editor_gui_status_t::INVALID_ARGUMENT; }
    prepared_keymap_t prepared;
    const auto status = PrepareKeymap( pGui, text, prepared, false );
    if ( status != editor_gui_status_t::OK ) { return status; }
    const auto header = EditorKeymap_Header( SettingsDocument_Root( prepared.pStore ) );
    const QString id = QString::fromUtf8( header.id.pData, static_cast<qsizetype>( header.id.cchLength ) );
    PublishKeymap( pGui, prepared );
    if ( pIdOut != nullptr ) { *pIdOut = id; }
    return editor_gui_status_t::OK;
}

editor_gui_status_t EditorGui_SaveKeymap( editor_gui_t *pGui, const QString &text, const QString &path, QString *pErrorOut )
{
    if ( pErrorOut != nullptr ) { pErrorOut->clear(); }
    if ( pGui == nullptr || !pGui->bInitialized || path.isEmpty() ) { return editor_gui_status_t::INVALID_ARGUMENT; }
    prepared_keymap_t prepared;
    const auto status = PrepareKeymap( pGui, text, prepared, true );
    if ( status != editor_gui_status_t::OK ) {
        if ( pErrorOut != nullptr ) { *pErrorOut = QString::fromLatin1( EditorGui_StatusName( status ) ); }
        return status;
    }
    // Never fall back to directly truncating the user's existing file.
    QSaveFile file( path ); file.setDirectWriteFallback( false );
    const QByteArray utf8 = text.toUtf8();
    if ( !file.open( QIODevice::WriteOnly ) || file.write( utf8 ) != utf8.size() || !file.commit() ) {
        if ( pErrorOut != nullptr ) { *pErrorOut = file.errorString(); }
        return editor_gui_status_t::IO_ERROR;
    }
    PublishKeymap( pGui, prepared );
    return editor_gui_status_t::OK;
}

usize EditorGui_LoadKeymapFolder( editor_gui_t *pGui, const QString &folder )
{
    usize nLoaded = 0u;
    const QDir directory( folder );
    for ( const QString &name : directory.entryList( { QStringLiteral( "*.cykeymap" ) }, QDir::Files, QDir::Name ) ) {
        QFile file( directory.filePath( name ) );
        if ( !file.open( QIODevice::ReadOnly ) || EditorGui_AddKeymap( pGui, QString::fromUtf8( file.readAll() ) ) != editor_gui_status_t::OK ) {
            const QByteArray message = QStringLiteral( "Keymap file skipped: %1" ).arg( file.fileName() ).toUtf8();
            Cy_LogWriteAt( log_level_t::Warning, log_channel_t::Gui, message.constData(), CY_SOURCE_LOCATION );
            continue;
        }
        ++nLoaded;
    }
    return nLoaded;
}

editor_gui_status_t EditorGui_SelectKeymap( editor_gui_t *pGui, string_view_t id )
{
    if ( pGui == nullptr || !pGui->bInitialized ) { return editor_gui_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < pGui->keymapLibrary.nCount; ++i ) {
        const key_value_t *pRoot = pGui->keymapLibrary.pData[i];
        if ( StringView_Equals( EditorKeymap_Header( pRoot ).id, id ) ) { return ApplyKeymapFrom( pGui, pRoot ); }
    }
    return editor_gui_status_t::RESOURCE_MISSING;
}

editor_gui_status_t EditorGui_PreviewKeymap( editor_gui_t *pGui, const key_value_t *pKeymapRoot )
{
    if ( pGui == nullptr || pKeymapRoot == nullptr || !pGui->bInitialized ) { return editor_gui_status_t::INVALID_ARGUMENT; }
    return ApplyKeymapFrom( pGui, pKeymapRoot );
}

string_view_t EditorGui_ActiveKeymapId( const editor_gui_t *pGui ) noexcept
{
    if ( pGui == nullptr || pGui->nKeymapChain == 0u ) { return {}; }
    return EditorKeymap_Header( pGui->keymapChain[0] ).id;
}

QStringList EditorGui_KeymapIds( const editor_gui_t *pGui )
{
    QStringList ids;
    for ( usize i = 0u; pGui != nullptr && i < pGui->keymapLibrary.nCount; ++i ) {
        const string_view_t id = EditorKeymap_Header( pGui->keymapLibrary.pData[i] ).id;
        const QString text = QString::fromUtf8( id.pData, static_cast<qsizetype>( id.cchLength ) );
        if ( !ids.contains( text ) ) { ids.append( text ); }
    }
    return ids;
}

bool_t EditorGui_AddKeymapListener( editor_gui_t *pGui, editor_keymap_listener_fn pfnChanged, void *pContext ) noexcept
{
    CY_ASSERT( pGui != nullptr && pfnChanged != nullptr );
    if ( pGui == nullptr || pfnChanged == nullptr || pGui->nKeymapListeners >= EDITOR_GUI_MAX_STYLE_LISTENERS ) { return CY_FALSE; }
    pGui->keymapListeners[pGui->nKeymapListeners++] = editor_keymap_listener_t{ pfnChanged, pContext };
    return CY_TRUE;
}

void EditorGui_RemoveKeymapListener( editor_gui_t *pGui, editor_keymap_listener_fn pfnChanged, void *pContext ) noexcept
{
    if ( pGui == nullptr ) { return; }
    for ( usize i = 0u; i < pGui->nKeymapListeners; ++i ) {
        if ( pGui->keymapListeners[i].pfnChanged != pfnChanged || pGui->keymapListeners[i].pContext != pContext ) { continue; }
        for ( usize j = i + 1u; j < pGui->nKeymapListeners; ++j ) { pGui->keymapListeners[j - 1u] = pGui->keymapListeners[j]; }
        --pGui->nKeymapListeners;
        return;
    }
}

const char *EditorGui_StatusName( editor_gui_status_t status ) noexcept
{
    switch ( status ) {
        case editor_gui_status_t::OK: return "OK";
        case editor_gui_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case editor_gui_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case editor_gui_status_t::RESOURCE_MISSING: return "RESOURCE_MISSING";
        case editor_gui_status_t::RESOURCE_INVALID: return "RESOURCE_INVALID";
        case editor_gui_status_t::STYLE_FAILED: return "STYLE_FAILED";
        case editor_gui_status_t::IO_ERROR: return "IO_ERROR";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor::gui
