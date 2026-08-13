//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier2/CypherCommon_Tier2_Schema_Tests.cpp
//  Purpose: Tests Tier2 CYKV schema descriptors, validation, and registry lookup.
//  Details: Covers valid project data, malformed descriptors, exact headers,
//           structural and scalar failures, bounded diagnostics, paths, and limits.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_ProjectSchema.h"
#include "CypherCommon_SchemaRegistry.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

key_value_document_t *ParseDocument( const char *pSource )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    const key_value_parse_result_t result = KeyValue_ParseText(
        StringView_FromCString( pSource ),
        {},
        pDocument );
    REQUIRE( result.status == key_value_parse_status_t::OK );
    return pDocument;
}

bool_t HasDiagnostic(
    const schema_diagnostic_t *pDiagnostics,
    usize nDiagnostics,
    schema_diagnostic_code_t code,
    const char *pPath )
{
    for ( usize iDiagnostic = 0u;
          iDiagnostic < nDiagnostics;
          ++iDiagnostic ) {
        if ( pDiagnostics[iDiagnostic].code == code &&
             StringView_Equals(
                 StringView_FromCString( pDiagnostics[iDiagnostic].path ),
                 StringView_FromCString( pPath ) ) ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

} // namespace

TEST_CASE( "Tier2 schema descriptors reject malformed contracts",
           "[CypherCommon][Tier2][Schema]" )
{
    schema_rule_t stringRule{};
    stringRule.allowedTypes = SCHEMA_TYPE_STRING;

    schema_descriptor_t descriptor{
        StringView_FromCString( "invalid" ),
        1u,
        &stringRule
    };
    REQUIRE( Schema_CheckDescriptor( &descriptor ) ==
             schema_descriptor_status_t::INVALID_SCHEMA_ID );

    descriptor.schemaId = StringView_FromCString( "cypher.test" );
    descriptor.nVersion = 0u;
    REQUIRE( Schema_CheckDescriptor( &descriptor ) ==
             schema_descriptor_status_t::INVALID_VERSION );

    descriptor.nVersion = 1u;
    stringRule.string.cbMinLength = 8u;
    stringRule.string.cbMaxLength = 4u;
    REQUIRE( Schema_CheckDescriptor( &descriptor ) ==
             schema_descriptor_status_t::INVALID_RANGE );

    stringRule.string = {};
    schema_member_t duplicateMembers[]{
        { StringView_FromCString( "value" ), &stringRule, SCHEMA_MEMBER_NONE },
        { StringView_FromCString( "value" ), &stringRule, SCHEMA_MEMBER_NONE }
    };
    schema_rule_t objectRule{};
    objectRule.allowedTypes = SCHEMA_TYPE_OBJECT;
    objectRule.object.pMembers = duplicateMembers;
    objectRule.object.nMembers = 2u;
    descriptor.pRootRule = &objectRule;
    REQUIRE( Schema_CheckDescriptor( &descriptor ) ==
             schema_descriptor_status_t::DUPLICATE_MEMBER );

    REQUIRE( Schema_CheckDescriptor( ProjectSchema_V1() ) ==
             schema_descriptor_status_t::OK );
}

TEST_CASE( "Tier2 validates a complete cypher project document",
           "[CypherCommon][Tier2][Schema][Project]" )
{
    constexpr const char *pSource = R"cykv(@cykv 1
@schema "cypher.project" 1
{
    id = "reap"
    name = "REAP"
    start_map = "maps/facility.cymap"
    search_paths = ["game", "engine", "mods/base",]
}
)cykv";

    key_value_document_t *pDocument = ParseDocument( pSource );
    schema_diagnostic_t diagnostics[8]{};
    const schema_validation_result_t result = Schema_ValidateDocument(
        ProjectSchema_V1(),
        pDocument,
        {},
        diagnostics,
        sizeof( diagnostics ) / sizeof( diagnostics[0] ) );

    REQUIRE( Schema_ValidationSucceeded( result ) );
    REQUIRE( result.nDiagnosticsRequired == 0u );
    REQUIRE( result.nNodesVisited == 8u );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 reports project errors with stable logical paths",
           "[CypherCommon][Tier2][Schema][Project]" )
{
    constexpr const char *pSource = R"cykv(@cykv 1
@schema "cypher.project" 1
{
    name = ""
    start_map = 7
    search_paths = ["game", 9]
    extra = true
}
)cykv";

    key_value_document_t *pDocument = ParseDocument( pSource );
    schema_diagnostic_t diagnostics[16]{};
    const schema_validation_result_t result = Schema_ValidateDocument(
        ProjectSchema_V1(),
        pDocument,
        {},
        diagnostics,
        sizeof( diagnostics ) / sizeof( diagnostics[0] ) );

    REQUIRE_FALSE( Schema_ValidationSucceeded( result ) );
    REQUIRE( result.status == schema_validation_status_t::INVALID_DOCUMENT );
    REQUIRE( result.nErrors == 5u );
    REQUIRE( result.nDiagnosticsRequired == 5u );
    REQUIRE( result.nDiagnosticsWritten == 5u );
    REQUIRE_FALSE( result.bDiagnosticsTruncated );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::MISSING_REQUIRED_MEMBER,
        "/id" ) );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::STRING_LENGTH,
        "/name" ) );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::TYPE_MISMATCH,
        "/start_map" ) );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::TYPE_MISMATCH,
        "/search_paths/1" ) );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::UNKNOWN_MEMBER,
        "/extra" ) );

    schema_diagnostic_t limited[2]{};
    const schema_validation_result_t truncated = Schema_ValidateDocument(
        ProjectSchema_V1(),
        pDocument,
        {},
        limited,
        sizeof( limited ) / sizeof( limited[0] ) );
    REQUIRE( truncated.nDiagnosticsRequired == 5u );
    REQUIRE( truncated.nDiagnosticsWritten == 2u );
    REQUIRE( truncated.bDiagnosticsTruncated );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 validates all scalar constraint families and warnings",
           "[CypherCommon][Tier2][Schema][Constraints]" )
{
    const string_view_t allowedModes[]{
        StringView_FromCString( "client" ),
        StringView_FromCString( "server" )
    };

    schema_rule_t modeRule{};
    modeRule.allowedTypes = SCHEMA_TYPE_STRING;
    modeRule.string.pAllowedValues = allowedModes;
    modeRule.string.nAllowedValues = 2u;

    schema_rule_t unsignedRule{};
    unsignedRule.allowedTypes = SCHEMA_TYPE_U64;
    unsignedRule.unsignedInteger.nMax = 10u;

    schema_rule_t floatRule{};
    floatRule.allowedTypes = SCHEMA_TYPE_F64;
    floatRule.floatingPoint.flMin = -1.0;
    floatRule.floatingPoint.flMax = 1.0;

    schema_rule_t binaryRule{};
    binaryRule.allowedTypes = SCHEMA_TYPE_BINARY;
    binaryRule.binary.cbMinSize = 2u;
    binaryRule.binary.cbMaxSize = 4u;

    schema_rule_t boolRule{};
    boolRule.allowedTypes = SCHEMA_TYPE_BOOL;

    schema_rule_t nonEmptyStringRule{};
    nonEmptyStringRule.allowedTypes = SCHEMA_TYPE_STRING;
    nonEmptyStringRule.string.cbMinLength = 1u;

    const schema_member_t members[]{
        { StringView_FromCString( "mode" ), &modeRule, SCHEMA_MEMBER_NONE },
        { StringView_FromCString( "unsigned" ), &unsignedRule, SCHEMA_MEMBER_NONE },
        { StringView_FromCString( "ratio" ), &floatRule, SCHEMA_MEMBER_NONE },
        { StringView_FromCString( "payload" ), &binaryRule, SCHEMA_MEMBER_NONE },
        { StringView_FromCString( "old" ), &boolRule, SCHEMA_MEMBER_DEPRECATED },
        { StringView_FromCString( "a/b~c" ), &nonEmptyStringRule, SCHEMA_MEMBER_NONE }
    };
    schema_rule_t rootRule{};
    rootRule.allowedTypes = SCHEMA_TYPE_OBJECT;
    rootRule.object.pMembers = members;
    rootRule.object.nMembers = sizeof( members ) / sizeof( members[0] );
    rootRule.object.flags = SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS;
    const schema_descriptor_t schema{
        StringView_FromCString( "cypher.constraints" ),
        1u,
        &rootRule
    };

    key_value_document_t *pDocument = ParseDocument(
        "@cykv 1\n@schema \"cypher.constraints\" 1\n"
        "{ mode = \"invalid\" unsigned = 11u ratio = 2.0 "
        "payload = hex\"00\" old = true \"a/b~c\" = \"\" }" );
    schema_diagnostic_t diagnostics[8]{};
    const schema_validation_result_t result = Schema_ValidateDocument(
        &schema,
        pDocument,
        {},
        diagnostics,
        sizeof( diagnostics ) / sizeof( diagnostics[0] ) );

    REQUIRE( result.status == schema_validation_status_t::INVALID_DOCUMENT );
    REQUIRE( result.nErrors == 5u );
    REQUIRE( result.nWarnings == 1u );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::STRING_VALUE,
        "/mode" ) );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::U64_RANGE,
        "/unsigned" ) );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::F64_RANGE,
        "/ratio" ) );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::BINARY_SIZE,
        "/payload" ) );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::DEPRECATED_MEMBER,
        "/old" ) );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::STRING_LENGTH,
        "/a~1b~0c" ) );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 enforces schema headers and validation traversal limits",
           "[CypherCommon][Tier2][Schema][Limits]" )
{
    key_value_document_t *pDocument = ParseDocument(
        "@cykv 1\n@schema \"cypher.project\" 2\n"
        "{ id = \"reap\" name = \"REAP\" "
        "start_map = \"maps/a.cymap\" }" );
    schema_diagnostic_t diagnostics[4]{};
    schema_validation_result_t result = Schema_ValidateDocument(
        ProjectSchema_V1(),
        pDocument,
        {},
        diagnostics,
        sizeof( diagnostics ) / sizeof( diagnostics[0] ) );
    REQUIRE( result.status == schema_validation_status_t::INVALID_DOCUMENT );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::SCHEMA_VERSION_MISMATCH,
        "/" ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseDocument(
        "@cykv 1\n@schema \"cypher.project\" 1\n"
        "{ id = \"reap\" name = \"REAP\" "
        "start_map = \"maps/a.cymap\" }" );
    schema_validation_options_t options{};
    options.nMaxNodes = 2u;
    result = Schema_ValidateDocument(
        ProjectSchema_V1(),
        pDocument,
        options,
        diagnostics,
        sizeof( diagnostics ) / sizeof( diagnostics[0] ) );
    REQUIRE( result.status == schema_validation_status_t::INVALID_DOCUMENT );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::NODE_LIMIT,
        "/name" ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    REQUIRE( KeyValue_SetDocumentHeader(
        pDocument,
        {
            CYKV_LANGUAGE_VERSION + 1u,
            StringView_FromCString( "cypher.project" ),
            CY_PROJECT_SCHEMA_VERSION
        } ) );
    REQUIRE( KeyValue_SetRootType(
        pDocument,
        key_value_type_t::OBJECT ) );
    result = Schema_ValidateDocument(
        ProjectSchema_V1(),
        pDocument,
        {},
        diagnostics,
        sizeof( diagnostics ) / sizeof( diagnostics[0] ) );
    REQUIRE( result.status == schema_validation_status_t::INVALID_DOCUMENT );
    REQUIRE( HasDiagnostic(
        diagnostics,
        result.nDiagnosticsWritten,
        schema_diagnostic_code_t::LANGUAGE_VERSION_MISMATCH,
        "/" ) );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 schema registry resolves exact document contracts",
           "[CypherCommon][Tier2][Schema][Registry]" )
{
    const schema_descriptor_t *storage[2]{};
    schema_registry_t registry{};
    REQUIRE( SchemaRegistry_Init(
        &registry,
        storage,
        sizeof( storage ) / sizeof( storage[0] ) ) );
    REQUIRE( SchemaRegistry_Register( &registry, ProjectSchema_V1() ) ==
             schema_registry_status_t::OK );
    REQUIRE( SchemaRegistry_Register( &registry, ProjectSchema_V1() ) ==
             schema_registry_status_t::DUPLICATE_SCHEMA );
    REQUIRE( SchemaRegistry_Find(
        &registry,
        StringView_FromCString( "cypher.project" ),
        1u ) == ProjectSchema_V1() );

    schema_descriptor_t projectV2 = *ProjectSchema_V1();
    projectV2.nVersion = 2u;
    REQUIRE( SchemaRegistry_Register( &registry, &projectV2 ) ==
             schema_registry_status_t::OK );
    REQUIRE( SchemaRegistry_FindLatest(
                 &registry,
                 StringView_FromCString( "cypher.project" ) ) == &projectV2 );
    REQUIRE( SchemaRegistry_FindLatest(
                 &registry,
                 StringView_FromCString( "cypher.unknown" ) ) == nullptr );

    schema_descriptor_t projectV3 = projectV2;
    projectV3.nVersion = 3u;
    REQUIRE( SchemaRegistry_Register( &registry, &projectV3 ) ==
             schema_registry_status_t::CAPACITY_EXCEEDED );

    key_value_document_t *pDocument = ParseDocument(
        "@cykv 1\n@schema \"cypher.unknown\" 1\n{}" );
    schema_diagnostic_t diagnostic{};
    const schema_validation_result_t result =
        SchemaRegistry_ValidateDocument(
            &registry,
            pDocument,
            {},
            &diagnostic,
            1u );
    REQUIRE( result.status == schema_validation_status_t::SCHEMA_NOT_FOUND );
    REQUIRE( result.nErrors == 1u );
    REQUIRE( diagnostic.code == schema_diagnostic_code_t::SCHEMA_NOT_FOUND );
    REQUIRE( StringView_Equals(
        StringView_FromCString( diagnostic.path ),
        StringView_FromCString( "/" ) ) );
    KeyValue_DestroyDocument( pDocument );

    SchemaRegistry_Clear( &registry );
    REQUIRE( registry.nCount == 0u );
}

TEST_CASE( "Tier2 schema utility names and type flags are stable",
           "[CypherCommon][Tier2][Schema][Contract]" )
{
    REQUIRE( Schema_TypeFlag( key_value_type_t::NULL_VALUE ) ==
             SCHEMA_TYPE_NULL );
    REQUIRE( Schema_TypeFlag( key_value_type_t::BOOL ) == SCHEMA_TYPE_BOOL );
    REQUIRE( Schema_TypeFlag( key_value_type_t::I64 ) == SCHEMA_TYPE_I64 );
    REQUIRE( Schema_TypeFlag( key_value_type_t::U64 ) == SCHEMA_TYPE_U64 );
    REQUIRE( Schema_TypeFlag( key_value_type_t::F64 ) == SCHEMA_TYPE_F64 );
    REQUIRE( Schema_TypeFlag( key_value_type_t::STRING ) ==
             SCHEMA_TYPE_STRING );
    REQUIRE( Schema_TypeFlag( key_value_type_t::BINARY ) ==
             SCHEMA_TYPE_BINARY );
    REQUIRE( Schema_TypeFlag( key_value_type_t::OBJECT ) ==
             SCHEMA_TYPE_OBJECT );
    REQUIRE( Schema_TypeFlag( key_value_type_t::ARRAY ) == SCHEMA_TYPE_ARRAY );
    REQUIRE( Schema_TypeFlag( static_cast<key_value_type_t>( 0xFFu ) ) ==
             SCHEMA_TYPE_NONE );

    REQUIRE(
        StringView_Equals(
            StringView_FromCString(
                Schema_DescriptorStatusName(
                    schema_descriptor_status_t::INVALID_RANGE ) ),
            StringView_FromCString( "INVALID_RANGE" ) ) );
    REQUIRE(
        StringView_Equals(
            StringView_FromCString(
                Schema_ValidationStatusName(
                    schema_validation_status_t::INVALID_DOCUMENT ) ),
            StringView_FromCString( "INVALID_DOCUMENT" ) ) );
    REQUIRE(
        StringView_Equals(
            StringView_FromCString(
                Schema_DiagnosticCodeName(
                    schema_diagnostic_code_t::MISSING_REQUIRED_MEMBER ) ),
            StringView_FromCString( "MISSING_REQUIRED_MEMBER" ) ) );
    REQUIRE(
        StringView_Equals(
            StringView_FromCString(
                SchemaRegistry_StatusName(
                    schema_registry_status_t::CAPACITY_EXCEEDED ) ),
            StringView_FromCString( "CAPACITY_EXCEEDED" ) ) );

    REQUIRE(
        StringView_Equals(
            StringView_FromCString(
                Schema_DescriptorStatusName(
                    static_cast<schema_descriptor_status_t>( 0xFFu ) ) ),
            StringView_FromCString( "UNKNOWN" ) ) );
    REQUIRE(
        StringView_Equals(
            StringView_FromCString(
                Schema_ValidationStatusName(
                    static_cast<schema_validation_status_t>( 0xFFu ) ) ),
            StringView_FromCString( "UNKNOWN" ) ) );
    REQUIRE(
        StringView_Equals(
            StringView_FromCString(
                Schema_DiagnosticCodeName(
                    static_cast<schema_diagnostic_code_t>( 0xFFu ) ) ),
            StringView_FromCString( "UNKNOWN" ) ) );
    REQUIRE(
        StringView_Equals(
            StringView_FromCString(
                SchemaRegistry_StatusName(
                    static_cast<schema_registry_status_t>( 0xFFu ) ) ),
            StringView_FromCString( "UNKNOWN" ) ) );
}
