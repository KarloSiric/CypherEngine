//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMason_MainWindow.cpp
//  Purpose: Implements Mason's main window and application commands.
//  Details: Build order matters: every command is registered before any
//           action exists, so the keymap sees the whole catalogue; panels
//           are registered before the layout is applied, so the layout can
//           place them. Teardown runs the other way: the window (and with it
//           every panel that listens to the workspace or registered console
//           commands) goes before the workspace and the framework it uses.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMason_MainWindow.h"

#include "CypherMason_RecentFiles.h"
#include "CypherMason_Welcome.h"
#include "CypherMason_WorkspaceState.h"
#include "CypherMason_Branding.h"

#include "CypherMapGui_Check.h"
#include "CypherMapGui_Dialogs.h"
#include "CypherMapGui_Panels.h"
#include "CypherMapGui_ToolPanels.h"
#include "CypherMapGui_Views.h"

#include "CypherEditorGui_Actions.h"
#include "CypherEditorGui_AppearancePage.h"
#include "CypherEditorGui_AssetBrowser.h"
#include "CypherEditorGui_AssetWindow.h"
#include "CypherEditorGui_DatabaseView.h"
#include "CypherEditorGui_CommandHistory.h"
#include "CypherEditorGui_CommandPalette.h"
#include "CypherEditorGui_Console.h"
#include "CypherEditorGui_Docking.h"
#include "CypherEditorGui_History.h"
#include "CypherEditorGui_LogPanels.h"
#include "CypherEditorGui_SettingsDialog.h"
#include "CypherEditorGui_KeymapSettings.h"
#include "CypherEditorGui_ShortcutsDialog.h"
#include "CypherEditorGui_ThemeEditor.h"

#include "CypherEditor_SettingsFile.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

#include "DockWidget.h"
#include "DockManager.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QDir>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>

#include <cmath>
#include <new>

namespace cypher::mason
{

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;
using namespace cypher::editor::map;

class mason_window_t;

struct mason_t {
    mason_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( mason_t );
    ~mason_t() noexcept = default;

    QApplication *pApplication{ nullptr };
    u32 flags{ MASON_FLAG_NONE };
    editor_gui_t gui{};
    map_workspace_t map{};
    editor_actions_t actions{};
    editor_docking_t docking{};
    layout_t defaultLayout{};
    mason_window_t *pWindow{ nullptr }; // Owned; deleted first in Mason_Destroy.
    QLabel *pStatusTool{ nullptr };
    QLabel *pStatusSelection{ nullptr };
    QLabel *pStatusSize{ nullptr };      // Hammer's "64w 96l 32h @(x y z)" for the selection.
    QLabel *pStatusCursor{ nullptr };
    QLabel *pStatusGrid{ nullptr };
    QComboBox *pGridCombo{ nullptr };    // Main toolbar: the TileEditor's "Grid" chooser.
    QComboBox *pAngleCombo{ nullptr };   // Status bar: Hammer's angle snap.
    QComboBox *pScaleCombo{ nullptr };   // Scale step is independent of angle/grid snapping.
    QToolButton *pStatusWarnings{ nullptr }; // Status bar: warning count; opens Problems.
    QToolButton *pStatusErrors{ nullptr };   // Status bar: error count; opens Problems.
    QWidget *pPalette{ nullptr };       // Command palette; owned by the window.
    QDialog *pSettingsDialog{ nullptr }; // Created on first use; owned by the window.
    QDialog *pThemeEditor{ nullptr };    // Created on first use; owned by the window.
    QWidget *pAssets{ nullptr };         // The asset browser panel: created at start (it owns the scan and thumbnails); owned by its dock once shown.
    QDialog *pAssetWindow{ nullptr };    // Hammer's pop-up Asset Browser; created on first use; owned by the window.
    QDialog *pDatabase{ nullptr };       // Sandbox's Database View; created on first use; owned by the window.
    QStringList usedAssets{};            // What the open map names, for Used in Map.
    QWidget *pCommandHistory{ nullptr }; // Every command run; owned by its dock.
    QDialog *pShortcutsDialog{ nullptr };  // Created on first use; owned by the window.
    QDialog *pMapInfo{ nullptr };          // Created on first use; owned by the window.
    mason_recent_t recent{};               // Recent maps; <config>/Mason/recent_maps.txt, or the scratch folder when headless.
    QDialog *pGoTo{ nullptr };             // Created on first use; owned by the window.
    QDialog *pTransform{ nullptr };        // Numeric transforms; modeless and selection-aware.
    QDialog *pCheck{ nullptr };            // Check for Problems; created on first use; owned by the window.
    QWidget *pHistory{ nullptr };        // Bound to the workspace's stable history member.
    QString workspaceStatePath{};       // Local UI state, separate from map content and preferences.
    QString themeFolder{};               // <config>/Mason/themes, or the scratch folder when headless.
    QString keymapFolder{};              // User .cykeymap files; isolated in headless runs.
    QTemporaryDir headlessScratch{ QDir::temp().filePath( QStringLiteral( "mason-headless-XXXXXX" ) ) }; // Removed with Mason.
    settings_file_t userSettings{};     // <config>/Mason/editor.cysettings; in memory only when headless.
    bool bUserSettingsOnDisk{ false };
    QTimer *pSettingsSaveTimer{ nullptr };
    QString lastDirectory{};
};

namespace
{

bool IsHeadless( const mason_t &mason ) noexcept
{
    return ( mason.flags & MASON_FLAG_HEADLESS ) != 0u;
}

void LogText( log_level_t level, const QString &text ) noexcept
{
    const QByteArray utf8 = text.toUtf8();
    Cy_LogWriteAt( level, log_channel_t::Editor, utf8.constData(), CY_SOURCE_LOCATION );
}

// Errors reach the user three ways: the console keeps the history, the
// status bar says it where they are looking, and a box makes sure a failed
// save is never missed.
void ReportError( mason_t &mason, const QString &title, const QString &text );

// Asks what to do with unsaved changes. True when it is fine to proceed.
bool ConfirmDiscard( mason_t &mason );

void ApplyDefaultLayout( mason_t &mason );
void RestoreStartupLayout( mason_t &mason );
bool SaveWorkspaceState( mason_t &mason );
void UpdateAssetRoots( mason_t &mason );
void OnAssetCatalogRescanned( void *pContext );
void OnAssetActivated( void *pContext, const QString &path, editor_asset_kind_t kind );
QDialog *AssetWindow( mason_t &mason );
QDialog *DatabaseView( mason_t &mason );

} // namespace

class mason_window_t final : public QMainWindow {
public:
    explicit mason_window_t( mason_t *pMason ) : m_pMason( pMason ) {}

    void InstallToolCancelRouting()
    {
        for ( const char *name : { "EditorToolPalette", "masonEditingTools", "masonSelectModes" } ) {
            if ( auto *controls = findChild<QWidget *>( QString::fromLatin1( name ) ) ) {
                for ( auto *button : controls->findChildren<QToolButton *>() ) { button->installEventFilter( this ); }
            }
        }
    }

protected:
    bool eventFilter( QObject *watched, QEvent *event ) override
    {
        // Only explicitly registered map tool buttons use this route. Escape
        // in fields, other docks, popups and dialogs keeps native semantics.
        auto *button = qobject_cast<QToolButton *>( watched );
        if ( button != nullptr && QApplication::focusWidget() == button &&
             MapViews_HandleToolCancel( m_pMason->docking.pCentral, event ) ) { return true; }
        return QMainWindow::eventFilter( watched, event );
    }

    // Layout fractions become pixel sizes, so the layout applied while the
    // window was being built (at Qt's default size) is applied again once
    // the window has its real size. Queued so the platform's first resize
    // has landed.
    void showEvent( QShowEvent *pEvent ) override
    {
        QMainWindow::showEvent( pEvent );
        if ( m_bShown ) { return; }
        m_bShown = true;
        QTimer::singleShot( 0, this, [this]() { RestoreStartupLayout( *m_pMason ); } );
    }

    void closeEvent( QCloseEvent *pEvent ) override
    {
        if ( ConfirmDiscard( *m_pMason ) ) {
            ( void )SaveWorkspaceState( *m_pMason );
            pEvent->accept();
        } else {
            pEvent->ignore();
        }
    }

private:
    mason_t *m_pMason{ nullptr };
    bool m_bShown{ false };
};

namespace
{

void ReportError( mason_t &mason, const QString &title, const QString &text )
{
    LogText( log_level_t::Error, title + QStringLiteral( ": " ) + text );
    if ( mason.pWindow != nullptr ) { mason.pWindow->statusBar()->showMessage( title, 8000 ); }
    if ( !IsHeadless( mason ) && mason.pWindow != nullptr ) { QMessageBox::warning( mason.pWindow, title, text ); }
}

QString DescribeFailure( const map_files_result_t &result )
{
    switch ( result.status ) {
        case map_files_status_t::OK: return QString();
        case map_files_status_t::INVALID_ARGUMENT: return QStringLiteral( "The path must end in .cymap." );
        case map_files_status_t::ROOT_MISSING: return QStringLiteral( "There is no map at that path." );
        case map_files_status_t::READ_FAILED: return QStringLiteral( "The map's files could not be read. The console lists which." );
        case map_files_status_t::WRITE_FAILED:
            return QStringLiteral( "The map could not be written. Files on disk are unchanged unless the console says otherwise." );
        case map_files_status_t::DOCUMENT_FAILED: break;
    }
    switch ( result.documentStatus ) {
        case map_status_t::READ_ONLY:
            return QStringLiteral( "This map has duplicate IDs and is read-only. The console lists them; fix them by hand or reopen "
                                   "with Reassign Duplicate IDs." );
        case map_status_t::SAVE_BLOCKED:
            return QStringLiteral( "Saving would overwrite a damaged chunk file, or two files disagree. The console lists the problem "
                                   "chunks; repair or move them first." );
        case map_status_t::LIMIT_EXCEEDED:
            return QStringLiteral( "A chunk would exceed the format's size limit. Use smaller cells for dense layers." );
        case map_status_t::ROOT_UNREADABLE:
            return QStringLiteral( "The file is not a Cypher map, or was written by a newer version." );
        case map_status_t::ROOT_INVALID: return QStringLiteral( "The map file is missing required members. The console lists them." );
        case map_status_t::OUT_OF_MEMORY: return QStringLiteral( "Out of memory." );
        default: break;
    }
    return QStringLiteral( "The map document reported %1." ).arg( QString::fromUtf8( MapDocument_StatusName( result.documentStatus ) ) );
}

QString EnsureMapSuffix( const QString &path )
{
    return path.endsWith( QStringLiteral( ".cymap" ) ) ? path : path + QStringLiteral( ".cymap" );
}

bool SaveTo( mason_t &mason, const QString &path )
{
    const map_files_result_t result = MapWorkspace_SaveAs( &mason.map, path );
    if ( result.status != map_files_status_t::OK ) {
        ReportError( mason, QStringLiteral( "Map not saved" ), DescribeFailure( result ) );
        return false;
    }
    mason.lastDirectory = QFileInfo( path ).absolutePath();
    ( void )MasonRecent_Add( &mason.recent, path );
    mason.pWindow->statusBar()->showMessage( QStringLiteral( "Saved %1" ).arg( MapWorkspace_DisplayName( &mason.map ) ), 4000 );
    return true;
}

// The path for Save As: from the console argument, or a dialog.
QString AskSavePath( mason_t &mason, const command_args_t &args )
{
    if ( args.nArgs == 1u ) { return QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) ); }
    if ( IsHeadless( mason ) ) { return QString(); }
    const QString start = mason.map.path.isEmpty() ? mason.lastDirectory : mason.map.path;
    return QFileDialog::getSaveFileName( mason.pWindow, QStringLiteral( "Save Map As" ), start, QStringLiteral( "Cypher maps (*.cymap)" ) );
}

bool SaveCurrent( mason_t &mason )
{
    if ( !mason.map.path.isEmpty() ) { return SaveTo( mason, mason.map.path ); }
    const QString path = AskSavePath( mason, command_args_t{} );
    return !path.isEmpty() && SaveTo( mason, EnsureMapSuffix( path ) );
}

bool ConfirmDiscard( mason_t &mason )
{
    if ( !MapWorkspace_IsModified( &mason.map ) ) { return true; }
    // Headless: never throw work away without a person saying so.
    if ( IsHeadless( mason ) ) { return false; }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        mason.pWindow, QStringLiteral( "Unsaved Changes" ),
        QStringLiteral( "Save changes to %1?" ).arg( MapWorkspace_DisplayName( &mason.map ) ),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
    if ( answer == QMessageBox::Save ) { return SaveCurrent( mason ); }
    return answer == QMessageBox::Discard;
}

// ---------------------------------------------------------------------------
// Application commands
// ---------------------------------------------------------------------------

mason_t &Mason( void *pContext ) noexcept
{
    CY_ASSERT( pContext != nullptr );
    return *static_cast<mason_t *>( pContext );
}

void OpenRecent( mason_t &mason, const QString &path )
{
    if ( !ConfirmDiscard( mason ) ) { return; }
    ( void )Mason_OpenMap( &mason, path );
}

// File > Open Recent, rebuilt each time it opens so it never goes stale.
void FillRecentMenu( mason_t &mason, QMenu *pMenu )
{
    pMenu->clear();
    MasonRecent_SetMax( &mason.recent, static_cast<int>( EditorSettings_Integer( &mason.gui.settings, "editor.general.recent_files", 16 ) ) );
    int index = 0;
    for ( const QString &file : mason.recent.files ) {
        const QFileInfo info( file );
        // &1 .. &9: Alt+F, R, then a digit opens it, as in most editors.
        const QString prefix = ++index <= 9 ? QStringLiteral( "&%1  " ).arg( index ) : QStringLiteral( "    " );
        QAction *pAction = pMenu->addAction( QStringLiteral( "%1%2   %3" ).arg( prefix, info.completeBaseName(), QDir::toNativeSeparators( info.absolutePath() ) ) );
        pAction->setToolTip( QDir::toNativeSeparators( file ) );
        pAction->setEnabled( info.exists() );
        QObject::connect( pAction, &QAction::triggered, mason.pWindow, [pMason = &mason, file]() { OpenRecent( *pMason, file ); } );
    }
    if ( mason.recent.files.isEmpty() ) { pMenu->addAction( QStringLiteral( "No recent maps" ) )->setEnabled( false ); }
    pMenu->addSeparator();
    QAction *pClear = pMenu->addAction( QStringLiteral( "Clear Recent Maps" ) );
    pClear->setEnabled( !mason.recent.files.isEmpty() );
    QObject::connect( pClear, &QAction::triggered, mason.pWindow, [pMason = &mason]() { ( void )MasonRecent_Clear( &pMason->recent ); } );
}

void AddRecentMenu( mason_t &mason )
{
    for ( QAction *pTop : mason.pWindow->menuBar()->actions() ) {
        QMenu *pFile = pTop->menu();
        if ( pFile == nullptr || pFile->title() != QStringLiteral( "&File" ) ) { continue; }
        const QList<QAction *> actions = pFile->actions();
        for ( qsizetype i = 0; i < actions.size(); ++i ) {
            if ( actions[i]->objectName() != QStringLiteral( "file.open" ) ) { continue; }
            auto *pRecent = new QMenu( QStringLiteral( "Open &Recent" ), pFile );
            pRecent->setObjectName( QStringLiteral( "masonRecentMenu" ) );
            pFile->insertMenu( i + 1 < actions.size() ? actions[i + 1] : nullptr, pRecent );
            QObject::connect( pRecent, &QMenu::aboutToShow, mason.pWindow, [pMason = &mason, pRecent]() { FillRecentMenu( *pMason, pRecent ); } );
            FillRecentMenu( mason, pRecent );
            return;
        }
    }
}

command_result_t ShowWelcome( void *pContext, const command_args_t & ) noexcept
{
    return Mason_ShowWelcome( &Mason( pContext ) ) ? command_result_t::OK : command_result_t::DISABLED;
}

command_result_t FileNew( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( !ConfirmDiscard( mason ) ) { return command_result_t::OK; }
    return MapWorkspace_New( &mason.map ) == map_status_t::OK ? command_result_t::OK : command_result_t::FAILED;
}

command_result_t FileOpen( void *pContext, const command_args_t &args ) noexcept
{
    mason_t &mason = Mason( pContext );
    QString path;
    if ( args.nArgs == 1u ) {
        path = QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) );
    } else if ( args.nArgs == 0u && !IsHeadless( mason ) ) {
        path = QFileDialog::getOpenFileName( mason.pWindow, QStringLiteral( "Open Map" ), mason.lastDirectory,
                                             QStringLiteral( "Cypher maps (*.cymap)" ) );
    } else {
        return command_result_t::INVALID_ARGUMENTS;
    }
    if ( path.isEmpty() ) { return command_result_t::OK; } // Cancelled.
    if ( !ConfirmDiscard( mason ) ) { return command_result_t::OK; }
    return Mason_OpenMap( &mason, path ) ? command_result_t::OK : command_result_t::FAILED;
}

command_result_t FileSave( void *pContext, const command_args_t & ) noexcept
{
    return SaveCurrent( Mason( pContext ) ) ? command_result_t::OK : command_result_t::FAILED;
}

command_result_t FileSaveAs( void *pContext, const command_args_t &args ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( args.nArgs > 1u ) { return command_result_t::INVALID_ARGUMENTS; }
    const QString path = AskSavePath( mason, args );
    if ( path.isEmpty() ) { return args.nArgs == 1u ? command_result_t::INVALID_ARGUMENTS : command_result_t::OK; }
    return SaveTo( mason, EnsureMapSuffix( path ) ) ? command_result_t::OK : command_result_t::FAILED;
}

command_result_t FileExit( void *pContext, const command_args_t & ) noexcept
{
    Mason( pContext ).pWindow->close();
    return command_result_t::OK;
}

u32 CanSelectWholeRoots( void *pContext ) noexcept
{
    const auto mode = Mason( pContext ).map.elementMode;
    return mode == map_element_mode_t::OBJECTS || mode == map_element_mode_t::GROUPS || mode == map_element_mode_t::MESHES ?
        COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

command_result_t SelectAll( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( CanSelectWholeRoots( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const map_wireframe_t &wire = mason.map.wire;
    vector_t<u64> ids{};
    if ( !Vector_Init( &ids, mason.gui.pAllocator, wire.objects.nCount + wire.entities.nCount ) ) { return command_result_t::FAILED; }
    for ( usize i = 0u; i < wire.objects.nCount; ++i ) {
        if ( MapWorkspace_IsSelectableObject( &mason.map, wire.objects.pData[i] ) ) { ( void )Vector_PushBack( &ids, wire.objects.pData[i].id ); }
    }
    // Originless entities remain whole objects, but cannot enter Meshes.
    if ( mason.map.elementMode != map_element_mode_t::MESHES ) {
        for ( usize i = 0u; i < wire.entities.nCount; ++i ) {
            if ( MapWireframe_FindObject( wire, wire.entities.pData[i].id ) == nullptr ) { ( void )Vector_PushBack( &ids, wire.entities.pData[i].id ); }
        }
    }
    MapWorkspace_SetSelection( &mason.map, ids.pData, ids.nCount );
    return command_result_t::OK;
}

command_result_t SelectNone( void *pContext, const command_args_t & ) noexcept
{
    MapWorkspace_Select( &Mason( pContext ).map, 0u, MAP_SELECT_REPLACE );
    return command_result_t::OK;
}

u32 HasSelection( void *pContext ) noexcept
{
    return EditorSelection_Count( &Mason( pContext ).map.selection ) != 0u ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

u32 CanEditGeometry( void *pContext ) noexcept
{
    return MapWorkspace_CanEditSelection( &Mason( pContext ).map ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

u32 CanMoveObjects( void *pContext ) noexcept
{
    return MapWorkspace_CanMoveSelection( &Mason( pContext ).map ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

command_result_t DeleteSelection( void *pContext, const command_args_t & ) noexcept
{
    return MapWorkspace_DeleteSelection( &Mason( pContext ).map ) ? command_result_t::OK : command_result_t::FAILED;
}

command_result_t DuplicateSelection( void *pContext, const command_args_t & ) noexcept
{
    return MapWorkspace_DuplicateSelection( &Mason( pContext ).map ) ? command_result_t::OK : command_result_t::FAILED;
}
enum class clipboard_action_t { COPY, CUT, PASTE };
QWidget *TextClipboardFocus( const mason_t &mason ) noexcept
{
    auto *focus = QApplication::focusWidget();
    if ( focus == nullptr || mason.pWindow == nullptr || !mason.pWindow->isAncestorOf( focus ) ) { return nullptr; }
    if ( qobject_cast<QLineEdit *>( focus ) != nullptr || qobject_cast<QPlainTextEdit *>( focus ) != nullptr || qobject_cast<QTextEdit *>( focus ) != nullptr ) { return focus; }
    return nullptr;
}
bool TextClipboardEnabled( QWidget *focus, clipboard_action_t action ) noexcept
{
    bool selection = false, readOnly = false;
    if ( auto *line = qobject_cast<QLineEdit *>( focus ) ) { selection = line->hasSelectedText(); readOnly = line->isReadOnly(); }
    else if ( auto *plain = qobject_cast<QPlainTextEdit *>( focus ) ) { selection = plain->textCursor().hasSelection(); readOnly = plain->isReadOnly(); }
    else if ( auto *rich = qobject_cast<QTextEdit *>( focus ) ) { selection = rich->textCursor().hasSelection(); readOnly = rich->isReadOnly(); }
    else { return false; }
    if ( action == clipboard_action_t::COPY ) { return selection; }
    if ( action == clipboard_action_t::CUT ) { return selection && !readOnly; }
    const auto *clipboard = QApplication::clipboard();
    const auto *mime = clipboard != nullptr ? clipboard->mimeData() : nullptr;
    return !readOnly && mime != nullptr && mime->hasText();
}
template <clipboard_action_t action>
u32 ClipboardState( void *context ) noexcept
{
    auto &mason = Mason( context );
    if ( auto *focus = TextClipboardFocus( mason ) ) { return TextClipboardEnabled( focus, action ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE; }
    bool enabled = false;
    if constexpr ( action == clipboard_action_t::COPY ) { enabled = MapWorkspace_CanCopySelection( &mason.map ); }
    else if constexpr ( action == clipboard_action_t::CUT ) { enabled = MapWorkspace_CanCutSelection( &mason.map ); }
    else { enabled = MapWorkspace_CanPaste( &mason.map ); }
    return enabled ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
template <clipboard_action_t action>
command_result_t ClipboardCommand( void *context, const command_args_t & ) noexcept
{
    auto &mason = Mason( context );
    if ( auto *focus = TextClipboardFocus( mason ) ) {
        if ( !TextClipboardEnabled( focus, action ) ) { return command_result_t::DISABLED; }
        const auto apply = []( auto *editor ) {
            if constexpr ( action == clipboard_action_t::COPY ) { editor->copy(); }
            else if constexpr ( action == clipboard_action_t::CUT ) { editor->cut(); }
            else { editor->paste(); }
        };
        if ( auto *line = qobject_cast<QLineEdit *>( focus ) ) { apply( line ); }
        else if ( auto *plain = qobject_cast<QPlainTextEdit *>( focus ) ) { apply( plain ); }
        else if ( auto *rich = qobject_cast<QTextEdit *>( focus ) ) { apply( rich ); }
        return command_result_t::OK;
    }
    bool done = false;
    if constexpr ( action == clipboard_action_t::COPY ) { done = MapWorkspace_CopySelection( &mason.map ); }
    else if constexpr ( action == clipboard_action_t::CUT ) { done = MapWorkspace_CutSelection( &mason.map ); }
    else { done = MapWorkspace_Paste( &mason.map ); }
    return done ? command_result_t::OK : command_result_t::FAILED;
}
u32 PasteInPlaceState( void *context ) noexcept
{
    return TextClipboardFocus( Mason( context ) ) == nullptr && MapWorkspace_CanPaste( &Mason( context ).map ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
command_result_t PasteInPlace( void *context, const command_args_t & ) noexcept
{
    return MapWorkspace_Paste( &Mason( context ).map, true ) ? command_result_t::OK : command_result_t::FAILED;
}

command_result_t TransformSelection( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.pTransform == nullptr ) { mason.pTransform = MapTransformDialog_Create( mason.pWindow, &mason.map ); }
    if ( mason.pTransform == nullptr ) { return command_result_t::FAILED; }
    mason.pTransform->show();
    mason.pTransform->raise();
    mason.pTransform->activateWindow();
    return command_result_t::OK;
}

command_result_t Undo( void *pContext, const command_args_t & ) noexcept
{
    const editor_history_status_t status = MapWorkspace_Undo( &Mason( pContext ).map );
    return status == editor_history_status_t::OK ? command_result_t::OK : command_result_t::FAILED;
}

command_result_t Redo( void *pContext, const command_args_t & ) noexcept
{
    const editor_history_status_t status = MapWorkspace_Redo( &Mason( pContext ).map );
    return status == editor_history_status_t::OK ? command_result_t::OK : command_result_t::FAILED;
}

u32 CanUndo( void *pContext ) noexcept
{
    return EditorHistory_CanUndo( &Mason( pContext ).map.history ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

u32 CanRedo( void *pContext ) noexcept
{
    return EditorHistory_CanRedo( &Mason( pContext ).map.history ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

// Unwired operations remain honest disabled catalogue entries.
command_result_t NotYetAvailable( void *, const command_args_t & ) noexcept { return command_result_t::DISABLED; }
u32 NeverEnabled( void * ) noexcept { return COMMAND_STATE_NONE; }

template <const char *const *kPanel>
command_result_t TogglePanel( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    const QString id = QString::fromUtf8( *kPanel );
    EditorDocking_SetPanelVisible( &mason.docking, id, !EditorDocking_IsPanelVisible( &mason.docking, id ) );
    return command_result_t::OK;
}

template <const char *const *kPanel>
u32 PanelState( void *pContext ) noexcept
{
    const bool bVisible = EditorDocking_IsPanelVisible( &Mason( pContext ).docking, QString::fromUtf8( *kPanel ) );
    return COMMAND_STATE_ENABLED | ( bVisible ? COMMAND_STATE_CHECKED : 0u );
}

constexpr const char *kConsolePanel = MASON_PANEL_CONSOLE;
constexpr const char *kOutputPanel = MASON_PANEL_OUTPUT;
constexpr const char *kProblemsPanel = MASON_PANEL_PROBLEMS;
constexpr const char *kOutlinerPanel = MAP_PANEL_OUTLINER;
constexpr const char *kPropertiesPanel = MAP_PANEL_PROPERTIES;
constexpr const char *kHistoryPanel = MASON_PANEL_HISTORY;
constexpr const char *kCommandHistoryPanel = MASON_PANEL_COMMAND_HISTORY;
constexpr const char *kVisibilityPanel = MAP_PANEL_VISGROUPS;
constexpr const char *kSelectionSetsPanel = MAP_PANEL_SELECTION_SETS;
constexpr const char *kToolPropertiesPanel = MAP_PANEL_TOOL_PROPERTIES;
constexpr const char *kActiveMaterialPanel = MAP_PANEL_ACTIVE_MATERIAL;

command_result_t OpenProperties( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    EditorDocking_SetPanelVisible( &mason.docking, QString::fromLatin1( kPropertiesPanel ), true );
    if ( auto *pDock = EditorDocking_Dock( &mason.docking, QString::fromLatin1( kPropertiesPanel ) ) ) {
        pDock->setAsCurrentTab();
        pDock->raise();
        if ( pDock->widget() != nullptr ) { pDock->widget()->setFocus( Qt::OtherFocusReason ); }
    }
    return command_result_t::OK;
}

// Keep earlier saved workspaces from restoring the former third row. Hidden
// toolbars stay hidden; custom CYLAYOUT placement remains available in Settings.
void ApplyToolbarRows( mason_t &mason )
{
    if ( !EditorSettings_Bool( &mason.gui.settings, "editor.ui.two_toolbar_rows", CY_TRUE ) ) { return; }
    constexpr const char *names[]{ "masonMainToolBar", "masonViewFilters", "masonUtilityTools", "masonSelectModes", "masonEditingTools", "masonGeometryTools" };
    QToolBar *bars[6]{};
    bool hidden[6]{};
    for ( int i = 0; i < 6; ++i ) {
        bars[i] = mason.pWindow->findChild<QToolBar *>( QString::fromLatin1( names[i] ) );
        if ( bars[i] == nullptr ) { return; }
        hidden[i] = bars[i]->isHidden();
    }
    for ( QToolBar *bar : bars ) {
        mason.pWindow->removeToolBarBreak( bar );
        mason.pWindow->removeToolBar( bar );
    }
    for ( int i = 0; i < 6; ++i ) {
        if ( i == 3 ) { mason.pWindow->addToolBarBreak( Qt::TopToolBarArea ); }
        mason.pWindow->addToolBar( Qt::TopToolBarArea, bars[i] );
        bars[i]->setVisible( !hidden[i] );
    }
}

// The layout's toolbar and status bar members (CYLAYOUT.md 3). Toolbars
// carry their layout ID as a property, so a layout names them without
// knowing Qt object names.
void ApplyChrome( mason_t &mason, const layout_t &layout )
{
    mason.pWindow->statusBar()->setVisible( layout.bStatusBarVisible );
    // A toolbar on a higher row than the one before it in the same area
    // starts a new row there.
    u32 lastRow[4]{};
    bool bAreaUsed[4]{};
    for ( usize i = 0u; i < Vector_Count( &layout.toolbars ); ++i ) {
        const layout_toolbar_t &placement = layout.toolbars.pData[i];
        const string_view_t id = EditorLayout_Text( &layout, placement.id );
        const QString toolbarId = QString::fromUtf8( id.pData, static_cast<qsizetype>( id.cchLength ) );
        for ( QToolBar *pToolBar : mason.pWindow->findChildren<QToolBar *>() ) {
            if ( pToolBar->property( "cypherToolbarId" ).toString() != toolbarId ) { continue; }
            constexpr Qt::ToolBarArea kAreas[]{ Qt::LeftToolBarArea, Qt::RightToolBarArea, Qt::TopToolBarArea, Qt::BottomToolBarArea };
            if ( placement.area != layout_area_t::FLOATING ) {
                const usize area = static_cast<usize>( placement.area );
                if ( bAreaUsed[area] && placement.row > lastRow[area] ) { mason.pWindow->addToolBarBreak( kAreas[area] ); }
                mason.pWindow->removeToolBarBreak( pToolBar );
                mason.pWindow->addToolBar( kAreas[area], pToolBar );
                bAreaUsed[area] = true;
                lastRow[area] = placement.row;
            }
            if ( placement.iconSize > 0.0 ) {
                const int edge = static_cast<int>( placement.iconSize );
                pToolBar->setIconSize( QSize( edge, edge ) );
            }
            pToolBar->setVisible( placement.bVisible );
        }
    }
}

void ApplyDefaultLayout( mason_t &mason )
{
    const usize nSkipped = EditorDocking_ApplyLayout( &mason.docking, mason.defaultLayout );
    if ( nSkipped != 0u ) { CY_LOG_WRITE( Warning, Editor, "The default layout names panels Mason does not have" ); }
    ApplyChrome( mason, mason.defaultLayout );
    ApplyToolbarRows( mason );
    // Dock placement alone does not restore panes that were closed,
    // maximized, or switched to another projection.
    if ( mason.docking.pCentral != nullptr && mason.defaultLayout.views.bPresent ) {
        QWidget *pViews = mason.docking.pCentral;
        MapViews_ShowAllPanes( pViews );
        const layout_views_t &views = mason.defaultLayout.views;
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT && static_cast<u32>( i ) < views.nPanes; ++i ) {
            map_view_type_t type = map_view_type_t::TOP;
            switch ( views.panes[i].view ) {
                case layout_view_kind_t::PERSPECTIVE: type = map_view_type_t::CAMERA; break;
                case layout_view_kind_t::FRONT: type = map_view_type_t::FRONT; break;
                case layout_view_kind_t::SIDE: type = map_view_type_t::SIDE; break;
                default: break; // The default layout uses the four supported projections.
            }
            MapViews_SetPaneType( pViews, i, type );
        }
        // One view arrangement per layout-file arrangement (CYLAYOUT.md 3).
        constexpr map_view_arrangement_t kArrangements[]{
            map_view_arrangement_t::PERSPECTIVE,          map_view_arrangement_t::TWO,                  map_view_arrangement_t::TWO_ROWS,
            map_view_arrangement_t::FOUR,                 map_view_arrangement_t::ONE_LEFT_TWO_RIGHT,   map_view_arrangement_t::TWO_LEFT_ONE_RIGHT,
            map_view_arrangement_t::HAMMER,               map_view_arrangement_t::TWO_TOP_ONE_BOTTOM,   map_view_arrangement_t::THREE_COLUMNS,
            map_view_arrangement_t::THREE_ROWS,           map_view_arrangement_t::ONE_LEFT_THREE_RIGHT, map_view_arrangement_t::THREE_LEFT_ONE_RIGHT,
            map_view_arrangement_t::ONE_TOP_THREE_BOTTOM, map_view_arrangement_t::THREE_TOP_ONE_BOTTOM, map_view_arrangement_t::FOUR_COLUMNS,
            map_view_arrangement_t::FOUR_ROWS,
        };
        const usize iArrangement = static_cast<usize>( views.arrangement );
        MapViews_SetArrangement( pViews, iArrangement < std::size( kArrangements ) ? kArrangements[iArrangement] : map_view_arrangement_t::FOUR );
        MapViews_SetMaximized( pViews, views.maximized );
    }
    if ( mason.defaultLayout.nInvalidMembers != 0u ) { CY_LOG_WRITE( Warning, Editor, "The default layout has members that fell back to defaults" ); }
    ( void )MasonWorkspace_ApplyPanelPolicy( mason.docking.pManager, true );
}

bool SaveWorkspaceState( mason_t &mason )
{
    const auto result = MasonWorkspace_Save( mason.pWindow, mason.docking.pManager, mason.docking.pCentral, mason.workspaceStatePath );
    if ( result != workspace_state_result_t::OK ) {
        LogText( log_level_t::Warning, QStringLiteral( "Workspace layout could not be saved to %1" ).arg( mason.workspaceStatePath ) );
    }
    return result == workspace_state_result_t::OK;
}

void RestoreStartupLayout( mason_t &mason )
{
    ApplyDefaultLayout( mason );
    const auto result = MasonWorkspace_Restore( mason.pWindow, mason.docking.pManager, mason.docking.pCentral, mason.workspaceStatePath );
    if ( result != workspace_state_result_t::OK && result != workspace_state_result_t::NOT_FOUND ) {
        LogText( log_level_t::Warning, QStringLiteral( "Saved workspace could not be restored; using the default layout." ) );
    }
    ( void )MasonWorkspace_ApplyPanelPolicy( mason.docking.pManager, true );
    ApplyToolbarRows( mason );
    EditorActions_RefreshStates( &mason.actions );
    // Start keyboard interaction in the restored active pane. Qt otherwise
    // may choose a toolbar field and turn editing keys into asset searches.
    if ( mason.docking.pCentral != nullptr ) {
        MapViews_SetActivePane( mason.docking.pCentral, MapViews_ActivePane( mason.docking.pCentral ) );
    }
}

command_result_t SaveLayout( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( !SaveWorkspaceState( mason ) ) { return command_result_t::FAILED; }
    mason.pWindow->statusBar()->showMessage( QStringLiteral( "Workspace layout saved" ), 4000 );
    return command_result_t::OK;
}

command_result_t RestoreLayout( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    const auto result = MasonWorkspace_Restore( mason.pWindow, mason.docking.pManager, mason.docking.pCentral, mason.workspaceStatePath );
    if ( result != workspace_state_result_t::OK ) {
        LogText( log_level_t::Warning, result == workspace_state_result_t::NOT_FOUND ? QStringLiteral( "No workspace layout has been saved." ) :
                 QStringLiteral( "Saved workspace could not be restored. Current layout retained." ) );
        return command_result_t::FAILED;
    }
    ( void )MasonWorkspace_ApplyPanelPolicy( mason.docking.pManager, false );
    ApplyToolbarRows( mason );
    return command_result_t::OK;
}

command_result_t ResetLayout( void *pContext, const command_args_t & ) noexcept
{
    ApplyDefaultLayout( Mason( pContext ) );
    return command_result_t::OK;
}

command_result_t ToggleFullscreen( void *pContext, const command_args_t & ) noexcept
{
    auto *window = Mason( pContext ).pWindow;
    if ( window == nullptr ) { return command_result_t::DISABLED; }
    // Preserve the underlying normal/maximized state for the return trip.
    window->setWindowState( window->windowState() ^ Qt::WindowFullScreen );
    return command_result_t::OK;
}

u32 FullscreenState( void *pContext ) noexcept
{
    const auto *window = Mason( pContext ).pWindow;
    if ( window == nullptr ) { return COMMAND_STATE_NONE; }
    return COMMAND_STATE_ENABLED | ( window->isFullScreen() ? COMMAND_STATE_CHECKED : 0u );
}

template <map_view_arrangement_t arrangement>
command_result_t ViewArrangement( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.docking.pCentral == nullptr ) { return command_result_t::DISABLED; }
    MapViews_SetArrangement( mason.docking.pCentral, arrangement );
    return command_result_t::OK;
}

template <map_view_arrangement_t arrangement>
u32 ViewArrangementState( void *pContext ) noexcept
{
    QWidget *pViews = Mason( pContext ).docking.pCentral;
    if ( pViews == nullptr ) { return COMMAND_STATE_NONE; }
    return COMMAND_STATE_ENABLED | ( MapViews_Arrangement( pViews ) == arrangement ? COMMAND_STATE_CHECKED : 0u );
}

// Created once and kept, so the Appearance page's draft and the dialog's
// page and search survive closing it.
bool CanImportSettings( void *context, settings_scope_t scope, QString *error )
{
    const auto &mason = *static_cast<mason_t *>( context );
    if ( scope != settings_scope_t::USER || mason.gui.settings.scopes[static_cast<usize>( scope )] != &mason.userSettings.store ) {
        if ( error != nullptr ) { *error = QStringLiteral( "This editor session can persist imported settings to User scope. Project and Workspace storage is not attached." ); }
        return false;
    }
    if ( mason.userSettings.bSaveBlocked || ( !IsHeadless( mason ) && !mason.bUserSettingsOnDisk ) ) {
        if ( error != nullptr ) { *error = QStringLiteral( "The User settings file could not be loaded safely. Resolve that file before importing settings." ); }
        return false;
    }
    return true;
}

bool SaveImportedSettings( void *context, settings_scope_t scope, const settings_document_t &candidate, QString *error )
{
    auto &mason = *static_cast<mason_t *>( context );
    if ( !CanImportSettings( context, scope, error ) ) { return false; }
    if ( IsHeadless( mason ) ) { return true; } // The isolated test session intentionally uses an in-memory User store.
    // Keep the registry's borrowed store untouched until both serialization
    // and the existing backup/atomic file policy have succeeded.
    settings_file_t staged{};
    text_buffer_t text{};
    if ( EditorSettingsFile_Init( &staged, candidate.pAllocator, candidate.identity, TextBuffer_View( &mason.userSettings.path ) ) != settings_file_status_t::OK ||
         !TextBuffer_Init( &text, candidate.pAllocator ) || SettingsDocument_Write( &candidate, &text ) != settings_document_status_t::OK ||
         SettingsDocument_Load( &staged.store, TextBuffer_View( &text ) ).status != settings_document_status_t::OK ) {
        if ( error != nullptr ) { *error = QStringLiteral( "The imported settings could not be prepared for saving. Current settings are unchanged." ); }
        return false;
    }
    const auto status = EditorSettingsFile_Save( &staged );
    if ( status != settings_file_status_t::OK ) {
        if ( error != nullptr ) { *error = QStringLiteral( "Imported settings were not saved: %1. Current settings are unchanged." ).arg( QString::fromUtf8( EditorSettingsFile_StatusName( status ) ) ); }
        return false;
    }
    return true;
}

QDialog *SettingsDialog( mason_t &mason )
{
    if ( mason.pSettingsDialog == nullptr ) {
        mason.pSettingsDialog = EditorSettingsDialog_Create( mason.pWindow, &mason.gui.settings, &mason.gui.style );
        EditorSettingsDialog_SetTransferHooks( mason.pSettingsDialog, { &mason, &CanImportSettings, &SaveImportedSettings } );
        if ( QWidget *pAppearance = EditorAppearancePage_Create( nullptr, &mason.gui, mason.pApplication, mason.themeFolder ) ) {
            EditorSettingsDialog_AddPage( mason.pSettingsDialog, QString::fromLatin1( EDITOR_APPEARANCE_PAGE ), pAppearance,
                                          EditorAppearancePage_Keywords(), &EditorAppearancePage_CanClose );
        }
        if ( QWidget *pKeybindings = EditorKeymapSettings_Create( nullptr, &mason.gui, mason.keymapFolder ) ) {
            EditorKeymapSettings_SetReferenceCallback( pKeybindings, []( void *context ) {
                auto &owner = *static_cast<mason_t *>( context );
                ( void )EditorCommands_Execute( &owner.gui.commands, StringView_FromCString( "help.shortcuts" ), {} );
            }, &mason );
            EditorSettingsDialog_AddPage( mason.pSettingsDialog, QString::fromLatin1( EDITOR_KEYBINDINGS_PAGE ), pKeybindings,
                                          EditorKeymapSettings_Keywords(), &EditorKeymapSettings_CanClose );
        }
    }
    return mason.pSettingsDialog;
}

command_result_t OpenSettings( void *pContext, const command_args_t & ) noexcept
{
    QDialog *pDialog = SettingsDialog( Mason( pContext ) );
    pDialog->show();
    pDialog->raise();
    return command_result_t::OK;
}

command_result_t OpenKeybindings( void *pContext, const command_args_t & ) noexcept
{
    QDialog *pDialog = SettingsDialog( Mason( pContext ) );
    EditorSettingsDialog_ShowPage( pDialog, QString::fromLatin1( EDITOR_KEYBINDINGS_PAGE ) );
    pDialog->show(); pDialog->raise();
    return command_result_t::OK;
}

// Active Material's Browse button and the Texture menu: the browser on its
// Materials tab, ready to type.
// Hammer's view keys act on the active viewport: F2 Top, F3 Front, F4 Side,
// Ctrl+Alt+4 Perspective, Ctrl+Space cycles the 2D projections.
template <map_view_type_t type>
command_result_t SetActiveView( void *pContext, const command_args_t & ) noexcept
{
    QWidget *pViews = Mason( pContext ).docking.pCentral;
    if ( pViews == nullptr ) { return command_result_t::DISABLED; }
    MapViews_SetPaneType( pViews, MapViews_ActivePane( pViews ), type );
    return command_result_t::OK;
}

template <map_view_type_t type>
u32 ActiveViewState( void *pContext ) noexcept
{
    QWidget *pViews = Mason( pContext ).docking.pCentral;
    if ( pViews == nullptr ) { return COMMAND_STATE_NONE; }
    return COMMAND_STATE_ENABLED | ( MapViews_PaneType( pViews, MapViews_ActivePane( pViews ) ) == type ? COMMAND_STATE_CHECKED : 0u );
}

// The active pane becomes a camera in its selected CPU preview mode.
// Fullbright follows Hammer's F5; Shaded is a fixed directional preview,
// and Normals is Mason's diagnostic mode, not renderer All Lighting/ToolsVis.
template <map_render_mode_t mode>
command_result_t SetActiveRender( void *pContext, const command_args_t & ) noexcept
{
    QWidget *pViews = Mason( pContext ).docking.pCentral;
    if ( pViews == nullptr ) { return command_result_t::DISABLED; }
    MapViews_SetPaneView( pViews, MapViews_ActivePane( pViews ), map_view_type_t::CAMERA, mode );
    return command_result_t::OK;
}

template <map_render_mode_t mode>
u32 ActiveRenderState( void *pContext ) noexcept
{
    QWidget *pViews = Mason( pContext ).docking.pCentral;
    if ( pViews == nullptr ) { return COMMAND_STATE_NONE; }
    const int iPane = MapViews_ActivePane( pViews );
    const bool bOn = MapViews_PaneType( pViews, iPane ) == map_view_type_t::CAMERA && MapViews_PaneRenderMode( pViews, iPane ) == mode;
    return COMMAND_STATE_ENABLED | ( bOn ? COMMAND_STATE_CHECKED : 0u );
}

template <bool bEdges>
command_result_t ToggleActiveOverlay( void *pContext, const command_args_t & ) noexcept
{
    QWidget *pViews = Mason( pContext ).docking.pCentral;
    if ( pViews == nullptr ) { return command_result_t::DISABLED; }
    const int iPane = MapViews_ActivePane( pViews );
    if ( bEdges ) { MapViews_SetPaneMeshEdges( pViews, iPane, !MapViews_PaneMeshEdges( pViews, iPane ) ); }
    else { MapViews_SetPaneWireOverlay( pViews, iPane, !MapViews_PaneWireOverlay( pViews, iPane ) ); }
    return command_result_t::OK;
}

template <bool bEdges>
u32 ActiveOverlayState( void *pContext ) noexcept
{
    QWidget *pViews = Mason( pContext ).docking.pCentral;
    if ( pViews == nullptr ) { return COMMAND_STATE_NONE; }
    const int iPane = MapViews_ActivePane( pViews );
    const bool bOn = bEdges ? MapViews_PaneMeshEdges( pViews, iPane ) : MapViews_PaneWireOverlay( pViews, iPane );
    return COMMAND_STATE_ENABLED | ( bOn ? COMMAND_STATE_CHECKED : 0u );
}

command_result_t CycleActive2D( void *pContext, const command_args_t & ) noexcept
{
    QWidget *pViews = Mason( pContext ).docking.pCentral;
    if ( pViews == nullptr ) { return command_result_t::DISABLED; }
    const int iPane = MapViews_ActivePane( pViews );
    const map_view_type_t current = MapViews_PaneType( pViews, iPane );
    const map_view_type_t next = current == map_view_type_t::TOP ? map_view_type_t::FRONT
                                 : current == map_view_type_t::FRONT ? map_view_type_t::SIDE
                                                                     : map_view_type_t::TOP; // Side and 3D go to Top.
    MapViews_SetPaneType( pViews, iPane, next );
    return command_result_t::OK;
}

command_result_t CycleActivePane( void *pContext, const command_args_t & ) noexcept
{
    QWidget *views = Mason( pContext ).docking.pCentral;
    if ( views == nullptr ) { return command_result_t::DISABLED; }
    ( void )MapViews_CycleActivePane( views ); // One visible pane is a valid no-op.
    return command_result_t::OK;
}

u32 NudgeSelectionState( void *pContext ) noexcept
{
    return MapViews_CanNudgeSelection( Mason( pContext ).docking.pCentral ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

template <int horizontal, int vertical, bool fine>
command_result_t NudgeSelection( void *pContext, const command_args_t & ) noexcept
{
    QWidget *views = Mason( pContext ).docking.pCentral;
    if ( !MapViews_CanNudgeSelection( views ) ) { return command_result_t::DISABLED; }
    return MapViews_NudgeSelection( views, horizontal, vertical, fine ) ? command_result_t::OK : command_result_t::FAILED;
}

command_result_t MaximizeActive( void *pContext, const command_args_t & ) noexcept
{
    QWidget *pViews = Mason( pContext ).docking.pCentral;
    if ( pViews == nullptr ) { return command_result_t::DISABLED; }
    const int iPane = MapViews_ActivePane( pViews );
    MapViews_SetMaximized( pViews, MapViews_MaximizedPane( pViews ) == iPane ? -1 : iPane );
    return command_result_t::OK;
}

u32 MaximizeActiveState( void *pContext ) noexcept
{
    QWidget *pViews = Mason( pContext ).docking.pCentral;
    if ( pViews == nullptr ) { return COMMAND_STATE_NONE; }
    return COMMAND_STATE_ENABLED | ( MapViews_MaximizedPane( pViews ) >= 0 ? COMMAND_STATE_CHECKED : 0u );
}

// Hammer's Shift+G: the selected Command History entries, or the last
// repeatable command when the panel has no selection.
command_result_t RepeatCommand( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    return mason.pCommandHistory != nullptr ? EditorCommandHistory_Repeat( mason.pCommandHistory ) : command_result_t::DISABLED;
}

// Active Material's Browse (Hammer): the Asset Browser window on Materials
// with the current material selected; Accept makes the choice active.
command_result_t BrowseMaterials( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    QDialog *pWindow = AssetWindow( mason );
    if ( pWindow == nullptr ) { return command_result_t::FAILED; }
    const string_view_t current = EditorSettings_Text( &mason.gui.settings, "editor.map.default_material", string_view_t{} );
    EditorAssetWindow_Pick( pWindow, editor_asset_kind_t::MATERIAL,
                            QString::fromUtf8( current.pData != nullptr ? current.pData : "", static_cast<qsizetype>( current.cchLength ) ), &OnAssetActivated, &mason );
    return command_result_t::OK;
}

// "assets.database [path | #id]": the Database View, on an asset's tab or
// an entity. Object Properties' and Active Material's folder buttons run it.
command_result_t OpenDatabase( void *pContext, const command_args_t &args ) noexcept
{
    mason_t &mason = Mason( pContext );
    QDialog *pView = DatabaseView( mason );
    if ( pView == nullptr ) { return command_result_t::FAILED; }
    pView->show();
    pView->raise();
    pView->activateWindow();
    if ( args.nArgs == 0u || args.pArgs[0].cchLength == 0u ) { return command_result_t::OK; }
    const QString target = QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) );
    if ( target.startsWith( QLatin1Char( '#' ) ) ) {
        bool bOk = false;
        const u64 id = target.mid( 1 ).toULongLong( &bOk );
        return bOk && EditorDatabaseView_OpenEntity( pView, id ) ? command_result_t::OK : command_result_t::INVALID_ARGUMENTS;
    }
    if ( !EditorDatabaseView_Open( pView, target ) ) {
        LogText( log_level_t::Warning, QStringLiteral( "The Database View cannot open %1" ).arg( target ) );
        return command_result_t::INVALID_ARGUMENTS;
    }
    return command_result_t::OK;
}

command_result_t OpenAssetBrowser( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    QDialog *pWindow = AssetWindow( mason );
    if ( pWindow == nullptr ) { return command_result_t::FAILED; }
    pWindow->show();
    pWindow->raise();
    pWindow->activateWindow();
    return command_result_t::OK;
}

command_result_t RefreshAssets( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.pAssets == nullptr ) { return command_result_t::FAILED; }
    UpdateAssetRoots( mason );
    EditorAssetBrowser_Rescan( mason.pAssets );
    return command_result_t::OK;
}

command_result_t OpenAppearance( void *pContext, const command_args_t & ) noexcept
{
    QDialog *pDialog = SettingsDialog( Mason( pContext ) );
    EditorSettingsDialog_ShowPage( pDialog, QString::fromLatin1( EDITOR_APPEARANCE_PAGE ) );
    pDialog->show();
    pDialog->raise();
    return command_result_t::OK;
}

command_result_t OpenThemeEditor( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.themeFolder.isEmpty() ) {
        LogText( log_level_t::Warning, QStringLiteral( "The theme editor has no folder to save themes in" ) );
        return command_result_t::FAILED;
    }
    // A fresh editor each time: it snapshots the theme active when it opens.
    if ( mason.pThemeEditor != nullptr && !mason.pThemeEditor->isVisible() ) {
        delete mason.pThemeEditor;
        mason.pThemeEditor = nullptr;
    }
    if ( mason.pThemeEditor == nullptr ) {
        mason.pThemeEditor = EditorThemeEditor_Create( mason.pWindow, &mason.gui, mason.pApplication, mason.themeFolder );
        if ( mason.pThemeEditor == nullptr ) { return command_result_t::FAILED; }
    }
    mason.pThemeEditor->show();
    mason.pThemeEditor->raise();
    return command_result_t::OK;
}

command_result_t OpenPalette( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.pPalette == nullptr ) { return command_result_t::FAILED; }
    EditorCommandPalette_Open( mason.pPalette );
    return command_result_t::OK;
}

command_result_t FocusAssetSearch( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    auto *search = mason.pWindow->findChild<QLineEdit *>( QStringLiteral( "MasonFastAssetSearch" ) );
    if ( search == nullptr ) { return command_result_t::FAILED; }
    if ( auto *bar = mason.pWindow->findChild<QToolBar *>( QStringLiteral( "masonUtilityTools" ) ) ) { bar->show(); }
    search->setFocus( Qt::ShortcutFocusReason );
    search->selectAll();
    return command_result_t::OK;
}

u32 SoundPreviewState( void *pContext ) noexcept
{
    return COMMAND_STATE_ENABLED | ( EditorSettings_Bool( &Mason( pContext ).gui.settings,
        "editor.audio.preview_enabled", CY_TRUE ) ? COMMAND_STATE_CHECKED : 0u );
}

command_result_t ToggleSoundPreview( void *pContext, const command_args_t & ) noexcept
{
    auto &settings = Mason( pContext ).gui.settings;
    const auto *descriptor = EditorSettings_Find( &settings, StringView_FromCString( "editor.audio.preview_enabled" ) );
    if ( descriptor == nullptr ) { return command_result_t::FAILED; }
    setting_value_t value{};
    value.type = setting_type_t::BOOL;
    value.bValue = !EditorSettings_Bool( &settings, "editor.audio.preview_enabled", CY_TRUE );
    return EditorSettings_Write( &settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK
        ? command_result_t::OK : command_result_t::FAILED;
}

// Check for Problems asks the asset browser's catalogue whether an asset
// path exists; without a browser (or content folders) the check is skipped.
bool AssetExists( void *pContext, const QString &path )
{
    mason_t &mason = Mason( pContext );
    if ( mason.pAssets == nullptr ) { return true; }
    const editor_asset_catalog_t *pCatalog = EditorAssetBrowser_Catalog( mason.pAssets );
    if ( pCatalog == nullptr || EditorAssets_Count( pCatalog ) == 0u ) { return true; } // No content folders: nothing to compare against.
    const QByteArray utf8 = path.toUtf8();
    return EditorAssets_Find( pCatalog, string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) } ) != nullptr;
}

// Hammer's Alt+P. The summary goes to the console (and so to Problems when
// there are any); the window lists each issue with Go to Error.
command_result_t CheckMap( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    const QVector<map_issue_t> issues = MapCheck_Run( &mason.map, &AssetExists, &mason );
    int counts[3]{};
    for ( const map_issue_t &issue : issues ) { ++counts[static_cast<int>( issue.severity )]; }
    const QString summary = issues.isEmpty() ? QStringLiteral( "Check for Problems: no problems found" )
                                             : QStringLiteral( "Check for Problems: %1 errors, %2 warnings, %3 notes" ).arg( counts[2] ).arg( counts[1] ).arg( counts[0] );
    LogText( counts[2] != 0 ? log_level_t::Error : counts[1] != 0 ? log_level_t::Warning : log_level_t::Info, summary );
    if ( IsHeadless( mason ) ) { return command_result_t::OK; }
    if ( mason.pCheck == nullptr ) {
        mason.pCheck = MapCheckDialog_Create( mason.pWindow, &mason.map, &AssetExists, &mason );
    } else {
        ( void )MapCheckDialog_Refresh( mason.pCheck );
    }
    mason.pCheck->show();
    mason.pCheck->raise();
    return command_result_t::OK;
}

// NetRadiant's Region > Set Selection, Hammer's cordon: work on the
// selection's box alone; everything outside hides until the cordon is off.
command_result_t CordonSelection( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    const map_bounds_t bounds = MapViews_SelectionGeometryBounds( &mason.map );
    if ( !bounds.bHas ) {
        LogText( log_level_t::Warning, QStringLiteral( "Select something to cordon around" ) );
        return command_result_t::FAILED;
    }
    MapWorkspace_SetCordon( &mason.map, bounds );
    return command_result_t::OK;
}

u32 CordonSelectionState( void *pContext ) noexcept
{
    return EditorSelection_Count( &Mason( pContext ).map.selection ) != 0u ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

command_result_t CordonToggle( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( !mason.map.cordon.bHas ) { return command_result_t::DISABLED; }
    MapWorkspace_SetCordonActive( &mason.map, MapWorkspace_IsCordonActive( &mason.map ) ? CY_FALSE : CY_TRUE );
    return command_result_t::OK;
}

u32 CordonToggleState( void *pContext ) noexcept
{
    const map_workspace_t &map = Mason( pContext ).map;
    if ( !map.cordon.bHas ) { return COMMAND_STATE_NONE; }
    return COMMAND_STATE_ENABLED | ( MapWorkspace_IsCordonActive( &map ) ? COMMAND_STATE_CHECKED : 0u );
}

command_result_t ShowMapInfo( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.pMapInfo == nullptr ) {
        mason.pMapInfo = MapInfoDialog_Create( mason.pWindow, &mason.map );
    } else {
        MapInfoDialog_Refresh( mason.pMapInfo ); // The map may have changed since.
    }
    mason.pMapInfo->show();
    mason.pMapInfo->raise();
    return command_result_t::OK;
}

// "map.go_to 1310" or "map.go_to spawn_a" runs directly (console, scripts);
// with no argument it opens the dialog.
command_result_t GoTo( void *pContext, const command_args_t &args ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( args.nArgs != 0u ) {
        QStringList parts;
        for ( usize i = 0u; i < args.nArgs; ++i ) { parts.append( QString::fromUtf8( args.pArgs[i].pData, static_cast<qsizetype>( args.pArgs[i].cchLength ) ) ); }
        QString message;
        const map_go_to_status_t status = MapGoTo_Run( &mason.map, parts.join( QLatin1Char( ' ' ) ), &message );
        const bool bOk = status == map_go_to_status_t::OBJECT || status == map_go_to_status_t::POSITION;
        LogText( bOk ? log_level_t::Info : log_level_t::Warning, message );
        return bOk ? command_result_t::OK : command_result_t::FAILED;
    }
    if ( mason.pGoTo == nullptr ) { mason.pGoTo = MapGoToDialog_Create( mason.pWindow, &mason.map ); }
    mason.pGoTo->show();
    mason.pGoTo->raise();
    mason.pGoTo->activateWindow();
    return command_result_t::OK;
}

// Hammer's command list, NetRadiant's Shortcuts: rebuilt each time it opens,
// so keymap changes since show.
command_result_t ShowShortcuts( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.pShortcutsDialog == nullptr ) {
        mason.pShortcutsDialog = EditorShortcutsDialog_Create( mason.pWindow, &mason.gui, &mason.actions );
    } else {
        EditorShortcutsDialog_Refresh( mason.pShortcutsDialog );
    }
    mason.pShortcutsDialog->show();
    mason.pShortcutsDialog->raise();
    return command_result_t::OK;
}

command_result_t About( void *pContext, const command_args_t & ) noexcept
{
    mason_t &mason = Mason( pContext );
    const QString text = QStringLiteral( "Mason - the CypherEngine level editor.\nQt %1" ).arg( QString::fromLatin1( qVersion() ) );
    if ( IsHeadless( mason ) ) {
        LogText( log_level_t::Info, text );
    } else {
        QMessageBox::about( mason.pWindow, QStringLiteral( "About Mason" ), text );
    }
    return command_result_t::OK;
}

CYPHER_NODISCARD bool RegisterCommands( mason_t &mason )
{
    void *pContext = &mason;
    const command_desc_t commands[]{
        { "file.new", "New Map", "Start a new, empty map.", "file-new", nullptr, COMMAND_FLAG_NONE, FileNew, nullptr, pContext },
        { "file.open", "Open Map...", "Open a .cymap map.", "file-open", "file.open [path]", COMMAND_FLAG_NONE, FileOpen, nullptr, pContext },
        { "file.save", "Save", "Save the map.", "file-save", nullptr, COMMAND_FLAG_NONE, FileSave, nullptr, pContext },
        { "file.save_as", "Save As...", "Save the map under a new name.", "file-save-as", "file.save_as [path]", COMMAND_FLAG_NONE, FileSaveAs,
          nullptr, pContext },
        { "file.exit", "Exit", "Close Mason.", nullptr, nullptr, COMMAND_FLAG_NONE, FileExit, nullptr, pContext },
        { "edit.undo", "Undo", "Undo the last change.", "edit-undo", nullptr, COMMAND_FLAG_NONE, Undo, CanUndo, pContext },
        { "edit.redo", "Redo", "Redo the last undone change.", "edit-redo", nullptr, COMMAND_FLAG_NONE, Redo, CanRedo, pContext },
        { "edit.cut", "Cut", "Cut whole authored objects in one undo step, or cut text in the focused editor.", "edit-cut", nullptr, COMMAND_FLAG_NONE, ClipboardCommand<clipboard_action_t::CUT>, ClipboardState<clipboard_action_t::CUT>, pContext },
        { "edit.copy", "Copy", "Copy whole authored objects with materials, properties and owned geometry, or copy text in the focused editor.", "edit-copy", nullptr, COMMAND_FLAG_NONE, ClipboardCommand<clipboard_action_t::COPY>, ClipboardState<clipboard_action_t::COPY>, pContext },
        { "edit.paste", "Paste", "Paste authored map objects using the configured placement, or paste text in the focused editor.", "edit-paste", nullptr, COMMAND_FLAG_NONE, ClipboardCommand<clipboard_action_t::PASTE>, ClipboardState<clipboard_action_t::PASTE>, pContext },
        { "edit.paste_special", "Paste In Place", "Paste authored objects at their original world coordinates with fresh identities, in one undo step.", "edit-paste", nullptr, COMMAND_FLAG_NONE, PasteInPlace, PasteInPlaceState, pContext },
        { "edit.duplicate", "Duplicate", "Duplicate the selected objects in place. The copies are selected and can be moved immediately.", "edit-copy", nullptr, COMMAND_FLAG_NONE,
          DuplicateSelection, CanMoveObjects, pContext },
        { "edit.delete", "Delete", "Delete the selected objects in one undo step.", "edit-delete", nullptr, COMMAND_FLAG_NONE, DeleteSelection, CanMoveObjects, pContext },
        { "map.transform.dialog", "Transform Selection...", "Move, rotate, or scale the selection using exact numeric values.", "tool-translate", nullptr, COMMAND_FLAG_NONE,
          TransformSelection, CanEditGeometry, pContext },
        { "edit.select_all", "Select All", "Select visible roots in the current selection mode.", "select-all", nullptr, COMMAND_FLAG_NONE, SelectAll, CanSelectWholeRoots, pContext },
        { "edit.select_none", "Select None", "Clear the selection.", "select-none", nullptr, COMMAND_FLAG_NONE, SelectNone, HasSelection, pContext },
        { "view.console", "Console", "Show or hide the console.", "view-console", nullptr, COMMAND_FLAG_CHECKABLE,
          TogglePanel<&kConsolePanel>, PanelState<&kConsolePanel>, pContext },
        { "view.output", "Output", "Show or hide build, game, and tool output.", "view-output", nullptr, COMMAND_FLAG_CHECKABLE,
          TogglePanel<&kOutputPanel>, PanelState<&kOutputPanel>, pContext },
        { "view.problems", "Problems", "Show or hide the warnings and errors list.", "view-problems", nullptr, COMMAND_FLAG_CHECKABLE,
          TogglePanel<&kProblemsPanel>, PanelState<&kProblemsPanel>, pContext },
        { "view.assets", "Asset Browser", "Open the Asset Browser window.", "view-assets", nullptr, COMMAND_FLAG_NONE,
          OpenAssetBrowser, nullptr, pContext },
        { "assets.browse_materials", "Browse Materials...", "Choose the Active Material in the Asset Browser window.", "asset-material", nullptr, COMMAND_FLAG_NONE,
          BrowseMaterials, nullptr, pContext },
        { "assets.browser", "Asset Browser", "Hammer's Asset Browser window: find any asset by type, folder, or name, and accept it.", "asset-browser", nullptr,
          COMMAND_FLAG_NONE, OpenAssetBrowser, nullptr, pContext },
        { "assets.database", "Content Library", "Browse map entities, triggers, script and sequence records, models, materials, and other project content.",
          "asset-material", "assets.database [asset path | #entity id]", COMMAND_FLAG_NONE, OpenDatabase, nullptr, pContext },
        { "assets.refresh", "Refresh Assets", "Scan the content folders again.", "asset-browser", nullptr, COMMAND_FLAG_NONE, RefreshAssets,
          nullptr, pContext },
        { "assets.search", "Fast Asset Search", "Search assets across all folders and asset types.", "search", nullptr,
          COMMAND_FLAG_NONE, FocusAssetSearch, nullptr, pContext },
        { "view.sound_preview", "Sound Preview", "Enable or mute editor sound previews. Playback requires the audio backend.", "filter-sound", nullptr,
          COMMAND_FLAG_CHECKABLE, ToggleSoundPreview, SoundPreviewState, pContext },
        { "view.outliner", "Outliner", "Show or hide the outliner.", "view-outliner", nullptr, COMMAND_FLAG_CHECKABLE,
          TogglePanel<&kOutlinerPanel>, PanelState<&kOutlinerPanel>, pContext },
        { "view.properties", "Properties", "Show or hide the properties panel.", "view-properties", nullptr,
          COMMAND_FLAG_CHECKABLE, TogglePanel<&kPropertiesPanel>, PanelState<&kPropertiesPanel>, pContext },
        { "view.properties.open", "Inspect Object", "Show the selected object's properties. Double-click an object in any map view.", "view-properties", nullptr,
          COMMAND_FLAG_NONE, OpenProperties, nullptr, pContext },
        { "view.history", "Undo History", "Show or hide the document's undo and redo history.", "view-history", nullptr,
          COMMAND_FLAG_CHECKABLE, TogglePanel<&kHistoryPanel>, PanelState<&kHistoryPanel>, pContext },
        { "view.command_history", "Command History", "Show or hide every command run, with Repeat.", "command-history", nullptr,
          COMMAND_FLAG_CHECKABLE, TogglePanel<&kCommandHistoryPanel>, PanelState<&kCommandHistoryPanel>, pContext },
        { "edit.repeat_command", "Repeat Command", "Run the selected Command History entries again, or the last repeatable command.",
          "command-repeat", nullptr, COMMAND_FLAG_NONE, RepeatCommand, nullptr, pContext },
        { "view.visibility", "Auto Vis Groups", "Show or hide the automatic visibility groups (world, entities, and their kinds).", "view-visgroups", nullptr,
          COMMAND_FLAG_CHECKABLE, TogglePanel<&kVisibilityPanel>, PanelState<&kVisibilityPanel>, pContext },
        { "view.selection_sets", "Selection Sets", "Show or hide saved selection sets.", "view-selection-sets", nullptr,
          COMMAND_FLAG_CHECKABLE, TogglePanel<&kSelectionSetsPanel>, PanelState<&kSelectionSetsPanel>, pContext },
        { "view.tool_properties", "Tool Properties", "Show or hide options for the current tool.", "view-tool-properties", nullptr,
          COMMAND_FLAG_CHECKABLE, TogglePanel<&kToolPropertiesPanel>, PanelState<&kToolPropertiesPanel>, pContext },
        { "view.active_material", "Active Material", "Show or hide the active material preview.", "asset-material", nullptr,
          COMMAND_FLAG_CHECKABLE, TogglePanel<&kActiveMaterialPanel>, PanelState<&kActiveMaterialPanel>, pContext },
        { "view.layout.save", "Save Workspace", "Save this window, dock and viewport arrangement.", "file-save", nullptr,
          COMMAND_FLAG_NONE, SaveLayout, nullptr, pContext },
        { "view.layout.restore", "Restore Saved Workspace", "Restore the last saved window, dock and viewport arrangement.", "layout-reset", nullptr,
          COMMAND_FLAG_NONE, RestoreLayout, nullptr, pContext },
        { "view.layout.reset", "Reset Layout", "Restore the default panel arrangement.", "layout-reset", nullptr, COMMAND_FLAG_NONE,
          ResetLayout, nullptr, pContext },
        { "view.fullscreen", "Fullscreen", "Toggle fullscreen while retaining the window's normal or maximized state.", "view-maximize", nullptr,
          COMMAND_FLAG_CHECKABLE, ToggleFullscreen, FullscreenState, pContext },
        { "view.layout.modeling", "Perspective + Two Views", "A large perspective pane above two orthographic panes.", "layout-one-top-two-bottom", nullptr,
          COMMAND_FLAG_CHECKABLE, ViewArrangement<map_view_arrangement_t::HAMMER>, ViewArrangementState<map_view_arrangement_t::HAMMER>, pContext },
        { "view.layout.four", "Four Views", "Four equally sized view panes; each projection remains configurable.", "layout-four", nullptr,
          COMMAND_FLAG_CHECKABLE, ViewArrangement<map_view_arrangement_t::FOUR>, ViewArrangementState<map_view_arrangement_t::FOUR>, pContext },
        { "view.layout.two", "Two Views", "Two view panes side by side.", "layout-two-columns", nullptr,
          COMMAND_FLAG_CHECKABLE, ViewArrangement<map_view_arrangement_t::TWO>, ViewArrangementState<map_view_arrangement_t::TWO>, pContext },
        { "view.layout.single", "Single View", "Use the first pane as the full editing area.", "layout-single", nullptr,
          COMMAND_FLAG_CHECKABLE, ViewArrangement<map_view_arrangement_t::PERSPECTIVE>, ViewArrangementState<map_view_arrangement_t::PERSPECTIVE>, pContext },
        { "view.command_palette", "Command Palette", "Search every command by name.", "command-palette", nullptr, COMMAND_FLAG_NONE, OpenPalette,
          nullptr, pContext },
        { "tools.settings", "Settings...", "Every editor setting, by page.", "settings", nullptr, COMMAND_FLAG_NONE, OpenSettings,
          nullptr, pContext },
        { "tools.appearance", "Appearance...", "Themes, colours, text and icon sizes.", "appearance", nullptr, COMMAND_FLAG_NONE, OpenAppearance,
          nullptr, pContext },
        { "tools.theme_editor", "Theme Editor...", "Edit the editor's colours, fonts, and sizes, with live preview.", "theme-editor", nullptr,
          COMMAND_FLAG_NONE, OpenThemeEditor, nullptr, pContext },
        { "tools.keymap_editor", "Keybindings...", "Edit keyboard shortcuts, camera keys and mouse gestures by context.", "keymap", nullptr,
          COMMAND_FLAG_NONE, OpenKeybindings, nullptr, pContext },
        { "map.view.top", "Top View", "Show the top view in the active pane.", "view-top", nullptr, COMMAND_FLAG_CHECKABLE,
          SetActiveView<map_view_type_t::TOP>, ActiveViewState<map_view_type_t::TOP>, pContext },
        { "map.view.front", "Front View", "Show the front view in the active pane.", "view-front", nullptr, COMMAND_FLAG_CHECKABLE,
          SetActiveView<map_view_type_t::FRONT>, ActiveViewState<map_view_type_t::FRONT>, pContext },
        { "map.view.side", "Side View", "Show the side view in the active pane.", "view-side", nullptr, COMMAND_FLAG_CHECKABLE,
          SetActiveView<map_view_type_t::SIDE>, ActiveViewState<map_view_type_t::SIDE>, pContext },
        { "map.view.perspective", "Perspective View", "Show the 3D view in the active pane.", "view-3d", nullptr, COMMAND_FLAG_CHECKABLE,
          SetActiveView<map_view_type_t::CAMERA>, ActiveViewState<map_view_type_t::CAMERA>, pContext },
        { "map.render.wireframe", "3d Wireframe", "The active pane shows the 3D view as edges.", "render-wireframe", nullptr, COMMAND_FLAG_CHECKABLE,
          SetActiveRender<map_render_mode_t::WIREFRAME>, ActiveRenderState<map_render_mode_t::WIREFRAME>, pContext },
        { "map.render.shaded", "3d Shaded", "Show material colours with the editor's fixed directional-light preview.", "render-lit", nullptr, COMMAND_FLAG_CHECKABLE,
          SetActiveRender<map_render_mode_t::SHADED>, ActiveRenderState<map_render_mode_t::SHADED>, pContext },
        { "map.render.fullbright", "3d Fullbright", "The active pane shows material colours without lighting.", "render-flat", nullptr, COMMAND_FLAG_CHECKABLE,
          SetActiveRender<map_render_mode_t::FULLBRIGHT>, ActiveRenderState<map_render_mode_t::FULLBRIGHT>, pContext },
        { "map.render.normals", "3d Normals", "The active pane shows face directions as colour.", "render-textured", nullptr, COMMAND_FLAG_CHECKABLE,
          SetActiveRender<map_render_mode_t::NORMALS>, ActiveRenderState<map_render_mode_t::NORMALS>, pContext },
        { "map.render.mesh_edges", "Mesh Edges", "Outline every face in the shaded modes.", "render-wireframe", nullptr, COMMAND_FLAG_CHECKABLE,
          ToggleActiveOverlay<true>, ActiveOverlayState<true>, pContext },
        { "map.render.wire_overlay", "Wireframe Overlay", "Draw every edge over the shaded view.", "render-wireframe", nullptr, COMMAND_FLAG_CHECKABLE,
          ToggleActiveOverlay<false>, ActiveOverlayState<false>, pContext },
        { "map.view.cycle_2d", "Cycle 2D View", "Switch the active pane between Top, Front, and Side.", "view-quad", nullptr, COMMAND_FLAG_NONE,
          CycleActive2D, nullptr, pContext },
        { "map.view.cycle", "Next View Pane", "Move focus to the next visible pane without changing its contents or camera.", "view-quad", nullptr,
          COMMAND_FLAG_NONE, CycleActivePane, nullptr, pContext },
        { "map.nudge.left", "Nudge Left", "Move whole objects left by the authored grid step in the active 2D view.", "tool-translate", nullptr,
          COMMAND_FLAG_NONE, NudgeSelection<-1, 0, false>, NudgeSelectionState, pContext },
        { "map.nudge.right", "Nudge Right", "Move whole objects right by the authored grid step in the active 2D view.", "tool-translate", nullptr,
          COMMAND_FLAG_NONE, NudgeSelection<1, 0, false>, NudgeSelectionState, pContext },
        { "map.nudge.up", "Nudge Up", "Move whole objects up by the authored grid step in the active 2D view.", "tool-translate", nullptr,
          COMMAND_FLAG_NONE, NudgeSelection<0, 1, false>, NudgeSelectionState, pContext },
        { "map.nudge.down", "Nudge Down", "Move whole objects down by the authored grid step in the active 2D view.", "tool-translate", nullptr,
          COMMAND_FLAG_NONE, NudgeSelection<0, -1, false>, NudgeSelectionState, pContext },
        { "map.nudge_fine.left", "Nudge Left by One Unit", "Move whole objects left by one world unit in the active 2D view.", "tool-translate", nullptr,
          COMMAND_FLAG_NONE, NudgeSelection<-1, 0, true>, NudgeSelectionState, pContext },
        { "map.nudge_fine.right", "Nudge Right by One Unit", "Move whole objects right by one world unit in the active 2D view.", "tool-translate", nullptr,
          COMMAND_FLAG_NONE, NudgeSelection<1, 0, true>, NudgeSelectionState, pContext },
        { "map.nudge_fine.up", "Nudge Up by One Unit", "Move whole objects up by one world unit in the active 2D view.", "tool-translate", nullptr,
          COMMAND_FLAG_NONE, NudgeSelection<0, 1, true>, NudgeSelectionState, pContext },
        { "map.nudge_fine.down", "Nudge Down by One Unit", "Move whole objects down by one world unit in the active 2D view.", "tool-translate", nullptr,
          COMMAND_FLAG_NONE, NudgeSelection<0, -1, true>, NudgeSelectionState, pContext },
        { "map.view.maximize", "Maximize Active View", "Maximize the active pane, or restore the layout.", "view-maximize", nullptr,
          COMMAND_FLAG_CHECKABLE, MaximizeActive, MaximizeActiveState, pContext },
        { "map.cordon.edit", "Cordon Selection", "Work on the selection's box alone: everything outside it is hidden.", "isolate", nullptr,
          COMMAND_FLAG_NONE, CordonSelection, CordonSelectionState, pContext },
        { "map.cordon.toggle", "Use Cordon", "Turn the cordon on or off; the box is kept.", "isolate", nullptr, COMMAND_FLAG_CHECKABLE,
          CordonToggle, CordonToggleState, pContext },
        { "map.check", "Check for Problems...", "Find what is wrong with the map before compiling it: broken outputs, missing assets, stacked entities.",
          "map-check", nullptr, COMMAND_FLAG_NONE, CheckMap, nullptr, pContext },
        { "map.go_to", "Go To...", "Select and frame an object by ID or entity name, or frame the views on a position.", "go-to",
          "map.go_to [id | name | x y z]", COMMAND_FLAG_NONE, GoTo, nullptr, pContext },
        { "map.info", "Map Info...", "Counts by kind and entity class, materials, layers, connections, and the map's extent.", "map-info",
          nullptr, COMMAND_FLAG_NONE, ShowMapInfo, nullptr, pContext },
        { "help.welcome", "Welcome...", "New map, open a map, or pick up a recent one.", "welcome", nullptr, COMMAND_FLAG_NONE, ShowWelcome,
          nullptr, pContext },
        { "help.shortcuts", "Keyboard Shortcuts...", "Every command and its keys, with the views' mouse and held keys.", "keymap", nullptr,
          COMMAND_FLAG_NONE, ShowShortcuts, nullptr, pContext },
        { "help.about", "About Mason", "Version and credits.", nullptr, nullptr, COMMAND_FLAG_NONE, About, nullptr, pContext },
    };
    return EditorCommands_Register( &mason.gui.commands, commands, std::size( commands ) ) == command_registry_status_t::OK &&
           MapWorkspace_RegisterCommands( &mason.map, &mason.gui.commands ) == command_registry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Window pieces
// ---------------------------------------------------------------------------

constexpr editor_menu_item_t kMenus[]{
    { "&File", "file.new" },
    { "&File", "file.open" },
    { "&File", nullptr },
    { "&File", "file.save" },
    { "&File", "file.save_as" },
    { "&File", nullptr },
    { "&File", "file.exit" },
    { "&Edit", "edit.undo" },
    { "&Edit", "edit.redo" },
    { "&Edit", "edit.repeat_command" },
    { "&Edit", nullptr },
    { "&Edit", "edit.cut" },
    { "&Edit", "edit.copy" },
    { "&Edit", "edit.paste" },
    { "&Edit", "edit.paste_special" },
    { "&Edit", "edit.duplicate" },
    { "&Edit", "edit.delete" },
    { "&Edit", nullptr },
    { "&Edit", "map.transform.dialog" },
    { "&Selection", "edit.select_all" },
    { "&Selection", "edit.select_none" },
    { "&Selection", "edit.invert_selection" },
    { "&Selection", nullptr },
    { "&Selection", "map.select.grow" },
    { "&Selection", "map.select.shrink" },
    { "&Selection", "map.select.loop" },
    { "&Selection", "map.select.ring" },
    { "&Selection", nullptr },
    { "&Selection", "map.select.touching" },
    { "&Selection", "map.select.same_material" },
    { "&Selection", "map.select.same_class" },
    { "&Selection", nullptr },
    { "&Selection", "map.select_mode.vertices" },
    { "&Selection", "map.select_mode.edges" },
    { "&Selection", "map.select_mode.faces" },
    { "&Selection", "map.select_mode.meshes" },
    { "&Selection", "map.select_mode.objects" },
    { "&Selection", "map.select_mode.groups" },
    { "&Selection", "map.select_mode.navigation" },
    { "&View", "map.grid.show" },
    { "&View", "map.grid.smaller" },
    { "&View", "map.grid.larger" },
    { "&View", "map.grid.snap" },
    { "&View", "map.grid.angle_snap" },
    { "&View", "map.grid.scale_snap" },
    { "&View", nullptr },
    { "&View", "map.view.frame_all" },
    { "&View", "map.view.center_selection_2d" },
    { "&View", "map.view.center_selection_3d" },
    { "&View", nullptr },
    { "&View", "map.view.top" },
    { "&View", "map.view.front" },
    { "&View", "map.view.side" },
    { "&View", "map.view.perspective" },
    { "&View", "map.view.maximize" },
    { "&View", nullptr },
    { "&View", "map.render.wireframe" },
    { "&View", "map.render.flat" },
    { "&View", "map.render.textured" },
    { "&View", "map.render.lit" },
    { "&View", nullptr },
    { "&View", "map.hide.selected" },
    { "&View", "map.hide.unselected" },
    { "&View", "map.hide.show_all" },
    { "&View", nullptr },
    { "&View", "view.command_palette" },
    { "&View", "assets.search" },
    { "&View", "view.sound_preview" },
    { "&Map", "map.brush.hollow" },
    { "&Map", "map.brush.carve" },
    { "&Map", "map.brush.merge" },
    { "&Map", "map.brush.snap_to_grid" },
    { "&Map", "map.brush.to_mesh" },
    { "&Map", nullptr },
    { "&Map", "map.transform.flip_horizontal" },
    { "&Map", "map.transform.flip_vertical" },
    { "&Map", "map.transform.rotate_cw" },
    { "&Map", "map.transform.rotate_ccw" },
    { "&Map", "map.align.left" },
    { "&Map", "map.align.right" },
    { "&Map", "map.align.top" },
    { "&Map", "map.align.bottom" },
    { "&Map", nullptr },
    { "&Map", "map.group.create" },
    { "&Map", "map.group.ungroup" },
    { "&Map", "map.prefab.create" },
    { "&Map", "map.layer.create" },
    { "&Map", "map.visgroup.create" },
    { "&Map", nullptr },
    { "&Map", "map.cordon.edit" },
    { "&Map", "map.cordon.toggle" },
    { "&Map", nullptr },
    { "&Map", "map.go_to" },
    { "&Map", "map.info" },
    { "&Map", nullptr },
    { "&Map", "map.check" },
    { "&Map", "map.compile" },
    { "&Map", "map.run" },
    { "&Map", "map.stop" },
    { "&Map", "map.leak.show" },
    { "&Mesh", "map.mesh.extrude" },
    { "&Mesh", "map.mesh.inset" },
    { "&Mesh", "map.mesh.bevel" },
    { "&Mesh", "map.mesh.bridge" },
    { "&Mesh", nullptr },
    { "&Mesh", "map.mesh.merge" },
    { "&Mesh", "map.mesh.collapse" },
    { "&Mesh", "map.mesh.dissolve" },
    { "&Mesh", "map.mesh.split" },
    { "&Mesh", nullptr },
    { "&Mesh", "map.mesh.slice" },
    { "&Mesh", "map.mesh.quad_slice" },
    { "&Mesh", "map.mesh.subdivide" },
    { "&Mesh", "map.mesh.smooth" },
    { "&Mesh", "map.mesh.solidify" },
    { "&Mesh", nullptr },
    { "&Mesh", "map.mesh.triangulate" },
    { "&Mesh", "map.mesh.flip_normals" },
    { "&Mesh", "map.mesh.fill_hole" },
    { "&Mesh", nullptr },
    { "&Mesh", "map.mesh.boolean_union" },
    { "&Mesh", "map.mesh.boolean_subtract" },
    { "&Mesh", "map.mesh.boolean_intersect" },
    { "&Mesh", nullptr },
    { "&Mesh", "map.mesh.to_brush" },
    { "&Texture", "assets.browse_materials" },
    { "&Texture", "assets.browser" },
    { "&Texture", "assets.database" },
    { "&Texture", "map.tool.apply_material" },
    { "&Texture", "map.texture.replace" },
    { "&Texture", nullptr },
    { "&Texture", "map.texture.fit" },
    { "&Texture", "map.texture.align_world" },
    { "&Texture", "map.texture.align_face" },
    { "&Texture", nullptr },
    { "&Texture", "map.texture.justify_left" },
    { "&Texture", "map.texture.justify_right" },
    { "&Texture", "map.texture.justify_top" },
    { "&Texture", "map.texture.justify_bottom" },
    { "&Texture", "map.texture.justify_center" },
    { "&Texture", nullptr },
    { "&Texture", "map.texture.rotate" },
    { "&Texture", "map.texture.scale" },
    { "&Texture", "map.texture.shift" },
    { "&Texture", "map.texture.unwrap" },
    { "&Texture", nullptr },
    { "&Texture", "map.texture.lock" },
    { "&Texture", "map.texture.scale_lock" },
    { "&Tools", "tools.settings" },
    { "&Tools", "tools.appearance" },
    { "&Tools", "tools.theme_editor" },
    { "&Tools", "tools.keymap_editor" },
    { "&Tools", nullptr },
    { "&Tools", "map.tool.select" },
    { "&Tools", "map.tool.camera" },
    { "&Tools", "map.tool.translate" },
    { "&Tools", "map.tool.rotate" },
    { "&Tools", "map.tool.scale" },
    { "&Tools", "map.tool.pivot" },
    { "&Tools", "map.tool.workplane" },
    { "&Tools", nullptr },
    { "&Tools", "map.tool.entity" },
    { "&Tools", "map.tool.block" },
    { "&Tools", "map.tool.polygon" },
    { "&Tools", "map.tool.path" },
    { "&Tools", "map.tool.curve" },
    { "&Tools", "map.tool.patch" },
    { "&Tools", "map.tool.terrain" },
    { "&Tools", "map.tool.measure" },
    { "&Tools", nullptr },
    { "&Tools", "map.tool.clip" },
    { "&Tools", "map.tool.vertex" },
    { "&Tools", "map.tool.extrude" },
    { "&Tools", "map.tool.knife" },
    { "&Tools", "map.tool.loop_cut" },
    { "&Tools", "map.tool.mirror" },
    { "&Tools", nullptr },
    { "&Tools", "map.tool.texture" },
    { "&Tools", "map.tool.eyedropper" },
    { "&Tools", "map.tool.decal" },
    { "&Tools", "map.tool.overlay" },
    { "&Tools", "map.tool.paint" },
    { "&Tools", nullptr },
    { "&Tools", "map.terrain.raise" },
    { "&Tools", "map.terrain.lower" },
    { "&Tools", "map.terrain.smooth" },
    { "&Tools", "map.terrain.flatten" },
    { "&Window", "view.layout.modeling" },
    { "&Window", "view.layout.four" },
    { "&Window", "view.layout.two" },
    { "&Window", "view.layout.single" },
    { "&Window", nullptr },
    { "&Window", "view.tool_properties" },
    { "&Window", "view.active_material" },
    { "&Window", "view.outliner" },
    { "&Window", "view.properties" },
    { "&Window", "view.history" },
    { "&Window", "view.command_history" },
    { "&Window", "view.selection_sets" },
    { "&Window", "view.visibility" },
    { "&Window", "view.assets" },
    { "&Window", "assets.database" },
    { "&Window", "view.console" },
    { "&Window", "view.output" },
    { "&Window", "view.problems" },
    { "&Window", nullptr },
    { "&Window", "view.layout.save" },
    { "&Window", "view.layout.restore" },
    { "&Window", "view.layout.reset" },
    { "&Help", "help.welcome" },
    { "&Help", "help.shortcuts" },
    { "&Help", nullptr },
    { "&Help", "help.about" },
};


// The TileEditor's main toolbar order: document, history, build, framing,
// then the grid chooser (inserted in code) and the editing toggles, then the
// panel toggles.
constexpr const char *kMainToolBar[]{
    "file.new", "file.open", "file.save", nullptr, "edit.undo", "edit.redo", nullptr, "map.check", "map.compile", "map.run", "map.stop", nullptr, "map.view.frame_all", nullptr,
};

constexpr const char *kMainToolBarEditing[]{
    "map.grid.show", "map.grid.snap", "map.grid.angle_snap", "map.grid.scale_snap", "map.texture.lock", nullptr,
    "edit.invert_selection", "map.hide.selected", "map.hide.unselected", "map.hide.show_all", nullptr,
    "view.assets", "view.console", "view.history", "view.outliner", "view.properties",
};

// Hammer 5's selection modes, with their names beside the icons.
constexpr const char *kSelectModes[]{
    "map.select_mode.vertices", "map.select_mode.edges",  "map.select_mode.faces",      "map.select_mode.meshes",
    "map.select_mode.objects",  "map.select_mode.groups", "map.select_mode.navigation",
};

// Keep the full original tool collection visible in two columns.
constexpr const char *kToolPalette[]{
    "map.tool.select", "map.tool.camera", nullptr,
    "map.tool.translate", "map.tool.rotate", "map.tool.scale", "map.tool.pivot",
    "map.tool.workplane", "view.properties.open", nullptr,
    "map.tool.entity", "map.tool.block", "map.tool.polygon", "map.tool.path",
    "map.tool.curve", "map.tool.patch", "map.tool.terrain", "map.tool.measure", nullptr,
    "map.tool.clip", "map.tool.vertex", "map.tool.extrude", "map.tool.knife",
    "map.tool.loop_cut", "map.tool.mirror", nullptr,
    "map.mesh.inset", "map.mesh.bevel", "map.mesh.bridge", "map.mesh.fill_hole", nullptr,
    "map.brush.hollow", "map.brush.carve", "map.brush.merge", "map.brush.to_mesh", nullptr,
    "map.tool.texture", "map.tool.apply_material", "map.tool.eyedropper",
    "map.tool.decal", "map.tool.overlay", "map.tool.paint",
    nullptr, "map.hide.unselected", "map.hide.show_all",
};

// Shared actions keep the top editing row and the left tool palette in sync.
constexpr const char *kEditingTools[]{
    "map.tool.translate", "map.tool.rotate", "map.tool.scale", nullptr,
    "map.tool.pivot", "map.tool.workplane",
};

// Hammer 5's "View:" group: what the views draw.
constexpr const char *kViewFilters[]{
    "map.show.world", "map.show.tied", "map.show.terrain", "map.show.entities", "map.show.lights", "map.show.triggers", "map.show.props",
};

constexpr double kGridSizes[]{ 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 512.0, 1024.0, 2048.0, 4096.0 };
constexpr double kAngleSnaps[]{ 0.0, 1.0, 5.0, 15.0, 45.0, 90.0 };
constexpr double kScaleSnaps[]{ 0.0, 0.05, 0.1, 0.25, 0.5, 1.0 };

QWidget *CreatePaneContent( QWidget *pParent, map_view_type_t type, void *pContext );
QWidget *CreateViews( void *pContext, QWidget *pParent )
{
    QWidget *views = MapViews_Create( pParent, &Mason( pContext ).map );
    MapViews_SetContentFactory( views, &CreatePaneContent, pContext );
    return views;
}
QWidget *CreateOutliner( void *pContext, QWidget *pParent ) { return MapOutliner_Create( pParent, &Mason( pContext ).map ); }
QWidget *CreateProperties( void *pContext, QWidget *pParent ) { return MapProperties_Create( pParent, &Mason( pContext ).map ); }
bool ToolControlCancel( void *pContext, QEvent *event )
{
    return MapViews_HandleToolCancel( Mason( pContext ).docking.pCentral, event );
}
QWidget *CreateToolProperties( void *pContext, QWidget *pParent )
{
    QWidget *panel = MapToolProperties_Create( pParent, &Mason( pContext ).map );
    MapToolProperties_SetCancelHandler( panel, &ToolControlCancel, pContext );
    return panel;
}
QWidget *CreateActiveMaterial( void *pContext, QWidget *pParent ) { return MapActiveMaterial_Create( pParent, &Mason( pContext ).map ); }
QWidget *CreateVisgroups( void *pContext, QWidget *pParent ) { return MapVisgroups_Create( pParent, &Mason( pContext ).map ); }
QWidget *CreateSelectionSets( void *pContext, QWidget *pParent ) { return MapSelectionSets_Create( pParent, &Mason( pContext ).map ); }

void HistoryApplied( void *pContext, editor_history_status_t status ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( status == editor_history_status_t::OK || status == editor_history_status_t::APPLY_FAILED ) {
        MapWorkspace_DocumentChanged( &mason.map );
    }
    if ( status != editor_history_status_t::OK ) {
        LogText( log_level_t::Error, QStringLiteral( "History operation failed; inspect the document and console before continuing." ) );
    }
}

// Created with the window, not when its dock first opens: commands run
// while the panel is closed still belong in the history (and Shift+G
// repeats them). The dock adopts this widget when shown.
QWidget *CreateCommandHistory( void *pContext, QWidget *pParent )
{
    mason_t &mason = Mason( pContext );
    if ( mason.pCommandHistory == nullptr ) { mason.pCommandHistory = EditorCommandHistory_Create( pParent, &mason.gui ); }
    return mason.pCommandHistory;
}

QWidget *CreateHistory( void *pContext, QWidget *pParent )
{
    mason_t &mason = Mason( pContext );
    mason.pHistory = EditorHistoryPanel_Create( pParent, &mason.map.history, &mason.gui.style, &HistoryApplied, &mason );
    return mason.pHistory;
}

QWidget *CreateOutput( void *pContext, QWidget *pParent )
{
    mason_t &mason = Mason( pContext );
    return EditorOutput_Create( pParent, &mason.gui.style, &mason.gui.log );
}

QWidget *CreateProblems( void *pContext, QWidget *pParent )
{
    mason_t &mason = Mason( pContext );
    return EditorProblems_Create( pParent, &mason.gui.style, &mason.gui.log );
}

// Content folders: editor.assets.roots when set (relative entries resolve
// against the project), else the project's assets folder. The project is the
// nearest folder above the map (or the working folder) holding an assets
// folder or a .cyproject file, until projects open explicitly.
QString ProjectFolder( const mason_t &mason )
{
    QDir dir( mason.map.path.isEmpty() ? QDir::currentPath() : QFileInfo( mason.map.path ).absolutePath() );
    const QString start = dir.absolutePath();
    for ( int depth = 0; depth < 12; ++depth ) {
        if ( QFileInfo( dir.filePath( QStringLiteral( "assets" ) ) ).isDir() || !dir.entryList( { QStringLiteral( "*.cyproject" ) }, QDir::Files ).isEmpty() ) {
            return dir.absolutePath();
        }
        if ( !dir.cdUp() ) { break; }
    }
    return start;
}

QStringList AssetRoots( const mason_t &mason )
{
    const QString project = ProjectFolder( mason );
    const string_view_t setting = EditorSettings_Text( &mason.gui.settings, "editor.assets.roots", string_view_t{} );
    QStringList roots;
    for ( const QString &part : QString::fromUtf8( setting.pData, static_cast<qsizetype>( setting.cchLength ) ).split( QLatin1Char( ';' ), Qt::SkipEmptyParts ) ) {
        const QString folder = part.trimmed();
        if ( !folder.isEmpty() ) { roots.append( QDir::cleanPath( QDir::isRelativePath( folder ) ? QDir( project ).filePath( folder ) : folder ) ); }
    }
    if ( roots.isEmpty() && QFileInfo( QDir( project ).filePath( QStringLiteral( "assets" ) ) ).isDir() ) {
        roots.append( QDir::cleanPath( QDir( project ).filePath( QStringLiteral( "assets" ) ) ) );
    }
    return roots;
}

void UpdateAssetRoots( mason_t &mason )
{
    if ( mason.pAssets == nullptr ) { return; }
    const QStringList roots = AssetRoots( mason );
    if ( roots != EditorAssetBrowser_Roots( mason.pAssets ) ) { EditorAssetBrowser_SetRoots( mason.pAssets, roots ); }
}

void OnAssetCatalogRescanned( void *pContext )
{
    mason_t &mason = Mason( pContext );
    if ( mason.pAssetWindow != nullptr ) { EditorAssetWindow_Refresh( mason.pAssetWindow ); }
    if ( mason.pDatabase != nullptr ) { EditorDatabaseView_Refresh( mason.pDatabase ); }
    if ( mason.docking.pCentral == nullptr ) { return; }
    for ( QWidget *pane : mason.docking.pCentral->findChildren<QWidget *>() ) {
        const QVariant type = pane->property( "masonContentType" );
        if ( !type.isValid() ) { continue; }
        if ( type.toInt() == static_cast<int>( map_view_type_t::ASSETS ) ) { EditorAssetWindow_Refresh( pane ); }
        else { EditorDatabaseView_Refresh( pane ); }
    }
}

// The Used in Map tab: the materials faces and patches name (the geometry
// library holds those), plus every asset path anywhere in the map's own
// documents - the sky, entity models and sounds, prefabs.
void UpdateUsedAssets( mason_t &mason )
{
    if ( mason.pAssets == nullptr ) { return; }
    QStringList used;
    if ( const map_document_t *pDocument = mason.map.pDocument ) {
        for ( u64 ref = 1u; ref <= pDocument->materials.entries.nCount; ++ref ) {
            const string_view_t path = MapMaterials_Path( &pDocument->materials, ref );
            if ( path.cchLength != 0u ) { used.append( QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) ) ); }
        }
        const auto collect = []( void *pContext, string_view_t path, editor_asset_kind_t ) {
            static_cast<QStringList *>( pContext )->append( QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) ) );
        };
        EditorAssets_VisitReferences( SettingsDocument_Root( &pDocument->root ), collect, &used );
        for ( usize i = 0u; i < pDocument->chunks.nCount; ++i ) {
            EditorAssets_VisitReferences( SettingsDocument_Root( &pDocument->chunks.pData[i]->store ), collect, &used );
        }
    }
    EditorAssetBrowser_SetUsed( mason.pAssets, QStringLiteral( "Used in Map" ), used ); // Duplicates collapse there.
    mason.usedAssets = used;
    if ( mason.pAssetWindow != nullptr ) { EditorAssetWindow_SetUsed( mason.pAssetWindow, used ); }
    if ( mason.pDatabase != nullptr ) { EditorDatabaseView_Refresh( mason.pDatabase ); }
    if ( mason.docking.pCentral != nullptr ) {
        for ( QWidget *pane : mason.docking.pCentral->findChildren<QWidget *>() ) {
            const QVariant content = pane->property( "masonContentType" );
            if ( !content.isValid() ) { continue; }
            if ( content.toInt() == static_cast<int>( map_view_type_t::ASSETS ) ) {
                EditorAssetWindow_SetUsed( pane, used );
            } else {
                EditorDatabaseView_Refresh( pane ); // Library refresh preserves unsaved recipes/stages.
            }
        }
    }
}

// Double-click or Enter: a material becomes the Active Material, Hammer's
// "what new faces get". Other kinds gain actions as their tools land.
void OnAssetActivated( void *pContext, const QString &path, editor_asset_kind_t kind )
{
    mason_t &mason = Mason( pContext );
    if ( kind != editor_asset_kind_t::MATERIAL ) {
        LogText( log_level_t::Info, QStringLiteral( "%1 assets have no action yet: %2" ).arg( QString::fromLatin1( EditorAssets_KindLabel( kind ) ), path ) );
        return;
    }
    const setting_descriptor_t *pSetting = EditorSettings_Find( &mason.gui.settings, StringView_FromCString( "editor.map.default_material" ) );
    const QByteArray utf8 = path.toUtf8();
    setting_value_t value{};
    value.type = setting_type_t::STRING;
    value.text = string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) };
    if ( pSetting == nullptr || EditorSettings_Write( &mason.gui.settings, settings_scope_t::USER, *pSetting, value ) != settings_registry_status_t::OK ) {
        LogText( log_level_t::Warning, QStringLiteral( "Active material not changed: %1" ).arg( path ) );
        return;
    }
    LogText( log_level_t::Info, QStringLiteral( "Active material: %1" ).arg( path ) );
}

QStringList SelectionAssets( void *pContext )
{
    return MapInfo_SelectionAssets( &Mason( pContext ).map );
}

QDialog *AssetWindow( mason_t &mason )
{
    if ( mason.pAssetWindow != nullptr ) { return mason.pAssetWindow; }
    if ( mason.pAssets == nullptr ) { return nullptr; }
    mason.pAssetWindow = EditorAssetWindow_Create( mason.pWindow, &mason.gui, mason.pAssets );
    // Browsing: Accept does what double-clicking in the panel does (a
    // material becomes the Active Material).
    EditorAssetWindow_SetAccept( mason.pAssetWindow, &OnAssetActivated, &mason );
    EditorAssetWindow_SetSelectionSource( mason.pAssetWindow, &SelectionAssets, &mason );
    EditorAssetWindow_SetUsed( mason.pAssetWindow, mason.usedAssets );
    return mason.pAssetWindow;
}

QVector<editor_database_entity_t> DatabaseEntities( void *pContext )
{
    const map_workspace_t &map = Mason( pContext ).map;
    QVector<editor_database_entity_t> entities;
    entities.reserve( static_cast<qsizetype>( map.wire.entities.nCount ) );
    for ( usize i = 0u; i < map.wire.entities.nCount; ++i ) {
        const map_wire_entity_t &entity = map.wire.entities.pData[i];
        entities.append( editor_database_entity_t{ entity.id, QString::fromUtf8( entity.className ), QString::fromUtf8( entity.name ) } );
    }
    return entities;
}

const key_value_t *DatabaseRecord( void *pContext, u64 id )
{
    mason_t &mason = Mason( pContext );
    return mason.map.pDocument != nullptr ? MapDocument_FindObject( mason.map.pDocument, id, nullptr ) : nullptr;
}

void DatabaseGoTo( void *pContext, u64 id )
{
    auto &mason = Mason( pContext );
    if ( MapGoTo_Run( &mason.map, QStringLiteral( "#%1" ).arg( id ) ) == map_go_to_status_t::OBJECT ) {
        ( void )EditorCommands_ExecuteLine( &mason.gui.commands, StringView_FromCString( "view.properties.open" ) );
    }
}

QDialog *DatabaseView( mason_t &mason )
{
    if ( mason.pDatabase != nullptr ) { return mason.pDatabase; }
    if ( mason.pAssets == nullptr ) { return nullptr; }
    mason.pDatabase = EditorDatabaseView_Create( mason.pWindow, &mason.gui, mason.pAssets );
    EditorDatabaseView_SetEntitySource( mason.pDatabase, &DatabaseEntities, &DatabaseRecord, &DatabaseGoTo, &mason );
    EditorDatabaseView_SetTab( mason.pDatabase, DATABASE_TAB_ENTITIES );
    return mason.pDatabase;
}

QWidget *CreatePaneContent( QWidget *parent, map_view_type_t type, void *context )
{
    mason_t &mason = Mason( context );
    if ( mason.pAssets == nullptr ) { return nullptr; }
    QWidget *pane = nullptr;
    if ( type == map_view_type_t::ASSETS ) {
        pane = EditorAssetWindow_CreateEmbedded( parent, &mason.gui, mason.pAssets );
        if ( pane != nullptr ) {
            EditorAssetWindow_SetAccept( pane, &OnAssetActivated, &mason );
            EditorAssetWindow_SetSelectionSource( pane, &SelectionAssets, &mason );
            EditorAssetWindow_SetUsed( pane, mason.usedAssets );
        }
    } else if ( type == map_view_type_t::DATABASE || type == map_view_type_t::SHADERS ) {
        const bool shaders = type == map_view_type_t::SHADERS;
        pane = EditorDatabaseView_CreateEmbedded( parent, &mason.gui, mason.pAssets,
            shaders ? DATABASE_TAB_SHADERS : DATABASE_TAB_ENTITIES, shaders );
        if ( pane != nullptr ) {
            EditorDatabaseView_SetEntitySource( pane, &DatabaseEntities, &DatabaseRecord, &DatabaseGoTo, &mason );
        }
    }
    if ( pane != nullptr ) { pane->setProperty( "masonContentType", static_cast<int>( type ) ); }
    return pane;
}

QImage MaterialImage( void *pContext, const QString &path )
{
    mason_t &mason = Mason( pContext );
    return mason.pAssets != nullptr ? EditorAssetBrowser_Image( mason.pAssets, path ) : QImage();
}

// Created at start-up: the Asset Browser window,
// Active Material's image, and Check for Problems all read its catalogue.
// Keep this shared source hidden and parent-owned; visible browsers use it.
QWidget *CreateAssets( void *pContext, QWidget *pParent )
{
    mason_t &mason = Mason( pContext );
    if ( mason.pAssets != nullptr ) { return mason.pAssets; }
    mason.pAssets = EditorAssetBrowser_Create( pParent, &mason.gui );
    auto *localSearch = mason.pAssets->findChild<QLineEdit *>( QStringLiteral( "AssetBrowserSearch" ) );
    auto *fastSearch = mason.pWindow->findChild<QLineEdit *>( QStringLiteral( "MasonFastAssetSearch" ) );
    if ( localSearch != nullptr && fastSearch != nullptr ) {
        // setText does not emit textEdited: synchronizing the visible query
        // cannot restart a global search or disturb local folder filters.
        QObject::connect( localSearch, &QLineEdit::textChanged, fastSearch, [fastSearch]( const QString &text ) {
            if ( fastSearch->text() != text ) { fastSearch->setText( text ); }
        } );
    }
    EditorAssetBrowser_SetActivate( mason.pAssets, &OnAssetActivated, &mason );
    EditorAssetBrowser_SetRescanCallback( mason.pAssets, &OnAssetCatalogRescanned, &mason );
    UpdateAssetRoots( mason );
    UpdateUsedAssets( mason );
    // Active Material and later the texture tools show materials from the
    // browser's thumbnails; the map workspace never sees the catalogue.
    MapWorkspace_SetMaterialImages( &mason.map, &MaterialImage, &mason );
    return mason.pAssets;
}

QWidget *CreateConsole( void *pContext, QWidget *pParent )
{
    mason_t &mason = Mason( pContext );
    QWidget *pConsole = EditorConsole_Create( pParent, &mason.gui.commands, &mason.gui.style, &mason.gui.log );
    if ( EditorConsole_RegisterCommands( &mason.gui.commands, pConsole ) != command_registry_status_t::OK ) {
        CY_LOG_WRITE( Error, Editor, "Console commands could not be registered" );
    }
    return pConsole;
}

void UpdateTitle( mason_t &mason )
{
    mason.pWindow->setWindowTitle( QStringLiteral( "%1[*] - Mason" ).arg( MapWorkspace_DisplayName( &mason.map ) ) );
    mason.pWindow->setWindowModified( MapWorkspace_IsModified( &mason.map ) );
}

// Hammer's size readout: width (x), length (y), height (z) of the selected
// geometry, and its centre. An active edit shows its current construction
// bounds; a selection of point entities only shows where the first one stands.
QString SelectionSizeText( const map_workspace_t &map )
{
    // Component picking does not imply an editable parent-mesh extent.
    if ( map.elementMode == map_element_mode_t::VERTICES || map.elementMode == map_element_mode_t::EDGES ) { return QString(); }
    if ( map.editPreview.bActive && map.editPreview.bClip ) {
        if ( map.editPreview.status != map_status_t::OK ) { return QStringLiteral( "Clip preview: %1" ).arg( QString::fromLatin1( MapDocument_StatusName( map.editPreview.status ) ) ); }
        if ( !map.editPreview.bounds.bHas ) { return QStringLiteral( "Clip preview: empty result" ); }
    }
    const bool preview = map.editPreview.bActive && map.editPreview.bounds.bHas;
    if ( !preview && EditorSelection_Count( &map.selection ) == 0u ) { return QString(); }
    const map_bounds_t bounds = preview ? map.editPreview.bounds : MapViews_SelectionGeometryBounds( &map );
    const auto number = []( f64 value ) { return QString::number( value, 'f', std::fabs( value - std::round( value ) ) < 1e-6 ? 0 : 2 ); };
    if ( bounds.bHas ) {
        const math::vec3d_t &a = bounds.box.minimum;
        const math::vec3d_t &b = bounds.box.maximum;
        const QString size = QStringLiteral( "%1w %2l %3h @(%4 %5 %6)" )
            .arg( number( b.x - a.x ), number( b.y - a.y ), number( b.z - a.z ), number( ( a.x + b.x ) * 0.5 ), number( ( a.y + b.y ) * 0.5 ),
                  number( ( a.z + b.z ) * 0.5 ) );
        return preview ? ( map.editPreview.status == map_status_t::OK ? QStringLiteral( "Preview " ) : QStringLiteral( "Invalid preview " ) ) + size : size;
    }
    for ( usize i = 0u; i < map.wire.entities.nCount; ++i ) {
        const map_wire_entity_t &entity = map.wire.entities.pData[i];
        if ( entity.bHasOrigin && MapWorkspace_IsSelected( &map, entity.id ) ) {
            return QStringLiteral( "@(%1 %2 %3)" ).arg( number( entity.origin.x ), number( entity.origin.y ), number( entity.origin.z ) );
        }
    }
    return QString();
}

void UpdateStatus( mason_t &mason, u32 changes )
{
    const map_workspace_t &map = mason.map;
    if ( ( changes & MAP_CHANGE_VIEW ) != 0u ) {
        mason.pStatusTool->setText( QString::fromUtf8( MapWorkspace_ToolName( map.tool ) ) );
        mason.pStatusGrid->setText( QStringLiteral( "Grid %1" ).arg( map.gridSize ) );
        // The choosers show the session's values without writing them back.
        if ( mason.pGridCombo != nullptr ) {
            const QSignalBlocker blocker( mason.pGridCombo );
            if ( mason.pGridCombo->findData( map.gridSize ) < 0 ) {
                mason.pGridCombo->addItem( QStringLiteral( "%1 u" ).arg( map.gridSize ), map.gridSize );
            }
            mason.pGridCombo->setCurrentIndex( mason.pGridCombo->findData( map.gridSize ) );
        }
        if ( mason.pAngleCombo != nullptr ) {
            const QSignalBlocker blocker( mason.pAngleCombo );
            const int index = mason.pAngleCombo->findData( map.angleSnap );
            if ( index < 0 ) { mason.pAngleCombo->addItem( QStringLiteral( "%1\u00B0" ).arg( map.angleSnap ), map.angleSnap ); }
            mason.pAngleCombo->setCurrentIndex( mason.pAngleCombo->findData( map.angleSnap ) );
        }
        if ( mason.pScaleCombo != nullptr ) {
            const QSignalBlocker blocker( mason.pScaleCombo );
            if ( mason.pScaleCombo->findData( map.scaleSnap ) < 0 ) {
                mason.pScaleCombo->addItem( QStringLiteral( "%1%" ).arg( map.scaleSnap * 100.0 ), map.scaleSnap );
            }
            mason.pScaleCombo->setCurrentIndex( mason.pScaleCombo->findData( map.scaleSnap ) );
        }
    }
    if ( ( changes & ( MAP_CHANGE_SELECTION | MAP_CHANGE_DOCUMENT | MAP_CHANGE_VIEW ) ) != 0u ) {
        const usize nSelected = EditorSelection_Count( &map.selection );
        if ( map.elementMode == map_element_mode_t::VERTICES ) {
            const usize vertices = MapWorkspace_HasMeshVertices( &map ) ? map.meshSelection.vertices.nCount : 0;
            if ( vertices == 0u ) { mason.pStatusSelection->setText( QStringLiteral( "No vertices selected" ) ); }
            else if ( vertices == 1u ) { mason.pStatusSelection->setText( QStringLiteral( "1 vertex selected" ) ); }
            else { mason.pStatusSelection->setText( QStringLiteral( "%1 vertices selected" ).arg( vertices ) ); }
        } else if ( MapWorkspace_HasMeshEdges( &map ) ) {
            mason.pStatusSelection->setText( map.meshSelection.edges.nCount == 1u ? QStringLiteral( "1 edge selected" ) :
                QStringLiteral( "%1 edges selected" ).arg( map.meshSelection.edges.nCount ) );
        } else {
            mason.pStatusSelection->setText( nSelected == 0u ? QStringLiteral( "No selection" ) : QStringLiteral( "%1 selected" ).arg( nSelected ) );
        }
    }
    if ( mason.pStatusSize != nullptr && ( changes & ( MAP_CHANGE_SELECTION | MAP_CHANGE_DOCUMENT | MAP_CHANGE_VIEW ) ) != 0u ) {
        mason.pStatusSize->setText( SelectionSizeText( map ) );
    }
    if ( ( changes & MAP_CHANGE_CURSOR ) != 0u ) {
        static const char kAxisNames[3]{ 'x', 'y', 'z' };
        const f64 components[3]{ map.cursor.x, map.cursor.y, map.cursor.z };
        QStringList parts;
        for ( u32 axis = 0u; axis < 3u; ++axis ) {
            if ( ( map.cursorAxes & ( 1u << axis ) ) == 0u ) { continue; }
            parts.append( QStringLiteral( "%1 %2" ).arg( QLatin1Char( kAxisNames[axis] ) ).arg( components[axis], 0, 'f', 0 ) );
        }
        mason.pStatusCursor->setText( parts.join( QStringLiteral( "  " ) ) );
    }
}

void OnMapChanged( void *pContext, u32 changes ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.pWindow == nullptr ) { return; }
    if ( ( changes & MAP_CHANGE_TITLE ) != 0u ) { UpdateTitle( mason ); }
    UpdateStatus( mason, changes );
    // Enabled and checked states follow the session: frame-selection needs a
    // selection, the active tool shows checked. The cursor changes on every
    // mouse move, so it skips the action pass.
    if ( ( changes & ~MAP_CHANGE_CURSOR ) != 0u ) { EditorActions_RefreshStates( &mason.actions ); }
    if ( ( changes & MAP_CHANGE_TITLE ) != 0u ) { UpdateAssetRoots( mason ); } // Another map may be another project.
    if ( ( changes & MAP_CHANGE_DOCUMENT ) != 0u ) { UpdateUsedAssets( mason ); }
}

// Every command, whichever way it ran - menu, shortcut, console, script -
// can change what other commands offer, so all action states refresh after
// each one.
void OnCommandExecuted( void *pContext, const command_desc_t &command, const command_args_t &args, command_result_t result ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.pCommandHistory != nullptr ) { EditorCommandHistory_Record( mason.pCommandHistory, command, args, result ); }
    if ( mason.pWindow != nullptr ) { EditorActions_RefreshStates( &mason.actions ); }
}

void UpdateProblemCounts( mason_t &mason )
{
    if ( mason.pStatusWarnings == nullptr ) { return; }
    mason.pStatusWarnings->setText( QString::number( EditorLog_Count( &mason.gui.log, log_level_t::Warning ) ) );
    mason.pStatusErrors->setText(
        QString::number( EditorLog_Count( &mason.gui.log, log_level_t::Error ) + EditorLog_Count( &mason.gui.log, log_level_t::Fatal ) ) );
}

void OnLogChanged( void *pContext, const editor_log_entry_t *pEntry ) noexcept
{
    if ( pEntry == nullptr || pEntry->level >= log_level_t::Warning ) { UpdateProblemCounts( Mason( pContext ) ); }
}

// The start-up report a game console prints: what was built, where it runs,
// what loaded, and how long it took.
void LogStartup( mason_t &mason )
{
#if defined( __clang__ )
    const QString compiler = QStringLiteral( "Clang %1.%2.%3" ).arg( __clang_major__ ).arg( __clang_minor__ ).arg( __clang_patchlevel__ );
#elif defined( __GNUC__ )
    const QString compiler = QStringLiteral( "GCC %1.%2" ).arg( __GNUC__ ).arg( __GNUC_MINOR__ );
#elif defined( _MSC_VER )
    const QString compiler = QStringLiteral( "MSVC %1" ).arg( _MSC_VER );
#else
    const QString compiler = QStringLiteral( "unknown compiler" );
#endif
#if defined( NDEBUG )
    const QString config = QStringLiteral( "Release" );
#else
    const QString config = QStringLiteral( "Debug" );
#endif
    LogText( log_level_t::Info, QStringLiteral( "Mason - CypherEngine level editor" ) );
    LogText( log_level_t::Info, QStringLiteral( "Build: %1, %2, C++20, Qt %3" ).arg( config, compiler, QString::fromLatin1( qVersion() ) ) );
    LogText( log_level_t::Info, QStringLiteral( "Platform: %1 (%2)" ).arg( QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture() ) );
    LogText( log_level_t::Info, QStringLiteral( "Commands: %1 registered" ).arg( EditorCommands_Count( &mason.gui.commands ) ) );
    LogText( log_level_t::Info, QStringLiteral( "Settings: %1 registered; user settings %2" )
                                    .arg( EditorSettings_Count( &mason.gui.settings ) )
                                    .arg( mason.bUserSettingsOnDisk ? QString::fromUtf8( TextBuffer_CStr( &mason.userSettings.path ) ) : QStringLiteral( "in memory" ) ) );
    LogText( log_level_t::Info, QStringLiteral( "Themes: %1 user, active \"%2\"; keymaps: %3 user, active \"%4\"" )
                                    .arg( mason.gui.themes.nCount )
                                    .arg( QString::fromUtf8( EditorGui_ActiveThemeId( &mason.gui ).pData,
                                                             static_cast<qsizetype>( EditorGui_ActiveThemeId( &mason.gui ).cchLength ) ) )
                                    .arg( mason.gui.keymaps.nCount )
                                    .arg( QString::fromUtf8( EditorGui_ActiveKeymapId( &mason.gui ).pData,
                                                             static_cast<qsizetype>( EditorGui_ActiveKeymapId( &mason.gui ).cchLength ) ) ) );
    LogText( log_level_t::Info, QStringLiteral( "Map workspace: %1, grid %2" ).arg( MapWorkspace_DisplayName( &mason.map ) ).arg( mason.map.gridSize ) );
    LogText( log_level_t::Info, QStringLiteral( "Panels: %1 registered, layout \"mason_default\"" ).arg( mason.docking.panels.size() ) );
    LogText( log_level_t::Info, QStringLiteral( "Ready in %1 ms" ).arg( mason.gui.log.clock.elapsed() ) );
}

// Toolbar icon sizes come from the theme (ui.icon_size, and
// ui.tool_strip.icon_size for the palette), so a theme can make the editor
// denser or roomier.
void ApplyToolbarMetrics( mason_t &mason )
{
    const int toolbar = static_cast<int>( EditorStyle_Metric( mason.gui.style, "ui.icon_size", 28.0 ) );
    const int palette = static_cast<int>( EditorStyle_Metric( mason.gui.style, "ui.tool_strip.icon_size", 32.0 ) );
    for ( QToolBar *pToolBar : mason.pWindow->findChildren<QToolBar *>() ) {
        const int edge = pToolBar->objectName() == QLatin1String( "EditorToolStrip" ) ? palette : toolbar;
        pToolBar->setIconSize( QSize( edge, edge ) );
        if ( pToolBar->objectName() == QLatin1String( "masonUtilityTools" ) ) {
            for ( auto *button : pToolBar->findChildren<QToolButton *>() ) {
                if ( button->property( "masonUtilityControl" ).toBool() ) { button->setIconSize( QSize( edge, edge ) ); }
            }
        }
    }
}

CYPHER_NODISCARD bool BuildWindow( mason_t &mason )
{
    mason_window_t *pWindow = mason.pWindow;
    pWindow->setObjectName( QStringLiteral( "masonMainWindow" ) );
    pWindow->setWindowIcon( Mason_ApplicationIcon() );

    // Keep File/Edit/etc. inside the editor by default, including macOS.
    pWindow->menuBar()->setNativeMenuBar( EditorSettings_Bool( &mason.gui.settings, "editor.ui.native_menu_bar", CY_FALSE ) );
    pWindow->menuBar()->setObjectName( QStringLiteral( "masonMenuBar" ) );
    EditorActions_Init( &mason.actions, &mason.gui.commands, &mason.gui.style, pWindow );
    mason.pPalette = EditorCommandPalette_Create( pWindow, &mason.actions, &mason.gui.settings );
    usize nSkipped = EditorActions_BuildMenus( &mason.actions, pWindow->menuBar(), kMenus, std::size( kMenus ) );
    AddRecentMenu( mason );

    const auto menu = [pWindow]( const char *path ) {
        return pWindow->menuBar()->findChild<QMenu *>( QStringLiteral( "menu:" ) + QString::fromLatin1( path ) );
    };
    const auto addMenuButton = []( QToolBar *pBar, const char *name, const char *label, QMenu *pMenu ) {
        auto *pButton = new QToolButton( pBar );
        pButton->setObjectName( QString::fromLatin1( name ) );
        pButton->setProperty( "toolbarGroup", true );
        pButton->setText( QString::fromLatin1( label ) );
        pButton->setAccessibleName( QString::fromLatin1( label ) + QStringLiteral( " options" ) );
        pButton->setToolTip( QString::fromLatin1( label ) + QStringLiteral( " options — click to show all commands" ) );
        pButton->setPopupMode( QToolButton::InstantPopup );
        pButton->setMenu( pMenu );
        pButton->setToolButtonStyle( Qt::ToolButtonTextOnly );
        pBar->addWidget( pButton );
    };

    QToolBar *pMain = pWindow->addToolBar( QStringLiteral( "Main" ) );
    pMain->setObjectName( QStringLiteral( "masonMainToolBar" ) );
    pMain->setProperty( "cypherToolbarId", QStringLiteral( "mason.main" ) );
    pMain->setMovable( false ); // The TileEditor's fixed toolbars; layouts place them.
    nSkipped += EditorActions_BuildToolBar( &mason.actions, pMain, kMainToolBar, std::size( kMainToolBar ) );
    pMain->addWidget( new QLabel( QStringLiteral( "Grid " ), pMain ) );
    mason.pGridCombo = new QComboBox( pMain );
    mason.pGridCombo->setObjectName( QStringLiteral( "masonGridSize" ) );
    mason.pGridCombo->setToolTip( QStringLiteral( "Grid size in units ([ and ] halve and double it)" ) );
    for ( const double size : kGridSizes ) { mason.pGridCombo->addItem( QStringLiteral( "%1 u" ).arg( size ), size ); }
    QObject::connect( mason.pGridCombo, qOverload<int>( &QComboBox::currentIndexChanged ), pWindow, [pMason = &mason]( int ) {
        MapWorkspace_SetGridSize( &pMason->map, pMason->pGridCombo->currentData().toDouble() );
    } );
    pMain->addWidget( mason.pGridCombo );
    nSkipped += EditorActions_BuildToolBar( &mason.actions, pMain, kMainToolBarEditing, std::size( kMainToolBarEditing ) );
    // The primary click toggles the preference; the split arrow chooses its
    // step. Both status and toolbar controls share the registered action.
    const auto snapMenu = [&]( const char *name, const char *command, bool angle ) {
        auto *pMenu = new QMenu( angle ? QStringLiteral( "Angle Snap Step" ) : QStringLiteral( "Scale Snap Step" ), pWindow );
        pMenu->setObjectName( QString::fromLatin1( name ) );
        auto *pCurrent = pMenu->addAction( QString() );
        pCurrent->setEnabled( false );
        pMenu->addSeparator();
        auto *pGroup = new QActionGroup( pMenu );
        pGroup->setExclusive( true );
        for ( double step : angle ? kAngleSnaps : kScaleSnaps ) {
            auto *pStep = pMenu->addAction( step == 0.0 ? QStringLiteral( "Off" ) :
                ( angle ? QStringLiteral( "%1\u00B0" ).arg( step ) : QStringLiteral( "%1%" ).arg( step * 100.0 ) ) );
            pStep->setData( step );
            pStep->setCheckable( true );
            pGroup->addAction( pStep );
            QObject::connect( pStep, &QAction::triggered, pWindow, [pMason = &mason, angle, step]() {
                if ( angle ) { MapWorkspace_SetAngleSnap( &pMason->map, step ); }
                else { MapWorkspace_SetScaleSnap( &pMason->map, step ); }
            } );
        }
        QObject::connect( pMenu, &QMenu::aboutToShow, pWindow, [pMason = &mason, pGroup, pCurrent, angle]() {
            const double value = angle ? pMason->map.angleSnap : pMason->map.scaleSnap;
            pCurrent->setText( value == 0.0 ? QStringLiteral( "Snapping off" ) :
                ( angle ? QStringLiteral( "Current step: %1\u00B0" ).arg( value ) : QStringLiteral( "Current step: %1%" ).arg( value * 100.0 ) ) );
            for ( auto *pAction : pGroup->actions() ) { pAction->setChecked( pAction->data().toDouble() == value ); }
        } );
        if ( auto *pButton = qobject_cast<QToolButton *>( pMain->widgetForAction( EditorActions_Get( &mason.actions, command ) ) ) ) {
            pButton->setMenu( pMenu );
            pButton->setPopupMode( QToolButton::MenuButtonPopup );
        }
        return pMenu;
    };
    QMenu *pAngleMenu = snapMenu( "masonAngleSnapMenu", "map.grid.angle_snap", true );
    QMenu *pScaleMenu = snapMenu( "masonScaleSnapMenu", "map.grid.scale_snap", false );

    auto *pSearch = new QToolButton( pMain );
    pSearch->setObjectName( QStringLiteral( "masonCommandSearch" ) );
    pSearch->setDefaultAction( EditorActions_Get( &mason.actions, "view.command_palette" ) );
    pSearch->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );

    // Hammer 5's "Select:" row: the selection modes, named.
    QToolBar *pSelect = pWindow->addToolBar( QStringLiteral( "Selection Modes" ) );
    pSelect->setObjectName( QStringLiteral( "masonSelectModes" ) );
    pSelect->setProperty( "cypherToolbarId", QStringLiteral( "map.select" ) );
    pSelect->setMovable( false );
    pSelect->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    addMenuButton( pSelect, "masonSelectionOptions", "Select", menu( "&Selection" ) );
    nSkipped += EditorActions_BuildToolBar( &mason.actions, pSelect, kSelectModes, std::size( kSelectModes ) );

    QToolBar *pEditing = pWindow->addToolBar( QStringLiteral( "Editing" ) );
    pEditing->setObjectName( QStringLiteral( "masonEditingTools" ) );
    pEditing->setProperty( "cypherToolbarId", QStringLiteral( "map.edit" ) );
    pEditing->setMovable( false );
    auto *pEditingMenu = new QMenu( QStringLiteral( "Editing" ), pEditing );
    pEditingMenu->setObjectName( QStringLiteral( "masonEditingMenu" ) );
    nSkipped += EditorActions_FillMenu( &mason.actions, pEditingMenu, kEditingTools, std::size( kEditingTools ) );
    addMenuButton( pEditing, "masonEditingOptions", "Editing", pEditingMenu );
    nSkipped += EditorActions_BuildToolBar( &mason.actions, pEditing, kEditingTools, std::size( kEditingTools ) );

    QToolBar *pView = pWindow->addToolBar( QStringLiteral( "View Filters" ) );
    pView->setObjectName( QStringLiteral( "masonViewFilters" ) );
    pView->setProperty( "cypherToolbarId", QStringLiteral( "map.view" ) );
    pView->setMovable( false );
    auto *pFilters = new QMenu( QStringLiteral( "View Filters" ), pView );
    pFilters->setObjectName( QStringLiteral( "masonViewFiltersMenu" ) );
    nSkipped += EditorActions_FillMenu( &mason.actions, pFilters, kViewFilters, std::size( kViewFilters ) );
    pFilters->addSeparator();
    constexpr const char *visibility[]{ "map.hide.selected", "map.hide.unselected", "map.hide.show_all" };
    nSkipped += EditorActions_FillMenu( &mason.actions, pFilters, visibility, std::size( visibility ) );
    QObject::connect( pFilters, &QMenu::aboutToShow, pWindow, [pMason = &mason]() { EditorActions_RefreshStates( &pMason->actions ); } );
    addMenuButton( pView, "masonVisibilityOptions", "View", pFilters );
    nSkipped += EditorActions_BuildToolBar( &mason.actions, pView, kViewFilters, std::size( kViewFilters ) );
    auto *pSpacer = new QWidget( pView );
    pSpacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
    pView->addWidget( pSpacer );

    // Keep these utilities together at the far right while the other toolbars
    // use their overflow menus on small windows.
    auto *pUtilities = new QToolBar( QStringLiteral( "Search and Settings" ), pWindow );
    pUtilities->setObjectName( QStringLiteral( "masonUtilityTools" ) );
    pUtilities->setProperty( "cypherToolbarId", QStringLiteral( "map.utilities" ) );
    pUtilities->setMovable( false );
    pWindow->addToolBar( pUtilities );
    // A single widget action keeps this group intact. Separate toolbar
    // actions let Qt overflow sound/settings while showing only the field.
    auto *pUtilityGroup = new QWidget( pUtilities );
    pUtilityGroup->setObjectName( QStringLiteral( "MasonUtilityGroup" ) );
    auto *pUtilityLayout = new QHBoxLayout( pUtilityGroup );
    pUtilityLayout->setContentsMargins( 0, 0, 0, 0 );
    pUtilityLayout->setSpacing( 2 );
    pUtilityLayout->setAlignment( Qt::AlignRight );
    pUtilityLayout->setSizeConstraint( QLayout::SetMinimumSize );
    auto *pFastSearch = new QLineEdit( pUtilityGroup );
    pFastSearch->setObjectName( QStringLiteral( "MasonFastAssetSearch" ) );
    pFastSearch->setPlaceholderText( QStringLiteral( "Fast Asset Search" ) );
    pFastSearch->setAccessibleName( QStringLiteral( "Fast Asset Search" ) );
    pFastSearch->setToolTip( QStringLiteral( "Search all asset types and folders. Enter focuses the first result." ) );
    pFastSearch->setClearButtonEnabled( true );
    pFastSearch->setMinimumWidth( 150 );
    pFastSearch->setMaximumWidth( 230 );
    pUtilityLayout->addWidget( pFastSearch );
    for ( const char *command : { "view.sound_preview", "tools.settings" } ) {
        auto *button = new QToolButton( pUtilityGroup );
        button->setObjectName( QString::fromLatin1( command ) + QStringLiteral( ".utility" ) );
        button->setProperty( "masonUtilityControl", true );
        button->setDefaultAction( EditorActions_Get( &mason.actions, command ) );
        button->setToolButtonStyle( Qt::ToolButtonIconOnly );
        pUtilityLayout->addWidget( button );
    }
    pSearch->setProperty( "masonUtilityControl", true );
    pUtilityLayout->addWidget( pSearch );
    pUtilities->addWidget( pUtilityGroup );
    // Fast Asset Search feeds the Asset Browser window (Hammer keeps assets
    // out of the docks): typing shows matches there while the keys stay in
    // the field; Enter moves into the results.
    QObject::connect( pFastSearch, &QLineEdit::textEdited, pWindow, [pMason = &mason, pFastSearch]( const QString &text ) {
        QDialog *pAssetWindow = AssetWindow( *pMason );
        if ( pAssetWindow == nullptr ) { return; }
        if ( !pAssetWindow->isVisible() ) {
            pAssetWindow->setAttribute( Qt::WA_ShowWithoutActivating, true );
            pAssetWindow->show();
            pAssetWindow->setAttribute( Qt::WA_ShowWithoutActivating, false );
        }
        if ( !EditorAssetWindow_IsPicking( pAssetWindow ) ) { EditorAssetWindow_SetTab( pAssetWindow, ASSET_WINDOW_TAB_ALL ); }
        EditorAssetWindow_SetFilter( pAssetWindow, text );
        pMason->pWindow->activateWindow();
        pFastSearch->setFocus();
    } );
    QObject::connect( pFastSearch, &QLineEdit::returnPressed, pWindow, [pMason = &mason, pFastSearch]() {
        QDialog *pAssetWindow = AssetWindow( *pMason );
        if ( pAssetWindow == nullptr ) { return; }
        EditorAssetWindow_SetFilter( pAssetWindow, pFastSearch->text() );
        pAssetWindow->show();
        pAssetWindow->raise();
        pAssetWindow->activateWindow();
        EditorAssetWindow_FocusResults( pAssetWindow );
    } );

    // Surface and topology operations have a fixed, discoverable home. These
    // use the same command actions as the menus, including availability.
    QToolBar *pGeometry = pWindow->addToolBar( QStringLiteral( "Geometry and Surfaces" ) );
    pGeometry->setObjectName( QStringLiteral( "masonGeometryTools" ) );
    pGeometry->setProperty( "cypherToolbarId", QStringLiteral( "map.geometry" ) );
    pGeometry->setMovable( false );
    const auto addGroup = [&]( const char *name, const char *label, QMenu *pMenu, std::initializer_list<const char *> commands ) {
        addMenuButton( pGeometry, name, label, pMenu );
        for ( const char *command : commands ) {
            if ( QAction *pAction = EditorActions_Get( &mason.actions, command ) ) { pGeometry->addAction( pAction ); }
            else { ++nSkipped; }
        }
    };
    addGroup( "masonMeshOptions", "Mesh", menu( "&Mesh" ), { "map.mesh.extrude", "map.mesh.inset", "map.mesh.bevel", "map.mesh.bridge",
                        "map.mesh.merge", "map.mesh.dissolve", "map.mesh.split", "map.mesh.quad_slice", "map.mesh.subdivide", "map.mesh.solidify" } );
    pGeometry->addSeparator();
    auto *pBooleanMenu = new QMenu( QStringLiteral( "CSG" ), pGeometry );
    pBooleanMenu->setObjectName( QStringLiteral( "masonBooleanMenu" ) );
    constexpr const char *booleans[]{ "map.mesh.boolean_union", "map.mesh.boolean_subtract", "map.mesh.boolean_intersect" };
    nSkipped += EditorActions_FillMenu( &mason.actions, pBooleanMenu, booleans, std::size( booleans ) );
    addGroup( "masonBooleanOptions", "CSG", pBooleanMenu, { "map.mesh.boolean_union", "map.mesh.boolean_subtract", "map.mesh.boolean_intersect" } );
    pGeometry->addSeparator();
    addGroup( "masonSurfaceOptions", "Surface", menu( "&Texture" ), { "map.texture.fit", "map.texture.align_world", "map.texture.align_face",
                           "map.texture.rotate", "map.texture.scale", "map.texture.shift", "map.texture.unwrap" } );

    auto *pTools = new QToolBar( QStringLiteral( "Tools" ), pWindow );
    pTools->setObjectName( QStringLiteral( "EditorToolStrip" ) );
    pTools->setProperty( "cypherToolbarId", QStringLiteral( "map.tools" ) );
    pTools->setOrientation( Qt::Vertical );
    pTools->setMovable( false );
    pWindow->addToolBar( Qt::LeftToolBarArea, pTools );
    nSkipped += EditorActions_BuildToolPalette( &mason.actions, pTools, kToolPalette, std::size( kToolPalette ), 2 );
    pWindow->InstallToolCancelRouting();

    // Qt persists toolbar visibility with the rest of the workspace. The
    // Window menu also makes a hidden toolbar recoverable without a reset.
    for ( QAction *pMenuAction : pWindow->menuBar()->actions() ) {
        QMenu *pMenu = pMenuAction->menu();
        if ( pMenu == nullptr || pMenu->title() != QStringLiteral( "&Window" ) ) { continue; }
        pMenu->addSeparator();
        auto *pToolbars = pMenu->addMenu( QStringLiteral( "Toolbars" ) );
        pToolbars->setObjectName( QStringLiteral( "masonToolbarMenu" ) );
        for ( QToolBar *pToolbar : { pMain, pSelect, pEditing, pView, pUtilities, pGeometry, pTools } ) {
            pToolbars->addAction( pToolbar->toggleViewAction() );
        }
    }
    ApplyToolbarMetrics( mason );
    if ( nSkipped != 0u ) { CY_LOG_WRITE( Warning, Editor, "Some menu or toolbar entries name commands that do not exist" ); }

    // Every command is registered by now, so the keymap sees all of them.
    // The map context is always active while the map workspace is the
    // document; viewport and tool contexts dispatch in the views.
    const string_view_t contexts[]{ StringView_FromCString( "map" ), StringView_FromCString( EDITOR_KEYMAP_CONTEXT_GLOBAL ) };
    EditorActions_ApplyKeymapStack( &mason.actions, mason.gui.keymapChain, mason.gui.nKeymapChain, EditorKeymap_HostPlatform(), contexts,
                                    std::size( contexts ) );

    QStatusBar *pStatus = pWindow->statusBar();
    // Compact panel switches stay available after a dock is closed, and share
    // the same actions as Window/View menus (including their checked state).
    auto *pPanelSwitches = new QWidget( pStatus );
    pPanelSwitches->setObjectName( QStringLiteral( "MasonPanelSwitches" ) );
    auto *pPanelLayout = new QHBoxLayout( pPanelSwitches );
    pPanelLayout->setContentsMargins( 2, 0, 6, 0 );
    pPanelLayout->setSpacing( 2 );
    for ( const char *pId : { "view.tool_properties", "view.active_material", "view.assets", "view.outliner", "view.properties", "view.history" } ) {
        auto *pButton = new QToolButton( pPanelSwitches );
        pButton->setObjectName( QString::fromLatin1( pId ) + QStringLiteral( ".status" ) );
        pButton->setDefaultAction( EditorActions_Get( &mason.actions, pId ) );
        pButton->setToolButtonStyle( Qt::ToolButtonIconOnly );
        pButton->setAutoRaise( true );
        // Sized from the icon, with the compact padding rule: a fixed box
        // with density-scaled padding left no room for the icon at Comfortable.
        pButton->setProperty( "compactIcon", true );
        pButton->setIconSize( QSize( 18, 18 ) );
        pButton->setFixedSize( 18 + 8, 18 + 8 );
        pButton->setFocusPolicy( Qt::NoFocus );
        pPanelLayout->addWidget( pButton );
    }
    pStatus->addPermanentWidget( pPanelSwitches );
    mason.pStatusTool = new QLabel( pStatus );
    mason.pStatusSelection = new QLabel( pStatus );
    mason.pStatusSize = new QLabel( pStatus );
    mason.pStatusSize->setObjectName( QStringLiteral( "masonStatusSize" ) );
    mason.pStatusSize->setToolTip( QStringLiteral( "Live edit or selection size (width, length, height) and centre" ) );
    mason.pStatusCursor = new QLabel( pStatus );
    mason.pStatusGrid = new QLabel( pStatus );
    mason.pStatusCursor->setMinimumWidth( 180 );
    pStatus->addPermanentWidget( mason.pStatusTool );
    pStatus->addPermanentWidget( mason.pStatusSelection );
    pStatus->addPermanentWidget( mason.pStatusSize );
    pStatus->addPermanentWidget( mason.pStatusCursor );
    pStatus->addPermanentWidget( mason.pStatusGrid );
    // Hammer 5 keeps snapping at the right of the status bar.
    auto *pSnap = new QToolButton( pStatus );
    pSnap->setObjectName( QStringLiteral( "masonSnapToggle" ) );
    pSnap->setDefaultAction( EditorActions_Get( &mason.actions, "map.grid.snap" ) );
    pSnap->setAutoRaise( true );
    pSnap->setIconSize( QSize( 16, 16 ) );
    auto *pSnapLabel = new QLabel( QStringLiteral( "Snap:" ), pStatus );
    pStatus->addPermanentWidget( pSnapLabel );
    pStatus->addPermanentWidget( pSnap );
    for ( const auto &[command, pMenu] : { std::pair{ "map.grid.angle_snap", pAngleMenu }, std::pair{ "map.grid.scale_snap", pScaleMenu } } ) {
        auto *pButton = new QToolButton( pStatus );
        pButton->setObjectName( QString::fromLatin1( command ) + QStringLiteral( ".status" ) );
        pButton->setDefaultAction( EditorActions_Get( &mason.actions, command ) );
        pButton->setMenu( pMenu );
        pButton->setPopupMode( QToolButton::MenuButtonPopup );
        pButton->setIconSize( QSize( 20, 20 ) );
        pStatus->addPermanentWidget( pButton );
    }
    mason.pAngleCombo = new QComboBox( pStatus );
    mason.pAngleCombo->setObjectName( QStringLiteral( "masonAngleSnap" ) );
    mason.pAngleCombo->setToolTip( QStringLiteral( "Rotation snap" ) );
    for ( const double angle : kAngleSnaps ) {
        mason.pAngleCombo->addItem( angle == 0.0 ? QStringLiteral( "Off" ) : QStringLiteral( "%1\u00B0" ).arg( angle ), angle );
    }
    QObject::connect( mason.pAngleCombo, qOverload<int>( &QComboBox::currentIndexChanged ), pWindow, [pMason = &mason]( int ) {
        MapWorkspace_SetAngleSnap( &pMason->map, pMason->pAngleCombo->currentData().toDouble() );
    } );
    pStatus->addPermanentWidget( new QLabel( QStringLiteral( "Angle:" ), pStatus ) );
    pStatus->addPermanentWidget( mason.pAngleCombo );
    mason.pScaleCombo = new QComboBox( pStatus );
    mason.pScaleCombo->setObjectName( QStringLiteral( "masonScaleSnap" ) );
    mason.pScaleCombo->setToolTip( QStringLiteral( "Scale snap step — independent of grid and angle snap" ) );
    for ( double step : kScaleSnaps ) {
        mason.pScaleCombo->addItem( step == 0.0 ? QStringLiteral( "Off" ) : QStringLiteral( "%1%" ).arg( step * 100.0 ), step );
    }
    QObject::connect( mason.pScaleCombo, qOverload<int>( &QComboBox::currentIndexChanged ), pWindow, [pMason = &mason]( int ) {
        MapWorkspace_SetScaleSnap( &pMason->map, pMason->pScaleCombo->currentData().toDouble() );
    } );
    pStatus->addPermanentWidget( new QLabel( QStringLiteral( "Scale:" ), pStatus ) );
    pStatus->addPermanentWidget( mason.pScaleCombo );
    // Warning and error counts, as an IDE shows them; either opens Problems.
    for ( QToolButton **ppButton : { &mason.pStatusWarnings, &mason.pStatusErrors } ) {
        *ppButton = new QToolButton( pStatus );
        ( *ppButton )->setAutoRaise( true );
        ( *ppButton )->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        ( *ppButton )->setIconSize( QSize( 14, 14 ) );
        ( *ppButton )->setToolTip( QStringLiteral( "Show the Problems panel" ) );
        QObject::connect( *ppButton, &QToolButton::clicked, pWindow, [pMason = &mason]() {
            EditorDocking_SetPanelVisible( &pMason->docking, QString::fromLatin1( MASON_PANEL_PROBLEMS ), true );
            if ( ads::CDockWidget *pDock = EditorDocking_Dock( &pMason->docking, QString::fromLatin1( MASON_PANEL_PROBLEMS ) ) ) { pDock->raise(); }
        } );
        pStatus->addPermanentWidget( *ppButton );
    }
    mason.pStatusWarnings->setObjectName( QStringLiteral( "masonStatusWarnings" ) );
    mason.pStatusErrors->setObjectName( QStringLiteral( "masonStatusErrors" ) );
    mason.pStatusWarnings->setIcon( EditorStyle_Icon( mason.gui.style, "log-warning" ) );
    mason.pStatusErrors->setIcon( EditorStyle_Icon( mason.gui.style, "log-error" ) );
    UpdateProblemCounts( mason );
    ( void )EditorLog_AddListener( &mason.gui.log, &OnLogChanged, &mason );

    mason.pCommandHistory = EditorCommandHistory_Create( pWindow, &mason.gui );
    mason.pCommandHistory->hide();
    ( void )CreateAssets( &mason, pWindow );
    mason.pAssets->hide(); // Shared catalogue source for the standalone windows.
    EditorDocking_Init( &mason.docking, pWindow, MASON_PANEL_VIEWS );
    mason.docking.bUppercaseTitles = false; // Keep narrow tab groups readable.
    const editor_panel_desc_t panels[]{
        { MASON_PANEL_VIEWS, "Views", CreateViews, &mason },
        { MASON_PANEL_CONSOLE, "Console", CreateConsole, &mason },
        { MASON_PANEL_OUTPUT, "Output", CreateOutput, &mason },
        { MASON_PANEL_PROBLEMS, "Problems", CreateProblems, &mason },
        { MAP_PANEL_OUTLINER, "Outliner", CreateOutliner, &mason },
        { MAP_PANEL_PROPERTIES, "Object Properties", CreateProperties, &mason },
        { MAP_PANEL_TOOL_PROPERTIES, "Tool Properties", CreateToolProperties, &mason },
        { MAP_PANEL_ACTIVE_MATERIAL, "Active Material", CreateActiveMaterial, &mason },
        { MAP_PANEL_VISGROUPS, "Auto Vis Groups", CreateVisgroups, &mason }, // Hammer 5's name.
        { MAP_PANEL_SELECTION_SETS, "Selection Sets", CreateSelectionSets, &mason },
        { MASON_PANEL_HISTORY, "Undo History", CreateHistory, &mason },
        { MASON_PANEL_COMMAND_HISTORY, "Command History", CreateCommandHistory, &mason },
    };
    if ( !EditorDocking_RegisterPanels( &mason.docking, panels, std::size( panels ) ) ) { return false; }
    if ( EditorLayout_Init( &mason.defaultLayout, mason.gui.pAllocator ) != layout_status_t::OK ||
         EditorGui_LoadLayoutResource( MASON_DEFAULT_LAYOUT_RESOURCE, &mason.defaultLayout, mason.gui.pAllocator ) != editor_gui_status_t::OK ) {
        CY_LOG_WRITE( Error, Editor, "Mason's default layout could not be loaded" );
        return false;
    }
    ApplyDefaultLayout( mason );
    if ( auto *pSession = pWindow->findChild<QToolButton *>( QStringLiteral( "masonSessionOptions" ) ) ) {
        const char *sessionCommands[]{ "view.layout.modeling", "view.layout.four", "view.layout.two", "view.layout.single", nullptr,
                                      "view.layout.save", "view.layout.restore", "view.layout.reset", nullptr, "view.history", "view.assets", "view.console" };
        nSkipped += EditorActions_FillMenu( &mason.actions, pSession->menu(), sessionCommands, std::size( sessionCommands ) );
        QObject::connect( pSession->menu(), &QMenu::aboutToShow, pWindow, [pMason = &mason]() { EditorActions_RefreshStates( &pMason->actions ); } );
    }
    // Dock toggles and the panels' own close buttons change visibility too.
    for ( ads::CDockWidget *pDock : mason.docking.docks ) {
        QObject::connect( pDock, &ads::CDockWidget::viewToggled, pWindow, [pMason = &mason]( bool ) {
            EditorActions_RefreshStates( &pMason->actions );
        } );
    }

    if ( !MapWorkspace_AddListener( &mason.map, &OnMapChanged, &mason ) ) { return false; }
    QObject::connect( QApplication::clipboard(), &QClipboard::dataChanged, pWindow, [pMason = &mason]() {
        EditorActions_RefreshStates( &pMason->actions );
    } );
    QObject::connect( mason.pApplication, &QApplication::focusChanged, pWindow, [pMason = &mason]( QWidget *, QWidget * ) {
        EditorActions_RefreshStates( &pMason->actions );
    } );
    EditorCommands_SetObserver( &mason.gui.commands, &OnCommandExecuted, &mason );
    UpdateTitle( mason );
    UpdateStatus( mason, MAP_CHANGE_VIEW | MAP_CHANGE_SELECTION );
    EditorActions_RefreshStates( &mason.actions );
    return true;
}

// editor.ui.theme names the active theme. A theme that is not installed
// keeps the current one and says so; the setting is left alone, so the
// theme comes back once its file does.
void ApplyThemeSetting( mason_t &mason )
{
    const string_view_t wanted = EditorSettings_Text( &mason.gui.settings, "editor.ui.theme", string_view_t{} );
    if ( wanted.cchLength == 0u || StringView_Equals( wanted, EditorGui_ActiveThemeId( &mason.gui ) ) ) { return; }
    const editor_gui_status_t status = EditorGui_SelectTheme( &mason.gui, mason.pApplication, wanted );
    if ( status != editor_gui_status_t::OK ) {
        LogText( log_level_t::Warning, QStringLiteral( "Theme \"%1\" is not installed; keeping the current theme" )
                                           .arg( QString::fromUtf8( wanted.pData, static_cast<qsizetype>( wanted.cchLength ) ) ) );
    }
}

void ApplyKeymapSetting( mason_t &mason )
{
    const auto wanted = EditorSettings_Text( &mason.gui.settings, "editor.ui.keymap", {} );
    if ( wanted.cchLength == 0u || StringView_Equals( wanted, EditorGui_ActiveKeymapId( &mason.gui ) ) ) { return; }
    if ( EditorGui_SelectKeymap( &mason.gui, wanted ) != editor_gui_status_t::OK ) {
        LogText( log_level_t::Warning, QStringLiteral( "Keymap \"%1\" is not installed; keeping the current keymap" )
                                      .arg( QString::fromUtf8( wanted.pData, static_cast<qsizetype>( wanted.cchLength ) ) ) );
    }
}

// Settings are saved shortly after they change, so a burst of edits (a
// slider, several [ presses) writes the file once.
void OnSettingsChanged( void *pContext, string_view_t path ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.bUserSettingsOnDisk && mason.pSettingsSaveTimer != nullptr ) { mason.pSettingsSaveTimer->start(); }
    if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.ui.theme" ) ) ) { ApplyThemeSetting( mason ); }
    if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.ui.keymap" ) ) ) { ApplyKeymapSetting( mason ); }
    if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.assets.roots" ) ) ) { UpdateAssetRoots( mason ); }
    if ( mason.pWindow != nullptr && ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.ui.native_menu_bar" ) ) ) ) {
        mason.pWindow->menuBar()->setNativeMenuBar( EditorSettings_Bool( &mason.gui.settings, "editor.ui.native_menu_bar", CY_FALSE ) );
    }
    if ( mason.pWindow != nullptr && ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.ui.two_toolbar_rows" ) ) ) ) {
        ApplyToolbarRows( mason );
    }
    if ( mason.pWindow != nullptr && ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.audio.preview_enabled" ) ) ) ) {
        EditorActions_RefreshStates( &mason.actions );
    }
}

// Icons are tinted from the theme, and the views paint with theme colours,
// so both follow every style change (a selected theme or a live preview).
void OnStyleChanged( void *pContext ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.pWindow == nullptr ) { return; }
    EditorActions_RefreshIcons( &mason.actions );
    ApplyToolbarMetrics( mason );
    if ( mason.pHistory != nullptr ) { EditorHistoryPanel_Refresh( mason.pHistory ); }
    if ( QWidget *pViews = mason.docking.pCentral ) {
        pViews->update();
        for ( QWidget *pChild : pViews->findChildren<QWidget *>() ) { pChild->update(); }
    }
}

void OnKeymapChanged( void *pContext ) noexcept
{
    mason_t &mason = Mason( pContext );
    if ( mason.pWindow == nullptr ) { return; }
    const string_view_t contexts[]{ StringView_FromCString( "map" ), StringView_FromCString( EDITOR_KEYMAP_CONTEXT_GLOBAL ) };
    EditorActions_ApplyKeymapStack( &mason.actions, mason.gui.keymapChain, mason.gui.nKeymapChain, EditorKeymap_HostPlatform(), contexts,
                                    std::size( contexts ) );
}

// User themes live beside the settings file. Headless runs start from an
// empty scratch folder, so tests see only the built-in theme and never touch
// the user's themes.
void LoadUserThemes( mason_t &mason )
{
    if ( IsHeadless( mason ) ) {
        mason.themeFolder = mason.headlessScratch.isValid() ? mason.headlessScratch.filePath( QStringLiteral( "themes" ) ) : QString();
        return;
    }
    mason.themeFolder = QDir( QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ) ).filePath( QStringLiteral( "themes" ) );
    const usize nLoaded = EditorGui_LoadThemeFolder( &mason.gui, mason.themeFolder );
    if ( nLoaded != 0u ) { LogText( log_level_t::Info, QStringLiteral( "%1 user themes loaded from %2" ).arg( nLoaded ).arg( mason.themeFolder ) ); }
}

void LoadUserKeymaps( mason_t &mason )
{
    if ( IsHeadless( mason ) ) {
        mason.keymapFolder = mason.headlessScratch.isValid() ? mason.headlessScratch.filePath( QStringLiteral( "keymaps" ) ) : QString();
        return;
    }
    mason.keymapFolder = QDir( QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ) ).filePath( QStringLiteral( "keymaps" ) );
    const usize nLoaded = EditorGui_LoadKeymapFolder( &mason.gui, mason.keymapFolder );
    if ( nLoaded != 0u ) { LogText( log_level_t::Info, QStringLiteral( "%1 user keymaps loaded from %2" ).arg( nLoaded ).arg( mason.keymapFolder ) ); }
}

void SaveUserSettings( mason_t &mason )
{
    if ( !mason.bUserSettingsOnDisk ) { return; }
    const settings_file_status_t status = EditorSettingsFile_Save( &mason.userSettings );
    if ( status != settings_file_status_t::OK ) {
        LogText( log_level_t::Warning, QStringLiteral( "Settings were not saved: %1" ).arg( QString::fromUtf8( EditorSettingsFile_StatusName( status ) ) ) );
    }
}

// The user scope: a file under the platform's config folder, or - headless,
// for tests - the same store kept in memory only.
CYPHER_NODISCARD bool AttachUserSettings( mason_t &mason )
{
    const bool bHeadless = IsHeadless( mason );
    mason.workspaceStatePath = bHeadless ? mason.headlessScratch.filePath( QStringLiteral( "workspace.json" ) ) :
        QDir( QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ) ).filePath( QStringLiteral( "workspace.json" ) );
    QString path = QStringLiteral( "headless.cysettings" );
    if ( !bHeadless ) {
        const QString folder = QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation );
        if ( !QDir().mkpath( folder ) ) { LogText( log_level_t::Warning, QStringLiteral( "Settings folder could not be created: %1" ).arg( folder ) ); }
        path = QDir( folder ).filePath( QStringLiteral( "editor.cysettings" ) );
    }
    const QByteArray utf8 = path.toUtf8();
    if ( EditorSettingsFile_Init( &mason.userSettings, mason.gui.pAllocator, EditorSettings_FileIdentity(),
                                  string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) } ) != settings_file_status_t::OK ) {
        return false;
    }
    if ( !bHeadless ) {
        const settings_file_status_t loaded = EditorSettingsFile_Load( &mason.userSettings );
        if ( loaded == settings_file_status_t::RESTORED_FROM_BACKUP || loaded == settings_file_status_t::UNREADABLE ) {
            LogText( log_level_t::Warning, QStringLiteral( "User settings: %1" ).arg( QString::fromUtf8( EditorSettingsFile_StatusName( loaded ) ) ) );
        }
        mason.bUserSettingsOnDisk = loaded != settings_file_status_t::IO_ERROR && loaded != settings_file_status_t::OUT_OF_MEMORY;
    } else if ( SettingsDocument_Load( &mason.userSettings.store, StringView_FromCString( "@cykv 1\n@schema \"cypher.settings\" 2\n{}\n" ) ).status !=
                settings_document_status_t::OK ) {
        return false;
    }
    EditorSettings_SetScope( &mason.gui.settings, settings_scope_t::USER, &mason.userSettings.store );
    const QString recentPath = bHeadless ? mason.headlessScratch.filePath( QStringLiteral( "recent_maps.txt" ) )
                                         : QDir( QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ) ).filePath( QStringLiteral( "recent_maps.txt" ) );
    MasonRecent_Load( &mason.recent, recentPath, static_cast<int>( EditorSettings_Integer( &mason.gui.settings, "editor.general.recent_files", 16 ) ) );
    return true;
}

} // namespace

mason_t *Mason_Create( QApplication *pApplication, const allocator_t *pAllocator, u32 flags,
                       mason_startup_callback_t pfnStartup, void *pStartupContext )
{
    CY_ASSERT( pApplication != nullptr && pAllocator != nullptr );
    if ( pApplication == nullptr || pAllocator == nullptr ) { return nullptr; }
    auto *pMason = new ( std::nothrow ) mason_t{};
    if ( pMason == nullptr ) { return nullptr; }
    pMason->pApplication = pApplication;
    pMason->flags = flags;
    const editor_gui_status_t guiStatus = EditorGui_Init( &pMason->gui, pApplication, pAllocator );
    if ( guiStatus != editor_gui_status_t::OK ) {
        LogText( log_level_t::Error, QStringLiteral( "Editor framework failed to start: %1" ).arg( QString::fromUtf8( EditorGui_StatusName( guiStatus ) ) ) );
        delete pMason;
        return nullptr;
    }
    // The user scope comes first, so the workspace starts from saved values
    // and the window is built in the saved theme.
    if ( !AttachUserSettings( *pMason ) ) {
        CY_LOG_WRITE( Error, Editor, "User settings could not be prepared" );
        Mason_Destroy( pMason );
        return nullptr;
    }
    LoadUserThemes( *pMason );
    ApplyThemeSetting( *pMason );
    LoadUserKeymaps( *pMason );
    ApplyKeymapSetting( *pMason );
    if ( pfnStartup != nullptr ) { pfnStartup( pStartupContext, pMason->gui, "Preparing map workspace..." ); }
    if ( !MapWorkspace_Init( &pMason->map, &pMason->gui ) || !RegisterCommands( *pMason ) ) {
        CY_LOG_WRITE( Error, Editor, "Map workspace failed to start" );
        Mason_Destroy( pMason );
        return nullptr;
    }
    if ( pfnStartup != nullptr ) { pfnStartup( pStartupContext, pMason->gui, "Building tools and panels..." ); }
    pMason->pWindow = new mason_window_t( pMason );
    pMason->pSettingsSaveTimer = new QTimer( pMason->pWindow );
    pMason->pSettingsSaveTimer->setSingleShot( true );
    pMason->pSettingsSaveTimer->setInterval( 400 );
    QObject::connect( pMason->pSettingsSaveTimer, &QTimer::timeout, pMason->pWindow, [pMason]() { SaveUserSettings( *pMason ); } );
    ( void )EditorSettings_AddListener( &pMason->gui.settings, &OnSettingsChanged, pMason );
    if ( !BuildWindow( *pMason ) || !EditorGui_AddStyleListener( &pMason->gui, &OnStyleChanged, pMason ) ||
         !EditorGui_AddKeymapListener( &pMason->gui, &OnKeymapChanged, pMason ) ) {
        Mason_Destroy( pMason );
        return nullptr;
    }
    if ( pfnStartup != nullptr ) { pfnStartup( pStartupContext, pMason->gui, "Ready." ); }
    LogStartup( *pMason );
    return pMason;
}

void Mason_Destroy( mason_t *pMason )
{
    if ( pMason == nullptr ) { return; }
    // Listeners first: closing panels notifies nobody who is already gone.
    EditorCommands_SetObserver( &pMason->gui.commands, nullptr, nullptr );
    EditorSettings_RemoveListener( &pMason->gui.settings, &OnSettingsChanged, pMason );
    EditorGui_RemoveKeymapListener( &pMason->gui, &OnKeymapChanged, pMason );
    EditorGui_RemoveStyleListener( &pMason->gui, &OnStyleChanged, pMason );
    EditorLog_RemoveListener( &pMason->gui.log, &OnLogChanged, pMason );
    if ( pMason->pSettingsSaveTimer != nullptr && pMason->pSettingsSaveTimer->isActive() ) { SaveUserSettings( *pMason ); }
    MapWorkspace_RemoveListener( &pMason->map, &OnMapChanged, pMason );
    // The browser goes with the window; nothing may ask it for images after.
    pMason->map.pfnMaterialImage = nullptr;
    pMason->map.pMaterialImageContext = nullptr;
    pMason->pCommandHistory = nullptr; // Its dock goes with the window.
    // QWidget child destruction can move focus after window-owned actions have
    // already been deleted. Stop state refresh before that teardown begins;
    // QObject's automatic receiver disconnect happens too late for this case.
    if ( pMason->pWindow != nullptr ) {
        if ( pMason->pApplication != nullptr ) { QObject::disconnect( pMason->pApplication, &QApplication::focusChanged, pMason->pWindow, nullptr ); }
        if ( auto *clipboard = QApplication::clipboard() ) { QObject::disconnect( clipboard, &QClipboard::dataChanged, pMason->pWindow, nullptr ); }
    }
    delete pMason->pWindow;
    pMason->pWindow = nullptr;
    pMason->pAssets = nullptr;
    pMason->pAssetWindow = nullptr; // A child of the window.
    pMason->pDatabase = nullptr;    // So is this.
    EditorLayout_Shutdown( &pMason->defaultLayout );
    MapWorkspace_Shutdown( &pMason->map );
    EditorSettings_SetScope( &pMason->gui.settings, settings_scope_t::USER, nullptr );
    EditorSettingsFile_Shutdown( &pMason->userSettings );
    EditorGui_Shutdown( &pMason->gui );
    delete pMason;
}

QMainWindow *Mason_Window( mason_t *pMason )
{
    CY_ASSERT( pMason != nullptr );
    return pMason->pWindow;
}

editor_gui_t *Mason_Gui( mason_t *pMason )
{
    CY_ASSERT( pMason != nullptr );
    return &pMason->gui;
}

map_workspace_t *Mason_MapWorkspace( mason_t *pMason )
{
    CY_ASSERT( pMason != nullptr );
    return &pMason->map;
}

bool Mason_OpenMap( mason_t *pMason, const QString &path )
{
    CY_ASSERT( pMason != nullptr );
    const map_files_result_t result = MapWorkspace_Open( &pMason->map, path );
    if ( result.status != map_files_status_t::OK ) {
        ReportError( *pMason, QStringLiteral( "Map not opened" ), DescribeFailure( result ) );
        return false;
    }
    pMason->lastDirectory = QFileInfo( path ).absolutePath();
    if ( !MasonRecent_Add( &pMason->recent, path ) ) { LogText( log_level_t::Warning, QStringLiteral( "The recent maps list could not be saved" ) ); }
    QString message = QStringLiteral( "Opened %1: %2 objects in %3 chunk files" )
                          .arg( MapWorkspace_DisplayName( &pMason->map ) )
                          .arg( pMason->map.wire.objects.nCount )
                          .arg( result.nChunkFiles );
    if ( pMason->map.pDocument->problems.nCount != 0u ) {
        message += QStringLiteral( ", %1 problems (see console)" ).arg( pMason->map.pDocument->problems.nCount );
    }
    LogText( log_level_t::Info, message );
    pMason->pWindow->statusBar()->showMessage( message, 8000 );
    return true;
}

QStringList Mason_RecentMaps( mason_t *pMason )
{
    CY_ASSERT( pMason != nullptr );
    return pMason->recent.files;
}

bool Mason_ShowWelcome( mason_t *pMason )
{
    CY_ASSERT( pMason != nullptr );
    mason_t &mason = *pMason;
    if ( IsHeadless( mason ) ) { return false; }
    const bool bShowAtStartup = EditorSettings_Bool( &mason.gui.settings, "editor.ui.show_welcome", CY_TRUE );
    QDialog *pWelcome = MasonWelcome_Create( mason.pWindow, mason.gui.style, mason.recent.files, bShowAtStartup );
    pWelcome->exec();
    const mason_welcome_choice_t choice = MasonWelcome_Choice( pWelcome );
    const QString file = MasonWelcome_ChosenFile( pWelcome );
    if ( MasonWelcome_ShowAtStartup( pWelcome ) != bShowAtStartup ) {
        if ( const setting_descriptor_t *pSetting = EditorSettings_Find( &mason.gui.settings, StringView_FromCString( "editor.ui.show_welcome" ) ) ) {
            setting_value_t value{};
            value.type = setting_type_t::BOOL;
            value.bValue = MasonWelcome_ShowAtStartup( pWelcome ) ? CY_TRUE : CY_FALSE;
            ( void )EditorSettings_Write( &mason.gui.settings, settings_scope_t::USER, *pSetting, value );
        }
    }
    delete pWelcome;
    const command_registry_t &commands = mason.gui.commands;
    switch ( choice ) {
        case mason_welcome_choice_t::NEW_MAP: ( void )EditorCommands_Execute( &commands, StringView_FromCString( "file.new" ), command_args_t{} ); break;
        case mason_welcome_choice_t::BROWSE: ( void )EditorCommands_Execute( &commands, StringView_FromCString( "file.open" ), command_args_t{} ); break;
        case mason_welcome_choice_t::OPEN: OpenRecent( mason, file ); break;
        case mason_welcome_choice_t::NONE: break;
    }
    return true;
}

QString Mason_StatusText( mason_t *pMason )
{
    CY_ASSERT( pMason != nullptr );
    return QStringList{ pMason->pStatusTool->text(), pMason->pStatusSelection->text(), pMason->pStatusSize->text(), pMason->pStatusCursor->text(),
                        pMason->pStatusGrid->text() }
        .join( QStringLiteral( " | " ) );
}

} // namespace cypher::mason
