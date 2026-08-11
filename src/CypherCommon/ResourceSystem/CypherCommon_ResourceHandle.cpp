/*======================================================================
   File: CypherCommon_ResourceHandle.cpp
   Project: CYPHER
   Author: ksiric <email@example.com>
   Created: 2026-08-11 21:17:40
   Last Modified by: ksiric
   Last Modified: 2026-08-11 22:05:39
   ---------------------------------------------------------------------
   Description:
       
   ---------------------------------------------------------------------
   License: 
   Company: 
   Version: 0.1.0
 ======================================================================
                                                                       */

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
    if ( nGeneration == CY_RESOURCE_GENERATION_INVALID || nGeneration > CY_RESOURCE_GENERATION_MAX ) {
        return CY_FALSE;
    }
    if ( iTypeSlot == CY_RESOURCE_TYPE_SLOT_INVALID || iTypeSlot > CY_RESOURCE_TYPE_SLOT_MAX ) {
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

}           // namespace cypher::common
