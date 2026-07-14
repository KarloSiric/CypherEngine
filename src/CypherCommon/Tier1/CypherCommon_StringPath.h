//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringPath.h
//  Purpose: Declares CypherCommon Tier1 StringPath support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
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

/*
================
CypherCommon String Path

Path-shaped string helpers. This is not the VFS policy layer.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

/*
================
Slash Normalization
================
*/
// Converts slash characters in place.
void Cy_FixSlashes( char *pPath, char chSeparator );

// Collapses repeated slashes in place.
void Cy_FixDoubleSlashes( char *pPath );

// Appends a trailing slash when space permits.
usize Cy_AppendSlash( char *pPath, usize cchPath, char chSeparator );

// Removes a trailing slash in place.
void Cy_StripTrailingSlash( char *pPath );

/*
================
Path Composition
================
*/
// Joins pPath and pFileName into pDest.
usize Cy_ComposeFileName( const char *pPath, const char *pFileName, char *pDest, usize cchDest );

// Makes pPath absolute against pBasePath.
usize Cy_MakeAbsolutePath( const char *pPath, const char *pBasePath, char *pDest, usize cchDest );

// Makes pPath relative to pBasePath.
usize Cy_MakeRelativePath( const char *pPath, const char *pBasePath, char *pDest, usize cchDest );

// Removes dot slash segments from pPath in place.
bool_t Cy_RemoveDotSlashes( char *pPath );

/*
================
Path Queries
================
*/
// Returns true when pPath is absolute for the active platform rules.
bool_t Cy_IsAbsolutePath( const char *pPath );

// Returns the unqualified file name portion of pPath.
const char *Cy_UnqualifiedFileName( const char *pPath );

// Returns the extension without the dot, or nullptr when none exists.
const char *Cy_GetFileExtension( const char *pPath );

// Returns true when pPath has pExtension.
bool_t Cy_HasFileExtension( const char *pPath, const char *pExtension );

/*
================
Path Extraction / Mutation
================
*/
// Extracts the directory portion into pDest.
usize Cy_ExtractFilePath( const char *pPath, char *pDest, usize cchDest );

// Extracts the extension into pDest.
usize Cy_ExtractFileExtension( const char *pPath, char *pDest, usize cchDest );

// Extracts the base filename without extension into pDest.
usize Cy_FileBase( const char *pPath, char *pDest, usize cchDest );

// Sets or replaces the extension.
usize Cy_SetExtension( const char *pPath, const char *pExtension, char *pDest, usize cchDest );

// Applies pExtension only when pPath has no extension.
usize Cy_DefaultExtension( const char *pPath, const char *pExtension, char *pDest, usize cchDest );

// Strips the extension in place.
void Cy_StripExtension( char *pPath );

// Strips the file name in place, leaving the directory.
void Cy_StripFilename( char *pPath );

// Strips the last directory from pPath in place.
void Cy_StripLastDir( char *pPath );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGPATH_H
