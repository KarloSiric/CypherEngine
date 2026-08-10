//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_KeyValuePack_Tests.cpp
//  Purpose: Tests versioned binary packing for CYKV documents.
//  Details: Covers exact sizing, deterministic bytes, typed round trips, endian
//           identity, input corruption, configured limits, and transactional reads.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValuePack.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace cypher::common;

namespace
{

key_value_document_t *BuildPackedDocument()
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    key_value_t *pRoot = KeyValue_Root( pDocument );
    REQUIRE( KeyValue_SetRootType( pDocument, key_value_type_t::OBJECT ) );

    key_value_t *pName = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( "name" ),
        key_value_type_t::STRING );
    REQUIRE( pName != nullptr );
    REQUIRE( KeyValue_SetString(
        pDocument,
        pName,
        StringView_FromCString( "Cypher" ) ) );

    key_value_t *pValues = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( "values" ),
        key_value_type_t::ARRAY );
    REQUIRE( pValues != nullptr );
    key_value_t *pSigned = KeyValue_ArrayAppend(
        pDocument,
        pValues,
        key_value_type_t::I64 );
    REQUIRE( pSigned != nullptr );
    REQUIRE( KeyValue_SetI64( pDocument, pSigned, CY_I64_MIN ) );
    key_value_t *pFloat = KeyValue_ArrayAppend(
        pDocument,
        pValues,
        key_value_type_t::F64 );
    REQUIRE( pFloat != nullptr );
    REQUIRE( KeyValue_SetF64( pDocument, pFloat, 1.25 ) );

    const byte bytes[]{ 0x00u, 0x80u, 0xFFu };
    key_value_t *pBinary = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( "binary" ),
        key_value_type_t::BINARY );
    REQUIRE( pBinary != nullptr );
    REQUIRE( KeyValue_SetBinary(
        pDocument,
        pBinary,
        { bytes, sizeof( bytes ) } ) );
    return pDocument;
}

std::vector<byte> WritePacked( const key_value_t *pRoot )
{
    const usize cbRequired = KeyValuePack_RequiredSize( pRoot );
    REQUIRE( cbRequired != 0u );
    std::vector<byte> bytes( cbRequired );
    const key_value_pack_result_t written = KeyValuePack_Write(
        pRoot,
        { bytes.data(), bytes.size() } );
    REQUIRE( written.status == key_value_pack_status_t::OK );
    REQUIRE( written.cbWritten == cbRequired );
    REQUIRE( written.cbRequired == cbRequired );
    return bytes;
}

} // namespace

TEST_CASE( "KeyValuePack writes deterministic little-endian bytes",
           "[CypherCommon][Tier1][KeyValuePack]" )
{
    key_value_document_t *pDocument = BuildPackedDocument();
    const std::vector<byte> first = WritePacked( KeyValue_Root( pDocument ) );
    const std::vector<byte> second = WritePacked( KeyValue_Root( pDocument ) );
    REQUIRE( first == second );
    REQUIRE( first[0] == static_cast<byte>( 'C' ) );
    REQUIRE( first[1] == static_cast<byte>( 'Y' ) );
    REQUIRE( first[2] == static_cast<byte>( 'K' ) );
    REQUIRE( first[3] == static_cast<byte>( 'V' ) );

    std::vector<byte> tooSmall( first.size() - 1u, 0xCDu );
    const key_value_pack_result_t shortWrite = KeyValuePack_Write(
        KeyValue_Root( pDocument ),
        { tooSmall.data(), tooSmall.size() } );
    REQUIRE( shortWrite.status == key_value_pack_status_t::OUTPUT_TOO_SMALL );
    REQUIRE( tooSmall.front() == 0xCDu );
    REQUIRE( tooSmall.back() == 0xCDu );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "KeyValuePack round trips complete typed trees",
           "[CypherCommon][Tier1][KeyValuePack]" )
{
    key_value_document_t *pSource = BuildPackedDocument();
    const std::vector<byte> packed = WritePacked( KeyValue_Root( pSource ) );
    key_value_document_t *pDest = KeyValue_CreateDocument( {} );
    REQUIRE( pDest != nullptr );

    const key_value_pack_result_t read = KeyValuePack_Read(
        { packed.data(), packed.size() },
        {},
        pDest );
    REQUIRE( read.status == key_value_pack_status_t::OK );
    REQUIRE( read.cbRead == packed.size() );

    const key_value_t *pRoot = KeyValue_Root( pDest );
    string_view_t name{};
    REQUIRE( KeyValue_GetString(
        KeyValue_Find( pRoot, StringView_FromCString( "name" ) ),
        &name ) );
    REQUIRE( StringView_Equals(
        name,
        StringView_FromCString( "Cypher" ) ) );

    const key_value_t *pValues = KeyValue_Find(
        pRoot,
        StringView_FromCString( "values" ) );
    i64 signedValue = 0;
    f64 floatValue = 0.0;
    REQUIRE( KeyValue_GetI64(
        KeyValue_ChildAt( pValues, 0u ),
        &signedValue ) );
    REQUIRE( signedValue == CY_I64_MIN );
    REQUIRE( KeyValue_GetF64(
        KeyValue_ChildAt( pValues, 1u ),
        &floatValue ) );
    REQUIRE( floatValue == 1.25 );

    binary_block_t binary{};
    REQUIRE( KeyValue_GetBinary(
        KeyValue_Find( pRoot, StringView_FromCString( "binary" ) ),
        &binary ) );
    REQUIRE( binary.cbSize == 3u );
    REQUIRE( binary.pData[1] == 0x80u );

    KeyValue_DestroyDocument( pDest );
    KeyValue_DestroyDocument( pSource );
}

TEST_CASE( "KeyValuePack rejects corrupt identities and preserves destination",
           "[CypherCommon][Tier1][KeyValuePack]" )
{
    key_value_document_t *pSource = BuildPackedDocument();
    const std::vector<byte> valid = WritePacked( KeyValue_Root( pSource ) );
    key_value_document_t *pDest = KeyValue_CreateDocument( {} );
    REQUIRE( pDest != nullptr );
    REQUIRE( KeyValue_SetString(
        pDest,
        KeyValue_Root( pDest ),
        StringView_FromCString( "stable" ) ) );

    std::vector<byte> corrupt = valid;
    corrupt[0] = 'X';
    REQUIRE( KeyValuePack_Read(
        { corrupt.data(), corrupt.size() },
        {},
        pDest ).status == key_value_pack_status_t::INVALID_MAGIC );

    corrupt = valid;
    corrupt[4] = 2u;
    REQUIRE( KeyValuePack_Read(
        { corrupt.data(), corrupt.size() },
        {},
        pDest ).status == key_value_pack_status_t::VERSION_MISMATCH );

    REQUIRE( KeyValuePack_Read(
        { valid.data(), valid.size() - 1u },
        {},
        pDest ).status == key_value_pack_status_t::CORRUPT_DATA );

    string_view_t stable{};
    REQUIRE( KeyValue_GetString( KeyValue_Root( pDest ), &stable ) );
    REQUIRE( StringView_Equals(
        stable,
        StringView_FromCString( "stable" ) ) );

    KeyValue_DestroyDocument( pDest );
    KeyValue_DestroyDocument( pSource );
}

TEST_CASE( "KeyValuePack enforces caller resource limits",
           "[CypherCommon][Tier1][KeyValuePack]" )
{
    key_value_document_t *pSource = BuildPackedDocument();
    const std::vector<byte> packed = WritePacked( KeyValue_Root( pSource ) );
    key_value_document_t *pDest = KeyValue_CreateDocument( {} );
    REQUIRE( pDest != nullptr );

    key_value_pack_limits_t limits{};
    limits.nMaxNodes = 2u;
    REQUIRE( KeyValuePack_Read(
        { packed.data(), packed.size() },
        limits,
        pDest ).status == key_value_pack_status_t::LIMIT_EXCEEDED );

    limits = {};
    limits.cbMaxData = 1u;
    REQUIRE( KeyValuePack_Read(
        { packed.data(), packed.size() },
        limits,
        pDest ).status == key_value_pack_status_t::LIMIT_EXCEEDED );

    REQUIRE( StringView_Equals(
        StringView_FromCString( KeyValuePack_StatusName(
            key_value_pack_status_t::CORRUPT_DATA ) ),
        StringView_FromCString( "CORRUPT_DATA" ) ) );

    KeyValue_DestroyDocument( pDest );
    KeyValue_DestroyDocument( pSource );
}
