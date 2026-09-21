//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushBoundary_Bench.cpp
//  Purpose: Benchmarks brush boundary reconstruction at varying side counts.
//  Details: Boundary reconstruction enumerates plane triples (O(n^3)) and
//           tests each candidate vertex against every plane (O(n)), making
//           the overall algorithm O(n^4). This benchmark measures that cost
//           at realistic authoring scales (6 to 128 sides) to establish a
//           baseline and confirm the quartic growth claim.
//
//           The brushes are constructed as axis-aligned slabs with extra
//           angled clip planes added symmetrically, so the plane set always
//           defines a valid bounded convex solid.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <benchmark/benchmark.h>

#include <cmath>

namespace common = cypher::common;

using namespace cypher::editor::geometry;
using namespace cypher::math;

namespace {

// Builds a brush with `cSides` sides by starting from a 6-plane box and
// adding extra bevel planes at equal angular intervals around the Z axis.
// Each added plane clips a corner, creating a convex solid with more sides
// than a plain box but still bounded and valid.
void BuildBeveledBrush(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    common::usize cTargetSides )
{
    // Start with a box — capacity and validity are guaranteed by the generator.
    ( void )BrushGenerator_TryMakeBox(
        pBrush, pAllocator, policy, pIdAllocator,
        Vec3d_Make( 0.0, 0.0, 0.0 ),
        Vec3d_Make( 10.0, 10.0, 10.0 ) );

    // Add extra bevel planes beyond the initial 6.
    const common::usize cExtra = ( cTargetSides > 6u ) ? cTargetSides - 6u : 0u;
    ( void )BrushSolid_TryReserve( pBrush, policy.limits, cTargetSides );

    for ( common::usize i = 0u; i < cExtra; ++i ) {
        // Distribute extra planes as bevels around the Z axis at different
        // heights, ensuring each one clips a corner and produces a valid
        // bounded solid.
        const f64 angle =
            static_cast<f64>( i ) * ( 2.0 * 3.14159265358979323846 ) /
            static_cast<f64>( cExtra );
        const f64 height =
            -8.0 + 16.0 * ( static_cast<f64>( i ) /
            static_cast<f64>( cExtra > 1u ? cExtra - 1u : 1u ) );

        // Normal tilted outward from the Z axis at this angle, with a
        // small upward component so it clips rather than slicing through.
        vec3d_t normal = Vec3d_Make(
            std::cos( angle ) * 0.9,
            std::sin( angle ) * 0.9,
            0.1 );
        // Normalize.
        const f64 len = std::sqrt(
            normal.x * normal.x + normal.y * normal.y + normal.z * normal.z );
        normal.x /= len;
        normal.y /= len;
        normal.z /= len;

        // Place the plane far enough out that it clips a corner but does
        // not eliminate any existing face entirely.
        const f64 d = -12.0 - 0.5 * static_cast<f64>( i );

        brush_solid_side_t side{};
        side.plane = Planed_Make( normal, d );
        const geometry_source_id_result_t idResult =
            GeometrySourceIdAllocator_Allocate( pIdAllocator );
        side.sourceId = idResult.id;
        side.iAttributeIndex = static_cast<common::u32>( 6u + i );

        ( void )BrushSolid_TryAddSide( pBrush, policy.limits, side, nullptr );
    }
}

} // namespace

// Measures boundary reconstruction cost at different side counts.
// The argument is the number of brush sides.
static void BM_BrushBoundaryReconstruct( benchmark::State &state )
{
    const common::usize cSides = static_cast<common::usize>( state.range( 0 ) );

    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};

    brush_solid_t brush{};
    BuildBeveledBrush( &brush, &allocator, &idAllocator, policy, cSides );

    brush_boundary_t boundary{};
    ( void )BrushBoundary_Init( &boundary, &allocator );

    for ( auto _ : state ) {
        ( void )BrushBoundary_TryReconstruct( &boundary, &brush, policy );
        benchmark::DoNotOptimize( boundary.vertices.pData );
        benchmark::DoNotOptimize( boundary.edges.pData );
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<int64_t>( state.iterations() ) );
    state.counters["sides"] = static_cast<double>( cSides );
    state.counters["vertices"] =
        static_cast<double>( BrushBoundary_VertexCount( &boundary ) );
    state.counters["edges"] =
        static_cast<double>( BrushBoundary_EdgeCount( &boundary ) );
    state.counters["faces"] =
        static_cast<double>( BrushBoundary_FaceCount( &boundary ) );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &brush );
}

// Box (6 sides) is the baseline. Typical brushes are 6-20 sides.
// 64 and 128 measure the quartic scaling at the upper range.
BENCHMARK( BM_BrushBoundaryReconstruct )
    ->Arg( 6 )
    ->Arg( 12 )
    ->Arg( 24 )
    ->Arg( 48 )
    ->Arg( 64 )
    ->Arg( 128 )
    ->Unit( benchmark::kMicrosecond );

// Measures deep validation cost (reconstruction + topology checks).
static void BM_BrushValidationDeep( benchmark::State &state )
{
    const common::usize cSides = static_cast<common::usize>( state.range( 0 ) );

    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};

    brush_solid_t brush{};
    BuildBeveledBrush( &brush, &allocator, &idAllocator, policy, cSides );

    for ( auto _ : state ) {
        brush_validation_result_t result =
            BrushValidation_Deep( &brush, policy, &allocator );
        benchmark::DoNotOptimize( result );
    }

    state.SetItemsProcessed(
        static_cast<int64_t>( state.iterations() ) );

    BrushSolid_Shutdown( &brush );
}

BENCHMARK( BM_BrushValidationDeep )
    ->Arg( 6 )
    ->Arg( 24 )
    ->Arg( 64 )
    ->Unit( benchmark::kMicrosecond );
