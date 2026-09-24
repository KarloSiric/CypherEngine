//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookSourceMapping_Tests.cpp
//  Purpose: Verifies deterministic neutral Cook with source provenance.
//  Details: Covers Gate 7 Cook acceptance: canonical box triangle count,
//           per-triangle source mapping, content hash stability across
//           repeated cooks, and error paths for null/uninitialized input.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookSourceMapping.h"
#include "CypherGeometry_BrushSerialization.h"
#include "CypherGeometry_Document.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;

namespace {

struct cook_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *CookFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<cook_failure_allocator_state_t *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) {
        return nullptr;
    }

    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    void *pMemory = pSystem->pfnAllocate(
        pSystem->pUserData, cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
    }
    return pMemory;
}

void CookFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<cook_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFrees;
    }

    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    pSystem->pfnFree(
        pSystem->pUserData, pMemory, cbSize, nAlignment );
}

common::allocator_t MakeCookFailureAllocator(
    cook_failure_allocator_state_t *pState ) noexcept
{
    return {
        &CookFailureAllocate,
        nullptr,
        &CookFailureFree,
        pState
    };
}

// Shared fixture: document with one or more brushes, ready to cook.
struct CookFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    geometry_document_t document{};
    geometry_cook_result_t cookResult{};

    CookFixture()
    {
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryCook_Init(
                     &cookResult, &allocator ) ==
                 geometry_cook_status_t::OK );
    }

    ~CookFixture()
    {
        GeometryCook_Shutdown( &cookResult );
        GeometryDocument_Shutdown( &document );
    }

    void AddBox(
        math::vec3d_t center = Vec3d_Make( 0.0, 0.0, 0.0 ),
        math::vec3d_t halfExtents = Vec3d_Make( 1.0, 1.0, 1.0 ) )
    {
        brush_solid_t brush{};
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAlloc,
                     center, halfExtents ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrush(
                     &document, &brush ) ==
                 geometry_status_t::OK );
        BrushSolid_Shutdown( &brush );
    }

    geometry_cook_status_t Cook()
    {
        return GeometryCook_TryCook(
            &cookResult, &document, policy );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Canonical box Cook
// ---------------------------------------------------------------------------

TEST_CASE( "Cook: canonical box produces 12 triangles and 8 vertices",
           "[Gate7][Cook]" )
{
    CookFixture f;
    f.AddBox();

    REQUIRE( f.Cook() == geometry_cook_status_t::OK );
    REQUIRE( GeometryCook_VertexCount( &f.cookResult ) == 8u );
    REQUIRE( GeometryCook_TriangleCount( &f.cookResult ) == 12u );
}

TEST_CASE( "Cook: every triangle has valid source mapping",
           "[Gate7][Cook]" )
{
    CookFixture f;
    f.AddBox();
    REQUIRE( f.Cook() == geometry_cook_status_t::OK );

    const common::usize cTris =
        GeometryCook_TriangleCount( &f.cookResult );
    for ( common::usize i = 0u; i < cTris; ++i ) {
        const cook_triangle_t &tri =
            f.cookResult.triangles.pData[i];

        // Every source ID must be non-zero (valid).
        REQUIRE( tri.source.brushSourceId.value != 0u );
        REQUIRE( tri.source.sideSourceId.value != 0u );

        // Side index must be within the box's 6 sides.
        REQUIRE( tri.source.iSideIndex < 6u );

        // Vertex indices must be within bounds.
        REQUIRE( tri.iVertex0 < 8u );
        REQUIRE( tri.iVertex1 < 8u );
        REQUIRE( tri.iVertex2 < 8u );
    }
}

// ---------------------------------------------------------------------------
// Determinism: repeated cooks yield identical hashes
// ---------------------------------------------------------------------------

TEST_CASE( "Cook: same input produces identical hash across two cooks",
           "[Gate7][Cook]" )
{
    CookFixture f;
    f.AddBox();

    REQUIRE( f.Cook() == geometry_cook_status_t::OK );
    const common::content_hash_t firstHash =
        GeometryCook_ContentHash( &f.cookResult );
    REQUIRE( common::ContentHash_IsValid( firstHash ) );

    // Cook again — hash must be identical.
    REQUIRE( f.Cook() == geometry_cook_status_t::OK );
    const common::content_hash_t secondHash =
        GeometryCook_ContentHash( &f.cookResult );
    REQUIRE( common::ContentHash_Equals( firstHash, secondHash ) );
}

TEST_CASE( "Cook: multi-brush cook is deterministic",
           "[Gate7][Cook]" )
{
    CookFixture f;
    f.AddBox( Vec3d_Make( 0.0, 0.0, 0.0 ) );
    f.AddBox( Vec3d_Make( 5.0, 0.0, 0.0 ) );

    REQUIRE( f.Cook() == geometry_cook_status_t::OK );
    const common::content_hash_t firstHash =
        GeometryCook_ContentHash( &f.cookResult );

    // Two boxes: 16 vertices, 24 triangles.
    REQUIRE( GeometryCook_VertexCount( &f.cookResult ) == 16u );
    REQUIRE( GeometryCook_TriangleCount( &f.cookResult ) == 24u );

    // Re-cook and verify.
    REQUIRE( f.Cook() == geometry_cook_status_t::OK );
    REQUIRE( common::ContentHash_Equals(
                 firstHash,
                 GeometryCook_ContentHash( &f.cookResult ) ) );
}

// ---------------------------------------------------------------------------
// Determinism: cook after serialize → load yields same hash
// ---------------------------------------------------------------------------

TEST_CASE( "Cook: serialize/load/cook produces same hash as direct cook",
           "[Gate7][Cook]" )
{
    CookFixture f;
    f.AddBox( Vec3d_Make( 1.0, 2.0, 3.0 ),
              Vec3d_Make( 0.5, 0.75, 1.25 ) );

    // Direct cook.
    REQUIRE( f.Cook() == geometry_cook_status_t::OK );
    const common::content_hash_t directHash =
        GeometryCook_ContentHash( &f.cookResult );

    // Serialize.
    common::text_buffer_t textBuf{};
    REQUIRE( common::TextBuffer_Init( &textBuf, &f.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText(
                 &f.document, &textBuf ).status ==
             geometry_serialization_status_t::OK );

    // Load into a fresh document.
    geometry_document_t loaded{};
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::TextBuffer_View( &textBuf ),
                 &f.allocator, f.policy, &loaded ).status ==
             geometry_serialization_status_t::OK );

    // Cook the loaded document.
    geometry_cook_result_t loadedCook{};
    REQUIRE( GeometryCook_Init(
                 &loadedCook, &f.allocator ) ==
             geometry_cook_status_t::OK );
    REQUIRE( GeometryCook_TryCook(
                 &loadedCook, &loaded, f.policy ) ==
             geometry_cook_status_t::OK );

    // Hashes must match.
    REQUIRE( common::ContentHash_Equals(
                 directHash,
                 GeometryCook_ContentHash( &loadedCook ) ) );

    GeometryCook_Shutdown( &loadedCook );
    GeometryDocument_Shutdown( &loaded );
}

// ---------------------------------------------------------------------------
// Empty document
// ---------------------------------------------------------------------------

TEST_CASE( "Cook: empty document produces zero vertices and triangles",
           "[Gate7][Cook]" )
{
    CookFixture f;
    REQUIRE( f.Cook() == geometry_cook_status_t::OK );
    REQUIRE( GeometryCook_VertexCount( &f.cookResult ) == 0u );
    REQUIRE( GeometryCook_TriangleCount( &f.cookResult ) == 0u );
}

// ---------------------------------------------------------------------------
// Error paths
// ---------------------------------------------------------------------------

TEST_CASE( "Cook: null arguments rejected", "[Gate7][Cook]" )
{
    REQUIRE( GeometryCook_Init( nullptr, nullptr ) ==
             geometry_cook_status_t::INVALID_ARGUMENT );

    REQUIRE( GeometryCook_TryCook( nullptr, nullptr, {} ) ==
             geometry_cook_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Cook: double init and invalid policy are rejected without damage",
           "[Gate7][Cook][contract]" )
{
    CookFixture f;
    f.AddBox();

    REQUIRE( GeometryCook_Init( &f.cookResult, &f.allocator ) ==
             geometry_cook_status_t::INVALID_ARGUMENT );
    REQUIRE( f.Cook() == geometry_cook_status_t::OK );

    geometry_policy_t invalidPolicy = f.policy;
    invalidPolicy.numerical.fAbsoluteDistanceTolerance =
        std::numeric_limits<common::f64>::quiet_NaN();
    REQUIRE( GeometryCook_TryCook(
                 &f.cookResult, &f.document, invalidPolicy ) ==
             geometry_cook_status_t::INVALID_ARGUMENT );

    // Invalid preconditions do not destroy the last valid published result.
    CHECK( GeometryCook_VertexCount( &f.cookResult ) == 8u );
    CHECK( GeometryCook_TriangleCount( &f.cookResult ) == 12u );
}

TEST_CASE( "Cook: StatusName returns non-null", "[Gate7][Cook]" )
{
    REQUIRE( GeometryCook_StatusName(
                 geometry_cook_status_t::OK ) != nullptr );
    REQUIRE( GeometryCook_StatusName(
                 geometry_cook_status_t::BOUNDARY_FAILED ) != nullptr );
    REQUIRE( GeometryCook_StatusName(
                 geometry_cook_status_t::ATTRIBUTE_FAILED ) != nullptr );
}

TEST_CASE( "Cook: every allocation failure leaves empty output",
           "[Gate7][Cook][allocation][contract]" )
{
    CookFixture f;
    f.AddBox();

    cook_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeCookFailureAllocator( &baselineState );
    geometry_cook_result_t baseline{};
    REQUIRE( GeometryCook_Init( &baseline, &baselineAllocator ) ==
             geometry_cook_status_t::OK );
    REQUIRE( GeometryCook_TryCook(
                 &baseline, &f.document, f.policy ) ==
             geometry_cook_status_t::OK );
    const common::usize cAllocationCalls =
        baselineState.cAllocationCalls;
    REQUIRE( cAllocationCalls > 0u );
    GeometryCook_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFrees );

    for ( common::usize iFail = 1u;
          iFail <= cAllocationCalls;
          ++iFail ) {
        CAPTURE( iFail, cAllocationCalls );
        cook_failure_allocator_state_t state{};
        state.iFailOnCall = iFail;
        common::allocator_t allocator = MakeCookFailureAllocator( &state );
        geometry_cook_result_t result{};
        REQUIRE( GeometryCook_Init( &result, &allocator ) ==
                 geometry_cook_status_t::OK );

        REQUIRE( GeometryCook_TryCook(
                     &result, &f.document, f.policy ) ==
                 geometry_cook_status_t::OUT_OF_MEMORY );
        CHECK( GeometryCook_VertexCount( &result ) == 0u );
        CHECK( GeometryCook_TriangleCount( &result ) == 0u );
        CHECK_FALSE( common::ContentHash_IsValid(
            GeometryCook_ContentHash( &result ) ) );
        CHECK( result.status == geometry_cook_status_t::OUT_OF_MEMORY );

        GeometryCook_Shutdown( &result );
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }
}

// ---------------------------------------------------------------------------
// Full Cook (normals + UVs)
// ---------------------------------------------------------------------------

namespace {

struct FullCookFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    geometry_document_t document{};
    geometry_cook_full_result_t cookResult{};

    FullCookFixture()
    {
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryCookFull_Init(
                     &cookResult, &allocator ) ==
                 geometry_cook_status_t::OK );
    }

    ~FullCookFixture()
    {
        GeometryCookFull_Shutdown( &cookResult );
        GeometryDocument_Shutdown( &document );
    }

    void AddBox(
        math::vec3d_t center = Vec3d_Make( 0.0, 0.0, 0.0 ),
        math::vec3d_t halfExtents = Vec3d_Make( 1.0, 1.0, 1.0 ) )
    {
        brush_solid_t brush{};
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAlloc,
                     center, halfExtents ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrush(
                     &document, &brush ) ==
                 geometry_status_t::OK );
        BrushSolid_Shutdown( &brush );
    }

    geometry_cook_status_t CookFull(
        const cook_brush_attributes_t *pAttrs = nullptr,
        common::usize cAttrs = 0u )
    {
        return GeometryCook_TryCookFull(
            &cookResult, &document, policy, pAttrs, cAttrs );
    }
};

} // namespace

TEST_CASE( "FullCook: box produces 24 split vertices (6 faces × 4)",
           "[Gate8][FullCook]" )
{
    FullCookFixture f;
    f.AddBox();

    REQUIRE( f.CookFull() == geometry_cook_status_t::OK );

    // 6 faces × 4 vertices per face = 24 split vertices.
    REQUIRE( GeometryCookFull_VertexCount( &f.cookResult ) == 24u );

    // 6 faces × 2 triangles per quad = 12 triangles.
    REQUIRE( GeometryCookFull_TriangleCount( &f.cookResult ) == 12u );
}

TEST_CASE( "FullCook: every vertex has unit-length normal",
           "[Gate8][FullCook]" )
{
    FullCookFixture f;
    f.AddBox();
    REQUIRE( f.CookFull() == geometry_cook_status_t::OK );

    const common::usize cVerts =
        GeometryCookFull_VertexCount( &f.cookResult );
    for ( common::usize i = 0u; i < cVerts; ++i ) {
        const cook_vertex_t &v = f.cookResult.vertices.pData[i];
        const common::f64 lenSq =
            cypher::math::Vec3d_LengthSquared( v.normal );
        REQUIRE( lenSq == Catch::Approx( 1.0 ).margin( 1e-10 ) );
    }
}

TEST_CASE( "FullCook: box normals are axis-aligned",
           "[Gate8][FullCook]" )
{
    FullCookFixture f;
    f.AddBox();
    REQUIRE( f.CookFull() == geometry_cook_status_t::OK );

    // A box has 6 faces, each with an axis-aligned normal. Each face
    // contributes 4 vertices, all sharing that normal. Count unique
    // normals by checking the 6 canonical directions.
    common::usize nAxisCounts[6] = {};
    const common::usize cVerts =
        GeometryCookFull_VertexCount( &f.cookResult );
    for ( common::usize i = 0u; i < cVerts; ++i ) {
        const math::vec3d_t &n =
            f.cookResult.vertices.pData[i].normal;
        if ( n.x > 0.5 ) ++nAxisCounts[0];       // +X
        else if ( n.x < -0.5 ) ++nAxisCounts[1];  // -X
        else if ( n.y > 0.5 ) ++nAxisCounts[2];   // +Y
        else if ( n.y < -0.5 ) ++nAxisCounts[3];  // -Y
        else if ( n.z > 0.5 ) ++nAxisCounts[4];   // +Z
        else if ( n.z < -0.5 ) ++nAxisCounts[5];  // -Z
    }
    // Each axis direction should have exactly 4 vertices.
    for ( auto c : nAxisCounts ) {
        REQUIRE( c == 4u );
    }
}

TEST_CASE( "FullCook: default UVs are finite and deterministic",
           "[Gate8][FullCook]" )
{
    FullCookFixture f;
    f.AddBox();
    REQUIRE( f.CookFull() == geometry_cook_status_t::OK );

    const common::usize cVerts =
        GeometryCookFull_VertexCount( &f.cookResult );
    for ( common::usize i = 0u; i < cVerts; ++i ) {
        const math::vec2d_t &uv =
            f.cookResult.vertices.pData[i].uv;
        REQUIRE( std::isfinite( uv.x ) );
        REQUIRE( std::isfinite( uv.y ) );
    }

    // Determinism: cook again, same results.
    geometry_cook_full_result_t second{};
    REQUIRE( GeometryCookFull_Init( &second, &f.allocator ) ==
             geometry_cook_status_t::OK );
    REQUIRE( GeometryCook_TryCookFull(
                 &second, &f.document, f.policy, nullptr, 0u ) ==
             geometry_cook_status_t::OK );
    REQUIRE( common::ContentHash_Equals(
                 GeometryCookFull_ContentHash( &f.cookResult ),
                 GeometryCookFull_ContentHash( &second ) ) );
    GeometryCookFull_Shutdown( &second );
}

TEST_CASE( "FullCook: triangle indices reference valid vertices",
           "[Gate8][FullCook]" )
{
    FullCookFixture f;
    f.AddBox();
    REQUIRE( f.CookFull() == geometry_cook_status_t::OK );

    const common::usize cVerts =
        GeometryCookFull_VertexCount( &f.cookResult );
    const common::usize cTris =
        GeometryCookFull_TriangleCount( &f.cookResult );
    for ( common::usize i = 0u; i < cTris; ++i ) {
        const cook_triangle_t &tri =
            f.cookResult.triangles.pData[i];
        REQUIRE( tri.iVertex0 < cVerts );
        REQUIRE( tri.iVertex1 < cVerts );
        REQUIRE( tri.iVertex2 < cVerts );
    }
}

TEST_CASE( "FullCook: with attribute store, UVs use projection",
           "[Gate8][FullCook]" )
{
    FullCookFixture f;
    f.AddBox();

    // Create an attribute store for the box brush with custom UV
    // projections on all 6 sides.
    geometry_brush_side_attribute_store_t attrStore{};
    REQUIRE( BrushSideAttributeStore_Init( &attrStore, &f.allocator ) ==
             geometry_status_t::OK );

    for ( common::usize i = 0u; i < 6u; ++i ) {
        geometry_brush_side_attributes_t attrs =
            BrushSideAttributes_MakeDefault();
        // Custom scale: 2 world units per UV unit.
        attrs.uvProjection.worldUnitsPerUv =
            math::Vec2d_Make( 2.0, 2.0 );
        REQUIRE( BrushSideAttributeStore_TryAppend(
                     &attrStore, f.policy, attrs, nullptr ) ==
                 geometry_status_t::OK );
    }

    cook_brush_attributes_t brushAttrs{};
    brushAttrs.pStore = &attrStore;

    REQUIRE( f.CookFull( &brushAttrs, 1u ) == geometry_cook_status_t::OK );

    // Just verify it produced valid output — the UVs will differ from
    // the default projection.
    const common::usize cVerts =
        GeometryCookFull_VertexCount( &f.cookResult );
    REQUIRE( cVerts == 24u );
    for ( common::usize i = 0u; i < cVerts; ++i ) {
        const math::vec2d_t &uv =
            f.cookResult.vertices.pData[i].uv;
        REQUIRE( std::isfinite( uv.x ) );
        REQUIRE( std::isfinite( uv.y ) );
    }

    BrushSideAttributeStore_Shutdown( &attrStore );
}

TEST_CASE( "FullCook: side attribute index selects the canonical record",
           "[Gate8][FullCook][Attributes][contract]" )
{
    FullCookFixture f;
    f.AddBox();

    brush_solid_t *pBrush = f.document.brushes.pData[0];
    REQUIRE( pBrush != nullptr );
    for ( common::usize i = 0u;
          i < BrushSolid_SideCount( pBrush );
          ++i ) {
        pBrush->sides.pData[i].iAttributeIndex = 1u;
    }

    geometry_brush_side_attribute_store_t attrStore{};
    REQUIRE( BrushSideAttributeStore_Init( &attrStore, &f.allocator ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t unused =
        BrushSideAttributes_MakeDefault();
    unused.uvProjection.offset = math::Vec2d_Make( -101.0, -103.0 );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &attrStore, f.policy, unused, nullptr ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t selected =
        BrushSideAttributes_MakeDefault();
    selected.uvProjection.worldUnitsPerUv =
        math::Vec2d_Make( 2.0, 4.0 );
    selected.uvProjection.offset = math::Vec2d_Make( 17.0, 23.0 );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &attrStore, f.policy, selected, nullptr ) ==
             geometry_status_t::OK );

    const cook_brush_attributes_t brushAttrs{ &attrStore };
    REQUIRE( f.CookFull( &brushAttrs, 1u ) ==
             geometry_cook_status_t::OK );

    for ( common::usize i = 0u;
          i < GeometryCookFull_VertexCount( &f.cookResult );
          ++i ) {
        const cook_vertex_t &vertex = f.cookResult.vertices.pData[i];
        math::vec2d_t expected{};
        REQUIRE( math::Uvd_TryProjectPlanarPoint(
            selected.uvProjection,
            vertex.position,
            f.policy.numerical.fAbsoluteDistanceTolerance,
            &expected ) );
        CHECK( vertex.uv.x == Catch::Approx( expected.x ).margin( 1.0e-12 ) );
        CHECK( vertex.uv.y == Catch::Approx( expected.y ).margin( 1.0e-12 ) );
    }

    BrushSideAttributeStore_Shutdown( &attrStore );
}

TEST_CASE( "FullCook: dangling side attribute index fails with empty output",
           "[Gate8][FullCook][Attributes][contract]" )
{
    FullCookFixture f;
    f.AddBox();

    brush_solid_t *pBrush = f.document.brushes.pData[0];
    REQUIRE( pBrush != nullptr );
    pBrush->sides.pData[0].iAttributeIndex = 99u;

    geometry_brush_side_attribute_store_t attrStore{};
    REQUIRE( BrushSideAttributeStore_Init( &attrStore, &f.allocator ) ==
             geometry_status_t::OK );
    const geometry_brush_side_attributes_t attrs =
        BrushSideAttributes_MakeDefault();
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &attrStore, f.policy, attrs, nullptr ) ==
             geometry_status_t::OK );

    const cook_brush_attributes_t brushAttrs{ &attrStore };
    REQUIRE( f.CookFull( &brushAttrs, 1u ) ==
             geometry_cook_status_t::ATTRIBUTE_FAILED );
    CHECK( GeometryCookFull_VertexCount( &f.cookResult ) == 0u );
    CHECK( GeometryCookFull_TriangleCount( &f.cookResult ) == 0u );
    CHECK_FALSE( common::ContentHash_IsValid(
        GeometryCookFull_ContentHash( &f.cookResult ) ) );
    CHECK( f.cookResult.status == geometry_cook_status_t::ATTRIBUTE_FAILED );

    BrushSideAttributeStore_Shutdown( &attrStore );
}

TEST_CASE( "FullCook: every allocation failure leaves empty output",
           "[Gate8][FullCook][allocation][contract]" )
{
    FullCookFixture f;
    f.AddBox();

    cook_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeCookFailureAllocator( &baselineState );
    geometry_cook_full_result_t baseline{};
    REQUIRE( GeometryCookFull_Init( &baseline, &baselineAllocator ) ==
             geometry_cook_status_t::OK );
    REQUIRE( GeometryCook_TryCookFull(
                 &baseline, &f.document, f.policy, nullptr, 0u ) ==
             geometry_cook_status_t::OK );
    const common::usize cAllocationCalls =
        baselineState.cAllocationCalls;
    REQUIRE( cAllocationCalls > 0u );
    GeometryCookFull_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFrees );

    for ( common::usize iFail = 1u;
          iFail <= cAllocationCalls;
          ++iFail ) {
        CAPTURE( iFail, cAllocationCalls );
        cook_failure_allocator_state_t state{};
        state.iFailOnCall = iFail;
        common::allocator_t allocator = MakeCookFailureAllocator( &state );
        geometry_cook_full_result_t result{};
        REQUIRE( GeometryCookFull_Init( &result, &allocator ) ==
                 geometry_cook_status_t::OK );

        REQUIRE( GeometryCook_TryCookFull(
                     &result, &f.document, f.policy, nullptr, 0u ) ==
                 geometry_cook_status_t::OUT_OF_MEMORY );
        CHECK( GeometryCookFull_VertexCount( &result ) == 0u );
        CHECK( GeometryCookFull_TriangleCount( &result ) == 0u );
        CHECK_FALSE( common::ContentHash_IsValid(
            GeometryCookFull_ContentHash( &result ) ) );
        CHECK( result.status == geometry_cook_status_t::OUT_OF_MEMORY );

        GeometryCookFull_Shutdown( &result );
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }
}

TEST_CASE( "FullCook: null arguments rejected", "[Gate8][FullCook]" )
{
    REQUIRE( GeometryCookFull_Init( nullptr, nullptr ) ==
             geometry_cook_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometryCook_TryCookFull(
                 nullptr, nullptr, {}, nullptr, 0u ) ==
             geometry_cook_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "FullCook: double init and invalid inputs preserve prior output",
           "[Gate8][FullCook][contract]" )
{
    FullCookFixture f;
    f.AddBox();
    REQUIRE( f.CookFull() == geometry_cook_status_t::OK );

    REQUIRE( GeometryCookFull_Init( &f.cookResult, &f.allocator ) ==
             geometry_cook_status_t::INVALID_ARGUMENT );

    geometry_policy_t invalidPolicy = f.policy;
    invalidPolicy.numerical.fAbsoluteDistanceTolerance =
        std::numeric_limits<common::f64>::infinity();
    REQUIRE( GeometryCook_TryCookFull(
                 &f.cookResult, &f.document, invalidPolicy,
                 nullptr, 0u ) ==
             geometry_cook_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometryCook_TryCookFull(
                 &f.cookResult, &f.document, f.policy,
                 nullptr, 1u ) ==
             geometry_cook_status_t::INVALID_ARGUMENT );

    CHECK( GeometryCookFull_VertexCount( &f.cookResult ) == 24u );
    CHECK( GeometryCookFull_TriangleCount( &f.cookResult ) == 12u );
}

TEST_CASE( "FullCook: empty document produces zero output",
           "[Gate8][FullCook]" )
{
    FullCookFixture f;
    REQUIRE( f.CookFull() == geometry_cook_status_t::OK );
    REQUIRE( GeometryCookFull_VertexCount( &f.cookResult ) == 0u );
    REQUIRE( GeometryCookFull_TriangleCount( &f.cookResult ) == 0u );
}

} // namespace cypher::editor::geometry
