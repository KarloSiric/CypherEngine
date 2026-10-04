// CypherEngine Source Code. Copyright (c) 2026 Karlo Siric. All rights reserved.
// Exercises session-file corruption against the real ADS layout, before restore
// can detach panels or replace a live view and discard its camera state.
#include "CypherMason_MainWindow.h"
#include "CypherMason_WorkspaceState.h"
#include "CypherMapGui_Views.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "DockAreaWidget.h"
#include "DockManager.h"
#include "DockWidget.h"

#include <catch2/catch_test_macros.hpp>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QSplitter>
#include <QTemporaryDir>
#include <QToolButton>
#include <cmath>

using namespace cypher::common;
using namespace cypher::mason;
using namespace cypher::editor::map;

namespace
{
struct workspace_session_t {
    workspace_session_t()
    {
        pMason = Mason_Create( qobject_cast<QApplication *>( QCoreApplication::instance() ), Allocator_GetSystem(), MASON_FLAG_HEADLESS );
        REQUIRE( pMason != nullptr );
        pWindow = Mason_Window( pMason );
        pWindow->resize( 1400, 900 );
        pWindow->show();
        QCoreApplication::processEvents();
        pDocks = pWindow->findChild<ads::CDockManager *>();
        pViews = pWindow->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
        REQUIRE( pDocks != nullptr );
        REQUIRE( pViews != nullptr );
        REQUIRE( directory.isValid() );
        path = directory.filePath( QStringLiteral( "workspace.json" ) );
    }
    ~workspace_session_t() { Mason_Destroy( pMason ); }

    QJsonObject Save() const
    {
        REQUIRE( MasonWorkspace_Save( pWindow, pDocks, pViews, path ) == workspace_state_result_t::OK );
        QFile file( path );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        return QJsonDocument::fromJson( file.readAll() ).object();
    }
    void Write( const QJsonObject &state ) const
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        const QByteArray bytes = QJsonDocument( state ).toJson();
        REQUIRE( file.write( bytes ) == bytes.size() );
    }
    workspace_state_result_t Restore() const { return MasonWorkspace_Restore( pWindow, pDocks, pViews, path ); }

    mason_t *pMason{};
    QMainWindow *pWindow{};
    ads::CDockManager *pDocks{};
    QWidget *pViews{};
    QTemporaryDir directory;
    QString path;
};

QByteArray Field( const QJsonObject &state, const char *key )
{
    return QByteArray::fromBase64( state.value( QLatin1String( key ) ).toString().toLatin1() );
}

void PutField( QJsonObject &state, const char *key, const QByteArray &value )
{
    state.insert( QLatin1String( key ), QString::fromLatin1( value.toBase64() ) );
}

void CheckPresentationProportions( const QByteArray &actual, const QByteArray &expected )
{
    QJsonObject actualState = QJsonDocument::fromJson( actual ).object();
    QJsonObject expectedState = QJsonDocument::fromJson( expected ).object();
    const QJsonArray actualSizes = actualState.take( QStringLiteral( "splitters" ) ).toArray();
    const QJsonArray expectedSizes = expectedState.take( QStringLiteral( "splitters" ) ).toArray();
    CHECK( actualState == expectedState );
    REQUIRE( actualSizes.size() == expectedSizes.size() );
    // Dock migration and restoreGeometry can resize the viewport. Its split
    // positions are proportions; serialized pixel lengths need not match.
    for ( int splitter = 0; splitter < actualSizes.size(); ++splitter ) {
        const QJsonArray a = actualSizes[splitter].toArray();
        const QJsonArray e = expectedSizes[splitter].toArray();
        REQUIRE( a.size() == e.size() );
        if ( a.isEmpty() ) { continue; }
        double totalA = 0.0, totalE = 0.0;
        for ( int i = 0; i < a.size(); ++i ) { totalA += a[i].toDouble(); totalE += e[i].toDouble(); }
        REQUIRE( totalA > 0.0 );
        REQUIRE( totalE > 0.0 );
        for ( int i = 0; i < a.size(); ++i ) {
            CHECK( std::fabs( a[i].toDouble() / totalA - e[i].toDouble() / totalE ) < 0.02 );
        }
    }
}
}

TEST_CASE( "Mason rejects corrupt ADS snapshots before changing panels or cameras", "[mason][workspace-state]" )
{
    workspace_session_t session;
    QJsonObject state = session.Save();
    QByteArray xml = Field( state, "docks" );
    REQUIRE( xml.startsWith( "<?xml" ) );
    const QByteArray oldDocks = session.pDocks->saveState( 1 );
    const QByteArray oldViews = MapViews_SavePresentation( session.pViews );
    const QPointer<QWidget> oldCamera = MapViews_PaneView( session.pViews, 0 );

    SECTION( "truncated XML accepted by the ADS format check" )
    {
        const qsizetype closing = xml.indexOf( "</QtAdvancedDockingSystem>" );
        REQUIRE( closing >= 0 );
        xml.truncate( closing );
    }
    SECTION( "zero containers would produce a negative floating index" )
    {
        const qsizetype start = xml.indexOf( "<Container " );
        const qsizetype end = xml.indexOf( "</QtAdvancedDockingSystem>" );
        REQUIRE( start >= 0 );
        REQUIRE( end > start );
        xml.remove( start, end - start );
        xml.replace( "Containers=\"1\"", "Containers=\"0\"" );
    }
    SECTION( "unknown dock IDs cannot silently close registered panels" )
    {
        REQUIRE( xml.contains( "Name=\"problems\"" ) );
        xml.replace( "Name=\"problems\"", "Name=\"obsolete.problems\"" );
    }
    SECTION( "a retired asset dock cannot substitute for a required current dock" )
    {
        REQUIRE( xml.contains( "Name=\"problems\"" ) );
        xml.replace( "Name=\"problems\"", "Name=\"assets\"" );
    }
    SECTION( "missing dock IDs cannot silently close registered panels" )
    {
        QString text = QString::fromUtf8( xml );
        const QRegularExpression widget( QStringLiteral( "<Widget[^>]*Name=\"problems\"[^>]*/>" ) );
        REQUIRE( widget.match( text ).hasMatch() );
        text.remove( widget );
        xml = text.toUtf8();
    }
    SECTION( "duplicate dock IDs cannot detach a panel twice" )
    {
        REQUIRE( xml.contains( "Name=\"console\"" ) );
        xml.replace( "Name=\"console\"", "Name=\"problems\"" );
    }
    SECTION( "invalid splitter sizes never reach ADS text parsing" )
    {
        const qsizetype start = xml.indexOf( "<Sizes>" );
        const qsizetype end = xml.indexOf( "</Sizes>", start );
        REQUIRE( start >= 0 );
        REQUIRE( end > start );
        xml.replace( start + 7, end - start - 7, "invalid " );
    }
    SECTION( "compressed input is not decompressed from disk" ) { xml = qCompress( xml ); }
    SECTION( "trailing XML is rejected" ) { xml.append( "<unexpected/>" ); }

    PutField( state, "docks", xml );
    session.Write( state );
    CHECK( session.Restore() == workspace_state_result_t::INVALID );
    CHECK( session.pDocks->saveState( 1 ) == oldDocks );
    CHECK( MapViews_SavePresentation( session.pViews ) == oldViews );
    REQUIRE( !oldCamera.isNull() );
    CHECK( MapViews_PaneView( session.pViews, 0 ) == oldCamera );
}

TEST_CASE( "Mason starts with Console collapsed beside Object Properties and assets in a window", "[mason][workspace-state]" )
{
    workspace_session_t session;
    auto *pConsole = session.pDocks->findDockWidget( QStringLiteral( "console" ) );
    auto *pProperties = session.pDocks->findDockWidget( QStringLiteral( "map.properties" ) );
    REQUIRE( pConsole != nullptr );
    REQUIRE( pProperties != nullptr );
    REQUIRE( pProperties->dockAreaWidget() != nullptr );
    CHECK( pConsole->isClosed() );
    CHECK( pConsole->dockAreaWidget() == pProperties->dockAreaWidget() );
    CHECK( session.pDocks->findDockWidget( QStringLiteral( "assets" ) ) == nullptr );

    auto *pButton = session.pWindow->findChild<QToolButton *>( QStringLiteral( "view.assets.status" ) );
    REQUIRE( pButton != nullptr );
    REQUIRE( pButton->defaultAction() != nullptr );
    CHECK_FALSE( pButton->defaultAction()->isCheckable() );
    CHECK( session.pWindow->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) ) == nullptr );
    pButton->click();
    QCoreApplication::processEvents();
    auto *pAssetWindow = session.pWindow->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) );
    REQUIRE( pAssetWindow != nullptr );
    CHECK( pAssetWindow->isVisible() );
    pAssetWindow->close();
    pButton->click();
    CHECK( session.pWindow->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) ) == pAssetWindow );
    CHECK( pAssetWindow->isVisible() );
    CHECK( session.pDocks->findDockWidget( QStringLiteral( "assets" ) ) == nullptr );
}

TEST_CASE( "Mason console policy relocates saved open consoles without replacing views", "[mason][workspace-state]" )
{
    workspace_session_t session;
    session.pWindow->setGeometry( session.pWindow->screen()->availableGeometry().adjusted( 24, 40, -24, -24 ) );
    QCoreApplication::processEvents();
    auto *pConsole = session.pDocks->findDockWidget( QStringLiteral( "console" ) );
    auto *pProperties = session.pDocks->findDockWidget( QStringLiteral( "map.properties" ) );
    auto *pCentral = session.pDocks->centralWidget();
    REQUIRE( pConsole != nullptr );
    REQUIRE( pProperties != nullptr );
    REQUIRE( pCentral != nullptr );
    const QPointer<QWidget> oldCamera = MapViews_PaneView( session.pViews, 0 );
    bool bCollapseConsole = true;
    SECTION( "old bottom Console" )
    {
        REQUIRE( session.pDocks->addDockWidget( ads::BottomDockWidgetArea, pConsole, pCentral->dockAreaWidget() ) != nullptr );
    }
    SECTION( "old floating Console" )
    {
        REQUIRE( session.pDocks->addDockWidgetFloating( pConsole ) != nullptr );
    }
    SECTION( "explicit restore retains an open Console" )
    {
        bCollapseConsole = false;
        REQUIRE( session.pDocks->addDockWidget( ads::BottomDockWidgetArea, pConsole, pCentral->dockAreaWidget() ) != nullptr );
    }
    pConsole->toggleView( true );
    QCoreApplication::processEvents();
    const QByteArray oldViews = Field( session.Save(), "views" );
    pConsole->toggleView( false );
    REQUIRE( session.Restore() == workspace_state_result_t::OK );
    REQUIRE_FALSE( pConsole->isClosed() );
    REQUIRE( pConsole->dockAreaWidget() != pProperties->dockAreaWidget() );
    CHECK( MasonWorkspace_ApplyPanelPolicy( session.pDocks, bCollapseConsole ) );
    CHECK( pConsole->isClosed() == bCollapseConsole );
    CHECK_FALSE( pConsole->isFloating() );
    CHECK( pConsole->dockAreaWidget() == pProperties->dockAreaWidget() );
    CheckPresentationProportions( MapViews_SavePresentation( session.pViews ), oldViews );
    REQUIRE( !oldCamera.isNull() );
    CHECK( MapViews_PaneView( session.pViews, 0 ) == oldCamera );
    CHECK( MasonWorkspace_ApplyPanelPolicy( session.pDocks, bCollapseConsole ) );
    CHECK( pConsole->isClosed() == bCollapseConsole );
}

TEST_CASE( "Mason restores legacy asset docks while preserving current panels and views", "[mason][workspace-state]" )
{
    workspace_session_t session;
    session.pWindow->setGeometry( session.pWindow->screen()->availableGeometry().adjusted( 24, 40, -24, -24 ) );
    QCoreApplication::processEvents();
    auto *pProperties = session.pDocks->findDockWidget( QStringLiteral( "map.properties" ) );
    auto *pProblems = session.pDocks->findDockWidget( QStringLiteral( "problems" ) );
    REQUIRE( pProperties != nullptr );
    REQUIRE( pProblems != nullptr );
    auto *pLegacyAssets = new ads::CDockWidget( session.pDocks, QStringLiteral( "Asset Browser" ) );
    pLegacyAssets->setObjectName( QStringLiteral( "assets" ) );
    pLegacyAssets->setWidget( new QWidget( pLegacyAssets ) );
    SECTION( "retired selected tab" )
    {
        REQUIRE( session.pDocks->addDockWidgetTabToArea( pLegacyAssets, pProperties->dockAreaWidget() ) != nullptr );
    }
    SECTION( "retired dock in its own splitter area" )
    {
        REQUIRE( session.pDocks->addDockWidget( ads::LeftDockWidgetArea, pLegacyAssets, pProperties->dockAreaWidget() ) != nullptr );
    }
    SECTION( "retired floating dock" )
    {
        REQUIRE( session.pDocks->addDockWidgetFloating( pLegacyAssets ) != nullptr );
    }
    pLegacyAssets->toggleView( true );
    pLegacyAssets->setAsCurrentTab();
    MapViews_SetArrangement( session.pViews, map_view_arrangement_t::TWO );
    QCoreApplication::processEvents();
    const QPointer<QWidget> oldCamera = MapViews_PaneView( session.pViews, 0 );
    const QJsonObject state = session.Save();
    const QByteArray oldViews = Field( state, "views" );
    REQUIRE( Field( state, "docks" ).contains( "Name=\"assets\"" ) );

    // Simulate the new binary, which no longer registers the old dock ID.
    session.pDocks->removeDockWidget( pLegacyAssets );
    delete pLegacyAssets;
    pProblems->toggleView( false );
    MapViews_SetArrangement( session.pViews, map_view_arrangement_t::FOUR );
    REQUIRE( session.Restore() == workspace_state_result_t::OK );
    CHECK( session.pDocks->findDockWidget( QStringLiteral( "assets" ) ) == nullptr );
    CHECK_FALSE( pProblems->isClosed() );
    CHECK_FALSE( pProperties->isClosed() );
    CheckPresentationProportions( MapViews_SavePresentation( session.pViews ), oldViews );
    REQUIRE( !oldCamera.isNull() );
    CHECK( MapViews_PaneView( session.pViews, 0 ) == oldCamera );
    const QJsonObject current = session.Save();
    CHECK_FALSE( Field( current, "docks" ).contains( "Name=\"assets\"" ) );
}

TEST_CASE( "Mason legacy asset migration removes its matching first or middle splitter size", "[mason][workspace-state]" )
{
    workspace_session_t session;
    // Keep the migration's size contract independent of Inspector/Outliner
    // minimum widths: those legitimately clamp the real Mason columns on a
    // small offscreen display. This is a real ADS layout with lightweight
    // live areas, while the existing Mason view supplies presentation state.
    QMainWindow window;
    window.setGeometry( session.pWindow->screen()->availableGeometry().adjusted( 24, 40, -24, -24 ) );
    auto *pDocks = new ads::CDockManager( &window );
    const auto makeDock = [pDocks]( const char *id, const char *title ) {
        auto *dock = new ads::CDockWidget( pDocks, QString::fromLatin1( title ) );
        dock->setObjectName( QString::fromLatin1( id ) );
        dock->setWidget( new QWidget( dock ) );
        return dock;
    };
    auto *pCentral = makeDock( "mason.views", "Views" );
    REQUIRE( pDocks->setCentralWidget( pCentral ) != nullptr );
    auto *pLeft = makeDock( "live.left", "L" );
    auto *pRight = makeDock( "live.right", "R" );
    auto *pLegacyAssets = makeDock( "assets", "Assets" );
    REQUIRE( pDocks->addDockWidget( ads::LeftDockWidgetArea, pLeft, pCentral->dockAreaWidget() ) != nullptr );
    REQUIRE( pDocks->addDockWidget( ads::RightDockWidgetArea, pRight, pCentral->dockAreaWidget() ) != nullptr );
    REQUIRE( pDocks->addDockWidget( ads::LeftDockWidgetArea, pLegacyAssets, pCentral->dockAreaWidget() ) != nullptr );
    pLeft->toggleView( true );
    pRight->toggleView( true );
    pLegacyAssets->toggleView( true );
    window.show();
    QCoreApplication::processEvents();
    auto *pSplitter = qobject_cast<QSplitter *>( pCentral->dockAreaWidget()->parentWidget() );
    REQUIRE( pSplitter != nullptr );
    REQUIRE( pLegacyAssets->dockAreaWidget()->parentWidget() == pSplitter );
    REQUIRE( pSplitter->count() == 4 );
    int retiredSlot = 0;
    SECTION( "first splitter slot" ) { retiredSlot = 0; }
    SECTION( "middle splitter slot" ) { retiredSlot = pSplitter->count() / 2; }
    pSplitter->insertWidget( retiredSlot, pLegacyAssets->dockAreaWidget() );
    QList<int> weights;
    for ( int i = 0; i < pSplitter->count(); ++i ) { weights.append( ( i + 1 ) * 200 ); }
    pSplitter->setSizes( weights );
    QCoreApplication::processEvents();
    REQUIRE( pSplitter->indexOf( pLegacyAssets->dockAreaWidget() ) == retiredSlot );
    QList<int> survivingSizes = pSplitter->sizes();
    REQUIRE( survivingSizes[retiredSlot] > 0 );
    const int retiredWidth = survivingSizes[retiredSlot];
    const int handleWidth = pSplitter->handleWidth();
    const int originalCentralSlot = pSplitter->indexOf( pCentral->dockAreaWidget() );
    REQUIRE( originalCentralSlot >= 0 );
    REQUIRE( originalCentralSlot != retiredSlot );
    const int centralSlot = originalCentralSlot - ( retiredSlot < originalCentralSlot ? 1 : 0 );
    const QRect oldGeometry = window.geometry();
    survivingSizes.removeAt( retiredSlot );
    const QPointer<QWidget> oldCamera = MapViews_PaneView( session.pViews, 0 );
    const QByteArray oldViews = MapViews_SavePresentation( session.pViews );
    REQUIRE( MasonWorkspace_Save( &window, pDocks, session.pViews, session.path ) == workspace_state_result_t::OK );
    pDocks->removeDockWidget( pLegacyAssets );
    delete pLegacyAssets;
    REQUIRE( MasonWorkspace_Restore( &window, pDocks, session.pViews, session.path ) == workspace_state_result_t::OK );
    QCoreApplication::processEvents();
    auto *pRestoredSplitter = qobject_cast<QSplitter *>( pCentral->dockAreaWidget()->parentWidget() );
    REQUIRE( pRestoredSplitter != nullptr );
    const QList<int> restoredSizes = pRestoredSplitter->sizes();
    CAPTURE( retiredSlot, centralSlot, retiredWidth, handleWidth, survivingSizes, restoredSizes );
    REQUIRE( restoredSizes.size() == survivingSizes.size() );
    CHECK( window.geometry() == oldGeometry );
    // ADS makes only the central dock stretch. Peripheral widths must retain
    // their saved pixels; the central dock absorbs the retired area and the
    // splitter handle that no longer separates it from a live sibling.
    for ( int i = 0; i < restoredSizes.size(); ++i ) {
        const int expected = survivingSizes[i] + ( i == centralSlot ? retiredWidth + handleWidth : 0 );
        CHECK( std::abs( restoredSizes[i] - expected ) <= 1 );
    }
    CHECK( pDocks->findDockWidget( QStringLiteral( "assets" ) ) == nullptr );
    REQUIRE( !oldCamera.isNull() );
    CHECK( MapViews_PaneView( session.pViews, 0 ) == oldCamera );
    CHECK( MapViews_SavePresentation( session.pViews ) == oldViews );
    REQUIRE( MasonWorkspace_Save( &window, pDocks, session.pViews, session.path ) == workspace_state_result_t::OK );
    QFile currentFile( session.path );
    REQUIRE( currentFile.open( QIODevice::ReadOnly ) );
    CHECK_FALSE( Field( QJsonDocument::fromJson( currentFile.readAll() ).object(), "docks" ).contains( "Name=\"assets\"" ) );
}

TEST_CASE( "Mason restores valid panel layouts and preserves views when later state fails", "[mason][workspace-state]" )
{
    workspace_session_t session;
    // The offscreen display can be smaller than the editor's usual 1400px
    // window. Stay inside it here: QWidget::restoreGeometry correctly clamps
    // offscreen windows, which is separate from restoring splitter proportions.
    session.pWindow->setGeometry( session.pWindow->screen()->availableGeometry().adjusted( 24, 40, -24, -24 ) );
    QCoreApplication::processEvents();
    QJsonObject state = session.Save();
    const QByteArray oldViews = MapViews_SavePresentation( session.pViews );
    const QSize oldWindowSize = session.pWindow->size();
    const QSize oldViewsSize = session.pViews->size();
    const QPointer<QWidget> oldCamera = MapViews_PaneView( session.pViews, 0 );
    // Any docked panel will do; Problems shares the bottom tab group.
    ads::CDockWidget *pAssets = session.pDocks->findDockWidget( QStringLiteral( "problems" ) );
    REQUIRE( pAssets != nullptr );

    SECTION( "valid state reopens a panel" )
    {
        pAssets->toggleView( false );
        REQUIRE( pAssets->isClosed() );
        CHECK( session.Restore() == workspace_state_result_t::OK );
        CHECK_FALSE( pAssets->isClosed() );
        REQUIRE( !oldCamera.isNull() );
        CHECK( MapViews_PaneView( session.pViews, 0 ) == oldCamera );
        CHECK( MapViews_SavePresentation( session.pViews ) == oldViews );
        CHECK( session.pWindow->size() == oldWindowSize );
        CHECK( session.pViews->size() == oldViewsSize );
    }
    SECTION( "floating panel geometry survives a real ADS round trip" )
    {
        REQUIRE( session.pDocks->addDockWidgetFloating( pAssets ) != nullptr );
        QCoreApplication::processEvents();
        state = session.Save();
        CHECK( Field( state, "docks" ).contains( "Floating=\"1\"" ) );
        pAssets->toggleView( false );
        CHECK( session.Restore() == workspace_state_result_t::OK );
        CHECK_FALSE( pAssets->isClosed() );
        CHECK( pAssets->isFloating() );
        REQUIRE( !oldCamera.isNull() );
        CHECK( MapViews_PaneView( session.pViews, 0 ) == oldCamera );
    }
    SECTION( "invalid Qt toolbar state must not replace the camera" )
    {
        QJsonObject views = QJsonDocument::fromJson( Field( state, "views" ) ).object();
        QJsonArray panes = views.value( QStringLiteral( "panes" ) ).toArray();
        QJsonObject pane = panes[0].toObject();
        pane.insert( QStringLiteral( "type" ), static_cast<int>( map_view_type_t::TOP ) );
        panes[0] = pane;
        views.insert( QStringLiteral( "panes" ), panes );
        PutField( state, "views", QJsonDocument( views ).toJson( QJsonDocument::Compact ) );
        PutField( state, "toolbars", QByteArray( "invalid Qt window state" ) );
        session.Write( state );
        CHECK( session.Restore() == workspace_state_result_t::INVALID );
        REQUIRE( !oldCamera.isNull() );
        CHECK( MapViews_PaneView( session.pViews, 0 ) == oldCamera );
        CHECK( MapViews_SavePresentation( session.pViews ) == oldViews );
    }
    SECTION( "invalid view state rolls panel visibility back" )
    {
        pAssets->toggleView( false );
        PutField( state, "views", QByteArray( "{}" ) );
        session.Write( state );
        CHECK( session.Restore() == workspace_state_result_t::INVALID );
        CHECK( pAssets->isClosed() );
        REQUIRE( !oldCamera.isNull() );
        CHECK( MapViews_PaneView( session.pViews, 0 ) == oldCamera );
        CHECK( MapViews_SavePresentation( session.pViews ) == oldViews );
    }
}
