//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSource_Tests.cpp
//  Purpose: Verifies authored brush and surface-store ownership as one unit.
//  Details: Covers legacy default migration, shared/unused surface records,
//           dangling binding rejection, independent cloning, and exhaustive
//           destination-allocation failure unwind.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushSource.h"

#include <catch2/catch_test_macros.hpp>

namespace cypher::editor::geometry {

namespace {

geometry_brush_side_attributes_t Material( common::u64 value ) noexcept
{
    geometry_brush_side_attributes_t result =
        BrushSideAttributes_MakeDefault();
    result.material.value = value;
    return result;
}

struct BrushFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    brush_solid_t brush{};
    geometry_brush_side_attribute_store_t attributes{};

    BrushFixture()
    {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &ids,
                     math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                     math::Vec3d_Make( 1.0, 2.0, 3.0 ) ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushSideAttributeStore_Init(
                     &attributes, &allocator ) == geometry_status_t::OK );

        // Record zero is deliberately unused. Every side aliases record one,
        // proving that the ownership boundary does not assume one record per
        // side or an index equal to the side ordinal.
        REQUIRE( BrushSideAttributeStore_TryAppend(
                     &attributes, policy, Material( 11u ), nullptr ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushSideAttributeStore_TryAppend(
                     &attributes, policy, Material( 22u ), nullptr ) ==
                 geometry_status_t::OK );
        for ( common::usize iSide = 0u;
              iSide < brush.sides.nCount;
              ++iSide ) {
            brush.sides.pData[iSide].iAttributeIndex = 1u;
        }
    }

    ~BrushFixture()
    {
        BrushSideAttributeStore_Shutdown( &attributes );
        BrushSolid_Shutdown( &brush );
    }
};

struct failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFreeCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *FailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUserData );
    const common::usize iCall = pState->cAllocationCalls++;
    if ( iCall == pState->iFailure ) {
        return nullptr;
    }
    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
    }
    return pMemory;
}

void FailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFreeCalls;
    }
    common::Allocator_Free(
        common::Allocator_GetSystem(),
        pMemory, cbSize, nAlignment );
}

common::allocator_t FailureAllocator(
    failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        FailureAllocate, nullptr, FailureFree, pState
    };
}

} // namespace

TEST_CASE( "Brush source default build owns a usable migrated surface",
           "[editor][geometry][brushsource]" )
{
    BrushFixture fixture;
    brush_source_t source{};

    REQUIRE_FALSE( BrushSource_IsInitialized( &source ) );
    CHECK( BrushSource_Validate( &source, fixture.policy ).fault ==
           brush_source_fault_t::NOT_INITIALIZED );

    brush_source_t partial{};
    REQUIRE( BrushSolid_Init(
                 &partial.solid, &fixture.allocator,
                 geometry_source_id_t{ 9000u } ) == geometry_status_t::OK );
    const brush_source_validation_t partialValidation =
        BrushSource_Validate( &partial, fixture.policy );
    CHECK( partialValidation.fault ==
           brush_source_fault_t::INVALID_STORAGE );
    CHECK( partialValidation.status == geometry_status_t::CORRUPT_STATE );
    BrushSource_Shutdown( &partial );

    REQUIRE( BrushSource_TryBuildDefault(
                 &fixture.brush, &fixture.allocator,
                 fixture.policy, &source ) == geometry_status_t::OK );
    REQUIRE( BrushSource_IsInitialized( &source ) );
    CHECK( BrushSource_Validate( &source, fixture.policy ).fault ==
           brush_source_fault_t::NONE );
    REQUIRE( source.solid.sides.nCount == fixture.brush.sides.nCount );
    REQUIRE( BrushSideAttributeStore_Count( &source.attributes ) ==
             source.solid.sides.nCount );

    for ( common::usize iSide = 0u;
          iSide < source.solid.sides.nCount;
          ++iSide ) {
        CHECK( source.solid.sides.pData[iSide].iAttributeIndex == iSide );
        // Migration must not rewrite the caller's legacy solid.
        CHECK( fixture.brush.sides.pData[iSide].iAttributeIndex == 1u );

        geometry_brush_side_attributes_t migrated{};
        REQUIRE( BrushSideAttributeStore_TryGet(
                     &source.attributes, iSide, &migrated ) ==
                 geometry_status_t::OK );
        CHECK_FALSE( GeometryMaterialRef_IsAssigned( migrated.material ) );
        CHECK( BrushSideAttributes_Validate(
                   fixture.policy.numerical, migrated ) ==
               geometry_status_t::OK );
    }

    CHECK( BrushSource_TryBuildDefault(
               &fixture.brush, &fixture.allocator,
               fixture.policy, &source ) ==
           geometry_status_t::ALREADY_INITIALIZED );

    BrushSource_Shutdown( &source );
    CHECK_FALSE( BrushSource_IsInitialized( &source ) );
    CHECK( BrushSource_Validate( &source, fixture.policy ).fault ==
           brush_source_fault_t::NOT_INITIALIZED );
    BrushSource_Shutdown( &source );
}

TEST_CASE( "Brush source preserves shared bindings and unused records",
           "[editor][geometry][brushsource]" )
{
    BrushFixture fixture;
    brush_source_t source{};

    REQUIRE( BrushSource_TryBuildWithStore(
                 &fixture.brush, &fixture.attributes,
                 &fixture.allocator, fixture.policy, &source ) ==
             geometry_status_t::OK );
    CHECK( BrushSource_Validate( &source, fixture.policy ).fault ==
           brush_source_fault_t::NONE );
    REQUIRE( BrushSideAttributeStore_Count( &source.attributes ) == 2u );

    for ( common::usize iSide = 0u;
          iSide < source.solid.sides.nCount;
          ++iSide ) {
        CHECK( source.solid.sides.pData[iSide].iAttributeIndex == 1u );
    }

    geometry_brush_side_attributes_t shared{};
    REQUIRE( BrushSideAttributeStore_TryGet(
                 &source.attributes, 1u, &shared ) == geometry_status_t::OK );
    CHECK( shared.material.value == 22u );

    BrushSource_Shutdown( &source );
}

TEST_CASE( "Brush source rejects and diagnoses dangling attribute bindings",
           "[editor][geometry][brushsource][validation]" )
{
    BrushFixture fixture;
    geometry_brush_side_attribute_store_t oneRecord{};
    REQUIRE( BrushSideAttributeStore_Init(
                 &oneRecord, &fixture.allocator ) == geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &oneRecord, fixture.policy,
                 Material( 90u ), nullptr ) == geometry_status_t::OK );

    brush_source_t rejected{};
    CHECK( BrushSource_TryBuildWithStore(
               &fixture.brush, &oneRecord, &fixture.allocator,
               fixture.policy, &rejected ) ==
           geometry_status_t::CORRUPT_STATE );
    CHECK_FALSE( BrushSource_IsInitialized( &rejected ) );

    brush_source_t source{};
    REQUIRE( BrushSource_TryBuildWithStore(
                 &fixture.brush, &fixture.attributes,
                 &fixture.allocator, fixture.policy, &source ) ==
             geometry_status_t::OK );
    source.solid.sides.pData[3u].iAttributeIndex = 500u;
    const brush_source_validation_t validation =
        BrushSource_Validate( &source, fixture.policy );
    CHECK( validation.fault ==
           brush_source_fault_t::DANGLING_ATTRIBUTE_INDEX );
    CHECK( validation.status == geometry_status_t::CORRUPT_STATE );
    CHECK( validation.iSide == 3u );

    BrushSource_Shutdown( &source );
    BrushSideAttributeStore_Shutdown( &oneRecord );
}

TEST_CASE( "Brush source clone is exact and storage-independent",
           "[editor][geometry][brushsource][clone]" )
{
    BrushFixture fixture;
    brush_source_t source{};
    brush_source_t clone{};
    REQUIRE( BrushSource_TryBuildWithStore(
                 &fixture.brush, &fixture.attributes,
                 &fixture.allocator, fixture.policy, &source ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSource_TryClone(
                 &source, &fixture.allocator,
                 fixture.policy, &clone ) == geometry_status_t::OK );

    REQUIRE( BrushSource_Equal( &source, &clone ) );
    CHECK( source.solid.sides.pData != clone.solid.sides.pData );
    CHECK( source.attributes.records.pData !=
           clone.attributes.records.pData );

    const math::planed_t clonePlane = clone.solid.sides.pData[0u].plane;
    math::planed_t changedPlane = source.solid.sides.pData[0u].plane;
    changedPlane.d -= 0.25;
    REQUIRE( BrushSolid_TrySetSidePlane(
                 &source.solid, 0u, changedPlane ) == geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TrySet(
                 &source.attributes, fixture.policy.numerical,
                 1u, Material( 1234u ) ) == geometry_status_t::OK );

    CHECK_FALSE( BrushSource_Equal( &source, &clone ) );
    CHECK( clone.solid.sides.pData[0u].plane.d == clonePlane.d );
    geometry_brush_side_attributes_t cloneAttributes{};
    REQUIRE( BrushSideAttributeStore_TryGet(
                 &clone.attributes, 1u, &cloneAttributes ) ==
             geometry_status_t::OK );
    CHECK( cloneAttributes.material.value == 22u );
    CHECK( BrushSource_Validate( &clone, fixture.policy ).fault ==
           brush_source_fault_t::NONE );

    BrushSource_Shutdown( &clone );
    BrushSource_Shutdown( &source );
}

TEST_CASE( "Brush source build and clone unwind every allocation failure",
           "[editor][geometry][brushsource][allocation]" )
{
    BrushFixture fixture;

    failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        FailureAllocator( &baselineState );
    brush_source_t baseline{};
    REQUIRE( BrushSource_TryBuildWithStore(
                 &fixture.brush, &fixture.attributes,
                 &baselineAllocator, fixture.policy, &baseline ) ==
             geometry_status_t::OK );
    const common::usize cBuildAllocations =
        baselineState.cAllocationCalls;
    REQUIRE( cBuildAllocations > 0u );

    SECTION( "build" ) {
        for ( common::usize iFailure = 0u;
              iFailure < cBuildAllocations;
              ++iFailure ) {
            CAPTURE( iFailure, cBuildAllocations );
            failure_allocator_state_t state{};
            state.iFailure = iFailure;
            common::allocator_t allocator = FailureAllocator( &state );
            brush_source_t output{};

            CHECK( BrushSource_TryBuildWithStore(
                       &fixture.brush, &fixture.attributes,
                       &allocator, fixture.policy, &output ) ==
                   geometry_status_t::ALLOCATION_FAILED );
            CHECK_FALSE( BrushSource_IsInitialized( &output ) );
            CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
            BrushSource_Shutdown( &output );
        }
    }

    SECTION( "clone" ) {
        for ( common::usize iFailure = 0u;
              iFailure < cBuildAllocations;
              ++iFailure ) {
            CAPTURE( iFailure, cBuildAllocations );
            failure_allocator_state_t state{};
            state.iFailure = iFailure;
            common::allocator_t allocator = FailureAllocator( &state );
            brush_source_t output{};

            CHECK( BrushSource_TryClone(
                       &baseline, &allocator,
                       fixture.policy, &output ) ==
                   geometry_status_t::ALLOCATION_FAILED );
            CHECK_FALSE( BrushSource_IsInitialized( &output ) );
            CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
            BrushSource_Shutdown( &output );
        }
    }

    BrushSource_Shutdown( &baseline );
    CHECK( baselineState.cSuccessfulAllocations ==
           baselineState.cFreeCalls );
}

} // namespace cypher::editor::geometry
