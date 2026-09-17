//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp
//  Purpose: Implements deterministic OpenGL shader recipe compilation.
//  Details: The compiler parses CYKV, validates the exact shader schema, loads
//           stage dependencies, preprocesses and links GLSL with glslang, and
//           writes the canonical CYSH/CYRS runtime resource. Quoted GLSL includes
//           resolve through the source VFS and become explicit build dependencies.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherShaderCompiler.h"

#include "CypherCommon_Align.h"
#include "CypherCommon_Allocator.h"
#include "CypherCommon_Blob.h"
#include "CypherCommon_CookedShader.h"
#include "CypherCommon_DataValidation.h"
#include "CypherCommon_FileIo.h"
#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_KeyValueWriter.h"
#include "CypherCommon_RenderAsset.h"
#include "CypherCommon_String.h"
#include "CypherCommon_StringFormat.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_TextBuffer.h"
#include "CypherCommon_ToolArtifactWriter.h"
#include "CypherCommon_Unicode.h"
#include "CypherCommon_Vfs.h"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_cross_c.h>

#include <cstring>
#include <exception>
#include <new>
#include <string>

namespace cypher::tools
{

using namespace cypher::common;

namespace
{

inline constexpr usize CY_SHADER_COMPILER_MAX_RECIPE_SIZE = 1u * CY_MIB;
inline constexpr usize CY_SHADER_COMPILER_MAX_STAGE_SIZE =
    static_cast<usize>( CY_COOKED_SHADER_MAX_CODE_SIZE ) - 1u;
inline constexpr usize CY_SHADER_COMPILER_MAX_PATH = 4095u;
inline constexpr usize CY_SHADER_COMPILER_SCHEMA_DIAGNOSTICS = 32u;
inline constexpr usize CY_SHADER_COMPILER_MAX_INCLUDE_FILES = 128u;
inline constexpr usize CY_SHADER_COMPILER_MAX_INCLUDE_REQUESTS = 256u;
inline constexpr usize CY_SHADER_COMPILER_MAX_INCLUDE_DEPTH = 32u;
inline constexpr usize CY_SHADER_COMPILER_MAX_INCLUDE_SIZE = 1u * CY_MIB;
inline constexpr usize CY_SHADER_COMPILER_MAX_INCLUDE_BYTES = 8u * CY_MIB;
inline constexpr u64 CY_SHADER_COMPILER_PROGRESS_STEPS = 5u;

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t ShaderText(
    const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

enum class shader_text_read_status_t : u8 {
    OK = 0u,
    IO_ERROR,
    EMPTY,
    SIZE_LIMIT,
    EMBEDDED_NUL,
    INVALID_UTF8,
    OUT_OF_MEMORY
};

struct key_value_document_owner_t {
    key_value_document_t *pDocument{ nullptr };

    key_value_document_owner_t() noexcept = default;
    key_value_document_owner_t( const key_value_document_owner_t & ) = delete;
    key_value_document_owner_t &operator=(
        const key_value_document_owner_t & ) = delete;

    ~key_value_document_owner_t() noexcept
    {
        if ( pDocument != nullptr ) {
            KeyValue_DestroyDocument( pDocument );
        }
    }
};

struct glslang_shader_owner_t {
    glslang_shader_t *pShader{ nullptr };

    glslang_shader_owner_t() noexcept = default;
    glslang_shader_owner_t( const glslang_shader_owner_t & ) = delete;
    glslang_shader_owner_t &operator=(
        const glslang_shader_owner_t & ) = delete;

    ~glslang_shader_owner_t() noexcept
    {
        if ( pShader != nullptr ) {
            glslang_shader_delete( pShader );
        }
    }
};

struct glslang_program_owner_t {
    glslang_program_t *pProgram{ nullptr };

    glslang_program_owner_t() noexcept = default;
    glslang_program_owner_t( const glslang_program_owner_t & ) = delete;
    glslang_program_owner_t &operator=(
        const glslang_program_owner_t & ) = delete;

    ~glslang_program_owner_t() noexcept
    {
        if ( pProgram != nullptr ) {
            glslang_program_delete( pProgram );
        }
    }
};

struct glslang_runtime_t {
    bool_t bInitialized{ CY_FALSE };

    glslang_runtime_t() noexcept
        : bInitialized( glslang_initialize_process() ? CY_TRUE : CY_FALSE )
    {
    }

    ~glslang_runtime_t() noexcept
    {
        if ( bInitialized ) {
            glslang_finalize_process();
        }
    }
};

enum class shader_include_status_t : u8 {
    OK = 0u,
    SYSTEM_INCLUDE_UNSUPPORTED,
    INVALID_PATH,
    DEPTH_LIMIT,
    FILE_LIMIT,
    REQUEST_LIMIT,
    TOTAL_SIZE_LIMIT,
    READ_FAILED,
    INVALID_TEXT,
    OUT_OF_MEMORY
};

struct shader_include_file_t {
    text_buffer_t virtualPath{};
    text_buffer_t source{};
    content_hash_t sourceHash{};
};

struct shader_include_cache_t {
    const vfs_t *pVfs{ nullptr };
    shader_include_file_t files[CY_SHADER_COMPILER_MAX_INCLUDE_FILES]{};
    usize nFiles{ 0u };
    usize cbRead{ 0u };
};

struct shader_include_context_t {
    shader_include_cache_t *pCache{ nullptr };
    tool_report_t *pReport{ nullptr };
    string_view_t rootVirtualPath{};
    glsl_include_result_t results[CY_SHADER_COMPILER_MAX_INCLUDE_REQUESTS]{};
    usize nResults{ 0u };
    shader_include_status_t status{ shader_include_status_t::OK };
    shader_text_read_status_t textReadStatus{ shader_text_read_status_t::OK };
    vfs_status_t vfsStatus{ vfs_status_t::OK };
    text_buffer_t failurePath{};
};

struct shader_stage_work_t {
    string_view_t virtualPath{};
    text_buffer_t diagnosticPath{};
    text_buffer_t source{};
    text_buffer_t preprocessed{};
    content_hash_t sourceHash{};
    render_shader_stage_t stage{ render_shader_stage_t::VERTEX };
    u32 nGlslVersion{ 0u };
    u32 nGlslDirectiveLine{ 1u };
    u32 nGlslDirectiveColumn{ 1u };
    glslang_stage_t glslangStage{ GLSLANG_STAGE_VERTEX };
    glslang_shader_owner_t compiler{};
    shader_include_context_t includes{};
};

struct shader_recipe_work_t {
    u32 nSchemaVersion{ 0u };
    format_version_t nCookedVersion{ 0u };
    render_shader_language_t language{ render_shader_language_t::GLSL };
    string_view_t vertexSource{};
    string_view_t fragmentSource{};
    string_view_t vertexEntry{};
    string_view_t fragmentEntry{};
    const string_view_t *pDefines{ nullptr };
    usize nDefines{ 0u };
};

struct shader_compile_work_t {
    text_buffer_t recipeDiagnosticPath{};
    text_buffer_t outputNativePath{};
    text_buffer_t recipeText{};
    text_buffer_t definePreamble{};
    key_value_document_owner_t document{};
    render_shader_source_view_t recipeV1{};
    render_shader_source_v2_view_t recipeV2{};
    shader_recipe_work_t recipe{};
    cooked_shader_binding_source_t
        bindings[CY_COOKED_SHADER_MAX_BINDINGS]{};
    flags32_t reflectedStageMasks[CY_COOKED_SHADER_MAX_BINDINGS]{};
    u32 nBindings{ 0u };
    content_hash_t interfaceHash{};
    shader_include_cache_t includeCache{};
    shader_stage_work_t stages[CY_COOKED_SHADER_MAX_STAGES]{};
    blob_t cooked{};
};

CYPHER_NODISCARD glslang_runtime_t &GlslangRuntime() noexcept
{
    // Function-local initialization gives the glslang process one lifecycle even
    // when multiple compiler requests arrive from parallel ResourceCompiler jobs.
    static glslang_runtime_t runtime{};
    return runtime;
}

CYPHER_NODISCARD bool_t InitTextBuffer(
    text_buffer_t &buffer,
    usize cchInitialCapacity = 0u ) noexcept
{
    return TextBuffer_Init(
        &buffer,
        Allocator_GetSystem(),
        cchInitialCapacity );
}

CYPHER_NODISCARD bool_t InitCompileWork(
    shader_compile_work_t &work ) noexcept
{
    if ( !InitTextBuffer( work.recipeDiagnosticPath ) ||
         !InitTextBuffer( work.outputNativePath ) ||
         !InitTextBuffer( work.recipeText ) ||
         !InitTextBuffer( work.definePreamble ) ||
         !Blob_Init( &work.cooked, Allocator_GetSystem() ) ) {
        return CY_FALSE;
    }
    for ( shader_stage_work_t &stage : work.stages ) {
        if ( !InitTextBuffer( stage.diagnosticPath ) ||
             !InitTextBuffer( stage.source ) ||
             !InitTextBuffer( stage.preprocessed ) ||
             !InitTextBuffer( stage.includes.failurePath ) ) {
            return CY_FALSE;
        }
        stage.includes.pCache = &work.includeCache;
    }
    for ( shader_include_file_t &file : work.includeCache.files ) {
        if ( !InitTextBuffer( file.virtualPath ) ||
             !InitTextBuffer( file.source ) ) {
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

CYPHER_NODISCARD shader_text_read_status_t ReadTextFile(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    usize cbMaximum,
    text_buffer_t &textOut,
    vfs_status_t *pVfsStatusOut ) noexcept
{
    if ( pVfsStatusOut != nullptr ) {
        *pVfsStatusOut = vfs_status_t::OK;
    }
    blob_t bytes{};
    if ( !Blob_Init( &bytes, Allocator_GetSystem() ) ) {
        return shader_text_read_status_t::OUT_OF_MEMORY;
    }
    const vfs_status_t vfsStatus = Vfs_ReadAll(
        pVfs,
        virtualPath,
        cbMaximum,
        &bytes );
    if ( pVfsStatusOut != nullptr ) {
        *pVfsStatusOut = vfsStatus;
    }
    if ( vfsStatus == vfs_status_t::OUT_OF_MEMORY ) {
        return shader_text_read_status_t::OUT_OF_MEMORY;
    }
    if ( vfsStatus == vfs_status_t::SIZE_LIMIT ) {
        return shader_text_read_status_t::SIZE_LIMIT;
    }
    if ( vfsStatus != vfs_status_t::OK ) {
        return shader_text_read_status_t::IO_ERROR;
    }
    if ( bytes.cbSize == 0u ) {
        return shader_text_read_status_t::EMPTY;
    }
    if ( bytes.cbSize > cbMaximum ) {
        return shader_text_read_status_t::SIZE_LIMIT;
    }

    for ( usize iByte = 0u; iByte < bytes.cbSize; ++iByte ) {
        if ( bytes.pData[iByte] == static_cast<byte>( '\0' ) ) {
            return shader_text_read_status_t::EMBEDDED_NUL;
        }
    }

    const string_view_t text{
        reinterpret_cast<const char *>( bytes.pData ),
        bytes.cbSize
    };
    if ( Unicode_ValidateUtf8( text ).status != unicode_status_t::OK ) {
        return shader_text_read_status_t::INVALID_UTF8;
    }
    return TextBuffer_Assign( &textOut, text )
        ? shader_text_read_status_t::OK
        : shader_text_read_status_t::OUT_OF_MEMORY;
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
    progress.nTotal = CY_SHADER_COMPILER_PROGRESS_STEPS;
    progress.timestamp = Cy_TimerNowTicks();
    progress.title = ShaderText( "Compile shader" );
    progress.detail = detail;
    ToolHost_EmitProgress( request.pInvocation->pHost, progress );
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
        ShaderText( "Cancelled" ) );
    return CY_TRUE;
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
        ShaderText( "Failed" ) );
}

void MarkFailed( tool_report_t &report ) noexcept
{
    report.nInputsProcessed = 1u;
    report.nFailed = 1u;
}

CYPHER_NODISCARD tool_status_t ReportReadFailure(
    const tool_compile_request_t &request,
    tool_report_t &report,
    string_view_t virtualPath,
    string_view_t diagnosticPath,
    shader_text_read_status_t status,
    vfs_status_t vfsStatus,
    bool_t bRecipe ) noexcept
{
    const tool_diagnostic_code_t code =
        status == shader_text_read_status_t::IO_ERROR
            ? CY_SHADER_DIAGNOSTIC_READ_FAILED
            : CY_SHADER_DIAGNOSTIC_INVALID_TEXT;
    const tool_status_t toolStatus =
        status == shader_text_read_status_t::OUT_OF_MEMORY
            ? tool_status_t::OUT_OF_MEMORY
        : status == shader_text_read_status_t::IO_ERROR
            ? tool_status_t::IO_ERROR
            : tool_status_t::VALIDATION_FAILED;
    char messageStorage[256]{};
    const string_format_result_t formatted = status ==
            shader_text_read_status_t::IO_ERROR
        ? StringFormat_Printf(
              messageStorage,
              sizeof( messageStorage ),
              "%s could not be read through the source VFS (%s).",
              bRecipe ? "Shader recipe" : "GLSL stage",
              Vfs_StatusName( vfsStatus ) )
        : string_format_result_t{};
    const string_view_t message =
        status == shader_text_read_status_t::IO_ERROR &&
        formatted.status == string_format_status_t::OK
            ? string_view_t{ messageStorage, formatted.cchWritten }
            : ( bRecipe
                ? ShaderText( "Shader recipe is not valid bounded UTF-8 text." )
                : ShaderText( "GLSL stage is not valid bounded UTF-8 text." ) );
    EmitDiagnostic(
        request,
        report,
        code,
        tool_diagnostic_severity_t::ERROR,
        status == shader_text_read_status_t::IO_ERROR
            ? tool_diagnostic_category_t::FILESYSTEM
            : tool_diagnostic_category_t::SOURCE,
        message,
        virtualPath,
        1u,
        1u,
        diagnosticPath );
    MarkFailed( report );
    return toolStatus;
}

CYPHER_NODISCARD bool_t BuildDefinePreamble(
    const string_view_t *pDefines,
    usize nDefines,
    text_buffer_t &preamble ) noexcept
{
    for ( usize iDefine = 0u; iDefine < nDefines; ++iDefine ) {
        if ( !TextBuffer_Append( &preamble, ShaderText( "#define " ) ) ||
             !TextBuffer_Append( &preamble, pDefines[iDefine] ) ||
             !TextBuffer_Append( &preamble, ShaderText( " 1\n" ) ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD render_shader_resource_type_t ShaderTextureResourceType(
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

CYPHER_NODISCARD render_shader_value_type_t ShaderParameterValueType(
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

CYPHER_NODISCARD bool_t AppendLogicalBinding(
    shader_compile_work_t &work,
    const cooked_shader_binding_source_t &binding ) noexcept
{
    if ( work.nBindings >= CY_COOKED_SHADER_MAX_BINDINGS ||
         binding.nLogicalBinding == 0u ) {
        return CY_FALSE;
    }
    for ( u32 iBinding = 0u; iBinding < work.nBindings; ++iBinding ) {
        if ( work.bindings[iBinding].nLogicalBinding == binding.nLogicalBinding ) {
            return CY_FALSE;
        }
    }
    work.bindings[work.nBindings++] = binding;
    return CY_TRUE;
}

CYPHER_NODISCARD flags32_t MaterialBindingFlags(
    bool_t bRequired ) noexcept
{
    return COOKED_SHADER_BINDING_FLAG_MATERIAL |
           ( bRequired ? COOKED_SHADER_BINDING_FLAG_REQUIRED : 0u );
}

CYPHER_NODISCARD bool_t BuildV2CookedInterface(
    shader_compile_work_t &work ) noexcept
{
    work.nBindings = 0u;
    work.interfaceHash = {};
    for ( flags32_t &stageMask : work.reflectedStageMasks ) {
        stageMask = RENDER_SHADER_STAGE_MASK_NONE;
    }

    for ( usize iTexture = 0u;
          iTexture < work.recipeV2.nTextures;
          ++iTexture ) {
        const render_shader_texture_interface_v2_view_t &source =
            work.recipeV2.textures[iTexture];
        cooked_shader_binding_source_t binding{};
        binding.name = source.name;
        binding.kind = render_shader_binding_kind_t::SAMPLED_TEXTURE;
        binding.resourceType = ShaderTextureResourceType( source.type );
        // CYSH and CYMT currently share conservative material visibility. Physical
        // stage use is tracked separately below so their interface hashes agree.
        binding.stageMask = RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS;
        binding.flags = MaterialBindingFlags( source.bRequired );
        if ( binding.resourceType == render_shader_resource_type_t::NONE ||
             !CookedShader_MakeLogicalBindingId(
                 binding.name,
                 &binding.nLogicalBinding ) ||
             !AppendLogicalBinding( work, binding ) ) {
            return CY_FALSE;
        }
    }

    // Material constants use one deterministic packed block. Parameters are laid
    // out in canonical name order: scalars align to 4 bytes, vec2 to 8 bytes, and
    // vec3/vec4/matrices to 16 bytes. Storage uses padded vec3/color3 (16 bytes),
    // mat3 as three padded vec4 columns (48 bytes), and mat4 as 64 bytes.
    usize parameterOrder[CY_RENDER_SHADER_MAX_INTERFACE_PARAMETERS]{};
    for ( usize iParameter = 0u;
          iParameter < work.recipeV2.nParameters;
          ++iParameter ) {
        parameterOrder[iParameter] = iParameter;
    }
    for ( usize iParameter = 1u;
          iParameter < work.recipeV2.nParameters;
          ++iParameter ) {
        const usize value = parameterOrder[iParameter];
        usize iInsert = iParameter;
        while ( iInsert != 0u &&
                StringView_Compare(
                    work.recipeV2.parameters[value].name,
                    work.recipeV2.parameters[
                        parameterOrder[iInsert - 1u] ].name ) < 0 ) {
            parameterOrder[iInsert] = parameterOrder[iInsert - 1u];
            --iInsert;
        }
        parameterOrder[iInsert] = value;
    }

    usize cbParameterBlock = 0u;
    for ( usize iOrdered = 0u;
          iOrdered < work.recipeV2.nParameters;
          ++iOrdered ) {
        const render_shader_parameter_interface_v2_view_t &source =
            work.recipeV2.parameters[parameterOrder[iOrdered]];
        cooked_shader_binding_source_t binding{};
        binding.name = source.name;
        binding.kind = render_shader_binding_kind_t::VALUE;
        binding.valueType = ShaderParameterValueType( source.type );
        binding.stageMask = RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS;
        binding.flags = MaterialBindingFlags( source.bRequired );
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
             !AppendLogicalBinding( work, binding ) ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

CYPHER_NODISCARD bool_t IsHorizontalWhitespace( char ch ) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\r';
}

enum class glsl_profile_parse_status_t : u8 {
    OK = 0u,
    MISSING_DIRECTIVE,
    INVALID_DIRECTIVE,
    UNSUPPORTED_VERSION,
    UNSUPPORTED_PROFILE
};

struct glsl_profile_parse_result_t {
    glsl_profile_parse_status_t status{
        glsl_profile_parse_status_t::MISSING_DIRECTIVE
    };
    u32 nVersion{ 0u };
    u32 nLine{ 1u };
    u32 nColumn{ 1u };
};

CYPHER_NODISCARD bool_t IsSupportedGlslCoreVersion(
    u32 nVersion ) noexcept
{
    return CookedShader_SupportsLanguage(
        render_shader_language_profile_t::GLSL_CORE,
        nVersion );
}

void AdvanceGlslLocation(
    string_view_t source,
    usize &iByte,
    u32 &nLine,
    u32 &nColumn ) noexcept
{
    if ( source.pData[iByte] == '\r' ) {
        ++iByte;
        if ( iByte < source.cchLength && source.pData[iByte] == '\n' ) {
            ++iByte;
        }
        ++nLine;
        nColumn = 1u;
        return;
    }
    if ( source.pData[iByte] == '\n' ) {
        ++iByte;
        ++nLine;
        nColumn = 1u;
        return;
    }
    ++iByte;
    ++nColumn;
}

template <usize nExtent>
CYPHER_NODISCARD bool_t ConsumeGlslToken(
    string_view_t source,
    usize &iByte,
    const char ( &token )[nExtent] ) noexcept
{
    constexpr usize cchToken = nExtent - 1u;
    if ( iByte > source.cchLength ||
         cchToken > source.cchLength - iByte ) {
        return CY_FALSE;
    }
    for ( usize iToken = 0u; iToken < cchToken; ++iToken ) {
        if ( source.pData[iByte + iToken] != token[iToken] ) {
            return CY_FALSE;
        }
    }
    iByte += cchToken;
    return CY_TRUE;
}

CYPHER_NODISCARD glsl_profile_parse_result_t ParseGlslProfile(
    string_view_t source ) noexcept
{
    glsl_profile_parse_result_t result{};
    if ( !StringView_IsValid( source ) || source.cchLength == 0u ) {
        return result;
    }

    usize iByte = 0u;
    u32 nLine = 1u;
    u32 nColumn = 1u;
    if ( source.cchLength >= 3u &&
         static_cast<u8>( source.pData[0] ) == 0xEFu &&
         static_cast<u8>( source.pData[1] ) == 0xBBu &&
         static_cast<u8>( source.pData[2] ) == 0xBFu ) {
        iByte = 3u;
    }

    // GLSL permits comments and whitespace before #version. No other token may
    // precede it, so a small bounded scanner is sufficient for this policy gate.
    while ( iByte < source.cchLength ) {
        const char ch = source.pData[iByte];
        if ( ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ) {
            AdvanceGlslLocation( source, iByte, nLine, nColumn );
            continue;
        }
        if ( ch == '/' && iByte + 1u < source.cchLength &&
             source.pData[iByte + 1u] == '/' ) {
            AdvanceGlslLocation( source, iByte, nLine, nColumn );
            AdvanceGlslLocation( source, iByte, nLine, nColumn );
            while ( iByte < source.cchLength &&
                    source.pData[iByte] != '\n' ) {
                AdvanceGlslLocation( source, iByte, nLine, nColumn );
            }
            continue;
        }
        if ( ch == '/' && iByte + 1u < source.cchLength &&
             source.pData[iByte + 1u] == '*' ) {
            AdvanceGlslLocation( source, iByte, nLine, nColumn );
            AdvanceGlslLocation( source, iByte, nLine, nColumn );
            bool_t bClosed = CY_FALSE;
            while ( iByte + 1u < source.cchLength ) {
                if ( source.pData[iByte] == '*' &&
                     source.pData[iByte + 1u] == '/' ) {
                    AdvanceGlslLocation( source, iByte, nLine, nColumn );
                    AdvanceGlslLocation( source, iByte, nLine, nColumn );
                    bClosed = CY_TRUE;
                    break;
                }
                AdvanceGlslLocation( source, iByte, nLine, nColumn );
            }
            if ( !bClosed ) {
                result.status = glsl_profile_parse_status_t::INVALID_DIRECTIVE;
                result.nLine = nLine;
                result.nColumn = nColumn;
                return result;
            }
            continue;
        }
        break;
    }

    result.nLine = nLine;
    result.nColumn = nColumn;
    if ( iByte >= source.cchLength || source.pData[iByte++] != '#' ) {
        return result;
    }
    while ( iByte < source.cchLength &&
            IsHorizontalWhitespace( source.pData[iByte] ) ) {
        ++iByte;
    }
    if ( !ConsumeGlslToken( source, iByte, "version" ) ) {
        result.status = glsl_profile_parse_status_t::INVALID_DIRECTIVE;
        return result;
    }
    if ( iByte >= source.cchLength ||
         !IsHorizontalWhitespace( source.pData[iByte] ) ) {
        result.status = glsl_profile_parse_status_t::INVALID_DIRECTIVE;
        return result;
    }
    while ( iByte < source.cchLength &&
            IsHorizontalWhitespace( source.pData[iByte] ) ) {
        ++iByte;
    }

    usize nDigits = 0u;
    while ( iByte < source.cchLength &&
            source.pData[iByte] >= '0' && source.pData[iByte] <= '9' ) {
        if ( nDigits == 3u ) {
            result.status = glsl_profile_parse_status_t::INVALID_DIRECTIVE;
            return result;
        }
        result.nVersion = result.nVersion * 10u +
            static_cast<u32>( source.pData[iByte] - '0' );
        ++iByte;
        ++nDigits;
    }
    if ( nDigits != 3u || iByte >= source.cchLength ||
         !IsHorizontalWhitespace( source.pData[iByte] ) ) {
        result.status = glsl_profile_parse_status_t::INVALID_DIRECTIVE;
        return result;
    }
    while ( iByte < source.cchLength &&
            IsHorizontalWhitespace( source.pData[iByte] ) ) {
        ++iByte;
    }
    if ( !ConsumeGlslToken( source, iByte, "core" ) ) {
        result.status = glsl_profile_parse_status_t::UNSUPPORTED_PROFILE;
        return result;
    }
    const bool_t bValidBoundary = iByte == source.cchLength ||
        source.pData[iByte] == '\n' || source.pData[iByte] == '\r' ||
        source.pData[iByte] == ' ' || source.pData[iByte] == '\t' ||
        ( source.pData[iByte] == '/' &&
          iByte + 1u < source.cchLength &&
          ( source.pData[iByte + 1u] == '/' ||
            source.pData[iByte + 1u] == '*' ) );
    if ( !bValidBoundary ) {
        result.status = glsl_profile_parse_status_t::UNSUPPORTED_PROFILE;
        return result;
    }
    result.status = IsSupportedGlslCoreVersion( result.nVersion )
        ? glsl_profile_parse_status_t::OK
        : glsl_profile_parse_status_t::UNSUPPORTED_VERSION;
    return result;
}

CYPHER_NODISCARD u32 TargetMaximumGlslCoreVersion(
    tool_target_t target ) noexcept
{
    return target.platform == tool_platform_t::MACOS ? 410u : 450u;
}

CYPHER_NODISCARD string_view_t GlslProfileFailureHint(
    glsl_profile_parse_status_t status ) noexcept
{
    switch ( status ) {
        case glsl_profile_parse_status_t::MISSING_DIRECTIVE:
            return ShaderText(
                "Make `#version` the first non-comment directive in the stage." );
        case glsl_profile_parse_status_t::INVALID_DIRECTIVE:
            return ShaderText( "Use the exact form `#version NNN core`." );
        case glsl_profile_parse_status_t::UNSUPPORTED_VERSION:
            return ShaderText(
                "Supported desktop core versions are 330, 400, 410, 420, 430, 440, and 450." );
        case glsl_profile_parse_status_t::UNSUPPORTED_PROFILE:
            return ShaderText(
                "Shader version 1 supports the desktop `core` profile; compatibility and ES profiles are separate future contracts." );
        case glsl_profile_parse_status_t::OK:
            break;
    }
    return {};
}

void SetIncludeFailure(
    shader_include_context_t &context,
    shader_include_status_t status,
    string_view_t path = {},
    shader_text_read_status_t textReadStatus =
        shader_text_read_status_t::OK,
    vfs_status_t vfsStatus = vfs_status_t::OK ) noexcept
{
    // Preserve the first failure. It is normally the most specific cause and
    // avoids later preprocessor cleanup replacing it with a secondary error.
    if ( context.status != shader_include_status_t::OK ) {
        return;
    }
    context.status = status;
    context.textReadStatus = textReadStatus;
    context.vfsStatus = vfsStatus;
    if ( path.cchLength != 0u &&
         !TextBuffer_Assign( &context.failurePath, path ) ) {
        context.status = shader_include_status_t::OUT_OF_MEMORY;
    }
}

CYPHER_NODISCARD bool_t ResolveIncludeVirtualPath(
    shader_include_context_t &context,
    const char *pHeaderName,
    const char *pIncluderName,
    char ( &pathOut )[CY_SHADER_COMPILER_MAX_PATH + 1u],
    usize &cchPathOut ) noexcept
{
    cchPathOut = 0u;
    if ( pHeaderName == nullptr ) {
        SetIncludeFailure(
            context,
            shader_include_status_t::INVALID_PATH );
        return CY_FALSE;
    }

    const usize cchHeader = Cy_strnlen(
        pHeaderName,
        CY_SHADER_COMPILER_MAX_PATH + 1u );
    if ( cchHeader == 0u || cchHeader > CY_SHADER_COMPILER_MAX_PATH ) {
        SetIncludeFailure(
            context,
            shader_include_status_t::INVALID_PATH );
        return CY_FALSE;
    }
    const string_view_t requested{ pHeaderName, cchHeader };

    string_view_t includer = context.rootVirtualPath;
    if ( pIncluderName != nullptr ) {
        const usize cchIncluder = Cy_strnlen(
            pIncluderName,
            CY_SHADER_COMPILER_MAX_PATH + 1u );
        const string_view_t candidate{ pIncluderName, cchIncluder };
        if ( cchIncluder <= CY_SHADER_COMPILER_MAX_PATH &&
             Vfs_IsCanonicalPath( candidate ) &&
             context.pCache != nullptr ) {
            for ( usize iFile = 0u;
                  iFile < context.pCache->nFiles;
                  ++iFile ) {
                if ( StringView_Equals(
                         TextBuffer_View(
                             &context.pCache->files[iFile].virtualPath ),
                         candidate ) ) {
                    includer = candidate;
                    break;
                }
            }
        }
    }

    const string_view_t parent = StringPath_Parent( includer );
    char joined[( CY_SHADER_COMPILER_MAX_PATH * 2u ) + 2u]{};
    const path_write_result_t joinedResult = StringPath_Join(
        parent,
        requested,
        path_style_t::VIRTUAL,
        joined,
        sizeof( joined ) );
    if ( joinedResult.status != path_status_t::OK ) {
        SetIncludeFailure(
            context,
            shader_include_status_t::INVALID_PATH,
            requested );
        return CY_FALSE;
    }

    constexpr flags32_t normalizeFlags =
        PATH_NORMALIZE_FLAG_COLLAPSE_SEPARATORS |
        PATH_NORMALIZE_FLAG_RESOLVE_DOT |
        PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT |
        PATH_NORMALIZE_FLAG_LOWERCASE_ASCII |
        PATH_NORMALIZE_FLAG_REJECT_ABSOLUTE |
        PATH_NORMALIZE_FLAG_REJECT_ABOVE_ROOT;
    const path_write_result_t normalized = StringPath_Normalize(
        { joined, joinedResult.cchWritten },
        path_style_t::VIRTUAL,
        normalizeFlags,
        pathOut,
        sizeof( pathOut ) );
    const string_view_t resolved{ pathOut, normalized.cchWritten };
    if ( normalized.status != path_status_t::OK ||
         !Vfs_IsCanonicalPath( resolved ) ||
         !StringPath_HasExtension(
             resolved,
             ShaderText( ".glsl" ),
             CY_FALSE ) ) {
        SetIncludeFailure(
            context,
            shader_include_status_t::INVALID_PATH,
            requested );
        pathOut[0] = '\0';
        return CY_FALSE;
    }

    cchPathOut = normalized.cchWritten;
    return CY_TRUE;
}

CYPHER_NODISCARD shader_include_file_t *FindIncludeFile(
    shader_include_cache_t &cache,
    string_view_t virtualPath ) noexcept
{
    for ( usize iFile = 0u; iFile < cache.nFiles; ++iFile ) {
        if ( StringView_Equals(
                 TextBuffer_View( &cache.files[iFile].virtualPath ),
                 virtualPath ) ) {
            return &cache.files[iFile];
        }
    }
    return nullptr;
}

CYPHER_NODISCARD shader_include_file_t *LoadIncludeFile(
    shader_include_context_t &context,
    string_view_t virtualPath ) noexcept
{
    shader_include_cache_t &cache = *context.pCache;
    if ( shader_include_file_t *pExisting =
             FindIncludeFile( cache, virtualPath ) ) {
        return pExisting;
    }
    if ( cache.nFiles >= CY_SHADER_COMPILER_MAX_INCLUDE_FILES ) {
        SetIncludeFailure(
            context,
            shader_include_status_t::FILE_LIMIT,
            virtualPath );
        return nullptr;
    }

    shader_include_file_t &file = cache.files[cache.nFiles];
    if ( !TextBuffer_Assign( &file.virtualPath, virtualPath ) ) {
        SetIncludeFailure(
            context,
            shader_include_status_t::OUT_OF_MEMORY,
            virtualPath );
        return nullptr;
    }

    vfs_status_t vfsStatus = vfs_status_t::OK;
    const shader_text_read_status_t readStatus = ReadTextFile(
        cache.pVfs,
        virtualPath,
        CY_SHADER_COMPILER_MAX_INCLUDE_SIZE,
        file.source,
        &vfsStatus );
    if ( readStatus != shader_text_read_status_t::OK ) {
        SetIncludeFailure(
            context,
            readStatus == shader_text_read_status_t::OUT_OF_MEMORY
                ? shader_include_status_t::OUT_OF_MEMORY
            : readStatus == shader_text_read_status_t::IO_ERROR
                ? shader_include_status_t::READ_FAILED
                : shader_include_status_t::INVALID_TEXT,
            virtualPath,
            readStatus,
            vfsStatus );
        return nullptr;
    }
    if ( file.source.cchLength >
         CY_SHADER_COMPILER_MAX_INCLUDE_BYTES - cache.cbRead ) {
        SetIncludeFailure(
            context,
            shader_include_status_t::TOTAL_SIZE_LIMIT,
            virtualPath );
        return nullptr;
    }

    file.sourceHash = ContentHash_String( TextBuffer_View( &file.source ) );
    if ( !ContentHash_IsValid( file.sourceHash ) ) {
        SetIncludeFailure(
            context,
            shader_include_status_t::INVALID_TEXT,
            virtualPath );
        return nullptr;
    }
    cache.cbRead += file.source.cchLength;
    if ( context.pReport != nullptr ) {
        context.pReport->cbRead += file.source.cchLength;
    }
    ++cache.nFiles;
    return &file;
}

glsl_include_result_t *ResolveLocalInclude(
    void *pContext,
    const char *pHeaderName,
    const char *pIncluderName,
    size_t nIncludeDepth ) noexcept
{
    if ( pContext == nullptr ) {
        return nullptr;
    }
    auto &context = *static_cast<shader_include_context_t *>( pContext );
    if ( context.status != shader_include_status_t::OK ) {
        return nullptr;
    }
    if ( nIncludeDepth > CY_SHADER_COMPILER_MAX_INCLUDE_DEPTH ) {
        SetIncludeFailure(
            context,
            shader_include_status_t::DEPTH_LIMIT,
            pHeaderName != nullptr
                ? StringView_FromCString( pHeaderName )
                : string_view_t{} );
        return nullptr;
    }
    if ( context.nResults >= CY_SHADER_COMPILER_MAX_INCLUDE_REQUESTS ) {
        SetIncludeFailure(
            context,
            shader_include_status_t::REQUEST_LIMIT,
            pHeaderName != nullptr
                ? StringView_FromCString( pHeaderName )
                : string_view_t{} );
        return nullptr;
    }

    char resolvedStorage[CY_SHADER_COMPILER_MAX_PATH + 1u]{};
    usize cchResolved = 0u;
    if ( !ResolveIncludeVirtualPath(
             context,
             pHeaderName,
             pIncluderName,
             resolvedStorage,
             cchResolved ) ) {
        return nullptr;
    }
    shader_include_file_t *pFile = LoadIncludeFile(
        context,
        { resolvedStorage, cchResolved } );
    if ( pFile == nullptr ) {
        return nullptr;
    }

    glsl_include_result_t &result = context.results[context.nResults++];
    result.header_name = TextBuffer_CStr( &pFile->virtualPath );
    result.header_data = TextBuffer_CStr( &pFile->source );
    result.header_length = pFile->source.cchLength;
    return &result;
}

glsl_include_result_t *RejectSystemInclude(
    void *pContext,
    const char *pHeaderName,
    const char *,
    size_t ) noexcept
{
    if ( pContext != nullptr ) {
        auto &context = *static_cast<shader_include_context_t *>( pContext );
        SetIncludeFailure(
            context,
            shader_include_status_t::SYSTEM_INCLUDE_UNSUPPORTED,
            pHeaderName != nullptr
                ? StringView_FromCString( pHeaderName )
                : string_view_t{} );
    }
    return nullptr;
}

int ReleaseInclude( void *, glsl_include_result_t * ) noexcept
{
    // Include results borrow request-owned fixed storage. They remain alive until
    // linking completes, so glslang release notifications require no action.
    return 0;
}

CYPHER_NODISCARD string_view_t IncludeFailureHint(
    const shader_include_context_t &context ) noexcept
{
    switch ( context.status ) {
    case shader_include_status_t::SYSTEM_INCLUDE_UNSUPPORTED:
        return ShaderText(
            "Use quoted project-local includes; `<system>` includes are not reproducible." );
    case shader_include_status_t::INVALID_PATH:
        return ShaderText(
            "Include a relative `.glsl` path that remains inside the source VFS root." );
    case shader_include_status_t::DEPTH_LIMIT:
        return ShaderText( "The GLSL include nesting limit is 32." );
    case shader_include_status_t::FILE_LIMIT:
        return ShaderText( "One shader may depend on at most 128 unique include files." );
    case shader_include_status_t::REQUEST_LIMIT:
        return ShaderText( "One shader stage may issue at most 256 include requests." );
    case shader_include_status_t::TOTAL_SIZE_LIMIT:
        return ShaderText( "Combined unique GLSL include text may not exceed 8 MiB." );
    case shader_include_status_t::READ_FAILED:
        return ShaderText( "The include could not be read through the source VFS." );
    case shader_include_status_t::INVALID_TEXT:
        return ShaderText(
            "GLSL includes must be non-empty UTF-8 text without embedded null bytes and at most 1 MiB each." );
    case shader_include_status_t::OUT_OF_MEMORY:
        return ShaderText( "Memory allocation failed while resolving GLSL includes." );
    case shader_include_status_t::OK:
        break;
    }
    return {};
}

CYPHER_NODISCARD const char *GlslangLog(
    glslang_shader_t *pShader ) noexcept
{
    const char *pLog = glslang_shader_get_info_log( pShader );
    if ( pLog != nullptr && pLog[0] != '\0' ) {
        return pLog;
    }
    pLog = glslang_shader_get_info_debug_log( pShader );
    return pLog != nullptr && pLog[0] != '\0' ? pLog : nullptr;
}

CYPHER_NODISCARD const char *GlslangLog(
    glslang_program_t *pProgram ) noexcept
{
    const char *pLog = glslang_program_get_info_log( pProgram );
    if ( pLog != nullptr && pLog[0] != '\0' ) {
        return pLog;
    }
    pLog = glslang_program_get_info_debug_log( pProgram );
    return pLog != nullptr && pLog[0] != '\0' ? pLog : nullptr;
}

CYPHER_NODISCARD string_view_t StdStringView(
    const std::string &text ) noexcept
{
    return { text.data(), text.size() };
}

CYPHER_NODISCARD flags32_t ShaderStageMask(
    render_shader_stage_t stage ) noexcept
{
    switch ( stage ) {
        case render_shader_stage_t::VERTEX:
            return RENDER_SHADER_STAGE_MASK_VERTEX;
        case render_shader_stage_t::FRAGMENT:
            return RENDER_SHADER_STAGE_MASK_FRAGMENT;
    }
    return RENDER_SHADER_STAGE_MASK_NONE;
}

CYPHER_NODISCARD cooked_shader_binding_source_t *FindAuthoredBinding(
    shader_compile_work_t &work,
    string_view_t name ) noexcept
{
    for ( u32 iBinding = 0u; iBinding < work.nBindings; ++iBinding ) {
        if ( StringView_Equals( work.bindings[iBinding].name, name ) ) {
            return &work.bindings[iBinding];
        }
    }
    return nullptr;
}

CYPHER_NODISCARD tool_status_t ReportInterfaceMismatch(
    const tool_compile_request_t &request,
    tool_report_t &report,
    string_view_t prefix,
    string_view_t name,
    string_view_t suffix,
    string_view_t path,
    string_view_t hint ) noexcept
{
    char message[512]{};
    const string_format_result_t formatted = StringFormat_Printf(
        message,
        sizeof( message ),
        "%.*s `%.*s`%.*s",
        static_cast<int>( prefix.cchLength ),
        prefix.pData,
        static_cast<int>( name.cchLength ),
        name.pData,
        static_cast<int>( suffix.cchLength ),
        suffix.pData );
    EmitDiagnostic(
        request,
        report,
        CY_SHADER_DIAGNOSTIC_INTERFACE_FAILED,
        tool_diagnostic_severity_t::ERROR,
        tool_diagnostic_category_t::VALIDATION,
        formatted.status == string_format_status_t::OK
            ? StringView_FromCString( message )
            : ShaderText( "Shader interface does not match active linked GLSL." ),
        path,
        1u,
        1u,
        hint );
    return tool_status_t::VALIDATION_FAILED;
}

CYPHER_NODISCARD render_shader_resource_type_t ReflectedTextureType(
    const spirv_cross::Compiler &compiler,
    const spirv_cross::SPIRType &type )
{
    if ( type.basetype != spirv_cross::SPIRType::SampledImage ||
         !type.array.empty() || type.image.depth || type.image.ms ||
         type.image.sampled != 1u ) {
        return render_shader_resource_type_t::NONE;
    }

    const spirv_cross::SPIRType &component = compiler.get_type(
        type.image.type );
    if ( component.basetype != spirv_cross::SPIRType::Float ) {
        return render_shader_resource_type_t::NONE;
    }

    switch ( type.image.dim ) {
        case spv::Dim2D:
            return type.image.arrayed
                ? render_shader_resource_type_t::TEXTURE_2D_ARRAY
                : render_shader_resource_type_t::TEXTURE_2D;
        case spv::Dim3D:
            return !type.image.arrayed
                ? render_shader_resource_type_t::TEXTURE_3D
                : render_shader_resource_type_t::NONE;
        case spv::DimCube:
            return !type.image.arrayed
                ? render_shader_resource_type_t::TEXTURE_CUBE
                : render_shader_resource_type_t::NONE;
        default:
            return render_shader_resource_type_t::NONE;
    }
}

CYPHER_NODISCARD render_shader_value_type_t ReflectedValueType(
    const spirv_cross::SPIRType &type ) noexcept
{
    if ( !type.array.empty() ) {
        return render_shader_value_type_t::NONE;
    }
    if ( type.basetype == spirv_cross::SPIRType::Boolean &&
         type.vecsize == 1u && type.columns == 1u ) {
        return render_shader_value_type_t::BOOL;
    }
    if ( type.width != 32u ) {
        return render_shader_value_type_t::NONE;
    }
    if ( type.basetype == spirv_cross::SPIRType::Int &&
         type.vecsize == 1u && type.columns == 1u ) {
        return render_shader_value_type_t::I32;
    }
    if ( type.basetype == spirv_cross::SPIRType::UInt &&
         type.vecsize == 1u && type.columns == 1u ) {
        return render_shader_value_type_t::U32;
    }
    if ( type.basetype != spirv_cross::SPIRType::Float ) {
        return render_shader_value_type_t::NONE;
    }
    if ( type.columns == 1u ) {
        switch ( type.vecsize ) {
            case 1u: return render_shader_value_type_t::F32;
            case 2u: return render_shader_value_type_t::F32X2;
            case 3u: return render_shader_value_type_t::F32X3;
            case 4u: return render_shader_value_type_t::F32X4;
            default: return render_shader_value_type_t::NONE;
        }
    }
    if ( type.columns == 3u && type.vecsize == 3u ) {
        return render_shader_value_type_t::F32X3X3;
    }
    if ( type.columns == 4u && type.vecsize == 4u ) {
        return render_shader_value_type_t::F32X4X4;
    }
    return render_shader_value_type_t::NONE;
}

CYPHER_NODISCARD tool_status_t ReflectStageInterfaceV2(
    const tool_compile_request_t &request,
    tool_report_t &report,
    glslang_program_t *pProgram,
    shader_stage_work_t &stage,
    shader_compile_work_t &work ) noexcept
{
    glslang_program_SPIRV_generate( pProgram, stage.glslangStage );
    const usize nWords = glslang_program_SPIRV_get_size( pProgram );
    const u32 *pWords = glslang_program_SPIRV_get_ptr( pProgram );
    if ( pWords == nullptr || nWords < 5u ) {
        const char *pLog = glslang_program_SPIRV_get_messages( pProgram );
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::COMPILER,
            pLog != nullptr && pLog[0] != '\0'
                ? StringView_FromCString( pLog )
                : ShaderText( "glslang did not produce a valid SPIR-V reflection module." ),
            stage.virtualPath );
        return tool_status_t::INTERNAL_ERROR;
    }

    const flags32_t stageMask = ShaderStageMask( stage.stage );
    if ( stageMask == RENDER_SHADER_STAGE_MASK_NONE ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::INTERNAL,
            ShaderText( "Shader reflection received an unsupported stage." ),
            stage.virtualPath );
        return tool_status_t::INTERNAL_ERROR;
    }

    try {
        const spirv_cross::Compiler compiler( pWords, nWords );
        const auto active = compiler.get_active_interface_variables();
        const spirv_cross::ShaderResources resources =
            compiler.get_shader_resources( active );

        for ( const spirv_cross::Resource &resource :
              resources.sampled_images ) {
            const string_view_t name = StdStringView( resource.name );
            cooked_shader_binding_source_t *pBinding = FindAuthoredBinding(
                work,
                name );
            if ( pBinding == nullptr ||
                 pBinding->kind !=
                     render_shader_binding_kind_t::SAMPLED_TEXTURE ) {
                return ReportInterfaceMismatch(
                    request,
                    report,
                    ShaderText( "Active GLSL sampled image" ),
                    name,
                    ShaderText(
                        " has no matching interface.textures declaration." ),
                    stage.virtualPath,
                    ShaderText(
                        "Declare the active combined sampler as a texture with the same name and dimension." ) );
            }
            const spirv_cross::SPIRType &type = compiler.get_type(
                resource.type_id );
            const render_shader_resource_type_t reflectedType =
                ReflectedTextureType( compiler, type );
            if ( reflectedType == render_shader_resource_type_t::NONE ||
                 reflectedType != pBinding->resourceType ||
                 pBinding->nArrayElements != 1u ) {
                return ReportInterfaceMismatch(
                    request,
                    report,
                    ShaderText( "GLSL sampled image" ),
                    name,
                    ShaderText(
                        " does not match its authored texture dimension or binding-array shape." ),
                    request.input,
                    ShaderText(
                        "Use an active float sampler2D, samplerCube, sampler2DArray, or sampler3D matching interface.textures; shadow, multisample, integer, and descriptor-array samplers are not representable yet." ) );
            }
            const usize iBinding = static_cast<usize>(
                pBinding - work.bindings );
            work.reflectedStageMasks[iBinding] |= stageMask;
        }

        for ( const spirv_cross::Resource &resource :
              resources.gl_plain_uniforms ) {
            const string_view_t name = StdStringView( resource.name );
            cooked_shader_binding_source_t *pBinding = FindAuthoredBinding(
                work,
                name );
            if ( pBinding == nullptr ||
                 pBinding->kind != render_shader_binding_kind_t::VALUE ) {
                return ReportInterfaceMismatch(
                    request,
                    report,
                    ShaderText( "Active GLSL uniform" ),
                    name,
                    ShaderText(
                        " has no matching interface.parameters declaration." ),
                    stage.virtualPath,
                    ShaderText(
                        "Declare the active plain uniform as a parameter with the same name and value type." ) );
            }
            const spirv_cross::SPIRType &type = compiler.get_type(
                resource.type_id );
            const render_shader_value_type_t reflectedType =
                ReflectedValueType( type );
            if ( reflectedType == render_shader_value_type_t::NONE ||
                 reflectedType != pBinding->valueType ||
                 pBinding->nArrayElements != 1u ) {
                return ReportInterfaceMismatch(
                    request,
                    report,
                    ShaderText( "GLSL uniform" ),
                    name,
                    ShaderText(
                        " does not match its authored parameter type or binding-array shape." ),
                    request.input,
                    ShaderText(
                        "Use a non-array bool, int, uint, float, vec2, vec3, vec4, mat3, or mat4 matching interface.parameters." ) );
            }
            const usize iBinding = static_cast<usize>(
                pBinding - work.bindings );
            work.reflectedStageMasks[iBinding] |= stageMask;
        }
    } catch ( const std::bad_alloc & ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::INTERNAL,
            ShaderText( "Out of memory while reflecting linked shader resources." ),
            stage.virtualPath );
        return tool_status_t::OUT_OF_MEMORY;
    } catch ( const std::exception &error ) {
        const char *pMessage = error.what();
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::COMPILER,
            pMessage != nullptr && pMessage[0] != '\0'
                ? StringView_FromCString( pMessage )
                : ShaderText( "SPIRV-Cross reflection failed." ),
            stage.virtualPath );
        return tool_status_t::INTERNAL_ERROR;
    } catch ( ... ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::COMPILER,
            ShaderText( "SPIRV-Cross reflection failed with an unknown error." ),
            stage.virtualPath );
        return tool_status_t::INTERNAL_ERROR;
    }
    return tool_status_t::OK;
}

CYPHER_NODISCARD tool_status_t FinalizeReflectedInterfaceV2(
    const tool_compile_request_t &request,
    tool_report_t &report,
    shader_compile_work_t &work ) noexcept
{
    for ( u32 iBinding = 0u; iBinding < work.nBindings; ++iBinding ) {
        const cooked_shader_binding_source_t &binding = work.bindings[iBinding];
        if ( work.reflectedStageMasks[iBinding] !=
             RENDER_SHADER_STAGE_MASK_NONE ) {
            continue;
        }
        const bool_t bTexture = binding.kind ==
            render_shader_binding_kind_t::SAMPLED_TEXTURE;
        return ReportInterfaceMismatch(
            request,
            report,
            bTexture
                ? ShaderText( "Authored texture" )
                : ShaderText( "Authored parameter" ),
            binding.name,
            ShaderText( " is absent or inactive in linked GLSL." ),
            request.input,
            bTexture
                ? ShaderText(
                      "Declare and actively sample a matching combined sampler uniform in at least one stage, or remove the texture declaration." )
                : ShaderText(
                      "Declare and actively read a matching plain uniform in at least one stage, or remove the parameter declaration." ) );
    }

    const cooked_shader_interface_source_t shaderInterface{
        { work.bindings, work.nBindings }
    };
    if ( !CookedShader_ComputeInterfaceHash(
             shaderInterface,
             &work.interfaceHash ) ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::INTERNAL,
            ShaderText( "Reflected shader interface could not be canonicalized." ),
            request.input );
        return tool_status_t::INTERNAL_ERROR;
    }
    return tool_status_t::OK;
}

CYPHER_NODISCARD tool_status_t CompileStage(
    const tool_compile_request_t &request,
    tool_report_t &report,
    const text_buffer_t &preamble,
    bool_t bGenerateSpirv,
    shader_stage_work_t &stage ) noexcept
{
    stage.includes.pReport = &report;
    stage.includes.rootVirtualPath = stage.virtualPath;
    const glslang_input_t input{
        GLSLANG_SOURCE_GLSL,
        stage.glslangStage,
        GLSLANG_CLIENT_OPENGL,
        GLSLANG_TARGET_OPENGL_450,
        bGenerateSpirv ? GLSLANG_TARGET_SPV : GLSLANG_TARGET_NONE,
        GLSLANG_TARGET_SPV_1_0,
        TextBuffer_CStr( &stage.source ),
        static_cast<int>( stage.nGlslVersion ),
        GLSLANG_CORE_PROFILE,
        false,
        true,
        static_cast<glslang_messages_t>(
            GLSLANG_MSG_DEFAULT_BIT |
            GLSLANG_MSG_DISPLAY_ERROR_COLUMN |
            ( bGenerateSpirv ? GLSLANG_MSG_SPV_RULES_BIT : 0 ) ),
        glslang_default_resource(),
        { &RejectSystemInclude, &ResolveLocalInclude, &ReleaseInclude },
        &stage.includes
    };

    stage.compiler.pShader = glslang_shader_create( &input );
    if ( stage.compiler.pShader == nullptr ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::COMPILER,
            ShaderText( "glslang could not create a shader compiler object." ),
            stage.virtualPath );
        return tool_status_t::INTERNAL_ERROR;
    }
    if ( bGenerateSpirv ) {
        glslang_shader_set_options(
            stage.compiler.pShader,
            GLSLANG_SHADER_AUTO_MAP_BINDINGS |
                GLSLANG_SHADER_AUTO_MAP_LOCATIONS );
    }
    glslang_shader_set_preamble(
        stage.compiler.pShader,
        TextBuffer_CStr( &preamble ) );

    if ( !glslang_shader_preprocess( stage.compiler.pShader, &input ) ) {
        const char *pLog = GlslangLog( stage.compiler.pShader );
        const bool_t bIncludeFailure =
            stage.includes.status != shader_include_status_t::OK;
        const bool_t bSystemInclude = stage.includes.status ==
            shader_include_status_t::SYSTEM_INCLUDE_UNSUPPORTED;
        const string_view_t failurePath = TextBuffer_IsEmpty(
            &stage.includes.failurePath )
                ? stage.virtualPath
                : TextBuffer_View( &stage.includes.failurePath );
        EmitDiagnostic(
            request,
            report,
            bSystemInclude
                ? CY_SHADER_DIAGNOSTIC_UNSUPPORTED_INCLUDE
            : bIncludeFailure
                ? CY_SHADER_DIAGNOSTIC_INCLUDE_FAILED
                : CY_SHADER_DIAGNOSTIC_PREPROCESS_FAILED,
            tool_diagnostic_severity_t::ERROR,
            bIncludeFailure &&
                    stage.includes.status == shader_include_status_t::READ_FAILED
                ? tool_diagnostic_category_t::FILESYSTEM
                : tool_diagnostic_category_t::COMPILER,
            pLog != nullptr
                ? StringView_FromCString( pLog )
                : ShaderText( "GLSL preprocessing failed." ),
            failurePath,
            1u,
            1u,
            bIncludeFailure
                ? IncludeFailureHint( stage.includes )
                : string_view_t{} );
        return stage.includes.status == shader_include_status_t::OUT_OF_MEMORY
            ? tool_status_t::OUT_OF_MEMORY
        : stage.includes.status == shader_include_status_t::READ_FAILED
            ? tool_status_t::IO_ERROR
            : tool_status_t::VALIDATION_FAILED;
    }

    const char *pPreprocessed = glslang_shader_get_preprocessed_code(
        stage.compiler.pShader );
    if ( pPreprocessed == nullptr || pPreprocessed[0] == '\0' ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_PREPROCESS_FAILED,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::COMPILER,
            ShaderText( "GLSL preprocessing produced no source code." ),
            stage.virtualPath );
        return tool_status_t::VALIDATION_FAILED;
    }

    const string_view_t preprocessed = StringView_FromCString( pPreprocessed );
    if ( preprocessed.cchLength > CY_SHADER_COMPILER_MAX_STAGE_SIZE ||
         Unicode_ValidateUtf8( preprocessed ).status != unicode_status_t::OK ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_INVALID_TEXT,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::COMPILER,
            ShaderText( "Preprocessed GLSL exceeds the cooked format limit or is not UTF-8." ),
            stage.virtualPath );
        return tool_status_t::VALIDATION_FAILED;
    }
    if ( !TextBuffer_Assign( &stage.preprocessed, preprocessed ) ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::INTERNAL,
            ShaderText( "Out of memory while retaining preprocessed GLSL." ),
            stage.virtualPath );
        return tool_status_t::OUT_OF_MEMORY;
    }

    if ( !glslang_shader_parse( stage.compiler.pShader, &input ) ) {
        const char *pLog = GlslangLog( stage.compiler.pShader );
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_PARSE_FAILED,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::COMPILER,
            pLog != nullptr
                ? StringView_FromCString( pLog )
                : ShaderText( "GLSL parsing failed." ),
            stage.virtualPath );
        return tool_status_t::VALIDATION_FAILED;
    }

    const char *pLog = GlslangLog( stage.compiler.pShader );
    if ( pLog != nullptr ) {
        const bool_t bWarningsAsErrors =
            ( request.pInvocation->output.flags &
              TOOL_OUTPUT_FLAG_WARNINGS_AS_ERRORS ) != 0u;
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_PARSE_FAILED,
            bWarningsAsErrors
                ? tool_diagnostic_severity_t::ERROR
                : tool_diagnostic_severity_t::WARNING,
            tool_diagnostic_category_t::COMPILER,
            StringView_FromCString( pLog ),
            stage.virtualPath );
        if ( bWarningsAsErrors ) {
            return tool_status_t::VALIDATION_FAILED;
        }
    }
    return tool_status_t::OK;
}

CYPHER_NODISCARD tool_status_t LinkProgram(
    const tool_compile_request_t &request,
    tool_report_t &report,
    shader_compile_work_t &work ) noexcept
{
    shader_stage_work_t ( &stages )[CY_COOKED_SHADER_MAX_STAGES] =
        work.stages;
    const bool_t bReflectInterface = work.recipe.nSchemaVersion ==
        CY_RENDER_ASSET_SCHEMA_VERSION_V2;
    glslang_program_owner_t program{};
    program.pProgram = glslang_program_create();
    if ( program.pProgram == nullptr ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::COMPILER,
            ShaderText( "glslang could not create a shader program object." ) );
        return tool_status_t::INTERNAL_ERROR;
    }

    for ( usize iStage = 0u;
          iStage < CY_COOKED_SHADER_MAX_STAGES;
          ++iStage ) {
        glslang_program_add_shader(
            program.pProgram,
            stages[iStage].compiler.pShader );
    }

    const int messages = GLSLANG_MSG_DEFAULT_BIT |
                         GLSLANG_MSG_DISPLAY_ERROR_COLUMN |
                         GLSLANG_MSG_VALIDATE_CROSS_STAGE_IO_BIT |
                         ( bReflectInterface ? GLSLANG_MSG_SPV_RULES_BIT : 0 );
    if ( !glslang_program_link( program.pProgram, messages ) ) {
        const char *pLog = GlslangLog( program.pProgram );
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_LINK_FAILED,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::COMPILER,
            pLog != nullptr
                ? StringView_FromCString( pLog )
                : ShaderText( "GLSL vertex/fragment interface linking failed." ) );
        return tool_status_t::VALIDATION_FAILED;
    }
    if ( !bReflectInterface ) {
        return tool_status_t::OK;
    }
    if ( !glslang_program_map_io( program.pProgram ) ) {
        const char *pLog = GlslangLog( program.pProgram );
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_INTERFACE_FAILED,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::COMPILER,
            pLog != nullptr
                ? StringView_FromCString( pLog )
                : ShaderText( "glslang could not assign deterministic reflection bindings." ),
            request.input );
        return tool_status_t::VALIDATION_FAILED;
    }
    for ( shader_stage_work_t &stage : stages ) {
        const tool_status_t status = ReflectStageInterfaceV2(
            request,
            report,
            program.pProgram,
            stage,
            work );
        if ( ToolStatus_Failed( status ) ) {
            return status;
        }
    }
    return FinalizeReflectedInterfaceV2( request, report, work );
}

CYPHER_NODISCARD content_hash_t GlslangToolchainHash() noexcept
{
    glslang_version_t version{};
    glslang_get_version( &version );
    char identity[192]{};
    const string_format_result_t formatted = StringFormat_Printf(
        identity,
        sizeof( identity ),
        "glslang-%d.%d.%d-%s-vcpkg-%s-build-%s",
        version.major,
        version.minor,
        version.patch,
        version.flavor != nullptr ? version.flavor : "",
        CYPHER_VCPKG_BASELINE,
        CYPHER_GLSLANG_BUILD_CONTRACT );
    return formatted.status == string_format_status_t::OK
        ? ContentHash_String( StringView_FromCString( identity ) )
        : CY_CONTENT_HASH_INVALID;
}

CYPHER_NODISCARD content_hash_t ReflectionToolchainHash() noexcept
{
    // SPIRV-Cross does not publish a runtime version string through its C++
    // interface. The public C ABI version identifies the reflection contract,
    // while the pinned vcpkg baseline identifies the exact dependency graph
    // used to build the compiler. Together they make upgrades cache-visible.
    char identity[192]{};
    const string_format_result_t formatted = StringFormat_Printf(
        identity,
        sizeof( identity ),
        "spirv-cross-c-api-%u.%u.%u-vcpkg-%s",
        SPVC_C_API_VERSION_MAJOR,
        SPVC_C_API_VERSION_MINOR,
        SPVC_C_API_VERSION_PATCH,
        CYPHER_VCPKG_BASELINE );
    return formatted.status == string_format_status_t::OK
        ? ContentHash_String( StringView_FromCString( identity ) )
        : CY_CONTENT_HASH_INVALID;
}

CYPHER_NODISCARD content_hash_t CompilerHash(
    u32 nSchemaVersion,
    format_version_t nCookedVersion ) noexcept
{
    char identity[128]{};
    const string_format_result_t formatted = StringFormat_Printf(
        identity,
        sizeof( identity ),
        "cypher.shader-compiler.api%u.compiler%u.schema%u.cysh%u",
        CY_SHADER_COMPILER_API_VERSION,
        CY_SHADER_COMPILER_VERSION,
        nSchemaVersion,
        nCookedVersion );
    return formatted.status == string_format_status_t::OK
        ? ContentHash_String( StringView_FromCString( identity ) )
        : CY_CONTENT_HASH_INVALID;
}

CYPHER_NODISCARD content_hash_t ConfigurationHash(
    const tool_context_t &context ) noexcept
{
    // Target controls the accepted GLSL ceiling, and profile is a semantic
    // build input. Keep this identity visible as a configuration dependency so
    // cache diagnostics can distinguish it from compiler/toolchain changes.
    content_hash_t hash = ContentHash_String(
        ShaderText( "cypher-shader-build-configuration-v1" ) );
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
    const shader_compile_work_t &work,
    content_hash_t recipeHash,
    content_hash_t compilerHash,
    content_hash_t glslangToolchainHash,
    content_hash_t reflectionToolchainHash,
    content_hash_t configurationHash ) noexcept
{
    const tool_dependency_t directDependencies[]{
        {
            request.input,
            tool_dependency_kind_t::SOURCE,
            recipeHash,
            TOOL_DEPENDENCY_FLAG_REQUIRED
        },
        {
            work.stages[0].virtualPath,
            tool_dependency_kind_t::SOURCE,
            work.stages[0].sourceHash,
            TOOL_DEPENDENCY_FLAG_REQUIRED
        },
        {
            work.stages[1].virtualPath,
            tool_dependency_kind_t::SOURCE,
            work.stages[1].sourceHash,
            TOOL_DEPENDENCY_FLAG_REQUIRED
        }
    };
    for ( const tool_dependency_t &dependency : directDependencies ) {
        ToolHost_EmitDependency( request.pInvocation->pHost, dependency );
    }
    for ( usize iFile = 0u;
          iFile < work.includeCache.nFiles;
          ++iFile ) {
        const shader_include_file_t &file = work.includeCache.files[iFile];
        const tool_dependency_t dependency{
            TextBuffer_View( &file.virtualPath ),
            tool_dependency_kind_t::SOURCE,
            file.sourceHash,
            TOOL_DEPENDENCY_FLAG_REQUIRED |
                TOOL_DEPENDENCY_FLAG_TRANSITIVE
        };
        ToolHost_EmitDependency( request.pInvocation->pHost, dependency );
    }
    const tool_dependency_t toolchainDependencies[]{
        {
            ShaderText( "toolchain/cypher-shader-compiler" ),
            tool_dependency_kind_t::TOOLCHAIN,
            compilerHash,
            TOOL_DEPENDENCY_FLAG_REQUIRED
        },
        {
            ShaderText( "toolchain/glslang" ),
            tool_dependency_kind_t::TOOLCHAIN,
            glslangToolchainHash,
            TOOL_DEPENDENCY_FLAG_REQUIRED
        }
    };
    for ( const tool_dependency_t &dependency : toolchainDependencies ) {
        ToolHost_EmitDependency( request.pInvocation->pHost, dependency );
    }
    if ( work.recipe.nSchemaVersion ==
         CY_RENDER_ASSET_SCHEMA_VERSION_V2 ) {
        const tool_dependency_t reflectionDependency{
            ShaderText( "toolchain/spirv-cross" ),
            tool_dependency_kind_t::TOOLCHAIN,
            reflectionToolchainHash,
            TOOL_DEPENDENCY_FLAG_REQUIRED
        };
        ToolHost_EmitDependency(
            request.pInvocation->pHost,
            reflectionDependency );
    }
    const tool_dependency_t configurationDependency{
        ShaderText( "configuration/shader-target-profile" ),
        tool_dependency_kind_t::CONFIGURATION,
        configurationHash,
        TOOL_DEPENDENCY_FLAG_REQUIRED
    };
    ToolHost_EmitDependency(
        request.pInvocation->pHost,
        configurationDependency );
}

CYPHER_NODISCARD bool_t ProbeShader(
    string_view_t input,
    void * ) noexcept
{
    return StringPath_HasExtension(
        input,
        ShaderText( ".cyshader" ),
        CY_TRUE );
}

CYPHER_NODISCARD tool_status_t ExecuteShaderCompiler(
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
        ShaderText( "Read recipe" ) );

    const bool_t bDryRun =
        ( request.pInvocation->flags & TOOL_INVOCATION_FLAG_DRY_RUN ) != 0u;
    const bool_t bInputPathValid = DataValidation_Succeeded(
        DataValidation_CheckResourcePath(
            request.input,
            ShaderText( ".cyshader" ),
            CY_SHADER_COMPILER_MAX_PATH ) );
    const bool_t bOutputPathValid = bDryRun && request.output.cchLength == 0u
        ? CY_TRUE
        : DataValidation_Succeeded(
              DataValidation_CheckResourcePath(
                  request.output,
                  ShaderText( ".cyshader_c" ),
                  CY_SHADER_COMPILER_MAX_PATH ) );
    if ( !bInputPathValid || !bOutputPathValid ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_INVALID_PATH,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::VALIDATION,
            ShaderText(
                "Shader input and output must be canonical virtual resource paths." ),
            !bInputPathValid ? request.input : request.output );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::INVALID_ARGUMENT,
            nCompleted );
        return tool_status_t::INVALID_ARGUMENT;
    }

    shader_compile_work_t work{};
    if ( !InitCompileWork( work ) ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::INTERNAL,
            ShaderText( "Out of memory while initializing shader compilation." ) );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::OUT_OF_MEMORY,
            nCompleted );
        return tool_status_t::OUT_OF_MEMORY;
    }
    const tool_context_t &context = *request.pInvocation->pContext;
    if ( !Vfs_IsValid( context.pSourceVfs ) ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_INVALID_PATH,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::FILESYSTEM,
            ShaderText( "Shader compilation requires a valid source VFS." ),
            request.input );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::INVALID_CONFIGURATION,
            nCompleted );
        return tool_status_t::INVALID_CONFIGURATION;
    }
    work.includeCache.pVfs = context.pSourceVfs;
    if ( !bDryRun &&
         !JoinNativePath(
             context.outputRoot,
             request.output,
             work.outputNativePath ) ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_INVALID_PATH,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::FILESYSTEM,
            ShaderText( "Shader path could not be resolved below its configured root." ),
            request.input );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::INVALID_CONFIGURATION,
            nCompleted );
        return tool_status_t::INVALID_CONFIGURATION;
    }
    if ( IsCancellationRequested( request, report, sequence, nCompleted ) ) {
        return tool_status_t::CANCELLED;
    }

    ResolveDiagnosticPath(
        context.pSourceVfs,
        request.input,
        work.recipeDiagnosticPath );
    vfs_status_t vfsStatus = vfs_status_t::OK;
    shader_text_read_status_t readStatus = ReadTextFile(
        context.pSourceVfs,
        request.input,
        CY_SHADER_COMPILER_MAX_RECIPE_SIZE,
        work.recipeText,
        &vfsStatus );
    if ( readStatus != shader_text_read_status_t::OK ) {
        const tool_status_t status = ReportReadFailure(
            request,
            report,
            request.input,
            TextBuffer_View( &work.recipeDiagnosticPath ),
            readStatus,
            vfsStatus,
            CY_TRUE );
        EmitFailureProgress( request, sequence, status, nCompleted );
        return status;
    }
    report.cbRead += work.recipeText.cchLength;

    key_value_document_desc_t documentDesc{};
    documentDesc.pAllocator = Allocator_GetSystem();
    work.document.pDocument = KeyValue_CreateDocument( documentDesc );
    if ( work.document.pDocument == nullptr ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::INTERNAL,
            ShaderText( "Out of memory while creating the CYKV document." ),
            request.input );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::OUT_OF_MEMORY,
            nCompleted );
        return tool_status_t::OUT_OF_MEMORY;
    }

    const key_value_parse_result_t parsed = KeyValue_ParseText(
        TextBuffer_View( &work.recipeText ),
        {},
        work.document.pDocument );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_CYKV_PARSE_FAILED,
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

    schema_diagnostic_t diagnostics[CY_SHADER_COMPILER_SCHEMA_DIAGNOSTICS]{};
    const key_value_document_header_t documentHeader =
        KeyValue_DocumentHeader( work.document.pDocument );
    render_asset_decode_result_t decoded{};
    switch ( documentHeader.nSchemaVersion ) {
        case CY_RENDER_ASSET_SCHEMA_VERSION_V1:
            decoded = RenderShaderSource_Decode(
                work.document.pDocument,
                {},
                diagnostics,
                CYPHER_ARRAY_COUNT( diagnostics ),
                &work.recipeV1 );
            if ( RenderAsset_DecodeSucceeded( decoded ) ) {
                work.recipe.nSchemaVersion =
                    CY_RENDER_ASSET_SCHEMA_VERSION_V1;
                work.recipe.nCookedVersion =
                    CY_COOKED_SHADER_RESOURCE_VERSION_V2;
                work.recipe.language = work.recipeV1.language;
                work.recipe.vertexSource = work.recipeV1.vertexSource;
                work.recipe.fragmentSource = work.recipeV1.fragmentSource;
                work.recipe.vertexEntry = ShaderText( "main" );
                work.recipe.fragmentEntry = ShaderText( "main" );
                work.recipe.pDefines = work.recipeV1.defines;
                work.recipe.nDefines = work.recipeV1.nDefines;
            }
            break;
        case CY_RENDER_ASSET_SCHEMA_VERSION_V2:
            decoded = RenderShaderSourceV2_Decode(
                work.document.pDocument,
                {},
                diagnostics,
                CYPHER_ARRAY_COUNT( diagnostics ),
                &work.recipeV2 );
            if ( RenderAsset_DecodeSucceeded( decoded ) ) {
                work.recipe.nSchemaVersion =
                    CY_RENDER_ASSET_SCHEMA_VERSION_V2;
                work.recipe.nCookedVersion =
                    CY_COOKED_SHADER_RESOURCE_VERSION_V3;
                work.recipe.language = work.recipeV2.language;
                work.recipe.vertexSource = work.recipeV2.vertex.source;
                work.recipe.fragmentSource = work.recipeV2.fragment.source;
                work.recipe.vertexEntry = work.recipeV2.vertex.entry;
                work.recipe.fragmentEntry = work.recipeV2.fragment.entry;
                work.recipe.pDefines = work.recipeV2.defines;
                work.recipe.nDefines = work.recipeV2.nDefines;
            }
            break;
        default:
            EmitDiagnostic(
                request,
                report,
                CY_SHADER_DIAGNOSTIC_SCHEMA_FAILED,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::SCHEMA,
                ShaderText( "Shader schema version must be 1 or 2." ),
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
    if ( !RenderAsset_DecodeSucceeded( decoded ) ) {
        if ( decoded.validation.nDiagnosticsWritten != 0u ) {
            for ( usize iDiagnostic = 0u;
                  iDiagnostic < decoded.validation.nDiagnosticsWritten;
                  ++iDiagnostic ) {
                const schema_diagnostic_t &schemaDiagnostic =
                    diagnostics[iDiagnostic];
                text_location_t sourceLocation{};
                switch ( schemaDiagnostic.code ) {
                    case schema_diagnostic_code_t::LANGUAGE_VERSION_MISMATCH:
                        sourceLocation = parsed.languageVersionLocation;
                        break;
                    case schema_diagnostic_code_t::SCHEMA_ID_MISMATCH:
                        sourceLocation = parsed.schemaIdLocation;
                        break;
                    case schema_diagnostic_code_t::SCHEMA_VERSION_MISMATCH:
                        sourceLocation = parsed.schemaVersionLocation;
                        break;
                    default:
                        break;
                }
                char message[512]{};
                const string_format_result_t formatted = StringFormat_Printf(
                    message,
                    sizeof( message ),
                    "%s at %s",
                    Schema_DiagnosticCodeName( schemaDiagnostic.code ),
                    schemaDiagnostic.path[0] != '\0'
                        ? schemaDiagnostic.path
                        : "$" );
                EmitDiagnostic(
                    request,
                    report,
                    CY_SHADER_DIAGNOSTIC_SCHEMA_FAILED,
                    schemaDiagnostic.severity ==
                            schema_diagnostic_severity_t::WARNING
                        ? tool_diagnostic_severity_t::WARNING
                        : tool_diagnostic_severity_t::ERROR,
                    tool_diagnostic_category_t::SCHEMA,
                    formatted.status == string_format_status_t::OK
                        ? StringView_FromCString( message )
                        : ShaderText( "Shader schema validation failed." ),
                    request.input,
                    sourceLocation.nLine,
                    sourceLocation.nColumn );
            }
        } else {
            char message[512]{};
            const string_view_t statusName = StringView_FromCString(
                RenderAsset_DecodeStatusName( decoded.status ) );
            string_format_result_t formatted{};
            if ( decoded.field.cchLength != 0u &&
                 decoded.iElement != CY_INVALID_SIZE ) {
                formatted = StringFormat_Printf(
                    message,
                    sizeof( message ),
                    "%.*s at %.*s[%zu]",
                    static_cast<int>( statusName.cchLength ),
                    statusName.pData,
                    static_cast<int>( decoded.field.cchLength ),
                    decoded.field.pData,
                    decoded.iElement );
            } else if ( decoded.field.cchLength != 0u ) {
                formatted = StringFormat_Printf(
                    message,
                    sizeof( message ),
                    "%.*s at %.*s",
                    static_cast<int>( statusName.cchLength ),
                    statusName.pData,
                    static_cast<int>( decoded.field.cchLength ),
                    decoded.field.pData );
            }
            EmitDiagnostic(
                request,
                report,
                CY_SHADER_DIAGNOSTIC_SCHEMA_FAILED,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::SCHEMA,
                decoded.field.cchLength != 0u &&
                        formatted.status == string_format_status_t::OK
                    ? StringView_FromCString( message )
                    : statusName,
                request.input );
        }
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::VALIDATION_FAILED,
            nCompleted );
        return tool_status_t::VALIDATION_FAILED;
    }
    if ( work.recipe.nSchemaVersion == CY_RENDER_ASSET_SCHEMA_VERSION_V2 ) {
        if ( !StringView_Equals(
                 work.recipe.vertexEntry,
                 ShaderText( "main" ) ) ||
             !StringView_Equals(
                 work.recipe.fragmentEntry,
                 ShaderText( "main" ) ) ) {
            EmitDiagnostic(
                request,
                report,
                CY_SHADER_DIAGNOSTIC_UNSUPPORTED_ENTRY_POINT,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::COMPILER,
                ShaderText(
                    "OpenGL GLSL cooked sources currently require `main` for both stages." ),
                request.input,
                1u,
                1u,
                ShaderText(
                    "Rename the authored entry point to `main`; CYSH V3 does not persist an alternate runtime entry point." ) );
            MarkFailed( report );
            EmitFailureProgress(
                request,
                sequence,
                tool_status_t::VALIDATION_FAILED,
                nCompleted );
            return tool_status_t::VALIDATION_FAILED;
        }
        if ( work.recipeV2.nSamplers != 0u ) {
            const string_view_t samplerName =
                work.recipeV2.samplers[0].name;
            char message[512]{};
            const string_format_result_t formatted = StringFormat_Printf(
                message,
                sizeof( message ),
                "Independent sampler `%.*s` has no versioned texture association.",
                static_cast<int>( samplerName.cchLength ),
                samplerName.pData );
            EmitDiagnostic(
                request,
                report,
                CY_SHADER_DIAGNOSTIC_UNSUPPORTED_SAMPLERS,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::VALIDATION,
                formatted.status == string_format_status_t::OK
                    ? StringView_FromCString( message )
                    : ShaderText(
                          "Independent shader samplers are not representable by the current cooked interface." ),
                request.input,
                1u,
                1u,
                ShaderText(
                    "Remove interface.samplers and declare each active GLSL combined sampler under interface.textures; independent sampler association requires a future schema." ) );
            MarkFailed( report );
            EmitFailureProgress(
                request,
                sequence,
                tool_status_t::VALIDATION_FAILED,
                nCompleted );
            return tool_status_t::VALIDATION_FAILED;
        }
        if ( work.recipeV2.nFeatures != 0u ) {
            EmitDiagnostic(
                request,
                report,
                CY_SHADER_DIAGNOSTIC_UNSUPPORTED_FEATURES,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::COMPILER,
                ShaderText(
                    "Shader features require a cooked variant table, which CYSH V3 does not contain." ),
                request.input,
                1u,
                1u,
                ShaderText(
                    "Remove `features` until a versioned shader-variant contract is available." ) );
            MarkFailed( report );
            EmitFailureProgress(
                request,
                sequence,
                tool_status_t::VALIDATION_FAILED,
                nCompleted );
            return tool_status_t::VALIDATION_FAILED;
        }
        if ( !BuildV2CookedInterface( work ) ) {
            EmitDiagnostic(
                request,
                report,
                CY_SHADER_DIAGNOSTIC_INTERFACE_FAILED,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::COMPILER,
                ShaderText(
                    "Shader logical interface could not be canonicalized or packed." ),
                request.input );
            MarkFailed( report );
            EmitFailureProgress(
                request,
                sequence,
                tool_status_t::VALIDATION_FAILED,
                nCompleted );
            return tool_status_t::VALIDATION_FAILED;
        }
    }
    if ( !BuildDefinePreamble(
             work.recipe.pDefines,
             work.recipe.nDefines,
             work.definePreamble ) ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::INTERNAL,
            ShaderText( "Out of memory while constructing shader definitions." ),
            request.input );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::OUT_OF_MEMORY,
            nCompleted );
        return tool_status_t::OUT_OF_MEMORY;
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        ShaderText( "Load dependencies" ) );
    if ( IsCancellationRequested( request, report, sequence, nCompleted ) ) {
        return tool_status_t::CANCELLED;
    }

    work.stages[0].virtualPath = work.recipe.vertexSource;
    work.stages[0].stage = render_shader_stage_t::VERTEX;
    work.stages[0].glslangStage = GLSLANG_STAGE_VERTEX;
    work.stages[1].virtualPath = work.recipe.fragmentSource;
    work.stages[1].stage = render_shader_stage_t::FRAGMENT;
    work.stages[1].glslangStage = GLSLANG_STAGE_FRAGMENT;
    for ( usize iStage = 0u;
          iStage < CY_COOKED_SHADER_MAX_STAGES;
          ++iStage ) {
        shader_stage_work_t &stage = work.stages[iStage];
        ResolveDiagnosticPath(
            context.pSourceVfs,
            stage.virtualPath,
            stage.diagnosticPath );
        readStatus = ReadTextFile(
            context.pSourceVfs,
            stage.virtualPath,
            CY_SHADER_COMPILER_MAX_STAGE_SIZE,
            stage.source,
            &vfsStatus );
        if ( readStatus != shader_text_read_status_t::OK ) {
            const tool_status_t status = ReportReadFailure(
                request,
                report,
                stage.virtualPath,
                TextBuffer_View( &stage.diagnosticPath ),
                readStatus,
                vfsStatus,
                CY_FALSE );
            EmitFailureProgress( request, sequence, status, nCompleted );
            return status;
        }
        report.cbRead += stage.source.cchLength;
        const glsl_profile_parse_result_t profile = ParseGlslProfile(
            TextBuffer_View( &stage.source ) );
        if ( profile.status != glsl_profile_parse_status_t::OK ) {
            EmitDiagnostic(
                request,
                report,
                CY_SHADER_DIAGNOSTIC_UNSUPPORTED_GLSL_PROFILE,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::SOURCE,
                ShaderText(
                    "The GLSL version/profile directive is missing, malformed, or unsupported." ),
                stage.virtualPath,
                profile.nLine,
                profile.nColumn,
                GlslProfileFailureHint( profile.status ) );
            MarkFailed( report );
            EmitFailureProgress(
                request,
                sequence,
                tool_status_t::VALIDATION_FAILED,
                nCompleted );
            return tool_status_t::VALIDATION_FAILED;
        }
        const u32 nTargetMaximum = TargetMaximumGlslCoreVersion(
            context.target );
        if ( profile.nVersion > nTargetMaximum ) {
            EmitDiagnostic(
                request,
                report,
                CY_SHADER_DIAGNOSTIC_UNSUPPORTED_GLSL_PROFILE,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::SOURCE,
                ShaderText(
                    "The GLSL version exceeds the selected target platform capability." ),
                stage.virtualPath,
                profile.nLine,
                profile.nColumn,
                context.target.platform == tool_platform_t::MACOS
                    ? ShaderText(
                          "macOS OpenGL targets are capped at GLSL 410 core." )
                    : ShaderText(
                          "Windows and Linux OpenGL targets are capped at GLSL 450 core." ) );
            MarkFailed( report );
            EmitFailureProgress(
                request,
                sequence,
                tool_status_t::VALIDATION_FAILED,
                nCompleted );
            return tool_status_t::VALIDATION_FAILED;
        }
        stage.nGlslVersion = profile.nVersion;
        stage.nGlslDirectiveLine = profile.nLine;
        stage.nGlslDirectiveColumn = profile.nColumn;
        stage.sourceHash = ContentHash_String(
            TextBuffer_View( &stage.source ) );
    }
    if ( work.stages[0].nGlslVersion != work.stages[1].nGlslVersion ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_GLSL_PROFILE_MISMATCH,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::SOURCE,
            ShaderText(
                "Vertex and fragment stages declare different GLSL core versions." ),
            work.stages[1].virtualPath,
            work.stages[1].nGlslDirectiveLine,
            work.stages[1].nGlslDirectiveColumn,
            ShaderText(
                "All stages in one graphics program must use the same `#version NNN core` directive." ) );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::VALIDATION_FAILED,
            nCompleted );
        return tool_status_t::VALIDATION_FAILED;
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        ShaderText( "Validate GLSL" ) );
    if ( IsCancellationRequested( request, report, sequence, nCompleted ) ) {
        return tool_status_t::CANCELLED;
    }
    if ( !GlslangRuntime().bInitialized ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::COMPILER,
            ShaderText( "glslang process initialization failed." ) );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::INTERNAL_ERROR,
            nCompleted );
        return tool_status_t::INTERNAL_ERROR;
    }

    for ( usize iStage = 0u;
          iStage < CY_COOKED_SHADER_MAX_STAGES;
          ++iStage ) {
        const tool_status_t status = CompileStage(
            request,
            report,
            work.definePreamble,
            work.recipe.nSchemaVersion ==
                CY_RENDER_ASSET_SCHEMA_VERSION_V2,
            work.stages[iStage] );
        if ( ToolStatus_Failed( status ) ) {
            MarkFailed( report );
            EmitFailureProgress( request, sequence, status, nCompleted );
            return status;
        }
    }
    tool_status_t status = LinkProgram( request, report, work );
    if ( ToolStatus_Failed( status ) ) {
        MarkFailed( report );
        EmitFailureProgress( request, sequence, status, nCompleted );
        return status;
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        ShaderText( "Build cooked resource" ) );
    if ( IsCancellationRequested( request, report, sequence, nCompleted ) ) {
        return tool_status_t::CANCELLED;
    }

    const key_value_canonical_hash_result_t canonicalRecipe =
        KeyValue_HashCanonicalDocument( work.document.pDocument );
    const content_hash_t recipeHash = canonicalRecipe.hash;
    const content_hash_t compilerHash = CompilerHash(
        work.recipe.nSchemaVersion,
        work.recipe.nCookedVersion );
    const content_hash_t glslangToolchainHash = GlslangToolchainHash();
    const bool_t bReflectInterface = work.recipe.nSchemaVersion ==
        CY_RENDER_ASSET_SCHEMA_VERSION_V2;
    const content_hash_t reflectionToolchainHash = bReflectInterface
        ? ReflectionToolchainHash()
        : CY_CONTENT_HASH_INVALID;
    const content_hash_t configurationHash = ConfigurationHash( context );
    content_hash_t sourceHash = ContentHash_Combine(
        compilerHash,
        recipeHash );
    sourceHash = ContentHash_Combine(
        sourceHash,
        work.stages[0].sourceHash );
    sourceHash = ContentHash_Combine(
        sourceHash,
        work.stages[1].sourceHash );
    if ( work.recipe.nCookedVersion ==
         CY_COOKED_SHADER_RESOURCE_VERSION_V3 ) {
        sourceHash = ContentHash_Combine(
            sourceHash,
            work.interfaceHash );
    }
    for ( usize iFile = 0u;
          iFile < work.includeCache.nFiles;
          ++iFile ) {
        const shader_include_file_t &file = work.includeCache.files[iFile];
        sourceHash = ContentHash_Combine(
            sourceHash,
            ContentHash_String( TextBuffer_View( &file.virtualPath ) ) );
        sourceHash = ContentHash_Combine( sourceHash, file.sourceHash );
    }
    sourceHash = ContentHash_Combine(
        sourceHash,
        glslangToolchainHash );
    if ( bReflectInterface ) {
        sourceHash = ContentHash_Combine(
            sourceHash,
            reflectionToolchainHash );
    }
    sourceHash = ContentHash_Combine( sourceHash, configurationHash );
    if ( canonicalRecipe.status != key_value_write_status_t::OK ||
         !ContentHash_IsValid( recipeHash ) ||
         !ContentHash_IsValid( compilerHash ) ||
         !ContentHash_IsValid( glslangToolchainHash ) ||
         ( bReflectInterface &&
           !ContentHash_IsValid( reflectionToolchainHash ) ) ||
         !ContentHash_IsValid( configurationHash ) ||
         !ContentHash_IsValid( work.stages[0].sourceHash ) ||
         !ContentHash_IsValid( work.stages[1].sourceHash ) ||
         ( work.recipe.nCookedVersion ==
               CY_COOKED_SHADER_RESOURCE_VERSION_V3 &&
           !ContentHash_IsValid( work.interfaceHash ) ) ||
         !ContentHash_IsValid( sourceHash ) ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::INTERNAL,
            ShaderText( "Shader source identity could not be constructed." ) );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::INTERNAL_ERROR,
            nCompleted );
        return tool_status_t::INTERNAL_ERROR;
    }

    cooked_shader_stage_source_t cookedStages[CY_COOKED_SHADER_MAX_STAGES]{};
    for ( usize iStage = 0u;
          iStage < CY_COOKED_SHADER_MAX_STAGES;
          ++iStage ) {
        const shader_stage_work_t &stage = work.stages[iStage];
        cookedStages[iStage].stage = stage.stage;
        cookedStages[iStage].codeFormat =
            render_shader_code_format_t::GLSL_UTF8;
        cookedStages[iStage].code = {
            reinterpret_cast<const byte *>(
                TextBuffer_CStr( &stage.preprocessed ) ),
            stage.preprocessed.cchLength + 1u
        };
    }
    cooked_shader_desc_t shaderDesc{};
    shaderDesc.languageProfile =
        render_shader_language_profile_t::GLSL_CORE;
    shaderDesc.nLanguageVersion = work.stages[0].nGlslVersion;
    const span_t<const cooked_shader_stage_source_t> stageSpan{
        cookedStages,
        CY_COOKED_SHADER_MAX_STAGES
    };
    const cooked_shader_interface_source_t shaderInterface{
        { work.bindings, work.nBindings }
    };
    const bool_t bWriteV3 = work.recipe.nCookedVersion ==
        CY_COOKED_SHADER_RESOURCE_VERSION_V3;
    const usize cbCooked = bWriteV3
        ? CookedShader_RequiredSizeV3(
              shaderDesc,
              stageSpan,
              shaderInterface )
        : CookedShader_RequiredSize(
              shaderDesc,
              stageSpan );
    if ( cbCooked == 0u || !Blob_Resize( &work.cooked, cbCooked ) ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::COMPILER,
            ShaderText( "Cooked shader size is invalid or could not be allocated." ) );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            cbCooked == 0u
                ? tool_status_t::INTERNAL_ERROR
                : tool_status_t::OUT_OF_MEMORY,
            nCompleted );
        return cbCooked == 0u
            ? tool_status_t::INTERNAL_ERROR
            : tool_status_t::OUT_OF_MEMORY;
    }
    const cooked_shader_result_t cooked = bWriteV3
        ? CookedShader_WriteV3(
              shaderDesc,
              stageSpan,
              shaderInterface,
              sourceHash,
              Blob_WritableSpan( &work.cooked ) )
        : CookedShader_Write(
              shaderDesc,
              stageSpan,
              sourceHash,
              Blob_WritableSpan( &work.cooked ) );
    if ( !CookedShader_Succeeded( cooked ) ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_COOK_FAILED,
            tool_diagnostic_severity_t::FATAL,
            tool_diagnostic_category_t::COMPILER,
            StringView_FromCString(
                CookedShader_StatusName( cooked.status ) ) );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::INTERNAL_ERROR,
            nCompleted );
        return tool_status_t::INTERNAL_ERROR;
    }

    ++nCompleted;
    EmitProgress(
        request,
        sequence++,
        tool_progress_state_t::UPDATE,
        tool_status_t::OK,
        nCompleted,
        bDryRun ? ShaderText( "Validate output" ) : ShaderText( "Write output" ) );
    if ( IsCancellationRequested( request, report, sequence, nCompleted ) ) {
        return tool_status_t::CANCELLED;
    }

    const tool_status_t writeStatus = bDryRun
        ? tool_status_t::OK
        : ToolArtifactWriter_WriteNative(
              TextBuffer_View( &work.outputNativePath ),
              Blob_Block( &work.cooked ) );
    if ( ToolStatus_Failed( writeStatus ) ) {
        EmitDiagnostic(
            request,
            report,
            CY_SHADER_DIAGNOSTIC_WRITE_FAILED,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::FILESYSTEM,
            ShaderText( "Cooked shader output could not be written." ),
            request.output );
        MarkFailed( report );
        EmitFailureProgress(
            request,
            sequence,
            tool_status_t::IO_ERROR,
            nCompleted );
        return writeStatus;
    }

    EmitDependencies(
        request,
        work,
        recipeHash,
        compilerHash,
        glslangToolchainHash,
        reflectionToolchainHash,
        configurationHash );
    if ( !bDryRun ) {
        const tool_artifact_t artifact{
            request.output,
            ShaderText( "application/x-cypher-shader" ),
            tool_artifact_kind_t::COOKED_RESOURCE,
            ContentHash_Data( Blob_Block( &work.cooked ) ),
            work.cooked.cbSize,
            TOOL_ARTIFACT_FLAG_PRIMARY |
                TOOL_ARTIFACT_FLAG_GENERATED
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
        bDryRun ? ShaderText( "Validated" ) : ShaderText( "Compiled" ) );
    return tool_status_t::OK;
}

inline constexpr string_view_t g_shaderSourceExtensions[]{
    ShaderText( ".cyshader" )
};

const tool_compiler_desc_t g_shaderCompiler{
    ShaderText( "cypher.shader" ),
    ShaderText( "Cypher Shader Compiler" ),
    ShaderText( "shader" ),
    ShaderText( ".cyshader_c" ),
    g_shaderSourceExtensions,
    CYPHER_ARRAY_COUNT( g_shaderSourceExtensions ),
    CY_SHADER_COMPILER_API_VERSION,
    CY_SHADER_COMPILER_VERSION,
    TOOL_COMPILER_FLAG_DETERMINISTIC |
        TOOL_COMPILER_FLAG_THREAD_SAFE |
        TOOL_COMPILER_FLAG_SUPPORTS_VALIDATE |
        TOOL_COMPILER_FLAG_SUPPORTS_DRY_RUN,
    &ProbeShader,
    &ExecuteShaderCompiler,
    nullptr
};

} // namespace

const tool_compiler_desc_t *CypherShaderCompiler_Descriptor() noexcept
{
    return &g_shaderCompiler;
}

} // namespace cypher::tools
