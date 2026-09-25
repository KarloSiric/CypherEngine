//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Primitive_Tests.cpp
//  Purpose: Tests the primitive creation front end: every kind in every
//           output it supports, closed outward-facing meshes that stay in
//           their box, brush/mesh agreement, parameter boundaries, and
//           failure atomicity of the fragment and the ID allocator.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_Primitive.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

using kind_t = geometry_primitive_kind_t;
using output_t = geometry_primitive_output_t;

const kind_t kAllKinds[] = { kind_t::BOX,  kind_t::RAMP,  kind_t::PYRAMID, kind_t::CYLINDER, kind_t::CONE,
                             kind_t::SPHERE, kind_t::TORUS, kind_t::PLANE,   kind_t::ARCH,     kind_t::STAIRS };

// A record that is valid for every kind: the box is wide enough for a
// torus (footprint more than twice the height, and square for patches).
geometry_primitive_t Make( kind_t kind )
{
    geometry_primitive_t p{};
    p.kind = kind;
    p.box.lo = math::Vec3d_Make( -64, -64, 0 );
    p.box.hi = math::Vec3d_Make( 64, 64, 48 );
    p.cSides = 16u;
    p.cRings = 8u;
    p.archThickness = 16.0;
    p.stepHeight = 8.0;
    p.material.value = 7u;
    p.worldUnitsPerUv = math::vec2d_t{ 32.0, 32.0 };
    if ( kind == kind_t::ARCH ) { p.axis = 0u; }
    return p;
}

bool Supports( kind_t k, output_t o )
{
    if ( o == output_t::MESH ) { return true; }
    if ( o == output_t::BRUSHES ) { return k != kind_t::TORUS && k != kind_t::PLANE; }
    return k == kind_t::CYLINDER || k == kind_t::CONE || k == kind_t::SPHERE || k == kind_t::TORUS || k == kind_t::PLANE;
}

struct mesh_facts_t {
    double volume6{ 0.0 };
    bool bInBox{ true };
    common::usize cFaces{ 0u }, cVertices{ 0u };
    bool bAllMaterial{ true };
};

mesh_facts_t FactsOf( const mesh_source_t &mesh, const geometry_primitive_t &p )
{
    mesh_facts_t f{};
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, common::Allocator_GetSystem(), GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &mesh, &d ) == geometry_status_t::OK );
    f.cFaces = d.faces.nCount;
    f.cVertices = d.vertices.nCount;
    const double eps = 1e-9;
    for ( common::usize i = 0; i < d.vertices.nCount; ++i ) {
        const math::vec3d_t q = d.vertices.pData[i].position;
        f.bInBox = f.bInBox && q.x >= p.box.lo.x - eps && q.x <= p.box.hi.x + eps && q.y >= p.box.lo.y - eps && q.y <= p.box.hi.y + eps &&
                   q.z >= p.box.lo.z - eps && q.z <= p.box.hi.z + eps;
    }
    for ( common::usize fi = 0; fi < d.faces.nCount; ++fi ) {
        const mesh_source_face_t &face = d.faces.pData[fi];
        f.bAllMaterial = f.bAllMaterial && face.attributes.material.value == p.material.value;
        const math::vec3d_t p0 = d.vertices.pData[d.corners.pData[face.iFirstCorner].iVertex].position;
        for ( common::u32 k = 1; k + 1 < face.cCorners; ++k ) {
            const math::vec3d_t p1 = d.vertices.pData[d.corners.pData[face.iFirstCorner + k].iVertex].position;
            const math::vec3d_t p2 = d.vertices.pData[d.corners.pData[face.iFirstCorner + k + 1].iVertex].position;
            f.volume6 += math::Vec3d_Dot( p0, math::Vec3d_Cross( p1, p2 ) );
        }
    }
    MeshSourceDescription_Shutdown( &d );
    return f;
}

double BoxVolume( const geometry_primitive_t &p )
{
    return ( p.box.hi.x - p.box.lo.x ) * ( p.box.hi.y - p.box.lo.y ) * ( p.box.hi.z - p.box.lo.z );
}

} // namespace

TEST_CASE( "Every primitive builds in every output it supports and nothing else", "[geometry][primitives]" )
{
    const geometry_policy_t policy{};
    for ( const kind_t kind : kAllKinds ) {
        for ( const output_t output : { output_t::BRUSHES, output_t::MESH, output_t::PATCHES } ) {
            CAPTURE( static_cast<int>( kind ), static_cast<int>( output ) );
            const geometry_primitive_t p = Make( kind );
            geometry_fragment_t frag{};
            REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
            geometry_source_id_allocator_t ids{};
            const geometry_status_t st = Primitive_TryBuild( p, output, policy, &ids, &frag );
            if ( !Supports( kind, output ) ) {
                CHECK( st == geometry_status_t::UNSUPPORTED );
                CHECK( GeometryFragment_ObjectCount( &frag ) == 0u );
                CHECK( ids.next.value == 1u );
                GeometryFragment_Shutdown( &frag );
                continue;
            }
            REQUIRE( st == geometry_status_t::OK );
            CHECK( ids.next.value > 1u );
            if ( output == output_t::BRUSHES ) {
                REQUIRE( frag.brushes.nCount >= 1u );
                const common::usize cExpected = kind == kind_t::STAIRS ? 6u : kind == kind_t::ARCH ? frag.brushes.nCount : 1u;
                CHECK( frag.brushes.nCount == cExpected );
                for ( common::usize i = 0; i < frag.brushes.nCount; ++i ) {
                    CHECK( BrushSource_Validate( frag.brushes.pData[i], policy ).fault == brush_source_fault_t::NONE );
                    geometry_brush_side_attributes_t r{};
                    REQUIRE( BrushSideAttributeStore_TryGet( &frag.brushes.pData[i]->attributes, 0u, &r ) == geometry_status_t::OK );
                    CHECK( r.material.value == 7u );
                    CHECK( r.uvProjection.worldUnitsPerUv.x == 32.0 );
                }
            } else if ( output == output_t::MESH ) {
                REQUIRE( frag.meshes.nCount == 1u );
                const mesh_source_t &mesh = *frag.meshes.pData[0];
                CHECK( MeshSource_Validate( &mesh, common::Allocator_GetSystem() ).fault == mesh_source_fault_t::NONE );
                const mesh_facts_t f = FactsOf( mesh, p );
                CHECK( f.bInBox );
                CHECK( f.bAllMaterial );
                if ( kind != kind_t::PLANE ) {
                    // Closed and outward: positive volume, never more than the box.
                    CHECK( f.volume6 > 0.0 );
                    CHECK( f.volume6 / 6.0 <= BoxVolume( p ) * ( 1.0 + 1e-12 ) );
                }
            } else {
                REQUIRE( frag.patches.nCount == 1u );
                CHECK( frag.patches.pData[0]->materialId == 7u );
            }
            GeometryFragment_Shutdown( &frag );
        }
    }
}

TEST_CASE( "Mesh primitives have the expected shape", "[geometry][primitives]" )
{
    const geometry_policy_t policy{};
    auto build = [&]( const geometry_primitive_t &p, mesh_facts_t *pFacts ) {
        geometry_fragment_t frag{};
        REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
        geometry_source_id_allocator_t ids{};
        REQUIRE( Primitive_TryBuild( p, output_t::MESH, policy, &ids, &frag ) == geometry_status_t::OK );
        *pFacts = FactsOf( *frag.meshes.pData[0], p );
        GeometryFragment_Shutdown( &frag );
    };
    mesh_facts_t f{};

    // A box is exactly its box; subdivision shares the edge vertices.
    geometry_primitive_t box = Make( kind_t::BOX );
    build( box, &f );
    CHECK( f.cFaces == 6u );
    CHECK( f.cVertices == 8u );
    CHECK( std::fabs( f.volume6 / 6.0 - BoxVolume( box ) ) < 1e-6 );
    box.cSegments[0] = 2u;
    box.cSegments[1] = 3u;
    box.cSegments[2] = 4u;
    build( box, &f );
    CHECK( f.cFaces == 2u * ( 2u * 3u + 3u * 4u + 2u * 4u ) );
    CHECK( f.cVertices == 2u * ( 2u * 3u + 3u * 4u + 2u * 4u ) + 2u ); // Euler: V = F + 2 for a quad sphere
    CHECK( std::fabs( f.volume6 / 6.0 - BoxVolume( box ) ) < 1e-6 );

    // Plane: an open grid.
    geometry_primitive_t plane = Make( kind_t::PLANE );
    plane.box.hi.z = 0.0; // flat along its axis is allowed for planes
    plane.cSegments[0] = 4u;
    plane.cSegments[1] = 2u;
    build( plane, &f );
    CHECK( f.cFaces == 8u );
    CHECK( f.cVertices == 15u );

    // Ramp: half the box. Pyramid: a third. Stairs: 6 steps of 8 in 48.
    build( Make( kind_t::RAMP ), &f );
    CHECK( std::fabs( f.volume6 / 6.0 - 0.5 * BoxVolume( box ) ) < 1e-6 );
    CHECK( f.cFaces == 5u );
    build( Make( kind_t::PYRAMID ), &f );
    CHECK( std::fabs( f.volume6 / 6.0 - BoxVolume( box ) / 3.0 ) < 1e-6 );
    CHECK( f.cFaces == 5u );
    const geometry_primitive_t stairs = Make( kind_t::STAIRS );
    build( stairs, &f );
    // Steps 1..6 of height 8, each 128/6 deep, 128 wide.
    CHECK( std::fabs( f.volume6 / 6.0 - ( 128.0 / 6.0 ) * 128.0 * 8.0 * ( 1 + 2 + 3 + 4 + 5 + 6 ) ) < 1e-6 );
    CHECK( f.cFaces == 2u + 2u * 6u + 2u ); // two sides, a tread and riser per step, floor and back

    // Cylinder: 16 sides, 3 bands -> 48 side quads + 2 caps.
    geometry_primitive_t cyl = Make( kind_t::CYLINDER );
    cyl.cSegments[2] = 3u;
    build( cyl, &f );
    CHECK( f.cFaces == 16u * 3u + 2u );
    CHECK( f.cVertices == 16u * 4u );
    // Sphere: 16 x 8 -> 16 * 7 ring vertices + 2 poles.
    build( Make( kind_t::SPHERE ), &f );
    CHECK( f.cVertices == 16u * 7u + 2u );
    CHECK( f.cFaces == 16u * 8u );
    // Torus: 16 x 8 quads on 128 vertices.
    geometry_primitive_t torus = Make( kind_t::TORUS );
    torus.box.hi.z = 32.0;
    build( torus, &f );
    CHECK( f.cVertices == 16u * 8u );
    CHECK( f.cFaces == 16u * 8u );
    // Arch: 8 segments -> 8 front + 8 back + 8 outer + 8 inner + 2 feet.
    build( Make( kind_t::ARCH ), &f );
    CHECK( f.cFaces == 8u * 4u + 2u );
}

TEST_CASE( "Every direction and axis builds a closed outward mesh", "[geometry][primitives]" )
{
    const geometry_policy_t policy{};
    for ( const kind_t kind : { kind_t::RAMP, kind_t::STAIRS } ) {
        for ( const brush_stairs_direction_t dir : { brush_stairs_direction_t::POS_X, brush_stairs_direction_t::NEG_X, brush_stairs_direction_t::POS_Y,
                                                     brush_stairs_direction_t::NEG_Y } ) {
            CAPTURE( static_cast<int>( kind ), static_cast<int>( dir ) );
            geometry_primitive_t p = Make( kind );
            p.stairsDirection = dir;
            for ( const output_t o : { output_t::MESH, output_t::BRUSHES } ) {
                geometry_fragment_t frag{};
                REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
                geometry_source_id_allocator_t ids{};
                REQUIRE( Primitive_TryBuild( p, o, policy, &ids, &frag ) == geometry_status_t::OK );
                if ( o == output_t::MESH ) { CHECK( FactsOf( *frag.meshes.pData[0], p ).volume6 > 0.0 ); }
                GeometryFragment_Shutdown( &frag );
            }
        }
    }
    for ( const kind_t kind : { kind_t::CYLINDER, kind_t::CONE, kind_t::PYRAMID, kind_t::SPHERE, kind_t::PLANE } ) {
        for ( common::u32 axis = 0; axis < 3u; ++axis ) {
            CAPTURE( static_cast<int>( kind ), axis );
            geometry_primitive_t p = Make( kind );
            p.axis = axis;
            p.box.lo = math::Vec3d_Make( -40, -50, -60 );
            p.box.hi = math::Vec3d_Make( 40, 50, 60 );
            geometry_fragment_t frag{};
            REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
            geometry_source_id_allocator_t ids{};
            REQUIRE( Primitive_TryBuild( p, output_t::MESH, policy, &ids, &frag ) == geometry_status_t::OK );
            const mesh_facts_t f = FactsOf( *frag.meshes.pData[0], p );
            CHECK( f.bInBox );
            if ( kind != kind_t::PLANE ) { CHECK( f.volume6 > 0.0 ); }
            GeometryFragment_Shutdown( &frag );
        }
    }
}

TEST_CASE( "A brush sphere and a mesh sphere from one record share their vertices", "[geometry][primitives]" )
{
    const geometry_policy_t policy{};
    const geometry_primitive_t p = Make( kind_t::SPHERE );
    geometry_fragment_t frag{};
    REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    geometry_source_id_allocator_t ids{};
    REQUIRE( Primitive_TryBuild( p, output_t::BRUSHES, policy, &ids, &frag ) == geometry_status_t::OK );
    REQUIRE( Primitive_TryBuild( p, output_t::MESH, policy, &ids, &frag ) == geometry_status_t::OK );
    // One brush side per mesh face (the hull merges each planar quad).
    mesh_facts_t f = FactsOf( *frag.meshes.pData[0], p );
    CHECK( frag.brushes.pData[0]->solid.sides.nCount == f.cFaces );
    GeometryFragment_Shutdown( &frag );
}

TEST_CASE( "Primitive parameters are checked at their boundaries", "[geometry][primitives]" )
{
    auto check = [&]( geometry_primitive_t p, output_t o, geometry_status_t expected ) {
        CAPTURE( static_cast<int>( p.kind ), static_cast<int>( o ) );
        CHECK( Primitive_Validate( p, o ) == expected );
    };
    geometry_primitive_t p = Make( kind_t::BOX );
    check( p, output_t::MESH, geometry_status_t::OK );
    p.box.hi.x = p.box.lo.x;
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );
    p = Make( kind_t::PLANE );
    p.box.hi.z = p.box.lo.z;
    check( p, output_t::MESH, geometry_status_t::OK );
    p.box.hi.x = p.box.lo.x;
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );
    p = Make( kind_t::BOX );
    p.cSegments[1] = 0u;
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );
    p.cSegments[1] = kPrimitiveSegmentsMax;
    check( p, output_t::MESH, geometry_status_t::OK );
    p.cSegments[1] = kPrimitiveSegmentsMax + 1u;
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );
    p = Make( kind_t::BOX );
    p.worldUnitsPerUv.x = 0.0;
    check( p, output_t::BRUSHES, geometry_status_t::INVALID_ARGUMENT );
    p = Make( kind_t::BOX );
    p.axis = 3u;
    check( p, output_t::BRUSHES, geometry_status_t::INVALID_ARGUMENT );

    p = Make( kind_t::CYLINDER );
    p.cSides = 2u;
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );
    p.cSides = 3u;
    check( p, output_t::MESH, geometry_status_t::OK );
    p.circleMode = brush_circle_mode_t::EDGE_ALIGNED; // needs a multiple of 4
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );
    p.cSides = kBrushCircleSidesMax;
    check( p, output_t::MESH, geometry_status_t::OK );
    p.cSides = 256u; // patches allow at most kPatchPrimitiveSegmentsMax arcs
    check( p, output_t::PATCHES, geometry_status_t::INVALID_ARGUMENT );

    p = Make( kind_t::SPHERE );
    p.cRings = 1u;
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );
    p.cRings = kPrimitiveRingsMax + 1u;
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );

    p = Make( kind_t::TORUS );
    p.box.hi.z = 64.0; // height 64 is not less than half of 128
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );
    p = Make( kind_t::TORUS );
    p.box.hi.y = 100.0; // oval footprint: fine as a mesh, not as a patch
    check( p, output_t::MESH, geometry_status_t::OK );
    check( p, output_t::PATCHES, geometry_status_t::INVALID_ARGUMENT );

    p = Make( kind_t::ARCH );
    p.archThickness = 64.0; // not thinner than half the 128 width
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );
    p = Make( kind_t::ARCH );
    p.axis = 2u;
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );
    p = Make( kind_t::ARCH );
    p.cSides = 15u;
    check( p, output_t::MESH, geometry_status_t::INVALID_ARGUMENT );

    p = Make( kind_t::STAIRS );
    p.stepHeight = 0.0;
    check( p, output_t::BRUSHES, geometry_status_t::INVALID_ARGUMENT );
    p.stepHeight = 48.0 / 127.0 + 1e-9; // 127 steps: 256 side corners, the face maximum
    check( p, output_t::MESH, geometry_status_t::OK );
    p.stepHeight = 48.0 / 200.0;
    check( p, output_t::MESH, geometry_status_t::LIMIT_EXCEEDED );
    check( p, output_t::BRUSHES, geometry_status_t::OK );
    p.stepHeight = 48.0 / 300.0;
    check( p, output_t::BRUSHES, geometry_status_t::LIMIT_EXCEEDED );

    p = Make( kind_t::CYLINDER );
    p.material.value = 1ull << 40; // patches carry 32-bit material IDs
    check( p, output_t::PATCHES, geometry_status_t::INVALID_ARGUMENT );
    check( p, output_t::MESH, geometry_status_t::OK );
}

TEST_CASE( "A primitive created into a document registers fresh identities", "[geometry][primitives]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};
    geometry_document_t doc{};
    REQUIRE( GeometryDocument_Init( &doc, &allocator, policy ) == geometry_status_t::OK );
    for ( const output_t o : { output_t::BRUSHES, output_t::MESH, output_t::PATCHES } ) {
        for ( int copy = 0; copy < 2; ++copy ) {
            geometry_fragment_t frag{};
            REQUIRE( GeometryFragment_Init( &frag, &allocator ) == geometry_status_t::OK );
            geometry_source_id_allocator_t ids = doc.sourceIds.allocator;
            REQUIRE( Primitive_TryBuild( Make( kind_t::CYLINDER ), o, doc.policy, &ids, &frag ) == geometry_status_t::OK );
            REQUIRE( GeometryFragment_TryInsert( &frag, &doc, nullptr ) == geometry_status_t::OK );
            CHECK( doc.sourceIds.allocator.next.value == ids.next.value );
            GeometryFragment_Shutdown( &frag );
        }
    }
    CHECK( doc.brushes.nCount == 2u );
    CHECK( GeometryDocument_MeshCount( &doc ) == 2u );
    CHECK( GeometryDocument_PatchCount( &doc ) == 2u );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &doc.sourceIds ) );
    CHECK( GeometryDocument_ValidateBrushAttributes( &doc ) == geometry_status_t::OK );
    GeometryDocument_Shutdown( &doc );
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

TEST_CASE( "Every primitive allocation failure leaves the fragment and allocator unchanged", "[geometry][primitives][allocation]" )
{
    const geometry_policy_t policy{};
    struct case_t {
        kind_t kind;
        output_t output;
    };
    const case_t cases[] = { { kind_t::STAIRS, output_t::BRUSHES }, { kind_t::SPHERE, output_t::BRUSHES }, { kind_t::ARCH, output_t::MESH },
                             { kind_t::TORUS, output_t::MESH },     { kind_t::SPHERE, output_t::PATCHES } };
    for ( const case_t &c : cases ) {
        CAPTURE( static_cast<int>( c.kind ), static_cast<int>( c.output ) );
        geometry_primitive_t p = Make( c.kind );
        if ( c.kind == kind_t::TORUS ) { p.box.hi.z = 32.0; }
        p.cSides = 8u;
        p.cRings = 4u;
        common::usize cOperation = 0u;
        {
            fail_state_t probe{};
            common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &probe };
            geometry_fragment_t frag{};
            REQUIRE( GeometryFragment_Init( &frag, &allocator ) == geometry_status_t::OK );
            geometry_source_id_allocator_t ids{};
            const common::usize cStart = probe.cCalls;
            REQUIRE( Primitive_TryBuild( p, c.output, policy, &ids, &frag ) == geometry_status_t::OK );
            cOperation = probe.cCalls - cStart;
            GeometryFragment_Shutdown( &frag );
            CHECK( probe.cLive == 0u );
        }
        REQUIRE( cOperation > 0u );
        for ( common::usize i = 1; i <= cOperation; ++i ) {
            CAPTURE( i );
            fail_state_t state{};
            common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &state };
            {
                geometry_fragment_t frag{};
                REQUIRE( GeometryFragment_Init( &frag, &allocator ) == geometry_status_t::OK );
                geometry_source_id_allocator_t ids{};
                state.iFailOnCall = state.cCalls + i;
                CHECK( Primitive_TryBuild( p, c.output, policy, &ids, &frag ) == geometry_status_t::ALLOCATION_FAILED );
                state.iFailOnCall = common::CY_INVALID_SIZE;
                CHECK( GeometryFragment_ObjectCount( &frag ) == 0u );
                CHECK( ids.next.value == 1u );
                GeometryFragment_Shutdown( &frag );
            }
            CHECK( state.cLive == 0u );
        }
    }
}

} // namespace cypher::editor::geometry
