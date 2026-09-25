//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Fragment_Tests.cpp
//  Purpose: Tests geometry fragments: extract, remap to fresh identities,
//           translate with texture lock, all-or-nothing insertion,
//           duplicate, cross-document paste through clipboard text, and
//           allocation-failure atomicity of remap and insert.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushSerialization.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_Fragment.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace cypher::editor::geometry
{

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

// A document holding one of each kind of object: a box brush whose record
// i has material 100 + i, a unit cube mesh, a bent 3 x 3 patch, and an
// 8 x 8 heightfield with a bump and a hole. IDs come from one allocator so
// they never collide inside the document.
struct Doc {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    geometry_document_t doc{};
    geometry_source_id_t brushId{}, meshId{}, patchId{}, fieldId{};

    explicit Doc( const common::allocator_t *pA = common::Allocator_GetSystem() ) : allocator( *pA )
    {
        REQUIRE( GeometryDocument_Init( &doc, &allocator, policy ) == geometry_status_t::OK );
    }
    ~Doc() { GeometryDocument_Shutdown( &doc ); }

    geometry_source_id_t NextId()
    {
        const geometry_source_id_result_t r = GeometrySourceIdAllocator_Allocate( &ids );
        REQUIRE( r.status == geometry_status_t::OK );
        return r.id;
    }

    void AddAll( double x )
    {
        const common::allocator_t *pSys = common::Allocator_GetSystem();
        // Brush with distinct materials per record.
        brush_solid_t box{};
        REQUIRE( BrushGenerator_TryMakeBox( &box, pSys, policy, &ids, math::Vec3d_Make( x, 0, 0 ), math::Vec3d_Make( 1, 1, 1 ) ) ==
                 geometry_status_t::OK );
        brush_source_t brush{};
        REQUIRE( BrushSource_TryBuildDefault( &box, pSys, policy, &brush ) == geometry_status_t::OK );
        BrushSolid_Shutdown( &box );
        for ( common::usize i = 0; i < BrushSideAttributeStore_Count( &brush.attributes ); ++i ) {
            geometry_brush_side_attributes_t r{};
            REQUIRE( BrushSideAttributeStore_TryGet( &brush.attributes, i, &r ) == geometry_status_t::OK );
            r.material.value = 100u + i;
            REQUIRE( BrushSideAttributeStore_TrySet( &brush.attributes, policy.numerical, i, r ) == geometry_status_t::OK );
        }
        REQUIRE( GeometryDocument_TryAddBrushSource( &doc, &brush ) == geometry_status_t::OK );
        brushId = brush.solid.sourceId;
        BrushSource_Shutdown( &brush );

        // Cube mesh.
        mesh_source_description_t desc{};
        meshId = NextId();
        REQUIRE( MeshSourceDescription_Init( &desc, pSys, meshId ) == geometry_status_t::OK );
        for ( int i = 0; i < 8; ++i ) {
            REQUIRE( MeshSourceDescription_TryAddVertex(
                         &desc, math::Vec3d_Make( ( ( i & 1 ) ? 1.0 : 0.0 ) + x, 3.0 + ( ( i & 2 ) ? 1.0 : 0.0 ), ( i & 4 ) ? 1.0 : 0.0 ),
                         NextId(), nullptr ) == geometry_status_t::OK );
        }
        const std::vector<std::vector<common::u32>> faces = {
            { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for ( common::usize f = 0; f < faces.size(); ++f ) {
            mesh_face_attributes_t a{};
            a.material.value = 40u + f;
            REQUIRE( MeshSourceDescription_TryAddFace( &desc, common::span_t<const common::u32>{ faces[f].data(), faces[f].size() }, NextId(),
                                                       a, nullptr ) == geometry_status_t::OK );
        }
        mesh_source_t mesh{};
        REQUIRE( MeshSource_TryBuild( &desc, pSys, &mesh ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddMesh( &doc, &mesh ) == geometry_status_t::OK );
        MeshSource_Shutdown( &mesh );
        MeshSourceDescription_Shutdown( &desc );

        // Patch.
        patch_surface_t patch{};
        REQUIRE( Patch_TryInitFlat( &patch, pSys, patch_basis_t::BIQUADRATIC_BEZIER, 3u, 3u, math::Vec3d_Make( x, 6, 0 ),
                                    math::Vec3d_Make( 2, 0, 0 ), math::Vec3d_Make( 0, 2, 0 ), NextId(), &ids ) == geometry_status_t::OK );
        patch_control_t mid = *Patch_Control( &patch, 1u, 1u );
        mid.position.z = 1.0;
        REQUIRE( Patch_TrySetControl( &patch, 1u, 1u, mid ) == geometry_status_t::OK );
        patch.materialId = 12u;
        REQUIRE( GeometryDocument_TryAddPatch( &doc, &patch ) == geometry_status_t::OK );
        patchId = patch.sourceId;
        Patch_Shutdown( &patch );

        // Heightfield.
        heightfield_t field{};
        REQUIRE( HeightField_TryInit( &field, pSys, math::Vec3d_Make( x, 10, 0 ), 1.0, 8u, 8u, 4u, NextId(), &ids ) == geometry_status_t::OK );
        heightfield_brush_t bump{};
        bump.centerX = x + 4.0; // world coordinates
        bump.centerY = 14.0;
        bump.radius = 3.0;
        bump.amount = 2.0;
        REQUIRE( HeightField_TryApplyBrush( &field, bump, nullptr ) == geometry_status_t::OK );
        REQUIRE( HeightField_TrySetHole( &field, 1u, 2u, true ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddHeightField( &doc, &field ) == geometry_status_t::OK );
        fieldId = field.sourceId;
        HeightField_Shutdown( &field );
    }

    std::vector<geometry_source_id_t> Roots() const { return { brushId, meshId, patchId, fieldId }; }
};

common::span_t<const geometry_source_id_t> SpanOf( const std::vector<geometry_source_id_t> &v )
{
    return common::span_t<const geometry_source_id_t>{ v.data(), v.size() };
}

struct document_state_t {
    common::usize cBrushes, cMeshes, cPatches, cFields, cLive, cClaimed;
    common::u64 next;
};

document_state_t StateOf( const geometry_document_t &doc )
{
    return document_state_t{ doc.brushes.nCount,
                             GeometryDocument_MeshCount( &doc ),
                             GeometryDocument_PatchCount( &doc ),
                             GeometryDocument_HeightFieldCount( &doc ),
                             GeometrySourceIdRegistry_Count( &doc.sourceIds ),
                             GeometrySourceIdRegistry_ClaimedCount( &doc.sourceIds ),
                             doc.sourceIds.allocator.next.value };
}

void CheckSameState( const geometry_document_t &doc, const document_state_t &s )
{
    const document_state_t now = StateOf( doc );
    CHECK( now.cBrushes == s.cBrushes );
    CHECK( now.cMeshes == s.cMeshes );
    CHECK( now.cPatches == s.cPatches );
    CHECK( now.cFields == s.cFields );
    CHECK( now.cLive == s.cLive );
    CHECK( now.cClaimed == s.cClaimed );
    CHECK( now.next == s.next );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &doc.sourceIds ) );
}

common::u64 MaterialOf( const geometry_document_t &doc, geometry_source_id_t brushId, common::usize iRecord )
{
    const geometry_brush_side_attribute_store_t *pRecords = GeometryDocument_FindBrushAttributes( &doc, brushId );
    REQUIRE( pRecords != nullptr );
    geometry_brush_side_attributes_t r{};
    REQUIRE( BrushSideAttributeStore_TryGet( pRecords, iRecord, &r ) == geometry_status_t::OK );
    return r.material.value;
}

// The document still saves and loads (every save-time identity audit
// passes) and the loaded copy has the same object counts.
void CheckRoundTrips( const geometry_document_t &doc )
{
    common::text_buffer_t text{};
    REQUIRE( common::TextBuffer_Init( &text, common::Allocator_GetSystem() ) );
    REQUIRE( GeometrySerialization_SaveToText( &doc, &text ).status == geometry_serialization_status_t::OK );
    geometry_document_t loaded{};
    const geometry_serialization_result_t r =
        GeometrySerialization_LoadFromText( common::TextBuffer_View( &text ), common::Allocator_GetSystem(), doc.policy, &loaded );
    CAPTURE( GeometrySerialization_StatusName( r.status ), r.field, r.iElement );
    REQUIRE( r.status == geometry_serialization_status_t::OK );
    CHECK( loaded.brushes.nCount == doc.brushes.nCount );
    CHECK( GeometryDocument_MeshCount( &loaded ) == GeometryDocument_MeshCount( &doc ) );
    CHECK( GeometryDocument_PatchCount( &loaded ) == GeometryDocument_PatchCount( &doc ) );
    CHECK( GeometryDocument_HeightFieldCount( &loaded ) == GeometryDocument_HeightFieldCount( &doc ) );
    GeometryDocument_Shutdown( &loaded );
    common::TextBuffer_Shutdown( &text );
}

geometry_source_id_t Mapped( const geometry_source_id_remap_t &remap, geometry_source_id_t old )
{
    geometry_source_id_t out{};
    REQUIRE( GeometrySourceIdRemap_Find( &remap, old, &out ) );
    return out;
}

} // namespace

TEST_CASE( "A fragment pasted back needs a remap and then enters with fresh identities", "[geometry][exchange][fragment]" )
{
    Doc d;
    d.AddAll( 0.0 );
    const std::vector<geometry_source_id_t> roots = d.Roots();

    geometry_fragment_t frag{};
    REQUIRE( GeometryFragment_Init( &frag, &d.allocator ) == geometry_status_t::OK );
    REQUIRE( GeometryFragment_TryExtract( &d.doc, SpanOf( roots ), &frag ) == geometry_status_t::OK );
    CHECK( GeometryFragment_ObjectCount( &frag ) == 4u );
    // Extraction copies, keeping IDs and records.
    CHECK( frag.brushes.pData[0]->solid.sourceId.value == d.brushId.value );
    CHECK( BrushSideAttributeStore_Count( &frag.brushes.pData[0]->attributes ) == 6u );

    // Pasting unchanged collides with the originals and changes nothing.
    const document_state_t before = StateOf( d.doc );
    CHECK( GeometryFragment_TryInsert( &frag, &d.doc, nullptr ) == geometry_status_t::IDENTITY_CONFLICT );
    CheckSameState( d.doc, before );

    geometry_source_id_remap_t remap{};
    REQUIRE( GeometryFragment_TryRemapForDocument( &frag, &d.doc, &remap ) == geometry_status_t::OK );
    CHECK( GeometrySourceIdRemap_IsValid( &remap ) );
    // Every ID of the document was remapped (the fragment is the whole document).
    CHECK( GeometrySourceIdRemap_Count( &remap ) == before.cLive );
    for ( common::usize i = 0; i < remap.entries.nCount; ++i ) {
        CHECK( remap.entries.pData[i].destination.value >= before.next );
        CHECK_FALSE( GeometrySourceIdRegistry_Contains( &d.doc.sourceIds, remap.entries.pData[i].destination ) );
    }
    // Remapping does not touch the document.
    CheckSameState( d.doc, before );

    common::vector_t<geometry_source_id_t> newRoots{};
    REQUIRE( common::Vector_Init( &newRoots, &d.allocator ) );
    REQUIRE( GeometryFragment_TryInsert( &frag, &d.doc, &newRoots ) == geometry_status_t::OK );
    REQUIRE( newRoots.nCount == 4u );
    for ( common::usize i = 0; i < 4u; ++i ) { CHECK( newRoots.pData[i].value == Mapped( remap, roots[i] ).value ); }
    CHECK( d.doc.brushes.nCount == 2u );
    CHECK( GeometryDocument_MeshCount( &d.doc ) == 2u );
    CHECK( GeometryDocument_PatchCount( &d.doc ) == 2u );
    CHECK( GeometryDocument_HeightFieldCount( &d.doc ) == 2u );
    CHECK( GeometrySourceIdRegistry_Count( &d.doc.sourceIds ) == 2u * before.cLive );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &d.doc.sourceIds ) );
    CHECK( GeometryDocument_ValidateBrushAttributes( &d.doc ) == geometry_status_t::OK );
    // Surface records, mesh attributes, and surface materials came along.
    for ( common::usize i = 0; i < 6u; ++i ) { CHECK( MaterialOf( d.doc, newRoots.pData[0], i ) == 100u + i ); }
    const mesh_source_t *pMesh = GeometryDocument_FindMesh( &d.doc, newRoots.pData[1] );
    REQUIRE( pMesh != nullptr );
    CHECK( MeshSource_Validate( pMesh, &d.allocator ).fault == mesh_source_fault_t::NONE );
    const patch_surface_t *pPatch = GeometryDocument_FindPatch( &d.doc, newRoots.pData[2] );
    REQUIRE( pPatch != nullptr );
    CHECK( pPatch->materialId == 12u );
    const heightfield_t *pField = GeometryDocument_FindHeightField( &d.doc, newRoots.pData[3] );
    const heightfield_t *pOldField = GeometryDocument_FindHeightField( &d.doc, d.fieldId );
    REQUIRE( pField != nullptr );
    REQUIRE( pOldField != nullptr );
    for ( common::usize i = 0; i < pField->heights.nCount; ++i ) { CHECK( pField->heights.pData[i] == pOldField->heights.pData[i] ); }
    for ( common::usize i = 0; i < pField->tiles.nCount; ++i ) {
        CHECK( pField->tiles.pData[i].sourceId.value == Mapped( remap, pOldField->tiles.pData[i].sourceId ).value );
    }
    // Fresh IDs allocated after the paste do not collide with pasted ones.
    CHECK( d.doc.sourceIds.allocator.next.value > remap.entries.pData[remap.entries.nCount - 1u].destination.value );
    CheckRoundTrips( d.doc );

    GeometrySourceIdRemap_Shutdown( &remap );
    GeometryFragment_Shutdown( &frag );
}

TEST_CASE( "Extraction rejects unknown and repeated IDs without changing the fragment", "[geometry][exchange][fragment]" )
{
    Doc d;
    d.AddAll( 0.0 );
    geometry_fragment_t frag{};
    REQUIRE( GeometryFragment_Init( &frag, &d.allocator ) == geometry_status_t::OK );
    const std::vector<geometry_source_id_t> first{ d.brushId };
    REQUIRE( GeometryFragment_TryExtract( &d.doc, SpanOf( first ), &frag ) == geometry_status_t::OK );

    // A side ID is not a root; the mesh before it must be dropped again.
    const std::vector<geometry_source_id_t> unknown{ d.meshId, GeometryDocument_FindBrush( &d.doc, d.brushId )->sides.pData[0].sourceId };
    CHECK( GeometryFragment_TryExtract( &d.doc, SpanOf( unknown ), &frag ) == geometry_status_t::INVALID_HANDLE );
    CHECK( GeometryFragment_ObjectCount( &frag ) == 1u );
    const std::vector<geometry_source_id_t> repeated{ d.patchId, d.patchId };
    CHECK( GeometryFragment_TryExtract( &d.doc, SpanOf( repeated ), &frag ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( GeometryFragment_ObjectCount( &frag ) == 1u );

    // Duplicate refuses the same way and leaves the document alone.
    const document_state_t before = StateOf( d.doc );
    CHECK( GeometryDocument_TryDuplicate( &d.doc, SpanOf( unknown ), math::Vec3d_Make( 1, 0, 0 ), nullptr, nullptr ) ==
           geometry_status_t::INVALID_HANDLE );
    CheckSameState( d.doc, before );
    GeometryFragment_Shutdown( &frag );
}

TEST_CASE( "Duplicate moves the copies by the offset with texture lock", "[geometry][exchange][fragment]" )
{
    Doc d;
    d.AddAll( 0.0 );
    const std::vector<geometry_source_id_t> roots = d.Roots();
    const math::vec3d_t offset = math::Vec3d_Make( 16.0, -4.0, 2.0 );
    common::vector_t<geometry_source_id_t> newRoots{};
    REQUIRE( common::Vector_Init( &newRoots, &d.allocator ) );
    geometry_source_id_remap_t remap{};
    REQUIRE( GeometryDocument_TryDuplicate( &d.doc, SpanOf( roots ), offset, &newRoots, &remap ) == geometry_status_t::OK );
    REQUIRE( newRoots.nCount == 4u );

    // Brush: every plane moved by n . offset, every record's projection
    // origin by the offset, and the originals are untouched.
    const brush_solid_t *pOld = GeometryDocument_FindBrush( &d.doc, d.brushId );
    const brush_solid_t *pNew = GeometryDocument_FindBrush( &d.doc, newRoots.pData[0] );
    REQUIRE( pOld != nullptr );
    REQUIRE( pNew != nullptr );
    REQUIRE( pNew->sides.nCount == pOld->sides.nCount );
    for ( common::usize k = 0; k < pOld->sides.nCount; ++k ) {
        const math::planed_t &a = pOld->sides.pData[k].plane;
        const math::planed_t &b = pNew->sides.pData[k].plane;
        CHECK( b.normal.x == a.normal.x );
        CHECK( b.normal.y == a.normal.y );
        CHECK( b.normal.z == a.normal.z );
        CHECK( std::fabs( b.d - ( a.d - math::Vec3d_Dot( a.normal, offset ) ) ) < 1e-12 );
        CHECK( pNew->sides.pData[k].sourceId.value == Mapped( remap, pOld->sides.pData[k].sourceId ).value );
        CHECK( pNew->sides.pData[k].iAttributeIndex == pOld->sides.pData[k].iAttributeIndex );
    }
    const geometry_brush_side_attribute_store_t *pOldRecords = GeometryDocument_FindBrushAttributes( &d.doc, d.brushId );
    const geometry_brush_side_attribute_store_t *pNewRecords = GeometryDocument_FindBrushAttributes( &d.doc, newRoots.pData[0] );
    REQUIRE( pOldRecords != nullptr );
    REQUIRE( pNewRecords != nullptr );
    for ( common::usize i = 0; i < BrushSideAttributeStore_Count( pOldRecords ); ++i ) {
        geometry_brush_side_attributes_t a{}, b{};
        REQUIRE( BrushSideAttributeStore_TryGet( pOldRecords, i, &a ) == geometry_status_t::OK );
        REQUIRE( BrushSideAttributeStore_TryGet( pNewRecords, i, &b ) == geometry_status_t::OK );
        CHECK( b.uvProjection.origin.x == a.uvProjection.origin.x + offset.x );
        CHECK( b.uvProjection.origin.y == a.uvProjection.origin.y + offset.y );
        CHECK( b.uvProjection.origin.z == a.uvProjection.origin.z + offset.z );
        CHECK( b.material.value == a.material.value );
    }

    // Mesh vertices, patch controls, heightfield origin.
    mesh_source_description_t oldDesc{}, newDesc{};
    REQUIRE( MeshSourceDescription_Init( &oldDesc, &d.allocator, GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSourceDescription_Init( &newDesc, &d.allocator, GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( GeometryDocument_FindMesh( &d.doc, d.meshId ), &oldDesc ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( GeometryDocument_FindMesh( &d.doc, newRoots.pData[1] ), &newDesc ) == geometry_status_t::OK );
    REQUIRE( newDesc.vertices.nCount == oldDesc.vertices.nCount );
    for ( common::usize i = 0; i < oldDesc.vertices.nCount; ++i ) {
        const geometry_source_id_t want = Mapped( remap, oldDesc.vertices.pData[i].sourceId );
        bool bFound = false;
        for ( common::usize j = 0; j < newDesc.vertices.nCount; ++j ) {
            if ( newDesc.vertices.pData[j].sourceId.value != want.value ) { continue; }
            bFound = true;
            CHECK( newDesc.vertices.pData[j].position.x == oldDesc.vertices.pData[i].position.x + offset.x );
            CHECK( newDesc.vertices.pData[j].position.y == oldDesc.vertices.pData[i].position.y + offset.y );
            CHECK( newDesc.vertices.pData[j].position.z == oldDesc.vertices.pData[i].position.z + offset.z );
        }
        CHECK( bFound );
    }
    for ( common::usize i = 0; i < newDesc.faces.nCount; ++i ) { CHECK( newDesc.faces.pData[i].attributes.material.value >= 40u ); }
    MeshSourceDescription_Shutdown( &oldDesc );
    MeshSourceDescription_Shutdown( &newDesc );

    const patch_surface_t *pOldPatch = GeometryDocument_FindPatch( &d.doc, d.patchId );
    const patch_surface_t *pNewPatch = GeometryDocument_FindPatch( &d.doc, newRoots.pData[2] );
    REQUIRE( pOldPatch != nullptr );
    REQUIRE( pNewPatch != nullptr );
    for ( common::usize i = 0; i < pOldPatch->controls.nCount; ++i ) {
        CHECK( pNewPatch->controls.pData[i].position.x == pOldPatch->controls.pData[i].position.x + offset.x );
        CHECK( pNewPatch->controls.pData[i].position.z == pOldPatch->controls.pData[i].position.z + offset.z );
        // Patch UVs are per control, so they travel unchanged (texture lock).
        CHECK( pNewPatch->controls.pData[i].uv.x == pOldPatch->controls.pData[i].uv.x );
    }
    const heightfield_t *pOldField = GeometryDocument_FindHeightField( &d.doc, d.fieldId );
    const heightfield_t *pNewField = GeometryDocument_FindHeightField( &d.doc, newRoots.pData[3] );
    REQUIRE( pOldField != nullptr );
    REQUIRE( pNewField != nullptr );
    CHECK( pNewField->origin.x == pOldField->origin.x + offset.x );
    CHECK( pNewField->origin.y == pOldField->origin.y + offset.y );
    CHECK( pNewField->origin.z == pOldField->origin.z + offset.z );
    CHECK( pOldField->origin.x == 0.0 );
    CheckRoundTrips( d.doc );

    GeometrySourceIdRemap_Shutdown( &remap );
}

TEST_CASE( "Translating out of range changes nothing", "[geometry][exchange][fragment]" )
{
    Doc d;
    d.AddAll( 0.0 );
    geometry_fragment_t frag{};
    REQUIRE( GeometryFragment_Init( &frag, &d.allocator ) == geometry_status_t::OK );
    REQUIRE( GeometryFragment_TryExtractAll( &d.doc, &frag ) == geometry_status_t::OK );
    REQUIRE( GeometryFragment_ObjectCount( &frag ) == 4u );
    const double d0 = frag.brushes.pData[0]->solid.sides.pData[0].plane.d;
    const double px = frag.patches.pData[0]->controls.pData[0].position.x;
    const double fx = frag.heightFields.pData[0]->origin.x;

    CHECK( GeometryFragment_TryTranslate( &frag, math::Vec3d_Make( 2.0e6, 0, 0 ) ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( GeometryFragment_TryTranslate( &frag, math::Vec3d_Make( std::numeric_limits<double>::quiet_NaN(), 0, 0 ) ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( frag.brushes.pData[0]->solid.sides.pData[0].plane.d == d0 );
    CHECK( frag.patches.pData[0]->controls.pData[0].position.x == px );
    CHECK( frag.heightFields.pData[0]->origin.x == fx );
    GeometryFragment_Shutdown( &frag );

    // A z offset that keeps the heightfield origin in range can still push
    // the top of its bump out, because samples sit at origin.z + height.
    geometry_fragment_t fieldOnly{};
    REQUIRE( GeometryFragment_Init( &fieldOnly, &d.allocator ) == geometry_status_t::OK );
    const std::vector<geometry_source_id_t> fieldRoot{ d.fieldId };
    REQUIRE( GeometryFragment_TryExtract( &d.doc, SpanOf( fieldRoot ), &fieldOnly ) == geometry_status_t::OK );
    const heightfield_t *pField = fieldOnly.heightFields.pData[0];
    double hMax = 0.0;
    for ( common::usize i = 0; i < pField->heights.nCount; ++i ) { hMax = std::fmax( hMax, pField->heights.pData[i] ); }
    REQUIRE( hMax > 0.0 );
    CHECK( GeometryFragment_TryTranslate( &fieldOnly, math::Vec3d_Make( 0, 0, kHeightFieldCoordinateMax - 0.5 * hMax ) ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( pField->origin.z == 0.0 );
    CHECK( GeometryFragment_TryTranslate( &fieldOnly, math::Vec3d_Make( 0, 0, kHeightFieldCoordinateMax - 2.0 * hMax ) ) ==
           geometry_status_t::OK );
    GeometryFragment_Shutdown( &fieldOnly );
}

TEST_CASE( "Clipboard text carries a fragment into another document", "[geometry][exchange][fragment]" )
{
    Doc source;
    source.AddAll( 0.0 );
    Doc dest;
    dest.AddAll( 50.0 ); // same ID sequence as the source: every ID collides
    const std::vector<geometry_source_id_t> roots = source.Roots();

    common::text_buffer_t clip{};
    REQUIRE( common::TextBuffer_Init( &clip, &source.allocator ) );
    {
        geometry_fragment_t copy{};
        REQUIRE( GeometryFragment_Init( &copy, &source.allocator ) == geometry_status_t::OK );
        REQUIRE( GeometryFragment_TryExtract( &source.doc, SpanOf( roots ), &copy ) == geometry_status_t::OK );
        REQUIRE( GeometryFragment_TrySaveToText( &copy, source.policy, &clip ) == geometry_status_t::OK );
        GeometryFragment_Shutdown( &copy );
    }
    CHECK( std::string( common::TextBuffer_Data( &clip ), common::TextBuffer_Length( &clip ) ).find( "cypher.geometry" ) !=
           std::string::npos );

    geometry_fragment_t paste{};
    REQUIRE( GeometryFragment_Init( &paste, &dest.allocator ) == geometry_status_t::OK );
    REQUIRE( GeometryFragment_TryLoadFromText( common::TextBuffer_View( &clip ), dest.policy, &paste ) == geometry_status_t::OK );
    REQUIRE( GeometryFragment_ObjectCount( &paste ) == 4u );
    CHECK( GeometryFragment_TryInsert( &paste, &dest.doc, nullptr ) == geometry_status_t::IDENTITY_CONFLICT );
    geometry_source_id_remap_t remap{};
    REQUIRE( GeometryFragment_TryRemapForDocument( &paste, &dest.doc, &remap ) == geometry_status_t::OK );
    common::vector_t<geometry_source_id_t> newRoots{};
    REQUIRE( common::Vector_Init( &newRoots, &dest.allocator ) );
    REQUIRE( GeometryFragment_TryInsert( &paste, &dest.doc, &newRoots ) == geometry_status_t::OK );
    CHECK( dest.doc.brushes.nCount == 2u );
    for ( common::usize i = 0; i < 6u; ++i ) { CHECK( MaterialOf( dest.doc, newRoots.pData[0], i ) == 100u + i ); }
    // The pasted copy sits where the source had it, not where dest's own is.
    const patch_surface_t *pPasted = GeometryDocument_FindPatch( &dest.doc, newRoots.pData[2] );
    REQUIRE( pPasted != nullptr );
    CHECK( pPasted->controls.pData[0].position.x == 0.0 );
    CHECK( GeometryDocument_FindPatch( &dest.doc, dest.patchId )->controls.pData[0].position.x == 50.0 );
    CheckRoundTrips( dest.doc );

    GeometrySourceIdRemap_Shutdown( &remap );
    GeometryFragment_Shutdown( &paste );
    common::TextBuffer_Shutdown( &clip );
}

TEST_CASE( "Every remap and insert allocation failure is atomic", "[geometry][exchange][fragment][allocation]" )
{
    Doc source;
    source.AddAll( 0.0 );

    SECTION( "remap leaves the fragment as it was" )
    {
        common::usize cOperation = 0u;
        {
            fail_state_t probe{};
            common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &probe };
            geometry_fragment_t frag{};
            REQUIRE( GeometryFragment_Init( &frag, &allocator ) == geometry_status_t::OK );
            REQUIRE( GeometryFragment_TryExtractAll( &source.doc, &frag ) == geometry_status_t::OK );
            const common::usize cStart = probe.cCalls;
            REQUIRE( GeometryFragment_TryRemapForDocument( &frag, &source.doc, nullptr ) == geometry_status_t::OK );
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
                REQUIRE( GeometryFragment_TryExtractAll( &source.doc, &frag ) == geometry_status_t::OK );
                state.iFailOnCall = state.cCalls + i;
                CHECK( GeometryFragment_TryRemapForDocument( &frag, &source.doc, nullptr ) == geometry_status_t::ALLOCATION_FAILED );
                state.iFailOnCall = common::CY_INVALID_SIZE;
                REQUIRE( GeometryFragment_ObjectCount( &frag ) == 4u );
                CHECK( frag.brushes.pData[0]->solid.sourceId.value == source.brushId.value );
                CHECK( frag.meshes.pData[0]->sourceId.value == source.meshId.value );
                CHECK( frag.patches.pData[0]->sourceId.value == source.patchId.value );
                CHECK( frag.heightFields.pData[0]->sourceId.value == source.fieldId.value );
                GeometryFragment_Shutdown( &frag );
            }
            CHECK( state.cLive == 0u );
        }
    }

    SECTION( "insert restores the document exactly" )
    {
        geometry_fragment_t frag{};
        REQUIRE( GeometryFragment_Init( &frag, &source.allocator ) == geometry_status_t::OK );
        REQUIRE( GeometryFragment_TryExtractAll( &source.doc, &frag ) == geometry_status_t::OK );

        common::usize cOperation = 0u;
        {
            fail_state_t probe{};
            common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &probe };
            {
                Doc dest( &allocator );
                dest.AddAll( 50.0 );
                REQUIRE( GeometryFragment_TryRemapForDocument( &frag, &dest.doc, nullptr ) == geometry_status_t::OK );
                const common::usize cStart = probe.cCalls;
                REQUIRE( GeometryFragment_TryInsert( &frag, &dest.doc, nullptr ) == geometry_status_t::OK );
                cOperation = probe.cCalls - cStart;
            }
            CHECK( probe.cLive == 0u );
        }
        REQUIRE( cOperation > 0u );
        for ( common::usize i = 1; i <= cOperation; ++i ) {
            CAPTURE( i );
            fail_state_t state{};
            common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &state };
            {
                Doc dest( &allocator );
                dest.AddAll( 50.0 );
                const document_state_t before = StateOf( dest.doc );
                state.iFailOnCall = state.cCalls + i;
                CHECK( GeometryFragment_TryInsert( &frag, &dest.doc, nullptr ) == geometry_status_t::ALLOCATION_FAILED );
                state.iFailOnCall = common::CY_INVALID_SIZE;
                CheckSameState( dest.doc, before );
                CHECK( GeometryDocument_ValidateBrushAttributes( &dest.doc ) == geometry_status_t::OK );
                // The document is fully usable afterwards: the same insert succeeds.
                REQUIRE( GeometryFragment_TryInsert( &frag, &dest.doc, nullptr ) == geometry_status_t::OK );
                CHECK( dest.doc.brushes.nCount == 2u );
            }
            CHECK( state.cLive == 0u );
        }
        GeometryFragment_Shutdown( &frag );
    }
}

} // namespace cypher::editor::geometry
