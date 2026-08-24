//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringPath.h
//  Purpose: Declares allocation-free lexical path manipulation.
//  Details: These helpers inspect and compose path text only. They never access the
//           filesystem, resolve mounts, follow links, or apply VFS security policy.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGPATH_H
#define CYPHER_COMMON_TIER1_STRINGPATH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class path_style_t : u8 {
    VIRTUAL = 0u, // Engine path: relative, slash-separated, host independent.
    POSIX,        // POSIX roots and '/' separators.
    WINDOWS,      // Drive/UNC roots and '\\' separators.
    NATIVE        // POSIX or Windows according to the build host.
};

enum path_normalize_flags_t : flags32_t {
    PATH_NORMALIZE_FLAG_NONE                 = 0u,                // Preserve the spelling supplied by the caller.
    PATH_NORMALIZE_FLAG_COLLAPSE_SEPARATORS  = CYPHER_BIT32( 0 ), // Coalesce adjacent directory separators.
    PATH_NORMALIZE_FLAG_RESOLVE_DOT          = CYPHER_BIT32( 1 ), // Remove lexical "." path components.
    PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT      = CYPHER_BIT32( 2 ), // Fold "name/.." without touching the filesystem.
    PATH_NORMALIZE_FLAG_LOWERCASE_ASCII       = CYPHER_BIT32( 3 ), // Fold ASCII letters for case-neutral asset keys.
    PATH_NORMALIZE_FLAG_KEEP_TRAILING_SLASH   = CYPHER_BIT32( 4 ), // Retain one final separator when present.
    PATH_NORMALIZE_FLAG_REJECT_ABSOLUTE       = CYPHER_BIT32( 5 ), // Refuse rooted paths at the VFS boundary.
    PATH_NORMALIZE_FLAG_REJECT_ABOVE_ROOT     = CYPHER_BIT32( 6 )  // Refuse unresolved ".." components above root.
};

enum class path_status_t : u8 {
    OK = 0u,                 // Operation completed and the complete result fits.
    INVALID_ARGUMENT,       // Pointer, capacity, style, or option contract was invalid.
    INVALID_PATH,           // Input contains a malformed root or path component.
    ABSOLUTE_PATH_REJECTED, // Policy disallows the absolute path supplied by the caller.
    ABOVE_ROOT,             // Lexical parent traversal would escape the allowed root.
    OUTPUT_TRUNCATED        // Destination received only a prefix of the required output.
};

struct path_write_result_t {
    path_status_t status{ path_status_t::OK }; // Final lexical operation state.
    usize cchWritten{ 0u };  // Characters physically stored, excluding NUL.
    usize cchRequired{ 0u }; // Complete path length, excluding NUL.
    usize iError{ CY_STRING_VIEW_NPOS }; // Source byte responsible for rejection.
};

// Returns the separator used by a requested path style.
CYPHER_NODISCARD CYPHER_COMMON_API
char StringPath_Separator( path_style_t style ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringPath_IsSeparator( char ch ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringPath_IsAbsolute( string_view_t path, path_style_t style ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringPath_HasRootName( string_view_t path, path_style_t style ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringPath_HasTrailingSeparator( string_view_t path ) noexcept;

// Returned views borrow path storage and are not null terminated.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringPath_RootName( string_view_t path, path_style_t style ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringPath_Parent( string_view_t path ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringPath_FileName( string_view_t path ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringPath_Stem( string_view_t path ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringPath_Extension( string_view_t path ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringPath_HasExtension(
    string_view_t path,
    string_view_t extension,
    bool_t bCaseInsensitiveAscii ) noexcept;

// All writers support count-only queries with pDest == nullptr and cchDest == 0.
CYPHER_NODISCARD_MSG( "Inspect cchRequired to detect path truncation." )
CYPHER_COMMON_API path_write_result_t StringPath_Normalize(
    string_view_t path,
    path_style_t style,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept;

// Joins lexical components only. An absolute or rooted right-hand path is rejected.
CYPHER_NODISCARD_MSG( "Inspect cchRequired to detect path truncation." )
CYPHER_COMMON_API path_write_result_t StringPath_Join(
    string_view_t left,
    string_view_t right,
    path_style_t style,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD_MSG( "Inspect cchRequired to detect path truncation." )
CYPHER_COMMON_API path_write_result_t StringPath_ReplaceExtension(
    string_view_t path,
    string_view_t extension,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD_MSG( "Inspect cchRequired to detect path truncation." )
CYPHER_COMMON_API path_write_result_t StringPath_RemoveExtension(
    string_view_t path,
    char *pDest,
    usize cchDest ) noexcept;

// MakeRelative treats base as a directory and compares components lexically.
// Normalize dot components before calling when canonical relative output is required.
CYPHER_NODISCARD_MSG( "Inspect cchRequired to detect path truncation." )
CYPHER_COMMON_API path_write_result_t StringPath_MakeRelative(
    string_view_t path,
    string_view_t base,
    path_style_t style,
    bool_t bCaseInsensitiveAscii,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGPATH_H
