#ifndef CYPHER_COMMON_RESOURCEHANDLE_H
#define CYPHER_COMMON_RESOURCEHANDLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Handle.h"

namespace cypher::common
{

using resource_slot_t           = u32;
using resource_generation_t     = u32;
using resource_type_slot_t      = u32;

constexpr resource_generation_t CY_RESOURCE_GENERATION_INVALID      = 0u;
constexpr resource_generation_t CY_RESOURCE_GENERATION_FIRST        = 1u;
constexpr resource_generation_t CY_RESOURCE_GENERATION_MAX          = CY_HANDLE64_GENERATION_MAX;

constexpr resource_type_slot_t CY_RESOURCE_TYPE_SLOT_INVALID        = 0u;
constexpr resource_type_slot_t CY_RESOURCE_TYPE_SLOT_MAX            = CY_HANDLE64_TYPE_MAX;

struct resource_handle_t {
    handle64_t packed{};        // @NOTE since our handle64_t already uses 16 bit | 16 bit | 32 bit layout, I decided to reuse that same thing for the resource system handle.
};

struct resource_handle_parts_t {
    resource_slot_t         iSlot{};
    resource_generation_t   nGeneration{};
    resource_type_slot_t    iTypeSlot{}; 
};

constexpr resource_handle_t CY_RESOURCE_HANDLE_INVALID{};
static_assert( sizeof( resource_handle_t ) == sizeof( u64 ), "Resource handles must remain exactly 64 bits." ); // @NOTE We want to keep the runtime handle always exactly 64 bits.

// this creates a resource handle after careful validation of the generation number and runtime type slot number.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ResourceHandle_TryMake( 
    resource_slot_t iSlot,
    resource_generation_t nGeneration,
    resource_type_slot_t iTypeSlot,
    resource_handle_t *pHandleOut ) noexcept;

// this creates a resource handle from already trusted manager owned values.
CYPHER_NODISCARD CYPHER_COMMON_API
resource_handle_t ResourceHandle_Make(
    resource_slot_t iSlot,
    resource_generation_t nGeneration,
    resource_type_slot_t iTypeSlot ) noexcept;

// Checks only the packed handle structure, not whether its record is still alive.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ResourceHandle_IsValid( resource_handle_t &handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ResourceHandle_Equals( 
    resource_handle_t left,
    resource_handle_t right ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
resource_handle_parts_t ResourceHandle_Unpack(
    resource_handle_t handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
resource_slot_t ResourceHandle_Slot( resource_handle_t handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
resource_generation_t ResourceHandle_Generation(
    resource_handle_t handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
resource_type_slot_t ResourceHandle_TypeSlot(
    resource_handle_t handle ) noexcept;
   
}           // namespace cypher::common

#endif      // CYPHER_COMMON_RESOURCEHANDLE_H
