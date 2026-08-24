//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_ProjectManifest.h
//  Purpose: Declares typed decoding for validated Cypher project manifests.
//  Details: Decoded strings are non-owning views into the source CYKV document.
//           The document must remain alive and unchanged while a manifest view is used.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER2_PROJECTMANIFEST_H
#define CYPHER_COMMON_TIER2_PROJECTMANIFEST_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ProjectSchema.h"

namespace cypher::common
{

enum class project_manifest_status_t : u8 {
    OK = 0u,               // Manifest decoded successfully.
    INVALID_ARGUMENT,     // Document, diagnostics, or output argument is invalid.
    INVALID_DOCUMENT,     // Generic project schema validation failed.
    INVALID_PROJECT_ID,   // Durable project ID is not a stable identifier.
    INVALID_START_MAP,    // Startup map is not a canonical .cymap path.
    INVALID_SEARCH_PATH,  // Search root is not a canonical virtual path.
    DUPLICATE_SEARCH_PATH,// Ordered search roots contain an exact duplicate.
    INTERNAL_ERROR        // Validated CYKV data could not be extracted.
};

/*
================
Project Manifest View

This is a zero-copy startup view, not an owning project object. Every string points
into the parsed CYKV document, so callers must retain that document for the complete
view lifetime. The fixed search-path array bounds memory and avoids hidden allocation.
================
*/
struct project_manifest_view_t {
    // Stable machine identifier used by tools, caches, and generated resources.
    string_view_t id{};

    // Human-readable project name; it is not used as persistent identity.
    string_view_t name{};

    // Canonical VFS path to the initial editable map resource.
    string_view_t startMap{};

    // Ordered VFS search roots; earlier entries retain caller-defined priority.
    string_view_t searchPaths[CY_PROJECT_MAX_SEARCH_PATHS]{};
    usize nSearchPaths{ 0u }; // Active entries in searchPaths.
};

// Schema diagnostics describe structural errors. status and iSearchPath describe
// project-specific semantic errors that cannot be represented by generic rules.
struct project_manifest_decode_result_t {
    project_manifest_status_t status{ project_manifest_status_t::OK }; // Decode result.
    schema_validation_result_t validation{}; // Structural schema result.
    usize iSearchPath{ CY_INVALID_SIZE }; // Failing search root, when applicable.
};

// Validates and decodes one project document without allocating or taking ownership.
// pManifestOut is modified only when the entire operation succeeds.
CYPHER_NODISCARD CYPHER_COMMON_API
project_manifest_decode_result_t ProjectManifest_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    project_manifest_view_t *pManifestOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ProjectManifest_DecodeSucceeded(
    const project_manifest_decode_result_t &result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ProjectManifest_StatusName(
    project_manifest_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_PROJECTMANIFEST_H
