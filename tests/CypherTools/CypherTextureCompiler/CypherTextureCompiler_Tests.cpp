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
    bool_t bSawRecipeDependency{ CY_FALSE };
    bool_t bSawImageDependency{ CY_FALSE };
    bool_t bSawCompilerDependency{ CY_FALSE };
    bool_t bSawToolchainDependency{ CY_FALSE };
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
    CHECK( ToolCompiler_SupportsInput(
        *pCompiler,
        TestText( "textures/panel.cytex" ) ) );
    CHECK_FALSE( ToolCompiler_SupportsInput(
        *pCompiler,
        TestText( "materials/panel.cymat" ) ) );
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
    CHECK( fixture.capture.nDependencies == 4u );
    CHECK( fixture.capture.bSawRecipeDependency );
    CHECK( fixture.capture.bSawImageDependency );
    CHECK( fixture.capture.bSawCompilerDependency );
    CHECK( fixture.capture.bSawToolchainDependency );
    CHECK( fixture.capture.bSawCompletedProgress );

    blob_t cooked{};
    ReadBlob( project.OutputPath( "textures/panel.cytex_c" ), cooked );
    CHECK( ContentHash_Equals(
        fixture.capture.artifactHash,
        ContentHash_Data( Blob_Block( &cooked ) ) ) );
    cooked_texture_view_t texture{};
    REQUIRE( CookedTexture_Succeeded(
        CookedTexture_Read( Blob_Block( &cooked ), &texture ) ) );
    CHECK( texture.desc.nWidth == 2u );
    CHECK( texture.desc.nHeight == 2u );
    CHECK( texture.desc.nMipLevels == 2u );
    CHECK( texture.desc.pixelFormat ==
           render_texture_pixel_format_t::RGBA8_SRGB );
    CHECK( texture.desc.usage == render_texture_usage_t::COLOR );
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
    WriteRecipe(
        project,
        "textures/source/panel.png",
        "    usage = \"data\"\n    generate_mips = true" );

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
    CHECK( fixture.capture.nDependencies == 4u );
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
            "@cykv 1\n@schema \"cypher.texture\" 2\n"
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
