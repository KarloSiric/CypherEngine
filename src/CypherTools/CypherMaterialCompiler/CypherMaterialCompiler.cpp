//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp
//  Purpose: Implements the reusable Cypher material compiler module.
//  Details: The compiler parses one CYKV material recipe, validates each direct
//           typed resource dependency through the VFS, records the dependency
//           graph, and packages canonical values through the Common material API.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Material Compiler Implementation Notes

Material compilation validates authored CYKV, resolves every referenced shader or texture
through the VFS, and publishes deterministic cooked output only after all dependencies succeed.
================
*/

#include "CypherMaterialCompiler.h"

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Blob.h"
#include "CypherCommon_CookedMaterial.h"
#include "CypherCommon_DataValidation.h"
#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_KeyValueWriter.h"
#include "CypherCommon_RenderAsset.h"
#include "CypherCommon_StringFormat.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_TextBuffer.h"
#include "CypherCommon_ToolArtifactWriter.h"
#include "CypherCommon_ToolFramework.h"
#include "CypherCommon_Unicode.h"
#include "CypherCommon_Vfs.h"

#include <cmath>

namespace cypher::tools
{

using namespace cypher::common;

namespace
{

inline constexpr usize CY_MATERIAL_COMPILER_MAX_PATH = 259u;
inline constexpr usize CY_MATERIAL_COMPILER_MAX_RECIPE_SIZE = 1u * CY_MIB;
inline constexpr usize CY_MATERIAL_COMPILER_SCHEMA_DIAGNOSTICS = 32u;
inline constexpr u64 CY_MATERIAL_COMPILER_PROGRESS_STEPS = 4u;
inline constexpr usize CY_MATERIAL_COMPILER_MAX_INHERITANCE_DEPTH = 16u;

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t MaterialText(
    const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

enum class material_text_read_status_t : u8 {
    OK = 0u,
    IO_ERROR,
    INVALID_TEXT,
    OUT_OF_MEMORY
};

enum class material_dependency_kind_t : u8 {
    SHADER = 0u,
    TEXTURE
};

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

struct material_base_file_v2_t {
    string_view_t virtualPath{};
    text_buffer_t text{};
    key_value_document_owner_t document{};
    render_material_source_v2_view_t recipe{};
    content_hash_t sourceHash{};
};

struct material_texture_dependency_v2_t {
    string_view_t virtualPath{};
    content_hash_t sourceHash{};
    render_texture_type_t type{ render_texture_type_t::TEXTURE_2D };
    render_texture_usage_t usage{ render_texture_usage_t::COLOR };
    render_texture_color_space_t colorSpace{
        render_texture_color_space_t::SRGB
    };
};

struct resolved_material_feature_v2_t {
    string_view_t name{};
    render_material_feature_value_type_t type{
        render_material_feature_value_type_t::BOOL
    };
    bool_t bValue{ CY_FALSE };
    string_view_t enumValue{};
};

struct resolved_material_texture_v2_t {
    string_view_t binding{};
    string_view_t resource{};
    string_view_t sampler{};
    render_material_uv_binding_v2_view_t uv{};
    bool_t bHasResource{ CY_FALSE };
    bool_t bHasSampler{ CY_FALSE };
};

struct resolved_material_parameter_v2_t {
    string_view_t name{};
    render_asset_value_view_t value{};
};

struct resolved_material_v2_t {
    string_view_t shader{};
    string_view_t surface{};
    render_material_domain_t domain{ render_material_domain_t::SURFACE };
    render_material_alpha_mode_t alphaMode{
        render_material_alpha_mode_t::OPAQUE
    };
    f64 alphaCutoff{ 0.5 };
    bool_t bTwoSided{ CY_FALSE };
    bool_t bCastsShadows{ CY_TRUE };
    bool_t bReceivesShadows{ CY_TRUE };
    bool_t bHasShader{ CY_FALSE };
    bool_t bHasSurface{ CY_FALSE };
    bool_t bSawFeatureOperation{ CY_FALSE };
    resolved_material_feature_v2_t
        features[CY_RENDER_MATERIAL_MAX_FEATURES]{};
    usize nFeatures{ 0u };
    resolved_material_texture_v2_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    usize nTextures{ 0u };
    resolved_material_parameter_v2_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    usize nParameters{ 0u };
};

struct material_compile_work_t {
    text_buffer_t outputNativePath{};
    text_buffer_t recipeText{};
    key_value_document_owner_t document{};
    u32 nSchemaVersion{ 0u };
    render_material_source_view_t recipe{};
    render_material_source_v2_view_t recipeV2{};
    content_hash_t recipeHash{};
    material_base_file_v2_t
        baseMaterials[CY_MATERIAL_COMPILER_MAX_INHERITANCE_DEPTH - 1u]{};
    usize nBaseMaterials{ 0u };
    text_buffer_t shaderText{};
    key_value_document_owner_t shaderDocument{};
    render_shader_source_v2_view_t shaderV2{};
    cooked_shader_binding_source_t
        shaderBindings[CY_COOKED_SHADER_MAX_BINDINGS]{};
    u32 nShaderBindings{ 0u };
    content_hash_t shaderInterfaceHash{};
    resolved_material_v2_t resolvedV2{};
    cooked_material_texture_source_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    cooked_material_parameter_source_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    cooked_material_feature_source_v2_t
        featuresV2[CY_RENDER_MATERIAL_MAX_FEATURES]{};
    cooked_material_texture_source_v2_t
        texturesV2[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    usize nTexturesV2{ 0u };
    cooked_material_parameter_source_v2_t
        parametersV2[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    usize nParametersV2{ 0u };
    content_hash_t textureHashes[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    material_texture_dependency_v2_t
        textureDependenciesV2[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    usize nTextureDependenciesV2{ 0u };
    content_hash_t shaderHash{};
    blob_t cooked{};
};

CYPHER_NODISCARD bool_t InitCompileWork(
    material_compile_work_t &work ) noexcept
{
    return TextBuffer_Init(
               &work.outputNativePath,
               Allocator_GetSystem() ) &&
           TextBuffer_Init( &work.recipeText, Allocator_GetSystem() ) &&
           TextBuffer_Init( &work.shaderText, Allocator_GetSystem() ) &&
           [&work]() noexcept {
               for ( material_base_file_v2_t &base : work.baseMaterials ) {
                   if ( !TextBuffer_Init(
                            &base.text,
                            Allocator_GetSystem() ) ) {
                       return CY_FALSE;
                   }
               }
               return CY_TRUE;
           }() &&
           Blob_Init( &work.cooked, Allocator_GetSystem() );
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
    progress.nTotal = CY_MATERIAL_COMPILER_PROGRESS_STEPS;
    progress.timestamp = Cy_TimerNowTicks();
    progress.title = MaterialText( "Compile material" );
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
        MaterialText( "Failed" ) );
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
        MaterialText( "Cancelled" ) );
    return CY_TRUE;
}

CYPHER_NODISCARD material_text_read_status_t ReadTextFile(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    text_buffer_t &textOut,
    usize &cbReadOut ) noexcept
{
    cbReadOut = 0u;
    blob_t bytes{};
    if ( !Blob_Init( &bytes, Allocator_GetSystem() ) ) {
        return material_text_read_status_t::OUT_OF_MEMORY;
    }
    const vfs_status_t status = Vfs_ReadAll(
        pVfs,
        virtualPath,
        CY_MATERIAL_COMPILER_MAX_RECIPE_SIZE,
        &bytes );
    if ( status == vfs_status_t::OUT_OF_MEMORY ) {
        return material_text_read_status_t::OUT_OF_MEMORY;
    }
    if ( status != vfs_status_t::OK || bytes.cbSize == 0u ) {
        return material_text_read_status_t::IO_ERROR;
    }
    for ( usize iByte = 0u; iByte < bytes.cbSize; ++iByte ) {
        if ( bytes.pData[iByte] == static_cast<byte>( '\0' ) ) {
            return material_text_read_status_t::INVALID_TEXT;
        }
    }
    const string_view_t text{
        reinterpret_cast<const char *>( bytes.pData ),
        bytes.cbSize
    };
    if ( Unicode_ValidateUtf8( text ).status != unicode_status_t::OK ) {
        return material_text_read_status_t::INVALID_TEXT;
    }
    if ( !TextBuffer_Assign( &textOut, text ) ) {
        return material_text_read_status_t::OUT_OF_MEMORY;
    }
    cbReadOut = bytes.cbSize;
    return material_text_read_status_t::OK;
}

CYPHER_NODISCARD text_location_t SchemaDiagnosticLocation(
    schema_diagnostic_code_t code,
    const key_value_parse_result_t &parsed ) noexcept
{
    if ( code == schema_diagnostic_code_t::LANGUAGE_VERSION_MISMATCH ) {
        return parsed.languageVersionLocation;
    }
    if ( code == schema_diagnostic_code_t::SCHEMA_ID_MISMATCH ) {
        return parsed.schemaIdLocation;
    }
    if ( code == schema_diagnostic_code_t::SCHEMA_VERSION_MISMATCH ) {
        return parsed.schemaVersionLocation;
    }
    return {};
}

void EmitSchemaFailure(
    const tool_compile_request_t &request,
    tool_report_t &report,
    tool_diagnostic_code_t code,
    string_view_t path,
    const schema_diagnostic_t &diagnostic,
    const key_value_parse_result_t &parsed ) noexcept
{
    char message[512]{};
    const string_format_result_t formatted = StringFormat_Printf(
        message,
        sizeof( message ),
        "%s at %s",
        Schema_DiagnosticCodeName( diagnostic.code ),
        diagnostic.path[0] != '\0' ? diagnostic.path : "$" );
    const text_location_t location = SchemaDiagnosticLocation(
        diagnostic.code,
        parsed );
    EmitDiagnostic(
        request,
        report,
        code,
        tool_diagnostic_severity_t::ERROR,
        tool_diagnostic_category_t::SCHEMA,
        formatted.status == string_format_status_t::OK
            ? StringView_FromCString( message )
            : MaterialText( "Resource schema validation failed." ),
        path,
        location.nLine,
        location.nColumn );
}

CYPHER_NODISCARD tool_status_t ParseMaterialRecipe(
    const tool_compile_request_t &request,
    tool_report_t &report,
    material_compile_work_t &work,
    tool_sequence_t sequence,
    u64 nCompleted ) noexcept
{
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
            CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            MaterialText( "Out of memory while creating the CYKV document." ),
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
            CY_MATERIAL_DIAGNOSTIC_CYKV_PARSE_FAILED,
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

    schema_diagnostic_t diagnostics[CY_MATERIAL_COMPILER_SCHEMA_DIAGNOSTICS]{};
    const key_value_document_header_t header =
        KeyValue_DocumentHeader( work.document.pDocument );
    render_asset_decode_result_t decoded{};
    switch ( header.nSchemaVersion ) {
        case CY_RENDER_ASSET_SCHEMA_VERSION_V1:
            decoded = RenderMaterialSource_Decode(
                work.document.pDocument,
                {},
                diagnostics,
                CYPHER_ARRAY_COUNT( diagnostics ),
                &work.recipe );
            break;
        case CY_RENDER_ASSET_SCHEMA_VERSION_V2:
            decoded = RenderMaterialSourceV2_Decode(
                work.document.pDocument,
                {},
                diagnostics,
                CYPHER_ARRAY_COUNT( diagnostics ),
                &work.recipeV2 );
            break;
        default:
            EmitDiagnostic(
                request,
                report,
                CY_MATERIAL_DIAGNOSTIC_SCHEMA_FAILED,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::SCHEMA,
                MaterialText( "Material schema version must be 1 or 2." ),
                request.input,
                parsed.schemaVersionLocation.nLine,
                parsed.schemaVersionLocation.nColumn );
            MarkFailed( report );
            EmitFailureProgress(
                request,
                sequence,
                tool_status_t::VALIDATION_FAILED,
                nCompleted );
            return tool_status_t::VALIDATION_FAILED;
    }
    if ( RenderAsset_DecodeSucceeded( decoded ) ) {
        const key_value_canonical_hash_result_t canonical =
            KeyValue_HashCanonicalDocument( work.document.pDocument );
        if ( canonical.status != key_value_write_status_t::OK ||
             !ContentHash_IsValid( canonical.hash ) ) {
            return Fail(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::INTERNAL_ERROR,
                CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
                tool_diagnostic_category_t::INTERNAL,
                MaterialText( "Material recipe identity could not be generated." ),
                request.input );
        }
        work.nSchemaVersion = header.nSchemaVersion;
        work.recipeHash = canonical.hash;
        return tool_status_t::OK;
    }
    if ( decoded.validation.nDiagnosticsWritten != 0u ) {
        EmitSchemaFailure(
            request,
            report,
            CY_MATERIAL_DIAGNOSTIC_SCHEMA_FAILED,
            request.input,
            diagnostics[0],
            parsed );
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
        CY_MATERIAL_DIAGNOSTIC_SCHEMA_FAILED,
        tool_diagnostic_category_t::SCHEMA,
        StringView_FromCString(
            RenderAsset_DecodeStatusName( decoded.status ) ),
        request.input );
}

CYPHER_NODISCARD tool_status_t ValidateDependencyRecipe(
    const tool_compile_request_t &request,
    tool_report_t &report,
    string_view_t path,
    material_dependency_kind_t kind,
    content_hash_t &hashOut,
    tool_sequence_t sequence,
    u64 nCompleted ) noexcept
{
    text_buffer_t text{};
    if ( !TextBuffer_Init( &text, Allocator_GetSystem() ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            MaterialText( "Out of memory while validating a material dependency." ),
            path );
    }
    usize cbRead = 0u;
    const material_text_read_status_t readStatus = ReadTextFile(
        request.pInvocation->pContext->pSourceVfs,
        path,
        text,
        cbRead );
    if ( readStatus != material_text_read_status_t::OK ) {
        const bool_t bOutOfMemory =
            readStatus == material_text_read_status_t::OUT_OF_MEMORY;
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            bOutOfMemory ? tool_status_t::OUT_OF_MEMORY
                         : tool_status_t::IO_ERROR,
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_READ_FAILED,
            tool_diagnostic_category_t::FILESYSTEM,
            readStatus == material_text_read_status_t::INVALID_TEXT
                ? MaterialText( "Referenced resource is not bounded UTF-8 text." )
                : MaterialText( "Referenced resource recipe could not be read." ),
            path );
    }
    report.cbRead += cbRead;
    hashOut = {};

    key_value_document_owner_t document{};
    key_value_document_desc_t documentDesc{};
    documentDesc.pAllocator = Allocator_GetSystem();
    document.pDocument = KeyValue_CreateDocument( documentDesc );
    if ( document.pDocument == nullptr ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            MaterialText( "Out of memory while parsing a material dependency." ),
            path );
    }
    const key_value_parse_result_t parsed = KeyValue_ParseText(
        TextBuffer_View( &text ),
        {},
        document.pDocument );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        EmitDiagnostic(
            request,
            report,
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_PARSE_FAILED,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::SOURCE,
            StringView_FromCString( KeyValue_ParseStatusName( parsed.status ) ),
            path,
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

    schema_diagnostic_t diagnostics[CY_MATERIAL_COMPILER_SCHEMA_DIAGNOSTICS]{};
    render_asset_decode_result_t decoded{};
    if ( kind == material_dependency_kind_t::SHADER ) {
        render_shader_source_view_t shader{};
        decoded = RenderShaderSource_Decode(
            document.pDocument,
            {},
            diagnostics,
            CYPHER_ARRAY_COUNT( diagnostics ),
            &shader );
    } else {
        render_texture_source_view_t texture{};
        decoded = RenderTextureSource_Decode(
            document.pDocument,
            {},
            diagnostics,
            CYPHER_ARRAY_COUNT( diagnostics ),
            &texture );
    }
    if ( RenderAsset_DecodeSucceeded( decoded ) ) {
        const key_value_canonical_hash_result_t canonical =
            KeyValue_HashCanonicalDocument( document.pDocument );
        if ( canonical.status != key_value_write_status_t::OK ||
             !ContentHash_IsValid( canonical.hash ) ) {
            return Fail(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::INTERNAL_ERROR,
                CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
                tool_diagnostic_category_t::INTERNAL,
                MaterialText(
                    "Referenced resource identity could not be generated." ),
                path );
        }
        hashOut = canonical.hash;
        return tool_status_t::OK;
    }
    if ( decoded.validation.nDiagnosticsWritten != 0u ) {
        EmitSchemaFailure(
            request,
            report,
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
            path,
            diagnostics[0],
            parsed );
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
        CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
        tool_diagnostic_category_t::SCHEMA,
        StringView_FromCString(
            RenderAsset_DecodeStatusName( decoded.status ) ),
        path );
}

CYPHER_NODISCARD tool_status_t FailNamed(
    const tool_compile_request_t &request,
    tool_report_t &report,
    tool_sequence_t sequence,
    u64 nCompleted,
    tool_status_t status,
    tool_diagnostic_code_t code,
    tool_diagnostic_category_t category,
    const char *pDescription,
    string_view_t name,
    string_view_t path ) noexcept
{
    char message[512]{};
    const string_format_result_t formatted = StringFormat_Printf(
        message,
        sizeof( message ),
        "%s '%.*s'.",
        pDescription,
        static_cast<int>( name.cchLength ),
        name.pData != nullptr ? name.pData : "" );
    return Fail(
        request,
        report,
        sequence,
        nCompleted,
        status,
        code,
        category,
        formatted.status == string_format_status_t::OK
            ? StringView_FromCString( message )
            : MaterialText( "Material validation failed." ),
        path );
}

CYPHER_NODISCARD bool_t MaterialPathIsInChain(
    const tool_compile_request_t &request,
    const material_compile_work_t &work,
    string_view_t path ) noexcept
{
    if ( StringView_Equals( request.input, path ) ) {
        return CY_TRUE;
    }
    for ( usize iBase = 0u;
          iBase < work.nBaseMaterials;
          ++iBase ) {
        if ( StringView_Equals(
                 work.baseMaterials[iBase].virtualPath,
                 path ) ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD tool_status_t LoadBaseMaterialsV2(
    const tool_compile_request_t &request,
    tool_report_t &report,
    material_compile_work_t &work,
    tool_sequence_t sequence,
    u64 nCompleted ) noexcept
{
    const render_material_source_v2_view_t *pCurrent = &work.recipeV2;
    while ( pCurrent->bHasBase ) {
        const string_view_t basePath = pCurrent->base;
        if ( MaterialPathIsInChain( request, work, basePath ) ) {
            return FailNamed(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::VALIDATION_FAILED,
                CY_MATERIAL_DIAGNOSTIC_INHERITANCE_CYCLE,
                tool_diagnostic_category_t::VALIDATION,
                "Material inheritance cycle reaches",
                basePath,
                basePath );
        }
        if ( work.nBaseMaterials >=
             CYPHER_ARRAY_COUNT( work.baseMaterials ) ) {
            return Fail(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::VALIDATION_FAILED,
                CY_MATERIAL_DIAGNOSTIC_INHERITANCE_DEPTH,
                tool_diagnostic_category_t::VALIDATION,
                MaterialText(
                    "Material inheritance exceeds the maximum depth of 16 recipes." ),
                basePath );
        }

        material_base_file_v2_t &base =
            work.baseMaterials[work.nBaseMaterials];
        base.virtualPath = basePath;
        usize cbRead = 0u;
        const material_text_read_status_t readStatus = ReadTextFile(
            request.pInvocation->pContext->pSourceVfs,
            basePath,
            base.text,
            cbRead );
        if ( readStatus != material_text_read_status_t::OK ) {
            const bool_t bOutOfMemory =
                readStatus == material_text_read_status_t::OUT_OF_MEMORY;
            return Fail(
                request,
                report,
                sequence,
                nCompleted,
                bOutOfMemory ? tool_status_t::OUT_OF_MEMORY
                             : tool_status_t::IO_ERROR,
                CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_READ_FAILED,
                tool_diagnostic_category_t::FILESYSTEM,
                readStatus == material_text_read_status_t::INVALID_TEXT
                    ? MaterialText(
                          "Base material is not bounded UTF-8 text." )
                    : MaterialText( "Base material could not be read." ),
                basePath );
        }
        report.cbRead += cbRead;

        key_value_document_desc_t documentDesc{};
        documentDesc.pAllocator = Allocator_GetSystem();
        base.document.pDocument = KeyValue_CreateDocument( documentDesc );
        if ( base.document.pDocument == nullptr ) {
            return Fail(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::OUT_OF_MEMORY,
                CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
                tool_diagnostic_category_t::INTERNAL,
                MaterialText(
                    "Out of memory while parsing a base material." ),
                basePath );
        }
        const key_value_parse_result_t parsed = KeyValue_ParseText(
            TextBuffer_View( &base.text ),
            {},
            base.document.pDocument );
        if ( parsed.status != key_value_parse_status_t::OK ) {
            EmitDiagnostic(
                request,
                report,
                CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_PARSE_FAILED,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::SOURCE,
                StringView_FromCString(
                    KeyValue_ParseStatusName( parsed.status ) ),
                basePath,
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

        const key_value_document_header_t header =
            KeyValue_DocumentHeader( base.document.pDocument );
        if ( header.nSchemaVersion != CY_RENDER_ASSET_SCHEMA_VERSION_V2 ) {
            return Fail(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::VALIDATION_FAILED,
                CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT,
                tool_diagnostic_category_t::SCHEMA,
                MaterialText(
                    "A V2 material may inherit only from another V2 material." ),
                basePath );
        }

        schema_diagnostic_t diagnostics[
            CY_MATERIAL_COMPILER_SCHEMA_DIAGNOSTICS]{};
        const render_asset_decode_result_t decoded =
            RenderMaterialSourceV2_Decode(
                base.document.pDocument,
                {},
                diagnostics,
                CYPHER_ARRAY_COUNT( diagnostics ),
                &base.recipe );
        if ( !RenderAsset_DecodeSucceeded( decoded ) ) {
            if ( decoded.validation.nDiagnosticsWritten != 0u ) {
                EmitSchemaFailure(
                    request,
                    report,
                    CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
                    basePath,
                    diagnostics[0],
                    parsed );
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
                CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
                tool_diagnostic_category_t::SCHEMA,
                StringView_FromCString(
                    RenderAsset_DecodeStatusName( decoded.status ) ),
                basePath );
        }

        const key_value_canonical_hash_result_t canonical =
            KeyValue_HashCanonicalDocument( base.document.pDocument );
        if ( canonical.status != key_value_write_status_t::OK ||
             !ContentHash_IsValid( canonical.hash ) ) {
            return Fail(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::INTERNAL_ERROR,
                CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
                tool_diagnostic_category_t::INTERNAL,
                MaterialText(
                    "Base material identity could not be generated." ),
                basePath );
        }
        base.sourceHash = canonical.hash;
        ++work.nBaseMaterials;
        pCurrent = &base.recipe;
    }
    return tool_status_t::OK;
}

CYPHER_NODISCARD usize FindResolvedFeatureV2(
    const resolved_material_v2_t &material,
    string_view_t name ) noexcept
{
    for ( usize iFeature = 0u;
          iFeature < material.nFeatures;
          ++iFeature ) {
        if ( StringView_Equals( material.features[iFeature].name, name ) ) {
            return iFeature;
        }
    }
    return CY_INVALID_SIZE;
}

CYPHER_NODISCARD usize FindResolvedTextureV2(
    const resolved_material_v2_t &material,
    string_view_t binding ) noexcept
{
    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        if ( StringView_Equals(
                 material.textures[iTexture].binding,
                 binding ) ) {
            return iTexture;
        }
    }
    return CY_INVALID_SIZE;
}

CYPHER_NODISCARD usize FindResolvedParameterV2(
    const resolved_material_v2_t &material,
    string_view_t name ) noexcept
{
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        if ( StringView_Equals(
                 material.parameters[iParameter].name,
                 name ) ) {
            return iParameter;
        }
    }
    return CY_INVALID_SIZE;
}

template <typename value_t>
void RemoveResolvedValue(
    value_t *pValues,
    usize &nValues,
    usize iRemove ) noexcept
{
    if ( iRemove >= nValues ) {
        return;
    }
    for ( usize iValue = iRemove + 1u;
          iValue < nValues;
          ++iValue ) {
        pValues[iValue - 1u] = pValues[iValue];
    }
    pValues[--nValues] = {};
}

enum class material_merge_status_t : u8 {
    OK = 0u,
    FEATURE_LIMIT,
    TEXTURE_LIMIT,
    PARAMETER_LIMIT,
    PARTIAL_TEXTURE_WITHOUT_BASE,
    ALPHA_CUTOFF_WITHOUT_MASK
};

CYPHER_NODISCARD material_merge_status_t ApplyMaterialLayerV2(
    resolved_material_v2_t &resolved,
    const render_material_source_v2_view_t &layer ) noexcept
{
    if ( layer.bHasShader ) {
        resolved.shader = layer.shader;
        resolved.bHasShader = CY_TRUE;
    }
    if ( layer.bHasSurface ) {
        resolved.surface = layer.surface;
        resolved.bHasSurface = CY_TRUE;
    }
    if ( layer.bHasDomain ) {
        resolved.domain = layer.domain;
    }
    if ( layer.state.bHasAlphaMode ) {
        resolved.alphaMode = layer.state.alphaMode;
        if ( resolved.alphaMode != render_material_alpha_mode_t::MASK ) {
            resolved.alphaCutoff = 0.5;
        }
    }
    if ( layer.state.bHasAlphaCutoff ) {
        if ( resolved.alphaMode != render_material_alpha_mode_t::MASK ) {
            return material_merge_status_t::ALPHA_CUTOFF_WITHOUT_MASK;
        }
        resolved.alphaCutoff = layer.state.alphaCutoff;
    }
    if ( layer.state.bHasTwoSided ) {
        resolved.bTwoSided = layer.state.bTwoSided;
    }
    if ( layer.state.bHasCastsShadows ) {
        resolved.bCastsShadows = layer.state.bCastsShadows;
    }
    if ( layer.state.bHasReceivesShadows ) {
        resolved.bReceivesShadows = layer.state.bReceivesShadows;
    }

    for ( usize iFeature = 0u;
          iFeature < layer.nFeatures;
          ++iFeature ) {
        const render_material_feature_v2_view_t &source =
            layer.features[iFeature];
        resolved.bSawFeatureOperation = CY_TRUE;
        usize iResolved = FindResolvedFeatureV2( resolved, source.name );
        if ( source.bRemove ) {
            RemoveResolvedValue(
                resolved.features,
                resolved.nFeatures,
                iResolved );
            continue;
        }
        if ( iResolved == CY_INVALID_SIZE ) {
            if ( resolved.nFeatures >=
                 CYPHER_ARRAY_COUNT( resolved.features ) ) {
                return material_merge_status_t::FEATURE_LIMIT;
            }
            iResolved = resolved.nFeatures++;
        }
        resolved_material_feature_v2_t &feature =
            resolved.features[iResolved];
        feature.name = source.name;
        feature.type = source.type;
        feature.bValue = source.bValue;
        feature.enumValue = source.type ==
                render_material_feature_value_type_t::ENUM
            ? source.enumValue
            : string_view_t{};
    }

    for ( usize iTexture = 0u;
          iTexture < layer.nTextures;
          ++iTexture ) {
        const render_material_texture_v2_view_t &source =
            layer.textures[iTexture];
        usize iResolved = FindResolvedTextureV2(
            resolved,
            source.binding );
        if ( source.bRemove ) {
            RemoveResolvedValue(
                resolved.textures,
                resolved.nTextures,
                iResolved );
            continue;
        }
        if ( iResolved == CY_INVALID_SIZE ) {
            if ( resolved.nTextures >=
                 CYPHER_ARRAY_COUNT( resolved.textures ) ) {
                return material_merge_status_t::TEXTURE_LIMIT;
            }
            iResolved = resolved.nTextures++;
            resolved.textures[iResolved].binding = source.binding;
        }
        resolved_material_texture_v2_t &texture =
            resolved.textures[iResolved];
        if ( source.bHasResource ) {
            texture.resource = source.resource;
            texture.bHasResource = CY_TRUE;
        }
        if ( source.bHasSampler ) {
            texture.sampler = source.sampler;
            texture.bHasSampler = CY_TRUE;
        }
        // `uv` is one atomic inherited block. An authored block replaces all
        // inherited fields, including members omitted back to schema defaults.
        if ( source.uv.bPresent ) {
            texture.uv = source.uv;
        }
        if ( !texture.bHasResource ) {
            return material_merge_status_t::PARTIAL_TEXTURE_WITHOUT_BASE;
        }
    }

    for ( usize iParameter = 0u;
          iParameter < layer.nParameters;
          ++iParameter ) {
        const render_material_parameter_v2_view_t &source =
            layer.parameters[iParameter];
        usize iResolved = FindResolvedParameterV2(
            resolved,
            source.name );
        if ( source.bRemove ) {
            RemoveResolvedValue(
                resolved.parameters,
                resolved.nParameters,
                iResolved );
            continue;
        }
        if ( iResolved == CY_INVALID_SIZE ) {
            if ( resolved.nParameters >=
                 CYPHER_ARRAY_COUNT( resolved.parameters ) ) {
                return material_merge_status_t::PARAMETER_LIMIT;
            }
            iResolved = resolved.nParameters++;
        }
        resolved.parameters[iResolved].name = source.name;
        resolved.parameters[iResolved].value = source.value;
    }
    return material_merge_status_t::OK;
}

CYPHER_NODISCARD tool_status_t ResolveMaterialV2(
    const tool_compile_request_t &request,
    tool_report_t &report,
    material_compile_work_t &work,
    tool_sequence_t sequence,
    u64 nCompleted ) noexcept
{
    resolved_material_v2_t resolved{};
    for ( usize iLayer = work.nBaseMaterials + 1u;
          iLayer != 0u;
          --iLayer ) {
        const usize iChain = iLayer - 1u;
        const render_material_source_v2_view_t &layer = iChain == 0u
            ? work.recipeV2
            : work.baseMaterials[iChain - 1u].recipe;
        const string_view_t path = iChain == 0u
            ? request.input
            : work.baseMaterials[iChain - 1u].virtualPath;
        const material_merge_status_t mergeStatus =
            ApplyMaterialLayerV2( resolved, layer );
        if ( mergeStatus == material_merge_status_t::OK ) {
            continue;
        }
        const char *pMessage = "Material inheritance could not be resolved.";
        switch ( mergeStatus ) {
            case material_merge_status_t::FEATURE_LIMIT:
                pMessage = "Resolved feature count exceeds the material limit.";
                break;
            case material_merge_status_t::TEXTURE_LIMIT:
                pMessage = "Resolved texture count exceeds the material limit.";
                break;
            case material_merge_status_t::PARAMETER_LIMIT:
                pMessage = "Resolved parameter count exceeds the material limit.";
                break;
            case material_merge_status_t::PARTIAL_TEXTURE_WITHOUT_BASE:
                pMessage = "A partial texture override has no inherited resource.";
                break;
            case material_merge_status_t::ALPHA_CUTOFF_WITHOUT_MASK:
                pMessage = "Alpha cutoff resolves without mask alpha mode.";
                break;
            case material_merge_status_t::OK:
                break;
        }
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
            tool_diagnostic_category_t::VALIDATION,
            StringView_FromCString( pMessage ),
            path );
    }
    if ( !resolved.bHasShader ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
            tool_diagnostic_category_t::VALIDATION,
            MaterialText(
                "Resolved V2 material does not select a shader." ),
            request.input );
    }
    work.resolvedV2 = resolved;
    return tool_status_t::OK;
}

CYPHER_NODISCARD render_shader_resource_type_t ShaderTextureResourceTypeV2(
    render_shader_texture_type_t type ) noexcept
{
    switch ( type ) {
        case render_shader_texture_type_t::TEXTURE_2D:
            return render_shader_resource_type_t::TEXTURE_2D;
        case render_shader_texture_type_t::TEXTURE_CUBE:
            return render_shader_resource_type_t::TEXTURE_CUBE;
        case render_shader_texture_type_t::TEXTURE_2D_ARRAY:
            return render_shader_resource_type_t::TEXTURE_2D_ARRAY;
        case render_shader_texture_type_t::TEXTURE_3D:
            return render_shader_resource_type_t::TEXTURE_3D;
    }
    return render_shader_resource_type_t::NONE;
}

CYPHER_NODISCARD render_shader_value_type_t ShaderParameterValueTypeV2(
    render_shader_parameter_type_t type ) noexcept
{
    switch ( type ) {
        case render_shader_parameter_type_t::BOOL:
            return render_shader_value_type_t::BOOL;
        case render_shader_parameter_type_t::I32:
            return render_shader_value_type_t::I32;
        case render_shader_parameter_type_t::U32:
            return render_shader_value_type_t::U32;
        case render_shader_parameter_type_t::F32:
            return render_shader_value_type_t::F32;
        case render_shader_parameter_type_t::F32X2:
            return render_shader_value_type_t::F32X2;
        case render_shader_parameter_type_t::F32X3:
        case render_shader_parameter_type_t::COLOR3:
            return render_shader_value_type_t::F32X3;
        case render_shader_parameter_type_t::F32X4:
        case render_shader_parameter_type_t::COLOR4:
            return render_shader_value_type_t::F32X4;
        case render_shader_parameter_type_t::MAT3:
            return render_shader_value_type_t::F32X3X3;
        case render_shader_parameter_type_t::MAT4:
            return render_shader_value_type_t::F32X4X4;
    }
    return render_shader_value_type_t::NONE;
}

CYPHER_NODISCARD flags32_t MaterialBindingFlagsV2(
    bool_t bRequired ) noexcept
{
    return COOKED_SHADER_BINDING_FLAG_MATERIAL |
           ( bRequired ? COOKED_SHADER_BINDING_FLAG_REQUIRED : 0u );
}

CYPHER_NODISCARD bool_t AppendShaderBindingV2(
    material_compile_work_t &work,
    const cooked_shader_binding_source_t &binding ) noexcept
{
    if ( work.nShaderBindings >=
             CYPHER_ARRAY_COUNT( work.shaderBindings ) ||
         binding.nLogicalBinding == 0u ) {
        return CY_FALSE;
    }
    for ( u32 iBinding = 0u;
          iBinding < work.nShaderBindings;
          ++iBinding ) {
        if ( work.shaderBindings[iBinding].nLogicalBinding ==
             binding.nLogicalBinding ) {
            return CY_FALSE;
        }
    }
    work.shaderBindings[work.nShaderBindings++] = binding;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t BuildShaderInterfaceV2(
    material_compile_work_t &work ) noexcept
{
    work.nShaderBindings = 0u;
    work.shaderInterfaceHash = {};

    for ( usize iTexture = 0u;
          iTexture < work.shaderV2.nTextures;
          ++iTexture ) {
        const render_shader_texture_interface_v2_view_t &source =
            work.shaderV2.textures[iTexture];
        cooked_shader_binding_source_t binding{};
        binding.name = source.name;
        binding.kind = render_shader_binding_kind_t::SAMPLED_TEXTURE;
        binding.resourceType = ShaderTextureResourceTypeV2( source.type );
        binding.stageMask = RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS;
        binding.flags = MaterialBindingFlagsV2( source.bRequired );
        if ( binding.resourceType == render_shader_resource_type_t::NONE ||
             !CookedShader_MakeLogicalBindingId(
                 binding.name,
                 &binding.nLogicalBinding ) ||
             !AppendShaderBindingV2( work, binding ) ) {
            return CY_FALSE;
        }
    }

    // Independent sampler bindings are reconstructed for hash parity even
    // though this compiler version rejects them before material publication.
    for ( usize iSampler = 0u;
          iSampler < work.shaderV2.nSamplers;
          ++iSampler ) {
        const render_shader_sampler_interface_v2_view_t &source =
            work.shaderV2.samplers[iSampler];
        cooked_shader_binding_source_t binding{};
        binding.name = source.name;
        binding.kind = render_shader_binding_kind_t::SAMPLER;
        binding.resourceType =
            source.type == render_shader_sampler_type_t::COMPARISON
                ? render_shader_resource_type_t::SAMPLER_COMPARISON
                : render_shader_resource_type_t::SAMPLER;
        binding.stageMask = RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS;
        binding.flags = MaterialBindingFlagsV2( source.bRequired );
        if ( !CookedShader_MakeLogicalBindingId(
                 binding.name,
                 &binding.nLogicalBinding ) ||
             !AppendShaderBindingV2( work, binding ) ) {
            return CY_FALSE;
        }
    }

    usize parameterOrder[CY_RENDER_SHADER_MAX_INTERFACE_PARAMETERS]{};
    for ( usize iParameter = 0u;
          iParameter < work.shaderV2.nParameters;
          ++iParameter ) {
        parameterOrder[iParameter] = iParameter;
    }
    for ( usize iParameter = 1u;
          iParameter < work.shaderV2.nParameters;
          ++iParameter ) {
        const usize value = parameterOrder[iParameter];
        usize iInsert = iParameter;
        while ( iInsert != 0u &&
                StringView_Compare(
                    work.shaderV2.parameters[value].name,
                    work.shaderV2.parameters[
                        parameterOrder[iInsert - 1u] ].name ) < 0 ) {
            parameterOrder[iInsert] = parameterOrder[iInsert - 1u];
            --iInsert;
        }
        parameterOrder[iInsert] = value;
    }

    usize cbParameterBlock = 0u;
    for ( usize iOrdered = 0u;
          iOrdered < work.shaderV2.nParameters;
          ++iOrdered ) {
        const render_shader_parameter_interface_v2_view_t &source =
            work.shaderV2.parameters[parameterOrder[iOrdered]];
        cooked_shader_binding_source_t binding{};
        binding.name = source.name;
        binding.kind = render_shader_binding_kind_t::VALUE;
        binding.valueType = ShaderParameterValueTypeV2( source.type );
        binding.stageMask = RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS;
        binding.flags = MaterialBindingFlagsV2( source.bRequired );
        const u32 cbAlignment = CookedShader_ValueTypeStorageAlignment(
            binding.valueType );
        const u32 cbStorage = CookedShader_ValueTypeStorageSize(
            binding.valueType );
        usize iAlignedOffset = 0u;
        if ( cbAlignment == 0u || cbStorage == 0u ||
             !Cy_AlignUpChecked(
                 cbParameterBlock,
                 cbAlignment,
                 iAlignedOffset ) ||
             iAlignedOffset > CY_U32_MAX ||
             cbStorage > CY_U32_MAX - iAlignedOffset ) {
            return CY_FALSE;
        }
        binding.iByteOffset = static_cast<u32>( iAlignedOffset );
        binding.cbByteSize = cbStorage;
        cbParameterBlock = iAlignedOffset + cbStorage;
        if ( !CookedShader_MakeLogicalBindingId(
                 binding.name,
                 &binding.nLogicalBinding ) ||
             !AppendShaderBindingV2( work, binding ) ) {
            return CY_FALSE;
        }
    }

    const cooked_shader_interface_source_t shaderInterface{
        { work.shaderBindings, work.nShaderBindings }
    };
    return CookedShader_ComputeInterfaceHash(
        shaderInterface,
        &work.shaderInterfaceHash );
}

CYPHER_NODISCARD tool_status_t LoadShaderV2(
    const tool_compile_request_t &request,
    tool_report_t &report,
    material_compile_work_t &work,
    tool_sequence_t sequence,
    u64 nCompleted ) noexcept
{
    const string_view_t path = work.resolvedV2.shader;
    usize cbRead = 0u;
    const material_text_read_status_t readStatus = ReadTextFile(
        request.pInvocation->pContext->pSourceVfs,
        path,
        work.shaderText,
        cbRead );
    if ( readStatus != material_text_read_status_t::OK ) {
        const bool_t bOutOfMemory =
            readStatus == material_text_read_status_t::OUT_OF_MEMORY;
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            bOutOfMemory ? tool_status_t::OUT_OF_MEMORY
                         : tool_status_t::IO_ERROR,
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_READ_FAILED,
            tool_diagnostic_category_t::FILESYSTEM,
            readStatus == material_text_read_status_t::INVALID_TEXT
                ? MaterialText(
                      "Referenced shader is not bounded UTF-8 text." )
                : MaterialText( "Referenced shader could not be read." ),
            path );
    }
    report.cbRead += cbRead;

    key_value_document_desc_t documentDesc{};
    documentDesc.pAllocator = Allocator_GetSystem();
    work.shaderDocument.pDocument = KeyValue_CreateDocument( documentDesc );
    if ( work.shaderDocument.pDocument == nullptr ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            MaterialText( "Out of memory while parsing the V2 shader." ),
            path );
    }
    const key_value_parse_result_t parsed = KeyValue_ParseText(
        TextBuffer_View( &work.shaderText ),
        {},
        work.shaderDocument.pDocument );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        EmitDiagnostic(
            request,
            report,
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_PARSE_FAILED,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::SOURCE,
            StringView_FromCString( KeyValue_ParseStatusName( parsed.status ) ),
            path,
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

    const key_value_document_header_t header =
        KeyValue_DocumentHeader( work.shaderDocument.pDocument );
    if ( header.nSchemaVersion != CY_RENDER_ASSET_SCHEMA_VERSION_V2 ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT,
            tool_diagnostic_category_t::SCHEMA,
            MaterialText(
                "A V2 material requires a V2 shader source contract." ),
            path );
    }

    schema_diagnostic_t diagnostics[CY_MATERIAL_COMPILER_SCHEMA_DIAGNOSTICS]{};
    const render_asset_decode_result_t decoded = RenderShaderSourceV2_Decode(
        work.shaderDocument.pDocument,
        {},
        diagnostics,
        CYPHER_ARRAY_COUNT( diagnostics ),
        &work.shaderV2 );
    if ( !RenderAsset_DecodeSucceeded( decoded ) ) {
        if ( decoded.validation.nDiagnosticsWritten != 0u ) {
            EmitSchemaFailure(
                request,
                report,
                CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
                path,
                diagnostics[0],
                parsed );
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
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
            tool_diagnostic_category_t::SCHEMA,
            StringView_FromCString(
                RenderAsset_DecodeStatusName( decoded.status ) ),
            path );
    }

    if ( work.shaderV2.nFeatures != 0u ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT,
            tool_diagnostic_category_t::VALIDATION,
            MaterialText(
                "Shader features require a versioned variant-table contract." ),
            path );
    }
    if ( work.shaderV2.nSamplers != 0u ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT,
            tool_diagnostic_category_t::VALIDATION,
            MaterialText(
                "Independent shader samplers require a versioned material association." ),
            path );
    }

    const key_value_canonical_hash_result_t canonical =
        KeyValue_HashCanonicalDocument( work.shaderDocument.pDocument );
    if ( canonical.status != key_value_write_status_t::OK ||
         !ContentHash_IsValid( canonical.hash ) ||
         !BuildShaderInterfaceV2( work ) ||
         !ContentHash_IsValid( work.shaderInterfaceHash ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INTERNAL_ERROR,
            CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            MaterialText(
                "V2 shader interface identity could not be reconstructed." ),
            path );
    }
    work.shaderHash = canonical.hash;
    return tool_status_t::OK;
}

CYPHER_NODISCARD const render_shader_texture_interface_v2_view_t *
FindShaderTextureV2(
    const render_shader_source_v2_view_t &shader,
    string_view_t name ) noexcept
{
    for ( usize iTexture = 0u;
          iTexture < shader.nTextures;
          ++iTexture ) {
        if ( StringView_Equals( shader.textures[iTexture].name, name ) ) {
            return &shader.textures[iTexture];
        }
    }
    return nullptr;
}

CYPHER_NODISCARD const render_shader_parameter_interface_v2_view_t *
FindShaderParameterV2(
    const render_shader_source_v2_view_t &shader,
    string_view_t name ) noexcept
{
    for ( usize iParameter = 0u;
          iParameter < shader.nParameters;
          ++iParameter ) {
        if ( StringView_Equals( shader.parameters[iParameter].name, name ) ) {
            return &shader.parameters[iParameter];
        }
    }
    return nullptr;
}

CYPHER_NODISCARD const cooked_shader_binding_source_t *FindShaderBindingV2(
    const material_compile_work_t &work,
    string_view_t name,
    render_shader_binding_kind_t kind ) noexcept
{
    for ( u32 iBinding = 0u;
          iBinding < work.nShaderBindings;
          ++iBinding ) {
        const cooked_shader_binding_source_t &binding =
            work.shaderBindings[iBinding];
        if ( binding.kind == kind &&
             StringView_Equals( binding.name, name ) ) {
            return &binding;
        }
    }
    return nullptr;
}

CYPHER_NODISCARD material_texture_dependency_v2_t *
FindTextureDependencyV2(
    material_compile_work_t &work,
    string_view_t path ) noexcept
{
    for ( usize iDependency = 0u;
          iDependency < work.nTextureDependenciesV2;
          ++iDependency ) {
        if ( StringView_Equals(
                 work.textureDependenciesV2[iDependency].virtualPath,
                 path ) ) {
            return &work.textureDependenciesV2[iDependency];
        }
    }
    return nullptr;
}

CYPHER_NODISCARD tool_status_t LoadTextureDependencyV2(
    const tool_compile_request_t &request,
    tool_report_t &report,
    material_compile_work_t &work,
    string_view_t path,
    material_texture_dependency_v2_t **ppDependencyOut,
    tool_sequence_t sequence,
    u64 nCompleted ) noexcept
{
    if ( ppDependencyOut == nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    *ppDependencyOut = FindTextureDependencyV2( work, path );
    if ( *ppDependencyOut != nullptr ) {
        return tool_status_t::OK;
    }
    if ( work.nTextureDependenciesV2 >=
         CYPHER_ARRAY_COUNT( work.textureDependenciesV2 ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::CAPACITY_EXCEEDED,
            CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            MaterialText( "Material texture dependency capacity was exceeded." ),
            path );
    }

    text_buffer_t text{};
    if ( !TextBuffer_Init( &text, Allocator_GetSystem() ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            MaterialText(
                "Out of memory while validating a V2 texture." ),
            path );
    }
    usize cbRead = 0u;
    const material_text_read_status_t readStatus = ReadTextFile(
        request.pInvocation->pContext->pSourceVfs,
        path,
        text,
        cbRead );
    if ( readStatus != material_text_read_status_t::OK ) {
        const bool_t bOutOfMemory =
            readStatus == material_text_read_status_t::OUT_OF_MEMORY;
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            bOutOfMemory ? tool_status_t::OUT_OF_MEMORY
                         : tool_status_t::IO_ERROR,
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_READ_FAILED,
            tool_diagnostic_category_t::FILESYSTEM,
            readStatus == material_text_read_status_t::INVALID_TEXT
                ? MaterialText(
                      "Referenced texture is not bounded UTF-8 text." )
                : MaterialText( "Referenced texture could not be read." ),
            path );
    }
    report.cbRead += cbRead;

    key_value_document_owner_t document{};
    key_value_document_desc_t documentDesc{};
    documentDesc.pAllocator = Allocator_GetSystem();
    document.pDocument = KeyValue_CreateDocument( documentDesc );
    if ( document.pDocument == nullptr ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            MaterialText( "Out of memory while parsing a V2 texture." ),
            path );
    }
    const key_value_parse_result_t parsed = KeyValue_ParseText(
        TextBuffer_View( &text ),
        {},
        document.pDocument );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        EmitDiagnostic(
            request,
            report,
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_PARSE_FAILED,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::SOURCE,
            StringView_FromCString( KeyValue_ParseStatusName( parsed.status ) ),
            path,
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
    const key_value_document_header_t header =
        KeyValue_DocumentHeader( document.pDocument );
    if ( header.nSchemaVersion != CY_RENDER_ASSET_SCHEMA_VERSION_V2 ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT,
            tool_diagnostic_category_t::SCHEMA,
            MaterialText(
                "A V2 material requires V2 texture source contracts." ),
            path );
    }

    render_texture_source_v2_view_t texture{};
    schema_diagnostic_t diagnostics[CY_MATERIAL_COMPILER_SCHEMA_DIAGNOSTICS]{};
    const render_asset_decode_result_t decoded = RenderTextureSourceV2_Decode(
        document.pDocument,
        {},
        diagnostics,
        CYPHER_ARRAY_COUNT( diagnostics ),
        &texture );
    if ( !RenderAsset_DecodeSucceeded( decoded ) ) {
        if ( decoded.validation.nDiagnosticsWritten != 0u ) {
            EmitSchemaFailure(
                request,
                report,
                CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
                path,
                diagnostics[0],
                parsed );
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
            CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
            tool_diagnostic_category_t::SCHEMA,
            StringView_FromCString(
                RenderAsset_DecodeStatusName( decoded.status ) ),
            path );
    }

    const key_value_canonical_hash_result_t canonical =
        KeyValue_HashCanonicalDocument( document.pDocument );
    if ( canonical.status != key_value_write_status_t::OK ||
         !ContentHash_IsValid( canonical.hash ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INTERNAL_ERROR,
            CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            MaterialText( "V2 texture identity could not be generated." ),
            path );
    }

    material_texture_dependency_v2_t &dependency =
        work.textureDependenciesV2[work.nTextureDependenciesV2++];
    dependency.virtualPath = path;
    dependency.sourceHash = canonical.hash;
    dependency.type = texture.type;
    dependency.usage = texture.usage;
    dependency.colorSpace = texture.colorSpace;
    *ppDependencyOut = &dependency;
    return tool_status_t::OK;
}

CYPHER_NODISCARD tool_status_t PrepareTexturesV2(
    const tool_compile_request_t &request,
    tool_report_t &report,
    material_compile_work_t &work,
    tool_sequence_t sequence,
    u64 nCompleted ) noexcept
{
    for ( usize iTexture = 0u;
          iTexture < work.resolvedV2.nTextures;
          ++iTexture ) {
        const resolved_material_texture_v2_t &texture =
            work.resolvedV2.textures[iTexture];
        if ( FindShaderTextureV2( work.shaderV2, texture.binding ) == nullptr ) {
            return FailNamed(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::VALIDATION_FAILED,
                CY_MATERIAL_DIAGNOSTIC_UNKNOWN_BINDING,
                tool_diagnostic_category_t::VALIDATION,
                "Material references unknown shader texture binding",
                texture.binding,
                request.input );
        }
    }

    work.nTexturesV2 = 0u;
    for ( usize iShaderTexture = 0u;
          iShaderTexture < work.shaderV2.nTextures;
          ++iShaderTexture ) {
        const render_shader_texture_interface_v2_view_t &shaderTexture =
            work.shaderV2.textures[iShaderTexture];
        const usize iResolved = FindResolvedTextureV2(
            work.resolvedV2,
            shaderTexture.name );
        if ( iResolved == CY_INVALID_SIZE ) {
            if ( shaderTexture.bRequired ) {
                return FailNamed(
                    request,
                    report,
                    sequence,
                    nCompleted,
                    tool_status_t::VALIDATION_FAILED,
                    CY_MATERIAL_DIAGNOSTIC_MISSING_REQUIRED_BINDING,
                    tool_diagnostic_category_t::VALIDATION,
                    "Material omits required shader texture binding",
                    shaderTexture.name,
                    request.input );
            }
            continue;
        }
        const resolved_material_texture_v2_t &resolved =
            work.resolvedV2.textures[iResolved];
        if ( shaderTexture.type !=
             render_shader_texture_type_t::TEXTURE_2D ) {
            return FailNamed(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::VALIDATION_FAILED,
                CY_MATERIAL_DIAGNOSTIC_TEXTURE_SEMANTIC_MISMATCH,
                tool_diagnostic_category_t::VALIDATION,
                "Material textures currently require a 2D shader binding",
                shaderTexture.name,
                resolved.resource );
        }

        material_texture_dependency_v2_t *pDependency = nullptr;
        const tool_status_t loadStatus = LoadTextureDependencyV2(
            request,
            report,
            work,
            resolved.resource,
            &pDependency,
            sequence,
            nCompleted );
        if ( ToolStatus_Failed( loadStatus ) ) {
            return loadStatus;
        }
        if ( pDependency == nullptr ||
             pDependency->type != render_texture_type_t::TEXTURE_2D ||
             pDependency->usage != shaderTexture.usage ||
             pDependency->colorSpace != shaderTexture.colorSpace ) {
            return FailNamed(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::VALIDATION_FAILED,
                CY_MATERIAL_DIAGNOSTIC_TEXTURE_SEMANTIC_MISMATCH,
                tool_diagnostic_category_t::VALIDATION,
                "Texture type, usage, or color space mismatches shader binding",
                shaderTexture.name,
                resolved.resource );
        }

        const cooked_shader_binding_source_t *pBinding = FindShaderBindingV2(
            work,
            shaderTexture.name,
            render_shader_binding_kind_t::SAMPLED_TEXTURE );
        if ( pBinding == nullptr || work.nTexturesV2 >=
             CYPHER_ARRAY_COUNT( work.texturesV2 ) ) {
            return Fail(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::INTERNAL_ERROR,
                CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
                tool_diagnostic_category_t::INTERNAL,
                MaterialText(
                    "Shader texture reflection could not be reconstructed." ),
                work.resolvedV2.shader );
        }
        cooked_material_texture_source_v2_t &output =
            work.texturesV2[work.nTexturesV2++];
        output.binding = resolved.binding;
        output.texture = resolved.resource;
        output.sampler = resolved.bHasSampler
            ? resolved.sampler
            : string_view_t{};
        output.nLogicalBinding = pBinding->nLogicalBinding;
        output.nUvSet = resolved.uv.nSet;
        output.uvScale[0] = resolved.uv.scale[0];
        output.uvScale[1] = resolved.uv.scale[1];
        output.uvOffset[0] = resolved.uv.offset[0];
        output.uvOffset[1] = resolved.uv.offset[1];
        output.uvRotation = resolved.uv.rotation;
        output.bHasSampler = resolved.bHasSampler;
    }
    return tool_status_t::OK;
}

enum class material_parameter_value_status_t : u8 {
    OK = 0u,
    TYPE_MISMATCH,
    RANGE_ERROR
};

CYPHER_NODISCARD usize ShaderParameterComponentCountV2(
    render_shader_parameter_type_t type ) noexcept
{
    switch ( type ) {
        case render_shader_parameter_type_t::BOOL:
        case render_shader_parameter_type_t::I32:
        case render_shader_parameter_type_t::U32:
        case render_shader_parameter_type_t::F32:
            return 1u;
        case render_shader_parameter_type_t::F32X2:
            return 2u;
        case render_shader_parameter_type_t::F32X3:
        case render_shader_parameter_type_t::COLOR3:
            return 3u;
        case render_shader_parameter_type_t::F32X4:
        case render_shader_parameter_type_t::COLOR4:
            return 4u;
        case render_shader_parameter_type_t::MAT3:
            return 9u;
        case render_shader_parameter_type_t::MAT4:
            return 16u;
    }
    return 0u;
}

CYPHER_NODISCARD bool_t AssetValueAsF64V2(
    const render_asset_value_view_t &value,
    f64 &numberOut ) noexcept
{
    switch ( value.type ) {
        case render_asset_value_type_t::I64:
            numberOut = static_cast<f64>( value.iValue );
            return CY_TRUE;
        case render_asset_value_type_t::U64:
            numberOut = static_cast<f64>( value.uValue );
            return CY_TRUE;
        case render_asset_value_type_t::F64:
            numberOut = value.values[0];
            return CY_TRUE;
        default:
            return CY_FALSE;
    }
}

CYPHER_NODISCARD material_parameter_value_status_t ConvertParameterValueV2(
    const render_shader_parameter_interface_v2_view_t &shaderParameter,
    const cooked_shader_binding_source_t &binding,
    const render_asset_value_view_t &value,
    cooked_material_parameter_source_v2_t &output ) noexcept
{
    output = {};
    output.name = shaderParameter.name;
    output.nLogicalBinding = binding.nLogicalBinding;
    output.type = binding.valueType;
    output.iByteOffset = binding.iByteOffset;
    output.cbByteSize = binding.cbByteSize;

    f64 scalar = 0.0;
    switch ( shaderParameter.type ) {
        case render_shader_parameter_type_t::BOOL:
            if ( value.type != render_asset_value_type_t::BOOL ) {
                return material_parameter_value_status_t::TYPE_MISMATCH;
            }
            output.bValue = value.bValue;
            return material_parameter_value_status_t::OK;
        case render_shader_parameter_type_t::I32: {
            i64 integer = 0;
            if ( value.type == render_asset_value_type_t::I64 ) {
                integer = value.iValue;
            } else if ( value.type == render_asset_value_type_t::U64 ) {
                if ( value.uValue > static_cast<u64>( CY_I32_MAX ) ) {
                    return material_parameter_value_status_t::RANGE_ERROR;
                }
                integer = static_cast<i64>( value.uValue );
            } else {
                return material_parameter_value_status_t::TYPE_MISMATCH;
            }
            if ( integer < CY_I32_MIN || integer > CY_I32_MAX ) {
                return material_parameter_value_status_t::RANGE_ERROR;
            }
            scalar = static_cast<f64>( integer );
            output.signedValues[0] = static_cast<i32>( integer );
            break;
        }
        case render_shader_parameter_type_t::U32: {
            u64 integer = 0u;
            if ( value.type == render_asset_value_type_t::U64 ) {
                integer = value.uValue;
            } else if ( value.type == render_asset_value_type_t::I64 ) {
                if ( value.iValue < 0 ) {
                    return material_parameter_value_status_t::RANGE_ERROR;
                }
                integer = static_cast<u64>( value.iValue );
            } else {
                return material_parameter_value_status_t::TYPE_MISMATCH;
            }
            if ( integer > CY_U32_MAX ) {
                return material_parameter_value_status_t::RANGE_ERROR;
            }
            scalar = static_cast<f64>( integer );
            output.unsignedValues[0] = static_cast<u32>( integer );
            break;
        }
        case render_shader_parameter_type_t::F32:
            if ( !AssetValueAsF64V2( value, scalar ) ||
                 !std::isfinite( scalar ) ||
                 scalar < -static_cast<f64>( CY_F32_MAX ) ||
                 scalar > static_cast<f64>( CY_F32_MAX ) ||
                 !std::isfinite( static_cast<f32>( scalar ) ) ) {
                return value.type == render_asset_value_type_t::BOOL ||
                               value.type == render_asset_value_type_t::F64_ARRAY
                    ? material_parameter_value_status_t::TYPE_MISMATCH
                    : material_parameter_value_status_t::RANGE_ERROR;
            }
            output.floatingValues[0] = scalar;
            break;
        case render_shader_parameter_type_t::F32X2:
        case render_shader_parameter_type_t::F32X3:
        case render_shader_parameter_type_t::F32X4:
        case render_shader_parameter_type_t::COLOR3:
        case render_shader_parameter_type_t::COLOR4:
        case render_shader_parameter_type_t::MAT3:
        case render_shader_parameter_type_t::MAT4: {
            const usize nComponents = ShaderParameterComponentCountV2(
                shaderParameter.type );
            if ( value.type != render_asset_value_type_t::F64_ARRAY ||
                 value.nComponents != nComponents ) {
                return material_parameter_value_status_t::TYPE_MISMATCH;
            }
            for ( usize iComponent = 0u;
                  iComponent < nComponents;
                  ++iComponent ) {
                const f64 component = value.values[iComponent];
                if ( !std::isfinite( component ) ||
                     component < -static_cast<f64>( CY_F32_MAX ) ||
                     component > static_cast<f64>( CY_F32_MAX ) ||
                     !std::isfinite( static_cast<f32>( component ) ) ) {
                    return material_parameter_value_status_t::RANGE_ERROR;
                }
                output.floatingValues[iComponent] = component;
            }
            return material_parameter_value_status_t::OK;
        }
    }

    if ( ( shaderParameter.bHasMinimum &&
           scalar < shaderParameter.minimum ) ||
         ( shaderParameter.bHasMaximum &&
           scalar > shaderParameter.maximum ) ) {
        return material_parameter_value_status_t::RANGE_ERROR;
    }
    return material_parameter_value_status_t::OK;
}

CYPHER_NODISCARD tool_status_t PrepareParametersV2(
    const tool_compile_request_t &request,
    tool_report_t &report,
    material_compile_work_t &work,
    tool_sequence_t sequence,
    u64 nCompleted ) noexcept
{
    for ( usize iParameter = 0u;
          iParameter < work.resolvedV2.nParameters;
          ++iParameter ) {
        const resolved_material_parameter_v2_t &parameter =
            work.resolvedV2.parameters[iParameter];
        if ( FindShaderParameterV2(
                 work.shaderV2,
                 parameter.name ) == nullptr ) {
            return FailNamed(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::VALIDATION_FAILED,
                CY_MATERIAL_DIAGNOSTIC_UNKNOWN_BINDING,
                tool_diagnostic_category_t::VALIDATION,
                "Material references unknown shader parameter",
                parameter.name,
                request.input );
        }
    }

    work.nParametersV2 = 0u;
    for ( usize iShaderParameter = 0u;
          iShaderParameter < work.shaderV2.nParameters;
          ++iShaderParameter ) {
        const render_shader_parameter_interface_v2_view_t &shaderParameter =
            work.shaderV2.parameters[iShaderParameter];
        const usize iResolved = FindResolvedParameterV2(
            work.resolvedV2,
            shaderParameter.name );
        const render_asset_value_view_t *pValue = nullptr;
        if ( iResolved != CY_INVALID_SIZE ) {
            pValue = &work.resolvedV2.parameters[iResolved].value;
        } else if ( shaderParameter.bHasDefault ) {
            pValue = &shaderParameter.defaultValue;
        } else if ( shaderParameter.bRequired ) {
            return FailNamed(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::VALIDATION_FAILED,
                CY_MATERIAL_DIAGNOSTIC_MISSING_REQUIRED_BINDING,
                tool_diagnostic_category_t::VALIDATION,
                "Material omits required shader parameter",
                shaderParameter.name,
                request.input );
        } else {
            continue;
        }

        const cooked_shader_binding_source_t *pBinding = FindShaderBindingV2(
            work,
            shaderParameter.name,
            render_shader_binding_kind_t::VALUE );
        if ( pBinding == nullptr || work.nParametersV2 >=
             CYPHER_ARRAY_COUNT( work.parametersV2 ) ) {
            return Fail(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::INTERNAL_ERROR,
                CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
                tool_diagnostic_category_t::INTERNAL,
                MaterialText(
                    "Shader parameter reflection could not be reconstructed." ),
                work.resolvedV2.shader );
        }

        cooked_material_parameter_source_v2_t output{};
        const material_parameter_value_status_t valueStatus =
            ConvertParameterValueV2(
                shaderParameter,
                *pBinding,
                *pValue,
                output );
        if ( valueStatus != material_parameter_value_status_t::OK ) {
            return FailNamed(
                request,
                report,
                sequence,
                nCompleted,
                tool_status_t::VALIDATION_FAILED,
                valueStatus ==
                        material_parameter_value_status_t::TYPE_MISMATCH
                    ? CY_MATERIAL_DIAGNOSTIC_PARAMETER_TYPE_MISMATCH
                    : CY_MATERIAL_DIAGNOSTIC_PARAMETER_RANGE,
                tool_diagnostic_category_t::VALIDATION,
                valueStatus ==
                        material_parameter_value_status_t::TYPE_MISMATCH
                    ? "Material parameter has the wrong type or component count"
                    : "Material parameter is outside its declared range",
                shaderParameter.name,
                request.input );
        }
        work.parametersV2[work.nParametersV2++] = output;
    }
    return tool_status_t::OK;
}

CYPHER_NODISCARD content_hash_t CompilerHash(
    u32 nSchemaVersion,
    format_version_t nCookedVersion ) noexcept
{
    char identity[128]{};
    const string_format_result_t formatted = StringFormat_Printf(
        identity,
        sizeof( identity ),
        "cypher.material-compiler.api%u.compiler%u.schema%u.cymat%u",
        CY_MATERIAL_COMPILER_API_VERSION,
        CY_MATERIAL_COMPILER_VERSION,
        nSchemaVersion,
        nCookedVersion );
    return formatted.status == string_format_status_t::OK
        ? ContentHash_String( StringView_FromCString( identity ) )
        : CY_CONTENT_HASH_INVALID;
}

void EmitDependenciesV1(
    const tool_compile_request_t &request,
    const material_compile_work_t &work,
    content_hash_t recipeHash,
    content_hash_t compilerHash ) noexcept
{
    ToolHost_EmitDependency(
        request.pInvocation->pHost,
        { request.input,
          tool_dependency_kind_t::SOURCE,
          recipeHash,
          TOOL_DEPENDENCY_FLAG_REQUIRED } );
    ToolHost_EmitDependency(
        request.pInvocation->pHost,
        { work.recipe.shader,
          tool_dependency_kind_t::RESOURCE,
          work.shaderHash,
          TOOL_DEPENDENCY_FLAG_REQUIRED } );
    for ( usize iTexture = 0u;
          iTexture < work.recipe.nTextures;
          ++iTexture ) {
        ToolHost_EmitDependency(
            request.pInvocation->pHost,
            { work.recipe.textures[iTexture].texture,
              tool_dependency_kind_t::RESOURCE,
              work.textureHashes[iTexture],
              TOOL_DEPENDENCY_FLAG_REQUIRED } );
    }
    ToolHost_EmitDependency(
        request.pInvocation->pHost,
        { MaterialText( "toolchain/cypher-material-compiler" ),
          tool_dependency_kind_t::TOOLCHAIN,
          compilerHash,
          TOOL_DEPENDENCY_FLAG_REQUIRED } );
}

void SortTextureDependenciesV2( material_compile_work_t &work ) noexcept
{
    for ( usize iDependency = 1u;
          iDependency < work.nTextureDependenciesV2;
          ++iDependency ) {
        const material_texture_dependency_v2_t value =
            work.textureDependenciesV2[iDependency];
        usize iInsert = iDependency;
        while ( iInsert != 0u &&
                StringView_Compare(
                    value.virtualPath,
                    work.textureDependenciesV2[
                        iInsert - 1u ].virtualPath ) < 0 ) {
            work.textureDependenciesV2[iInsert] =
                work.textureDependenciesV2[iInsert - 1u];
            --iInsert;
        }
        work.textureDependenciesV2[iInsert] = value;
    }
}

CYPHER_NODISCARD content_hash_t BuildSourceHashV2(
    const material_compile_work_t &work,
    content_hash_t compilerHash ) noexcept
{
    content_hash_t sourceHash = compilerHash;
    for ( usize iBase = work.nBaseMaterials;
          iBase != 0u;
          --iBase ) {
        const material_base_file_v2_t &base =
            work.baseMaterials[iBase - 1u];
        sourceHash = ContentHash_Combine( sourceHash, base.sourceHash );
    }
    sourceHash = ContentHash_Combine( sourceHash, work.recipeHash );
    sourceHash = ContentHash_Combine( sourceHash, work.shaderHash );
    sourceHash = ContentHash_Combine(
        sourceHash,
        work.shaderInterfaceHash );
    for ( usize iDependency = 0u;
          iDependency < work.nTextureDependenciesV2;
          ++iDependency ) {
        const material_texture_dependency_v2_t &dependency =
            work.textureDependenciesV2[iDependency];
        sourceHash = ContentHash_Combine(
            sourceHash,
            dependency.sourceHash );
    }
    return sourceHash;
}

void EmitDependenciesV2(
    const tool_compile_request_t &request,
    const material_compile_work_t &work,
    content_hash_t compilerHash ) noexcept
{
    ToolHost_EmitDependency(
        request.pInvocation->pHost,
        { request.input,
          tool_dependency_kind_t::SOURCE,
          work.recipeHash,
          TOOL_DEPENDENCY_FLAG_REQUIRED } );
    for ( usize iBase = 0u;
          iBase < work.nBaseMaterials;
          ++iBase ) {
        const material_base_file_v2_t &base = work.baseMaterials[iBase];
        ToolHost_EmitDependency(
            request.pInvocation->pHost,
            { base.virtualPath,
              tool_dependency_kind_t::RESOURCE,
              base.sourceHash,
              TOOL_DEPENDENCY_FLAG_REQUIRED |
                  ( iBase == 0u
                        ? TOOL_DEPENDENCY_FLAG_NONE
                        : TOOL_DEPENDENCY_FLAG_TRANSITIVE ) } );
    }
    ToolHost_EmitDependency(
        request.pInvocation->pHost,
        { work.resolvedV2.shader,
          tool_dependency_kind_t::RESOURCE,
          work.shaderHash,
          TOOL_DEPENDENCY_FLAG_REQUIRED } );
    for ( usize iDependency = 0u;
          iDependency < work.nTextureDependenciesV2;
          ++iDependency ) {
        const material_texture_dependency_v2_t &dependency =
            work.textureDependenciesV2[iDependency];
        ToolHost_EmitDependency(
            request.pInvocation->pHost,
            { dependency.virtualPath,
              tool_dependency_kind_t::RESOURCE,
              dependency.sourceHash,
              TOOL_DEPENDENCY_FLAG_REQUIRED } );
    }
    ToolHost_EmitDependency(
        request.pInvocation->pHost,
        { MaterialText( "toolchain/cypher-material-compiler" ),
          tool_dependency_kind_t::TOOLCHAIN,
          compilerHash,
          TOOL_DEPENDENCY_FLAG_REQUIRED } );
}

CYPHER_NODISCARD tool_status_t ExecuteMaterialCompilerV2(
    const tool_compile_request_t &request,
    tool_report_t &report,
    material_compile_work_t &work,
    tool_sequence_t sequence,
    u64 nCompleted,
    bool_t bDryRun ) noexcept
{
    tool_status_t status = LoadBaseMaterialsV2(
        request,
        report,
        work,
        sequence,
        nCompleted );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    status = ResolveMaterialV2(
        request,
        report,
        work,
        sequence,
        nCompleted );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    if ( work.resolvedV2.bSawFeatureOperation ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT,
            tool_diagnostic_category_t::VALIDATION,
            MaterialText(
                "Material feature overrides require a cooked shader variant table." ),
            request.input );
    }
    if ( work.resolvedV2.bHasSurface ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_MATERIAL_DIAGNOSTIC_UNSUPPORTED_CONTRACT,
            tool_diagnostic_category_t::VALIDATION,
            MaterialText(
                "Material surfaces require a versioned surface compiler and runtime contract." ),
            request.input );
    }
    status = LoadShaderV2(
        request,
        report,
        work,
        sequence,
        nCompleted );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    status = PrepareTexturesV2(
        request,
        report,
        work,
        sequence,
        nCompleted );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    status = PrepareParametersV2(
        request,
        report,
        work,
        sequence,
        nCompleted );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    SortTextureDependenciesV2( work );

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        MaterialText( "Build cooked resource" ) );
    if ( IsCancellationRequested( request, report, sequence, nCompleted ) ) {
        return tool_status_t::CANCELLED;
    }

    flags32_t flags = COOKED_MATERIAL_FLAG_NONE;
    if ( work.resolvedV2.bTwoSided ) {
        flags |= COOKED_MATERIAL_FLAG_TWO_SIDED;
    }
    if ( work.resolvedV2.bCastsShadows ) {
        flags |= COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    }
    if ( work.resolvedV2.bReceivesShadows ) {
        flags |= COOKED_MATERIAL_FLAG_RECEIVES_SHADOWS;
    }
    const cooked_material_source_v2_t material{
        work.resolvedV2.shader,
        {},
        work.shaderInterfaceHash,
        {},
        work.resolvedV2.domain,
        work.resolvedV2.alphaMode,
        work.resolvedV2.alphaCutoff,
        { work.featuresV2, 0u },
        { work.texturesV2, work.nTexturesV2 },
        { work.parametersV2, work.nParametersV2 },
        flags,
        CY_FALSE
    };
    const content_hash_t compilerHash = CompilerHash(
        CY_RENDER_ASSET_SCHEMA_VERSION_V2,
        CY_COOKED_MATERIAL_RESOURCE_VERSION_V2 );
    const content_hash_t sourceHash = BuildSourceHashV2(
        work,
        compilerHash );
    if ( !ContentHash_IsValid( compilerHash ) ||
         !ContentHash_IsValid( sourceHash ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INTERNAL_ERROR,
            CY_MATERIAL_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            MaterialText( "V2 material build identity could not be generated." ),
            request.input );
    }

    const usize cbCooked = CookedMaterial_RequiredSizeV2( material );
    if ( cbCooked == 0u ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_MATERIAL_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            MaterialText( "V2 cooked material could not be sized." ),
            request.input );
    }
    if ( !Blob_Resize( &work.cooked, cbCooked ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_MATERIAL_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            MaterialText( "V2 cooked material storage could not be allocated." ),
            request.input );
    }
    const cooked_material_result_t cooked = CookedMaterial_WriteV2(
        material,
        sourceHash,
        Blob_WritableSpan( &work.cooked ) );
    if ( !CookedMaterial_Succeeded( cooked ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::VALIDATION_FAILED,
            CY_MATERIAL_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            StringView_FromCString(
                CookedMaterial_StatusName( cooked.status ) ),
            request.input );
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        bDryRun ? MaterialText( "Validate output" )
                : MaterialText( "Write output" ) );
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
            CY_MATERIAL_DIAGNOSTIC_WRITE_FAILED,
            tool_diagnostic_category_t::FILESYSTEM,
            MaterialText( "Cooked material output could not be written." ),
            request.output );
    }

    EmitDependenciesV2( request, work, compilerHash );
    if ( !bDryRun ) {
        const tool_artifact_t artifact{
            request.output,
            MaterialText( "application/x-cypher-material" ),
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
        bDryRun ? MaterialText( "Validated" )
                : MaterialText( "Compiled" ) );
    return tool_status_t::OK;
}

CYPHER_NODISCARD bool_t ProbeMaterial(
    string_view_t input,
    void * ) noexcept
{
    return StringPath_HasExtension(
        input,
        MaterialText( ".cymat" ),
        CY_TRUE );
}

CYPHER_NODISCARD tool_status_t ExecuteMaterialCompiler(
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
        MaterialText( "Read recipe" ) );

    const bool_t bDryRun =
        ( request.pInvocation->flags & TOOL_INVOCATION_FLAG_DRY_RUN ) != 0u;
    const bool_t bInputPathValid = DataValidation_Succeeded(
        DataValidation_CheckResourcePath(
            request.input,
            MaterialText( ".cymat" ),
            CY_MATERIAL_COMPILER_MAX_PATH ) );
    const bool_t bOutputPathValid = bDryRun && request.output.cchLength == 0u
        ? CY_TRUE
        : DataValidation_Succeeded(
              DataValidation_CheckResourcePath(
                  request.output,
                  MaterialText( ".cymat_c" ),
                  CY_MATERIAL_COMPILER_MAX_PATH ) );
    if ( !bInputPathValid || !bOutputPathValid ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INVALID_ARGUMENT,
            CY_MATERIAL_DIAGNOSTIC_INVALID_PATH,
            tool_diagnostic_category_t::VALIDATION,
            MaterialText(
                "Material input and output must be canonical virtual resource paths." ),
            !bInputPathValid ? request.input : request.output );
    }

    material_compile_work_t work{};
    if ( !InitCompileWork( work ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_category_t::INTERNAL,
            MaterialText( "Out of memory while initializing material compilation." ) );
    }
    const tool_context_t &context = *request.pInvocation->pContext;
    if ( !Vfs_IsValid( context.pSourceVfs ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INVALID_CONFIGURATION,
            CY_MATERIAL_DIAGNOSTIC_INVALID_PATH,
            tool_diagnostic_category_t::FILESYSTEM,
            MaterialText( "Material compilation requires a valid source VFS." ),
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
            CY_MATERIAL_DIAGNOSTIC_INVALID_PATH,
            tool_diagnostic_category_t::FILESYSTEM,
            MaterialText( "Material output could not be resolved below its root." ),
            request.output );
    }
    if ( IsCancellationRequested( request, report, sequence, nCompleted ) ) {
        return tool_status_t::CANCELLED;
    }

    usize cbRead = 0u;
    const material_text_read_status_t readStatus = ReadTextFile(
        context.pSourceVfs,
        request.input,
        work.recipeText,
        cbRead );
    if ( readStatus != material_text_read_status_t::OK ) {
        const bool_t bOutOfMemory =
            readStatus == material_text_read_status_t::OUT_OF_MEMORY;
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            bOutOfMemory ? tool_status_t::OUT_OF_MEMORY
                         : tool_status_t::IO_ERROR,
            readStatus == material_text_read_status_t::INVALID_TEXT
                ? CY_MATERIAL_DIAGNOSTIC_INVALID_TEXT
                : CY_MATERIAL_DIAGNOSTIC_READ_FAILED,
            tool_diagnostic_category_t::FILESYSTEM,
            readStatus == material_text_read_status_t::INVALID_TEXT
                ? MaterialText( "Material recipe is not bounded UTF-8 text." )
                : MaterialText( "Material recipe could not be read." ),
            request.input );
    }
    report.cbRead += cbRead;

    const tool_status_t parseStatus = ParseMaterialRecipe(
        request,
        report,
        work,
        sequence,
        nCompleted );
    if ( ToolStatus_Failed( parseStatus ) ) {
        return parseStatus;
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        MaterialText( "Validate resource references" ) );
    if ( IsCancellationRequested( request, report, sequence, nCompleted ) ) {
        return tool_status_t::CANCELLED;
    }

    if ( work.nSchemaVersion == CY_RENDER_ASSET_SCHEMA_VERSION_V2 ) {
        return ExecuteMaterialCompilerV2(
            request,
            report,
            work,
            sequence,
            nCompleted,
            bDryRun );
    }

    tool_status_t dependencyStatus = ValidateDependencyRecipe(
        request,
        report,
        work.recipe.shader,
        material_dependency_kind_t::SHADER,
        work.shaderHash,
        sequence,
        nCompleted );
    if ( ToolStatus_Failed( dependencyStatus ) ) {
        return dependencyStatus;
    }
    for ( usize iTexture = 0u;
          iTexture < work.recipe.nTextures;
          ++iTexture ) {
        dependencyStatus = ValidateDependencyRecipe(
            request,
            report,
            work.recipe.textures[iTexture].texture,
            material_dependency_kind_t::TEXTURE,
            work.textureHashes[iTexture],
            sequence,
            nCompleted );
        if ( ToolStatus_Failed( dependencyStatus ) ) {
            return dependencyStatus;
        }
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        MaterialText( "Build cooked resource" ) );
    for ( usize iTexture = 0u;
          iTexture < work.recipe.nTextures;
          ++iTexture ) {
        work.textures[iTexture] = {
            work.recipe.textures[iTexture].binding,
            work.recipe.textures[iTexture].texture
        };
    }
    for ( usize iParameter = 0u;
          iParameter < work.recipe.nParameters;
          ++iParameter ) {
        const render_material_parameter_view_t &source =
            work.recipe.parameters[iParameter];
        cooked_material_parameter_source_t &parameter =
            work.parameters[iParameter];
        parameter.name = source.name;
        parameter.type = source.type;
        parameter.bValue = source.bValue;
        parameter.nComponents = static_cast<u32>( source.nComponents );
        for ( usize iValue = 0u;
              iValue < CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS;
              ++iValue ) {
            parameter.values[iValue] = source.values[iValue];
        }
    }

    const cooked_material_source_t material{
        work.recipe.shader,
        { work.textures, work.recipe.nTextures },
        { work.parameters, work.recipe.nParameters },
        COOKED_MATERIAL_FLAG_NONE
    };
    const content_hash_t recipeHash = work.recipeHash;
    const content_hash_t compilerHash = CompilerHash(
        CY_RENDER_ASSET_SCHEMA_VERSION_V1,
        CY_COOKED_MATERIAL_RESOURCE_VERSION_V1 );
    content_hash_t sourceHash = ContentHash_Combine(
        compilerHash,
        recipeHash );
    sourceHash = ContentHash_Combine( sourceHash, work.shaderHash );
    for ( usize iTexture = 0u;
          iTexture < work.recipe.nTextures;
          ++iTexture ) {
        sourceHash = ContentHash_Combine(
            sourceHash,
            work.textureHashes[iTexture] );
    }
    if ( !ContentHash_IsValid( recipeHash ) ||
         !ContentHash_IsValid( sourceHash ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INTERNAL_ERROR,
            CY_MATERIAL_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            MaterialText( "Material build identity could not be generated." ),
            request.input );
    }
    const usize cbCooked = CookedMaterial_RequiredSize( material );
    if ( cbCooked == 0u ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INTERNAL_ERROR,
            CY_MATERIAL_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            MaterialText( "Cooked material size could not be calculated." ),
            request.input );
    }
    if ( !Blob_Resize( &work.cooked, cbCooked ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::OUT_OF_MEMORY,
            CY_MATERIAL_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            MaterialText( "Cooked material storage could not be allocated." ),
            request.input );
    }
    const cooked_material_result_t cooked = CookedMaterial_Write(
        material,
        sourceHash,
        Blob_WritableSpan( &work.cooked ) );
    if ( !CookedMaterial_Succeeded( cooked ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            tool_status_t::INTERNAL_ERROR,
            CY_MATERIAL_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            StringView_FromCString(
                CookedMaterial_StatusName( cooked.status ) ),
            request.input );
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        bDryRun ? MaterialText( "Validate output" )
                : MaterialText( "Write output" ) );
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
            CY_MATERIAL_DIAGNOSTIC_WRITE_FAILED,
            tool_diagnostic_category_t::FILESYSTEM,
            MaterialText( "Cooked material output could not be written." ),
            request.output );
    }

    EmitDependenciesV1(
        request,
        work,
        recipeHash,
        compilerHash );
    if ( !bDryRun ) {
        const tool_artifact_t artifact{
            request.output,
            MaterialText( "application/x-cypher-material" ),
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
        bDryRun ? MaterialText( "Validated" )
                : MaterialText( "Compiled" ) );
    return tool_status_t::OK;
}

inline constexpr string_view_t g_materialSourceExtensions[]{
    MaterialText( ".cymat" )
};

const tool_compiler_desc_t g_materialCompiler{
    MaterialText( "cypher.material" ),
    MaterialText( "Cypher Material Compiler" ),
    MaterialText( "material" ),
    MaterialText( ".cymat_c" ),
    g_materialSourceExtensions,
    CYPHER_ARRAY_COUNT( g_materialSourceExtensions ),
    CY_MATERIAL_COMPILER_API_VERSION,
    CY_MATERIAL_COMPILER_VERSION,
    TOOL_COMPILER_FLAG_DETERMINISTIC |
        TOOL_COMPILER_FLAG_THREAD_SAFE |
        TOOL_COMPILER_FLAG_SUPPORTS_VALIDATE |
        TOOL_COMPILER_FLAG_SUPPORTS_DRY_RUN,
    &ProbeMaterial,
    &ExecuteMaterialCompiler,
    nullptr
};

} // namespace

const tool_compiler_desc_t *CypherMaterialCompiler_Descriptor() noexcept
{
    return &g_materialCompiler;
}

} // namespace cypher::tools
