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

#include "CypherCommon_KeyValueWriterInternal.h"

#include "CypherCommon_KeyValueInternal.h"
#include "CypherCommon_Sort.h"
#include "CypherCommon_StringConvert.h"
#include "CypherCommon_StringEscape.h"
#include "CypherCommon_Unicode.h"

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_KEY_VALUE_WRITE_VALID_FLAGS =
    KEY_VALUE_WRITE_FLAG_PRETTY |
    KEY_VALUE_WRITE_FLAG_CANONICAL |
    KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE |
    KEY_VALUE_WRITE_FLAG_ASCII_ONLY;

constexpr flags32_t CY_KEY_VALUE_ESCAPE_FLAGS =
    STRING_ESCAPE_FLAG_QUOTES |
    STRING_ESCAPE_FLAG_BACKSLASH |
    STRING_ESCAPE_FLAG_CONTROL_CHARS;

struct writer_t {
    key_value_write_fn_t pfnWrite{ nullptr };
    void *pUserData{ nullptr };
    key_value_write_options_t options{};
    key_value_write_result_t result{};
    const allocator_t *pAllocator{ nullptr };
    bool_t bStrictJson{ CY_FALSE };
    bool_t bPretty{ CY_FALSE };
    bool_t bCanonical{ CY_FALSE };
};

struct buffer_sink_t {
    char *pDest{ nullptr };
    usize cchCapacity{ 0u };
    usize cchWritten{ 0u };
};

struct canonical_child_t {
    const key_value_t *pValue{ nullptr };
    usize iOriginal{ 0u };
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

CYPHER_NODISCARD bool_t EmitI64(
    writer_t &writer,
    i64 value ) noexcept
{
    char text[64]{};
    string_integer_format_t format{};
    const string_convert_result_t converted = StringConvert_I64(
        value,
        format,
        text,
        sizeof( text ) );
    return converted.status == string_convert_status_t::OK
        ? Emit( writer, text, converted.cchWritten )
        : ( Fail( writer, key_value_write_status_t::INVALID_DOCUMENT ), CY_FALSE );
}

CYPHER_NODISCARD bool_t EmitU64(
    writer_t &writer,
    u64 value ) noexcept
{
    char text[64]{};
    const string_convert_result_t converted = StringConvert_U64(
        value,
        {},
        text,
        sizeof( text ) );
    if ( converted.status != string_convert_status_t::OK ||
         !Emit( writer, text, converted.cchWritten ) ) {
        if ( converted.status != string_convert_status_t::OK ) {
            Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
        }
        return CY_FALSE;
    }
    return writer.bStrictJson || EmitLiteral( writer, "u" );
}

CYPHER_NODISCARD bool_t EmitF64(
    writer_t &writer,
    f64 value ) noexcept
{
    char text[128]{};
    const string_convert_result_t converted = StringConvert_F64(
        value,
        {
            string_float_style_t::GENERAL,
            17u,
            STRING_FLOAT_FORMAT_FLAG_TRIM_TRAILING_ZERO
        },
        text,
        sizeof( text ) );
    if ( converted.status != string_convert_status_t::OK ||
         !Emit( writer, text, converted.cchWritten ) ) {
        if ( converted.status != string_convert_status_t::OK ) {
            Fail( writer, key_value_write_status_t::INVALID_DOCUMENT );
        }
        return CY_FALSE;
    }
    if ( writer.bStrictJson ) {
        return CY_TRUE;
    }
    for ( usize iByte = 0u; iByte < converted.cchWritten; ++iByte ) {
        if ( text[iByte] == '.' || text[iByte] == 'e' || text[iByte] == 'E' ) {
            return CY_TRUE;
        }
    }
    return EmitLiteral( writer, ".0" );
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
                   EmitEscapedString(
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
    if ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION ||
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
    bool_t bStrictJson ) noexcept
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
    if ( !bStrictJson && KeyValue_Type( pRoot ) != key_value_type_t::OBJECT ) {
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
    writer.bPretty = !writer.bCanonical &&
        ( options.flags & KEY_VALUE_WRITE_FLAG_PRETTY ) != 0u;

    if ( !writer.bStrictJson ) {
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

} // namespace

key_value_write_result_t KeyValue_InternalWriteText(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    char *pDest,
    usize cchDest,
    bool_t bStrictJson ) noexcept
{
    if ( pDest == nullptr && cchDest != 0u ) {
        return { key_value_write_status_t::INVALID_ARGUMENT, 0u, 0u };
    }
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
        bStrictJson );
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
