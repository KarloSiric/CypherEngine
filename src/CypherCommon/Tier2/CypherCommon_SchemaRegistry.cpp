//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_SchemaRegistry.cpp
//  Purpose: Implements caller-owned registration and lookup for CYKV schemas.
//  Details: Exact schema ID/version pairs are kept in fixed caller storage. The
//           registry neither owns descriptors nor performs hidden allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SchemaRegistry.h"

#include "CypherCommon_StringView.h"

namespace cypher::common
{

namespace
{

CYPHER_NODISCARD bool_t RegistryIsValid(
    const schema_registry_t *pRegistry ) noexcept
{
    return pRegistry != nullptr &&
           pRegistry->nCount <= pRegistry->nCapacity &&
           ( pRegistry->ppSchemas != nullptr || pRegistry->nCapacity == 0u );
}

} // namespace

bool_t SchemaRegistry_Init(
    schema_registry_t *pRegistry,
    const schema_descriptor_t **ppStorage,
    usize nCapacity ) noexcept
{
    if ( pRegistry == nullptr ||
         ( ppStorage == nullptr && nCapacity != 0u ) ) {
        return CY_FALSE;
    }
    *pRegistry = { ppStorage, 0u, nCapacity };
    for ( usize iSchema = 0u; iSchema < nCapacity; ++iSchema ) {
        ppStorage[iSchema] = nullptr;
    }
    return CY_TRUE;
}

void SchemaRegistry_Clear( schema_registry_t *pRegistry ) noexcept
{
    if ( !RegistryIsValid( pRegistry ) ) {
        return;
    }
    for ( usize iSchema = 0u; iSchema < pRegistry->nCount; ++iSchema ) {
        pRegistry->ppSchemas[iSchema] = nullptr;
    }
    pRegistry->nCount = 0u;
}

schema_registry_status_t SchemaRegistry_Register(
    schema_registry_t *pRegistry,
    const schema_descriptor_t *pSchema ) noexcept
{
    if ( !RegistryIsValid( pRegistry ) || pSchema == nullptr ) {
        return schema_registry_status_t::INVALID_ARGUMENT;
    }
    if ( Schema_CheckDescriptor( pSchema ) !=
         schema_descriptor_status_t::OK ) {
        return schema_registry_status_t::INVALID_SCHEMA;
    }
    if ( SchemaRegistry_Find(
             pRegistry,
             pSchema->schemaId,
             pSchema->nVersion ) != nullptr ) {
        return schema_registry_status_t::DUPLICATE_SCHEMA;
    }
    if ( pRegistry->nCount == pRegistry->nCapacity ) {
        return schema_registry_status_t::CAPACITY_EXCEEDED;
    }
    pRegistry->ppSchemas[pRegistry->nCount++] = pSchema;
    return schema_registry_status_t::OK;
}

const schema_descriptor_t *SchemaRegistry_Find(
    const schema_registry_t *pRegistry,
    string_view_t schemaId,
    u32 nVersion ) noexcept
{
    if ( !RegistryIsValid( pRegistry ) ||
         ( schemaId.pData == nullptr && schemaId.cchLength != 0u ) ||
         schemaId.cchLength == 0u || nVersion == 0u ) {
        return nullptr;
    }

    for ( usize iSchema = 0u; iSchema < pRegistry->nCount; ++iSchema ) {
        const schema_descriptor_t *pSchema = pRegistry->ppSchemas[iSchema];
        if ( pSchema != nullptr && pSchema->nVersion == nVersion &&
             StringView_Equals( pSchema->schemaId, schemaId ) ) {
            return pSchema;
        }
    }
    return nullptr;
}

schema_validation_result_t SchemaRegistry_ValidateDocument(
    const schema_registry_t *pRegistry,
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity ) noexcept
{
    schema_validation_result_t result{};
    if ( !RegistryIsValid( pRegistry ) || pDocument == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ) {
        result.status = schema_validation_status_t::INVALID_ARGUMENT;
        return result;
    }

    const key_value_document_header_t header =
        KeyValue_DocumentHeader( pDocument );
    const schema_descriptor_t *pSchema = SchemaRegistry_Find(
        pRegistry,
        header.schemaId,
        header.nSchemaVersion );
    if ( pSchema != nullptr ) {
        return Schema_ValidateDocument(
            pSchema,
            pDocument,
            options,
            pDiagnostics,
            nDiagnosticCapacity );
    }

    result.status = schema_validation_status_t::SCHEMA_NOT_FOUND;
    result.nDiagnosticsRequired = 1u;
    result.nErrors = 1u;
    if ( nDiagnosticCapacity != 0u ) {
        schema_diagnostic_t &diagnostic = pDiagnostics[0];
        diagnostic = {};
        diagnostic.code = schema_diagnostic_code_t::SCHEMA_NOT_FOUND;
        diagnostic.severity = schema_diagnostic_severity_t::ERROR;
        diagnostic.actualType = KeyValue_Type( KeyValue_Root( pDocument ) );
        diagnostic.path[0] = '/';
        diagnostic.path[1] = '\0';
        result.nDiagnosticsWritten = 1u;
    } else {
        result.bDiagnosticsTruncated = CY_TRUE;
    }
    return result;
}

const char *SchemaRegistry_StatusName(
    schema_registry_status_t status ) noexcept
{
    switch ( status ) {
        case schema_registry_status_t::OK: return "OK";
        case schema_registry_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case schema_registry_status_t::INVALID_SCHEMA: return "INVALID_SCHEMA";
        case schema_registry_status_t::DUPLICATE_SCHEMA: return "DUPLICATE_SCHEMA";
        case schema_registry_status_t::CAPACITY_EXCEEDED: return "CAPACITY_EXCEEDED";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
