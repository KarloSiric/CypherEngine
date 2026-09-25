//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgMesh_Tests.cpp
//  Purpose: Tests the general mesh Boolean end to end against analytic
//           volumes: overlapping, contained, disjoint, face-touching,
//           shared-plane, rotated and non-convex operands, plus attribute
//           transfer, identity, seams, determinism and diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgMesh.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_Primitive.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

using op_t = csg_operator_t;

geometry_source_id_t Id( common::u64 v )
{
    geometry_source_id_t id{};
    id.value = v;
    return id;
}

// A box [lo, hi] (optionally turned by `turn` radians about the vertical
// line through its centre), material `material`, corner UV0 = (x + y, z)
// so every face's UVs are a linear function of position.
struct Box {
    mesh_source_t mesh{};
    Box( math::vec3d_t lo, math::vec3d_t hi, common::u64 material, common::u64 idBase, double turn = 0.0, bool bReverseFaces = false )
    {
        mesh_source_description_t d{};
        REQUIRE( MeshSourceDescription_Init( &d, common::Allocator_GetSystem(), Id( idBase ) ) == geometry_status_t::OK );
        const double cx = 0.5 * ( lo.x + hi.x ), cy = 0.5 * ( lo.y + hi.y );
        for ( int i = 0; i < 8; ++i ) {
            double x = ( i & 1 ) ? hi.x : lo.x, y = ( i & 2 ) ? hi.y : lo.y;
            const double z = ( i & 4 ) ? hi.z : lo.z;
            if ( turn != 0.0 ) {
                const double dx = x - cx, dy = y - cy;
                x = cx + dx * std::cos( turn ) - dy * std::sin( turn );
                y = cy + dx * std::sin( turn ) + dy * std::cos( turn );
            }
            REQUIRE( MeshSourceDescription_TryAddVertex( &d, math::Vec3d_Make( x, y, z ), Id( idBase + 1 + i ), nullptr ) == geometry_status_t::OK );
        }
        std::vector<std::vector<common::u32>> faces = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        if ( bReverseFaces ) { std::reverse( faces.begin(), faces.end() ); }
        for ( common::usize f = 0; f < faces.size(); ++f ) {
            mesh_face_attributes_t a{};
            a.material.value = material;
            common::u32 iFace = 0;
            REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ faces[f].data(), faces[f].size() }, Id( idBase + 20 + f ), a,
                                                       &iFace ) == geometry_status_t::OK );
            const mesh_source_face_t &face = d.faces.pData[iFace];
            for ( common::u32 c = 0; c < face.cCorners; ++c ) {
                const math::vec3d_t p = d.vertices.pData[d.corners.pData[face.iFirstCorner + c].iVertex].position;
                d.corners.pData[face.iFirstCorner + c].attributes.uv0 = math::vec2d_t{ p.x + p.y, p.z };
            }
        }
        REQUIRE( MeshSource_TryBuild( &d, common::Allocator_GetSystem(), &mesh ) == geometry_status_t::OK );
        MeshSourceDescription_Shutdown( &d );
    }
    ~Box() { MeshSource_Shutdown( &mesh ); }
};

struct Result {
    csg_mesh_result_t r{};
    Result() { REQUIRE( CsgResult_Init( &r, common::Allocator_GetSystem() ) == geometry_status_t::OK ); }
    ~Result() { CsgResult_Shutdown( &r ); }
};

double Volume( const mesh_source_description_t &d )
{
    double v = 0.0;
    for ( common::usize f = 0; f < d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = d.faces.pData[f];
        const math::vec3d_t p0 = d.vertices.pData[d.corners.pData[face.iFirstCorner].iVertex].position;
        for ( common::u32 k = 1; k + 1 < face.cCorners; ++k ) {
            const math::vec3d_t p1 = d.vertices.pData[d.corners.pData[face.iFirstCorner + k].iVertex].position;
            const math::vec3d_t p2 = d.vertices.pData[d.corners.pData[face.iFirstCorner + k + 1].iVertex].position;
            v += math::Vec3d_Dot( p0, math::Vec3d_Cross( p1, p2 ) );
        }
    }
    return v / 6.0;
}

// Evaluates, checks the result builds as a valid closed authored mesh,
// and returns its volume.
double Run( const mesh_source_t &a, const mesh_source_t &b, op_t op, common::usize *pcFacesOut = nullptr )
{
    CAPTURE( static_cast<int>( op ) );
    csg_mesh_options_t o{};
    o.op = op;
    Result res;
    csg_diagnostics_t diag{};
    const geometry_status_t st = CsgMesh_TryEvaluate( &a, &b, o, &res.r, &diag );
    CAPTURE( static_cast<int>( st ), static_cast<int>( diag.stage ), diag.witness.x, diag.witness.y, diag.witness.z );
    REQUIRE( st == geometry_status_t::OK );
    if ( pcFacesOut != nullptr ) { *pcFacesOut = res.r.mesh.faces.nCount; }
    if ( res.r.mesh.faces.nCount == 0u ) { return 0.0; }
    geometry_source_id_allocator_t ids{};
    ids.next.value = 100000u;
    mesh_source_t out{};
    const geometry_status_t bst = CsgMesh_TryBuild( &a, &b, o, common::Allocator_GetSystem(), &ids, &out, &diag );
    CAPTURE( static_cast<int>( bst ) );
    REQUIRE( bst == geometry_status_t::OK );
    CHECK( MeshSource_Validate( &out, common::Allocator_GetSystem() ).fault == mesh_source_fault_t::NONE );
    CHECK( MeshBoundary_CountBoundaryEdges( &out.mesh ) == 0u );
    MeshSource_Shutdown( &out );
    return Volume( res.r.mesh );
}

bool Near( double a, double b ) { return std::fabs( a - b ) < 1e-9 * ( 1.0 + std::fabs( b ) ); }

} // namespace

TEST_CASE( "Overlapping boxes: union, intersection and difference volumes", "[geometry][csg]" )
{
    Box a( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 2, 2, 2 ), 1u, 1000u );
    Box b( math::Vec3d_Make( 1, 1, 1 ), math::Vec3d_Make( 3, 3, 3 ), 2u, 2000u );
    CHECK( Near( Run( a.mesh, b.mesh, op_t::UNION ), 15.0 ) );
    CHECK( Near( Run( a.mesh, b.mesh, op_t::INTERSECTION ), 1.0 ) );
    CHECK( Near( Run( a.mesh, b.mesh, op_t::DIFFERENCE ), 7.0 ) );
    CHECK( Near( Run( b.mesh, a.mesh, op_t::DIFFERENCE ), 7.0 ) );
    // The intersection comes back as one box of six quads (faces merged back).
    common::usize cFaces = 0;
    (void)Run( a.mesh, b.mesh, op_t::INTERSECTION, &cFaces );
    CHECK( cFaces == 6u );
}

TEST_CASE( "Contained, disjoint and face-touching operands", "[geometry][csg]" )
{
    Box big( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 4, 4, 4 ), 1u, 1000u );
    Box small( math::Vec3d_Make( 1, 1, 1 ), math::Vec3d_Make( 2, 2, 2 ), 2u, 2000u );
    CHECK( Near( Run( big.mesh, small.mesh, op_t::DIFFERENCE ), 63.0 ) ); // a cavity: two shells
    CHECK( Near( Run( big.mesh, small.mesh, op_t::UNION ), 64.0 ) );
    CHECK( Near( Run( big.mesh, small.mesh, op_t::INTERSECTION ), 1.0 ) );

    Box far( math::Vec3d_Make( 10, 0, 0 ), math::Vec3d_Make( 11, 1, 1 ), 2u, 3000u );
    Box unit( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 1, 1, 1 ), 1u, 4000u );
    CHECK( Near( Run( unit.mesh, far.mesh, op_t::UNION ), 2.0 ) );
    common::usize cFaces = 99;
    CHECK( Run( unit.mesh, far.mesh, op_t::INTERSECTION, &cFaces ) == 0.0 );
    CHECK( cFaces == 0u );
    CHECK( Near( Run( unit.mesh, far.mesh, op_t::DIFFERENCE ), 1.0 ) );

    // Touching face to face: the union merges into one solid (the shared
    // faces face opposite ways and both go), the intersection is empty.
    Box next( math::Vec3d_Make( 1, 0, 0 ), math::Vec3d_Make( 2, 1, 1 ), 2u, 5000u );
    CHECK( Near( Run( unit.mesh, next.mesh, op_t::UNION ), 2.0 ) );
    cFaces = 99;
    CHECK( Run( unit.mesh, next.mesh, op_t::INTERSECTION, &cFaces ) == 0.0 );
    CHECK( cFaces == 0u );
    CHECK( Near( Run( unit.mesh, next.mesh, op_t::DIFFERENCE ), 1.0 ) );
}

TEST_CASE( "Flush faces on shared planes resolve exactly", "[geometry][csg]" )
{
    Box a( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 2, 2, 2 ), 1u, 1000u );
    Box b( math::Vec3d_Make( 1, 0, 0 ), math::Vec3d_Make( 3, 2, 2 ), 2u, 2000u );
    CHECK( Near( Run( a.mesh, b.mesh, op_t::UNION ), 12.0 ) );
    CHECK( Near( Run( a.mesh, b.mesh, op_t::INTERSECTION ), 4.0 ) );
    CHECK( Near( Run( a.mesh, b.mesh, op_t::DIFFERENCE ), 4.0 ) );
    // Identical operands.
    Box same( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 2, 2, 2 ), 2u, 3000u );
    CHECK( Near( Run( a.mesh, same.mesh, op_t::UNION ), 8.0 ) );
    CHECK( Near( Run( a.mesh, same.mesh, op_t::INTERSECTION ), 8.0 ) );
    common::usize cFaces = 99;
    CHECK( Run( a.mesh, same.mesh, op_t::DIFFERENCE, &cFaces ) == 0.0 );
    CHECK( cFaces == 0u );
}

TEST_CASE( "A rotated operand cuts along slanted planes", "[geometry][csg]" )
{
    Box a( math::Vec3d_Make( -1, -1, -1 ), math::Vec3d_Make( 1, 1, 1 ), 1u, 1000u );
    Box b( math::Vec3d_Make( -1, -1, -1 ), math::Vec3d_Make( 1, 1, 1 ), 2u, 2000u, math::CY_PI_D / 4.0 );
    // Two squares of side 2, one turned 45 degrees, overlap in a regular
    // octagon of area 8 (sqrt 2 - 1); the prisms share the full height 2.
    const double octagon = 8.0 * ( std::sqrt( 2.0 ) - 1.0 ) * 2.0;
    CHECK( std::fabs( Run( a.mesh, b.mesh, op_t::INTERSECTION ) - octagon ) < 1e-9 );
    CHECK( std::fabs( Run( a.mesh, b.mesh, op_t::DIFFERENCE ) - ( 8.0 - octagon ) ) < 1e-9 );
    CHECK( std::fabs( Run( a.mesh, b.mesh, op_t::UNION ) - ( 16.0 - octagon ) ) < 1e-9 );
    // A turned box straddling a corner of the other in all three axes.
    Box c( math::Vec3d_Make( 0.3, 0.2, 0.1 ), math::Vec3d_Make( 1.7, 1.9, 1.6 ), 2u, 3000u, 0.3 );
    const double inter = Run( a.mesh, c.mesh, op_t::INTERSECTION );
    const double diff = Run( a.mesh, c.mesh, op_t::DIFFERENCE );
    const double uni = Run( a.mesh, c.mesh, op_t::UNION );
    const double volC = 1.4 * 1.7 * 1.5;
    CHECK( std::fabs( inter + diff - 8.0 ) < 1e-9 );
    CHECK( std::fabs( uni - ( 8.0 + volC - inter ) ) < 1e-9 );
    CHECK( inter > 0.0 );
}

TEST_CASE( "Non-convex operands: an L-shape cut by a pillar", "[geometry][csg]" )
{
    Box arm1( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 3, 1, 1 ), 1u, 1000u );
    Box arm2( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 1, 3, 1 ), 1u, 2000u );
    // Build the L as an authored mesh first (union), then use it.
    csg_mesh_options_t o{};
    o.op = op_t::UNION;
    geometry_source_id_allocator_t ids{};
    ids.next.value = 50000u;
    mesh_source_t ell{};
    csg_diagnostics_t diag{};
    REQUIRE( CsgMesh_TryBuild( &arm1.mesh, &arm2.mesh, o, common::Allocator_GetSystem(), &ids, &ell, &diag ) == geometry_status_t::OK );
    Box pillar( math::Vec3d_Make( 0.5, 0.5, -1 ), math::Vec3d_Make( 2, 2, 2 ), 2u, 3000u );
    CHECK( Near( Run( ell, pillar.mesh, op_t::INTERSECTION ), 1.25 ) );
    CHECK( Near( Run( ell, pillar.mesh, op_t::DIFFERENCE ), 3.75 ) );
    CHECK( Near( Run( ell, pillar.mesh, op_t::UNION ), 10.5 ) );
    MeshSource_Shutdown( &ell );
}

TEST_CASE( "Attributes, identity, seams and determinism carry through", "[geometry][csg]" )
{
    Box a( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 2, 2, 2 ), 1u, 1000u );
    Box b( math::Vec3d_Make( 1, 1, 1 ), math::Vec3d_Make( 3, 3, 3 ), 2u, 2000u );
    csg_mesh_options_t o{};
    o.op = op_t::DIFFERENCE;
    o.attributes.bOverrideCutMaterial = true;
    o.attributes.cutMaterial.value = 7u;
    Result res;
    REQUIRE( CsgMesh_TryEvaluate( &a.mesh, &b.mesh, o, &res.r, nullptr ) == geometry_status_t::OK );
    const mesh_source_description_t &d = res.r.mesh;
    bool bCut = false, bSeam = false;
    for ( common::usize f = 0; f < d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = d.faces.pData[f];
        if ( res.r.faceFlipped.pData[f] ) {
            bCut = true;
            CHECK( face.attributes.material.value == 7u );
            CHECK( res.r.faceOperand.pData[f] == kCsgOperandB );
        } else {
            CHECK( face.attributes.material.value == 1u );
        }
        // UVs were linear in position on every source face, so the
        // interpolated ones must still be exactly that function.
        for ( common::u32 c = 0; c < face.cCorners; ++c ) {
            const mesh_source_corner_t &corner = d.corners.pData[face.iFirstCorner + c];
            const math::vec3d_t p = d.vertices.pData[corner.iVertex].position;
            CHECK( std::fabs( corner.attributes.uv0.x - ( p.x + p.y ) ) < 1e-9 );
            CHECK( std::fabs( corner.attributes.uv0.y - p.z ) < 1e-9 );
        }
    }
    for ( common::usize e = 0; e < d.edges.nCount; ++e ) { bSeam = bSeam || ( d.edges.pData[e].attributes.flags & MESH_EDGE_FLAG_HARD ) != 0u; }
    CHECK( bCut );
    CHECK( bSeam );
    // A's seven corners outside B keep their IDs; B's corner inside A too.
    common::usize cA = 0, cB = 0;
    for ( common::usize v = 0; v < d.vertices.nCount; ++v ) {
        const common::u64 id = d.vertices.pData[v].sourceId.value;
        cA += id > 1000u && id <= 1008u ? 1u : 0u;
        cB += id > 2000u && id <= 2008u ? 1u : 0u;
    }
    CHECK( cA == 7u );
    CHECK( cB == 1u );
    CHECK( d.sourceId.value == 1000u );

    // The same operands with their faces listed in another order give the
    // same result.
    Box aPermuted( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 2, 2, 2 ), 1u, 1000u, 0.0, true );
    Result res2;
    REQUIRE( CsgMesh_TryEvaluate( &aPermuted.mesh, &b.mesh, o, &res2.r, nullptr ) == geometry_status_t::OK );
    CHECK( res2.r.mesh.faces.nCount == d.faces.nCount );
    CHECK( res2.r.mesh.vertices.nCount == d.vertices.nCount );
    CHECK( Near( Volume( res2.r.mesh ), Volume( d ) ) );
}

TEST_CASE( "CSG failures are atomic and explained", "[geometry][csg]" )
{
    Box a( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 2, 2, 2 ), 1u, 1000u );
    // An open operand (one face missing) cannot bound a volume.
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, common::Allocator_GetSystem(), GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &a.mesh, &d ) == geometry_status_t::OK );
    // Drop the last face and its corners (every vertex is still on another face).
    d.faces.nCount -= 1u;
    d.corners.nCount = d.faces.pData[d.faces.nCount - 1u].iFirstCorner + d.faces.pData[d.faces.nCount - 1u].cCorners;
    mesh_source_t open{};
    REQUIRE( MeshSource_TryBuild( &d, common::Allocator_GetSystem(), &open ) == geometry_status_t::OK );
    MeshSourceDescription_Shutdown( &d );
    Box b( math::Vec3d_Make( 1, 1, 1 ), math::Vec3d_Make( 3, 3, 3 ), 2u, 2000u );
    csg_mesh_options_t o{};
    Result res;
    csg_diagnostics_t diag{};
    CHECK( CsgMesh_TryEvaluate( &open, &b.mesh, o, &res.r, &diag ) == geometry_status_t::OPEN_VOLUME );
    CHECK( diag.stage == csg_stage_t::INPUT );
    CHECK( res.r.mesh.faces.nCount == 0u );
    // CLIP only needs the cutter closed: the open shell is clipped.
    o.op = op_t::CLIP;
    CHECK( CsgMesh_TryEvaluate( &open, &b.mesh, o, &res.r, &diag ) == geometry_status_t::OK );
    CHECK( res.r.mesh.faces.nCount > 0u );
    // Overlapping symmetric difference: the two parts meet along edges,
    // which no manifold mesh can represent.
    o.op = op_t::SYMMETRIC_DIFFERENCE;
    CHECK( CsgMesh_TryEvaluate( &a.mesh, &b.mesh, o, &res.r, &diag ) == geometry_status_t::NON_MANIFOLD );
    CHECK( diag.stage == csg_stage_t::RECONSTRUCTION );
    CHECK( res.r.mesh.faces.nCount == 0u );
    // Disjoint symmetric difference is just both.
    Box far( math::Vec3d_Make( 10, 0, 0 ), math::Vec3d_Make( 11, 1, 1 ), 2u, 3000u );
    CHECK( Near( Run( a.mesh, far.mesh, op_t::SYMMETRIC_DIFFERENCE ), 9.0 ) );
    MeshSource_Shutdown( &open );
}

TEST_CASE( "Random rotated operands satisfy the Boolean volume identities", "[geometry][csg][stress]" )
{
    // A fixed LCG, so every run tests the same configurations.
    common::u64 state = 0x9E3779B97F4A7C15ull;
    auto next = [&]() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>( state >> 11 ) / static_cast<double>( 1ull << 53 );
    };
    Box a( math::Vec3d_Make( -1, -1, -1 ), math::Vec3d_Make( 1, 1, 1 ), 1u, 1000u );
    for ( int i = 0; i < 24; ++i ) {
        CAPTURE( i );
        const math::vec3d_t c = math::Vec3d_Make( next() * 1.6 - 0.8, next() * 1.6 - 0.8, next() * 1.6 - 0.8 );
        const math::vec3d_t h = math::Vec3d_Make( 0.3 + next() * 0.9, 0.3 + next() * 0.9, 0.3 + next() * 0.9 );
        const double turn = next() * math::CY_PI_D;
        Box b( math::Vec3d_Subtract( c, h ), math::Vec3d_Add( c, h ), 2u, 2000u, turn );
        const double volB = 8.0 * h.x * h.y * h.z;
        const double inter = Run( a.mesh, b.mesh, op_t::INTERSECTION );
        const double diff = Run( a.mesh, b.mesh, op_t::DIFFERENCE );
        const double uni = Run( a.mesh, b.mesh, op_t::UNION );
        CHECK( std::fabs( inter + diff - 8.0 ) < 1e-8 );
        CHECK( std::fabs( uni - ( 8.0 + volB - inter ) ) < 1e-8 );
        CHECK( inter >= -1e-12 );
    }
}

TEST_CASE( "A faceted sphere cut by a box", "[geometry][csg]" )
{
    geometry_fragment_t frag{};
    REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    geometry_primitive_t p{};
    p.kind = geometry_primitive_kind_t::SPHERE;
    p.box.lo = math::Vec3d_Make( -1, -1, -1 );
    p.box.hi = math::Vec3d_Make( 1, 1, 1 );
    p.cSides = 24u;
    p.cRings = 12u;
    geometry_source_id_allocator_t ids{};
    ids.next.value = 10u;
    REQUIRE( Primitive_TryBuild( p, geometry_primitive_output_t::MESH, geometry_policy_t{}, &ids, &frag ) == geometry_status_t::OK );
    const mesh_source_t &sphere = *frag.meshes.pData[0];
    Box cutter( math::Vec3d_Make( 0.2, -2, -2 ), math::Vec3d_Make( 3, 2, 2 ), 2u, 90000u, 0.1 );
    const double inter = Run( sphere, cutter.mesh, op_t::INTERSECTION );
    const double diff = Run( sphere, cutter.mesh, op_t::DIFFERENCE );
    // The sphere's own volume, from a union with something far away.
    Box far( math::Vec3d_Make( 50, 50, 50 ), math::Vec3d_Make( 51, 51, 51 ), 2u, 95000u );
    const double both = Run( sphere, far.mesh, op_t::UNION );
    const double volSphere = both - 1.0;
    CHECK( std::fabs( inter + diff - volSphere ) < 1e-8 );
    CHECK( inter > 0.0 );
    CHECK( diff > inter ); // the cut at x = 0.2 keeps the larger part
    GeometryFragment_Shutdown( &frag );
}

} // namespace cypher::editor::geometry
