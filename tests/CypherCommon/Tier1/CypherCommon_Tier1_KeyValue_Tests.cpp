//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_KeyValue_Tests.cpp
//  Purpose: Tests the owned hierarchical CYKV data model.
//  Details: Covers stable nodes, owned values, object policy, array ordering,
//           subtree removal, container replacement, and cross-document cloning.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValue.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "KeyValue owns object names and scalar payloads",
           "[CypherCommon][Tier1][KeyValue]" )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    key_value_t *pRoot = KeyValue_Root( pDocument );
    REQUIRE( KeyValue_SetRootType( pDocument, key_value_type_t::OBJECT ) );

    char mutableName[]{ "title" };
    char mutableValue[]{ "Cypher" };
    key_value_t *pTitle = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( mutableName ),
        key_value_type_t::STRING );
    REQUIRE( pTitle != nullptr );
    REQUIRE( KeyValue_SetString(
        pDocument,
        pTitle,
        StringView_FromCString( mutableValue ) ) );
    mutableName[0] = 'X';
    mutableValue[0] = 'X';

    REQUIRE( StringView_Equals(
        KeyValue_Name( pTitle ),
        StringView_FromCString( "title" ) ) );
    string_view_t value{};
    REQUIRE( KeyValue_GetString( pTitle, &value ) );
    REQUIRE( StringView_Equals( value, StringView_FromCString( "Cypher" ) ) );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "KeyValue growth preserves pointers and array order",
           "[CypherCommon][Tier1][KeyValue]" )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument({
        nullptr,
        2u,
        64u,
        CY_FALSE
    });
    REQUIRE( pDocument != nullptr );
    key_value_t *pRoot = KeyValue_Root( pDocument );
    REQUIRE( KeyValue_SetRootType( pDocument, key_value_type_t::ARRAY ) );
    key_value_t *pFirst = KeyValue_ArrayAppend(
        pDocument,
        pRoot,
        key_value_type_t::U64 );
    REQUIRE( pFirst != nullptr );
    REQUIRE( KeyValue_SetU64( pDocument, pFirst, 1u ) );

    for ( u64 value = 2u; value <= 300u; ++value ) {
        key_value_t *pChild = KeyValue_ArrayAppend(
            pDocument,
            pRoot,
            key_value_type_t::U64 );
        REQUIRE( pChild != nullptr );
        REQUIRE( KeyValue_SetU64( pDocument, pChild, value ) );
    }
    REQUIRE( KeyValue_ChildCount( pRoot ) == 300u );
    REQUIRE( KeyValue_ChildAt( pRoot, 0u ) == pFirst );
    u64 firstValue = 0u;
    u64 lastValue = 0u;
    REQUIRE( KeyValue_GetU64( pFirst, &firstValue ) );
    REQUIRE( KeyValue_GetU64(
        KeyValue_ChildAt( pRoot, 299u ),
        &lastValue ) );
    REQUIRE( firstValue == 1u );
    REQUIRE( lastValue == 300u );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "KeyValue applies document key comparison policy",
           "[CypherCommon][Tier1][KeyValue]" )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument({
        nullptr,
        8u,
        128u,
        CY_TRUE
    });
    REQUIRE( pDocument != nullptr );
    key_value_t *pRoot = KeyValue_Root( pDocument );
    REQUIRE( KeyValue_SetRootType( pDocument, key_value_type_t::OBJECT ) );
    key_value_t *pValue = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( "PlayerSpeed" ),
        key_value_type_t::F64 );
    REQUIRE( pValue != nullptr );
    REQUIRE(
        KeyValue_Find( pRoot, StringView_FromCString( "playerspeed" ) ) ==
        pValue );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "KeyValue removes subtrees and replaces containers",
           "[CypherCommon][Tier1][KeyValue]" )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    key_value_t *pRoot = KeyValue_Root( pDocument );
    REQUIRE( KeyValue_SetRootType( pDocument, key_value_type_t::OBJECT ) );
    key_value_t *pNested = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( "nested" ),
        key_value_type_t::ARRAY );
    REQUIRE( pNested != nullptr );
    REQUIRE( KeyValue_ArrayAppend(
        pDocument,
        pNested,
        key_value_type_t::BOOL ) != nullptr );
    REQUIRE( KeyValue_Remove( pDocument, pRoot, pNested ) );
    REQUIRE( KeyValue_ChildCount( pRoot ) == 0u );

    key_value_t *pAgain = KeyValue_ObjectInsert(
        pDocument,
        pRoot,
        StringView_FromCString( "again" ),
        key_value_type_t::ARRAY );
    REQUIRE( pAgain != nullptr );
    REQUIRE( KeyValue_ArrayAppend(
        pDocument,
        pAgain,
        key_value_type_t::NULL_VALUE ) != nullptr );
    REQUIRE( KeyValue_SetString(
        pDocument,
        pAgain,
        StringView_FromCString( "now scalar" ) ) );
    REQUIRE( KeyValue_ChildCount( pAgain ) == 0u );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "KeyValue clones complete subtrees across documents",
           "[CypherCommon][Tier1][KeyValue]" )
{
    key_value_document_t *pSource = KeyValue_CreateDocument( {} );
    REQUIRE( KeyValue_SetRootType(
        pSource,
        key_value_type_t::OBJECT ) );
    key_value_t *pArray = KeyValue_ObjectInsert(
        pSource,
        KeyValue_Root( pSource ),
        StringView_FromCString( "values" ),
        key_value_type_t::ARRAY );
    key_value_t *pItem = KeyValue_ArrayAppend(
        pSource,
        pArray,
        key_value_type_t::I64 );
    REQUIRE( KeyValue_SetI64( pSource, pItem, -42 ) );

    key_value_document_t *pDest = KeyValue_CreateDocument( {} );
    const key_value_t *pClone = KeyValue_CloneInto(
        pDest,
        nullptr,
        KeyValue_Root( pSource ) );
    REQUIRE( pClone != nullptr );
    const key_value_t *pClonedArray = KeyValue_Find(
        pClone,
        StringView_FromCString( "values" ) );
    REQUIRE( pClonedArray != nullptr );
    i64 value = 0;
    REQUIRE( KeyValue_GetI64(
        KeyValue_ChildAt( pClonedArray, 0u ),
        &value ) );
    REQUIRE( value == -42 );

    KeyValue_DestroyDocument( pSource );
    REQUIRE( KeyValue_GetI64(
        KeyValue_ChildAt( pClonedArray, 0u ),
        &value ) );
    KeyValue_DestroyDocument( pDest );
}

TEST_CASE( "KeyValue bounds recursive tree depth at construction",
           "[CypherCommon][Tier1][KeyValue]" )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument({
        nullptr,
        CY_KEY_VALUE_MAX_DEPTH + 1u,
        128u,
        CY_FALSE
    });
    REQUIRE( pDocument != nullptr );
    key_value_t *pCurrent = KeyValue_Root( pDocument );
    REQUIRE( KeyValue_SetRootType(
        pDocument,
        key_value_type_t::ARRAY ) );
    for ( usize nDepth = 0u; nDepth < CY_KEY_VALUE_MAX_DEPTH; ++nDepth ) {
        pCurrent = KeyValue_ArrayAppend(
            pDocument,
            pCurrent,
            key_value_type_t::ARRAY );
        REQUIRE( pCurrent != nullptr );
    }
    REQUIRE( KeyValue_ArrayAppend(
        pDocument,
        pCurrent,
        key_value_type_t::ARRAY ) == nullptr );
    KeyValue_DestroyDocument( pDocument );
}
