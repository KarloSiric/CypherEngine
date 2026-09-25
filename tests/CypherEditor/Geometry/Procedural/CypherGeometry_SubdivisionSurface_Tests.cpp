//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SubdivisionSurface_Tests.cpp
//  Purpose: Tests retained subdivision: agreement with the destructive
//           Catmull-Clark, sharp creases keeping a box exact, boundary
//           rules on open meshes, surface-data interpolation, provenance,
//           count prediction, collapse identity, limits, and allocation
//           failure.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSubdivision.h"
#include "CypherGeometry_Primitive.h"
#include "CypherGeometry_SubdivisionSurface.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

struct Mesh {
    geometry_fragment_t frag{};
    mesh_source_t *pSource{ nullptr };
    explicit Mesh( geometry_primitive_kind_t kind = geometry_primitive_kind_t::BOX, common::u32 segments = 1u )
    {
        REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
        geometry_primitive_t p{};
        p.kind = kind;
        p.box.lo = math::Vec3d_Make( -1, -1, -1 );
        p.box.hi = math::Vec3d_Make( 1, 1, kind == geometry_primitive_kind_t::PLANE ? -1 : 1 );
        p.cSegments[0] = p.cSegments[1] = p.cSegments[2] = segments;
        p.material.value = 9u;
        geometry_source_id_allocator_t ids{};
        REQUIRE( Primitive_TryBuild( p, geometry_primitive_output_t::MESH, geometry_policy_t{}, &ids, &frag ) == geometry_status_t::OK );
        pSource = frag.meshes.pData[0];
    }
    ~Mesh() { GeometryFragment_Shutdown( &frag ); }
};

struct Result {
    subdivision_result_t r{};
    Result() { REQUIRE( SubdivisionResult_Init( &r, common::Allocator_GetSystem() ) == geometry_status_t::OK ); }
    ~Result() { SubdivisionResult_Shutdown( &r ); }
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

std::vector<math::vec3d_t> SortedPositions( std::vector<math::vec3d_t> ps )
{
    std::sort( ps.begin(), ps.end(), []( const math::vec3d_t &a, const math::vec3d_t &b ) {
        const auto r = []( double x ) { return std::round( x * 1e9 ); };
        if ( r( a.x ) != r( b.x ) ) { return r( a.x ) < r( b.x ); }
        if ( r( a.y ) != r( b.y ) ) { return r( a.y ) < r( b.y ); }
        return r( a.z ) < r( b.z );
    } );
    return ps;
}

} // namespace

TEST_CASE( "Retained Catmull-Clark agrees with the destructive one on a closed cage", "[geometry][subdivision]" )
{
    Mesh cube;
    Result res;
    subdivision_descriptor_t d{};
    d.cLevels = 1u;
    REQUIRE( Subdivision_TryEvaluate( cube.pSource, d, &res.r ) == geometry_status_t::OK );
    CHECK( res.r.mesh.vertices.nCount == 26u );
    CHECK( res.r.mesh.faces.nCount == 24u );

    editable_mesh_t out{};
    REQUIRE( EditableMesh_Init( &out, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    REQUIRE( MeshSubdivision_TryCatmullClark( &cube.pSource->mesh, &out ) == geometry_status_t::OK );
    std::vector<math::vec3d_t> a, b;
    for ( common::usize i = 0; i < res.r.mesh.vertices.nCount; ++i ) { a.push_back( res.r.mesh.vertices.pData[i].position ); }
    (void)common::GenerationPool_ForEach( &out.vertices, [&]( geometry_mesh_vertex_handle_t, const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
        b.push_back( v.position );
        return true;
    } );
    REQUIRE( a.size() == b.size() );
    a = SortedPositions( a );
    b = SortedPositions( b );
    for ( size_t i = 0; i < a.size(); ++i ) {
        CHECK( std::fabs( a[i].x - b[i].x ) < 1e-9 );
        CHECK( std::fabs( a[i].y - b[i].y ) < 1e-9 );
        CHECK( std::fabs( a[i].z - b[i].z ) < 1e-9 );
    }
    EditableMesh_Shutdown( &out );

    // Every output face came from one of the six cage faces, four each, and
    // kept the material; the cage corners kept their IDs.
    std::vector<int> perSource( 6, 0 );
    for ( common::usize f = 0; f < res.r.faceSources.nCount; ++f ) {
        REQUIRE( res.r.faceSources.pData[f] < 6u );
        ++perSource[res.r.faceSources.pData[f]];
        CHECK( res.r.mesh.faces.pData[f].attributes.material.value == 9u );
    }
    for ( const int n : perSource ) { CHECK( n == 4 ); }
    common::usize cKept = 0;
    for ( common::usize v = 0; v < res.r.mesh.vertices.nCount; ++v ) {
        const bool bVertex = res.r.vertexOrigins.pData[v].kind == subdivision_origin_kind_t::VERTEX;
        CHECK( bVertex == GeometrySourceId_IsValid( res.r.mesh.vertices.pData[v].sourceId ) );
        cKept += bVertex ? 1u : 0u;
    }
    CHECK( cKept == 8u );
    // Smoothing shrinks the cube (one level of a 2-unit cube: 10/3).
    CHECK( std::fabs( Volume( res.r.mesh ) - 10.0 / 3.0 ) < 1e-9 );
}

TEST_CASE( "Fully sharp creases and linear subdivision keep a box exact", "[geometry][subdivision]" )
{
    Mesh cube;
    // Mark every edge fully sharp through the description round trip.
    mesh_source_description_t desc{};
    REQUIRE( MeshSourceDescription_Init( &desc, common::Allocator_GetSystem(), GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( cube.pSource, &desc ) == geometry_status_t::OK );
    for ( common::usize f = 0; f < desc.faces.nCount; ++f ) {
        const mesh_source_face_t &face = desc.faces.pData[f];
        for ( common::u32 k = 0; k < face.cCorners; ++k ) {
            const common::u32 a = desc.corners.pData[face.iFirstCorner + k].iVertex;
            const common::u32 b = desc.corners.pData[face.iFirstCorner + ( k + 1 ) % face.cCorners].iVertex;
            if ( a < b ) { REQUIRE( MeshSourceDescription_TrySetEdge( &desc, a, b, mesh_edge_attributes_t{}, 1.0 ) == geometry_status_t::OK ); }
        }
    }
    mesh_source_t sharp{};
    REQUIRE( MeshSource_TryBuild( &desc, common::Allocator_GetSystem(), &sharp ) == geometry_status_t::OK );
    MeshSourceDescription_Shutdown( &desc );

    for ( const subdivision_scheme_t scheme : { subdivision_scheme_t::CATMULL_CLARK, subdivision_scheme_t::LINEAR } ) {
        CAPTURE( static_cast<int>( scheme ) );
        Result res;
        subdivision_descriptor_t d{};
        d.scheme = scheme;
        d.cLevels = 3u;
        REQUIRE( Subdivision_TryEvaluate( scheme == subdivision_scheme_t::LINEAR ? cube.pSource : &sharp, d, &res.r ) == geometry_status_t::OK );
        CHECK( std::fabs( Volume( res.r.mesh ) - 8.0 ) < 1e-9 );
        for ( common::usize v = 0; v < res.r.mesh.vertices.nCount; ++v ) {
            const math::vec3d_t p = res.r.mesh.vertices.pData[v].position;
            const double m = std::max( { std::fabs( p.x ), std::fabs( p.y ), std::fabs( p.z ) } );
            CHECK( std::fabs( m - 1.0 ) < 1e-9 ); // on the surface of the box
        }
        if ( scheme == subdivision_scheme_t::CATMULL_CLARK ) {
            // Creases carry to both halves at every level: 12 edges x 2^3.
            common::usize cCreased = 0;
            for ( common::usize e = 0; e < res.r.mesh.edges.nCount; ++e ) { cCreased += res.r.mesh.edges.pData[e].creaseWeight == 1.0 ? 1u : 0u; }
            CHECK( cCreased == 12u * 8u );
        }
    }
    MeshSource_Shutdown( &sharp );
}

TEST_CASE( "A half-sharp crease blends the smooth and sharp edge rules", "[geometry][subdivision]" )
{
    Mesh cube;
    mesh_source_description_t desc{};
    REQUIRE( MeshSourceDescription_Init( &desc, common::Allocator_GetSystem(), GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( cube.pSource, &desc ) == geometry_status_t::OK );
    // The first edge of the first face, weight 0.5.
    const mesh_source_face_t &f0 = desc.faces.pData[0];
    const common::u32 a = desc.corners.pData[f0.iFirstCorner].iVertex, b = desc.corners.pData[f0.iFirstCorner + 1].iVertex;
    REQUIRE( MeshSourceDescription_TrySetEdge( &desc, std::min( a, b ), std::max( a, b ), mesh_edge_attributes_t{}, 0.5 ) == geometry_status_t::OK );
    mesh_source_t creased{};
    REQUIRE( MeshSource_TryBuild( &desc, common::Allocator_GetSystem(), &creased ) == geometry_status_t::OK );

    // Expected: the two faces on edge a-b, their centroids, the rule blend.
    mesh_source_description_t cage{};
    REQUIRE( MeshSourceDescription_Init( &cage, common::Allocator_GetSystem(), GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &creased, &cage ) == geometry_status_t::OK );
    common::u32 ca = 0, cb = 0; // the edge's endpoints in the creased cage's indexing
    for ( common::usize v = 0; v < cage.vertices.nCount; ++v ) {
        if ( cage.vertices.pData[v].sourceId.value == desc.vertices.pData[a].sourceId.value ) { ca = static_cast<common::u32>( v ); }
        if ( cage.vertices.pData[v].sourceId.value == desc.vertices.pData[b].sourceId.value ) { cb = static_cast<common::u32>( v ); }
    }
    math::vec3d_t faceSum{};
    int cAdjacent = 0;
    for ( common::usize f = 0; f < cage.faces.nCount; ++f ) {
        const mesh_source_face_t &face = cage.faces.pData[f];
        bool bA = false, bB = false;
        math::vec3d_t c{};
        for ( common::u32 k = 0; k < face.cCorners; ++k ) {
            const common::u32 v = cage.corners.pData[face.iFirstCorner + k].iVertex;
            bA = bA || v == ca;
            bB = bB || v == cb;
            c = math::Vec3d_Add( c, cage.vertices.pData[v].position );
        }
        if ( bA && bB ) {
            faceSum = math::Vec3d_Add( faceSum, math::Vec3d_Scale( c, 1.0 / face.cCorners ) );
            ++cAdjacent;
        }
    }
    REQUIRE( cAdjacent == 2 );
    const math::vec3d_t pa = cage.vertices.pData[ca].position, pb = cage.vertices.pData[cb].position;
    const math::vec3d_t mid = math::Vec3d_Scale( math::Vec3d_Add( pa, pb ), 0.5 );
    const math::vec3d_t smooth = math::Vec3d_Scale( math::Vec3d_Add( math::Vec3d_Add( pa, pb ), faceSum ), 0.25 );
    const math::vec3d_t expected = math::Vec3d_Lerp( smooth, mid, 0.5 );

    Result res;
    subdivision_descriptor_t d{};
    REQUIRE( Subdivision_TryEvaluate( &creased, d, &res.r ) == geometry_status_t::OK );
    bool bFound = false;
    for ( common::usize v = 0; v < res.r.mesh.vertices.nCount; ++v ) {
        const subdivision_origin_t &o = res.r.vertexOrigins.pData[v];
        if ( o.kind == subdivision_origin_kind_t::EDGE && o.iSource == std::min( ca, cb ) && o.iSourceB == std::max( ca, cb ) ) {
            bFound = true;
            const math::vec3d_t p = res.r.mesh.vertices.pData[v].position;
            CHECK( std::fabs( p.x - expected.x ) < 1e-12 );
            CHECK( std::fabs( p.y - expected.y ) < 1e-12 );
            CHECK( std::fabs( p.z - expected.z ) < 1e-12 );
        }
    }
    CHECK( bFound );
    // Both halves of the edge carry the weight on.
    common::usize cHalf = 0;
    for ( common::usize e = 0; e < res.r.mesh.edges.nCount; ++e ) { cHalf += res.r.mesh.edges.pData[e].creaseWeight == 0.5 ? 1u : 0u; }
    CHECK( cHalf == 2u );
    MeshSourceDescription_Shutdown( &cage );
    MeshSourceDescription_Shutdown( &desc );
    MeshSource_Shutdown( &creased );
}

TEST_CASE( "Open meshes follow the boundary rule and interpolate surface data", "[geometry][subdivision]" )
{
    Mesh plane( geometry_primitive_kind_t::PLANE );
    for ( const subdivision_boundary_t boundary : { subdivision_boundary_t::PIN_CORNERS, subdivision_boundary_t::SMOOTH } ) {
        CAPTURE( static_cast<int>( boundary ) );
        Result res;
        subdivision_descriptor_t d{};
        d.cLevels = 2u;
        d.boundary = boundary;
        REQUIRE( Subdivision_TryEvaluate( plane.pSource, d, &res.r ) == geometry_status_t::OK );
        CHECK( res.r.mesh.faces.nCount == 16u );
        bool bCornersFixed = true;
        for ( common::usize v = 0; v < res.r.mesh.vertices.nCount; ++v ) {
            if ( res.r.vertexOrigins.pData[v].kind != subdivision_origin_kind_t::VERTEX ) { continue; }
            const math::vec3d_t p = res.r.mesh.vertices.pData[v].position;
            bCornersFixed = bCornersFixed && std::fabs( std::fabs( p.x ) - 1.0 ) < 1e-12 && std::fabs( std::fabs( p.y ) - 1.0 ) < 1e-12;
            CHECK( p.z == -1.0 ); // stays in the plane
        }
        CHECK( bCornersFixed == ( boundary == subdivision_boundary_t::PIN_CORNERS ) );
    }

    // UVs: the face point of a unit-UV quad sits at (0.5, 0.5) of the
    // corner UVs' mean, and edge points at the edge midpoints.
    Result res;
    subdivision_descriptor_t d{};
    d.cLevels = 1u;
    REQUIRE( Subdivision_TryEvaluate( plane.pSource, d, &res.r ) == geometry_status_t::OK );
    mesh_source_description_t cage{};
    REQUIRE( MeshSourceDescription_Init( &cage, common::Allocator_GetSystem(), GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( plane.pSource, &cage ) == geometry_status_t::OK );
    math::vec2d_t mean{};
    for ( common::usize c = 0; c < cage.corners.nCount; ++c ) {
        mean.x += cage.corners.pData[c].attributes.uv0.x / 4.0;
        mean.y += cage.corners.pData[c].attributes.uv0.y / 4.0;
    }
    for ( common::usize c = 0; c < res.r.mesh.corners.nCount; ++c ) {
        const common::u32 v = res.r.mesh.corners.pData[c].iVertex;
        if ( res.r.vertexOrigins.pData[v].kind == subdivision_origin_kind_t::FACE ) {
            CHECK( std::fabs( res.r.mesh.corners.pData[c].attributes.uv0.x - mean.x ) < 1e-12 );
            CHECK( std::fabs( res.r.mesh.corners.pData[c].attributes.uv0.y - mean.y ) < 1e-12 );
        }
    }
    MeshSourceDescription_Shutdown( &cage );
}

TEST_CASE( "Predicted counts match, level 0 is the cage, and limits are enforced", "[geometry][subdivision]" )
{
    Mesh box( geometry_primitive_kind_t::BOX, 2u );
    for ( common::u32 levels = 0; levels <= 3u; ++levels ) {
        CAPTURE( levels );
        subdivision_descriptor_t d{};
        d.cLevels = levels;
        common::usize cV = 0, cF = 0;
        REQUIRE( Subdivision_TryPredictCounts( box.pSource, d, &cV, &cF ) == geometry_status_t::OK );
        Result res;
        REQUIRE( Subdivision_TryEvaluate( box.pSource, d, &res.r ) == geometry_status_t::OK );
        CHECK( res.r.mesh.vertices.nCount == cV );
        CHECK( res.r.mesh.faces.nCount == cF );
        if ( levels == 0u ) {
            CHECK( cV == EditableMesh_VertexCount( &box.pSource->mesh ) );
            CHECK( cF == EditableMesh_FaceCount( &box.pSource->mesh ) );
        }
    }
    subdivision_descriptor_t d{};
    d.cLevels = kSubdivisionLevelsMax + 1u;
    Result res;
    CHECK( Subdivision_TryEvaluate( box.pSource, d, &res.r ) == geometry_status_t::INVALID_ARGUMENT );
    Mesh dense( geometry_primitive_kind_t::BOX, 32u );
    d.cLevels = kSubdivisionLevelsMax;
    common::usize cV = 0, cF = 0;
    CHECK( Subdivision_TryPredictCounts( dense.pSource, d, &cV, &cF ) == geometry_status_t::LIMIT_EXCEEDED );
    CHECK( Subdivision_TryEvaluate( dense.pSource, d, &res.r ) == geometry_status_t::LIMIT_EXCEEDED );
    CHECK( res.r.mesh.vertices.nCount == 0u );
}

TEST_CASE( "Collapsing keeps the cage identities and gives new ones to the rest", "[geometry][subdivision]" )
{
    Mesh cube;
    subdivision_descriptor_t d{};
    d.cLevels = 2u;
    geometry_source_id_allocator_t ids{};
    ids.next.value = 1000u;
    mesh_source_t out{};
    REQUIRE( Subdivision_TryCollapse( cube.pSource, d, common::Allocator_GetSystem(), &ids, &out ) == geometry_status_t::OK );
    CHECK( MeshSource_Validate( &out, common::Allocator_GetSystem() ).fault == mesh_source_fault_t::NONE );
    CHECK( out.sourceId.value == cube.pSource->sourceId.value );
    const common::usize cV = EditableMesh_VertexCount( &out.mesh ), cF = EditableMesh_FaceCount( &out.mesh );
    CHECK( cF == 96u );
    CHECK( ids.next.value == 1000u + ( cV - 8u ) + cF );
    // The eight cage vertices are still there by ID.
    mesh_source_description_t cage{};
    REQUIRE( MeshSourceDescription_Init( &cage, common::Allocator_GetSystem(), GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( cube.pSource, &cage ) == geometry_status_t::OK );
    for ( common::usize v = 0; v < cage.vertices.nCount; ++v ) {
        geometry_mesh_vertex_handle_t h{};
        CHECK( MeshSource_TryFindVertex( &out, cage.vertices.pData[v].sourceId, &h ) );
    }
    MeshSourceDescription_Shutdown( &cage );
    MeshSource_Shutdown( &out );

    // Evaluable, but too dense to be an authored mesh (98 304 faces against
    // the 65 536 face limit): nothing changes.
    Mesh dense( geometry_primitive_kind_t::BOX, 8u );
    d.cLevels = 4u;
    geometry_source_id_allocator_t before = ids;
    mesh_source_t big{};
    CHECK( Subdivision_TryCollapse( dense.pSource, d, common::Allocator_GetSystem(), &ids, &big ) != geometry_status_t::OK );
    CHECK( ids.next.value == before.next.value );
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

TEST_CASE( "Every subdivision allocation failure leaves an empty result and no leak", "[geometry][subdivision][allocation]" )
{
    Mesh cube;
    subdivision_descriptor_t d{};
    d.cLevels = 2u;
    common::usize cOperation = 0u;
    {
        fail_state_t probe{};
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &probe };
        subdivision_result_t r{};
        REQUIRE( SubdivisionResult_Init( &r, &allocator ) == geometry_status_t::OK );
        const common::usize cStart = probe.cCalls;
        REQUIRE( Subdivision_TryEvaluate( cube.pSource, d, &r ) == geometry_status_t::OK );
        cOperation = probe.cCalls - cStart;
        SubdivisionResult_Shutdown( &r );
        CHECK( probe.cLive == 0u );
    }
    REQUIRE( cOperation > 0u );
    for ( common::usize i = 1; i <= cOperation; ++i ) {
        CAPTURE( i );
        fail_state_t state{};
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &state };
        {
            subdivision_result_t r{};
            REQUIRE( SubdivisionResult_Init( &r, &allocator ) == geometry_status_t::OK );
            state.iFailOnCall = state.cCalls + i;
            CHECK( Subdivision_TryEvaluate( cube.pSource, d, &r ) == geometry_status_t::ALLOCATION_FAILED );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            CHECK( r.mesh.vertices.nCount == 0u );
            CHECK( r.mesh.faces.nCount == 0u );
            SubdivisionResult_Shutdown( &r );
        }
        CHECK( state.cLive == 0u );
    }
}

} // namespace cypher::editor::geometry
