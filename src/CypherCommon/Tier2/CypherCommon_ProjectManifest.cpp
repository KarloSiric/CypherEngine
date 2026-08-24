//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_ProjectManifest.cpp
//  Purpose: Implements typed decoding for Cypher project manifests.
//  Details: Schema validation handles structure and scalar bounds. This decoder adds
//           project-specific identifier, canonical virtual-path, extension, and
//           duplicate-search-path policy before committing the borrowed output view.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ProjectManifest.h"

#include "CypherCommon_DataValidation.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t ManifestText(
    const char ( &text )[nExtent] ) noexcept
{
    // Static field names become borrowed views without their trailing NUL.
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

CYPHER_NODISCARD bool_t ReadStringMember(
    const key_value_t *pRoot,
    string_view_t name,
    string_view_t &valueOut ) noexcept
{
    return KeyValue_GetString(
        KeyValue_Find( pRoot, name ),
        &valueOut );
}

} // namespace

project_manifest_decode_result_t ProjectManifest_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    project_manifest_view_t *pManifestOut ) noexcept
{
    project_manifest_decode_result_t result{};
    if ( pDocument == nullptr || pManifestOut == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ) {
        result.status = project_manifest_status_t::INVALID_ARGUMENT;
        result.validation.status = schema_validation_status_t::INVALID_ARGUMENT;
        return result;
    }

    // Generic validation establishes member types, required fields, array limits,
    // and scalar bounds before project-specific policy is evaluated.
    result.validation = Schema_ValidateDocument(
        ProjectSchema_V1(),
        pDocument,
        options,
        pDiagnostics,
        nDiagnosticCapacity );
    if ( !Schema_ValidationSucceeded( result.validation ) ) {
        result.status = project_manifest_status_t::INVALID_DOCUMENT;
        return result;
    }

    // Build locally so a later semantic failure cannot partially modify output.
    const key_value_t *pRoot = KeyValue_Root( pDocument );
    project_manifest_view_t manifest{};
    if ( !ReadStringMember( pRoot, ManifestText( "id" ), manifest.id ) ||
         !ReadStringMember( pRoot, ManifestText( "name" ), manifest.name ) ||
         !ReadStringMember(
             pRoot,
             ManifestText( "start_map" ),
             manifest.startMap ) ) {
        result.status = project_manifest_status_t::INTERNAL_ERROR;
        return result;
    }

    // Schema bounds strings; semantic checks establish durable naming policy.
    if ( !DataValidation_Succeeded(
             DataValidation_CheckStableIdentifier(
                 manifest.id,
                 CY_PROJECT_ID_MAX_LENGTH ) ) ) {
        result.status = project_manifest_status_t::INVALID_PROJECT_ID;
        return result;
    }
    if ( !DataValidation_Succeeded(
             DataValidation_CheckResourcePath(
                 manifest.startMap,
                 ManifestText( ".cymap" ),
                 CY_PROJECT_PATH_MAX_LENGTH ) ) ) {
        result.status = project_manifest_status_t::INVALID_START_MAP;
        return result;
    }

    // Search-path order is meaningful because earlier mounts have higher priority.
    const key_value_t *pSearchPaths = KeyValue_Find(
        pRoot,
        ManifestText( "search_paths" ) );
    if ( pSearchPaths != nullptr ) {
        manifest.nSearchPaths = KeyValue_ChildCount( pSearchPaths );
        if ( manifest.nSearchPaths > CY_PROJECT_MAX_SEARCH_PATHS ) {
            result.status = project_manifest_status_t::INTERNAL_ERROR;
            return result;
        }

        for ( usize iPath = 0u;
              iPath < manifest.nSearchPaths;
              ++iPath ) {
            if ( !KeyValue_GetString(
                     KeyValue_ChildAt( pSearchPaths, iPath ),
                     &manifest.searchPaths[iPath] ) ) {
                result.status = project_manifest_status_t::INTERNAL_ERROR;
                return result;
            }
            if ( !DataValidation_Succeeded(
                     DataValidation_CheckCanonicalVirtualPath(
                         manifest.searchPaths[iPath],
                         CY_PROJECT_PATH_MAX_LENGTH ) ) ) {
                result.status = project_manifest_status_t::INVALID_SEARCH_PATH;
                result.iSearchPath = iPath;
                return result;
            }

            // With at most 64 entries, direct comparison avoids temporary storage
            // and is simpler than constructing a hash table during startup.
            for ( usize iPrevious = 0u;
                  iPrevious < iPath;
                  ++iPrevious ) {
                if ( StringView_Equals(
                         manifest.searchPaths[iPrevious],
                         manifest.searchPaths[iPath] ) ) {
                    result.status =
                        project_manifest_status_t::DUPLICATE_SEARCH_PATH;
                    result.iSearchPath = iPath;
                    return result;
                }
            }
        }
    }

    // Single transaction commit point.
    *pManifestOut = manifest;
    return result;
}

bool_t ProjectManifest_DecodeSucceeded(
    const project_manifest_decode_result_t &result ) noexcept
{
    return result.status == project_manifest_status_t::OK;
}

const char *ProjectManifest_StatusName(
    project_manifest_status_t status ) noexcept
{
    switch ( status ) {
        case project_manifest_status_t::OK: return "OK";
        case project_manifest_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case project_manifest_status_t::INVALID_DOCUMENT: return "INVALID_DOCUMENT";
        case project_manifest_status_t::INVALID_PROJECT_ID: return "INVALID_PROJECT_ID";
        case project_manifest_status_t::INVALID_START_MAP: return "INVALID_START_MAP";
        case project_manifest_status_t::INVALID_SEARCH_PATH: return "INVALID_SEARCH_PATH";
        case project_manifest_status_t::DUPLICATE_SEARCH_PATH: return "DUPLICATE_SEARCH_PATH";
        case project_manifest_status_t::INTERNAL_ERROR: return "INTERNAL_ERROR";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
