//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_AssetWindow_Tests.cpp
//  Purpose: Contract tests for the Asset Browser window on a scratch
//           project: tabs and counts, the filter, Asset Types, Sources and
//           Content Roots, the three views sharing one selection, Used in
//           Map with missing assets, the Selection tab, pick mode, and saved
//           searches kept in the user settings.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_AssetWindow.h"
#include "CypherEditorGui_AssetBrowser.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <memory>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

void WriteFile( const QString &path, const QByteArray &contents )
{
    REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( contents ) == contents.size() );
}

// Two roots: main (materials, a texture and its image, a sound) and extra
// (a material of its own).
struct project_t {
    QApplication *pApplication{ qobject_cast<QApplication *>( QCoreApplication::instance() ) };
    editor_gui_t gui{};
    QTemporaryDir folder{};
    std::unique_ptr<QWidget> pBrowser{};
    std::unique_ptr<QDialog> pWindow{};

    project_t()
    {
        REQUIRE( pApplication != nullptr );
        REQUIRE( folder.isValid() );
        REQUIRE( EditorGui_Init( &gui, pApplication, Allocator_GetSystem() ) == editor_gui_status_t::OK );
        const QString main = folder.filePath( QStringLiteral( "main" ) );
        const QString extra = folder.filePath( QStringLiteral( "extra" ) );
        WriteFile( main + QStringLiteral( "/materials/walls/brick.cymat" ),
                   "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = \"shaders/surface.cyshader\" textures = { base_color = \"textures/walls/brick.cytex\" } }\n" );
        WriteFile( main + QStringLiteral( "/materials/floors/tile.cymat" ), "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = \"shaders/surface.cyshader\" }\n" );
        WriteFile( main + QStringLiteral( "/textures/walls/brick.cytex" ), "@cykv 1\n@schema \"cypher.texture\" 1\n{ source = \"textures/walls/brick.png\" usage = \"color\" }\n" );
        QImage brick( 8, 8, QImage::Format_RGB32 );
        brick.fill( QColor( 0xB0, 0x40, 0x30 ) );
        REQUIRE( brick.save( main + QStringLiteral( "/textures/walls/brick.png" ) ) );
        WriteFile( main + QStringLiteral( "/sounds/door.cysnd" ), "@cykv 1\n@schema \"cypher.sound\" 1\n{ source = \"sounds/door.wav\" }\n" );
        WriteFile( main + QStringLiteral( "/sounds/door.wav" ), "RIFF" );
        WriteFile( extra + QStringLiteral( "/materials/extra/glass.cymat" ), "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = \"x.cyshader\" }\n" );
        pBrowser.reset( EditorAssetBrowser_Create( nullptr, &gui ) );
        REQUIRE( pBrowser != nullptr );
        EditorAssetBrowser_SetRoots( pBrowser.get(), { main, extra } );
        pWindow.reset( EditorAssetWindow_Create( nullptr, &gui, pBrowser.get() ) );
        REQUIRE( pWindow != nullptr );
        EditorAssetWindow_Refresh( pWindow.get() );
    }
    ~project_t()
    {
        pWindow.reset();
        pBrowser.reset();
        EditorGui_Shutdown( &gui );
    }
};

struct accepted_t {
    QString path;
    editor_asset_kind_t kind{ editor_asset_kind_t::COUNT };
    int nCalls{ 0 };
    static void Record( void *pContext, const QString &path, editor_asset_kind_t kind )
    {
        auto *pSelf = static_cast<accepted_t *>( pContext );
        pSelf->path = path;
        pSelf->kind = kind;
        ++pSelf->nCalls;
    }
};

struct user_settings_t {
    explicit user_settings_t( settings_registry_t *pRegistry ) : pRegistry( pRegistry )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( pRegistry, settings_scope_t::USER, &store );
    }
    ~user_settings_t() { EditorSettings_SetScope( pRegistry, settings_scope_t::USER, nullptr ); }
    settings_registry_t *pRegistry;
    settings_document_t store{};
};

} // namespace

TEST_CASE( "The asset window lists the catalogue by tab and narrows by filter, types, sources, and roots", "[editor][gui][assets][asset-window]" )
{
    project_t p;
    QDialog *pWindow = p.pWindow.get();
    // All lists recipes from both roots; sources stay hidden.
    const QStringList all = EditorAssetWindow_Visible( pWindow );
    CHECK( all.contains( QStringLiteral( "materials/walls/brick.cymat" ) ) );
    CHECK( all.contains( QStringLiteral( "materials/extra/glass.cymat" ) ) );
    CHECK( all.contains( QStringLiteral( "sounds/door.cysnd" ) ) );
    CHECK_FALSE( all.contains( QStringLiteral( "textures/walls/brick.png" ) ) );
    CHECK( EditorAssetWindow_Status( pWindow ) == QStringLiteral( "%1 Assets Visible" ).arg( all.size() ) );
    auto *pTabs = pWindow->findChild<QTabBar *>( QStringLiteral( "AssetWindowTabs" ) );
    REQUIRE( pTabs != nullptr );
    CHECK( pTabs->count() == ASSET_WINDOW_TAB_COUNT );
    CHECK( pTabs->tabText( EditorAssetWindow_KindTab( editor_asset_kind_t::MATERIAL ) ) == QStringLiteral( "Materials  3" ) );

    // A kind tab shows only that kind.
    EditorAssetWindow_SetTab( pWindow, EditorAssetWindow_KindTab( editor_asset_kind_t::MATERIAL ) );
    CHECK( EditorAssetWindow_Visible( pWindow ).size() == 3 );
    // The filter is fuzzy.
    EditorAssetWindow_SetFilter( pWindow, QStringLiteral( "brck" ) );
    CHECK( EditorAssetWindow_Visible( pWindow ) == QStringList{ QStringLiteral( "materials/walls/brick.cymat" ) } );
    CHECK( EditorAssetWindow_Status( pWindow ) == QStringLiteral( "1 Asset Visible" ) );
    EditorAssetWindow_SetFilter( pWindow, QString() );

    // Asset Types narrow the All tab.
    EditorAssetWindow_SetTab( pWindow, ASSET_WINDOW_TAB_ALL );
    EditorAssetWindow_SetKindMask( pWindow, EditorAssets_KindBit( editor_asset_kind_t::SOUND ) );
    CHECK( EditorAssetWindow_Visible( pWindow ) == QStringList{ QStringLiteral( "sounds/door.cysnd" ) } );
    EditorAssetWindow_SetKindMask( pWindow, EDITOR_ASSET_KIND_ALL );
    // Sources add the files recipes are built from.
    EditorAssetWindow_SetSources( pWindow, true );
    CHECK( EditorAssetWindow_Visible( pWindow ).contains( QStringLiteral( "textures/walls/brick.png" ) ) );
    EditorAssetWindow_SetSources( pWindow, false );
    // Hiding the second content root hides its assets only.
    EditorAssetWindow_SetRootVisible( pWindow, 1, false );
    CHECK_FALSE( EditorAssetWindow_Visible( pWindow ).contains( QStringLiteral( "materials/extra/glass.cymat" ) ) );
    CHECK( EditorAssetWindow_Visible( pWindow ).contains( QStringLiteral( "materials/walls/brick.cymat" ) ) );
    EditorAssetWindow_SetRootVisible( pWindow, 1, true );
    CHECK( EditorAssetWindow_Visible( pWindow ).contains( QStringLiteral( "materials/extra/glass.cymat" ) ) );
}

TEST_CASE( "The asset window's views share one selection and grid thumbnails come from the browser", "[editor][gui][assets][asset-window]" )
{
    project_t p;
    QDialog *pWindow = p.pWindow.get();
    CHECK( EditorAssetWindow_View( pWindow ) == editor_asset_window_view_t::GRID );
    REQUIRE( EditorAssetWindow_Select( pWindow, QStringLiteral( "materials/walls/brick.cymat" ) ) );
    for ( const auto view : { editor_asset_window_view_t::LIST, editor_asset_window_view_t::TREE, editor_asset_window_view_t::GRID } ) {
        EditorAssetWindow_SetView( pWindow, view );
        CHECK( EditorAssetWindow_View( pWindow ) == view );
        CHECK( EditorAssetWindow_Selected( pWindow ) == QStringLiteral( "materials/walls/brick.cymat" ) );
    }
    // The tree groups by folder.
    EditorAssetWindow_SetView( pWindow, editor_asset_window_view_t::TREE );
    auto *pTree = pWindow->findChild<QTreeWidget *>( QStringLiteral( "AssetWindowTreeView" ) );
    REQUIRE( pTree != nullptr );
    QStringList topLevel;
    for ( int i = 0; i < pTree->topLevelItemCount(); ++i ) { topLevel.append( pTree->topLevelItem( i )->text( 0 ) ); }
    CHECK( topLevel.contains( QStringLiteral( "materials" ) ) );
    CHECK( topLevel.contains( QStringLiteral( "sounds" ) ) );
    REQUIRE( pTree->currentItem() != nullptr );
    CHECK( pTree->currentItem()->data( 0, Qt::UserRole ).toString() == QStringLiteral( "materials/walls/brick.cymat" ) );
    // A material shows its base colour image; one without textures does not.
    EditorAssetWindow_SetView( pWindow, editor_asset_window_view_t::GRID );
    EditorAssetWindow_LoadThumbnails( pWindow );
    CHECK( EditorAssetWindow_HasThumbnail( pWindow, QStringLiteral( "materials/walls/brick.cymat" ) ) );
    CHECK_FALSE( EditorAssetWindow_HasThumbnail( pWindow, QStringLiteral( "materials/floors/tile.cymat" ) ) );
}

TEST_CASE( "Used in Map marks missing assets and the Selection tab lists what the selection uses", "[editor][gui][assets][asset-window]" )
{
    project_t p;
    QDialog *pWindow = p.pWindow.get();
    EditorAssetWindow_SetUsed( pWindow, { QStringLiteral( "materials/walls/brick.cymat" ), QStringLiteral( "materials/gone/lost.cymat" ),
                                          QStringLiteral( "materials/walls/brick.cymat" ) } );
    EditorAssetWindow_Refresh( pWindow );
    auto *pTabs = pWindow->findChild<QTabBar *>( QStringLiteral( "AssetWindowTabs" ) );
    REQUIRE( pTabs != nullptr );
    CHECK( pTabs->tabText( ASSET_WINDOW_TAB_USED ) == QStringLiteral( "Used in Map  2 (1 missing)" ) );
    EditorAssetWindow_SetTab( pWindow, ASSET_WINDOW_TAB_USED );
    CHECK( EditorAssetWindow_Visible( pWindow ) == ( QStringList{ QStringLiteral( "materials/gone/lost.cymat" ), QStringLiteral( "materials/walls/brick.cymat" ) } ) );
    // A missing asset can be selected and inspected, not accepted.
    accepted_t accepted;
    EditorAssetWindow_SetAccept( pWindow, &accepted_t::Record, &accepted );
    REQUIRE( EditorAssetWindow_Select( pWindow, QStringLiteral( "materials/gone/lost.cymat" ) ) );
    CHECK_FALSE( EditorAssetWindow_Accept( pWindow ) );
    CHECK( accepted.nCalls == 0 );
    auto *pPath = pWindow->findChild<QLabel *>( QStringLiteral( "AssetWindowPath" ) );
    REQUIRE( pPath != nullptr );
    CHECK( pPath->text().contains( QStringLiteral( "missing" ) ) );

    static QStringList s_selection;
    s_selection = { QStringLiteral( "sounds/door.cysnd" ), QStringLiteral( "materials/floors/tile.cymat" ) };
    EditorAssetWindow_SetSelectionSource( pWindow, []( void * ) { return s_selection; }, nullptr );
    EditorAssetWindow_SetTab( pWindow, ASSET_WINDOW_TAB_SELECTION );
    CHECK( EditorAssetWindow_Visible( pWindow ) == ( QStringList{ QStringLiteral( "materials/floors/tile.cymat" ), QStringLiteral( "sounds/door.cysnd" ) } ) );
    // Browsing: Accept hands over the asset and the window stays.
    REQUIRE( EditorAssetWindow_Select( pWindow, QStringLiteral( "sounds/door.cysnd" ) ) );
    CHECK( EditorAssetWindow_Accept( pWindow ) );
    CHECK( accepted.nCalls == 1 );
    CHECK( accepted.path == QStringLiteral( "sounds/door.cysnd" ) );
    CHECK( accepted.kind == editor_asset_kind_t::SOUND );
}

TEST_CASE( "Picking accepts only the asked kind, once, and closing picks nothing", "[editor][gui][assets][asset-window]" )
{
    project_t p;
    QDialog *pWindow = p.pWindow.get();
    accepted_t browsed;
    EditorAssetWindow_SetAccept( pWindow, &accepted_t::Record, &browsed );
    accepted_t picked;
    EditorAssetWindow_Pick( pWindow, editor_asset_kind_t::MATERIAL, QStringLiteral( "materials/floors/tile.cymat" ), &accepted_t::Record, &picked );
    CHECK( pWindow->isVisible() );
    CHECK( EditorAssetWindow_IsPicking( pWindow ) );
    CHECK( EditorAssetWindow_Tab( pWindow ) == EditorAssetWindow_KindTab( editor_asset_kind_t::MATERIAL ) );
    CHECK( EditorAssetWindow_Selected( pWindow ) == QStringLiteral( "materials/floors/tile.cymat" ) ); // The current value.
    auto *pAccept = pWindow->findChild<QPushButton *>( QStringLiteral( "AssetWindowAccept" ) );
    REQUIRE( pAccept != nullptr );
    CHECK( pAccept->isEnabled() );
    // Another kind's asset cannot be the answer.
    EditorAssetWindow_SetTab( pWindow, ASSET_WINDOW_TAB_ALL );
    REQUIRE( EditorAssetWindow_Select( pWindow, QStringLiteral( "sounds/door.cysnd" ) ) );
    CHECK_FALSE( pAccept->isEnabled() );
    CHECK_FALSE( EditorAssetWindow_Accept( pWindow ) );
    REQUIRE( EditorAssetWindow_Select( pWindow, QStringLiteral( "materials/walls/brick.cymat" ) ) );
    CHECK( EditorAssetWindow_Accept( pWindow ) );
    CHECK( picked.nCalls == 1 );
    CHECK( picked.path == QStringLiteral( "materials/walls/brick.cymat" ) );
    CHECK( browsed.nCalls == 0 ); // The pick, not the browse callback.
    CHECK_FALSE( pWindow->isVisible() );
    CHECK_FALSE( EditorAssetWindow_IsPicking( pWindow ) );
    // Closing a pick answers nothing.
    EditorAssetWindow_Pick( pWindow, editor_asset_kind_t::MATERIAL, QString(), &accepted_t::Record, &picked );
    pWindow->reject();
    CHECK_FALSE( EditorAssetWindow_IsPicking( pWindow ) );
    CHECK( picked.nCalls == 1 );
}

TEST_CASE( "Saved searches restore tab, filter, and types and live in the user settings", "[editor][gui][assets][asset-window]" )
{
    project_t p;
    user_settings_t settings( &p.gui.settings );
    QDialog *pWindow = p.pWindow.get();
    EditorAssetWindow_SetTab( pWindow, EditorAssetWindow_KindTab( editor_asset_kind_t::MATERIAL ) );
    EditorAssetWindow_SetFilter( pWindow, QStringLiteral( "walls" ) );
    CHECK( EditorAssetWindow_SaveSearch( pWindow, QStringLiteral( "Wall materials" ) ) );
    CHECK_FALSE( EditorAssetWindow_SaveSearch( pWindow, QStringLiteral( "   " ) ) );
    const QStringList expected = EditorAssetWindow_Visible( pWindow );
    EditorAssetWindow_SetTab( pWindow, ASSET_WINDOW_TAB_ALL );
    EditorAssetWindow_SetFilter( pWindow, QString() );
    CHECK( EditorAssetWindow_Visible( pWindow ) != expected );
    REQUIRE( EditorAssetWindow_LoadSearch( pWindow, QStringLiteral( "wall MATERIALS" ) ) ); // Names compare without case.
    CHECK( EditorAssetWindow_Tab( pWindow ) == EditorAssetWindow_KindTab( editor_asset_kind_t::MATERIAL ) );
    CHECK( EditorAssetWindow_Visible( pWindow ) == expected );
    // A new window reads them back from the settings.
    std::unique_ptr<QDialog> pOther( EditorAssetWindow_Create( nullptr, &p.gui, p.pBrowser.get() ) );
    CHECK( EditorAssetWindow_SavedSearches( pOther.get() ) == QStringList{ QStringLiteral( "Wall materials" ) } );
    CHECK( EditorAssetWindow_DeleteSearch( pWindow, QStringLiteral( "Wall materials" ) ) );
    CHECK_FALSE( EditorAssetWindow_LoadSearch( pWindow, QStringLiteral( "Wall materials" ) ) );
    std::unique_ptr<QDialog> pThird( EditorAssetWindow_Create( nullptr, &p.gui, p.pBrowser.get() ) );
    CHECK( EditorAssetWindow_SavedSearches( pThird.get() ).isEmpty() );
}

TEST_CASE( "An embedded asset browser belongs to its pane and accepts without closing", "[editor][gui][assets][asset-window][embedded]" )
{
    project_t p;
    QWidget host;
    REQUIRE( EditorAssetWindow_CreateEmbedded( nullptr, &p.gui, p.pBrowser.get() ) == nullptr );
    REQUIRE( EditorAssetWindow_CreateEmbedded( &host, nullptr, p.pBrowser.get() ) == nullptr );
    QWidget *pPane = EditorAssetWindow_CreateEmbedded( &host, &p.gui, p.pBrowser.get() );
    REQUIRE( pPane != nullptr );
    CHECK( pPane->parentWidget() == &host );
    CHECK_FALSE( pPane->isWindow() );
    CHECK( ( pPane->windowFlags() & Qt::WindowType_Mask ) == Qt::Widget );
    CHECK( p.pWindow->isWindow() ); // The standalone factory still creates a window.
    host.show();
    pPane->show();
    QCoreApplication::processEvents();
    REQUIRE( pPane->isVisible() );

    auto *pClose = pPane->findChild<QPushButton *>( QStringLiteral( "AssetWindowClose" ) );
    auto *pAccept = pPane->findChild<QPushButton *>( QStringLiteral( "AssetWindowAccept" ) );
    REQUIRE( pClose != nullptr );
    REQUIRE( pAccept != nullptr );
    CHECK( pClose->isHidden() );
    CHECK_FALSE( pAccept->isDefault() );
    CHECK_FALSE( pAccept->autoDefault() );
    accepted_t browsed;
    EditorAssetWindow_SetAccept( pPane, &accepted_t::Record, &browsed );
    REQUIRE( EditorAssetWindow_Select( pPane, QStringLiteral( "materials/walls/brick.cymat" ) ) );
    pAccept->click();
    CHECK( browsed.nCalls == 1 );
    CHECK( browsed.path == QStringLiteral( "materials/walls/brick.cymat" ) );
    CHECK( browsed.kind == editor_asset_kind_t::MATERIAL );
    CHECK( pPane->isVisible() );

    // A picker request must not change a persistent pane's callbacks or tab.
    accepted_t picked;
    const int tab = EditorAssetWindow_Tab( pPane );
    EditorAssetWindow_Pick( pPane, editor_asset_kind_t::SOUND, QStringLiteral( "sounds/door.cysnd" ), &accepted_t::Record, &picked );
    CHECK_FALSE( EditorAssetWindow_IsPicking( pPane ) );
    CHECK( EditorAssetWindow_Tab( pPane ) == tab );
    CHECK( EditorAssetWindow_Selected( pPane ) == QStringLiteral( "materials/walls/brick.cymat" ) );
    REQUIRE( EditorAssetWindow_Accept( pPane ) );
    CHECK( browsed.nCalls == 2 );
    CHECK( picked.nCalls == 0 );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    QCoreApplication::sendEvent( pPane, &escape );
    CHECK( pPane->isVisible() );
    pPane->hide();
    pPane->show();
    CHECK( EditorAssetWindow_Selected( pPane ) == QStringLiteral( "materials/walls/brick.cymat" ) );

    p.pWindow->show();
    QKeyEvent standaloneEscape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    QCoreApplication::sendEvent( p.pWindow.get(), &standaloneEscape );
    CHECK_FALSE( p.pWindow->isVisible() );
}

TEST_CASE( "An embedded asset browser tolerates its shared source being destroyed first", "[editor][gui][assets][asset-window][embedded]" )
{
    project_t p;
    QWidget host;
    std::unique_ptr<QWidget> pPane( EditorAssetWindow_CreateEmbedded( &host, &p.gui, p.pBrowser.get() ) );
    REQUIRE( pPane != nullptr );
    REQUIRE( EditorAssetWindow_Select( pPane.get(), QStringLiteral( "materials/walls/brick.cymat" ) ) );
    // Model and thumbnail operations after source deletion must tolerate
    // an empty catalogue, and pane teardown must not consult the old source.
    p.pBrowser.reset();
    EditorAssetWindow_Refresh( pPane.get() );
    EditorAssetWindow_LoadThumbnails( pPane.get() );
    CHECK( EditorAssetWindow_Visible( pPane.get() ).isEmpty() );
    CHECK_FALSE( EditorAssetWindow_Accept( pPane.get() ) );
    pPane.reset();
    QCoreApplication::processEvents();
}
