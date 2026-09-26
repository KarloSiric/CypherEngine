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
//  - Added V2 decoding on 2026-09-25
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
#include "CypherCommon_SettingsDocument.h"

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
    INTERNAL_ERROR,       // Validated CYKV data could not be extracted.
    INVALID_GAME,         // V2 game profile is not a stable identifier.
    INVALID_MAPS_PATH,    // V2 maps root is not a canonical virtual path.
    UNSUPPORTED_VERSION   // Schema version is neither V1 nor V2.
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

    // V2 (ADR 0009). Empty or null in V1 documents.
    u32 nVersion{ 0u };                       // Schema version decoded.
    string_view_t game{};                     // Game profile the project builds for.
    string_view_t mapsPath{};                 // Maps root; "maps" when absent.
    const key_value_t *pSettings{ nullptr };  // Team settings block, read with descriptors.
    const key_value_t *pMapDefaults{ nullptr }; // Default map settings for new maps.
};

// Schema diagnostics describe structural errors. status and iSearchPath describe
// project-specific semantic errors that cannot be represented by generic rules.
struct project_manifest_decode_result_t {
    project_manifest_status_t status{ project_manifest_status_t::OK }; // Decode result.
    schema_validation_result_t validation{}; // Structural schema result.
    usize iSearchPath{ CY_INVALID_SIZE }; // Failing search root, when applicable.
};

// Identity for settings stores editing project files: reads V1-V2, writes V2.
CYPHER_NODISCARD CYPHER_COMMON_API
settings_document_identity_t ProjectManifest_Identity() noexcept;

// Validates and decodes one project document without allocating or taking ownership.
// V1 and V2 are accepted. Identity, the start map, and content roots are
// validated strictly because a wrong mount set loads the wrong game data;
// the settings blocks are returned raw for tolerant, per-value reads.
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
