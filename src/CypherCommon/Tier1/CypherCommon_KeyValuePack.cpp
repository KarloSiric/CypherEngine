//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValuePack.cpp
//  Purpose: Implements versioned little-endian CYKV binary packing.
//  Details: The reader validates the full record stream and configured limits before
//           allocating nodes. A failed read never changes the destination document.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValuePack.h"

#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"
#include "CypherCommon_KeyValueInternal.h"

#include <bit>
#include <cmath>

namespace cypher::common
{

namespace
{

// Header: magic, version, header size, flags, total bytes, nodes, data bytes.
inline constexpr u32 CY_KEY_VALUE_PACK_HEADER_SIZE = 40u;
// Record: type, flags, reserved, children, name bytes, value bytes, scalar bits.
inline constexpr u32 CY_KEY_VALUE_PACK_RECORD_SIZE = 32u;
inline constexpr u32 CY_KEY_VALUE_PACK_FLAGS = 0u;

struct pack_totals_t {
    usize nNodes{ 0u };
    usize cbData{ 0u };
    usize cbRequired{ 0u };
};

struct packed_record_t {
    key_value_type_t type{ key_value_type_t::NULL_VALUE };
    u32 nChildren{ 0u };
    usize cchName{ 0u };
    usize cbValue{ 0u };
    u64 nScalarBits{ 0u };
    binary_block_t name{};
    binary_block_t value{};
};

struct validate_context_t {
    byte_reader_t reader{};
    key_value_pack_limits_t limits{};
    usize nNodes{ 0u };
    usize cbData{ 0u };
    bool_t bLimitExceeded{ CY_FALSE };
};

CYPHER_NODISCARD bool_t IsValidType( key_value_type_t type ) noexcept
{
    return static_cast<u8>( type ) <=
           static_cast<u8>( key_value_type_t::ARRAY );
}

CYPHER_NODISCARD bool_t IsContainer( key_value_type_t type ) noexcept
{
    return type == key_value_type_t::OBJECT ||
           type == key_value_type_t::ARRAY;
}

CYPHER_NODISCARD bool_t AddSize(
    usize value,
    usize &total ) noexcept
{
    if ( value > CY_USIZE_MAX - total ) return CY_FALSE;
    total += value;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t CollectTotals(
    const key_value_t *pRoot,
    pack_totals_t &totals ) noexcept
{
    if ( !KeyValue_InternalTreeIsValid( pRoot ) ) return CY_FALSE;
    const key_value_t *pNode = pRoot;
    while ( pNode != nullptr ) {
        if ( totals.nNodes == CY_USIZE_MAX ||
             pNode->nChildren > CY_U32_MAX ) return CY_FALSE;
        ++totals.nNodes;
        if ( !AddSize( pNode->cchName, totals.cbData ) ) return CY_FALSE;
        if ( pNode->type == key_value_type_t::STRING ||
             pNode->type == key_value_type_t::BINARY ) {
            if ( !AddSize( pNode->value.bytes.cbSize, totals.cbData ) ) {
                return CY_FALSE;
            }
        }

        if ( pNode->pFirstChild != nullptr ) {
            pNode = pNode->pFirstChild;
            continue;
        }
        while ( pNode != pRoot && pNode->pNext == nullptr ) {
            pNode = pNode->pParent;
        }
        pNode = pNode == pRoot ? nullptr : pNode->pNext;
    }

    usize cbRecords = 0u;
    if ( totals.nNodes > CY_USIZE_MAX / CY_KEY_VALUE_PACK_RECORD_SIZE ) {
        return CY_FALSE;
    }
    cbRecords = totals.nNodes * CY_KEY_VALUE_PACK_RECORD_SIZE;
    totals.cbRequired = CY_KEY_VALUE_PACK_HEADER_SIZE;
    return AddSize( cbRecords, totals.cbRequired ) &&
           AddSize( totals.cbData, totals.cbRequired );
}

CYPHER_NODISCARD u64 ScalarBits( const key_value_t &value ) noexcept
{
    switch ( value.type ) {
        case key_value_type_t::BOOL:
            return value.value.bValue ? 1u : 0u;
        case key_value_type_t::I64:
            return std::bit_cast<u64>( value.value.iValue );
        case key_value_type_t::U64:
            return value.value.uValue;
        case key_value_type_t::F64:
            return std::bit_cast<u64>( value.value.flValue );
        default:
            return 0u;
    }
}

CYPHER_NODISCARD bool_t WriteRecord(
    byte_writer_t &writer,
    const key_value_t &value ) noexcept
{
    const usize cbValue = value.type == key_value_type_t::STRING ||
                          value.type == key_value_type_t::BINARY
        ? value.value.bytes.cbSize
        : 0u;
    const void *pValueData = cbValue != 0u
        ? value.value.bytes.pData
        : nullptr;
    return ByteWriter_WriteU8(
               &writer,
               static_cast<u8>( value.type ) ) &&
           ByteWriter_WriteU8( &writer, 0u ) &&
           ByteWriter_WriteU16( &writer, 0u ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( value.nChildren ) ) &&
           ByteWriter_WriteU64( &writer, value.cchName ) &&
           ByteWriter_WriteU64( &writer, cbValue ) &&
           ByteWriter_WriteU64( &writer, ScalarBits( value ) ) &&
           ByteWriter_Write( &writer, value.pName, value.cchName ) &&
           ByteWriter_Write(
               &writer,
               pValueData,
               cbValue );
}

CYPHER_NODISCARD bool_t ReadRecord(
    byte_reader_t &reader,
    packed_record_t &record ) noexcept
{
    u8 nType = 0u;
    u8 nFlags = 0u;
    u16 nReserved = 0u;
    u32 nChildren = 0u;
    u64 cchName = 0u;
    u64 cbValue = 0u;
    u64 nScalarBits = 0u;
    if ( !ByteReader_ReadU8( &reader, &nType ) ||
         !ByteReader_ReadU8( &reader, &nFlags ) ||
         !ByteReader_ReadU16( &reader, &nReserved ) ||
         !ByteReader_ReadU32( &reader, &nChildren ) ||
         !ByteReader_ReadU64( &reader, &cchName ) ||
         !ByteReader_ReadU64( &reader, &cbValue ) ||
         !ByteReader_ReadU64( &reader, &nScalarBits ) ||
         nFlags != 0u || nReserved != 0u ||
         nType > static_cast<u8>( key_value_type_t::ARRAY ) ||
         cchName > CY_USIZE_MAX || cbValue > CY_USIZE_MAX ) {
        return CY_FALSE;
    }
    record.type = static_cast<key_value_type_t>( nType );
    record.nChildren = nChildren;
    record.cchName = static_cast<usize>( cchName );
    record.cbValue = static_cast<usize>( cbValue );
    record.nScalarBits = nScalarBits;
    if ( !ByteReader_ReadBlock(
             &reader,
             record.cchName,
             &record.name ) ||
         !ByteReader_ReadBlock(
             &reader,
             record.cbValue,
             &record.value ) ) {
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t RecordShapeIsValid(
    const packed_record_t &record,
    key_value_type_t parentType,
    bool_t bRoot ) noexcept
{
    if ( !IsValidType( record.type ) ||
         ( bRoot && record.cchName != 0u ) ||
         ( !bRoot && parentType == key_value_type_t::ARRAY &&
           record.cchName != 0u ) ) {
        return CY_FALSE;
    }
    if ( IsContainer( record.type ) ) {
        return record.cbValue == 0u && record.nScalarBits == 0u;
    }
    if ( record.nChildren != 0u ) return CY_FALSE;
    switch ( record.type ) {
        case key_value_type_t::NULL_VALUE:
            return record.cbValue == 0u && record.nScalarBits == 0u;
        case key_value_type_t::BOOL:
            return record.cbValue == 0u && record.nScalarBits <= 1u;
        case key_value_type_t::I64:
        case key_value_type_t::U64:
            return record.cbValue == 0u;
        case key_value_type_t::F64:
            return record.cbValue == 0u &&
                   std::isfinite( std::bit_cast<f64>( record.nScalarBits ) );
        case key_value_type_t::STRING:
        case key_value_type_t::BINARY:
            return record.nScalarBits == 0u;
        case key_value_type_t::OBJECT:
        case key_value_type_t::ARRAY:
            return CY_FALSE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ValidateNode(
    validate_context_t &context,
    key_value_type_t parentType,
    usize nDepth,
    bool_t bRoot ) noexcept
{
    if ( nDepth > context.limits.nMaxDepth ||
         context.nNodes >= context.limits.nMaxNodes ) {
        context.bLimitExceeded = CY_TRUE;
        return CY_FALSE;
    }
    packed_record_t record{};
    if ( !ReadRecord( context.reader, record ) ||
         !RecordShapeIsValid( record, parentType, bRoot ) ) {
        return CY_FALSE;
    }
    ++context.nNodes;
    if ( !AddSize( record.cchName, context.cbData ) ||
         !AddSize( record.cbValue, context.cbData ) ||
         context.cbData > context.limits.cbMaxData ) {
        context.bLimitExceeded = CY_TRUE;
        return CY_FALSE;
    }
    for ( u32 iChild = 0u; iChild < record.nChildren; ++iChild ) {
        if ( !ValidateNode(
                 context,
                 record.type,
                 nDepth + 1u,
                 CY_FALSE ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t SetPackedValue(
    key_value_document_t *pDocument,
    key_value_t *pValue,
    const packed_record_t &record ) noexcept
{
    switch ( record.type ) {
        case key_value_type_t::NULL_VALUE:
            return KeyValue_SetNull( pDocument, pValue );
        case key_value_type_t::BOOL:
            return KeyValue_SetBool(
                pDocument,
                pValue,
                record.nScalarBits != 0u );
        case key_value_type_t::I64:
            return KeyValue_SetI64(
                pDocument,
                pValue,
                std::bit_cast<i64>( record.nScalarBits ) );
        case key_value_type_t::U64:
            return KeyValue_SetU64(
                pDocument,
                pValue,
                record.nScalarBits );
        case key_value_type_t::F64:
            return KeyValue_SetF64(
                pDocument,
                pValue,
                std::bit_cast<f64>( record.nScalarBits ) );
        case key_value_type_t::STRING:
            return KeyValue_SetString(
                pDocument,
                pValue,
                {
                    reinterpret_cast<const char *>( record.value.pData ),
                    record.value.cbSize
                } );
        case key_value_type_t::BINARY:
            return KeyValue_SetBinary(
                pDocument,
                pValue,
                record.value );
        case key_value_type_t::OBJECT:
        case key_value_type_t::ARRAY:
            return KeyValue_SetContainerType(
                pDocument,
                pValue,
                record.type );
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ConstructNode(
    byte_reader_t &reader,
    key_value_document_t *pDocument,
    key_value_t *pParent,
    usize nDepth,
    bool_t bRoot ) noexcept
{
    if ( nDepth > CY_KEY_VALUE_MAX_DEPTH ) return CY_FALSE;
    packed_record_t record{};
    if ( !ReadRecord( reader, record ) ) return CY_FALSE;

    key_value_t *pValue = nullptr;
    if ( bRoot ) {
        pValue = KeyValue_Root( pDocument );
    } else if ( KeyValue_Type( pParent ) == key_value_type_t::OBJECT ) {
        pValue = KeyValue_ObjectInsert(
            pDocument,
            pParent,
            {
                reinterpret_cast<const char *>( record.name.pData ),
                record.name.cbSize
            },
            record.type );
    } else {
        pValue = KeyValue_ArrayAppend(
            pDocument,
            pParent,
            record.type );
    }
    if ( pValue == nullptr ||
         !SetPackedValue( pDocument, pValue, record ) ) {
        return CY_FALSE;
    }
    for ( u32 iChild = 0u; iChild < record.nChildren; ++iChild ) {
        if ( !ConstructNode(
                 reader,
                 pDocument,
                 pValue,
                 nDepth + 1u,
                 CY_FALSE ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

} // namespace

usize KeyValuePack_RequiredSize( const key_value_t *pRoot ) noexcept
{
    pack_totals_t totals{};
    return CollectTotals( pRoot, totals ) ? totals.cbRequired : 0u;
}

key_value_pack_result_t KeyValuePack_Write(
    const key_value_t *pRoot,
    byte_span_t output ) noexcept
{
    key_value_pack_result_t result{};
    if ( !Span_IsValid( output ) ) {
        result.status = key_value_pack_status_t::INVALID_ARGUMENT;
        return result;
    }
    pack_totals_t totals{};
    if ( !CollectTotals( pRoot, totals ) ) {
        result.status = key_value_pack_status_t::INVALID_ARGUMENT;
        return result;
    }
    result.cbRequired = totals.cbRequired;
    if ( output.nCount < totals.cbRequired ) {
        result.status = key_value_pack_status_t::OUTPUT_TOO_SMALL;
        return result;
    }

    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             output,
             data_byte_order_t::LITTLE ) ||
         !ByteWriter_WriteU32( &writer, CY_KEY_VALUE_PACK_MAGIC ) ||
         !ByteWriter_WriteU32( &writer, CY_KEY_VALUE_PACK_VERSION ) ||
         !ByteWriter_WriteU32( &writer, CY_KEY_VALUE_PACK_HEADER_SIZE ) ||
         !ByteWriter_WriteU32( &writer, CY_KEY_VALUE_PACK_FLAGS ) ||
         !ByteWriter_WriteU64( &writer, totals.cbRequired ) ||
         !ByteWriter_WriteU64( &writer, totals.nNodes ) ||
         !ByteWriter_WriteU64( &writer, totals.cbData ) ) {
        result.status = key_value_pack_status_t::OUTPUT_TOO_SMALL;
        return result;
    }

    const key_value_t *pNode = pRoot;
    while ( pNode != nullptr ) {
        if ( !WriteRecord( writer, *pNode ) ) {
            result.status = key_value_pack_status_t::OUTPUT_TOO_SMALL;
            return result;
        }
        if ( pNode->pFirstChild != nullptr ) {
            pNode = pNode->pFirstChild;
            continue;
        }
        while ( pNode != pRoot && pNode->pNext == nullptr ) {
            pNode = pNode->pParent;
        }
        pNode = pNode == pRoot ? nullptr : pNode->pNext;
    }
    result.cbWritten = ByteWriter_BytesWritten( &writer );
    if ( result.cbWritten != result.cbRequired ) {
        result.status = key_value_pack_status_t::CORRUPT_DATA;
    }
    return result;
}

key_value_pack_result_t KeyValuePack_Read(
    binary_block_t input,
    const key_value_pack_limits_t &limits,
    key_value_document_t *pDocument ) noexcept
{
    key_value_pack_result_t result{};
    if ( !BinaryBlock_IsValid( input ) ||
         !KeyValue_InternalDocumentIsValid( pDocument ) ||
         limits.nMaxDepth == 0u ||
         limits.nMaxDepth > CY_KEY_VALUE_MAX_DEPTH ||
         limits.nMaxNodes == 0u || limits.cbMaxData == 0u ) {
        result.status = key_value_pack_status_t::INVALID_ARGUMENT;
        return result;
    }

    byte_reader_t headerReader{};
    u32 magic = 0u;
    u32 version = 0u;
    u32 cbHeader = 0u;
    u32 flags = 0u;
    u64 cbTotal = 0u;
    u64 nExpectedNodes = 0u;
    u64 cbExpectedData = 0u;
    if ( !ByteReader_Init(
             &headerReader,
             input,
             data_byte_order_t::LITTLE ) ||
         !ByteReader_ReadU32( &headerReader, &magic ) ) {
        result.status = key_value_pack_status_t::CORRUPT_DATA;
        return result;
    }
    if ( magic != CY_KEY_VALUE_PACK_MAGIC ) {
        result.status = key_value_pack_status_t::INVALID_MAGIC;
        return result;
    }
    if ( !ByteReader_ReadU32( &headerReader, &version ) ) {
        result.status = key_value_pack_status_t::CORRUPT_DATA;
        return result;
    }
    if ( version != CY_KEY_VALUE_PACK_VERSION ) {
        result.status = key_value_pack_status_t::VERSION_MISMATCH;
        return result;
    }
    if ( !ByteReader_ReadU32( &headerReader, &cbHeader ) ||
         !ByteReader_ReadU32( &headerReader, &flags ) ||
         !ByteReader_ReadU64( &headerReader, &cbTotal ) ||
         !ByteReader_ReadU64( &headerReader, &nExpectedNodes ) ||
         !ByteReader_ReadU64( &headerReader, &cbExpectedData ) ||
         cbHeader != CY_KEY_VALUE_PACK_HEADER_SIZE ||
         flags != CY_KEY_VALUE_PACK_FLAGS ||
         cbTotal != input.cbSize ||
         nExpectedNodes == 0u ) {
        result.status = key_value_pack_status_t::CORRUPT_DATA;
        return result;
    }
    if ( nExpectedNodes > limits.nMaxNodes ||
         cbExpectedData > limits.cbMaxData ||
         nExpectedNodes > CY_USIZE_MAX ||
         cbExpectedData > CY_USIZE_MAX ) {
        result.status = key_value_pack_status_t::LIMIT_EXCEEDED;
        return result;
    }

    validate_context_t validation{};
    validation.reader = headerReader;
    validation.limits = limits;
    if ( !ValidateNode(
             validation,
             key_value_type_t::NULL_VALUE,
             0u,
             CY_TRUE ) ||
         validation.nNodes != static_cast<usize>( nExpectedNodes ) ||
         validation.cbData != static_cast<usize>( cbExpectedData ) ||
         ByteReader_Remaining( &validation.reader ) != 0u ) {
        result.status = validation.bLimitExceeded
            ? key_value_pack_status_t::LIMIT_EXCEEDED
            : key_value_pack_status_t::CORRUPT_DATA;
        result.cbRead = ByteReader_Offset( &validation.reader );
        return result;
    }

    key_value_document_t *pTemporary = KeyValue_InternalCreateLike( pDocument );
    if ( pTemporary == nullptr ) {
        result.status = key_value_pack_status_t::OUT_OF_MEMORY;
        return result;
    }
    byte_reader_t constructReader = headerReader;
    const bool_t bConstructed = ConstructNode(
        constructReader,
        pTemporary,
        nullptr,
        0u,
        CY_TRUE );
    if ( !bConstructed || ByteReader_Remaining( &constructReader ) != 0u ) {
        KeyValue_DestroyDocument( pTemporary );
        result.status = key_value_pack_status_t::OUT_OF_MEMORY;
        result.cbRead = ByteReader_Offset( &constructReader );
        return result;
    }
    KeyValue_InternalMoveDocumentContents( pDocument, pTemporary );
    KeyValue_DestroyDocument( pTemporary );
    result.cbRead = input.cbSize;
    return result;
}

const char *KeyValuePack_StatusName(
    key_value_pack_status_t status ) noexcept
{
    switch ( status ) {
        case key_value_pack_status_t::OK:               return "OK";
        case key_value_pack_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case key_value_pack_status_t::OUTPUT_TOO_SMALL: return "OUTPUT_TOO_SMALL";
        case key_value_pack_status_t::INVALID_MAGIC:    return "INVALID_MAGIC";
        case key_value_pack_status_t::VERSION_MISMATCH: return "VERSION_MISMATCH";
        case key_value_pack_status_t::CORRUPT_DATA:     return "CORRUPT_DATA";
        case key_value_pack_status_t::LIMIT_EXCEEDED:   return "LIMIT_EXCEEDED";
        case key_value_pack_status_t::OUT_OF_MEMORY:    return "OUT_OF_MEMORY";
    }
    return "UNKNOWN_KEY_VALUE_PACK_STATUS";
}

} // namespace cypher::common
