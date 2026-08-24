//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ResourceSystem/CypherCommon_ResourceHandle.cpp
//  Purpose: Implements compact generation-checked runtime resource handles.
//  Details: Resource handles use a 16-bit slot, 32-bit generation, and 16-bit
//           runtime type. Zero generation and type values remain invalid.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ResourceHandle.h"
#include "CypherCommon_Assert.h"

namespace cypher::common
{

// The packed handle layout is part of the runtime ABI:
//
//   63                    48 47                    16 15             0
//  +-----------------------+------------------------+----------------+
//  |      type slot        |       generation       |      slot      |
//  +-----------------------+------------------------+----------------+
//
// Slot zero is valid. Type slot zero and generation zero are reserved so an
// all-zero handle can always represent "no resource".

bool_t ResourceHandle_TryMake(
    resource_slot_t iSlot,
    resource_generation_t nGeneration,
    resource_type_slot_t iTypeSlot,
    resource_handle_t *pHandleOut ) noexcept
{
    if ( pHandleOut == nullptr ) {
        return CY_FALSE;
    }

    *pHandleOut = CY_RESOURCE_HANDLE_INVALID;
    if ( iSlot > CY_RESOURCE_SLOT_MAX ||
         nGeneration == CY_RESOURCE_GENERATION_INVALID ) {
        return CY_FALSE;
    }
    if ( iTypeSlot == CY_RESOURCE_TYPE_SLOT_INVALID ||
         iTypeSlot > CY_RESOURCE_TYPE_SLOT_MAX ) {
        return CY_FALSE;
    }

    // Cast before shifting so each field is widened to the 64-bit packed domain.
    pHandleOut->value =
        ( static_cast<u64>( iTypeSlot ) << CY_RESOURCE_TYPE_SLOT_SHIFT ) |
        ( static_cast<u64>( nGeneration ) << CY_RESOURCE_GENERATION_SHIFT ) |
        static_cast<u64>( iSlot );
    return CY_TRUE;
}

resource_handle_t ResourceHandle_Make(
    resource_slot_t iSlot,
    resource_generation_t nGeneration,
    resource_type_slot_t iTypeSlot ) noexcept
{
    resource_handle_t handle = CY_RESOURCE_HANDLE_INVALID;

    const bool_t bCreated = ResourceHandle_TryMake( iSlot, nGeneration, iTypeSlot, &handle );
    CY_ASSERT_MSG( bCreated, "ResourceHandle_Make received an invalid slot, generation, or type slot."  );

    return handle;
}

bool_t ResourceHandle_IsValid( resource_handle_t handle ) noexcept
{
    if ( handle.value == 0u ) {
        return CY_FALSE;
    }

    const resource_generation_t nGeneration =
        ResourceHandle_Generation( handle );
    const resource_type_slot_t iTypeSlot =
        ResourceHandle_TypeSlot( handle );

    // The slot field is intentionally not checked; every 16-bit slot is encodable.
    return ( nGeneration != CY_RESOURCE_GENERATION_INVALID && iTypeSlot != CY_RESOURCE_TYPE_SLOT_INVALID );
}

bool_t ResourceHandle_Equals(
    resource_handle_t left,
    resource_handle_t right ) noexcept
{
    return ( left.value == right.value );
}

resource_handle_parts_t ResourceHandle_Unpack(
    resource_handle_t handle ) noexcept
{
    return {
        ResourceHandle_Slot( handle ),
        ResourceHandle_Generation( handle ),
        ResourceHandle_TypeSlot( handle )
    };
}

resource_slot_t ResourceHandle_Slot( resource_handle_t handle ) noexcept
{
    // Slot occupies the low bits and therefore needs no right shift.
    return static_cast<resource_slot_t>(
        handle.value & static_cast<u64>( CY_RESOURCE_SLOT_MAX ) );
}

resource_generation_t ResourceHandle_Generation(
   resource_handle_t handle ) noexcept
{
    // Mask after shifting to prevent the type field from entering the result.
    return static_cast<resource_generation_t>(
        ( handle.value >> CY_RESOURCE_GENERATION_SHIFT ) &
        static_cast<u64>( CY_RESOURCE_GENERATION_MAX ) );
}

resource_type_slot_t ResourceHandle_TypeSlot(
    resource_handle_t handle ) noexcept
{
    // Type is the highest field; the mask documents its fixed 16-bit width.
    return static_cast<resource_type_slot_t>(
        ( handle.value >> CY_RESOURCE_TYPE_SLOT_SHIFT ) &
        static_cast<u64>( CY_RESOURCE_TYPE_SLOT_MAX ) );
}

} // namespace cypher::common
