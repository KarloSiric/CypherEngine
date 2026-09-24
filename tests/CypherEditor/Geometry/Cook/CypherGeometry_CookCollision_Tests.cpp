//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookCollision_Tests.cpp
//  Purpose: Contract tests for cook dependency keys and the collision cook.
//  Details: Gates exercised:
//             - DependencyGraph: change one bounded brush attribute,
//               invalidate exactly the affected product, keep byte-identical
//               keys for every other product;
//             - Cook: same source and policy cook to identical outputs and
//               hashes, and an incremental cook (reusing unchanged surfaces)
//               is byte-identical to a full cook;
//             - float conversion is checked: a triangle collapsed by binary32
//               rounding is dropped with a diagnostic naming its face.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookCollision.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_MeshTransaction.h"
#include "CypherGeometry_MeshSourceModeling.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

namespace cypher::editor::geometry {

using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

struct World {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_document_t doc{};
    geometry_source_id_allocator_t brushIds{};
    geometry_revision_t rev{ 0 };

    explicit World( const geometry_policy_t &p = geometry_policy_t{} ) : policy( p ) {
        REQUIRE( GeometryDocument_Init( &doc, &allocator, policy ) == geometry_status_t::OK );
    }
    ~World() { GeometryDocument_Shutdown( &doc ); }

    geometry_source_id_t AddBox( math::vec3d_t center ) {
        brush_solid_t brush{};
        REQUIRE( BrushGenerator_TryMakeBox( &brush, &allocator, policy, &brushIds, center, Vec3d_Make( 1, 1, 1 ) ) ==
                 geometry_status_t::OK );
        const geometry_source_id_t id = brush.sourceId;
        REQUIRE( GeometryDocument_TryAddBrush( &doc, &brush ) == geometry_status_t::OK );
        BrushSolid_Shutdown( &brush );
        return id;
    }

    void AddCube( common::u64 root, double offset ) {
        mesh_source_description_t d{};
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( root ) ) == geometry_status_t::OK );
        for ( int i = 0; i < 8; ++i ) {
            REQUIRE( MeshSourceDescription_TryAddVertex(
                         &d, Vec3d_Make( ( ( i & 1 ) ? 1.0 : 0.0 ) + offset, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0 ),
                         Id( root + 1u + static_cast<common::u64>( i ) ), nullptr ) == geometry_status_t::OK );
        }
        const std::vector<std::vector<common::u32>> faces = {
            { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for ( common::usize f = 0; f < faces.size(); ++f ) {
            REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ faces[f].data(), faces[f].size() },
                                                       Id( root + 9u + f ), mesh_face_attributes_t{}, nullptr ) ==
                     geometry_status_t::OK );
        }
        AddMesh( &d );
        MeshSourceDescription_Shutdown( &d );
    }

    void AddMesh( const mesh_source_description_t *pD ) {
        geometry_mesh_delta_t delta{};
        REQUIRE( GeometryMeshDelta_Init( &delta, &allocator ) == geometry_status_t::OK );
        REQUIRE( GeometryMeshCommand_TryAdd( &doc, pD, &delta, &rev ) == geometry_status_t::OK );
        GeometryMeshDelta_Shutdown( &delta );
    }

    // Moves one brush side outward by `amount` (remove + re-add keeps IDs).
    void PushSide( geometry_source_id_t brushId, common::usize iSide, double amount ) {
        brush_solid_t copy{};
        REQUIRE( BrushSolid_DeepCopy( &copy, GeometryDocument_FindBrush( &doc, brushId ), &allocator, policy.limits ) ==
                 geometry_status_t::OK );
        copy.sides.pData[iSide].plane.d -= amount;
        REQUIRE( GeometryDocument_TryRemoveBrush( &doc, brushId ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrush( &doc, &copy ) == geometry_status_t::OK );
        BrushSolid_Shutdown( &copy );
        ++doc.revision;
    }
};

struct Cooked {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_snapshot_t snap{};
    cook_key_set_t keys{};
    cook_collision_t col{};
    Cooked() {
        REQUIRE( CookKeySet_Init( &keys, &allocator ) == geometry_status_t::OK );
        REQUIRE( CookCollision_Init( &col, &allocator ) == geometry_status_t::OK );
    }
    ~Cooked() {
        CookCollision_Shutdown( &col );
        CookKeySet_Shutdown( &keys );
        GeometrySnapshot_Shutdown( &snap );
    }
    void Run( const geometry_document_t *pDoc, const cook_collision_t *pPrevious = nullptr ) {
        REQUIRE( GeometrySnapshot_TakeFromDocument( &snap, pDoc ) == geometry_status_t::OK );
        REQUIRE( CookKeySet_TryBuild( &keys, &snap ) == geometry_status_t::OK );
        REQUIRE( CookCollision_TryBuild( &snap, &keys, pPrevious, &col ) == geometry_status_t::OK );
    }
};

bool SameBytes( const cook_collision_t &a, const cook_collision_t &b ) {
    auto eq = []( const auto &x, const auto &y ) {
        return x.nCount == y.nCount && ( x.nCount == 0u || std::memcmp( x.pData, y.pData, sizeof( *x.pData ) * x.nCount ) == 0 );
    };
    if ( !eq( a.positions, b.positions ) || !eq( a.indices, b.indices ) || !eq( a.triangleElement, b.triangleElement ) ||
         a.surfaces.nCount != b.surfaces.nCount || a.diagnostics.nCount != b.diagnostics.nCount ) {
        return false;
    }
    for ( common::usize i = 0; i < a.surfaces.nCount; ++i ) {
        const cook_collision_surface_t &s = a.surfaces.pData[i], &t = b.surfaces.pData[i];
        if ( s.objectId.value != t.objectId.value || s.cVertices != t.cVertices || s.cTriangles != t.cTriangles ||
             s.iFirstVertex != t.iFirstVertex || s.iFirstTriangle != t.iFirstTriangle || s.bConvex != t.bConvex ||
             s.bClosed != t.bClosed || !common::ContentHash_Equals( s.productKey, t.productKey ) ||
             !common::ContentHash_Equals( s.dataHash, t.dataHash ) ) {
            return false;
        }
    }
    return common::ContentHash_Equals( a.contentHash, b.contentHash );
}

} // namespace

TEST_CASE( "Cook keys are deterministic and independent of document order", "[geometry][cook]" ) {
    World a, b;
    const geometry_source_id_t boxA = a.AddBox( Vec3d_Make( 5, 0, 0 ) );
    a.AddCube( 1000, 10.0 );
    // Same content added in the opposite order.
    b.AddCube( 1000, 10.0 );
    const geometry_source_id_t boxB = b.AddBox( Vec3d_Make( 5, 0, 0 ) );
    REQUIRE( boxA.value == boxB.value );
    Cooked ca, cb;
    ca.Run( &a.doc );
    cb.Run( &b.doc );
    REQUIRE( ca.keys.keys.nCount == 2u );
    REQUIRE( cb.keys.keys.nCount == 2u );
    for ( common::usize i = 0; i < 2; ++i ) {
        CHECK( ca.keys.keys.pData[i].sourceId.value == cb.keys.keys.pData[i].sourceId.value );
        CHECK( common::ContentHash_Equals( ca.keys.keys.pData[i].sourceHash, cb.keys.keys.pData[i].sourceHash ) );
    }
    CHECK( common::ContentHash_Equals( ca.keys.policyHash, cb.keys.policyHash ) );
    const cook_source_key_t *pMesh = CookKeySet_Find( &ca.keys, Id( 1000 ) );
    REQUIRE( pMesh != nullptr );
    CHECK( pMesh->kind == cook_source_kind_t::MESH );
    CHECK_FALSE( common::ContentHash_Equals( CookKeys_ProductKey( &ca.keys, *pMesh, cook_product_kind_t::COLLISION ),
                                             CookKeys_ProductKey( &ca.keys, *pMesh, cook_product_kind_t::RENDER ) ) );
    CHECK( CookKeySet_Find( &ca.keys, Id( 424242 ) ) == nullptr );
    // Same input, same output, twice.
    CHECK( SameBytes( ca.col, cb.col ) );
}

TEST_CASE( "DependencyGraph gate: one brush edit invalidates exactly one product", "[geometry][cook]" ) {
    World w;
    const geometry_source_id_t b0 = w.AddBox( Vec3d_Make( 0, 0, 0 ) );
    const geometry_source_id_t b1 = w.AddBox( Vec3d_Make( 5, 0, 0 ) );
    const geometry_source_id_t b2 = w.AddBox( Vec3d_Make( 10, 0, 0 ) );
    w.AddCube( 1000, 20.0 );
    Cooked before;
    before.Run( &w.doc );

    w.PushSide( b1, 0, 0.5 );
    Cooked after;
    after.Run( &w.doc );

    cook_key_diff_t diff{};
    REQUIRE( CookKeyDiff_Init( &diff, &w.allocator ) == geometry_status_t::OK );
    REQUIRE( CookKeyDiff_TryCompute( &before.keys, &after.keys, &diff ) == geometry_status_t::OK );
    CHECK_FALSE( diff.bPolicyChanged );
    REQUIRE( diff.changed.nCount == 1u );
    CHECK( diff.changed.pData[0].value == b1.value );
    CHECK( diff.unchanged.nCount == 3u );
    CHECK( diff.added.nCount == 0u );
    CHECK( diff.removed.nCount == 0u );
    for ( const geometry_source_id_t id : { b0, b2, Id( 1000 ) } ) {
        const cook_source_key_t *p0 = CookKeySet_Find( &before.keys, id );
        const cook_source_key_t *p1 = CookKeySet_Find( &after.keys, id );
        REQUIRE( p0 != nullptr );
        REQUIRE( p1 != nullptr );
        const common::content_hash_t k0 = CookKeys_ProductKey( &before.keys, *p0, cook_product_kind_t::COLLISION );
        const common::content_hash_t k1 = CookKeys_ProductKey( &after.keys, *p1, cook_product_kind_t::COLLISION );
        CHECK( std::memcmp( &k0, &k1, sizeof( k0 ) ) == 0 );
    }

    // Mesh removal and a new mesh show up as removed / added.
    geometry_mesh_delta_t delta{};
    REQUIRE( GeometryMeshDelta_Init( &delta, &w.allocator ) == geometry_status_t::OK );
    REQUIRE( GeometryMeshCommand_TryRemove( &w.doc, Id( 1000 ), &delta, &w.rev ) == geometry_status_t::OK );
    GeometryMeshDelta_Shutdown( &delta );
    w.AddCube( 2000, 30.0 );
    Cooked third;
    third.Run( &w.doc );
    REQUIRE( CookKeyDiff_TryCompute( &after.keys, &third.keys, &diff ) == geometry_status_t::OK );
    CHECK( diff.removed.nCount == 1u );
    CHECK( diff.added.nCount == 1u );
    CHECK( diff.unchanged.nCount == 3u );
    CookKeyDiff_Shutdown( &diff );
}

TEST_CASE( "A policy change invalidates every product", "[geometry][cook]" ) {
    geometry_policy_t p1{};
    geometry_policy_t p2{};
    p2.numerical.fWeldDistance *= 2.0;
    World a( p1 ), b( p2 );
    a.AddBox( Vec3d_Make( 0, 0, 0 ) );
    b.AddBox( Vec3d_Make( 0, 0, 0 ) );
    Cooked ca, cb;
    ca.Run( &a.doc );
    cb.Run( &b.doc );
    CHECK( common::ContentHash_Equals( ca.keys.keys.pData[0].sourceHash, cb.keys.keys.pData[0].sourceHash ) );
    cook_key_diff_t diff{};
    REQUIRE( CookKeyDiff_Init( &diff, &a.allocator ) == geometry_status_t::OK );
    REQUIRE( CookKeyDiff_TryCompute( &ca.keys, &cb.keys, &diff ) == geometry_status_t::OK );
    CHECK( diff.bPolicyChanged );
    CHECK( diff.changed.nCount == 1u );
    CHECK_FALSE( common::ContentHash_Equals( ca.col.surfaces.pData[0].productKey, cb.col.surfaces.pData[0].productKey ) );
    CookKeyDiff_Shutdown( &diff );
}

TEST_CASE( "Collision cook maps every triangle to its source element", "[geometry][cook]" ) {
    World w;
    const geometry_source_id_t box = w.AddBox( Vec3d_Make( 0, 0, 0 ) );
    w.AddCube( 1000, 5.0 );
    Cooked c;
    c.Run( &w.doc );
    REQUIRE( c.col.surfaces.nCount == 2u );
    CHECK( c.col.diagnostics.nCount == 0u );
    CHECK( c.col.cSurfacesRebuilt == 2u );
    CHECK( c.col.cSurfacesReused == 0u );
    const cook_collision_surface_t &sBox = c.col.surfaces.pData[0];
    const cook_collision_surface_t &sMesh = c.col.surfaces.pData[1];
    CHECK( sBox.objectId.value == box.value );
    CHECK( sBox.bConvex );
    CHECK( sBox.bClosed );
    CHECK( sBox.cVertices == 8u );
    CHECK( sBox.cTriangles == 12u );
    CHECK( sMesh.objectId.value == 1000u );
    CHECK_FALSE( sMesh.bConvex );
    CHECK( sMesh.bClosed );
    CHECK( sMesh.cTriangles == 12u );

    const brush_solid_t *pBrush = GeometryDocument_FindBrush( &w.doc, box );
    for ( common::u32 t = 0; t < sBox.cTriangles; ++t ) {
        const common::u64 e = c.col.triangleElement.pData[sBox.iFirstTriangle + t].value;
        bool bSide = false;
        for ( common::usize s = 0; s < pBrush->sides.nCount; ++s ) { bSide = bSide || pBrush->sides.pData[s].sourceId.value == e; }
        CHECK( bSide );
    }
    for ( common::u32 t = 0; t < sMesh.cTriangles; ++t ) {
        const common::u64 e = c.col.triangleElement.pData[sMesh.iFirstTriangle + t].value;
        CHECK( e >= 1009u );
        CHECK( e <= 1014u );
        // Surface-local indices stay inside the surface.
        for ( int k = 0; k < 3; ++k ) {
            CHECK( c.col.indices.pData[( sMesh.iFirstTriangle + t ) * 3u + k] < sMesh.cVertices );
        }
    }
    // Float positions are the rounded doubles.
    CHECK( c.col.positions.pData[sMesh.iFirstVertex].x >= 5.0f );
}

TEST_CASE( "Incremental collision cook reuses unchanged surfaces byte-identically", "[geometry][cook]" ) {
    World w;
    w.AddBox( Vec3d_Make( 0, 0, 0 ) );
    const geometry_source_id_t moved = w.AddBox( Vec3d_Make( 5, 0, 0 ) );
    w.AddCube( 1000, 10.0 );
    Cooked first;
    first.Run( &w.doc );

    // Edit the mesh (extrude) and one brush.
    geometry_mesh_transaction_t txn{};
    REQUIRE( GeometryMeshTransaction_Begin( &txn, &w.doc, Id( 1000 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSourceEdit_TryExtrudeFace( GeometryMeshTransaction_Working( &txn ), Id( 1010 ), 0.25, nullptr ) ==
             geometry_status_t::OK );
    geometry_mesh_delta_t delta{};
    REQUIRE( GeometryMeshDelta_Init( &delta, &w.allocator ) == geometry_status_t::OK );
    bool bChanged = false;
    REQUIRE( GeometryMeshTransaction_Commit( &txn, &delta, &w.rev, &bChanged ) == geometry_status_t::OK );
    GeometryMeshDelta_Shutdown( &delta );
    w.PushSide( moved, 1, 0.25 );

    Cooked incremental, full;
    incremental.Run( &w.doc, &first.col );
    full.Run( &w.doc );
    CHECK( incremental.col.cSurfacesReused == 1u );
    CHECK( incremental.col.cSurfacesRebuilt == 2u );
    CHECK( full.col.cSurfacesReused == 0u );
    CHECK( SameBytes( incremental.col, full.col ) );
    CHECK_FALSE( common::ContentHash_Equals( first.col.contentHash, full.col.contentHash ) );
    // The reused surface is bit-for-bit the one from the first cook.
    CHECK( common::ContentHash_Equals( first.col.surfaces.pData[0].dataHash, incremental.col.surfaces.pData[0].dataHash ) );
    // The extruded mesh surface has grown.
    CHECK( incremental.col.surfaces.pData[2].cTriangles == 20u );
}

TEST_CASE( "Triangles collapsed by float rounding are dropped with a diagnostic", "[geometry][cook]" ) {
    World w;
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &w.allocator, Id( 500 ) ) == geometry_status_t::OK );
    // At x = 1e6 binary32 spacing is 0.0625: the first two vertices round to
    // the same float although they are 0.01 apart in double.
    REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( 1.0e6, 0, 0 ), Id( 501 ), nullptr ) == geometry_status_t::OK );
    REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( 1.0e6 + 0.01, 0, 0 ), Id( 502 ), nullptr ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( 1.0e6, 1, 0 ), Id( 503 ), nullptr ) == geometry_status_t::OK );
    REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( 1.0e6 + 1, 1, 0 ), Id( 504 ), nullptr ) ==
             geometry_status_t::OK );
    const common::u32 sliver[] = { 0, 1, 2 };
    const common::u32 good[] = { 1, 3, 2 };
    REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ sliver, 3 }, Id( 510 ),
                                               mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
    REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ good, 3 }, Id( 511 ),
                                               mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
    w.AddMesh( &d );
    MeshSourceDescription_Shutdown( &d );

    Cooked c;
    c.Run( &w.doc );
    REQUIRE( c.col.surfaces.nCount == 1u );
    CHECK( c.col.surfaces.pData[0].cTriangles == 1u );
    CHECK_FALSE( c.col.surfaces.pData[0].bClosed );
    REQUIRE( c.col.diagnostics.nCount == 1u );
    CHECK( c.col.diagnostics.pData[0].kind == cook_diagnostic_kind_t::DEGENERATE_AFTER_FLOAT );
    CHECK( c.col.diagnostics.pData[0].objectId.value == 500u );
    CHECK( c.col.diagnostics.pData[0].elementId.value == 510u );
    CHECK( c.col.triangleElement.pData[0].value == 511u );

    // Reusing the surface replays its diagnostic.
    Cooked again;
    again.Run( &w.doc, &c.col );
    CHECK( again.col.cSurfacesReused == 1u );
    CHECK( SameBytes( again.col, c.col ) );
}

TEST_CASE( "Collision cook rejects mismatched keys and aliasing", "[geometry][cook]" ) {
    World w;
    w.AddBox( Vec3d_Make( 0, 0, 0 ) );
    Cooked c;
    c.Run( &w.doc );
    w.AddBox( Vec3d_Make( 4, 0, 0 ) );
    ++w.doc.revision;
    geometry_snapshot_t newer{};
    REQUIRE( GeometrySnapshot_TakeFromDocument( &newer, &w.doc ) == geometry_status_t::OK );
    // Keys from the old snapshot do not describe the new one.
    CHECK( CookCollision_TryBuild( &newer, &c.keys, nullptr, &c.col ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( CookCollision_TryBuild( &c.snap, &c.keys, &c.col, &c.col ) == geometry_status_t::INVALID_ARGUMENT );
    cook_collision_t never{};
    CHECK( CookCollision_TryBuild( &c.snap, &c.keys, nullptr, &never ) == geometry_status_t::NOT_INITIALIZED );
    GeometrySnapshot_Shutdown( &newer );
}

} // namespace cypher::editor::geometry
