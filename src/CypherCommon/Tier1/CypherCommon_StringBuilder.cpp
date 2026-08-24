//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringBuilder.cpp
//  Purpose: Implements non-owning bounded text construction.
//  Details: The builder keeps caller storage terminated, counts complete required
//           output after truncation, and never allocates while appending or formatting.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
String Builder Implementation Notes

Text operations distinguish bounded byte ranges from null-terminated strings. Cursor movement
and conversion validate limits before reading, and failure never relies on ambient locale state.
================
*/

#include "CypherCommon_StringBuilder.h"

namespace cypher::common
{

namespace
{

bool_t IsStringBuilderStatusValid( string_builder_status_t status ) noexcept
{
    switch ( status ) {
        case string_builder_status_t::OK:
        case string_builder_status_t::INVALID_ARGUMENT:
        case string_builder_status_t::OUTPUT_TRUNCATED:
        case string_builder_status_t::FORMAT_ERROR:
            return CY_TRUE;
    }
    return CY_FALSE;
}

bool_t IsStringBuilderTerminal( string_builder_status_t status ) noexcept
{
    return status == string_builder_status_t::INVALID_ARGUMENT ||
           status == string_builder_status_t::FORMAT_ERROR;
}

string_builder_status_t AppendRequiredLength(
    string_builder_t *pBuilder,
    usize cchAdditional ) noexcept
{
    // Required length is maintained even after truncation, enabling an exact
    // measurement pass without a separate formatting implementation.
    if ( cchAdditional > CY_USIZE_MAX - pBuilder->cchRequired ) {
        CY_ASSERT_MSG( CY_FALSE, "StringBuilder required length overflowed." );
        pBuilder->status = string_builder_status_t::FORMAT_ERROR;
        return pBuilder->status;
    }

    pBuilder->cchRequired += cchAdditional;
    return pBuilder->status;
}

} // namespace

bool_t StringBuilder_Init(
    string_builder_t *pBuilder,
    char *pBuffer,
    usize cchCapacity ) noexcept
{
    const bool_t bValidBuilder = pBuilder != nullptr;
    const bool_t bValidStorage = pBuffer != nullptr || cchCapacity == 0u;
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_Init requires a builder object." );
    CY_ASSERT_MSG(
        bValidStorage,
        "StringBuilder_Init requires storage when capacity is nonzero." );
    if ( !bValidBuilder || !bValidStorage ) {
        return CY_FALSE;
    }

    pBuilder->pData = cchCapacity > 0u ? pBuffer : nullptr;
    pBuilder->cchLength = 0u;
    pBuilder->cchCapacity = cchCapacity;
    pBuilder->cchRequired = 0u;
    pBuilder->status = string_builder_status_t::OK;
    if ( cchCapacity > 0u ) {
        pBuffer[0] = '\0';
    }
    return CY_TRUE;
}

void StringBuilder_Clear( string_builder_t *pBuilder ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_Clear requires a valid builder." );
    if ( !bValidBuilder ) {
        return;
    }

    pBuilder->cchLength = 0u;
    pBuilder->cchRequired = 0u;
    pBuilder->status = string_builder_status_t::OK;
    if ( pBuilder->pData != nullptr ) {
        pBuilder->pData[0] = '\0';
    }
}

bool_t StringBuilder_IsValid( const string_builder_t *pBuilder ) noexcept
{
    if ( pBuilder == nullptr ||
         !IsStringBuilderStatusValid( pBuilder->status ) ||
         pBuilder->cchRequired < pBuilder->cchLength ) {
        return CY_FALSE;
    }
    if ( pBuilder->cchCapacity == 0u ) {
        return pBuilder->pData == nullptr && pBuilder->cchLength == 0u;
    }

    return pBuilder->pData != nullptr &&
           pBuilder->cchLength < pBuilder->cchCapacity &&
           pBuilder->pData[pBuilder->cchLength] == '\0';
}

bool_t StringBuilder_WasTruncated( const string_builder_t *pBuilder ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_WasTruncated requires a valid builder." );
    return bValidBuilder &&
           pBuilder->status == string_builder_status_t::OUTPUT_TRUNCATED;
}

usize StringBuilder_Length( const string_builder_t *pBuilder ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_Length requires a valid builder." );
    return bValidBuilder ? pBuilder->cchLength : 0u;
}

usize StringBuilder_Required( const string_builder_t *pBuilder ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_Required requires a valid builder." );
    return bValidBuilder ? pBuilder->cchRequired : 0u;
}

string_builder_status_t StringBuilder_Status(
    const string_builder_t *pBuilder ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_Status requires a valid builder." );
    return bValidBuilder
        ? pBuilder->status
        : string_builder_status_t::INVALID_ARGUMENT;
}

usize StringBuilder_Remaining( const string_builder_t *pBuilder ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_Remaining requires a valid builder." );
    if ( !bValidBuilder || pBuilder->cchCapacity == 0u ) {
        return 0u;
    }
    return pBuilder->cchCapacity - 1u - pBuilder->cchLength;
}

string_view_t StringBuilder_View( const string_builder_t *pBuilder ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_View requires a valid builder." );
    return bValidBuilder
        ? string_view_t{ pBuilder->pData, pBuilder->cchLength }
        : string_view_t{};
}

const char *StringBuilder_CStr( const string_builder_t *pBuilder ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_CStr requires a valid builder." );
    return bValidBuilder && pBuilder->pData != nullptr
        ? pBuilder->pData
        : "";
}

string_builder_status_t StringBuilder_Append(
    string_builder_t *pBuilder,
    string_view_t text ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_Append requires a valid builder." );
    CY_ASSERT_MSG(
        bValidText,
        "StringBuilder_Append requires a valid text view." );
    if ( !bValidBuilder ) {
        return string_builder_status_t::INVALID_ARGUMENT;
    }
    if ( !bValidText ) {
        pBuilder->status = string_builder_status_t::INVALID_ARGUMENT;
        return pBuilder->status;
    }
    if ( IsStringBuilderTerminal( pBuilder->status ) ) {
        return pBuilder->status;
    }
    if ( AppendRequiredLength( pBuilder, text.cchLength ) ==
         string_builder_status_t::FORMAT_ERROR ) {
        return pBuilder->status;
    }

    const usize cchRemaining = StringBuilder_Remaining( pBuilder );
    const usize cchCopy = text.cchLength < cchRemaining
        ? text.cchLength
        : cchRemaining;
    // MemMove permits appending a view that aliases the builder's own storage.
    if ( cchCopy > 0u ) {
        Cy_MemMove(
            pBuilder->pData + pBuilder->cchLength,
            text.pData,
            cchCopy );
        pBuilder->cchLength += cchCopy;
        pBuilder->pData[pBuilder->cchLength] = '\0';
    }

    if ( cchCopy != text.cchLength ) {
        pBuilder->status = string_builder_status_t::OUTPUT_TRUNCATED;
    }
    return pBuilder->status;
}

string_builder_status_t StringBuilder_AppendChar(
    string_builder_t *pBuilder,
    char ch ) noexcept
{
    return StringBuilder_Append(
        pBuilder,
        string_view_t{ &ch, 1u } );
}

string_builder_status_t StringBuilder_AppendRepeat(
    string_builder_t *pBuilder,
    char ch,
    usize cchCount ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_AppendRepeat requires a valid builder." );
    if ( !bValidBuilder ) {
        return string_builder_status_t::INVALID_ARGUMENT;
    }
    if ( IsStringBuilderTerminal( pBuilder->status ) ) {
        return pBuilder->status;
    }
    if ( AppendRequiredLength( pBuilder, cchCount ) ==
         string_builder_status_t::FORMAT_ERROR ) {
        return pBuilder->status;
    }

    const usize cchRemaining = StringBuilder_Remaining( pBuilder );
    const usize cchWrite = cchCount < cchRemaining ? cchCount : cchRemaining;
    if ( cchWrite > 0u ) {
        Cy_MemSet(
            pBuilder->pData + pBuilder->cchLength,
            static_cast<byte>( ch ),
            cchWrite );
        pBuilder->cchLength += cchWrite;
        pBuilder->pData[pBuilder->cchLength] = '\0';
    }

    if ( cchWrite != cchCount ) {
        pBuilder->status = string_builder_status_t::OUTPUT_TRUNCATED;
    }
    return pBuilder->status;
}

string_builder_status_t StringBuilder_AppendFormatV(
    string_builder_t *pBuilder,
    const char *pFormat,
    std::va_list args ) noexcept
{
    const bool_t bValidBuilder = StringBuilder_IsValid( pBuilder );
    const bool_t bValidFormat = pFormat != nullptr;
    CY_ASSERT_MSG(
        bValidBuilder,
        "StringBuilder_AppendFormatV requires a valid builder." );
    CY_ASSERT_MSG(
        bValidFormat,
        "StringBuilder_AppendFormatV requires a format string." );
    if ( !bValidBuilder ) {
        return string_builder_status_t::INVALID_ARGUMENT;
    }
    if ( !bValidFormat ) {
        pBuilder->status = string_builder_status_t::INVALID_ARGUMENT;
        return pBuilder->status;
    }
    if ( IsStringBuilderTerminal( pBuilder->status ) ) {
        return pBuilder->status;
    }

    // Formatting writes directly into the unoccupied tail while preserving the
    // preexisting truncation state and complete required-length accounting.
    const string_builder_status_t previousStatus = pBuilder->status;
    const usize cchPreviousLength = pBuilder->cchLength;
    const usize cchRemaining = StringBuilder_Remaining( pBuilder );
    char *pFormatDest = pBuilder->pData != nullptr
        ? pBuilder->pData + pBuilder->cchLength
        : nullptr;
    const string_format_result_t result = StringFormat_VPrintf(
        pFormatDest,
        pBuilder->pData != nullptr ? cchRemaining + 1u : 0u,
        pFormat,
        args );
    if ( result.status == string_format_status_t::INVALID_ARGUMENT ) {
        pBuilder->status = string_builder_status_t::INVALID_ARGUMENT;
        return pBuilder->status;
    }
    if ( result.status == string_format_status_t::FORMAT_ERROR ) {
        pBuilder->status = string_builder_status_t::FORMAT_ERROR;
        return pBuilder->status;
    }
    if ( AppendRequiredLength( pBuilder, result.cchRequired ) ==
         string_builder_status_t::FORMAT_ERROR ) {
        // Restore the original terminator if required-length arithmetic failed
        // after the formatter had already touched the tail.
        if ( pBuilder->pData != nullptr ) {
            pBuilder->pData[cchPreviousLength] = '\0';
        }
        return pBuilder->status;
    }

    pBuilder->cchLength += result.cchWritten;
    if ( result.status == string_format_status_t::OUTPUT_TRUNCATED ||
         previousStatus == string_builder_status_t::OUTPUT_TRUNCATED ) {
        pBuilder->status = string_builder_status_t::OUTPUT_TRUNCATED;
    } else {
        pBuilder->status = string_builder_status_t::OK;
    }
    return pBuilder->status;
}

string_builder_status_t StringBuilder_AppendFormat(
    string_builder_t *pBuilder,
    const char *pFormat,
    ... ) noexcept
{
    std::va_list args;
    va_start( args, pFormat );
    const string_builder_status_t status =
        StringBuilder_AppendFormatV( pBuilder, pFormat, args );
    va_end( args );
    return status;
}

} // namespace cypher::common
