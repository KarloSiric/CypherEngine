//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_History_Tests.cpp
//  Purpose: Verifies history navigation against a real undo stack, including
//           transactions, clean markers, failures, and document rebinding.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_History.h"
#include "CypherEditorGui_Application.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QFontMetrics>
#include <QLabel>
#include <QToolButton>
#include <QTreeWidget>

#include <memory>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

struct document_t {
    i32 value{ 0 };
    bool bFailUndo{ false };
    int nPanelApplies{ 0 };
    editor_history_status_t lastStatus{ editor_history_status_t::OK };
};

error_code_t Undo( binary_block_t payload, void *pContext ) noexcept
{
    auto &document = *static_cast<document_t *>( pContext );
    if ( document.bFailUndo ) { return Cy_ErrorMake( common_error_t::ERR_INVALID_STATE ); }
    document.value -= static_cast<i8>( payload.pData[0] );
    return CY_ERROR_OK;
}

error_code_t Redo( binary_block_t payload, void *pContext ) noexcept
{
    static_cast<document_t *>( pContext )->value += static_cast<i8>( payload.pData[0] );
    return CY_ERROR_OK;
}

void Applied( void *pContext, editor_history_status_t status ) noexcept
{
    auto &document = *static_cast<document_t *>( pContext );
    ++document.nPanelApplies;
    document.lastStatus = status;
}

void Add( editor_history_t &history, document_t &document, i8 amount, const char *pLabel )
{
    undo_operation_desc_t operation{};
    operation.id = 1u;
    operation.label = StringView_FromCString( pLabel );
    operation.payload = BinaryBlock_FromData( &amount, sizeof( amount ) );
    operation.pfnUndo = &Undo;
    operation.pfnRedo = &Redo;
    operation.pUserData = &document;
    document.value += amount;
    REQUIRE( EditorHistory_Push( &history, operation ) == editor_history_status_t::OK );
}

struct fixture_t {
    fixture_t()
    {
        EditorGui_RegisterResources();
        REQUIRE( EditorThemeRegistry_Init( &registry, Allocator_GetSystem() ) == theme_status_t::OK );
        REQUIRE( EditorStyle_RegisterTokens( &registry ) == theme_status_t::OK );
        style = EditorStyle_Resolve( &registry, nullptr, 0u );
        REQUIRE( EditorHistory_Init( &history, Allocator_GetSystem() ) == editor_history_status_t::OK );
        panel.reset( EditorHistoryPanel_Create( nullptr, &history, &style, &Applied, &document ) );
        REQUIRE( panel != nullptr );
        pTree = panel->findChild<QTreeWidget *>( QStringLiteral( "EditorHistorySteps" ) );
        pUndo = panel->findChild<QToolButton *>( QStringLiteral( "EditorHistoryUndo" ) );
        pRedo = panel->findChild<QToolButton *>( QStringLiteral( "EditorHistoryRedo" ) );
        pStatus = panel->findChild<QLabel *>( QStringLiteral( "EditorHistoryStatus" ) );
        REQUIRE( pTree != nullptr );
        REQUIRE( pUndo != nullptr );
        REQUIRE( pRedo != nullptr );
        REQUIRE( pStatus != nullptr );
    }

    ~fixture_t()
    {
        panel.reset();
        EditorThemeRegistry_Shutdown( &registry );
    }

    void ClickState( int row )
    {
        QTreeWidgetItem *pItem = pTree->topLevelItem( row );
        REQUIRE( pItem != nullptr );
        // Exercise the same signal as the view's mouse interaction without
        // making this state/navigation test depend on offscreen hit testing.
        pTree->itemClicked( pItem, 0 );
    }

    theme_registry_t registry{};
    editor_style_t style{};
    document_t document{};
    editor_history_t history{};
    std::unique_ptr<QWidget> panel{};
    QTreeWidget *pTree{};
    QToolButton *pUndo{};
    QToolButton *pRedo{};
    QLabel *pStatus{};
};

} // namespace

TEST_CASE( "History panel displays real edits and walks between applied and redo states", "[editor][gui][history]" )
{
    fixture_t f;
    CHECK( f.pTree->topLevelItemCount() == 1 );
    CHECK( f.pTree->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "Base state" ) );
    CHECK_FALSE( f.pUndo->isEnabled() );
    CHECK_FALSE( f.pRedo->isEnabled() );
    Add( f.history, f.document, 5, "Move brush" );
    Add( f.history, f.document, 3, "Extrude face" );
    Add( f.history, f.document, 2, "Bevel edge" );
    REQUIRE( f.pTree->topLevelItemCount() == 4 );
    CHECK( f.pTree->topLevelItem( 1 )->text( 0 ) == QStringLiteral( "Move brush" ) );
    CHECK( f.pTree->topLevelItem( 2 )->text( 0 ) == QStringLiteral( "Extrude face" ) );
    CHECK( f.pTree->topLevelItem( 1 )->text( 1 ) == QStringLiteral( "Applied" ) );
    CHECK( f.pTree->currentItem() == f.pTree->topLevelItem( 3 ) );
    CHECK( f.pUndo->toolTip() == QStringLiteral( "Undo Bevel edge" ) );

    f.ClickState( 1 );
    CHECK( f.document.value == 5 );
    CHECK( f.document.nPanelApplies == 1 );
    CHECK( EditorHistory_AppliedStepCount( &f.history ) == 1u );
    CHECK( f.pTree->topLevelItem( 2 )->text( 1 ) == QStringLiteral( "Redo" ) );
    CHECK( f.pRedo->isEnabled() );
    CHECK( f.pRedo->toolTip() == QStringLiteral( "Redo Extrude face" ) );
    f.ClickState( 3 );
    CHECK( f.document.value == 10 );
    CHECK( f.document.nPanelApplies == 2 );
    f.ClickState( 3 ); // Current-row clicks are not document mutations.
    CHECK( f.document.nPanelApplies == 2 );
    f.pUndo->click();
    CHECK( f.document.value == 8 );
    f.pRedo->click();
    CHECK( f.document.value == 10 );
    CHECK( f.document.nPanelApplies == 4 );
}

TEST_CASE( "History actions remain readable in a narrow inspector tab", "[editor][gui][history]" )
{
    fixture_t f;
    f.panel->setFont( f.style.uiFont );
    QFile stylesheet( QStringLiteral( ":/cypher/editor/style/editor.qss" ) );
    REQUIRE( stylesheet.open( QIODevice::ReadOnly ) );
    f.panel->setStyleSheet( EditorStyle_BuildStyleSheet( f.style, QString::fromUtf8( stylesheet.readAll() ) ) );
    f.panel->resize( 180, 320 );
    f.panel->show();
    QCoreApplication::processEvents();
    CHECK( f.panel->width() == 180 );
    CHECK( f.pTree->isHeaderHidden() );
    CHECK( f.pTree->isColumnHidden( 1 ) );
    CHECK( f.pTree->columnWidth( 0 ) >= f.pTree->viewport()->width() - 2 );
    const auto checkLabelFits = [&f]() {
        const auto *pCurrent = f.pTree->currentItem();
        REQUIRE( pCurrent != nullptr );
        CHECK_FALSE( pCurrent->icon( 0 ).isNull() );
        const QFont font = pCurrent->font( 0 ).resolve( f.pTree->font() );
        const int requiredWidth = QFontMetrics( font ).horizontalAdvance( pCurrent->text( 0 ) )
                                + f.pTree->iconSize().width() + 20;
        CHECK( requiredWidth < f.pTree->visualItemRect( pCurrent ).width() );
        CHECK( pCurrent->toolTip( 0 ).contains( pCurrent->text( 1 ) ) );
        CHECK( pCurrent->data( 0, Qt::AccessibleTextRole ).toString().contains( pCurrent->text( 1 ) ) );
    };
    checkLabelFits();
    auto *pCount = f.panel->findChild<QLabel *>( QStringLiteral( "EditorHistoryCount" ) );
    REQUIRE( pCount != nullptr );
    CHECK( pCount->text() == QStringLiteral( "0 edits" ) );
    Add( f.history, f.document, 1, "Extrude face" );
    QCoreApplication::processEvents();
    checkLabelFits();
    CHECK( pCount->text() == QStringLiteral( "1 edit" ) );
    f.pUndo->click();
    CHECK( pCount->text() == QStringLiteral( "0 / 1" ) );
    CHECK( pCount->toolTip() == QStringLiteral( "0 of 1 edits applied" ) );
    CHECK( f.pTree->topLevelItem( 1 )->foreground( 0 ).color() == EditorStyle_TokenColor( f.style, "ui.text.muted" ) );
}

TEST_CASE( "History panel follows transaction availability without displaying unfinished edits", "[editor][gui][history]" )
{
    fixture_t f;
    Add( f.history, f.document, 5, "Move" );
    REQUIRE( EditorHistory_Begin( &f.history, StringView_FromCString( "Drag vertices" ) ) == editor_history_status_t::OK );
    CHECK_FALSE( f.pUndo->isEnabled() );
    CHECK_FALSE( f.pRedo->isEnabled() );
    CHECK_FALSE( f.pTree->isEnabled() );
    CHECK( f.pTree->topLevelItemCount() == 0 );
    Add( f.history, f.document, 1, "Preview" );
    EditorHistoryPanel_Refresh( f.panel.get() );
    CHECK( f.pTree->topLevelItemCount() == 0 );
    f.pUndo->click();
    CHECK( f.document.value == 6 );
    CHECK( f.document.nPanelApplies == 0 );
    REQUIRE( EditorHistory_Commit( &f.history ) == editor_history_status_t::OK );
    CHECK( f.pTree->isEnabled() );
    REQUIRE( f.pTree->topLevelItemCount() == 3 );
    CHECK( f.pTree->topLevelItem( 2 )->text( 0 ) == QStringLiteral( "Drag vertices" ) );
    REQUIRE( EditorHistory_Begin( &f.history, StringView_FromCString( "Cancelled preview" ) ) == editor_history_status_t::OK );
    EditorHistory_Cancel( &f.history );
    CHECK( f.pUndo->isEnabled() );
    CHECK( f.pTree->isEnabled() );
    CHECK( f.pTree->topLevelItemCount() == 3 );
}

TEST_CASE( "History panel uses truthful clean markers across undo branches and resets", "[editor][gui][history]" )
{
    fixture_t f;
    CHECK( f.pStatus->text() == QStringLiteral( "Clean" ) );
    CHECK_FALSE( f.pTree->currentItem()->text( 1 ).contains( QStringLiteral( "Saved" ) ) );
    Add( f.history, f.document, 1, "One" );
    EditorHistory_MarkClean( &f.history );
    Add( f.history, f.document, 2, "Two" );
    CHECK( f.pStatus->text() == QStringLiteral( "Modified" ) );
    f.pUndo->click();
    CHECK( f.pStatus->text() == QStringLiteral( "Clean" ) );
    CHECK( f.pTree->currentItem()->text( 1 ) == QStringLiteral( "Current \u00B7 clean" ) );
    Add( f.history, f.document, 3, "Replacement" );
    REQUIRE( f.pTree->topLevelItemCount() == 3 );
    CHECK( f.pTree->topLevelItem( 2 )->text( 0 ) == QStringLiteral( "Replacement" ) );
    CHECK_FALSE( f.pRedo->isEnabled() );
    EditorHistory_Clear( &f.history );
    CHECK( f.pStatus->text() == QStringLiteral( "Modified" ) );
    CHECK( f.pTree->topLevelItemCount() == 1 );
    EditorHistory_MarkClean( &f.history ); // The same reset used for New/Open.
    CHECK( f.pStatus->text() == QStringLiteral( "Clean" ) );
    CHECK_FALSE( f.pUndo->isEnabled() );
}

TEST_CASE( "History panel reports failures and refreshes the owning workspace", "[editor][gui][history]" )
{
    fixture_t f;
    Add( f.history, f.document, 5, "Move" );
    f.document.bFailUndo = true;
    f.pUndo->click();
    CHECK( f.document.value == 5 );
    CHECK( f.document.nPanelApplies == 1 );
    CHECK( f.document.lastStatus == editor_history_status_t::APPLY_FAILED );
    CHECK( f.pStatus->text().contains( QStringLiteral( "failed" ) ) );
    CHECK( f.pTree->currentItem() == f.pTree->topLevelItem( 1 ) );
}

TEST_CASE( "History panel rebinding and closing detach old document listeners", "[editor][gui][history]" )
{
    fixture_t f;
    editor_history_t other{};
    document_t otherDocument{};
    REQUIRE( EditorHistory_Init( &other, Allocator_GetSystem() ) == editor_history_status_t::OK );
    Add( other, otherDocument, 4, "Other document" );
    REQUIRE( EditorHistoryPanel_Bind( f.panel.get(), &other ) );
    CHECK( f.history.nListeners == 0u );
    CHECK( other.nListeners == 1u );
    Add( f.history, f.document, 3, "Old document" );
    REQUIRE( f.pTree->topLevelItemCount() == 2 );
    CHECK( f.pTree->topLevelItem( 1 )->text( 0 ) == QStringLiteral( "Other document" ) );
    REQUIRE( EditorHistoryPanel_Bind( f.panel.get(), nullptr ) );
    CHECK( other.nListeners == 0u );
    CHECK_FALSE( f.pUndo->isEnabled() );
    CHECK( f.pTree->topLevelItemCount() == 0 );
    REQUIRE( EditorHistoryPanel_Bind( f.panel.get(), &f.history ) );
    f.panel.reset();
    CHECK( f.history.nListeners == 0u );
    Add( f.history, f.document, 1, "After closing" );
}

TEST_CASE( "A full listener table keeps the history panel on its original document", "[editor][gui][history]" )
{
    fixture_t f;
    editor_history_t other{};
    REQUIRE( EditorHistory_Init( &other, Allocator_GetSystem() ) == editor_history_status_t::OK );
    const auto ignore = []( void * ) noexcept {};
    for ( usize i = 0u; i < EDITOR_HISTORY_MAX_LISTENERS; ++i ) { REQUIRE( EditorHistory_AddListener( &other, ignore, nullptr ) ); }
    CHECK_FALSE( EditorHistoryPanel_Bind( f.panel.get(), &other ) );
    CHECK( f.history.nListeners == 1u );
    Add( f.history, f.document, 2, "Original document" );
    REQUIRE( f.pTree->topLevelItemCount() == 2 );
    CHECK( f.pTree->topLevelItem( 1 )->text( 0 ) == QStringLiteral( "Original document" ) );
    CHECK( EditorHistoryPanel_Create( nullptr, &other, &f.style ) == nullptr );
}
