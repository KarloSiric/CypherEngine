//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_SchemaRegistry.h
//  Purpose: Declares caller-owned registration and lookup for CYKV schemas.
//  Details: The registry stores immutable descriptor pointers in caller-provided
//           memory. It performs no allocation and resolves exact schema ID/version
//           pairs from parsed CYKV document headers.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER2_SCHEMAREGISTRY_H
#define CYPHER_COMMON_TIER2_SCHEMAREGISTRY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Schema.h"

namespace cypher::common
{

struct schema_registry_t {
    const schema_descriptor_t **ppSchemas{ nullptr }; // Caller-owned pointer slots.
    usize nCount{ 0u };                               // Registered descriptors.
    usize nCapacity{ 0u };                            // Total writable slots.
};

enum class schema_registry_status_t : u8 {
    OK = 0u,          // Registry operation completed.
    INVALID_ARGUMENT,// Registry or descriptor pointer is invalid.
    INVALID_SCHEMA,  // Descriptor graph failed structural validation.
    DUPLICATE_SCHEMA,// Exact schema ID and version pair already exists.
    CAPACITY_EXCEEDED// Caller-owned pointer storage is full.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SchemaRegistry_Init(
    schema_registry_t *pRegistry,
    const schema_descriptor_t **ppStorage,
    usize nCapacity ) noexcept;

CYPHER_COMMON_API void SchemaRegistry_Clear(
    schema_registry_t *pRegistry ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
schema_registry_status_t SchemaRegistry_Register(
    schema_registry_t *pRegistry,
    const schema_descriptor_t *pSchema ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const schema_descriptor_t *SchemaRegistry_Find(
    const schema_registry_t *pRegistry,
    string_view_t schemaId,
    u32 nVersion ) noexcept;

// Returns the highest registered version for tooling and migration discovery.
// Validation remains exact and never treats this as an automatic fallback.
CYPHER_NODISCARD CYPHER_COMMON_API
const schema_descriptor_t *SchemaRegistry_FindLatest(
    const schema_registry_t *pRegistry,
    string_view_t schemaId ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
schema_validation_result_t SchemaRegistry_ValidateDocument(
    const schema_registry_t *pRegistry,
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *SchemaRegistry_StatusName(
    schema_registry_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_SCHEMAREGISTRY_H
