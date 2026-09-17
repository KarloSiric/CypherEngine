//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Checks staging, synchronization, errors, and precision in map properties.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileDocumentBridge.h"
#include "CypherTileMapProperties.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace cypher::tools::tile_editor;

namespace
{
void EnsureMapPropertiesApplication()
{
    if ( QApplication::instance() )
        return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileMapPropertiesTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

template <typename T> T *Field( CypherTileMapProperties &widget, const char *name )
{
    auto *pField = widget.findChild<T *>( QString::fromLatin1( name ) );
    REQUIRE( pField != nullptr );
    return pField;
}
} // namespace

TEST_CASE( "Map properties stage dimensions and retain pending values across unrelated notifications",
           "[TileEditor][MapProperties]" )
{
    EnsureMapPropertiesApplication();
    CypherTileMapProperties widget;
    auto *pWidth = Field<QSpinBox>( widget, "TileMapWidth" );
    auto *pHeight = Field<QSpinBox>( widget, "TileMapHeight" );
    auto *pApply = Field<QPushButton>( widget, "TileMapApply" );
    auto *pRevert = Field<QPushButton>( widget, "TileMapRevert" );
    CHECK_FALSE( pWidth->isEnabled() );
    CHECK_FALSE( pApply->isEnabled() );
    CHECK( pWidth->minimum() == 1 );
    CHECK( pWidth->maximum() == static_cast<int>( TILE_MAP_MAX_WIDTH ) );
    CHECK( pHeight->maximum() == static_cast<int>( TILE_MAP_MAX_HEIGHT ) );

    CypherTileDocumentBridge bridge;
    REQUIRE( bridge.newDocument( { 8, 6, 2.0f, 3.0f } ) );
    int applyCount = 0;
    widget.setApplyCallback( [&]( const tile_map_document_desc_t &, QString & ) {
        ++applyCount;
        return true;
    } );
    widget.setDocument( bridge.document() );
    CHECK( pWidth->value() == 8 );
    CHECK( pHeight->value() == 6 );
    CHECK( Field<QLineEdit>( widget, "TileMapIdentity" )->isReadOnly() );
    CHECK_FALSE( Field<QLineEdit>( widget, "TileMapIdentity" )->text().isEmpty() );
    CHECK( Field<QLabel>( widget, "TileMapWorldExtents" )->text().contains( QStringLiteral( "16 × 12" ) ) );
    pWidth->setValue( 12 );
    CHECK( bridge.document()->nWidth == 8 );
    CHECK( pApply->isEnabled() );
    CHECK( pRevert->isEnabled() );
    CHECK( Field<QLabel>( widget, "TileMapWorldExtents" )->text().contains( QStringLiteral( "24 × 12" ) ) );
    widget.setDocument( bridge.document() );
    CHECK( pWidth->value() == 12 );
    widget.resetChanges();
    CHECK( pWidth->value() == 8 );
    CHECK_FALSE( pApply->isEnabled() );
    CHECK_FALSE( pRevert->isEnabled() );
    CHECK( applyCount == 0 );
}

TEST_CASE( "Rejected map properties keep staged inputs and plain text error without applying again",
           "[TileEditor][MapProperties]" )
{
    EnsureMapPropertiesApplication();
    CypherTileDocumentBridge bridge;
    REQUIRE( bridge.newDocument( { 8, 6, 2.0f, 3.0f } ) );
    CypherTileMapProperties widget;
    widget.setDocument( bridge.document() );
    int applyCount = 0;
    widget.setApplyCallback( [&]( const tile_map_document_desc_t &desc, QString &error ) {
        ++applyCount;
        CHECK( desc.nWidth == 4 );
        CHECK( desc.nHeight == 5 );
        error = QStringLiteral( "Occupied cell <7, 5> lies outside these bounds." );
        return false;
    } );
    Field<QSpinBox>( widget, "TileMapWidth" )->setValue( 4 );
    Field<QSpinBox>( widget, "TileMapHeight" )->setValue( 5 );
    CHECK_FALSE( widget.applyChanges() );
    CHECK( applyCount == 1 );
    CHECK( bridge.document()->nWidth == 8 );
    CHECK( Field<QSpinBox>( widget, "TileMapWidth" )->value() == 4 );
    auto *pMessage = Field<QLabel>( widget, "TileMapPropertiesMessage" );
    CHECK( pMessage->textFormat() == Qt::PlainText );
    CHECK( pMessage->text().contains( QStringLiteral( "<7, 5>" ) ) );
    widget.setDocument( bridge.document() );
    CHECK( pMessage->text().contains( QStringLiteral( "<7, 5>" ) ) );
    Field<QPushButton>( widget, "TileMapRevert" )->click();
    CHECK( Field<QSpinBox>( widget, "TileMapWidth" )->value() == 8 );
    CHECK( Field<QSpinBox>( widget, "TileMapHeight" )->value() == 6 );
    CHECK( applyCount == 1 );
}

TEST_CASE( "Map properties apply one description and accept synchronous document replacement",
           "[TileEditor][MapProperties]" )
{
    EnsureMapPropertiesApplication();
    CypherTileDocumentBridge bridge;
    REQUIRE( bridge.newDocument( { 8, 6, 2.0f, 3.0f } ) );
    CypherTileMapProperties widget;
    widget.setDocument( bridge.document() );
    int applyCount = 0;
    widget.setApplyCallback( [&]( const tile_map_document_desc_t &desc, QString &error ) {
        ++applyCount;
        CHECK( desc.nWidth == 12 );
        CHECK( desc.nHeight == 9 );
        CHECK( desc.nCellSize == 4.0f );
        CHECK( desc.nLevelHeight == 6.0f );
        // This is a widget-lifetime fixture, not the production resize command.
        const bool replaced = bridge.newDocument( desc, &error );
        widget.setDocument( bridge.document() );
        return replaced;
    } );
    Field<QSpinBox>( widget, "TileMapWidth" )->setValue( 12 );
    Field<QSpinBox>( widget, "TileMapHeight" )->setValue( 9 );
    Field<QDoubleSpinBox>( widget, "TileMapCellSize" )->setValue( 4.0 );
    Field<QDoubleSpinBox>( widget, "TileMapLevelHeight" )->setValue( 6.0 );
    CHECK( widget.applyChanges() );
    CHECK( applyCount == 1 );
    CHECK( bridge.document()->nWidth == 12 );
    CHECK( bridge.document()->nHeight == 9 );
    CHECK_FALSE( Field<QPushButton>( widget, "TileMapApply" )->isEnabled() );
    CHECK( widget.applyChanges() );
    CHECK( applyCount == 1 );
    widget.setDocument( nullptr );
    CHECK_FALSE( Field<QSpinBox>( widget, "TileMapWidth" )->isEnabled() );
    CHECK_FALSE( widget.applyChanges() );
    CHECK( applyCount == 1 );
}

TEST_CASE( "Editing map bounds preserves precise and unusually large loaded metrics",
           "[TileEditor][MapProperties]" )
{
    EnsureMapPropertiesApplication();
    const float preciseCellSize = std::nextafter( 2.0f, 3.0f );
    const float largeLevelHeight = 2048.125f;
    CypherTileDocumentBridge bridge;
    REQUIRE( bridge.newDocument( { 8, 6, preciseCellSize, largeLevelHeight } ) );
    CypherTileMapProperties widget;
    widget.setDocument( bridge.document() );
    auto *pLevel = Field<QDoubleSpinBox>( widget, "TileMapLevelHeight" );
    CHECK( pLevel->maximum() >= largeLevelHeight );
    CHECK( pLevel->value() == largeLevelHeight );
    bool captured = false;
    widget.setApplyCallback( [&]( const tile_map_document_desc_t &desc, QString &error ) {
        captured = true;
        CHECK( desc.nWidth == 9 );
        CHECK( desc.nCellSize == preciseCellSize );
        CHECK( desc.nLevelHeight == largeLevelHeight );
        error = QStringLiteral( "Keep fixture document unchanged." );
        return false;
    } );
    Field<QSpinBox>( widget, "TileMapWidth" )->setValue( 9 );
    CHECK_FALSE( widget.applyChanges() );
    CHECK( captured );

    // A new document must replace staged values, even if its metrics match.
    REQUIRE( bridge.newDocument( { 3, 4, preciseCellSize, largeLevelHeight } ) );
    widget.setDocument( bridge.document() );
    CHECK( Field<QSpinBox>( widget, "TileMapWidth" )->value() == 3 );
    CHECK( Field<QSpinBox>( widget, "TileMapHeight" )->value() == 4 );
    CHECK_FALSE( Field<QPushButton>( widget, "TileMapApply" )->isEnabled() );
}
