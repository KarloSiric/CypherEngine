//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueWriter.cpp
//  Purpose: Implements deterministic native CYKV text output.
//  Details: One bounded emitter supports buffers and callback sinks. Canonical mode
//           sorts object keys without mutating the source document.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Key Value Writer Implementation Notes

Writes CYKV data deterministically into caller-controlled output. Escaping, indentation, and key
order must not depend on pointer values, locale, or process state.
================
*/

#include "CypherCommon_KeyValueWriterInternal.h"

#include "CypherCommon_KeyValueInternal.h"
#include "CypherCommon_HashXXH.h"
#include "CypherCommon_Sort.h"
#include "CypherCommon_StringConvert.h"
#include "CypherCommon_StringEscape.h"
#include "CypherCommon_StringParse.h"
#include "CypherCommon_Unicode.h"

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_KEY_VALUE_WRITE_VALID_FLAGS =
    KEY_VALUE_WRITE_FLAG_PRETTY |
    KEY_VALUE_WRITE_FLAG_CANONICAL |
    KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE |
    KEY_VALUE_WRITE_FLAG_ASCII_ONLY |
    KEY_VALUE_WRITE_FLAG_BARE_KEYS |
    KEY_VALUE_WRITE_FLAG_SHORTEST_REALS |
    KEY_VALUE_WRITE_FLAG_QUOTE_DOTTED_KEYS; // Reject unknown output policy bits.

constexpr flags32_t CY_KEY_VALUE_ESCAPE_FLAGS =
    STRING_ESCAPE_FLAG_QUOTES |
    STRING_ESCAPE_FLAG_BACKSLASH |
    STRING_ESCAPE_FLAG_CONTROL_CHARS; // Minimum escaping needed for quoted CYKV/JSON text.

struct writer_t {
    key_value_write_fn_t pfnWrite{ nullptr }; // Caller sink receiving serialized chunks.
    void *pUserData{ nullptr };               // Opaque context forwarded to pfnWrite.
    key_value_write_options_t options{};      // Formatting policy and recursion limit.
    key_value_write_result_t result{};        // Sticky first failure and output counters.
    const allocator_t *pAllocator{ nullptr }; // Temporary escaping/sorting allocations.
    bool_t bStrictJson{ CY_FALSE };            // Select JSON syntax instead of CYKV syntax.
    bool_t bPretty{ CY_FALSE };                // Emit indentation and line breaks.
    bool_t bCanonical{ CY_FALSE };             // Sort keys and suppress optional whitespace.
    // Compact layout (pretty CYKV only).
    bool_t bBareKeys{ CY_FALSE };              // Unquoted keys where the grammar allows.
    bool_t bQuoteDottedKeys{ CY_FALSE };       // ...except keys holding a '.'.
    bool_t bShortestReals{ CY_FALSE };         // Fewest digits that round-trip.
    usize nLineWidth{ 0u };                    // One-line and packed containers; 0 = off.
    bool_t bInline{ CY_FALSE };                // Inside a container being written on one line.
    bool_t bForceExpand{ CY_FALSE };           // The next container spreads over lines (its siblings did not fit).
    usize column{ 0u };                        // Characters since the last line break.
};

struct buffer_sink_t {
    char *pDest{ nullptr };   // Optional caller buffer for write or measure mode.
    usize cchCapacity{ 0u };  // Characters available after reserving the terminator.
    usize cchWritten{ 0u };   // Prefix physically copied into pDest.
};

struct canonical_child_t {
    const key_value_t *pValue{ nullptr }; // Borrowed child selected for output.
    usize iOriginal{ 0u };                // Stable tie-breaker for duplicate-name corruption.
};

struct canonical_hash_sink_t {
    hash_xxh3_stream_t stream{}; // Incremental XXH3-128 canonical byte stream.
};

struct canonical_child_less_t {
    CYPHER_NODISCARD bool_t operator()(
        const canonical_child_t &left,
        const canonical_child_t &right ) const noexcept
    {
        const i32 comparison = StringView_Compare(
            { left.pValue->pName, left.pValue->cchName },
            { right.pValue->pName, right.pValue->cchName } );
        return comparison < 0 ||
               ( comparison == 0 && left.iOriginal < right.iOriginal );
    }
};

CYPHER_NODISCARD bool_t BufferSink(
    string_view_t text,
    void *pUserData ) noexcept
{
    auto &sink = *static_cast<buffer_sink_t *>( pUserData );
    const usize cchAvailable = sink.cchCapacity - sink.cchWritten;
    const usize cchCopy = text.cchLength < cchAvailable
        ? text.cchLength
        : cchAvailable;
    if ( cchCopy != 0u ) {
        Cy_MemCopy(
            sink.pDest + sink.cchWritten,
            text.pData,
            cchCopy );
        sink.cchWritten += cchCopy;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t CanonicalHashSink(
    string_view_t text,
    void *pUserData ) noexcept
{
    if ( pUserData == nullptr || !StringView_IsValid( text ) ) {
        return CY_FALSE;
    }
    auto &sink = *static_cast<canonical_hash_sink_t *>( pUserData );
    return HashXXH3_StreamUpdate(
        &sink.stream,
        BinaryBlock_FromData( text.pData, text.cchLength ) );
}

void Fail( writer_t &writer, key_value_write_status_t status ) noexcept
{
    if ( writer.result.status == key_value_write_status_t::OK ) {
        writer.result.status = status;
    }
}

CYPHER_NODISCARD bool_t Emit(
    writer_t &writer,
    const char *pText,
    usize cchText ) noexcept
{
    // Required size advances even when a bounded buffer sink stores only a prefix.
    if ( writer.result.status != key_value_write_status_t::OK ) {
        return CY_FALSE;
    }
    if ( pText == nullptr && cchText != 0u ) {
        Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
        return CY_FALSE;
    }
    if ( cchText > CY_USIZE_MAX - writer.result.cchRequired ) {
        writer.result.cchRequired = CY_USIZE_MAX;
        Fail( writer, key_value_write_status_t::SIZE_OVERFLOW );
        return CY_FALSE;
    }
    writer.result.cchRequired += cchText;
    if ( cchText != 0u &&
         !writer.pfnWrite( { pText, cchText }, writer.pUserData ) ) {
        Fail( writer, key_value_write_status_t::SINK_FAILED );
        return CY_FALSE;
    }
    writer.result.cchWritten += cchText;
    // The column decides whether the next container fits on its line.
    usize iLastBreak = cchText;
    for ( usize i = cchText; i != 0u; --i ) {
        if ( pText[i - 1u] == '\n' ) {
            iLastBreak = i - 1u;
            break;
        }
    }
    writer.column = iLastBreak == cchText ? writer.column + cchText : cchText - iLastBreak - 1u;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t EmitLiteral(
    writer_t &writer,
    const char *pText ) noexcept
{
    return Emit(
        writer,
        pText,
        StringView_FromCString( pText ).cchLength );
}

CYPHER_NODISCARD bool_t EmitIndent(
    writer_t &writer,
    usize nDepth ) noexcept
{
    if ( !writer.bPretty ) return CY_TRUE;
    constexpr char spaces[] =
        "                                                                ";
    usize nRemaining = nDepth * static_cast<usize>( writer.options.nIndentSpaces );
    while ( nRemaining != 0u ) {
        const usize cchChunk = nRemaining < sizeof( spaces ) - 1u
            ? nRemaining
            : sizeof( spaces ) - 1u;
        if ( !Emit( writer, spaces, cchChunk ) ) return CY_FALSE;
        nRemaining -= cchChunk;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t EmitEscapedString(
    writer_t &writer,
    string_view_t text ) noexcept
{
    bool_t bValidText = StringView_IsValid( text ) &&
        Unicode_ValidateUtf8( text ).status == unicode_status_t::OK;
    if ( bValidText && !writer.bStrictJson ) {
        for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
            if ( text.pData[iByte] == '\0' ) {
                bValidText = CY_FALSE;
                break;
            }
        }
    }
    if ( !bValidText || !EmitLiteral( writer, "\"" ) ) {
        if ( !bValidText ) {
            Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
        }
        return CY_FALSE;
    }
    flags32_t flags = CY_KEY_VALUE_ESCAPE_FLAGS;
    if ( ( writer.options.flags & KEY_VALUE_WRITE_FLAG_ASCII_ONLY ) != 0u ) {
        flags |= STRING_ESCAPE_FLAG_NON_ASCII;
    }
    const string_escape_style_t style = string_escape_style_t::JSON;
    // Measure then encode into exact temporary storage; sinks never see partial escapes.
    const string_escape_result_t measured = StringEscape_Encode(
        text,
        style,
        flags,
        nullptr,
        0u );
    if ( measured.status != string_escape_status_t::OUTPUT_TRUNCATED &&
         measured.status != string_escape_status_t::OK ) {
        Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
        return CY_FALSE;
    }
    if ( measured.cchRequired == CY_USIZE_MAX ) {
        Fail( writer, key_value_write_status_t::SIZE_OVERFLOW );
        return CY_FALSE;
    }
    char *pEncoded = static_cast<char *>( Allocator_Allocate(
        writer.pAllocator,
        measured.cchRequired + 1u,
        alignof( char ) ) );
    if ( pEncoded == nullptr ) {
        Fail( writer, key_value_write_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    const string_escape_result_t encoded = StringEscape_Encode(
        text,
        style,
        flags,
        pEncoded,
        measured.cchRequired + 1u );
    const bool_t bValid = encoded.status == string_escape_status_t::OK;
    const bool_t bWritten = bValid && Emit(
        writer,
        pEncoded,
        encoded.cchWritten );
    Allocator_Free(
        writer.pAllocator,
        pEncoded,
        measured.cchRequired + 1u,
        alignof( char ) );
    if ( !bValid ) {
        Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
        return CY_FALSE;
    }
    return bWritten && EmitLiteral( writer, "\"" );
}

// Scalars are formatted once into a small buffer so the compact layout can
// measure exactly what will be written.
struct scalar_text_t {
    char text[128]{};
    usize cch{ 0u };
};

CYPHER_NODISCARD bool_t FormatI64( i64 value, scalar_text_t &out ) noexcept
{
    string_integer_format_t format{};
    const string_convert_result_t converted = StringConvert_I64( value, format, out.text, sizeof( out.text ) );
    out.cch = converted.cchWritten;
    return converted.status == string_convert_status_t::OK;
}

CYPHER_NODISCARD bool_t FormatU64( const writer_t &writer, u64 value, scalar_text_t &out ) noexcept
{
    const string_convert_result_t converted = StringConvert_U64( value, {}, out.text, sizeof( out.text ) - 1u );
    if ( converted.status != string_convert_status_t::OK ) { return CY_FALSE; }
    out.cch = converted.cchWritten;
    if ( !writer.bStrictJson ) { out.text[out.cch++] = 'u'; }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t FormatF64( const writer_t &writer, f64 value, scalar_text_t &out ) noexcept
{
    const auto format = [&]( u8 nDigits ) noexcept {
        const string_convert_result_t converted = StringConvert_F64(
            value,
            { string_float_style_t::GENERAL, nDigits, STRING_FLOAT_FORMAT_FLAG_TRIM_TRAILING_ZERO },
            out.text,
            sizeof( out.text ) - 2u );
        out.cch = converted.cchWritten;
        return converted.status == string_convert_status_t::OK;
    };
    bool_t bFormatted = CY_FALSE;
    if ( writer.bShortestReals ) {
        // 17 significant digits always round-trip; fewer usually do and read
        // far better. The reader's own parser decides what "round-trips".
        for ( u8 nDigits = 15u; nDigits < 17u && !bFormatted; ++nDigits ) {
            f64 back = 0.0;
            bFormatted = format( nDigits ) &&
                         StringParse_Succeeded( StringParse_F64( { out.text, out.cch }, STRING_PARSE_FLAG_ALLOW_PLUS_SIGN, &back ) ) &&
                         Cy_MemCompare( &back, &value, sizeof( f64 ) ) == 0;
        }
    }
    if ( !bFormatted && !format( 17u ) ) { return CY_FALSE; }
    if ( writer.bStrictJson ) { return CY_TRUE; }
    for ( usize iByte = 0u; iByte < out.cch; ++iByte ) {
        if ( out.text[iByte] == '.' || out.text[iByte] == 'e' || out.text[iByte] == 'E' ) { return CY_TRUE; }
    }
    // A real is always spelled as one so it reads back as F64, not I64.
    out.text[out.cch++] = '.';
    out.text[out.cch++] = '0';
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t EmitI64(
    writer_t &writer,
    i64 value ) noexcept
{
    scalar_text_t text{};
    return FormatI64( value, text )
        ? Emit( writer, text.text, text.cch )
        : ( Fail( writer, key_value_write_status_t::INVALID_DOCUMENT ), CY_FALSE );
}

CYPHER_NODISCARD bool_t EmitU64(
    writer_t &writer,
    u64 value ) noexcept
{
    scalar_text_t text{};
    return FormatU64( writer, value, text )
        ? Emit( writer, text.text, text.cch )
        : ( Fail( writer, key_value_write_status_t::INVALID_DOCUMENT ), CY_FALSE );
}

CYPHER_NODISCARD bool_t EmitF64(
    writer_t &writer,
    f64 value ) noexcept
{
    scalar_text_t text{};
    return FormatF64( writer, value, text )
        ? Emit( writer, text.text, text.cch )
        : ( Fail( writer, key_value_write_status_t::INVALID_DOCUMENT ), CY_FALSE );
}

CYPHER_NODISCARD bool_t EmitBinary(
    writer_t &writer,
    binary_block_t value ) noexcept
{
    if ( writer.bStrictJson || !BinaryBlock_IsValid( value ) ||
         !EmitLiteral( writer, "hex\"" ) ) {
        Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
        return CY_FALSE;
    }
    // Encode bounded chunks to avoid allocating a second full-size binary representation.
    constexpr char digits[] = "0123456789abcdef";
    char encoded[512]{};
    usize iByte = 0u;
    while ( iByte < value.cbSize ) {
        usize nBytes = value.cbSize - iByte;
        if ( nBytes > sizeof( encoded ) / 2u ) {
            nBytes = sizeof( encoded ) / 2u;
        }
        for ( usize i = 0u; i < nBytes; ++i ) {
            const byte current = value.pData[iByte + i];
            encoded[i * 2u] = digits[current >> 4u];
            encoded[i * 2u + 1u] = digits[current & 0x0Fu];
        }
        if ( !Emit( writer, encoded, nBytes * 2u ) ) return CY_FALSE;
        iByte += nBytes;
    }
    return EmitLiteral( writer, "\"" );
}

CYPHER_NODISCARD bool_t WriteValue(
    writer_t &writer,
    const key_value_t *pValue,
    usize nDepth ) noexcept;

// CYKV bare-key grammar: ASCII letter or '_', then letters, digits, '_',
// '.', '-'. Words the reader treats as literals stay quoted.
CYPHER_NODISCARD bool_t IsBareKey( string_view_t name ) noexcept
{
    if ( name.cchLength == 0u ) { return CY_FALSE; }
    const auto isLetter = []( char c ) noexcept { return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ); };
    if ( !isLetter( name.pData[0] ) && name.pData[0] != '_' ) { return CY_FALSE; }
    for ( usize i = 1u; i < name.cchLength; ++i ) {
        const char c = name.pData[i];
        if ( !isLetter( c ) && !( c >= '0' && c <= '9' ) && c != '_' && c != '.' && c != '-' ) { return CY_FALSE; }
    }
    for ( const char *pReserved : { "true", "false", "null", "hex", "inf", "nan", "infinity" } ) {
        if ( StringView_Equals( name, StringView_FromCString( pReserved ) ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t WritesBareKey( const writer_t &writer, string_view_t name ) noexcept
{
    if ( !writer.bBareKeys || writer.bStrictJson || !IsBareKey( name ) ) { return CY_FALSE; }
    if ( writer.bQuoteDottedKeys ) {
        for ( usize i = 0u; i < name.cchLength; ++i ) {
            if ( name.pData[i] == '.' ) { return CY_FALSE; }
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t EmitKey( writer_t &writer, string_view_t name ) noexcept
{
    if ( WritesBareKey( writer, name ) ) { return Emit( writer, name.pData, name.cchLength ); }
    return EmitEscapedString( writer, name );
}

CYPHER_NODISCARD usize EscapedLength( const writer_t &writer, string_view_t text ) noexcept
{
    flags32_t flags = CY_KEY_VALUE_ESCAPE_FLAGS;
    if ( ( writer.options.flags & KEY_VALUE_WRITE_FLAG_ASCII_ONLY ) != 0u ) { flags |= STRING_ESCAPE_FLAG_NON_ASCII; }
    const string_escape_result_t measured = StringEscape_Encode( text, string_escape_style_t::JSON, flags, nullptr, 0u );
    return measured.cchRequired == CY_USIZE_MAX ? CY_USIZE_MAX / 2u : measured.cchRequired + 2u;
}

// Length of a value's one-line form, or more than nCap once it is known to
// exceed it (so measuring a huge container stops early).
CYPHER_NODISCARD usize FlatLength( const writer_t &writer, const key_value_t *pValue, usize nCap ) noexcept
{
    scalar_text_t text{};
    switch ( pValue->type ) {
        case key_value_type_t::NULL_VALUE: return 4u;
        case key_value_type_t::BOOL: return pValue->value.bValue ? 4u : 5u;
        case key_value_type_t::I64: return FormatI64( pValue->value.iValue, text ) ? text.cch : nCap + 1u;
        case key_value_type_t::U64: return FormatU64( writer, pValue->value.uValue, text ) ? text.cch : nCap + 1u;
        case key_value_type_t::F64: return FormatF64( writer, pValue->value.flValue, text ) ? text.cch : nCap + 1u;
        case key_value_type_t::STRING:
            return EscapedLength( writer, { reinterpret_cast<const char *>( pValue->value.bytes.pData ), pValue->value.bytes.cbSize } );
        case key_value_type_t::BINARY: return 5u + 2u * pValue->value.bytes.cbSize;
        case key_value_type_t::ARRAY:
        case key_value_type_t::OBJECT: {
            if ( pValue->nChildren == 0u ) { return 2u; }
            const bool_t bObject = pValue->type == key_value_type_t::OBJECT;
            usize cch = 4u; // "[ " and " ]", or "{ " and " }".
            usize i = 0u;
            for ( const key_value_t *pChild = pValue->pFirstChild; pChild != nullptr && cch <= nCap; pChild = pChild->pNext, ++i ) {
                if ( i != 0u ) { cch += bObject ? 1u : 2u; } // " " between members, ", " between elements.
                if ( bObject ) {
                    const string_view_t name{ pChild->pName, pChild->cchName };
                    cch += ( WritesBareKey( writer, name ) ? name.cchLength : EscapedLength( writer, name ) ) + 3u;
                }
                cch += FlatLength( writer, pChild, nCap - ( cch < nCap ? cch : nCap ) );
            }
            return cch;
        }
    }
    return nCap + 1u;
}

CYPHER_NODISCARD bool_t IsScalar( const key_value_t *pValue ) noexcept
{
    return pValue->type != key_value_type_t::OBJECT && pValue->type != key_value_type_t::ARRAY;
}

CYPHER_NODISCARD bool_t UseCompactLayout( const writer_t &writer ) noexcept
{
    return writer.bPretty && !writer.bStrictJson && writer.nLineWidth != 0u;
}

// Whether a container starting at the current column fits on the line,
// leaving room for a following comma.
CYPHER_NODISCARD bool_t FitsOnLine( const writer_t &writer, const key_value_t *pContainer ) noexcept
{
    if ( writer.column + 1u >= writer.nLineWidth ) { return CY_FALSE; }
    const usize nBudget = writer.nLineWidth - writer.column - 1u;
    return FlatLength( writer, pContainer, nBudget ) <= nBudget;
}

CYPHER_NODISCARD bool_t WriteFlatContainer( writer_t &writer, const key_value_t *pContainer, usize nDepth ) noexcept
{
    const bool_t bObject = pContainer->type == key_value_type_t::OBJECT;
    if ( pContainer->nChildren == 0u ) { return EmitLiteral( writer, bObject ? "{}" : "[]" ); }
    const bool_t bWasInline = writer.bInline;
    writer.bInline = CY_TRUE;
    bool_t bSuccess = EmitLiteral( writer, bObject ? "{ " : "[ " );
    usize i = 0u;
    for ( const key_value_t *pChild = pContainer->pFirstChild; bSuccess && pChild != nullptr; pChild = pChild->pNext, ++i ) {
        if ( i != 0u ) { bSuccess = EmitLiteral( writer, bObject ? " " : ", " ); }
        if ( bSuccess && bObject ) {
            bSuccess = EmitKey( writer, { pChild->pName, pChild->cchName } ) && EmitLiteral( writer, " = " );
        }
        bSuccess = bSuccess && WriteValue( writer, pChild, nDepth + 1u );
    }
    writer.bInline = bWasInline;
    return bSuccess && EmitLiteral( writer, bObject ? " }" : " ]" );
}

CYPHER_NODISCARD canonical_child_t *BuildCanonicalOrder(
    writer_t &writer,
    const key_value_t *pObject ) noexcept
{
    if ( pObject->nChildren == 0u ) return nullptr;
    canonical_child_t *pChildren =
        Allocator_AllocateArrayStorage<canonical_child_t>(
        writer.pAllocator,
        pObject->nChildren );
    if ( pChildren == nullptr ) {
        Fail( writer, key_value_write_status_t::OUT_OF_MEMORY );
        return nullptr;
    }
    // Sort an auxiliary pointer array; never mutate document insertion order.
    usize iChild = 0u;
    for ( const key_value_t *pChild = pObject->pFirstChild;
          pChild != nullptr;
          pChild = pChild->pNext ) {
        pChildren[iChild] = { pChild, iChild };
        ++iChild;
    }
    if ( iChild != pObject->nChildren ) {
        Allocator_FreeArrayStorage(
            writer.pAllocator,
            pChildren,
            pObject->nChildren );
        Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
        return nullptr;
    }
    Sort_Unstable(
        span_t<canonical_child_t>{ pChildren, pObject->nChildren },
        canonical_child_less_t{} );
    return pChildren;
}

CYPHER_NODISCARD bool_t WriteObject(
    writer_t &writer,
    const key_value_t *pObject,
    usize nDepth ) noexcept
{
    if ( nDepth > writer.options.nMaxDepth ) {
        Fail( writer, key_value_write_status_t::DEPTH_LIMIT );
        return CY_FALSE;
    }
    // Canonical mode supplies deterministic object order for hashing and cooked output.
    const bool_t bForceExpand = writer.bForceExpand;
    writer.bForceExpand = CY_FALSE;
    // The document root always spreads over lines; one-line roots read badly.
    if ( UseCompactLayout( writer ) && nDepth != 0u && ( writer.bInline || ( !bForceExpand && FitsOnLine( writer, pObject ) ) ) ) {
        return WriteFlatContainer( writer, pObject, nDepth );
    }
    canonical_child_t *pCanonical = writer.bCanonical
        ? BuildCanonicalOrder( writer, pObject )
        : nullptr;
    if ( writer.bCanonical && pObject->nChildren != 0u &&
         pCanonical == nullptr ) return CY_FALSE;

    bool_t bSuccess = EmitLiteral( writer, "{" );
    if ( bSuccess && pObject->nChildren != 0u && writer.bPretty ) {
        bSuccess = EmitLiteral( writer, "\n" );
    }
    for ( usize i = 0u; bSuccess && i < pObject->nChildren; ++i ) {
        const key_value_t *pChild = pCanonical != nullptr
            ? pCanonical[i].pValue
            : KeyValue_ChildAt( pObject, i );
        bSuccess = pChild != nullptr &&
                   EmitIndent( writer, nDepth + 1u ) &&
                   EmitKey(
                       writer,
                       { pChild->pName, pChild->cchName } ) &&
                   EmitLiteral(
                       writer,
                       writer.bStrictJson
                           ? ( writer.bPretty ? ": " : ":" )
                           : ( writer.bPretty ? " = " : "=" ) ) &&
                   WriteValue( writer, pChild, nDepth + 1u );
        if ( bSuccess && i + 1u < pObject->nChildren ) {
            if ( writer.bStrictJson ) {
                bSuccess = EmitLiteral( writer, "," );
            } else if ( !writer.bPretty ) {
                bSuccess = EmitLiteral( writer, " " );
            }
        }
        if ( bSuccess && writer.bPretty ) {
            bSuccess = EmitLiteral( writer, "\n" );
        }
    }
    if ( bSuccess && pObject->nChildren != 0u ) {
        bSuccess = EmitIndent( writer, nDepth );
    }
    if ( bSuccess ) bSuccess = EmitLiteral( writer, "}" );

    if ( pCanonical != nullptr ) {
        Allocator_FreeArrayStorage(
            writer.pAllocator,
            pCanonical,
            pObject->nChildren );
    }
    return bSuccess;
}

CYPHER_NODISCARD bool_t WriteArray(
    writer_t &writer,
    const key_value_t *pArray,
    usize nDepth ) noexcept
{
    if ( nDepth > writer.options.nMaxDepth ) {
        Fail( writer, key_value_write_status_t::DEPTH_LIMIT );
        return CY_FALSE;
    }
    const bool_t bForceExpand = writer.bForceExpand;
    writer.bForceExpand = CY_FALSE;
    if ( UseCompactLayout( writer ) ) {
        if ( writer.bInline || ( !bForceExpand && FitsOnLine( writer, pArray ) ) ) { return WriteFlatContainer( writer, pArray, nDepth ); }
        bool_t bAllContainers = pArray->nChildren != 0u;
        for ( const key_value_t *pChild = pArray->pFirstChild; bAllContainers && pChild != nullptr; pChild = pChild->pNext ) {
            bAllContainers = !IsScalar( pChild );
        }
        if ( bAllContainers ) {
            // Siblings share one layout - every record on one line, or every
            // record spread - so a list of similar records (brush faces,
            // outputs) reads uniformly instead of alternating by a character.
            const usize childColumn = ( nDepth + 1u ) * writer.options.nIndentSpaces;
            const usize nBudget = childColumn + 1u < writer.nLineWidth ? writer.nLineWidth - childColumn - 1u : 0u;
            bool_t bAllFit = CY_TRUE;
            for ( const key_value_t *pChild = pArray->pFirstChild; bAllFit && pChild != nullptr; pChild = pChild->pNext ) {
                bAllFit = FlatLength( writer, pChild, nBudget ) <= nBudget;
            }
            bool_t bWritten = EmitLiteral( writer, "[\n" );
            for ( const key_value_t *pChild = pArray->pFirstChild; bWritten && pChild != nullptr; pChild = pChild->pNext ) {
                bWritten = EmitIndent( writer, nDepth + 1u );
                if ( bWritten && bAllFit ) {
                    bWritten = WriteFlatContainer( writer, pChild, nDepth + 1u );
                } else if ( bWritten ) {
                    writer.bForceExpand = CY_TRUE;
                    bWritten = WriteValue( writer, pChild, nDepth + 1u );
                }
                if ( bWritten && pChild->pNext != nullptr ) { bWritten = EmitLiteral( writer, "," ); }
                if ( bWritten ) { bWritten = EmitLiteral( writer, "\n" ); }
            }
            return bWritten && EmitIndent( writer, nDepth ) && EmitLiteral( writer, "]" );
        }
        bool_t bAllScalars = pArray->nChildren != 0u;
        for ( const key_value_t *pChild = pArray->pFirstChild; bAllScalars && pChild != nullptr; pChild = pChild->pNext ) {
            bAllScalars = IsScalar( pChild );
        }
        if ( bAllScalars ) {
            // Packed: as many scalars per line as fit, lines ending in commas.
            bool_t bPacked = EmitLiteral( writer, "[\n" ) && EmitIndent( writer, nDepth + 1u );
            usize i = 0u;
            for ( const key_value_t *pChild = pArray->pFirstChild; bPacked && pChild != nullptr; pChild = pChild->pNext, ++i ) {
                if ( i != 0u ) {
                    const usize cchNext = 2u + FlatLength( writer, pChild, writer.nLineWidth ) + ( pChild->pNext != nullptr ? 1u : 0u );
                    bPacked = writer.column + cchNext <= writer.nLineWidth
                        ? EmitLiteral( writer, ", " )
                        : EmitLiteral( writer, ",\n" ) && EmitIndent( writer, nDepth + 1u );
                }
                bPacked = bPacked && WriteValue( writer, pChild, nDepth + 1u );
            }
            return bPacked && EmitLiteral( writer, "\n" ) && EmitIndent( writer, nDepth ) && EmitLiteral( writer, "]" );
        }
    }
    bool_t bSuccess = EmitLiteral( writer, "[" );
    if ( bSuccess && pArray->nChildren != 0u && writer.bPretty ) {
        bSuccess = EmitLiteral( writer, "\n" );
    }
    for ( usize i = 0u; bSuccess && i < pArray->nChildren; ++i ) {
        const key_value_t *pChild = KeyValue_ChildAt( pArray, i );
        bSuccess = pChild != nullptr &&
                   EmitIndent( writer, nDepth + 1u ) &&
                   WriteValue( writer, pChild, nDepth + 1u );
        if ( bSuccess && i + 1u < pArray->nChildren ) {
            bSuccess = EmitLiteral( writer, "," );
        }
        if ( bSuccess && writer.bPretty ) {
            bSuccess = EmitLiteral( writer, "\n" );
        }
    }
    if ( bSuccess && pArray->nChildren != 0u ) {
        bSuccess = EmitIndent( writer, nDepth );
    }
    return bSuccess && EmitLiteral( writer, "]" );
}

bool_t WriteValue(
    writer_t &writer,
    const key_value_t *pValue,
    usize nDepth ) noexcept
{
    // The type tag is the sole dispatch key; malformed tags fail closed.
    if ( pValue == nullptr || nDepth > writer.options.nMaxDepth ) {
        Fail(
            writer,
            pValue == nullptr
                ? key_value_write_status_t::INVALID_DOCUMENT
                : key_value_write_status_t::DEPTH_LIMIT );
        return CY_FALSE;
    }
    switch ( pValue->type ) {
        case key_value_type_t::NULL_VALUE:
            return EmitLiteral( writer, "null" );
        case key_value_type_t::BOOL:
            return EmitLiteral( writer, pValue->value.bValue ? "true" : "false" );
        case key_value_type_t::I64:
            return EmitI64( writer, pValue->value.iValue );
        case key_value_type_t::U64:
            return EmitU64( writer, pValue->value.uValue );
        case key_value_type_t::F64:
            return EmitF64( writer, pValue->value.flValue );
        case key_value_type_t::STRING:
            return EmitEscapedString(
                writer,
                {
                    reinterpret_cast<const char *>( pValue->value.bytes.pData ),
                    pValue->value.bytes.cbSize
                } );
        case key_value_type_t::BINARY:
            return EmitBinary(
                writer,
                { pValue->value.bytes.pData, pValue->value.bytes.cbSize } );
        case key_value_type_t::OBJECT:
            return WriteObject( writer, pValue, nDepth );
        case key_value_type_t::ARRAY:
            return WriteArray( writer, pValue, nDepth );
    }
    Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t WriteDocumentHeader(
    writer_t &writer,
    const key_value_document_t *pDocument ) noexcept
{
    const key_value_document_header_t header =
        KeyValue_DocumentHeader( pDocument );
    if ( ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION_1 &&
           header.nLanguageVersion != CYKV_LANGUAGE_VERSION_2 ) ||
         header.nSchemaVersion == 0u ||
         !StringView_IsValid( header.schemaId ) ||
         header.schemaId.cchLength == 0u ) {
        Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
        return CY_FALSE;
    }

    char languageVersion[16]{};
    const string_convert_result_t languageConverted = StringConvert_U64(
        header.nLanguageVersion,
        {},
        languageVersion,
        sizeof( languageVersion ) );
    char schemaVersion[16]{};
    const string_convert_result_t schemaConverted = StringConvert_U64(
        header.nSchemaVersion,
        {},
        schemaVersion,
        sizeof( schemaVersion ) );
    if ( languageConverted.status != string_convert_status_t::OK ||
         schemaConverted.status != string_convert_status_t::OK ) {
        Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
        return CY_FALSE;
    }

    // Keep the required two-line header byte-stable across hosts.
    return EmitLiteral( writer, "@cykv " ) &&
           Emit(
               writer,
               languageVersion,
               languageConverted.cchWritten ) &&
           EmitLiteral( writer, "\n@schema " ) &&
           EmitEscapedString( writer, header.schemaId ) &&
           EmitLiteral( writer, " " ) &&
           Emit( writer, schemaVersion, schemaConverted.cchWritten ) &&
           EmitLiteral( writer, "\n" ) &&
           ( !writer.bPretty || EmitLiteral( writer, "\n" ) );
}

CYPHER_NODISCARD key_value_write_result_t WriteToSink(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    key_value_write_fn_t pfnWrite,
    void *pUserData,
    bool_t bStrictJson,
    bool_t bValueOnly = CY_FALSE ) noexcept
{
    key_value_write_result_t invalid{};
    if ( pfnWrite == nullptr ||
         ( options.flags & ~CY_KEY_VALUE_WRITE_VALID_FLAGS ) != 0u ||
         options.nMaxDepth == 0u ||
         options.nMaxDepth > CY_KEY_VALUE_MAX_DEPTH ||
         !KeyValue_InternalTreeIsValid( pRoot ) ) {
        invalid.status = pRoot == nullptr || pfnWrite == nullptr ||
                         ( options.flags & ~CY_KEY_VALUE_WRITE_VALID_FLAGS ) != 0u ||
                         options.nMaxDepth == 0u ||
                         options.nMaxDepth > CY_KEY_VALUE_MAX_DEPTH
            ? key_value_write_status_t::INVALID_ARGUMENT
            : key_value_write_status_t::INVALID_DOCUMENT;
        return invalid;
    }
    if ( !bStrictJson && !bValueOnly && KeyValue_Type( pRoot ) != key_value_type_t::OBJECT ) {
        invalid.status = key_value_write_status_t::INVALID_DOCUMENT;
        return invalid;
    }
    writer_t writer{};
    writer.pfnWrite = pfnWrite;
    writer.pUserData = pUserData;
    writer.options = options;
    writer.pAllocator = pRoot->pDocument->pAllocator;
    writer.bStrictJson = bStrictJson;
    writer.bCanonical =
        ( options.flags & KEY_VALUE_WRITE_FLAG_CANONICAL ) != 0u;
    // Canonical output deliberately overrides pretty formatting.
    writer.bPretty = !writer.bCanonical &&
        ( options.flags & KEY_VALUE_WRITE_FLAG_PRETTY ) != 0u;
    // Canonical output is one fixed spelling for hashing; layout options
    // never change it.
    writer.bBareKeys = !writer.bCanonical && ( options.flags & KEY_VALUE_WRITE_FLAG_BARE_KEYS ) != 0u;
    writer.bQuoteDottedKeys = ( options.flags & KEY_VALUE_WRITE_FLAG_QUOTE_DOTTED_KEYS ) != 0u;
    writer.bShortestReals = !writer.bCanonical && ( options.flags & KEY_VALUE_WRITE_FLAG_SHORTEST_REALS ) != 0u;
    writer.nLineWidth = writer.bPretty ? options.nLineWidth : 0u;

    if ( !writer.bStrictJson && !bValueOnly ) {
        static_cast<void>( WriteDocumentHeader(
            writer,
            pRoot->pDocument ) );
    }
    if ( writer.result.status == key_value_write_status_t::OK ) {
        static_cast<void>( WriteValue( writer, pRoot, 0u ) );
    }
    if ( writer.result.status == key_value_write_status_t::OK &&
         !writer.bCanonical &&
         ( options.flags & KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE ) != 0u ) {
        static_cast<void>( EmitLiteral( writer, "\n" ) );
    }
    return writer.result;
}

CYPHER_NODISCARD key_value_write_result_t WriteToBuffer(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    char *pDest,
    usize cchDest,
    bool_t bStrictJson,
    bool_t bValueOnly = CY_FALSE ) noexcept
{
    if ( pDest == nullptr && cchDest != 0u ) {
        return { key_value_write_status_t::INVALID_ARGUMENT, 0u, 0u };
    }
    // Reserve one byte for NUL while still measuring the complete serialized length.
    buffer_sink_t sink{
        pDest,
        cchDest > 0u ? cchDest - 1u : 0u,
        0u
    };
    key_value_write_result_t result = WriteToSink(
        pRoot,
        options,
        BufferSink,
        &sink,
        bStrictJson,
        bValueOnly );
    if ( pDest != nullptr && cchDest != 0u ) {
        pDest[sink.cchWritten] = '\0';
    }
    result.cchWritten = sink.cchWritten;
    if ( result.status == key_value_write_status_t::OK &&
         result.cchWritten != result.cchRequired ) {
        result.status = key_value_write_status_t::OUTPUT_TRUNCATED;
    }
    return result;
}

} // namespace

key_value_write_result_t KeyValue_InternalWriteText(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    char *pDest,
    usize cchDest,
    bool_t bStrictJson ) noexcept
{
    return WriteToBuffer( pRoot, options, pDest, cchDest, bStrictJson );
}

key_value_write_result_t KeyValue_InternalWriteTextToSink(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    key_value_write_fn_t pfnWrite,
    void *pUserData,
    bool_t bStrictJson ) noexcept
{
    return WriteToSink(
        pRoot,
        options,
        pfnWrite,
        pUserData,
        bStrictJson );
}

key_value_write_result_t KeyValue_WriteText(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    char *pDest,
    usize cchDest ) noexcept
{
    return KeyValue_InternalWriteText(
        pRoot,
        options,
        pDest,
        cchDest,
        CY_FALSE );
}

key_value_write_result_t KeyValue_WriteValueText(
    const key_value_t *pValue,
    const key_value_write_options_t &options,
    char *pDest,
    usize cchDest ) noexcept
{
    return WriteToBuffer( pValue, options, pDest, cchDest, CY_FALSE, CY_TRUE );
}

key_value_write_result_t KeyValue_WriteTextToSink(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    key_value_write_fn_t pfnWrite,
    void *pUserData ) noexcept
{
    return KeyValue_InternalWriteTextToSink(
        pRoot,
        options,
        pfnWrite,
        pUserData,
        CY_FALSE );
}

key_value_canonical_hash_result_t KeyValue_HashCanonicalDocument(
    const key_value_document_t *pDocument ) noexcept
{
    key_value_canonical_hash_result_t result{};
    if ( pDocument == nullptr ) {
        result.status = key_value_write_status_t::INVALID_ARGUMENT;
        return result;
    }

    canonical_hash_sink_t sink{};
    if ( !HashXXH3_StreamInit(
             &sink.stream,
             hash_xxh3_stream_mode_t::HASH_128 ) ) {
        result.status = key_value_write_status_t::SINK_FAILED;
        return result;
    }

    key_value_write_options_t options{};
    options.flags = KEY_VALUE_WRITE_FLAG_CANONICAL;
    const key_value_write_result_t written = KeyValue_WriteTextToSink(
        KeyValue_Root( pDocument ),
        options,
        CanonicalHashSink,
        &sink );
    result.status = written.status;
    result.cbHashed = written.cchWritten;
    if ( written.status != key_value_write_status_t::OK ) {
        return result;
    }

    hash128_t hash{};
    if ( !HashXXH3_StreamDigest128( &sink.stream, &hash ) ) {
        result.status = key_value_write_status_t::SINK_FAILED;
        result.cbHashed = 0u;
        return result;
    }
    result.hash = { hash.low, hash.high };
    return result;
}

const char *KeyValue_WriteStatusName(
    key_value_write_status_t status ) noexcept
{
    switch ( status ) {
        case key_value_write_status_t::OK:               return "OK";
        case key_value_write_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case key_value_write_status_t::INVALID_DOCUMENT: return "INVALID_DOCUMENT";
        case key_value_write_status_t::DEPTH_LIMIT:      return "DEPTH_LIMIT";
        case key_value_write_status_t::OUT_OF_MEMORY:    return "OUT_OF_MEMORY";
        case key_value_write_status_t::SIZE_OVERFLOW:    return "SIZE_OVERFLOW";
        case key_value_write_status_t::OUTPUT_TRUNCATED: return "OUTPUT_TRUNCATED";
        case key_value_write_status_t::SINK_FAILED:      return "SINK_FAILED";
    }
    return "UNKNOWN_KEY_VALUE_WRITE_STATUS";
}

} // namespace cypher::common
