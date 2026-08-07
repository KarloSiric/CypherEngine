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
    VIRTUAL = 0u,
    POSIX,
    WINDOWS,
    NATIVE
};

enum path_normalize_flags_t : flags32_t {
    PATH_NORMALIZE_FLAG_NONE                 = 0u,
    PATH_NORMALIZE_FLAG_COLLAPSE_SEPARATORS  = CYPHER_BIT32( 0 ),
    PATH_NORMALIZE_FLAG_RESOLVE_DOT          = CYPHER_BIT32( 1 ),
    PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT      = CYPHER_BIT32( 2 ),
    PATH_NORMALIZE_FLAG_LOWERCASE_ASCII       = CYPHER_BIT32( 3 ),
    PATH_NORMALIZE_FLAG_KEEP_TRAILING_SLASH   = CYPHER_BIT32( 4 ),
    PATH_NORMALIZE_FLAG_REJECT_ABSOLUTE       = CYPHER_BIT32( 5 ),
    PATH_NORMALIZE_FLAG_REJECT_ABOVE_ROOT     = CYPHER_BIT32( 6 )
};

enum class path_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_PATH,
    ABSOLUTE_PATH_REJECTED,
    ABOVE_ROOT,
    OUTPUT_TRUNCATED
};

struct path_write_result_t {
    path_status_t status{ path_status_t::OK };
    usize cchWritten{ 0u };
    usize cchRequired{ 0u };
    usize iError{ CY_STRING_VIEW_NPOS };
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
