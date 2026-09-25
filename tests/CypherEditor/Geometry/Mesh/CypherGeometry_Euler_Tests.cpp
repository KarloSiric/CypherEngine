//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Euler_Tests.cpp
//  Purpose: Tests the Euler operators: exact count changes, inverse pairs
//           that restore the mesh, every precondition failure leaving the
//           mesh untouched, stale handles for killed elements, and an
//           exhaustive sweep over every edge and face of a subdivided box.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Euler.h"
#include "CypherGeometry_MeshValidation.h"
#include "CypherGeometry_Primitive.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

// A box mesh (optionally subdivided) owned by a fragment; tests edit its
// raw editable mesh.
struct Box {
    geometry_fragment_t frag{};
    editable_mesh_t *pMesh{ nullptr };
    explicit Box( common::u32 segments = 1u, bool bPlane = false )
    {
        REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
        geometry_primitive_t p{};
        p.kind = bPlane ? geometry_primitive_kind_t::PLANE : geometry_primitive_kind_t::BOX;
        p.box.lo = math::Vec3d_Make( 0, 0, 0 );
        p.box.hi = math::Vec3d_Make( 4, 4, bPlane ? 0 : 4 );
        p.cSegments[0] = p.cSegments[1] = p.cSegments[2] = segments;
        geometry_source_id_allocator_t ids{};
        REQUIRE( Primitive_TryBuild( p, geometry_primitive_output_t::MESH, geometry_policy_t{}, &ids, &frag ) == geometry_status_t::OK );
        pMesh = &frag.meshes.pData[0]->mesh;
    }
    ~Box() { GeometryFragment_Shutdown( &frag ); }
};

bool Valid( const editable_mesh_t *pMesh )
{
    const mesh_validation_result_t v = MeshValidation_Validate( pMesh );
    return v.bReciprocalTwins && v.bClosedLoops && v.bAllVerticesReferenced && v.bEdgeLinks && v.bShellLinks;
}

bool SameCounts( const euler_counts_t &a, const euler_counts_t &b )
{
    return a.cVertices == b.cVertices && a.cEdges == b.cEdges && a.cFaces == b.cFaces && a.cShells == b.cShells;
}

template <typename tag_t> bool Same( common::generation_handle_t<tag_t> a, common::generation_handle_t<tag_t> b )
{
    return a.nSlot == b.nSlot && a.nGeneration == b.nGeneration;
}

template <typename record_t, typename tag_t> std::vector<common::generation_handle_t<tag_t>> Handles( common::generation_pool_t<record_t, tag_t> *pPool )
{
    std::vector<common::generation_handle_t<tag_t>> out;
    (void)common::GenerationPool_ForEach( pPool, [&]( common::generation_handle_t<tag_t> h, const record_t & ) noexcept -> common::bool_t {
        out.push_back( h );
        return true;
    } );
    return out;
}

std::vector<geometry_mesh_vertex_handle_t> Corners( const editable_mesh_t *pMesh, geometry_mesh_face_handle_t f )
{
    std::vector<geometry_mesh_vertex_handle_t> out;
    const mesh_face_record_t *pF = EditableMesh_GetFace( pMesh, f );
    if ( pF == nullptr ) { return out; }
    const mesh_loop_record_t *pL = EditableMesh_GetLoop( pMesh, pF->hOuterLoop );
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( common::u32 i = 0; i < pL->cHalfEdges; ++i ) {
        const mesh_half_edge_record_t *p = EditableMesh_GetHalfEdge( pMesh, h );
        out.push_back( p->hOrigin );
        h = p->hNext;
    }
    return out;
}

// Equal as cyclic sequences (the loop's first half-edge may move).
bool SameCycle( const std::vector<geometry_mesh_vertex_handle_t> &a, const std::vector<geometry_mesh_vertex_handle_t> &b )
{
    if ( a.size() != b.size() ) { return false; }
    for ( size_t shift = 0; shift < a.size(); ++shift ) {
        bool bAll = true;
        for ( size_t i = 0; i < a.size() && bAll; ++i ) { bAll = Same( a[i], b[( i + shift ) % a.size()] ); }
        if ( bAll ) { return true; }
    }
    return false;
}

// Snapshot of every face's corner cycle, to prove a mesh came back exactly.
std::vector<std::vector<geometry_mesh_vertex_handle_t>> FaceCycles( editable_mesh_t *pMesh )
{
    std::vector<std::vector<geometry_mesh_vertex_handle_t>> out;
    for ( const geometry_mesh_face_handle_t f : Handles( &pMesh->faces ) ) { out.push_back( Corners( pMesh, f ) ); }
    return out;
}

} // namespace

TEST_CASE( "Euler operator table: deltas and inverses", "[geometry][euler]" )
{
    for ( const euler_operator_t op : { euler_operator_t::SEMV, euler_operator_t::JEKV, euler_operator_t::MEF, euler_operator_t::KEF } ) {
        const euler_counts_t d = Euler_Delta( op ), inv = Euler_Delta( Euler_Inverse( op ) );
        CHECK( d.cVertices - d.cEdges + d.cFaces == 0 ); // V - E + F is invariant
        CHECK( d.cVertices == -inv.cVertices );
        CHECK( d.cEdges == -inv.cEdges );
        CHECK( d.cFaces == -inv.cFaces );
        CHECK( Euler_Inverse( Euler_Inverse( op ) ) == op );
    }
}

TEST_CASE( "Split edge then join edges restores the box exactly", "[geometry][euler]" )
{
    Box box;
    const euler_counts_t start = Euler_Counts( box.pMesh );
    const auto cyclesBefore = FaceCycles( box.pMesh );
    const geometry_mesh_edge_handle_t e = Handles( &box.pMesh->edges )[0];
    const mesh_half_edge_record_t *pH = EditableMesh_GetHalfEdge( box.pMesh, EditableMesh_GetEdge( box.pMesh, e )->hHalfEdge );
    const math::vec3d_t p0 = EditableMesh_GetVertex( box.pMesh, pH->hOrigin )->position;
    const math::vec3d_t p1 = EditableMesh_GetVertex( box.pMesh, EditableMesh_GetHalfEdge( box.pMesh, pH->hNext )->hOrigin )->position;

    const euler_semv_result_t s = Euler_SplitEdge( box.pMesh, e, 0.25, true );
    REQUIRE( s.status == geometry_status_t::OK );
    const euler_counts_t mid = Euler_Counts( box.pMesh );
    CHECK( mid.cVertices == start.cVertices + 1 );
    CHECK( mid.cEdges == start.cEdges + 1 );
    CHECK( mid.cFaces == start.cFaces );
    const math::vec3d_t m = EditableMesh_GetVertex( box.pMesh, s.hNewVertex )->position;
    CHECK( std::fabs( m.x - ( p0.x + 0.25 * ( p1.x - p0.x ) ) ) < 1e-12 );
    CHECK( std::fabs( m.y - ( p0.y + 0.25 * ( p1.y - p0.y ) ) ) < 1e-12 );
    CHECK( std::fabs( m.z - ( p0.z + 0.25 * ( p1.z - p0.z ) ) ) < 1e-12 );

    const euler_jekv_result_t j = Euler_JoinEdges( box.pMesh, s.hNewVertex, true );
    REQUIRE( j.status == geometry_status_t::OK );
    CHECK( SameCounts( Euler_Counts( box.pMesh ), start ) );
    CHECK( Valid( box.pMesh ) );
    // Every face has its original corners again, and the killed vertex is stale.
    const auto cyclesAfter = FaceCycles( box.pMesh );
    REQUIRE( cyclesAfter.size() == cyclesBefore.size() );
    for ( size_t i = 0; i < cyclesBefore.size(); ++i ) { CHECK( SameCycle( cyclesBefore[i], cyclesAfter[i] ) ); }
    CHECK( Euler_CheckJoinEdges( box.pMesh, s.hNewVertex ) == geometry_status_t::INVALID_HANDLE );
    CHECK( Euler_CheckSplitEdge( box.pMesh, Same( j.hSurvivingEdge, e ) ? s.hNewEdge : e, 0.5 ) == geometry_status_t::INVALID_HANDLE );
}

TEST_CASE( "Split face then join faces restores the box exactly", "[geometry][euler]" )
{
    Box box;
    const euler_counts_t start = Euler_Counts( box.pMesh );
    const auto cyclesBefore = FaceCycles( box.pMesh );
    const geometry_mesh_face_handle_t f = Handles( &box.pMesh->faces )[0];
    const auto corners = Corners( box.pMesh, f );
    REQUIRE( corners.size() == 4u );
    const euler_mef_result_t s = Euler_SplitFace( box.pMesh, f, corners[0], corners[2], true );
    REQUIRE( s.status == geometry_status_t::OK );
    CHECK( Euler_Counts( box.pMesh ).cFaces == start.cFaces + 1 );
    CHECK( Corners( box.pMesh, f ).size() == 3u );
    CHECK( Corners( box.pMesh, s.hNewFace ).size() == 3u );

    const euler_kef_result_t k = Euler_JoinFaces( box.pMesh, s.hNewEdge, true );
    REQUIRE( k.status == geometry_status_t::OK );
    CHECK( SameCounts( Euler_Counts( box.pMesh ), start ) );
    CHECK( Corners( box.pMesh, k.hSurvivingFace ).size() == 4u );
    CHECK( SameCycle( Corners( box.pMesh, k.hSurvivingFace ), corners ) );
    CHECK( Euler_CheckJoinFaces( box.pMesh, s.hNewEdge ) == geometry_status_t::INVALID_HANDLE );
    CHECK( EditableMesh_GetFace( box.pMesh, k.hKilledFace ) == nullptr );
    (void)cyclesBefore;
}

TEST_CASE( "A boundary vertex splits and joins inside one open face", "[geometry][euler]" )
{
    Box plane( 1u, true );
    const euler_counts_t start = Euler_Counts( plane.pMesh );
    REQUIRE( start.cFaces == 1 );
    const geometry_mesh_edge_handle_t e = Handles( &plane.pMesh->edges )[0];
    const euler_semv_result_t s = Euler_SplitEdge( plane.pMesh, e, 0.5, true );
    REQUIRE( s.status == geometry_status_t::OK );
    CHECK( Corners( plane.pMesh, Handles( &plane.pMesh->faces )[0] ).size() == 5u );
    const euler_jekv_result_t j = Euler_JoinEdges( plane.pMesh, s.hNewVertex, true );
    REQUIRE( j.status == geometry_status_t::OK );
    CHECK( SameCounts( Euler_Counts( plane.pMesh ), start ) );
    // A corner of the single quad has two edges; joining it leaves a triangle.
    const geometry_mesh_vertex_handle_t corner = Handles( &plane.pMesh->vertices )[0];
    REQUIRE( Euler_JoinEdges( plane.pMesh, corner, true ).status == geometry_status_t::OK );
    CHECK( Corners( plane.pMesh, Handles( &plane.pMesh->faces )[0] ).size() == 3u );
    // A triangle's corner cannot go: the face would have two corners.
    CHECK( Euler_CheckJoinEdges( plane.pMesh, Handles( &plane.pMesh->vertices )[0] ) == geometry_status_t::DEGENERATE );
    // A boundary edge has one face, so there is nothing to join.
    CHECK( Euler_CheckJoinFaces( plane.pMesh, Handles( &plane.pMesh->edges )[0] ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Euler preconditions refuse without touching the mesh", "[geometry][euler]" )
{
    Box box;
    const euler_counts_t start = Euler_Counts( box.pMesh );
    const auto cycles = FaceCycles( box.pMesh );
    auto unchanged = [&]() {
        CHECK( SameCounts( Euler_Counts( box.pMesh ), start ) );
        const auto now = FaceCycles( box.pMesh );
        REQUIRE( now.size() == cycles.size() );
        for ( size_t i = 0; i < now.size(); ++i ) { CHECK( SameCycle( now[i], cycles[i] ) ); }
    };
    const geometry_mesh_edge_handle_t e = Handles( &box.pMesh->edges )[0];
    const geometry_mesh_face_handle_t f = Handles( &box.pMesh->faces )[0];
    const auto corners = Corners( box.pMesh, f );
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK( Euler_SplitEdge( box.pMesh, e, 0.0 ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Euler_SplitEdge( box.pMesh, e, 1.0 ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Euler_SplitEdge( box.pMesh, e, nan ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Euler_SplitEdge( box.pMesh, geometry_mesh_edge_handle_t{}, 0.5 ).status == geometry_status_t::INVALID_HANDLE );
    // A box corner has three edges.
    CHECK( Euler_JoinEdges( box.pMesh, corners[0] ).status == geometry_status_t::INVALID_ARGUMENT );
    // Neighbours on the face, the same corner twice, a vertex off the face.
    CHECK( Euler_SplitFace( box.pMesh, f, corners[0], corners[1] ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Euler_SplitFace( box.pMesh, f, corners[0], corners[0] ).status == geometry_status_t::INVALID_ARGUMENT );
    geometry_mesh_vertex_handle_t off{};
    for ( const geometry_mesh_vertex_handle_t v : Handles( &box.pMesh->vertices ) ) {
        bool bOn = false;
        for ( const auto &c : corners ) { bOn = bOn || Same( c, v ); }
        if ( !bOn ) { off = v; }
    }
    CHECK( Euler_SplitFace( box.pMesh, f, corners[0], off ).status == geometry_status_t::INVALID_ARGUMENT );
    unchanged();

    // Split an edge of the face (m between a and b), then join a-b across
    // the face: m now sits in the triangle a-m-b, which cannot lose a
    // corner, and the other face through a, m, b cannot get a second a-b
    // edge.
    const mesh_half_edge_record_t *pH = EditableMesh_GetHalfEdge( box.pMesh, EditableMesh_GetEdge( box.pMesh, e )->hHalfEdge );
    const geometry_mesh_vertex_handle_t a = pH->hOrigin;
    const geometry_mesh_vertex_handle_t b = EditableMesh_GetHalfEdge( box.pMesh, pH->hNext )->hOrigin;
    const geometry_mesh_face_handle_t fe = EditableMesh_GetLoop( box.pMesh, pH->hLoop )->hFace;
    const euler_semv_result_t s = Euler_SplitEdge( box.pMesh, e, 0.5, true );
    REQUIRE( s.status == geometry_status_t::OK );
    // a, m, b are collinear, so the triangle a-m-b would have no area
    // (DEGENERATE); pull m into the face first.
    CHECK( Euler_CheckSplitFace( box.pMesh, fe, a, b ) == geometry_status_t::DEGENERATE );
    {
        const auto cs = Corners( box.pMesh, fe );
        math::vec3d_t c{};
        for ( const auto &v : cs ) { c = math::Vec3d_Add( c, EditableMesh_GetVertex( box.pMesh, v )->position ); }
        c = math::Vec3d_Scale( c, 1.0 / static_cast<double>( cs.size() ) );
        math::vec3d_t &mp = common::GenerationPool_Get( &box.pMesh->vertices, s.hNewVertex )->position;
        mp = math::Vec3d_Lerp( mp, c, 0.25 );
    }
    const euler_mef_result_t cut = Euler_SplitFace( box.pMesh, fe, a, b, true );
    REQUIRE( cut.status == geometry_status_t::OK );
    const euler_counts_t before = Euler_Counts( box.pMesh );
    CHECK( Euler_JoinEdges( box.pMesh, s.hNewVertex ).status == geometry_status_t::DEGENERATE );
    geometry_mesh_face_handle_t other{};
    for ( const geometry_mesh_face_handle_t g : Handles( &box.pMesh->faces ) ) {
        const auto cs = Corners( box.pMesh, g );
        bool bA = false, bB = false, bM = false;
        for ( const auto &c : cs ) {
            bA = bA || Same( c, a );
            bB = bB || Same( c, b );
            bM = bM || Same( c, s.hNewVertex );
        }
        if ( bA && bB && bM && cs.size() > 3u ) { other = g; }
    }
    REQUIRE( EditableMesh_GetFace( box.pMesh, other ) != nullptr );
    CHECK( Euler_SplitFace( box.pMesh, other, a, b ).status == geometry_status_t::NON_MANIFOLD );
    CHECK( SameCounts( Euler_Counts( box.pMesh ), before ) );
    CHECK( Valid( box.pMesh ) );
}

TEST_CASE( "Every edge and every face of a subdivided box round-trips", "[geometry][euler]" )
{
    Box box( 3u );
    const euler_counts_t start = Euler_Counts( box.pMesh );
    REQUIRE( start.cFaces == 54 );
    const auto cycles = FaceCycles( box.pMesh );
    auto restored = [&]() {
        CHECK( SameCounts( Euler_Counts( box.pMesh ), start ) );
        const auto now = FaceCycles( box.pMesh );
        REQUIRE( now.size() == cycles.size() );
        bool bAll = true;
        for ( size_t i = 0; i < now.size(); ++i ) { bAll = bAll && SameCycle( now[i], cycles[i] ); }
        CHECK( bAll );
    };
    for ( const geometry_mesh_edge_handle_t e : Handles( &box.pMesh->edges ) ) {
        for ( const double t : { 0.1, 0.5, 0.9 } ) {
            const euler_semv_result_t s = Euler_SplitEdge( box.pMesh, e, t, true );
            REQUIRE( s.status == geometry_status_t::OK );
            REQUIRE( Euler_JoinEdges( box.pMesh, s.hNewVertex, true ).status == geometry_status_t::OK );
        }
    }
    restored();
    CHECK( Valid( box.pMesh ) );
    for ( const geometry_mesh_face_handle_t f : Handles( &box.pMesh->faces ) ) {
        const auto cs = Corners( box.pMesh, f );
        for ( size_t i = 0; i < 2; ++i ) {
            const euler_mef_result_t s = Euler_SplitFace( box.pMesh, f, cs[i], cs[i + 2], true );
            REQUIRE( s.status == geometry_status_t::OK );
            REQUIRE( Euler_JoinFaces( box.pMesh, s.hNewEdge, true ).status == geometry_status_t::OK );
        }
    }
    CHECK( SameCounts( Euler_Counts( box.pMesh ), start ) );
    CHECK( Valid( box.pMesh ) );
}

} // namespace cypher::editor::geometry
