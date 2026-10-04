//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Docking.cpp
//  Purpose: Implements the dock manager over the Qt Advanced Docking System.
//  Details: ADS offers two ways to insert: relative to an existing dock area
//           (the area's own splitter is split) and relative to a whole
//           container (the new area goes at the outermost edge). A layout
//           tree is rebuilt with both:
//
//           - Along the path from the root to the central panel, levels are
//             added innermost first, each sibling at the container's edge,
//             so every level wraps everything built before it.
//           - Any other subtree is built from its first tab group outwards:
//             the first tab group of each child is placed next to the
//             previous child's, then each child's own subtree is expanded
//             in place.
//
//           Splitter sizes are applied last, once the whole tree exists: the
//           splitter holding a split's children is the lowest splitter that
//           contains a representative area of every child.
//
//           Capturing walks the ADS splitter tree of the main container and
//           of each floating container.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Moved from QDockWidget onto the Qt Advanced Docking System on
//    2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Docking.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include "DockAreaWidget.h"
#include "DockManager.h"
#include "DockSplitter.h"
#include "DockWidget.h"
#include "FloatingDockContainer.h"

#include <QLabel>
#include <QMainWindow>

#include <algorithm>
#include <numeric>
#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

QString LayoutString( const layout_t &layout, layout_text_t text )
{
    const string_view_t view = EditorLayout_Text( &layout, text );
    return QString::fromUtf8( view.pData, static_cast<qsizetype>( view.cchLength ) );
}

bool NodeHoldsPanel( const layout_t &layout, u32 iNode, const QString &id )
{
    const layout_node_t &node = layout.nodes.pData[iNode];
    if ( node.kind != layout_node_kind_t::TABS ) { return false; }
    for ( u32 i = 0u; i < node.nPanels; ++i ) {
        if ( LayoutString( layout, layout.panels.pData[node.iFirstPanel + i] ) == id ) { return true; }
    }
    return false;
}

struct path_step_t {
    u32 iNode;  // A split on the way to the central tab group.
    u32 iChild; // Which of its children leads there.
};

bool FindCentralPath( const layout_t &layout, u32 iNode, const QString &centralId, std::vector<path_step_t> &path, u32 &iCentralOut )
{
    const layout_node_t &node = layout.nodes.pData[iNode];
    if ( node.kind == layout_node_kind_t::TABS ) {
        if ( !NodeHoldsPanel( layout, iNode, centralId ) ) { return false; }
        iCentralOut = iNode;
        return true;
    }
    for ( u32 i = 0u; i < node.nChildren; ++i ) {
        path.push_back( { iNode, i } );
        if ( FindCentralPath( layout, layout.children.pData[node.iFirstChild + i], centralId, path, iCentralOut ) ) { return true; }
        path.pop_back();
    }
    return false;
}

// Sizes to apply to one layout split once the tree exists.
struct size_request_t {
    std::vector<QWidget *> representatives; // One area per placed child.
    std::vector<f32> weights;
};

struct builder_t {
    editor_docking_t *pDocking{ nullptr };
    const layout_t *pLayout{ nullptr };
    std::vector<size_request_t> sizes;
    usize nSkipped{ 0u };
};

// Where a tab group lands: at a container's edge, beside an area, or in a
// new floating window.
struct anchor_t {
    ads::DockWidgetArea side{ ads::RightDockWidgetArea };
    ads::CDockAreaWidget *pBeside{ nullptr };           // Null: the container's edge.
    ads::CDockContainerWidget *pContainer{ nullptr };   // For edge placement; null means the main container.
    bool bFloat{ false };                               // Start a floating window instead.
};

ads::CDockAreaWidget *Insert( builder_t &builder, ads::CDockWidget *pDock, const anchor_t &anchor )
{
    ads::CDockManager *pManager = builder.pDocking->pManager;
    if ( anchor.bFloat ) {
        ( void )pManager->addDockWidgetFloating( pDock );
        return pDock->dockAreaWidget();
    }
    if ( anchor.pBeside != nullptr ) { return pManager->addDockWidget( anchor.side, pDock, anchor.pBeside ); }
    if ( anchor.pContainer != nullptr && anchor.pContainer != pManager ) {
        return pManager->addDockWidgetToContainer( anchor.side, pDock, anchor.pContainer );
    }
    return pManager->addDockWidget( anchor.side, pDock );
}

// Places one tab group; null when none of its panels is registered.
ads::CDockAreaWidget *PlaceTabs( builder_t &builder, u32 iNode, const anchor_t &anchor )
{
    const layout_t &layout = *builder.pLayout;
    const layout_node_t &node = layout.nodes.pData[iNode];
    CY_ASSERT( node.kind == layout_node_kind_t::TABS );
    ads::CDockAreaWidget *pArea = nullptr;
    ads::CDockWidget *pCurrent = nullptr;
    for ( u32 i = 0u; i < node.nPanels; ++i ) {
        const QString id = LayoutString( layout, layout.panels.pData[node.iFirstPanel + i] );
        ads::CDockWidget *pDock = EditorDocking_Dock( builder.pDocking, id );
        if ( pDock == nullptr ) {
            // The central panel inside a side group is already shown; any
            // other miss is a panel nobody registered.
            if ( id != builder.pDocking->centralId ) { ++builder.nSkipped; }
            continue;
        }
        pArea = pArea == nullptr ? Insert( builder, pDock, anchor ) : builder.pDocking->pManager->addDockWidgetTabToArea( pDock, pArea );
        pDock->toggleView( true );
        if ( i == node.iCurrent || pCurrent == nullptr ) { pCurrent = pDock; }
    }
    if ( pArea != nullptr && pCurrent != nullptr ) { pArea->setCurrentDockWidget( pCurrent ); }
    return pArea;
}

ads::CDockAreaWidget *PlaceFirstGroup( builder_t &builder, u32 iNode, const anchor_t &anchor, u32 &iPlacedGroup );
void Expand( builder_t &builder, u32 iNode, u32 iFirstGroup, ads::CDockAreaWidget *pFirstArea );

// Places the first tab group of a subtree that yields anything. iPlacedGroup
// receives the tab group's node index.
ads::CDockAreaWidget *PlaceFirstGroup( builder_t &builder, u32 iNode, const anchor_t &anchor, u32 &iPlacedGroup )
{
    const layout_t &layout = *builder.pLayout;
    const layout_node_t &node = layout.nodes.pData[iNode];
    if ( node.kind == layout_node_kind_t::TABS ) {
        iPlacedGroup = iNode;
        return PlaceTabs( builder, iNode, anchor );
    }
    for ( u32 i = 0u; i < node.nChildren; ++i ) {
        ads::CDockAreaWidget *pArea = PlaceFirstGroup( builder, layout.children.pData[node.iFirstChild + i], anchor, iPlacedGroup );
        if ( pArea != nullptr ) { return pArea; }
    }
    return nullptr;
}

bool SubtreeContains( const layout_t &layout, u32 iNode, u32 iTarget )
{
    if ( iNode == iTarget ) { return true; }
    const layout_node_t &node = layout.nodes.pData[iNode];
    if ( node.kind == layout_node_kind_t::TABS ) { return false; }
    for ( u32 i = 0u; i < node.nChildren; ++i ) {
        if ( SubtreeContains( layout, layout.children.pData[node.iFirstChild + i], iTarget ) ) { return true; }
    }
    return false;
}

// Builds the rest of a subtree whose first tab group (iFirstGroup) is
// already placed as pFirstArea.
void Expand( builder_t &builder, u32 iNode, u32 iFirstGroup, ads::CDockAreaWidget *pFirstArea )
{
    const layout_t &layout = *builder.pLayout;
    const layout_node_t &node = layout.nodes.pData[iNode];
    if ( node.kind == layout_node_kind_t::TABS ) { return; }
    const ads::DockWidgetArea after = node.bVertical ? ads::BottomDockWidgetArea : ads::RightDockWidgetArea;
    struct child_t {
        u32 iChild;
        u32 iGroup;
        ads::CDockAreaWidget *pArea;
        f32 weight;
    };
    std::vector<child_t> placed;
    // The child holding the already placed first group keeps its place; the
    // first groups of later children line up after it along this split.
    ads::CDockAreaWidget *pPrevious = nullptr;
    for ( u32 i = 0u; i < node.nChildren; ++i ) {
        const u32 iChild = layout.children.pData[node.iFirstChild + i];
        const f32 weight = layout.sizes.pData[node.iFirstChild + i];
        if ( pPrevious == nullptr ) {
            if ( SubtreeContains( layout, iChild, iFirstGroup ) ) {
                placed.push_back( { iChild, iFirstGroup, pFirstArea, weight } );
                pPrevious = pFirstArea;
            }
            continue; // Children before it produced nothing, or it comes later.
        }
        u32 iGroup = 0u;
        ads::CDockAreaWidget *pArea = PlaceFirstGroup( builder, iChild, anchor_t{ after, pPrevious, nullptr }, iGroup );
        if ( pArea == nullptr ) { continue; }
        placed.push_back( { iChild, iGroup, pArea, weight } );
        pPrevious = pArea;
    }
    for ( const child_t &child : placed ) { Expand( builder, child.iChild, child.iGroup, child.pArea ); }
    if ( placed.size() > 1u ) {
        size_request_t request;
        for ( const child_t &child : placed ) {
            request.representatives.push_back( child.pArea );
            request.weights.push_back( child.weight );
        }
        builder.sizes.push_back( std::move( request ) );
    }
}

// Places a whole subtree at a container edge; returns its first area.
ads::CDockAreaWidget *PlaceAtEdge( builder_t &builder, u32 iNode, ads::DockWidgetArea side, ads::CDockContainerWidget *pContainer )
{
    u32 iGroup = 0u;
    ads::CDockAreaWidget *pArea = PlaceFirstGroup( builder, iNode, anchor_t{ side, nullptr, pContainer }, iGroup );
    if ( pArea != nullptr ) { Expand( builder, iNode, iGroup, pArea ); }
    return pArea;
}

QWidget *CreateCentralWidget( editor_docking_t *pDocking )
{
    auto it = pDocking->panels.constFind( pDocking->centralId );
    if ( it != pDocking->panels.constEnd() ) { return it->pfnCreate( it->pContext, nullptr ); }
    QLabel *pPlaceholder = new QLabel( QStringLiteral( "No central panel registered" ) );
    pPlaceholder->setObjectName( QStringLiteral( "EditorPanelPlaceholder" ) );
    pPlaceholder->setAlignment( Qt::AlignCenter );
    return pPlaceholder;
}

// ADS requires the central widget before any other dock widget is added.
void EnsureCentral( editor_docking_t *pDocking )
{
    if ( pDocking->pCentralDock != nullptr ) { return; }
    pDocking->pCentral = CreateCentralWidget( pDocking );
    auto *pDock = new ads::CDockWidget( pDocking->pManager, QString::fromUtf8( "Central" ) );
    pDock->setObjectName( pDocking->centralId );
    pDock->setWidget( pDocking->pCentral, ads::CDockWidget::ForceNoScrollArea );
    pDock->setFeatures( ads::CDockWidget::NoDockWidgetFeatures );
    ads::CDockAreaWidget *pArea = pDocking->pManager->setCentralWidget( pDock );
    CY_ASSERT( pArea != nullptr ); // Only fails when docks were added first.
    ( void )pArea;
    pDocking->pCentralDock = pDock;
}

// The widget's ancestor chain up to (not including) stop.
std::vector<QWidget *> Ancestors( QWidget *pWidget, const QWidget *pStop )
{
    std::vector<QWidget *> chain;
    for ( QWidget *p = pWidget; p != nullptr && p != pStop; p = p->parentWidget() ) { chain.push_back( p ); }
    return chain;
}

void ApplySizes( editor_docking_t *pDocking, const size_request_t &request )
{
    // The lowest splitter above every representative.
    const std::vector<QWidget *> first = Ancestors( request.representatives[0], pDocking->pWindow );
    QSplitter *pSplitter = nullptr;
    for ( QWidget *pCandidate : first ) {
        auto *pAsSplitter = qobject_cast<QSplitter *>( pCandidate );
        if ( pAsSplitter == nullptr || pCandidate == request.representatives[0] ) { continue; }
        bool bAll = true;
        for ( QWidget *pRep : request.representatives ) { bAll = bAll && pAsSplitter->isAncestorOf( pRep ); }
        if ( bAll ) {
            pSplitter = pAsSplitter;
            break;
        }
    }
    if ( pSplitter == nullptr ) { return; }
    // Map each child to the splitter slot containing it. Slots that no child
    // claims (same-direction splits ADS merged into one splitter) keep their
    // current share.
    QList<int> current = pSplitter->sizes();
    const int total = std::max( 1, std::accumulate( current.begin(), current.end(), 0 ) );
    f32 weightSum = 0.0f;
    for ( f32 weight : request.weights ) { weightSum += weight; }
    int claimedPixels = 0;
    std::vector<int> slotIndices;
    for ( QWidget *pRep : request.representatives ) {
        int iSlot = -1;
        for ( int i = 0; i < pSplitter->count(); ++i ) {
            QWidget *pSlot = pSplitter->widget( i );
            if ( pSlot == pRep || pSlot->isAncestorOf( pRep ) ) { iSlot = i; break; }
        }
        if ( iSlot < 0 || std::find( slotIndices.begin(), slotIndices.end(), iSlot ) != slotIndices.end() ) { return; }
        slotIndices.push_back( iSlot );
        claimedPixels += current[iSlot];
    }
    if ( weightSum <= 0.0f ) { return; }
    // Unclaimed slots exist only when ADS merged levels; the claimed slots
    // share what they had between them.
    int budget = pSplitter->count() == static_cast<int>( slotIndices.size() ) ? total : std::max( 1, claimedPixels );
    // Before the first layout pass every size is zero; QSplitter distributes
    // any total proportionally, so weights on a fixed scale work then too.
    constexpr int kUnsizedBudget = 10000;
    if ( budget < static_cast<int>( slotIndices.size() ) * 16 ) { budget = kUnsizedBudget; }
    for ( usize i = 0u; i < slotIndices.size(); ++i ) {
        current[slotIndices[i]] = std::max( 1, static_cast<int>( budget * ( request.weights[i] / weightSum ) ) );
    }
    // ADS gives the central area a stretch factor, and with any stretch
    // factor set QSplitter ignores setSizes in favour of stretching. Plain
    // proportional sizing keeps the layout's shares, also on resize.
    for ( int i = 0; i < pSplitter->count(); ++i ) { pSplitter->setStretchFactor( i, 0 ); }
    pSplitter->setSizes( current );
}

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

struct capture_t {
    editor_docking_t *pDocking{ nullptr };
    layout_t *pLayout{ nullptr };
    bool bFailed{ false };
};

// A container's root splitter. ADS keeps the accessor protected, but the
// splitter is always the container's direct child.
QSplitter *RootSplitter( const QWidget *pContainer )
{
    return pContainer != nullptr ? pContainer->findChild<ads::CDockSplitter *>( QString(), Qt::FindDirectChildrenOnly ) : nullptr;
}

// Returns false when the widget shows nothing (hidden or all panels closed).
bool CaptureWidget( capture_t &capture, QWidget *pWidget, u32 &iNodeOut )
{
    if ( pWidget == nullptr || pWidget->isHidden() ) { return false; }
    if ( auto *pArea = qobject_cast<ads::CDockAreaWidget *>( pWidget ) ) {
        const QList<ads::CDockWidget *> open = pArea->openedDockWidgets();
        if ( open.isEmpty() ) { return false; }
        std::vector<QByteArray> names;
        std::vector<string_view_t> views;
        u32 iCurrent = 0u;
        for ( ads::CDockWidget *pDock : open ) {
            if ( pDock == pArea->currentDockWidget() ) { iCurrent = static_cast<u32>( names.size() ); }
            names.push_back( pDock->objectName().toUtf8() );
        }
        for ( const QByteArray &name : names ) { views.push_back( { name.constData(), static_cast<usize>( name.size() ) } ); }
        if ( EditorLayout_AddTabs( capture.pLayout, views.data(), views.size(), iCurrent, &iNodeOut ) != layout_status_t::OK ) {
            capture.bFailed = true;
            return false;
        }
        return true;
    }
    auto *pSplitter = qobject_cast<QSplitter *>( pWidget );
    if ( pSplitter == nullptr ) { return false; }
    std::vector<u32> nodes;
    std::vector<f32> weights;
    const QList<int> sizes = pSplitter->sizes();
    for ( int i = 0; i < pSplitter->count(); ++i ) {
        u32 iChild = 0u;
        if ( !CaptureWidget( capture, pSplitter->widget( i ), iChild ) ) { continue; }
        nodes.push_back( iChild );
        weights.push_back( static_cast<f32>( std::max( 1, sizes.value( i ) ) ) );
    }
    if ( nodes.empty() ) { return false; }
    if ( nodes.size() == 1u ) {
        iNodeOut = nodes[0];
        return true;
    }
    if ( EditorLayout_AddSplit( capture.pLayout, pSplitter->orientation() == Qt::Vertical, nodes.data(), weights.data(), nodes.size(),
                                &iNodeOut ) != layout_status_t::OK ) {
        capture.bFailed = true;
        return false;
    }
    return true;
}

} // namespace

void EditorDocking_Init( editor_docking_t *pDocking, QMainWindow *pWindow, const char *pCentralPanelId )
{
    CY_ASSERT( pDocking != nullptr && pWindow != nullptr && pCentralPanelId != nullptr && pDocking->pManager == nullptr );
    // Configuration is global to ADS and read when a manager is created.
    // The editor stylesheet styles the dock chrome from theme tokens, so
    // ADS's own stylesheet stays off.
    // Dock areas read as TileEditor title strips: no close button on every
    // tab (the area has one), and no tab menu while an area holds one panel.
    ads::CDockManager::ConfigFlags flags = ads::CDockManager::DefaultOpaqueConfig | ads::CDockManager::DisableStylesheet |
                                           ads::CDockManager::FocusHighlighting | ads::CDockManager::DockAreaHideDisabledButtons |
                                           ads::CDockManager::DockAreaDynamicTabsMenuButtonVisibility;
    // No undock button either: narrow side docks need the room for their
    // titles, and dragging a title out still floats it.
    flags &= ~ads::CDockManager::ConfigFlags( ads::CDockManager::ActiveTabHasCloseButton | ads::CDockManager::DockAreaHasUndockButton );
    ads::CDockManager::setConfigFlags( flags );
    pDocking->pWindow = pWindow;
    pDocking->centralId = QString::fromUtf8( pCentralPanelId );
    pDocking->panels.clear();
    pDocking->docks.clear();
    pDocking->pCentralDock = nullptr;
    pDocking->pCentral = nullptr;
    // A QMainWindow parent makes the manager the window's central widget.
    pDocking->pManager = new ads::CDockManager( pWindow );
    pDocking->pManager->setObjectName( QStringLiteral( "EditorDockManager" ) );
}

bool EditorDocking_RegisterPanels( editor_docking_t *pDocking, const editor_panel_desc_t *pPanels, usize nPanels )
{
    CY_ASSERT( pDocking != nullptr && ( pPanels != nullptr || nPanels == 0u ) );
    for ( usize i = 0u; i < nPanels; ++i ) {
        if ( pPanels[i].pId == nullptr || pPanels[i].pTitle == nullptr || pPanels[i].pfnCreate == nullptr ||
             pDocking->panels.contains( QString::fromUtf8( pPanels[i].pId ) ) ) {
            return false;
        }
    }
    for ( usize i = 0u; i < nPanels; ++i ) { pDocking->panels.insert( QString::fromUtf8( pPanels[i].pId ), pPanels[i] ); }
    return true;
}

ads::CDockWidget *EditorDocking_Dock( editor_docking_t *pDocking, const QString &id )
{
    CY_ASSERT( pDocking != nullptr && pDocking->pManager != nullptr );
    if ( id == pDocking->centralId ) { return nullptr; }
    if ( ads::CDockWidget *pExisting = pDocking->docks.value( id, nullptr ) ) { return pExisting; }
    auto it = pDocking->panels.constFind( id );
    if ( it == pDocking->panels.constEnd() ) { return nullptr; }
    const QString title = QString::fromUtf8( it->pTitle );
    auto *pDock = new ads::CDockWidget( pDocking->pManager, pDocking->bUppercaseTitles ? title.toUpper() : title );
    // The object name is the panel ID: ADS keys docks by it and captured
    // layouts are built from it.
    pDock->setObjectName( id );
    // Panels manage their own scrolling (trees, consoles, viewports).
    pDock->setWidget( it->pfnCreate( it->pContext, pDock ), ads::CDockWidget::ForceNoScrollArea );
    pDocking->docks.insert( id, pDock );
    return pDock;
}

usize EditorDocking_ApplyLayout( editor_docking_t *pDocking, const layout_t &layout )
{
    CY_ASSERT( pDocking != nullptr && pDocking->pManager != nullptr );
    EnsureCentral( pDocking );
    ads::CDockManager *pManager = pDocking->pManager;
    // Start from nothing but the central panel; docks stay alive detached.
    for ( ads::CDockWidget *pDock : pDocking->docks ) {
        if ( pDock->dockAreaWidget() != nullptr ) { pManager->removeDockWidget( pDock ); }
    }

    builder_t builder{};
    builder.pDocking = pDocking;
    builder.pLayout = &layout;
    if ( layout.bHasRoot ) {
        std::vector<path_step_t> path;
        u32 iCentral = 0u;
        if ( FindCentralPath( layout, layout.iRoot, pDocking->centralId, path, iCentral ) ) {
            // Other panels in the central tab group cannot share ADS's
            // central area; they open beside it.
            ads::CDockAreaWidget *pCentralArea = pDocking->pCentralDock->dockAreaWidget();
            ( void )PlaceTabs( builder, iCentral, anchor_t{ ads::RightDockWidgetArea, pCentralArea, nullptr } );
            // Innermost level first, so each level wraps what is built.
            for ( auto step = path.rbegin(); step != path.rend(); ++step ) {
                const layout_node_t &node = layout.nodes.pData[step->iNode];
                const ads::DockWidgetArea before = node.bVertical ? ads::TopDockWidgetArea : ads::LeftDockWidgetArea;
                const ads::DockWidgetArea after = node.bVertical ? ads::BottomDockWidgetArea : ads::RightDockWidgetArea;
                size_request_t request;
                request.representatives.push_back( pCentralArea );
                request.weights.push_back( layout.sizes.pData[node.iFirstChild + step->iChild] );
                // Nearest sibling first, so farther ones land farther out.
                for ( u32 i = step->iChild; i-- > 0u; ) {
                    ads::CDockAreaWidget *pArea = PlaceAtEdge( builder, layout.children.pData[node.iFirstChild + i], before, nullptr );
                    if ( pArea == nullptr ) { continue; }
                    request.representatives.insert( request.representatives.begin(), pArea );
                    request.weights.insert( request.weights.begin(), layout.sizes.pData[node.iFirstChild + i] );
                }
                for ( u32 i = step->iChild + 1u; i < node.nChildren; ++i ) {
                    ads::CDockAreaWidget *pArea = PlaceAtEdge( builder, layout.children.pData[node.iFirstChild + i], after, nullptr );
                    if ( pArea == nullptr ) { continue; }
                    request.representatives.push_back( pArea );
                    request.weights.push_back( layout.sizes.pData[node.iFirstChild + i] );
                }
                if ( request.representatives.size() > 1u ) { builder.sizes.push_back( std::move( request ) ); }
            }
        } else {
            // No central branch: show the tree on the left rather than nothing.
            CY_LOG_WRITE( Warning, Gui, "Layout does not contain the central panel; docking it on the left" );
            ( void )PlaceAtEdge( builder, layout.iRoot, ads::LeftDockWidgetArea, nullptr );
        }
    }

    for ( u32 iWindow = 0u; iWindow < Vector_Count( &layout.windows ); ++iWindow ) {
        const layout_window_t &floating = layout.windows.pData[iWindow];
        // The first tab group starts the floating window; the rest of the
        // tree builds inside it.
        u32 iGroup = 0u;
        anchor_t anchor{};
        anchor.bFloat = true;
        ads::CDockAreaWidget *pFirst = PlaceFirstGroup( builder, floating.iRoot, anchor, iGroup );
        if ( pFirst == nullptr ) { continue; }
        Expand( builder, floating.iRoot, iGroup, pFirst );
        pFirst->window()->setGeometry( floating.x, floating.y, static_cast<int>( floating.width ), static_cast<int>( floating.height ) );
    }

    for ( const size_request_t &request : builder.sizes ) { ApplySizes( pDocking, request ); }
    if ( builder.nSkipped != 0u ) { CY_LOG_WRITE( Info, Gui, "Layout names panels that are not registered; they were skipped" ); }
    return builder.nSkipped;
}

bool EditorDocking_CaptureLayout( editor_docking_t *pDocking, layout_t *pLayout )
{
    CY_ASSERT( pDocking != nullptr && pDocking->pManager != nullptr && pLayout != nullptr );
    EditorLayout_Clear( pLayout );
    capture_t capture{ pDocking, pLayout, false };
    u32 iRoot = 0u;
    if ( CaptureWidget( capture, RootSplitter( pDocking->pManager ), iRoot ) ) {
        pLayout->iRoot = iRoot;
        pLayout->bHasRoot = CY_TRUE;
    }
    for ( ads::CFloatingDockContainer *pFloating : pDocking->pManager->floatingWidgets() ) {
        if ( pFloating == nullptr || pFloating->isHidden() ) { continue; }
        layout_window_t window{};
        if ( !CaptureWidget( capture, RootSplitter( pFloating->dockContainer() ), window.iRoot ) ) { continue; }
        const QRect rect = pFloating->geometry();
        window.x = rect.x();
        window.y = rect.y();
        window.width = static_cast<u32>( std::max( 1, rect.width() ) );
        window.height = static_cast<u32>( std::max( 1, rect.height() ) );
        if ( EditorLayout_AddWindow( pLayout, window ) != layout_status_t::OK ) { return false; }
    }
    return !capture.bFailed;
}

void EditorDocking_SetPanelVisible( editor_docking_t *pDocking, const QString &id, bool bVisible )
{
    CY_ASSERT( pDocking != nullptr && pDocking->pManager != nullptr );
    ads::CDockWidget *pDock = EditorDocking_Dock( pDocking, id );
    if ( pDock == nullptr ) { return; }
    if ( bVisible && pDock->dockAreaWidget() == nullptr ) {
        EnsureCentral( pDocking );
        ( void )pDocking->pManager->addDockWidget( ads::RightDockWidgetArea, pDock );
    }
    // Closing keeps the dock in its area, so reopening returns it there.
    if ( pDock->dockAreaWidget() != nullptr ) { pDock->toggleView( bVisible ); }
    if ( bVisible ) { pDock->setAsCurrentTab(); }
}

bool EditorDocking_IsPanelVisible( const editor_docking_t *pDocking, const QString &id )
{
    CY_ASSERT( pDocking != nullptr );
    const ads::CDockWidget *pDock = pDocking->docks.value( id, nullptr );
    return pDock != nullptr && pDock->dockAreaWidget() != nullptr && !pDock->isClosed();
}

} // namespace cypher::editor::gui
