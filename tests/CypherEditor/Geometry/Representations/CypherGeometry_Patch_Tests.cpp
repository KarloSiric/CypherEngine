//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Patch_Tests.cpp
//  Purpose: Contract tests for the Patch representation and its
//           tessellation.
//  Details: Oracles are closed-form where possible: a flat patch evaluates
//           to its bilinear map; a single raised centre control of height h
//           gives S(0.5, 0.5) = h/4 (B_1^2(0.5)^2 = 1/4). Refinement must
//           leave the surface unchanged under the known parameter remap.
//           The adaptive tessellator's a-priori bound is checked
//           empirically by sampling inside every output triangle.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Patch.h"
#include "CypherGeometry_PatchTessellation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

// Small deterministic generator so "random" patches are reproducible
// without depending on the test runner's seed.
struct Lcg {
    common::u64 state;
    double Next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>( state >> 11 ) * ( 1.0 / 9007199254740992.0 );
    }
    double Range( double a, double b ) { return a + ( b - a ) * Next(); }
};

struct Fixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 100u } };
    patch_surface_t patch{};
    ~Fixture() { Patch_Shutdown( &patch ); }

    void Flat( patch_basis_t basis, common::u32 cols, common::u32 rows,
               vec3d_t uAxis = Vec3d_Make( 4.0, 0.0, 0.0 ), vec3d_t vAxis = Vec3d_Make( 0.0, 2.0, 0.0 ) ) {
        REQUIRE( Patch_TryInitFlat( &patch, &allocator, basis, cols, rows, Vec3d_Make( 1.0, 1.0, 0.0 ), uAxis, vAxis,
                                    Id( 1 ), &ids ) == geometry_status_t::OK );
    }

    // Flat grid with every control displaced in z (and jittered in x/y) by a
    // seeded amount: a generic curved patch with no special structure.
    void Bumpy( patch_basis_t basis, common::u32 cols, common::u32 rows, common::u64 seed, double amp = 1.0 ) {
        Flat( basis, cols, rows, Vec3d_Make( 8.0, 0.0, 0.0 ), Vec3d_Make( 0.0, 8.0, 0.0 ) );
        Lcg rng{ seed };
        for ( common::u32 r = 0u; r < rows; ++r ) {
            for ( common::u32 c = 0u; c < cols; ++c ) {
                patch_control_t ctl = *Patch_Control( &patch, c, r );
                ctl.position.x += rng.Range( -0.3, 0.3 ) * amp;
                ctl.position.y += rng.Range( -0.3, 0.3 ) * amp;
                ctl.position.z += rng.Range( -2.0, 2.0 ) * amp;
                ctl.uv.x += rng.Range( -0.05, 0.05 );
                REQUIRE( Patch_TrySetControl( &patch, c, r, ctl ) == geometry_status_t::OK );
            }
        }
    }
};

bool Near( vec3d_t a, vec3d_t b, double eps ) {
    return std::fabs( a.x - b.x ) <= eps && std::fabs( a.y - b.y ) <= eps && std::fabs( a.z - b.z ) <= eps;
}

// Maps a sub-patch local parameter on the pre-refinement patch to the
// refined patch after splitting sub-patch `iSplit` at its midpoint.
std::pair<common::u32, double> RemapAfterSplit( common::u32 iSub, double s, common::u32 iSplit ) {
    if ( iSub < iSplit ) { return { iSub, s }; }
    if ( iSub > iSplit ) { return { iSub + 1u, s }; }
    if ( s < 0.5 ) { return { iSub, 2.0 * s }; }
    return { iSub + 1u, 2.0 * s - 1.0 };
}

struct Tess {
    patch_tessellation_t t{};
    Tess( const common::allocator_t *a ) { REQUIRE( PatchTessellation_Init( &t, a ) == geometry_status_t::OK ); }
    ~Tess() { PatchTessellation_Shutdown( &t ); }
};

struct FailureAllocatorState {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *FailureAllocate( void *pUserData, common::usize cbSize, common::usize nAlignment ) noexcept {
    auto *pState = static_cast<FailureAllocatorState *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) { return nullptr; }
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    void *pMemory = pSystem->pfnAllocate( pSystem->pUserData, cbSize, nAlignment );
    if ( pMemory != nullptr ) { ++pState->cSuccessfulAllocations; }
    return pMemory;
}

void FailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<FailureAllocatorState *>( pUserData );
    if ( pMemory != nullptr ) { ++pState->cFrees; }
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    pSystem->pfnFree( pSystem->pUserData, pMemory, cbSize, nAlignment );
}

common::allocator_t MakeFailureAllocator( FailureAllocatorState *pState ) noexcept {
    return common::allocator_t{ &FailureAllocate, nullptr, &FailureFree, pState };
}

bool PatchIsCanonicalEmpty( const patch_surface_t &patch ) noexcept {
    return patch.controls.pData == nullptr && patch.controls.nCount == 0u &&
           patch.controls.nCapacity == 0u && patch.controls.pAllocator == nullptr &&
           patch.cColumns == 0u && patch.cRows == 0u &&
           patch.basis == patch_basis_t::BIQUADRATIC_BEZIER &&
           patch.materialId == 0u && !GeometrySourceId_IsValid( patch.sourceId );
}

bool TessellationIsLogicallyEmpty( const patch_tessellation_t &t ) noexcept {
    return t.positions.nCount == 0u && t.normals.nCount == 0u &&
           t.uvs.nCount == 0u && t.params.nCount == 0u &&
           t.vertexControl.nCount == 0u && t.indices.nCount == 0u &&
           t.triangleSubPatch.nCount == 0u &&
           t.columnSubdivisions.nCount == 0u && t.rowSubdivisions.nCount == 0u &&
           t.cGridColumns == 0u && t.cGridRows == 0u &&
           t.cCollapsedTriangles == 0u && t.cNormalFallbacks == 0u &&
           t.cUnresolvedNormals == 0u && t.bToleranceMet;
}

std::vector<patch_control_t> CopyControls( const patch_surface_t &patch ) {
    return std::vector<patch_control_t>( patch.controls.pData,
                                         patch.controls.pData + patch.controls.nCount );
}

bool ControlsEqual( const patch_surface_t &patch, const std::vector<patch_control_t> &controls ) noexcept {
    return patch.controls.nCount == controls.size() &&
           ( controls.empty() ||
             std::memcmp( patch.controls.pData, controls.data(),
                          sizeof( patch_control_t ) * controls.size() ) == 0 );
}

vec3d_t TriangleNormal( const patch_tessellation_t &t, common::usize iTri ) {
    const vec3d_t a = t.positions.pData[t.indices.pData[iTri * 3]];
    const vec3d_t b = t.positions.pData[t.indices.pData[iTri * 3 + 1]];
    const vec3d_t c = t.positions.pData[t.indices.pData[iTri * 3 + 2]];
    return math::Vec3d_Cross( math::Vec3d_Subtract( b, a ), math::Vec3d_Subtract( c, a ) );
}

} // namespace

// ---------------------------------------------------------------------------
// Representation
// ---------------------------------------------------------------------------

TEST_CASE( "Patch axis counts follow the shared-boundary layout", "[geometry][patch]" ) {
    CHECK( Patch_IsValidAxisCount( patch_basis_t::BIQUADRATIC_BEZIER, 3 ) );
    CHECK( Patch_IsValidAxisCount( patch_basis_t::BIQUADRATIC_BEZIER, 5 ) );
    CHECK( Patch_IsValidAxisCount( patch_basis_t::BIQUADRATIC_BEZIER, 9 ) );
    CHECK_FALSE( Patch_IsValidAxisCount( patch_basis_t::BIQUADRATIC_BEZIER, 1 ) );
    CHECK_FALSE( Patch_IsValidAxisCount( patch_basis_t::BIQUADRATIC_BEZIER, 2 ) );
    CHECK_FALSE( Patch_IsValidAxisCount( patch_basis_t::BIQUADRATIC_BEZIER, 4 ) );
    CHECK( Patch_IsValidAxisCount( patch_basis_t::BICUBIC_BEZIER, 4 ) );
    CHECK( Patch_IsValidAxisCount( patch_basis_t::BICUBIC_BEZIER, 7 ) );
    CHECK_FALSE( Patch_IsValidAxisCount( patch_basis_t::BICUBIC_BEZIER, 5 ) );
    CHECK_FALSE( Patch_IsValidAxisCount( patch_basis_t::BIQUADRATIC_BEZIER, kPatchControlsPerAxisMax + 2u ) );
    CHECK_FALSE( Patch_IsValidAxisCount( static_cast<patch_basis_t>( 7 ), 3 ) );
    CHECK( Patch_Degree( static_cast<patch_basis_t>( 7u ) ) == 0u );
}

TEST_CASE( "Patch Init rejects bad arguments and double init", "[geometry][patch]" ) {
    Fixture f;
    CHECK( Patch_Init( nullptr, &f.allocator, patch_basis_t::BIQUADRATIC_BEZIER, 3, 3, Id( 1 ) ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( Patch_Init( &f.patch, &f.allocator, patch_basis_t::BIQUADRATIC_BEZIER, 3, 3, GEOMETRY_SOURCE_ID_INVALID ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( Patch_Init( &f.patch, &f.allocator, patch_basis_t::BIQUADRATIC_BEZIER, 4, 3, Id( 1 ) ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK_FALSE( Patch_IsInitialized( &f.patch ) );
    REQUIRE( Patch_Init( &f.patch, &f.allocator, patch_basis_t::BIQUADRATIC_BEZIER, 3, 5, Id( 1 ) ) ==
             geometry_status_t::OK );
    CHECK( Patch_Init( &f.patch, &f.allocator, patch_basis_t::BIQUADRATIC_BEZIER, 3, 3, Id( 1 ) ) ==
           geometry_status_t::ALREADY_INITIALIZED );
    CHECK( f.patch.controls.nCount == 15u );
    CHECK( Patch_SubPatchColumns( &f.patch ) == 1u );
    CHECK( Patch_SubPatchRows( &f.patch ) == 2u );
    // Fresh controls have no IDs yet, so the patch is not valid.
    CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::INVALID_SOURCE_ID );
}

TEST_CASE( "Patch IDs are unique across the object and controls", "[geometry][patch]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_source_id_allocator_t ids{ Id( 100u ) };
    patch_surface_t patch{};
    REQUIRE( Patch_TryInitFlat( &patch, &allocator, patch_basis_t::BIQUADRATIC_BEZIER,
                                3u, 3u, {}, Vec3d_Make( 2.0, 0.0, 0.0 ),
                                Vec3d_Make( 0.0, 2.0, 0.0 ), Id( 100u ), &ids ) ==
             geometry_status_t::OK );
    CHECK( patch.controls.pData[0].sourceId.value == 101u );
    CHECK( ids.next.value == 110u );
    CHECK( Patch_Validate( &patch, &allocator ).fault == patch_fault_t::NONE );

    patch_control_t control = patch.controls.pData[4];
    const patch_control_t before = control;
    control.sourceId = patch.sourceId;
    CHECK( Patch_TrySetControl( &patch, 1u, 1u, control ) == geometry_status_t::IDENTITY_CONFLICT );
    CHECK( std::memcmp( &patch.controls.pData[4], &before, sizeof( before ) ) == 0 );
    control.sourceId = patch.controls.pData[0].sourceId;
    CHECK( Patch_TrySetControl( &patch, 1u, 1u, control ) == geometry_status_t::IDENTITY_CONFLICT );
    CHECK( std::memcmp( &patch.controls.pData[4], &before, sizeof( before ) ) == 0 );

    patch.controls.pData[4].sourceId = patch.sourceId;
    const patch_validation_result_t result = Patch_Validate( &patch, &allocator );
    CHECK( result.fault == patch_fault_t::DUPLICATE_SOURCE_ID );
    CHECK( result.iControl == 4u );
    patch.controls.pData[4] = before;
    Patch_Shutdown( &patch );
}

TEST_CASE( "Patch flat initialization is atomic on allocation and ID exhaustion", "[geometry][patch]" ) {
    FailureAllocatorState baselineState{};
    common::allocator_t baselineAllocator = MakeFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds{ Id( 10u ) };
    patch_surface_t baseline{};
    REQUIRE( Patch_TryInitFlat( &baseline, &baselineAllocator, patch_basis_t::BIQUADRATIC_BEZIER,
                                3u, 3u, {}, Vec3d_Make( 1.0, 0.0, 0.0 ),
                                Vec3d_Make( 0.0, 1.0, 0.0 ), Id( 1u ), &baselineIds ) ==
             geometry_status_t::OK );
    const common::usize cAllocations = baselineState.cAllocationCalls;
    REQUIRE( cAllocations > 0u );
    Patch_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFrees );

    for ( common::usize iFail = 1u; iFail <= cAllocations; ++iFail ) {
        DYNAMIC_SECTION( "flat allocation " << iFail ) {
            FailureAllocatorState state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator = MakeFailureAllocator( &state );
            geometry_source_id_allocator_t ids{ Id( 10u ) };
            patch_surface_t output{};
            CHECK( Patch_TryInitFlat( &output, &allocator, patch_basis_t::BIQUADRATIC_BEZIER,
                                      3u, 3u, {}, Vec3d_Make( 1.0, 0.0, 0.0 ),
                                      Vec3d_Make( 0.0, 1.0, 0.0 ), Id( 1u ), &ids ) ==
                   geometry_status_t::ALLOCATION_FAILED );
            CHECK( PatchIsCanonicalEmpty( output ) );
            CHECK( ids.next.value == 10u );
            CHECK( state.cSuccessfulAllocations == state.cFrees );
        }
    }

    geometry_source_id_allocator_t exhausted{ Id( common::CY_U64_MAX ) };
    patch_surface_t output{};
    CHECK( Patch_TryInitFlat( &output, common::Allocator_GetSystem(),
                              patch_basis_t::BIQUADRATIC_BEZIER, 3u, 3u, {},
                              Vec3d_Make( 1.0, 0.0, 0.0 ), Vec3d_Make( 0.0, 1.0, 0.0 ),
                              Id( common::CY_U64_MAX ), &exhausted ) ==
           geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( PatchIsCanonicalEmpty( output ) );
    CHECK( exhausted.next.value == common::CY_U64_MAX );
}

TEST_CASE( "Flat patch evaluates to its bilinear map with a constant normal", "[geometry][patch]" ) {
    Fixture f;
    f.Flat( patch_basis_t::BIQUADRATIC_BEZIER, 5, 3 );
    CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::NONE );
    CHECK( f.ids.next.value == 115u ); // 15 controls allocated row-major
    CHECK( Patch_Control( &f.patch, 0, 0 )->sourceId.value == 100u );
    CHECK( Patch_Control( &f.patch, 4, 2 )->sourceId.value == 114u );

    for ( double u : { 0.0, 0.2, 0.5, 0.77, 1.0 } ) {
        for ( double v : { 0.0, 0.3, 0.5, 1.0 } ) {
            const patch_sample_t s = Patch_Evaluate( &f.patch, u, v );
            CHECK( Near( s.position, Vec3d_Make( 1.0 + 4.0 * u, 1.0 + 2.0 * v, 0.0 ), 1e-12 ) );
            CHECK( s.uv.x == Approx( u ).margin( 1e-12 ) );
            CHECK( s.uv.y == Approx( v ).margin( 1e-12 ) );
            REQUIRE( s.bNormalValid );
            CHECK_FALSE( s.bNormalFromNeighbour );
            CHECK( Near( s.normal, Vec3d_Make( 0.0, 0.0, 1.0 ), 1e-12 ) );
            // Global derivatives are the full axis vectors.
            CHECK( Near( s.dPdu, Vec3d_Make( 4.0, 0.0, 0.0 ), 1e-9 ) );
            CHECK( Near( s.dPdv, Vec3d_Make( 0.0, 2.0, 0.0 ), 1e-9 ) );
        }
    }
}

TEST_CASE( "Raised centre control gives the closed-form biquadratic height", "[geometry][patch]" ) {
    Fixture f;
    f.Flat( patch_basis_t::BIQUADRATIC_BEZIER, 3, 3 );
    patch_control_t c = *Patch_Control( &f.patch, 1, 1 );
    c.position.z = 8.0;
    REQUIRE( Patch_TrySetControl( &f.patch, 1, 1, c ) == geometry_status_t::OK );
    CHECK( Patch_Evaluate( &f.patch, 0.5, 0.5 ).position.z == Approx( 2.0 ) );
    // Corners interpolate the corner controls exactly.
    CHECK( Patch_Evaluate( &f.patch, 0.0, 0.0 ).position.z == 0.0 );
    CHECK( Patch_Evaluate( &f.patch, 1.0, 1.0 ).position.z == 0.0 );
    // Symmetric bump: gradient vanishes at the apex.
    const patch_sample_t apex = Patch_Evaluate( &f.patch, 0.5, 0.5 );
    CHECK( std::fabs( apex.dPdu.z ) < 1e-12 );
    CHECK( std::fabs( apex.dPdv.z ) < 1e-12 );
    // Control bounds contain the surface (convex hull property).
    const math::aabbd_t b = Patch_ControlBounds( &f.patch );
    CHECK( b.maximum.z == 8.0 );
    CHECK( b.minimum.z == 0.0 );
}

TEST_CASE( "Patch derivatives match finite differences", "[geometry][patch]" ) {
    for ( patch_basis_t basis : { patch_basis_t::BIQUADRATIC_BEZIER, patch_basis_t::BICUBIC_BEZIER } ) {
        Fixture f;
        const common::u32 n = Patch_Degree( basis );
        f.Bumpy( basis, 2 * n + 1, n + 1, 42u + n );
        Lcg rng{ 7u };
        for ( common::u32 k = 0u; k < 40u; ++k ) {
            // Keep away from sub-patch boundaries where one-sided
            // differences straddle two polynomial pieces.
            double u = rng.Range( 0.02, 0.98 ), v = rng.Range( 0.02, 0.98 );
            if ( std::fabs( u - 0.5 ) < 0.01 ) { u += 0.02; }
            const double h = 1e-6;
            const patch_sample_t s = Patch_Evaluate( &f.patch, u, v );
            const vec3d_t fu = math::Vec3d_Scale(
                math::Vec3d_Subtract( Patch_Evaluate( &f.patch, u + h, v ).position,
                                      Patch_Evaluate( &f.patch, u - h, v ).position ),
                0.5 / h );
            const vec3d_t fv = math::Vec3d_Scale(
                math::Vec3d_Subtract( Patch_Evaluate( &f.patch, u, v + h ).position,
                                      Patch_Evaluate( &f.patch, u, v - h ).position ),
                0.5 / h );
            CHECK( Near( s.dPdu, fu, 1e-5 ) );
            CHECK( Near( s.dPdv, fv, 1e-5 ) );
        }
    }
}

TEST_CASE( "Sub-patch boundaries agree and follow the higher-index convention", "[geometry][patch]" ) {
    Fixture f;
    f.Bumpy( patch_basis_t::BIQUADRATIC_BEZIER, 5, 5, 3u );
    for ( double t : { 0.0, 0.25, 0.6, 1.0 } ) {
        // u = 0.5 is the boundary between sub-columns 0 and 1.
        const vec3d_t left = Patch_EvaluateSubPatch( &f.patch, 0, 0, 1.0, t ).position;
        const vec3d_t right = Patch_EvaluateSubPatch( &f.patch, 1, 0, 0.0, t ).position;
        CHECK( Near( left, right, 1e-12 ) );
        const patch_sample_t g = Patch_Evaluate( &f.patch, 0.5, t * 0.5 );
        const patch_sample_t hi = Patch_EvaluateSubPatch( &f.patch, 1, 0, 0.0, t );
        CHECK( math::Vec3d_EqualsExact( g.position, hi.position ) );
        // Global derivative is the local one scaled by the sub-patch count.
        CHECK( Near( g.dPdu, math::Vec3d_Scale( hi.dPdu, 2.0 ), 1e-12 ) );
    }
    CHECK( Patch_SubPatchControlIndex( &f.patch, 1, 1, 0, 0 ) == 2u * 5u + 2u );
    CHECK( Patch_SubPatchControlIndex( &f.patch, 1, 1, 2, 2 ) == 4u * 5u + 4u );
    CHECK( Patch_SubPatchControlIndex( &f.patch, 2, 0, 0, 0 ) == CY_INVALID_INDEX );
    CHECK( Patch_SubPatchControlIndex( &f.patch, 0, 0, 3, 0 ) == CY_INVALID_INDEX );
    // Out-of-range parameters clamp, NaN maps to 0.
    CHECK( math::Vec3d_EqualsExact( Patch_Evaluate( &f.patch, -3.0, 0.0 ).position,
                                    Patch_Evaluate( &f.patch, 0.0, 0.0 ).position ) );
    CHECK( math::Vec3d_EqualsExact( Patch_Evaluate( &f.patch, std::nan( "" ), 2.0 ).position,
                                    Patch_Evaluate( &f.patch, 0.0, 1.0 ).position ) );
}

TEST_CASE( "InsertColumn and InsertRow preserve the surface and control identity", "[geometry][patch]" ) {
    for ( patch_basis_t basis : { patch_basis_t::BIQUADRATIC_BEZIER, patch_basis_t::BICUBIC_BEZIER } ) {
        for ( bool bColumns : { true, false } ) {
            Fixture f;
            const common::u32 n = Patch_Degree( basis );
            f.Bumpy( basis, 2 * n + 1, 2 * n + 1, 11u + n );
            patch_surface_t before{};
            REQUIRE( Patch_TryClone( &f.patch, &f.allocator, &before ) == geometry_status_t::OK );
            const common::u64 nextBefore = f.ids.next.value;

            const geometry_status_t st = bColumns ? Patch_TryInsertColumn( &f.patch, 1, &f.ids )
                                                  : Patch_TryInsertRow( &f.patch, 1, &f.ids );
            REQUIRE( st == geometry_status_t::OK );
            CHECK( f.patch.cColumns == before.cColumns + ( bColumns ? n : 0u ) );
            CHECK( f.patch.cRows == before.cRows + ( bColumns ? 0u : n ) );
            // n new IDs per crossing line.
            CHECK( f.ids.next.value == nextBefore + n * ( 2 * n + 1 ) );
            CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::NONE );

            // Every original ID survives.
            std::set<common::u64> after;
            for ( common::usize i = 0u; i < f.patch.controls.nCount; ++i ) {
                after.insert( f.patch.controls.pData[i].sourceId.value );
            }
            for ( common::usize i = 0u; i < before.controls.nCount; ++i ) {
                CHECK( after.count( before.controls.pData[i].sourceId.value ) == 1u );
            }

            Lcg rng{ 99u };
            for ( common::u32 k = 0u; k < 60u; ++k ) {
                const common::u32 iSubU = static_cast<common::u32>( rng.Next() * 2.0 );
                const common::u32 iSubV = static_cast<common::u32>( rng.Next() * 2.0 );
                const double s = rng.Next(), t = rng.Next();
                const patch_sample_t old = Patch_EvaluateSubPatch( &before, iSubU, iSubV, s, t );
                patch_sample_t now{};
                if ( bColumns ) {
                    const auto m = RemapAfterSplit( iSubU, s, 1u );
                    now = Patch_EvaluateSubPatch( &f.patch, m.first, iSubV, m.second, t );
                } else {
                    const auto m = RemapAfterSplit( iSubV, t, 1u );
                    now = Patch_EvaluateSubPatch( &f.patch, iSubU, m.first, s, m.second );
                }
                CHECK( Near( old.position, now.position, 1e-12 ) );
                CHECK( std::fabs( old.uv.x - now.uv.x ) < 1e-12 );
                CHECK( std::fabs( old.uv.y - now.uv.y ) < 1e-12 );
            }
            Patch_Shutdown( &before );
        }
    }
}

TEST_CASE( "Insert is failure-atomic at the axis limit and rejects bad sub-patches", "[geometry][patch]" ) {
    Fixture f;
    f.Flat( patch_basis_t::BIQUADRATIC_BEZIER, kPatchControlsPerAxisMax, 3 );
    const common::u64 next = f.ids.next.value;
    CHECK( Patch_TryInsertColumn( &f.patch, 0, &f.ids ) == geometry_status_t::LIMIT_EXCEEDED );
    CHECK( f.patch.cColumns == kPatchControlsPerAxisMax );
    CHECK( f.ids.next.value == next );
    CHECK( Patch_TryInsertRow( &f.patch, 1, &f.ids ) == geometry_status_t::INVALID_HANDLE );
    CHECK( Patch_TryInsertRow( &f.patch, 0, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Patch_TryInsertRow( &f.patch, 0, &f.ids ) == geometry_status_t::OK );
    CHECK( f.patch.cRows == 5u );
}

TEST_CASE( "Patch clone, refinement, and transpose are atomic on every allocation failure", "[geometry][patch]" ) {
    Fixture source;
    source.Bumpy( patch_basis_t::BIQUADRATIC_BEZIER, 5u, 5u, 93u );

    FailureAllocatorState cloneProbeState{};
    common::allocator_t cloneProbeAllocator = MakeFailureAllocator( &cloneProbeState );
    patch_surface_t cloneProbe{};
    REQUIRE( Patch_TryClone( &source.patch, &cloneProbeAllocator, &cloneProbe ) == geometry_status_t::OK );
    const common::usize cCloneAllocations = cloneProbeState.cAllocationCalls;
    Patch_Shutdown( &cloneProbe );
    REQUIRE( cloneProbeState.cSuccessfulAllocations == cloneProbeState.cFrees );
    for ( common::usize iFail = 1u; iFail <= cCloneAllocations; ++iFail ) {
        DYNAMIC_SECTION( "clone allocation " << iFail ) {
            FailureAllocatorState state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator = MakeFailureAllocator( &state );
            patch_surface_t output{};
            CHECK( Patch_TryClone( &source.patch, &allocator, &output ) ==
                   geometry_status_t::ALLOCATION_FAILED );
            CHECK( PatchIsCanonicalEmpty( output ) );
            CHECK( state.cSuccessfulAllocations == state.cFrees );
        }
    }

    FailureAllocatorState refineProbeState{};
    common::allocator_t refineProbeAllocator = MakeFailureAllocator( &refineProbeState );
    geometry_source_id_allocator_t refineProbeIds{ Id( 100u ) };
    patch_surface_t refineProbe{};
    REQUIRE( Patch_TryInitFlat( &refineProbe, &refineProbeAllocator,
                                patch_basis_t::BIQUADRATIC_BEZIER, 5u, 5u, {},
                                Vec3d_Make( 4.0, 0.0, 0.0 ), Vec3d_Make( 0.0, 4.0, 0.0 ),
                                Id( 1u ), &refineProbeIds ) == geometry_status_t::OK );
    const common::usize iRefineFirstAllocation = refineProbeState.cAllocationCalls + 1u;
    REQUIRE( Patch_TryInsertColumn( &refineProbe, 0u, &refineProbeIds ) == geometry_status_t::OK );
    const common::usize cRefineAllocations =
        refineProbeState.cAllocationCalls - iRefineFirstAllocation + 1u;
    REQUIRE( cRefineAllocations > 0u );
    Patch_Shutdown( &refineProbe );
    REQUIRE( refineProbeState.cSuccessfulAllocations == refineProbeState.cFrees );

    for ( common::usize iFailure = 0u; iFailure < cRefineAllocations; ++iFailure ) {
        DYNAMIC_SECTION( "refinement allocation " << iFailure ) {
            FailureAllocatorState state{};
            common::allocator_t allocator = MakeFailureAllocator( &state );
            geometry_source_id_allocator_t ids{ Id( 100u ) };
            patch_surface_t patch{};
            REQUIRE( Patch_TryInitFlat( &patch, &allocator, patch_basis_t::BIQUADRATIC_BEZIER,
                                        5u, 5u, {}, Vec3d_Make( 4.0, 0.0, 0.0 ),
                                        Vec3d_Make( 0.0, 4.0, 0.0 ), Id( 1u ), &ids ) ==
                     geometry_status_t::OK );
            const std::vector<patch_control_t> before = CopyControls( patch );
            const patch_control_t *pBefore = patch.controls.pData;
            const common::usize capacityBefore = patch.controls.nCapacity;
            const geometry_source_id_t nextBefore = ids.next;
            state.iFailOnCall = state.cAllocationCalls + iFailure + 1u;
            CHECK( Patch_TryInsertColumn( &patch, 0u, &ids ) == geometry_status_t::ALLOCATION_FAILED );
            CHECK( patch.controls.pData == pBefore );
            CHECK( patch.controls.nCapacity == capacityBefore );
            CHECK( patch.cColumns == 5u );
            CHECK( patch.cRows == 5u );
            CHECK( ControlsEqual( patch, before ) );
            CHECK( ids.next.value == nextBefore.value );
            CHECK( Patch_Validate( &patch, common::Allocator_GetSystem() ).fault == patch_fault_t::NONE );
            Patch_Shutdown( &patch );
            CHECK( state.cSuccessfulAllocations == state.cFrees );
        }
    }

    FailureAllocatorState transposeState{};
    common::allocator_t transposeAllocator = MakeFailureAllocator( &transposeState );
    geometry_source_id_allocator_t transposeIds{ Id( 100u ) };
    patch_surface_t transposePatch{};
    REQUIRE( Patch_TryInitFlat( &transposePatch, &transposeAllocator,
                                patch_basis_t::BIQUADRATIC_BEZIER, 5u, 3u, {},
                                Vec3d_Make( 4.0, 0.0, 0.0 ), Vec3d_Make( 0.0, 2.0, 0.0 ),
                                Id( 1u ), &transposeIds ) == geometry_status_t::OK );
    const std::vector<patch_control_t> transposeBefore = CopyControls( transposePatch );
    const patch_control_t *pTransposeBefore = transposePatch.controls.pData;
    transposeState.iFailOnCall = transposeState.cAllocationCalls + 1u;
    CHECK( Patch_TryTranspose( &transposePatch ) == geometry_status_t::ALLOCATION_FAILED );
    CHECK( transposePatch.controls.pData == pTransposeBefore );
    CHECK( transposePatch.cColumns == 5u );
    CHECK( transposePatch.cRows == 3u );
    CHECK( ControlsEqual( transposePatch, transposeBefore ) );
    Patch_Shutdown( &transposePatch );
    CHECK( transposeState.cSuccessfulAllocations == transposeState.cFrees );
}

TEST_CASE( "Patch refinement skips live identities and preserves state on exhaustion", "[geometry][patch]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_source_id_allocator_t ids{ Id( 100u ) };
    patch_surface_t patch{};
    REQUIRE( Patch_TryInitFlat( &patch, &allocator, patch_basis_t::BIQUADRATIC_BEZIER,
                                3u, 3u, {}, Vec3d_Make( 2.0, 0.0, 0.0 ),
                                Vec3d_Make( 0.0, 2.0, 0.0 ), Id( common::CY_U64_MAX ), &ids ) ==
             geometry_status_t::OK );
    const std::vector<patch_control_t> before = CopyControls( patch );
    ids.next = Id( common::CY_U64_MAX );
    CHECK( Patch_TryInsertColumn( &patch, 0u, &ids ) == geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( ids.next.value == common::CY_U64_MAX );
    CHECK( patch.cColumns == 3u );
    CHECK( ControlsEqual( patch, before ) );
    CHECK( Patch_Validate( &patch, &allocator ).fault == patch_fault_t::NONE );
    Patch_Shutdown( &patch );
}

TEST_CASE( "Transpose and ReverseColumns re-parameterize and flip the face", "[geometry][patch]" ) {
    Fixture f;
    f.Bumpy( patch_basis_t::BICUBIC_BEZIER, 7, 4, 5u );
    patch_surface_t original{};
    REQUIRE( Patch_TryClone( &f.patch, &f.allocator, &original ) == geometry_status_t::OK );

    REQUIRE( Patch_TryTranspose( &f.patch ) == geometry_status_t::OK );
    CHECK( f.patch.cColumns == 4u );
    CHECK( f.patch.cRows == 7u );
    for ( double u : { 0.1, 0.4, 0.9 } ) {
        for ( double v : { 0.2, 0.7 } ) {
            const patch_sample_t a = Patch_Evaluate( &original, u, v );
            const patch_sample_t b = Patch_Evaluate( &f.patch, v, u );
            CHECK( Near( a.position, b.position, 1e-12 ) );
            CHECK( Near( a.normal, math::Vec3d_Negate( b.normal ), 1e-9 ) );
        }
    }
    REQUIRE( Patch_TryTranspose( &f.patch ) == geometry_status_t::OK );
    CHECK( std::memcmp( f.patch.controls.pData, original.controls.pData,
                        sizeof( patch_control_t ) * original.controls.nCount ) == 0 );

    Patch_ReverseColumns( &f.patch );
    for ( double u : { 0.0, 0.3, 0.8 } ) {
        const patch_sample_t a = Patch_Evaluate( &original, u, 0.4 );
        const patch_sample_t b = Patch_Evaluate( &f.patch, 1.0 - u, 0.4 );
        CHECK( Near( a.position, b.position, 1e-12 ) );
        CHECK( Near( a.normal, math::Vec3d_Negate( b.normal ), 1e-9 ) );
    }
    Patch_Shutdown( &original );
}

TEST_CASE( "Patch transform moves controls and is atomic on range failure", "[geometry][patch]" ) {
    Fixture f;
    f.Flat( patch_basis_t::BIQUADRATIC_BEZIER, 3, 3 );
    math::affine3d_t move = math::CY_AFFINE3D_IDENTITY;
    math::Affine3d_SetComponent( &move, 0, 3, 10.0 );
    REQUIRE( Patch_TryTransform( &f.patch, move ) == geometry_status_t::OK );
    CHECK( Patch_Evaluate( &f.patch, 0.0, 0.0 ).position.x == Approx( 11.0 ) );
    // UVs are authored data and do not move with the geometry.
    CHECK( Patch_Evaluate( &f.patch, 0.0, 0.0 ).uv.x == 0.0 );

    math::affine3d_t huge = math::CY_AFFINE3D_IDENTITY;
    math::Affine3d_SetComponent( &huge, 0, 3, kPatchCoordinateMax );
    const vec3d_t before = Patch_Control( &f.patch, 2, 2 )->position;
    CHECK( Patch_TryTransform( &f.patch, huge ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( math::Vec3d_EqualsExact( Patch_Control( &f.patch, 2, 2 )->position, before ) );

    math::affine3d_t nonFinite = math::CY_AFFINE3D_IDENTITY;
    math::Affine3d_SetComponent( &nonFinite, 1u, 1u, std::numeric_limits<double>::quiet_NaN() );
    const std::vector<patch_control_t> controlsBefore = CopyControls( f.patch );
    CHECK( Patch_TryTransform( &f.patch, nonFinite ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( ControlsEqual( f.patch, controlsBefore ) );
}

TEST_CASE( "Patch validation reports faults without repairing", "[geometry][patch]" ) {
    Fixture f;
    f.Flat( patch_basis_t::BIQUADRATIC_BEZIER, 3, 3 );

    patch_control_t c = *Patch_Control( &f.patch, 2, 1 );
    c.sourceId = Patch_Control( &f.patch, 0, 0 )->sourceId;
    CHECK( Patch_TrySetControl( &f.patch, 2, 1, c ) == geometry_status_t::IDENTITY_CONFLICT );
    CHECK( Patch_Control( &f.patch, 2, 1 )->sourceId.value != c.sourceId.value );
    // Direct storage corruption remains diagnosable even though the setter
    // now prevents callers from creating it.
    f.patch.controls.pData[1u * 3u + 2u].sourceId = c.sourceId;
    patch_validation_result_t r = Patch_Validate( &f.patch, &f.allocator );
    CHECK( r.fault == patch_fault_t::DUPLICATE_SOURCE_ID );
    CHECK( r.iControl == 1u * 3u + 2u );

    c.sourceId = Id( 5000 );
    REQUIRE( Patch_TrySetControl( &f.patch, 2, 1, c ) == geometry_status_t::OK );
    CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::NONE );

    // Setters refuse bad data; direct writes are caught by Validate.
    c.position.x = std::nan( "" );
    CHECK( Patch_TrySetControl( &f.patch, 2, 1, c ) == geometry_status_t::NUMERIC_FAILURE );
    c.position.x = kPatchCoordinateMax * 2.0;
    CHECK( Patch_TrySetControl( &f.patch, 2, 1, c ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( Patch_TrySetControl( &f.patch, 3, 1, c ) == geometry_status_t::INVALID_HANDLE );
    f.patch.controls.pData[4].position.y = std::nan( "" );
    r = Patch_Validate( &f.patch, &f.allocator );
    CHECK( r.fault == patch_fault_t::NON_FINITE );
    CHECK( r.iControl == 4u );
    f.patch.controls.pData[4].position.y = -kPatchCoordinateMax * 4.0;
    CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::COORDINATE_RANGE );

    for ( common::usize i = 0u; i < f.patch.controls.nCount; ++i ) {
        f.patch.controls.pData[i].position = Vec3d_Make( 3.0, 3.0, 3.0 );
    }
    CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::COLLAPSED_SURFACE );

    f.patch.cColumns = 5u; // dimensions no longer match storage
    CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::INVALID_DIMENSIONS );
    f.patch.cColumns = 3u;
    CHECK( Patch_Validate( nullptr, &f.allocator ).fault == patch_fault_t::NOT_INITIALIZED );
}

TEST_CASE( "Patch rejects corrupt basis and dimension state without indexing storage", "[geometry][patch]" ) {
    Fixture f;
    f.Flat( patch_basis_t::BIQUADRATIC_BEZIER, 3u, 3u );
    const std::vector<patch_control_t> before = CopyControls( f.patch );

    f.patch.basis = static_cast<patch_basis_t>( 0xffu );
    CHECK( Patch_Degree( f.patch.basis ) == 0u );
    CHECK_FALSE( Patch_IsInitialized( &f.patch ) );
    CHECK( Patch_Control( &f.patch, 0u, 0u ) == nullptr );
    CHECK( Patch_SubPatchColumns( &f.patch ) == 0u );
    CHECK( Patch_SubPatchControlIndex( &f.patch, 0u, 0u, 0u, 0u ) == CY_INVALID_INDEX );
    CHECK( math::Vec3d_EqualsExact( Patch_Evaluate( &f.patch, 0.5, 0.5 ).position, vec3d_t{} ) );
    CHECK( Patch_TryInsertColumn( &f.patch, 0u, &f.ids ) == geometry_status_t::CORRUPT_STATE );
    CHECK( Patch_TryTranspose( &f.patch ) == geometry_status_t::CORRUPT_STATE );
    CHECK( Patch_TryTransform( &f.patch, math::CY_AFFINE3D_IDENTITY ) == geometry_status_t::CORRUPT_STATE );
    Patch_ReverseColumns( &f.patch );
    CHECK( ControlsEqual( f.patch, before ) );
    CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::INVALID_DIMENSIONS );
    f.patch.basis = patch_basis_t::BIQUADRATIC_BEZIER;

    f.patch.cRows = 5u;
    CHECK_FALSE( Patch_IsInitialized( &f.patch ) );
    CHECK( Patch_TrySetControl( &f.patch, 0u, 0u, before[0] ) == geometry_status_t::CORRUPT_STATE );
    CHECK( Patch_TryTranspose( &f.patch ) == geometry_status_t::CORRUPT_STATE );
    CHECK( ControlsEqual( f.patch, before ) );
    f.patch.cRows = 3u;
    CHECK( Patch_IsInitialized( &f.patch ) );
}

TEST_CASE( "Patch validation reports scratch allocation failure", "[geometry][patch]" ) {
    Fixture f;
    f.Flat( patch_basis_t::BIQUADRATIC_BEZIER, 3u, 3u );
    FailureAllocatorState state{};
    state.iFailOnCall = 1u;
    common::allocator_t allocator = MakeFailureAllocator( &state );
    CHECK( Patch_Validate( &f.patch, &allocator ).fault == patch_fault_t::VALIDATION_INCOMPLETE );
    CHECK( state.cSuccessfulAllocations == state.cFrees );
    CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::NONE );
}

TEST_CASE( "Collapsed edge (cone apex) resolves its normal from the interior", "[geometry][patch]" ) {
    Fixture f;
    // Quadratic cone-like patch: row 0 collapsed to the apex, rows 1..2 a
    // curved arc below it.
    REQUIRE( Patch_Init( &f.patch, &f.allocator, patch_basis_t::BIQUADRATIC_BEZIER, 3, 3, Id( 1 ) ) ==
             geometry_status_t::OK );
    const vec3d_t apex = Vec3d_Make( 0.0, 0.0, 4.0 );
    const vec3d_t rim[3] = { Vec3d_Make( 2.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 2.0, 0.0 ), Vec3d_Make( 0.0, 2.0, 0.0 ) };
    common::u64 id = 10;
    for ( common::u32 i = 0u; i < 3u; ++i ) {
        REQUIRE( Patch_TrySetControl( &f.patch, i, 0, patch_control_t{ apex, {}, Id( id++ ) } ) ==
                 geometry_status_t::OK );
        REQUIRE( Patch_TrySetControl( &f.patch, i, 1,
                                      patch_control_t{ math::Vec3d_Lerp( apex, rim[i], 0.5 ), {}, Id( id++ ) } ) ==
                 geometry_status_t::OK );
        REQUIRE( Patch_TrySetControl( &f.patch, i, 2, patch_control_t{ rim[i], {}, Id( id++ ) } ) ==
                 geometry_status_t::OK );
    }
    CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::NONE );
    const patch_sample_t atApex = Patch_Evaluate( &f.patch, 0.5, 0.0 );
    CHECK( math::Vec3d_EqualsExact( atApex.position, apex ) );
    // Regression: the partial along a collapsed row must be exactly zero.
    // Summed Bernstein-derivative weights left ~1e-15 noise here, which the
    // normal code then trusted as a real direction.
    for ( double u : { 0.0, 0.13, 0.5, 0.71, 1.0 } ) {
        CHECK( math::Vec3d_EqualsExact( Patch_Evaluate( &f.patch, u, 0.0 ).dPdu, Vec3d_Make( 0.0, 0.0, 0.0 ) ) );
    }
    REQUIRE( atApex.bNormalValid );
    CHECK( atApex.bNormalFromNeighbour );
    const patch_sample_t nearApex = Patch_Evaluate( &f.patch, 0.5, 0.01 );
    REQUIRE( nearApex.bNormalValid );
    CHECK_FALSE( nearApex.bNormalFromNeighbour );
    CHECK( math::Vec3d_Dot( atApex.normal, nearApex.normal ) > 0.99 );
}

// ---------------------------------------------------------------------------
// Tessellation
// ---------------------------------------------------------------------------

TEST_CASE( "Flat patch tessellates adaptively to one quad with corner provenance", "[geometry][patch]" ) {
    Fixture f;
    f.Flat( patch_basis_t::BIQUADRATIC_BEZIER, 3, 3 );
    Tess t( &f.allocator );
    REQUIRE( PatchTessellation_TryBuild( &f.patch, patch_tessellation_options_t{}, &t.t ) == geometry_status_t::OK );
    CHECK( t.t.cGridColumns == 2u );
    CHECK( t.t.cGridRows == 2u );
    CHECK( t.t.indices.nCount == 6u );
    CHECK( t.t.bToleranceMet );
    CHECK( t.t.vertexControl.pData[0] == 0u );
    CHECK( t.t.vertexControl.pData[1] == 2u );
    CHECK( t.t.vertexControl.pData[2] == 6u );
    CHECK( t.t.vertexControl.pData[3] == 8u );
    for ( common::usize i = 0u; i < 2u; ++i ) {
        CHECK( TriangleNormal( t.t, i ).z > 0.0 );
        CHECK( t.t.triangleSubPatch.pData[i] == 0u );
    }
}

TEST_CASE( "Fixed subdivision produces the exact grid, winding, and provenance", "[geometry][patch]" ) {
    Fixture f;
    f.Bumpy( patch_basis_t::BIQUADRATIC_BEZIER, 5, 3, 21u, 0.3 );
    Tess t( &f.allocator );
    patch_tessellation_options_t o{};
    o.fixedSubdivisions = 4u;
    REQUIRE( PatchTessellation_TryBuild( &f.patch, o, &t.t ) == geometry_status_t::OK );
    CHECK( t.t.cGridColumns == 9u );
    CHECK( t.t.cGridRows == 5u );
    CHECK( t.t.positions.nCount == 45u );
    CHECK( t.t.indices.nCount == 8u * 4u * 6u );
    for ( common::usize v = 0u; v < t.t.positions.nCount; ++v ) {
        const vec2d_t p = t.t.params.pData[v];
        const patch_sample_t s = Patch_Evaluate( &f.patch, p.x, p.y );
        CHECK( Near( s.position, t.t.positions.pData[v], 1e-12 ) );
        const common::u32 ic = t.t.vertexControl.pData[v];
        if ( ic != CY_INVALID_INDEX ) {
            CHECK( Near( f.patch.controls.pData[ic].position, t.t.positions.pData[v], 1e-12 ) );
        }
    }
    // Corner controls of both sub-patches (6 of them) are the only
    // provenance-linked vertices.
    common::usize cLinked = 0;
    for ( common::usize v = 0u; v < t.t.vertexControl.nCount; ++v ) {
        cLinked += t.t.vertexControl.pData[v] != CY_INVALID_INDEX ? 1u : 0u;
    }
    CHECK( cLinked == 6u );
    const common::usize cTris = t.t.indices.nCount / 3u;
    for ( common::usize i = 0u; i < cTris; ++i ) {
        const common::u32 i0 = t.t.indices.pData[i * 3];
        CHECK( math::Vec3d_Dot( TriangleNormal( t.t, i ), t.t.normals.pData[i0] ) > 0.0 );
        const common::u32 sub = t.t.triangleSubPatch.pData[i];
        CHECK( sub < 2u );
        // The triangle's parameters lie inside its sub-patch column.
        const double u = ( t.t.params.pData[i0].x + t.t.params.pData[t.t.indices.pData[i * 3 + 1]].x +
                           t.t.params.pData[t.t.indices.pData[i * 3 + 2]].x ) / 3.0;
        CHECK( ( sub == 0u ? u < 0.5 : u > 0.5 ) );
    }
}

TEST_CASE( "Adaptive tessellation honours its chord error bound", "[geometry][patch]" ) {
    for ( patch_basis_t basis : { patch_basis_t::BIQUADRATIC_BEZIER, patch_basis_t::BICUBIC_BEZIER } ) {
        for ( double tol : { 0.5, 0.05 } ) {
            Fixture f;
            const common::u32 n = Patch_Degree( basis );
            f.Bumpy( basis, 2 * n + 1, 3 * n + 1, 1234u + n );
            Tess t( &f.allocator );
            patch_tessellation_options_t o{};
            o.fMaxChordError = tol;
            REQUIRE( PatchTessellation_TryBuild( &f.patch, o, &t.t ) == geometry_status_t::OK );
            REQUIRE( t.t.bToleranceMet );
            // Each chosen level satisfies the bound for every sub-patch in its
            // column / row.
            for ( common::u32 b = 0u; b < 3u; ++b ) {
                for ( common::u32 a = 0u; a < 2u; ++a ) {
                    CHECK( PatchTessellation_ErrorBound( &f.patch, a, b, t.t.columnSubdivisions.pData[a],
                                                         t.t.rowSubdivisions.pData[b] ) <= tol );
                }
            }
            // Empirical check: sample inside every triangle.
            double worst = 0.0;
            const common::usize cTris = t.t.indices.nCount / 3u;
            for ( common::usize i = 0u; i < cTris; ++i ) {
                const common::u32 *tri = &t.t.indices.pData[i * 3];
                for ( const auto &l : { std::array<double, 3>{ 1.0 / 3, 1.0 / 3, 1.0 / 3 },
                                        std::array<double, 3>{ 0.5, 0.5, 0.0 }, std::array<double, 3>{ 0.0, 0.5, 0.5 },
                                        std::array<double, 3>{ 0.5, 0.0, 0.5 },
                                        std::array<double, 3>{ 0.6, 0.2, 0.2 } } ) {
                    vec2d_t p{};
                    vec3d_t lin{};
                    for ( common::usize k = 0u; k < 3u; ++k ) {
                        p = math::Vec2d_Add( p, math::Vec2d_Scale( t.t.params.pData[tri[k]], l[k] ) );
                        lin = math::Vec3d_Add( lin, math::Vec3d_Scale( t.t.positions.pData[tri[k]], l[k] ) );
                    }
                    const double d =
                        std::sqrt( math::Vec3d_DistanceSquared( Patch_Evaluate( &f.patch, p.x, p.y ).position, lin ) );
                    if ( d > worst ) { worst = d; }
                }
            }
            CHECK( worst <= tol * ( 1.0 + 1e-9 ) );
            // And the bound is not absurdly loose: the finer tolerance really
            // did require refinement.
            CHECK( t.t.positions.nCount > 4u );
        }
    }
}

TEST_CASE( "Patch tessellation is watertight across sub-patches with mixed levels", "[geometry][patch]" ) {
    Fixture f;
    f.Bumpy( patch_basis_t::BIQUADRATIC_BEZIER, 7, 5, 77u );
    // Flatten the first sub-column so its own level would be low: the shared
    // column level must still produce a conforming grid.
    for ( common::u32 r = 0u; r < 5u; ++r ) {
        for ( common::u32 c = 0u; c < 3u; ++c ) {
            patch_control_t ctl = *Patch_Control( &f.patch, c, r );
            ctl.position.z = 0.0;
            REQUIRE( Patch_TrySetControl( &f.patch, c, r, ctl ) == geometry_status_t::OK );
        }
    }
    Tess t( &f.allocator );
    patch_tessellation_options_t o{};
    o.fMaxChordError = 0.02;
    REQUIRE( PatchTessellation_TryBuild( &f.patch, o, &t.t ) == geometry_status_t::OK );
    std::map<std::pair<common::u32, common::u32>, common::u32> edges;
    const common::usize cTris = t.t.indices.nCount / 3u;
    for ( common::usize i = 0u; i < cTris; ++i ) {
        for ( common::usize k = 0u; k < 3u; ++k ) {
            const common::u32 a = t.t.indices.pData[i * 3u + k];
            const common::u32 b = t.t.indices.pData[i * 3u + ( k + 1u ) % 3u];
            ++edges[{ std::min( a, b ), std::max( a, b ) }];
        }
    }
    common::usize cBoundary = 0;
    for ( const auto &e : edges ) {
        REQUIRE( e.second <= 2 );
        cBoundary += e.second == 1 ? 1u : 0u;
    }
    CHECK( cBoundary == 2u * ( t.t.cGridColumns - 1u ) + 2u * ( t.t.cGridRows - 1u ) );
    CHECK( t.t.cCollapsedTriangles == 0u );
}

TEST_CASE( "Collapsed apex triangles are dropped or kept by option", "[geometry][patch]" ) {
    Fixture f;
    REQUIRE( Patch_Init( &f.patch, &f.allocator, patch_basis_t::BIQUADRATIC_BEZIER, 3, 3, Id( 1 ) ) ==
             geometry_status_t::OK );
    common::u64 id = 10;
    for ( common::u32 i = 0u; i < 3u; ++i ) {
        const double x = static_cast<double>( i ) - 1.0;
        REQUIRE( Patch_TrySetControl( &f.patch, i, 0, patch_control_t{ Vec3d_Make( 0, 0, 3 ), {}, Id( id++ ) } ) ==
                 geometry_status_t::OK );
        REQUIRE( Patch_TrySetControl( &f.patch, i, 1, patch_control_t{ Vec3d_Make( x, 1, 1.5 ), {}, Id( id++ ) } ) ==
                 geometry_status_t::OK );
        REQUIRE( Patch_TrySetControl( &f.patch, i, 2, patch_control_t{ Vec3d_Make( 2 * x, 2, 0 ), {}, Id( id++ ) } ) ==
                 geometry_status_t::OK );
    }
    patch_tessellation_options_t o{};
    o.fixedSubdivisions = 4u;
    Tess dropped( &f.allocator ), kept( &f.allocator );
    REQUIRE( PatchTessellation_TryBuild( &f.patch, o, &dropped.t ) == geometry_status_t::OK );
    o.bDropCollapsedTriangles = false;
    REQUIRE( PatchTessellation_TryBuild( &f.patch, o, &kept.t ) == geometry_status_t::OK );
    CHECK( dropped.t.cCollapsedTriangles == 4u ); // one per apex cell
    CHECK( kept.t.cCollapsedTriangles == 4u );
    CHECK( kept.t.indices.nCount == 16u * 2u * 3u );
    CHECK( dropped.t.indices.nCount == kept.t.indices.nCount - 4u * 3u );
    CHECK( dropped.t.cNormalFallbacks == 5u ); // the five apex grid vertices
    CHECK( dropped.t.cUnresolvedNormals == 0u );
    const common::usize cTris = dropped.t.indices.nCount / 3u;
    for ( common::usize i = 0u; i < cTris; ++i ) {
        CHECK( math::Vec3d_LengthSquared( TriangleNormal( dropped.t, i ) ) > 0.0 );
    }
}

TEST_CASE( "Patch tessellation is deterministic and validates its inputs", "[geometry][patch]" ) {
    Fixture f;
    f.Bumpy( patch_basis_t::BICUBIC_BEZIER, 7, 7, 8u );
    Tess a( &f.allocator ), b( &f.allocator );
    patch_tessellation_options_t o{};
    o.fMaxChordError = 0.1;
    REQUIRE( PatchTessellation_TryBuild( &f.patch, o, &a.t ) == geometry_status_t::OK );
    REQUIRE( PatchTessellation_TryBuild( &f.patch, o, &b.t ) == geometry_status_t::OK );
    REQUIRE( a.t.positions.nCount == b.t.positions.nCount );
    REQUIRE( a.t.indices.nCount == b.t.indices.nCount );
    CHECK( std::memcmp( a.t.positions.pData, b.t.positions.pData, sizeof( vec3d_t ) * a.t.positions.nCount ) == 0 );
    CHECK( std::memcmp( a.t.indices.pData, b.t.indices.pData, sizeof( common::u32 ) * a.t.indices.nCount ) == 0 );

    o.fMaxChordError = 0.0;
    CHECK( PatchTessellation_TryBuild( &f.patch, o, &a.t ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( a.t.positions.nCount == 0u ); // failure leaves the output empty
    o.fMaxChordError = 0.1;
    o.maxSubdivisions = kPatchTessellationSubdivisionsMax + 1u;
    CHECK( PatchTessellation_TryBuild( &f.patch, o, &a.t ) == geometry_status_t::INVALID_ARGUMENT );
    o.maxSubdivisions = 1u;
    REQUIRE( PatchTessellation_TryBuild( &f.patch, o, &a.t ) == geometry_status_t::OK );
    CHECK_FALSE( a.t.bToleranceMet );
    o.fixedSubdivisions = kPatchTessellationSubdivisionsMax + 1u;
    CHECK( PatchTessellation_TryBuild( &f.patch, o, &a.t ) == geometry_status_t::LIMIT_EXCEEDED );

    patch_tessellation_t never{};
    CHECK( PatchTessellation_TryBuild( &f.patch, o, &never ) == geometry_status_t::NOT_INITIALIZED );
    CHECK( PatchTessellation_ErrorBound( &f.patch, 5, 0, 1, 1 ) == std::numeric_limits<double>::infinity() );
    CHECK( PatchTessellation_ErrorBound( &f.patch, 0, 0, 2, 2 ) < PatchTessellation_ErrorBound( &f.patch, 0, 0, 1, 1 ) );
}

TEST_CASE( "Patch tessellation lifecycle rejects partial state", "[geometry][patch]" ) {
    patch_tessellation_t tessellation{};
    CHECK( PatchTessellation_Init( &tessellation, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    CHECK( PatchTessellation_Init( &tessellation, common::Allocator_GetSystem() ) ==
           geometry_status_t::ALREADY_INITIALIZED );
    PatchTessellation_Shutdown( &tessellation );
    CHECK( TessellationIsLogicallyEmpty( tessellation ) );
    CHECK( tessellation.positions.pAllocator == nullptr );

    tessellation.cGridColumns = 1u;
    CHECK( PatchTessellation_Init( &tessellation, common::Allocator_GetSystem() ) ==
           geometry_status_t::CORRUPT_STATE );
    tessellation.cGridColumns = 0u;
    CHECK( PatchTessellation_Init( &tessellation, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    PatchTessellation_Shutdown( &tessellation );
}

TEST_CASE( "Patch tessellation is empty on every allocation failure", "[geometry][patch]" ) {
    Fixture f;
    f.Bumpy( patch_basis_t::BICUBIC_BEZIER, 7u, 7u, 123u );
    const std::vector<patch_control_t> patchBefore = CopyControls( f.patch );
    patch_tessellation_options_t options{};
    options.fixedSubdivisions = 3u;

    FailureAllocatorState probeState{};
    common::allocator_t probeAllocator = MakeFailureAllocator( &probeState );
    patch_tessellation_t probe{};
    REQUIRE( PatchTessellation_Init( &probe, &probeAllocator ) == geometry_status_t::OK );
    REQUIRE( PatchTessellation_TryBuild( &f.patch, options, &probe ) == geometry_status_t::OK );
    const common::usize cAllocations = probeState.cAllocationCalls;
    REQUIRE( cAllocations > 0u );
    PatchTessellation_Shutdown( &probe );
    REQUIRE( probeState.cSuccessfulAllocations == probeState.cFrees );

    for ( common::usize iFail = 1u; iFail <= cAllocations; ++iFail ) {
        DYNAMIC_SECTION( "tessellation allocation " << iFail ) {
            FailureAllocatorState state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator = MakeFailureAllocator( &state );
            patch_tessellation_t output{};
            REQUIRE( PatchTessellation_Init( &output, &allocator ) == geometry_status_t::OK );
            CHECK( PatchTessellation_TryBuild( &f.patch, options, &output ) ==
                   geometry_status_t::ALLOCATION_FAILED );
            CHECK( TessellationIsLogicallyEmpty( output ) );
            CHECK( ControlsEqual( f.patch, patchBefore ) );
            CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::NONE );
            PatchTessellation_Shutdown( &output );
            CHECK( state.cSuccessfulAllocations == state.cFrees );
        }
    }
}

TEST_CASE( "Patch tessellation rejects corrupt and excessive inputs with neutral output", "[geometry][patch]" ) {
    Fixture f;
    f.Flat( patch_basis_t::BIQUADRATIC_BEZIER, 9u, 9u );
    Tess tessellation( &f.allocator );
    patch_tessellation_options_t options{};
    options.fixedSubdivisions = kPatchTessellationSubdivisionsMax;
    CHECK( PatchTessellation_TryBuild( &f.patch, options, &tessellation.t ) ==
           geometry_status_t::LIMIT_EXCEEDED );
    CHECK( TessellationIsLogicallyEmpty( tessellation.t ) );

    const patch_basis_t basis = f.patch.basis;
    f.patch.basis = static_cast<patch_basis_t>( 9u );
    CHECK( PatchTessellation_TryBuild( &f.patch, patch_tessellation_options_t{}, &tessellation.t ) ==
           geometry_status_t::CORRUPT_STATE );
    CHECK( TessellationIsLogicallyEmpty( tessellation.t ) );
    CHECK( PatchTessellation_ErrorBound( &f.patch, 0u, 0u, 1u, 1u ) ==
           std::numeric_limits<double>::infinity() );
    f.patch.basis = basis;

    const double x = f.patch.controls.pData[0].position.x;
    f.patch.controls.pData[0].position.x = std::numeric_limits<double>::infinity();
    CHECK( PatchTessellation_TryBuild( &f.patch, patch_tessellation_options_t{}, &tessellation.t ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( TessellationIsLogicallyEmpty( tessellation.t ) );
    CHECK( PatchTessellation_ErrorBound( &f.patch, 0u, 0u, 1u, 1u ) ==
           std::numeric_limits<double>::infinity() );
    f.patch.controls.pData[0].position.x = x;

    CHECK( PatchTessellation_ErrorBound( &f.patch, 0u, 0u,
                                         kPatchTessellationSubdivisionsMax + 1u, 1u ) ==
           std::numeric_limits<double>::infinity() );
}

} // namespace cypher::editor::geometry
