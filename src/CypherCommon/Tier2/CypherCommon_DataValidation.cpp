//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_DataValidation.cpp
//  Purpose: Implements reusable semantic checks for authored Cypher data.
//  Details: Validation is deterministic, allocation-free, and deliberately
//           lexical. It does not probe mounts, resolve files, or mutate source text.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Data Validation Implementation Notes

Validation accumulates source-aware diagnostics without mutating the document being checked.
Schema failures remain distinct from parser and filesystem failures.
================
*/

#include "CypherCommon_DataValidation.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_StringPath.h"

namespace cypher::common
{

namespace
{

CYPHER_NODISCARD data_validation_result_t Failure(
    data_validation_status_t status,
    usize iByte = CY_INVALID_SIZE ) noexcept
{
    return { status, iByte };
}

CYPHER_NODISCARD bool_t IsForbiddenVirtualPathByte( char ch ) noexcept
{
    // Exclude whitespace, non-ASCII, and characters with host-filesystem meaning.
    const u8 value = static_cast<u8>( ch );
    return value < 0x21u || value >= 0x7fu || ch == ':' || ch == '*' ||
           ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|';
}

CYPHER_NODISCARD bool_t SegmentEquals(
    string_view_t path,
    usize iStart,
    usize cchLength,
    const char *pText,
    usize cchText ) noexcept
{
    if ( cchLength != cchText ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < cchLength; ++iByte ) {
        if ( path.pData[iStart + iByte] != pText[iByte] ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD data_validation_result_t CheckSegment(
    string_view_t path,
    usize iStart,
    usize iEnd ) noexcept
{
    // Empty, current-directory, and parent-directory segments are noncanonical.
    const usize cchLength = iEnd - iStart;
    if ( cchLength == 0u ) {
        return Failure(
            data_validation_status_t::NON_CANONICAL_PATH,
            iStart );
    }
    if ( SegmentEquals( path, iStart, cchLength, ".", 1u ) ) {
        return Failure(
            data_validation_status_t::NON_CANONICAL_PATH,
            iStart );
    }
    if ( SegmentEquals( path, iStart, cchLength, "..", 2u ) ) {
        return Failure(
            data_validation_status_t::PARENT_TRAVERSAL,
            iStart );
    }
    return {};
}

CYPHER_NODISCARD bool_t ExtensionIsValid(
    string_view_t extension ) noexcept
{
    if ( !StringView_IsValid( extension ) || extension.cchLength < 2u ||
         extension.pData[0] != '.' ) {
        return CY_FALSE;
    }
    for ( usize iByte = 1u; iByte < extension.cchLength; ++iByte ) {
        const char ch = extension.pData[iByte];
        if ( !Char_IsLowerAscii( ch ) && !Char_IsDigitAscii( ch ) &&
             ch != '_' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

} // namespace

data_validation_result_t DataValidation_CheckAsciiIdentifier(
    string_view_t value,
    usize cchMax ) noexcept
{
    if ( !StringView_IsValid( value ) || cchMax == 0u ) {
        return Failure( data_validation_status_t::INVALID_ARGUMENT );
    }
    if ( value.cchLength == 0u ) {
        return Failure( data_validation_status_t::EMPTY_VALUE, 0u );
    }
    if ( value.cchLength > cchMax ) {
        return Failure( data_validation_status_t::LENGTH_LIMIT, cchMax );
    }
    if ( !Char_IsAlphaAscii( value.pData[0] ) && value.pData[0] != '_' ) {
        return Failure(
            data_validation_status_t::INVALID_IDENTIFIER_START,
            0u );
    }
    for ( usize iByte = 1u; iByte < value.cchLength; ++iByte ) {
        const char ch = value.pData[iByte];
        if ( !Char_IsAlphaAscii( ch ) && !Char_IsDigitAscii( ch ) &&
             ch != '_' ) {
            return Failure(
                data_validation_status_t::INVALID_IDENTIFIER_BYTE,
                iByte );
        }
    }
    return {};
}

data_validation_result_t DataValidation_CheckStableIdentifier(
    string_view_t value,
    usize cchMax ) noexcept
{
    if ( !StringView_IsValid( value ) || cchMax == 0u ) {
        return Failure( data_validation_status_t::INVALID_ARGUMENT );
    }
    if ( value.cchLength == 0u ) {
        return Failure( data_validation_status_t::EMPTY_VALUE, 0u );
    }
    if ( value.cchLength > cchMax ) {
        return Failure( data_validation_status_t::LENGTH_LIMIT, cchMax );
    }
    if ( !Char_IsLowerAscii( value.pData[0] ) ) {
        return Failure(
            data_validation_status_t::INVALID_IDENTIFIER_START,
            0u );
    }
    for ( usize iByte = 1u; iByte < value.cchLength; ++iByte ) {
        const char ch = value.pData[iByte];
        if ( !Char_IsLowerAscii( ch ) && !Char_IsDigitAscii( ch ) &&
             ch != '_' && ch != '-' ) {
            return Failure(
                data_validation_status_t::INVALID_IDENTIFIER_BYTE,
                iByte );
        }
    }
    return {};
}

data_validation_result_t DataValidation_CheckCanonicalVirtualPath(
    string_view_t path,
    usize cchMax ) noexcept
{
    if ( !StringView_IsValid( path ) || cchMax == 0u ) {
        return Failure( data_validation_status_t::INVALID_ARGUMENT );
    }
    if ( path.cchLength == 0u ) {
        return Failure( data_validation_status_t::EMPTY_VALUE, 0u );
    }
    if ( path.cchLength > cchMax ) {
        return Failure( data_validation_status_t::LENGTH_LIMIT, cchMax );
    }
    if ( path.pData[0] == '/' || path.pData[0] == '\\' ) {
        return Failure( data_validation_status_t::NON_CANONICAL_PATH, 0u );
    }

    // Paths stay lexical here; mount lookup and symlink resolution belong to VFS.
    usize iSegmentStart = 0u;
    for ( usize iByte = 0u; iByte < path.cchLength; ++iByte ) {
        const char ch = path.pData[iByte];
        if ( ch == '/' ) {
            const data_validation_result_t segment = CheckSegment(
                path,
                iSegmentStart,
                iByte );
            if ( !DataValidation_Succeeded( segment ) ) {
                return segment;
            }
            iSegmentStart = iByte + 1u;
            continue;
        }
        if ( ch == '\\' || Char_IsUpperAscii( ch ) ) {
            return Failure(
                data_validation_status_t::NON_CANONICAL_PATH,
                iByte );
        }
        if ( IsForbiddenVirtualPathByte( ch ) ) {
            return Failure(
                data_validation_status_t::INVALID_PATH_BYTE,
                iByte );
        }
    }
    return CheckSegment( path, iSegmentStart, path.cchLength );
}

data_validation_result_t DataValidation_CheckResourcePath(
    string_view_t path,
    string_view_t extension,
    usize cchMax ) noexcept
{
    if ( !ExtensionIsValid( extension ) ) {
        return Failure( data_validation_status_t::INVALID_ARGUMENT );
    }
    const data_validation_result_t pathResult =
        DataValidation_CheckCanonicalVirtualPath( path, cchMax );
    if ( !DataValidation_Succeeded( pathResult ) ) {
        return pathResult;
    }
    // Extension comparison is case-sensitive because canonical paths are lowercase.
    if ( !StringPath_HasExtension( path, extension, CY_FALSE ) ) {
        return Failure(
            data_validation_status_t::EXTENSION_MISMATCH,
            path.cchLength );
    }
    return {};
}

bool_t DataValidation_Succeeded(
    const data_validation_result_t &result ) noexcept
{
    return result.status == data_validation_status_t::OK;
}

const char *DataValidation_StatusName(
    data_validation_status_t status ) noexcept
{
    switch ( status ) {
        case data_validation_status_t::OK: return "OK";
        case data_validation_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case data_validation_status_t::EMPTY_VALUE: return "EMPTY_VALUE";
        case data_validation_status_t::LENGTH_LIMIT: return "LENGTH_LIMIT";
        case data_validation_status_t::INVALID_IDENTIFIER_START: return "INVALID_IDENTIFIER_START";
        case data_validation_status_t::INVALID_IDENTIFIER_BYTE: return "INVALID_IDENTIFIER_BYTE";
        case data_validation_status_t::INVALID_PATH_BYTE: return "INVALID_PATH_BYTE";
        case data_validation_status_t::NON_CANONICAL_PATH: return "NON_CANONICAL_PATH";
        case data_validation_status_t::PARENT_TRAVERSAL: return "PARENT_TRAVERSAL";
        case data_validation_status_t::EXTENSION_MISMATCH: return "EXTENSION_MISMATCH";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
