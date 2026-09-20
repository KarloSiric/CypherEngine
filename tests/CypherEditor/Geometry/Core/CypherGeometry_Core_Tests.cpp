//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Core_Tests.cpp
//  Purpose: Verifies geometry source-ID allocation and policy contracts.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_Policy.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace cypher::editor::geometry
{

TEST_CASE( "geometry source IDs allocate monotonically and never reuse zero",
           "[editor][geometry][core]" )
{
    geometry_source_id_allocator_t allocator{};

    const geometry_source_id_result_t first =
        GeometrySourceIdAllocator_Allocate( &allocator );
    const geometry_source_id_result_t second =
        GeometrySourceIdAllocator_Allocate( &allocator );

    REQUIRE( first.status == geometry_status_t::OK );
    REQUIRE( first.id.value == 1u );
    REQUIRE( second.status == geometry_status_t::OK );
    REQUIRE( second.id.value == 2u );
    REQUIRE( allocator.next.value == 3u );
}

TEST_CASE( "geometry source ID loading advances but never rewinds identity",
           "[editor][geometry][core]" )
{
    geometry_source_id_allocator_t allocator{};

    REQUIRE( GeometrySourceIdAllocator_AdvancePast(
                 &allocator,
                 geometry_source_id_t{ 41u } ) == geometry_status_t::OK );
    REQUIRE( allocator.next.value == 42u );

    REQUIRE( GeometrySourceIdAllocator_AdvancePast(
                 &allocator,
                 geometry_source_id_t{ 7u } ) == geometry_status_t::OK );
    REQUIRE( allocator.next.value == 42u );

    const geometry_source_id_result_t allocated =
        GeometrySourceIdAllocator_Allocate( &allocator );
    REQUIRE( allocated.status == geometry_status_t::OK );
    REQUIRE( allocated.id.value == 42u );
}

TEST_CASE( "geometry source ID allocator reports exhaustion after maximum ID",
           "[editor][geometry][core]" )
{
    geometry_source_id_allocator_t allocator{};
    REQUIRE( GeometrySourceIdAllocator_Reset(
                 &allocator,
                 geometry_source_id_t{ common::CY_U64_MAX } ) ==
             geometry_status_t::OK );

    const geometry_source_id_result_t last =
        GeometrySourceIdAllocator_Allocate( &allocator );
    REQUIRE( last.status == geometry_status_t::OK );
    REQUIRE( last.id.value == common::CY_U64_MAX );
    REQUIRE( GeometrySourceIdAllocator_IsExhausted( &allocator ) );

    const geometry_source_id_result_t overflow =
        GeometrySourceIdAllocator_Allocate( &allocator );
    REQUIRE( overflow.status == geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE_FALSE( GeometrySourceId_IsValid( overflow.id ) );
}

TEST_CASE( "default geometry policy is coherent and explicitly bounded",
           "[editor][geometry][core]" )
{
    const geometry_policy_t policy{};
    REQUIRE( GeometryNumericalPolicy_IsValid( policy.numerical ) );
    REQUIRE( GeometryLimitPolicy_IsValid( policy.limits ) );
    REQUIRE( GeometryPolicy_IsValid( policy ) );
}

TEST_CASE( "geometry policy rejects incompatible and non-finite tolerances",
           "[editor][geometry][core]" )
{
    geometry_policy_t policy{};

    policy.numerical.fWeldDistance =
        policy.numerical.fSnapDistance * 2.0;
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );

    policy = {};
    policy.numerical.fPlanarityTolerance =
        std::numeric_limits<common::f64>::infinity();
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );

    policy = {};
    policy.numerical.fCoordinateMagnitudeLimit = 1.0e10;
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );

    policy = {};
    policy.numerical.fUnitNormalTolerance = 1.5;
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );

    policy = {};
    policy.numerical.fUnitNormalTolerance =
        policy.numerical.fAbsoluteDistanceTolerance * 0.5;
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );

    policy = {};
    policy.limits.cHalfEdgesMax = policy.limits.cEdgesMax - 1u;
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );

    policy = {};
    policy.limits.cBrushSidesMax = policy.limits.cBrushesMax * 4u - 1u;
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );

    policy = {};
    policy.limits.cBrushSidesPerBrushMax =
        policy.limits.cBrushSidesMax + 1u;
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );

    policy = {};
    policy.limits.cTraversalDepthMax = 0u;
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );

    policy = {};
    policy.limits.cHalfEdgesMax = policy.limits.cEdgesMax * 2u - 1u;
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );

    policy = {};
    policy.limits.cVerticesMax =
        static_cast<common::u64>( common::CY_INVALID_INDEX ) + 1u;
    REQUIRE_FALSE( GeometryPolicy_IsValid( policy ) );
}

TEST_CASE( "geometry source ID allocator rejects invalid API inputs",
           "[editor][geometry][core]" )
{
    geometry_source_id_allocator_t allocator{};

    REQUIRE( GeometrySourceIdAllocator_Reset( nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometrySourceIdAllocator_Reset(
                 &allocator,
                 GEOMETRY_SOURCE_ID_INVALID ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometrySourceIdAllocator_AdvancePast(
                 &allocator,
                 GEOMETRY_SOURCE_ID_INVALID ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometrySourceIdAllocator_Allocate( nullptr ).status ==
             geometry_status_t::INVALID_ARGUMENT );
}

} // namespace cypher::editor::geometry
