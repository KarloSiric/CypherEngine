//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Docking_Tests.cpp
//  Purpose: Contract tests for the dock manager: layouts shown through the
//           Qt Advanced Docking System and captured back.
//  Details: Runs on the offscreen platform with a shown window, so layout
//           and splitter sizing run for real. Placement is checked by where
//           panels end up relative to each other and by tab grouping; sizes
//           are checked with a tolerance, since splitter handles take
//           pixels.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Rewritten for the Qt Advanced Docking System on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Application.h"
#include "CypherEditorGui_Docking.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include "DockAreaWidget.h"
#include "DockManager.h"
#include "DockWidget.h"
#include "FloatingDockContainer.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QLabel>
#include <QMainWindow>

#include <cmath>
#include <string>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

QWidget *CreateLabel( void *pContext, QWidget *pParent )
{
    return new QLabel( QString::fromUtf8( static_cast<const char *>( pContext ) ), pParent );
}

const editor_panel_desc_t kPanels[]{
    { "mason.views", "Views", CreateLabel, const_cast<char *>( "views" ) },
    { "console", "Console", CreateLabel, const_cast<char *>( "console" ) },
    { "output", "Output", CreateLabel, const_cast<char *>( "output" ) },
    { "problems", "Problems", CreateLabel, const_cast<char *>( "problems" ) },
    { "assets", "Asset Browser", CreateLabel, const_cast<char *>( "assets" ) },
    { "map.outliner", "Outliner", CreateLabel, const_cast<char *>( "outliner" ) },
    { "map.properties", "Properties", CreateLabel, const_cast<char *>( "properties" ) },
    { "map.history", "History", CreateLabel, const_cast<char *>( "history" ) },
    { "map.tool_properties", "Tool Properties", CreateLabel, const_cast<char *>( "tool properties" ) },
    { "map.active_material", "Active Material", CreateLabel, const_cast<char *>( "active material" ) },
    { "map.visgroups", "Auto Vis Groups", CreateLabel, const_cast<char *>( "visgroups" ) },
    { "map.selection_sets", "Selection Sets", CreateLabel, const_cast<char *>( "selection sets" ) },
};

struct window_t {
    QMainWindow window{};
    editor_docking_t docking{};
    window_t()
    {
        window.resize( 1600, 1000 );
        EditorDocking_Init( &docking, &window, "mason.views" );
        REQUIRE( EditorDocking_RegisterPanels( &docking, kPanels, std::size( kPanels ) ) );
        window.show();
    }
    ads::CDockWidget *Dock( const char *pId ) const { return docking.docks.value( QString::fromUtf8( pId ) ); }
    // A panel's rectangle in window coordinates: its tab group's, since ADS
    // unparents the widgets of tabs that are not current.
    QRect Rect( const char *pId ) const
    {
        const QWidget *pWidget = QString::fromUtf8( pId ) == docking.centralId ? static_cast<const QWidget *>( docking.pCentral )
                                                                               : Dock( pId )->dockAreaWidget();
        REQUIRE( pWidget != nullptr );
        return QRect( pWidget->mapTo( &window, QPoint( 0, 0 ) ), pWidget->size() );
    }
    bool Open( const char *pId ) const { return EditorDocking_IsPanelVisible( &docking, QString::fromUtf8( pId ) ); }
    bool SameGroup( const char *pA, const char *pB ) const { return Dock( pA )->dockAreaWidget() == Dock( pB )->dockAreaWidget(); }
};

struct layout_owner_t {
    layout_t layout{};
    layout_owner_t() { REQUIRE( EditorLayout_Init( &layout, Allocator_GetSystem() ) == layout_status_t::OK ); }
    ~layout_owner_t() { EditorLayout_Shutdown( &layout ); }
};

u32 Tabs( layout_t *pLayout, std::initializer_list<const char *> ids, u32 iCurrent = 0u )
{
    string_view_t views[8]{};
    usize n = 0u;
    for ( const char *pId : ids ) { views[n++] = StringView_FromCString( pId ); }
    u32 iNode = 0u;
    REQUIRE( EditorLayout_AddTabs( pLayout, views, n, iCurrent, &iNode ) == layout_status_t::OK );
    return iNode;
}

u32 Split( layout_t *pLayout, bool bVertical, std::initializer_list<u32> children, const f32 *pSizes = nullptr )
{
    u32 nodes[8]{};
    usize n = 0u;
    for ( u32 iChild : children ) { nodes[n++] = iChild; }
    u32 iNode = 0u;
    REQUIRE( EditorLayout_AddSplit( pLayout, bVertical, nodes, pSizes, n, &iNode ) == layout_status_t::OK );
    return iNode;
}

// Hammer's arrangement with a history column on the left: the bottom
// console spans everything above it.
void BuildWideLayout( layout_t *pLayout )
{
    const u32 iLeft = Tabs( pLayout, { "map.history" } );
    const u32 iCenter = Tabs( pLayout, { "mason.views" } );
    const u32 iRight = Tabs( pLayout, { "map.outliner", "map.properties" }, 1u );
    const f32 across[]{ 0.2f, 0.6f, 0.2f };
    const u32 iMiddle = Split( pLayout, false, { iLeft, iCenter, iRight }, across );
    const u32 iBottom = Tabs( pLayout, { "console" } );
    const f32 down[]{ 0.75f, 0.25f };
    pLayout->iRoot = Split( pLayout, true, { iMiddle, iBottom }, down );
    pLayout->bHasRoot = CY_TRUE;
}

void CheckWideLayout( const window_t &w )
{
    CHECK( w.Rect( "map.history" ).right() < w.Rect( "mason.views" ).left() );
    CHECK( w.Rect( "map.outliner" ).left() > w.Rect( "mason.views" ).right() );
    CHECK( w.Rect( "console" ).top() > w.Rect( "mason.views" ).bottom() );
    // The vertical root is outermost: the console runs under the side columns too.
    CHECK( w.Rect( "console" ).left() <= w.Rect( "map.history" ).left() );
    CHECK( w.Rect( "console" ).right() >= w.Rect( "map.outliner" ).right() );
    CHECK( w.SameGroup( "map.outliner", "map.properties" ) );
    CHECK( w.Dock( "map.properties" )->dockAreaWidget()->currentDockWidget() == w.Dock( "map.properties" ) );
}

} // namespace

TEST_CASE( "The Mason default layout prioritizes views with tabbed support panels", "[editor][gui][docking]" )
{
    layout_owner_t owner;
    REQUIRE( EditorGui_LoadLayoutResource( ":/cypher/editor/layouts/mason_default.cylayout", &owner.layout, Allocator_GetSystem() ) ==
             editor_gui_status_t::OK );
    window_t w;
    CHECK( EditorDocking_ApplyLayout( &w.docking, owner.layout ) == 0u );
    QCoreApplication::processEvents();
    REQUIRE( w.docking.pCentral != nullptr );
    // Left: Tool Properties above Active Material.
    CHECK( w.Rect( "map.tool_properties" ).right() < w.Rect( "mason.views" ).left() );
    CHECK( w.Rect( "map.tool_properties" ).bottom() < w.Rect( "map.active_material" ).top() );
    // Diagnostics share Object Properties' lower-right tabs; the views retain
    // the full center height. Mason applies Console's startup closed state.
    CHECK( w.SameGroup( "console", "map.properties" ) );
    CHECK( w.SameGroup( "output", "map.properties" ) );
    CHECK( w.SameGroup( "problems", "map.properties" ) );
    CHECK( w.Rect( "console" ).left() > w.Rect( "mason.views" ).right() );
    CHECK( w.Dock( "map.properties" )->dockAreaWidget()->currentDockWidget() == w.Dock( "map.properties" ) );
    CHECK( ( w.Dock( "assets" ) == nullptr || w.Dock( "assets" )->isClosed() ) );
    // Right, as in Hammer 5: Auto Vis Groups over Selection Sets beside the
    // Outliner, which shares its tab group with Undo History; Object
    // Properties and diagnostics span the column underneath.
    CHECK( w.Open( "map.visgroups" ) );
    CHECK( w.Rect( "map.visgroups" ).left() > w.Rect( "mason.views" ).right() );
    CHECK( w.Rect( "map.visgroups" ).bottom() < w.Rect( "map.selection_sets" ).top() );
    CHECK( w.Rect( "map.visgroups" ).right() < w.Rect( "map.outliner" ).left() );
    CHECK( w.Dock( "map.history" )->dockAreaWidget() == w.Dock( "map.outliner" )->dockAreaWidget() );
    CHECK( w.Rect( "map.outliner" ).bottom() < w.Rect( "map.properties" ).top() );
    CHECK( w.Rect( "map.properties" ).left() <= w.Rect( "map.visgroups" ).left() );
    CHECK( w.Open( "console" ) );
    CHECK( w.Open( "map.history" ) );
    const double rightShare = static_cast<double>( w.Rect( "map.properties" ).width() ) / w.window.width();
    CHECK( std::fabs( rightShare - 0.26 ) < 0.03 );
    const int rightHeight = w.Rect( "map.properties" ).bottom() - w.Rect( "map.visgroups" ).top() + 1;
    const double propertiesShare = static_cast<double>( w.Rect( "map.properties" ).height() ) / rightHeight;
    CHECK( std::fabs( propertiesShare - 0.5 ) < 0.05 );
    CHECK( std::abs( w.Rect( "mason.views" ).height() - rightHeight ) <= 4 );
}

TEST_CASE( "A captured layout shows the same arrangement again", "[editor][gui][docking]" )
{
    layout_owner_t original;
    BuildWideLayout( &original.layout );

    window_t first;
    CHECK( EditorDocking_ApplyLayout( &first.docking, original.layout ) == 0u );
    QCoreApplication::processEvents();
    CheckWideLayout( first );

    layout_owner_t captured;
    REQUIRE( EditorDocking_CaptureLayout( &first.docking, &captured.layout ) );
    CHECK( captured.layout.bHasRoot );
    CHECK( Vector_Count( &captured.layout.panels ) == 5u );

    window_t second;
    CHECK( EditorDocking_ApplyLayout( &second.docking, captured.layout ) == 0u );
    QCoreApplication::processEvents();
    CheckWideLayout( second );
    // Proportions survive the round trip.
    const double firstShare = static_cast<double>( first.Rect( "map.history" ).width() ) / first.window.width();
    const double secondShare = static_cast<double>( second.Rect( "map.history" ).width() ) / second.window.width();
    CHECK( std::fabs( firstShare - secondShare ) < 0.02 );
}

TEST_CASE( "Floating windows are shown and captured with their rectangle", "[editor][gui][docking]" )
{
    layout_owner_t owner;
    const u32 iCenter = Tabs( &owner.layout, { "mason.views" } );
    const u32 iBottom = Tabs( &owner.layout, { "console" } );
    owner.layout.iRoot = Split( &owner.layout, true, { iCenter, iBottom } );
    owner.layout.bHasRoot = CY_TRUE;
    layout_window_t floating{};
    floating.x = 200;
    floating.y = 150;
    floating.width = 420;
    floating.height = 360;
    const u32 iTop = Tabs( &owner.layout, { "map.outliner" } );
    const u32 iLow = Tabs( &owner.layout, { "map.properties" } );
    floating.iRoot = Split( &owner.layout, true, { iTop, iLow } );
    REQUIRE( EditorLayout_AddWindow( &owner.layout, floating ) == layout_status_t::OK );

    window_t w;
    CHECK( EditorDocking_ApplyLayout( &w.docking, owner.layout ) == 0u );
    QCoreApplication::processEvents();
    REQUIRE( w.docking.pManager->floatingWidgets().size() == 1 );
    const ads::CFloatingDockContainer *pFloating = w.docking.pManager->floatingWidgets().front();
    CHECK( w.Dock( "map.outliner" )->window() == pFloating );
    CHECK( w.Dock( "map.properties" )->window() == pFloating );
    CHECK_FALSE( w.SameGroup( "map.outliner", "map.properties" ) ); // Split, not tabbed.
    CHECK( pFloating->geometry().width() == 420 );

    layout_owner_t captured;
    REQUIRE( EditorDocking_CaptureLayout( &w.docking, &captured.layout ) );
    REQUIRE( Vector_Count( &captured.layout.windows ) == 1u );
    CHECK( captured.layout.windows.pData[0].width == 420u );
    CHECK( Vector_Count( &captured.layout.panels ) == 4u );
}

TEST_CASE( "Unregistered panels are skipped and panels toggle", "[editor][gui][docking]" )
{
    layout_owner_t owner;
    const u32 iCenter = Tabs( &owner.layout, { "mason.views" } );
    const u32 iRight = Tabs( &owner.layout, { "plugin.not_loaded", "console" } );
    owner.layout.iRoot = Split( &owner.layout, false, { iCenter, iRight } );
    owner.layout.bHasRoot = CY_TRUE;
    window_t w;
    CHECK( EditorDocking_ApplyLayout( &w.docking, owner.layout ) == 1u );
    QCoreApplication::processEvents();
    CHECK( w.Rect( "console" ).left() > w.Rect( "mason.views" ).right() );

    EditorDocking_SetPanelVisible( &w.docking, QStringLiteral( "console" ), false );
    CHECK_FALSE( w.Open( "console" ) );
    EditorDocking_SetPanelVisible( &w.docking, QStringLiteral( "console" ), true );
    CHECK( w.Open( "console" ) );
    EditorDocking_SetPanelVisible( &w.docking, QStringLiteral( "map.history" ), true );
    QCoreApplication::processEvents();
    CHECK( w.Open( "map.history" ) );
    CHECK( w.Rect( "map.history" ).left() > w.Rect( "mason.views" ).right() );
    // Re-applying closes panels the layout does not name.
    CHECK( EditorDocking_ApplyLayout( &w.docking, owner.layout ) == 1u );
    CHECK_FALSE( w.Open( "map.history" ) );
    CHECK( w.Open( "console" ) );
    CHECK( EditorDocking_Dock( &w.docking, QStringLiteral( "mason.views" ) ) == nullptr ); // Central, never a dock.
    CHECK_FALSE( EditorDocking_RegisterPanels( &w.docking, kPanels, 1u ) );                // Duplicate.
}
