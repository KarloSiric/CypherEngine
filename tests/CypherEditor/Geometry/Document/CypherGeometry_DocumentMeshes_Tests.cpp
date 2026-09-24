//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentMeshes_Tests.cpp
//  Purpose: Integration tests for meshes as first-class document objects:
//           ownership and identity bookkeeping, edit transactions,
//           undo/redo deltas, immutable snapshots, and save/load.
//  Details: The vertical-slice oracle is a full edit cycle: add a mesh,
//           edit it in a transaction (new topology gets fresh IDs), undo,
//           redo, snapshot in between, save, load, and compare canonical
//           descriptions and registry state at every step. Brushes live in
//           the same document throughout to prove cross-kind identity rules.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_MeshTransaction.h"
#include "CypherGeometry_MeshSerialization.h"
#include "CypherGeometry_Snapshot.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_MeshTopologyOps.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

namespace cypher::editor::geometry {

using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

// Canonical unit cube with root `root`, vertex IDs root+1.., face IDs
// root+9.. and a few non-default attributes.
void CubeDescription( mesh_source_description_t *pD, common::u64 root, double offset = 0.0 ) {
    MeshSourceDescription_Clear( pD, Id( root ) );
    for ( int i = 0; i < 8; ++i ) {
        REQUIRE( MeshSourceDescription_TryAddVertex(
                     pD,
                     Vec3d_Make( ( ( i & 1 ) ? 1.0 : 0.0 ) + offset, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0 ),
                     Id( root + 1u + static_cast<common::u64>( i ) ), nullptr ) == geometry_status_t::OK );
    }
    const std::vector<std::vector<common::u32>> faces = {
        { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
    for ( common::usize f = 0; f < faces.size(); ++f ) {
        mesh_face_attributes_t a{};
        a.material.value = f == 1 ? 42u : 0u;
        REQUIRE( MeshSourceDescription_TryAddFace(
                     pD, common::span_t<const common::u32>{ faces[f].data(), faces[f].size() },
                     Id( root + 9u + f ), a, nullptr ) == geometry_status_t::OK );
    }
    pD->corners.pData[5].attributes.uv0 = math::vec2d_t{ 0.5, 0.25 };
    mesh_edge_attributes_t hard{};
    hard.flags = MESH_EDGE_FLAG_HARD;
    REQUIRE( MeshSourceDescription_TrySetEdge( pD, 0, 1, hard, 0.75 ) == geometry_status_t::OK );
}

struct Fixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_document_t doc{};
    geometry_source_id_allocator_t brushIds{}; // brush IDs start at 1
    mesh_source_description_t desc{};
    geometry_mesh_delta_t delta{};

    Fixture() {
        REQUIRE( GeometryDocument_Init( &doc, &allocator, policy ) == geometry_status_t::OK );
        REQUIRE( MeshSourceDescription_Init( &desc, &allocator, Id( 1000 ) ) == geometry_status_t::OK );
        REQUIRE( GeometryMeshDelta_Init( &delta, &allocator ) == geometry_status_t::OK );
    }
    ~Fixture() {
        GeometryMeshDelta_Shutdown( &delta );
        MeshSourceDescription_Shutdown( &desc );
        GeometryDocument_Shutdown( &doc );
    }
    void AddBrush() {
        brush_solid_t brush{};
        REQUIRE( BrushGenerator_TryMakeBox( &brush, &allocator, policy, &brushIds, Vec3d_Make( 5, 5, 5 ),
                                            Vec3d_Make( 1, 1, 1 ) ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrush( &doc, &brush ) == geometry_status_t::OK );
        BrushSolid_Shutdown( &brush );
    }
    // Current canonical description of document mesh `id`.
    bool Describe( common::u64 id, mesh_source_description_t *pOut ) {
        const mesh_source_t *pMesh = GeometryDocument_FindMesh( &doc, Id( id ) );
        return pMesh != nullptr && MeshSource_TryDescribe( pMesh, pOut ) == geometry_status_t::OK;
    }
    bool Live( common::u64 id ) { return GeometrySourceIdRegistry_Contains( &doc.sourceIds, Id( id ) ); }
    bool Claimed( common::u64 id ) { return common::HashSet_Contains( &doc.sourceIds.claimedIds, Id( id ) ); }
};

struct ScopedDesc {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    ScopedDesc() { REQUIRE( MeshSourceDescription_Init( &d, &allocator, GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK ); }
    ~ScopedDesc() { MeshSourceDescription_Shutdown( &d ); }
};

struct mesh_document_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFreeCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *MeshDocumentFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<mesh_document_failure_allocator_state_t *>( pUserData );
    const common::usize iAllocation = pState->cAllocationCalls++;
    if ( iAllocation == pState->iFailure ) { return nullptr; }
    void *pMemory = common::Allocator_Allocate( common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) { ++pState->cSuccessfulAllocations; }
    return pMemory;
}

void MeshDocumentFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<mesh_document_failure_allocator_state_t *>( pUserData );
    ++pState->cFreeCalls;
    common::Allocator_Free( common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeMeshDocumentFailureAllocator(
    mesh_document_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        MeshDocumentFailureAllocate,
        nullptr,
        MeshDocumentFailureFree,
        pState
    };
}

bool DescriptionUsesAllocator(
    const mesh_source_description_t &description,
    const common::allocator_t *pAllocator ) noexcept
{
    return description.vertices.pAllocator == pAllocator &&
           description.corners.pAllocator == pAllocator &&
           description.faces.pAllocator == pAllocator &&
           description.edges.pAllocator == pAllocator;
}

struct ScopedKeyValueDocument {
    common::key_value_document_t *pDocument{ nullptr };

    ScopedKeyValueDocument() = default;
    explicit ScopedKeyValueDocument(
        common::key_value_document_t *pValue ) noexcept
        : pDocument( pValue ) {}
    ScopedKeyValueDocument( const ScopedKeyValueDocument & ) = delete;
    ScopedKeyValueDocument &operator=(
        const ScopedKeyValueDocument & ) = delete;
    ScopedKeyValueDocument(
        ScopedKeyValueDocument &&other ) noexcept
        : pDocument( other.pDocument ) {
        other.pDocument = nullptr;
    }

    ~ScopedKeyValueDocument() {
        common::KeyValue_DestroyDocument( pDocument );
    }
};

ScopedKeyValueDocument ParseDocument(
    common::string_view_t text,
    const common::allocator_t *pAllocator ) {
    common::key_value_document_desc_t desc{};
    desc.pAllocator = pAllocator;
    ScopedKeyValueDocument parsed{
        common::KeyValue_CreateDocument( desc )
    };
    REQUIRE( parsed.pDocument != nullptr );
    REQUIRE( common::KeyValue_ParseText(
                 text, {}, parsed.pDocument ).status ==
             common::key_value_parse_status_t::OK );
    return parsed;
}

bool IsCanonicalEmptyDocument(
    const geometry_document_t &document ) noexcept {
    return !GeometryDocument_IsInitialized( &document ) &&
           document.brushes.pData == nullptr &&
           document.brushes.nCount == 0u &&
           document.brushes.nCapacity == 0u &&
           document.brushes.pAllocator == nullptr &&
           document.meshes.pData == nullptr &&
           document.meshes.nCount == 0u &&
           document.meshes.nCapacity == 0u &&
           document.meshes.pAllocator == nullptr &&
           document.sourceIds.claimedIds.pSlots == nullptr &&
           document.sourceIds.liveIds.pSlots == nullptr &&
           document.sourceIds.pAllocator == nullptr &&
           document.sourceIds.allocator.next.value == 1u;
}

} // namespace

TEST_CASE( "Document owns meshes with atomic identity bookkeeping", "[geometry][document][meshes]" ) {
    Fixture f;
    f.AddBrush(); // IDs 1..7
    CubeDescription( &f.desc, 1000 );
    geometry_revision_t rev = 0;
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &rev ) == geometry_status_t::OK );
    CHECK( rev == 1u );
    CHECK( GeometryDocument_MeshCount( &f.doc ) == 1u );
    CHECK( f.delta.kind == geometry_mesh_delta_kind_t::MESH_ADDED );
    CHECK( MeshSourceDescription_Equal( &f.delta.after, &f.desc ) );
    for ( common::u64 id = 1000; id <= 1014; ++id ) { CHECK( f.Live( id ) ); }
    CHECK( GeometrySourceIdRegistry_Count( &f.doc.sourceIds ) == 7u + 15u );

    // Same mesh again: its root is already present.
    CHECK( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &rev ) == geometry_status_t::IDENTITY_CONFLICT );
    // A different mesh reusing one brush side ID is a cross-kind conflict,
    // and nothing changes.
    CubeDescription( &f.desc, 2000, 3.0 );
    f.desc.vertices.pData[4].sourceId = Id( 3 );
    const common::usize cLive = GeometrySourceIdRegistry_Count( &f.doc.sourceIds );
    CHECK( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &rev ) == geometry_status_t::IDENTITY_CONFLICT );
    CHECK( GeometryDocument_MeshCount( &f.doc ) == 1u );
    CHECK( GeometrySourceIdRegistry_Count( &f.doc.sourceIds ) == cLive );
    CHECK_FALSE( f.Live( 2000 ) );
    CHECK( f.doc.revision == 1u );

    // Remove retires every ID but keeps it claimed, so it can never be
    // reissued to something else.
    REQUIRE( GeometryMeshCommand_TryRemove( &f.doc, Id( 1000 ), &f.delta, &rev ) == geometry_status_t::OK );
    CHECK( rev == 2u );
    CHECK( GeometryDocument_MeshCount( &f.doc ) == 0u );
    CHECK_FALSE( f.Live( 1005 ) );
    CHECK( f.Claimed( 1005 ) );
    CHECK( f.delta.kind == geometry_mesh_delta_kind_t::MESH_REMOVED );

    // Undo the removal: the mesh returns under its original identity.
    GeometryMeshDelta_Invert( &f.delta );
    REQUIRE( GeometryMeshDelta_TryApply( &f.delta, &f.doc, &rev ) == geometry_status_t::OK );
    CHECK( rev == 3u );
    CHECK( f.Live( 1005 ) );
    ScopedDesc now;
    REQUIRE( f.Describe( 1000, &now.d ) );
    CHECK( MeshSourceDescription_Equal( &now.d, &f.delta.after ) );
    // Applying the same ADDED delta again is refused: the mesh exists.
    CHECK( GeometryMeshDelta_TryApply( &f.delta, &f.doc, &rev ) == geometry_status_t::STALE_REVISION );
    CHECK( f.doc.revision == 3u );
}

TEST_CASE( "Mesh edit transaction commits new topology with fresh IDs; undo and redo are exact",
           "[geometry][document][meshes]" ) {
    Fixture f;
    f.AddBrush();
    CubeDescription( &f.desc, 1000 );
    geometry_revision_t rev = 0;
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &rev ) == geometry_status_t::OK );
    const common::u64 nextBefore = f.doc.sourceIds.allocator.next.value;
    ScopedDesc original;
    REQUIRE( f.Describe( 1000, &original.d ) );

    geometry_mesh_transaction_t txn{};
    REQUIRE( GeometryMeshTransaction_Begin( &txn, &f.doc, Id( 1000 ) ) == geometry_status_t::OK );
    CHECK( GeometryMeshTransaction_Begin( &txn, &f.doc, Id( 1000 ) ) == geometry_status_t::TRANSACTION_ACTIVE );
    mesh_source_t *pWork = GeometryMeshTransaction_Working( &txn );
    REQUIRE( pWork != nullptr );
    geometry_mesh_face_handle_t hTop{};
    REQUIRE( MeshSource_TryFindFace( pWork, Id( 1010 ), &hTop ) );
    const mesh_extrude_face_result_t ex = MeshOps_ExtrudeFace( &pWork->mesh, hTop, 0.5 );
    REQUIRE( ex.status == geometry_status_t::OK );
    // The document has not changed while the edit is in flight.
    ScopedDesc during;
    REQUIRE( f.Describe( 1000, &during.d ) );
    CHECK( MeshSourceDescription_Equal( &during.d, &original.d ) );
    CHECK( f.doc.revision == 1u );

    bool bChanged = false;
    REQUIRE( GeometryMeshTransaction_Commit( &txn, &f.delta, &rev, &bChanged ) == geometry_status_t::OK );
    CHECK( bChanged );
    CHECK( rev == 2u );
    CHECK_FALSE( GeometryMeshTransaction_IsActive( &txn ) );
    CHECK( f.delta.kind == geometry_mesh_delta_kind_t::MESH_REPLACED );
    CHECK( MeshSourceDescription_Equal( &f.delta.before, &original.d ) );
    // Extrude adds 4 vertices and 5 faces (4 sides + new cap); the old cap
    // face is gone. All 9 new IDs came from the registry's allocator.
    CHECK( f.delta.after.vertices.nCount == 12u );
    CHECK( f.delta.after.faces.nCount == 10u );
    CHECK( f.doc.sourceIds.allocator.next.value == nextBefore + 9u );
    CHECK_FALSE( f.Live( 1010 ) );
    CHECK( f.Claimed( 1010 ) );
    for ( common::u64 id = nextBefore; id < nextBefore + 9u; ++id ) { CHECK( f.Live( id ) ); }
    ScopedDesc edited;
    REQUIRE( f.Describe( 1000, &edited.d ) );
    CHECK( MeshSourceDescription_Equal( &edited.d, &f.delta.after ) );
    // Attributes on untouched elements survived the edit.
    CHECK( edited.d.edges.nCount >= 1u );

    // Undo.
    GeometryMeshDelta_Invert( &f.delta );
    REQUIRE( GeometryMeshDelta_TryApply( &f.delta, &f.doc, &rev ) == geometry_status_t::OK );
    CHECK( rev == 3u );
    ScopedDesc undone;
    REQUIRE( f.Describe( 1000, &undone.d ) );
    CHECK( MeshSourceDescription_Equal( &undone.d, &original.d ) );
    CHECK( f.Live( 1010 ) );
    CHECK_FALSE( f.Live( nextBefore ) );
    // Undo applied twice is stale.
    CHECK( GeometryMeshDelta_TryApply( &f.delta, &f.doc, &rev ) == geometry_status_t::STALE_REVISION );

    // Redo restores the edit with the *same* IDs it had (restored, not
    // reissued).
    GeometryMeshDelta_Invert( &f.delta );
    REQUIRE( GeometryMeshDelta_TryApply( &f.delta, &f.doc, &rev ) == geometry_status_t::OK );
    ScopedDesc redone;
    REQUIRE( f.Describe( 1000, &redone.d ) );
    CHECK( MeshSourceDescription_Equal( &redone.d, &edited.d ) );
    CHECK( f.doc.sourceIds.allocator.next.value == nextBefore + 9u );
}

TEST_CASE( "Mesh transactions refuse stale commits and skip no-op commits", "[geometry][document][meshes]" ) {
    Fixture f;
    CubeDescription( &f.desc, 1000 );
    geometry_revision_t rev = 0;
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &rev ) == geometry_status_t::OK );

    geometry_mesh_transaction_t txn{};
    REQUIRE( GeometryMeshTransaction_Begin( &txn, &f.doc, Id( 1000 ) ) == geometry_status_t::OK );
    bool bChanged = true;
    REQUIRE( GeometryMeshTransaction_Commit( &txn, &f.delta, &rev, &bChanged ) == geometry_status_t::OK );
    CHECK_FALSE( bChanged );
    CHECK( f.doc.revision == 1u );
    CHECK( f.delta.kind == geometry_mesh_delta_kind_t::INVALID );
    CHECK_FALSE( GeometrySourceId_IsValid( f.delta.meshId ) );
    CHECK( f.delta.before.vertices.nCount == 0u );
    CHECK( f.delta.after.vertices.nCount == 0u );

    REQUIRE( GeometryMeshTransaction_Begin( &txn, &f.doc, Id( 1000 ) ) == geometry_status_t::OK );
    geometry_mesh_vertex_handle_t hv{};
    REQUIRE( MeshSource_TryFindVertex( GeometryMeshTransaction_Working( &txn ), Id( 1001 ), &hv ) );
    REQUIRE( MeshOps_MoveVertex( &GeometryMeshTransaction_Working( &txn )->mesh, hv, Vec3d_Make( -0.5, -0.5, -0.5 ) ) ==
             geometry_status_t::OK );
    // Someone else publishes first.
    CubeDescription( &f.desc, 3000, 5.0 );
    geometry_mesh_delta_t other{};
    REQUIRE( GeometryMeshDelta_Init( &other, &f.allocator ) == geometry_status_t::OK );
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &other, &rev ) == geometry_status_t::OK );
    GeometryMeshDelta_Shutdown( &other );
    CHECK( GeometryMeshTransaction_Commit( &txn, &f.delta, &rev, &bChanged ) == geometry_status_t::STALE_REVISION );
    CHECK( GeometryMeshTransaction_IsActive( &txn ) );
    GeometryMeshTransaction_Cancel( &txn );
    CHECK_FALSE( GeometryMeshTransaction_IsActive( &txn ) );
    CHECK( GeometryMeshTransaction_Commit( &txn, &f.delta, &rev, &bChanged ) == geometry_status_t::NO_ACTIVE_TRANSACTION );
    CHECK( GeometryMeshTransaction_Begin( &txn, &f.doc, Id( 424242 ) ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Snapshots keep an immutable copy of document meshes", "[geometry][document][meshes]" ) {
    Fixture f;
    f.AddBrush();
    CubeDescription( &f.desc, 1000 );
    geometry_revision_t rev = 0;
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &rev ) == geometry_status_t::OK );

    geometry_snapshot_t snap{};
    REQUIRE( GeometrySnapshot_TakeFromDocument( &snap, &f.doc ) == geometry_status_t::OK );
    CHECK( GeometrySnapshot_BrushCount( &snap ) == 1u );
    CHECK( GeometrySnapshot_MeshCount( &snap ) == 1u );
    CHECK( GeometrySnapshot_GetRevision( &snap ) == 1u );

    geometry_mesh_transaction_t txn{};
    REQUIRE( GeometryMeshTransaction_Begin( &txn, &f.doc, Id( 1000 ) ) == geometry_status_t::OK );
    geometry_mesh_vertex_handle_t hv{};
    REQUIRE( MeshSource_TryFindVertex( GeometryMeshTransaction_Working( &txn ), Id( 1008 ), &hv ) );
    REQUIRE( MeshOps_MoveVertex( &GeometryMeshTransaction_Working( &txn )->mesh, hv, Vec3d_Make( 2, 2, 2 ) ) ==
             geometry_status_t::OK );
    bool bChanged = false;
    REQUIRE( GeometryMeshTransaction_Commit( &txn, &f.delta, &rev, &bChanged ) == geometry_status_t::OK );

    ScopedDesc fromSnap;
    REQUIRE( MeshSource_TryDescribe( GeometrySnapshot_FindMesh( &snap, Id( 1000 ) ), &fromSnap.d ) ==
             geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal( &fromSnap.d, &f.delta.before ) );
    CHECK_FALSE( MeshSourceDescription_Equal( &fromSnap.d, &f.delta.after ) );
    CHECK( GeometrySnapshot_MeshAt( &snap, 1 ) == nullptr );
    GeometrySnapshot_Shutdown( &snap );

    // A second snapshot sees the edit.
    geometry_snapshot_t snap2{};
    REQUIRE( GeometrySnapshot_TakeFromDocument( &snap2, &f.doc ) == geometry_status_t::OK );
    ScopedDesc fromSnap2;
    REQUIRE( MeshSource_TryDescribe( GeometrySnapshot_MeshAt( &snap2, 0 ), &fromSnap2.d ) == geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal( &fromSnap2.d, &f.delta.after ) );
    GeometrySnapshot_Shutdown( &snap2 );
}

TEST_CASE( "Documents with brushes and meshes save and load exactly", "[geometry][document][meshes]" ) {
    Fixture f;
    f.AddBrush();
    CubeDescription( &f.desc, 1000 );
    geometry_revision_t rev = 0;
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &rev ) == geometry_status_t::OK );
    // A removed mesh leaves retired-but-claimed IDs that must persist.
    CubeDescription( &f.desc, 2000, 4.0 );
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &rev ) == geometry_status_t::OK );
    REQUIRE( GeometryMeshCommand_TryRemove( &f.doc, Id( 2000 ), &f.delta, &rev ) == geometry_status_t::OK );

    common::text_buffer_t text{};
    REQUIRE( common::TextBuffer_Init( &text, &f.allocator ) );
    const geometry_serialization_result_t saved = GeometrySerialization_SaveToText( &f.doc, &text );
    REQUIRE( saved.status == geometry_serialization_status_t::OK );
    const std::string s( common::TextBuffer_View( &text ).pData, common::TextBuffer_View( &text ).cchLength );
    CHECK( s.find( "meshes" ) != std::string::npos );
    CHECK( s.find( "corner_uv0" ) != std::string::npos );
    CHECK( s.find( "corner_uv1" ) == std::string::npos ); // default channel omitted

    geometry_document_t loaded{};
    const geometry_serialization_result_t r =
        GeometrySerialization_LoadFromText( common::TextBuffer_View( &text ), &f.allocator, f.policy, &loaded );
    REQUIRE( r.status == geometry_serialization_status_t::OK );
    CHECK( GeometryDocument_BrushCount( &loaded ) == 1u );
    REQUIRE( GeometryDocument_MeshCount( &loaded ) == 1u );
    ScopedDesc a, b;
    REQUIRE( f.Describe( 1000, &a.d ) );
    REQUIRE( MeshSource_TryDescribe( GeometryDocument_FindMesh( &loaded, Id( 1000 ) ), &b.d ) == geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal( &a.d, &b.d ) );
    CHECK( loaded.sourceIds.allocator.next.value == f.doc.sourceIds.allocator.next.value );
    CHECK( GeometrySourceIdRegistry_ClaimedCount( &loaded.sourceIds ) ==
           GeometrySourceIdRegistry_ClaimedCount( &f.doc.sourceIds ) );
    CHECK( common::HashSet_Contains( &loaded.sourceIds.claimedIds, Id( 2005 ) ) );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains( &loaded.sourceIds, Id( 2005 ) ) );

    // Undo of the removal still works on the loaded document.
    GeometryMeshDelta_Invert( &f.delta );
    REQUIRE( GeometryMeshDelta_TryApply( &f.delta, &loaded, &rev ) == geometry_status_t::OK );
    CHECK( GeometryDocument_MeshCount( &loaded ) == 2u );

    // Saving is deterministic: save the original again, compare text.
    common::text_buffer_t again{};
    REQUIRE( common::TextBuffer_Init( &again, &f.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText( &f.doc, &again ).status == geometry_serialization_status_t::OK );
    CHECK( std::string( common::TextBuffer_View( &again ).pData, common::TextBuffer_View( &again ).cchLength ) == s );

    common::TextBuffer_Shutdown( &again );
    common::TextBuffer_Shutdown( &text );
    GeometryDocument_Shutdown( &loaded );
}

TEST_CASE( "Saving rejects aggregate mesh topology beyond document policy without changing output",
           "[geometry][document][meshes][serialization][limits]" ) {
    Fixture f;
    CubeDescription( &f.desc, 1000 );
    geometry_revision_t revision = 0u;
    REQUIRE( GeometryMeshCommand_TryAdd(
                 &f.doc, &f.desc, &f.delta, &revision ) ==
             geometry_status_t::OK );

    // Construct an internally coherent but over-policy document fixture.
    // Keep the registry's policy-derived lifetime capacity in sync so the
    // save must reach the aggregate mesh-topology audit rather than fail the
    // earlier registry-capacity consistency check.
    f.doc.policy.limits.cVerticesMax = 7u;
    const common::u64 cSourceIdsMax =
        GeometryDocument_SourceIdCapacity( f.doc.policy );
    REQUIRE( cSourceIdsMax > 0u );
    REQUIRE( cSourceIdsMax <= common::CY_USIZE_MAX );
    f.doc.sourceIds.cEntriesMax =
        static_cast<common::usize>( cSourceIdsMax );

    common::text_buffer_t output{};
    REQUIRE( common::TextBuffer_Init( &output, &f.allocator ) );
    REQUIRE( common::TextBuffer_Assign(
                 &output,
                 common::StringView_FromCString( "sentinel" ) ) );

    const geometry_serialization_result_t result =
        GeometrySerialization_SaveToText( &f.doc, &output );
    CHECK( result.status ==
           geometry_serialization_status_t::CORRUPT_DOCUMENT );
    CHECK( common::StringView_Equals(
        common::TextBuffer_View( &output ),
        common::StringView_FromCString( "sentinel" ) ) );

    common::TextBuffer_Shutdown( &output );
}

TEST_CASE( "Mesh load budget preflight is allocation-free and does not publish a prefix",
           "[geometry][document][meshes][serialization][limits][allocation]" ) {
    Fixture source;
    geometry_revision_t revision = 0u;
    CubeDescription( &source.desc, 1000 );
    REQUIRE( GeometryMeshCommand_TryAdd(
                 &source.doc, &source.desc, &source.delta, &revision ) ==
             geometry_status_t::OK );
    CubeDescription( &source.desc, 2000, 4.0 );
    REQUIRE( GeometryMeshCommand_TryAdd(
                 &source.doc, &source.desc, &source.delta, &revision ) ==
             geometry_status_t::OK );

    common::text_buffer_t text{};
    REQUIRE( common::TextBuffer_Init( &text, &source.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText(
                 &source.doc, &text ).status ==
             geometry_serialization_status_t::OK );
    ScopedKeyValueDocument parsed = ParseDocument(
        common::TextBuffer_View( &text ), &source.allocator );

    geometry_policy_t constrained{};
    constrained.limits.cVerticesMax = 15u;
    REQUIRE( GeometryPolicy_IsValid( constrained ) );

    mesh_document_failure_allocator_state_t allocatorState{};
    common::allocator_t destinationAllocator =
        MakeMeshDocumentFailureAllocator( &allocatorState );
    geometry_document_t destination{};
    REQUIRE( GeometryDocument_Init(
                 &destination, &destinationAllocator, constrained ) ==
             geometry_status_t::OK );
    const common::usize cAllocationCallsBefore =
        allocatorState.cAllocationCalls;

    const geometry_serialization_result_t result =
        MeshSerialization_TryReadMeshes(
            common::KeyValue_Root( parsed.pDocument ),
            &destination,
            false );
    CHECK( result.status ==
           geometry_serialization_status_t::LIMIT_EXCEEDED );
    CHECK( allocatorState.cAllocationCalls == cAllocationCallsBefore );
    CHECK( GeometryDocument_MeshCount( &destination ) == 0u );
    CHECK( GeometrySourceIdRegistry_Count(
               &destination.sourceIds ) == 0u );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
               &destination.sourceIds ) == 0u );
    CHECK( destination.revision == 0u );
    CHECK( GeometrySourceIdRegistry_ValidateDeep(
               &destination.sourceIds ) );

    GeometryDocument_Shutdown( &destination );
    CHECK( allocatorState.cSuccessfulAllocations ==
           allocatorState.cFreeCalls );
    common::TextBuffer_Shutdown( &text );
}

TEST_CASE( "Top-level mesh load preflight enforces topology lower bounds and exact boundaries",
           "[geometry][document][meshes][serialization][limits]" ) {
    Fixture source;
    geometry_revision_t revision = 0u;
    CubeDescription( &source.desc, 1000 );
    REQUIRE( GeometryMeshCommand_TryAdd(
                 &source.doc, &source.desc, &source.delta, &revision ) ==
             geometry_status_t::OK );
    CubeDescription( &source.desc, 2000, 4.0 );
    REQUIRE( GeometryMeshCommand_TryAdd(
                 &source.doc, &source.desc, &source.delta, &revision ) ==
             geometry_status_t::OK );

    common::text_buffer_t text{};
    REQUIRE( common::TextBuffer_Init( &text, &source.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText(
                 &source.doc, &text ).status ==
             geometry_serialization_status_t::OK );

    SECTION( "edge budget uses topology lower bounds, not edge attribute entries" ) {
        geometry_policy_t constrained{};
        constrained.limits.cEdgesMax = 23u;
        REQUIRE( GeometryPolicy_IsValid( constrained ) );
        geometry_document_t loaded{};
        const geometry_serialization_result_t result =
            GeometrySerialization_LoadFromText(
                common::TextBuffer_View( &text ),
                &source.allocator,
                constrained,
                &loaded );
        CHECK( result.status ==
               geometry_serialization_status_t::LIMIT_EXCEEDED );
        CHECK( IsCanonicalEmptyDocument( loaded ) );
    }

    SECTION( "each nonempty mesh contributes at least one shell" ) {
        geometry_policy_t constrained{};
        constrained.limits.cShellsMax = 1u;
        REQUIRE( GeometryPolicy_IsValid( constrained ) );
        geometry_document_t loaded{};
        const geometry_serialization_result_t result =
            GeometrySerialization_LoadFromText(
                common::TextBuffer_View( &text ),
                &source.allocator,
                constrained,
                &loaded );
        CHECK( result.status ==
               geometry_serialization_status_t::LIMIT_EXCEEDED );
        CHECK( IsCanonicalEmptyDocument( loaded ) );
    }

    SECTION( "exact two-cube topology boundary remains loadable" ) {
        geometry_policy_t exact{};
        exact.limits.cVerticesMax = 16u;
        exact.limits.cHalfEdgesMax = 48u;
        exact.limits.cEdgesMax = 24u;
        exact.limits.cLoopsMax = 12u;
        exact.limits.cFacesMax = 12u;
        exact.limits.cShellsMax = 2u;
        REQUIRE( GeometryPolicy_IsValid( exact ) );
        geometry_document_t loaded{};
        const geometry_serialization_result_t result =
            GeometrySerialization_LoadFromText(
                common::TextBuffer_View( &text ),
                &source.allocator,
                exact,
                &loaded );
        REQUIRE( result.status ==
                 geometry_serialization_status_t::OK );
        CHECK( GeometryDocument_MeshCount( &loaded ) == 2u );
        GeometryDocument_Shutdown( &loaded );
    }

    common::TextBuffer_Shutdown( &text );
}

TEST_CASE( "Loading rejects tampered mesh sections", "[geometry][document][meshes]" ) {
    Fixture f;
    CubeDescription( &f.desc, 1000 );
    geometry_revision_t rev = 0;
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &rev ) == geometry_status_t::OK );
    common::text_buffer_t text{};
    REQUIRE( common::TextBuffer_Init( &text, &f.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText( &f.doc, &text ).status == geometry_serialization_status_t::OK );
    const std::string original( common::TextBuffer_View( &text ).pData, common::TextBuffer_View( &text ).cchLength );
    common::TextBuffer_Shutdown( &text );

    auto load = [&]( const std::string &t ) {
        geometry_document_t d{};
        const geometry_serialization_result_t r = GeometrySerialization_LoadFromText(
            common::string_view_t{ t.data(), t.size() }, &f.allocator, f.policy, &d );
        if ( r.status == geometry_serialization_status_t::OK ) { GeometryDocument_Shutdown( &d ); }
        return r;
    };
    REQUIRE( load( original ).status == geometry_serialization_status_t::OK );

    // A vertex ID that is not in claimed_source_ids.
    {
        std::string t = original;
        const std::size_t at = t.find( "1008" , t.find( "vertex_ids" ) );
        REQUIRE( at != std::string::npos );
        t.replace( at, 4, "7777" );
        const geometry_serialization_result_t r = load( t );
        CHECK( r.status == geometry_serialization_status_t::INVALID_SOURCE_ID );
    }
    // A required channel renamed: reported with the missing field's name.
    {
        std::string t = original;
        const std::size_t at = t.find( "face_ids" );
        REQUIRE( at != std::string::npos );
        t.replace( at, 8, "face_idz" );
        const geometry_serialization_result_t r = load( t );
        CHECK( r.status == geometry_serialization_status_t::MISSING_FIELD );
        CHECK( std::string( r.field ) == "face_ids" );
    }
}

TEST_CASE( "Document mesh limits are enforced atomically", "[geometry][document][meshes]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    policy.limits.cVerticesMax = 12u;
    geometry_document_t doc{};
    REQUIRE( GeometryDocument_Init( &doc, &allocator, policy ) == geometry_status_t::OK );
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
    geometry_mesh_delta_t delta{};
    REQUIRE( GeometryMeshDelta_Init( &delta, &allocator ) == geometry_status_t::OK );
    geometry_revision_t rev = 0;
    CubeDescription( &d, 100 );
    REQUIRE( GeometryMeshCommand_TryAdd( &doc, &d, &delta, &rev ) == geometry_status_t::OK );
    CubeDescription( &d, 200, 3.0 );
    CHECK( GeometryMeshCommand_TryAdd( &doc, &d, &delta, &rev ) == geometry_status_t::LIMIT_EXCEEDED );
    CHECK( GeometryDocument_MeshCount( &doc ) == 1u );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains( &doc.sourceIds, Id( 200 ) ) );
    CHECK( doc.revision == 1u );
    GeometryMeshDelta_Shutdown( &delta );
    MeshSourceDescription_Shutdown( &d );
    GeometryDocument_Shutdown( &doc );
}

TEST_CASE( "Document mesh edge limit is enforced before publication", "[geometry][document][meshes][limits]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    policy.limits.cEdgesMax = 11u; // a cube has 12 unique edges
    geometry_document_t doc{};
    REQUIRE( GeometryDocument_Init( &doc, &allocator, policy ) == geometry_status_t::OK );
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 100 ) ) == geometry_status_t::OK );
    CubeDescription( &d, 100 );
    mesh_source_t mesh{};
    REQUIRE( MeshSource_TryBuild( &d, &allocator, &mesh ) == geometry_status_t::OK );

    CHECK( GeometryDocument_TryAddMesh( &doc, &mesh ) == geometry_status_t::LIMIT_EXCEEDED );
    CHECK( GeometryDocument_MeshCount( &doc ) == 0u );
    CHECK( GeometrySourceIdRegistry_Count( &doc.sourceIds ) == 0u );
    CHECK( GeometrySourceIdRegistry_ClaimedCount( &doc.sourceIds ) == 0u );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &doc.sourceIds ) );

    MeshSource_Shutdown( &mesh );
    MeshSourceDescription_Shutdown( &d );
    GeometryDocument_Shutdown( &doc );
}

TEST_CASE( "Every document mesh removal allocation failure terminates and is atomic",
           "[geometry][document][meshes][allocation]" ) {
    common::allocator_t sourceAllocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &sourceAllocator, Id( 1000 ) ) == geometry_status_t::OK );
    CubeDescription( &d, 1000 );
    mesh_source_t source{};
    REQUIRE( MeshSource_TryBuild( &d, &sourceAllocator, &source ) == geometry_status_t::OK );
    geometry_policy_t policy{};

    mesh_document_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator = MakeMeshDocumentFailureAllocator( &baselineState );
    geometry_document_t baseline{};
    REQUIRE( GeometryDocument_Init( &baseline, &baselineAllocator, policy ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddMesh( &baseline, &source ) == geometry_status_t::OK );
    const common::usize iRemoveBegin = baselineState.cAllocationCalls;
    REQUIRE( GeometryDocument_TryRemoveMesh( &baseline, Id( 1000 ) ) == geometry_status_t::OK );
    const common::usize cRemoveAllocations = baselineState.cAllocationCalls - iRemoveBegin;
    REQUIRE( cRemoveAllocations > 0u );
    GeometryDocument_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFreeCalls );

    for ( common::usize iFailure = 0u; iFailure < cRemoveAllocations; ++iFailure ) {
        CAPTURE( iFailure, cRemoveAllocations );
        mesh_document_failure_allocator_state_t state{};
        common::allocator_t allocator = MakeMeshDocumentFailureAllocator( &state );
        geometry_document_t doc{};
        REQUIRE( GeometryDocument_Init( &doc, &allocator, policy ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddMesh( &doc, &source ) == geometry_status_t::OK );
        const common::usize iFailureCall = state.cAllocationCalls + iFailure;
        state.iFailure = iFailureCall;

        CHECK( GeometryDocument_TryRemoveMesh( &doc, Id( 1000 ) ) == geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cAllocationCalls == iFailureCall + 1u );
        CHECK( GeometryDocument_MeshCount( &doc ) == 1u );
        CHECK( GeometryDocument_FindMesh( &doc, Id( 1000 ) ) != nullptr );
        CHECK( GeometrySourceIdRegistry_Count( &doc.sourceIds ) == 15u );
        CHECK( GeometrySourceIdRegistry_ClaimedCount( &doc.sourceIds ) == 15u );
        CHECK( GeometrySourceIdRegistry_ValidateDeep( &doc.sourceIds ) );

        GeometryDocument_Shutdown( &doc );
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }

    MeshSource_Shutdown( &source );
    MeshSourceDescription_Shutdown( &d );
}

TEST_CASE( "Mesh command deltas retain their caller-selected allocator",
           "[geometry][document][meshes][transactions][allocator]" ) {
    common::allocator_t documentAllocator{ *common::Allocator_GetSystem() };
    common::allocator_t deltaAllocator{ *common::Allocator_GetSystem() };
    geometry_document_t doc{};
    geometry_policy_t policy{};
    REQUIRE( GeometryDocument_Init( &doc, &documentAllocator, policy ) == geometry_status_t::OK );
    mesh_source_description_t description{};
    REQUIRE( MeshSourceDescription_Init( &description, &documentAllocator, Id( 1000 ) ) == geometry_status_t::OK );
    CubeDescription( &description, 1000 );
    geometry_mesh_delta_t delta{};
    REQUIRE( GeometryMeshDelta_Init( &delta, &deltaAllocator ) == geometry_status_t::OK );
    geometry_revision_t revision = 0u;

    REQUIRE( GeometryMeshCommand_TryAdd( &doc, &description, &delta, &revision ) == geometry_status_t::OK );
    CHECK( DescriptionUsesAllocator( delta.before, &deltaAllocator ) );
    CHECK( DescriptionUsesAllocator( delta.after, &deltaAllocator ) );

    geometry_mesh_transaction_t transaction{};
    REQUIRE( GeometryMeshTransaction_Begin( &transaction, &doc, Id( 1000 ) ) == geometry_status_t::OK );
    geometry_mesh_vertex_handle_t hVertex{};
    REQUIRE( MeshSource_TryFindVertex( GeometryMeshTransaction_Working( &transaction ), Id( 1001 ), &hVertex ) );
    REQUIRE( MeshOps_MoveVertex( &GeometryMeshTransaction_Working( &transaction )->mesh, hVertex,
                                Vec3d_Make( -0.25, 0.0, 0.0 ) ) == geometry_status_t::OK );
    bool bChanged = false;
    REQUIRE( GeometryMeshTransaction_Commit( &transaction, &delta, &revision, &bChanged ) == geometry_status_t::OK );
    CHECK( bChanged );
    CHECK( DescriptionUsesAllocator( delta.before, &deltaAllocator ) );
    CHECK( DescriptionUsesAllocator( delta.after, &deltaAllocator ) );

    REQUIRE( GeometryMeshCommand_TryRemove( &doc, Id( 1000 ), &delta, &revision ) == geometry_status_t::OK );
    CHECK( DescriptionUsesAllocator( delta.before, &deltaAllocator ) );
    CHECK( DescriptionUsesAllocator( delta.after, &deltaAllocator ) );

    GeometryMeshDelta_Shutdown( &delta );
    MeshSourceDescription_Shutdown( &description );
    GeometryDocument_Shutdown( &doc );
}

TEST_CASE( "Mesh transaction tentative IDs survive a failed commit without reuse",
           "[geometry][document][meshes][transactions][identity][allocation]" ) {
    Fixture f;
    geometry_revision_t revision = 0u;
    CubeDescription( &f.desc, 1000 );
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &revision ) == geometry_status_t::OK );
    const common::u64 firstFresh = f.doc.sourceIds.allocator.next.value;

    geometry_mesh_transaction_t transaction{};
    REQUIRE( GeometryMeshTransaction_Begin( &transaction, &f.doc, Id( 1000 ) ) == geometry_status_t::OK );
    mesh_source_t *pWorking = GeometryMeshTransaction_Working( &transaction );
    geometry_mesh_face_handle_t hFace{};
    REQUIRE( MeshSource_TryFindFace( pWorking, Id( 1009 ), &hFace ) );
    const mesh_face_record_t *pFace = common::GenerationPool_Get( &pWorking->mesh.faces, hFace );
    REQUIRE( pFace != nullptr );
    const mesh_loop_record_t *pLoop = common::GenerationPool_Get( &pWorking->mesh.loops, pFace->hOuterLoop );
    REQUIRE( pLoop != nullptr );
    const mesh_half_edge_record_t *pHalfEdge =
        common::GenerationPool_Get( &pWorking->mesh.halfEdges, pLoop->hFirstHalfEdge );
    REQUIRE( pHalfEdge != nullptr );
    const geometry_mesh_edge_handle_t hEdge = pHalfEdge->hEdge;
    const mesh_split_edge_result_t firstSplit = MeshOps_SplitEdge( &pWorking->mesh, hEdge, 0.25 );
    REQUIRE( firstSplit.status == geometry_status_t::OK );

    mesh_document_failure_allocator_state_t failureState{};
    common::allocator_t deltaAllocator = MakeMeshDocumentFailureAllocator( &failureState );
    geometry_mesh_delta_t output{};
    REQUIRE( GeometryMeshDelta_Init( &output, &deltaAllocator ) == geometry_status_t::OK );
    failureState.iFailure = failureState.cAllocationCalls;
    bool bChanged = true;
    CHECK( GeometryMeshTransaction_Commit( &transaction, &output, &revision, &bChanged ) ==
           geometry_status_t::ALLOCATION_FAILED );
    CHECK_FALSE( bChanged );
    REQUIRE( GeometryMeshTransaction_IsActive( &transaction ) );
    CHECK( MeshSource_VertexId( pWorking, firstSplit.hNewVertex ).value == firstFresh );
    CHECK( f.doc.sourceIds.allocator.next.value == firstFresh );

    failureState.iFailure = common::CY_USIZE_MAX;
    const mesh_split_edge_result_t secondSplit = MeshOps_SplitEdge( &pWorking->mesh, hEdge, 0.75 );
    REQUIRE( secondSplit.status == geometry_status_t::OK );
    REQUIRE( GeometryMeshTransaction_Commit( &transaction, &output, &revision, &bChanged ) == geometry_status_t::OK );
    CHECK( bChanged );
    CHECK( output.after.vertices.nCount == 10u );
    CHECK( f.doc.sourceIds.allocator.next.value == firstFresh + 2u );
    CHECK( f.Live( firstFresh ) );
    CHECK( f.Live( firstFresh + 1u ) );

    GeometryMeshDelta_Shutdown( &output );
    CHECK( failureState.cSuccessfulAllocations == failureState.cFreeCalls );
}

TEST_CASE( "Mesh transactions reject arbitrary unallocated element identities",
           "[geometry][document][meshes][transactions][identity]" ) {
    Fixture f;
    geometry_revision_t revision = 0u;
    CubeDescription( &f.desc, 1000 );
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &revision ) == geometry_status_t::OK );
    REQUIRE_FALSE( f.Claimed( 42u ) );

    geometry_mesh_transaction_t transaction{};
    REQUIRE( GeometryMeshTransaction_Begin( &transaction, &f.doc, Id( 1000 ) ) == geometry_status_t::OK );
    mesh_source_t *pWorking = GeometryMeshTransaction_Working( &transaction );
    geometry_mesh_face_handle_t hFace{};
    REQUIRE( MeshSource_TryFindFace( pWorking, Id( 1009 ), &hFace ) );
    const mesh_face_record_t *pFace = common::GenerationPool_Get( &pWorking->mesh.faces, hFace );
    REQUIRE( pFace != nullptr );
    const mesh_loop_record_t *pLoop = common::GenerationPool_Get( &pWorking->mesh.loops, pFace->hOuterLoop );
    REQUIRE( pLoop != nullptr );
    const mesh_half_edge_record_t *pHalfEdge =
        common::GenerationPool_Get( &pWorking->mesh.halfEdges, pLoop->hFirstHalfEdge );
    REQUIRE( pHalfEdge != nullptr );
    const mesh_split_edge_result_t split = MeshOps_SplitEdge( &pWorking->mesh, pHalfEdge->hEdge, 0.5 );
    REQUIRE( split.status == geometry_status_t::OK );
    REQUIRE( MeshSource_TrySetVertexId( pWorking, split.hNewVertex, Id( 42 ) ) == geometry_status_t::OK );

    bool bChanged = true;
    CHECK( GeometryMeshTransaction_Commit( &transaction, &f.delta, &revision, &bChanged ) ==
           geometry_status_t::IDENTITY_CONFLICT );
    CHECK_FALSE( bChanged );
    CHECK( GeometryMeshTransaction_IsActive( &transaction ) );
    CHECK_FALSE( f.Claimed( 42u ) );
    CHECK( f.doc.revision == 1u );
    GeometryMeshTransaction_Cancel( &transaction );
}

TEST_CASE( "Mesh transactions preserve vertex and face identity domains",
           "[geometry][document][meshes][transactions][identity]" ) {
    Fixture f;
    geometry_revision_t revision = 0u;
    CubeDescription( &f.desc, 1000 );
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &revision ) == geometry_status_t::OK );

    geometry_mesh_transaction_t transaction{};
    REQUIRE( GeometryMeshTransaction_Begin( &transaction, &f.doc, Id( 1000 ) ) == geometry_status_t::OK );
    mesh_source_t *pWorking = GeometryMeshTransaction_Working( &transaction );
    geometry_mesh_vertex_handle_t hVertex{};
    geometry_mesh_face_handle_t hFace{};
    REQUIRE( MeshSource_TryFindVertex( pWorking, Id( 1001 ), &hVertex ) );
    REQUIRE( MeshSource_TryFindFace( pWorking, Id( 1009 ), &hFace ) );
    REQUIRE( MeshSource_TrySetVertexId( pWorking, hVertex, Id( 1009 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TrySetFaceId( pWorking, hFace, Id( 1001 ) ) == geometry_status_t::OK );

    bool bChanged = true;
    CHECK( GeometryMeshTransaction_Commit( &transaction, &f.delta, &revision, &bChanged ) ==
           geometry_status_t::IDENTITY_CONFLICT );
    CHECK_FALSE( bChanged );
    CHECK( GeometryMeshTransaction_IsActive( &transaction ) );
    CHECK( f.doc.revision == 1u );
    CHECK( f.Live( 1001 ) );
    CHECK( f.Live( 1009 ) );
    GeometryMeshTransaction_Cancel( &transaction );
}

TEST_CASE( "Normal mesh commands cannot steal retired identities reserved for undo",
           "[geometry][document][meshes][transactions][identity]" ) {
    Fixture f;
    geometry_revision_t revision = 0u;
    CubeDescription( &f.desc, 1000 );
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &revision ) == geometry_status_t::OK );
    CubeDescription( &f.desc, 2000, 3.0 );
    REQUIRE( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &revision ) == geometry_status_t::OK );
    REQUIRE( GeometryMeshCommand_TryRemove( &f.doc, Id( 2000 ), &f.delta, &revision ) == geometry_status_t::OK );
    REQUIRE_FALSE( f.Live( 2001 ) );
    REQUIRE( f.Claimed( 2001 ) );

    geometry_mesh_transaction_t transaction{};
    REQUIRE( GeometryMeshTransaction_Begin( &transaction, &f.doc, Id( 1000 ) ) == geometry_status_t::OK );
    mesh_source_t *pWorking = GeometryMeshTransaction_Working( &transaction );
    geometry_mesh_vertex_handle_t hVertex{};
    REQUIRE( MeshSource_TryFindVertex( pWorking, Id( 1001 ), &hVertex ) );
    REQUIRE( MeshSource_TrySetVertexId( pWorking, hVertex, Id( 2001 ) ) == geometry_status_t::OK );
    bool bChanged = true;
    CHECK( GeometryMeshTransaction_Commit( &transaction, &f.delta, &revision, &bChanged ) ==
           geometry_status_t::IDENTITY_CONFLICT );
    CHECK_FALSE( bChanged );
    CHECK( GeometryMeshTransaction_IsActive( &transaction ) );
    CHECK( f.Live( 1001 ) );
    CHECK_FALSE( f.Live( 2001 ) );
    CHECK( f.doc.revision == 3u );
    GeometryMeshTransaction_Cancel( &transaction );

    // A normal add cannot reclaim the removed mesh either. Its recorded
    // inverse delta is the provenance-bearing path that may restore it.
    CHECK( GeometryMeshCommand_TryAdd( &f.doc, &f.desc, &f.delta, &revision ) ==
           geometry_status_t::IDENTITY_CONFLICT );
    CHECK( GeometryDocument_MeshCount( &f.doc ) == 1u );
    GeometryMeshDelta_Invert( &f.delta );
    REQUIRE( GeometryMeshDelta_TryApply( &f.delta, &f.doc, &revision ) == geometry_status_t::OK );
    CHECK( GeometryDocument_MeshCount( &f.doc ) == 2u );
    CHECK( f.Live( 2001 ) );
}

} // namespace cypher::editor::geometry
