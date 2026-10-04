//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_AssetBrowser_Tests.cpp
//  Purpose: Contract tests for the asset browser panel on a scratch
//           project: scanning roots with priority, tabs, search, folders,
//           sources, image thumbnails through material and texture
//           recipes, the Used tab with missing assets, and activation.
//
//  History:
//  - Created by Karlo Siric on 2026-10-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_AssetBrowser.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QTextCursor>
#include <QWidget>

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

struct project_t {
    QApplication *pApplication{ qobject_cast<QApplication *>( QCoreApplication::instance() ) };
    editor_gui_t gui{};
    QTemporaryDir folder{};
    QString main, extra;

    project_t()
    {
        REQUIRE( pApplication != nullptr );
        REQUIRE( folder.isValid() );
        REQUIRE( EditorGui_Init( &gui, pApplication, Allocator_GetSystem() ) == editor_gui_status_t::OK );
        main = folder.filePath( QStringLiteral( "main" ) );
        extra = folder.filePath( QStringLiteral( "extra" ) );
        WriteFile( main + QStringLiteral( "/materials/test/brick.cymat" ),
                   "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = \"shaders/surface.cyshader\" textures = { base_color = \"textures/test/brick.cytex\" } }\n" );
        WriteFile( main + QStringLiteral( "/textures/test/brick.cytex" ),
                   "@cykv 1\n@schema \"cypher.texture\" 1\n{ source = \"textures/test/brick.png\" usage = \"color\" }\n" );
        QImage brick( 8, 8, QImage::Format_RGB32 );
        brick.fill( QColor( 0xB0, 0x40, 0x30 ) );
        REQUIRE( brick.save( main + QStringLiteral( "/textures/test/brick.png" ) ) );
        WriteFile( main + QStringLiteral( "/sounds/door.wav" ), "RIFF" );
        WriteFile( main + QStringLiteral( "/notes.md" ), "not an asset" );
        // A lower-priority root: its brick is hidden by main's, its extra shows.
        WriteFile( extra + QStringLiteral( "/materials/test/brick.cymat" ), "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = \"x.cyshader\" }\n" );
        WriteFile( extra + QStringLiteral( "/materials/plain.cymat" ), "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = \"x.cyshader\" }\n" );
    }
    ~project_t() { EditorGui_Shutdown( &gui ); }
};

struct activation_t {
    QString path;
    editor_asset_kind_t kind{ editor_asset_kind_t::COUNT };
    static void Record( void *pContext, const QString &path, editor_asset_kind_t kind )
    {
        auto *pSelf = static_cast<activation_t *>( pContext );
        pSelf->path = path;
        pSelf->kind = kind;
    }
};

struct user_settings_t {
    explicit user_settings_t( settings_registry_t *pRegistry ) : pRegistry( pRegistry )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( pRegistry, settings_scope_t::USER, &store );
    }
    ~user_settings_t() { EditorSettings_SetScope( pRegistry, settings_scope_t::USER, nullptr ); }
    void SetInteger( const char *pPath, i64 number )
    {
        const setting_descriptor_t *pDescriptor = EditorSettings_Find( pRegistry, StringView_FromCString( pPath ) );
        REQUIRE( pDescriptor != nullptr );
        setting_value_t value{};
        value.type = setting_type_t::INTEGER;
        value.nValue = number;
        REQUIRE( EditorSettings_Write( pRegistry, settings_scope_t::USER, *pDescriptor, value ) == settings_registry_status_t::OK );
    }
    void SetBool( const char *pPath, bool enabled )
    {
        const setting_descriptor_t *pDescriptor = EditorSettings_Find( pRegistry, StringView_FromCString( pPath ) );
        REQUIRE( pDescriptor != nullptr );
        setting_value_t value{};
        value.type = setting_type_t::BOOL;
        value.bValue = enabled;
        REQUIRE( EditorSettings_Write( pRegistry, settings_scope_t::USER, *pDescriptor, value ) == settings_registry_status_t::OK );
    }
    settings_registry_t *pRegistry;
    settings_document_t store{};
};

} // namespace

TEST_CASE( "The asset browser scans roots in priority order and filters by tab, search, and folder", "[editor][gui][assets]" )
{
    project_t p;
    std::unique_ptr<QWidget> pBrowser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    REQUIRE( pBrowser != nullptr );
    CHECK( EditorAssetBrowser_Visible( pBrowser.get() ).isEmpty() );
    CHECK( EditorAssetBrowser_Status( pBrowser.get() ).contains( QStringLiteral( "No content folders" ) ) );

    EditorAssetBrowser_SetRoots( pBrowser.get(), { p.main, p.extra } );
    const editor_asset_catalog_t *pCatalog = EditorAssetBrowser_Catalog( pBrowser.get() );
    REQUIRE( pCatalog != nullptr );
    CHECK( EditorAssets_Count( pCatalog ) == 5u ); // brick.cymat (main), plain.cymat, brick.cytex, brick.png, door.wav.
    const editor_asset_t *pBrick = EditorAssets_Find( pCatalog, StringView_FromCString( "materials/test/brick.cymat" ) );
    REQUIRE( pBrick != nullptr );
    CHECK( pBrick->iRoot == 0u );

    // All, without sources: the recipes.
    CHECK( EditorAssetBrowser_Visible( pBrowser.get() ) ==
           QStringList{ QStringLiteral( "materials/plain.cymat" ), QStringLiteral( "materials/test/brick.cymat" ), QStringLiteral( "textures/test/brick.cytex" ) } );

    EditorAssetBrowser_SetTab( pBrowser.get(), EditorAssetBrowser_KindTab( editor_asset_kind_t::MATERIAL ) );
    CHECK( EditorAssetBrowser_Visible( pBrowser.get() ).size() == 2 );
    CHECK( EditorAssetBrowser_Status( pBrowser.get() ).startsWith( QStringLiteral( "2 materials" ) ) );
    EditorAssetBrowser_SetSearch( pBrowser.get(), QStringLiteral( "plain" ) );
    CHECK( EditorAssetBrowser_Visible( pBrowser.get() ) == QStringList{ QStringLiteral( "materials/plain.cymat" ) } );
    EditorAssetBrowser_SetSearch( pBrowser.get(), QString() );
    EditorAssetBrowser_SetFolder( pBrowser.get(), QStringLiteral( "materials/test" ) );
    CHECK( EditorAssetBrowser_Visible( pBrowser.get() ) == QStringList{ QStringLiteral( "materials/test/brick.cymat" ) } );
    EditorAssetBrowser_SetFolder( pBrowser.get(), QString() );

    // Sources join on request.
    EditorAssetBrowser_SetTab( pBrowser.get(), EditorAssetBrowser_KindTab( editor_asset_kind_t::SOUND ) );
    CHECK( EditorAssetBrowser_Visible( pBrowser.get() ).isEmpty() );
    EditorAssetBrowser_SetSources( pBrowser.get(), true );
    CHECK( EditorAssetBrowser_Visible( pBrowser.get() ) == QStringList{ QStringLiteral( "sounds/door.wav" ) } );
}

TEST_CASE( "Asset thumbnails follow material and texture recipes to their images", "[editor][gui][assets]" )
{
    project_t p;
    std::unique_ptr<QWidget> pBrowser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    REQUIRE( pBrowser != nullptr );
    EditorAssetBrowser_SetRoots( pBrowser.get(), { p.main, p.extra } );
    EditorAssetBrowser_SetSources( pBrowser.get(), true );
    EditorAssetBrowser_LoadThumbnails( pBrowser.get() );
    CHECK( EditorAssetBrowser_HasImage( pBrowser.get(), QStringLiteral( "materials/test/brick.cymat" ) ) ); // Material -> .cytex -> .png.
    CHECK( EditorAssetBrowser_HasImage( pBrowser.get(), QStringLiteral( "textures/test/brick.cytex" ) ) );
    CHECK( EditorAssetBrowser_HasImage( pBrowser.get(), QStringLiteral( "textures/test/brick.png" ) ) );
    CHECK_FALSE( EditorAssetBrowser_HasImage( pBrowser.get(), QStringLiteral( "materials/plain.cymat" ) ) ); // No texture: its kind's icon.
    CHECK_FALSE( EditorAssetBrowser_HasImage( pBrowser.get(), QStringLiteral( "sounds/door.wav" ) ) );
}

TEST_CASE( "The Used tab lists the document's assets and flags missing ones", "[editor][gui][assets]" )
{
    project_t p;
    std::unique_ptr<QWidget> pBrowser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    REQUIRE( pBrowser != nullptr );
    EditorAssetBrowser_SetRoots( pBrowser.get(), { p.main, p.extra } );
    EditorAssetBrowser_SetUsed( pBrowser.get(), QStringLiteral( "Used in Map" ),
                                { QStringLiteral( "materials/test/brick.cymat" ), QStringLiteral( "materials/gone.cymat" ), QStringLiteral( "materials/test/brick.cymat" ) } );
    CHECK( EditorAssetBrowser_Missing( pBrowser.get() ) == QStringList{ QStringLiteral( "materials/gone.cymat" ) } );
    EditorAssetBrowser_SetTab( pBrowser.get(), ASSET_TAB_USED );
    CHECK( EditorAssetBrowser_Visible( pBrowser.get() ) == QStringList{ QStringLiteral( "materials/gone.cymat" ), QStringLiteral( "materials/test/brick.cymat" ) } );
    CHECK( EditorAssetBrowser_Status( pBrowser.get() ).startsWith( QStringLiteral( "2 used in map" ) ) );

    // Activating calls the application; a missing asset has nothing to use.
    activation_t activation;
    EditorAssetBrowser_SetActivate( pBrowser.get(), &activation_t::Record, &activation );
    EditorAssetBrowser_Activate( pBrowser.get(), QStringLiteral( "materials/gone.cymat" ) );
    CHECK( activation.path.isEmpty() );
    EditorAssetBrowser_Activate( pBrowser.get(), QStringLiteral( "materials/test/brick.cymat" ) );
    CHECK( activation.path == QStringLiteral( "materials/test/brick.cymat" ) );
    CHECK( activation.kind == editor_asset_kind_t::MATERIAL );
}

TEST_CASE( "Asset previews have larger defaults and size presets preserve the active view", "[editor][gui][assets]" )
{
    project_t p;
    user_settings_t settings( &p.gui.settings );
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main, p.extra } );
    auto *view = browser->findChild<QListView *>( QStringLiteral( "AssetBrowserView" ) );
    auto *slider = browser->findChild<QSlider *>( QStringLiteral( "AssetBrowserTileSize" ) );
    auto *preset = browser->findChild<QToolButton *>( QStringLiteral( "AssetBrowserSizePreset" ) );
    auto *grid = browser->findChild<QToolButton *>( QStringLiteral( "AssetBrowserGrid" ) );
    auto *list = browser->findChild<QToolButton *>( QStringLiteral( "AssetBrowserList" ) );
    REQUIRE( view != nullptr );
    REQUIRE( slider != nullptr );
    REQUIRE( preset != nullptr );
    REQUIRE( grid != nullptr );
    REQUIRE( list != nullptr );
    CHECK( view->viewMode() == QListView::IconMode );
    CHECK( view->iconSize() == QSize( 144, 144 ) );
    CHECK( slider->minimum() == 48 );
    CHECK( slider->maximum() == 256 );
    CHECK( preset->popupMode() == QToolButton::InstantPopup );
    const QStringList visible = EditorAssetBrowser_Visible( browser.get() );
    view->setCurrentIndex( view->model()->index( 0, 0 ) );
    const QModelIndex selected = view->currentIndex();
    auto *large = browser->findChild<QAction *>( QStringLiteral( "AssetBrowserSize256" ) );
    REQUIRE( large != nullptr );
    large->trigger();
    CHECK( view->iconSize() == QSize( 256, 256 ) );
    CHECK( slider->value() == 256 );
    CHECK( EditorSettings_Integer( &p.gui.settings, "editor.assets.thumbnail_size", 0 ) == 256 );
    CHECK( EditorAssetBrowser_Visible( browser.get() ) == visible );
    CHECK( view->currentIndex() == selected );
    const QPixmap tile = qvariant_cast<QPixmap>( view->model()->data( selected, Qt::DecorationRole ) );
    CHECK( tile.deviceIndependentSize() == QSizeF( 256, 256 ) );
    list->click();
    CHECK( list->isChecked() );
    CHECK_FALSE( grid->isChecked() );
    CHECK( view->viewMode() == QListView::ListMode );
    CHECK( view->iconSize() == QSize( 22, 22 ) );
    CHECK_FALSE( slider->isEnabled() );
    CHECK_FALSE( preset->isEnabled() );
    CHECK( slider->value() == 256 );
    CHECK( EditorSettings_Bool( &p.gui.settings, "editor.assets.list_view", CY_FALSE ) );
    grid->click();
    CHECK( view->iconSize() == QSize( 256, 256 ) );
    CHECK( view->currentIndex() == selected );
    grid->click(); // Clicking the active radio-like button must not uncheck it.
    CHECK( grid->isChecked() );
    CHECK_FALSE( list->isChecked() );
    CHECK( slider->isEnabled() );
    CHECK( EditorAssetBrowser_Visible( browser.get() ) == visible );
}

TEST_CASE( "Asset preview preferences survive panel recreation and respond to external settings", "[editor][gui][assets]" )
{
    project_t p;
    user_settings_t settings( &p.gui.settings );
    {
        std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
        auto *slider = browser->findChild<QSlider *>( QStringLiteral( "AssetBrowserTileSize" ) );
        auto *list = browser->findChild<QToolButton *>( QStringLiteral( "AssetBrowserList" ) );
        REQUIRE( slider != nullptr );
        REQUIRE( list != nullptr );
        slider->setValue( 192 );
        list->click();
    }
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    auto *view = browser->findChild<QListView *>( QStringLiteral( "AssetBrowserView" ) );
    auto *slider = browser->findChild<QSlider *>( QStringLiteral( "AssetBrowserTileSize" ) );
    auto *grid = browser->findChild<QToolButton *>( QStringLiteral( "AssetBrowserGrid" ) );
    REQUIRE( view != nullptr );
    REQUIRE( slider != nullptr );
    REQUIRE( grid != nullptr );
    CHECK( view->viewMode() == QListView::ListMode );
    CHECK( slider->value() == 192 );
    settings.SetInteger( "editor.assets.thumbnail_size", 128 );
    CHECK( slider->value() == 128 );
    CHECK( view->viewMode() == QListView::ListMode );
    CHECK( view->iconSize() == QSize( 22, 22 ) );
    grid->click();
    CHECK( view->iconSize() == QSize( 128, 128 ) );
    CHECK_FALSE( EditorSettings_Bool( &p.gui.settings, "editor.assets.list_view", CY_TRUE ) );
    settings.SetInteger( "editor.assets.thumbnail_size", 256 );
    CHECK( view->iconSize() == QSize( 256, 256 ) );
}

TEST_CASE( "Asset thumbnail decoding retains enough resolution for large high DPI previews", "[editor][gui][assets]" )
{
    project_t p;
    QImage source( 1024, 512, QImage::Format_RGB32 );
    source.fill( QColor( 0xB0, 0x40, 0x30 ) );
    REQUIRE( source.save( p.main + QStringLiteral( "/textures/test/brick.png" ) ) );
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main } );
    const QImage image = EditorAssetBrowser_Image( browser.get(), QStringLiteral( "materials/test/brick.cymat" ) );
    CHECK( image.size() == QSize( 512, 256 ) );
}

TEST_CASE( "Asset previews retain the artwork at the bottom without a category color strip", "[editor][gui][assets]" )
{
    project_t p;
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main } );
    EditorAssetBrowser_LoadThumbnails( browser.get() );
    auto *view = browser->findChild<QListView *>( QStringLiteral( "AssetBrowserView" ) );
    REQUIRE( view != nullptr );
    const QStringList visible = EditorAssetBrowser_Visible( browser.get() );
    const int row = static_cast<int>( visible.indexOf( QStringLiteral( "materials/test/brick.cymat" ) ) );
    REQUIRE( row >= 0 );
    const QImage tile = qvariant_cast<QPixmap>( view->model()->data( view->model()->index( row, 0 ), Qt::DecorationRole ) ).toImage();
    REQUIRE_FALSE( tile.isNull() );
    const qreal ratio = tile.devicePixelRatio();
    const int middle = static_cast<int>( 72 * ratio );
    const int bottom = static_cast<int>( 141 * ratio );
    CHECK( tile.pixelColor( middle, middle ) == QColor( 0xB0, 0x40, 0x30 ) );
    CHECK( tile.pixelColor( middle, bottom ) == tile.pixelColor( middle, middle ) );
}

TEST_CASE( "Fast asset search escapes local filters and focuses a result without activating it", "[editor][gui][assets]" )
{
    project_t p;
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main, p.extra } );
    browser->resize( 800, 500 );
    browser->show();
    browser->activateWindow();
    QCoreApplication::processEvents();
    activation_t activation;
    EditorAssetBrowser_SetActivate( browser.get(), &activation_t::Record, &activation );
    auto *view = browser->findChild<QListView *>( QStringLiteral( "AssetBrowserView" ) );
    REQUIRE( view != nullptr );
    EditorAssetBrowser_SetTab( browser.get(), EditorAssetBrowser_KindTab( editor_asset_kind_t::TEXTURE ) );
    EditorAssetBrowser_SetFolder( browser.get(), QStringLiteral( "textures/test" ) );
    EditorAssetBrowser_SetSearch( browser.get(), QStringLiteral( "plain" ) );
    CHECK( EditorAssetBrowser_Visible( browser.get() ).isEmpty() );
    EditorAssetBrowser_Search( browser.get(), QStringLiteral( "plain" ), true );
    CHECK( EditorAssetBrowser_Visible( browser.get() ) == QStringList{ QStringLiteral( "materials/plain.cymat" ) } );
    CHECK( view->currentIndex() == view->model()->index( 0, 0 ) );
    CHECK( view->hasFocus() );
    CHECK( activation.path.isEmpty() );
    EditorAssetBrowser_Search( browser.get(), QStringLiteral( "door" ), true );
    CHECK( EditorAssetBrowser_Visible( browser.get() ).isEmpty() ); // Sources remain hidden.
    CHECK_FALSE( view->currentIndex().isValid() );
    EditorAssetBrowser_SetSources( browser.get(), true );
    EditorAssetBrowser_Search( browser.get(), QStringLiteral( "door" ), true );
    CHECK( EditorAssetBrowser_Visible( browser.get() ) == QStringList{ QStringLiteral( "sounds/door.wav" ) } );
    CHECK( activation.path.isEmpty() );
}

TEST_CASE( "Asset folder rows separate counts and keep expansion when rescanned", "[editor][gui][assets]" )
{
    project_t p;
    user_settings_t settings( &p.gui.settings );
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main, p.extra } );
    browser->resize( 800, 500 );
    browser->show();
    QCoreApplication::processEvents();
    auto *tree = browser->findChild<QTreeWidget *>( QStringLiteral( "AssetBrowserFolders" ) );
    REQUIRE( tree != nullptr );
    CHECK( tree->columnCount() == 2 );
    CHECK( tree->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "All folders" ) );
    CHECK( tree->topLevelItem( 0 )->text( 1 ) == QStringLiteral( "3" ) );
    auto rows = tree->findItems( QStringLiteral( "materials" ), Qt::MatchExactly | Qt::MatchRecursive );
    REQUIRE( rows.size() == 1 );
    CHECK( rows.front()->text( 1 ) == QStringLiteral( "2" ) );
    CHECK( rows.front()->childCount() == 1 );
    // The default folder allocation must fit a common nested name, its icon,
    // the hierarchy indentation and the separate count column.
    CHECK( tree->columnWidth( 0 ) >= tree->fontMetrics().horizontalAdvance( QStringLiteral( "materials" ) ) +
                                          tree->indentation() * 2 + tree->iconSize().width() + 12 );
    rows.front()->setExpanded( false );
    EditorAssetBrowser_Rescan( browser.get() );
    rows = tree->findItems( QStringLiteral( "materials" ), Qt::MatchExactly | Qt::MatchRecursive );
    REQUIRE( rows.size() == 1 );
    CHECK_FALSE( rows.front()->isExpanded() );
    QCoreApplication::processEvents();
    const QRect item = tree->visualItemRect( rows.front() );
    REQUIRE( item.left() > 0 );
    const QRect branch( 0, item.top(), item.left(), item.height() );
    const QImage withLines = tree->viewport()->grab( branch ).toImage();
    settings.SetBool( "editor.ui.tree_lines", false );
    QCoreApplication::processEvents();
    const QImage withoutLines = tree->viewport()->grab( branch ).toImage();
    CHECK( withLines != withoutLines );
    CHECK_FALSE( rows.front()->isExpanded() );
    CHECK( tree->itemsExpandable() );
    auto *splitter = browser->findChild<QSplitter *>( QStringLiteral( "AssetBrowserSplitter" ) );
    REQUIRE( splitter != nullptr );
    const int originalWidth = tree->width();
    splitter->setSizes( { 120, 680 } );
    QCoreApplication::processEvents();
    CHECK( tree->width() < originalWidth ); // Wider default is not a fixed-width constraint.
}

TEST_CASE( "Material inspection shows the resolved source image without changing the active material", "[editor][gui][assets][preview]" )
{
    project_t p;
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main, p.extra } );
    activation_t activation;
    EditorAssetBrowser_SetActivate( browser.get(), &activation_t::Record, &activation );
    auto *preview = EditorAssetBrowser_Preview( browser.get(), QStringLiteral( "materials/test/brick.cymat" ) );
    REQUIRE( preview != nullptr );
    CHECK( activation.path.isEmpty() );
    auto *source = preview->findChild<QPlainTextEdit *>( QStringLiteral( "AssetPreviewSource" ) );
    REQUIRE( source != nullptr );
    CHECK( source->isReadOnly() );
    CHECK( source->toPlainText().contains( QStringLiteral( "shaders/surface.cyshader" ) ) ); // Winning content root.
    auto *image = preview->findChild<QWidget *>( QStringLiteral( "AssetPreviewImage" ) );
    REQUIRE( image != nullptr );
    CHECK( image->property( "decodedSize" ).toSize() == QSize( 8, 8 ) );
    auto *imageInfo = preview->findChild<QLabel *>( QStringLiteral( "AssetPreviewImageInfo" ) );
    REQUIRE( imageInfo != nullptr );
    CHECK( imageInfo->text().contains( QStringLiteral( "textures/test/brick.png" ) ) );
    CHECK( imageInfo->text().contains( QStringLiteral( "8 × 8 px" ) ) );
    auto *dependencies = preview->findChild<QTreeWidget *>( QStringLiteral( "AssetPreviewDependencies" ) );
    REQUIRE( dependencies != nullptr );
    const auto texture = dependencies->findItems( QStringLiteral( "textures/test/brick.cytex" ), Qt::MatchExactly );
    REQUIRE( texture.size() == 1 );
    CHECK( texture.front()->text( 1 ) == QStringLiteral( "Found" ) );
    const auto shader = dependencies->findItems( QStringLiteral( "shaders/surface.cyshader" ), Qt::MatchExactly );
    REQUIRE( shader.size() == 1 );
    CHECK( shader.front()->text( 1 ) == QStringLiteral( "Missing" ) );
    CHECK( EditorAssetBrowser_Preview( browser.get(), QStringLiteral( "materials/test/brick.cymat" ) ) == preview );
    // Reload follows an edited recipe, rather than showing the old image.
    WriteFile( p.main + QStringLiteral( "/materials/test/brick.cymat" ),
        "@cykv 1\n@schema \"cypher.material\" 1\n{ textures = { base_color = \"textures/test/wide.png\" } }" );
    QImage wide( 3072, 12, QImage::Format_RGB32 );
    wide.fill( Qt::blue );
    REQUIRE( wide.save( p.main + QStringLiteral( "/textures/test/wide.png" ) ) );
    preview->findChild<QPushButton *>( QStringLiteral( "AssetPreviewReload" ) )->click();
    CHECK( image->property( "decodedSize" ).toSize().width() <= 2048 );
    CHECK( imageInfo->text().contains( QStringLiteral( "wide.png" ) ) );
    CHECK( imageInfo->text().contains( QStringLiteral( "3072 × 12 px" ) ) );
    EditorAssetBrowser_Activate( browser.get(), QStringLiteral( "materials/test/brick.cymat" ) );
    CHECK( activation.path == QStringLiteral( "materials/test/brick.cymat" ) ); // Existing assignment action still works.
    CHECK( EditorAssetBrowser_Preview( browser.get(), QStringLiteral( "materials/absent.cymat" ) ) == nullptr );
    QPointer<QWidget> lifetime( preview );
    browser.reset();
    CHECK( lifetime.isNull() );
}

TEST_CASE( "Shader activation inspects real stage sources with search and reload", "[editor][gui][assets][preview]" )
{
    project_t p;
    const QString recipe = QStringLiteral( "shaders/surface.cyshader" );
    const QString stage = QStringLiteral( "shaders/surface.vert" );
    WriteFile( p.main + QLatin1Char( '/' ) + recipe,
        "@cykv 1\n@schema \"cypher.shader\" 1\n{ language = \"glsl\" vertex = \"shaders/surface.vert\" fragment = \"shaders/missing.frag\" }" );
    WriteFile( p.main + QLatin1Char( '/' ) + stage, "#version 450\nvoid main() { gl_Position = vec4(1.0); }\n" );
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main } );
    activation_t activation;
    EditorAssetBrowser_SetActivate( browser.get(), &activation_t::Record, &activation );
    EditorAssetBrowser_Activate( browser.get(), recipe );
    auto *preview = browser->findChild<QDialog *>( QStringLiteral( "AssetPreviewDialog" ) );
    REQUIRE( preview != nullptr );
    CHECK( activation.path.isEmpty() );
    auto *files = preview->findChild<QComboBox *>( QStringLiteral( "AssetPreviewFiles" ) );
    auto *source = preview->findChild<QPlainTextEdit *>( QStringLiteral( "AssetPreviewSource" ) );
    REQUIRE( files != nullptr );
    REQUIRE( source != nullptr );
    REQUIRE( files->findData( stage ) >= 0 );
    files->setCurrentIndex( files->findData( stage ) );
    CHECK( source->toPlainText().contains( QStringLiteral( "gl_Position" ) ) );
    CHECK( source->isReadOnly() );
    auto *find = preview->findChild<QLineEdit *>( QStringLiteral( "AssetPreviewFind" ) );
    REQUIRE( find != nullptr );
    find->setText( QStringLiteral( "gl_Position" ) );
    CHECK( source->textCursor().selectedText() == QStringLiteral( "gl_Position" ) );
    WriteFile( p.main + QLatin1Char( '/' ) + stage, "#version 450\nvoid changed() {}\n" );
    auto *reload = preview->findChild<QPushButton *>( QStringLiteral( "AssetPreviewReload" ) );
    REQUIRE( reload != nullptr );
    reload->click();
    CHECK( source->toPlainText().contains( QStringLiteral( "changed" ) ) );
    REQUIRE( files->findData( QStringLiteral( "shaders/missing.frag" ) ) >= 0 );
    files->setCurrentIndex( files->findData( QStringLiteral( "shaders/missing.frag" ) ) );
    CHECK( source->toPlainText().isEmpty() );
    CHECK( preview->findChild<QLabel *>( QStringLiteral( "AssetPreviewStatus" ) )->text().contains( QStringLiteral( "missing" ) ) );
    CHECK( activation.path.isEmpty() );
}

TEST_CASE( "Map inspection exposes authored structure and chunks without opening a document", "[editor][gui][assets][preview]" )
{
    project_t p;
    const QString path = QStringLiteral( "maps/room.cymap" );
    WriteFile( p.main + QLatin1Char( '/' ) + path,
        "@cykv 1\n@schema \"cypher.map\" 10\n{ name = \"Test Room\" layers = [ { id = \"structure\" name = \"Structure\" } ] }" );
    const QString chunk = QStringLiteral( "maps/room/structure/x0_y0.cymapchunk" );
    WriteFile( p.main + QLatin1Char( '/' ) + chunk,
        "@cykv 1\n@schema \"cypher.map_chunk\" 10\n{ brushes = [ { id = 1000u name = \"floor\" } ] entities = [] }" );
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main } );
    activation_t activation;
    EditorAssetBrowser_SetActivate( browser.get(), &activation_t::Record, &activation );
    EditorAssetBrowser_Search( browser.get(), QStringLiteral( "room" ) );
    const QStringList visible = EditorAssetBrowser_Visible( browser.get() );
    EditorAssetBrowser_Activate( browser.get(), path );
    auto *preview = browser->findChild<QDialog *>( QStringLiteral( "AssetPreviewDialog" ) );
    REQUIRE( preview != nullptr );
    CHECK( activation.path.isEmpty() );
    CHECK( EditorAssetBrowser_Visible( browser.get() ) == visible );
    auto *metadata = preview->findChild<QTreeWidget *>( QStringLiteral( "AssetPreviewMetadata" ) );
    REQUIRE( metadata != nullptr );
    const auto schemas = metadata->findItems( QStringLiteral( "Schema" ), Qt::MatchExactly );
    REQUIRE( schemas.size() == 1 );
    CHECK( schemas.front()->text( 1 ) == QStringLiteral( "cypher.map · version 10" ) );
    auto *files = preview->findChild<QComboBox *>( QStringLiteral( "AssetPreviewFiles" ) );
    REQUIRE( files != nullptr );
    REQUIRE( files->findData( chunk ) >= 0 );
    files->setCurrentIndex( files->findData( chunk ) );
    auto *structure = preview->findChild<QTreeWidget *>( QStringLiteral( "AssetPreviewStructure" ) );
    REQUIRE( structure != nullptr );
    const auto names = structure->findItems( QStringLiteral( "name" ), Qt::MatchExactly | Qt::MatchRecursive );
    REQUIRE( names.size() == 1 );
    CHECK( names.front()->text( 1 ) == QStringLiteral( "floor" ) );
    const auto brushes = metadata->findItems( QStringLiteral( "brushes" ), Qt::MatchExactly );
    REQUIRE( brushes.size() == 1 );
    CHECK( brushes.front()->text( 1 ) == QStringLiteral( "1 items" ) );
    CHECK( activation.path.isEmpty() );
}

TEST_CASE( "Asset inspection bounds large files and reports malformed recipes", "[editor][gui][assets][preview]" )
{
    project_t p;
    WriteFile( p.main + QStringLiteral( "/shaders/large.frag" ), QByteArray( 1024 * 1024 + 100, 'x' ) );
    WriteFile( p.main + QStringLiteral( "/materials/broken.cymat" ), "@cykv 1\n@schema \"cypher.material\" 1\n{ invalid = " );
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main } );
    auto *preview = EditorAssetBrowser_Preview( browser.get(), QStringLiteral( "shaders/large.frag" ) );
    REQUIRE( preview != nullptr );
    auto *source = preview->findChild<QPlainTextEdit *>( QStringLiteral( "AssetPreviewSource" ) );
    REQUIRE( source != nullptr );
    CHECK( source->toPlainText().size() == 1024 * 1024 );
    CHECK( preview->findChild<QLabel *>( QStringLiteral( "AssetPreviewStatus" ) )->text().contains( QStringLiteral( "first 1 MiB" ) ) );
    QPointer<QWidget> oldPreview( preview );
    preview = EditorAssetBrowser_Preview( browser.get(), QStringLiteral( "materials/broken.cymat" ) );
    REQUIRE( preview != nullptr );
    CHECK( oldPreview.isNull() );
    CHECK( preview->findChild<QLabel *>( QStringLiteral( "AssetPreviewStatus" ) )->text().contains( QStringLiteral( "could not be parsed" ) ) );
    CHECK( preview->findChild<QPlainTextEdit *>( QStringLiteral( "AssetPreviewSource" ) )->toPlainText().contains( QStringLiteral( "invalid =" ) ) );
    CHECK( preview->findChild<QTreeWidget *>( QStringLiteral( "AssetPreviewStructure" ) )->topLevelItemCount() == 0 );
}

TEST_CASE( "Asset inspectors retain unchanged roots and invalidate when mount resolution changes", "[editor][gui][assets][preview]" )
{
    project_t p;
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main, p.extra } );
    const QString path = QStringLiteral( "materials/test/brick.cymat" );
    QPointer<QWidget> original( EditorAssetBrowser_Preview( browser.get(), path ) );
    REQUIRE( original != nullptr );
    CHECK( original->findChild<QPlainTextEdit *>( QStringLiteral( "AssetPreviewSource" ) )->toPlainText().contains( QStringLiteral( "shaders/surface.cyshader" ) ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main, p.extra } );
    CHECK_FALSE( original.isNull() );
    CHECK( EditorAssetBrowser_Preview( browser.get(), path ) == original.data() );
    EditorAssetBrowser_SetRoots( browser.get(), { p.extra, p.main } );
    CHECK( original.isNull() );
    auto *replacement = EditorAssetBrowser_Preview( browser.get(), path );
    REQUIRE( replacement != nullptr );
    CHECK( replacement->findChild<QPlainTextEdit *>( QStringLiteral( "AssetPreviewSource" ) )->toPlainText().contains( QStringLiteral( "x.cyshader" ) ) );
    CHECK_FALSE( replacement->findChild<QPlainTextEdit *>( QStringLiteral( "AssetPreviewSource" ) )->toPlainText().contains( QStringLiteral( "shaders/surface.cyshader" ) ) );
}

TEST_CASE( "A deleted map preview never discovers chunks from the process working directory", "[editor][gui][assets][preview]" )
{
    project_t p;
    const QString path = QStringLiteral( "maps/vanished.cymap" );
    const QString rootFile = p.main + QLatin1Char( '/' ) + path;
    WriteFile( rootFile, "@cykv 1\n@schema \"cypher.map\" 10\n{}" );
    const QString unrelated = p.folder.filePath( QStringLiteral( "unrelated/structure/x0_y0.cymapchunk" ) );
    WriteFile( unrelated, "@cykv 1\n@schema \"cypher.map_chunk\" 10\n{ name = \"Unrelated\" }" );
    struct working_directory_t {
        QString previous{ QDir::currentPath() };
        ~working_directory_t() { ( void )QDir::setCurrent( previous ); }
    } cwd;
    REQUIRE( QDir::setCurrent( p.folder.filePath( QStringLiteral( "unrelated" ) ) ) );
    std::unique_ptr<QWidget> browser( EditorAssetBrowser_Create( nullptr, &p.gui ) );
    EditorAssetBrowser_SetRoots( browser.get(), { p.main } );
    REQUIRE( QFile::remove( rootFile ) ); // Catalogue still contains it until the next scan.
    auto *preview = EditorAssetBrowser_Preview( browser.get(), path );
    REQUIRE( preview != nullptr );
    auto *files = preview->findChild<QComboBox *>( QStringLiteral( "AssetPreviewFiles" ) );
    REQUIRE( files != nullptr );
    CHECK( files->count() == 1 );
    CHECK( files->currentData().toString() == path );
    CHECK( preview->findChild<QPlainTextEdit *>( QStringLiteral( "AssetPreviewSource" ) )->toPlainText().isEmpty() );
    CHECK( preview->findChild<QLabel *>( QStringLiteral( "AssetPreviewStatus" ) )->text().contains( QStringLiteral( "missing" ) ) );
}
