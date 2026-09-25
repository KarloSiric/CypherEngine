//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSource.cpp
//  Purpose: Implements the authored BrushSolid plus surface-store boundary.
//  Details: Construction validates the complete input before allocating and
//           unwinds both owned members on every failure. This prevents a
//           document, snapshot, or transaction from retaining numeric side
//           indices after the records that give those indices meaning have
//           gone out of scope.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushSource.h"

#include "CypherGeometry_BrushValidation.h"

namespace cypher::editor::geometry
{

namespace
{

template <typename type_t>
bool VectorIsCanonicalEmpty( const common::vector_t<type_t> &vector ) noexcept
{
    return vector.pData == nullptr && vector.nCount == 0u &&
           vector.nCapacity == 0u && vector.pAllocator == nullptr;
}

bool SourceIsCanonicalEmpty( const brush_source_t &source ) noexcept
{
    return VectorIsCanonicalEmpty( source.solid.sides ) &&
           !GeometrySourceId_IsValid( source.solid.sourceId ) &&
           VectorIsCanonicalEmpty( source.attributes.records );
}

geometry_status_t ValidateDestination( const brush_source_t *pOut ) noexcept
{
    if ( pOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( SourceIsCanonicalEmpty( *pOut ) ) {
        return geometry_status_t::OK;
    }
    if ( pOut->solid.sides.pAllocator != nullptr ||
         pOut->attributes.records.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    return geometry_status_t::CORRUPT_STATE;
}

geometry_status_t ValidateBuildArguments(
    const brush_solid_t *pSolid,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    brush_source_t *pOut ) noexcept
{
    if ( pSolid == nullptr || !common::Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return ValidateDestination( pOut );
}

geometry_status_t ValidateBindings(
    const brush_solid_t *pSolid,
    const geometry_brush_side_attribute_store_t *pAttributes ) noexcept
{
    const common::usize cAttributes =
        BrushSideAttributeStore_Count( pAttributes );
    const common::usize cSides = BrushSolid_SideCount( pSolid );
    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        brush_solid_side_t side{};
        const geometry_status_t sideStatus =
            BrushSolid_TryGetSide( pSolid, iSide, &side );
        if ( sideStatus != geometry_status_t::OK ) {
            return sideStatus;
        }
        if ( static_cast<common::usize>( side.iAttributeIndex ) >=
             cAttributes ) {
            return geometry_status_t::CORRUPT_STATE;
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t TryCopySolid(
    const brush_solid_t *pSource,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits,
    brush_solid_t *pOut ) noexcept
{
    geometry_status_t status = BrushSolid_Init(
        pOut, pAllocator, pSource->sourceId );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const common::usize cSides = BrushSolid_SideCount( pSource );
    status = BrushSolid_TryReserve( pOut, limits, cSides );
    for ( common::usize iSide = 0u;
          iSide < cSides && status == geometry_status_t::OK;
          ++iSide ) {
        brush_solid_side_t side{};
        status = BrushSolid_TryGetSide( pSource, iSide, &side );
        if ( status == geometry_status_t::OK ) {
            status = BrushSolid_TryAddSide(
                pOut, limits, side, nullptr );
        }
    }

    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( pOut );
    }
    return status;
}

bool UvMappingEqual(
    const math::planar_uv_mappingd_t &a,
    const math::planar_uv_mappingd_t &b ) noexcept
{
    return math::Vec3d_EqualsExact( a.origin, b.origin ) &&
           math::Vec3d_EqualsExact( a.uAxis, b.uAxis ) &&
           math::Vec3d_EqualsExact( a.vAxis, b.vAxis ) &&
           math::Vec3d_EqualsExact( a.normal, b.normal ) &&
           math::Vec2d_EqualsExact(
               a.worldUnitsPerUv, b.worldUnitsPerUv ) &&
           a.rotationRadians == b.rotationRadians &&
           math::Vec2d_EqualsExact( a.offset, b.offset );
}

bool AttributesEqual(
    const geometry_brush_side_attributes_t &a,
    const geometry_brush_side_attributes_t &b ) noexcept
{
    return GeometryMaterialRef_Equals( a.material, b.material ) &&
           UvMappingEqual( a.uvProjection, b.uvProjection );
}

geometry_status_t FinishBuild(
    const brush_solid_t *pSolid,
    const geometry_brush_side_attribute_store_t *pAttributes,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    bool bUseDefaultAttributes,
    brush_source_t *pOut ) noexcept
{
    brush_source_t staged{};
    geometry_status_t status = BrushSideAttributeStore_Init(
        &staged.attributes, pAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    if ( bUseDefaultAttributes ) {
        const common::usize cSides = BrushSolid_SideCount( pSolid );
        status = BrushSideAttributeStore_TryReserve(
            &staged.attributes, policy.limits, cSides );
        const geometry_brush_side_attributes_t attributes =
            BrushSideAttributes_MakeDefault();
        for ( common::usize iSide = 0u;
              iSide < cSides && status == geometry_status_t::OK;
              ++iSide ) {
            status = BrushSideAttributeStore_TryAppend(
                &staged.attributes, policy, attributes, nullptr );
        }
    } else {
        status = BrushSideAttributeStore_TryCopyFrom(
            &staged.attributes, pAttributes, policy.limits );
    }

    if ( status == geometry_status_t::OK ) {
        status = TryCopySolid(
            pSolid, pAllocator, policy.limits, &staged.solid );
    }

    if ( status == geometry_status_t::OK && bUseDefaultAttributes ) {
        for ( common::usize iSide = 0u;
              iSide < staged.solid.sides.nCount;
              ++iSide ) {
            staged.solid.sides.pData[iSide].iAttributeIndex =
                static_cast<common::u32>( iSide );
        }
    }

    if ( status != geometry_status_t::OK ) {
        BrushSource_Shutdown( &staged );
        return status;
    }

    // Publication is allocation-free. Until both owned members are complete,
    // pOut remains its required canonical-empty value.
    common::Vector_Move( &pOut->solid.sides, &staged.solid.sides );
    pOut->solid.sourceId = staged.solid.sourceId;
    staged.solid.sourceId = GEOMETRY_SOURCE_ID_INVALID;
    common::Vector_Move(
        &pOut->attributes.records, &staged.attributes.records );
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t BrushSource_TryBuildDefault(
    const brush_solid_t *pSolid,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    brush_source_t *pOut ) noexcept
{
    const geometry_status_t argumentStatus =
        ValidateBuildArguments( pSolid, pAllocator, policy, pOut );
    if ( argumentStatus != geometry_status_t::OK ) {
        return argumentStatus;
    }

    const geometry_status_t solidStatus =
        BrushValidation_Quick( pSolid, policy );
    if ( solidStatus != geometry_status_t::OK ) {
        return solidStatus;
    }

    return FinishBuild(
        pSolid, nullptr, pAllocator, policy, true, pOut );
}

geometry_status_t BrushSource_TryBuildWithStore(
    const brush_solid_t *pSolid,
    const geometry_brush_side_attribute_store_t *pAttributes,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    brush_source_t *pOut ) noexcept
{
    const geometry_status_t argumentStatus =
        ValidateBuildArguments( pSolid, pAllocator, policy, pOut );
    if ( argumentStatus != geometry_status_t::OK ) {
        return argumentStatus;
    }
    if ( pAttributes == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const geometry_status_t solidStatus =
        BrushValidation_Quick( pSolid, policy );
    if ( solidStatus != geometry_status_t::OK ) {
        return solidStatus;
    }
    const geometry_status_t attributeStatus =
        BrushSideAttributeStore_Validate( pAttributes, policy );
    if ( attributeStatus != geometry_status_t::OK ) {
        return attributeStatus;
    }
    const geometry_status_t bindingStatus =
        ValidateBindings( pSolid, pAttributes );
    if ( bindingStatus != geometry_status_t::OK ) {
        return bindingStatus;
    }

    return FinishBuild(
        pSolid, pAttributes, pAllocator, policy, false, pOut );
}

geometry_status_t BrushSource_TryClone(
    const brush_source_t *pSource,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    brush_source_t *pOut ) noexcept
{
    if ( pSource == nullptr || pOut == pSource ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const brush_source_validation_t validation =
        BrushSource_Validate( pSource, policy );
    if ( validation.fault != brush_source_fault_t::NONE ) {
        return validation.status;
    }
    return BrushSource_TryBuildWithStore(
        &pSource->solid, &pSource->attributes,
        pAllocator, policy, pOut );
}

void BrushSource_Shutdown( brush_source_t *pSource ) noexcept
{
    if ( pSource == nullptr ) {
        return;
    }
    BrushSolid_Shutdown( &pSource->solid );
    BrushSideAttributeStore_Shutdown( &pSource->attributes );
}

bool BrushSource_IsInitialized( const brush_source_t *pSource ) noexcept
{
    if ( pSource == nullptr ||
         pSource->solid.sides.pAllocator == nullptr ||
         pSource->attributes.records.pAllocator == nullptr ||
         pSource->solid.sides.pAllocator !=
             pSource->attributes.records.pAllocator ) {
        return false;
    }
    return common::Allocator_IsValid(
               pSource->solid.sides.pAllocator ) &&
           common::Vector_IsValid( &pSource->solid.sides ) &&
           common::Vector_IsValid( &pSource->attributes.records );
}

brush_source_validation_t BrushSource_Validate(
    const brush_source_t *pSource,
    const geometry_policy_t &policy ) noexcept
{
    brush_source_validation_t result{};
    if ( pSource == nullptr || SourceIsCanonicalEmpty( *pSource ) ) {
        result.fault = brush_source_fault_t::NOT_INITIALIZED;
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !BrushSource_IsInitialized( pSource ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        result.fault = brush_source_fault_t::INVALID_STORAGE;
        result.status = GeometryPolicy_IsValid( policy )
            ? geometry_status_t::CORRUPT_STATE
            : geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    result.status = BrushValidation_Quick( &pSource->solid, policy );
    if ( result.status != geometry_status_t::OK ) {
        result.fault = brush_source_fault_t::INVALID_SOLID;
        return result;
    }

    result.status = BrushSideAttributeStore_Validate(
        &pSource->attributes, policy );
    if ( result.status != geometry_status_t::OK ) {
        result.fault = brush_source_fault_t::INVALID_ATTRIBUTES;
        return result;
    }

    const common::usize cAttributes =
        BrushSideAttributeStore_Count( &pSource->attributes );
    for ( common::usize iSide = 0u;
          iSide < pSource->solid.sides.nCount;
          ++iSide ) {
        if ( static_cast<common::usize>(
                 pSource->solid.sides.pData[iSide].iAttributeIndex ) >=
             cAttributes ) {
            result.fault =
                brush_source_fault_t::DANGLING_ATTRIBUTE_INDEX;
            result.status = geometry_status_t::CORRUPT_STATE;
            result.iSide = iSide;
            return result;
        }
    }

    return result;
}

bool BrushSource_Equal(
    const brush_source_t *pA,
    const brush_source_t *pB ) noexcept
{
    if ( !BrushSource_IsInitialized( pA ) ||
         !BrushSource_IsInitialized( pB ) ) {
        return false;
    }
    if ( pA->solid.sourceId.value != pB->solid.sourceId.value ||
         pA->solid.sides.nCount != pB->solid.sides.nCount ||
         pA->attributes.records.nCount != pB->attributes.records.nCount ) {
        return false;
    }

    for ( common::usize iSide = 0u;
          iSide < pA->solid.sides.nCount;
          ++iSide ) {
        const brush_solid_side_t &a = pA->solid.sides.pData[iSide];
        const brush_solid_side_t &b = pB->solid.sides.pData[iSide];
        if ( !math::Vec3d_EqualsExact( a.plane.normal, b.plane.normal ) ||
             a.plane.d != b.plane.d ||
             a.sourceId.value != b.sourceId.value ||
             a.iAttributeIndex != b.iAttributeIndex ) {
            return false;
        }
    }

    for ( common::usize iAttribute = 0u;
          iAttribute < pA->attributes.records.nCount;
          ++iAttribute ) {
        if ( !AttributesEqual(
                 pA->attributes.records.pData[iAttribute],
                 pB->attributes.records.pData[iAttribute] ) ) {
            return false;
        }
    }
    return true;
}

} // namespace cypher::editor::geometry
