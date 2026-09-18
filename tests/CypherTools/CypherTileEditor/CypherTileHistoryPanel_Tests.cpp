//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies the editor history panel against the real document stack.
//////////////////////////////////////////////////////////////////////////

#include "CypherTileHistoryPanel.h"

#include "CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QAbstractItemView>
#include <QLabel>
#include <QToolButton>
#include <QTreeWidget>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{

void EnsureHistoryApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char name[] = "CypherTileHistoryPanelTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

void CommitPaint(
    tile_map_document_t &document,
    const char *pLabel,
    tile_map_grid_coord_t coordinate )
{
    REQUIRE( CypherTileMapDocument_BeginEditGroup(
                 &document,
                 StringView_FromCString( pLabel ) ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 coordinate,
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_CommitEditGroup( &document ) ==
             tile_map_document_status_t::OK );
}

template <typename Widget>
Widget *HistoryWidget( CypherTileHistoryPanel &panel, const char *pName )
{
    auto *pWidget = panel.findChild<Widget *>( QString::fromLatin1( pName ) );
    REQUIRE( pWidget != nullptr );
    return pWidget;
}

} // namespace

TEST_CASE( "History panel distinguishes current saved applied and redo states",
           "[TileEditor][History][Widget]" )
{
    EnsureHistoryApplication();
    tile_map_document_t document{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 8u, 8u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    CypherTileMapDocument_MarkSaved( &document );
    CommitPaint( document, "Paint west room", { 1, 1 } );
    CypherTileMapDocument_MarkSaved( &document );
    const u64 savedRevision = document.nSavedRevision;
    CommitPaint( document, "Paint east room", { 6, 6 } );

    CypherTileHistoryPanel panel;
    panel.setUndoCallback( [&] {
        REQUIRE( CypherTileMapDocument_Undo( &document ) ==
                 tile_map_document_status_t::OK );
        panel.refresh();
    } );
    panel.setRedoCallback( [&] {
        REQUIRE( CypherTileMapDocument_Redo( &document ) ==
                 tile_map_document_status_t::OK );
        panel.refresh();
    } );
    panel.setDocument( &document );

    auto *pTree = HistoryWidget<QTreeWidget>( panel, "TileHistoryTree" );
    auto *pUndo = HistoryWidget<QToolButton>( panel, "TileHistoryUndo" );
    auto *pRedo = HistoryWidget<QToolButton>( panel, "TileHistoryRedo" );
    auto *pBadge = HistoryWidget<QLabel>( panel, "TileHistoryStateBadge" );
    auto *pSummary = HistoryWidget<QLabel>( panel, "TileHistorySummary" );
    auto *pDetails = HistoryWidget<QLabel>( panel, "TileHistoryDetails" );

    REQUIRE( pTree->topLevelItemCount() == 3 );
    auto *pBoundaryRow = pTree->topLevelItem( 0 );
    auto *pWestRow = pTree->topLevelItem( 1 );
    auto *pEastRow = pTree->topLevelItem( 2 );
    CHECK( pTree->topLevelItem( 0 )->text( 0 ).contains( "History boundary" ) );
    CHECK( pTree->topLevelItem( 1 )->text( 0 ).contains( "Paint west room" ) );
    CHECK( pTree->topLevelItem( 1 )->text( 1 ).contains( "Saved" ) );
    CHECK( pTree->topLevelItem( 2 )->text( 0 ).contains( "Paint east room" ) );
    CHECK( pTree->topLevelItem( 2 )->text( 1 ).contains( "Current" ) );
    CHECK( pTree->selectionMode() == QAbstractItemView::NoSelection );
    CHECK( pTree->currentItem() == nullptr );
    CHECK( pTree->selectedItems().isEmpty() );
    CHECK( pBadge->text() == "UNSAVED" );
    CHECK( pSummary->text().contains( "2 / 2 actions applied" ) );
    CHECK( pSummary->text().contains(
        QString( "saved r%1" ).arg( static_cast<qulonglong>( savedRevision ) ) ) );
    CHECK( pUndo->isEnabled() );
    CHECK_FALSE( pRedo->isEnabled() );
    CHECK( pUndo->text() == "Undo" );
    CHECK( pUndo->toolTip().contains( "Paint east room" ) );
    CHECK( pDetails->text().contains( "Action 2 of 2" ) );

    pUndo->click();
    REQUIRE( document.nCurrentRevision == savedRevision );
    CHECK( pTree->topLevelItem( 0 ) == pBoundaryRow );
    CHECK( pTree->topLevelItem( 1 ) == pWestRow );
    CHECK( pTree->topLevelItem( 2 ) == pEastRow );
    CHECK( pTree->currentItem() == nullptr );
    CHECK( pTree->selectedItems().isEmpty() );
    CHECK( pTree->topLevelItem( 1 )->text( 1 ).contains( "Current" ) );
    CHECK( pTree->topLevelItem( 2 )->text( 1 ).contains( "Redo" ) );
    CHECK( pBadge->text() == "SAVED" );
    CHECK( pUndo->isEnabled() );
    CHECK( pRedo->isEnabled() );
    CHECK( pRedo->text() == "Redo" );
    CHECK( pRedo->toolTip().contains( "Paint east room" ) );

    pRedo->click();
    CHECK( pTree->topLevelItem( 0 ) == pBoundaryRow );
    CHECK( pTree->topLevelItem( 1 ) == pWestRow );
    CHECK( pTree->topLevelItem( 2 ) == pEastRow );
    CHECK( pTree->currentItem() == nullptr );
    CHECK( pTree->selectedItems().isEmpty() );
    CHECK( pBadge->text() == "UNSAVED" );

    // Inspecting an older row updates details without creating a Qt current or
    // selected item. macOS accessibility must always observe an empty selected
    // child set for this timeline.
    Q_EMIT pTree->itemClicked( pWestRow, 0 );
    CHECK( pDetails->text().contains( "Action 1 of 2" ) );
    CHECK( pTree->currentItem() == nullptr );
    CHECK( pTree->selectedItems().isEmpty() );

    // Native accessibility objects can outlive a refresh. A temporarily
    // detached document must hide retained rows without deleting their item
    // identities, then reuse those exact rows when the document returns.
    panel.setDocument( nullptr );
    CHECK( pTree->topLevelItemCount() == 3 );
    CHECK( pTree->currentItem() == nullptr );
    CHECK( pTree->selectedItems().isEmpty() );
    CHECK( pBoundaryRow->isHidden() );
    CHECK( pWestRow->isHidden() );
    CHECK( pEastRow->isHidden() );
    panel.setDocument( &document );
    CHECK( pTree->topLevelItem( 0 ) == pBoundaryRow );
    CHECK( pTree->topLevelItem( 1 ) == pWestRow );
    CHECK( pTree->topLevelItem( 2 ) == pEastRow );
    CHECK_FALSE( pBoundaryRow->isHidden() );
    CHECK_FALSE( pWestRow->isHidden() );
    CHECK_FALSE( pEastRow->isHidden() );
    CHECK( pTree->currentItem() == nullptr );
    CHECK( pTree->selectedItems().isEmpty() );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "History panel presents a safe empty state without a document",
           "[TileEditor][History][Widget]" )
{
    EnsureHistoryApplication();
    CypherTileHistoryPanel panel;
    auto *pTree = HistoryWidget<QTreeWidget>( panel, "TileHistoryTree" );
    auto *pUndo = HistoryWidget<QToolButton>( panel, "TileHistoryUndo" );
    auto *pRedo = HistoryWidget<QToolButton>( panel, "TileHistoryRedo" );
    auto *pBadge = HistoryWidget<QLabel>( panel, "TileHistoryStateBadge" );
    auto *pDetails = HistoryWidget<QLabel>( panel, "TileHistoryDetails" );
    CHECK( pTree->topLevelItemCount() == 0 );
    CHECK_FALSE( pUndo->isEnabled() );
    CHECK_FALSE( pRedo->isEnabled() );
    CHECK( pBadge->text() == "NO MAP" );
    CHECK( pDetails->text().contains( "No document history" ) );
}
