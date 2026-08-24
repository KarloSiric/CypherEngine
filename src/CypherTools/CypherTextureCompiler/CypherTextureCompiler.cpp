//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp
//  Purpose: Implements the reusable Cypher texture compiler module.
//  Details: VFS-backed CYKV and image inputs are decoded through tools-only
//           libraries, normalized to canonical RGBA storage, mipmapped on the
//           CPU, and packaged through the Common cooked-texture contract.
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
#include "CypherCommon_DataValidation.h"
#include "CypherCommon_Endian.h"
#include "CypherCommon_ImageCodec.h"
#include "CypherCommon_ImageView.h"
#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_MemoryOps.h"
#include "CypherCommon_RenderAsset.h"
#include "CypherCommon_StringFormat.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_TextBuffer.h"
#include "CypherCommon_ToolArtifactWriter.h"
#include "CypherCommon_ToolFramework.h"
#include "CypherCommon_Unicode.h"
#include "CypherCommon_Vfs.h"

#include <png.h>
#include <turbojpeg.h>

#include <cmath>
#include <cstring>

namespace cypher::tools
{

using namespace cypher::common;

namespace
{

inline constexpr usize CY_TEXTURE_COMPILER_MAX_PATH = 259u;
inline constexpr usize CY_TEXTURE_COMPILER_MAX_RECIPE_SIZE = 1u * CY_MIB;
inline constexpr usize CY_TEXTURE_COMPILER_MAX_IMAGE_SIZE = 512u * CY_MIB;
inline constexpr usize CY_TEXTURE_COMPILER_SCHEMA_DIAGNOSTICS = 32u;
inline constexpr u64 CY_TEXTURE_COMPILER_PROGRESS_STEPS = 5u;

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t TextureText(
    const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

struct key_value_document_owner_t {
    key_value_document_t *pDocument{ nullptr };

    key_value_document_owner_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( key_value_document_owner_t );

    ~key_value_document_owner_t() noexcept
    {
        if ( pDocument != nullptr ) {
            KeyValue_DestroyDocument( pDocument );
        }
    }
};

struct decoded_image_t {
    render_texture_pixel_format_t pixelFormat{
        render_texture_pixel_format_t::RGBA8_UNORM
    };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 cbRowPitch{ 0u };
    blob_t pixels{};
};

struct texture_compile_work_t {
    text_buffer_t recipeDiagnosticPath{};
    text_buffer_t sourceDiagnosticPath{};
    text_buffer_t outputNativePath{};
    text_buffer_t recipeText{};
    key_value_document_owner_t document{};
    render_texture_source_view_t recipe{};
    blob_t sourceBytes{};
    decoded_image_t image{};
    blob_t mipStorage[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    cooked_texture_mip_source_t mips[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    u32 nMipLevels{ 0u };
    blob_t cooked{};
};

CYPHER_NODISCARD bool_t InitTextBuffer( text_buffer_t &buffer ) noexcept
{
    return TextBuffer_Init( &buffer, Allocator_GetSystem() );
}

CYPHER_NODISCARD bool_t InitCompileWork(
    texture_compile_work_t &work ) noexcept
{
    if ( !InitTextBuffer( work.recipeDiagnosticPath ) ||
         !InitTextBuffer( work.sourceDiagnosticPath ) ||
         !InitTextBuffer( work.outputNativePath ) ||
         !InitTextBuffer( work.recipeText ) ||
         !Blob_Init( &work.sourceBytes, Allocator_GetSystem() ) ||
         !Blob_Init( &work.image.pixels, Allocator_GetSystem() ) ||
         !Blob_Init( &work.cooked, Allocator_GetSystem() ) ) {
        return CY_FALSE;
    }
    for ( blob_t &mip : work.mipStorage ) {
        if ( !Blob_Init( &mip, Allocator_GetSystem() ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t JoinNativePath(
    string_view_t root,
    string_view_t relativePath,
    text_buffer_t &pathOut ) noexcept
{
    const path_write_result_t measured = StringPath_Join(
        root,
        relativePath,
        path_style_t::NATIVE,
        nullptr,
        0u );
    if ( measured.cchRequired == 0u ||
         ( measured.status != path_status_t::OUTPUT_TRUNCATED &&
           measured.status != path_status_t::OK ) ||
         !TextBuffer_Resize( &pathOut, measured.cchRequired ) ) {
        return CY_FALSE;
    }
    const path_write_result_t written = StringPath_Join(
        root,
        relativePath,
        path_style_t::NATIVE,
        TextBuffer_Data( &pathOut ),
        TextBuffer_Capacity( &pathOut ) + 1u );
    return written.status == path_status_t::OK &&
           written.cchWritten == measured.cchRequired;
}

void ResolveDiagnosticPath(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    text_buffer_t &pathOut ) noexcept
{
    TextBuffer_Clear( &pathOut );
    if ( Vfs_ResolveDiagnosticPath(
             pVfs,
             virtualPath,
             &pathOut ) != vfs_status_t::OK ) {
        TextBuffer_Clear( &pathOut );
    }
}

void EmitDiagnostic(
    const tool_compile_request_t &request,
    tool_report_t &report,
    tool_diagnostic_code_t code,
    tool_diagnostic_severity_t severity,
    tool_diagnostic_category_t category,
    string_view_t message,
    string_view_t path = {},
    u32 nLine = 0u,
    u32 nColumn = 0u,
    string_view_t hint = {} ) noexcept
{
    tool_diagnostic_t diagnostic{};
    diagnostic.operationId = request.operationId;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.category = category;
    diagnostic.message = message;
    diagnostic.hint = hint;
    if ( path.cchLength != 0u ) {
        diagnostic.source.path = path;
        diagnostic.source.nLine = nLine == 0u ? 1u : nLine;
        diagnostic.source.nColumn = nColumn == 0u ? 1u : nColumn;
        diagnostic.flags |= TOOL_DIAGNOSTIC_FLAG_HAS_SOURCE;
    }
    if ( hint.cchLength != 0u ) {
        diagnostic.flags |= TOOL_DIAGNOSTIC_FLAG_HAS_HINT;
    }
    if ( severity == tool_diagnostic_severity_t::WARNING ) {
        ++report.nWarnings;
    } else if ( severity == tool_diagnostic_severity_t::ERROR ||
                severity == tool_diagnostic_severity_t::FATAL ) {
        ++report.nErrors;
    }
    ToolHost_EmitDiagnostic( request.pInvocation->pHost, diagnostic );
}

void EmitProgress(
    const tool_compile_request_t &request,
    tool_sequence_t sequence,
    tool_progress_state_t state,
    tool_status_t status,
    u64 nCompleted,
    string_view_t detail ) noexcept
{
    tool_progress_t progress{};
    progress.operationId = request.operationId;
    progress.sequence = sequence;
    progress.state = state;
    progress.unit = tool_progress_unit_t::STEPS;
    progress.status = status;
    progress.nCompleted = nCompleted;
    progress.nTotal = CY_TEXTURE_COMPILER_PROGRESS_STEPS;
    progress.timestamp = Cy_TimerNowTicks();
    progress.title = TextureText( "Compile texture" );
    progress.detail = detail;
    ToolHost_EmitProgress( request.pInvocation->pHost, progress );
}

void MarkFailed( tool_report_t &report ) noexcept
{
    report.nInputsProcessed = 1u;
    report.nFailed = 1u;
}

void EmitFailureProgress(
    const tool_compile_request_t &request,
    tool_sequence_t sequence,
    tool_status_t status,
    u64 nCompleted ) noexcept
{
    EmitProgress(
        request,
        sequence,
        tool_progress_state_t::FAILED,
        status,
        nCompleted,
        TextureText( "Failed" ) );
}

CYPHER_NODISCARD bool_t IsCancellationRequested(
    const tool_compile_request_t &request,
    tool_report_t &report,
    tool_sequence_t sequence,
    u64 nCompleted ) noexcept
{
    if ( !ToolHost_IsCancellationRequested( request.pInvocation->pHost ) ) {
        return CY_FALSE;
    }
    report.nSkipped = 1u;
    EmitProgress(
        request,
        sequence,
        tool_progress_state_t::CANCELLED,
        tool_status_t::CANCELLED,
        nCompleted,
        TextureText( "Cancelled" ) );
    return CY_TRUE;
}

CYPHER_NODISCARD tool_status_t Fail(
    const tool_compile_request_t &request,
    tool_report_t &report,
    tool_sequence_t sequence,
    u64 nCompleted,
    tool_status_t status,
    tool_diagnostic_code_t code,
    tool_diagnostic_category_t category,
    string_view_t message,
    string_view_t path = {},
    string_view_t hint = {} ) noexcept
{
    EmitDiagnostic(
        request,
        report,
        code,
        tool_diagnostic_severity_t::ERROR,
        category,
        message,
        path,
        1u,
        1u,
        hint );
    MarkFailed( report );
    EmitFailureProgress( request, sequence, status, nCompleted );
    return status;
}

CYPHER_NODISCARD bool_t ReadTextFile(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    usize cbMaximum,
    text_buffer_t &textOut,
    vfs_status_t &vfsStatusOut ) noexcept
{
    blob_t bytes{};
    if ( !Blob_Init( &bytes, Allocator_GetSystem() ) ) {
        vfsStatusOut = vfs_status_t::OUT_OF_MEMORY;
        return CY_FALSE;
    }
    vfsStatusOut = Vfs_ReadAll( pVfs, virtualPath, cbMaximum, &bytes );
    if ( vfsStatusOut != vfs_status_t::OK || bytes.cbSize == 0u ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < bytes.cbSize; ++iByte ) {
        if ( bytes.pData[iByte] == static_cast<byte>( '\0' ) ) {
            return CY_FALSE;
        }
    }
    const string_view_t text{
        reinterpret_cast<const char *>( bytes.pData ),
        bytes.cbSize
    };
    return Unicode_ValidateUtf8( text ).status == unicode_status_t::OK &&
           TextBuffer_Assign( &textOut, text );
}

CYPHER_NODISCARD bool_t DecodeImage(
    string_view_t path,
    binary_block_t source,
    decoded_image_t &image ) noexcept
{
    image_decode_options_t options{};
    options.formatHint = ImageCodec_FormatFromPath( path );
    options.nMaximumDimension = CY_COOKED_TEXTURE_MAX_DIMENSION;
    options.cbMaximumDecodedSize = CY_TEXTURE_COMPILER_MAX_IMAGE_SIZE;

    image_surface_t decoded{};
    const image_decode_result_t result = ImageCodec_Decode(
        source,
        Allocator_GetSystem(),
        options,
        &decoded );
    if ( result.status != image_codec_status_t::OK ) {
        return CY_FALSE;
    }

    const image_format_info_t *pFormatInfo = ImageFormat_GetInfo(
        decoded.desc.pixelFormat );
    if ( pFormatInfo == nullptr ||
         ( decoded.desc.pixelFormat != image_pixel_format_t::RGBA8_UNORM &&
           decoded.desc.pixelFormat != image_pixel_format_t::RGBA32_FLOAT ) ) {
        return CY_FALSE;
    }

    image.nWidth = decoded.desc.extent.nWidth;
    image.nHeight = decoded.desc.extent.nHeight;
    image.cbRowPitch = image.nWidth * pFormatInfo->cbPixel;
    const usize cbImage =
        static_cast<usize>( image.cbRowPitch ) * image.nHeight;
    if ( !Blob_Resize( &image.pixels, cbImage ) ) {
        return CY_FALSE;
    }

    const const_image_view_t decodedView = ImageSurface_GetView(
        static_cast<const image_surface_t *>( &decoded ) );
    if ( decoded.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM ) {
        for ( u32 iRow = 0u; iRow < image.nHeight; ++iRow ) {
            const binary_block_t row = ImageView_GetRow(
                decodedView, iRow, 0u );
            Cy_MemCopy(
                image.pixels.pData +
                    static_cast<usize>( iRow ) * image.cbRowPitch,
                row.pData,
                image.cbRowPitch );
        }
        return CY_TRUE;
    }

    // Cooked FLOAT32 payloads are little-endian even though decoded authoring
    // surfaces deliberately use native in-memory floating-point representation.
    image.pixelFormat = render_texture_pixel_format_t::RGBA32_FLOAT;
    const usize nComponents = cbImage / sizeof( f32 );
    const auto *pComponents = static_cast<const f32 *>(
        decoded.allocation.pData );
    for ( usize iComponent = 0u; iComponent < nComponents; ++iComponent ) {
        const f32 little = Cy_HostToLittleF32( pComponents[iComponent] );
        Cy_MemCopy(
            image.pixels.pData + iComponent * sizeof( f32 ),
            &little,
            sizeof( little ) );
    }
    return CY_TRUE;
}

CYPHER_NODISCARD f32 SrgbToLinear( f32 value ) noexcept
{
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow( ( value + 0.055f ) / 1.055f, 2.4f );
}

CYPHER_NODISCARD f32 LinearToSrgb( f32 value ) noexcept
{
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow( value, 1.0f / 2.4f ) - 0.055f;
}

CYPHER_NODISCARD byte QuantizeUnorm8( f32 value ) noexcept
{
    if ( value < 0.0f ) {
        value = 0.0f;
    } else if ( value > 1.0f ) {
        value = 1.0f;
    }
    return static_cast<byte>( value * 255.0f + 0.5f );
}

CYPHER_NODISCARD bool_t GenerateNextMip8(
    const cooked_texture_mip_source_t &source,
    bool_t bSrgb,
    bool_t bNormal,
    blob_t &dest,
    cooked_texture_mip_source_t &mipOut ) noexcept
{
    const u32 nWidth = source.nWidth > 1u ? source.nWidth / 2u : 1u;
    const u32 nHeight = source.nHeight > 1u ? source.nHeight / 2u : 1u;
    const usize cbDest = static_cast<usize>( nWidth ) * nHeight * 4u;
    if ( !Blob_Resize( &dest, cbDest ) ) {
        return CY_FALSE;
    }

    for ( u32 y = 0u; y < nHeight; ++y ) {
        for ( u32 x = 0u; x < nWidth; ++x ) {
            f32 sums[4]{};
            u32 nSamples = 0u;
            const u32 syBegin = static_cast<u32>(
                static_cast<u64>( y ) * source.nHeight / nHeight );
            const u32 syEnd = static_cast<u32>(
                static_cast<u64>( y + 1u ) * source.nHeight / nHeight );
            const u32 sxBegin = static_cast<u32>(
                static_cast<u64>( x ) * source.nWidth / nWidth );
            const u32 sxEnd = static_cast<u32>(
                static_cast<u64>( x + 1u ) * source.nWidth / nWidth );
            for ( u32 sy = syBegin; sy < syEnd; ++sy ) {
                for ( u32 sx = sxBegin; sx < sxEnd; ++sx ) {
                    const byte *pPixel = source.pixels.pData +
                        static_cast<usize>( sy ) * source.cbRowPitch + sx * 4u;
                    for ( u32 iChannel = 0u; iChannel < 4u; ++iChannel ) {
                        f32 value = static_cast<f32>( pPixel[iChannel] ) / 255.0f;
                        if ( bSrgb && iChannel < 3u ) {
                            value = SrgbToLinear( value );
                        }
                        if ( bNormal && iChannel < 3u ) {
                            value = value * 2.0f - 1.0f;
                        }
                        sums[iChannel] += value;
                    }
                    ++nSamples;
                }
            }

            byte *pDest = dest.pData +
                ( static_cast<usize>( y ) * nWidth + x ) * 4u;
            if ( bNormal ) {
                f32 nx = sums[0] / nSamples;
                f32 ny = sums[1] / nSamples;
                f32 nz = sums[2] / nSamples;
                const f32 nLength = std::sqrt( nx * nx + ny * ny + nz * nz );
                if ( nLength > 0.000001f ) {
                    nx /= nLength;
                    ny /= nLength;
                    nz /= nLength;
                } else {
                    nx = 0.0f;
                    ny = 0.0f;
                    nz = 1.0f;
                }
                pDest[0] = QuantizeUnorm8( nx * 0.5f + 0.5f );
                pDest[1] = QuantizeUnorm8( ny * 0.5f + 0.5f );
                pDest[2] = QuantizeUnorm8( nz * 0.5f + 0.5f );
            } else {
                for ( u32 iChannel = 0u; iChannel < 3u; ++iChannel ) {
                    f32 value = sums[iChannel] / nSamples;
                    if ( bSrgb ) {
                        value = LinearToSrgb( value );
                    }
                    pDest[iChannel] = QuantizeUnorm8( value );
                }
            }
            pDest[3] = QuantizeUnorm8( sums[3] / nSamples );
        }
    }
    mipOut = {
        nWidth,
        nHeight,
        1u,
        nWidth * 4u,
        Blob_Block( &dest )
    };
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t GenerateNextMipFloat(
    const cooked_texture_mip_source_t &source,
    blob_t &dest,
    cooked_texture_mip_source_t &mipOut ) noexcept
{
    const u32 nWidth = source.nWidth > 1u ? source.nWidth / 2u : 1u;
    const u32 nHeight = source.nHeight > 1u ? source.nHeight / 2u : 1u;
    const usize cbDest = static_cast<usize>( nWidth ) * nHeight * 16u;
    if ( !Blob_Resize( &dest, cbDest ) ) {
        return CY_FALSE;
    }

    for ( u32 y = 0u; y < nHeight; ++y ) {
        for ( u32 x = 0u; x < nWidth; ++x ) {
            f32 sums[4]{};
            u32 nSamples = 0u;
            const u32 syBegin = static_cast<u32>(
                static_cast<u64>( y ) * source.nHeight / nHeight );
            const u32 syEnd = static_cast<u32>(
                static_cast<u64>( y + 1u ) * source.nHeight / nHeight );
            const u32 sxBegin = static_cast<u32>(
                static_cast<u64>( x ) * source.nWidth / nWidth );
            const u32 sxEnd = static_cast<u32>(
                static_cast<u64>( x + 1u ) * source.nWidth / nWidth );
            for ( u32 sy = syBegin; sy < syEnd; ++sy ) {
                for ( u32 sx = sxBegin; sx < sxEnd; ++sx ) {
                    const byte *pPixel = source.pixels.pData +
                        static_cast<usize>( sy ) * source.cbRowPitch + sx * 16u;
                    for ( u32 iChannel = 0u; iChannel < 4u; ++iChannel ) {
                        f32 value = 0.0f;
                        Cy_MemCopy(
                            &value,
                            pPixel + iChannel * sizeof( f32 ),
                            sizeof( value ) );
                        sums[iChannel] += Cy_LittleToHostF32( value );
                    }
                    ++nSamples;
                }
            }
            byte *pDest = dest.pData +
                ( static_cast<usize>( y ) * nWidth + x ) * 16u;
            for ( u32 iChannel = 0u; iChannel < 4u; ++iChannel ) {
                const f32 value = Cy_HostToLittleF32(
                    sums[iChannel] / nSamples );
                Cy_MemCopy(
                    pDest + iChannel * sizeof( f32 ),
                    &value,
                    sizeof( value ) );
            }
        }
    }
    mipOut = {
        nWidth,
        nHeight,
        1u,
        nWidth * 16u,
        Blob_Block( &dest )
    };
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t BuildMipChain(
    texture_compile_work_t &work ) noexcept
{
    work.nMipLevels = work.recipe.bGenerateMips
        ? CookedTexture_FullMipCount( work.image.nWidth, work.image.nHeight )
        : 1u;
    if ( work.nMipLevels == 0u ||
         work.nMipLevels > CY_COOKED_TEXTURE_MAX_MIP_LEVELS ) {
        return CY_FALSE;
    }
    work.mips[0] = {
        work.image.nWidth,
        work.image.nHeight,
        1u,
        work.image.cbRowPitch,
        Blob_Block( &work.image.pixels )
    };
    const bool_t bSrgb =
        work.recipe.colorSpace == render_texture_color_space_t::SRGB;
    const bool_t bNormal =
        work.recipe.usage == render_texture_usage_t::NORMAL;
    for ( usize iMip = 1u; iMip < work.nMipLevels; ++iMip ) {
        const bool_t bGenerated =
            work.image.pixelFormat == render_texture_pixel_format_t::RGBA32_FLOAT
                ? GenerateNextMipFloat(
                      work.mips[iMip - 1u],
                      work.mipStorage[iMip],
                      work.mips[iMip] )
                : GenerateNextMip8(
                      work.mips[iMip - 1u],
                      bSrgb,
                      bNormal,
                      work.mipStorage[iMip],
                      work.mips[iMip] );
        if ( !bGenerated ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD content_hash_t CompilerHash() noexcept
{
    const u32 identity[]{
        CY_TEXTURE_COMPILER_API_VERSION,
        CY_TEXTURE_COMPILER_VERSION,
        CY_RENDER_TEXTURE_RESOURCE_VERSION
    };
    return ContentHash_Data( BinaryBlock_FromData( identity, sizeof( identity ) ) );
}

CYPHER_NODISCARD content_hash_t ToolchainHash() noexcept
{
    return ContentHash_String( TextureText(
        "libpng:" PNG_LIBPNG_VER_STRING
        ";libjpeg-turbo:" CYPHER_STRINGIFY( TURBOJPEG_VERSION_NUMBER )
        ";tinyexr:vcpkg" ) );
}

void EmitDependencies(
    const tool_compile_request_t &request,
    const texture_compile_work_t &work,
    content_hash_t recipeHash,
    content_hash_t imageHash,
    content_hash_t compilerHash,
    content_hash_t toolchainHash ) noexcept
{
    const tool_dependency_t dependencies[]{
        { request.input, tool_dependency_kind_t::SOURCE, recipeHash,
          TOOL_DEPENDENCY_FLAG_REQUIRED },
        { work.recipe.source, tool_dependency_kind_t::SOURCE, imageHash,
          TOOL_DEPENDENCY_FLAG_REQUIRED },
        { TextureText( "toolchain/cypher-texture-compiler" ),
          tool_dependency_kind_t::TOOLCHAIN, compilerHash,
          TOOL_DEPENDENCY_FLAG_REQUIRED },
        { TextureText( "toolchain/image-import" ),
          tool_dependency_kind_t::TOOLCHAIN, toolchainHash,
          TOOL_DEPENDENCY_FLAG_REQUIRED }
    };
    for ( const tool_dependency_t &dependency : dependencies ) {
        ToolHost_EmitDependency( request.pInvocation->pHost, dependency );
    }
}

CYPHER_NODISCARD bool_t ProbeTexture(
    string_view_t input,
    void * ) noexcept
{
    return StringPath_HasExtension(
        input,
        TextureText( ".cytex" ),
        CY_TRUE );
}

CYPHER_NODISCARD tool_status_t ExecuteTextureCompiler(
    const tool_compile_request_t &request,
    tool_report_t *pReport,
    void * ) noexcept
{
    if ( pReport == nullptr || request.pInvocation == nullptr ||
         request.pInvocation->pContext == nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    tool_report_t &report = *pReport;
    report.nInputsDiscovered = 1u;
    tool_sequence_t sequence = 1u;
    u64 nCompleted = 0u;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::BEGIN,
        tool_status_t::OK,
        nCompleted,
        TextureText( "Read recipe" ) );

    const bool_t bDryRun =
        ( request.pInvocation->flags & TOOL_INVOCATION_FLAG_DRY_RUN ) != 0u;
    const bool_t bInputPathValid = DataValidation_Succeeded(
        DataValidation_CheckResourcePath(
            request.input,
            TextureText( ".cytex" ),
            CY_TEXTURE_COMPILER_MAX_PATH ) );
    const bool_t bOutputPathValid = bDryRun && request.output.cchLength == 0u
        ? CY_TRUE
        : DataValidation_Succeeded(
              DataValidation_CheckResourcePath(
                  request.output,
                  TextureText( ".cytex_c" ),
                  CY_TEXTURE_COMPILER_MAX_PATH ) );
    if ( !bInputPathValid || !bOutputPathValid ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INVALID_ARGUMENT,
            CY_TEXTURE_DIAGNOSTIC_INVALID_PATH,
            tool_diagnostic_category_t::VALIDATION,
            TextureText(
                "Texture input and output must be canonical virtual resource paths." ),
            !bInputPathValid ? request.input : request.output );
    }

    texture_compile_work_t work{};
    if ( !InitCompileWork( work ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_TEXTURE_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            TextureText( "Out of memory while initializing texture compilation." ) );
    }
    const tool_context_t &context = *request.pInvocation->pContext;
    if ( !Vfs_IsValid( context.pSourceVfs ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INVALID_CONFIGURATION,
            CY_TEXTURE_DIAGNOSTIC_INVALID_PATH,
            tool_diagnostic_category_t::FILESYSTEM,
            TextureText( "Texture compilation requires a valid source VFS." ),
            request.input );
    }
    if ( !bDryRun &&
         !JoinNativePath(
             context.outputRoot,
             request.output,
             work.outputNativePath ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INVALID_CONFIGURATION,
            CY_TEXTURE_DIAGNOSTIC_INVALID_PATH,
            tool_diagnostic_category_t::FILESYSTEM,
            TextureText( "Texture output could not be resolved below its root." ),
            request.output );
    }
    if ( IsCancellationRequested( request, report, sequence, nCompleted ) ) {
        return tool_status_t::CANCELLED;
    }

    ResolveDiagnosticPath(
        context.pSourceVfs,
        request.input,
        work.recipeDiagnosticPath );
    vfs_status_t vfsStatus = vfs_status_t::OK;
    if ( !ReadTextFile(
             context.pSourceVfs,
             request.input,
             CY_TEXTURE_COMPILER_MAX_RECIPE_SIZE,
             work.recipeText,
             vfsStatus ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            vfsStatus == vfs_status_t::OUT_OF_MEMORY
                ? tool_status_t::OUT_OF_MEMORY
                : tool_status_t::IO_ERROR,
            CY_TEXTURE_DIAGNOSTIC_READ_FAILED,
            tool_diagnostic_category_t::FILESYSTEM,
            TextureText( "Texture recipe could not be read as bounded UTF-8 text." ),
            request.input );
    }
    report.cbRead += work.recipeText.cchLength;

    key_value_document_desc_t documentDesc{};
    documentDesc.pAllocator = Allocator_GetSystem();
    work.document.pDocument = KeyValue_CreateDocument( documentDesc );
    if ( work.document.pDocument == nullptr ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_TEXTURE_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            TextureText( "Out of memory while creating the CYKV document." ),
            request.input );
    }
    const key_value_parse_result_t parsed = KeyValue_ParseText(
        TextBuffer_View( &work.recipeText ),
        {},
        work.document.pDocument );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        EmitDiagnostic(
            request,
            report,
            CY_TEXTURE_DIAGNOSTIC_CYKV_PARSE_FAILED,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::SOURCE,
            StringView_FromCString( KeyValue_ParseStatusName( parsed.status ) ),
            request.input,
            parsed.errorLocation.nLine,
            parsed.errorLocation.nColumn );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::VALIDATION_FAILED,
            nCompleted );
        return tool_status_t::VALIDATION_FAILED;
    }

    schema_diagnostic_t diagnostics[CY_TEXTURE_COMPILER_SCHEMA_DIAGNOSTICS]{};
    const render_asset_decode_result_t decoded = RenderTextureSource_Decode(
        work.document.pDocument,
        {},
        diagnostics,
        CYPHER_ARRAY_COUNT( diagnostics ),
        &work.recipe );
    if ( !RenderAsset_DecodeSucceeded( decoded ) ) {
        if ( decoded.validation.nDiagnosticsWritten != 0u ) {
            const schema_diagnostic_t &diagnostic = diagnostics[0];
            text_location_t location{};
            if ( diagnostic.code ==
                 schema_diagnostic_code_t::LANGUAGE_VERSION_MISMATCH ) {
                location = parsed.languageVersionLocation;
            } else if ( diagnostic.code ==
                        schema_diagnostic_code_t::SCHEMA_ID_MISMATCH ) {
                location = parsed.schemaIdLocation;
            } else if ( diagnostic.code ==
                        schema_diagnostic_code_t::SCHEMA_VERSION_MISMATCH ) {
                location = parsed.schemaVersionLocation;
            }
            char message[512]{};
            const string_format_result_t formatted = StringFormat_Printf(
                message,
                sizeof( message ),
                "%s at %s",
                Schema_DiagnosticCodeName( diagnostic.code ),
                diagnostic.path[0] != '\0' ? diagnostic.path : "$" );
            EmitDiagnostic(
                request,
                report,
                CY_TEXTURE_DIAGNOSTIC_SCHEMA_FAILED,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::SCHEMA,
                formatted.status == string_format_status_t::OK
                    ? StringView_FromCString( message )
                    : TextureText( "Texture schema validation failed." ),
                request.input,
                location.nLine,
                location.nColumn );
            MarkFailed( report );
            EmitFailureProgress(
                request,
                sequence,
                tool_status_t::VALIDATION_FAILED,
                nCompleted );
            return tool_status_t::VALIDATION_FAILED;
        }
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_TEXTURE_DIAGNOSTIC_SCHEMA_FAILED,
            tool_diagnostic_category_t::SCHEMA,
            StringView_FromCString(
                RenderAsset_DecodeStatusName( decoded.status ) ),
            request.input );
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        TextureText( "Load source image" ) );
    if ( IsCancellationRequested( request, report, sequence, nCompleted ) ) {
        return tool_status_t::CANCELLED;
    }
    ResolveDiagnosticPath(
        context.pSourceVfs,
        work.recipe.source,
        work.sourceDiagnosticPath );
    vfsStatus = Vfs_ReadAll(
        context.pSourceVfs,
        work.recipe.source,
        CY_TEXTURE_COMPILER_MAX_IMAGE_SIZE,
        &work.sourceBytes );
    if ( vfsStatus != vfs_status_t::OK || work.sourceBytes.cbSize == 0u ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            vfsStatus == vfs_status_t::OUT_OF_MEMORY
                ? tool_status_t::OUT_OF_MEMORY
                : tool_status_t::IO_ERROR,
            CY_TEXTURE_DIAGNOSTIC_IMAGE_READ_FAILED,
            tool_diagnostic_category_t::FILESYSTEM,
            TextureText( "Texture source image could not be read through the source VFS." ),
            work.recipe.source );
    }
    report.cbRead += work.sourceBytes.cbSize;

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        TextureText( "Decode and generate mips" ) );
    if ( !DecodeImage(
             work.recipe.source,
             Blob_Block( &work.sourceBytes ),
             work.image ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_TEXTURE_DIAGNOSTIC_IMAGE_DECODE_FAILED,
            tool_diagnostic_category_t::COMPILER,
            TextureText( "Texture source image is malformed or unsupported." ),
            work.recipe.source,
            TextureText( "Version 1 accepts PNG, JPEG, TGA, and finite RGBA EXR images." ) );
    }
    if ( !BuildMipChain( work ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_TEXTURE_DIAGNOSTIC_MIP_GENERATION_FAILED,
            tool_diagnostic_category_t::COMPILER,
            TextureText( "Texture mip chain could not be generated." ),
            work.recipe.source );
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        TextureText( "Build cooked resource" ) );
    const content_hash_t recipeHash = ContentHash_String(
        TextBuffer_View( &work.recipeText ) );
    const content_hash_t imageHash = ContentHash_Data(
        Blob_Block( &work.sourceBytes ) );
    const content_hash_t compilerHash = CompilerHash();
    const content_hash_t toolchainHash = ToolchainHash();
    content_hash_t sourceHash = ContentHash_Combine( compilerHash, recipeHash );
    sourceHash = ContentHash_Combine( sourceHash, imageHash );
    sourceHash = ContentHash_Combine( sourceHash, toolchainHash );
    if ( !ContentHash_IsValid( sourceHash ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INTERNAL_ERROR,
            CY_TEXTURE_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            TextureText( "Texture source identity could not be constructed." ) );
    }

    cooked_texture_desc_t texture{};
    texture.pixelFormat = work.image.pixelFormat;
    if ( work.image.pixelFormat !=
         render_texture_pixel_format_t::RGBA32_FLOAT ) {
        texture.pixelFormat =
            work.recipe.colorSpace == render_texture_color_space_t::SRGB
                ? render_texture_pixel_format_t::RGBA8_SRGB
                : render_texture_pixel_format_t::RGBA8_UNORM;
    }
    texture.usage = work.recipe.usage;
    texture.colorSpace = work.recipe.colorSpace;
    texture.flags = work.recipe.bGenerateMips && work.nMipLevels > 1u
        ? COOKED_TEXTURE_FLAG_GENERATED_MIPS
        : COOKED_TEXTURE_FLAG_NONE;
    texture.nWidth = work.image.nWidth;
    texture.nHeight = work.image.nHeight;
    texture.nMipLevels = work.nMipLevels;
    const span_t<const cooked_texture_mip_source_t> mipSpan{
        work.mips,
        work.nMipLevels
    };
    const usize cbCooked = CookedTexture_RequiredSize( texture, mipSpan );
    if ( cbCooked == 0u || !Blob_Resize( &work.cooked, cbCooked ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            cbCooked == 0u
                ? tool_status_t::INTERNAL_ERROR
                : tool_status_t::OUT_OF_MEMORY,
            CY_TEXTURE_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            TextureText( "Cooked texture size is invalid or could not be allocated." ) );
    }
    const cooked_texture_result_t cooked = CookedTexture_Write(
        texture,
        mipSpan,
        sourceHash,
        Blob_WritableSpan( &work.cooked ) );
    if ( !CookedTexture_Succeeded( cooked ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INTERNAL_ERROR,
            CY_TEXTURE_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            StringView_FromCString(
                CookedTexture_StatusName( cooked.status ) ) );
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        bDryRun ? TextureText( "Validate output" )
                : TextureText( "Write output" ) );
    const tool_status_t writeStatus = bDryRun
        ? tool_status_t::OK
        : ToolArtifactWriter_WriteNative(
              TextBuffer_View( &work.outputNativePath ),
              Blob_Block( &work.cooked ) );
    if ( ToolStatus_Failed( writeStatus ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            writeStatus,
            CY_TEXTURE_DIAGNOSTIC_WRITE_FAILED,
            tool_diagnostic_category_t::FILESYSTEM,
            TextureText( "Cooked texture output could not be written." ),
            request.output );
    }

    EmitDependencies(
        request,
        work,
        recipeHash,
        imageHash,
        compilerHash,
        toolchainHash );
    if ( !bDryRun ) {
        const tool_artifact_t artifact{
            request.output,
            TextureText( "application/x-cypher-texture" ),
            tool_artifact_kind_t::COOKED_RESOURCE,
            ContentHash_Data( Blob_Block( &work.cooked ) ),
            work.cooked.cbSize,
            TOOL_ARTIFACT_FLAG_PRIMARY | TOOL_ARTIFACT_FLAG_GENERATED
        };
        ToolHost_EmitArtifact( request.pInvocation->pHost, artifact );
        report.nArtifacts = 1u;
        report.cbWritten = work.cooked.cbSize;
    }
    report.nInputsProcessed = 1u;
    report.nSucceeded = 1u;
    ++nCompleted;
    EmitProgress(
        request,
        sequence,
        tool_progress_state_t::COMPLETE,
        tool_status_t::OK,
        nCompleted,
        bDryRun ? TextureText( "Validated" ) : TextureText( "Compiled" ) );
    return tool_status_t::OK;
}

inline constexpr string_view_t g_textureSourceExtensions[]{
    TextureText( ".cytex" )
};

const tool_compiler_desc_t g_textureCompiler{
    TextureText( "cypher.texture" ),
    TextureText( "Cypher Texture Compiler" ),
    TextureText( "texture" ),
    TextureText( ".cytex_c" ),
    g_textureSourceExtensions,
    CYPHER_ARRAY_COUNT( g_textureSourceExtensions ),
    CY_TEXTURE_COMPILER_API_VERSION,
    CY_TEXTURE_COMPILER_VERSION,
    TOOL_COMPILER_FLAG_DETERMINISTIC |
        TOOL_COMPILER_FLAG_THREAD_SAFE |
        TOOL_COMPILER_FLAG_SUPPORTS_VALIDATE |
        TOOL_COMPILER_FLAG_SUPPORTS_DRY_RUN,
    &ProbeTexture,
    &ExecuteTextureCompiler,
    nullptr
};

} // namespace

const tool_compiler_desc_t *CypherTextureCompiler_Descriptor() noexcept
{
    return &g_textureCompiler;
}

} // namespace cypher::tools
