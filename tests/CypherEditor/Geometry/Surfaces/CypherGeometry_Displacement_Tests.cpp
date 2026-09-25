//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Displacement_Tests.cpp
//  Purpose: Tests displacement surfaces: flat evaluation, distances,
//           elevation and axis, deterministic noise, resampling, sewing
//           neighbours with different normals, mesh building, validation,
//           and allocation-failure atomicity.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Displacement.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

// A 64 x 64 floor quad at z = 0, facing +Z, UVs 0..1.
displacement_quad_t Floor( double x0 = 0.0 )
{
    displacement_quad_t q{};
    q.corners[0] = math::Vec3d_Make( x0, 0, 0 );
    q.corners[1] = math::Vec3d_Make( x0 + 64, 0, 0 );
    q.corners[2] = math::Vec3d_Make( x0 + 64, 64, 0 );
    q.corners[3] = math::Vec3d_Make( x0, 64, 0 );
    q.uvs[0] = { 0, 0 };
    q.uvs[1] = { 1, 0 };
    q.uvs[2] = { 1, 1 };
    q.uvs[3] = { 0, 1 };
    return q;
}

// A wall standing on the floor's x = 64 edge, facing +X... wound so it
// shares that edge with the floor (in the opposite direction).
displacement_quad_t Wall()
{
    displacement_quad_t q{};
    q.corners[0] = math::Vec3d_Make( 64, 64, 0 );
    q.corners[1] = math::Vec3d_Make( 64, 0, 0 );
    q.corners[2] = math::Vec3d_Make( 64, 0, 64 );
    q.corners[3] = math::Vec3d_Make( 64, 64, 64 );
    return q;
}

struct Disp {
    displacement_t d{};
    explicit Disp( common::u32 power ) { REQUIRE( Displacement_Init( &d, common::Allocator_GetSystem(), power ) == geometry_status_t::OK ); }
    ~Disp() { Displacement_Shutdown( &d ); }
};

struct Grid {
    displacement_grid_t g{};
    Grid() { REQUIRE( DisplacementGrid_Init( &g, common::Allocator_GetSystem() ) == geometry_status_t::OK ); }
    ~Grid() { DisplacementGrid_Shutdown( &g ); }
};

bool Close( math::vec3d_t a, math::vec3d_t b, double eps ) { return std::fabs( a.x - b.x ) <= eps && std::fabs( a.y - b.y ) <= eps && std::fabs( a.z - b.z ) <= eps; }

} // namespace

TEST_CASE( "A flat displacement is the quad's bilinear grid", "[geometry][displacement]" )
{
    Disp disp( 3u );
    Grid grid;
    REQUIRE( Displacement_TryEvaluate( &disp.d, Floor(), &grid.g ) == geometry_status_t::OK );
    REQUIRE( grid.g.side == 9u );
    REQUIRE( grid.g.positions.nCount == 81u );
    for ( common::u32 j = 0; j < 9u; ++j ) {
        for ( common::u32 i = 0; i < 9u; ++i ) {
            const common::usize k = j * 9u + i;
            CHECK( Close( grid.g.positions.pData[k], math::Vec3d_Make( 8.0 * i, 8.0 * j, 0 ), 1e-12 ) );
            CHECK( Close( grid.g.normals.pData[k], math::Vec3d_Make( 0, 0, 1 ), 1e-12 ) );
            CHECK( std::fabs( grid.g.uvs.pData[k].x - i / 8.0 ) < 1e-12 );
            CHECK( std::fabs( grid.g.uvs.pData[k].y - j / 8.0 ) < 1e-12 );
        }
    }
    CHECK( Displacement_Side( 0u ) == 0u );
    CHECK( Displacement_Side( 4u ) == 17u );
    CHECK( Displacement_Side( 5u ) == 0u );
}

TEST_CASE( "Distances, elevation, offsets and an explicit axis move the grid", "[geometry][displacement]" )
{
    Disp disp( 2u );
    const common::usize centre = 2u * 5u + 2u;
    disp.d.distances.pData[centre] = 10.0;
    disp.d.offsets.pData[0] = math::Vec3d_Make( 1, 2, 3 );
    disp.d.elevation = 4.0;
    Grid grid;
    REQUIRE( Displacement_TryEvaluate( &disp.d, Floor(), &grid.g ) == geometry_status_t::OK );
    CHECK( Close( grid.g.positions.pData[centre], math::Vec3d_Make( 32, 32, 14 ), 1e-12 ) );
    CHECK( Close( grid.g.positions.pData[0], math::Vec3d_Make( 1, 2, 7 ), 1e-12 ) );
    CHECK( Close( grid.g.positions.pData[1], math::Vec3d_Make( 16, 0, 4 ), 1e-12 ) );
    // The bump tilts the normals around it outward.
    CHECK( grid.g.normals.pData[centre - 1u].x < 0.0 );
    CHECK( grid.g.normals.pData[centre + 1u].x > 0.0 );
    disp.d.bUseAxis = true;
    disp.d.axis = math::Vec3d_Make( 1, 0, 0 );
    REQUIRE( Displacement_TryEvaluate( &disp.d, Floor(), &grid.g ) == geometry_status_t::OK );
    CHECK( Close( grid.g.positions.pData[centre], math::Vec3d_Make( 46, 32, 0 ), 1e-12 ) );
}

TEST_CASE( "Noise is deterministic, bounded, and seed-dependent", "[geometry][displacement]" )
{
    Disp disp( 4u );
    disp.d.noise.amplitude = 5.0;
    disp.d.noise.frequency = 1.0 / 16.0;
    disp.d.noise.cOctaves = 4u;
    disp.d.noise.seed = 7u;
    Grid a, b, c;
    REQUIRE( Displacement_TryEvaluate( &disp.d, Floor(), &a.g ) == geometry_status_t::OK );
    REQUIRE( Displacement_TryEvaluate( &disp.d, Floor(), &b.g ) == geometry_status_t::OK );
    disp.d.noise.seed = 8u;
    REQUIRE( Displacement_TryEvaluate( &disp.d, Floor(), &c.g ) == geometry_status_t::OK );
    bool bSame = true, bDiffers = false, bMoves = false;
    for ( common::usize k = 0; k < a.g.positions.nCount; ++k ) {
        const math::vec3d_t pa = a.g.positions.pData[k], pb = b.g.positions.pData[k], pc = c.g.positions.pData[k];
        bSame = bSame && pa.x == pb.x && pa.y == pb.y && pa.z == pb.z;
        bDiffers = bDiffers || pa.z != pc.z;
        bMoves = bMoves || std::fabs( pa.z ) > 0.1;
        CHECK( std::fabs( pa.z ) <= 5.0 );
    }
    CHECK( bSame );
    CHECK( bDiffers );
    CHECK( bMoves );
}

TEST_CASE( "Resampling up and back down restores the samples", "[geometry][displacement]" )
{
    Disp disp( 2u );
    for ( common::usize k = 0; k < disp.d.distances.nCount; ++k ) {
        disp.d.distances.pData[k] = static_cast<double>( k % 7 ) - 3.0;
        disp.d.alphas.pData[k] = static_cast<double>( k % 5 ) / 4.0;
        disp.d.offsets.pData[k] = math::Vec3d_Make( 0.5 * k, 0, 0 );
    }
    std::vector<double> before( disp.d.distances.pData, disp.d.distances.pData + disp.d.distances.nCount );
    REQUIRE( Displacement_TryResample( &disp.d, 4u ) == geometry_status_t::OK );
    CHECK( disp.d.power == 4u );
    CHECK( disp.d.distances.nCount == 17u * 17u );
    // Every old sample reappears at its spot in the finer grid.
    for ( common::u32 j = 0; j < 5u; ++j ) {
        for ( common::u32 i = 0; i < 5u; ++i ) { CHECK( disp.d.distances.pData[( 4u * j ) * 17u + 4u * i] == before[j * 5u + i] ); }
    }
    // Between samples it is the bilinear value.
    CHECK( std::fabs( disp.d.distances.pData[2] - 0.5 * ( before[0] + before[1] ) ) < 1e-12 );
    REQUIRE( Displacement_TryResample( &disp.d, 2u ) == geometry_status_t::OK );
    for ( common::usize k = 0; k < before.size(); ++k ) { CHECK( disp.d.distances.pData[k] == before[k] ); }
    CHECK( Displacement_TryResample( &disp.d, 9u ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( disp.d.power == 2u );
}

TEST_CASE( "Sewing makes neighbours with different normals meet", "[geometry][displacement]" )
{
    Disp floor( 3u ), wall( 3u );
    for ( common::usize k = 0; k < floor.d.distances.nCount; ++k ) {
        floor.d.distances.pData[k] = 3.0 + 0.1 * static_cast<double>( k % 4 );
        wall.d.distances.pData[k] = -2.0;
    }
    wall.d.noise.amplitude = 1.0;
    wall.d.noise.cOctaves = 2u;
    const displacement_quad_t qf = Floor(), qw = Wall();
    auto borderGap = [&]() {
        Grid gf, gw;
        REQUIRE( Displacement_TryEvaluate( &floor.d, qf, &gf.g ) == geometry_status_t::OK );
        REQUIRE( Displacement_TryEvaluate( &wall.d, qw, &gw.g ) == geometry_status_t::OK );
        // Floor edge x = 64 is i = 8 (j = 0..8, y = 8j); the wall's first
        // edge runs y = 64 -> 0 along i at j = 0.
        double worst = 0.0;
        for ( common::u32 t = 0; t <= 8u; ++t ) {
            const math::vec3d_t a = gf.g.positions.pData[t * 9u + 8u];
            const math::vec3d_t b = gw.g.positions.pData[8u - t];
            worst = std::fmax( worst, std::sqrt( math::Vec3d_DistanceSquared( a, b ) ) );
        }
        return worst;
    };
    CHECK( borderGap() > 1.0 );
    const double interiorBefore = floor.d.distances.pData[4u * 9u + 4u];
    REQUIRE( Displacement_TrySew( &floor.d, qf, &wall.d, qw ) == geometry_status_t::OK );
    CHECK( borderGap() < 1e-9 );
    CHECK( floor.d.distances.pData[4u * 9u + 4u] == interiorBefore );

    Disp coarse( 2u );
    CHECK( Displacement_TrySew( &floor.d, qf, &coarse.d, qw ) == geometry_status_t::UNSUPPORTED );
    Disp far( 3u );
    CHECK( Displacement_TrySew( &floor.d, qf, &far.d, Floor( 500.0 ) ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "A displacement builds as an authored mesh", "[geometry][displacement]" )
{
    Disp disp( 2u );
    disp.d.distances.pData[12] = 6.0;
    disp.d.alphas.pData[12] = 1.0;
    geometry_source_id_allocator_t ids{};
    mesh_source_t mesh{};
    geometry_material_ref_t material{};
    material.value = 33u;
    REQUIRE( Displacement_TryBuildMesh( &disp.d, Floor(), material, common::Allocator_GetSystem(), &ids, &mesh ) == geometry_status_t::OK );
    CHECK( MeshSource_Validate( &mesh, common::Allocator_GetSystem() ).fault == mesh_source_fault_t::NONE );
    CHECK( EditableMesh_VertexCount( &mesh.mesh ) == 25u );
    CHECK( EditableMesh_FaceCount( &mesh.mesh ) == 16u );
    CHECK( ids.next.value == 1u + 1u + 25u + 16u );
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, common::Allocator_GetSystem(), GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &mesh, &d ) == geometry_status_t::OK );
    bool bAlphaCarried = false;
    for ( common::usize c = 0; c < d.corners.nCount; ++c ) {
        const common::u32 v = d.corners.pData[c].iVertex;
        if ( std::fabs( d.vertices.pData[v].position.z - 6.0 ) < 1e-12 ) { bAlphaCarried = bAlphaCarried || ( d.corners.pData[c].attributes.colorRgba & 0xFFu ) == 0xFFu; }
    }
    CHECK( bAlphaCarried );
    for ( common::usize f = 0; f < d.faces.nCount; ++f ) { CHECK( d.faces.pData[f].attributes.material.value == 33u ); }
    MeshSourceDescription_Shutdown( &d );
    MeshSource_Shutdown( &mesh );
}

TEST_CASE( "Displacement inputs are validated", "[geometry][displacement]" )
{
    Disp disp( 2u );
    CHECK( Displacement_Validate( &disp.d ) == geometry_status_t::OK );
    disp.d.alphas.pData[3] = 1.5;
    CHECK( Displacement_Validate( &disp.d ) == geometry_status_t::INVALID_ARGUMENT );
    disp.d.alphas.pData[3] = 0.0;
    disp.d.distances.pData[3] = std::numeric_limits<double>::quiet_NaN();
    CHECK( Displacement_Validate( &disp.d ) == geometry_status_t::INVALID_ARGUMENT );
    disp.d.distances.pData[3] = 0.0;
    disp.d.noise.amplitude = 1.0;
    disp.d.noise.cOctaves = kDisplacementOctavesMax + 1u;
    CHECK( Displacement_Validate( &disp.d ) == geometry_status_t::INVALID_ARGUMENT );
    disp.d.noise.cOctaves = 1u;
    disp.d.bUseAxis = true;
    disp.d.axis = math::Vec3d_Make( 0, 0, 2 );
    CHECK( Displacement_Validate( &disp.d ) == geometry_status_t::INVALID_ARGUMENT );
    disp.d.bUseAxis = false;
    displacement_quad_t flat = Floor();
    flat.corners[2] = flat.corners[1];
    flat.corners[3] = flat.corners[0];
    CHECK( Displacement_ValidateQuad( flat ) == geometry_status_t::DEGENERATE );
    Grid grid;
    CHECK( Displacement_TryEvaluate( &disp.d, flat, &grid.g ) == geometry_status_t::DEGENERATE );
    CHECK( grid.g.side == 0u );
    displacement_t unset{};
    CHECK( Displacement_Validate( &unset ) == geometry_status_t::NOT_INITIALIZED );
}

namespace
{

struct fail_state_t {
    common::usize cCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cLive{ 0u };
};

void *FailAllocate( void *pUser, common::usize cb, common::usize align ) noexcept
{
    auto *p = static_cast<fail_state_t *>( pUser );
    if ( ++p->cCalls == p->iFailOnCall ) { return nullptr; }
    void *pMem = common::Allocator_Allocate( common::Allocator_GetSystem(), cb, align );
    p->cLive += pMem != nullptr ? 1u : 0u;
    return pMem;
}

void FailFree( void *pUser, void *pMem, common::usize cb, common::usize align ) noexcept
{
    auto *p = static_cast<fail_state_t *>( pUser );
    p->cLive -= pMem != nullptr ? 1u : 0u;
    common::Allocator_Free( common::Allocator_GetSystem(), pMem, cb, align );
}

} // namespace

TEST_CASE( "Displacement resample and sew are atomic under allocation failure", "[geometry][displacement][allocation]" )
{
    for ( int op = 0; op < 2; ++op ) {
        CAPTURE( op );
        auto run = [&]( displacement_t *pA, displacement_t *pB ) {
            return op == 0 ? Displacement_TryResample( pA, 4u ) : Displacement_TrySew( pA, Floor(), pB, Wall() );
        };
        auto fill = []( displacement_t *p ) {
            for ( common::usize k = 0; k < p->distances.nCount; ++k ) { p->distances.pData[k] = 0.25 * static_cast<double>( k ); }
        };
        common::usize cOperation = 0u;
        {
            fail_state_t probe{};
            common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &probe };
            displacement_t a{}, b{};
            REQUIRE( Displacement_Init( &a, &allocator, 3u ) == geometry_status_t::OK );
            REQUIRE( Displacement_Init( &b, &allocator, 3u ) == geometry_status_t::OK );
            fill( &a );
            const common::usize cStart = probe.cCalls;
            REQUIRE( run( &a, &b ) == geometry_status_t::OK );
            cOperation = probe.cCalls - cStart;
            Displacement_Shutdown( &a );
            Displacement_Shutdown( &b );
            CHECK( probe.cLive == 0u );
        }
        REQUIRE( cOperation > 0u );
        for ( common::usize i = 1; i <= cOperation; ++i ) {
            CAPTURE( i );
            fail_state_t state{};
            common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &state };
            {
                displacement_t a{}, b{};
                REQUIRE( Displacement_Init( &a, &allocator, 3u ) == geometry_status_t::OK );
                REQUIRE( Displacement_Init( &b, &allocator, 3u ) == geometry_status_t::OK );
                fill( &a );
                state.iFailOnCall = state.cCalls + i;
                CHECK( run( &a, &b ) == geometry_status_t::ALLOCATION_FAILED );
                state.iFailOnCall = common::CY_INVALID_SIZE;
                CHECK( a.power == 3u );
                bool bSame = a.distances.nCount == 81u;
                for ( common::usize k = 0; bSame && k < a.distances.nCount; ++k ) { bSame = a.distances.pData[k] == 0.25 * static_cast<double>( k ); }
                CHECK( bSame );
                Displacement_Shutdown( &a );
                Displacement_Shutdown( &b );
            }
            CHECK( state.cLive == 0u );
        }
    }
}

} // namespace cypher::editor::geometry
