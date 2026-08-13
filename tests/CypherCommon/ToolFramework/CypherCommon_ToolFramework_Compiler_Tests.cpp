//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/ToolFramework/CypherCommon_ToolFramework_Compiler_Tests.cpp
//  Purpose: Verifies reusable compiler registration, dispatch, and reporting.
//  Details: Tests extension matching, ambiguous ownership, request validation,
//           host report delivery, input-set policy, and callback result handling.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolFramework.h"

#include "CypherCommon_Blob.h"
#include "CypherCommon_FileIo.h"
#include "CypherCommon_MemoryOps.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <string>

using namespace cypher::common;

namespace
{

struct compiler_state_t {
    u32 cExecute{ 0u };
    u32 cReports{ 0u };
    tool_status_t reportedStatus{ tool_status_t::INTERNAL_ERROR };
};

tool_status_t ExecuteCompiler(
    const tool_compile_request_t &,
    tool_report_t *pReport,
    void *pUserData ) noexcept
{
    auto *pState = static_cast<compiler_state_t *>( pUserData );
    ++pState->cExecute;
    pReport->nInputsDiscovered = 1u;
    pReport->nInputsProcessed = 1u;
    pReport->nSucceeded = 1u;
    pReport->nArtifacts = 1u;
    pReport->cbRead = 128u;
    pReport->cbWritten = 64u;
    return tool_status_t::OK;
}

void CaptureReport( const tool_report_t &report, void *pUserData ) noexcept
{
    auto *pState = static_cast<compiler_state_t *>( pUserData );
    ++pState->cReports;
    pState->reportedStatus = report.status;
}

tool_application_desc_t MakeApplication() noexcept
{
    return {
        StringView_FromCString( "cypher-resource-compiler" ),
        StringView_FromCString( "CypherResourceCompiler" ),
        StringView_FromCString( "Compiles Cypher resources." ),
        tool_delivery_t::COMMAND_LINE,
        1u,
        TOOL_APPLICATION_FLAG_HEADLESS
    };
}

tool_command_desc_t MakeCommand() noexcept
{
    return {
        StringView_FromCString( "compile" ),
        StringView_FromCString( "Compiles one or more resources." ),
        {},
        nullptr,
        0u,
        TOOL_COMMAND_FLAG_ACCEPTS_INPUTS |
            TOOL_COMMAND_FLAG_ALLOW_MULTIPLE_INPUTS
    };
}

tool_compiler_desc_t MakeCompiler(
    compiler_state_t *pState,
    string_view_t id,
    const string_view_t *pExtensions ) noexcept
{
    return {
        id,
        StringView_FromCString( "Material compiler" ),
        StringView_FromCString( "material" ),
        StringView_FromCString( ".cymat_c" ),
        pExtensions,
        1u,
        1u,
        1u,
        TOOL_COMPILER_FLAG_DETERMINISTIC |
            TOOL_COMPILER_FLAG_SUPPORTS_DRY_RUN,
        nullptr,
        &ExecuteCompiler,
        pState
    };
}

std::string PathToUtf8( const std::filesystem::path &path )
{
    const std::u8string bytes = path.u8string();
    return {
        reinterpret_cast<const char *>( bytes.data() ),
        bytes.size()
    };
}

struct temporary_artifact_directory_t {
    temporary_artifact_directory_t()
    {
        static std::atomic<u64> nSequence{ 0u };
        root = std::filesystem::temp_directory_path() /
            ( "cypher_tool_artifact_" +
              std::to_string( nSequence.fetch_add( 1u ) ) );
        std::filesystem::remove_all( root );
    }

    ~temporary_artifact_directory_t()
    {
        std::error_code error{};
        std::filesystem::remove_all( root, error );
    }

    std::filesystem::path root{};
};

} // namespace

TEST_CASE( "Compiler registry rejects duplicate IDs and ambiguous input ownership",
           "[CypherCommon][ToolFramework][Compiler]" )
{
    compiler_state_t state{};
    const string_view_t extensions[]{ StringView_FromCString( ".cymat" ) };
    const tool_compiler_desc_t compilerA = MakeCompiler(
        &state,
        StringView_FromCString( "material" ),
        extensions );
    const tool_compiler_desc_t compilerB = MakeCompiler(
        &state,
        StringView_FromCString( "material-alternate" ),
        extensions );
    const tool_compiler_desc_t *storage[2]{};
    tool_compiler_registry_t registry{};
    REQUIRE( ToolCompilerRegistry_Init( &registry, storage, 2u ) ==
             tool_status_t::OK );
    REQUIRE( ToolCompilerRegistry_Register( &registry, &compilerA ) ==
             tool_status_t::OK );
    REQUIRE( ToolCompilerRegistry_Register( &registry, &compilerA ) ==
             tool_status_t::ALREADY_EXISTS );
    REQUIRE( ToolCompilerRegistry_Register( &registry, &compilerB ) ==
             tool_status_t::OK );

    const tool_compiler_desc_t *pFound = nullptr;
    REQUIRE( ToolCompilerRegistry_FindForInput(
                 &registry,
                 StringView_FromCString( "materials/wall.CYMAT" ),
                 &pFound ) == tool_status_t::INVALID_CONFIGURATION );
    REQUIRE( pFound == nullptr );
}

TEST_CASE( "Compiler execution validates the request and emits one final report",
           "[CypherCommon][ToolFramework][Compiler]" )
{
    compiler_state_t state{};
    const string_view_t extensions[]{ StringView_FromCString( ".cymat" ) };
    const tool_compiler_desc_t compiler = MakeCompiler(
        &state,
        StringView_FromCString( "material" ),
        extensions );
    const tool_application_desc_t application = MakeApplication();
    const tool_command_desc_t command = MakeCommand();
    const tool_context_t context{
        application.id,
        {},
        StringView_FromCString( "." ),
        StringView_FromCString( "assets" ),
        StringView_FromCString( "cooked" ),
        StringView_FromCString( "cache" ),
        nullptr,
        ToolTarget_Host(),
        tool_profile_t::DEVELOPMENT,
        1u,
        TOOL_CONTEXT_FLAG_AUTOMATION
    };
    tool_option_set_t optionSet{};
    REQUIRE( ToolOptionSet_Init( &optionSet, nullptr, 0u ) == tool_status_t::OK );
    tool_host_t host{};
    host.pfnReport = &CaptureReport;
    host.pUserData = &state;
    const string_view_t input = StringView_FromCString( "wall.cymat" );
    const tool_invocation_t invocation{
        &application,
        &command,
        &context,
        &optionSet,
        &input,
        1u,
        &host,
        {},
        TOOL_INVOCATION_FLAG_NONE
    };
    const tool_compile_request_t request{
        &invocation,
        42u,
        input,
        StringView_FromCString( "wall.cymat_c" ),
        compiler.resourceType
    };
    tool_report_t report{};

    REQUIRE( ToolCompiler_Execute( compiler, request, &report ) ==
             tool_status_t::OK );
    CHECK( state.cExecute == 1u );
    CHECK( state.cReports == 1u );
    CHECK( state.reportedStatus == tool_status_t::OK );
    CHECK( report.operationId == 42u );
    CHECK( report.nInputsProcessed == 1u );
    CHECK( report.nArtifacts == 1u );
}

TEST_CASE( "Input sets validate traversal policy and apply filters",
           "[CypherCommon][ToolFramework][Compiler]" )
{
    tool_input_t storage[2]{};
    tool_input_set_t inputs{};
    REQUIRE( ToolInputSet_Init( &inputs, storage, 2u ) == tool_status_t::OK );

    const tool_input_t invalid{
        StringView_FromCString( "asset.cymat" ),
        {},
        tool_input_kind_t::FILE,
        TOOL_INPUT_FLAG_RECURSIVE
    };
    REQUIRE( ToolInputSet_Add( &inputs, invalid ) ==
             tool_status_t::INVALID_CONFIGURATION );

    const tool_input_t directory{
        StringView_FromCString( "materials" ),
        StringView_FromCString( "assets" ),
        tool_input_kind_t::DIRECTORY,
        TOOL_INPUT_FLAG_REQUIRED | TOOL_INPUT_FLAG_RECURSIVE
    };
    REQUIRE( ToolInputSet_Add( &inputs, directory ) == tool_status_t::OK );

    const string_view_t includes[]{ StringView_FromCString( "*.cymat" ) };
    const string_view_t excludes[]{ StringView_FromCString( "*temp*" ) };
    const path_filter_t filter{
        includes,
        1u,
        excludes,
        1u,
        PATH_MATCH_FLAG_STAR_CROSSES_SEPARATOR
    };
    REQUIRE( ToolInputSet_SetFilter( &inputs, filter ) == tool_status_t::OK );
    CHECK( ToolInputSet_AcceptsPath(
        &inputs,
        StringView_FromCString( "materials/wall.cymat" ) ) );
    CHECK_FALSE( ToolInputSet_AcceptsPath(
        &inputs,
        StringView_FromCString( "materials/temp_wall.cymat" ) ) );
    CHECK_FALSE( ToolInputSet_AcceptsPath(
        &inputs,
        StringView_FromCString( "materials/wall.png" ) ) );
}

TEST_CASE( "Artifact writer creates parents and transactionally replaces output",
           "[CypherCommon][ToolFramework][Compiler]" )
{
    temporary_artifact_directory_t temporary{};
    const std::filesystem::path output =
        temporary.root / "nested" / "asset.cyshader_c";
    const std::string outputText = PathToUtf8( output );
    const string_view_t outputView{ outputText.data(), outputText.size() };
    const byte first[]{ 1u, 2u, 3u };
    const byte second[]{ 4u, 5u, 6u, 7u };

    REQUIRE( ToolArtifactWriter_WriteNative(
        outputView,
        { first, sizeof( first ) } ) == tool_status_t::OK );
    REQUIRE( ToolArtifactWriter_WriteNative(
        outputView,
        { second, sizeof( second ) } ) == tool_status_t::OK );

    blob_t contents{};
    REQUIRE( Blob_Init( &contents, Allocator_GetSystem() ) );
    REQUIRE( FileIo_ReadAllNative( outputView, &contents ) );
    REQUIRE( Blob_Size( &contents ) == sizeof( second ) );
    REQUIRE( Cy_MemCompare(
        Blob_Data( &contents ),
        second,
        sizeof( second ) ) == 0 );

    const std::string prefix = output.filename().string() + ".cytmp.";
    for ( const std::filesystem::directory_entry &entry :
          std::filesystem::directory_iterator( output.parent_path() ) ) {
        CHECK( entry.path().filename().string().find( prefix ) != 0u );
    }
}
