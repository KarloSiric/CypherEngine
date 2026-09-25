//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentSurfaces_Tests.cpp
//  Purpose: Tests the geometry document's patch and heightfield stores:
//           identity registration, replace/remove, conflicts, snapshots,
//           save/load round trips, cook keys, and allocation atomicity.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushSerialization.h"
#include "CypherGeometry_CookKeys.h"
#include "CypherGeometry_DocumentMeshSet.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_Snapshot.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace cypher::editor::geometry
{

namespace
{

struct Doc {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    geometry_document_t doc{};
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
    // A 3 x 3 biquadratic patch bent upward in the middle.
    void MakePatch( patch_surface_t *pOut, double x )
    {
        REQUIRE( Patch_TryInitFlat( pOut, &allocator, patch_basis_t::BIQUADRATIC_BEZIER, 3u, 3u, math::Vec3d_Make( x, 0, 0 ),
                                    math::Vec3d_Make( 2, 0, 0 ), math::Vec3d_Make( 0, 2, 0 ), NextId(), &ids ) == geometry_status_t::OK );
        patch_control_t mid = *Patch_Control( pOut, 1u, 1u );
        mid.position.z = 1.0;
        REQUIRE( Patch_TrySetControl( pOut, 1u, 1u, mid ) == geometry_status_t::OK );
        pOut->materialId = 12u;
    }
    // An 8 x 8 cell field in 4 x 4 tiles with a bump and one hole.
    void MakeField( heightfield_t *pOut )
    {
        REQUIRE( HeightField_TryInit( pOut, &allocator, math::Vec3d_Make( 0, 0, 0 ), 1.0, 8u, 8u, 4u, NextId(), &ids ) ==
                 geometry_status_t::OK );
        heightfield_brush_t bump{};
        bump.centerX = 4.0;
        bump.centerY = 4.0;
        bump.radius = 3.0;
        bump.amount = 2.0;
        REQUIRE( HeightField_TryApplyBrush( pOut, bump, nullptr ) == geometry_status_t::OK );
        REQUIRE( HeightField_TrySetHole( pOut, 1u, 2u, true ) == geometry_status_t::OK );
    }
};

} // namespace

TEST_CASE( "Patches and heightfields enter the document with their identities", "[geometry][document][surfaces]" )
{
    Doc d;
    patch_surface_t patch{};
    heightfield_t field{};
    d.MakePatch( &patch, 0.0 );
    d.MakeField( &field );
    REQUIRE( GeometryDocument_TryAddPatch( &d.doc, &patch ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddHeightField( &d.doc, &field ) == geometry_status_t::OK );
    CHECK( GeometryDocument_PatchCount( &d.doc ) == 1u );
    CHECK( GeometryDocument_HeightFieldCount( &d.doc ) == 1u );
    CHECK( GeometrySourceIdRegistry_Contains( &d.doc.sourceIds, patch.sourceId ) );
    CHECK( GeometrySourceIdRegistry_Contains( &d.doc.sourceIds, patch.controls.pData[4].sourceId ) );
    CHECK( GeometrySourceIdRegistry_Contains( &d.doc.sourceIds, field.tiles.pData[3].sourceId ) );
    // 1 + 9 patch IDs, 1 + 4 field IDs.
    CHECK( GeometrySourceIdRegistry_Count( &d.doc.sourceIds ) == 15u );
    // The stored copies are independent of the caller's objects.
    const patch_surface_t *pStored = GeometryDocument_FindPatch( &d.doc, patch.sourceId );
    REQUIRE( pStored != nullptr );
    CHECK( pStored != &patch );
    CHECK( Patch_Control( pStored, 1u, 1u )->position.z == 1.0 );

    // A second object reusing a live ID conflicts.
    CHECK( GeometryDocument_TryAddPatch( &d.doc, &patch ) == geometry_status_t::IDENTITY_CONFLICT );

    // Replace grows the patch (new control IDs) and releases nothing it keeps.
    patch_surface_t grown{};
    REQUIRE( Patch_TryClone( &patch, &d.allocator, &grown ) == geometry_status_t::OK );
    REQUIRE( Patch_TryInsertColumn( &grown, 0u, &d.ids ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryReplacePatch( &d.doc, &grown ) == geometry_status_t::OK );
    CHECK( GeometryDocument_FindPatch( &d.doc, patch.sourceId )->cColumns == 5u );
    CHECK( GeometrySourceIdRegistry_Count( &d.doc.sourceIds ) == 15u + 6u );

    // Remove retires every ID (they stay claimed so they are never reused).
    REQUIRE( GeometryDocument_TryRemoveHeightField( &d.doc, field.sourceId ) == geometry_status_t::OK );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains( &d.doc.sourceIds, field.tiles.pData[0].sourceId ) );
    CHECK( common::HashSet_Contains( &d.doc.sourceIds.claimedIds, field.tiles.pData[0].sourceId ) );
    CHECK( GeometryDocument_TryRemoveHeightField( &d.doc, field.sourceId ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &d.doc.sourceIds ) );

    // An invalid patch (a control without an ID) is refused.
    patch_surface_t bad{};
    REQUIRE( Patch_Init( &bad, &d.allocator, patch_basis_t::BIQUADRATIC_BEZIER, 3u, 3u, d.NextId() ) == geometry_status_t::OK );
    CHECK( GeometryDocument_TryAddPatch( &d.doc, &bad ) != geometry_status_t::OK );

    Patch_Shutdown( &bad );
    Patch_Shutdown( &grown );
    Patch_Shutdown( &patch );
    HeightField_Shutdown( &field );
}

TEST_CASE( "Snapshots, cook keys and saved files carry patches and heightfields", "[geometry][document][surfaces][serialization]" )
{
    Doc d;
    brush_solid_t box{};
    REQUIRE( BrushGenerator_TryMakeBox( &box, &d.allocator, d.policy, &d.ids, math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 1, 1, 1 ) ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush( &d.doc, &box ) == geometry_status_t::OK );
    patch_surface_t patch{};
    heightfield_t field{};
    d.MakePatch( &patch, 5.0 );
    d.MakeField( &field );
    REQUIRE( GeometryDocument_TryAddPatch( &d.doc, &patch ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddHeightField( &d.doc, &field ) == geometry_status_t::OK );

    geometry_snapshot_t snap{};
    REQUIRE( GeometrySnapshot_TakeFromDocument( &snap, &d.doc ) == geometry_status_t::OK );
    CHECK( GeometrySnapshot_PatchCount( &snap ) == 1u );
    CHECK( GeometrySnapshot_HeightFieldCount( &snap ) == 1u );
    cook_key_set_t keys{};
    REQUIRE( CookKeySet_Init( &keys, &d.allocator ) == geometry_status_t::OK );
    REQUIRE( CookKeySet_TryBuild( &keys, &snap ) == geometry_status_t::OK );
    const cook_source_key_t *pPatchKey = CookKeySet_Find( &keys, patch.sourceId );
    const cook_source_key_t *pFieldKey = CookKeySet_Find( &keys, field.sourceId );
    REQUIRE( ( pPatchKey != nullptr && pFieldKey != nullptr ) );
    CHECK( pPatchKey->kind == cook_source_kind_t::PATCH );
    CHECK( pFieldKey->kind == cook_source_kind_t::HEIGHTFIELD );

    // Editing the field changes its key and nothing else.
    heightfield_t edited{};
    REQUIRE( HeightField_TryClone( &field, &d.allocator, &edited ) == geometry_status_t::OK );
    edited.heights.pData[0] = 0.75;
    REQUIRE( GeometryDocument_TryReplaceHeightField( &d.doc, &edited ) == geometry_status_t::OK );
    geometry_snapshot_t snap2{};
    cook_key_set_t keys2{};
    REQUIRE( GeometrySnapshot_TakeFromDocument( &snap2, &d.doc ) == geometry_status_t::OK );
    REQUIRE( CookKeySet_Init( &keys2, &d.allocator ) == geometry_status_t::OK );
    REQUIRE( CookKeySet_TryBuild( &keys2, &snap2 ) == geometry_status_t::OK );
    CHECK_FALSE( common::ContentHash_Equals( CookKeySet_Find( &keys2, field.sourceId )->sourceHash, pFieldKey->sourceHash ) );
    CHECK( common::ContentHash_Equals( CookKeySet_Find( &keys2, patch.sourceId )->sourceHash, pPatchKey->sourceHash ) );

    // Save and load.
    common::text_buffer_t text{};
    REQUIRE( common::TextBuffer_Init( &text, &d.allocator ) );
    const geometry_serialization_result_t saved = GeometrySerialization_SaveToText( &d.doc, &text );
    REQUIRE( saved.status == geometry_serialization_status_t::OK );
    geometry_document_t loaded{};
    const geometry_serialization_result_t read =
        GeometrySerialization_LoadFromText( common::TextBuffer_View( &text ), &d.allocator, d.policy, &loaded );
    CAPTURE( GeometrySerialization_StatusName( read.status ), read.field, read.iElement );
    REQUIRE( read.status == geometry_serialization_status_t::OK );
    REQUIRE( GeometryDocument_PatchCount( &loaded ) == 1u );
    REQUIRE( GeometryDocument_HeightFieldCount( &loaded ) == 1u );
    const patch_surface_t *pLoadedPatch = GeometryDocument_FindPatch( &loaded, patch.sourceId );
    REQUIRE( pLoadedPatch != nullptr );
    CHECK( pLoadedPatch->materialId == 12u );
    CHECK( pLoadedPatch->basis == patch_basis_t::BIQUADRATIC_BEZIER );
    for ( common::usize i = 0; i < patch.controls.nCount; ++i ) {
        CHECK( pLoadedPatch->controls.pData[i].sourceId.value == patch.controls.pData[i].sourceId.value );
        CHECK( pLoadedPatch->controls.pData[i].position.z == patch.controls.pData[i].position.z );
        CHECK( pLoadedPatch->controls.pData[i].uv.x == patch.controls.pData[i].uv.x );
    }
    const heightfield_t *pLoadedField = GeometryDocument_FindHeightField( &loaded, field.sourceId );
    REQUIRE( pLoadedField != nullptr );
    CHECK( pLoadedField->heights.pData[0] == 0.75 );
    for ( common::usize i = 1; i < field.heights.nCount; ++i ) { CHECK( pLoadedField->heights.pData[i] == field.heights.pData[i] ); }
    for ( common::usize i = 0; i < field.holes.nCount; ++i ) { CHECK( pLoadedField->holes.pData[i] == field.holes.pData[i] ); }
    for ( common::usize i = 0; i < field.tiles.nCount; ++i ) {
        CHECK( pLoadedField->tiles.pData[i].sourceId.value == field.tiles.pData[i].sourceId.value );
    }
    // Saving the loaded document reproduces the same text.
    common::text_buffer_t again{};
    REQUIRE( common::TextBuffer_Init( &again, &d.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText( &loaded, &again ).status == geometry_serialization_status_t::OK );
    CHECK( std::string( common::TextBuffer_Data( &text ), common::TextBuffer_Length( &text ) ) ==
           std::string( common::TextBuffer_Data( &again ), common::TextBuffer_Length( &again ) ) );

    common::TextBuffer_Shutdown( &again );
    common::TextBuffer_Shutdown( &text );
    GeometryDocument_Shutdown( &loaded );
    CookKeySet_Shutdown( &keys );
    CookKeySet_Shutdown( &keys2 );
    GeometrySnapshot_Shutdown( &snap );
    GeometrySnapshot_Shutdown( &snap2 );
    HeightField_Shutdown( &edited );
    HeightField_Shutdown( &field );
    Patch_Shutdown( &patch );
    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Document-wide identity audits account for patches and heightfields", "[geometry][document][surfaces]" )
{
    // Mesh-set publication audits every live ID in the document; patches
    // and heightfields must not look like strays to it.
    Doc d;
    patch_surface_t patch{};
    heightfield_t field{};
    d.MakePatch( &patch, 0.0 );
    d.MakeField( &field );
    REQUIRE( GeometryDocument_TryAddPatch( &d.doc, &patch ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddHeightField( &d.doc, &field ) == geometry_status_t::OK );

    mesh_source_description_t desc{};
    REQUIRE( MeshSourceDescription_Init( &desc, &d.allocator, geometry_source_id_t{ 9000u } ) == geometry_status_t::OK );
    common::u32 v[4];
    const math::vec3d_t pts[4] = { { 0, 0, 5 }, { 1, 0, 5 }, { 1, 1, 5 }, { 0, 1, 5 } };
    for ( int i = 0; i < 4; ++i ) {
        REQUIRE( MeshSourceDescription_TryAddVertex( &desc, pts[i], geometry_source_id_t{ 9001u + static_cast<common::u64>( i ) }, &v[i] ) ==
                 geometry_status_t::OK );
    }
    REQUIRE( MeshSourceDescription_TryAddFace( &desc, common::span_t<const common::u32>{ v, 4 }, geometry_source_id_t{ 9010u },
                                               mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
    mesh_source_t mesh{};
    REQUIRE( MeshSource_TryBuild( &desc, &d.allocator, &mesh ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddMesh( &d.doc, &mesh ) == geometry_status_t::OK );

    const geometry_source_id_t removals[] = { geometry_source_id_t{ 9000u } };
    CHECK( GeometryDocument_TryPublishMeshSetExact( &d.doc, { removals, 1u }, {}, {} ) == geometry_status_t::OK );
    CHECK( GeometryDocument_MeshCount( &d.doc ) == 0u );
    CHECK( GeometryDocument_PatchCount( &d.doc ) == 1u );
    geometry_snapshot_t snap{};
    CHECK( GeometrySnapshot_TakeFromDocument( &snap, &d.doc ) == geometry_status_t::OK );

    GeometrySnapshot_Shutdown( &snap );
    MeshSource_Shutdown( &mesh );
    MeshSourceDescription_Shutdown( &desc );
    Patch_Shutdown( &patch );
    HeightField_Shutdown( &field );
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

TEST_CASE( "Every patch and heightfield publication allocation failure is atomic", "[geometry][document][surfaces][allocation]" )
{
    Doc source;
    patch_surface_t patch{};
    heightfield_t field{};
    source.MakePatch( &patch, 0.0 );
    source.MakeField( &field );
    for ( const int which : { 0, 1 } ) {
        CAPTURE( which );
        auto run = [&]( geometry_document_t *pDoc ) {
            return which == 0 ? GeometryDocument_TryAddPatch( pDoc, &patch ) : GeometryDocument_TryAddHeightField( pDoc, &field );
        };
        common::usize cOperation = 0u;
        {
            fail_state_t probe{};
            common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &probe };
            Doc d( &allocator );
            const common::usize cBefore = probe.cCalls;
            REQUIRE( run( &d.doc ) == geometry_status_t::OK );
            cOperation = probe.cCalls - cBefore;
        }
        REQUIRE( cOperation > 0u );
        for ( common::usize i = 1u; i <= cOperation; ++i ) {
            CAPTURE( i, cOperation );
            fail_state_t state{};
            common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &state };
            {
                Doc d( &allocator );
                state.iFailOnCall = state.cCalls + i;
                CHECK( run( &d.doc ) == geometry_status_t::ALLOCATION_FAILED );
                state.iFailOnCall = common::CY_INVALID_SIZE;
                CHECK( GeometryDocument_PatchCount( &d.doc ) == 0u );
                CHECK( GeometryDocument_HeightFieldCount( &d.doc ) == 0u );
                CHECK( GeometrySourceIdRegistry_Count( &d.doc.sourceIds ) == 0u );
                CHECK( GeometrySourceIdRegistry_ValidateDeep( &d.doc.sourceIds ) );
            }
            CHECK( state.cLive == 0u );
        }
    }
    Patch_Shutdown( &patch );
    HeightField_Shutdown( &field );
}

} // namespace cypher::editor::geometry
