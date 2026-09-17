//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies viewport picking against the rendered blockout boxes.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileRenderViewport.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <QApplication>
#include <array>
#include <cmath>
#include <limits>

using namespace cypher::tools::tile_editor;
namespace math = ::cypher::math;

namespace {
void EnsurePickingApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TilePickingTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

tile_camera_t PickingCamera()
{
    tile_camera_t camera;
    camera.position = { 0, 0, 0 };
    camera.yawRadians = 0;
    camera.pitchRadians = 0;
    camera.settings.verticalFovDegrees = 90;
    return camera;
}

tile_map_geometry_box_t Box( float x, float y, float z, int cellX, int cellY )
{
    tile_map_geometry_box_t box;
    box.centerX = x;
    box.centerY = y;
    box.centerZ = z;
    box.halfExtentX = box.halfExtentY = box.halfExtentZ = 0.5f;
    box.sourceCell = { cellX, cellY };
    return box;
}
}

TEST_CASE( "3D picking chooses the nearest visible box regardless of storage order", "[TileEditor][Picking]" )
{
    const auto camera = PickingCamera();
    std::array boxes{ Box( 10, 0, 0, 1, 1 ), Box( 5, 0, 0, 2, 2 ), Box( -2, 0, 0, 3, 3 ) };
    tile_map_grid_coord_t hit{ -1, -1 };
    REQUIRE( CypherTileRenderViewport_PickGeometry( camera, boxes, 0, 0, 1, hit ) );
    CHECK( hit.x == 2 );
    CHECK( hit.y == 2 );
    std::swap( boxes[0], boxes[1] );
    REQUIRE( CypherTileRenderViewport_PickGeometry( camera, boxes, 0, 0, 1, hit ) );
    CHECK( hit.x == 2 );
}

TEST_CASE( "3D picking follows viewport aspect ratio and camera right and up", "[TileEditor][Picking]" )
{
    auto camera = PickingCamera();
    const std::array boxes{ Box( 5, -5, 0, 4, 1 ), Box( 5, 0, 2.5f, 4, 2 ) };
    tile_map_grid_coord_t hit{};
    REQUIRE( CypherTileRenderViewport_PickGeometry( camera, boxes, 0.5f, 0, 2, hit ) );
    CHECK( hit.y == 1 );
    CHECK_FALSE( CypherTileRenderViewport_PickGeometry( camera, boxes, 0.5f, 0, 1, hit ) );
    REQUIRE( CypherTileRenderViewport_PickGeometry( camera, boxes, 0, 0.5f, 1, hit ) );
    CHECK( hit.y == 2 );

    camera.settings.verticalFovDegrees = 60;
    const std::array narrowerFov{ Box( 5, -2.8867513f, 0, 6, 1 ) };
    REQUIRE( CypherTileRenderViewport_PickGeometry( camera, narrowerFov, 0.5f, 0, 2, hit ) );
    CHECK( hit.x == 6 );

    camera.yawRadians = 1.57079632679f;
    const std::array rotated{ Box( 0, 5, 0, 8, 2 ) };
    REQUIRE( CypherTileRenderViewport_PickGeometry( camera, rotated, 0, 0, 1, hit ) );
    CHECK( hit.x == 8 );
}

TEST_CASE( "3D picking respects near and far clipping and handles parallel rays", "[TileEditor][Picking]" )
{
    const auto camera = PickingCamera();
    auto tooClose = Box( 0.01f, 0, 0, 1, 1 );
    tooClose.halfExtentX = 0.005f;
    const std::array hidden{ tooClose, Box( -5, 0, 0, 2, 2 ), Box( 200, 0, 0, 3, 3 ), Box( 5, 2, 0, 4, 4 ) };
    tile_map_grid_coord_t hit{ 9, 9 };
    CHECK_FALSE( CypherTileRenderViewport_PickGeometry( camera, hidden, 0, 0, 1, hit ) );
    CHECK( hit.x == 9 );
    CHECK( hit.y == 9 );
    const std::array enclosing{ Box( 0, 0, 0, 7, 7 ) };
    REQUIRE( CypherTileRenderViewport_PickGeometry( camera, enclosing, 0, 0, 1, hit ) );
    CHECK( hit.x == 7 );
}

TEST_CASE( "3D picking rejects invalid inputs and ignores malformed boxes", "[TileEditor][Picking]" )
{
    auto camera = PickingCamera();
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    auto malformed = Box( invalid, 0, 0, 1, 1 );
    const std::array boxes{ malformed, Box( 5, 0, 0, 2, 2 ) };
    tile_map_grid_coord_t hit{};
    REQUIRE( CypherTileRenderViewport_PickGeometry( camera, boxes, 0, 0, 1, hit ) );
    CHECK( hit.x == 2 );
    CHECK_FALSE( CypherTileRenderViewport_PickGeometry( camera, boxes, invalid, 0, 1, hit ) );
    CHECK_FALSE( CypherTileRenderViewport_PickGeometry( camera, boxes, 0, 0, 0, hit ) );
    CHECK_FALSE( CypherTileRenderViewport_PickGeometry( camera, {}, 0, 0, 1, hit ) );
    camera.position.x = invalid;
    CHECK_FALSE( CypherTileRenderViewport_PickGeometry( camera, boxes, 0, 0, 1, hit ) );
}

TEST_CASE( "3D framing includes every selected cell and excludes distant unselected cells", "[TileEditor][Selection]" )
{
    EnsurePickingApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    description.nWidth = 12;
    description.nHeight = 10;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Selection fixture" ), &error ) );
    REQUIRE( document.paintCell( { 2, 2 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.paintCell( { 5, 4 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.paintCell( { 10, 8 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    CypherTileRenderViewport viewport;
    viewport.resize( 640, 480 );
    viewport.setDocumentBridge( &document );
    viewport.setSelection( true, { 2, 2 } );
    viewport.frameSelection();
    const auto singleDistance = viewport.camera().orbitDistance;
    viewport.setSelectionRect( true, { 2, 2, 4, 3 } );
    const auto mapBoundsCenter = viewport.camera().boundsCenter;
    auto expected = viewport.camera();
    // The GUI must frame only the two selected cells. Core framing may pan
    // laterally to center their projected silhouette; its orbit pivot need
    // not coincide with the selected AABB's geometric center.
    CypherTileCamera_FrameBounds( expected,
        { 2 * description.nCellSize, 2 * description.nCellSize, -TILE_MAP_DEFAULT_FLOOR_THICKNESS },
        { 6 * description.nCellSize, 5 * description.nCellSize, description.nLevelHeight }, 640.0f / 480.0f );
    viewport.frameSelection();
    const auto &camera = viewport.camera();
    CHECK( camera.orbitDistance > singleDistance );
    CHECK( camera.orbitDistance == Catch::Approx( expected.orbitDistance ) );
    CHECK( math::Vec3_NearlyEquals( camera.position, expected.position, 0.001f, 0.00001f ) );
    CHECK( camera.yawRadians == expected.yawRadians );
    CHECK( camera.pitchRadians == expected.pitchRadians );
    // Framing selection must still retain whole-map bounds for far clipping.
    CHECK( math::Vec3_EqualsExact( camera.boundsCenter, mapBoundsCenter ) );
    const auto regionDistance = camera.orbitDistance;
    viewport.setSelectionRect( false, {} );
    viewport.frameSelection();
    CHECK( viewport.camera().orbitDistance > regionDistance );
}
