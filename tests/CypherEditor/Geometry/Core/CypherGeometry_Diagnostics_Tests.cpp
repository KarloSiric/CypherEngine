//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Diagnostics_Tests.cpp
//  Purpose: Verifies bounded structured geometry diagnostic contracts.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Diagnostics.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace cypher::editor::geometry
{
namespace
{

constexpr geometry_diagnostic_code_t kValidationCodeA =
    GeometryDiagnosticCode_Make(
        geometry_diagnostic_module_t::VALIDATION,
        1u );
constexpr geometry_diagnostic_code_t kValidationCodeB =
    GeometryDiagnosticCode_Make(
        geometry_diagnostic_module_t::VALIDATION,
        2u );
constexpr geometry_diagnostic_code_t kCoreCode =
    GeometryDiagnosticCode_Make(
        geometry_diagnostic_module_t::CORE,
        1u );

geometry_diagnostic_t MakeDiagnostic(
    geometry_diagnostic_code_t code,
    geometry_diagnostic_module_t module,
    geometry_diagnostic_severity_t severity )
{
    geometry_diagnostic_t diagnostic{};
    diagnostic.code = code;
    diagnostic.module = module;
    diagnostic.severity = severity;
    return diagnostic;
}

} // namespace

TEST_CASE( "geometry diagnostic codes retain stable module qualification",
           "[editor][geometry][diagnostics]" )
{
    STATIC_REQUIRE( kValidationCodeA == 0x04000001u );
    STATIC_REQUIRE( GeometryDiagnosticCode_IsValid( kValidationCodeA ) );
    STATIC_REQUIRE( GeometryDiagnosticCode_Module( kValidationCodeA ) ==
                    geometry_diagnostic_module_t::VALIDATION );
    STATIC_REQUIRE( GeometryDiagnosticCode_Local( kValidationCodeA ) == 1u );
    STATIC_REQUIRE( GeometryDiagnosticCode_Make(
                        geometry_diagnostic_module_t::INVALID,
                        1u ) == GEOMETRY_DIAGNOSTIC_CODE_NONE );
    STATIC_REQUIRE( GeometryDiagnosticCode_Make(
                        geometry_diagnostic_module_t::CORE,
                        0u ) == GEOMETRY_DIAGNOSTIC_CODE_NONE );
}

TEST_CASE( "geometry diagnostic targets use persistent representation-qualified identities",
           "[editor][geometry][diagnostics]" )
{
    const geometry_diagnostic_target_t meshVertex =
        GeometryDiagnosticTarget_Component(
            geometry_source_representation_kind_t::EDITABLE_MESH,
            geometry_source_id_t{ 10u },
            geometry_source_id_t{ 11u },
            1u );
    const geometry_diagnostic_target_t planarVertex =
        GeometryDiagnosticTarget_Component(
            geometry_source_representation_kind_t::PLANAR_REGION,
            geometry_source_id_t{ 20u },
            geometry_source_id_t{ 21u },
            1u );

    REQUIRE( GeometryDiagnosticTarget_IsValid( meshVertex ) );
    REQUIRE( GeometryDiagnosticTarget_IsValid( planarVertex ) );
    REQUIRE( meshVertex.representation != planarVertex.representation );
    REQUIRE( meshVertex.componentCode == planarVertex.componentCode );
    REQUIRE( GeometryDiagnosticTarget_IsValid(
        GeometryDiagnosticTarget_Root(
            geometry_source_representation_kind_t::BRUSH_SOLID,
            geometry_source_id_t{ 30u } ) ) );

    STATIC_REQUIRE( std::is_trivially_copyable_v<geometry_diagnostic_target_t> );
}

TEST_CASE( "geometry diagnostic initialization is failure atomic and allocation free",
           "[editor][geometry][diagnostics]" )
{
    geometry_diagnostic_t callerStorage[2]{};
    geometry_diagnostic_buffer_t buffer{};

    REQUIRE( GeometryDiagnosticBuffer_Init(
                 &buffer,
                 common::Span_FromArray( callerStorage ) ) ==
             geometry_status_t::OK );
    REQUIRE( buffer.storage.pData == callerStorage );
    REQUIRE( GeometryDiagnosticBuffer_Capacity( &buffer ) == 2u );

    const geometry_diagnostic_buffer_t before = buffer;
    const span_t<geometry_diagnostic_t> invalidStorage{ nullptr, 1u };
    REQUIRE( GeometryDiagnosticBuffer_Init( &buffer, invalidStorage ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( buffer.storage.pData == before.storage.pData );
    REQUIRE( buffer.storage.nCount == before.storage.nCount );
    REQUIRE( buffer.cStored == before.cStored );
    REQUIRE( buffer.cTruncated == before.cTruncated );
    REQUIRE( buffer.bInitialized == before.bInitialized );

    REQUIRE( GeometryDiagnosticBuffer_Init(
                 &buffer,
                 common::Span_FromArray( callerStorage ) ) ==
             geometry_status_t::ALREADY_INITIALIZED );
    REQUIRE( GeometryDiagnosticBuffer_Init( nullptr, {} ) ==
             geometry_status_t::INVALID_ARGUMENT );

    geometry_diagnostic_buffer_t corrupt = buffer;
    corrupt.storage = { nullptr, 1u };
    REQUIRE( GeometryDiagnosticBuffer_Init( &corrupt, {} ) ==
             geometry_status_t::CORRUPT_STATE );
    REQUIRE( corrupt.storage.pData == nullptr );
    REQUIRE( corrupt.storage.nCount == 1u );
}

TEST_CASE( "geometry diagnostic buffer preserves append order and reports truncation",
           "[editor][geometry][diagnostics]" )
{
    geometry_diagnostic_t callerStorage[2]{};
    geometry_diagnostic_buffer_t buffer{};
    REQUIRE( GeometryDiagnosticBuffer_Init(
                 &buffer,
                 common::Span_FromArray( callerStorage ) ) ==
             geometry_status_t::OK );

    const geometry_diagnostic_t first = MakeDiagnostic(
        kValidationCodeA,
        geometry_diagnostic_module_t::VALIDATION,
        geometry_diagnostic_severity_t::WARNING );
    const geometry_diagnostic_t second = MakeDiagnostic(
        kCoreCode,
        geometry_diagnostic_module_t::CORE,
        geometry_diagnostic_severity_t::ERROR );
    const geometry_diagnostic_t third = MakeDiagnostic(
        kValidationCodeB,
        geometry_diagnostic_module_t::VALIDATION,
        geometry_diagnostic_severity_t::NOTE );

    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, first ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, second ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, third ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, third ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );

    const span_t<const geometry_diagnostic_t> records =
        GeometryDiagnosticBuffer_Records( &buffer );
    REQUIRE( records.nCount == 2u );
    REQUIRE( records.pData[0].code == first.code );
    REQUIRE( records.pData[1].code == second.code );
    REQUIRE( GeometryDiagnosticBuffer_TruncatedCount( &buffer ) == 2u );
}

TEST_CASE( "invalid geometry diagnostics neither publish nor consume budget",
           "[editor][geometry][diagnostics]" )
{
    geometry_diagnostic_t callerStorage[1]{};
    geometry_diagnostic_buffer_t buffer{};
    REQUIRE( GeometryDiagnosticBuffer_Init(
                 &buffer,
                 common::Span_FromArray( callerStorage ) ) ==
             geometry_status_t::OK );

    geometry_diagnostic_t invalid = MakeDiagnostic(
        kValidationCodeA,
        geometry_diagnostic_module_t::CORE,
        geometry_diagnostic_severity_t::ERROR );
    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, invalid ) ==
             geometry_status_t::INVALID_ARGUMENT );

    invalid = MakeDiagnostic(
        kValidationCodeA,
        geometry_diagnostic_module_t::VALIDATION,
        geometry_diagnostic_severity_t::COUNT );
    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, invalid ) ==
             geometry_status_t::INVALID_ARGUMENT );

    invalid = MakeDiagnostic(
        kValidationCodeA,
        geometry_diagnostic_module_t::VALIDATION,
        geometry_diagnostic_severity_t::ERROR );
    invalid.target = GeometryDiagnosticTarget_Component(
        geometry_source_representation_kind_t::EDITABLE_MESH,
        geometry_source_id_t{ 8u },
        GEOMETRY_SOURCE_ID_INVALID,
        1u );
    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, invalid ) ==
             geometry_status_t::INVALID_ARGUMENT );

    invalid = MakeDiagnostic(
        kValidationCodeA,
        geometry_diagnostic_module_t::VALIDATION,
        geometry_diagnostic_severity_t::ERROR );
    invalid.related = GeometryDiagnosticTarget_Root(
        geometry_source_representation_kind_t::BRUSH_SOLID,
        geometry_source_id_t{ 12u } );
    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, invalid ) ==
             geometry_status_t::INVALID_ARGUMENT );

    REQUIRE( GeometryDiagnosticBuffer_Count( &buffer ) == 0u );
    REQUIRE( GeometryDiagnosticBuffer_TruncatedCount( &buffer ) == 0u );
    REQUIRE( GeometryDiagnosticBuffer_Append( nullptr, invalid ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "geometry diagnostic storage clears and reuses the same caller budget",
           "[editor][geometry][diagnostics]" )
{
    geometry_diagnostic_t callerStorage[1]{};
    geometry_diagnostic_buffer_t buffer{};
    REQUIRE( GeometryDiagnosticBuffer_Init(
                 &buffer,
                 common::Span_FromArray( callerStorage ) ) ==
             geometry_status_t::OK );

    const geometry_diagnostic_t first = MakeDiagnostic(
        kCoreCode,
        geometry_diagnostic_module_t::CORE,
        geometry_diagnostic_severity_t::WARNING );
    const geometry_diagnostic_t second = MakeDiagnostic(
        kValidationCodeA,
        geometry_diagnostic_module_t::VALIDATION,
        geometry_diagnostic_severity_t::ERROR );
    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, first ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, second ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );

    geometry_diagnostic_t *const pStorage = buffer.storage.pData;
    REQUIRE( GeometryDiagnosticBuffer_Clear( &buffer ) ==
             geometry_status_t::OK );
    REQUIRE( buffer.storage.pData == pStorage );
    REQUIRE( GeometryDiagnosticBuffer_Count( &buffer ) == 0u );
    REQUIRE( GeometryDiagnosticBuffer_TruncatedCount( &buffer ) == 0u );

    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, second ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDiagnosticBuffer_Records( &buffer ).pData[0].code ==
             second.code );

    GeometryDiagnosticBuffer_Shutdown( &buffer );
    REQUIRE_FALSE( GeometryDiagnosticBuffer_IsValid( &buffer ) );
    REQUIRE( GeometryDiagnosticBuffer_Clear( &buffer ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( GeometryDiagnosticBuffer_Clear( nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "zero-budget geometry diagnostics count every valid dropped record",
           "[editor][geometry][diagnostics]" )
{
    geometry_diagnostic_buffer_t buffer{};
    REQUIRE( GeometryDiagnosticBuffer_Init( &buffer, {} ) ==
             geometry_status_t::OK );

    const geometry_diagnostic_t diagnostic = MakeDiagnostic(
        kCoreCode,
        geometry_diagnostic_module_t::CORE,
        geometry_diagnostic_severity_t::ERROR );
    REQUIRE( GeometryDiagnosticBuffer_Append( &buffer, diagnostic ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE( GeometryDiagnosticBuffer_Count( &buffer ) == 0u );
    REQUIRE( GeometryDiagnosticBuffer_TruncatedCount( &buffer ) == 1u );
}

TEST_CASE( "geometry diagnostic truncation saturates and corrupt counts fail closed",
           "[editor][geometry][diagnostics]" )
{
    const geometry_diagnostic_t diagnostic = MakeDiagnostic(
        kCoreCode,
        geometry_diagnostic_module_t::CORE,
        geometry_diagnostic_severity_t::ERROR );

    geometry_diagnostic_buffer_t zeroBudget{};
    REQUIRE( GeometryDiagnosticBuffer_Init( &zeroBudget, {} ) ==
             geometry_status_t::OK );
    zeroBudget.cTruncated = common::CY_U64_MAX;
    REQUIRE( GeometryDiagnosticBuffer_Append( &zeroBudget, diagnostic ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE( zeroBudget.cTruncated == common::CY_U64_MAX );

    geometry_diagnostic_t storage[1]{};
    geometry_diagnostic_buffer_t corrupt{};
    REQUIRE( GeometryDiagnosticBuffer_Init(
                 &corrupt,
                 common::Span_FromArray( storage ) ) == geometry_status_t::OK );
    corrupt.cStored = 2u;
    const geometry_diagnostic_buffer_t before = corrupt;
    REQUIRE_FALSE( GeometryDiagnosticBuffer_IsValid( &corrupt ) );
    REQUIRE_FALSE( GeometryDiagnosticBuffer_ValidateDeep( &corrupt ) );
    REQUIRE( GeometryDiagnosticBuffer_Clear( &corrupt ) ==
             geometry_status_t::CORRUPT_STATE );
    REQUIRE( GeometryDiagnosticBuffer_Append( &corrupt, diagnostic ) ==
             geometry_status_t::CORRUPT_STATE );
    REQUIRE( corrupt.cStored == before.cStored );
    REQUIRE( corrupt.cTruncated == before.cTruncated );
}

TEST_CASE( "geometry diagnostic deep validation detects caller mutation",
           "[editor][geometry][diagnostics]" )
{
    geometry_diagnostic_t storage[1]{};
    geometry_diagnostic_buffer_t buffer{};
    REQUIRE( GeometryDiagnosticBuffer_Init(
                 &buffer,
                 common::Span_FromArray( storage ) ) == geometry_status_t::OK );
    REQUIRE( GeometryDiagnosticBuffer_Append(
                 &buffer,
                 MakeDiagnostic(
                     kValidationCodeA,
                     geometry_diagnostic_module_t::VALIDATION,
                     geometry_diagnostic_severity_t::WARNING ) ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDiagnosticBuffer_ValidateDeep( &buffer ) );

    storage[0].module = geometry_diagnostic_module_t::CORE;
    REQUIRE( GeometryDiagnosticBuffer_IsValid( &buffer ) );
    REQUIRE_FALSE( GeometryDiagnosticBuffer_ValidateDeep( &buffer ) );
}

} // namespace cypher::editor::geometry
