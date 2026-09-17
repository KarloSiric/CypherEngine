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
#include "CypherCommon_KeyValueWriter.h"
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

#ifndef CYPHER_VCPKG_BASELINE
    #define CYPHER_VCPKG_BASELINE "untracked"
#endif

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

struct texture_recipe_t {
    u32 nSchemaVersion{ CY_RENDER_ASSET_SCHEMA_VERSION_V1 };
    string_view_t source{};
    render_texture_usage_t usage{ render_texture_usage_t::COLOR };
    render_texture_color_space_t colorSpace{
        render_texture_color_space_t::SRGB
    };
    cooked_texture_alpha_mode_t alphaMode{
        cooked_texture_alpha_mode_t::STRAIGHT
    };
    f32 alphaCutoff{ 0.5f };
    render_texture_mip_mode_t mipMode{
        render_texture_mip_mode_t::GENERATE
    };
    render_texture_mip_filter_t mipFilter{
        render_texture_mip_filter_t::BOX
    };
    render_texture_edge_mode_t edgeMode{
        render_texture_edge_mode_t::CLAMP
    };
    render_texture_output_format_t outputFormat{
        render_texture_output_format_t::AUTO
    };
    render_texture_output_quality_t outputQuality{
        render_texture_output_quality_t::BALANCED
    };
    string_view_t streamingClass{};
    u32 nResidentMipLevels{ 0u };
    u32 nStreamingPriority{ 0u };
    bool_t bPreserveAlphaCoverage{ CY_FALSE };
    bool_t bDilateRgb{ CY_FALSE };
    bool_t bStreaming{ CY_FALSE };
};

enum class texture_policy_status_t : u8 {
    OK = 0u,
    UNSUPPORTED_MIP_FILTER,
    UNSUPPORTED_EDGE_MODE,
    UNSUPPORTED_ALPHA_COVERAGE,
    UNSUPPORTED_ALPHA_DILATION,
    UNSUPPORTED_PRESERVED_IMPORT
};

CYPHER_NODISCARD cooked_texture_alpha_mode_t ConvertAlphaMode(
    render_texture_alpha_mode_t mode ) noexcept
{
    switch ( mode ) {
        case render_texture_alpha_mode_t::NONE:
            return cooked_texture_alpha_mode_t::NONE;
        case render_texture_alpha_mode_t::STRAIGHT:
            return cooked_texture_alpha_mode_t::STRAIGHT;
        case render_texture_alpha_mode_t::PREMULTIPLIED:
            return cooked_texture_alpha_mode_t::PREMULTIPLIED;
        case render_texture_alpha_mode_t::MASK:
            return cooked_texture_alpha_mode_t::MASK;
        case render_texture_alpha_mode_t::DATA:
            return cooked_texture_alpha_mode_t::DATA;
    }
    return cooked_texture_alpha_mode_t::NONE;
}

CYPHER_NODISCARD u32 StreamingPriority(
    string_view_t resourceClass ) noexcept
{
    // These stable values are serialized scheduling hints. Known classes leave
    // deliberate gaps for future policy tiers. Unknown valid identifiers use
    // neutral world priority; changing this table requires a compiler bump.
    if ( StringView_Equals( resourceClass, TextureText( "critical" ) ) ) {
        return 255u;
    }
    if ( StringView_Equals( resourceClass, TextureText( "ui" ) ) ) {
        return 224u;
    }
    if ( StringView_Equals( resourceClass, TextureText( "character" ) ) ) {
        return 192u;
    }
    if ( StringView_Equals( resourceClass, TextureText( "world" ) ) ) {
        return 128u;
    }
    if ( StringView_Equals( resourceClass, TextureText( "effects" ) ) ) {
        return 96u;
    }
    if ( StringView_Equals( resourceClass, TextureText( "background" ) ) ) {
        return 32u;
    }
    return 128u;
}

void NormalizeRecipe(
    const render_texture_source_view_t &source,
    texture_recipe_t &recipeOut ) noexcept
{
    texture_recipe_t recipe{};
    recipe.nSchemaVersion = CY_RENDER_ASSET_SCHEMA_VERSION_V1;
    recipe.source = source.source;
    recipe.usage = source.usage;
    recipe.colorSpace = source.colorSpace;
    recipe.alphaMode = source.usage == render_texture_usage_t::COLOR
        ? cooked_texture_alpha_mode_t::STRAIGHT
        : source.usage == render_texture_usage_t::DATA
            ? cooked_texture_alpha_mode_t::DATA
            : cooked_texture_alpha_mode_t::NONE;
    recipe.mipMode = source.bGenerateMips
        ? render_texture_mip_mode_t::GENERATE
        : render_texture_mip_mode_t::NONE;
    recipeOut = recipe;
}

void NormalizeRecipe(
    const render_texture_source_v2_view_t &source,
    texture_recipe_t &recipeOut ) noexcept
{
    texture_recipe_t recipe{};
    recipe.nSchemaVersion = CY_RENDER_ASSET_SCHEMA_VERSION_V2;
    recipe.source = source.source;
    recipe.usage = source.usage;
    recipe.colorSpace = source.colorSpace;
    recipe.alphaMode = ConvertAlphaMode( source.alpha.mode );
    recipe.alphaCutoff = static_cast<f32>( source.alpha.cutoff );
    recipe.mipMode = source.mips.mode;
    recipe.mipFilter = source.mips.filter;
    recipe.edgeMode = source.mips.edge;
    recipe.outputFormat = source.output.format;
    recipe.outputQuality = source.output.quality;
    recipe.streamingClass = source.streaming.resourceClass;
    recipe.nResidentMipLevels = static_cast<u32>(
        source.streaming.nResidentMipCount );
    recipe.nStreamingPriority = source.streaming.bEnabled
        ? StreamingPriority( source.streaming.resourceClass )
        : 0u;
    recipe.bPreserveAlphaCoverage = source.mips.bPreserveAlphaCoverage;
    recipe.bDilateRgb = source.alpha.bDilateRgb;
    recipe.bStreaming = source.streaming.bEnabled;
    recipeOut = recipe;
}

CYPHER_NODISCARD texture_policy_status_t ValidateTexturePolicy(
    const texture_recipe_t &recipe ) noexcept
{
    if ( StringPath_HasExtension(
             recipe.source,
             TextureText( ".dds" ),
             CY_TRUE ) ||
         StringPath_HasExtension(
             recipe.source,
             TextureText( ".ktx2" ),
             CY_TRUE ) ||
         recipe.mipMode == render_texture_mip_mode_t::PRESERVE ) {
        return texture_policy_status_t::UNSUPPORTED_PRESERVED_IMPORT;
    }
    if ( recipe.mipFilter != render_texture_mip_filter_t::BOX ) {
        return texture_policy_status_t::UNSUPPORTED_MIP_FILTER;
    }
    if ( recipe.edgeMode != render_texture_edge_mode_t::CLAMP ) {
        return texture_policy_status_t::UNSUPPORTED_EDGE_MODE;
    }
    if ( recipe.bPreserveAlphaCoverage ) {
        return texture_policy_status_t::UNSUPPORTED_ALPHA_COVERAGE;
    }
    if ( recipe.bDilateRgb ) {
        return texture_policy_status_t::UNSUPPORTED_ALPHA_DILATION;
    }
    return texture_policy_status_t::OK;
}

CYPHER_NODISCARD string_view_t PolicyDiagnosticMessage(
    texture_policy_status_t status ) noexcept
{
    switch ( status ) {
        case texture_policy_status_t::UNSUPPORTED_MIP_FILTER:
            return TextureText(
                "Texture compiler currently supports only the box mip filter." );
        case texture_policy_status_t::UNSUPPORTED_EDGE_MODE:
            return TextureText(
                "Texture compiler currently supports only clamped mip edges." );
        case texture_policy_status_t::UNSUPPORTED_ALPHA_COVERAGE:
            return TextureText(
                "Alpha-coverage-preserving mip generation is not implemented." );
        case texture_policy_status_t::UNSUPPORTED_ALPHA_DILATION:
            return TextureText(
                "Transparent-pixel RGB dilation is not implemented." );
        case texture_policy_status_t::UNSUPPORTED_PRESERVED_IMPORT:
            return TextureText(
                "DDS/KTX2 container and preserved-mip import is not implemented." );
        case texture_policy_status_t::OK:
            break;
    }
    return TextureText( "Texture policy is unsupported." );
}

CYPHER_NODISCARD cooked_texture_target_t CookedTarget(
    tool_target_t target ) noexcept
{
    switch ( target.platform ) {
        case tool_platform_t::WINDOWS:
        case tool_platform_t::LINUX:
            return cooked_texture_target_t::DESKTOP;
        case tool_platform_t::MACOS:
            return cooked_texture_target_t::APPLE;
        case tool_platform_t::UNKNOWN:
            break;
    }
    return cooked_texture_target_t::PORTABLE;
}

CYPHER_NODISCARD render_format_t SelectStorageFormat(
    const texture_recipe_t &recipe,
    render_texture_pixel_format_t pixelFormat ) noexcept
{
    // AUTO intentionally resolves to the same uncompressed storage as the
    // explicit UNCOMPRESSED policy until target compressors are implemented.
    switch ( recipe.outputFormat ) {
        case render_texture_output_format_t::AUTO:
        case render_texture_output_format_t::UNCOMPRESSED:
            break;
    }
    return pixelFormat == render_texture_pixel_format_t::RGBA32_FLOAT
        ? render_format_t::RGBA32_FLOAT
        : pixelFormat == render_texture_pixel_format_t::RGBA8_SRGB
            ? render_format_t::RGBA8_SRGB
            : render_format_t::RGBA8_UNORM;
}

struct texture_compile_work_t {
    text_buffer_t recipeDiagnosticPath{};
    text_buffer_t sourceDiagnosticPath{};
    text_buffer_t outputNativePath{};
    text_buffer_t recipeText{};
    key_value_document_owner_t document{};
    texture_recipe_t recipe{};
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
    bool_t bStraightAlpha,
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
            f64 sums[4]{};
            u64 nTotalWeight = 0u;
            // Boundaries use exact rational coordinates. A 5 -> 2 reduction,
            // for example, gives the shared center texel half weight in each
            // destination texel instead of assigning it wholly to one side.
            const u64 yBegin = static_cast<u64>( y ) * source.nHeight;
            const u64 yEnd = static_cast<u64>( y + 1u ) * source.nHeight;
            const u32 syBegin = static_cast<u32>( yBegin / nHeight );
            const u32 syEnd = static_cast<u32>(
                ( yEnd + nHeight - 1u ) / nHeight );
            const u64 xBegin = static_cast<u64>( x ) * source.nWidth;
            const u64 xEnd = static_cast<u64>( x + 1u ) * source.nWidth;
            const u32 sxBegin = static_cast<u32>( xBegin / nWidth );
            const u32 sxEnd = static_cast<u32>(
                ( xEnd + nWidth - 1u ) / nWidth );
            for ( u32 sy = syBegin; sy < syEnd; ++sy ) {
                const u64 sourceYBegin = static_cast<u64>( sy ) * nHeight;
                const u64 sourceYEnd = static_cast<u64>( sy + 1u ) * nHeight;
                const u64 overlapYBegin =
                    yBegin > sourceYBegin ? yBegin : sourceYBegin;
                const u64 overlapYEnd =
                    yEnd < sourceYEnd ? yEnd : sourceYEnd;
                const u64 weightY = overlapYEnd - overlapYBegin;
                for ( u32 sx = sxBegin; sx < sxEnd; ++sx ) {
                    const u64 sourceXBegin = static_cast<u64>( sx ) * nWidth;
                    const u64 sourceXEnd = static_cast<u64>( sx + 1u ) * nWidth;
                    const u64 overlapXBegin =
                        xBegin > sourceXBegin ? xBegin : sourceXBegin;
                    const u64 overlapXEnd =
                        xEnd < sourceXEnd ? xEnd : sourceXEnd;
                    const u64 weight = weightY *
                        ( overlapXEnd - overlapXBegin );
                    const byte *pPixel = source.pixels.pData +
                        static_cast<usize>( sy ) * source.cbRowPitch + sx * 4u;
                    const f64 alpha =
                        static_cast<f64>( pPixel[3] ) / 255.0;
                    for ( u32 iChannel = 0u; iChannel < 3u; ++iChannel ) {
                        f64 value =
                            static_cast<f64>( pPixel[iChannel] ) / 255.0;
                        if ( bSrgb ) {
                            value = SrgbToLinear(
                                static_cast<f32>( value ) );
                        }
                        if ( bNormal ) {
                            value = value * 2.0 - 1.0;
                        }
                        if ( bStraightAlpha && !bNormal ) {
                            value *= alpha;
                        }
                        sums[iChannel] +=
                            value * static_cast<f64>( weight );
                    }
                    sums[3] += alpha * static_cast<f64>( weight );
                    nTotalWeight += weight;
                }
            }

            byte *pDest = dest.pData +
                ( static_cast<usize>( y ) * nWidth + x ) * 4u;
            const f64 flTotalWeight = static_cast<f64>( nTotalWeight );
            if ( bNormal ) {
                f64 nx = sums[0] / flTotalWeight;
                f64 ny = sums[1] / flTotalWeight;
                f64 nz = sums[2] / flTotalWeight;
                const f64 nLength = std::sqrt( nx * nx + ny * ny + nz * nz );
                if ( nLength > 0.000001 ) {
                    nx /= nLength;
                    ny /= nLength;
                    nz /= nLength;
                } else {
                    nx = 0.0;
                    ny = 0.0;
                    nz = 1.0;
                }
                pDest[0] = QuantizeUnorm8(
                    static_cast<f32>( nx * 0.5 + 0.5 ) );
                pDest[1] = QuantizeUnorm8(
                    static_cast<f32>( ny * 0.5 + 0.5 ) );
                pDest[2] = QuantizeUnorm8(
                    static_cast<f32>( nz * 0.5 + 0.5 ) );
            } else {
                for ( u32 iChannel = 0u; iChannel < 3u; ++iChannel ) {
                    f64 value = bStraightAlpha
                        ? sums[3] > 0.000000001
                            ? sums[iChannel] / sums[3]
                            : 0.0
                        : sums[iChannel] / flTotalWeight;
                    if ( bSrgb ) {
                        value = LinearToSrgb( static_cast<f32>( value ) );
                    }
                    pDest[iChannel] = QuantizeUnorm8(
                        static_cast<f32>( value ) );
                }
            }
            pDest[3] = QuantizeUnorm8(
                static_cast<f32>( sums[3] / flTotalWeight ) );
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
    bool_t bNormal,
    bool_t bStraightAlpha,
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
            f64 sums[4]{};
            u64 nTotalWeight = 0u;
            const u64 yBegin = static_cast<u64>( y ) * source.nHeight;
            const u64 yEnd = static_cast<u64>( y + 1u ) * source.nHeight;
            const u32 syBegin = static_cast<u32>( yBegin / nHeight );
            const u32 syEnd = static_cast<u32>(
                ( yEnd + nHeight - 1u ) / nHeight );
            const u64 xBegin = static_cast<u64>( x ) * source.nWidth;
            const u64 xEnd = static_cast<u64>( x + 1u ) * source.nWidth;
            const u32 sxBegin = static_cast<u32>( xBegin / nWidth );
            const u32 sxEnd = static_cast<u32>(
                ( xEnd + nWidth - 1u ) / nWidth );
            for ( u32 sy = syBegin; sy < syEnd; ++sy ) {
                const u64 sourceYBegin = static_cast<u64>( sy ) * nHeight;
                const u64 sourceYEnd = static_cast<u64>( sy + 1u ) * nHeight;
                const u64 overlapYBegin =
                    yBegin > sourceYBegin ? yBegin : sourceYBegin;
                const u64 overlapYEnd =
                    yEnd < sourceYEnd ? yEnd : sourceYEnd;
                const u64 weightY = overlapYEnd - overlapYBegin;
                for ( u32 sx = sxBegin; sx < sxEnd; ++sx ) {
                    const u64 sourceXBegin = static_cast<u64>( sx ) * nWidth;
                    const u64 sourceXEnd = static_cast<u64>( sx + 1u ) * nWidth;
                    const u64 overlapXBegin =
                        xBegin > sourceXBegin ? xBegin : sourceXBegin;
                    const u64 overlapXEnd =
                        xEnd < sourceXEnd ? xEnd : sourceXEnd;
                    const u64 weight = weightY *
                        ( overlapXEnd - overlapXBegin );
                    const byte *pPixel = source.pixels.pData +
                        static_cast<usize>( sy ) * source.cbRowPitch + sx * 16u;
                    f32 components[4]{};
                    for ( u32 iChannel = 0u; iChannel < 4u; ++iChannel ) {
                        Cy_MemCopy(
                            &components[iChannel],
                            pPixel + iChannel * sizeof( f32 ),
                            sizeof( components[iChannel] ) );
                        components[iChannel] = Cy_LittleToHostF32(
                            components[iChannel] );
                    }
                    const f64 alpha = components[3];
                    for ( u32 iChannel = 0u; iChannel < 3u; ++iChannel ) {
                        f64 value = components[iChannel];
                        if ( bNormal ) {
                            value = value * 2.0 - 1.0;
                        } else if ( bStraightAlpha ) {
                            value *= alpha;
                        }
                        sums[iChannel] +=
                            value * static_cast<f64>( weight );
                    }
                    sums[3] += alpha * static_cast<f64>( weight );
                    nTotalWeight += weight;
                }
            }
            byte *pDest = dest.pData +
                ( static_cast<usize>( y ) * nWidth + x ) * 16u;
            const f64 flTotalWeight = static_cast<f64>( nTotalWeight );
            f64 output[4]{
                sums[0] / flTotalWeight,
                sums[1] / flTotalWeight,
                sums[2] / flTotalWeight,
                sums[3] / flTotalWeight
            };
            if ( bNormal ) {
                const f64 nLength = std::sqrt(
                    output[0] * output[0] +
                    output[1] * output[1] +
                    output[2] * output[2] );
                if ( nLength > 0.000001 ) {
                    output[0] /= nLength;
                    output[1] /= nLength;
                    output[2] /= nLength;
                } else {
                    output[0] = 0.0;
                    output[1] = 0.0;
                    output[2] = 1.0;
                }
                output[0] = output[0] * 0.5 + 0.5;
                output[1] = output[1] * 0.5 + 0.5;
                output[2] = output[2] * 0.5 + 0.5;
            } else if ( bStraightAlpha ) {
                for ( u32 iChannel = 0u; iChannel < 3u; ++iChannel ) {
                    output[iChannel] = sums[3] > 0.000000001
                        ? sums[iChannel] / sums[3]
                        : 0.0;
                }
            }
            for ( u32 iChannel = 0u; iChannel < 4u; ++iChannel ) {
                const f32 value = Cy_HostToLittleF32(
                    static_cast<f32>( output[iChannel] ) );
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

enum class mip_chain_status_t : u8 {
    OK = 0u,
    INVALID_PLAN,
    CAPACITY_EXCEEDED,
    OUT_OF_MEMORY
};

CYPHER_NODISCARD mip_chain_status_t BuildMipChain(
    texture_compile_work_t &work ) noexcept
{
    u64 cbMipData = 0u;
    const u32 cbPixel = work.image.pixelFormat ==
            render_texture_pixel_format_t::RGBA32_FLOAT
        ? 16u
        : 4u;
    if ( !CypherTextureCompiler_CalculateMipDataSize(
             work.image.nWidth,
             work.image.nHeight,
             cbPixel,
             work.recipe.mipMode == render_texture_mip_mode_t::GENERATE,
             &work.nMipLevels,
             &cbMipData ) ||
         work.nMipLevels > CY_COOKED_TEXTURE_MAX_MIP_LEVELS ) {
        return mip_chain_status_t::INVALID_PLAN;
    }
    if ( cbMipData > CY_COOKED_TEXTURE_MAX_TOTAL_DATA_SIZE ) {
        return mip_chain_status_t::CAPACITY_EXCEEDED;
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
    const bool_t bStraightAlpha =
        work.recipe.alphaMode == cooked_texture_alpha_mode_t::STRAIGHT;
    for ( usize iMip = 1u; iMip < work.nMipLevels; ++iMip ) {
        const bool_t bGenerated =
            work.image.pixelFormat == render_texture_pixel_format_t::RGBA32_FLOAT
                ? GenerateNextMipFloat(
                      work.mips[iMip - 1u],
                      bNormal,
                      bStraightAlpha,
                      work.mipStorage[iMip],
                      work.mips[iMip] )
                : GenerateNextMip8(
                      work.mips[iMip - 1u],
                      bSrgb,
                      bNormal,
                      bStraightAlpha,
                      work.mipStorage[iMip],
                      work.mips[iMip] );
        if ( !bGenerated ) {
            return mip_chain_status_t::OUT_OF_MEMORY;
        }
    }
    return mip_chain_status_t::OK;
}

CYPHER_NODISCARD content_hash_t CompilerHash() noexcept
{
    char identity[160]{};
    const string_format_result_t formatted = StringFormat_Printf(
        identity,
        sizeof( identity ),
        "cypher.texture-compiler.api%u.compiler%u.schema%u-%u.cytx%u",
        CY_TEXTURE_COMPILER_API_VERSION,
        CY_TEXTURE_COMPILER_VERSION,
        CY_RENDER_ASSET_SCHEMA_VERSION_V1,
        CY_RENDER_ASSET_SCHEMA_VERSION_V2,
        CY_RENDER_TEXTURE_RESOURCE_VERSION );
    return formatted.status == string_format_status_t::OK
        ? ContentHash_String( StringView_FromCString( identity ) )
        : CY_CONTENT_HASH_INVALID;
}

CYPHER_NODISCARD content_hash_t ToolchainHash() noexcept
{
    return ContentHash_String( TextureText(
        "libpng:" PNG_LIBPNG_VER_STRING
        ";libjpeg-turbo:" CYPHER_STRINGIFY( TURBOJPEG_VERSION_NUMBER )
        ";tinyexr:vcpkg-baseline:" CYPHER_VCPKG_BASELINE ) );
}

CYPHER_NODISCARD content_hash_t ConfigurationHash(
    const tool_context_t &context ) noexcept
{
    // Target and profile are semantic build inputs. Keep them separate from the
    // compiler implementation identity so dependency reports can explain a
    // rebuild caused by an invocation configuration change.
    content_hash_t hash = ContentHash_String(
        TextureText( "cypher-texture-build-configuration-v1" ) );
    hash = ContentHash_Combine(
        hash,
        ContentHash_String( StringView_FromCString(
            ToolTarget_Name( context.target ) ) ) );
    return ContentHash_Combine(
        hash,
        ContentHash_String( StringView_FromCString(
            ToolProfile_Name( context.profile ) ) ) );
}

void EmitDependencies(
    const tool_compile_request_t &request,
    const texture_compile_work_t &work,
    content_hash_t recipeHash,
    content_hash_t imageHash,
    content_hash_t compilerHash,
    content_hash_t toolchainHash,
    content_hash_t configurationHash ) noexcept
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
          TOOL_DEPENDENCY_FLAG_REQUIRED },
        { TextureText( "configuration/texture-target-profile" ),
          tool_dependency_kind_t::CONFIGURATION, configurationHash,
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
    render_asset_decode_result_t decoded{};
    const key_value_document_header_t documentHeader =
        KeyValue_DocumentHeader( work.document.pDocument );
    if ( documentHeader.nSchemaVersion ==
         CY_RENDER_ASSET_SCHEMA_VERSION_V2 ) {
        render_texture_source_v2_view_t source{};
        decoded = RenderTextureSourceV2_Decode(
            work.document.pDocument,
            {},
            diagnostics,
            CYPHER_ARRAY_COUNT( diagnostics ),
            &source );
        if ( RenderAsset_DecodeSucceeded( decoded ) ) {
            NormalizeRecipe( source, work.recipe );
        }
    } else {
        // Version 1 remains the compatibility path. Unknown versions are sent
        // through this decoder as well so its standard schema-version location
        // and diagnostic remain authoritative.
        render_texture_source_view_t source{};
        decoded = RenderTextureSource_Decode(
            work.document.pDocument,
            {},
            diagnostics,
            CYPHER_ARRAY_COUNT( diagnostics ),
            &source );
        if ( RenderAsset_DecodeSucceeded( decoded ) ) {
            NormalizeRecipe( source, work.recipe );
        }
    }
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

    const texture_policy_status_t policyStatus = ValidateTexturePolicy(
        work.recipe );
    if ( policyStatus != texture_policy_status_t::OK ) {
        const bool_t bPreservedImport =
            policyStatus ==
            texture_policy_status_t::UNSUPPORTED_PRESERVED_IMPORT;
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            bPreservedImport
                ? CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_PRESERVED_IMAGE
                : CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_POLICY,
            tool_diagnostic_category_t::COMPILER,
            PolicyDiagnosticMessage( policyStatus ),
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
            TextureText( "The current importer accepts PNG, JPEG, TGA, and finite RGBA EXR images." ) );
    }
    const mip_chain_status_t mipStatus = BuildMipChain( work );
    if ( mipStatus != mip_chain_status_t::OK ) {
        const bool_t bCapacityExceeded =
            mipStatus == mip_chain_status_t::CAPACITY_EXCEEDED;
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            bCapacityExceeded
                ? tool_status_t::VALIDATION_FAILED
                : mipStatus == mip_chain_status_t::OUT_OF_MEMORY
                    ? tool_status_t::OUT_OF_MEMORY
                    : tool_status_t::VALIDATION_FAILED,
            bCapacityExceeded
                ? CY_TEXTURE_DIAGNOSTIC_CAPACITY_EXCEEDED
                : CY_TEXTURE_DIAGNOSTIC_MIP_GENERATION_FAILED,
            bCapacityExceeded
                ? tool_diagnostic_category_t::VALIDATION
                : tool_diagnostic_category_t::COMPILER,
            bCapacityExceeded
                ? TextureText(
                      "Decoded texture mip data exceeds the 512 MiB CYTX limit." )
                : TextureText( "Texture mip chain could not be generated." ),
            work.recipe.source,
            bCapacityExceeded
                ? TextureText(
                      "Reduce source dimensions, use a smaller decoded pixel format, or disable generated mips." )
                : string_view_t{} );
    }
    if ( work.recipe.bStreaming &&
         work.recipe.nResidentMipLevels > work.nMipLevels ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_TEXTURE_DIAGNOSTIC_INVALID_STREAMING_POLICY,
            tool_diagnostic_category_t::VALIDATION,
            TextureText(
                "Texture streaming resident_mips exceeds the generated mip count." ),
            request.input,
            TextureText(
                "Reduce streaming.resident_mips or generate a larger mip chain." ) );
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        TextureText( "Build cooked resource" ) );
    const key_value_canonical_hash_result_t canonicalRecipe =
        KeyValue_HashCanonicalDocument( work.document.pDocument );
    char recipeHeaderIdentity[384]{};
    const string_format_result_t formattedRecipeHeader = StringFormat_Printf(
        recipeHeaderIdentity,
        sizeof( recipeHeaderIdentity ),
        "cykv%u.schema.%.*s.%u",
        documentHeader.nLanguageVersion,
        static_cast<int>( documentHeader.schemaId.cchLength ),
        documentHeader.schemaId.pData,
        documentHeader.nSchemaVersion );
    content_hash_t recipeHash = formattedRecipeHeader.status ==
            string_format_status_t::OK
        ? ContentHash_String( StringView_FromCString( recipeHeaderIdentity ) )
        : CY_CONTENT_HASH_INVALID;
    recipeHash = ContentHash_Combine( recipeHash, canonicalRecipe.hash );
    const content_hash_t imageHash = ContentHash_Data(
        Blob_Block( &work.sourceBytes ) );
    const content_hash_t compilerHash = CompilerHash();
    const content_hash_t toolchainHash = ToolchainHash();
    const content_hash_t configurationHash = ConfigurationHash( context );
    content_hash_t sourceHash = ContentHash_Combine( compilerHash, recipeHash );
    sourceHash = ContentHash_Combine( sourceHash, imageHash );
    sourceHash = ContentHash_Combine( sourceHash, toolchainHash );
    sourceHash = ContentHash_Combine( sourceHash, configurationHash );
    if ( canonicalRecipe.status != key_value_write_status_t::OK ||
         !ContentHash_IsValid( recipeHash ) ||
         !ContentHash_IsValid( configurationHash ) ||
         !ContentHash_IsValid( sourceHash ) ) {
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
    // Output quality still participates in the canonical source identity even
    // though the current uncompressed paths do not need an encoder quality.
    texture.storageFormat = SelectStorageFormat(
        work.recipe,
        texture.pixelFormat );
    texture.usage = work.recipe.usage;
    texture.colorSpace = work.recipe.colorSpace;
    texture.alphaMode = work.recipe.alphaMode;
    texture.alphaCutoff = work.recipe.alphaCutoff;
    texture.target = CookedTarget( context.target );
    texture.residency = work.recipe.bStreaming
        ? cooked_texture_residency_t::MIP_STREAMED
        : cooked_texture_residency_t::FULLY_RESIDENT;
    texture.nResidentMipLevels = work.recipe.bStreaming
        ? work.recipe.nResidentMipLevels
        : 0u;
    texture.nStreamingPriority = work.recipe.bStreaming
        ? work.recipe.nStreamingPriority
        : 0u;
    texture.flags =
        work.recipe.mipMode == render_texture_mip_mode_t::GENERATE &&
        work.nMipLevels > 1u
        ? COOKED_TEXTURE_FLAG_GENERATED_MIPS
        : COOKED_TEXTURE_FLAG_NONE;
    texture.nWidth = work.image.nWidth;
    texture.nHeight = work.image.nHeight;
    texture.nMipLevels = work.nMipLevels;
    const span_t<const cooked_texture_mip_source_t> mipSpan{
        work.mips,
        work.nMipLevels
    };
    cooked_texture_subresource_source_t
        subresources[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    const span_t<const cooked_texture_subresource_source_t> subresourceSpan{
        subresources,
        work.nMipLevels
    };
    if ( work.recipe.nSchemaVersion == CY_RENDER_ASSET_SCHEMA_VERSION_V2 ) {
        for ( u32 iMip = 0u; iMip < work.nMipLevels; ++iMip ) {
            const cooked_texture_mip_source_t &mip = work.mips[iMip];
            subresources[iMip] = {
                iMip,
                0u,
                0u,
                0u,
                mip.nWidth,
                mip.nHeight,
                mip.nDepth,
                mip.cbRowPitch,
                mip.cbRowPitch * mip.nHeight,
                mip.pixels
            };
        }
    }
    const usize cbCooked =
        work.recipe.nSchemaVersion == CY_RENDER_ASSET_SCHEMA_VERSION_V2
            ? CookedTexture_RequiredSizeSubresources(
                  texture,
                  subresourceSpan )
            : CookedTexture_RequiredSize( texture, mipSpan );
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
    const cooked_texture_result_t cooked =
        work.recipe.nSchemaVersion == CY_RENDER_ASSET_SCHEMA_VERSION_V2
            ? CookedTexture_WriteSubresources(
                  texture,
                  subresourceSpan,
                  sourceHash,
                  Blob_WritableSpan( &work.cooked ) )
            : CookedTexture_Write(
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
        toolchainHash,
        configurationHash );
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

bool_t CypherTextureCompiler_CalculateMipDataSize(
    u32 nWidth,
    u32 nHeight,
    u32 cbPixel,
    bool_t bGenerateMips,
    u32 *pMipLevelsOut,
    u64 *pDataSizeOut ) noexcept
{
    if ( nWidth == 0u || nHeight == 0u || cbPixel == 0u ||
         pMipLevelsOut == nullptr || pDataSizeOut == nullptr ) {
        return CY_FALSE;
    }

    u32 nLevels = 0u;
    u64 cbTotal = 0u;
    u32 nLevelWidth = nWidth;
    u32 nLevelHeight = nHeight;
    for ( ;; ) {
        const u64 nPixels =
            static_cast<u64>( nLevelWidth ) * nLevelHeight;
        if ( nPixels > CY_U64_MAX / cbPixel ) {
            return CY_FALSE;
        }
        const u64 cbLevel = nPixels * cbPixel;
        if ( cbLevel > CY_U64_MAX - cbTotal ) {
            return CY_FALSE;
        }
        cbTotal += cbLevel;
        ++nLevels;
        if ( !bGenerateMips ||
             ( nLevelWidth == 1u && nLevelHeight == 1u ) ) {
            break;
        }
        nLevelWidth = nLevelWidth > 1u ? nLevelWidth / 2u : 1u;
        nLevelHeight = nLevelHeight > 1u ? nLevelHeight / 2u : 1u;
    }

    *pMipLevelsOut = nLevels;
    *pDataSizeOut = cbTotal;
    return CY_TRUE;
}

const tool_compiler_desc_t *CypherTextureCompiler_Descriptor() noexcept
{
    return &g_textureCompiler;
}

} // namespace cypher::tools
