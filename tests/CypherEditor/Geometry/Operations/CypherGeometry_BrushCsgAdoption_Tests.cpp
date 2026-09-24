//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCsgAdoption_Tests.cpp
//  Purpose: Verifies exact CSG ancestry and document-ready surface transfer.
//  Details: Exercises a doorway subtraction whose output repeats subject-side
//           ancestry and whose two operand stores intentionally reuse slot 0
//           for different material and UV records.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushCsgAdoption.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_DocumentBrushReplacement.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace cypher::editor::geometry
{

namespace
{

void SetEverySideAttributeIndex(
    brush_solid_t *pBrush,
    common::u32 iAttribute )
{
    for ( common::usize i = 0u; i < pBrush->sides.nCount; ++i ) {
        pBrush->sides.pData[i].iAttributeIndex = iAttribute;
    }
}

geometry_brush_side_attributes_t MakeMaterial(
    common::u64 material )
{
    geometry_brush_side_attributes_t result =
        BrushSideAttributes_MakeDefault();
    result.material.value = material;
    result.uvProjection.offset.x = static_cast<common::f64>( material );
    return result;
}

struct adoption_failure_allocator_state_t {
    common::usize cCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cSuccessful{ 0u };
    common::usize cFrees{ 0u };
};

void *AdoptionFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<adoption_failure_allocator_state_t *>(
        pUserData );
    ++pState->cCalls;
    if ( pState->cCalls == pState->iFailOnCall ) {
        return nullptr;
    }
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    void *pMemory = pSystem->pfnAllocate(
        pSystem->pUserData, cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessful;
    }
    return pMemory;
}

void AdoptionFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<adoption_failure_allocator_state_t *>(
        pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFrees;
    }
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    pSystem->pfnFree(
        pSystem->pUserData, pMemory, cbSize, nAlignment );
}

common::allocator_t MakeAdoptionFailureAllocator(
    adoption_failure_allocator_state_t *pState ) noexcept
{
    return {
        &AdoptionFailureAllocate,
        nullptr,
        &AdoptionFailureFree,
        pState
    };
}

bool IsCanonicalAdoptionResult(
    const brush_csg_adoption_result_t &result ) noexcept
{
    return result.pFragments == nullptr &&
           result.pAttributeStores == nullptr &&
           result.cFragments == 0u &&
           result.cCapacity == 0u &&
           result.sideProvenance.pData == nullptr &&
           result.sideProvenance.nCount == 0u &&
           result.sideProvenance.nCapacity == 0u &&
           result.sideProvenance.pAllocator == nullptr &&
           result.pAllocator == nullptr;
}

} // namespace

TEST_CASE(
    "CSG adoption assigns unique identities and transfers operand-qualified surfaces",
    "[CSG][CSGAdoption][Attributes][Provenance]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};

    brush_solid_t wall{};
    brush_solid_t doorway{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &wall,
                 &allocator,
                 policy,
                 &ids,
                 math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                 math::Vec3d_Make( 4.0, 1.0, 3.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushGenerator_TryMakeBox(
                 &doorway,
                 &allocator,
                 policy,
                 &ids,
                 math::Vec3d_Make( 0.0, 0.0, -1.0 ),
                 math::Vec3d_Make( 1.0, 2.0, 2.0 ) ) ==
             geometry_status_t::OK );

    geometry_brush_side_attribute_store_t wallAttributes{};
    geometry_brush_side_attribute_store_t doorwayAttributes{};
    REQUIRE( BrushSideAttributeStore_Init(
                 &wallAttributes, &allocator ) == geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_Init(
                 &doorwayAttributes, &allocator ) == geometry_status_t::OK );
    const geometry_brush_side_attributes_t wallSurface =
        MakeMaterial( 101u );
    const geometry_brush_side_attributes_t doorwaySurface =
        MakeMaterial( 202u );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &wallAttributes, policy, wallSurface, nullptr ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &doorwayAttributes, policy, doorwaySurface, nullptr ) ==
             geometry_status_t::OK );
    SetEverySideAttributeIndex( &wall, 0u );
    SetEverySideAttributeIndex( &doorway, 0u );

    brush_csg_subtract_result_t raw{};
    REQUIRE( BrushCSG_TrySubtract(
                 &wall,
                 &doorway,
                 &allocator,
                 &ids,
                 policy,
                 &raw ) == geometry_status_t::OK );
    REQUIRE( raw.cFragments > 1u );
    REQUIRE_FALSE( raw.sideProvenance.nCount == 0u );

    common::usize cRawSides = 0u;
    for ( common::usize i = 0u; i < raw.cFragments; ++i ) {
        cRawSides += raw.fragments[i].sides.nCount;
    }
    REQUIRE( raw.sideProvenance.nCount == cRawSides );

    bool bRawHasSubject = false;
    bool bRawHasCutter = false;
    for ( common::usize i = 0u;
          i < raw.sideProvenance.nCount;
          ++i ) {
        const brush_csg_raw_side_provenance_t &entry =
            raw.sideProvenance.pData[i];
        bRawHasSubject = bRawHasSubject ||
            entry.operand == brush_csg_operand_t::MINUEND_A;
        bRawHasCutter = bRawHasCutter ||
            entry.operand == brush_csg_operand_t::SUBTRAHEND_B;
    }
    REQUIRE( bRawHasSubject );
    REQUIRE( bRawHasCutter );

    const geometry_source_id_t firstDestinationId = ids.next;
    brush_csg_adoption_result_t adopted{};
    REQUIRE( BrushCsgAdoption_TryPrepareSubtraction(
                 &raw,
                 &wall,
                 &wallAttributes,
                 &doorway,
                 &doorwayAttributes,
                 &allocator,
                 &ids,
                 policy,
                 &adopted ) == geometry_status_t::OK );
    REQUIRE( adopted.cFragments == raw.cFragments );
    REQUIRE( adopted.sideProvenance.nCount == cRawSides );

    std::unordered_set<common::u64> destinationIds;
    std::unordered_map<common::u64, common::usize> subjectOccurrences;
    bool bTransferredWallSurface = false;
    bool bTransferredDoorwaySurface = false;

    for ( common::usize iFragment = 0u;
          iFragment < adopted.cFragments;
          ++iFragment ) {
        const brush_solid_t &fragment =
            adopted.pFragments[iFragment];
        const geometry_brush_side_attribute_store_t &fragmentAttributes =
            adopted.pAttributeStores[iFragment];
        REQUIRE( fragment.sourceId.value >=
                 firstDestinationId.value );
        REQUIRE( destinationIds.insert(
                     fragment.sourceId.value ).second );
        REQUIRE( BrushSideAttributeStore_Count(
                     &fragmentAttributes ) ==
                 fragment.sides.nCount );

        for ( common::usize iSide = 0u;
              iSide < fragment.sides.nCount;
              ++iSide ) {
            const brush_solid_side_t &side =
                fragment.sides.pData[iSide];
            REQUIRE( destinationIds.insert( side.sourceId.value ).second );
            REQUIRE( side.iAttributeIndex == iSide );

            const brush_csg_side_provenance_t *pProvenance =
                BrushCsgAdoption_FindSideProvenance(
                    &adopted, side.sourceId );
            REQUIRE( pProvenance != nullptr );
            REQUIRE( pProvenance->destinationBrushId.value ==
                     fragment.sourceId.value );

            geometry_brush_side_attributes_t attributes{};
            REQUIRE( BrushSideAttributeStore_TryGet(
                         &fragmentAttributes,
                         side.iAttributeIndex,
                         &attributes ) == geometry_status_t::OK );
            if ( pProvenance->operand ==
                 brush_csg_operand_t::MINUEND_A ) {
                REQUIRE( pProvenance->sourceBrushId.value ==
                         wall.sourceId.value );
                CHECK( attributes.material.value == 101u );
                CHECK( attributes.uvProjection.offset.x == 101.0 );
                ++subjectOccurrences[
                    pProvenance->sourceSideId.value];
                bTransferredWallSurface = true;
            }
            else {
                REQUIRE( pProvenance->sourceBrushId.value ==
                         doorway.sourceId.value );
                CHECK( attributes.material.value == 202u );
                CHECK( attributes.uvProjection.offset.x == 202.0 );
                bTransferredDoorwaySurface = true;
            }
        }
    }

    bool bOneToManyAncestry = false;
    for ( const auto &[sourceId, count] : subjectOccurrences ) {
        (void)sourceId;
        bOneToManyAncestry = bOneToManyAncestry || count > 1u;
    }
    CHECK( bTransferredWallSurface );
    CHECK( bTransferredDoorwaySurface );
    CHECK( bOneToManyAncestry );

    // The prepared brushes form one contiguous batch and can therefore enter
    // canonical document storage in one all-or-nothing publication. The
    // lower-level publication primitive intentionally leaves revision
    // ownership to the command/undo layer.
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init(
                 &document, &allocator, policy ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush(
                 &document, &wall ) == geometry_status_t::OK );
    const geometry_revision_t revisionBefore = document.revision;
    const geometry_source_id_t removed[]{ wall.sourceId };
    REQUIRE( GeometryDocument_TryReplaceBrushesExact(
                 &document,
                 common::span_t<const geometry_source_id_t>{ removed, 1u },
                 common::span_t<const brush_solid_t>{
                     adopted.pFragments, adopted.cFragments } ) ==
             geometry_status_t::OK );
    CHECK( GeometryDocument_BrushCount( &document ) ==
           adopted.cFragments );
    CHECK( GeometryDocument_FindBrush(
               &document, wall.sourceId ) == nullptr );
    CHECK( document.revision == revisionBefore );
    for ( common::usize i = 0u; i < adopted.cFragments; ++i ) {
        const brush_solid_t *pPublished = GeometryDocument_FindBrush(
            &document, adopted.pFragments[i].sourceId );
        REQUIRE( pPublished != nullptr );
        CHECK( pPublished->sides.nCount ==
               adopted.pFragments[i].sides.nCount );
    }

    GeometryDocument_Shutdown( &document );
    BrushCsgAdoptionResult_Shutdown( &adopted );
    BrushCSGSubtractResult_Shutdown( &raw );
    BrushSideAttributeStore_Shutdown( &doorwayAttributes );
    BrushSideAttributeStore_Shutdown( &wallAttributes );
    BrushSolid_Shutdown( &doorway );
    BrushSolid_Shutdown( &wall );
}

TEST_CASE(
    "CSG adoption is allocation and identity atomic",
    "[CSG][CSGAdoption][Allocation][Contract]" )
{
    common::allocator_t sourceAllocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t sourceIds{};
    brush_solid_t brushA{};
    brush_solid_t brushB{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brushA, &sourceAllocator, policy, &sourceIds,
                 math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                 math::Vec3d_Make( 4.0, 1.0, 3.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brushB, &sourceAllocator, policy, &sourceIds,
                 math::Vec3d_Make( 0.0, 0.0, -1.0 ),
                 math::Vec3d_Make( 1.0, 2.0, 2.0 ) ) ==
             geometry_status_t::OK );
    SetEverySideAttributeIndex( &brushA, 0u );
    SetEverySideAttributeIndex( &brushB, 0u );

    geometry_brush_side_attribute_store_t attributesA{};
    geometry_brush_side_attribute_store_t attributesB{};
    REQUIRE( BrushSideAttributeStore_Init(
                 &attributesA, &sourceAllocator ) == geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_Init(
                 &attributesB, &sourceAllocator ) == geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &attributesA, policy, MakeMaterial( 11u ), nullptr ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &attributesB, policy, MakeMaterial( 22u ), nullptr ) ==
             geometry_status_t::OK );

    brush_csg_subtract_result_t raw{};
    REQUIRE( BrushCSG_TrySubtract(
                 &brushA, &brushB, &sourceAllocator, &sourceIds,
                 policy, &raw ) == geometry_status_t::OK );

    adoption_failure_allocator_state_t baselineState{};
    baselineState.iFailOnCall = std::numeric_limits<common::usize>::max();
    common::allocator_t baselineAllocator =
        MakeAdoptionFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds = sourceIds;
    brush_csg_adoption_result_t baseline{};
    REQUIRE( BrushCsgAdoption_TryPrepareSubtraction(
                 &raw, &brushA, &attributesA, &brushB, &attributesB,
                 &baselineAllocator, &baselineIds, policy, &baseline ) ==
             geometry_status_t::OK );
    const common::usize cAllocationCalls = baselineState.cCalls;
    REQUIRE( cAllocationCalls > 0u );
    BrushCsgAdoptionResult_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessful == baselineState.cFrees );

    for ( common::usize iFail = 1u;
          iFail <= cAllocationCalls;
          ++iFail ) {
        DYNAMIC_SECTION( "allocation call " << iFail ) {
            adoption_failure_allocator_state_t state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator =
                MakeAdoptionFailureAllocator( &state );
            geometry_source_id_allocator_t ids = sourceIds;
            const geometry_source_id_t nextBefore = ids.next;
            brush_csg_adoption_result_t result{};

            REQUIRE( BrushCsgAdoption_TryPrepareSubtraction(
                         &raw,
                         &brushA,
                         &attributesA,
                         &brushB,
                         &attributesB,
                         &allocator,
                         &ids,
                         policy,
                         &result ) ==
                     geometry_status_t::ALLOCATION_FAILED );
            CHECK( IsCanonicalAdoptionResult( result ) );
            CHECK( ids.next.value == nextBefore.value );
            CHECK( state.cSuccessful == state.cFrees );
        }
    }

    BrushCSGSubtractResult_Shutdown( &raw );
    BrushSideAttributeStore_Shutdown( &attributesB );
    BrushSideAttributeStore_Shutdown( &attributesA );
    BrushSolid_Shutdown( &brushB );
    BrushSolid_Shutdown( &brushA );
}

TEST_CASE(
    "CSG adoption resolves hollow cut surfaces against the original brush",
    "[CSG][CSGAdoption][Hollow][Attributes][Provenance]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &ids,
                 math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                 math::Vec3d_Make( 4.0, 4.0, 4.0 ) ) ==
             geometry_status_t::OK );
    SetEverySideAttributeIndex( &brush, 0u );

    geometry_brush_side_attribute_store_t attributes{};
    REQUIRE( BrushSideAttributeStore_Init(
                 &attributes, &allocator ) == geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &attributes, policy, MakeMaterial( 303u ), nullptr ) ==
             geometry_status_t::OK );

    brush_csg_subtract_result_t raw{};
    REQUIRE( BrushCSG_TryHollow(
                 &brush, 1.0, &allocator, &ids, policy, &raw ) ==
             geometry_status_t::OK );

    bool bHasInteriorCutSurface = false;
    for ( common::usize i = 0u; i < raw.sideProvenance.nCount; ++i ) {
        const brush_csg_raw_side_provenance_t &provenance =
            raw.sideProvenance.pData[i];
        CHECK( provenance.sourceBrushId.value == brush.sourceId.value );
        bHasInteriorCutSurface = bHasInteriorCutSurface ||
            provenance.operand == brush_csg_operand_t::SUBTRAHEND_B;
    }
    REQUIRE( bHasInteriorCutSurface );

    brush_csg_adoption_result_t adopted{};
    REQUIRE( BrushCsgAdoption_TryPrepareSubtraction(
                 &raw,
                 &brush,
                 &attributes,
                 &brush,
                 &attributes,
                 &allocator,
                 &ids,
                 policy,
                 &adopted ) == geometry_status_t::OK );
    REQUIRE( adopted.cFragments == raw.cFragments );

    for ( common::usize iFragment = 0u;
          iFragment < adopted.cFragments;
          ++iFragment ) {
        const brush_solid_t &fragment = adopted.pFragments[iFragment];
        for ( common::usize iSide = 0u;
              iSide < fragment.sides.nCount;
              ++iSide ) {
            geometry_brush_side_attributes_t surface{};
            REQUIRE( BrushSideAttributeStore_TryGet(
                         &adopted.pAttributeStores[iFragment],
                         fragment.sides.pData[iSide].iAttributeIndex,
                         &surface ) == geometry_status_t::OK );
            CHECK( surface.material.value == 303u );
            CHECK( surface.uvProjection.offset.x == 303.0 );
        }
    }

    BrushCsgAdoptionResult_Shutdown( &adopted );
    BrushCSGSubtractResult_Shutdown( &raw );
    BrushSideAttributeStore_Shutdown( &attributes );
    BrushSolid_Shutdown( &brush );
}

TEST_CASE(
    "CSG adoption rejects undefined raw side-origin values",
    "[CSG][CSGAdoption][Contract][Provenance]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    brush_solid_t brushA{};
    brush_solid_t brushB{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brushA, &allocator, policy, &ids,
                 math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                 math::Vec3d_Make( 4.0, 1.0, 3.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brushB, &allocator, policy, &ids,
                 math::Vec3d_Make( 0.0, 0.0, -1.0 ),
                 math::Vec3d_Make( 1.0, 2.0, 2.0 ) ) ==
             geometry_status_t::OK );
    SetEverySideAttributeIndex( &brushA, 0u );
    SetEverySideAttributeIndex( &brushB, 0u );

    geometry_brush_side_attribute_store_t attributesA{};
    geometry_brush_side_attribute_store_t attributesB{};
    REQUIRE( BrushSideAttributeStore_Init(
                 &attributesA, &allocator ) == geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_Init(
                 &attributesB, &allocator ) == geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &attributesA, policy, MakeMaterial( 11u ), nullptr ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &attributesB, policy, MakeMaterial( 22u ), nullptr ) ==
             geometry_status_t::OK );

    brush_csg_subtract_result_t raw{};
    REQUIRE( BrushCSG_TrySubtract(
                 &brushA, &brushB, &allocator, &ids,
                 policy, &raw ) == geometry_status_t::OK );

    brush_csg_raw_side_provenance_t *pCutterProvenance = nullptr;
    for ( common::usize i = 0u; i < raw.sideProvenance.nCount; ++i ) {
        if ( raw.sideProvenance.pData[i].operand ==
             brush_csg_operand_t::SUBTRAHEND_B ) {
            pCutterProvenance = &raw.sideProvenance.pData[i];
            break;
        }
    }
    REQUIRE( pCutterProvenance != nullptr );
    pCutterProvenance->origin =
        static_cast<brush_csg_side_origin_t>( 0xffu );

    const geometry_source_id_t nextBefore = ids.next;
    brush_csg_adoption_result_t adopted{};
    REQUIRE( BrushCsgAdoption_TryPrepareSubtraction(
                 &raw,
                 &brushA,
                 &attributesA,
                 &brushB,
                 &attributesB,
                 &allocator,
                 &ids,
                 policy,
                 &adopted ) == geometry_status_t::CORRUPT_STATE );
    CHECK( IsCanonicalAdoptionResult( adopted ) );
    CHECK( ids.next.value == nextBefore.value );

    BrushCsgAdoptionResult_Shutdown( &adopted );
    BrushCSGSubtractResult_Shutdown( &raw );
    BrushSideAttributeStore_Shutdown( &attributesB );
    BrushSideAttributeStore_Shutdown( &attributesA );
    BrushSolid_Shutdown( &brushB );
    BrushSolid_Shutdown( &brushA );
}

} // namespace cypher::editor::geometry
