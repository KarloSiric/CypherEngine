//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherTools/CypherShaderCompiler/CypherShaderCompiler_Tests.cpp
//  Purpose: Tests the complete `.cyshader` source-to-cooked compiler path.
//  Details: Tests exercise real glslang preprocessing and cross-stage linking,
//           deterministic CYRS output, ToolFramework records, dry runs, malformed
//           CYKV, invalid GLSL, and bounded VFS-backed include resolution.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherShaderCompiler.h"

#include "CypherCommon_Blob.h"
#include "CypherCommon_CookedShader.h"
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
               ( "cypher_shader_compiler_" +
                 std::to_string( nSequence.fetch_add( 1u ) ) );
        source = root / "source";
        output = root / "output";
        std::filesystem::create_directories( source / "shaders" );
        std::filesystem::create_directories( output / "shaders" );
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

    void Write( const char *pRelativePath, const char *pContents ) const
    {
        const std::filesystem::path path = source / pRelativePath;
        std::filesystem::create_directories( path.parent_path() );
        std::ofstream file( path, std::ios::binary | std::ios::trunc );
        REQUIRE( file.is_open() );
        file.write(
            pContents,
            static_cast<std::streamsize>( std::strlen( pContents ) ) );
        REQUIRE( file.good() );
    }

    std::string OutputPath( const char *pRelativePath ) const
    {
        return ( output / pRelativePath ).string();
    }
};

struct host_capture_t {
    u32 nDiagnostics{ 0u };
    u32 nErrors{ 0u };
    u32 nDependencies{ 0u };
    u32 nArtifacts{ 0u };
    u32 nProgress{ 0u };
    u32 nReports{ 0u };
    tool_diagnostic_code_t lastDiagnostic{ CY_TOOL_DIAGNOSTIC_NONE };
    u32 nLastDiagnosticLine{ 0u };
    u32 nLastDiagnosticColumn{ 0u };
    tool_status_t reportedStatus{ tool_status_t::INTERNAL_ERROR };
    content_hash_t artifactHash{};
    content_hash_t compilerDependencyHash{};
    content_hash_t glslangDependencyHash{};
    content_hash_t configurationHash{};
    std::string lastDiagnosticMessage{};
    std::string lastDiagnosticHint{};
    bool_t bSawRecipeDependency{ CY_FALSE };
    bool_t bSawVertexDependency{ CY_FALSE };
    bool_t bSawFragmentDependency{ CY_FALSE };
    bool_t bSawSharedDependency{ CY_FALSE };
    bool_t bSawNestedDependency{ CY_FALSE };
    bool_t bSawTransitiveDependency{ CY_FALSE };
    bool_t bSawCompilerDependency{ CY_FALSE };
    bool_t bSawToolchainDependency{ CY_FALSE };
    bool_t bSawReflectionToolchainDependency{ CY_FALSE };
    bool_t bSawConfigurationDependency{ CY_FALSE };
    bool_t bSawCompletedProgress{ CY_FALSE };
};

void CaptureDiagnostic(
    const tool_diagnostic_t &diagnostic,
    void *pUserData ) noexcept
{
    auto &capture = *static_cast<host_capture_t *>( pUserData );
    ++capture.nDiagnostics;
    capture.lastDiagnostic = diagnostic.code;
    capture.nLastDiagnosticLine = diagnostic.source.nLine;
    capture.nLastDiagnosticColumn = diagnostic.source.nColumn;
    capture.lastDiagnosticMessage.assign(
        diagnostic.message.pData != nullptr ? diagnostic.message.pData : "",
        diagnostic.message.cchLength );
    capture.lastDiagnosticHint.assign(
        diagnostic.hint.pData != nullptr ? diagnostic.hint.pData : "",
        diagnostic.hint.cchLength );
    if ( diagnostic.severity == tool_diagnostic_severity_t::ERROR ||
         diagnostic.severity == tool_diagnostic_severity_t::FATAL ) {
        ++capture.nErrors;
    }
}

void CaptureDependency(
    const tool_dependency_t &dependency,
    void *pUserData ) noexcept
{
    auto &capture = *static_cast<host_capture_t *>( pUserData );
    ++capture.nDependencies;
    capture.bSawRecipeDependency |= StringView_Equals(
        dependency.path,
        TestText( "shaders/world.cyshader" ) );
    capture.bSawVertexDependency |= StringView_Equals(
        dependency.path,
        TestText( "shaders/world.vert" ) );
    capture.bSawFragmentDependency |= StringView_Equals(
        dependency.path,
        TestText( "shaders/world.frag" ) );
    const bool_t bShared = StringView_Equals(
        dependency.path,
        TestText( "shaders/include/shared.glsl" ) );
    const bool_t bNested = StringView_Equals(
        dependency.path,
        TestText( "shaders/include/nested/constants.glsl" ) );
    capture.bSawSharedDependency |= bShared;
    capture.bSawNestedDependency |= bNested;
    if ( bShared || bNested ) {
        capture.bSawTransitiveDependency |=
            ( dependency.flags & TOOL_DEPENDENCY_FLAG_TRANSITIVE ) != 0u;
    }
    capture.bSawCompilerDependency |= StringView_Equals(
        dependency.path,
        TestText( "toolchain/cypher-shader-compiler" ) );
    if ( StringView_Equals(
             dependency.path,
             TestText( "toolchain/cypher-shader-compiler" ) ) ) {
        capture.compilerDependencyHash = dependency.contentHash;
    }
    capture.bSawToolchainDependency |= StringView_Equals(
        dependency.path,
        TestText( "toolchain/glslang" ) );
    if ( StringView_Equals(
             dependency.path,
             TestText( "toolchain/glslang" ) ) ) {
        capture.glslangDependencyHash = dependency.contentHash;
    }
    capture.bSawReflectionToolchainDependency |= StringView_Equals(
        dependency.path,
        TestText( "toolchain/spirv-cross" ) );
    if ( StringView_Equals(
             dependency.path,
             TestText( "configuration/shader-target-profile" ) ) ) {
        capture.bSawConfigurationDependency = CY_TRUE;
        capture.configurationHash = dependency.contentHash;
    }
}

void CaptureArtifact(
    const tool_artifact_t &artifact,
    void *pUserData ) noexcept
{
    auto &capture = *static_cast<host_capture_t *>( pUserData );
    ++capture.nArtifacts;
    capture.artifactHash = artifact.contentHash;
}

void CaptureProgress(
    const tool_progress_t &progress,
    void *pUserData ) noexcept
{
    auto &capture = *static_cast<host_capture_t *>( pUserData );
    ++capture.nProgress;
    if ( progress.state == tool_progress_state_t::COMPLETE ) {
        capture.bSawCompletedProgress = CY_TRUE;
    }
}

void CaptureReport(
    const tool_report_t &report,
    void *pUserData ) noexcept
{
    auto &capture = *static_cast<host_capture_t *>( pUserData );
    ++capture.nReports;
    capture.reportedStatus = report.status;
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
    string_view_t input{ TestText( "shaders/world.cyshader" ) };
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
        host.pfnReport = &CaptureReport;
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
            CypherShaderCompiler_Descriptor();
        const tool_compile_request_t request{
            &invocation,
            17u,
            input,
            output,
            pCompiler->resourceType
        };
        return ToolCompiler_Execute( *pCompiler, request, &report );
    }
};

constexpr char g_validRecipe[] = R"cykv(@cykv 1
@schema "cypher.shader" 1
{
    language = "glsl"
    vertex = "shaders/world.vert"
    fragment = "shaders/world.frag"
    defines = ["CY_WORLD_PASS"]
}
)cykv";

constexpr char g_validRecipeV2[] = R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" entry = "main" }
        fragment = { source = "shaders/world.frag" }
    }
    defines = ["CY_WORLD_PASS"]
    interface = {
        textures = {
            albedo_texture = {
                type = "texture2d"
                usage = "color"
                color_space = "srgb"
                required = true
            }
        }
        parameters = {
            tint = { type = "color3" default = [1, 1, 1] }
            uv_scale = { type = "f32x2" default = [1, 1] }
            roughness = { type = "f32" default = 0.5 }
            model = { type = "mat4" }
        }
    }
    variant_budget = 8u
}
)cykv";

constexpr char g_validVertex[] = R"glsl(#version 410 core
layout(location = 0) out vec2 vUv;
#if CY_WORLD_PASS
const float kWorldPass = 1.0;
#else
const float kWorldPass = 0.0;
#endif
void main()
{
    vUv = vec2(kWorldPass);
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)glsl";

constexpr char g_validFragment[] = R"glsl(#version 410 core
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main()
{
    outColor = vec4(vUv, 0.0, 1.0);
}
)glsl";

constexpr char g_validVertexV2[] = R"glsl(#version 410 core
layout(location = 0) out vec2 vUv;
uniform mat4 model;
uniform vec2 uv_scale;
uniform float roughness;
#if CY_WORLD_PASS
const float kWorldPass = 1.0;
#else
const float kWorldPass = 0.0;
#endif
void main()
{
    vUv = vec2(kWorldPass) * uv_scale;
    gl_Position = model * vec4(0.0, 0.0, roughness, 1.0);
}
)glsl";

constexpr char g_validFragmentV2[] = R"glsl(#version 410 core
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
uniform sampler2D albedo_texture;
uniform vec3 tint;
uniform float roughness;
void main()
{
    vec4 albedo = texture(albedo_texture, vUv);
    outColor = vec4(
        albedo.rgb * tint * (1.0 - 0.5 * roughness),
        albedo.a);
}
)glsl";

void WriteValidShader( const temporary_project_t &project )
{
    project.Write( "shaders/world.cyshader", g_validRecipe );
    project.Write( "shaders/world.vert", g_validVertex );
    project.Write( "shaders/world.frag", g_validFragment );
}

void WriteValidShaderV2( const temporary_project_t &project )
{
    project.Write( "shaders/world.cyshader", g_validRecipeV2 );
    project.Write( "shaders/world.vert", g_validVertexV2 );
    project.Write( "shaders/world.frag", g_validFragmentV2 );
}

void WriteShaderV2Case(
    const temporary_project_t &project,
    const char *pRecipe,
    const char *pVertex,
    const char *pFragment )
{
    project.Write( "shaders/world.cyshader", pRecipe );
    project.Write( "shaders/world.vert", pVertex );
    project.Write( "shaders/world.frag", pFragment );
}

void ReadBlob( const std::string &path, blob_t &blob )
{
    REQUIRE( Blob_Init( &blob, Allocator_GetSystem() ) );
    REQUIRE( FileIo_ReadAllNative( View( path ), &blob ) );
}

} // namespace

TEST_CASE( "Shader compiler descriptor owns `.cyshader` inputs",
           "[CypherTools][ShaderCompiler][Descriptor]" )
{
    const tool_compiler_desc_t *pCompiler =
        CypherShaderCompiler_Descriptor();
    REQUIRE( pCompiler != nullptr );
    CHECK( ToolCompiler_CheckDescriptor( *pCompiler ) == tool_status_t::OK );
    CHECK( pCompiler->nCompilerVersion == CY_SHADER_COMPILER_VERSION );
    CHECK( pCompiler->nCompilerVersion == 5u );
    CHECK( ToolCompiler_SupportsInput(
        *pCompiler,
        TestText( "shaders/world.cyshader" ) ) );
    CHECK_FALSE( ToolCompiler_SupportsInput(
        *pCompiler,
        TestText( "materials/world.cymat" ) ) );

    tool_compile_request_t request{};
    tool_report_t report{};
    CHECK( pCompiler->pfnExecute(
               request,
               nullptr,
               pCompiler->pUserData ) == tool_status_t::INVALID_ARGUMENT );
    CHECK( pCompiler->pfnExecute(
               request,
               &report,
               pCompiler->pUserData ) == tool_status_t::INVALID_ARGUMENT );
    tool_invocation_t invocation{};
    request.pInvocation = &invocation;
    CHECK( pCompiler->pfnExecute(
               request,
               &report,
               pCompiler->pUserData ) == tool_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Shader compiler cooks linked OpenGL GLSL and reports its graph",
           "[CypherTools][ShaderCompiler][Integration]" )
{
    temporary_project_t project{};
    WriteValidShader( project );
    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/world.cyshader_c" ),
        report ) == tool_status_t::OK );
    CHECK( report.nInputsProcessed == 1u );
    CHECK( report.nSucceeded == 1u );
    CHECK( report.nFailed == 0u );
    CHECK( report.nArtifacts == 1u );
    CHECK( report.cbRead > 0u );
    CHECK( report.cbWritten > 0u );
    CHECK( fixture.capture.nErrors == 0u );
    CHECK( fixture.capture.nDependencies == 6u );
    CHECK( fixture.capture.nArtifacts == 1u );
    CHECK( fixture.capture.nReports == 1u );
    CHECK( fixture.capture.reportedStatus == tool_status_t::OK );
    CHECK( fixture.capture.bSawRecipeDependency );
    CHECK( fixture.capture.bSawVertexDependency );
    CHECK( fixture.capture.bSawFragmentDependency );
    CHECK( fixture.capture.bSawCompilerDependency );
    CHECK( fixture.capture.bSawToolchainDependency );
    CHECK( ContentHash_IsValid(
        fixture.capture.glslangDependencyHash ) );
    CHECK_FALSE( fixture.capture.bSawReflectionToolchainDependency );
    CHECK( fixture.capture.bSawConfigurationDependency );
    CHECK( ContentHash_IsValid( fixture.capture.configurationHash ) );
    CHECK( fixture.capture.bSawCompletedProgress );

    const std::string outputPath = project.OutputPath(
        "shaders/world.cyshader_c" );
    blob_t cooked{};
    ReadBlob( outputPath, cooked );
    CHECK( ContentHash_Equals(
        fixture.capture.artifactHash,
        ContentHash_Data( Blob_Block( &cooked ) ) ) );

    cooked_shader_view_t shader{};
    const cooked_shader_result_t read = CookedShader_Read(
        Blob_Block( &cooked ),
        &shader );
    REQUIRE( CookedShader_Succeeded( read ) );
    REQUIRE( shader.nResourceVersion ==
             CY_COOKED_SHADER_RESOURCE_VERSION_V2 );
    CHECK( shader.nBindings == 0u );
    REQUIRE( shader.backend == render_shader_backend_t::OPENGL );
    REQUIRE( shader.languageProfile ==
             render_shader_language_profile_t::GLSL_CORE );
    REQUIRE( shader.nLanguageVersion == 410u );
    REQUIRE( shader.nStages == 2u );
    const cooked_shader_stage_view_t *pVertex = CookedShader_FindStage(
        shader,
        render_shader_stage_t::VERTEX );
    REQUIRE( pVertex != nullptr );
    const string_view_t vertexText{
        reinterpret_cast<const char *>( pVertex->code.pData ),
        pVertex->code.cbSize - 1u
    };
    CHECK( StringView_StartsWith(
        vertexText,
        TestText( "#version 410 core" ) ) );
    CHECK( StringView_Contains(
        vertexText,
        TestText( "kWorldPass = 1.0" ) ) );
    CHECK_FALSE( StringView_Contains(
        vertexText,
        TestText( "kWorldPass = 0.0" ) ) );
}

TEST_CASE( "Shader compiler cooks V2 logical interfaces as CYSH V3",
           "[CypherTools][ShaderCompiler][Integration][V2][Interface]" )
{
    temporary_project_t project{};
    WriteValidShaderV2( project );
    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/world.cyshader_c" ),
        report ) == tool_status_t::OK );
    CHECK( fixture.capture.nDependencies == 7u );
    CHECK( fixture.capture.bSawReflectionToolchainDependency );

    blob_t cooked{};
    ReadBlob( project.OutputPath( "shaders/world.cyshader_c" ), cooked );
    cooked_shader_view_t shader{};
    REQUIRE( CookedShader_Succeeded(
        CookedShader_Read( Blob_Block( &cooked ), &shader ) ) );
    REQUIRE( shader.nResourceVersion ==
             CY_COOKED_SHADER_RESOURCE_VERSION_V3 );
    REQUIRE( shader.nBindings == 5u );
    REQUIRE( ContentHash_IsValid( shader.interfaceHash ) );

    for ( u32 iBinding = 1u; iBinding < shader.nBindings; ++iBinding ) {
        CHECK( StringView_Compare(
                   shader.bindings[iBinding - 1u].name,
                   shader.bindings[iBinding].name ) < 0 );
    }

    const cooked_shader_binding_view_t *pTexture =
        CookedShader_FindBinding( shader, TestText( "albedo_texture" ) );
    REQUIRE( pTexture != nullptr );
    CHECK( pTexture->kind ==
           render_shader_binding_kind_t::SAMPLED_TEXTURE );
    CHECK( pTexture->resourceType ==
           render_shader_resource_type_t::TEXTURE_2D );
    CHECK( pTexture->stageMask == RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS );
    CHECK( ( pTexture->flags & COOKED_SHADER_BINDING_FLAG_REQUIRED ) != 0u );
    CHECK( ( pTexture->flags & COOKED_SHADER_BINDING_FLAG_MATERIAL ) != 0u );

    struct expected_parameter_t {
        string_view_t name{};
        render_shader_value_type_t type{ render_shader_value_type_t::NONE };
        u32 iByteOffset{ 0u };
        u32 cbByteSize{ 0u };
    };
    constexpr expected_parameter_t parameters[]{
        { TestText( "model" ), render_shader_value_type_t::F32X4X4, 0u, 64u },
        { TestText( "roughness" ), render_shader_value_type_t::F32, 64u, 4u },
        { TestText( "tint" ), render_shader_value_type_t::F32X3, 80u, 16u },
        { TestText( "uv_scale" ), render_shader_value_type_t::F32X2, 96u, 8u }
    };
    for ( const expected_parameter_t &expected : parameters ) {
        const cooked_shader_binding_view_t *pBinding =
            CookedShader_FindBinding( shader, expected.name );
        REQUIRE( pBinding != nullptr );
        CHECK( pBinding->kind == render_shader_binding_kind_t::VALUE );
        CHECK( pBinding->valueType == expected.type );
        CHECK( pBinding->stageMask ==
               RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS );
        CHECK( pBinding->iByteOffset == expected.iByteOffset );
        CHECK( pBinding->cbByteSize == expected.cbByteSize );
        u64 expectedId = 0u;
        REQUIRE( CookedShader_MakeLogicalBindingId(
            expected.name,
            &expectedId ) );
        CHECK( pBinding->nLogicalBinding == expectedId );
    }

    // Reconstruct the material compiler's logical interface directly from the
    // authored contract. This protects the shared CYSH/CYMT interface hash from
    // accidentally persisting physical per-stage visibility discovered below.
    cooked_shader_binding_source_t interfaceBindings[5]{};
    interfaceBindings[0].name = TestText( "albedo_texture" );
    interfaceBindings[0].kind =
        render_shader_binding_kind_t::SAMPLED_TEXTURE;
    interfaceBindings[0].resourceType =
        render_shader_resource_type_t::TEXTURE_2D;
    interfaceBindings[0].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL |
        COOKED_SHADER_BINDING_FLAG_REQUIRED;
    REQUIRE( CookedShader_MakeLogicalBindingId(
        interfaceBindings[0].name,
        &interfaceBindings[0].nLogicalBinding ) );
    for ( usize iParameter = 0u;
          iParameter < CYPHER_ARRAY_COUNT( parameters );
          ++iParameter ) {
        const expected_parameter_t &expected = parameters[iParameter];
        cooked_shader_binding_source_t &binding =
            interfaceBindings[iParameter + 1u];
        binding.name = expected.name;
        binding.kind = render_shader_binding_kind_t::VALUE;
        binding.valueType = expected.type;
        binding.flags = COOKED_SHADER_BINDING_FLAG_MATERIAL;
        binding.iByteOffset = expected.iByteOffset;
        binding.cbByteSize = expected.cbByteSize;
        REQUIRE( CookedShader_MakeLogicalBindingId(
            binding.name,
            &binding.nLogicalBinding ) );
    }
    content_hash_t recomputedInterfaceHash{};
    REQUIRE( CookedShader_ComputeInterfaceHash(
        { { interfaceBindings, CYPHER_ARRAY_COUNT( interfaceBindings ) } },
        &recomputedInterfaceHash ) );
    CHECK( ContentHash_Equals(
        shader.interfaceHash,
        recomputedInterfaceHash ) );
}

TEST_CASE( "Shader compiler accepts an exact empty V2 material interface",
           "[CypherTools][ShaderCompiler][Integration][V2][Reflection]" )
{
    temporary_project_t project{};
    WriteShaderV2Case(
        project,
        R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
}
)cykv",
        g_validVertex,
        g_validFragment );
    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/world.cyshader_c" ),
        report ) == tool_status_t::OK );
    CHECK( fixture.capture.bSawReflectionToolchainDependency );

    blob_t cooked{};
    ReadBlob( project.OutputPath( "shaders/world.cyshader_c" ), cooked );
    cooked_shader_view_t shader{};
    REQUIRE( CookedShader_Succeeded(
        CookedShader_Read( Blob_Block( &cooked ), &shader ) ) );
    CHECK( shader.nResourceVersion ==
           CY_COOKED_SHADER_RESOURCE_VERSION_V3 );
    CHECK( shader.nBindings == 0u );
    CHECK( ContentHash_IsValid( shader.interfaceHash ) );
}

TEST_CASE( "Shader compiler output is deterministic for identical inputs",
           "[CypherTools][ShaderCompiler][Determinism]" )
{
    temporary_project_t project{};
    WriteValidShader( project );
    compiler_fixture_t fixture{ project };
    tool_report_t firstReport{};
    tool_report_t secondReport{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/first.cyshader_c" ),
        firstReport ) == tool_status_t::OK );
    REQUIRE( fixture.Compile(
        TestText( "shaders/second.cyshader_c" ),
        secondReport ) == tool_status_t::OK );

    blob_t first{};
    blob_t second{};
    ReadBlob( project.OutputPath( "shaders/first.cyshader_c" ), first );
    ReadBlob( project.OutputPath( "shaders/second.cyshader_c" ), second );
    REQUIRE( first.cbSize == second.cbSize );
    CHECK( Cy_MemEqual( first.pData, second.pData, first.cbSize ) );
}

TEST_CASE( "Shader compiler identity includes source and cooked versions",
           "[CypherTools][ShaderCompiler][Identity][Version]" )
{
    temporary_project_t project{};
    WriteValidShader( project );
    compiler_fixture_t fixture{ project };
    tool_report_t v1Report{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/v1.cyshader_c" ),
        v1Report ) == tool_status_t::OK );
    const content_hash_t v1CompilerHash =
        fixture.capture.compilerDependencyHash;
    const content_hash_t v1GlslangHash =
        fixture.capture.glslangDependencyHash;
    REQUIRE( ContentHash_IsValid( v1CompilerHash ) );
    REQUIRE( ContentHash_IsValid( v1GlslangHash ) );

    WriteValidShaderV2( project );
    tool_report_t v2Report{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/v2.cyshader_c" ),
        v2Report ) == tool_status_t::OK );
    const content_hash_t v2CompilerHash =
        fixture.capture.compilerDependencyHash;
    const content_hash_t v2GlslangHash =
        fixture.capture.glslangDependencyHash;
    REQUIRE( ContentHash_IsValid( v2CompilerHash ) );
    REQUIRE( ContentHash_IsValid( v2GlslangHash ) );
    CHECK_FALSE( ContentHash_Equals(
        v1CompilerHash,
        v2CompilerHash ) );
    CHECK( ContentHash_Equals(
        v1GlslangHash,
        v2GlslangHash ) );
}

TEST_CASE( "Shader compiler ignores authoring-only CYKV formatting",
           "[CypherTools][ShaderCompiler][Determinism][CYKV]" )
{
    temporary_project_t project{};
    WriteValidShader( project );
    compiler_fixture_t fixture{ project };
    tool_report_t firstReport{};
    tool_report_t reformattedReport{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/canonical.cyshader_c" ),
        firstReport ) == tool_status_t::OK );

    project.Write(
        "shaders/world.cyshader",
        "@cykv 1\n"
        "@schema \"cypher.shader\" 1\n"
        "{ // Source layout must not participate in build identity.\n"
        "    defines = [ \"CY_WORLD_PASS\" ]\n"
        "    fragment = \"shaders/world.frag\"\n"
        "    vertex = \"shaders/world.vert\"\n"
        "    language = \"glsl\"\n"
        "}\n" );
    REQUIRE( fixture.Compile(
        TestText( "shaders/reformatted.cyshader_c" ),
        reformattedReport ) == tool_status_t::OK );

    blob_t canonical{};
    blob_t reformatted{};
    ReadBlob(
        project.OutputPath( "shaders/canonical.cyshader_c" ),
        canonical );
    ReadBlob(
        project.OutputPath( "shaders/reformatted.cyshader_c" ),
        reformatted );
    REQUIRE( canonical.cbSize == reformatted.cbSize );
    CHECK( Cy_MemEqual(
        canonical.pData,
        reformatted.pData,
        canonical.cbSize ) );
}

TEST_CASE( "Shader V2 canonicalizes interface declaration order and formatting",
           "[CypherTools][ShaderCompiler][V2][Determinism][CYKV]" )
{
    temporary_project_t project{};
    WriteValidShaderV2( project );
    compiler_fixture_t fixture{ project };
    tool_report_t canonicalReport{};
    tool_report_t reorderedReport{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/canonical.cyshader_c" ),
        canonicalReport ) == tool_status_t::OK );

    project.Write(
        "shaders/world.cyshader",
        R"cykv(@cykv 1
@schema "cypher.shader" 2
{ // Equivalent source with object members intentionally reordered.
    variant_budget = 8u
    interface = {
        parameters = {
            model = { type = "mat4" }
            roughness = { default = 0.5 type = "f32" }
            uv_scale = { default = [ 1, 1 ] type = "f32x2" }
            tint = { default = [ 1, 1, 1 ] type = "color3" }
        }
        textures = {
            albedo_texture = {
                required = true
                color_space = "srgb"
                usage = "color"
                type = "texture2d"
            }
        }
    }
    defines = [ "CY_WORLD_PASS" ]
    stages = {
        fragment = { source = "shaders/world.frag" }
        vertex = { entry = "main" source = "shaders/world.vert" }
    }
    language = "glsl"
}
)cykv" );
    REQUIRE( fixture.Compile(
        TestText( "shaders/reordered.cyshader_c" ),
        reorderedReport ) == tool_status_t::OK );

    blob_t canonical{};
    blob_t reordered{};
    ReadBlob(
        project.OutputPath( "shaders/canonical.cyshader_c" ),
        canonical );
    ReadBlob(
        project.OutputPath( "shaders/reordered.cyshader_c" ),
        reordered );
    REQUIRE( canonical.cbSize == reordered.cbSize );
    CHECK( Cy_MemEqual(
        canonical.pData,
        reordered.pData,
        canonical.cbSize ) );
}

TEST_CASE( "Shader compiler identities include target and build profile",
           "[CypherTools][ShaderCompiler][Identity][Configuration]" )
{
    temporary_project_t project{};
    WriteValidShader( project );
    compiler_fixture_t fixture{ project };

    fixture.context.target = {
        tool_platform_t::LINUX,
        tool_architecture_t::X64
    };
    fixture.context.profile = tool_profile_t::DEVELOPMENT;
    tool_report_t linuxDevelopmentReport{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/linux-development.cyshader_c" ),
        linuxDevelopmentReport ) == tool_status_t::OK );
    const content_hash_t linuxDevelopmentConfiguration =
        fixture.capture.configurationHash;

    fixture.capture = {};
    fixture.context.target = {
        tool_platform_t::MACOS,
        tool_architecture_t::ARM64
    };
    tool_report_t macDevelopmentReport{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/mac-development.cyshader_c" ),
        macDevelopmentReport ) == tool_status_t::OK );
    const content_hash_t macDevelopmentConfiguration =
        fixture.capture.configurationHash;

    fixture.capture = {};
    fixture.context.target = {
        tool_platform_t::LINUX,
        tool_architecture_t::X64
    };
    fixture.context.profile = tool_profile_t::SHIPPING;
    tool_report_t linuxShippingReport{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/linux-shipping.cyshader_c" ),
        linuxShippingReport ) == tool_status_t::OK );
    const content_hash_t linuxShippingConfiguration =
        fixture.capture.configurationHash;

    blob_t linuxDevelopment{};
    blob_t macDevelopment{};
    blob_t linuxShipping{};
    ReadBlob(
        project.OutputPath( "shaders/linux-development.cyshader_c" ),
        linuxDevelopment );
    ReadBlob(
        project.OutputPath( "shaders/mac-development.cyshader_c" ),
        macDevelopment );
    ReadBlob(
        project.OutputPath( "shaders/linux-shipping.cyshader_c" ),
        linuxShipping );
    cooked_shader_view_t linuxDevelopmentShader{};
    cooked_shader_view_t macDevelopmentShader{};
    cooked_shader_view_t linuxShippingShader{};
    REQUIRE( CookedShader_Succeeded( CookedShader_Read(
        Blob_Block( &linuxDevelopment ),
        &linuxDevelopmentShader ) ) );
    REQUIRE( CookedShader_Succeeded( CookedShader_Read(
        Blob_Block( &macDevelopment ),
        &macDevelopmentShader ) ) );
    REQUIRE( CookedShader_Succeeded( CookedShader_Read(
        Blob_Block( &linuxShipping ),
        &linuxShippingShader ) ) );

    CHECK_FALSE( ContentHash_Equals(
        linuxDevelopmentShader.sourceHash,
        macDevelopmentShader.sourceHash ) );
    CHECK_FALSE( ContentHash_Equals(
        linuxDevelopmentShader.sourceHash,
        linuxShippingShader.sourceHash ) );
    CHECK_FALSE( ContentHash_Equals(
        linuxDevelopmentConfiguration,
        macDevelopmentConfiguration ) );
    CHECK_FALSE( ContentHash_Equals(
        linuxDevelopmentConfiguration,
        linuxShippingConfiguration ) );
}

TEST_CASE( "Shader compiler dry run validates without writing an artifact",
           "[CypherTools][ShaderCompiler][DryRun]" )
{
    temporary_project_t project{};
    WriteValidShader( project );
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
    CHECK( fixture.capture.nDependencies == 6u );
}

TEST_CASE( "Shader compiler rejects malformed CYKV transactionally",
           "[CypherTools][ShaderCompiler][Invalid]" )
{
    temporary_project_t project{};
    project.Write(
        "shaders/world.cyshader",
        "@cykv 1\n@schema \"cypher.shader\" 1\n{ language = \"glsl\"" );
    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    CHECK( fixture.Compile(
        TestText( "shaders/world.cyshader_c" ),
        report ) == tool_status_t::VALIDATION_FAILED );
    CHECK( report.nFailed == 1u );
    CHECK( report.nErrors == 1u );
    CHECK( fixture.capture.lastDiagnostic ==
           CY_SHADER_DIAGNOSTIC_CYKV_PARSE_FAILED );
    CHECK_FALSE( std::filesystem::exists(
        project.output / "shaders/world.cyshader_c" ) );
}

TEST_CASE( "Shader compiler reports exact CYKV header mismatch locations",
           "[CypherTools][ShaderCompiler][Diagnostics]" )
{
    temporary_project_t project{};
    project.Write(
        "shaders/world.cyshader",
        "@cykv 1\n@schema \"cypher.shader\" 3\n{ language = \"glsl\" }\n" );
    compiler_fixture_t fixture{ project };
    tool_report_t report{};

    CHECK( fixture.Compile(
        TestText( "shaders/world.cyshader_c" ),
        report ) == tool_status_t::VALIDATION_FAILED );
    CHECK( fixture.capture.lastDiagnostic ==
           CY_SHADER_DIAGNOSTIC_SCHEMA_FAILED );
    CHECK( fixture.capture.nLastDiagnosticLine == 2u );
    CHECK( fixture.capture.nLastDiagnosticColumn == 25u );
}

TEST_CASE( "Shader compiler rejects invalid V2 declarations and entry points",
           "[CypherTools][ShaderCompiler][V2][Invalid]" )
{
    SECTION( "interface declaration violates the V2 schema" )
    {
        temporary_project_t project{};
        project.Write(
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
            albedo_texture = { type = "texture2d" }
        }
    }
}
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_SCHEMA_FAILED );
        CHECK_FALSE( std::filesystem::exists(
            project.output / "shaders/world.cyshader_c" ) );
    }

    SECTION( "alternate runtime entry point is not representable in CYSH V3" )
    {
        temporary_project_t project{};
        project.Write(
            "shaders/world.cyshader",
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" entry = "vertex_main" }
        fragment = { source = "shaders/world.frag" }
    }
}
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_UNSUPPORTED_ENTRY_POINT );
        CHECK_FALSE( fixture.capture.lastDiagnosticHint.empty() );
    }
}

TEST_CASE( "Shader compiler requires the V2 authored interface to match active GLSL",
           "[CypherTools][ShaderCompiler][V2][Reflection][Invalid]" )
{
    constexpr char emptyFragment[] = R"glsl(#version 410 core
layout(location = 0) out vec4 outColor;
void main(){ outColor = vec4(1.0); }
)glsl";

    SECTION( "authored parameter is absent from GLSL" )
    {
        temporary_project_t project{};
        WriteShaderV2Case(
            project,
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
    interface = { parameters = { exposure = { type = "f32" } } }
}
)cykv",
            R"glsl(#version 410 core
void main(){ gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }
)glsl",
            emptyFragment );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_INTERFACE_FAILED );
        CHECK( fixture.capture.lastDiagnosticMessage.find( "exposure" ) !=
               std::string::npos );
        CHECK_FALSE( std::filesystem::exists(
            project.output / "shaders/world.cyshader_c" ) );
    }

    SECTION( "declared but inactive GLSL parameter is rejected" )
    {
        temporary_project_t project{};
        WriteShaderV2Case(
            project,
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
    interface = { parameters = { exposure = { type = "f32" } } }
}
)cykv",
            R"glsl(#version 410 core
uniform float exposure;
void main(){ gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }
)glsl",
            emptyFragment );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_INTERFACE_FAILED );
        CHECK( fixture.capture.lastDiagnosticMessage.find( "inactive" ) !=
               std::string::npos );
    }

    SECTION( "active GLSL-only uniform is rejected" )
    {
        temporary_project_t project{};
        WriteShaderV2Case(
            project,
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
}
)cykv",
            R"glsl(#version 410 core
uniform float exposure;
void main(){ gl_Position = vec4(0.0, 0.0, exposure, 1.0); }
)glsl",
            emptyFragment );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_INTERFACE_FAILED );
        CHECK( fixture.capture.lastDiagnosticMessage.find( "exposure" ) !=
               std::string::npos );
    }

    SECTION( "parameter type mismatch is rejected" )
    {
        temporary_project_t project{};
        WriteShaderV2Case(
            project,
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
    interface = { parameters = { exposure = { type = "f32" } } }
}
)cykv",
            R"glsl(#version 410 core
uniform vec2 exposure;
void main(){ gl_Position = vec4(exposure, 0.0, 1.0); }
)glsl",
            emptyFragment );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_INTERFACE_FAILED );
        CHECK( fixture.capture.lastDiagnosticMessage.find( "type" ) !=
               std::string::npos );
    }

    SECTION( "plain uniform array shape mismatch is rejected" )
    {
        temporary_project_t project{};
        WriteShaderV2Case(
            project,
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
    interface = { parameters = { exposure = { type = "f32" } } }
}
)cykv",
            R"glsl(#version 410 core
uniform float exposure[2];
void main(){ gl_Position = vec4(0.0, 0.0, exposure[1], 1.0); }
)glsl",
            emptyFragment );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_INTERFACE_FAILED );
        CHECK( fixture.capture.lastDiagnosticMessage.find( "array" ) !=
               std::string::npos );
    }

    SECTION( "texture dimension mismatch is rejected" )
    {
        temporary_project_t project{};
        WriteShaderV2Case(
            project,
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
            environment = { type = "texture2d" usage = "color" }
        }
    }
}
)cykv",
            R"glsl(#version 410 core
void main(){ gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }
)glsl",
            R"glsl(#version 410 core
layout(location = 0) out vec4 outColor;
uniform samplerCube environment;
void main(){ outColor = texture(environment, vec3(0.0, 0.0, 1.0)); }
)glsl" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_INTERFACE_FAILED );
        CHECK( fixture.capture.lastDiagnosticMessage.find( "dimension" ) !=
               std::string::npos );
    }

    SECTION( "descriptor array shape mismatch is rejected" )
    {
        temporary_project_t project{};
        WriteShaderV2Case(
            project,
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
            albedo_texture = { type = "texture2d" usage = "color" }
        }
    }
}
)cykv",
            R"glsl(#version 410 core
void main(){ gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }
)glsl",
            R"glsl(#version 410 core
layout(location = 0) out vec4 outColor;
uniform sampler2D albedo_texture[2];
void main(){ outColor = texture(albedo_texture[1], vec2(0.5)); }
)glsl" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_INTERFACE_FAILED );
        CHECK( fixture.capture.lastDiagnosticMessage.find( "array" ) !=
               std::string::npos );
    }

    SECTION( "independent sampler declaration is rejected explicitly" )
    {
        temporary_project_t project{};
        WriteShaderV2Case(
            project,
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
    interface = {
        samplers = {
            linear_sampler = { type = "filtering" default = "linear_wrap" }
        }
    }
}
)cykv",
            R"glsl(#version 410 core
void main(){ gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }
)glsl",
            emptyFragment );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_UNSUPPORTED_SAMPLERS );
        CHECK( fixture.capture.lastDiagnosticMessage.find( "linear_sampler" ) !=
               std::string::npos );
    }

    SECTION( "cross-stage uniform type conflict is rejected by the linker" )
    {
        temporary_project_t project{};
        WriteShaderV2Case(
            project,
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
    interface = { parameters = { shared_value = { type = "f32" } } }
}
)cykv",
            R"glsl(#version 410 core
uniform float shared_value;
void main(){ gl_Position = vec4(0.0, 0.0, shared_value, 1.0); }
)glsl",
            R"glsl(#version 410 core
layout(location = 0) out vec4 outColor;
uniform vec2 shared_value;
void main(){ outColor = vec4(shared_value, 0.0, 1.0); }
)glsl" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_LINK_FAILED );
    }
}

TEST_CASE( "Shader compiler enforces V2 variant limits without losing features",
           "[CypherTools][ShaderCompiler][V2][Features][Invalid]" )
{
    SECTION( "static variant count exceeds the declared budget" )
    {
        temporary_project_t project{};
        project.Write(
            "shaders/world.cyshader",
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
    features = {
        detail = { type = "bool" default = false }
        quality = {
            type = "enum"
            values = ["low", "medium", "high"]
            default = "high"
        }
    }
    variant_budget = 4u
}
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_SCHEMA_FAILED );
        CHECK( fixture.capture.lastDiagnosticMessage.find( "variant_budget" ) !=
               std::string::npos );
    }

    SECTION( "valid feature declarations wait for a versioned variant table" )
    {
        temporary_project_t project{};
        project.Write(
            "shaders/world.cyshader",
            R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" }
        fragment = { source = "shaders/world.frag" }
    }
    features = {
        detail = { type = "bool" default = false }
    }
    variant_budget = 2u
}
)cykv" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_UNSUPPORTED_FEATURES );
        CHECK_FALSE( fixture.capture.lastDiagnosticHint.empty() );
    }
}

TEST_CASE( "Shader compiler reports the resolved native path for missing input",
           "[CypherTools][ShaderCompiler][FileSystem]" )
{
    temporary_project_t project{};
    compiler_fixture_t fixture{ project };
    tool_report_t report{};

    CHECK( fixture.Compile(
        TestText( "shaders/world.cyshader_c" ),
        report ) == tool_status_t::IO_ERROR );
    CHECK( report.nFailed == 1u );
    CHECK( report.nErrors == 1u );
    CHECK( fixture.capture.lastDiagnostic ==
           CY_SHADER_DIAGNOSTIC_READ_FAILED );
    CHECK( std::filesystem::path( fixture.capture.lastDiagnosticHint ) ==
           std::filesystem::weakly_canonical(
               project.source / "shaders/world.cyshader" ) );
}

TEST_CASE( "Shader compiler rejects invalid GLSL and stage interface mismatches",
           "[CypherTools][ShaderCompiler][GLSL]" )
{
    SECTION( "invalid stage syntax" )
    {
        temporary_project_t project{};
        WriteValidShader( project );
        project.Write(
            "shaders/world.vert",
            "#version 410 core\nvoid main( {\n" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_PARSE_FAILED );
    }

    SECTION( "cross-stage interface mismatch" )
    {
        temporary_project_t project{};
        WriteValidShader( project );
        project.Write(
            "shaders/world.vert",
            "#version 410 core\n"
            "out vec3 sharedValue;\n"
            "void main(){ sharedValue=vec3(1); gl_Position=vec4(0,0,0,1);}\n" );
        project.Write(
            "shaders/world.frag",
            "#version 410 core\n"
            "in vec2 sharedValue; out vec4 color;\n"
            "void main(){ color=vec4(sharedValue,0,1);}\n" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_LINK_FAILED );
    }
}

TEST_CASE( "Shader compiler resolves nested local includes through the VFS",
           "[CypherTools][ShaderCompiler][Include]" )
{
    temporary_project_t project{};
    WriteValidShader( project );
    project.Write(
        "shaders/world.vert",
        "#version 410 core\n"
        "#extension GL_GOOGLE_include_directive : require\n"
        "#include \"include/shared.glsl\"\n"
        "layout(location=0) out vec2 vUv;\n"
        "void main(){ vUv=CySharedUv(); gl_Position=vec4(0,0,0,1);}\n" );
    project.Write(
        "shaders/world.frag",
        "#version 410 core\n"
        "#extension GL_GOOGLE_include_directive : require\n"
        "#include \"include/shared.glsl\"\n"
        "layout(location=0) in vec2 vUv;\n"
        "layout(location=0) out vec4 outColor;\n"
        "void main(){ outColor=vec4(vUv,0,1);}\n" );
    project.Write(
        "shaders/include/shared.glsl",
        "#include \"nested/constants.glsl\"\n"
        "vec2 CySharedUv(){ return vec2(CY_SHARED_VALUE); }\n" );
    project.Write(
        "shaders/include/nested/constants.glsl",
        "const float CY_SHARED_VALUE = 0.5;\n" );
    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/world.cyshader_c" ),
        report ) == tool_status_t::OK );
    CHECK( fixture.capture.nDependencies == 8u );
    CHECK( fixture.capture.bSawSharedDependency );
    CHECK( fixture.capture.bSawNestedDependency );
    CHECK( fixture.capture.bSawTransitiveDependency );
    CHECK( report.cbRead >
           sizeof( g_validRecipe ) +
               sizeof( g_validVertex ) +
               sizeof( g_validFragment ) );
}

TEST_CASE( "Shader compiler rejects unsafe or unavailable GLSL includes",
           "[CypherTools][ShaderCompiler][Include][Invalid]" )
{
    SECTION( "missing local include" )
    {
        temporary_project_t project{};
        WriteValidShader( project );
        project.Write(
            "shaders/world.vert",
            "#version 410 core\n"
            "#extension GL_GOOGLE_include_directive : require\n"
            "#include \"include/missing.glsl\"\n"
            "layout(location=0) out vec2 vUv;\n"
            "void main(){ vUv=vec2(0); gl_Position=vec4(0,0,0,1);}\n" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::IO_ERROR );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_INCLUDE_FAILED );
    }

    SECTION( "path escapes the source root" )
    {
        temporary_project_t project{};
        WriteValidShader( project );
        project.Write(
            "shaders/world.vert",
            "#version 410 core\n"
            "#extension GL_GOOGLE_include_directive : require\n"
            "#include \"../../outside.glsl\"\n"
            "layout(location=0) out vec2 vUv;\n"
            "void main(){ vUv=vec2(0); gl_Position=vec4(0,0,0,1);}\n" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_INCLUDE_FAILED );
    }

    SECTION( "system include is not reproducible" )
    {
        temporary_project_t project{};
        WriteValidShader( project );
        project.Write(
            "shaders/world.vert",
            "#version 410 core\n"
            "#extension GL_GOOGLE_include_directive : require\n"
            "#include <shared.glsl>\n"
            "layout(location=0) out vec2 vUv;\n"
            "void main(){ vUv=vec2(0); gl_Position=vec4(0,0,0,1);}\n" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_UNSUPPORTED_INCLUDE );
    }
}

TEST_CASE( "Transitive include content participates in shader source identity",
           "[CypherTools][ShaderCompiler][Include][Determinism]" )
{
    temporary_project_t project{};
    WriteValidShader( project );
    project.Write(
        "shaders/world.vert",
        "#version 410 core\n"
        "#extension GL_GOOGLE_include_directive : require\n"
        "#include \"include/value.glsl\"\n"
        "layout(location=0) out vec2 vUv;\n"
        "void main(){ vUv=vec2(CY_VALUE); gl_Position=vec4(0,0,0,1);}\n" );
    project.Write(
        "shaders/include/value.glsl",
        "const float CY_VALUE = 0.25;\n" );

    compiler_fixture_t fixture{ project };
    tool_report_t firstReport{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/first.cyshader_c" ),
        firstReport ) == tool_status_t::OK );
    blob_t first{};
    ReadBlob( project.OutputPath( "shaders/first.cyshader_c" ), first );
    cooked_shader_view_t firstShader{};
    REQUIRE( CookedShader_Succeeded(
        CookedShader_Read( Blob_Block( &first ), &firstShader ) ) );

    project.Write(
        "shaders/include/value.glsl",
        "const float CY_VALUE = 0.75;\n" );
    tool_report_t secondReport{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/second.cyshader_c" ),
        secondReport ) == tool_status_t::OK );
    blob_t second{};
    ReadBlob( project.OutputPath( "shaders/second.cyshader_c" ), second );
    cooked_shader_view_t secondShader{};
    REQUIRE( CookedShader_Succeeded(
        CookedShader_Read( Blob_Block( &second ), &secondShader ) ) );

    CHECK_FALSE( ContentHash_Equals(
        firstShader.sourceHash,
        secondShader.sourceHash ) );
    CHECK_FALSE( Cy_MemEqual(
        first.pData,
        second.pData,
        first.cbSize < second.cbSize ? first.cbSize : second.cbSize ) );
}

TEST_CASE( "Shader compiler supports versioned desktop GLSL core profiles",
           "[CypherTools][ShaderCompiler][GLSL][Version]" )
{
    temporary_project_t project{};
    WriteValidShader( project );
    project.Write(
        "shaders/world.vert",
        "// An older supported desktop profile.\n"
        "#version 330 core\n"
        "out vec2 vUv;\n"
        "void main(){ vUv=vec2(0); gl_Position=vec4(0,0,0,1);}\n" );
    project.Write(
        "shaders/world.frag",
        "/* The profile may follow leading comments. */\n"
        "#version 330 core\n"
        "in vec2 vUv; out vec4 color;\n"
        "void main(){ color=vec4(vUv,0,1);}\n" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
        TestText( "shaders/world.cyshader_c" ),
        report ) == tool_status_t::OK );

    blob_t cooked{};
    ReadBlob( project.OutputPath( "shaders/world.cyshader_c" ), cooked );
    cooked_shader_view_t shader{};
    REQUIRE( CookedShader_Succeeded(
        CookedShader_Read( Blob_Block( &cooked ), &shader ) ) );
    CHECK( shader.languageProfile ==
           render_shader_language_profile_t::GLSL_CORE );
    CHECK( shader.nLanguageVersion == 330u );
}

TEST_CASE( "Shader compiler rejects incompatible GLSL profile contracts",
           "[CypherTools][ShaderCompiler][GLSL][Version][Invalid]" )
{
    SECTION( "stage versions differ" )
    {
        temporary_project_t project{};
        WriteValidShader( project );
        project.Write(
            "shaders/world.vert",
            "#version 330 core\n"
            "out vec2 value;\n"
            "void main(){ value=vec2(0); gl_Position=vec4(0,0,0,1);}\n" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_GLSL_PROFILE_MISMATCH );
    }

    SECTION( "compatibility profile is outside shader version 1" )
    {
        temporary_project_t project{};
        WriteValidShader( project );
        project.Write(
            "shaders/world.vert",
            "#version 330 compatibility\nvoid main(){ gl_Position=vec4(0);}\n" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_UNSUPPORTED_GLSL_PROFILE );
    }

    SECTION( "selected platform limits the maximum version" )
    {
        temporary_project_t project{};
        WriteValidShader( project );
        project.Write(
            "shaders/world.vert",
            "#version 450 core\n"
            "out vec2 value;\n"
            "void main(){ value=vec2(0); gl_Position=vec4(0,0,0,1);}\n" );
        project.Write(
            "shaders/world.frag",
            "#version 450 core\n"
            "in vec2 value; out vec4 color;\n"
            "void main(){ color=vec4(value,0,1);}\n" );
        compiler_fixture_t fixture{ project };
        fixture.context.target = {
            tool_platform_t::LINUX,
            tool_architecture_t::X64
        };
        tool_report_t linuxReport{};
        REQUIRE( fixture.Compile(
            TestText( "shaders/linux.cyshader_c" ),
            linuxReport ) == tool_status_t::OK );

        fixture.capture = {};
        fixture.context.target = {
            tool_platform_t::MACOS,
            tool_architecture_t::ARM64
        };
        tool_report_t macReport{};
        CHECK( fixture.Compile(
            TestText( "shaders/world.cyshader_c" ),
            macReport ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_SHADER_DIAGNOSTIC_UNSUPPORTED_GLSL_PROFILE );
    }
}
