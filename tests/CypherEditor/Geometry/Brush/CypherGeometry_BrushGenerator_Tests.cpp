//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushGenerator_Tests.cpp
//  Purpose: Verifies architectural primitive generators added in Gate 6.
//  Details: Covers wedge (5-plane triangular prism), prism (N+2 planes),
//           staircase (multiple boxes), and arch (arc segments). Each
//           generated brush must produce a valid boundary with correct
//           topology (Euler v - e + f = 2).
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushQueries.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using Catch::Approx;

namespace {

struct GeneratorFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_boundary_t boundary{};

    GeneratorFixture()
    {
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
    }

    ~GeneratorFixture()
    {
        BrushBoundary_Shutdown( &boundary );
    }

    void validateBrush( brush_solid_t *pBrush )
    {
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, pBrush, policy ) ==
                 geometry_status_t::OK );
        // Euler relation for a closed convex polyhedron: v - e + f = 2.
        const auto v = static_cast<common::i32>( boundary.vertices.nCount );
        const auto e = static_cast<common::i32>( boundary.edges.nCount );
        const auto f = static_cast<common::i32>( boundary.faces.nCount );
        REQUIRE( ( v - e + f ) == 2 );
    }
};

struct generator_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *GeneratorFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<generator_failure_allocator_state_t *>(
        pUserData );
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

void GeneratorFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<generator_failure_allocator_state_t *>(
        pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFrees;
    }
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    pSystem->pfnFree(
        pSystem->pUserData, pMemory, cbSize, nAlignment );
}

common::allocator_t MakeGeneratorFailureAllocator(
    generator_failure_allocator_state_t *pState ) noexcept
{
    return {
        &GeneratorFailureAllocate,
        nullptr,
        &GeneratorFailureFree,
        pState
    };
}

bool BrushIsCanonicalEmpty( const brush_solid_t &brush ) noexcept
{
    return brush.sides.pData == nullptr &&
           brush.sides.nCount == 0u &&
           brush.sides.nCapacity == 0u &&
           brush.sides.pAllocator == nullptr &&
           !GeometrySourceId_IsValid( brush.sourceId );
}

template <typename value_t>
std::array<unsigned char, sizeof( value_t )> ObjectBytes(
    const value_t &value ) noexcept
{
    std::array<unsigned char, sizeof( value_t )> bytes{};
    std::memcpy( bytes.data(), &value, sizeof( value ) );
    return bytes;
}

template <typename make_fn_t>
void VerifySingleGeneratorAllocationSweep( make_fn_t make )
{
    generator_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeGeneratorFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds{};
    brush_solid_t baseline{};
    REQUIRE( make( &baseline, &baselineAllocator, &baselineIds ) ==
             geometry_status_t::OK );
    const common::usize cAllocationCalls = baselineState.cAllocationCalls;
    REQUIRE( cAllocationCalls > 0u );
    BrushSolid_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFrees );

    for ( common::usize iFail = 1u;
          iFail <= cAllocationCalls;
          ++iFail ) {
        DYNAMIC_SECTION( "allocation call " << iFail ) {
            generator_failure_allocator_state_t state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator =
                MakeGeneratorFailureAllocator( &state );
            geometry_source_id_allocator_t ids{};
            const geometry_source_id_t firstId = ids.next;
            brush_solid_t output{};
            const auto outputBytes = ObjectBytes( output );

            REQUIRE( make( &output, &allocator, &ids ) ==
                     geometry_status_t::ALLOCATION_FAILED );
            CHECK( BrushIsCanonicalEmpty( output ) );
            CHECK( ObjectBytes( output ) == outputBytes );
            CHECK( ids.next.value == firstId.value );
            CHECK( state.cSuccessfulAllocations == state.cFrees );
        }
    }
}

template <common::usize nCount, typename make_fn_t>
void VerifyMultiGeneratorAllocationSweep( make_fn_t make )
{
    generator_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeGeneratorFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds{};
    brush_solid_t baseline[nCount]{};
    REQUIRE( make( baseline, &baselineAllocator, &baselineIds ) ==
             geometry_status_t::OK );
    const common::usize cAllocationCalls = baselineState.cAllocationCalls;
    REQUIRE( cAllocationCalls > 0u );
    for ( common::usize i = 0u; i < nCount; ++i ) {
        BrushSolid_Shutdown( &baseline[i] );
    }
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFrees );

    for ( common::usize iFail = 1u;
          iFail <= cAllocationCalls;
          ++iFail ) {
        DYNAMIC_SECTION( "allocation call " << iFail ) {
            generator_failure_allocator_state_t state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator =
                MakeGeneratorFailureAllocator( &state );
            geometry_source_id_allocator_t ids{};
            const geometry_source_id_t firstId = ids.next;
            brush_solid_t output[nCount]{};
            const auto outputBytes = ObjectBytes( output );

            REQUIRE( make( output, &allocator, &ids ) ==
                     geometry_status_t::ALLOCATION_FAILED );
            for ( common::usize i = 0u; i < nCount; ++i ) {
                CHECK( BrushIsCanonicalEmpty( output[i] ) );
            }
            CHECK( ObjectBytes( output ) == outputBytes );
            CHECK( ids.next.value == firstId.value );
            CHECK( state.cSuccessfulAllocations == state.cFrees );
        }
    }
}

template <typename make_fn_t>
void VerifySingleGeneratorIdExhaustionSweep( make_fn_t make )
{
    generator_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeGeneratorFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds{};
    brush_solid_t baseline{};
    REQUIRE( make( &baseline, &baselineAllocator, &baselineIds ) ==
             geometry_status_t::OK );
    const common::u64 cRequiredIds = baselineIds.next.value - 1u;
    REQUIRE( cRequiredIds > 0u );
    BrushSolid_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFrees );

    for ( common::u64 cAvailable = 0u;
          cAvailable < cRequiredIds;
          ++cAvailable ) {
        DYNAMIC_SECTION( "available source IDs " << cAvailable ) {
            generator_failure_allocator_state_t state{};
            common::allocator_t allocator =
                MakeGeneratorFailureAllocator( &state );
            geometry_source_id_allocator_t ids{};
            ids.next = cAvailable == 0u
                ? GEOMETRY_SOURCE_ID_INVALID
                : geometry_source_id_t{
                    common::CY_U64_MAX - cAvailable + 1u };
            const geometry_source_id_t firstId = ids.next;
            brush_solid_t output{};
            const auto outputBytes = ObjectBytes( output );

            REQUIRE( make( &output, &allocator, &ids ) ==
                     geometry_status_t::INSUFFICIENT_CAPACITY );
            CHECK( ObjectBytes( output ) == outputBytes );
            CHECK( ids.next.value == firstId.value );
            CHECK( state.cSuccessfulAllocations == state.cFrees );
        }
    }
}

template <common::usize nCount, typename make_fn_t>
void VerifyMultiGeneratorIdExhaustionSweep( make_fn_t make )
{
    generator_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeGeneratorFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds{};
    brush_solid_t baseline[nCount]{};
    REQUIRE( make( baseline, &baselineAllocator, &baselineIds ) ==
             geometry_status_t::OK );
    const common::u64 cRequiredIds = baselineIds.next.value - 1u;
    REQUIRE( cRequiredIds > 0u );
    for ( common::usize i = 0u; i < nCount; ++i ) {
        BrushSolid_Shutdown( &baseline[i] );
    }
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFrees );

    for ( common::u64 cAvailable = 0u;
          cAvailable < cRequiredIds;
          ++cAvailable ) {
        DYNAMIC_SECTION( "available source IDs " << cAvailable ) {
            generator_failure_allocator_state_t state{};
            common::allocator_t allocator =
                MakeGeneratorFailureAllocator( &state );
            geometry_source_id_allocator_t ids{};
            ids.next = cAvailable == 0u
                ? GEOMETRY_SOURCE_ID_INVALID
                : geometry_source_id_t{
                    common::CY_U64_MAX - cAvailable + 1u };
            const geometry_source_id_t firstId = ids.next;
            brush_solid_t output[nCount]{};
            const auto outputBytes = ObjectBytes( output );

            REQUIRE( make( output, &allocator, &ids ) ==
                     geometry_status_t::INSUFFICIENT_CAPACITY );
            CHECK( ObjectBytes( output ) == outputBytes );
            CHECK( ids.next.value == firstId.value );
            CHECK( state.cSuccessfulAllocations == state.cFrees );
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Wedge
// ---------------------------------------------------------------------------

TEST_CASE( "Generator: wedge has 5 sides and valid topology",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brush{};

    // Wedge with cut along X, slope along Z.
    REQUIRE( BrushGenerator_TryMakeWedge(
                 &brush, &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ),
                 0u, 2u ) ==
             geometry_status_t::OK );

    REQUIRE( BrushSolid_SideCount( &brush ) == 5u );
    f.validateBrush( &brush );

    // A wedge has 6 vertices, 9 edges, 5 faces.
    REQUIRE( f.boundary.vertices.nCount == 6u );
    REQUIRE( f.boundary.edges.nCount == 9u );
    REQUIRE( f.boundary.faces.nCount == 5u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: wedge with different axis combination",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brush{};

    // Wedge with cut along Y, slope along X.
    REQUIRE( BrushGenerator_TryMakeWedge(
                 &brush, &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 5.0, 5.0, 5.0 ),
                 Vec3d_Make( 2.0, 3.0, 1.0 ),
                 1u, 0u ) ==
             geometry_status_t::OK );

    REQUIRE( BrushSolid_SideCount( &brush ) == 5u );
    f.validateBrush( &brush );
    REQUIRE( f.boundary.vertices.nCount == 6u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: wedge same axis rejected",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brush{};

    // cutAxis == slopeAxis is invalid.
    REQUIRE( BrushGenerator_TryMakeWedge(
                 &brush, &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ),
                 1u, 1u ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Generator: wedge null args rejected",
           "[Gate6][Generator]" )
{
    REQUIRE( BrushGenerator_TryMakeWedge(
                 nullptr, nullptr, geometry_policy_t{}, nullptr,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ),
                 0u, 2u ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// Prism
// ---------------------------------------------------------------------------

TEST_CASE( "Generator: triangular prism (3 sides) has valid topology",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakePrism(
                 &brush, &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 1.0, 2.0, 3u, 2u ) ==
             geometry_status_t::OK );

    // 3 lateral + 2 caps = 5 sides.
    REQUIRE( BrushSolid_SideCount( &brush ) == 5u );
    f.validateBrush( &brush );

    // Triangular prism: 6 vertices, 9 edges, 5 faces.
    REQUIRE( f.boundary.vertices.nCount == 6u );
    REQUIRE( f.boundary.edges.nCount == 9u );
    REQUIRE( f.boundary.faces.nCount == 5u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: hexagonal prism (6 sides) has valid topology",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakePrism(
                 &brush, &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 1.0, 1.0, 6u, 1u ) ==
             geometry_status_t::OK );

    // 6 lateral + 2 caps = 8 sides.
    REQUIRE( BrushSolid_SideCount( &brush ) == 8u );
    f.validateBrush( &brush );

    // Hexagonal prism: 12 vertices, 18 edges, 8 faces.
    REQUIRE( f.boundary.vertices.nCount == 12u );
    REQUIRE( f.boundary.edges.nCount == 18u );
    REQUIRE( f.boundary.faces.nCount == 8u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: prism along X axis",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakePrism(
                 &brush, &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 3.0, 0.0, 0.0 ),
                 2.0, 5.0, 4u, 0u ) ==
             geometry_status_t::OK );

    // 4 lateral + 2 caps = 6 sides (square prism = box).
    REQUIRE( BrushSolid_SideCount( &brush ) == 6u );
    f.validateBrush( &brush );
    REQUIRE( f.boundary.vertices.nCount == 8u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: prism with < 3 sides rejected",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakePrism(
                 &brush, &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 1.0, 1.0, 2u, 2u ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Generator: prism zero radius rejected",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakePrism(
                 &brush, &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 0.0, 1.0, 4u, 2u ) ==
             geometry_status_t::DEGENERATE );
}

// ---------------------------------------------------------------------------
// Staircase
// ---------------------------------------------------------------------------

TEST_CASE( "Generator: staircase produces correct number of steps",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brushes[4]{};

    REQUIRE( BrushGenerator_TryMakeStaircase(
                 brushes, 4u,
                 &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 1.0, 0.5, 2.0 ) ==
             geometry_status_t::OK );

    // Each step is a valid box.
    for ( common::usize i = 0u; i < 4u; ++i ) {
        REQUIRE( BrushSolid_SideCount( &brushes[i] ) == 6u );
        f.validateBrush( &brushes[i] );
        REQUIRE( f.boundary.vertices.nCount == 8u );
    }

    // Each step has a unique brush ID.
    for ( common::usize i = 0u; i < 4u; ++i ) {
        for ( common::usize j = i + 1u; j < 4u; ++j ) {
            REQUIRE( brushes[i].sourceId.value !=
                     brushes[j].sourceId.value );
        }
    }

    // Steps should ascend: step i has height (i+1)*stepHeight.
    for ( common::usize i = 0u; i < 4u; ++i ) {
        REQUIRE( BrushBoundary_TryReconstruct(
                     &f.boundary, &brushes[i], f.policy ) ==
                 geometry_status_t::OK );
        const math::aabbd_t bounds =
            BrushQueries_ComputeBoundsd( &f.boundary );
        const common::f64 expectedHeight =
            static_cast<common::f64>( i + 1u ) * 0.5;
        REQUIRE( bounds.maximum.z ==
                 Approx( expectedHeight ).margin( 0.01 ) );
    }

    for ( common::usize i = 0u; i < 4u; ++i ) {
        BrushSolid_Shutdown( &brushes[i] );
    }
}

TEST_CASE( "Generator: staircase zero steps rejected",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeStaircase(
                 &brush, 0u,
                 &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 1.0, 0.5, 2.0 ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Generator: staircase null args rejected",
           "[Gate6][Generator]" )
{
    REQUIRE( BrushGenerator_TryMakeStaircase(
                 nullptr, 4u,
                 nullptr, geometry_policy_t{}, nullptr,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 1.0, 0.5, 2.0 ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// Arch
// ---------------------------------------------------------------------------

TEST_CASE( "Generator: arch produces correct number of segments",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brushes[6]{};

    // 6-segment semicircular arch.
    REQUIRE( BrushGenerator_TryMakeArch(
                 brushes, 6u,
                 &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 5.0, 3.0, 2.0,
                 3.14159265358979323846 ) ==
             geometry_status_t::OK );

    for ( common::usize i = 0u; i < 6u; ++i ) {
        REQUIRE( BrushSolid_SideCount( &brushes[i] ) == 6u );
        f.validateBrush( &brushes[i] );
        REQUIRE( f.boundary.vertices.nCount == 8u );
    }

    // Each segment has a unique brush ID.
    for ( common::usize i = 0u; i < 6u; ++i ) {
        for ( common::usize j = i + 1u; j < 6u; ++j ) {
            REQUIRE( brushes[i].sourceId.value !=
                     brushes[j].sourceId.value );
        }
    }

    for ( common::usize i = 0u; i < 6u; ++i ) {
        BrushSolid_Shutdown( &brushes[i] );
    }
}

TEST_CASE( "Generator: full-ring arch",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brushes[8]{};

    // 8-segment full ring.
    REQUIRE( BrushGenerator_TryMakeArch(
                 brushes, 8u,
                 &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 4.0, 2.0, 1.0,
                 2.0 * 3.14159265358979323846 ) ==
             geometry_status_t::OK );

    for ( common::usize i = 0u; i < 8u; ++i ) {
        f.validateBrush( &brushes[i] );
    }

    for ( common::usize i = 0u; i < 8u; ++i ) {
        BrushSolid_Shutdown( &brushes[i] );
    }
}

TEST_CASE( "Generator: arch inner >= outer rejected",
           "[Gate6][Generator]" )
{
    GeneratorFixture f;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeArch(
                 &brush, 1u,
                 &f.allocator, f.policy, &f.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 3.0, 5.0, 1.0,
                 3.14159265358979323846 ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "Generator: arch null args rejected",
           "[Gate6][Generator]" )
{
    REQUIRE( BrushGenerator_TryMakeArch(
                 nullptr, 4u,
                 nullptr, geometry_policy_t{}, nullptr,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 5.0, 3.0, 2.0,
                 3.14159265358979323846 ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ===========================================================================
// Cylinder (Gate 8)
// ===========================================================================

TEST_CASE( "Generator: cylinder 16-sided produces valid boundary",
           "[Gate8][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeCylinder(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 2.0, 4.0, 16u, 2u ) ==
             geometry_status_t::OK );

    REQUIRE( BrushBoundary_TryReconstruct(
                 &fix.boundary, &brush, fix.policy ) ==
             geometry_status_t::OK );

    // 16 lateral faces + 2 caps = 18 faces.
    REQUIRE( BrushBoundary_FaceCount( &fix.boundary ) == 18u );

    // Euler: V - E + F = 2.
    const auto cV = BrushBoundary_VertexCount( &fix.boundary );
    const auto cE = BrushBoundary_EdgeCount( &fix.boundary );
    const auto cF = BrushBoundary_FaceCount( &fix.boundary );
    REQUIRE( static_cast<common::i32>( cV ) -
             static_cast<common::i32>( cE ) +
             static_cast<common::i32>( cF ) == 2 );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: cylinder is equivalent to prism",
           "[Gate8][Generator]" )
{
    // Both should produce the same topology — same side count.
    GeneratorFixture fix;
    brush_solid_t cylBrush{};
    geometry_source_id_allocator_t idAlloc2{};
    brush_solid_t prismBrush{};

    REQUIRE( BrushGenerator_TryMakeCylinder(
                 &cylBrush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 3.0, 2.0, 8u, 1u ) ==
             geometry_status_t::OK );

    REQUIRE( BrushGenerator_TryMakePrism(
                 &prismBrush, &fix.allocator, fix.policy, &idAlloc2,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 3.0, 2.0, 8u, 1u ) ==
             geometry_status_t::OK );

    REQUIRE( BrushSolid_SideCount( &cylBrush ) ==
             BrushSolid_SideCount( &prismBrush ) );

    BrushSolid_Shutdown( &cylBrush );
    BrushSolid_Shutdown( &prismBrush );
}

// ===========================================================================
// Cone / Frustum (Gate 8)
// ===========================================================================

TEST_CASE( "Generator: cone (topRadius=0) produces valid boundary",
           "[Gate8][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    // True cone: bottom radius 2.0, top radius 0.0, 8 sides.
    REQUIRE( BrushGenerator_TryMakeCone(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 2.0, 0.0, 3.0, 8u, 2u ) ==
             geometry_status_t::OK );

    REQUIRE( BrushBoundary_TryReconstruct(
                 &fix.boundary, &brush, fix.policy ) ==
             geometry_status_t::OK );

    // A true cone has no zero-area cap at its apex: 8 lateral faces plus
    // one bottom cap.
    const auto cF = BrushBoundary_FaceCount( &fix.boundary );
    REQUIRE( cF == 9u );

    // Euler must hold regardless.
    const auto cV = BrushBoundary_VertexCount( &fix.boundary );
    const auto cE = BrushBoundary_EdgeCount( &fix.boundary );
    REQUIRE( static_cast<common::i32>( cV ) -
             static_cast<common::i32>( cE ) +
             static_cast<common::i32>( cF ) == 2 );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: frustum (two different radii) produces valid boundary",
           "[Gate8][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    // Frustum: bottom radius 3.0, top radius 1.5, 12 sides.
    REQUIRE( BrushGenerator_TryMakeCone(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 3.0, 1.5, 4.0, 12u, 2u ) ==
             geometry_status_t::OK );

    REQUIRE( BrushBoundary_TryReconstruct(
                 &fix.boundary, &brush, fix.policy ) ==
             geometry_status_t::OK );

    // 12 lateral + 2 caps = 14 faces.
    REQUIRE( BrushBoundary_FaceCount( &fix.boundary ) == 14u );

    // Euler check.
    const auto cV = BrushBoundary_VertexCount( &fix.boundary );
    const auto cE = BrushBoundary_EdgeCount( &fix.boundary );
    const auto cF = BrushBoundary_FaceCount( &fix.boundary );
    REQUIRE( static_cast<common::i32>( cV ) -
             static_cast<common::i32>( cE ) +
             static_cast<common::i32>( cF ) == 2 );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: cone with equal radii degenerates to prism",
           "[Gate8][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeCone(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 2.0, 2.0, 3.0, 6u, 2u ) ==
             geometry_status_t::OK );

    // Should produce 6+2=8 sides like a prism.
    REQUIRE( BrushSolid_SideCount( &brush ) == 8u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: cone null args rejected",
           "[Gate8][Generator]" )
{
    REQUIRE( BrushGenerator_TryMakeCone(
                 nullptr, nullptr, geometry_policy_t{}, nullptr,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 2.0, 0.0, 3.0, 8u, 2u ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Generator: cone rejects negative bottom radius",
           "[Gate8][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeCone(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 -1.0, 0.0, 3.0, 8u, 2u ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "Generator: cone rejects invalid axis",
           "[Gate8][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeCone(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 2.0, 0.0, 3.0, 8u, 3u ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Generator: cylinder null args rejected",
           "[Gate8][Generator]" )
{
    REQUIRE( BrushGenerator_TryMakeCylinder(
                 nullptr, nullptr, geometry_policy_t{}, nullptr,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 2.0, 3.0, 16u, 2u ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// Sphere (Icosphere)
// ---------------------------------------------------------------------------

TEST_CASE( "Generator: sphere subdivision 0 is an icosahedron",
           "[Gate11][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeSphere(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 10.0, 0u ) ==
             geometry_status_t::OK );

    fix.validateBrush( &brush );

    // An icosahedron has 12 vertices, 30 edges, 20 faces.
    REQUIRE( fix.boundary.vertices.nCount == 12u );
    REQUIRE( fix.boundary.edges.nCount == 30u );
    REQUIRE( fix.boundary.faces.nCount == 20u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: sphere subdivision 1 has correct vertex count",
           "[Gate11][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeSphere(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 10.0, 1u ) ==
             geometry_status_t::OK );

    fix.validateBrush( &brush );

    // Subdivision 1: 42 vertices, 120 edges, 80 faces.
    REQUIRE( fix.boundary.vertices.nCount == 42u );
    REQUIRE( fix.boundary.faces.nCount == 80u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: sphere vertices lie on the sphere surface",
           "[Gate11][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    const auto center = Vec3d_Make( 5.0, 10.0, -3.0 );
    const common::f64 radius = 20.0;

    REQUIRE( BrushGenerator_TryMakeSphere(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 center, radius, 1u ) ==
             geometry_status_t::OK );

    fix.validateBrush( &brush );

    // Every boundary vertex should be approximately `radius` from center.
    const common::usize cVerts = fix.boundary.vertices.nCount;
    for ( common::usize i = 0u; i < cVerts; ++i ) {
        const auto diff = math::Vec3d_Subtract(
            fix.boundary.vertices.pData[i], center );
        const common::f64 dist = std::sqrt( math::Vec3d_LengthSquared( diff ) );
        REQUIRE( dist == Approx( radius ).margin( 0.5 ) );
    }

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: sphere at offset center has valid topology",
           "[Gate11][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeSphere(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 100.0, -50.0, 25.0 ),
                 5.0, 1u ) ==
             geometry_status_t::OK );

    fix.validateBrush( &brush );

    // Subdivision 1: 42 vertices.
    REQUIRE( fix.boundary.vertices.nCount == 42u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Generator: sphere rejects zero radius",
           "[Gate11][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeSphere(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 0.0, 0u ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "Generator: sphere rejects negative radius",
           "[Gate11][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeSphere(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 -5.0, 0u ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "Generator: sphere null args rejected",
           "[Gate11][Generator]" )
{
    REQUIRE( BrushGenerator_TryMakeSphere(
                 nullptr, nullptr, geometry_policy_t{}, nullptr,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 10.0, 0u ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Generator: sphere rejects subdivision beyond active limits",
           "[Gate11][Generator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    const geometry_source_id_t firstId = fix.idAlloc.next;
    REQUIRE( BrushGenerator_TryMakeSphere(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 10.0, 5u ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    CHECK( BrushIsCanonicalEmpty( brush ) );
    CHECK( fix.idAlloc.next.value == firstId.value );
}

// ---------------------------------------------------------------------------
// Tetrahedron (Gate 15)
// ---------------------------------------------------------------------------

TEST_CASE( "BrushGenerator: tetrahedron has 4 sides",
           "[Gate15][BrushGenerator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeTetrahedron(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ), 2.0 ) ==
             geometry_status_t::OK );

    CHECK( BrushSolid_SideCount( &brush ) == 4u );

    fix.validateBrush( &brush );
    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "BrushGenerator: tetrahedron boundary has 4 vertices",
           "[Gate15][BrushGenerator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeTetrahedron(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ), 2.0 ) ==
             geometry_status_t::OK );

    fix.validateBrush( &brush );

    CHECK( fix.boundary.vertices.nCount == 4u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "BrushGenerator: tetrahedron rejects zero radius",
           "[Gate15][BrushGenerator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    CHECK( BrushGenerator_TryMakeTetrahedron(
               &brush, &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 0.0 ) ==
           geometry_status_t::DEGENERATE );
}

TEST_CASE( "BrushGenerator: tetrahedron rejects negative radius",
           "[Gate15][BrushGenerator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};

    CHECK( BrushGenerator_TryMakeTetrahedron(
               &brush, &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), -1.0 ) ==
           geometry_status_t::DEGENERATE );
}

TEST_CASE( "BrushGenerator: tetrahedron null brush rejected",
           "[Gate15][BrushGenerator]" )
{
    GeneratorFixture fix;

    CHECK( BrushGenerator_TryMakeTetrahedron(
               nullptr, &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 2.0 ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "BrushGenerator: every primitive is allocation-failure atomic",
           "[Gate15][BrushGenerator][Allocation][Contract]" )
{
    geometry_policy_t policy{};

    SECTION( "box" ) {
        const auto make = [&]( brush_solid_t *pOutput,
                               const common::allocator_t *pAllocator,
                               geometry_source_id_allocator_t *pIds ) {
                return BrushGenerator_TryMakeBox(
                    pOutput, pAllocator, policy, pIds,
                    Vec3d_Make( 1.0, 2.0, 3.0 ),
                    Vec3d_Make( 2.0, 3.0, 4.0 ) );
            };
        VerifySingleGeneratorAllocationSweep( make );
        VerifySingleGeneratorIdExhaustionSweep( make );
    }
    SECTION( "wedge" ) {
        const auto make = [&]( brush_solid_t *pOutput,
                               const common::allocator_t *pAllocator,
                               geometry_source_id_allocator_t *pIds ) {
                return BrushGenerator_TryMakeWedge(
                    pOutput, pAllocator, policy, pIds,
                    Vec3d_Make( 1.0, 2.0, 3.0 ),
                    Vec3d_Make( 4.0, 2.0, 1.0 ), 0u, 2u );
            };
        VerifySingleGeneratorAllocationSweep( make );
        VerifySingleGeneratorIdExhaustionSweep( make );
    }
    SECTION( "prism" ) {
        const auto make = [&]( brush_solid_t *pOutput,
                               const common::allocator_t *pAllocator,
                               geometry_source_id_allocator_t *pIds ) {
                return BrushGenerator_TryMakePrism(
                    pOutput, pAllocator, policy, pIds,
                    Vec3d_Make( 1.0, 2.0, 3.0 ),
                    4.0, 3.0, 12u, 1u );
            };
        VerifySingleGeneratorAllocationSweep( make );
        VerifySingleGeneratorIdExhaustionSweep( make );
    }
    SECTION( "cylinder" ) {
        const auto make = [&]( brush_solid_t *pOutput,
                               const common::allocator_t *pAllocator,
                               geometry_source_id_allocator_t *pIds ) {
                return BrushGenerator_TryMakeCylinder(
                    pOutput, pAllocator, policy, pIds,
                    Vec3d_Make( 1.0, 2.0, 3.0 ),
                    4.0, 3.0, 12u, 1u );
            };
        VerifySingleGeneratorAllocationSweep( make );
        VerifySingleGeneratorIdExhaustionSweep( make );
    }
    SECTION( "cone" ) {
        const auto make = [&]( brush_solid_t *pOutput,
                               const common::allocator_t *pAllocator,
                               geometry_source_id_allocator_t *pIds ) {
                return BrushGenerator_TryMakeCone(
                    pOutput, pAllocator, policy, pIds,
                    Vec3d_Make( 1.0, 2.0, 3.0 ),
                    4.0, 1.0, 3.0, 12u, 1u );
            };
        VerifySingleGeneratorAllocationSweep( make );
        VerifySingleGeneratorIdExhaustionSweep( make );
    }
    SECTION( "sphere" ) {
        const auto make = [&]( brush_solid_t *pOutput,
                               const common::allocator_t *pAllocator,
                               geometry_source_id_allocator_t *pIds ) {
                return BrushGenerator_TryMakeSphere(
                    pOutput, pAllocator, policy, pIds,
                    Vec3d_Make( 1.0, 2.0, 3.0 ), 4.0, 1u );
            };
        VerifySingleGeneratorAllocationSweep( make );
        VerifySingleGeneratorIdExhaustionSweep( make );
    }
    SECTION( "tetrahedron" ) {
        const auto make = [&]( brush_solid_t *pOutput,
                               const common::allocator_t *pAllocator,
                               geometry_source_id_allocator_t *pIds ) {
                return BrushGenerator_TryMakeTetrahedron(
                    pOutput, pAllocator, policy, pIds,
                    Vec3d_Make( 1.0, 2.0, 3.0 ), 4.0 );
            };
        VerifySingleGeneratorAllocationSweep( make );
        VerifySingleGeneratorIdExhaustionSweep( make );
    }
    SECTION( "staircase" ) {
        const auto make = [&]( brush_solid_t *pOutput,
                               const common::allocator_t *pAllocator,
                               geometry_source_id_allocator_t *pIds ) {
                return BrushGenerator_TryMakeStaircase(
                    pOutput, 4u, pAllocator, policy, pIds,
                    Vec3d_Make( -2.0, -1.0, 0.0 ), 1.0, 0.5, 2.0 );
            };
        VerifyMultiGeneratorAllocationSweep<4u>( make );
        VerifyMultiGeneratorIdExhaustionSweep<4u>( make );
    }
    SECTION( "arch" ) {
        const auto make = [&]( brush_solid_t *pOutput,
                               const common::allocator_t *pAllocator,
                               geometry_source_id_allocator_t *pIds ) {
                return BrushGenerator_TryMakeArch(
                    pOutput, 4u, pAllocator, policy, pIds,
                    Vec3d_Make( 0.0, 0.0, 0.0 ),
                    5.0, 3.0, 2.0, math::CY_PI_D );
            };
        VerifyMultiGeneratorAllocationSweep<4u>( make );
        VerifyMultiGeneratorIdExhaustionSweep<4u>( make );
    }
}

TEST_CASE( "BrushGenerator: invalid numerical inputs never publish output",
           "[Gate15][BrushGenerator][Contract]" )
{
    const common::f64 nan = std::numeric_limits<common::f64>::quiet_NaN();
    GeneratorFixture fix;
    brush_solid_t output[4]{};
    const geometry_source_id_t firstId = fix.idAlloc.next;

    CHECK( BrushGenerator_TryMakeBox(
               &output[0], &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( nan, 0.0, 0.0 ),
               Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( BrushGenerator_TryMakeWedge(
               &output[0], &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ),
               Vec3d_Make( 1.0, nan, 1.0 ), 0u, 2u ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( BrushGenerator_TryMakePrism(
               &output[0], &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), nan, 1.0, 8u, 2u ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( BrushGenerator_TryMakeCylinder(
               &output[0], &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 1.0, nan, 8u, 2u ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( BrushGenerator_TryMakeCone(
               &output[0], &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 2.0, nan, 1.0, 8u, 2u ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( BrushGenerator_TryMakeSphere(
               &output[0], &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), nan, 0u ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( BrushGenerator_TryMakeTetrahedron(
               &output[0], &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), nan ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( BrushGenerator_TryMakeStaircase(
               output, 4u, &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), nan, 1.0, 1.0 ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( BrushGenerator_TryMakeArch(
               output, 4u, &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ),
               5.0, 3.0, 2.0, nan ) ==
           geometry_status_t::NUMERIC_FAILURE );

    for ( const brush_solid_t &brush : output ) {
        CHECK( BrushIsCanonicalEmpty( brush ) );
    }
    CHECK( fix.idAlloc.next.value == firstId.value );
}

TEST_CASE( "BrushGenerator: every public entry validates policy",
           "[Gate15][BrushGenerator][Contract]" )
{
    GeneratorFixture fix;
    geometry_policy_t invalidPolicy = fix.policy;
    invalidPolicy.numerical.fCanonicalQuantization = 0.0;
    brush_solid_t output[4]{};
    const geometry_source_id_t firstId = fix.idAlloc.next;

    CHECK( BrushGenerator_TryMakeBox(
               &output[0], &fix.allocator, invalidPolicy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ),
               Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushGenerator_TryMakeWedge(
               &output[0], &fix.allocator, invalidPolicy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ),
               Vec3d_Make( 1.0, 1.0, 1.0 ), 0u, 2u ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushGenerator_TryMakePrism(
               &output[0], &fix.allocator, invalidPolicy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 1.0, 1.0, 8u, 2u ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushGenerator_TryMakeCylinder(
               &output[0], &fix.allocator, invalidPolicy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 1.0, 1.0, 8u, 2u ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushGenerator_TryMakeCone(
               &output[0], &fix.allocator, invalidPolicy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 2.0, 1.0, 1.0, 8u, 2u ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushGenerator_TryMakeSphere(
               &output[0], &fix.allocator, invalidPolicy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 1.0, 0u ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushGenerator_TryMakeTetrahedron(
               &output[0], &fix.allocator, invalidPolicy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 1.0 ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushGenerator_TryMakeStaircase(
               output, 4u, &fix.allocator, invalidPolicy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 1.0, 1.0, 1.0 ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushGenerator_TryMakeArch(
               output, 4u, &fix.allocator, invalidPolicy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ),
               5.0, 3.0, 2.0, math::CY_PI_D ) ==
           geometry_status_t::INVALID_ARGUMENT );
    for ( const brush_solid_t &brush : output ) {
        CHECK( BrushIsCanonicalEmpty( brush ) );
    }
    CHECK( fix.idAlloc.next.value == firstId.value );
}

TEST_CASE( "BrushGenerator: staged workspace obeys the scratch budget",
           "[Gate15][BrushGenerator][Contract]" )
{
    GeneratorFixture fix;
    fix.policy.limits.cbScratchMax = 1u;
    REQUIRE( GeometryPolicy_IsValid( fix.policy ) );
    brush_solid_t output[4]{};
    const auto outputBytes = ObjectBytes( output );
    const geometry_source_id_t firstId = fix.idAlloc.next;

    CHECK( BrushGenerator_TryMakeStaircase(
               output, 4u, &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 1.0, 1.0, 1.0 ) ==
           geometry_status_t::LIMIT_EXCEEDED );
    CHECK( BrushGenerator_TryMakeArch(
               output, 4u, &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ),
               5.0, 3.0, 2.0, math::CY_PI_D ) ==
           geometry_status_t::LIMIT_EXCEEDED );
    CHECK( BrushGenerator_TryMakeSphere(
               &output[0], &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 2.0, 1u ) ==
           geometry_status_t::LIMIT_EXCEEDED );
    CHECK( ObjectBytes( output ) == outputBytes );
    CHECK( fix.idAlloc.next.value == firstId.value );
}

TEST_CASE( "BrushGenerator: counts are governed by policy instead of legacy caps",
           "[Gate15][BrushGenerator][Contract]" )
{
    GeneratorFixture fix;

    brush_solid_t steps[65]{};
    REQUIRE( BrushGenerator_TryMakeStaircase(
                 steps, 65u, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ), 1.0, 0.25, 1.0 ) ==
             geometry_status_t::OK );
    for ( brush_solid_t &step : steps ) {
        BrushSolid_Shutdown( &step );
    }

    brush_solid_t segments[257]{};
    REQUIRE( BrushGenerator_TryMakeArch(
                 segments, 257u, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 5.0, 3.0, 1.0, math::CY_PI_D ) ==
             geometry_status_t::OK );
    for ( brush_solid_t &segment : segments ) {
        BrushSolid_Shutdown( &segment );
    }

    geometry_policy_t spherePolicy = fix.policy;
    spherePolicy.limits.cBrushSidesPerBrushMax = 400u;
    REQUIRE( GeometryPolicy_IsValid( spherePolicy ) );
    brush_solid_t sphere{};
    REQUIRE( BrushGenerator_TryMakeSphere(
                 &sphere, &fix.allocator, spherePolicy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ), 2.0, 2u ) ==
             geometry_status_t::OK );
    CHECK( BrushSolid_SideCount( &sphere ) == 320u );
    BrushSolid_Shutdown( &sphere );
}

TEST_CASE( "BrushGenerator: initialized destinations and exhausted IDs are unchanged",
           "[Gate15][BrushGenerator][Contract]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    const auto *pSides = brush.sides.pData;
    const common::usize cSides = brush.sides.nCount;
    const geometry_source_id_t brushId = brush.sourceId;
    const geometry_source_id_t nextId = fix.idAlloc.next;

    CHECK( BrushGenerator_TryMakeSphere(
               &brush, &fix.allocator, fix.policy, &fix.idAlloc,
               Vec3d_Make( 0.0, 0.0, 0.0 ), 2.0, 0u ) ==
           geometry_status_t::ALREADY_INITIALIZED );
    CHECK( brush.sides.pData == pSides );
    CHECK( brush.sides.nCount == cSides );
    CHECK( brush.sourceId.value == brushId.value );
    CHECK( fix.idAlloc.next.value == nextId.value );
    BrushSolid_Shutdown( &brush );

    geometry_source_id_allocator_t exhaustedSoon{
        geometry_source_id_t{ common::CY_U64_MAX - 2u } };
    brush_solid_t failed{};
    const geometry_source_id_t before = exhaustedSoon.next;
    CHECK( BrushGenerator_TryMakeBox(
               &failed, &fix.allocator, fix.policy, &exhaustedSoon,
               Vec3d_Make( 0.0, 0.0, 0.0 ),
               Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
           geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( BrushIsCanonicalEmpty( failed ) );
    CHECK( exhaustedSoon.next.value == before.value );
}

TEST_CASE( "BrushGenerator: wedge slope honors unequal extents",
           "[Gate15][BrushGenerator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeWedge(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 4.0, 2.0, 1.0 ), 0u, 2u ) ==
             geometry_status_t::OK );
    brush_solid_side_t slope{};
    REQUIRE( BrushSolid_TryGetSide( &brush, 4u, &slope ) ==
             geometry_status_t::OK );
    CHECK( slope.plane.normal.x / slope.plane.normal.z ==
           Approx( 0.25 ).margin( 1.0e-12 ) );
    fix.validateBrush( &brush );
    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "BrushGenerator: axial coordinate limit follows extrusion axis",
           "[Gate15][BrushGenerator]" )
{
    GeneratorFixture fix;
    fix.policy.numerical.fCoordinateMagnitudeLimit = 100.0;
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakePrism(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 95.0, 0.0, 0.0 ),
                 10.0, 2.0, 8u, 0u ) ==
             geometry_status_t::OK );
    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "BrushGenerator: tetrahedron attributes are one-to-one with sides",
           "[Gate15][BrushGenerator]" )
{
    GeneratorFixture fix;
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeTetrahedron(
                 &brush, &fix.allocator, fix.policy, &fix.idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ), 2.0 ) ==
             geometry_status_t::OK );
    for ( common::u32 i = 0u; i < 4u; ++i ) {
        brush_solid_side_t side{};
        REQUIRE( BrushSolid_TryGetSide( &brush, i, &side ) ==
                 geometry_status_t::OK );
        CHECK( side.iAttributeIndex == i );
    }
    BrushSolid_Shutdown( &brush );
}

} // namespace cypher::editor::geometry
