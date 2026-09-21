//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueSource.h
//  Purpose: Declares bounded CYKV 2 source dependency resolution.
//  Details: A caller-provided loader resolves #include and #base through its VFS.
//           Parsing, dependency expansion, and base merging commit transactionally.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_KEYVALUESOURCE_H
#define CYPHER_COMMON_TIER1_KEYVALUESOURCE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_KeyValueParser.h"

namespace cypher::common
{

inline constexpr usize CY_KEY_VALUE_SOURCE_DIAGNOSTIC_PATH_CAPACITY = 1024u;
// Hard ceilings protect recursive traversal and the resolver's upfront arrays.
// Callers may lower defaults or raise them only through these finite bounds.
inline constexpr usize CY_KEY_VALUE_SOURCE_MAX_DEPENDENCY_DEPTH = 64u;
inline constexpr usize CY_KEY_VALUE_SOURCE_MAX_SOURCES = 1024u;
inline constexpr usize CY_KEY_VALUE_SOURCE_MAX_EDGES = 8192u;

enum class key_value_source_open_status_t : u8 {
    OK = 0u,       // Source view and canonical path were returned.
    NOT_FOUND,     // Referenced source does not exist in the active VFS view.
    ACCESS_DENIED, // Source exists but policy does not permit access.
    IO_ERROR       // Source could not be read completely.
};

enum class key_value_dependency_kind_t : u8 {
    INCLUDE = 0u, // Typed namespace import.
    BASE           // Missing-value inheritance source.
};

struct key_value_source_view_t {
    string_view_t canonicalPath{}; // Normalized, root-confined virtual path.
    string_view_t text{};          // Complete borrowed UTF-8 source bytes.
    void *pHandle{ nullptr };       // Loader-owned token passed to release.
};

// requestingSourcePath is canonical. referencedPath is the spelling from the
// directive and may be relative. The callback resolves mounts, relative paths,
// and symlinks, then returns a canonical root-confined virtual path.
using key_value_source_open_fn_t = key_value_source_open_status_t ( * )(
    string_view_t requestingSourcePath,
    string_view_t referencedPath,
    key_value_source_view_t *pOutSource,
    void *pUserData ) noexcept;

using key_value_source_release_fn_t = void ( * )(
    const key_value_source_view_t *pSource,
    void *pUserData ) noexcept;

// Called once for every dependency edge, including repeated references to an
// already parsed source. Views are valid only for the duration of the callback.
using key_value_dependency_fn_t = bool_t ( * )(
    string_view_t requestingSourcePath,
    string_view_t dependencyCanonicalPath,
    key_value_dependency_kind_t kind,
    void *pUserData ) noexcept;

enum class key_value_source_status_t : u8 {
    OK = 0u,              // Root plus every transitive dependency resolved and committed.
    INVALID_ARGUMENT,     // Options, callback, source, path, or destination is invalid.
    PATH_LIMIT,           // A referenced or canonical path exceeds policy.
    INVALID_PATH,         // A path is absolute, noncanonical, empty, or escapes its root.
    INVALID_DIRECTIVE,    // #include or #base spelling violates the CYKV 2 grammar.
    OPEN_FAILED,          // The source loader did not return a dependency.
    DEPENDENCY_CYCLE,     // An include/base edge reaches a source already resolving.
    DEPENDENCY_DEPTH_LIMIT, // Transitive source depth exceeds policy.
    SOURCE_LIMIT,         // Unique source count exceeds policy.
    EDGE_LIMIT,           // Dependency edge count exceeds policy.
    SOURCE_BYTE_LIMIT,    // Aggregate bytes of unique sources exceed policy.
    DUPLICATE_ALIAS,      // Two direct includes bind the same namespace.
    SCHEMA_MISMATCH,      // A base does not have the local document's exact schema identity.
    MERGE_CONFLICT,       // Local/base values at one path have incompatible types.
    DEPENDENCY_SINK_FAILED, // Dependency reporting callback rejected an edge.
    PARSE_FAILED,         // One source failed ordinary CYKV parsing.
    OUT_OF_MEMORY         // Scratch storage, a dependency document, or a merge allocation failed.
};

struct key_value_source_options_t {
    key_value_parse_options_t parseOptions{}; // Grammar and semantic expansion limits.
    const allocator_t *pScratchAllocator{ nullptr }; // Null selects the system allocator.
    key_value_source_open_fn_t pfnOpen{ nullptr }; // Required dependency loader.
    key_value_source_release_fn_t pfnRelease{ nullptr }; // Required matching release.
    key_value_dependency_fn_t pfnDependency{ nullptr }; // Optional edge reporter.
    void *pUserData{ nullptr }; // Shared callback state.
    usize nMaxDependencyDepth{ 16u }; // Maximum edge depth; hard ceiling is 64.
    usize nMaxSources{ 64u };         // Unique sources; hard ceiling is 1,024.
    usize nMaxEdges{ 256u };          // Combined directives; hard ceiling is 8,192.
    usize cbMaxAggregateSources{ 64u * CY_MIB }; // Unique source bytes combined.
    usize cchMaxPath{ 259u }; // Current engine VFS/resource path payload limit.
};

struct key_value_source_result_t {
    key_value_source_status_t status{ key_value_source_status_t::OK };
    // Nested parser diagnostic on PARSE_FAILED; final effective-tree counts on OK.
    key_value_parse_result_t parseResult{};
    key_value_source_open_status_t openStatus{
        key_value_source_open_status_t::OK
    }; // Loader result when status is OPEN_FAILED.
    key_value_dependency_kind_t dependencyKind{
        key_value_dependency_kind_t::INCLUDE
    }; // Directive associated with a dependency error.
    text_location_t errorLocation{}; // Directive or parser source location.
    usize nUniqueSources{ 0u };      // Root plus distinct canonical dependencies.
    usize nDependencyEdges{ 0u };    // Every include/base occurrence.
    usize cbAggregateSources{ 0u };  // Bytes across unique sources.
    usize cchErrorSourcePath{ 0u };
    char errorSourcePath[CY_KEY_VALUE_SOURCE_DIAGNOSTIC_PATH_CAPACITY]{};
    usize cchErrorReferencedPath{ 0u };
    // Authored directive operand associated with the failure; empty when none.
    char errorReferencedPath[CY_KEY_VALUE_SOURCE_DIAGNOSTIC_PATH_CAPACITY]{};
};

// Dependency directives are available only in CYKV 2 and must appear after the
// two-line header and before local #define declarations. #include requires an
// explicit namespace: #include "path.cydf" as namespace. #base takes one path.
CYPHER_NODISCARD CYPHER_COMMON_API
key_value_source_result_t KeyValue_ParseSource(
    string_view_t rootCanonicalPath,
    string_view_t rootText,
    const key_value_source_options_t &options,
    key_value_document_t *pDocument ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *KeyValue_SourceStatusName(
    key_value_source_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUESOURCE_H
