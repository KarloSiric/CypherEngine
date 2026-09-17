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

void WriteTextureV2(
    const temporary_project_t &project,
    const char *pPath,
    const char *pUsage = "color",
    const char *pColorSpace = "srgb" )
{
    const std::string recipe =
        "@cykv 1\n"
        "@schema \"cypher.texture\" 2\n"
        "{\n"
        "    source = \"textures/source/test.png\"\n"
        "    type = \"2d\"\n"
        "    usage = \"" + std::string( pUsage ) + "\"\n"
        "    color_space = \"" + std::string( pColorSpace ) + "\"\n"
        "}\n";
    project.WriteText( pPath, recipe.c_str() );
}

void WriteShaderV2(
    const temporary_project_t &project,
    const char *pAdditionalInterface = "",
    const char *pAdditionalRoot = "" )
{
    const std::string recipe =
        "@cykv 1\n"
        "@schema \"cypher.shader\" 2\n"
        "{\n"
        "    language = \"glsl\"\n"
        "    stages = {\n"
        "        vertex = { source = \"shaders/world.vert\" }\n"
        "        fragment = { source = \"shaders/world.frag\" }\n"
        "    }\n"
        "    interface = {\n"
        "        textures = {\n"
        "            albedo_texture = {\n"
        "                type = \"texture2d\"\n"
        "                usage = \"color\"\n"
        "                color_space = \"srgb\"\n"
        "                required = true\n"
        "            }\n"
        "            detail_texture = {\n"
        "                type = \"texture2d\"\n"
        "                usage = \"data\"\n"
        "                color_space = \"linear\"\n"
        "                required = false\n"
        "            }\n"
        "        }\n"
        "        parameters = {\n"
        "            enabled = { type = \"bool\" required = true }\n"
        "            count = { type = \"i32\" default = 3 minimum = 0 maximum = 10 }\n"
        "            roughness = { type = \"f32\" default = 0.5 minimum = 0 maximum = 1 }\n"
        "            tint = { type = \"color3\" default = [1, 1, 1] }\n"
        "            transform = { type = \"mat4\" required = false }\n"
        "        }\n" + std::string( pAdditionalInterface ) +
        "    }\n" + std::string( pAdditionalRoot ) +
        "}\n";
    project.WriteText( "shaders/world.cyshader", recipe.c_str() );
}

void WriteMaterialV2(
    const temporary_project_t &project,
    const char *pPath,
    const char *pBody )
{
    const std::string recipe =
        "@cykv 1\n"
        "@schema \"cypher.material\" 2\n"
        "{\n" + std::string( pBody ) + "\n}\n";
    project.WriteText( pPath, recipe.c_str() );
}

void WriteV2Dependencies( const temporary_project_t &project )
{
    WriteShaderV2( project );
    WriteTextureV2( project, "textures/albedo.cytex" );
    WriteTextureV2(
        project,
        "textures/detail.cytex",
        "data",
        "linear" );
}

struct host_capture_t {
    u32 nDiagnostics{ 0u };
    u32 nDependencies{ 0u };
    u32 nArtifacts{ 0u };
    tool_diagnostic_code_t lastDiagnostic{ CY_TOOL_DIAGNOSTIC_NONE };
    u32 nLastDiagnosticLine{ 0u };
    bool_t bSawShader{ CY_FALSE };
    bool_t bSawTexture{ CY_FALSE };
    bool_t bSawBase{ CY_FALSE };
    bool_t bSawMidBase{ CY_FALSE };
    bool_t bSawTransitiveBase{ CY_FALSE };
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
    capture.bSawTexture |= StringView_Equals(
        dependency.path,
        TestText( "textures/albedo.cytex" ) );
    capture.bSawBase |= StringView_Equals(
        dependency.path,
        TestText( "materials/base.cymat" ) );
    capture.bSawMidBase |= StringView_Equals(
        dependency.path,
        TestText( "materials/mid.cymat" ) );
    if ( StringView_Equals(
             dependency.path,
             TestText( "materials/base.cymat" ) ) ) {
        capture.bSawTransitiveBase |=
            ( dependency.flags & TOOL_DEPENDENCY_FLAG_TRANSITIVE ) != 0u;
    }
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
    CHECK( pCompiler->nCompilerVersion == CY_MATERIAL_COMPILER_VERSION );
    CHECK( pCompiler->nCompilerVersion == 2u );
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
    CHECK( material.nResourceVersion ==
           CY_COOKED_MATERIAL_RESOURCE_VERSION_V1 );
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

TEST_CASE( "Material compiler ignores authoring-only CYKV formatting",
           "[CypherTools][MaterialCompiler][Determinism][CYKV]" )
{
    temporary_project_t project{};
    WriteDependencies( project );
    WriteMaterial( project );
    compiler_fixture_t fixture{ project };
    tool_report_t firstReport{};
    tool_report_t reformattedReport{};
    REQUIRE( fixture.Compile(
        TestText( "materials/canonical.cymat_c" ),
        firstReport ) == tool_status_t::OK );

    project.WriteText(
        "materials/wall.cymat",
        "@cykv 1\n"
        "@schema \"cypher.material\" 1\n"
        "{ // Member order and whitespace do not change an asset.\n"
        "    parameters = { Tint = [ 1, 0.75, 0.5 ] Roughness = 0.5 }\n"
        "    textures = { AlbedoMap = \"textures/wall.cytex\" }\n"
        "    shader = \"shaders/world.cyshader\"\n"
        "}\n" );
    project.WriteText(
        "shaders/world.cyshader",
        "@cykv 1\n@schema \"cypher.shader\" 1\n"
        "{ // Dependency formatting is also excluded from identity.\n"
        "  fragment = \"shaders/world.frag\"\n"
        "  vertex = \"shaders/world.vert\"\n"
        "  language = \"glsl\"\n}\n" );
    project.WriteText(
        "textures/wall.cytex",
        "@cykv 1\n@schema \"cypher.texture\" 1\n"
        "{ // Same dependency semantics, different source text.\n"
        "  source = \"textures/source/wall.png\"\n}\n" );
    REQUIRE( fixture.Compile(
        TestText( "materials/reformatted.cymat_c" ),
        reformattedReport ) == tool_status_t::OK );

    blob_t canonical{};
    blob_t reformatted{};
    ReadBlob(
        project.OutputPath( "materials/canonical.cymat_c" ),
        canonical );
    ReadBlob(
        project.OutputPath( "materials/reformatted.cymat_c" ),
        reformatted );
    REQUIRE( canonical.cbSize == reformatted.cbSize );
    CHECK( Cy_MemEqual(
        canonical.pData,
        reformatted.pData,
        canonical.cbSize ) );
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
    WriteMaterial( project, 3u );
    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    CHECK( fixture.Compile(
               TestText( "materials/wall.cymat_c" ),
               report ) == tool_status_t::VALIDATION_FAILED );
    CHECK( fixture.capture.lastDiagnostic ==
           CY_MATERIAL_DIAGNOSTIC_SCHEMA_FAILED );
    CHECK( fixture.capture.nLastDiagnosticLine == 2u );
}

TEST_CASE( "Material compiler resolves and cooks a V2 inheritance chain",
           "[CypherTools][MaterialCompiler][V2][Inheritance]" )
{
    temporary_project_t project{};
    WriteV2Dependencies( project );
    WriteMaterialV2(
        project,
        "materials/base.cymat",
        R"cykv(
    shader = "shaders/world.cyshader"
    domain = "surface"
    state = {
        alpha_mode = "mask"
        alpha_cutoff = 0.25
        two_sided = true
        casts_shadows = false
    }
    textures = {
        albedo_texture = {
            resource = "textures/albedo.cytex"
            sampler = "linear_wrap"
            uv = {
                set = 2u
                scale = [2, 3]
                offset = [0.1, 0.2]
                rotation = 0.75
            }
        }
        detail_texture = { resource = "textures/detail.cytex" }
    }
    parameters = { enabled = true count = 4 roughness = 0.8 }
)cykv" );
    WriteMaterialV2(
        project,
        "materials/mid.cymat",
        R"cykv(
    base = "materials/base.cymat"
    parameters = { count = 5 }
)cykv" );
    WriteMaterialV2(
        project,
        "materials/wall.cymat",
        R"cykv(
    base = "materials/mid.cymat"
    state = { alpha_mode = "opaque" receives_shadows = false }
    textures = {
        albedo_texture = {
            sampler = "point_clamp"
            uv = { offset = [0.25, 0.5] }
        }
        detail_texture = null
    }
    parameters = { count = 6 roughness = null }
)cykv" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
                 TestText( "materials/wall_v2.cymat_c" ),
                 report ) == tool_status_t::OK );
    CHECK( report.nSucceeded == 1u );
    CHECK( fixture.capture.bSawBase );
    CHECK( fixture.capture.bSawMidBase );
    CHECK( fixture.capture.bSawTransitiveBase );
    CHECK( fixture.capture.bSawShader );
    CHECK( fixture.capture.bSawTexture );
    CHECK( fixture.capture.nDependencies == 6u );

    blob_t cooked{};
    ReadBlob( project.OutputPath( "materials/wall_v2.cymat_c" ), cooked );
    cooked_material_view_t material{};
    REQUIRE( CookedMaterial_Succeeded(
        CookedMaterial_Read( Blob_Block( &cooked ), &material ) ) );
    CHECK( material.nResourceVersion ==
           CY_COOKED_MATERIAL_RESOURCE_VERSION_V2 );
    CHECK( StringView_Equals(
        material.shader,
        TestText( "shaders/world.cyshader" ) ) );
    CHECK( ContentHash_IsValid( material.shaderInterfaceHash ) );
    CHECK( ContentHash_IsValid( material.variantHash ) );
    CHECK( ContentHash_IsValid( material.sourceHash ) );
    CHECK( material.domain == render_material_domain_t::SURFACE );
    CHECK( material.alphaMode == render_material_alpha_mode_t::OPAQUE );
    CHECK( material.alphaCutoff == 0.5F );
    CHECK( ( material.flags & COOKED_MATERIAL_FLAG_TWO_SIDED ) != 0u );
    CHECK( ( material.flags & COOKED_MATERIAL_FLAG_CASTS_SHADOWS ) == 0u );
    CHECK( ( material.flags &
             COOKED_MATERIAL_FLAG_RECEIVES_SHADOWS ) == 0u );

    REQUIRE( material.nTextures == 1u );
    const cooked_material_texture_view_t *pAlbedo =
        CookedMaterial_FindTexture(
            material,
            TestText( "albedo_texture" ) );
    REQUIRE( pAlbedo != nullptr );
    CHECK( StringView_Equals(
        pAlbedo->texture,
        TestText( "textures/albedo.cytex" ) ) );
    CHECK( StringView_Equals(
        pAlbedo->sampler,
        TestText( "point_clamp" ) ) );
    CHECK( pAlbedo->nUvSet == 0u );
    CHECK( pAlbedo->uvScale[0] == 1.0F );
    CHECK( pAlbedo->uvScale[1] == 1.0F );
    CHECK( pAlbedo->uvOffset[0] == 0.25F );
    CHECK( pAlbedo->uvOffset[1] == 0.5F );
    CHECK( pAlbedo->uvRotation == 0.0F );
    CHECK( pAlbedo->nLogicalBinding != 0u );

    REQUIRE( material.nParameters == 4u );
    const cooked_material_parameter_view_t *pEnabled =
        CookedMaterial_FindParameter( material, TestText( "enabled" ) );
    const cooked_material_parameter_view_t *pCount =
        CookedMaterial_FindParameter( material, TestText( "count" ) );
    const cooked_material_parameter_view_t *pRoughness =
        CookedMaterial_FindParameter( material, TestText( "roughness" ) );
    const cooked_material_parameter_view_t *pTint =
        CookedMaterial_FindParameter( material, TestText( "tint" ) );
    REQUIRE( pEnabled != nullptr );
    REQUIRE( pCount != nullptr );
    REQUIRE( pRoughness != nullptr );
    REQUIRE( pTint != nullptr );
    CHECK( pEnabled->bValue );
    CHECK( pEnabled->iByteOffset == 4u );
    CHECK( pCount->signedValues[0] == 6 );
    CHECK( pCount->iByteOffset == 0u );
    CHECK( pRoughness->floatingValues[0] == 0.5 );
    CHECK( pRoughness->iByteOffset == 8u );
    CHECK( pTint->floatingValues[0] == 1.0 );
    CHECK( pTint->iByteOffset == 16u );
    CHECK( pTint->cbByteSize == 16u );
    CHECK( material.constantData.cbSize == 32u );

    cooked_shader_binding_source_t bindings[7]{};
    const string_view_t textureNames[]{
        TestText( "albedo_texture" ),
        TestText( "detail_texture" )
    };
    for ( usize iTexture = 0u; iTexture < 2u; ++iTexture ) {
        bindings[iTexture].name = textureNames[iTexture];
        bindings[iTexture].kind =
            render_shader_binding_kind_t::SAMPLED_TEXTURE;
        bindings[iTexture].resourceType =
            render_shader_resource_type_t::TEXTURE_2D;
        bindings[iTexture].stageMask = RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS;
        bindings[iTexture].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL |
            ( iTexture == 0u ? COOKED_SHADER_BINDING_FLAG_REQUIRED : 0u );
        REQUIRE( CookedShader_MakeLogicalBindingId(
            bindings[iTexture].name,
            &bindings[iTexture].nLogicalBinding ) );
    }
    const string_view_t parameterNames[]{
        TestText( "count" ), TestText( "enabled" ),
        TestText( "roughness" ), TestText( "tint" ),
        TestText( "transform" )
    };
    const render_shader_value_type_t parameterTypes[]{
        render_shader_value_type_t::I32,
        render_shader_value_type_t::BOOL,
        render_shader_value_type_t::F32,
        render_shader_value_type_t::F32X3,
        render_shader_value_type_t::F32X4X4
    };
    const u32 parameterOffsets[]{ 0u, 4u, 8u, 16u, 32u };
    for ( usize iParameter = 0u; iParameter < 5u; ++iParameter ) {
        cooked_shader_binding_source_t &binding = bindings[2u + iParameter];
        binding.name = parameterNames[iParameter];
        binding.kind = render_shader_binding_kind_t::VALUE;
        binding.valueType = parameterTypes[iParameter];
        binding.stageMask = RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS;
        binding.flags = COOKED_SHADER_BINDING_FLAG_MATERIAL |
            ( iParameter == 1u ? COOKED_SHADER_BINDING_FLAG_REQUIRED : 0u );
        binding.iByteOffset = parameterOffsets[iParameter];
        binding.cbByteSize = CookedShader_ValueTypeStorageSize(
            binding.valueType );
        REQUIRE( CookedShader_MakeLogicalBindingId(
            binding.name,
            &binding.nLogicalBinding ) );
    }
    content_hash_t expectedInterface{};
    REQUIRE( CookedShader_ComputeInterfaceHash(
        { { bindings, CYPHER_ARRAY_COUNT( bindings ) } },
        &expectedInterface ) );
    CHECK( ContentHash_Equals(
        material.shaderInterfaceHash,
        expectedInterface ) );
}

TEST_CASE( "Material compiler rejects V2 inheritance cycles and depth overflow",
           "[CypherTools][MaterialCompiler][V2][Inheritance][Failure]" )
{
    SECTION( "cycle" )
    {
        temporary_project_t project{};
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            "    base = \"materials/base.cymat\"" );
        WriteMaterialV2(
            project,
            "materials/base.cymat",
            "    base = \"materials/wall.cymat\"" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/cycle.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_INHERITANCE_CYCLE );
        CHECK( report.nArtifacts == 0u );
    }

    SECTION( "depth" )
    {
        temporary_project_t project{};
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            "    base = \"materials/depth_0.cymat\"" );
        for ( usize iDepth = 0u; iDepth < 15u; ++iDepth ) {
            const std::string path = "materials/depth_" +
                std::to_string( iDepth ) + ".cymat";
            const std::string next = "    base = \"materials/depth_" +
                std::to_string( iDepth + 1u ) + ".cymat\"";
            WriteMaterialV2( project, path.c_str(), next.c_str() );
        }
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/depth.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_INHERITANCE_DEPTH );
        CHECK( report.nArtifacts == 0u );
    }
}

TEST_CASE( "Material compiler enforces V2 required and typed parameters",
           "[CypherTools][MaterialCompiler][V2][Parameters][Failure]" )
{
    SECTION( "required parameter" )
    {
        temporary_project_t project{};
        WriteV2Dependencies( project );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/missing_required.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_MISSING_REQUIRED_BINDING );
    }

    SECTION( "unknown parameter" )
    {
        temporary_project_t project{};
        WriteV2Dependencies( project );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { enabled = true misspelled = 1 }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/unknown.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_UNKNOWN_BINDING );
    }

    SECTION( "type mismatch" )
    {
        temporary_project_t project{};
        WriteV2Dependencies( project );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { enabled = 1 }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/type.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_PARAMETER_TYPE_MISMATCH );
    }

    SECTION( "component mismatch" )
    {
        temporary_project_t project{};
        WriteV2Dependencies( project );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { enabled = true tint = [1, 1] }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/components.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_PARAMETER_TYPE_MISMATCH );
    }

    SECTION( "declared numeric range" )
    {
        temporary_project_t project{};
        WriteV2Dependencies( project );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { enabled = true count = 11 }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/range.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_PARAMETER_RANGE );
    }
}

TEST_CASE( "Material compiler validates V2 texture interface semantics",
           "[CypherTools][MaterialCompiler][V2][Textures][Failure]" )
{
    SECTION( "required binding" )
    {
        temporary_project_t project{};
        WriteV2Dependencies( project );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    parameters = { enabled = true }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/texture_required.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_MISSING_REQUIRED_BINDING );
    }

    SECTION( "usage and color space" )
    {
        temporary_project_t project{};
        WriteV2Dependencies( project );
        WriteTextureV2(
            project,
            "textures/albedo.cytex",
            "data",
            "linear" );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { enabled = true }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/texture_semantics.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_TEXTURE_SEMANTIC_MISMATCH );
    }

    SECTION( "unknown texture binding" )
    {
        temporary_project_t project{};
        WriteV2Dependencies( project );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
        typo_texture = { resource = "textures/detail.cytex" }
    }
    parameters = { enabled = true }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/texture_unknown.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_UNKNOWN_BINDING );
    }

    SECTION( "V1 texture source" )
    {
        temporary_project_t project{};
        WriteV2Dependencies( project );
        project.WriteText(
            "textures/albedo.cytex",
            "@cykv 1\n@schema \"cypher.texture\" 1\n"
            "{ source = \"textures/source/test.png\" }\n" );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { enabled = true }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/texture_v1.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT );
    }

    SECTION( "non-2D shader resource" )
    {
        temporary_project_t project{};
        project.WriteText(
            "shaders/world.cyshader",
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
    interface = {
        textures = {
            albedo_texture = {
                type = "texture_cube"
                usage = "color"
                color_space = "srgb"
                required = true
            }
        }
    }
}
)cykv" );
        WriteTextureV2( project, "textures/albedo.cytex" );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/texture_cube.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_TEXTURE_SEMANTIC_MISMATCH );
    }
}

TEST_CASE( "Material compiler rejects unresolved V2 runtime contracts",
           "[CypherTools][MaterialCompiler][V2][Gates]" )
{
    SECTION( "material feature override" )
    {
        temporary_project_t project{};
        WriteShaderV2( project );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    features = { quality = true }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/feature_gate.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT );
    }

    SECTION( "inherited feature removal" )
    {
        temporary_project_t project{};
        WriteMaterialV2(
            project,
            "materials/base.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    features = { quality = true }
)cykv" );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    base = "materials/base.cymat"
    features = { quality = null }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/feature_remove_gate.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT );
    }

    SECTION( "surface reference" )
    {
        temporary_project_t project{};
        WriteShaderV2( project );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    surface = "surfaces/concrete.cysurface"
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/surface_gate.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT );
    }

    SECTION( "shader feature table" )
    {
        temporary_project_t project{};
        WriteShaderV2(
            project,
            "",
            R"cykv(
    features = { quality = { type = "bool" default = false } }
)cykv" );
        WriteTextureV2( project, "textures/albedo.cytex" );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { enabled = true }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/shader_feature_gate.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT );
    }

    SECTION( "independent sampler" )
    {
        temporary_project_t project{};
        WriteShaderV2(
            project,
            R"cykv(
        samplers = {
            linear_sampler = {
                type = "filtering"
                default = "linear_wrap"
                required = false
            }
        }
)cykv" );
        WriteTextureV2( project, "textures/albedo.cytex" );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { enabled = true }
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/sampler_gate.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT );
    }

    SECTION( "V1 base material" )
    {
        temporary_project_t project{};
        WriteDependencies( project );
        project.WriteText(
            "materials/base.cymat",
            "@cykv 1\n@schema \"cypher.material\" 1\n"
            "{ shader = \"shaders/world.cyshader\" }\n" );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            "    base = \"materials/base.cymat\"" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/base_v1_gate.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT );
    }

    SECTION( "V1 shader source" )
    {
        temporary_project_t project{};
        WriteDependencies( project );
        WriteMaterialV2(
            project,
            "materials/wall.cymat",
            "    shader = \"shaders/world.cyshader\"" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "materials/shader_v1_gate.cymat_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT );
    }
}

TEST_CASE( "Material compiler V2 output is independent of source member order",
           "[CypherTools][MaterialCompiler][V2][Determinism]" )
{
    temporary_project_t project{};
    WriteV2Dependencies( project );
    WriteMaterialV2(
        project,
        "materials/wall.cymat",
        R"cykv(
    shader = "shaders/world.cyshader"
    state = { two_sided = true casts_shadows = false }
    textures = {
        detail_texture = { resource = "textures/detail.cytex" }
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { tint = [0.25, 0.5, 0.75] enabled = true count = 2 }
)cykv" );
    compiler_fixture_t fixture{ project };
    tool_report_t firstReport{};
    REQUIRE( fixture.Compile(
                 TestText( "materials/ordered.cymat_c" ),
                 firstReport ) == tool_status_t::OK );

    project.WriteText(
        "shaders/world.cyshader",
        R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    interface = {
        parameters = {
            transform = { required = false type = "mat4" }
            tint = { default = [1, 1, 1] type = "color3" }
            roughness = { maximum = 1 minimum = 0 default = 0.5 type = "f32" }
            enabled = { required = true type = "bool" }
            count = { maximum = 10 minimum = 0 default = 3 type = "i32" }
        }
        textures = {
            detail_texture = {
                required = false color_space = "linear"
                usage = "data" type = "texture2d"
            }
            albedo_texture = {
                required = true color_space = "srgb"
                usage = "color" type = "texture2d"
            }
        }
    }
    stages = {
        fragment = { source = "shaders/world.frag" }
        vertex = { source = "shaders/world.vert" }
    }
    language = "glsl"
}
)cykv" );
    project.WriteText(
        "textures/albedo.cytex",
        "@cykv 1\n@schema \"cypher.texture\" 2\n"
        "{ color_space = \"srgb\" usage = \"color\" type = \"2d\" "
        "source = \"textures/source/test.png\" }\n" );
    WriteMaterialV2(
        project,
        "materials/wall.cymat",
        R"cykv(
    parameters = { count = 2 enabled = true tint = [0.25, 0.5, 0.75] }
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
        detail_texture = { resource = "textures/detail.cytex" }
    }
    state = { casts_shadows = false two_sided = true }
    shader = "shaders/world.cyshader"
)cykv" );
    tool_report_t reorderedReport{};
    REQUIRE( fixture.Compile(
                 TestText( "materials/reordered.cymat_c" ),
                 reorderedReport ) == tool_status_t::OK );

    blob_t ordered{};
    blob_t reordered{};
    ReadBlob( project.OutputPath( "materials/ordered.cymat_c" ), ordered );
    ReadBlob( project.OutputPath( "materials/reordered.cymat_c" ), reordered );
    REQUIRE( ordered.cbSize == reordered.cbSize );
    CHECK( Cy_MemEqual( ordered.pData, reordered.pData, ordered.cbSize ) );
}

TEST_CASE( "Material compiler V2 identity includes overridden base recipes",
           "[CypherTools][MaterialCompiler][V2][Identity]" )
{
    temporary_project_t project{};
    WriteV2Dependencies( project );
    WriteMaterialV2(
        project,
        "materials/base.cymat",
        R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { enabled = true roughness = 0.8 }
)cykv" );
    WriteMaterialV2(
        project,
        "materials/wall.cymat",
        R"cykv(
    base = "materials/base.cymat"
    parameters = { roughness = null }
)cykv" );
    compiler_fixture_t fixture{ project };
    tool_report_t firstReport{};
    REQUIRE( fixture.Compile(
                 TestText( "materials/base_identity_a.cymat_c" ),
                 firstReport ) == tool_status_t::OK );

    WriteMaterialV2(
        project,
        "materials/base.cymat",
        R"cykv(
    shader = "shaders/world.cyshader"
    textures = {
        albedo_texture = { resource = "textures/albedo.cytex" }
    }
    parameters = { enabled = true roughness = 0.7 }
)cykv" );
    tool_report_t secondReport{};
    REQUIRE( fixture.Compile(
                 TestText( "materials/base_identity_b.cymat_c" ),
                 secondReport ) == tool_status_t::OK );

    blob_t firstBytes{};
    blob_t secondBytes{};
    ReadBlob(
        project.OutputPath( "materials/base_identity_a.cymat_c" ),
        firstBytes );
    ReadBlob(
        project.OutputPath( "materials/base_identity_b.cymat_c" ),
        secondBytes );
    cooked_material_view_t first{};
    cooked_material_view_t second{};
    REQUIRE( CookedMaterial_Succeeded(
        CookedMaterial_Read( Blob_Block( &firstBytes ), &first ) ) );
    REQUIRE( CookedMaterial_Succeeded(
        CookedMaterial_Read( Blob_Block( &secondBytes ), &second ) ) );
    const cooked_material_parameter_view_t *pFirstRoughness =
        CookedMaterial_FindParameter( first, TestText( "roughness" ) );
    const cooked_material_parameter_view_t *pSecondRoughness =
        CookedMaterial_FindParameter( second, TestText( "roughness" ) );
    REQUIRE( pFirstRoughness != nullptr );
    REQUIRE( pSecondRoughness != nullptr );
    CHECK( pFirstRoughness->floatingValues[0] == 0.5 );
    CHECK( pSecondRoughness->floatingValues[0] == 0.5 );
    CHECK_FALSE( ContentHash_Equals(
        first.sourceHash,
        second.sourceHash ) );
}
