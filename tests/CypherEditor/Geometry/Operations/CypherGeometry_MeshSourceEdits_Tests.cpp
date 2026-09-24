//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceEdits_Tests.cpp
//  Purpose: Contract tests for identity-addressed mesh edits and the
//           attribute/identity transfer engine behind them.
//  Details: Oracle: a cube whose corner UVs are an exact axis projection
//           per face ((x,y) for Z-facing faces, (x,z) for Y-facing, (y,z)
//           for X-facing) and whose material encodes the face. Any edit that
//           keeps faces axis-aligned must preserve "uv == projection of
//           position" on every corner, including brand-new faces: extrude
//           side walls extend a coplanar cube wall, so they must continue
//           that wall's projection and material. The Modeling gate (extrude
//           with propagation, remap, validation, determinism, exact undo) is
//           exercised end to end through a document transaction.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceModeling.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_MeshTransaction.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace cypher::editor::geometry {

using math::vec2d_t;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

// Dominant-axis projection used as the UV oracle.
int DominantAxis( vec3d_t n ) {
    const double ax = std::fabs( n.x ), ay = std::fabs( n.y ), az = std::fabs( n.z );
    return az >= ax && az >= ay ? 2 : ( ay >= ax ? 1 : 0 );
}

vec2d_t Project( int axis, vec3d_t p ) {
    switch ( axis ) {
    case 2: return vec2d_t{ p.x, p.y };
    case 1: return vec2d_t{ p.x, p.z };
    default: return vec2d_t{ p.y, p.z };
    }
}

// Material encodes the (signed) axis a face points along.
common::u64 MaterialFor( vec3d_t n ) {
    const int axis = DominantAxis( n );
    const double c = axis == 2 ? n.z : ( axis == 1 ? n.y : n.x );
    return 10u + static_cast<common::u64>( axis ) * 2u + ( c > 0.0 ? 1u : 0u );
}

vec3d_t Newell( const mesh_source_description_t &d, const mesh_source_face_t &f ) {
    vec3d_t n{};
    for ( common::u32 k = 0; k < f.cCorners; ++k ) {
        const vec3d_t a = d.vertices.pData[d.corners.pData[f.iFirstCorner + k].iVertex].position;
        const vec3d_t b = d.vertices.pData[d.corners.pData[f.iFirstCorner + ( k + 1 ) % f.cCorners].iVertex].position;
        n.x += ( a.y - b.y ) * ( a.z + b.z );
        n.y += ( a.z - b.z ) * ( a.x + b.x );
        n.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    return n;
}

struct Env {
    common::allocator_t allocator{};
    mesh_source_t mesh{};
    mesh_source_description_t d{};
    geometry_source_id_allocator_t ids{ Id( 5000 ) };

    explicit Env(
        const common::allocator_t &allocatorValue =
            *common::Allocator_GetSystem() )
        : allocator( allocatorValue ) {
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        for ( int i = 0; i < 8; ++i ) {
            REQUIRE( MeshSourceDescription_TryAddVertex( &d,
                                                         Vec3d_Make( ( i & 1 ) ? 1.0 : 0.0, ( i & 2 ) ? 1.0 : 0.0,
                                                                     ( i & 4 ) ? 1.0 : 0.0 ),
                                                         Id( 10u + static_cast<common::u64>( i ) ), nullptr ) ==
                     geometry_status_t::OK );
        }
        const std::vector<std::vector<common::u32>> faces = {
            { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for ( common::usize f = 0; f < faces.size(); ++f ) {
            REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ faces[f].data(), faces[f].size() },
                                                       Id( 20u + f ), mesh_face_attributes_t{}, nullptr ) ==
                     geometry_status_t::OK );
        }
        ApplyOracle( &d );
        REQUIRE( MeshSource_TryBuild( &d, &allocator, &mesh ) == geometry_status_t::OK );
    }
    ~Env() {
        MeshSource_Shutdown( &mesh );
        MeshSourceDescription_Shutdown( &d );
    }

    // Writes oracle UVs and materials into a description.
    static void ApplyOracle( mesh_source_description_t *pD ) {
        for ( common::usize f = 0; f < pD->faces.nCount; ++f ) {
            mesh_source_face_t &face = pD->faces.pData[f];
            const vec3d_t n = Newell( *pD, face );
            face.attributes.material.value = MaterialFor( n );
            for ( common::u32 k = 0; k < face.cCorners; ++k ) {
                mesh_source_corner_t &c = pD->corners.pData[face.iFirstCorner + k];
                c.attributes.uv0 = Project( DominantAxis( n ), pD->vertices.pData[c.iVertex].position );
            }
        }
    }

    void Describe() { REQUIRE( MeshSource_TryDescribe( &mesh, &d ) == geometry_status_t::OK ); }

    // Every face: material matches its axis, every corner UV matches the
    // projection. Returns the number of faces checked.
    common::usize CheckOracle() {
        REQUIRE( MeshSource_TryAssignMissingIds( &mesh, &ids, nullptr ) == geometry_status_t::OK );
        REQUIRE( MeshSource_Validate( &mesh, &allocator ).fault == mesh_source_fault_t::NONE );
        Describe();
        for ( common::usize f = 0; f < d.faces.nCount; ++f ) {
            const mesh_source_face_t &face = d.faces.pData[f];
            const vec3d_t n = Newell( d, face );
            CAPTURE( f, face.sourceId.value );
            CHECK( face.attributes.material.value == MaterialFor( n ) );
            for ( common::u32 k = 0; k < face.cCorners; ++k ) {
                const mesh_source_corner_t &c = d.corners.pData[face.iFirstCorner + k];
                const vec2d_t want = Project( DominantAxis( n ), d.vertices.pData[c.iVertex].position );
                CHECK( c.attributes.uv0.x == Catch::Approx( want.x ).margin( 1e-9 ) );
                CHECK( c.attributes.uv0.y == Catch::Approx( want.y ).margin( 1e-9 ) );
            }
        }
        return d.faces.nCount;
    }
};

struct mesh_edit_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_USIZE_MAX };
    common::usize cRejectedAllocations{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *MeshEditFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<mesh_edit_failure_allocator_state_t *>( pUserData );
    const common::usize iCall = pState->cAllocationCalls++;
    if ( iCall == pState->iFailOnCall ) {
        ++pState->cRejectedAllocations;
        return nullptr;
    }
    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
    }
    return pMemory;
}

void MeshEditFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<mesh_edit_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFrees;
    }
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeMeshEditFailureAllocator(
    mesh_edit_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        &MeshEditFailureAllocate,
        nullptr,
        &MeshEditFailureFree,
        pState
    };
}

struct ScopedDescription {
    mesh_source_description_t value{};

    explicit ScopedDescription(
        const common::allocator_t *pAllocator ) {
        REQUIRE( MeshSourceDescription_Init(
                     &value,
                     pAllocator,
                     GEOMETRY_SOURCE_ID_INVALID ) ==
                 geometry_status_t::OK );
    }
    ~ScopedDescription() {
        MeshSourceDescription_Shutdown( &value );
    }
    void Capture( const mesh_source_t *pSource ) {
        REQUIRE( MeshSource_TryDescribe(
                     pSource, &value ) == geometry_status_t::OK );
    }
};

} // namespace

TEST_CASE( "Split edge interpolates corner UVs and keeps edge flags on both halves", "[geometry][meshedit]" ) {
    Env e;
    mesh_edge_attributes_t hard{};
    hard.flags = MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM;
    REQUIRE( MeshSourceEdit_TrySetEdgeAttributes( &e.mesh, Id( 10 ), Id( 11 ), hard, 0.5 ) == geometry_status_t::OK );

    geometry_mesh_vertex_handle_t hNew{};
    mesh_edit_report_t report{};
    REQUIRE( MeshSourceEdit_TrySplitEdge( &e.mesh, Id( 11 ), Id( 10 ), 0.25, &hNew, &report ) == geometry_status_t::OK );
    // t measured from the first ID (vertex 11 at x = 1).
    const vec3d_t p = EditableMesh_GetVertex( &e.mesh.mesh, hNew )->position;
    CHECK( p.x == Catch::Approx( 0.75 ) );
    CHECK( report.stats.cCornersInterpolated == 2u ); // the new corner in each adjacent face
    CHECK( report.stats.cCornersDefaulted == 0u );
    CHECK( report.stats.cEdgesInherited + report.stats.cEdgesRestored >= 2u );
    CHECK( e.CheckOracle() == 6u );

    // Both halves are hard seams with the crease.
    const common::u64 newId = MeshSource_VertexId( &e.mesh, hNew ).value;
    common::u32 cHard = 0;
    for ( common::usize i = 0; i < e.d.edges.nCount; ++i ) {
        const mesh_source_edge_t &ed = e.d.edges.pData[i];
        const common::u64 a = e.d.vertices.pData[ed.iVertexA].sourceId.value;
        const common::u64 b = e.d.vertices.pData[ed.iVertexB].sourceId.value;
        const bool bHalf = ( a == newId || b == newId ) && ( a == 10u || b == 10u || a == 11u || b == 11u );
        if ( bHalf ) {
            CHECK( ed.attributes.flags == ( MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM ) );
            CHECK( ed.creaseWeight == 0.5 );
            ++cHard;
        }
    }
    CHECK( cHard == 2u );
    CHECK( MeshSourceEdit_TrySplitEdge( &e.mesh, Id( 10 ), Id( 17 ), 0.5, nullptr, nullptr ) ==
           geometry_status_t::INVALID_HANDLE ); // not an edge
}

TEST_CASE( "Source split rejects endpoint-rounded parameters independent of stored orientation",
           "[geometry][meshedit][numeric][contract]" ) {
    const double tiny = std::numeric_limits<double>::denorm_min();
    REQUIRE( tiny > 0.0 );

    for ( const std::pair<common::u64, common::u64> endpoints :
          { std::pair<common::u64, common::u64>{ 10u, 11u },
            std::pair<common::u64, common::u64>{ 11u, 10u } } ) {
        CAPTURE( endpoints.first, endpoints.second );
        Env e;
        ScopedDescription before{ &e.allocator };
        ScopedDescription after{ &e.allocator };
        before.Capture( &e.mesh );
        geometry_mesh_vertex_handle_t hNew{};

        CHECK( MeshSourceEdit_TrySplitEdge(
                   &e.mesh,
                   Id( endpoints.first ),
                   Id( endpoints.second ),
                   tiny,
                   &hNew,
                   nullptr ) == geometry_status_t::DEGENERATE );
        CHECK_FALSE( GeometryHandle_IsValid( hNew ) );
        after.Capture( &e.mesh );
        CHECK( MeshSourceDescription_Equal(
            &before.value, &after.value ) );
    }
}

TEST_CASE( "Extrude keeps the cap's identity; walls continue the coplanar sides", "[geometry][meshedit]" ) {
    Env e;
    mesh_edit_report_t report{};
    REQUIRE( MeshSourceEdit_TryExtrudeFace( &e.mesh, Id( 21 ), 0.5, &report ) == geometry_status_t::OK );
    CHECK( report.stats.cFacesInheritingIdentity == 1u );
    CHECK( report.stats.cFacesFromParent == 5u );
    CHECK( report.stats.cFacesWithoutParent == 0u );
    geometry_mesh_face_handle_t hCap{};
    REQUIRE( MeshSource_TryFindFace( &e.mesh, Id( 21 ), &hCap ) );
    CHECK( EditableMesh_GetFace( &e.mesh.mesh, hCap )->normal.z > 0.99 );
    CHECK( e.CheckOracle() == 10u );
}

TEST_CASE( "Extrude reports every allocation failure, including inferred face-parent scratch growth",
           "[geometry][meshedit][allocation][contract]" ) {
    common::usize cOperationAllocations = 0u;
    {
        mesh_edit_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeMeshEditFailureAllocator( &state );
        {
            Env e{ allocator };
            const common::usize iOperationBegin =
                state.cAllocationCalls;
            REQUIRE( MeshSourceEdit_TryExtrudeFace(
                         &e.mesh, Id( 21 ), 0.5, nullptr ) ==
                     geometry_status_t::OK );
            cOperationAllocations =
                state.cAllocationCalls - iOperationBegin;
            REQUIRE( cOperationAllocations > 0u );
        }
        REQUIRE( state.cSuccessfulAllocations == state.cFrees );
    }

    for ( common::usize iFailure = 0u;
          iFailure < cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        mesh_edit_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeMeshEditFailureAllocator( &state );
        {
            Env e{ allocator };
            state.iFailOnCall =
                state.cAllocationCalls + iFailure;

            const geometry_status_t status =
                MeshSourceEdit_TryExtrudeFace(
                    &e.mesh, Id( 21 ), 0.5, nullptr );

            CHECK( state.cRejectedAllocations == 1u );
            CHECK( status == geometry_status_t::ALLOCATION_FAILED );
            state.iFailOnCall = common::CY_USIZE_MAX;
        }
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }
}

TEST_CASE( "Inset keeps the inner face's identity and the ring stays coplanar", "[geometry][meshedit]" ) {
    Env e;
    REQUIRE( MeshSourceEdit_TryInsetFace( &e.mesh, Id( 21 ), 0.2, nullptr ) == geometry_status_t::OK );
    geometry_mesh_face_handle_t h{};
    REQUIRE( MeshSource_TryFindFace( &e.mesh, Id( 21 ), &h ) );
    CHECK( e.CheckOracle() == 10u );
}

TEST_CASE( "Triangulation, split face, and loop cut propagate exactly", "[geometry][meshedit]" ) {
    {
        Env e;
        common::u32 cCreated = 0;
        mesh_edit_report_t report{};
        REQUIRE( MeshSourceEdit_TryTriangulate( &e.mesh, GEOMETRY_SOURCE_ID_INVALID, &cCreated, &report ) ==
                 geometry_status_t::OK );
        CHECK( cCreated == 6u );
        CHECK( report.stats.cCornersDefaulted == 0u );
        CHECK( report.stats.cCornersInterpolated == 0u ); // every corner sits on an existing vertex
        CHECK( e.CheckOracle() == 12u );
        // Each original face ID survives on one of its triangles.
        for ( common::u64 id = 20; id < 26; ++id ) {
            geometry_mesh_face_handle_t h{};
            CHECK( MeshSource_TryFindFace( &e.mesh, Id( id ), &h ) );
        }
    }
    {
        Env e;
        REQUIRE( MeshSourceEdit_TrySplitFace( &e.mesh, Id( 21 ), Id( 14 ), Id( 17 ), nullptr ) == geometry_status_t::OK );
        CHECK( e.CheckOracle() == 7u );
    }
    {
        Env e;
        common::u32 cSplit = 0;
        REQUIRE( MeshSourceEdit_TryLoopCut( &e.mesh, Id( 10 ), Id( 11 ), 0.5, &cSplit, nullptr ) == geometry_status_t::OK );
        CHECK( cSplit == 4u );
        CHECK( e.CheckOracle() == 10u );
    }
}

TEST_CASE( "Mirror keeps corner attributes with their face and vertex", "[geometry][meshedit]" ) {
    Env e;
    e.Describe();
    std::map<std::pair<common::u64, common::u64>, vec2d_t> before;
    for ( common::usize f = 0; f < e.d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = e.d.faces.pData[f];
        for ( common::u32 k = 0; k < face.cCorners; ++k ) {
            const mesh_source_corner_t &c = e.d.corners.pData[face.iFirstCorner + k];
            before[{ face.sourceId.value, e.d.vertices.pData[c.iVertex].sourceId.value }] = c.attributes.uv0;
        }
    }
    const double volBefore = EditableMesh_SignedVolume( &e.mesh.mesh );
    REQUIRE( MeshSourceEdit_TryMirror( &e.mesh, math::planed_t{ Vec3d_Make( 1, 0, 0 ), -3.0 }, nullptr ) ==
             geometry_status_t::OK );
    CHECK( EditableMesh_SignedVolume( &e.mesh.mesh ) == Catch::Approx( volBefore ) ); // still outward
    e.Describe();
    common::usize cChecked = 0;
    for ( common::usize f = 0; f < e.d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = e.d.faces.pData[f];
        for ( common::u32 k = 0; k < face.cCorners; ++k ) {
            const mesh_source_corner_t &c = e.d.corners.pData[face.iFirstCorner + k];
            const auto it = before.find( { face.sourceId.value, e.d.vertices.pData[c.iVertex].sourceId.value } );
            REQUIRE( it != before.end() );
            CHECK( c.attributes.uv0.x == it->second.x );
            CHECK( c.attributes.uv0.y == it->second.y );
            ++cChecked;
        }
    }
    CHECK( cChecked == 24u );
    CHECK( e.d.vertices.pData[0].position.x == Catch::Approx( 6.0 ) ); // x -> 6 - x
}

TEST_CASE( "Collapse, dissolve, detach, fill, and weld keep identity rules", "[geometry][meshedit]" ) {
    {
        Env e;
        // Keep 11 (not the edge's recorded start in general): 11 must remain
        // at its own position.
        REQUIRE( MeshSourceEdit_TryCollapseEdge( &e.mesh, Id( 11 ), Id( 10 ), nullptr ) == geometry_status_t::OK );
        geometry_mesh_vertex_handle_t h{};
        REQUIRE( MeshSource_TryFindVertex( &e.mesh, Id( 11 ), &h ) );
        CHECK( math::Vec3d_EqualsExact( EditableMesh_GetVertex( &e.mesh.mesh, h )->position, Vec3d_Make( 1, 0, 0 ) ) );
        CHECK_FALSE( MeshSource_TryFindVertex( &e.mesh, Id( 10 ), &h ) );
        REQUIRE( MeshSource_Validate( &e.mesh, &e.allocator ).fault == mesh_source_fault_t::NONE );
    }
    {
        Env e;
        REQUIRE( MeshSourceEdit_TrySplitFace( &e.mesh, Id( 21 ), Id( 14 ), Id( 17 ), nullptr ) == geometry_status_t::OK );
        REQUIRE( MeshSource_TryAssignMissingIds( &e.mesh, &e.ids, nullptr ) == geometry_status_t::OK );
        REQUIRE( MeshSourceEdit_TryDissolveEdge( &e.mesh, Id( 14 ), Id( 17 ), nullptr ) == geometry_status_t::OK );
        CHECK( e.CheckOracle() == 6u );
    }
    {
        Env e;
        common::u32 cMerged = 99;
        REQUIRE( MeshSourceEdit_TryWeld( &e.mesh, 1e-6, &cMerged, nullptr ) == geometry_status_t::OK );
        CHECK( cMerged == 0u ); // nothing is that close
        CHECK( e.CheckOracle() == 6u );
    }
}


// Detach runs on MeshBoundary_DetachFaces (open-boundary contract).
TEST_CASE( "Detach keeps face identity and copies corners verbatim", "[geometry][meshedit]" ) {
    Env e;
    const geometry_source_id_t top[] = { Id( 21 ) };
    mesh_edit_report_t report{};
    REQUIRE( MeshSourceEdit_TryDetachFaces( &e.mesh, common::span_t<const geometry_source_id_t>{ top, 1 }, &report ) ==
             geometry_status_t::OK );
    CHECK( report.stats.cCornersDefaulted == 0u );
    CHECK( EditableMesh_VertexCount( &e.mesh.mesh ) == 12u );
    CHECK( e.CheckOracle() == 6u );
    // The detached cap left a hole; fill it again. The new face takes
    // the attributes of the wall across the chosen boundary edge, so
    // compare against that rather than the oracle's axis rule.
    geometry_mesh_face_handle_t hTop{};
    REQUIRE( MeshSource_TryFindFace( &e.mesh, Id( 21 ), &hTop ) );
}

// Fill hole runs on MeshBoundary_FillHole (open-boundary contract).
TEST_CASE( "Fill hole parents the new face to the face across the edge", "[geometry][meshedit]" ) {
    // Open box (no top): filling the hole through edge (14, 15) copies the
    // -y wall's attributes, the face owning that boundary edge.
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
    for ( int i = 0; i < 8; ++i ) {
        REQUIRE( MeshSourceDescription_TryAddVertex(
                     &d, Vec3d_Make( ( i & 1 ) ? 1.0 : 0.0, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0 ),
                     Id( 10u + static_cast<common::u64>( i ) ), nullptr ) == geometry_status_t::OK );
    }
    const std::vector<std::vector<common::u32>> faces = { { 0, 2, 3, 1 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 },
                                                          { 1, 3, 7, 5 } };
    for ( common::usize f = 0; f < faces.size(); ++f ) {
        REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ faces[f].data(), faces[f].size() },
                                                   Id( 20u + f ), mesh_face_attributes_t{}, nullptr ) ==
                 geometry_status_t::OK );
    }
    Env::ApplyOracle( &d );
    mesh_source_t mesh{};
    REQUIRE( MeshSource_TryBuild( &d, &allocator, &mesh ) == geometry_status_t::OK );
    mesh_edit_report_t report{};
    REQUIRE( MeshSourceEdit_TryFillHole( &mesh, Id( 14 ), Id( 15 ), &report ) == geometry_status_t::OK );
    CHECK( report.stats.cFacesFromParent == 1u );
    CHECK( EditableMesh_FaceCount( &mesh.mesh ) == 6u );
    geometry_source_id_allocator_t ids{ Id( 900 ) };
    REQUIRE( MeshSource_TryAssignMissingIds( &mesh, &ids, nullptr ) == geometry_status_t::OK );
    geometry_mesh_face_handle_t hNew{};
    REQUIRE( MeshSource_TryFindFace( &mesh, Id( 900 + 0 ), &hNew ) );
    CHECK( MeshAttributeStore_GetFace( &mesh.attributes, hNew ).material.value == MaterialFor( Vec3d_Make( 0, -1, 0 ) ) );
    CHECK( MeshSource_Validate( &mesh, &allocator ).fault == mesh_source_fault_t::NONE );
    MeshSource_Shutdown( &mesh );
    MeshSourceDescription_Shutdown( &d );
}

TEST_CASE( "Transforms, attribute setters, and bad IDs", "[geometry][meshedit]" ) {
    Env e;
    math::affine3d_t move = math::CY_AFFINE3D_IDENTITY;
    math::Affine3d_SetComponent( &move, 2, 3, 4.0 );
    REQUIRE( MeshSourceEdit_TryTransform( &e.mesh, move ) == geometry_status_t::OK );
    geometry_mesh_vertex_handle_t h{};
    REQUIRE( MeshSource_TryFindVertex( &e.mesh, Id( 17 ), &h ) );
    CHECK( EditableMesh_GetVertex( &e.mesh.mesh, h )->position.z == 5.0 );

    math::affine3d_t flip = math::CY_AFFINE3D_IDENTITY;
    math::Affine3d_SetComponent( &flip, 0, 0, -1.0 );
    CHECK( MeshSourceEdit_TryTransform( &e.mesh, flip ) == geometry_status_t::UNSUPPORTED );
    math::affine3d_t far = math::CY_AFFINE3D_IDENTITY;
    math::Affine3d_SetComponent( &far, 0, 3, kMeshSourceCoordinateMax );
    CHECK( MeshSourceEdit_TryTransform( &e.mesh, far ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( EditableMesh_GetVertex( &e.mesh.mesh, h )->position.x == 1.0 );

    mesh_corner_attributes_t c{};
    c.uv0 = vec2d_t{ 9.0, 8.0 };
    c.colorRgba = 0x11223344u;
    REQUIRE( MeshSourceEdit_TrySetCornerAttributes( &e.mesh, Id( 21 ), Id( 17 ), c ) == geometry_status_t::OK );
    CHECK( MeshSourceEdit_TrySetCornerAttributes( &e.mesh, Id( 20 ), Id( 17 ), c ) ==
           geometry_status_t::INVALID_HANDLE ); // 17 is not a corner of face 20
    mesh_face_attributes_t fa{};
    fa.material.value = 777u;
    REQUIRE( MeshSourceEdit_TrySetFaceAttributes( &e.mesh, Id( 23 ), fa ) == geometry_status_t::OK );
    e.Describe();
    bool bFoundCorner = false, bFoundFace = false;
    for ( common::usize f = 0; f < e.d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = e.d.faces.pData[f];
        if ( face.sourceId.value == 23u ) { bFoundFace = face.attributes.material.value == 777u; }
        if ( face.sourceId.value != 21u ) { continue; }
        for ( common::u32 k = 0; k < face.cCorners; ++k ) {
            const mesh_source_corner_t &cc = e.d.corners.pData[face.iFirstCorner + k];
            if ( e.d.vertices.pData[cc.iVertex].sourceId.value == 17u ) {
                bFoundCorner = cc.attributes.uv0.x == 9.0 && cc.attributes.colorRgba == 0x11223344u;
            }
        }
    }
    CHECK( bFoundCorner );
    CHECK( bFoundFace );
    CHECK( MeshSourceEdit_TryExtrudeFace( &e.mesh, Id( 999 ), 1.0, nullptr ) == geometry_status_t::INVALID_HANDLE );
    CHECK( MeshSourceEdit_TryMoveVertex( &e.mesh, Id( 17 ), Vec3d_Make( std::nan( "" ), 0, 0 ) ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( MeshSourceEdit_TrySetEdgeAttributes( &e.mesh, Id( 10 ), Id( 11 ), mesh_edge_attributes_t{}, -1.0 ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Source modeling preflights coordinate range and transform viability atomically",
           "[geometry][meshedit][numeric][contract]" ) {
    SECTION( "extrude cannot publish cap vertices outside the source domain" ) {
        Env e;
        ScopedDescription before{ &e.allocator };
        ScopedDescription after{ &e.allocator };
        before.Capture( &e.mesh );

        CHECK( MeshSourceEdit_TryExtrudeFace(
                   &e.mesh,
                   Id( 25u ),
                   kMeshSourceCoordinateMax,
                   nullptr ) == geometry_status_t::NUMERIC_FAILURE );
        after.Capture( &e.mesh );
        CHECK( MeshSourceDescription_Equal(
            &before.value, &after.value ) );
        CHECK( MeshSource_Validate(
                   &e.mesh, &e.allocator ).fault ==
               mesh_source_fault_t::NONE );
    }

    SECTION( "snap cannot move an in-range source vertex out of range" ) {
        Env e;
        const double m = kMeshSourceCoordinateMax;
        for ( common::u64 i = 0u; i < 8u; ++i ) {
            const math::vec3d_t position = Vec3d_Make(
                ( i & 1u ) != 0u ? 0.9 * m : 0.3 * m,
                ( i & 2u ) != 0u ? 0.3 * m : -0.3 * m,
                ( i & 4u ) != 0u ? 0.3 * m : -0.3 * m );
            REQUIRE( MeshSourceEdit_TryMoveVertex(
                         &e.mesh,
                         Id( 10u + i ),
                         position ) == geometry_status_t::OK );
        }
        REQUIRE( MeshSource_Validate(
                     &e.mesh, &e.allocator ).fault ==
                 mesh_source_fault_t::NONE );
        ScopedDescription before{ &e.allocator };
        ScopedDescription after{ &e.allocator };
        before.Capture( &e.mesh );

        CHECK( MeshSourceEdit_TrySnapToGrid(
                   &e.mesh, 0.6 * m ) ==
               geometry_status_t::NUMERIC_FAILURE );
        after.Capture( &e.mesh );
        CHECK( MeshSourceDescription_Equal(
            &before.value, &after.value ) );
    }

    SECTION( "tiny nonsingular transforms that destroy face viability are rejected" ) {
        Env e;
        ScopedDescription before{ &e.allocator };
        ScopedDescription after{ &e.allocator };
        before.Capture( &e.mesh );

        constexpr double scale = 1.0e-9;
        math::affine3d_t transform = math::CY_AFFINE3D_IDENTITY;
        math::Affine3d_SetComponent( &transform, 0u, 0u, 0.0 );
        math::Affine3d_SetComponent( &transform, 0u, 1u, -scale );
        math::Affine3d_SetComponent( &transform, 1u, 0u, scale );
        math::Affine3d_SetComponent( &transform, 1u, 1u, 0.0 );
        math::Affine3d_SetComponent( &transform, 2u, 2u, scale );

        CHECK( MeshSourceEdit_TryTransform(
                   &e.mesh, transform ) ==
               geometry_status_t::DEGENERATE );
        after.Capture( &e.mesh );
        CHECK( MeshSourceDescription_Equal(
            &before.value, &after.value ) );
        CHECK( MeshSource_Validate(
                   &e.mesh, &e.allocator ).fault ==
               mesh_source_fault_t::NONE );
    }
}

TEST_CASE( "Modeling gate: extrude through a document transaction with exact undo and redo",
           "[geometry][meshedit][document]" ) {
    Env e;
    geometry_policy_t policy{};
    geometry_document_t doc{};
    REQUIRE( GeometryDocument_Init( &doc, &e.allocator, policy ) == geometry_status_t::OK );
    geometry_mesh_delta_t delta{};
    REQUIRE( GeometryMeshDelta_Init( &delta, &e.allocator ) == geometry_status_t::OK );
    e.Describe();
    geometry_revision_t rev = 0;
    REQUIRE( GeometryMeshCommand_TryAdd( &doc, &e.d, &delta, &rev ) == geometry_status_t::OK );

    geometry_mesh_transaction_t txn{};
    REQUIRE( GeometryMeshTransaction_Begin( &txn, &doc, Id( 1 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSourceEdit_TryExtrudeFace( GeometryMeshTransaction_Working( &txn ), Id( 21 ), 0.5, nullptr ) ==
             geometry_status_t::OK );
    bool bChanged = false;
    REQUIRE( GeometryMeshTransaction_Commit( &txn, &delta, &rev, &bChanged ) == geometry_status_t::OK );
    REQUIRE( bChanged );
    // The cap kept face 21; 4 walls + 4 vertices got fresh registry IDs.
    CHECK( GeometrySourceIdRegistry_Contains( &doc.sourceIds, Id( 21 ) ) );
    CHECK( delta.after.faces.nCount == 10u );
    CHECK( delta.after.vertices.nCount == 12u );

    // Deterministic: the same edit on a fresh copy yields the same result.
    {
        mesh_source_t again{};
        REQUIRE( MeshSource_TryBuild( &delta.before, &e.allocator, &again ) == geometry_status_t::OK );
        REQUIRE( MeshSourceEdit_TryExtrudeFace( &again, Id( 21 ), 0.5, nullptr ) == geometry_status_t::OK );
        geometry_source_id_allocator_t ids{ Id( 1 + 14 ) }; // same next ID the registry used
        ids.next = delta.after.vertices.pData[8].sourceId; // first fresh ID
        REQUIRE( MeshSource_TryAssignMissingIds( &again, &ids, nullptr ) == geometry_status_t::OK );
        mesh_source_description_t d2{};
        REQUIRE( MeshSourceDescription_Init( &d2, &e.allocator, Id( 1 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSource_TryDescribe( &again, &d2 ) == geometry_status_t::OK );
        CHECK( MeshSourceDescription_Equal( &d2, &delta.after ) );
        MeshSourceDescription_Shutdown( &d2 );
        MeshSource_Shutdown( &again );
    }

    mesh_source_description_t snapshotAfter{};
    REQUIRE( MeshSourceDescription_Init( &snapshotAfter, &e.allocator, Id( 1 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSourceDescription_TryCopy( &snapshotAfter, &delta.after ) == geometry_status_t::OK );

    GeometryMeshDelta_Invert( &delta );
    REQUIRE( GeometryMeshDelta_TryApply( &delta, &doc, &rev ) == geometry_status_t::OK );
    mesh_source_description_t now{};
    REQUIRE( MeshSourceDescription_Init( &now, &e.allocator, Id( 1 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( GeometryDocument_FindMesh( &doc, Id( 1 ) ), &now ) == geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal( &now, &e.d ) );

    GeometryMeshDelta_Invert( &delta );
    REQUIRE( GeometryMeshDelta_TryApply( &delta, &doc, &rev ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( GeometryDocument_FindMesh( &doc, Id( 1 ) ), &now ) == geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal( &now, &snapshotAfter ) );

    MeshSourceDescription_Shutdown( &now );
    MeshSourceDescription_Shutdown( &snapshotAfter );
    GeometryMeshDelta_Shutdown( &delta );
    GeometryDocument_Shutdown( &doc );
}

} // namespace cypher::editor::geometry
