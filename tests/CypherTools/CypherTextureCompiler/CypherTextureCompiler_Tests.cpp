//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherTools/CypherTextureCompiler/CypherTextureCompiler_Tests.cpp
//  Purpose: Tests the complete `.cytex` source-to-cooked compiler path.
//  Details: Tests exercise real PNG, JPEG, and EXR importers, semantic mip
//           generation, deterministic CYRS output, ToolFramework records,
//           diagnostics, malformed inputs, and dry-run behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTextureCompiler.h"

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Blob.h"
#include "CypherCommon_CookedTexture.h"
#include "CypherCommon_Endian.h"
#include "CypherCommon_FileIo.h"
#include "CypherCommon_MemoryOps.h"
#include "CypherCommon_ToolFramework.h"
#include "CypherCommon_Vfs.h"
#include "CypherCommon_VfsDirectory.h"

#include <png.h>
#include <tinyexr.h>
#include <turbojpeg.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

void WriteBytes(
    const std::filesystem::path &path,
    const void *pData,
    usize cbData )
{
    std::filesystem::create_directories( path.parent_path() );
    std::ofstream file( path, std::ios::binary | std::ios::trunc );
    REQUIRE( file.is_open() );
    file.write(
        static_cast<const char *>( pData ),
        static_cast<std::streamsize>( cbData ) );
    REQUIRE( file.good() );
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
               ( "cypher_texture_compiler_" +
                 std::to_string( nSequence.fetch_add( 1u ) ) );
        source = root / "source";
        output = root / "output";
        std::filesystem::create_directories( source / "textures/source" );
        std::filesystem::create_directories( output / "textures" );
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

    void WriteText( const char *pRelativePath, const char *pContents ) const
    {
        WriteBytes(
            source / pRelativePath,
            pContents,
            std::strlen( pContents ) );
    }

    void WriteBinary(
        const char *pRelativePath,
        const std::vector<byte> &bytes ) const
    {
        WriteBytes( source / pRelativePath, bytes.data(), bytes.size() );
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
    u32 nReports{ 0u };
    tool_diagnostic_code_t lastDiagnostic{ CY_TOOL_DIAGNOSTIC_NONE };
    u32 nLastDiagnosticLine{ 0u };
    u32 nLastDiagnosticColumn{ 0u };
    tool_status_t reportedStatus{ tool_status_t::INTERNAL_ERROR };
    content_hash_t artifactHash{};
    content_hash_t configurationHash{};
    std::string lastDiagnosticMessage{};
    bool_t bSawRecipeDependency{ CY_FALSE };
    bool_t bSawImageDependency{ CY_FALSE };
    bool_t bSawCompilerDependency{ CY_FALSE };
    bool_t bSawToolchainDependency{ CY_FALSE };
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
    capture.lastDiagnosticMessage.clear();
    if ( diagnostic.message.pData != nullptr ) {
        capture.lastDiagnosticMessage.assign(
            diagnostic.message.pData,
            diagnostic.message.cchLength );
    }
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
        TestText( "textures/panel.cytex" ) );
    capture.bSawImageDependency |= StringView_Equals(
        dependency.path,
        TestText( "textures/source/panel.png" ) ) ||
        StringView_Equals(
            dependency.path,
            TestText( "textures/source/panel.jpg" ) ) ||
        StringView_Equals(
            dependency.path,
            TestText( "textures/source/panel.exr" ) );
    capture.bSawCompilerDependency |= StringView_Equals(
        dependency.path,
        TestText( "toolchain/cypher-texture-compiler" ) );
    capture.bSawToolchainDependency |= StringView_Equals(
        dependency.path,
        TestText( "toolchain/image-import" ) );
    if ( StringView_Equals(
             dependency.path,
             TestText( "configuration/texture-target-profile" ) ) ) {
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
    string_view_t input{ TestText( "textures/panel.cytex" ) };
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
            CypherTextureCompiler_Descriptor();
        const tool_compile_request_t request{
            &invocation,
            23u,
            input,
            output,
            pCompiler->resourceType
        };
        return ToolCompiler_Execute( *pCompiler, request, &report );
    }
};

std::vector<byte> MakePng(
    u32 nWidth,
    u32 nHeight,
    const std::vector<byte> &rgba )
{
    REQUIRE( rgba.size() ==
             static_cast<usize>( nWidth ) * nHeight * 4u );
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = nWidth;
    image.height = nHeight;
    image.format = PNG_FORMAT_RGBA;
    png_alloc_size_t cbPng = 0u;
    REQUIRE( png_image_write_to_memory(
                 &image,
                 nullptr,
                 &cbPng,
                 0,
                 rgba.data(),
                 0,
                 nullptr ) != 0 );
    std::vector<byte> encoded( static_cast<usize>( cbPng ) );
    REQUIRE( png_image_write_to_memory(
                 &image,
                 encoded.data(),
                 &cbPng,
                 0,
                 rgba.data(),
                 0,
                 nullptr ) != 0 );
    encoded.resize( static_cast<usize>( cbPng ) );
    return encoded;
}

std::vector<byte> MakeJpeg(
    u32 nWidth,
    u32 nHeight,
    const std::vector<byte> &rgba )
{
    REQUIRE( rgba.size() ==
             static_cast<usize>( nWidth ) * nHeight * 4u );
    tjhandle encoder = tj3Init( TJINIT_COMPRESS );
    REQUIRE( encoder != nullptr );
    REQUIRE( tj3Set( encoder, TJPARAM_QUALITY, 95 ) == 0 );
    REQUIRE( tj3Set( encoder, TJPARAM_SUBSAMP, TJSAMP_444 ) == 0 );
    unsigned char *pJpeg = nullptr;
    size_t cbJpeg = 0u;
    REQUIRE( tj3Compress8(
                 encoder,
                 rgba.data(),
                 static_cast<int>( nWidth ),
                 0,
                 static_cast<int>( nHeight ),
                 TJPF_RGBA,
                 &pJpeg,
                 &cbJpeg ) == 0 );
    std::vector<byte> encoded( pJpeg, pJpeg + cbJpeg );
    tj3Free( pJpeg );
    tj3Destroy( encoder );
    return encoded;
}

std::vector<byte> MakeExr(
    u32 nWidth,
    u32 nHeight,
    const std::vector<f32> &rgba )
{
    REQUIRE( rgba.size() ==
             static_cast<usize>( nWidth ) * nHeight * 4u );
    unsigned char *pExr = nullptr;
    const char *pError = nullptr;
    const int cbExr = SaveEXRToMemory(
        rgba.data(),
        static_cast<int>( nWidth ),
        static_cast<int>( nHeight ),
        4,
        0,
        &pExr,
        &pError );
    INFO( ( pError != nullptr ? pError : "" ) );
    REQUIRE( cbExr > 0 );
    std::vector<byte> encoded( pExr, pExr + cbExr );
    std::free( pExr );
    if ( pError != nullptr ) {
        FreeEXRErrorMessage( pError );
    }
    return encoded;
}

void WriteRecipe(
    const temporary_project_t &project,
    const char *pSource,
    const char *pOptions = "" )
{
    const std::string recipe =
        "@cykv 1\n"
        "@schema \"cypher.texture\" 1\n"
        "{\n    source = \"" + std::string( pSource ) + "\"\n" +
        pOptions + "\n}\n";
    project.WriteText( "textures/panel.cytex", recipe.c_str() );
}

void WriteRecipeV2(
    const temporary_project_t &project,
    const char *pSource,
    const char *pOptions = "",
    const char *pUsage = "color",
    const char *pColorSpace = "srgb" )
{
    const std::string recipe =
        "@cykv 1\n"
        "@schema \"cypher.texture\" 2\n"
        "{\n"
        "    source = \"" + std::string( pSource ) + "\"\n"
        "    type = \"2d\"\n"
        "    usage = \"" + std::string( pUsage ) + "\"\n"
        "    color_space = \"" + std::string( pColorSpace ) + "\"\n" +
        pOptions + "\n}\n";
    project.WriteText( "textures/panel.cytex", recipe.c_str() );
}

cooked_texture_target_t ExpectedCookedTarget(
    tool_target_t target ) noexcept
{
    if ( target.platform == tool_platform_t::MACOS ) {
        return cooked_texture_target_t::APPLE;
    }
    if ( target.platform == tool_platform_t::WINDOWS ||
         target.platform == tool_platform_t::LINUX ) {
        return cooked_texture_target_t::DESKTOP;
    }
    return cooked_texture_target_t::PORTABLE;
}

void ReadBlob( const std::string &path, blob_t &blob )
{
    REQUIRE( Blob_Init( &blob, Allocator_GetSystem() ) );
    REQUIRE( FileIo_ReadAllNative( View( path ), &blob ) );
}

f32 ReadLittleF32( const byte *pData )
{
    f32 value = 0.0f;
    Cy_MemCopy( &value, pData, sizeof( value ) );
    return Cy_LittleToHostF32( value );
}

} // namespace

TEST_CASE( "Texture compiler descriptor owns `.cytex` inputs",
           "[CypherTools][TextureCompiler][Descriptor]" )
{
    const tool_compiler_desc_t *pCompiler =
        CypherTextureCompiler_Descriptor();
    REQUIRE( pCompiler != nullptr );
    CHECK( ToolCompiler_CheckDescriptor( *pCompiler ) == tool_status_t::OK );
    CHECK( pCompiler->nApiVersion == CY_TEXTURE_COMPILER_API_VERSION );
    CHECK( pCompiler->nCompilerVersion == CY_TEXTURE_COMPILER_VERSION );
    CHECK( ToolCompiler_SupportsInput(
        *pCompiler,
        TestText( "textures/panel.cytex" ) ) );
    CHECK_FALSE( ToolCompiler_SupportsInput(
        *pCompiler,
        TestText( "materials/panel.cymat" ) ) );
}

TEST_CASE( "Texture compiler preflights decoded mip-chain capacity",
           "[CypherTools][TextureCompiler][Mips][Capacity]" )
{
    u32 nMipLevels = 77u;
    u64 cbMipData = 99u;
    REQUIRE( CypherTextureCompiler_CalculateMipDataSize(
        5632u,
        5632u,
        16u,
        CY_TRUE,
        &nMipLevels,
        &cbMipData ) );
    REQUIRE( nMipLevels == CookedTexture_FullMipCount( 5632u, 5632u ) );
    REQUIRE( cbMipData > CY_COOKED_TEXTURE_MAX_TOTAL_DATA_SIZE );

    u32 nSingleLevel = 0u;
    u64 cbSingleLevel = 0u;
    REQUIRE( CypherTextureCompiler_CalculateMipDataSize(
        5632u,
        5632u,
        16u,
        CY_FALSE,
        &nSingleLevel,
        &cbSingleLevel ) );
    REQUIRE( nSingleLevel == 1u );
    REQUIRE( cbSingleLevel == 507510784u );
    REQUIRE( cbSingleLevel <= CY_COOKED_TEXTURE_MAX_TOTAL_DATA_SIZE );

    nMipLevels = 77u;
    cbMipData = 99u;
    REQUIRE_FALSE( CypherTextureCompiler_CalculateMipDataSize(
        0u,
        1u,
        4u,
        CY_TRUE,
        &nMipLevels,
        &cbMipData ) );
    REQUIRE( nMipLevels == 77u );
    REQUIRE( cbMipData == 99u );
}

TEST_CASE( "Texture compiler imports PNG and emits a complete mip chain",
           "[CypherTools][TextureCompiler][PNG][Integration]" )
{
    temporary_project_t project{};
    const std::vector<byte> pixels{
        255u, 0u, 0u, 255u,      0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u,      255u, 255u, 255u, 255u
    };
    project.WriteBinary(
        "textures/source/panel.png",
        MakePng( 2u, 2u, pixels ) );
    WriteRecipe( project, "textures/source/panel.png" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
                 TestText( "textures/panel.cytex_c" ),
                 report ) == tool_status_t::OK );
    CHECK( report.nInputsProcessed == 1u );
    CHECK( report.nSucceeded == 1u );
    CHECK( report.nFailed == 0u );
    CHECK( report.nArtifacts == 1u );
    CHECK( fixture.capture.nDependencies == 5u );
    CHECK( fixture.capture.bSawRecipeDependency );
    CHECK( fixture.capture.bSawImageDependency );
    CHECK( fixture.capture.bSawCompilerDependency );
    CHECK( fixture.capture.bSawToolchainDependency );
    CHECK( fixture.capture.bSawConfigurationDependency );
    CHECK( ContentHash_IsValid( fixture.capture.configurationHash ) );
    CHECK( fixture.capture.bSawCompletedProgress );

    blob_t cooked{};
    ReadBlob( project.OutputPath( "textures/panel.cytex_c" ), cooked );
    CHECK( ContentHash_Equals(
        fixture.capture.artifactHash,
        ContentHash_Data( Blob_Block( &cooked ) ) ) );
    cooked_texture_view_t texture{};
    REQUIRE( CookedTexture_Succeeded(
        CookedTexture_Read( Blob_Block( &cooked ), &texture ) ) );
    CHECK( texture.nResourceVersion ==
           CY_COOKED_TEXTURE_RESOURCE_VERSION_V2 );
    CHECK( texture.desc.nWidth == 2u );
    CHECK( texture.desc.nHeight == 2u );
    CHECK( texture.desc.nMipLevels == 2u );
    CHECK( texture.desc.pixelFormat ==
           render_texture_pixel_format_t::RGBA8_SRGB );
    CHECK( texture.desc.storageFormat == render_format_t::RGBA8_SRGB );
    CHECK( texture.desc.usage == render_texture_usage_t::COLOR );
    CHECK( texture.desc.alphaMode ==
           cooked_texture_alpha_mode_t::STRAIGHT );
    CHECK( texture.desc.target ==
           ExpectedCookedTarget( fixture.context.target ) );
    CHECK( texture.desc.residency ==
           cooked_texture_residency_t::FULLY_RESIDENT );
    CHECK( texture.desc.nResidentMipLevels == 2u );
    CHECK( texture.desc.nStreamingPriority == 0u );
    REQUIRE( texture.mips[0].pixels.cbSize == pixels.size() );
    CHECK( Cy_MemEqual(
        texture.mips[0].pixels.pData,
        pixels.data(),
        pixels.size() ) );
    REQUIRE( texture.mips[1].pixels.cbSize == 4u );
    CHECK( texture.mips[1].pixels.pData[3] == 255u );
}

TEST_CASE( "Texture compiler output is deterministic and dry runs do not write",
           "[CypherTools][TextureCompiler][Determinism][DryRun]" )
{
    temporary_project_t project{};
    std::vector<byte> pixels( 3u * 5u * 4u, 127u );
    for ( usize iPixel = 0u; iPixel < 15u; ++iPixel ) {
        pixels[iPixel * 4u + 3u] = 255u;
    }
    project.WriteBinary(
        "textures/source/panel.png",
        MakePng( 3u, 5u, pixels ) );
    project.WriteText(
        "textures/panel.cytex",
        "@cykv 1\n"
        "@schema \"cypher.texture\" 2\n"
        "{\n"
        "    source = \"textures/source/panel.png\"\n"
        "    type = \"2d\"\n"
        "    usage = \"data\"\n"
        "    color_space = \"linear\"\n"
        "    alpha = { mode = \"data\" }\n"
        "    mips = { mode = \"generate\" filter = \"box\" edge = \"clamp\" }\n"
        "    output = { format = \"auto\" quality = \"balanced\" }\n"
        "}\n" );

    compiler_fixture_t fixture{ project };
    tool_report_t firstReport{};
    tool_report_t secondReport{};
    REQUIRE( fixture.Compile(
                 TestText( "textures/first.cytex_c" ),
                 firstReport ) == tool_status_t::OK );
    REQUIRE( fixture.Compile(
                 TestText( "textures/second.cytex_c" ),
                 secondReport ) == tool_status_t::OK );
    blob_t first{};
    blob_t second{};
    ReadBlob( project.OutputPath( "textures/first.cytex_c" ), first );
    ReadBlob( project.OutputPath( "textures/second.cytex_c" ), second );
    REQUIRE( first.cbSize == second.cbSize );
    CHECK( Cy_MemEqual( first.pData, second.pData, first.cbSize ) );

    host_capture_t dryCapture{};
    fixture.capture = dryCapture;
    tool_report_t dryReport{};
    REQUIRE( fixture.Compile(
                 {},
                 dryReport,
                 TOOL_INVOCATION_FLAG_DRY_RUN ) == tool_status_t::OK );
    CHECK( dryReport.nSucceeded == 1u );
    CHECK( dryReport.nArtifacts == 0u );
    CHECK( dryReport.cbWritten == 0u );
    CHECK( fixture.capture.nArtifacts == 0u );
    CHECK( fixture.capture.nDependencies == 5u );
}

TEST_CASE( "Texture compiler ignores authoring-only CYKV formatting",
           "[CypherTools][TextureCompiler][Determinism][CYKV]" )
{
    temporary_project_t project{};
    std::vector<byte> pixels( 4u * 4u * 4u, 91u );
    for ( usize iPixel = 0u; iPixel < 16u; ++iPixel ) {
        pixels[iPixel * 4u + 3u] = 255u;
    }
    project.WriteBinary(
        "textures/source/panel.png",
        MakePng( 4u, 4u, pixels ) );
    project.WriteText(
        "textures/panel.cytex",
        "@cykv 1\n"
        "@schema \"cypher.texture\" 2\n"
        "{\n"
        "    source = \"textures/source/panel.png\"\n"
        "    type = \"2d\"\n"
        "    usage = \"data\"\n"
        "    color_space = \"linear\"\n"
        "    alpha = { mode = \"data\" }\n"
        "    mips = { mode = \"generate\" filter = \"box\" edge = \"clamp\" }\n"
        "    output = { format = \"auto\" quality = \"balanced\" }\n"
        "}\n" );

    compiler_fixture_t fixture{ project };
    tool_report_t firstReport{};
    tool_report_t reformattedReport{};
    REQUIRE( fixture.Compile(
        TestText( "textures/canonical.cytex_c" ),
        firstReport ) == tool_status_t::OK );

    project.WriteText(
        "textures/panel.cytex",
        "@cykv 1\n"
        "@schema \"cypher.texture\" 2\n"
        "{ // Reordering, whitespace, and comments are authoring-only.\n"
        "    output = { format = \"auto\" quality = \"balanced\" }\n"
        "    mips = { mode = \"generate\" filter = \"box\" edge = \"clamp\" }\n"
        "    color_space = \"linear\"\n"
        "    alpha = { mode = \"data\" }\n"
        "    usage = \"data\"\n"
        "    type = \"2d\"\n"
        "    source = \"textures/source/panel.png\" // same source\n"
        "}\n" );
    REQUIRE( fixture.Compile(
        TestText( "textures/reformatted.cytex_c" ),
        reformattedReport ) == tool_status_t::OK );

    blob_t canonical{};
    blob_t reformatted{};
    ReadBlob(
        project.OutputPath( "textures/canonical.cytex_c" ),
        canonical );
    ReadBlob(
        project.OutputPath( "textures/reformatted.cytex_c" ),
        reformatted );
    REQUIRE( canonical.cbSize == reformatted.cbSize );
    CHECK( Cy_MemEqual(
        canonical.pData,
        reformatted.pData,
        canonical.cbSize ) );
}

TEST_CASE( "Texture compiler identities include target and build profile",
           "[CypherTools][TextureCompiler][Identity][Configuration]" )
{
    temporary_project_t project{};
    const std::vector<byte> pixels( 2u * 2u * 4u, 255u );
    project.WriteBinary(
        "textures/source/panel.png",
        MakePng( 2u, 2u, pixels ) );
    WriteRecipeV2(
        project,
        "textures/source/panel.png",
        "    mips = { mode = \"none\" }" );

    compiler_fixture_t fixture{ project };
    fixture.context.target = {
        tool_platform_t::LINUX,
        tool_architecture_t::X64
    };
    fixture.context.profile = tool_profile_t::DEVELOPMENT;
    tool_report_t linuxDevelopmentReport{};
    REQUIRE( fixture.Compile(
        TestText( "textures/linux-development.cytex_c" ),
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
        TestText( "textures/mac-development.cytex_c" ),
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
        TestText( "textures/linux-shipping.cytex_c" ),
        linuxShippingReport ) == tool_status_t::OK );
    const content_hash_t linuxShippingConfiguration =
        fixture.capture.configurationHash;

    blob_t linuxDevelopment{};
    blob_t macDevelopment{};
    blob_t linuxShipping{};
    ReadBlob(
        project.OutputPath( "textures/linux-development.cytex_c" ),
        linuxDevelopment );
    ReadBlob(
        project.OutputPath( "textures/mac-development.cytex_c" ),
        macDevelopment );
    ReadBlob(
        project.OutputPath( "textures/linux-shipping.cytex_c" ),
        linuxShipping );
    cooked_texture_view_t linuxDevelopmentTexture{};
    cooked_texture_view_t macDevelopmentTexture{};
    cooked_texture_view_t linuxShippingTexture{};
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Read(
        Blob_Block( &linuxDevelopment ),
        &linuxDevelopmentTexture ) ) );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Read(
        Blob_Block( &macDevelopment ),
        &macDevelopmentTexture ) ) );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Read(
        Blob_Block( &linuxShipping ),
        &linuxShippingTexture ) ) );

    CHECK( linuxDevelopmentTexture.desc.target ==
           cooked_texture_target_t::DESKTOP );
    CHECK( macDevelopmentTexture.desc.target ==
           cooked_texture_target_t::APPLE );
    CHECK_FALSE( ContentHash_Equals(
        linuxDevelopmentTexture.sourceHash,
        macDevelopmentTexture.sourceHash ) );
    CHECK_FALSE( ContentHash_Equals(
        linuxDevelopmentTexture.sourceHash,
        linuxShippingTexture.sourceHash ) );
    CHECK_FALSE( ContentHash_Equals(
        linuxDevelopmentConfiguration,
        macDevelopmentConfiguration ) );
    CHECK_FALSE( ContentHash_Equals(
        linuxDevelopmentConfiguration,
        linuxShippingConfiguration ) );
}

TEST_CASE( "Texture V2 compiles rich policy into streamed CYTX metadata",
           "[CypherTools][TextureCompiler][V2][Streaming]" )
{
    temporary_project_t project{};
    std::vector<byte> pixels( 4u * 4u * 4u, 173u );
    project.WriteBinary(
        "textures/source/panel.png",
        MakePng( 4u, 4u, pixels ) );
    WriteRecipeV2(
        project,
        "textures/source/panel.png",
        "    alpha = { mode = \"mask\" cutoff = 0.42 }\n"
        "    mips = { mode = \"generate\" filter = \"box\" edge = \"clamp\" }\n"
        "    output = { format = \"uncompressed\" quality = \"production\" }\n"
        "    streaming = { class = \"world\" resident_mips = 2u }" );

    compiler_fixture_t fixture{ project };
    fixture.context.target = {
        tool_platform_t::LINUX,
        tool_architecture_t::X64
    };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
                 TestText( "textures/panel.cytex_c" ),
                 report ) == tool_status_t::OK );

    blob_t cooked{};
    ReadBlob( project.OutputPath( "textures/panel.cytex_c" ), cooked );
    cooked_texture_view_t texture{};
    REQUIRE( CookedTexture_Succeeded(
        CookedTexture_Read( Blob_Block( &cooked ), &texture ) ) );
    CHECK( texture.nResourceVersion ==
           CY_COOKED_TEXTURE_RESOURCE_VERSION_V2 );
    CHECK( texture.desc.storageFormat == render_format_t::RGBA8_SRGB );
    CHECK( texture.desc.alphaMode == cooked_texture_alpha_mode_t::MASK );
    CHECK( std::fabs( texture.desc.alphaCutoff - 0.42f ) < 0.00001f );
    CHECK( texture.desc.target == cooked_texture_target_t::DESKTOP );
    CHECK( texture.desc.residency ==
           cooked_texture_residency_t::MIP_STREAMED );
    CHECK( texture.desc.nMipLevels == 3u );
    CHECK( texture.desc.nSubresources == 3u );
    CHECK( texture.desc.nResidentMipLevels == 2u );
    CHECK( texture.desc.iResidentFirstSubresource == 1u );
    CHECK( texture.desc.nResidentSubresources == 2u );
    CHECK( texture.desc.cbResidentData == 20u );
    // The compiler's stable class table maps world to neutral priority 128.
    CHECK( texture.desc.nStreamingPriority == 128u );
    cooked_texture_subresource_view_t mip2{};
    REQUIRE( CookedTexture_GetSubresource(
        texture,
        2u,
        0u,
        0u,
        0u,
        &mip2 ) );
    CHECK( mip2.nWidth == 1u );
    CHECK( mip2.nHeight == 1u );
    CHECK( mip2.cbRowPitch == 4u );
    CHECK( mip2.cbSlicePitch == 4u );
}

TEST_CASE( "Texture V2 supports no-mip data resources",
           "[CypherTools][TextureCompiler][V2][NoMips]" )
{
    temporary_project_t project{};
    std::vector<byte> pixels( 4u * 4u * 4u, 67u );
    project.WriteBinary(
        "textures/source/panel.png",
        MakePng( 4u, 4u, pixels ) );
    WriteRecipeV2(
        project,
        "textures/source/panel.png",
        "    alpha = { mode = \"data\" }\n"
        "    mips = { mode = \"none\" }\n"
        "    output = { format = \"auto\" quality = \"fast\" }\n"
        "    streaming = { class = \"ui\" resident_mips = 1u }",
        "data",
        "linear" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
                 TestText( "textures/panel.cytex_c" ),
                 report ) == tool_status_t::OK );
    blob_t cooked{};
    ReadBlob( project.OutputPath( "textures/panel.cytex_c" ), cooked );
    cooked_texture_view_t texture{};
    REQUIRE( CookedTexture_Succeeded(
        CookedTexture_Read( Blob_Block( &cooked ), &texture ) ) );
    CHECK( texture.desc.nMipLevels == 1u );
    CHECK( texture.desc.nSubresources == 1u );
    CHECK( texture.desc.storageFormat == render_format_t::RGBA8_UNORM );
    CHECK( texture.desc.alphaMode == cooked_texture_alpha_mode_t::DATA );
    CHECK( ( texture.desc.flags & COOKED_TEXTURE_FLAG_GENERATED_MIPS ) == 0u );
    CHECK( texture.desc.residency ==
           cooked_texture_residency_t::MIP_STREAMED );
    CHECK( texture.desc.nResidentMipLevels == 1u );
    // UI is deliberately above world in the stable scheduling table.
    CHECK( texture.desc.nStreamingPriority == 224u );
}

TEST_CASE( "Texture compiler imports JPEG as canonical RGBA8",
           "[CypherTools][TextureCompiler][JPEG]" )
{
    temporary_project_t project{};
    const std::vector<byte> pixels{
        255u, 32u, 16u, 255u,
        16u, 64u, 255u, 255u
    };
    project.WriteBinary(
        "textures/source/panel.jpg",
        MakeJpeg( 2u, 1u, pixels ) );
    WriteRecipe(
        project,
        "textures/source/panel.jpg",
        "    generate_mips = false" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
                 TestText( "textures/panel.cytex_c" ),
                 report ) == tool_status_t::OK );
    blob_t cooked{};
    ReadBlob( project.OutputPath( "textures/panel.cytex_c" ), cooked );
    cooked_texture_view_t texture{};
    REQUIRE( CookedTexture_Succeeded(
        CookedTexture_Read( Blob_Block( &cooked ), &texture ) ) );
    CHECK( texture.desc.nWidth == 2u );
    CHECK( texture.desc.nHeight == 1u );
    CHECK( texture.desc.nMipLevels == 1u );
    CHECK( texture.mips[0].pixels.cbSize == 8u );
    CHECK( texture.mips[0].pixels.pData[3] == 255u );
    CHECK( texture.mips[0].pixels.pData[7] == 255u );
}

TEST_CASE( "Texture compiler imports finite EXR into little-endian RGBA32F",
           "[CypherTools][TextureCompiler][EXR]" )
{
    temporary_project_t project{};
    const std::vector<f32> pixels{
        0.25f, 0.5f, 1.5f, 1.0f,
        2.0f, 1.0f, 0.0f, 0.5f
    };
    project.WriteBinary(
        "textures/source/panel.exr",
        MakeExr( 2u, 1u, pixels ) );
    WriteRecipe(
        project,
        "textures/source/panel.exr",
        "    generate_mips = false" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
                 TestText( "textures/panel.cytex_c" ),
                 report ) == tool_status_t::OK );
    blob_t cooked{};
    ReadBlob( project.OutputPath( "textures/panel.cytex_c" ), cooked );
    cooked_texture_view_t texture{};
    REQUIRE( CookedTexture_Succeeded(
        CookedTexture_Read( Blob_Block( &cooked ), &texture ) ) );
    CHECK( texture.desc.pixelFormat ==
           render_texture_pixel_format_t::RGBA32_FLOAT );
    CHECK( texture.desc.colorSpace == render_texture_color_space_t::LINEAR );
    REQUIRE( texture.mips[0].pixels.cbSize == 32u );
    CHECK( std::fabs(
        ReadLittleF32( texture.mips[0].pixels.pData ) - 0.25f ) < 0.001f );
    CHECK( std::fabs(
        ReadLittleF32( texture.mips[0].pixels.pData + 8u ) - 1.5f ) < 0.001f );
}

TEST_CASE( "Texture compiler normal-map mips remain normalized",
           "[CypherTools][TextureCompiler][NormalMap]" )
{
    temporary_project_t project{};
    const std::vector<byte> pixels{
        128u, 128u, 255u, 255u,  255u, 128u, 128u, 255u,
        128u, 255u, 128u, 255u,  128u, 128u, 255u, 255u
    };
    project.WriteBinary(
        "textures/source/panel.png",
        MakePng( 2u, 2u, pixels ) );
    WriteRecipe(
        project,
        "textures/source/panel.png",
        "    usage = \"normal\"" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
                 TestText( "textures/panel.cytex_c" ),
                 report ) == tool_status_t::OK );
    blob_t cooked{};
    ReadBlob( project.OutputPath( "textures/panel.cytex_c" ), cooked );
    cooked_texture_view_t texture{};
    REQUIRE( CookedTexture_Succeeded(
        CookedTexture_Read( Blob_Block( &cooked ), &texture ) ) );
    REQUIRE( texture.desc.nMipLevels == 2u );
    const byte *pMip = texture.mips[1].pixels.pData;
    const f32 nx = static_cast<f32>( pMip[0] ) / 127.5f - 1.0f;
    const f32 ny = static_cast<f32>( pMip[1] ) / 127.5f - 1.0f;
    const f32 nz = static_cast<f32>( pMip[2] ) / 127.5f - 1.0f;
    const f32 nLength = std::sqrt( nx * nx + ny * ny + nz * nz );
    CHECK( std::fabs( nLength - 1.0f ) < 0.02f );
}

TEST_CASE( "Texture compiler box filtering preserves odd-size texel area",
           "[CypherTools][TextureCompiler][Mip][OddDimensions]" )
{
    temporary_project_t project{};
    const std::vector<byte> pixels{
        0u,   0u, 0u, 255u,
        0u,   0u, 0u, 255u,
        255u, 0u, 0u, 255u,
        255u, 0u, 0u, 255u,
        255u, 0u, 0u, 255u
    };
    project.WriteBinary(
        "textures/source/panel.png",
        MakePng( 5u, 1u, pixels ) );
    WriteRecipeV2(
        project,
        "textures/source/panel.png",
        "    alpha = { mode = \"data\" }\n"
        "    mips = { mode = \"generate\" filter = \"box\" }",
        "data",
        "linear" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
        TestText( "textures/panel.cytex_c" ),
        report ) == tool_status_t::OK );
    blob_t cooked{};
    ReadBlob( project.OutputPath( "textures/panel.cytex_c" ), cooked );
    cooked_texture_view_t texture{};
    REQUIRE( CookedTexture_Succeeded(
        CookedTexture_Read( Blob_Block( &cooked ), &texture ) ) );
    cooked_texture_subresource_view_t mip1{};
    REQUIRE( CookedTexture_GetSubresource(
        texture,
        1u,
        0u,
        0u,
        0u,
        &mip1 ) );
    REQUIRE( mip1.nWidth == 2u );
    REQUIRE( mip1.pixels.cbSize == 8u );
    // Destination texel zero covers source texels 0, 1, and half of 2:
    // (0 + 0 + 0.5 * 255) / 2.5 = 51.
    CHECK( mip1.pixels.pData[0] == 51u );
    CHECK( mip1.pixels.pData[4] == 255u );
}

TEST_CASE( "Texture compiler filters straight alpha through premultiplied color",
           "[CypherTools][TextureCompiler][Mip][StraightAlpha]" )
{
    temporary_project_t project{};
    const std::vector<byte> pixels{
        255u, 0u, 0u, 255u,
        0u, 0u, 255u, 0u
    };
    project.WriteBinary(
        "textures/source/panel.png",
        MakePng( 2u, 1u, pixels ) );
    WriteRecipeV2(
        project,
        "textures/source/panel.png",
        "    alpha = { mode = \"straight\" }\n"
        "    mips = { mode = \"generate\" filter = \"box\" }" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
        TestText( "textures/panel.cytex_c" ),
        report ) == tool_status_t::OK );
    blob_t cooked{};
    ReadBlob( project.OutputPath( "textures/panel.cytex_c" ), cooked );
    cooked_texture_view_t texture{};
    REQUIRE( CookedTexture_Succeeded(
        CookedTexture_Read( Blob_Block( &cooked ), &texture ) ) );
    cooked_texture_subresource_view_t mip1{};
    REQUIRE( CookedTexture_GetSubresource(
        texture,
        1u,
        0u,
        0u,
        0u,
        &mip1 ) );
    REQUIRE( mip1.pixels.cbSize == 4u );
    CHECK( mip1.pixels.pData[0] == 255u );
    CHECK( mip1.pixels.pData[1] == 0u );
    CHECK( mip1.pixels.pData[2] == 0u );
    CHECK( mip1.pixels.pData[3] == 128u );
}

TEST_CASE( "Texture compiler renormalizes floating-point normal-map mips",
           "[CypherTools][TextureCompiler][Mip][EXR][NormalMap]" )
{
    temporary_project_t project{};
    const std::vector<f32> pixels{
        0.5f, 0.5f, 1.0f, 1.0f,
        1.0f, 0.5f, 0.5f, 1.0f
    };
    project.WriteBinary(
        "textures/source/panel.exr",
        MakeExr( 2u, 1u, pixels ) );
    WriteRecipeV2(
        project,
        "textures/source/panel.exr",
        "    alpha = { mode = \"none\" }\n"
        "    mips = { mode = \"generate\" filter = \"box\" }",
        "normal",
        "linear" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    REQUIRE( fixture.Compile(
        TestText( "textures/panel.cytex_c" ),
        report ) == tool_status_t::OK );
    blob_t cooked{};
    ReadBlob( project.OutputPath( "textures/panel.cytex_c" ), cooked );
    cooked_texture_view_t texture{};
    REQUIRE( CookedTexture_Succeeded(
        CookedTexture_Read( Blob_Block( &cooked ), &texture ) ) );
    cooked_texture_subresource_view_t mip1{};
    REQUIRE( CookedTexture_GetSubresource(
        texture,
        1u,
        0u,
        0u,
        0u,
        &mip1 ) );
    REQUIRE( mip1.pixels.cbSize == 16u );
    const f32 nx = ReadLittleF32( mip1.pixels.pData ) * 2.0f - 1.0f;
    const f32 ny = ReadLittleF32( mip1.pixels.pData + 4u ) * 2.0f - 1.0f;
    const f32 nz = ReadLittleF32( mip1.pixels.pData + 8u ) * 2.0f - 1.0f;
    const f32 nLength = std::sqrt( nx * nx + ny * ny + nz * nz );
    CHECK( std::fabs( nLength - 1.0f ) < 0.0001f );
    CHECK( std::fabs( nx - 0.7071067f ) < 0.0001f );
    CHECK( std::fabs( ny ) < 0.0001f );
    CHECK( std::fabs( nz - 0.7071067f ) < 0.0001f );
}

TEST_CASE( "Texture V2 rejects policies that the current importer cannot honor",
           "[CypherTools][TextureCompiler][V2][Policy][Diagnostics]" )
{
    struct policy_case_t {
        const char *pName;
        const char *pSource;
        const char *pOptions;
        tool_diagnostic_code_t expectedDiagnostic;
        const char *pExpectedMessage;
    };
    const policy_case_t cases[]{
        {
            "kaiser filter",
            "textures/source/panel.png",
            "    mips = { mode = \"generate\" filter = \"kaiser\" }",
            CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_POLICY,
            "box mip filter"
        },
        {
            "lanczos filter",
            "textures/source/panel.png",
            "    mips = { mode = \"generate\" filter = \"lanczos\" }",
            CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_POLICY,
            "box mip filter"
        },
        {
            "repeat edge",
            "textures/source/panel.png",
            "    mips = { mode = \"generate\" edge = \"repeat\" }",
            CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_POLICY,
            "clamped mip edges"
        },
        {
            "mirror edge",
            "textures/source/panel.png",
            "    mips = { mode = \"generate\" edge = \"mirror\" }",
            CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_POLICY,
            "clamped mip edges"
        },
        {
            "alpha coverage",
            "textures/source/panel.png",
            "    alpha = { mode = \"mask\" }\n"
            "    mips = { mode = \"generate\" preserve_alpha_coverage = true }",
            CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_POLICY,
            "Alpha-coverage"
        },
        {
            "alpha dilation",
            "textures/source/panel.png",
            "    alpha = { mode = \"straight\" dilate_rgb = true }",
            CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_POLICY,
            "RGB dilation"
        },
        {
            "DDS preserve",
            "textures/source/panel.dds",
            "    mips = { mode = \"preserve\" }",
            CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_PRESERVED_IMAGE,
            "DDS/KTX2"
        },
        {
            "KTX2 preserve",
            "textures/source/panel.ktx2",
            "    mips = { mode = \"preserve\" }",
            CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_PRESERVED_IMAGE,
            "DDS/KTX2"
        }
    };

    for ( const policy_case_t &policy : cases ) {
        INFO( policy.pName );
        temporary_project_t project{};
        WriteRecipeV2( project, policy.pSource, policy.pOptions );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "textures/panel.cytex_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic == policy.expectedDiagnostic );
        CHECK( fixture.capture.lastDiagnosticMessage.find(
                   policy.pExpectedMessage ) != std::string::npos );
        CHECK_FALSE( std::filesystem::exists(
            project.output / "textures/panel.cytex_c" ) );
    }
}

TEST_CASE( "Texture V2 rejects resident tails larger than the generated chain",
           "[CypherTools][TextureCompiler][V2][Streaming][Diagnostics]" )
{
    temporary_project_t project{};
    std::vector<byte> pixels( 2u * 2u * 4u, 255u );
    project.WriteBinary(
        "textures/source/panel.png",
        MakePng( 2u, 2u, pixels ) );
    WriteRecipeV2(
        project,
        "textures/source/panel.png",
        "    mips = { mode = \"generate\" filter = \"box\" }\n"
        "    streaming = { class = \"world\" resident_mips = 3u }" );

    compiler_fixture_t fixture{ project };
    tool_report_t report{};
    CHECK( fixture.Compile(
               TestText( "textures/panel.cytex_c" ),
               report ) == tool_status_t::VALIDATION_FAILED );
    CHECK( fixture.capture.lastDiagnostic ==
           CY_TEXTURE_DIAGNOSTIC_INVALID_STREAMING_POLICY );
    CHECK( fixture.capture.lastDiagnosticMessage.find( "resident_mips" ) !=
           std::string::npos );
    CHECK_FALSE( std::filesystem::exists(
        project.output / "textures/panel.cytex_c" ) );
}

TEST_CASE( "Texture compiler rejects malformed input and locates schema errors",
           "[CypherTools][TextureCompiler][Invalid][Diagnostics]" )
{
    SECTION( "malformed image" )
    {
        temporary_project_t project{};
        project.WriteText( "textures/source/panel.png", "not a png" );
        WriteRecipe( project, "textures/source/panel.png" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "textures/panel.cytex_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_TEXTURE_DIAGNOSTIC_IMAGE_DECODE_FAILED );
        CHECK_FALSE( std::filesystem::exists(
            project.output / "textures/panel.cytex_c" ) );
    }

    SECTION( "schema version mismatch" )
    {
        temporary_project_t project{};
        project.WriteText(
            "textures/panel.cytex",
            "@cykv 1\n@schema \"cypher.texture\" 99\n"
            "{ source = \"textures/source/panel.png\" }\n" );
        compiler_fixture_t fixture{ project };
        tool_report_t report{};
        CHECK( fixture.Compile(
                   TestText( "textures/panel.cytex_c" ),
                   report ) == tool_status_t::VALIDATION_FAILED );
        CHECK( fixture.capture.lastDiagnostic ==
               CY_TEXTURE_DIAGNOSTIC_SCHEMA_FAILED );
        CHECK( fixture.capture.nLastDiagnosticLine == 2u );
        CHECK( fixture.capture.nLastDiagnosticColumn == 26u );
    }
}
