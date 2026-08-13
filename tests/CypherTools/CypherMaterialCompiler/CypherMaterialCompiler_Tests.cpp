//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler_Tests.cpp
//  Purpose: Tests the complete `.cymat` source-to-cooked compiler path.
//  Details: Exercises CYKV decoding, typed resource dependencies, canonical
//           cooked output, ToolFramework records, dry runs, and diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMaterialCompiler.h"

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Blob.h"
#include "CypherCommon_CookedMaterial.h"
#include "CypherCommon_FileIo.h"
#include "CypherCommon_MemoryOps.h"
#include "CypherCommon_ToolFramework.h"
#include "CypherCommon_Vfs.h"
#include "CypherCommon_VfsDirectory.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cypher::common;
using namespace cypher::tools;

namespace
{

template <usize nExtent>
constexpr string_view_t TestText( const char ( &text )[nExtent] ) noexcept
{
    return { text, nExtent - 1u };
}

string_view_t View( const std::string &text ) noexcept
{
    return { text.data(), text.size() };
}

struct temporary_project_t {
    std::filesystem::path root{};
    std::filesystem::path source{};
    std::filesystem::path output{};
    std::string sourceText{};
    std::string outputText{};

    temporary_project_t()
    {
        static std::atomic<u64> nSequence{ 0u };
        root = std::filesystem::temp_directory_path() /
               ( "cypher_material_compiler_" +
                 std::to_string( nSequence.fetch_add( 1u ) ) );
        source = root / "source";
        output = root / "output";
        std::filesystem::create_directories( source / "materials" );
        std::filesystem::create_directories( source / "shaders" );
        std::filesystem::create_directories( source / "textures" );
        std::filesystem::create_directories( output / "materials" );
        sourceText = source.string();
        outputText = output.string();
    }

    temporary_project_t( const temporary_project_t & ) = delete;
    temporary_project_t &operator=( const temporary_project_t & ) = delete;

    ~temporary_project_t()
    {
        std::error_code error{};
        std::filesystem::remove_all( root, error );
    }

    void WriteText( const char *pRelativePath, const char *pText ) const
    {
        const std::filesystem::path path = source / pRelativePath;
        std::filesystem::create_directories( path.parent_path() );
        std::ofstream file( path, std::ios::binary | std::ios::trunc );
        REQUIRE( file.is_open() );
        file.write(
            pText,
            static_cast<std::streamsize>( std::strlen( pText ) ) );
        REQUIRE( file.good() );
    }

    std::string OutputPath( const char *pRelativePath ) const
    {
        return ( output / pRelativePath ).string();
    }
};

void WriteDependencies( const temporary_project_t &project )
{
    project.WriteText(
        "shaders/world.cyshader",
        "@cykv 1\n@schema \"cypher.shader\" 1\n"
        "{ language = \"glsl\" vertex = \"shaders/world.vert\" "
        "fragment = \"shaders/world.frag\" }\n" );
    project.WriteText(
        "textures/wall.cytex",
        "@cykv 1\n@schema \"cypher.texture\" 1\n"
        "{ source = \"textures/source/wall.png\" }\n" );
}

void WriteMaterial(
    const temporary_project_t &project,
    u32 nSchemaVersion = 1u )
{
    const std::string material =
        "@cykv 1\n@schema \"cypher.material\" " +
        std::to_string( nSchemaVersion ) +
        "\n{\n"
        "    shader = \"shaders/world.cyshader\"\n"
        "    textures = { AlbedoMap = \"textures/wall.cytex\" }\n"
        "    parameters = { Roughness = 0.5 Tint = [1, 0.75, 0.5] }\n"
        "}\n";
    project.WriteText( "materials/wall.cymat", material.c_str() );
}

struct host_capture_t {
    u32 nDiagnostics{ 0u };
    u32 nDependencies{ 0u };
    u32 nArtifacts{ 0u };
    tool_diagnostic_code_t lastDiagnostic{ CY_TOOL_DIAGNOSTIC_NONE };
    u32 nLastDiagnosticLine{ 0u };
    bool_t bSawShader{ CY_FALSE };
    bool_t bSawTexture{ CY_FALSE };
    bool_t bSawCompiler{ CY_FALSE };
    bool_t bCompleted{ CY_FALSE };
};

void CaptureDiagnostic(
    const tool_diagnostic_t &diagnostic,
    void *pUserData ) noexcept
{
    auto &capture = *static_cast<host_capture_t *>( pUserData );
    ++capture.nDiagnostics;
    capture.lastDiagnostic = diagnostic.code;
    capture.nLastDiagnosticLine = diagnostic.source.nLine;
}

void CaptureDependency(
    const tool_dependency_t &dependency,
    void *pUserData ) noexcept
{
    auto &capture = *static_cast<host_capture_t *>( pUserData );
    ++capture.nDependencies;
    capture.bSawShader |= StringView_Equals(
        dependency.path,
        TestText( "shaders/world.cyshader" ) );
    capture.bSawTexture |= StringView_Equals(
        dependency.path,
        TestText( "textures/wall.cytex" ) );
    capture.bSawCompiler |= StringView_Equals(
        dependency.path,
        TestText( "toolchain/cypher-material-compiler" ) );
}

void CaptureArtifact(
    const tool_artifact_t &,
    void *pUserData ) noexcept
{
    ++static_cast<host_capture_t *>( pUserData )->nArtifacts;
}

void CaptureProgress(
    const tool_progress_t &progress,
    void *pUserData ) noexcept
{
    if ( progress.state == tool_progress_state_t::COMPLETE ) {
        static_cast<host_capture_t *>( pUserData )->bCompleted = CY_TRUE;
    }
}

struct compiler_fixture_t {
    temporary_project_t &project;
    host_capture_t capture{};
    tool_application_desc_t application{};
    tool_command_desc_t command{};
    tool_context_t context{};
    vfs_directory_t sourceDirectory{};
    vfs_t sourceVfs{};
    tool_option_set_t options{};
    tool_host_t host{};
    string_view_t input{ TestText( "materials/wall.cymat" ) };
    tool_invocation_t invocation{};

    explicit compiler_fixture_t( temporary_project_t &projectIn )
        : project( projectIn )
    {
        application = {
            TestText( "cypher-resource-compiler" ),
            TestText( "CypherResourceCompiler" ),
            TestText( "Compiles Cypher resources." ),
            tool_delivery_t::COMMAND_LINE,
            1u,
            TOOL_APPLICATION_FLAG_HEADLESS
        };
        command = {
            TestText( "compile" ),
            TestText( "Compiles one resource." ),
            {},
            nullptr,
            0u,
            TOOL_COMMAND_FLAG_ACCEPTS_INPUTS |
                TOOL_COMMAND_FLAG_SUPPORTS_DRY_RUN
        };
        REQUIRE( ToolOptionSet_Init( &options, nullptr, 0u ) ==
                 tool_status_t::OK );
        host.pfnDiagnostic = &CaptureDiagnostic;
        host.pfnProgress = &CaptureProgress;
        host.pfnDependency = &CaptureDependency;
        host.pfnArtifact = &CaptureArtifact;
        host.pUserData = &capture;
        REQUIRE( VfsDirectory_Init(
                     &sourceDirectory,
                     View( project.sourceText ) ) == vfs_status_t::OK );
        sourceVfs = VfsDirectory_Make( &sourceDirectory );
        context = {
            application.id,
            {},
            View( project.sourceText ),
            View( project.sourceText ),
            View( project.outputText ),
            {},
            &sourceVfs,
            ToolTarget_Host(),
            tool_profile_t::DEVELOPMENT,
            1u,
            TOOL_CONTEXT_FLAG_AUTOMATION |
                TOOL_CONTEXT_FLAG_REPRODUCIBLE
        };
        invocation = {
            &application,
            &command,
            &context,
            &options,
            &input,
            1u,
            &host,
            {},
            TOOL_INVOCATION_FLAG_NONE
        };
    }

    ~compiler_fixture_t()
    {
        VfsDirectory_Shutdown( &sourceDirectory );
    }

    tool_status_t Compile(
        string_view_t output,
        tool_report_t &report,
        flags32_t flags = TOOL_INVOCATION_FLAG_NONE ) noexcept
    {
        invocation.flags = flags;
        const tool_compiler_desc_t *pCompiler =
            CypherMaterialCompiler_Descriptor();
        const tool_compile_request_t request{
            &invocation,
            29u,
            input,
            output,
            pCompiler->resourceType
        };
        return ToolCompiler_Execute( *pCompiler, request, &report );
    }
};

void ReadBlob( const std::string &path, blob_t &blob )
{
    REQUIRE( Blob_Init( &blob, Allocator_GetSystem() ) );
    REQUIRE( FileIo_ReadAllNative( View( path ), &blob ) );
}

} // namespace

TEST_CASE( "Material compiler descriptor owns `.cymat` inputs",
           "[CypherTools][MaterialCompiler][Descriptor]" )
{
    const tool_compiler_desc_t *pCompiler =
        CypherMaterialCompiler_Descriptor();
    REQUIRE( pCompiler != nullptr );
    CHECK( ToolCompiler_CheckDescriptor( *pCompiler ) == tool_status_t::OK );
    CHECK( ToolCompiler_SupportsInput(
        *pCompiler,
        TestText( "materials/wall.cymat" ) ) );
    CHECK_FALSE( ToolCompiler_SupportsInput(
        *pCompiler,
        TestText( "textures/wall.cytex" ) ) );
}

TEST_CASE( "Material compiler validates dependencies and emits cooked data",
           "[CypherTools][MaterialCompiler][Integration]" )
{
    temporary_project_t project{};
    WriteDependencies( project );
    WriteMaterial( project );
    compiler_fixture_t fixture{ project };
    tool_report_t report{};

    REQUIRE( fixture.Compile(
                 TestText( "materials/wall.cymat_c" ),
                 report ) == tool_status_t::OK );
    CHECK( report.nSucceeded == 1u );
    CHECK( report.nArtifacts == 1u );
    CHECK( fixture.capture.nDependencies == 4u );
    CHECK( fixture.capture.bSawShader );
    CHECK( fixture.capture.bSawTexture );
    CHECK( fixture.capture.bSawCompiler );
    CHECK( fixture.capture.bCompleted );

    blob_t cooked{};
    ReadBlob( project.OutputPath( "materials/wall.cymat_c" ), cooked );
    cooked_material_view_t material{};
    REQUIRE( CookedMaterial_Succeeded(
        CookedMaterial_Read( Blob_Block( &cooked ), &material ) ) );
    CHECK( StringView_Equals(
        material.shader,
        TestText( "shaders/world.cyshader" ) ) );
    REQUIRE( CookedMaterial_FindTexture(
        material,
        TestText( "AlbedoMap" ) ) != nullptr );
    const cooked_material_parameter_view_t *pRoughness =
        CookedMaterial_FindParameter( material, TestText( "Roughness" ) );
    REQUIRE( pRoughness != nullptr );
    CHECK( pRoughness->values[0] == 0.5 );
}

TEST_CASE( "Material compiler dry runs validate without publishing",
           "[CypherTools][MaterialCompiler][DryRun]" )
{
    temporary_project_t project{};
    WriteDependencies( project );
    WriteMaterial( project );
    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
                 {},
                 report,
                 TOOL_INVOCATION_FLAG_DRY_RUN ) == tool_status_t::OK );
    CHECK( report.nSucceeded == 1u );
    CHECK( report.nArtifacts == 0u );
    CHECK( report.cbWritten == 0u );
    CHECK( fixture.capture.nArtifacts == 0u );
    CHECK( fixture.capture.nDependencies == 4u );
}

TEST_CASE( "Material compiler rejects missing and malformed dependencies",
           "[CypherTools][MaterialCompiler][Dependencies][Failure]" )
{
    SECTION( "missing shader" )
    {
        temporary_project_t project{};
        project.WriteText(
            "textures/wall.cytex",
            "@cykv 1\n@schema \"cypher.texture\" 1\n"
            "{ source = \"textures/source/wall.png\" }\n" );
        WriteMaterial( project );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/wall.cymat_c" ),
                   report ) == tool_status_t::IO_ERROR );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_READ_FAILED );
    }

    SECTION( "texture recipe has wrong schema" )
    {
        temporary_project_t project{};
        WriteDependencies( project );
        project.WriteText(
            "textures/wall.cytex",
            "@cykv 1\n@schema \"cypher.material\" 1\n"
            "{ shader = \"shaders/world.cyshader\" }\n" );
        WriteMaterial( project );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/wall.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED );
    }
}

TEST_CASE( "Material compiler reports schema header locations",
           "[CypherTools][MaterialCompiler][Diagnostics]" )
{
    temporary_project_t project{};
    WriteDependencies( project );
    WriteMaterial( project, 2u );
    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    CHECK( fixture.Compile(
               TestText( "materials/wall.cymat_c" ),
               report ) == tool_status_t::VALIDATION_FAILED );
    CHECK( fixture.capture.lastDiagnostic ==
           CY_MATERIAL_DIAGNOSTIC_SCHEMA_FAILED );
    CHECK( fixture.capture.nLastDiagnosticLine == 2u );
}
