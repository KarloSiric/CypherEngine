//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Checks movable viewport panes without losing widget or layout state.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileViewWorkspace.h"

#include <catch2/catch_test_macros.hpp>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QSettings>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

using namespace cypher::tools::tile_editor;

namespace
{
void EnsureViewOrderApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileViewOrderTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

std::array<QWidget *, 4> CreateViews()
{
    std::array<QWidget *, 4> views{};
    for ( int index = 0; index < 4; ++index ) {
        views[index] = new QWidget();
        views[index]->setMinimumSize( 120, 80 );
        views[index]->setFocusPolicy( Qt::StrongFocus );
        views[index]->setProperty( "retainedNavigationState", 100 + index );
    }
    return views;
}

void CheckPhysicalOrder( CypherTileViewWorkspace &workspace,
                         const std::array<tile_editor_view_t, 4> &expected )
{
    for ( int position = 0; position < 4; ++position ) {
        const int semantic = static_cast<int>( expected[position] );
        CHECK( workspace.viewAtPosition( position ) == expected[position] );
        auto *row = workspace.findChild<QSplitter *>( QStringLiteral( "TileViewRow%1" ).arg( position / 2 ) );
        REQUIRE( row );
        CHECK( row->count() == 2 );
        CHECK( row->widget( position % 2 ) ==
               workspace.findChild<QWidget *>( QStringLiteral( "TileViewPane%1" ).arg( semantic ) ) );
    }
}

constexpr std::array kDefaultOrder{ tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::TOP,
                                   tile_editor_view_t::FRONT, tile_editor_view_t::SIDE };
}

TEST_CASE( "View title choices swap original widgets across rows and retain semantic controls",
           "[TileEditor][Workspace][ViewOrder]" )
{
    EnsureViewOrderApplication();
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    workspace.setActivateOnHover( false );
    workspace.resize( 960, 700 );
    workspace.show();
    QApplication::processEvents();
    CheckPhysicalOrder( workspace, kDefaultOrder );

    auto *choose = workspace.findChild<QToolButton *>( QStringLiteral( "TileViewChoose1" ) );
    REQUIRE( choose );
    REQUIRE( choose->menu() );
    REQUIRE( choose->menu()->actions().size() == 4 );
    auto *front = choose->menu()->findChild<QAction *>( QStringLiteral( "TileViewChoose1To2" ) );
    REQUIRE( front );
    front->trigger();
    QApplication::processEvents();
    CheckPhysicalOrder( workspace, { tile_editor_view_t::FRONT, tile_editor_view_t::TOP,
                                     tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::SIDE } );
    CHECK( workspace.activeView() == tile_editor_view_t::FRONT );
    for ( int index = 0; index < 4; ++index ) {
        const auto *pane = workspace.findChild<QWidget *>( QStringLiteral( "TileViewPane%1" ).arg( index ) );
        REQUIRE( pane );
        CHECK( views[index]->parentWidget() == pane );
        CHECK( views[index]->property( "retainedNavigationState" ).toInt() == 100 + index );
        CHECK( views[index]->isVisible() );
    }
    int framed = -1;
    workspace.setFrameCallback( [&]( tile_editor_view_t view ) { framed = static_cast<int>( view ); } );
    auto *frame = workspace.findChild<QToolButton *>( QStringLiteral( "TileViewFrame1" ) );
    REQUIRE( frame );
    frame->click();
    CHECK( framed == static_cast<int>( tile_editor_view_t::PERSPECTIVE ) );
    CHECK( workspace.activeView() == tile_editor_view_t::PERSPECTIVE );
}

TEST_CASE( "Swapping a maximized pane displays its requested replacement then restores all panes",
           "[TileEditor][Workspace][ViewOrder]" )
{
    EnsureViewOrderApplication();
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    workspace.setActivateOnHover( false );
    workspace.resize( 960, 700 );
    workspace.show();
    QApplication::processEvents();
    const QSize original = views[1]->size();
    workspace.focusView( tile_editor_view_t::PERSPECTIVE );
    workspace.toggleMaximize();
    REQUIRE( workspace.swapViews( tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::SIDE ) );
    QApplication::processEvents();
    CHECK( workspace.isMaximized() );
    CHECK( workspace.activeView() == tile_editor_view_t::SIDE );
    for ( int index = 0; index < 4; ++index ) CHECK( views[index]->isVisible() == ( index == 3 ) );
    CHECK( views[3]->height() > original.height() );
    CHECK( views[3]->width() > original.width() );
    workspace.showFourViews();
    QApplication::processEvents();
    CheckPhysicalOrder( workspace, { tile_editor_view_t::SIDE, tile_editor_view_t::TOP,
                                     tile_editor_view_t::FRONT, tile_editor_view_t::PERSPECTIVE } );
    for ( QWidget *view : views ) CHECK( view->isVisible() );
    CHECK( views[3]->size() == original );

    // Semantic shortcut focus must still restore the quad before selecting another pane.
    workspace.toggleMaximize();
    workspace.focusView( tile_editor_view_t::PERSPECTIVE );
    CHECK_FALSE( workspace.isMaximized() );
    CHECK( workspace.activeView() == tile_editor_view_t::PERSPECTIVE );
}

TEST_CASE( "Reordered panes persist alongside splitter sizes while a view is maximized",
           "[TileEditor][Workspace][ViewOrder]" )
{
    EnsureViewOrderApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "layout.ini" ) );
    QList<int> expectedWidths;
    {
        auto views = CreateViews();
        CypherTileViewWorkspace workspace( views );
        workspace.resize( 960, 700 );
        workspace.show();
        QApplication::processEvents();
        REQUIRE( workspace.swapViews( tile_editor_view_t::TOP, tile_editor_view_t::SIDE ) );
        auto *row = workspace.findChild<QSplitter *>( QStringLiteral( "TileViewRow0" ) );
        REQUIRE( row );
        row->setSizes( { 600, 350 } );
        expectedWidths = row->sizes();
        workspace.toggleMaximize();
        QSettings settings( path, QSettings::IniFormat );
        workspace.saveLayout( settings );
        settings.sync();
    }
    auto restoredViews = CreateViews();
    CypherTileViewWorkspace restored( restoredViews );
    restored.resize( 960, 700 );
    restored.show();
    QApplication::processEvents();
    QSettings settings( path, QSettings::IniFormat );
    restored.restoreLayout( settings );
    QApplication::processEvents();
    CheckPhysicalOrder( restored, { tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::SIDE,
                                    tile_editor_view_t::FRONT, tile_editor_view_t::TOP } );
    CHECK_FALSE( restored.isMaximized() );
    const auto *row = restored.findChild<QSplitter *>( QStringLiteral( "TileViewRow0" ) );
    REQUIRE( row );
    CHECK( row->sizes() == expectedWidths );
    for ( QWidget *view : restoredViews ) CHECK( view->isVisible() );
}

TEST_CASE( "Malformed stored pane orders restore the default without duplicating views",
           "[TileEditor][Workspace][ViewOrder]" )
{
    EnsureViewOrderApplication();
    QTemporaryDir directory;
    QSettings settings( directory.filePath( QStringLiteral( "layout.ini" ) ), QSettings::IniFormat );
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    const std::array<QVariantList, 6> malformed{
        QVariantList{ 0, 0, 2, 3 }, QVariantList{ 0, 1, 2, 4 },
        QVariantList{ 0, 1, 2, -1 }, QVariantList{ 0, 1, 2 },
        QVariantList{ 0, 1, 2, QStringLiteral( "invalid" ) }, QVariantList{ 0, 1, 2, 3.25 } };
    for ( const auto &order : malformed ) {
        REQUIRE( workspace.swapViews( tile_editor_view_t::TOP, tile_editor_view_t::FRONT ) );
        settings.setValue( QStringLiteral( "TileEditor/QuadView/order" ), order );
        workspace.restoreLayout( settings );
        CheckPhysicalOrder( workspace, kDefaultOrder );
    }
    CHECK_FALSE( workspace.swapViews( static_cast<tile_editor_view_t>( 99 ), tile_editor_view_t::TOP ) );
    CheckPhysicalOrder( workspace, kDefaultOrder );
}

TEST_CASE( "Resetting view order retains the maximized view and all widget instances",
           "[TileEditor][Workspace][ViewOrder]" )
{
    EnsureViewOrderApplication();
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    workspace.setActivateOnHover( false );
    workspace.resize( 960, 700 );
    workspace.show();
    QApplication::processEvents();
    REQUIRE( workspace.swapViews( tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::FRONT ) );
    workspace.toggleMaximize();
    workspace.resetViewOrder();
    QApplication::processEvents();
    CheckPhysicalOrder( workspace, kDefaultOrder );
    CHECK( workspace.isMaximized() );
    CHECK( workspace.activeView() == tile_editor_view_t::FRONT );
    for ( int index = 0; index < 4; ++index ) CHECK( views[index]->isVisible() == ( index == 2 ) );
    workspace.showFourViews();
    for ( QWidget *view : views ) CHECK( view->isVisible() );
}

TEST_CASE( "Splitter width and viewport numbers remain physical across pane reordering",
           "[TileEditor][Workspace][ViewOrder]" )
{
    EnsureViewOrderApplication();
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    workspace.resize( 960, 700 );
    workspace.show();
    QApplication::processEvents();

    CHECK( workspace.splitterWidth() == 6 );
    workspace.setSplitterWidth( 11 );
    CHECK( workspace.splitterWidth() == 11 );
    const auto splitters = workspace.findChildren<QSplitter *>();
    REQUIRE( splitters.size() == 3 );
    for ( const auto *splitter : splitters ) CHECK( splitter->handleWidth() == 11 );
    workspace.setSplitterWidth( 1 );
    CHECK( workspace.splitterWidth() == 3 );
    workspace.setSplitterWidth( 99 );
    CHECK( workspace.splitterWidth() == 16 );

    const auto number = [&]( int stablePane ) {
        const auto *label = workspace.findChild<QLabel *>(
            QStringLiteral( "TileViewNumber%1" ).arg( stablePane ) );
        REQUIRE( label != nullptr );
        return label->text();
    };
    CHECK( number( 1 ) == QStringLiteral( "1" ) );
    CHECK( number( 0 ) == QStringLiteral( "2" ) );
    CHECK( number( 2 ) == QStringLiteral( "3" ) );
    CHECK( number( 3 ) == QStringLiteral( "4" ) );

    REQUIRE( workspace.swapViews(
        tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::SIDE ) );
    CHECK( number( 3 ) == QStringLiteral( "1" ) );
    CHECK( number( 0 ) == QStringLiteral( "2" ) );
    CHECK( number( 2 ) == QStringLiteral( "3" ) );
    CHECK( number( 1 ) == QStringLiteral( "4" ) );
}

TEST_CASE( "Q3Edit viewport gutters stay draggable while panes use compact overlay chrome",
           "[TileEditor][Workspace][Separators]" )
{
    EnsureViewOrderApplication();
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    workspace.setActivateOnHover( false );
    workspace.resize( 960, 700 );
    workspace.show();
    QApplication::processEvents();

    REQUIRE( workspace.layout() != nullptr );
    CHECK( workspace.layout()->contentsMargins() == QMargins( 0, 0, 0, 0 ) );
    CHECK( workspace.layout()->spacing() == 0 );
    CHECK( workspace.splitterWidth() == 6 );

    const auto splitters = workspace.findChildren<QSplitter *>();
    REQUIRE( splitters.size() == 3 );
    for ( auto *splitter : splitters ) {
        REQUIRE( splitter != nullptr );
        INFO( splitter->objectName().toStdString() );
        CHECK_FALSE( splitter->childrenCollapsible() );
        CHECK( splitter->opaqueResize() );
        CHECK( splitter->handleWidth() == 6 );
        REQUIRE( splitter->count() == 2 );
        auto *handle = splitter->handle( 1 );
        REQUIRE( handle != nullptr );
        CHECK( handle->isVisible() );
        CHECK( handle->isEnabled() );
        if ( splitter->orientation() == Qt::Horizontal ) {
            CHECK( handle->width() == 6 );
            CHECK( handle->cursor().shape() == Qt::SplitHCursor );
        } else {
            CHECK( handle->height() == 6 );
            CHECK( handle->cursor().shape() == Qt::SplitVCursor );
        }

        // The gutter must resize both sides without allowing an accidental
        // zero-sized viewport. This is the behavior used by pointer dragging.
        splitter->setSizes( { 0, 10000 } );
        QApplication::processEvents();
        const auto resized = splitter->sizes();
        REQUIRE( resized.size() == 2 );
        CHECK( resized[0] > 0 );
        CHECK( resized[1] > 0 );
        splitter->setSizes( { 1, 1 } );
    }

    for ( int pane = 0; pane < 4; ++pane ) {
        auto *frame = workspace.findChild<QWidget *>(
            QStringLiteral( "TileViewPane%1" ).arg( pane ) );
        REQUIRE( frame != nullptr );
        CHECK( frame->property( "tileViewPane" ).toBool() );
        CHECK( frame->testAttribute( Qt::WA_StyledBackground ) );
        auto *layout = qobject_cast<QGridLayout *>( frame->layout() );
        REQUIRE( layout != nullptr );
        CHECK( layout->contentsMargins() == QMargins( 0, 0, 0, 0 ) );
        CHECK( layout->spacing() == 0 );

        auto *header = frame->findChild<QWidget *>(
            QStringLiteral( "TileViewHeader" ), Qt::FindDirectChildrenOnly );
        REQUIRE( header != nullptr );
        CHECK( header->property( "tileViewOverlayHeader" ).toBool() );
        CHECK( header->sizePolicy().horizontalPolicy() == QSizePolicy::Maximum );
        CHECK( header->sizePolicy().verticalPolicy() == QSizePolicy::Fixed );

        int viewRow = -1, viewColumn = -1, viewRowSpan = -1, viewColumnSpan = -1;
        int headerRow = -1, headerColumn = -1, headerRowSpan = -1, headerColumnSpan = -1;
        layout->getItemPosition( layout->indexOf( views[pane] ),
            &viewRow, &viewColumn, &viewRowSpan, &viewColumnSpan );
        layout->getItemPosition( layout->indexOf( header ),
            &headerRow, &headerColumn, &headerRowSpan, &headerColumnSpan );
        CHECK( viewRow == 0 );
        CHECK( viewColumn == 0 );
        CHECK( headerRow == 0 );
        CHECK( headerColumn == 0 );
        CHECK( header->width() < frame->width() );
    }
}

TEST_CASE( "Viewport visibility reaches one pane and maximize restores only the authored set",
           "[TileEditor][Workspace][Visibility]" )
{
    EnsureViewOrderApplication();
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    workspace.setActivateOnHover( false );
    workspace.resize( 960, 700 );
    workspace.show();
    workspace.activateWindow();
    workspace.focusView( tile_editor_view_t::PERSPECTIVE );
    QApplication::processEvents();

    REQUIRE( workspace.setPaneVisible( 0, false ) );
    QApplication::processEvents();
    CHECK( workspace.visiblePaneCount() == 3 );
    CHECK_FALSE( workspace.isPaneVisible( 0 ) );
    CHECK( workspace.activeView() == tile_editor_view_t::TOP );
    CHECK( views[0]->hasFocus() );

    REQUIRE( workspace.setPaneVisible( 1, false ) );
    REQUIRE( workspace.setPaneVisible( 2, false ) );
    QApplication::processEvents();
    CHECK( workspace.visiblePaneCount() == 1 );
    CHECK( workspace.activeView() == tile_editor_view_t::SIDE );
    CHECK_FALSE( workspace.setPaneVisible( 3, false ) );
    auto *lastClose = workspace.findChild<QToolButton *>( QStringLiteral( "TileViewClose3" ) );
    REQUIRE( lastClose != nullptr );
    CHECK_FALSE( lastClose->isEnabled() );
    CHECK_FALSE( workspace.setPaneVisible( -1, true ) );
    CHECK_FALSE( workspace.setPaneVisible( 4, true ) );

    auto *topRow = workspace.findChild<QSplitter *>( QStringLiteral( "TileViewRow0" ) );
    auto *bottomRow = workspace.findChild<QSplitter *>( QStringLiteral( "TileViewRow1" ) );
    REQUIRE( topRow != nullptr );
    REQUIRE( bottomRow != nullptr );
    CHECK_FALSE( topRow->isVisible() );
    CHECK( bottomRow->isVisible() );

    REQUIRE( workspace.setPaneVisible( 0, true ) );
    CHECK( workspace.visiblePaneCount() == 2 );
    workspace.toggleMaximize();
    QApplication::processEvents();
    CHECK( workspace.isMaximized() );
    CHECK( views[3]->isVisible() );
    CHECK_FALSE( views[1]->isVisible() );
    workspace.toggleMaximize();
    QApplication::processEvents();
    CHECK_FALSE( workspace.isMaximized() );
    CHECK( views[1]->isVisible() );
    CHECK( views[3]->isVisible() );
    CHECK_FALSE( views[0]->isVisible() );
    CHECK_FALSE( views[2]->isVisible() );

    workspace.showFourViews();
    QApplication::processEvents();
    CHECK( workspace.visiblePaneCount() == 4 );
    for ( int position = 0; position < 4; ++position ) {
        CHECK( workspace.isPaneVisible( position ) );
        CHECK( views[static_cast<int>( kDefaultOrder[position] )]->isVisible() );
    }
}

TEST_CASE( "Hidden viewport persistence retains fallback focus and complete splitter geometry",
           "[TileEditor][Workspace][Visibility][Persistence]" )
{
    EnsureViewOrderApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "visibility.ini" ) );
    QList<int> expectedWidths;
    {
        auto views = CreateViews();
        CypherTileViewWorkspace workspace( views );
        workspace.resize( 960, 700 );
        workspace.show();
        QApplication::processEvents();
        auto *row = workspace.findChild<QSplitter *>( QStringLiteral( "TileViewRow0" ) );
        REQUIRE( row != nullptr );
        row->setSizes( { 610, 330 } );
        expectedWidths = row->sizes();
        workspace.focusView( tile_editor_view_t::PERSPECTIVE );
        REQUIRE( workspace.setPaneVisible( 0, false ) );
        workspace.toggleMaximize();
        REQUIRE( workspace.isMaximized() );
        QSettings settings( path, QSettings::IniFormat );
        workspace.saveLayout( settings );
        settings.sync();
    }

    auto views = CreateViews();
    CypherTileViewWorkspace restored( views );
    restored.resize( 960, 700 );
    restored.show();
    QApplication::processEvents();
    QSettings settings( path, QSettings::IniFormat );
    restored.restoreLayout( settings );
    QApplication::processEvents();
    CHECK_FALSE( restored.isMaximized() );
    CHECK( restored.visiblePaneCount() == 3 );
    CHECK_FALSE( restored.isPaneVisible( 0 ) );
    CHECK( restored.activeView() == tile_editor_view_t::TOP );
    CHECK_FALSE( views[1]->isVisible() );

    restored.showFourViews();
    QApplication::processEvents();
    auto *row = restored.findChild<QSplitter *>( QStringLiteral( "TileViewRow0" ) );
    REQUIRE( row != nullptr );
    CHECK( row->sizes() == expectedWidths );
    for ( const int size : row->sizes() ) CHECK( size > 0 );

    for ( const QVariantList malformed : {
             QVariantList{ false, false, false, false },
             QVariantList{ true, false, true },
             QVariantList{ true, false, QStringLiteral( "sometimes" ), true } } ) {
        settings.setValue( QStringLiteral( "TileEditor/QuadView/visiblePanes" ), malformed );
        REQUIRE( restored.setPaneVisible( 0, false ) );
        restored.restoreLayout( settings );
        CHECK( restored.visiblePaneCount() == 4 );
    }
}

TEST_CASE( "Header context menu targets its pane and exposes contributed view commands",
           "[TileEditor][Workspace][ContextMenu]" )
{
    EnsureViewOrderApplication();
    auto views = CreateViews();
    CypherTileViewWorkspace workspace( views );
    workspace.setActivateOnHover( false );
    workspace.resize( 960, 700 );
    workspace.show();
    QApplication::processEvents();

    int framed = 0;
    bool sawMenu = false;
    bool sawCheckedPerspective = false;
    bool sawContributor = false;
    workspace.setFrameCallback( [&]( tile_editor_view_t view ) {
        if ( view == tile_editor_view_t::PERSPECTIVE ) ++framed;
    } );
    workspace.setContextMenuContributor( [&]( QMenu &menu, tile_editor_view_t view ) {
        sawContributor = view == tile_editor_view_t::PERSPECTIVE;
        QAction *shared = menu.addAction( QStringLiteral( "Shared View Option" ) );
        shared->setObjectName( QStringLiteral( "TestSharedViewOption" ) );
    } );

    auto *header = views[1]->parentWidget()->findChild<QWidget *>(
        QStringLiteral( "TileViewHeader" ), Qt::FindDirectChildrenOnly );
    REQUIRE( header != nullptr );
    QTimer::singleShot( 0, [&] {
        auto *menu = workspace.findChild<QMenu *>( QStringLiteral( "TileViewContextMenu" ) );
        if ( menu == nullptr ) return;
        sawMenu = true;
        auto *perspective = menu->findChild<QAction *>( QStringLiteral( "TileViewContextType1" ) );
        sawCheckedPerspective = perspective != nullptr && perspective->isChecked();
        if ( auto *frame = menu->findChild<QAction *>( QStringLiteral( "TileViewContextFrame" ) ) )
            frame->trigger();
        if ( auto *close = menu->findChild<QAction *>( QStringLiteral( "TileViewContextClose" ) ) )
            close->trigger();
        menu->close();
    } );
    QContextMenuEvent event( QContextMenuEvent::Mouse, QPoint( 4, 4 ),
        header->mapToGlobal( QPoint( 4, 4 ) ) );
    CHECK( QApplication::sendEvent( header, &event ) );
    CHECK( sawMenu );
    CHECK( sawCheckedPerspective );
    CHECK( sawContributor );
    CHECK( framed == 1 );
    CHECK( workspace.visiblePaneCount() == 3 );
    CHECK_FALSE( workspace.isPaneVisible( 0 ) );
    CHECK( workspace.activeView() == tile_editor_view_t::TOP );

    bool maximizedCloseEnabled = false;
    workspace.toggleMaximize();
    REQUIRE( workspace.isMaximized() );
    auto *topHeader = views[0]->parentWidget()->findChild<QWidget *>(
        QStringLiteral( "TileViewHeader" ), Qt::FindDirectChildrenOnly );
    REQUIRE( topHeader != nullptr );
    QTimer::singleShot( 0, [&] {
        auto *menu = workspace.findChild<QMenu *>( QStringLiteral( "TileViewContextMenu" ) );
        if ( menu == nullptr ) return;
        if ( auto *close = menu->findChild<QAction *>( QStringLiteral( "TileViewContextClose" ) ) )
            maximizedCloseEnabled = close->isEnabled();
        menu->close();
    } );
    QContextMenuEvent maximizedEvent( QContextMenuEvent::Mouse, QPoint( 4, 4 ),
        topHeader->mapToGlobal( QPoint( 4, 4 ) ) );
    CHECK( QApplication::sendEvent( topHeader, &maximizedEvent ) );
    CHECK( maximizedCloseEnabled );
    workspace.toggleMaximize();
    CHECK_FALSE( workspace.isMaximized() );
    CHECK_FALSE( workspace.isPaneVisible( 0 ) );
}
