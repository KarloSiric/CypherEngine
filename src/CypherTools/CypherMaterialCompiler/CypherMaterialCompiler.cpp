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
#include "CypherCommon_RenderAsset.h"
#include "CypherCommon_StringFormat.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_TextBuffer.h"
#include "CypherCommon_ToolArtifactWriter.h"
#include "CypherCommon_ToolFramework.h"
#include "CypherCommon_Unicode.h"
#include "CypherCommon_Vfs.h"

namespace cypher::tools
{

using namespace cypher::common;

namespace
{

inline constexpr usize CY_MATERIAL_COMPILER_MAX_PATH = 259u;
inline constexpr usize CY_MATERIAL_COMPILER_MAX_RECIPE_SIZE = 1u * CY_MIB;
inline constexpr usize CY_MATERIAL_COMPILER_SCHEMA_DIAGNOSTICS = 32u;
inline constexpr u64 CY_MATERIAL_COMPILER_PROGRESS_STEPS = 4u;

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

struct material_compile_work_t {
    text_buffer_t outputNativePath{};
    text_buffer_t recipeText{};
    key_value_document_owner_t document{};
    render_material_source_view_t recipe{};
    cooked_material_texture_source_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    cooked_material_parameter_source_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    content_hash_t textureHashes[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
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
    const render_asset_decode_result_t decoded = RenderMaterialSource_Decode(
        work.document.pDocument,
        {},
        diagnostics,
        CYPHER_ARRAY_COUNT( diagnostics ),
        &work.recipe );
    if ( RenderAsset_DecodeSucceeded( decoded ) ) {
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
    hashOut = ContentHash_String( TextBuffer_View( &text ) );

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

CYPHER_NODISCARD content_hash_t CompilerHash() noexcept
{
    const u32 identity[]{
        CY_MATERIAL_COMPILER_API_VERSION,
        CY_MATERIAL_COMPILER_VERSION,
        CY_RENDER_MATERIAL_RESOURCE_VERSION
    };
    return ContentHash_Data( BinaryBlock_FromData( identity, sizeof( identity ) ) );
}

void EmitDependencies(
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
    const content_hash_t recipeHash = ContentHash_String(
        TextBuffer_View( &work.recipeText ) );
    const content_hash_t compilerHash = CompilerHash();
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
    const usize cbCooked = CookedMaterial_RequiredSize( material );
    if ( !ContentHash_IsValid( sourceHash ) || cbCooked == 0u ||
         !Blob_Resize( &work.cooked, cbCooked ) ) {
        return Fail(
            request,
            report,
            sequence,
            nCompleted,
            cbCooked != 0u ? tool_status_t::OUT_OF_MEMORY
                           : tool_status_t::INTERNAL_ERROR,
            CY_MATERIAL_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_category_t::COMPILER,
            MaterialText( "Cooked material could not be sized or allocated." ),
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

    EmitDependencies(
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
