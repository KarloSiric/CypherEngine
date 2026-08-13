//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp
//  Purpose: Implements deterministic OpenGL shader recipe compilation.
//  Details: The compiler parses CYKV, validates the exact shader schema, loads
//           stage dependencies, preprocesses and links GLSL with glslang, and
//           writes the canonical CYSH/CYRS runtime resource. Version 1 rejects
//           includes until dependency-safe include resolution is implemented.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherShaderCompiler.h"

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Blob.h"
#include "CypherCommon_CookedShader.h"
#include "CypherCommon_DataValidation.h"
#include "CypherCommon_FileIo.h"
#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_RenderAsset.h"
#include "CypherCommon_StringFormat.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_TextBuffer.h"
#include "CypherCommon_ToolArtifactWriter.h"
#include "CypherCommon_Unicode.h"
#include "CypherCommon_Vfs.h"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <cstring>

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

struct shader_stage_work_t {
    string_view_t virtualPath{};
    text_buffer_t diagnosticPath{};
    text_buffer_t source{};
    text_buffer_t preprocessed{};
    content_hash_t sourceHash{};
    render_shader_stage_t stage{ render_shader_stage_t::VERTEX };
    glslang_stage_t glslangStage{ GLSLANG_STAGE_VERTEX };
    glslang_shader_owner_t compiler{};
};

struct shader_compile_work_t {
    text_buffer_t recipeDiagnosticPath{};
    text_buffer_t outputNativePath{};
    text_buffer_t recipeText{};
    text_buffer_t definePreamble{};
    key_value_document_owner_t document{};
    render_shader_source_view_t recipe{};
    shader_stage_work_t stages[CY_COOKED_SHADER_MAX_STAGES]{};
    blob_t cooked{};
};

struct include_rejection_t {
    bool_t bAttempted{ CY_FALSE };
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
    return InitTextBuffer( work.recipeDiagnosticPath ) &&
           InitTextBuffer( work.outputNativePath ) &&
           InitTextBuffer( work.recipeText ) &&
           InitTextBuffer( work.definePreamble ) &&
           InitTextBuffer( work.stages[0].diagnosticPath ) &&
           InitTextBuffer( work.stages[0].source ) &&
           InitTextBuffer( work.stages[0].preprocessed ) &&
           InitTextBuffer( work.stages[1].diagnosticPath ) &&
           InitTextBuffer( work.stages[1].source ) &&
           InitTextBuffer( work.stages[1].preprocessed ) &&
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
    const render_shader_source_view_t &recipe,
    text_buffer_t &preamble ) noexcept
{
    for ( usize iDefine = 0u; iDefine < recipe.nDefines; ++iDefine ) {
        if ( !TextBuffer_Append( &preamble, ShaderText( "#define " ) ) ||
             !TextBuffer_Append( &preamble, recipe.defines[iDefine] ) ||
             !TextBuffer_Append( &preamble, ShaderText( " 1\n" ) ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

glsl_include_result_t *RejectInclude(
    void *pContext,
    const char *,
    const char *,
    size_t ) noexcept
{
    // Returning no result prevents glslang's default includer from escaping the
    // project root. Includes are enabled only after canonical dependency tracking.
    if ( pContext != nullptr ) {
        static_cast<include_rejection_t *>( pContext )->bAttempted = CY_TRUE;
    }
    return nullptr;
}

int ReleaseInclude( void *, glsl_include_result_t * ) noexcept
{
    return 0;
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

CYPHER_NODISCARD tool_status_t CompileStage(
    const tool_compile_request_t &request,
    tool_report_t &report,
    const text_buffer_t &preamble,
    shader_stage_work_t &stage ) noexcept
{
    include_rejection_t includeRejection{};
    const glslang_input_t input{
        GLSLANG_SOURCE_GLSL,
        stage.glslangStage,
        GLSLANG_CLIENT_OPENGL,
        GLSLANG_TARGET_OPENGL_450,
        GLSLANG_TARGET_NONE,
        GLSLANG_TARGET_SPV_1_0,
        TextBuffer_CStr( &stage.source ),
        450,
        GLSLANG_CORE_PROFILE,
        false,
        false,
        static_cast<glslang_messages_t>(
            GLSLANG_MSG_DEFAULT_BIT |
            GLSLANG_MSG_DISPLAY_ERROR_COLUMN ),
        glslang_default_resource(),
        { &RejectInclude, &RejectInclude, &ReleaseInclude },
        &includeRejection
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
    glslang_shader_set_preamble(
        stage.compiler.pShader,
        TextBuffer_CStr( &preamble ) );

    if ( !glslang_shader_preprocess( stage.compiler.pShader, &input ) ) {
        const char *pLog = GlslangLog( stage.compiler.pShader );
        const string_view_t hint = includeRejection.bAttempted
            ? ShaderText(
                  "Shader includes are disabled in `.cyshader` version 1." )
            : string_view_t{};
        EmitDiagnostic(
            request,
            report,
            includeRejection.bAttempted
                ? CY_SHADER_DIAGNOSTIC_UNSUPPORTED_INCLUDE
                : CY_SHADER_DIAGNOSTIC_PREPROCESS_FAILED,
            tool_diagnostic_severity_t::ERROR,
            tool_diagnostic_category_t::COMPILER,
            pLog != nullptr
                ? StringView_FromCString( pLog )
                : ShaderText( "GLSL preprocessing failed." ),
            stage.virtualPath,
            1u,
            1u,
            hint );
        return tool_status_t::VALIDATION_FAILED;
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
    shader_stage_work_t ( &stages )[CY_COOKED_SHADER_MAX_STAGES] ) noexcept
{
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
                         GLSLANG_MSG_VALIDATE_CROSS_STAGE_IO_BIT;
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
    return tool_status_t::OK;
}

CYPHER_NODISCARD content_hash_t ToolchainHash() noexcept
{
    glslang_version_t version{};
    glslang_get_version( &version );
    char identity[128]{};
    const string_format_result_t formatted = StringFormat_Printf(
        identity,
        sizeof( identity ),
        "glslang-%d.%d.%d-%s",
        version.major,
        version.minor,
        version.patch,
        version.flavor != nullptr ? version.flavor : "" );
    return formatted.status == string_format_status_t::OK
        ? ContentHash_String( StringView_FromCString( identity ) )
        : CY_CONTENT_HASH_INVALID;
}

CYPHER_NODISCARD content_hash_t CompilerHash() noexcept
{
    return ContentHash_String(
        ShaderText( "cypher.shader-compiler.api1.compiler1" ) );
}

void EmitDependencies(
    const tool_compile_request_t &request,
    const shader_compile_work_t &work,
    content_hash_t recipeHash,
    content_hash_t compilerHash,
    content_hash_t toolchainHash ) noexcept
{
    const tool_dependency_t dependencies[]{
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
        },
        {
            ShaderText( "toolchain/cypher-shader-compiler" ),
            tool_dependency_kind_t::TOOLCHAIN,
            compilerHash,
            TOOL_DEPENDENCY_FLAG_REQUIRED
        },
        {
            ShaderText( "toolchain/glslang" ),
            tool_dependency_kind_t::TOOLCHAIN,
            toolchainHash,
            TOOL_DEPENDENCY_FLAG_REQUIRED
        }
    };
    for ( const tool_dependency_t &dependency : dependencies ) {
        ToolHost_EmitDependency( request.pInvocation->pHost, dependency );
    }
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
    const render_asset_decode_result_t decoded = RenderShaderSource_Decode(
        work.document.pDocument,
        {},
        diagnostics,
        CYPHER_ARRAY_COUNT( diagnostics ),
        &work.recipe );
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
            EmitDiagnostic(
                request,
                report,
                CY_SHADER_DIAGNOSTIC_SCHEMA_FAILED,
                tool_diagnostic_severity_t::ERROR,
                tool_diagnostic_category_t::SCHEMA,
                StringView_FromCString(
                    RenderAsset_DecodeStatusName( decoded.status ) ),
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
    if ( !BuildDefinePreamble( work.recipe, work.definePreamble ) ) {
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
        stage.sourceHash = ContentHash_String(
            TextBuffer_View( &stage.source ) );
        report.cbRead += stage.source.cchLength;
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
            work.stages[iStage] );
        if ( ToolStatus_Failed( status ) ) {
            MarkFailed( report );
            EmitFailureProgress( request, sequence, status, nCompleted );
            return status;
        }
    }
    tool_status_t status = LinkProgram( request, report, work.stages );
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

    const content_hash_t recipeHash = ContentHash_String(
        TextBuffer_View( &work.recipeText ) );
    const content_hash_t compilerHash = CompilerHash();
    const content_hash_t toolchainHash = ToolchainHash();
    content_hash_t sourceHash = ContentHash_Combine(
        compilerHash,
        recipeHash );
    sourceHash = ContentHash_Combine(
        sourceHash,
        work.stages[0].sourceHash );
    sourceHash = ContentHash_Combine(
        sourceHash,
        work.stages[1].sourceHash );
    sourceHash = ContentHash_Combine( sourceHash, toolchainHash );
    if ( !ContentHash_IsValid( recipeHash ) ||
         !ContentHash_IsValid( compilerHash ) ||
         !ContentHash_IsValid( toolchainHash ) ||
         !ContentHash_IsValid( work.stages[0].sourceHash ) ||
         !ContentHash_IsValid( work.stages[1].sourceHash ) ||
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
    const cooked_shader_desc_t shaderDesc{};
    const span_t<const cooked_shader_stage_source_t> stageSpan{
        cookedStages,
        CY_COOKED_SHADER_MAX_STAGES
    };
    const usize cbCooked = CookedShader_RequiredSize(
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
    const cooked_shader_result_t cooked = CookedShader_Write(
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
        toolchainHash );
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
