//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentBrushAttributes_Tests.cpp
//  Purpose: Tests that the geometry document keeps every brush's surface
//           records (material + UV projection) with it: add/remove
//           alignment, replacement, undo deltas, clip transactions,
//           snapshots, save/load (including pre-surface files), cook keys,
//           and allocation-failure atomicity.
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
#include "CypherGeometry_Delta.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentBrushReplacement.h"
#include "CypherGeometry_Snapshot.h"
#include "CypherGeometry_Transaction.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

// A unit box at x as an authored brush whose record i carries material
// materialBase + i (one record per side, as BrushSource_TryBuildDefault
// makes them).
void MakeSourceBox( brush_source_t *pOut, const common::allocator_t *pAllocator, const geometry_policy_t &policy,
                    geometry_source_id_allocator_t *pIds, double x, common::u64 materialBase )
{
    brush_solid_t box{};
    REQUIRE( BrushGenerator_TryMakeBox( &box, pAllocator, policy, pIds, math::Vec3d_Make( x, 0, 0 ), math::Vec3d_Make( 1, 1, 1 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSource_TryBuildDefault( &box, pAllocator, policy, pOut ) == geometry_status_t::OK );
    BrushSolid_Shutdown( &box );
    for ( common::usize i = 0; i < BrushSideAttributeStore_Count( &pOut->attributes ); ++i ) {
        geometry_brush_side_attributes_t r{};
        REQUIRE( BrushSideAttributeStore_TryGet( &pOut->attributes, i, &r ) == geometry_status_t::OK );
        r.material.value = materialBase + i;
        r.uvProjection.offset = math::vec2d_t{ 0.125 * static_cast<double>( i ), -0.5 };
        REQUIRE( BrushSideAttributeStore_TrySet( &pOut->attributes, policy.numerical, i, r ) == geometry_status_t::OK );
    }
}

common::u64 MaterialOf( const geometry_document_t &doc, geometry_source_id_t brushId, common::usize iRecord )
{
    const geometry_brush_side_attribute_store_t *pRecords = GeometryDocument_FindBrushAttributes( &doc, brushId );
    REQUIRE( pRecords != nullptr );
    geometry_brush_side_attributes_t r{};
    REQUIRE( BrushSideAttributeStore_TryGet( pRecords, iRecord, &r ) == geometry_status_t::OK );
    return r.material.value;
}

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

struct Doc {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    geometry_document_t doc{};
    Doc() { REQUIRE( GeometryDocument_Init( &doc, &allocator, policy ) == geometry_status_t::OK ); }
    ~Doc() { GeometryDocument_Shutdown( &doc ); }
};

} // namespace

TEST_CASE( "Brushes added without records get default records covering every side", "[geometry][document][brush-attributes]" )
{
    Doc d;
    brush_solid_t box{};
    REQUIRE( BrushGenerator_TryMakeBox( &box, &d.allocator, d.policy, &d.ids, math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 1, 1, 1 ) ) ==
             geometry_status_t::OK );
    // Point one side at a record index past the others.
    box.sides.pData[2].iAttributeIndex = 9u;
    REQUIRE( GeometryDocument_TryAddBrush( &d.doc, &box ) == geometry_status_t::OK );
    const geometry_brush_side_attribute_store_t *pRecords = GeometryDocument_FindBrushAttributes( &d.doc, box.sourceId );
    REQUIRE( pRecords != nullptr );
    CHECK( BrushSideAttributeStore_Count( pRecords ) == 10u );
    CHECK( GeometryDocument_ValidateBrushAttributes( &d.doc ) == geometry_status_t::OK );
    // The solid is stored exactly (the binding is not rewritten).
    CHECK( GeometryDocument_FindBrush( &d.doc, box.sourceId )->sides.pData[2].iAttributeIndex == 9u );
    BrushSolid_Shutdown( &box );
}

TEST_CASE( "Authored brushes keep their records through add, copy and remove", "[geometry][document][brush-attributes]" )
{
    Doc d;
    brush_source_t boxes[4]{};
    for ( int i = 0; i < 4; ++i ) {
        MakeSourceBox( &boxes[i], &d.allocator, d.policy, &d.ids, 3.0 * i, 100u * static_cast<common::u64>( i + 1 ) );
        REQUIRE( GeometryDocument_TryAddBrushSource( &d.doc, &boxes[i] ) == geometry_status_t::OK );
    }
    // Remove the second: swap-erasing must keep brushes and records paired.
    REQUIRE( GeometryDocument_TryRemoveBrush( &d.doc, boxes[1].solid.sourceId ) == geometry_status_t::OK );
    CHECK( GeometryDocument_ValidateBrushAttributes( &d.doc ) == geometry_status_t::OK );
    CHECK( GeometryDocument_FindBrushAttributes( &d.doc, boxes[1].solid.sourceId ) == nullptr );
    for ( const int i : { 0, 2, 3 } ) {
        for ( common::usize r = 0; r < 6; ++r ) {
            CHECK( MaterialOf( d.doc, boxes[i].solid.sourceId, r ) == 100u * static_cast<common::u64>( i + 1 ) + r );
        }
        brush_source_t copy{};
        REQUIRE( GeometryDocument_TryCopyBrushSource( &d.doc, boxes[i].solid.sourceId, &d.allocator, &copy ) == geometry_status_t::OK );
        CHECK( BrushSource_Equal( &copy, &boxes[i] ) );
        BrushSource_Shutdown( &copy );
    }
    brush_source_t unknown{};
    CHECK( GeometryDocument_TryCopyBrushSource( &d.doc, boxes[1].solid.sourceId, &d.allocator, &unknown ) ==
           geometry_status_t::INVALID_ARGUMENT );
    for ( brush_source_t &b : boxes ) { BrushSource_Shutdown( &b ); }
}

TEST_CASE( "Brush replacement publishes records with authored outputs and defaults otherwise", "[geometry][document][brush-attributes]" )
{
    Doc d;
    brush_source_t kept{}, gone{};
    MakeSourceBox( &kept, &d.allocator, d.policy, &d.ids, 0.0, 100u );
    MakeSourceBox( &gone, &d.allocator, d.policy, &d.ids, 3.0, 200u );
    REQUIRE( GeometryDocument_TryAddBrushSource( &d.doc, &kept ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrushSource( &d.doc, &gone ) == geometry_status_t::OK );

    brush_source_t outputs[2]{};
    MakeSourceBox( &outputs[0], &d.allocator, d.policy, &d.ids, 6.0, 300u );
    MakeSourceBox( &outputs[1], &d.allocator, d.policy, &d.ids, 9.0, 400u );
    const geometry_source_id_t removals[] = { gone.solid.sourceId };
    REQUIRE( GeometryDocument_TryReplaceBrushSourcesExact( &d.doc, common::span_t<const geometry_source_id_t>{ removals, 1 },
                                                           common::span_t<const brush_source_t>{ outputs, 2 } ) == geometry_status_t::OK );
    CHECK( GeometryDocument_BrushCount( &d.doc ) == 3u );
    CHECK( GeometryDocument_ValidateBrushAttributes( &d.doc ) == geometry_status_t::OK );
    CHECK( MaterialOf( d.doc, kept.solid.sourceId, 3 ) == 103u );
    CHECK( MaterialOf( d.doc, outputs[0].solid.sourceId, 5 ) == 305u );
    CHECK( MaterialOf( d.doc, outputs[1].solid.sourceId, 0 ) == 400u );

    // The legacy entry point gives bare solids default records.
    brush_solid_t bare{};
    REQUIRE( BrushGenerator_TryMakeBox( &bare, &d.allocator, d.policy, &d.ids, math::Vec3d_Make( 12, 0, 0 ), math::Vec3d_Make( 1, 1, 1 ) ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryReplaceBrushesExact( &d.doc, {}, common::span_t<const brush_solid_t>{ &bare, 1 } ) == geometry_status_t::OK );
    CHECK( MaterialOf( d.doc, bare.sourceId, 0 ) == 0u );
    CHECK( GeometryDocument_ValidateBrushAttributes( &d.doc ) == geometry_status_t::OK );
    BrushSolid_Shutdown( &bare );
    for ( brush_source_t *p : { &kept, &gone, &outputs[0], &outputs[1] } ) { BrushSource_Shutdown( p ); }
}

TEST_CASE( "Attribute and removal deltas undo and redo materials exactly", "[geometry][document][brush-attributes][delta]" )
{
    Doc d;
    brush_source_t box{};
    MakeSourceBox( &box, &d.allocator, d.policy, &d.ids, 0.0, 100u );
    REQUIRE( GeometryDocument_TryAddBrushSource( &d.doc, &box ) == geometry_status_t::OK );
    const geometry_source_id_t id = box.solid.sourceId;

    // Assign a material to record 2.
    geometry_brush_side_attributes_t value = BrushSideAttributes_MakeDefault();
    value.material.value = 777u;
    geometry_delta_t change{}, undo{};
    REQUIRE( GeometryDelta_TryMakeAttributeChange( &d.doc, id, 2u, value, &change ) == geometry_status_t::OK );
    REQUIRE( GeometryDelta_TryApplyToDocument( &change, &d.doc ) == geometry_status_t::OK );
    CHECK( MaterialOf( d.doc, id, 2 ) == 777u );
    REQUIRE( GeometryDelta_TryComputeInverse( &change, &d.allocator, d.policy.limits, &undo ) == geometry_status_t::OK );
    REQUIRE( GeometryDelta_TryApplyToDocument( &undo, &d.doc ) == geometry_status_t::OK );
    CHECK( MaterialOf( d.doc, id, 2 ) == 102u );
    geometry_delta_t bad{};
    CHECK( GeometryDelta_TryMakeAttributeChange( &d.doc, id, 99u, value, &bad ) == geometry_status_t::INVALID_ARGUMENT );

    // Delete the brush, then undo the deletion.
    geometry_delta_t removal{}, restore{};
    REQUIRE( GeometryDelta_TryCaptureBrush( &d.doc, id, geometry_delta_kind_t::BRUSH_REMOVED, &d.allocator, &removal ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDelta_TryApplyToDocument( &removal, &d.doc ) == geometry_status_t::OK );
    CHECK( GeometryDocument_FindBrush( &d.doc, id ) == nullptr );
    REQUIRE( GeometryDelta_TryComputeInverse( &removal, &d.allocator, d.policy.limits, &restore ) == geometry_status_t::OK );
    REQUIRE( GeometryDelta_TryApplyToDocument( &restore, &d.doc ) == geometry_status_t::OK );
    for ( common::usize r = 0; r < 6; ++r ) { CHECK( MaterialOf( d.doc, id, r ) == 100u + r ); }
    CHECK( GeometryDocument_ValidateBrushAttributes( &d.doc ) == geometry_status_t::OK );

    for ( geometry_delta_t *p : { &change, &undo, &removal, &restore } ) { GeometryDelta_Shutdown( p ); }
    BrushSource_Shutdown( &box );
}

TEST_CASE( "A clip transaction covers the new side's record and cancel removes it", "[geometry][document][brush-attributes][transaction]" )
{
    Doc d;
    brush_source_t box{};
    MakeSourceBox( &box, &d.allocator, d.policy, &d.ids, 0.0, 100u );
    REQUIRE( GeometryDocument_TryAddBrushSource( &d.doc, &box ) == geometry_status_t::OK );
    const geometry_source_id_t id = box.solid.sourceId;

    // A diagonal cut addressing a new record (index 6).
    brush_solid_side_t cut{};
    REQUIRE( math::Planed_TryNormalize( math::planed_t{ math::Vec3d_Make( 1, 1, 0 ), -1.5 }, 1e-12, &cut.plane ) );
    const geometry_source_id_result_t cutId = GeometrySourceIdAllocator_Allocate( &d.ids );
    REQUIRE( cutId.status == geometry_status_t::OK );
    cut.sourceId = cutId.id;
    cut.iAttributeIndex = 6u;

    for ( const bool bCommit : { false, true } ) {
        CAPTURE( bCommit );
        geometry_transaction_t t{};
        REQUIRE( GeometryTransaction_Begin( &t, &d.doc, id ) == geometry_status_t::OK );
        REQUIRE( GeometryTransaction_TryPreviewAddSide( &t, cut, d.policy.limits ) == geometry_status_t::OK );
        CHECK( BrushSideAttributeStore_Count( GeometryDocument_FindBrushAttributes( &d.doc, id ) ) == 7u );
        CHECK( GeometryDocument_ValidateBrushAttributes( &d.doc ) == geometry_status_t::OK );
        if ( bCommit ) {
            geometry_delta_t delta{};
            geometry_changeset_t changes{};
            geometry_revision_t revision = 0u;
            REQUIRE( GeometryTransaction_Commit( &t, &delta, &changes, &revision ) == geometry_status_t::OK );
            CHECK( BrushSideAttributeStore_Count( GeometryDocument_FindBrushAttributes( &d.doc, id ) ) == 7u );
            GeometryDelta_Shutdown( &delta );
            GeometryChangeset_Shutdown( &changes );
        } else {
            REQUIRE( GeometryTransaction_Cancel( &t ) == geometry_status_t::OK );
            CHECK( BrushSideAttributeStore_Count( GeometryDocument_FindBrushAttributes( &d.doc, id ) ) == 6u );
        }
        CHECK( MaterialOf( d.doc, id, 4 ) == 104u );
    }
    BrushSource_Shutdown( &box );
}

TEST_CASE( "Snapshots, cook keys and saved files carry the records", "[geometry][document][brush-attributes][serialization]" )
{
    Doc d;
    brush_source_t box{};
    MakeSourceBox( &box, &d.allocator, d.policy, &d.ids, 0.0, 100u );
    REQUIRE( GeometryDocument_TryAddBrushSource( &d.doc, &box ) == geometry_status_t::OK );
    const geometry_source_id_t id = box.solid.sourceId;

    // Snapshot, then edit the document: the snapshot keeps the old value.
    geometry_snapshot_t before{};
    REQUIRE( GeometrySnapshot_TakeFromDocument( &before, &d.doc ) == geometry_status_t::OK );
    geometry_brush_side_attributes_t value = BrushSideAttributes_MakeDefault();
    value.material.value = 555u;
    geometry_delta_t change{};
    REQUIRE( GeometryDelta_TryMakeAttributeChange( &d.doc, id, 0u, value, &change ) == geometry_status_t::OK );
    REQUIRE( GeometryDelta_TryApplyToDocument( &change, &d.doc ) == geometry_status_t::OK );
    geometry_snapshot_t after{};
    REQUIRE( GeometrySnapshot_TakeFromDocument( &after, &d.doc ) == geometry_status_t::OK );
    geometry_brush_side_attributes_t r{};
    REQUIRE( BrushSideAttributeStore_TryGet( GeometrySnapshot_FindBrushAttributes( &before, id ), 0u, &r ) == geometry_status_t::OK );
    CHECK( r.material.value == 100u );

    // The material change alters the brush's cook key.
    cook_key_set_t keysBefore{}, keysAfter{};
    REQUIRE( CookKeySet_Init( &keysBefore, &d.allocator ) == geometry_status_t::OK );
    REQUIRE( CookKeySet_Init( &keysAfter, &d.allocator ) == geometry_status_t::OK );
    REQUIRE( CookKeySet_TryBuild( &keysBefore, &before ) == geometry_status_t::OK );
    REQUIRE( CookKeySet_TryBuild( &keysAfter, &after ) == geometry_status_t::OK );
    const cook_source_key_t *pA = CookKeySet_Find( &keysBefore, id );
    const cook_source_key_t *pB = CookKeySet_Find( &keysAfter, id );
    REQUIRE( ( pA != nullptr && pB != nullptr ) );
    CHECK_FALSE( common::ContentHash_Equals( pA->sourceHash, pB->sourceHash ) );

    // Save and load: every record survives.
    common::text_buffer_t text{};
    REQUIRE( common::TextBuffer_Init( &text, &d.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText( &d.doc, &text ).status == geometry_serialization_status_t::OK );
    geometry_document_t loaded{};
    REQUIRE( GeometrySerialization_LoadFromText( common::TextBuffer_View( &text ), &d.allocator, d.policy, &loaded ).status ==
             geometry_serialization_status_t::OK );
    CHECK( MaterialOf( loaded, id, 0 ) == 555u );
    for ( common::usize i = 1; i < 6; ++i ) { CHECK( MaterialOf( loaded, id, i ) == 100u + i ); }
    brush_source_t original{}, roundTrip{};
    REQUIRE( GeometryDocument_TryCopyBrushSource( &d.doc, id, &d.allocator, &original ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryCopyBrushSource( &loaded, id, &d.allocator, &roundTrip ) == geometry_status_t::OK );
    CHECK( BrushSource_Equal( &original, &roundTrip ) );

    // A version-3 file (no surfaces) still loads, with default records.
    std::string v3( common::TextBuffer_Data( &text ), common::TextBuffer_Length( &text ) );
    const std::string header4 = "\"cypher.geometry\" 4";
    const std::size_t at = v3.find( header4 );
    REQUIRE( at != std::string::npos );
    v3.replace( at, header4.size(), "\"cypher.geometry\" 3" );
    geometry_document_t old{};
    REQUIRE( GeometrySerialization_LoadFromText( common::string_view_t{ v3.data(), v3.size() }, &d.allocator, d.policy, &old ).status ==
             geometry_serialization_status_t::OK );
    CHECK( MaterialOf( old, id, 0 ) == 0u );
    CHECK( GeometryDocument_ValidateBrushAttributes( &old ) == geometry_status_t::OK );

    // A side addressing a record the file does not have is rejected.
    std::string broken( common::TextBuffer_Data( &text ), common::TextBuffer_Length( &text ) );
    const std::string index0 = "\"attribute_index\"=0u";
    const std::size_t ai = broken.find( index0 );
    REQUIRE( ai != std::string::npos );
    broken.replace( ai, index0.size(), "\"attribute_index\"=60u" );
    geometry_document_t rejected{};
    CHECK( GeometrySerialization_LoadFromText( common::string_view_t{ broken.data(), broken.size() }, &d.allocator, d.policy, &rejected ).status ==
           geometry_serialization_status_t::VALUE_OUT_OF_RANGE );

    GeometryDocument_Shutdown( &old );
    GeometryDocument_Shutdown( &loaded );
    BrushSource_Shutdown( &original );
    BrushSource_Shutdown( &roundTrip );
    common::TextBuffer_Shutdown( &text );
    CookKeySet_Shutdown( &keysBefore );
    CookKeySet_Shutdown( &keysAfter );
    GeometrySnapshot_Shutdown( &before );
    GeometrySnapshot_Shutdown( &after );
    GeometryDelta_Shutdown( &change );
    BrushSource_Shutdown( &box );
}

TEST_CASE( "Every authored-brush replacement allocation failure is atomic", "[geometry][document][brush-attributes][allocation]" )
{
    common::allocator_t system{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    brush_source_t a{}, b{}, out{};
    MakeSourceBox( &a, &system, policy, &ids, 0.0, 100u );
    MakeSourceBox( &b, &system, policy, &ids, 3.0, 200u );
    MakeSourceBox( &out, &system, policy, &ids, 6.0, 300u );
    auto run = [&]( geometry_document_t *pDoc ) {
        const geometry_source_id_t removals[] = { b.solid.sourceId };
        return GeometryDocument_TryReplaceBrushSourcesExact( pDoc, common::span_t<const geometry_source_id_t>{ removals, 1 },
                                                             common::span_t<const brush_source_t>{ &out, 1 } );
    };
    common::usize cOperation = 0u;
    {
        fail_state_t probe{};
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &probe };
        geometry_document_t doc{};
        REQUIRE( GeometryDocument_Init( &doc, &allocator, policy ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrushSource( &doc, &a ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrushSource( &doc, &b ) == geometry_status_t::OK );
        const common::usize cBefore = probe.cCalls;
        REQUIRE( run( &doc ) == geometry_status_t::OK );
        cOperation = probe.cCalls - cBefore;
        GeometryDocument_Shutdown( &doc );
        REQUIRE( probe.cLive == 0u );
    }
    REQUIRE( cOperation > 0u );
    for ( common::usize i = 1u; i <= cOperation; ++i ) {
        CAPTURE( i, cOperation );
        fail_state_t state{};
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &state };
        geometry_document_t doc{};
        REQUIRE( GeometryDocument_Init( &doc, &allocator, policy ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrushSource( &doc, &a ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrushSource( &doc, &b ) == geometry_status_t::OK );
        state.iFailOnCall = state.cCalls + i;
        CHECK( run( &doc ) == geometry_status_t::ALLOCATION_FAILED );
        state.iFailOnCall = common::CY_INVALID_SIZE;
        CHECK( GeometryDocument_BrushCount( &doc ) == 2u );
        CHECK( GeometryDocument_ValidateBrushAttributes( &doc ) == geometry_status_t::OK );
        CHECK( MaterialOf( doc, b.solid.sourceId, 1 ) == 201u );
        CHECK( GeometryDocument_FindBrush( &doc, out.solid.sourceId ) == nullptr );
        GeometryDocument_Shutdown( &doc );
        CHECK( state.cLive == 0u );
    }
    for ( brush_source_t *p : { &a, &b, &out } ) { BrushSource_Shutdown( p ); }
}

} // namespace cypher::editor::geometry
