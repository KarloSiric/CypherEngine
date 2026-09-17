//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verify independent 2D panes, retained navigation, and one 3D host.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileViewWorkspace.h"

#include <catch2/catch_test_macros.hpp>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <vector>

using namespace cypher::tools::tile_editor;

namespace {
void EnsureDuplicateViewApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileDuplicateViewTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

std::array<QWidget *, 4> CreateViews()
{
    std::array<QWidget *, 4> views{};
    for ( int index = 0; index < 4; ++index ) {
        views[index] = new QWidget;
        views[index]->setFocusPolicy( Qt::StrongFocus );
        views[index]->setMinimumSize( 80, 60 );
        views[index]->setProperty( "navigation", 100 + index );
    }
    return views;
}

struct factory_t {
    std::vector<QPointer<QWidget>> widgets;
    QWidget *create( tile_editor_view_t view )
    {
        REQUIRE( view != tile_editor_view_t::PERSPECTIVE );
        auto *widget = new QWidget;
        widget->setFocusPolicy( Qt::StrongFocus );
        widget->setMinimumSize( 80, 60 );
        widget->setProperty( "type", static_cast<int>( view ) );
        widgets.push_back( widget );
        return widget;
    }
    void attach( CypherTileViewWorkspace &workspace )
    {
        workspace.setViewFactory( [this]( tile_editor_view_t view ) { return create( view ); } );
        workspace.setActivateOnHover( false );
    }
};

void CheckTypes( const CypherTileViewWorkspace &workspace,
    const std::array<tile_editor_view_t, 4> &expected )
{
    for ( int position = 0; position < 4; ++position ) CHECK( workspace.viewAtPosition( position ) == expected[position] );
}
constexpr std::array defaultTypes{ tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::TOP,
    tile_editor_view_t::FRONT, tile_editor_view_t::SIDE };
}

TEST_CASE( "Duplicate Top panes use independent widgets and retain state when their type changes",
    "[TileEditor][Workspace][DuplicateViews]" )
{
    EnsureDuplicateViewApplication();
    factory_t factory;
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    factory.attach( workspace );
    workspace.resize( 960, 700 );
    workspace.show();
    REQUIRE( workspace.setPaneView( 2, tile_editor_view_t::TOP ) );
    QWidget *duplicate = workspace.activeWidget();
    REQUIRE( duplicate != nullptr );
    CHECK( duplicate != views[0] );
    CHECK( workspace.activeView() == tile_editor_view_t::TOP );
    CheckTypes( workspace, { tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::TOP,
        tile_editor_view_t::TOP, tile_editor_view_t::SIDE } );
    duplicate->setProperty( "navigation", 912 );
    CHECK( views[0]->property( "navigation" ).toInt() == 100 );
    workspace.focusView( tile_editor_view_t::TOP );
    CHECK( workspace.activeWidget() == duplicate ); // Prefer the active duplicate.
    REQUIRE( workspace.setPaneView( 2, tile_editor_view_t::SIDE ) );
    CHECK( workspace.activeWidget() != duplicate );
    REQUIRE( workspace.setPaneView( 2, tile_editor_view_t::TOP ) );
    CHECK( workspace.activeWidget() == duplicate );
    CHECK( duplicate->property( "navigation" ).toInt() == 912 );
    CHECK( factory.widgets.size() == 2u );
    CHECK_FALSE( views[2]->isVisible() ); // The original Front remains cached.
    REQUIRE( workspace.setPaneView( 2, tile_editor_view_t::FRONT ) );
    CHECK( workspace.activeWidget() == views[2] );
    CHECK( views[2]->property( "navigation" ).toInt() == 102 );
}

TEST_CASE( "Header frame maximize and type choices address the clicked duplicate pane",
    "[TileEditor][Workspace][DuplicateViews]" )
{
    EnsureDuplicateViewApplication();
    factory_t factory;
    CypherTileViewWorkspace workspace( CreateViews() );
    factory.attach( workspace );
    workspace.resize( 960, 700 );
    workspace.show();
    REQUIRE( workspace.setPaneView( 2, tile_editor_view_t::TOP ) );
    QWidget *first = workspace.activeWidget();
    REQUIRE( workspace.setPaneView( 3, tile_editor_view_t::TOP ) );
    QWidget *second = workspace.activeWidget();
    QWidget *framed = nullptr;
    workspace.setFrameCallback( [&]( tile_editor_view_t view ) {
        CHECK( view == tile_editor_view_t::TOP );
        framed = workspace.activeWidget();
    } );
    auto *frame = workspace.findChild<QToolButton *>( "TileViewFrame2" );
    REQUIRE( frame != nullptr );
    frame->click();
    CHECK( framed == first );
    CHECK( workspace.activeWidget() == first );
    auto *maximize = workspace.findChild<QToolButton *>( "TileViewMaximize3" );
    REQUIRE( maximize != nullptr );
    maximize->click();
    CHECK( workspace.isMaximized() );
    CHECK( workspace.activeWidget() == second );
    CHECK( second->isVisible() );
    CHECK_FALSE( first->isVisible() );
    auto *choose = workspace.findChild<QToolButton *>( "TileViewChoose3" );
    REQUIRE( choose != nullptr );
    auto *front = choose->menu()->findChild<QAction *>( "TileViewChoose3To2" );
    REQUIRE( front != nullptr );
    front->trigger();
    CHECK( workspace.isMaximized() );
    CHECK( workspace.activeView() == tile_editor_view_t::FRONT );
    CHECK( workspace.activeWidget() != second );
    CHECK( choose->text() == QStringLiteral( "XZ FRONT" ) );
    workspace.showFourViews();
    CHECK( first->isVisible() );
}

TEST_CASE( "Choosing 3D relocates the one existing renderer or restores it from a hidden pane",
    "[TileEditor][Workspace][DuplicateViews]" )
{
    EnsureDuplicateViewApplication();
    factory_t factory;
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    factory.attach( workspace );
    QMenu cameraMenu;
    workspace.setCameraMenu( &cameraMenu );
    workspace.resize( 960, 700 );
    workspace.show();
    QWidget *originalParent = views[1]->parentWidget();
    REQUIRE( workspace.setPaneView( 3, tile_editor_view_t::PERSPECTIVE ) );
    CHECK( workspace.activeWidget() == views[1] );
    CHECK( views[1]->parentWidget() == originalParent );
    CHECK( factory.widgets.empty() );
    CheckTypes( workspace, { tile_editor_view_t::SIDE, tile_editor_view_t::TOP,
        tile_editor_view_t::FRONT, tile_editor_view_t::PERSPECTIVE } );
    REQUIRE( workspace.setPaneView( 3, tile_editor_view_t::TOP ) );
    CHECK_FALSE( views[1]->isVisible() );
    auto *cameraButton = workspace.findChild<QToolButton *>( "TileViewCameraOptions" );
    REQUIRE( cameraButton != nullptr );
    CHECK_FALSE( cameraButton->isVisible() );
    REQUIRE( workspace.setPaneView( 0, tile_editor_view_t::PERSPECTIVE ) );
    CHECK( workspace.activeWidget() == views[1] );
    CHECK( views[1]->parentWidget() != originalParent );
    CHECK( cameraButton->isVisible() );
    CHECK( cameraButton->parentWidget()->parentWidget() == views[1]->parentWidget() );
    CHECK( views[1]->property( "navigation" ).toInt() == 101 );
    int rendererPanes = 0;
    for ( int position = 0; position < 4; ++position )
        rendererPanes += workspace.viewAtPosition( position ) == tile_editor_view_t::PERSPECTIVE;
    CHECK( rendererPanes == 1 );
    CHECK( factory.widgets.size() == 1u );
}

TEST_CASE( "Focusing an absent semantic view replaces the active pane and factories cannot share 2D instances",
    "[TileEditor][Workspace][DuplicateViews]" )
{
    EnsureDuplicateViewApplication();
    factory_t factory;
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    factory.attach( workspace );
    for ( int position = 0; position < 4; ++position ) REQUIRE( workspace.setPaneView( position, tile_editor_view_t::TOP ) );
    workspace.focusView( tile_editor_view_t::FRONT );
    CHECK( workspace.activeView() == tile_editor_view_t::FRONT );
    CheckTypes( workspace, { tile_editor_view_t::TOP, tile_editor_view_t::TOP,
        tile_editor_view_t::TOP, tile_editor_view_t::FRONT } );
    QWidget *active = workspace.activeWidget();
    workspace.setViewFactory( [&]( tile_editor_view_t ) { return views[0]; } );
    CHECK_FALSE( workspace.setPaneView( 0, tile_editor_view_t::SIDE ) );
    CHECK( workspace.activeWidget() == active );
    CHECK( workspace.viewAtPosition( 0 ) == tile_editor_view_t::TOP );
    CHECK_FALSE( workspace.setPaneView( -1, tile_editor_view_t::TOP ) );
    CHECK_FALSE( workspace.setPaneView( 4, tile_editor_view_t::TOP ) );
    CHECK_FALSE( workspace.setPaneView( 0, static_cast<tile_editor_view_t>( 44 ) ) );
}

TEST_CASE( "Duplicate pane types persist independently from physical order and restore with a factory",
    "[TileEditor][Workspace][DuplicateViews]" )
{
    EnsureDuplicateViewApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QSettings settings( directory.filePath( "views.ini" ), QSettings::IniFormat );
    {
        factory_t factory;
        CypherTileViewWorkspace workspace( CreateViews() );
        factory.attach( workspace );
        REQUIRE( workspace.setPaneView( 2, tile_editor_view_t::TOP ) );
        REQUIRE( workspace.swapViews( tile_editor_view_t::TOP, tile_editor_view_t::SIDE ) );
        workspace.saveLayout( settings );
    }
    factory_t restoredFactory;
    CypherTileViewWorkspace restored( CreateViews() );
    restoredFactory.attach( restored );
    restored.restoreLayout( settings );
    CheckTypes( restored, { tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::TOP,
        tile_editor_view_t::SIDE, tile_editor_view_t::TOP } );
    REQUIRE( restored.setPaneView( 1, tile_editor_view_t::TOP ) );
    QWidget *first = restored.activeWidget();
    REQUIRE( restored.setPaneView( 3, tile_editor_view_t::TOP ) );
    CHECK( restored.activeWidget() != first );
    restored.resetViewOrder();
    CheckTypes( restored, defaultTypes );
}

TEST_CASE( "Invalid duplicate-view configurations reject multiple renderer hosts and restore default types",
    "[TileEditor][Workspace][DuplicateViews]" )
{
    EnsureDuplicateViewApplication();
    QTemporaryDir directory;
    QSettings settings( directory.filePath( "views.ini" ), QSettings::IniFormat );
    factory_t factory;
    CypherTileViewWorkspace workspace( CreateViews() );
    factory.attach( workspace );
    const std::array<QVariantList, 5> invalid{
        QVariantList{ 0, 1, 1, 3 }, QVariantList{ 0, 1, 9, 3 }, QVariantList{ 0, 1, -1, 3 },
        QVariantList{ 0, 1, 2 }, QVariantList{ 0, 1, 2.5, 3 } };
    for ( const auto &types : invalid ) {
        REQUIRE( workspace.setPaneView( 2, tile_editor_view_t::TOP ) );
        settings.setValue( "TileEditor/QuadView/types", types );
        workspace.restoreLayout( settings );
        CheckTypes( workspace, defaultTypes );
    }
}

TEST_CASE( "Cycling pane types keeps a bounded widget cache owned by the workspace",
    "[TileEditor][Workspace][DuplicateViews]" )
{
    EnsureDuplicateViewApplication();
    factory_t factory;
    {
        CypherTileViewWorkspace workspace( CreateViews() );
        factory.attach( workspace );
        for ( int repeat = 0; repeat < 5; ++repeat ) {
            for ( int pane = 0; pane < 4; ++pane ) {
                for ( const auto view : { tile_editor_view_t::TOP, tile_editor_view_t::FRONT, tile_editor_view_t::SIDE } )
                    REQUIRE( workspace.setPaneView( pane, view ) );
            }
        }
        CHECK( factory.widgets.size() == 9u ); // Twelve 2D cache entries minus three initial widgets.
        for ( const auto &widget : factory.widgets ) CHECK_FALSE( widget.isNull() );
    }
    for ( const auto &widget : factory.widgets ) CHECK( widget.isNull() );
}

TEST_CASE( "Semantic focus prefers a visible duplicate without reopening a closed pane",
    "[TileEditor][Workspace][DuplicateViews][Visibility]" )
{
    EnsureDuplicateViewApplication();
    factory_t factory;
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    factory.attach( workspace );
    workspace.resize( 960, 700 );
    workspace.show();
    QApplication::processEvents();

    REQUIRE( workspace.setPaneView( 2, tile_editor_view_t::TOP ) );
    QWidget *visibleDuplicate = workspace.activeWidget();
    REQUIRE( visibleDuplicate != nullptr );
    REQUIRE( visibleDuplicate != views[0] );
    REQUIRE( workspace.setPaneVisible( 1, false ) );
    CHECK( workspace.visiblePaneCount() == 3 );
    workspace.focusView( tile_editor_view_t::SIDE );
    REQUIRE( workspace.activeView() == tile_editor_view_t::SIDE );

    workspace.focusView( tile_editor_view_t::TOP );
    CHECK( workspace.activeWidget() == visibleDuplicate );
    CHECK( workspace.visiblePaneCount() == 3 );
    CHECK_FALSE( workspace.isPaneVisible( 1 ) );
}

TEST_CASE( "Closing and restoring the 3D pane retains its one renderer and parent",
    "[TileEditor][Workspace][DuplicateViews][Visibility]" )
{
    EnsureDuplicateViewApplication();
    factory_t factory;
    auto views = CreateViews();
    QPointer<QWidget> renderer = views[1];
    CypherTileViewWorkspace workspace( views );
    factory.attach( workspace );
    workspace.resize( 960, 700 );
    workspace.show();
    QApplication::processEvents();
    QWidget *const rendererParent = renderer->parentWidget();

    REQUIRE( workspace.setPaneVisible( 0, false ) );
    QApplication::processEvents();
    CHECK_FALSE( renderer.isNull() );
    CHECK_FALSE( renderer->isVisible() );
    CHECK( renderer->parentWidget() == rendererParent );
    CHECK( factory.widgets.empty() );

    REQUIRE( workspace.setPaneVisible( 0, true ) );
    QApplication::processEvents();
    CHECK_FALSE( renderer.isNull() );
    CHECK( renderer->isVisible() );
    CHECK( renderer->parentWidget() == rendererParent );
    CHECK( workspace.viewAtPosition( 0 ) == tile_editor_view_t::PERSPECTIVE );
    int rendererPanes = 0;
    for ( int position = 0; position < 4; ++position )
        rendererPanes += workspace.viewAtPosition( position ) == tile_editor_view_t::PERSPECTIVE;
    CHECK( rendererPanes == 1 );
    CHECK( factory.widgets.empty() );
}
