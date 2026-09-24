//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Document_Tests.cpp
//  Purpose: Verifies the geometry document store, atomic change sets,
//           identity bookkeeping, and immutable snapshots.
//  Details: Covers the Document acceptance gate: publish an immutable
//           snapshot of one validated brush, commit a revisioned change,
//           and prove the older snapshot remains unchanged. Stale revision,
//           validation failure, and allocation failure publish nothing.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_Document.h"
#include "CypherGeometry_Snapshot.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::planed_t;
using cypher::math::vec3d_t;

namespace {

struct failing_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *FailingAllocate(
    void *pUserData, common::usize cbSize, common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failing_allocator_state_t *>( pUserData );
    const common::usize iAllocation = pState->cAllocationCalls++;
    if ( iAllocation == pState->iFailure ) {
        return nullptr;
    }
    return common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
}

void FailingFree(
    void *, void *pMemory, common::usize cbSize, common::usize nAlignment ) noexcept
{
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeFailingAllocator( failing_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{ FailingAllocate, nullptr, FailingFree, pState };
}

// The document's allocator must outlive every value and snapshot it
// creates. By default the holder uses the process-lifetime system
// allocator, so snapshots may safely outlive the holder. A custom
// allocator is stored in the holder and must not be outlived.
struct document_holder_t {
    common::allocator_t custom{};
    geometry_document_t document{};

    explicit document_holder_t( const geometry_policy_t &policy = {} )
    {
        geometry_document_desc_t desc{};
        desc.pAllocator = common::Allocator_GetSystem();
        desc.policy = policy;
        REQUIRE( GeometryDocument_Init( &document, desc ) == geometry_status_t::OK );
    }
    explicit document_holder_t( const common::allocator_t &allocator,
                                const geometry_policy_t &policy = {} )
        : custom( allocator )
    {
        geometry_document_desc_t desc{};
        desc.pAllocator = &custom;
        desc.policy = policy;
        REQUIRE( GeometryDocument_Init( &document, desc ) == geometry_status_t::OK );
    }
    ~document_holder_t() { GeometryDocument_Shutdown( &document ); }
};

// Owns one caller reference to a value.
struct value_ref_t {
    const geometry_brush_value_t *p{ nullptr };
    value_ref_t() = default;
    value_ref_t( const value_ref_t & ) = delete;
    value_ref_t &operator=( const value_ref_t & ) = delete;
    ~value_ref_t() { BrushValue_Release( p ); }
};

struct snapshot_ref_t {
    const geometry_document_snapshot_t *p{ nullptr };
    snapshot_ref_t() = default;
    snapshot_ref_t( const snapshot_ref_t & ) = delete;
    snapshot_ref_t &operator=( const snapshot_ref_t & ) = delete;
    ~snapshot_ref_t() { GeometrySnapshot_Release( p ); }
};

// Builds a box value whose seven IDs come from the document registry.
void MakeBoxValue(
    geometry_document_t *pDocument,
    vec3d_t center,
    vec3d_t halfExtents,
    value_ref_t *pOut )
{
    geometry_source_id_t ids[BRUSH_GENERATOR_BOX_ID_COUNT]{};
    REQUIRE( GeometryDocument_TryAllocateSourceIds(
                 pDocument,
                 common::span_t<geometry_source_id_t>{ ids, BRUSH_GENERATOR_BOX_ID_COUNT } ) ==
             geometry_status_t::OK );

    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeBoxWithIds(
                 &brush, common::Allocator_GetSystem(), pDocument->policy, ids,
                 center, halfExtents ) == geometry_status_t::OK );
    geometry_brush_side_attribute_store_t attributes{};
    REQUIRE( BrushSideAttributeStore_Init( &attributes, common::Allocator_GetSystem() ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppendDefaults( &attributes, pDocument->policy, 6u ) ==
             geometry_status_t::OK );

    const geometry_status_t status = GeometryDocument_TryCreateBrushValue(
        pDocument, &brush, &attributes, &pOut->p, nullptr );

    BrushSideAttributeStore_Shutdown( &attributes );
    BrushSolid_Shutdown( &brush );
    REQUIRE( status == geometry_status_t::OK );
    REQUIRE( pOut->p != nullptr );
}

// Builds a new value from an existing one with one side plane replaced.
geometry_status_t MakeEditedValue(
    geometry_document_t *pDocument,
    const geometry_brush_value_t *pSource,
    common::usize iSide,
    planed_t plane,
    value_ref_t *pOut )
{
    brush_solid_t brush{};
    REQUIRE( BrushSolid_Init( &brush, common::Allocator_GetSystem(), pSource->brush.sourceId ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_TryCopyFrom( &brush, &pSource->brush, pDocument->policy.limits ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_TrySetSidePlane( &brush, iSide, plane ) == geometry_status_t::OK );

    const geometry_status_t status = GeometryDocument_TryCreateBrushValue(
        pDocument, &brush, &pSource->attributes, &pOut->p, nullptr );
    BrushSolid_Shutdown( &brush );
    return status;
}

geometry_status_t ApplyOne(
    geometry_document_t *pDocument,
    geometry_document_change_kind_t kind,
    geometry_source_id_t brushId,
    const geometry_brush_value_t *pValue )
{
    const geometry_document_change_t change{ kind, brushId, pValue };
    return GeometryDocument_TryApply(
        pDocument, GeometryDocument_Revision( pDocument ),
        common::span_t<const geometry_document_change_t>{ &change, 1u }, nullptr );
}

constexpr auto INSERT = geometry_document_change_kind_t::INSERT_BRUSH;
constexpr auto REPLACE = geometry_document_change_kind_t::REPLACE_BRUSH;
constexpr auto REMOVE = geometry_document_change_kind_t::REMOVE_BRUSH;

planed_t PlusXPlaneAt( double x ) noexcept
{
    return cypher::math::Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -x );
}

// Captures every externally observable document property so failure paths
// can prove the document did not change at all.
struct document_state_t {
    common::u64 revision;
    common::usize cBrushes;
    common::u64 cSides;
    common::usize cOwners;
    common::usize cLive;
    common::usize cClaimed;
    common::u64 nextId;
};

document_state_t Capture( const geometry_document_t &document )
{
    return {
        document.revision,
        GeometryDocument_BrushCount( &document ),
        document.cSides,
        common::HashMap_Count( &document.owners ),
        GeometrySourceIdRegistry_Count( &document.registry ),
        GeometrySourceIdRegistry_ClaimedCount( &document.registry ),
        document.registry.allocator.next.value,
    };
}

void RequireUnchanged( const geometry_document_t &document, const document_state_t &before )
{
    const document_state_t after = Capture( document );
    REQUIRE( after.revision == before.revision );
    REQUIRE( after.cBrushes == before.cBrushes );
    REQUIRE( after.cSides == before.cSides );
    REQUIRE( after.cOwners == before.cOwners );
    REQUIRE( after.cLive == before.cLive );
    REQUIRE( after.cClaimed == before.cClaimed );
    REQUIRE( after.nextId == before.nextId );
    REQUIRE( GeometryDocument_ValidateDeep( &document ) );
}

} // namespace

//==========================================================================
// Lifecycle
//==========================================================================

TEST_CASE( "document init validates arguments and starts at revision 1",
           "[editor][geometry][document]" ) {
    geometry_document_t document{};
    geometry_document_desc_t desc{};
    REQUIRE( GeometryDocument_Init( nullptr, desc ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometryDocument_Init( &document, desc ) == geometry_status_t::INVALID_ARGUMENT );

    desc.pAllocator = common::Allocator_GetSystem();
    desc.policy.numerical.fWeldDistance = -1.0;
    REQUIRE( GeometryDocument_Init( &document, desc ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( document.bInitialized );

    desc.policy = {};
    REQUIRE( GeometryDocument_Init( &document, desc ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_Init( &document, desc ) == geometry_status_t::ALREADY_INITIALIZED );
    REQUIRE( GeometryDocument_Revision( &document ) == 1u );
    REQUIRE( GeometryDocument_BrushCount( &document ) == 0u );
    REQUIRE( GeometryDocument_ValidateDeep( &document ) );

    GeometryDocument_Shutdown( &document );
    REQUIRE( GeometryDocument_Revision( &document ) == 0u );
    GeometryDocument_Shutdown( &document ); // idempotent
}

TEST_CASE( "uninitialized document rejects every operation",
           "[editor][geometry][document]" ) {
    geometry_document_t document{};
    geometry_brush_handle_t handle{};
    const geometry_brush_value_t *pValue = nullptr;
    geometry_source_id_t owner{};
    geometry_source_id_t ids[2]{};
    const geometry_document_snapshot_t *pSnapshot = nullptr;
    const geometry_document_change_t change{ REMOVE, geometry_source_id_t{ 1u }, nullptr };

    REQUIRE( GeometryDocument_TryFindBrush( &document, geometry_source_id_t{ 1u }, &handle ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( GeometryDocument_TryGetBrush( &document, handle, &pValue ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( GeometryDocument_TryFindOwner( &document, geometry_source_id_t{ 1u }, &owner ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( GeometryDocument_TryAllocateSourceIds(
                 &document, common::span_t<geometry_source_id_t>{ ids, 2u } ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( GeometryDocument_TryApply(
                 &document, 0u,
                 common::span_t<const geometry_document_change_t>{ &change, 1u }, nullptr ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( GeometryDocument_TryAcquireSnapshot( &document, &pSnapshot ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( pSnapshot == nullptr );
    REQUIRE_FALSE( GeometryDocument_ValidateDeep( &document ) );
}

//==========================================================================
// Publication and snapshots
//==========================================================================

TEST_CASE( "a committed change leaves the older snapshot unchanged",
           "[editor][geometry][document][snapshot]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;

    value_ref_t box{};
    MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );
    const geometry_source_id_t brushId = box.p->brush.sourceId;
    REQUIRE( ApplyOne( pDocument, INSERT, brushId, box.p ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_Revision( pDocument ) == 2u );
    REQUIRE( BrushValue_RefCount( box.p ) == 2u );

    snapshot_ref_t before{};
    REQUIRE( GeometryDocument_TryAcquireSnapshot( pDocument, &before.p ) == geometry_status_t::OK );
    REQUIRE( GeometrySnapshot_Revision( before.p ) == 2u );
    REQUIRE( GeometrySnapshot_BrushCount( before.p ) == 1u );

    value_ref_t dragged{};
    REQUIRE( MakeEditedValue( pDocument, box.p, 0u, PlusXPlaneAt( 3.0 ), &dragged ) ==
             geometry_status_t::OK );
    REQUIRE( ApplyOne( pDocument, REPLACE, brushId, dragged.p ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_Revision( pDocument ) == 3u );

    // The old snapshot still sees the original +X plane and bounds.
    const geometry_brush_value_t *pOld = nullptr;
    REQUIRE( GeometrySnapshot_TryFindBrush( before.p, brushId, &pOld ) == geometry_status_t::OK );
    REQUIRE( pOld == box.p );
    REQUIRE( pOld->brush.sides.pData[0].plane.d == -1.0 );
    REQUIRE( pOld->bounds.maximum.x == 1.0 );

    snapshot_ref_t after{};
    REQUIRE( GeometryDocument_TryAcquireSnapshot( pDocument, &after.p ) == geometry_status_t::OK );
    REQUIRE( after.p != before.p );
    REQUIRE( GeometrySnapshot_Revision( after.p ) == 3u );
    const geometry_brush_value_t *pNew = nullptr;
    REQUIRE( GeometrySnapshot_TryFindBrush( after.p, brushId, &pNew ) == geometry_status_t::OK );
    REQUIRE( pNew == dragged.p );
    REQUIRE( pNew->bounds.maximum.x == 3.0 );

    // Identity survives the edit: same brush ID, same side IDs, same handle.
    for ( common::usize i = 0u; i < 6u; ++i ) {
        REQUIRE( pNew->brush.sides.pData[i].sourceId.value ==
                 pOld->brush.sides.pData[i].sourceId.value );
    }
    REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );
}

TEST_CASE( "snapshots outlive the document that published them",
           "[editor][geometry][document][snapshot]" ) {
    snapshot_ref_t snapshot{};
    geometry_source_id_t brushId{};
    {
        document_holder_t holder{};
        value_ref_t box{};
        MakeBoxValue( &holder.document, Vec3d_Make( 5.0, 0.0, 0.0 ),
                      Vec3d_Make( 1.0, 2.0, 3.0 ), &box );
        brushId = box.p->brush.sourceId;
        REQUIRE( ApplyOne( &holder.document, INSERT, brushId, box.p ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAcquireSnapshot( &holder.document, &snapshot.p ) ==
                 geometry_status_t::OK );
    }
    const geometry_brush_value_t *pValue = nullptr;
    REQUIRE( GeometrySnapshot_TryFindBrush( snapshot.p, brushId, &pValue ) ==
             geometry_status_t::OK );
    REQUIRE( BrushValue_RefCount( pValue ) == 1u );
    REQUIRE( BrushBoundary_VertexCount( &pValue->boundary ) == 8u );
    REQUIRE( pValue->bounds.minimum.x == 4.0 );
    REQUIRE( pValue->bounds.maximum.z == 3.0 );
}

TEST_CASE( "snapshot acquisition is cached per revision and sorted by brush ID",
           "[editor][geometry][document][snapshot]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;

    value_ref_t a{};
    value_ref_t b{};
    value_ref_t c{};
    MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &a );
    MakeBoxValue( pDocument, Vec3d_Make( 4.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &b );
    MakeBoxValue( pDocument, Vec3d_Make( 8.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &c );

    // Insert in reverse ID order; the snapshot must still sort ascending.
    const geometry_document_change_t changes[3] = {
        { INSERT, c.p->brush.sourceId, c.p },
        { INSERT, a.p->brush.sourceId, a.p },
        { INSERT, b.p->brush.sourceId, b.p },
    };
    REQUIRE( GeometryDocument_TryApply(
                 pDocument, 1u,
                 common::span_t<const geometry_document_change_t>{ changes, 3u }, nullptr ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_Revision( pDocument ) == 2u );

    snapshot_ref_t first{};
    snapshot_ref_t second{};
    REQUIRE( GeometryDocument_TryAcquireSnapshot( pDocument, &first.p ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAcquireSnapshot( pDocument, &second.p ) == geometry_status_t::OK );
    REQUIRE( first.p == second.p );
    REQUIRE( GeometrySnapshot_BrushCount( first.p ) == 3u );

    geometry_snapshot_brush_t entry{};
    common::u64 previous = 0u;
    for ( common::usize i = 0u; i < 3u; ++i ) {
        REQUIRE( GeometrySnapshot_TryGetBrush( first.p, i, &entry ) == geometry_status_t::OK );
        REQUIRE( entry.brushId.value > previous );
        previous = entry.brushId.value;
        const geometry_brush_value_t *pLive = nullptr;
        REQUIRE( GeometryDocument_TryGetBrush( pDocument, entry.handle, &pLive ) ==
                 geometry_status_t::OK );
        REQUIRE( pLive == entry.pValue );
    }
    REQUIRE( GeometrySnapshot_TryGetBrush( first.p, 3u, &entry ) ==
             geometry_status_t::INVALID_ARGUMENT );

    const geometry_brush_value_t *pMissing = nullptr;
    REQUIRE( GeometrySnapshot_TryFindBrush( first.p, geometry_source_id_t{ 999u }, &pMissing ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( pMissing == nullptr );
}

TEST_CASE( "empty documents publish empty snapshots",
           "[editor][geometry][document][snapshot]" ) {
    document_holder_t holder{};
    snapshot_ref_t snapshot{};
    REQUIRE( GeometryDocument_TryAcquireSnapshot( &holder.document, &snapshot.p ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySnapshot_Revision( snapshot.p ) == 1u );
    REQUIRE( GeometrySnapshot_BrushCount( snapshot.p ) == 0u );
    const geometry_brush_value_t *pValue = nullptr;
    REQUIRE( GeometrySnapshot_TryFindBrush( snapshot.p, geometry_source_id_t{ 1u }, &pValue ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

//==========================================================================
// Queries
//==========================================================================

TEST_CASE( "brush and owner lookups resolve roots and sides",
           "[editor][geometry][document]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    value_ref_t box{};
    MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );
    const geometry_source_id_t brushId = box.p->brush.sourceId;
    const geometry_source_id_t sideId = box.p->brush.sides.pData[3].sourceId;
    REQUIRE( ApplyOne( pDocument, INSERT, brushId, box.p ) == geometry_status_t::OK );

    geometry_brush_handle_t handle{};
    REQUIRE( GeometryDocument_TryFindBrush( pDocument, brushId, &handle ) == geometry_status_t::OK );
    REQUIRE( GeometryHandle_IsValid( handle ) );
    REQUIRE( GeometryDocument_TryFindBrush( pDocument, sideId, &handle ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( GeometryHandle_IsValid( handle ) );

    geometry_source_id_t owner{};
    REQUIRE( GeometryDocument_TryFindOwner( pDocument, sideId, &owner ) == geometry_status_t::OK );
    REQUIRE( owner.value == brushId.value );
    REQUIRE( GeometryDocument_TryFindOwner( pDocument, brushId, &owner ) == geometry_status_t::OK );
    REQUIRE( owner.value == brushId.value );
    REQUIRE( GeometryDocument_TryFindOwner( pDocument, geometry_source_id_t{ 999u }, &owner ) ==
             geometry_status_t::INVALID_ARGUMENT );

    const geometry_brush_value_t *pValue = nullptr;
    REQUIRE( GeometryDocument_TryGetBrushById( pDocument, brushId, &pValue ) ==
             geometry_status_t::OK );
    REQUIRE( pValue == box.p );

    // A removed brush's handle goes stale.
    REQUIRE( GeometryDocument_TryFindBrush( pDocument, brushId, &handle ) == geometry_status_t::OK );
    REQUIRE( ApplyOne( pDocument, REMOVE, brushId, nullptr ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryGetBrush( pDocument, handle, &pValue ) ==
             geometry_status_t::STALE_HANDLE );
    REQUIRE( pValue == nullptr );
    REQUIRE( BrushValue_RefCount( box.p ) == 1u );
}

//==========================================================================
// Change-set validation: nothing publishes on failure
//==========================================================================

TEST_CASE( "stale revisions and malformed change sets publish nothing",
           "[editor][geometry][document][atomicity]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    value_ref_t box{};
    MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );
    const geometry_source_id_t brushId = box.p->brush.sourceId;
    REQUIRE( ApplyOne( pDocument, INSERT, brushId, box.p ) == geometry_status_t::OK );

    value_ref_t other{};
    MakeBoxValue( pDocument, Vec3d_Make( 4.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &other );
    const geometry_source_id_t otherId = other.p->brush.sourceId;
    const document_state_t before = Capture( *pDocument );

    SECTION( "stale revision" ) {
        const geometry_document_change_t change{ INSERT, otherId, other.p };
        REQUIRE( GeometryDocument_TryApply(
                     pDocument, 1u,
                     common::span_t<const geometry_document_change_t>{ &change, 1u }, nullptr ) ==
                 geometry_status_t::STALE_REVISION );
    }
    SECTION( "empty change set" ) {
        REQUIRE( GeometryDocument_TryApply(
                     pDocument, 2u, common::span_t<const geometry_document_change_t>{}, nullptr ) ==
                 geometry_status_t::INVALID_ARGUMENT );
    }
    SECTION( "invalid kind" ) {
        REQUIRE( ApplyOne( pDocument, geometry_document_change_kind_t::INVALID, otherId, other.p ) ==
                 geometry_status_t::INVALID_ARGUMENT );
    }
    SECTION( "remove carrying a value" ) {
        REQUIRE( ApplyOne( pDocument, REMOVE, brushId, box.p ) ==
                 geometry_status_t::INVALID_ARGUMENT );
    }
    SECTION( "insert without a value" ) {
        REQUIRE( ApplyOne( pDocument, INSERT, otherId, nullptr ) ==
                 geometry_status_t::INVALID_ARGUMENT );
    }
    SECTION( "value whose brush ID differs from the target" ) {
        REQUIRE( ApplyOne( pDocument, INSERT, geometry_source_id_t{ otherId.value + 1u }, other.p ) ==
                 geometry_status_t::INVALID_ARGUMENT );
    }
    SECTION( "same brush named twice" ) {
        const geometry_document_change_t changes[2] = {
            { INSERT, otherId, other.p },
            { REMOVE, otherId, nullptr },
        };
        REQUIRE( GeometryDocument_TryApply(
                     pDocument, 2u,
                     common::span_t<const geometry_document_change_t>{ changes, 2u }, nullptr ) ==
                 geometry_status_t::INVALID_ARGUMENT );
    }
    SECTION( "insert of an already committed brush" ) {
        REQUIRE( ApplyOne( pDocument, INSERT, brushId, box.p ) ==
                 geometry_status_t::IDENTITY_CONFLICT );
    }
    SECTION( "replace or remove of an uncommitted brush" ) {
        REQUIRE( ApplyOne( pDocument, REPLACE, otherId, other.p ) ==
                 geometry_status_t::INVALID_HANDLE );
        REQUIRE( ApplyOne( pDocument, REMOVE, otherId, nullptr ) ==
                 geometry_status_t::INVALID_HANDLE );
        // A side ID is not a brush root.
        REQUIRE( ApplyOne( pDocument, REMOVE, box.p->brush.sides.pData[0].sourceId, nullptr ) ==
                 geometry_status_t::INVALID_HANDLE );
    }
    SECTION( "a later failing change rolls back an earlier valid one" ) {
        const geometry_document_change_t changes[2] = {
            { INSERT, otherId, other.p },
            { REMOVE, geometry_source_id_t{ 999u }, nullptr },
        };
        REQUIRE( GeometryDocument_TryApply(
                     pDocument, 2u,
                     common::span_t<const geometry_document_change_t>{ changes, 2u }, nullptr ) ==
                 geometry_status_t::INVALID_HANDLE );
    }

    RequireUnchanged( *pDocument, before );
    REQUIRE( BrushValue_RefCount( other.p ) == 1u );
    REQUIRE( BrushValue_RefCount( box.p ) == 2u );
}

TEST_CASE( "values from another document are rejected",
           "[editor][geometry][document][identity]" ) {
    document_holder_t first{};
    document_holder_t second{};
    value_ref_t foreign{};
    MakeBoxValue( &second.document, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
                  &foreign );
    const document_state_t before = Capture( first.document );
    REQUIRE( ApplyOne( &first.document, INSERT, foreign.p->brush.sourceId, foreign.p ) ==
             geometry_status_t::INVALID_ARGUMENT );
    RequireUnchanged( first.document, before );
}

//==========================================================================
// Identity bookkeeping
//==========================================================================

TEST_CASE( "new values may only use identities this document issued",
           "[editor][geometry][document][identity]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    const document_state_t before = Capture( *pDocument );

    // IDs 500..506 were never allocated by this document.
    geometry_source_id_t ids[BRUSH_GENERATOR_BOX_ID_COUNT]{};
    for ( common::usize i = 0u; i < BRUSH_GENERATOR_BOX_ID_COUNT; ++i ) {
        ids[i] = geometry_source_id_t{ 500u + i };
    }
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeBoxWithIds(
                 &brush, common::Allocator_GetSystem(), pDocument->policy, ids,
                 Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    geometry_brush_side_attribute_store_t attributes{};
    REQUIRE( BrushSideAttributeStore_Init( &attributes, common::Allocator_GetSystem() ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppendDefaults( &attributes, pDocument->policy, 6u ) ==
             geometry_status_t::OK );
    value_ref_t value{};
    REQUIRE( GeometryDocument_TryCreateBrushValue( pDocument, &brush, &attributes, &value.p,
                                                   nullptr ) == geometry_status_t::OK );
    BrushSideAttributeStore_Shutdown( &attributes );
    BrushSolid_Shutdown( &brush );

    REQUIRE( ApplyOne( pDocument, INSERT, ids[0], value.p ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    RequireUnchanged( *pDocument, before );
}

TEST_CASE( "an identity owned by another brush cannot be reused",
           "[editor][geometry][document][identity]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    value_ref_t a{};
    MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &a );
    REQUIRE( ApplyOne( pDocument, INSERT, a.p->brush.sourceId, a.p ) == geometry_status_t::OK );

    // Build a second brush that steals a's +X side ID.
    value_ref_t b{};
    MakeBoxValue( pDocument, Vec3d_Make( 4.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &b );
    brush_solid_t thief{};
    REQUIRE( BrushSolid_Init( &thief, common::Allocator_GetSystem(), b.p->brush.sourceId ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_TryCopyFrom( &thief, &b.p->brush, pDocument->policy.limits ) ==
             geometry_status_t::OK );
    thief.sides.pData[0].sourceId = a.p->brush.sides.pData[0].sourceId;
    value_ref_t stolen{};
    REQUIRE( GeometryDocument_TryCreateBrushValue( pDocument, &thief, &b.p->attributes, &stolen.p,
                                                   nullptr ) == geometry_status_t::OK );
    BrushSolid_Shutdown( &thief );

    const document_state_t before = Capture( *pDocument );
    REQUIRE( ApplyOne( pDocument, INSERT, stolen.p->brush.sourceId, stolen.p ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    RequireUnchanged( *pDocument, before );

    // Two new values in one set that share an ID conflict too, even when
    // the brush that owns it is freed by the same set.
    const geometry_document_change_t duplicate[2] = {
        { INSERT, stolen.p->brush.sourceId, stolen.p },
        { REPLACE, a.p->brush.sourceId, a.p },
    };
    REQUIRE( GeometryDocument_TryApply(
                 pDocument, before.revision,
                 common::span_t<const geometry_document_change_t>{ duplicate, 2u }, nullptr ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    RequireUnchanged( *pDocument, before );

    // The untampered second box inserts cleanly alongside a no-op replace.
    const geometry_document_change_t valid[2] = {
        { INSERT, b.p->brush.sourceId, b.p },
        { REPLACE, a.p->brush.sourceId, a.p },
    };
    REQUIRE( GeometryDocument_TryApply(
                 pDocument, before.revision,
                 common::span_t<const geometry_document_change_t>{ valid, 2u }, nullptr ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );
}

TEST_CASE( "a side identity may move between brushes freed in the same set",
           "[editor][geometry][document][identity]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    value_ref_t a{};
    value_ref_t b{};
    MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &a );
    MakeBoxValue( pDocument, Vec3d_Make( 4.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &b );
    const geometry_document_change_t inserts[2] = {
        { INSERT, a.p->brush.sourceId, a.p },
        { INSERT, b.p->brush.sourceId, b.p },
    };
    REQUIRE( GeometryDocument_TryApply(
                 pDocument, 1u,
                 common::span_t<const geometry_document_change_t>{ inserts, 2u }, nullptr ) ==
             geometry_status_t::OK );

    // Swap the +X side IDs of a and b in a single change set.
    const geometry_source_id_t aSide = a.p->brush.sides.pData[0].sourceId;
    const geometry_source_id_t bSide = b.p->brush.sides.pData[0].sourceId;
    auto makeSwapped = [&]( const geometry_brush_value_t *pSource, geometry_source_id_t newId,
                            value_ref_t *pOut ) {
        brush_solid_t brush{};
        REQUIRE( BrushSolid_Init( &brush, common::Allocator_GetSystem(), pSource->brush.sourceId ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushSolid_TryCopyFrom( &brush, &pSource->brush, pDocument->policy.limits ) ==
                 geometry_status_t::OK );
        brush.sides.pData[0].sourceId = newId;
        REQUIRE( GeometryDocument_TryCreateBrushValue( pDocument, &brush, &pSource->attributes,
                                                       &pOut->p, nullptr ) ==
                 geometry_status_t::OK );
        BrushSolid_Shutdown( &brush );
    };
    value_ref_t a2{};
    value_ref_t b2{};
    makeSwapped( a.p, bSide, &a2 );
    makeSwapped( b.p, aSide, &b2 );

    // Replacing only one side of the swap conflicts with the other brush.
    REQUIRE( ApplyOne( pDocument, REPLACE, a.p->brush.sourceId, a2.p ) ==
             geometry_status_t::IDENTITY_CONFLICT );

    const geometry_document_change_t swap[2] = {
        { REPLACE, a.p->brush.sourceId, a2.p },
        { REPLACE, b.p->brush.sourceId, b2.p },
    };
    REQUIRE( GeometryDocument_TryApply(
                 pDocument, 2u,
                 common::span_t<const geometry_document_change_t>{ swap, 2u }, nullptr ) ==
             geometry_status_t::OK );
    geometry_source_id_t owner{};
    REQUIRE( GeometryDocument_TryFindOwner( pDocument, aSide, &owner ) == geometry_status_t::OK );
    REQUIRE( owner.value == b.p->brush.sourceId.value );
    REQUIRE( GeometryDocument_TryFindOwner( pDocument, bSide, &owner ) == geometry_status_t::OK );
    REQUIRE( owner.value == a.p->brush.sourceId.value );
    REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );
}

TEST_CASE( "remove retires identities and re-insert restores them",
           "[editor][geometry][document][identity]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    value_ref_t box{};
    MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );
    const geometry_source_id_t brushId = box.p->brush.sourceId;
    REQUIRE( ApplyOne( pDocument, INSERT, brushId, box.p ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Count( &pDocument->registry ) == 7u );

    geometry_brush_handle_t firstHandle{};
    REQUIRE( GeometryDocument_TryFindBrush( pDocument, brushId, &firstHandle ) ==
             geometry_status_t::OK );

    REQUIRE( ApplyOne( pDocument, REMOVE, brushId, nullptr ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( pDocument ) == 0u );
    REQUIRE( pDocument->cSides == 0u );
    REQUIRE( GeometrySourceIdRegistry_Count( &pDocument->registry ) == 0u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &pDocument->registry ) == 7u );
    REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );

    // Undo of the removal: same value, same identities, new handle.
    REQUIRE( ApplyOne( pDocument, INSERT, brushId, box.p ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Count( &pDocument->registry ) == 7u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &pDocument->registry ) == 7u );
    geometry_brush_handle_t secondHandle{};
    REQUIRE( GeometryDocument_TryFindBrush( pDocument, brushId, &secondHandle ) ==
             geometry_status_t::OK );
    REQUIRE( ( secondHandle.nSlot != firstHandle.nSlot ||
               secondHandle.nGeneration != firstHandle.nGeneration ) );
    REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );
}

TEST_CASE( "a replace that drops a side retires only that side's identity",
           "[editor][geometry][document][identity]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;

    // Box plus a chamfer side cutting the (+,+,+) corner.
    value_ref_t box{};
    MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );
    geometry_source_id_t chamferId{};
    REQUIRE( GeometryDocument_TryAllocateSourceIds(
                 pDocument, common::span_t<geometry_source_id_t>{ &chamferId, 1u } ) ==
             geometry_status_t::OK );

    brush_solid_t brush{};
    REQUIRE( BrushSolid_Init( &brush, common::Allocator_GetSystem(), box.p->brush.sourceId ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_TryCopyFrom( &brush, &box.p->brush, pDocument->policy.limits ) ==
             geometry_status_t::OK );
    brush_solid_side_t chamfer{};
    const double k = 1.0 / std::sqrt( 3.0 );
    chamfer.plane = cypher::math::Planed_Make( Vec3d_Make( k, k, k ), -2.5 * k );
    chamfer.sourceId = chamferId;
    chamfer.iAttributeIndex = 6u;
    REQUIRE( BrushSolid_TryAddSide( &brush, pDocument->policy.limits, chamfer, nullptr ) ==
             geometry_status_t::OK );
    geometry_brush_side_attribute_store_t attributes{};
    REQUIRE( BrushSideAttributeStore_Init( &attributes, common::Allocator_GetSystem() ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppendDefaults( &attributes, pDocument->policy, 7u ) ==
             geometry_status_t::OK );
    value_ref_t chamfered{};
    REQUIRE( GeometryDocument_TryCreateBrushValue( pDocument, &brush, &attributes, &chamfered.p,
                                                   nullptr ) == geometry_status_t::OK );
    BrushSideAttributeStore_Shutdown( &attributes );
    BrushSolid_Shutdown( &brush );
    REQUIRE( BrushBoundary_VertexCount( &chamfered.p->boundary ) == 10u );

    const geometry_source_id_t brushId = box.p->brush.sourceId;
    REQUIRE( ApplyOne( pDocument, INSERT, brushId, chamfered.p ) == geometry_status_t::OK );
    REQUIRE( pDocument->cSides == 7u );

    // Replace with the plain box: the chamfer side disappears.
    REQUIRE( ApplyOne( pDocument, REPLACE, brushId, box.p ) == geometry_status_t::OK );
    REQUIRE( pDocument->cSides == 6u );
    REQUIRE_FALSE( GeometrySourceIdRegistry_Contains( &pDocument->registry, chamferId ) );
    REQUIRE( GeometrySourceIdRegistry_IsClaimed( &pDocument->registry, chamferId ) );
    geometry_source_id_t owner{};
    REQUIRE( GeometryDocument_TryFindOwner( pDocument, chamferId, &owner ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );

    // And back again (redo): the retired chamfer identity is restored.
    REQUIRE( ApplyOne( pDocument, REPLACE, brushId, chamfered.p ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Contains( &pDocument->registry, chamferId ) );
    REQUIRE( GeometryDocument_TryFindOwner( pDocument, chamferId, &owner ) == geometry_status_t::OK );
    REQUIRE( owner.value == brushId.value );
    REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );
}

TEST_CASE( "pending identities can be released but owned ones cannot",
           "[editor][geometry][document][identity]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    value_ref_t box{};
    MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );
    REQUIRE( ApplyOne( pDocument, INSERT, box.p->brush.sourceId, box.p ) == geometry_status_t::OK );

    geometry_source_id_t pending[2]{};
    REQUIRE( GeometryDocument_TryAllocateSourceIds(
                 pDocument, common::span_t<geometry_source_id_t>{ pending, 2u } ) ==
             geometry_status_t::OK );
    REQUIRE( pending[0].value == 8u );
    REQUIRE( pending[1].value == 9u );

    REQUIRE( GeometryDocument_ReleasePendingId( pDocument, box.p->brush.sourceId ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    REQUIRE( GeometryDocument_ReleasePendingId( pDocument, pending[0] ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_ReleasePendingId( pDocument, pending[0] ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometrySourceIdRegistry_Count( &pDocument->registry ) == 8u );
    REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );

    // Zero-length allocation is a no-op.
    REQUIRE( GeometryDocument_TryAllocateSourceIds(
                 pDocument, common::span_t<geometry_source_id_t>{} ) == geometry_status_t::OK );
}

TEST_CASE( "source ID exhaustion is reported before any ID is claimed",
           "[editor][geometry][document][identity]" ) {
    geometry_document_t document{};
    geometry_document_desc_t desc{};
    desc.pAllocator = common::Allocator_GetSystem();
    desc.firstSourceId = geometry_source_id_t{ common::CY_U64_MAX - 1u };
    REQUIRE( GeometryDocument_Init( &document, desc ) == geometry_status_t::OK );

    geometry_source_id_t ids[3]{};
    REQUIRE( GeometryDocument_TryAllocateSourceIds(
                 &document, common::span_t<geometry_source_id_t>{ ids, 3u } ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &document.registry ) == 0u );
    REQUIRE( ids[0].value == 0u );
    REQUIRE( GeometryDocument_TryAllocateSourceIds(
                 &document, common::span_t<geometry_source_id_t>{ ids, 2u } ) ==
             geometry_status_t::OK );
    REQUIRE( ids[1].value == common::CY_U64_MAX );
    GeometryDocument_Shutdown( &document );
}

//==========================================================================
// Values
//==========================================================================

TEST_CASE( "invalid brush values are rejected before publication",
           "[editor][geometry][document][validation]" ) {
    document_holder_t holder{};
    geometry_document_t *pDocument = &holder.document;
    value_ref_t box{};
    MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );

    SECTION( "empty intersection" ) {
        value_ref_t bad{};
        REQUIRE( MakeEditedValue( pDocument, box.p, 0u, PlusXPlaneAt( -2.0 ), &bad ) ==
                 geometry_status_t::DEGENERATE );
        REQUIRE( bad.p == nullptr );
    }
    SECTION( "attribute store with the wrong record count" ) {
        geometry_brush_side_attribute_store_t attributes{};
        REQUIRE( BrushSideAttributeStore_Init( &attributes, common::Allocator_GetSystem() ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushSideAttributeStore_TryAppendDefaults( &attributes, pDocument->policy, 5u ) ==
                 geometry_status_t::OK );
        value_ref_t bad{};
        REQUIRE( GeometryDocument_TryCreateBrushValue( pDocument, &box.p->brush, &attributes,
                                                       &bad.p, nullptr ) ==
                 geometry_status_t::INVALID_ARGUMENT );
        REQUIRE( bad.p == nullptr );
        BrushSideAttributeStore_Shutdown( &attributes );
    }
    SECTION( "two sides bound to one attribute record" ) {
        brush_solid_t brush{};
        REQUIRE( BrushSolid_Init( &brush, common::Allocator_GetSystem(), box.p->brush.sourceId ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushSolid_TryCopyFrom( &brush, &box.p->brush, pDocument->policy.limits ) ==
                 geometry_status_t::OK );
        brush.sides.pData[5].iAttributeIndex = 0u;
        REQUIRE( BrushValue_ValidateAttributeBinding( &brush, &box.p->attributes ) ==
                 geometry_status_t::INVALID_ARGUMENT );
        value_ref_t bad{};
        REQUIRE( GeometryDocument_TryCreateBrushValue( pDocument, &brush, &box.p->attributes,
                                                       &bad.p, nullptr ) ==
                 geometry_status_t::INVALID_ARGUMENT );
        BrushSolid_Shutdown( &brush );
    }
    SECTION( "validation result is reported" ) {
        brush_validation_result_t validation{};
        value_ref_t good{};
        REQUIRE( GeometryDocument_TryCreateBrushValue( pDocument, &box.p->brush, &box.p->attributes,
                                                       &good.p, &validation ) ==
                 geometry_status_t::OK );
        REQUIRE( validation.bWatertight );
        REQUIRE( validation.cVertices == 8u );
        REQUIRE( validation.cEdges == 12u );
        REQUIRE( validation.cFaces == 6u );
    }
}

TEST_CASE( "value creation is leak-free under every allocation failure",
           "[editor][geometry][document][atomicity]" ) {
    document_holder_t source{};
    value_ref_t box{};
    MakeBoxValue( &source.document, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
                  &box );

    bool bSucceeded = false;
    common::usize cInjected = 0u;
    for ( common::usize iFailure = 0u; iFailure < 64u && !bSucceeded; ++iFailure ) {
        failing_allocator_state_t state{};
        document_holder_t holder{ MakeFailingAllocator( &state ) };
        state.iFailure = state.cAllocationCalls + iFailure;
        value_ref_t value{};
        const geometry_status_t status = GeometryDocument_TryCreateBrushValue(
            &holder.document, &box.p->brush, &box.p->attributes, &value.p, nullptr );
        if ( status == geometry_status_t::OK ) {
            bSucceeded = true;
            REQUIRE( value.p != nullptr );
            REQUIRE( BrushBoundary_VertexCount( &value.p->boundary ) == 8u );
        } else {
            ++cInjected;
            REQUIRE( status == geometry_status_t::ALLOCATION_FAILED );
            REQUIRE( value.p == nullptr );
        }
        // Value must be released before the holder's allocator state dies.
        BrushValue_Release( value.p );
        value.p = nullptr;
    }
    REQUIRE( bSucceeded );
    REQUIRE( cInjected > 0u );
}

//==========================================================================
// Allocation failure and limits during apply
//==========================================================================

TEST_CASE( "apply under every allocation failure publishes all or nothing",
           "[editor][geometry][document][atomicity]" ) {
    bool bSucceeded = false;
    common::usize cInjected = 0u;
    for ( common::usize iFailure = 0u; iFailure < 128u && !bSucceeded; ++iFailure ) {
        failing_allocator_state_t state{};
        document_holder_t holder{ MakeFailingAllocator( &state ) };
        geometry_document_t *pDocument = &holder.document;

        value_ref_t a{};
        value_ref_t b{};
        MakeBoxValue( pDocument, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &a );
        MakeBoxValue( pDocument, Vec3d_Make( 4.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &b );
        REQUIRE( ApplyOne( pDocument, INSERT, a.p->brush.sourceId, a.p ) == geometry_status_t::OK );
        value_ref_t a2{};
        REQUIRE( MakeEditedValue( pDocument, a.p, 0u, PlusXPlaneAt( 2.0 ), &a2 ) ==
                 geometry_status_t::OK );

        snapshot_ref_t snapshot{};
        REQUIRE( GeometryDocument_TryAcquireSnapshot( pDocument, &snapshot.p ) ==
                 geometry_status_t::OK );
        const document_state_t before = Capture( *pDocument );

        const geometry_document_change_t changes[2] = {
            { REPLACE, a.p->brush.sourceId, a2.p },
            { INSERT, b.p->brush.sourceId, b.p },
        };
        state.iFailure = state.cAllocationCalls + iFailure;
        const geometry_status_t status = GeometryDocument_TryApply(
            pDocument, before.revision,
            common::span_t<const geometry_document_change_t>{ changes, 2u }, nullptr );
        state.iFailure = common::CY_USIZE_MAX;

        if ( status == geometry_status_t::OK ) {
            bSucceeded = true;
            REQUIRE( GeometryDocument_Revision( pDocument ) == before.revision + 1u );
            REQUIRE( GeometryDocument_BrushCount( pDocument ) == 2u );
            REQUIRE( GeometryDocument_ValidateDeep( pDocument ) );
        } else {
            ++cInjected;
            REQUIRE( status == geometry_status_t::ALLOCATION_FAILED );
            RequireUnchanged( *pDocument, before );
            const geometry_brush_value_t *pLive = nullptr;
            REQUIRE( GeometryDocument_TryGetBrushById( pDocument, a.p->brush.sourceId, &pLive ) ==
                     geometry_status_t::OK );
            REQUIRE( pLive == a.p );
            // The cached snapshot survived the failed apply.
            snapshot_ref_t again{};
            REQUIRE( GeometryDocument_TryAcquireSnapshot( pDocument, &again.p ) ==
                     geometry_status_t::OK );
            REQUIRE( again.p == snapshot.p );
        }
    }
    REQUIRE( bSucceeded );
    REQUIRE( cInjected > 0u );
}

TEST_CASE( "brush and side limits are enforced atomically",
           "[editor][geometry][document][limits]" ) {
    SECTION( "brush count" ) {
        geometry_policy_t policy{};
        policy.limits.cBrushesMax = 1u;
        REQUIRE( GeometryPolicy_IsValid( policy ) );
        document_holder_t holder{ policy };
        value_ref_t a{};
        value_ref_t b{};
        MakeBoxValue( &holder.document, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &a );
        MakeBoxValue( &holder.document, Vec3d_Make( 4.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &b );
        REQUIRE( ApplyOne( &holder.document, INSERT, a.p->brush.sourceId, a.p ) ==
                 geometry_status_t::OK );
        const document_state_t before = Capture( holder.document );
        REQUIRE( ApplyOne( &holder.document, INSERT, b.p->brush.sourceId, b.p ) ==
                 geometry_status_t::LIMIT_EXCEEDED );
        RequireUnchanged( holder.document, before );

        // Remove-then-insert in one set stays within the limit.
        const geometry_document_change_t swap[2] = {
            { REMOVE, a.p->brush.sourceId, nullptr },
            { INSERT, b.p->brush.sourceId, b.p },
        };
        REQUIRE( GeometryDocument_TryApply(
                     &holder.document, before.revision,
                     common::span_t<const geometry_document_change_t>{ swap, 2u }, nullptr ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryDocument_BrushCount( &holder.document ) == 1u );
        REQUIRE( GeometryDocument_ValidateDeep( &holder.document ) );
    }
    SECTION( "total sides" ) {
        geometry_policy_t policy{};
        policy.limits.cBrushesMax = 2u;
        policy.limits.cBrushSidesPerBrushMax = 8u;
        policy.limits.cBrushSidesMax = 10u;
        REQUIRE( GeometryPolicy_IsValid( policy ) );
        document_holder_t holder{ policy };
        value_ref_t a{};
        value_ref_t b{};
        MakeBoxValue( &holder.document, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &a );
        MakeBoxValue( &holder.document, Vec3d_Make( 4.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &b );
        REQUIRE( ApplyOne( &holder.document, INSERT, a.p->brush.sourceId, a.p ) ==
                 geometry_status_t::OK );
        const document_state_t before = Capture( holder.document );
        REQUIRE( ApplyOne( &holder.document, INSERT, b.p->brush.sourceId, b.p ) ==
                 geometry_status_t::LIMIT_EXCEEDED );
        RequireUnchanged( holder.document, before );
    }
}

TEST_CASE( "a brush replaced by its own value keeps a single reference",
           "[editor][geometry][document]" ) {
    document_holder_t holder{};
    value_ref_t box{};
    MakeBoxValue( &holder.document, Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ), &box );
    REQUIRE( ApplyOne( &holder.document, INSERT, box.p->brush.sourceId, box.p ) ==
             geometry_status_t::OK );
    REQUIRE( ApplyOne( &holder.document, REPLACE, box.p->brush.sourceId, box.p ) ==
             geometry_status_t::OK );
    REQUIRE( BrushValue_RefCount( box.p ) == 2u );
    REQUIRE( GeometryDocument_Revision( &holder.document ) == 3u );
    REQUIRE( GeometryDocument_ValidateDeep( &holder.document ) );
}

} // namespace cypher::editor::geometry
