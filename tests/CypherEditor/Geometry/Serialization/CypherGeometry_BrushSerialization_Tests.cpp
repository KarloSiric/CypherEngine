//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSerialization_Tests.cpp
//  Purpose: Verifies deterministic CYKV round-trip for brush geometry.
//  Details: Covers Gate 7 serialization acceptance: write/read/write byte
//           identity, malformed input rejection within budgets, empty
//           document round-trip, multi-brush documents, plane precision
//           preservation, and error paths for invalid arguments, schema
//           mismatches, missing fields, and budget violations.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushSerialization.h"
#include "CypherGeometry_Document.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherCommon_HashFNV.h"
#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Planed_Make;
using Catch::Approx;

namespace {

// Shared fixture: system allocator, default policy, ID allocator, and a
// geometry document pre-initialised for save operations.
struct SerializationFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    geometry_document_t document{};
    common::text_buffer_t textBuffer{};

    SerializationFixture()
    {
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( common::TextBuffer_Init(
                     &textBuffer, &allocator ) );
    }

    ~SerializationFixture()
    {
        GeometryDocument_Shutdown( &document );
    }

    // Builds a unit box at the given center and adds it to the document.
    void AddBox(
        math::vec3d_t center = Vec3d_Make( 0.0, 0.0, 0.0 ),
        math::vec3d_t halfExtents = Vec3d_Make( 1.0, 1.0, 1.0 ) )
    {
        brush_solid_t brush{};
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAlloc,
                     center, halfExtents ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrush(
                     &document, &brush ) ==
                 geometry_status_t::OK );
        BrushSolid_Shutdown( &brush );
    }

    // Saves the document to textBuffer and returns the result.
    geometry_serialization_result_t Save()
    {
        return GeometrySerialization_SaveToText(
            &document, &textBuffer );
    }

    // Loads from textBuffer into a fresh document. Caller must shut down
    // the loaded document.
    geometry_serialization_result_t Load(
        geometry_document_t *pLoadedOut )
    {
        return GeometrySerialization_LoadFromText(
            common::TextBuffer_View( &textBuffer ),
            &allocator,
            policy,
            pLoadedOut );
    }
};

struct serialization_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_USIZE_MAX };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *SerializationFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<serialization_failure_allocator_state_t *>( pUserData );
    const common::usize iCall = pState->cAllocationCalls++;
    if ( iCall == pState->iFailOnCall ) {
        return nullptr;
    }

    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
    }
    return pMemory;
}

void SerializationFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<serialization_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFrees;
    }
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeSerializationFailureAllocator(
    serialization_failure_allocator_state_t *pState ) noexcept
{
    return {
        &SerializationFailureAllocate,
        nullptr,
        &SerializationFailureFree,
        pState
    };
}

common::key_value_document_t *ParseSavedDocument(
    common::string_view_t text,
    const common::allocator_t *pAllocator )
{
    common::key_value_document_desc_t desc{};
    desc.pAllocator = pAllocator;
    common::key_value_document_t *pDocument =
        common::KeyValue_CreateDocument( desc );
    REQUIRE( pDocument != nullptr );
    REQUIRE( common::KeyValue_ParseText(
                 text, {}, pDocument ).status ==
             common::key_value_parse_status_t::OK );
    return pDocument;
}

void WriteDocument(
    const common::key_value_document_t *pDocument,
    const common::allocator_t *pAllocator,
    common::text_buffer_t *pTextOut )
{
    common::key_value_write_options_t options{};
    options.flags = common::KEY_VALUE_WRITE_FLAG_CANONICAL |
                    common::KEY_VALUE_WRITE_FLAG_PRETTY |
                    common::KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE;
    options.nIndentSpaces = 2u;
    options.nMaxDepth = 8u;

    const common::key_value_write_result_t measured =
        common::KeyValue_WriteText(
            common::KeyValue_Root( pDocument ), options, nullptr, 0u );
    REQUIRE( measured.status ==
             common::key_value_write_status_t::OUTPUT_TRUNCATED );
    REQUIRE( common::TextBuffer_Init(
                 pTextOut, pAllocator, measured.cchRequired ) );
    REQUIRE( common::TextBuffer_Resize(
                 pTextOut, measured.cchRequired ) );
    const common::key_value_write_result_t written =
        common::KeyValue_WriteText(
            common::KeyValue_Root( pDocument ),
            options,
            common::TextBuffer_Data( pTextOut ),
            common::TextBuffer_Capacity( pTextOut ) + 1u );
    REQUIRE( written.status == common::key_value_write_status_t::OK );
    REQUIRE( written.cchWritten == measured.cchRequired );
}

bool IsCanonicalEmptyDocument(
    const geometry_document_t &document ) noexcept
{
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

// ---------------------------------------------------------------------------
// Round-trip: write/read/write byte identity
// ---------------------------------------------------------------------------

TEST_CASE( "Serialization: empty document round-trips",
           "[Gate7][Serialization]" )
{
    SerializationFixture f;

    // Save the empty document.
    const geometry_serialization_result_t saveResult = f.Save();
    REQUIRE( saveResult.status ==
             geometry_serialization_status_t::OK );
    REQUIRE( saveResult.cchText > 0u );

    // Load it back.
    geometry_document_t loaded{};
    const geometry_serialization_result_t loadResult = f.Load( &loaded );
    REQUIRE( loadResult.status ==
             geometry_serialization_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &loaded ) == 0u );

    // Save the loaded document and compare bytes.
    common::text_buffer_t secondPass{};
    REQUIRE( common::TextBuffer_Init( &secondPass, &f.allocator ) );
    const geometry_serialization_result_t resave =
        GeometrySerialization_SaveToText( &loaded, &secondPass );
    REQUIRE( resave.status ==
             geometry_serialization_status_t::OK );
    REQUIRE( common::StringView_Equals(
                 common::TextBuffer_View( &f.textBuffer ),
                 common::TextBuffer_View( &secondPass ) ) );

    GeometryDocument_Shutdown( &loaded );
}

TEST_CASE( "Serialization: single box write/read/write identity",
           "[Gate7][Serialization]" )
{
    SerializationFixture f;
    f.AddBox();

    const geometry_serialization_result_t saveResult = f.Save();
    REQUIRE( saveResult.status ==
             geometry_serialization_status_t::OK );

    geometry_document_t loaded{};
    const geometry_serialization_result_t loadResult = f.Load( &loaded );
    REQUIRE( loadResult.status ==
             geometry_serialization_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &loaded ) == 1u );

    // Re-save and verify byte identity.
    common::text_buffer_t secondPass{};
    REQUIRE( common::TextBuffer_Init( &secondPass, &f.allocator ) );
    const geometry_serialization_result_t resave =
        GeometrySerialization_SaveToText( &loaded, &secondPass );
    REQUIRE( resave.status ==
             geometry_serialization_status_t::OK );
    REQUIRE( common::StringView_Equals(
                 common::TextBuffer_View( &f.textBuffer ),
                 common::TextBuffer_View( &secondPass ) ) );

    GeometryDocument_Shutdown( &loaded );
}

TEST_CASE( "Serialization: repeated saves have identical bytes and hash",
           "[Gate7][Serialization][Determinism]" )
{
    SerializationFixture f;
    f.AddBox( Vec3d_Make( 3.0, 0.0, 0.0 ) );
    f.AddBox( Vec3d_Make( -3.0, 0.0, 0.0 ) );
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    const common::string_view_t first =
        common::TextBuffer_View( &f.textBuffer );
    const common::hash64_t firstHash =
        common::HashFNV1a64_String( first );

    common::text_buffer_t repeated{};
    REQUIRE( common::TextBuffer_Init( &repeated, &f.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText(
                 &f.document, &repeated ).status ==
             geometry_serialization_status_t::OK );
    const common::string_view_t second =
        common::TextBuffer_View( &repeated );

    REQUIRE( common::StringView_Equals( first, second ) );
    REQUIRE( common::HashFNV1a64_String( second ) == firstHash );
}

TEST_CASE( "Serialization: multi-brush document preserves count and planes",
           "[Gate7][Serialization]" )
{
    SerializationFixture f;
    f.AddBox( Vec3d_Make( 0.0, 0.0, 0.0 ) );
    f.AddBox( Vec3d_Make( 5.0, 0.0, 0.0 ) );
    f.AddBox( Vec3d_Make( 0.0, 5.0, 0.0 ) );

    const geometry_serialization_result_t saveResult = f.Save();
    REQUIRE( saveResult.status ==
             geometry_serialization_status_t::OK );

    geometry_document_t loaded{};
    REQUIRE( f.Load( &loaded ).status ==
             geometry_serialization_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &loaded ) == 3u );

    // Verify plane data survived: check the first brush has 6 sides.
    const brush_solid_t *pFirst = loaded.brushes.pData[0];
    REQUIRE( pFirst != nullptr );
    REQUIRE( BrushSolid_SideCount( pFirst ) == 6u );

    // Verify write/read/write identity for the multi-brush document.
    common::text_buffer_t secondPass{};
    REQUIRE( common::TextBuffer_Init( &secondPass, &f.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText(
                 &loaded, &secondPass ).status ==
             geometry_serialization_status_t::OK );
    REQUIRE( common::StringView_Equals(
                 common::TextBuffer_View( &f.textBuffer ),
                 common::TextBuffer_View( &secondPass ) ) );

    GeometryDocument_Shutdown( &loaded );
}

TEST_CASE( "Serialization: plane precision preserves exact f64 values",
           "[Gate7][Serialization]" )
{
    SerializationFixture f;

    // Build a box with non-trivial half-extents to create non-integer
    // plane distances, verifying f64 precision survives the round-trip.
    f.AddBox(
        Vec3d_Make( 1.23456789012345, -0.98765432109876, 0.0 ),
        Vec3d_Make( 0.123456789, 0.987654321, 1.0 ) );

    REQUIRE( f.Save().status ==
             geometry_serialization_status_t::OK );

    geometry_document_t loaded{};
    REQUIRE( f.Load( &loaded ).status ==
             geometry_serialization_status_t::OK );

    const brush_solid_t *pOriginal = f.document.brushes.pData[0];
    const brush_solid_t *pLoaded = loaded.brushes.pData[0];
    REQUIRE( BrushSolid_SideCount( pOriginal ) ==
             BrushSolid_SideCount( pLoaded ) );

    // Compare every plane field for exact equality (not epsilon).
    const common::usize cSides = BrushSolid_SideCount( pOriginal );
    for ( common::usize i = 0u; i < cSides; ++i ) {
        brush_solid_side_t origSide{};
        brush_solid_side_t loadSide{};
        (void)BrushSolid_TryGetSide( pOriginal, i, &origSide );
        (void)BrushSolid_TryGetSide( pLoaded, i, &loadSide );
        REQUIRE( origSide.plane.normal.x == loadSide.plane.normal.x );
        REQUIRE( origSide.plane.normal.y == loadSide.plane.normal.y );
        REQUIRE( origSide.plane.normal.z == loadSide.plane.normal.z );
        REQUIRE( origSide.plane.d == loadSide.plane.d );
        REQUIRE( origSide.sourceId.value == loadSide.sourceId.value );
        REQUIRE( origSide.iAttributeIndex == loadSide.iAttributeIndex );
    }

    GeometryDocument_Shutdown( &loaded );
}

TEST_CASE( "Serialization: schema 2 preserves retired identity ownership and high-water",
           "[Gate7][Serialization][Identity]" )
{
    SerializationFixture f;
    f.AddBox();
    const geometry_source_id_t retiredBrushId =
        f.document.brushes.pData[0]->sourceId;
    REQUIRE( GeometryDocument_TryRemoveBrush(
                 &f.document, retiredBrushId ) ==
             geometry_status_t::OK );
    f.AddBox( Vec3d_Make( 5.0, 0.0, 0.0 ) );

    REQUIRE( f.document.sourceIds.allocator.next.value == 15u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount(
                 &f.document.sourceIds ) == 14u );
    REQUIRE( GeometrySourceIdRegistry_Count(
                 &f.document.sourceIds ) == 7u );
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    geometry_document_t loaded{};
    REQUIRE( f.Load( &loaded ).status ==
             geometry_serialization_status_t::OK );
    REQUIRE( loaded.sourceIds.allocator.next.value == 15u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount(
                 &loaded.sourceIds ) == 14u );
    REQUIRE( GeometrySourceIdRegistry_Count(
                 &loaded.sourceIds ) == 7u );
    REQUIRE( common::HashSet_Contains(
                 &loaded.sourceIds.claimedIds, retiredBrushId ) );
    REQUIRE_FALSE( GeometrySourceIdRegistry_Contains(
        &loaded.sourceIds, retiredBrushId ) );

    const geometry_source_id_result_t next =
        GeometrySourceIdRegistry_Allocate( &loaded.sourceIds );
    REQUIRE( next.status == geometry_status_t::OK );
    REQUIRE( next.id.value == 15u );
    REQUIRE( GeometrySourceIdRegistry_Release(
                 &loaded.sourceIds, next.id ) ==
             geometry_status_t::OK );
    GeometryDocument_Shutdown( &loaded );
}

TEST_CASE( "Serialization: schema 2 writes claimed IDs in canonical order",
           "[Gate7][Serialization][Identity]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    common::key_value_document_t *pDocument = ParseSavedDocument(
        common::TextBuffer_View( &f.textBuffer ), &f.allocator );
    const common::key_value_t *pClaimed = common::KeyValue_Find(
        common::KeyValue_Root( pDocument ),
        common::StringView_FromCString( "claimed_source_ids" ) );
    REQUIRE( pClaimed != nullptr );
    REQUIRE( common::KeyValue_ChildCount( pClaimed ) == 7u );
    common::u64 previous = 0u;
    for ( common::usize i = 0u;
          i < common::KeyValue_ChildCount( pClaimed );
          ++i ) {
        common::u64 id = 0u;
        REQUIRE( common::KeyValue_GetU64(
                     common::KeyValue_ChildAt( pClaimed, i ), &id ) );
        REQUIRE( id > previous );
        previous = id;
    }
    common::KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Serialization: schema 1 reconstructs high-water from live IDs",
           "[Gate7][Serialization][Migration]" )
{
    SerializationFixture f;
    f.AddBox();
    const geometry_source_id_t retiredBrushId =
        f.document.brushes.pData[0]->sourceId;
    REQUIRE( GeometryDocument_TryRemoveBrush(
                 &f.document, retiredBrushId ) ==
             geometry_status_t::OK );
    f.AddBox( Vec3d_Make( 5.0, 0.0, 0.0 ) );
    REQUIRE( f.document.sourceIds.allocator.next.value == 15u );
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    common::key_value_document_t *pDocument = ParseSavedDocument(
        common::TextBuffer_View( &f.textBuffer ), &f.allocator );
    common::key_value_t *pRoot = common::KeyValue_Root( pDocument );
    common::key_value_t *pNext = common::KeyValue_Find(
        pRoot, common::StringView_FromCString( "next_source_id" ) );
    common::key_value_t *pClaimed = common::KeyValue_Find(
        pRoot, common::StringView_FromCString( "claimed_source_ids" ) );
    REQUIRE( pNext != nullptr );
    REQUIRE( pClaimed != nullptr );
    REQUIRE( common::KeyValue_Remove( pDocument, pRoot, pNext ) );
    REQUIRE( common::KeyValue_Remove( pDocument, pRoot, pClaimed ) );
    REQUIRE( common::KeyValue_SetDocumentHeader(
                 pDocument,
                 {
                     common::CYKV_LANGUAGE_VERSION,
                     common::StringView_FromCString( GEOMETRY_SCHEMA_ID ),
                     1u
                 } ) );

    common::text_buffer_t schema1Text{};
    WriteDocument( pDocument, &f.allocator, &schema1Text );
    common::KeyValue_DestroyDocument( pDocument );

    geometry_document_t loaded{};
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::TextBuffer_View( &schema1Text ),
                 &f.allocator,
                 f.policy,
                 &loaded ).status ==
             geometry_serialization_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &loaded ) == 1u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount(
                 &loaded.sourceIds ) == 7u );
    REQUIRE( loaded.sourceIds.allocator.next.value == 15u );
    // Version 1 had no retired-ID set. Migration can preserve only the live
    // identities present in the old payload, while still reconstructing a
    // safe allocator high-water from their maximum value.
    REQUIRE_FALSE( common::HashSet_Contains(
        &loaded.sourceIds.claimedIds, retiredBrushId ) );

    common::text_buffer_t migrated{};
    REQUIRE( common::TextBuffer_Init( &migrated, &f.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText(
                 &loaded, &migrated ).status ==
             geometry_serialization_status_t::OK );
    common::key_value_document_t *pMigrated = ParseSavedDocument(
        common::TextBuffer_View( &migrated ), &f.allocator );
    REQUIRE( common::KeyValue_DocumentHeader(
                 pMigrated ).nSchemaVersion ==
             GEOMETRY_SCHEMA_VERSION );

    common::KeyValue_DestroyDocument( pMigrated );
    GeometryDocument_Shutdown( &loaded );
}

// ---------------------------------------------------------------------------
// Error paths: invalid arguments
// ---------------------------------------------------------------------------

TEST_CASE( "Serialization: save rejects null document",
           "[Gate7][Serialization]" )
{
    SerializationFixture f;
    REQUIRE( GeometrySerialization_SaveToText(
                 nullptr, &f.textBuffer ).status ==
             geometry_serialization_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Serialization: save rejects null text buffer",
           "[Gate7][Serialization]" )
{
    SerializationFixture f;
    REQUIRE( GeometrySerialization_SaveToText(
                 &f.document, nullptr ).status ==
             geometry_serialization_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Serialization: load rejects empty text",
           "[Gate7][Serialization]" )
{
    SerializationFixture f;
    geometry_document_t loaded{};
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::StringView_FromCString( "" ),
                 &f.allocator, f.policy, &loaded ).status ==
             geometry_serialization_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Serialization: load rejects null allocator",
           "[Gate7][Serialization]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status ==
             geometry_serialization_status_t::OK );

    geometry_document_t loaded{};
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::TextBuffer_View( &f.textBuffer ),
                 nullptr, f.policy, &loaded ).status ==
             geometry_serialization_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Serialization: load rejects already-initialized destination",
           "[Gate7][Serialization]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status ==
             geometry_serialization_status_t::OK );

    // Initialize the destination before loading — should be rejected.
    geometry_document_t loaded{};
    REQUIRE( GeometryDocument_Init(
                 &loaded, &f.allocator, f.policy ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::TextBuffer_View( &f.textBuffer ),
                 &f.allocator, f.policy, &loaded ).status ==
             geometry_serialization_status_t::DESTINATION_NOT_EMPTY );
    GeometryDocument_Shutdown( &loaded );
}

TEST_CASE( "Serialization: load requires an exact default destination",
           "[Gate7][Serialization][FailureAtomic]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    geometry_document_t loaded{};
    loaded.policy.numerical.fSnapDistance = 0.25;
    const geometry_serialization_result_t result =
        GeometrySerialization_LoadFromText(
            common::TextBuffer_View( &f.textBuffer ),
            &f.allocator,
            f.policy,
            &loaded );
    REQUIRE( result.status ==
             geometry_serialization_status_t::DESTINATION_NOT_EMPTY );
    REQUIRE( loaded.policy.numerical.fSnapDistance == 0.25 );
    REQUIRE_FALSE( GeometryDocument_IsInitialized( &loaded ) );
}

TEST_CASE( "Serialization: load rejects a destination with noncanonical mesh capacity",
           "[Gate7][Serialization][FailureAtomic]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    geometry_document_t loaded{};
    loaded.meshes.nCapacity = 1u;
    const geometry_serialization_result_t result =
        GeometrySerialization_LoadFromText(
            common::TextBuffer_View( &f.textBuffer ),
            &f.allocator,
            f.policy,
            &loaded );
    REQUIRE( result.status == geometry_serialization_status_t::DESTINATION_NOT_EMPTY );
    REQUIRE( loaded.meshes.nCapacity == 1u );
    loaded.meshes.nCapacity = 0u;
    REQUIRE( IsCanonicalEmptyDocument( loaded ) );
}

TEST_CASE( "Serialization: save rejects corrupt finite state without changing output",
           "[Gate7][Serialization][FailureAtomic]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( common::TextBuffer_Assign(
                 &f.textBuffer,
                 common::StringView_FromCString( "sentinel" ) ) );
    f.document.brushes.pData[0]->sides.pData[0].plane.d =
        std::numeric_limits<double>::quiet_NaN();

    const geometry_serialization_result_t result = f.Save();
    REQUIRE( result.status ==
             geometry_serialization_status_t::CORRUPT_DOCUMENT );
    REQUIRE( common::StringView_Equals(
        common::TextBuffer_View( &f.textBuffer ),
        common::StringView_FromCString( "sentinel" ) ) );
}

TEST_CASE( "Serialization: every save allocation failure preserves destination bytes",
           "[Gate7][Serialization][FailureAtomic][allocation]" )
{
    SerializationFixture f;
    f.AddBox();

    serialization_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeSerializationFailureAllocator( &baselineState );
    common::text_buffer_t baselineOut{};
    REQUIRE( common::TextBuffer_Init(
                 &baselineOut, &baselineAllocator ) );
    REQUIRE( common::TextBuffer_Assign(
                 &baselineOut,
                 common::StringView_FromCString( "sentinel" ) ) );
    const common::usize iBaselineBegin =
        baselineState.cAllocationCalls;
    REQUIRE( GeometrySerialization_SaveToText(
                 &f.document, &baselineOut ).status ==
             geometry_serialization_status_t::OK );
    const common::usize cSaveAllocations =
        baselineState.cAllocationCalls - iBaselineBegin;
    REQUIRE( cSaveAllocations > 0u );
    common::TextBuffer_Shutdown( &baselineOut );
    REQUIRE( baselineState.cSuccessfulAllocations ==
             baselineState.cFrees );

    for ( common::usize iFailure = 0u;
          iFailure < cSaveAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cSaveAllocations );
        serialization_failure_allocator_state_t state{};
        common::allocator_t allocator =
            MakeSerializationFailureAllocator( &state );
        common::text_buffer_t output{};
        REQUIRE( common::TextBuffer_Init( &output, &allocator ) );
        REQUIRE( common::TextBuffer_Assign(
                     &output,
                     common::StringView_FromCString( "sentinel" ) ) );
        state.iFailOnCall = state.cAllocationCalls + iFailure;

        const geometry_serialization_result_t result =
            GeometrySerialization_SaveToText( &f.document, &output );
        REQUIRE( result.status ==
                 geometry_serialization_status_t::OUT_OF_MEMORY );
        REQUIRE( common::StringView_Equals(
            common::TextBuffer_View( &output ),
            common::StringView_FromCString( "sentinel" ) ) );

        state.iFailOnCall = common::CY_USIZE_MAX;
        common::TextBuffer_Shutdown( &output );
        REQUIRE( state.cSuccessfulAllocations == state.cFrees );
    }
}

TEST_CASE( "Serialization: every load allocation failure leaves a canonical destination",
           "[Gate7][Serialization][FailureAtomic][allocation]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    serialization_failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeSerializationFailureAllocator( &baselineState );
    geometry_document_t baseline{};
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::TextBuffer_View( &f.textBuffer ),
                 &baselineAllocator,
                 f.policy,
                 &baseline ).status ==
             geometry_serialization_status_t::OK );
    const common::usize cLoadAllocations =
        baselineState.cAllocationCalls;
    REQUIRE( cLoadAllocations > 0u );
    GeometryDocument_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessfulAllocations ==
             baselineState.cFrees );

    for ( common::usize iFailure = 0u;
          iFailure < cLoadAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cLoadAllocations );
        serialization_failure_allocator_state_t state{};
        state.iFailOnCall = iFailure;
        common::allocator_t allocator =
            MakeSerializationFailureAllocator( &state );
        geometry_document_t loaded{};

        const geometry_serialization_result_t result =
            GeometrySerialization_LoadFromText(
                common::TextBuffer_View( &f.textBuffer ),
                &allocator,
                f.policy,
                &loaded );
        REQUIRE( result.status ==
                 geometry_serialization_status_t::OUT_OF_MEMORY );
        REQUIRE( IsCanonicalEmptyDocument( loaded ) );
        REQUIRE( state.cSuccessfulAllocations == state.cFrees );
    }
}

// ---------------------------------------------------------------------------
// Error paths: malformed input
// ---------------------------------------------------------------------------

TEST_CASE( "Serialization: load rejects garbage text",
           "[Gate7][Serialization]" )
{
    SerializationFixture f;
    geometry_document_t loaded{};
    const geometry_serialization_result_t result =
        GeometrySerialization_LoadFromText(
            common::StringView_FromCString( "not valid cykv data at all" ),
            &f.allocator, f.policy, &loaded );
    REQUIRE( result.status !=
             geometry_serialization_status_t::OK );
    REQUIRE( IsCanonicalEmptyDocument( loaded ) );
}

TEST_CASE( "Serialization: truncated input is failure-atomic",
           "[Gate7][Serialization][FailureAtomic]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    const common::string_view_t saved =
        common::TextBuffer_View( &f.textBuffer );
    REQUIRE( saved.cchLength > 8u );
    const common::usize truncationLengths[]{
        1u,
        saved.cchLength / 3u,
        saved.cchLength / 2u,
        saved.cchLength - 1u
    };
    for ( const common::usize cch : truncationLengths ) {
        CAPTURE( cch, saved.cchLength );
        geometry_document_t loaded{};
        const geometry_serialization_result_t result =
            GeometrySerialization_LoadFromText(
                { saved.pData, cch },
                &f.allocator,
                f.policy,
                &loaded );
        REQUIRE( result.status ==
                 geometry_serialization_status_t::CYKV_PARSE_FAILED );
        REQUIRE( IsCanonicalEmptyDocument( loaded ) );
    }
}

TEST_CASE( "Serialization: wrong brush container type is rejected exactly",
           "[Gate7][Serialization][Bounds]" )
{
    SerializationFixture f;
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    common::key_value_document_t *pDocument = ParseSavedDocument(
        common::TextBuffer_View( &f.textBuffer ), &f.allocator );
    common::key_value_t *pRoot = common::KeyValue_Root( pDocument );
    common::key_value_t *pBrushes = common::KeyValue_Find(
        pRoot, common::StringView_FromCString( "brushes" ) );
    REQUIRE( pBrushes != nullptr );
    REQUIRE( common::KeyValue_Remove( pDocument, pRoot, pBrushes ) );
    pBrushes = common::KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        common::StringView_FromCString( "brushes" ),
        common::key_value_type_t::U64 );
    REQUIRE( pBrushes != nullptr );
    REQUIRE( common::KeyValue_SetU64( pDocument, pBrushes, 0u ) );

    common::text_buffer_t malformed{};
    WriteDocument( pDocument, &f.allocator, &malformed );
    common::KeyValue_DestroyDocument( pDocument );

    geometry_document_t loaded{};
    const geometry_serialization_result_t result =
        GeometrySerialization_LoadFromText(
            common::TextBuffer_View( &malformed ),
            &f.allocator,
            f.policy,
            &loaded );
    REQUIRE( result.status ==
             geometry_serialization_status_t::TYPE_MISMATCH );
    REQUIRE( IsCanonicalEmptyDocument( loaded ) );
}

TEST_CASE( "Serialization: load enforces the document-wide side budget",
           "[Gate7][Serialization][Bounds]" )
{
    SerializationFixture f;
    for ( common::usize i = 0u; i < 43u; ++i ) {
        f.AddBox( Vec3d_Make( static_cast<double>( i ) * 3.0, 0.0, 0.0 ) );
    }
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    geometry_policy_t constrained = f.policy;
    constrained.limits.cBrushesMax = 64u;
    constrained.limits.cBrushSidesMax = 256u;
    constrained.limits.cBrushSidesPerBrushMax = 6u;
    REQUIRE( GeometryPolicy_IsValid( constrained ) );

    geometry_document_t loaded{};
    const geometry_serialization_result_t result =
        GeometrySerialization_LoadFromText(
            common::TextBuffer_View( &f.textBuffer ),
            &f.allocator,
            constrained,
            &loaded );
    REQUIRE( result.status ==
             geometry_serialization_status_t::LIMIT_EXCEEDED );
    REQUIRE( IsCanonicalEmptyDocument( loaded ) );
}

TEST_CASE( "Serialization: attribute index overflow is rejected without narrowing",
           "[Gate7][Serialization][Bounds]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    common::key_value_document_t *pDocument = ParseSavedDocument(
        common::TextBuffer_View( &f.textBuffer ), &f.allocator );
    common::key_value_t *pRoot = common::KeyValue_Root( pDocument );
    common::key_value_t *pBrushes = common::KeyValue_Find(
        pRoot, common::StringView_FromCString( "brushes" ) );
    REQUIRE( pBrushes != nullptr );
    common::key_value_t *pBrush = common::KeyValue_ChildAt( pBrushes, 0u );
    common::key_value_t *pSides = common::KeyValue_Find(
        pBrush, common::StringView_FromCString( "sides" ) );
    REQUIRE( pSides != nullptr );
    common::key_value_t *pSide = common::KeyValue_ChildAt( pSides, 0u );
    common::key_value_t *pAttribute = common::KeyValue_Find(
        pSide, common::StringView_FromCString( "attribute_index" ) );
    REQUIRE( pAttribute != nullptr );
    REQUIRE( common::KeyValue_SetU64(
                 pDocument,
                 pAttribute,
                 static_cast<common::u64>( common::CY_U32_MAX ) + 1u ) );

    common::text_buffer_t malformed{};
    WriteDocument( pDocument, &f.allocator, &malformed );
    common::KeyValue_DestroyDocument( pDocument );

    geometry_document_t loaded{};
    const geometry_serialization_result_t result =
        GeometrySerialization_LoadFromText(
            common::TextBuffer_View( &malformed ),
            &f.allocator,
            f.policy,
            &loaded );
    REQUIRE( result.status ==
             geometry_serialization_status_t::VALUE_OUT_OF_RANGE );
    REQUIRE( IsCanonicalEmptyDocument( loaded ) );
}

TEST_CASE( "Serialization: schema 2 rejects duplicate claimed identities",
           "[Gate7][Serialization][Identity]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    common::key_value_document_t *pDocument = ParseSavedDocument(
        common::TextBuffer_View( &f.textBuffer ), &f.allocator );
    common::key_value_t *pClaimed = common::KeyValue_Find(
        common::KeyValue_Root( pDocument ),
        common::StringView_FromCString( "claimed_source_ids" ) );
    REQUIRE( pClaimed != nullptr );
    common::u64 firstId = 0u;
    REQUIRE( common::KeyValue_GetU64(
                 common::KeyValue_ChildAt( pClaimed, 0u ), &firstId ) );
    common::key_value_t *pDuplicate = common::KeyValue_ArrayAppend(
        pDocument, pClaimed, common::key_value_type_t::U64 );
    REQUIRE( pDuplicate != nullptr );
    REQUIRE( common::KeyValue_SetU64(
                 pDocument, pDuplicate, firstId ) );

    common::text_buffer_t malformed{};
    WriteDocument( pDocument, &f.allocator, &malformed );
    common::KeyValue_DestroyDocument( pDocument );

    geometry_document_t loaded{};
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::TextBuffer_View( &malformed ),
                 &f.allocator,
                 f.policy,
                 &loaded ).status ==
             geometry_serialization_status_t::DUPLICATE_SOURCE_ID );
    REQUIRE( IsCanonicalEmptyDocument( loaded ) );
}

TEST_CASE( "Serialization: schema 2 requires every live identity to be claimed",
           "[Gate7][Serialization][Identity]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    common::key_value_document_t *pDocument = ParseSavedDocument(
        common::TextBuffer_View( &f.textBuffer ), &f.allocator );
    common::key_value_t *pClaimed = common::KeyValue_Find(
        common::KeyValue_Root( pDocument ),
        common::StringView_FromCString( "claimed_source_ids" ) );
    REQUIRE( pClaimed != nullptr );
    common::key_value_t *pFirst = common::KeyValue_ChildAt( pClaimed, 0u );
    REQUIRE( pFirst != nullptr );
    REQUIRE( common::KeyValue_Remove( pDocument, pClaimed, pFirst ) );

    common::text_buffer_t malformed{};
    WriteDocument( pDocument, &f.allocator, &malformed );
    common::KeyValue_DestroyDocument( pDocument );

    geometry_document_t loaded{};
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::TextBuffer_View( &malformed ),
                 &f.allocator,
                 f.policy,
                 &loaded ).status ==
             geometry_serialization_status_t::INVALID_SOURCE_ID );
    REQUIRE( IsCanonicalEmptyDocument( loaded ) );
}

TEST_CASE( "Serialization: schema 2 validates claimed IDs against high-water",
           "[Gate7][Serialization][Identity]" )
{
    SerializationFixture f;
    f.AddBox();
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    common::key_value_document_t *pDocument = ParseSavedDocument(
        common::TextBuffer_View( &f.textBuffer ), &f.allocator );
    common::key_value_t *pNext = common::KeyValue_Find(
        common::KeyValue_Root( pDocument ),
        common::StringView_FromCString( "next_source_id" ) );
    REQUIRE( pNext != nullptr );
    REQUIRE( common::KeyValue_SetU64( pDocument, pNext, 1u ) );

    common::text_buffer_t malformed{};
    WriteDocument( pDocument, &f.allocator, &malformed );
    common::KeyValue_DestroyDocument( pDocument );

    geometry_document_t loaded{};
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::TextBuffer_View( &malformed ),
                 &f.allocator,
                 f.policy,
                 &loaded ).status ==
             geometry_serialization_status_t::VALUE_OUT_OF_RANGE );
    REQUIRE( IsCanonicalEmptyDocument( loaded ) );
}

TEST_CASE( "Serialization: unsupported future schema is rejected",
           "[Gate7][Serialization][Version]" )
{
    SerializationFixture f;
    REQUIRE( f.Save().status == geometry_serialization_status_t::OK );

    common::key_value_document_t *pDocument = ParseSavedDocument(
        common::TextBuffer_View( &f.textBuffer ), &f.allocator );
    REQUIRE( common::KeyValue_SetDocumentHeader(
                 pDocument,
                 {
                     common::CYKV_LANGUAGE_VERSION,
                     common::StringView_FromCString( GEOMETRY_SCHEMA_ID ),
                     GEOMETRY_SCHEMA_VERSION + 1u
                 } ) );
    common::text_buffer_t future{};
    WriteDocument( pDocument, &f.allocator, &future );
    common::KeyValue_DestroyDocument( pDocument );

    geometry_document_t loaded{};
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::TextBuffer_View( &future ),
                 &f.allocator,
                 f.policy,
                 &loaded ).status ==
             geometry_serialization_status_t::VERSION_UNSUPPORTED );
    REQUIRE( IsCanonicalEmptyDocument( loaded ) );
}

TEST_CASE( "Serialization: load rejects wrong schema",
           "[Gate7][Serialization]" )
{
    // Build valid CYKV text with a different schema ID.
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    common::key_value_document_desc_t desc{};
    desc.pAllocator = &allocator;

    common::key_value_document_t *pDoc =
        common::KeyValue_CreateDocument( desc );
    REQUIRE( pDoc != nullptr );

    REQUIRE( common::KeyValue_SetDocumentHeader(
                 pDoc,
                 {
                     common::CYKV_LANGUAGE_VERSION,
                     common::StringView_FromCString( "wrong.schema" ),
                     1u
                 } ) );
    REQUIRE( common::KeyValue_SetRootType(
                 pDoc, common::key_value_type_t::OBJECT ) );

    common::key_value_t *pRoot = common::KeyValue_Root( pDoc );
    (void)common::KeyValue_ObjectInsert(
        pDoc, pRoot,
        common::StringView_FromCString( "brushes" ),
        common::key_value_type_t::ARRAY );

    common::key_value_write_options_t writeOpts{};
    writeOpts.flags = common::KEY_VALUE_WRITE_FLAG_PRETTY |
                      common::KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE;
    writeOpts.nIndentSpaces = 2u;
    writeOpts.nMaxDepth = 8u;

    // Measure.
    const common::key_value_write_result_t measured =
        common::KeyValue_WriteText( pRoot, writeOpts, nullptr, 0u );
    REQUIRE( measured.status ==
             common::key_value_write_status_t::OUTPUT_TRUNCATED );

    common::text_buffer_t buf{};
    REQUIRE( common::TextBuffer_Init(
                 &buf, &allocator, measured.cchRequired ) );
    REQUIRE( common::TextBuffer_Resize( &buf, measured.cchRequired ) );

    const common::key_value_write_result_t written =
        common::KeyValue_WriteText(
            pRoot, writeOpts,
            common::TextBuffer_Data( &buf ),
            common::TextBuffer_Capacity( &buf ) + 1u );
    REQUIRE( written.status == common::key_value_write_status_t::OK );

    common::KeyValue_DestroyDocument( pDoc );

    geometry_policy_t policy{};
    geometry_document_t loaded{};
    const geometry_serialization_result_t result =
        GeometrySerialization_LoadFromText(
            common::TextBuffer_View( &buf ),
            &allocator, policy, &loaded );
    REQUIRE( result.status ==
             geometry_serialization_status_t::SCHEMA_MISMATCH );
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

TEST_CASE( "Serialization: StatusName returns non-null for every status",
           "[Gate7][Serialization]" )
{
    REQUIRE( GeometrySerialization_StatusName(
                 geometry_serialization_status_t::OK ) != nullptr );
    REQUIRE( GeometrySerialization_StatusName(
                 geometry_serialization_status_t::INVALID_ARGUMENT ) !=
             nullptr );
    REQUIRE( GeometrySerialization_StatusName(
                 geometry_serialization_status_t::DOCUMENT_INIT_FAILED ) !=
             nullptr );
}

} // namespace cypher::editor::geometry
