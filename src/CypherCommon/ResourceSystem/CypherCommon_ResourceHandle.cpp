//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ResourceSystem/CypherCommon_ResourceHandle.cpp
//  Purpose: Implements compact generation-checked runtime resource handles.
//  Details: The resource handle specializes Tier0's 64-bit packed handle by
//           reserving zero generation and type values as invalid sentinels.
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
    if ( nGeneration == CY_RESOURCE_GENERATION_INVALID ||
         nGeneration > CY_RESOURCE_GENERATION_MAX ) {
        return CY_FALSE;
    }
    if ( iTypeSlot == CY_RESOURCE_TYPE_SLOT_INVALID ||
         iTypeSlot > CY_RESOURCE_TYPE_SLOT_MAX ) {
        return CY_FALSE;
    }

    return Cy_Handle64TryMake( iSlot, nGeneration, iTypeSlot, &pHandleOut->packed );
}

resource_handle_t ResourceHandle_Make(
    resource_slot_t iSlot,
    resource_generation_t nGeneration,
    resource_type_slot_t iTypeSlot ) noexcept
{
    resource_handle_t handle = CY_RESOURCE_HANDLE_INVALID;

    const bool_t bCreated = ResourceHandle_TryMake( iSlot, nGeneration, iTypeSlot, &handle );
    CY_ASSERT_MSG( bCreated, "ResourceHandle_Make received an invalid generation or type slot."  );

    return handle;
}

bool_t ResourceHandle_IsValid( resource_handle_t handle ) noexcept
{
    if ( !Cy_Handle64IsValid( handle.packed ) ) {
        return CY_FALSE;
    }

    const resource_generation_t nGeneration = Cy_Handle64Generation( handle.packed );
    const resource_type_slot_t iTypeSlot = Cy_Handle64Type( handle.packed );

    return ( nGeneration != CY_RESOURCE_GENERATION_INVALID && iTypeSlot != CY_RESOURCE_TYPE_SLOT_INVALID );
}

bool_t ResourceHandle_Equals(
    resource_handle_t left,
    resource_handle_t right ) noexcept
{
    return ( left.packed.value == right.packed.value );
}

resource_handle_parts_t ResourceHandle_Unpack(
    resource_handle_t handle ) noexcept
{
    const handle_parts64_t packedParts = Cy_Handle64Unpack( handle.packed );
    resource_handle_parts_t resourceParts{};
    resourceParts.iSlot = packedParts.nIndex;
    resourceParts.nGeneration = packedParts.nGeneration;
    resourceParts.iTypeSlot = packedParts.nType;

    return resourceParts;
}

resource_slot_t ResourceHandle_Slot( resource_handle_t handle ) noexcept
{
    return ( Cy_Handle64Index( handle.packed ) );
}

resource_generation_t ResourceHandle_Generation(
   resource_handle_t handle ) noexcept
{
    return ( Cy_Handle64Generation( handle.packed ) );
}

resource_type_slot_t ResourceHandle_TypeSlot(
    resource_handle_t handle ) noexcept
{
    return ( Cy_Handle64Type( handle.packed ) );
}

} // namespace cypher::common
