//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_ThemeDraft.cpp
//  Purpose: Implements the shared theme working copy.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30 (extracted from the Theme Editor)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_ThemeDraft.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

#include <QDir>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

constexpr const char *kSkeleton = "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"draft\" name = \"Draft\" }\n";

string_view_t ViewOf( const QByteArray &bytes ) noexcept
{
    return string_view_t{ bytes.constData(), static_cast<usize>( bytes.size() ) };
}

QString FromView( string_view_t view )
{
    return QString::fromUtf8( view.pData != nullptr ? view.pData : "", static_cast<qsizetype>( view.cchLength ) );
}

bool NewThemeDocument( settings_document_t *pDocument, const allocator_t *pAllocator ) noexcept
{
    return SettingsDocument_Init( pDocument, pAllocator, EditorTheme_Identity() ) == settings_document_status_t::OK &&
           SettingsDocument_Load( pDocument, StringView_FromCString( kSkeleton ) ).status == settings_document_status_t::OK;
}

bool SameValue( const theme_token_t &token, const theme_value_t &a, const theme_value_t &b ) noexcept
{
    switch ( token.kind ) {
        case theme_token_kind_t::COLOR: return a.bFormula == b.bFormula && ( a.bFormula || a.rgba == b.rgba );
        case theme_token_kind_t::FONT:
            return StringView_Equals( a.font.family, b.font.family ) && a.font.flSize == b.font.flSize && a.font.nWeight == b.font.nWeight;
        case theme_token_kind_t::METRIC: return a.flMetric == b.flMetric;
        case theme_token_kind_t::CHOICE: return StringView_Equals( a.choice, b.choice );
    }
    return true;
}

void BuildOrder( editor_theme_draft_t *pDraft )
{
    pDraft->order.clear();
    usize nFramework = 0u;
    const theme_token_t *pFramework = EditorStyle_Tokens( &nFramework );
    for ( usize i = 0u; i < nFramework; ++i ) {
        if ( const theme_token_t *pToken = EditorThemeRegistry_Find( &pDraft->pGui->themeTokens, StringView_FromCString( pFramework[i].pId ) ) ) {
            pDraft->order.push_back( pToken );
        }
    }
    for ( usize i = 0u; i < pDraft->pGui->themeTokens.tokens.nCount; ++i ) {
        const theme_token_t *pToken = pDraft->pGui->themeTokens.tokens.pData[i];
        if ( std::find( pDraft->order.begin(), pDraft->order.end(), pToken ) == pDraft->order.end() ) { pDraft->order.push_back( pToken ); }
    }
}

// Complete copy of the active chain; only while the chain is a real theme.
void TakeSnapshot( editor_theme_draft_t *pDraft )
{
    editor_gui_t *pGui = pDraft->pGui;
    SettingsDocument_Shutdown( &pDraft->snapshot );
    const bool bOk = NewThemeDocument( &pDraft->snapshot, pGui->pAllocator ) &&
                     EditorTheme_WriteComplete( &pDraft->snapshot, &pGui->themeTokens, pDraft->order.data(), pDraft->order.size(), pGui->themeChain,
                                                pGui->nThemeChain ) == theme_status_t::OK &&
                     EditorTheme_KeepUnknownTokens( &pDraft->snapshot, &pGui->themeTokens, pGui->themeChain, pGui->nThemeChain ) == theme_status_t::OK;
    CY_ASSERT( bOk );
    ( void )bOk;
    pDraft->previousId = FromView( pGui->nThemeChain != 0u ? EditorTheme_Header( pGui->themeChain[0] ).id : string_view_t{} );
}

void ResetDraft( editor_theme_draft_t *pDraft )
{
    CY_ASSERT( !EditorThemeDraft_IsPreviewing( pDraft ) );
    SettingsDocument_Shutdown( &pDraft->draft );
    const key_value_t *pSnapshot = SettingsDocument_Root( &pDraft->snapshot );
    const bool bOk = NewThemeDocument( &pDraft->draft, pDraft->pGui->pAllocator ) &&
                     EditorTheme_WriteComplete( &pDraft->draft, &pDraft->pGui->themeTokens, pDraft->order.data(), pDraft->order.size(), &pSnapshot,
                                                1u ) == theme_status_t::OK &&
                     EditorTheme_KeepUnknownTokens( &pDraft->draft, &pDraft->pGui->themeTokens, &pSnapshot, 1u ) == theme_status_t::OK;
    CY_ASSERT( bOk );
    ( void )bOk;
}

} // namespace

editor_theme_draft_t::~editor_theme_draft_t() noexcept
{
    EditorThemeDraft_End( this );
}

bool EditorThemeDraft_Begin( editor_theme_draft_t *pDraft, editor_gui_t *pGui, QApplication *pApplication )
{
    CY_ASSERT( pDraft != nullptr && !pDraft->bActive );
    if ( pDraft == nullptr || pGui == nullptr || !pGui->bInitialized || pApplication == nullptr ) { return false; }
    pDraft->pGui = pGui;
    pDraft->pApplication = pApplication;
    pDraft->builtinId = FromView( EditorTheme_Header( SettingsDocument_Root( &pGui->builtinTheme ) ).id );
    BuildOrder( pDraft );
    TakeSnapshot( pDraft );
    ResetDraft( pDraft );
    pDraft->bActive = true;
    return true;
}

void EditorThemeDraft_End( editor_theme_draft_t *pDraft )
{
    if ( pDraft == nullptr || !pDraft->bActive ) { return; }
    EditorThemeDraft_RestorePrevious( pDraft );
    SettingsDocument_Shutdown( &pDraft->draft );
    SettingsDocument_Shutdown( &pDraft->snapshot );
    pDraft->bActive = false;
}

void EditorThemeDraft_Rebase( editor_theme_draft_t *pDraft )
{
    if ( pDraft == nullptr || !pDraft->bActive ) { return; }
    EditorThemeDraft_RestorePrevious( pDraft );
    BuildOrder( pDraft ); // Workspaces may have registered tokens since.
    TakeSnapshot( pDraft );
    ResetDraft( pDraft );
}

const theme_token_t *EditorThemeDraft_Token( const editor_theme_draft_t *pDraft, const QString &id )
{
    const QByteArray utf8 = id.toUtf8();
    return EditorThemeRegistry_Find( &pDraft->pGui->themeTokens, ViewOf( utf8 ) );
}

theme_value_t EditorThemeDraft_Value( const editor_theme_draft_t *pDraft, const theme_token_t &token, bool bSnapshot )
{
    const key_value_t *pRoot = SettingsDocument_Root( bSnapshot ? &pDraft->snapshot : &pDraft->draft );
    theme_value_t value{};
    theme_resolution_t resolution{};
    switch ( token.kind ) {
        case theme_token_kind_t::COLOR:
            ( void )EditorTheme_ResolveColor( &pRoot, 1u, token, &resolution );
            value.bFormula = resolution.iSource == CY_INVALID_SIZE && token.derive.op != theme_derive_op_t::NONE;
            value.rgba = EditorTheme_ResolveColorIn( &pDraft->pGui->themeTokens, &pRoot, 1u, token, nullptr );
            break;
        case theme_token_kind_t::FONT: value.font = EditorTheme_ResolveFont( &pRoot, 1u, token, nullptr ); break;
        case theme_token_kind_t::METRIC: value.flMetric = EditorTheme_ResolveMetric( &pRoot, 1u, token, nullptr ); break;
        case theme_token_kind_t::CHOICE: value.choice = EditorTheme_ResolveChoice( &pRoot, 1u, token, nullptr ); break;
    }
    return value;
}

bool EditorThemeDraft_IsChanged( const editor_theme_draft_t *pDraft, const theme_token_t &token )
{
    return !SameValue( token, EditorThemeDraft_Value( pDraft, token ), EditorThemeDraft_Value( pDraft, token, true ) );
}

bool EditorThemeDraft_HasChanges( const editor_theme_draft_t *pDraft )
{
    if ( pDraft == nullptr || !pDraft->bActive ) { return false; }
    for ( const theme_token_t *pToken : pDraft->order ) {
        if ( EditorThemeDraft_IsChanged( pDraft, *pToken ) ) { return true; }
    }
    return false;
}

void EditorThemeDraft_Write( editor_theme_draft_t *pDraft, const theme_token_t &token, const theme_value_t &value, bool bPreview )
{
    CY_ASSERT( pDraft != nullptr && pDraft->bActive );
    // The "inherited" argument of each setter is one no real value equals,
    // so the value is always written (EditorTheme_WriteComplete's device).
    theme_status_t status = theme_status_t::OK;
    settings_document_t *pTheme = &pDraft->draft;
    switch ( token.kind ) {
        case theme_token_kind_t::COLOR:
            status = value.bFormula ? EditorTheme_SetColorAuto( pTheme, token ) : EditorTheme_SetColor( pTheme, token, value.rgba, ~value.rgba );
            break;
        case theme_token_kind_t::FONT: status = EditorTheme_SetFont( pTheme, token, value.font, theme_font_t{ {}, 0.0, 0u } ); break;
        case theme_token_kind_t::METRIC: status = EditorTheme_SetMetric( pTheme, token, value.flMetric, std::nan( "" ) ); break;
        case theme_token_kind_t::CHOICE: status = EditorTheme_SetChoice( pTheme, token, value.choice, string_view_t{} ); break;
    }
    if ( status != theme_status_t::OK ) { CY_LOG_WRITE( Warning, Gui, "Theme value was rejected by its token" ); }
    if ( bPreview ) { EditorThemeDraft_Preview( pDraft ); }
}

void EditorThemeDraft_SetColor( editor_theme_draft_t *pDraft, const theme_token_t &token, const QColor &color, bool bPreview )
{
    if ( token.kind != theme_token_kind_t::COLOR || !color.isValid() ) { return; }
    theme_value_t value{};
    value.rgba = ( static_cast<u32>( color.red() ) << 24u ) | ( static_cast<u32>( color.green() ) << 16u ) | ( static_cast<u32>( color.blue() ) << 8u ) |
                 static_cast<u32>( color.alpha() );
    EditorThemeDraft_Write( pDraft, token, value, bPreview );
}

void EditorThemeDraft_SetFormula( editor_theme_draft_t *pDraft, const theme_token_t &token )
{
    // "auto" on a token without a formula would silently mean its default.
    if ( token.kind != theme_token_kind_t::COLOR || token.derive.op == theme_derive_op_t::NONE ) { return; }
    theme_value_t value{};
    value.bFormula = true;
    EditorThemeDraft_Write( pDraft, token, value );
}

void EditorThemeDraft_RevertToken( editor_theme_draft_t *pDraft, const theme_token_t &token )
{
    EditorThemeDraft_Write( pDraft, token, EditorThemeDraft_Value( pDraft, token, true ) );
}

void EditorThemeDraft_RevertAll( editor_theme_draft_t *pDraft )
{
    if ( pDraft == nullptr || !pDraft->bActive ) { return; }
    // The real theme goes back first so the style stops referring to the
    // draft before its storage is replaced.
    EditorThemeDraft_RestorePrevious( pDraft );
    ResetDraft( pDraft );
}

void EditorThemeDraft_Preview( editor_theme_draft_t *pDraft )
{
    if ( EditorGui_PreviewTheme( pDraft->pGui, pDraft->pApplication, SettingsDocument_Root( &pDraft->draft ) ) != editor_gui_status_t::OK ) {
        CY_LOG_WRITE( Warning, Gui, "Theme preview could not be applied" );
    }
}

bool EditorThemeDraft_IsPreviewing( const editor_theme_draft_t *pDraft )
{
    return pDraft != nullptr && pDraft->pGui != nullptr && pDraft->pGui->nThemeChain != 0u && SettingsDocument_IsInitialized( &pDraft->draft ) &&
           pDraft->pGui->themeChain[0] == SettingsDocument_Root( &pDraft->draft );
}

void EditorThemeDraft_RestorePrevious( editor_theme_draft_t *pDraft )
{
    if ( !EditorThemeDraft_IsPreviewing( pDraft ) ) { return; }
    const QByteArray previous = pDraft->previousId.toUtf8();
    if ( EditorGui_SelectTheme( pDraft->pGui, pDraft->pApplication, ViewOf( previous ) ) != editor_gui_status_t::OK ) {
        // The previous theme is gone; the built-in theme is always there.
        const QByteArray builtin = pDraft->builtinId.toUtf8();
        const editor_gui_status_t fallback = EditorGui_SelectTheme( pDraft->pGui, pDraft->pApplication, ViewOf( builtin ) );
        CY_ASSERT( fallback == editor_gui_status_t::OK );
        ( void )fallback;
    }
}

QString EditorThemeDraft_Text( const editor_theme_draft_t *pDraft, const QString &id, const QString &name, const QString &author,
                               const QString &description )
{
    const QByteArray idUtf8 = id.toUtf8();
    const QByteArray nameUtf8 = name.toUtf8();
    const QByteArray authorUtf8 = author.trimmed().toUtf8();
    const QByteArray descriptionUtf8 = description.trimmed().toUtf8();
    editor_gui_t *pGui = pDraft->pGui;
    const key_value_t *pDraftRoot = SettingsDocument_Root( &pDraft->draft );
    // The same rule the style uses to pick light or dark icons.
    const theme_token_t *pBackground = EditorThemeRegistry_Find( &pGui->themeTokens, StringView_FromCString( "ui.background" ) );
    const bool bLight = pBackground != nullptr &&
                        EditorThemeDraft_ToColor( EditorTheme_ResolveColorIn( &pGui->themeTokens, &pDraftRoot, 1u, *pBackground, nullptr ) ).lightnessF() >= 0.5;
    settings_document_t complete{};
    text_buffer_t text{};
    const bool bOk = NewThemeDocument( &complete, pGui->pAllocator ) &&
                     EditorTheme_SetHeader( &complete, ViewOf( idUtf8 ), ViewOf( nameUtf8 ), string_view_t{} ) == theme_status_t::OK &&
                     EditorTheme_SetDetails( &complete, ViewOf( authorUtf8 ), ViewOf( descriptionUtf8 ), bLight ) == theme_status_t::OK &&
                     EditorTheme_WriteComplete( &complete, &pGui->themeTokens, pDraft->order.data(), pDraft->order.size(), &pDraftRoot, 1u ) ==
                         theme_status_t::OK &&
                     EditorTheme_KeepUnknownTokens( &complete, &pGui->themeTokens, &pDraftRoot, 1u ) == theme_status_t::OK &&
                     TextBuffer_Init( &text, pGui->pAllocator ) && SettingsDocument_Write( &complete, &text ) == settings_document_status_t::OK;
    const QString contents = bOk ? QString::fromUtf8( TextBuffer_CStr( &text ), static_cast<qsizetype>( TextBuffer_Length( &text ) ) ) : QString();
    TextBuffer_Shutdown( &text );
    SettingsDocument_Shutdown( &complete );
    return contents;
}

QString EditorThemeDraft_Save( editor_theme_draft_t *pDraft, const QString &id, const QString &name, const QString &author, const QString &description,
                               const QString &folder, QString *pErrorOut )
{
    const auto fail = [pErrorOut]( const QString &reason ) {
        if ( pErrorOut != nullptr ) { *pErrorOut = reason; }
        const QByteArray utf8 = reason.toUtf8();
        Cy_LogWriteAt( log_level_t::Warning, log_channel_t::Gui, utf8.constData(), CY_SOURCE_LOCATION );
        return QString();
    };
    if ( pDraft == nullptr || !pDraft->bActive ) { return fail( QStringLiteral( "No theme is being edited." ) ); }
    if ( id == pDraft->builtinId ) { return fail( QStringLiteral( "The built-in theme cannot be replaced; choose another ID." ) ); }
    const QString contents = EditorThemeDraft_Text( pDraft, id, name, author, description );
    if ( contents.isEmpty() ) {
        return fail( QStringLiteral( "The theme could not be written. The ID takes lower-case letters, digits, and underscores; the name must not be empty." ) );
    }
    if ( folder.isEmpty() || !QDir().mkpath( folder ) ) { return fail( QStringLiteral( "The theme folder could not be created: %1" ).arg( folder ) ); }
    const QString path = QDir( folder ).filePath( id + QStringLiteral( ".cytheme" ) );
    QSaveFile file( path ); // Atomic: a failed save leaves the previous file intact.
    if ( !file.open( QIODevice::WriteOnly ) || file.write( contents.toUtf8() ) < 0 || !file.commit() ) {
        return fail( QStringLiteral( "The theme file could not be saved: %1" ).arg( path ) );
    }
    const QByteArray idUtf8 = id.toUtf8();
    if ( EditorGui_AddTheme( pDraft->pGui, contents ) != editor_gui_status_t::OK ||
         EditorGui_SelectTheme( pDraft->pGui, pDraft->pApplication, ViewOf( idUtf8 ) ) != editor_gui_status_t::OK ) {
        return fail( QStringLiteral( "The theme was saved but could not be loaded back." ) );
    }
    // The saved theme is what the next session starts with.
    if ( const setting_descriptor_t *pSetting = EditorSettings_Find( &pDraft->pGui->settings, StringView_FromCString( "editor.ui.theme" ) ) ) {
        setting_value_t value{};
        value.type = setting_type_t::STRING;
        value.text = ViewOf( idUtf8 );
        const settings_registry_status_t written = EditorSettings_Write( &pDraft->pGui->settings, settings_scope_t::USER, *pSetting, value );
        if ( written != settings_registry_status_t::OK && written != settings_registry_status_t::NO_SCOPE ) {
            CY_LOG_WRITE( Warning, Gui, "Saved theme could not be recorded as the active theme setting" );
        }
    }
    TakeSnapshot( pDraft );
    ResetDraft( pDraft );
    return path;
}

QColor EditorThemeDraft_ToColor( u32 rgba )
{
    return QColor( static_cast<int>( ( rgba >> 24u ) & 0xFFu ), static_cast<int>( ( rgba >> 16u ) & 0xFFu ), static_cast<int>( ( rgba >> 8u ) & 0xFFu ),
                   static_cast<int>( rgba & 0xFFu ) );
}

QString EditorThemeDraft_ColorText( u32 rgba )
{
    char buffer[10]{};
    return QString::fromLatin1( buffer, static_cast<qsizetype>( SettingColor_Format( rgba, buffer ) ) );
}

QString EditorThemeDraft_FormulaText( const theme_derive_t &derive )
{
    const QString a = QString::fromUtf8( derive.pA != nullptr ? derive.pA : "" );
    const QString b = QString::fromUtf8( derive.pB != nullptr ? derive.pB : "" );
    switch ( derive.op ) {
        case theme_derive_op_t::NONE: return {};
        case theme_derive_op_t::COPY: return a;
        case theme_derive_op_t::MIX: return QStringLiteral( "mix(%1, %2, %3)" ).arg( a, b ).arg( derive.flAmount );
        case theme_derive_op_t::LIGHTER: return QStringLiteral( "lighter(%1, %2%)" ).arg( a ).arg( derive.flAmount );
        case theme_derive_op_t::DARKER: return QStringLiteral( "darker(%1, %2%)" ).arg( a ).arg( derive.flAmount );
        case theme_derive_op_t::MIX_STATUS: return QStringLiteral( "status(%1, hue %2, %3)" ).arg( a ).arg( derive.flHue ).arg( derive.flAmount );
    }
    return {};
}

} // namespace cypher::editor::gui
