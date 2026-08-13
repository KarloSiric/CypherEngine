//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/CypherResourceCompiler/CypherResourceCompiler.cpp
//  Purpose: Implements the generic resource compiler command-line host.
//  Details: Version 1 discovers explicit paths, directories, and wildcard inputs
//           through an injected source VFS, then dispatches canonical resource
//           identities by registered compiler ownership. Compiler modules remain
//           independent of process, terminal, and native path policy.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherResourceCompiler.h"

#include "CypherMaterialCompiler.h"
#include "CypherShaderCompiler.h"
#include "CypherTextureCompiler.h"

#include "CypherCommon_Allocator.h"
#include "CypherCommon_PathMatch.h"
#include "CypherCommon_Sort.h"
#include "CypherCommon_StringBuilder.h"
#include "CypherCommon_StringPool.h"
#include "CypherCommon_StringParse.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_SystemInfo.h"
#include "CypherCommon_TextBuffer.h"
#include "CypherCommon_ToolDiagnostic.h"
#include "CypherCommon_ToolInvocation.h"
#include "CypherCommon_ToolSession.h"
#include "CypherCommon_Vector.h"
#include "CypherCommon_Vfs.h"
#include "CypherCommon_VfsDirectory.h"

namespace cypher::tools
{

using namespace cypher::common;

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t ResourceCompilerText(
    const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

inline constexpr tool_diagnostic_code_t
    CY_RESOURCE_COMPILER_DIAGNOSTIC_DISPATCH = 0x43520001u;
inline constexpr tool_diagnostic_code_t
    CY_RESOURCE_COMPILER_DIAGNOSTIC_OUTPUT_PATH = 0x43520002u;
inline constexpr tool_diagnostic_code_t
    CY_RESOURCE_COMPILER_DIAGNOSTIC_CONTEXT = 0x43520003u;
inline constexpr tool_diagnostic_code_t
    CY_RESOURCE_COMPILER_DIAGNOSTIC_INPUT = 0x43520004u;
inline constexpr usize CY_RESOURCE_COMPILER_MAX_DISCOVERED_INPUTS = 65536u;

inline constexpr string_view_t g_outputFormatValues[]{
    ResourceCompilerText( "text" ),
    ResourceCompilerText( "json" )
};

inline constexpr string_view_t g_progressValues[]{
    ResourceCompilerText( "auto" ),
    ResourceCompilerText( "plain" ),
    ResourceCompilerText( "json" ),
    ResourceCompilerText( "none" )
};

inline constexpr string_view_t g_colorValues[]{
    ResourceCompilerText( "auto" ),
    ResourceCompilerText( "always" ),
    ResourceCompilerText( "never" )
};

inline constexpr string_view_t g_verbosityValues[]{
    ResourceCompilerText( "quiet" ),
    ResourceCompilerText( "normal" ),
    ResourceCompilerText( "verbose" ),
    ResourceCompilerText( "trace" )
};

inline constexpr string_view_t g_profileValues[]{
    ResourceCompilerText( "development" ),
    ResourceCompilerText( "release" ),
    ResourceCompilerText( "shipping" )
};

inline constexpr string_view_t g_targetValues[]{
    ResourceCompilerText( "host" ),
    ResourceCompilerText( "windows-x86" ),
    ResourceCompilerText( "windows-x64" ),
    ResourceCompilerText( "windows-arm64" ),
    ResourceCompilerText( "linux-x86" ),
    ResourceCompilerText( "linux-x64" ),
    ResourceCompilerText( "linux-arm32" ),
    ResourceCompilerText( "linux-arm64" ),
    ResourceCompilerText( "macos-x64" ),
    ResourceCompilerText( "macos-arm64" )
};

inline constexpr char g_resourceCompilerBanner[] = R"banner(
========================================================================================
   ______  __     __  ______   __    __  ______   ______
  / ____/  \ \   / / |  __  \ |  |  |  ||  ____| |  __  \
 | |        \ \_/ /  | |__) / |  |__|  || |__    | |__) |
 | |         \   /   |  ___/  |   __   ||  __|   |  _  /
 | |____      | |    | |      |  |  |  || |____  | | \ \
  \_____|     |_|    |_|      |__|  |__||______| |_|  \_\

              C Y P H E R   R E S O U R C E   C O M P I L E R
                     OFFLINE ASSET TOOLCHAIN  |  1.0.0

                         A COMPONENT OF CYPHERENGINE
              Copyright (c) 2026 Karlo Siric. All rights reserved.
                Proprietary and confidential. See LICENSE for terms.
========================================================================================

)banner";

inline constexpr char g_applicationDetails[] = R"details(CAPABILITIES
  - Validates authored resources without publishing output.
  - Dispatches inputs through a deterministic compiler registry.
  - Cooks explicit files, VFS directories, and quoted wildcard patterns.
  - Emits structured diagnostics, dependencies, artifacts, and reports.
  - Publishes completed files transactionally to protect previous output.
  - Supports human-readable text and newline-delimited JSON records.
  - Supports terminal-aware ANSI color, progress, verbosity, and Ctrl+C.
  - Expands recursive @response files for long or repeatable invocations.

REGISTERED RESOURCE TYPES
  .cyshader  ->  .cyshader_c    Cypher Shader Compiler

CURRENT SHADER PIPELINE
  CYKV parse -> exact schema -> semantic decode -> GLSL preprocess
  -> stage validation -> cross-stage link -> deterministic CYSH/CYRS output

EXAMPLES
  CypherResourceCompiler validate -s assets shaders/world.cyshader
  CypherResourceCompiler compile -s assets -o cooked shaders/world.cyshader
  CypherResourceCompiler compile -s assets -o cooked -r shaders
  CypherResourceCompiler compile -s assets -o cooked 'shaders/ui/*.cyshader'
  CypherResourceCompiler compile -s assets -o cooked @resources.rsp
  CypherResourceCompiler list-compilers
  CypherResourceCompiler describe-compiler cypher.shader
  CypherResourceCompiler list-formats --output-format json

EXIT CODES
  0  Success                  3  Configuration error
  1  Compilation failure      4  Filesystem/infrastructure error
  2  Command-line usage error 5  Internal error
  6  Cancelled

PROGRAM DETAILS
  Product:       CypherResourceCompiler
  Version:       1.0.0
  Compiler API:  1
  Runtime:       C++20 / CypherToolFramework
  License:       Proprietary - see LICENSE

Run 'CypherResourceCompiler <command> --help' for command options.)details";

inline constexpr char g_zshCompletion[] = R"completion(#compdef CypherResourceCompiler

_cypher_resource_compiler() {
    local -a commands
    commands=(
        'compile:validate, cook, and publish resources'
        'validate:validate resources without writing output'
        'list-compilers:list registered compiler modules'
        'describe-compiler:describe one registered compiler'
        'list-formats:list source and cooked format mappings'
        'completion:generate shell completion definitions'
    )

    if (( CURRENT == 2 )); then
        _describe 'command' commands
        return
    fi

    case ${words[2]} in
        compile|validate)
            local source_root='.'
            local i
            for (( i = 3; i < CURRENT; ++i )); do
                case ${words[i]} in
                    -s|--source-root)
                        (( ++i ))
                        source_root=${words[i]}
                        ;;
                    --source-root=*) source_root=${words[i]#*=} ;;
                esac
            done
            _arguments -s \
                '(-i --input)'{-i,--input}'[add input file, directory, or wildcard]:input:_files -W "$source_root"' \
                '(-r --recursive)'{-r,--recursive}'[search directories recursively]' \
                '(-s --source-root)'{-s,--source-root}'[set authored content root]:directory:_directories' \
                '(-o --output-root)'{-o,--output-root}'[set cooked output root]:directory:_directories' \
                '(-t --target)'{-t,--target}'[select target]:target:(host windows-x86 windows-x64 windows-arm64 linux-x86 linux-x64 linux-arm32 linux-arm64 macos-x64 macos-arm64)' \
                '(-p --profile)'{-p,--profile}'[select profile]:profile:(development release shipping)' \
                '(-j --jobs)'{-j,--jobs}'[set worker count]:count:' \
                '(-v --verbose)'{-v,--verbose}'[enable verbose records]' \
                '(-W --warnings-as-errors)'{-W,--warnings-as-errors}'[treat warnings as errors]' \
                '(-k --keep-going)'{-k,--keep-going}'[continue after independent failures]' \
                '--output-format[select output records]:format:(text json)' \
                '--progress[select progress mode]:mode:(auto plain json none)' \
                '--color[select ANSI color policy]:policy:(auto always never)' \
                '--verbosity[select record detail]:level:(quiet normal verbose trace)' \
                '*:virtual resource:_files -W "$source_root"'
            ;;
        describe-compiler)
            _arguments '1:compiler:(cypher.shader)'
            ;;
        completion)
            _arguments '1:shell:(zsh)'
            ;;
    esac
}

compdef _cypher_resource_compiler CypherResourceCompiler
)completion";

inline constexpr tool_cli_presentation_t g_presentation{
    ResourceCompilerText( g_resourceCompilerBanner ),
    ResourceCompilerText(
        "Deterministic validation and cooking for CypherEngine resources." ),
    ResourceCompilerText( g_applicationDetails ),
    CY_FALSE
};

inline constexpr tool_option_desc_t g_compileOptions[]{
    {
        ResourceCompilerText( "input" ),
        'i',
        tool_option_type_t::PATH,
        ResourceCompilerText( "PATH" ),
        ResourceCompilerText(
            "Adds a virtual file, directory, or quoted wildcard input." ),
        {},
        nullptr,
        0u,
        TOOL_OPTION_FLAG_REPEATABLE |
            TOOL_OPTION_FLAG_SEMANTIC
    },
    {
        ResourceCompilerText( "recursive" ),
        'r',
        tool_option_type_t::BOOLEAN,
        {},
        ResourceCompilerText(
            "Searches input directories and wildcard roots recursively." ),
        ResourceCompilerText( "false" ),
        nullptr,
        0u,
        TOOL_OPTION_FLAG_NONE
    },
    {
        ResourceCompilerText( "source-root" ),
        's',
        tool_option_type_t::PATH,
        ResourceCompilerText( "PATH" ),
        ResourceCompilerText( "Sets the native root containing authored resources." ),
        ResourceCompilerText( "." ),
        nullptr,
        0u,
        TOOL_OPTION_FLAG_SEMANTIC
    },
    {
        ResourceCompilerText( "output-root" ),
        'o',
        tool_option_type_t::PATH,
        ResourceCompilerText( "PATH" ),
        ResourceCompilerText( "Sets the native root receiving cooked resources." ),
        ResourceCompilerText( "." ),
        nullptr,
        0u,
        TOOL_OPTION_FLAG_SEMANTIC
    },
    {
        ResourceCompilerText( "target" ),
        't',
        tool_option_type_t::ENUM,
        ResourceCompilerText( "TARGET" ),
        ResourceCompilerText(
            "Selects host, windows-x86/x64/arm64, linux-x86/x64/arm32/arm64, "
            "or macos-x64/arm64." ),
        ResourceCompilerText( "host" ),
        g_targetValues,
        CYPHER_ARRAY_COUNT( g_targetValues ),
        TOOL_OPTION_FLAG_SEMANTIC
    },
    {
        ResourceCompilerText( "profile" ),
        'p',
        tool_option_type_t::ENUM,
        ResourceCompilerText( "PROFILE" ),
        ResourceCompilerText( "Selects development, release, or shipping cook policy." ),
        ResourceCompilerText( "development" ),
        g_profileValues,
        CYPHER_ARRAY_COUNT( g_profileValues ),
        TOOL_OPTION_FLAG_SEMANTIC
    },
    {
        ResourceCompilerText( "jobs" ),
        'j',
        tool_option_type_t::U64,
        ResourceCompilerText( "COUNT" ),
        ResourceCompilerText(
            "Records the requested worker count; version 1 execution is sequential." ),
        ResourceCompilerText( "1" ),
        nullptr,
        0u,
        TOOL_OPTION_FLAG_NONE
    },
    {
        ResourceCompilerText( "output-format" ),
        '\0',
        tool_option_type_t::ENUM,
        ResourceCompilerText( "FORMAT" ),
        ResourceCompilerText( "Writes human-readable text or JSON records." ),
        ResourceCompilerText( "text" ),
        g_outputFormatValues,
        CYPHER_ARRAY_COUNT( g_outputFormatValues ),
        TOOL_OPTION_FLAG_NONE
    },
    {
        ResourceCompilerText( "progress" ),
        '\0',
        tool_option_type_t::ENUM,
        ResourceCompilerText( "MODE" ),
        ResourceCompilerText( "Selects automatic, plain, JSON, or disabled progress." ),
        ResourceCompilerText( "auto" ),
        g_progressValues,
        CYPHER_ARRAY_COUNT( g_progressValues ),
        TOOL_OPTION_FLAG_NONE
    },
    {
        ResourceCompilerText( "color" ),
        '\0',
        tool_option_type_t::ENUM,
        ResourceCompilerText( "WHEN" ),
        ResourceCompilerText( "Controls ANSI color for text output." ),
        ResourceCompilerText( "auto" ),
        g_colorValues,
        CYPHER_ARRAY_COUNT( g_colorValues ),
        TOOL_OPTION_FLAG_NONE
    },
    {
        ResourceCompilerText( "verbose" ),
        'v',
        tool_option_type_t::BOOLEAN,
        {},
        ResourceCompilerText( "Enables verbose records (alias for --verbosity verbose)." ),
        ResourceCompilerText( "false" ),
        nullptr,
        0u,
        TOOL_OPTION_FLAG_NONE
    },
    {
        ResourceCompilerText( "verbosity" ),
        '\0',
        tool_option_type_t::ENUM,
        ResourceCompilerText( "LEVEL" ),
        ResourceCompilerText( "Selects quiet, normal, verbose, or trace records." ),
        ResourceCompilerText( "normal" ),
        g_verbosityValues,
        CYPHER_ARRAY_COUNT( g_verbosityValues ),
        TOOL_OPTION_FLAG_NONE
    },
    {
        ResourceCompilerText( "warnings-as-errors" ),
        'W',
        tool_option_type_t::BOOLEAN,
        {},
        ResourceCompilerText( "Treats compiler warnings as operation errors." ),
        ResourceCompilerText( "false" ),
        nullptr,
        0u,
        TOOL_OPTION_FLAG_NONE
    },
    {
        ResourceCompilerText( "keep-going" ),
        'k',
        tool_option_type_t::BOOLEAN,
        {},
        ResourceCompilerText( "Continues compiling independent inputs after a failure." ),
        ResourceCompilerText( "true" ),
        nullptr,
        0u,
        TOOL_OPTION_FLAG_NONE
    }
};

inline constexpr tool_option_desc_t g_inspectionOptions[]{
    {
        ResourceCompilerText( "output-format" ),
        '\0',
        tool_option_type_t::ENUM,
        ResourceCompilerText( "FORMAT" ),
        ResourceCompilerText( "Writes human-readable text or JSON records." ),
        ResourceCompilerText( "text" ),
        g_outputFormatValues,
        CYPHER_ARRAY_COUNT( g_outputFormatValues ),
        TOOL_OPTION_FLAG_NONE
    },
    {
        ResourceCompilerText( "color" ),
        '\0',
        tool_option_type_t::ENUM,
        ResourceCompilerText( "WHEN" ),
        ResourceCompilerText( "Controls ANSI color for text output." ),
        ResourceCompilerText( "auto" ),
        g_colorValues,
        CYPHER_ARRAY_COUNT( g_colorValues ),
        TOOL_OPTION_FLAG_NONE
    },
    {
        ResourceCompilerText( "verbosity" ),
        '\0',
        tool_option_type_t::ENUM,
        ResourceCompilerText( "LEVEL" ),
        ResourceCompilerText( "Selects quiet, normal, verbose, or trace records." ),
        ResourceCompilerText( "normal" ),
        g_verbosityValues,
        CYPHER_ARRAY_COUNT( g_verbosityValues ),
        TOOL_OPTION_FLAG_NONE
    }
};

inline constexpr tool_command_desc_t g_commands[]{
    {
        ResourceCompilerText( "compile" ),
        ResourceCompilerText( "Validates and cooks one or more authored resources." ),
        ResourceCompilerText( "compile [options] [-i PATH] [inputs...]" ),
        g_compileOptions,
        CYPHER_ARRAY_COUNT( g_compileOptions ),
        TOOL_COMMAND_FLAG_ACCEPTS_INPUTS |
            TOOL_COMMAND_FLAG_ALLOW_MULTIPLE_INPUTS |
            TOOL_COMMAND_FLAG_SUPPORTS_DRY_RUN,
        ResourceCompilerText(
            "Runs the complete compiler pipeline and transactionally publishes "
            "one cooked output under --output-root. Directory and wildcard "
            "inputs are expanded by the VFS, not by the host shell." )
    },
    {
        ResourceCompilerText( "validate" ),
        ResourceCompilerText( "Runs the complete compiler pipeline without writing outputs." ),
        ResourceCompilerText( "validate [options] [-i PATH] [inputs...]" ),
        g_compileOptions,
        CYPHER_ARRAY_COUNT( g_compileOptions ),
        TOOL_COMMAND_FLAG_ACCEPTS_INPUTS |
            TOOL_COMMAND_FLAG_ALLOW_MULTIPLE_INPUTS |
            TOOL_COMMAND_FLAG_SUPPORTS_DRY_RUN,
        ResourceCompilerText(
            "Executes the same parsing, schema, semantic, and compiler checks as "
            "compile, but never creates output directories or artifacts." )
    },
    {
        ResourceCompilerText( "list-compilers" ),
        ResourceCompilerText( "Lists every compiler module registered in this executable." ),
        ResourceCompilerText( "list-compilers [options]" ),
        g_inspectionOptions,
        CYPHER_ARRAY_COUNT( g_inspectionOptions ),
        TOOL_COMMAND_FLAG_NONE,
        ResourceCompilerText(
            "Reports compiler IDs, resource types, versions, extensions, and "
            "declared capabilities from the live compiler registry." )
    },
    {
        ResourceCompilerText( "describe-compiler" ),
        ResourceCompilerText( "Describes one registered compiler module in detail." ),
        ResourceCompilerText( "describe-compiler [options] <compiler-id>" ),
        g_inspectionOptions,
        CYPHER_ARRAY_COUNT( g_inspectionOptions ),
        TOOL_COMMAND_FLAG_ACCEPTS_INPUTS,
        ResourceCompilerText(
            "The compiler ID is the stable registry identity shown by "
            "list-compilers, for example 'cypher.shader'." )
    },
    {
        ResourceCompilerText( "list-formats" ),
        ResourceCompilerText( "Lists source-to-cooked mappings currently accepted." ),
        ResourceCompilerText( "list-formats [options]" ),
        g_inspectionOptions,
        CYPHER_ARRAY_COUNT( g_inspectionOptions ),
        TOOL_COMMAND_FLAG_NONE,
        ResourceCompilerText(
            "Only formats backed by a registered and executable compiler module "
            "are reported; planned formats are deliberately excluded." )
    },
    {
        ResourceCompilerText( "completion" ),
        ResourceCompilerText( "Generates shell completion definitions." ),
        ResourceCompilerText( "completion <shell>" ),
        nullptr,
        0u,
        TOOL_COMMAND_FLAG_ACCEPTS_INPUTS,
        ResourceCompilerText(
            "Version 1 supports zsh. Source the generated definition once per "
            "shell session or from the user's zsh configuration." )
    }
};

inline constexpr tool_application_desc_t g_application{
    ResourceCompilerText( "cypher-resource-compiler" ),
    ResourceCompilerText( "CypherResourceCompiler" ),
    ResourceCompilerText(
        "Deterministic offline validation and cooking for CypherEngine resources." ),
    tool_delivery_t::COMMAND_LINE,
    CY_RESOURCE_COMPILER_API_VERSION,
    TOOL_APPLICATION_FLAG_HEADLESS |
        TOOL_APPLICATION_FLAG_PROJECT_AWARE |
        TOOL_APPLICATION_FLAG_EMBEDDABLE
};

struct resource_compiler_state_t {
    tool_compiler_registry_t registry{};
    const tool_compiler_desc_t *pCompilerStorage[32]{};
};

CYPHER_NODISCARD const tool_option_value_t *FindOption(
    const tool_cli_parse_result_t &arguments,
    const char *pName ) noexcept
{
    return ToolOptionSet_Find(
        &arguments.options,
        StringView_FromCString( pName ) );
}

CYPHER_NODISCARD bool_t OptionIsTrue(
    const tool_cli_parse_result_t &arguments,
    const char *pName ) noexcept
{
    const tool_option_value_t *pOption = FindOption( arguments, pName );
    if ( pOption == nullptr ) {
        return CY_FALSE;
    }
    bool_t bValue = CY_FALSE;
    const string_parse_result_t parsed = StringParse_Bool(
        pOption->value,
        STRING_PARSE_FLAG_ALLOW_NUMERIC_BOOL,
        &bValue );
    return StringParse_Succeeded( parsed ) &&
           parsed.cchConsumed == pOption->value.cchLength &&
           bValue;
}

CYPHER_NODISCARD string_view_t OptionValue(
    const tool_cli_parse_result_t &arguments,
    const char *pName ) noexcept
{
    const tool_option_value_t *pOption = FindOption( arguments, pName );
    return pOption != nullptr ? pOption->value : string_view_t{};
}

void EmitHostDiagnostic(
    const tool_host_t &host,
    tool_operation_id_t operationId,
    tool_diagnostic_code_t code,
    tool_diagnostic_category_t category,
    string_view_t message,
    string_view_t path = {} ) noexcept
{
    tool_diagnostic_t diagnostic{};
    diagnostic.operationId = operationId;
    diagnostic.code = code;
    diagnostic.severity = tool_diagnostic_severity_t::ERROR;
    diagnostic.category = category;
    diagnostic.message = message;
    if ( path.cchLength != 0u ) {
        diagnostic.source.path = path;
        diagnostic.source.nLine = 1u;
        diagnostic.source.nColumn = 1u;
        diagnostic.flags |= TOOL_DIAGNOSTIC_FLAG_HAS_SOURCE;
    }
    ToolHost_EmitDiagnostic( &host, diagnostic );
}

CYPHER_NODISCARD tool_status_t ResolveOutputPolicy(
    const tool_cli_parse_result_t &arguments,
    tool_output_policy_t *pPolicy,
    void * ) noexcept
{
    if ( pPolicy == nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( arguments.action != tool_cli_parse_action_t::EXECUTE ) {
        return tool_status_t::OK;
    }

    const string_view_t outputFormat = OptionValue( arguments, "output-format" );
    const string_view_t progress = OptionValue( arguments, "progress" );
    const string_view_t color = OptionValue( arguments, "color" );
    const string_view_t verbosity = OptionValue( arguments, "verbosity" );

    pPolicy->diagnosticsFormat = StringView_Equals(
        outputFormat,
        ResourceCompilerText( "json" ) )
        ? tool_output_format_t::JSON
        : tool_output_format_t::TEXT;

    if ( StringView_Equals( progress, ResourceCompilerText( "plain" ) ) ) {
        pPolicy->progressMode = tool_progress_mode_t::PLAIN;
    } else if ( StringView_Equals( progress, ResourceCompilerText( "json" ) ) ) {
        pPolicy->progressMode = tool_progress_mode_t::JSON;
    } else if ( StringView_Equals( progress, ResourceCompilerText( "none" ) ) ) {
        pPolicy->progressMode = tool_progress_mode_t::NONE;
    } else {
        pPolicy->progressMode = tool_progress_mode_t::AUTO;
    }

    if ( pPolicy->diagnosticsFormat == tool_output_format_t::JSON ) {
        if ( pPolicy->progressMode == tool_progress_mode_t::AUTO ) {
            pPolicy->progressMode = tool_progress_mode_t::JSON;
        } else if ( pPolicy->progressMode == tool_progress_mode_t::PLAIN ) {
            return tool_status_t::INVALID_CONFIGURATION;
        }
        pPolicy->flags &= ~( TOOL_OUTPUT_FLAG_COLOR |
                             TOOL_OUTPUT_FLAG_FORCE_COLOR );
    } else if ( !StringView_Equals( color, ResourceCompilerText( "never" ) ) ) {
        pPolicy->flags |= TOOL_OUTPUT_FLAG_COLOR;
        if ( StringView_Equals( color, ResourceCompilerText( "always" ) ) ) {
            pPolicy->flags |= TOOL_OUTPUT_FLAG_FORCE_COLOR;
        }
    } else {
        pPolicy->flags &= ~( TOOL_OUTPUT_FLAG_COLOR |
                             TOOL_OUTPUT_FLAG_FORCE_COLOR );
    }

    if ( OptionIsTrue( arguments, "verbose" ) ) {
        pPolicy->verbosity = tool_verbosity_t::VERBOSE;
    } else if ( StringView_Equals( verbosity, ResourceCompilerText( "quiet" ) ) ) {
        pPolicy->verbosity = tool_verbosity_t::QUIET;
    } else if ( StringView_Equals(
                    verbosity,
                    ResourceCompilerText( "verbose" ) ) ) {
        pPolicy->verbosity = tool_verbosity_t::VERBOSE;
    } else if ( StringView_Equals(
                    verbosity,
                    ResourceCompilerText( "trace" ) ) ) {
        pPolicy->verbosity = tool_verbosity_t::TRACE;
    } else {
        pPolicy->verbosity = tool_verbosity_t::NORMAL;
    }

    if ( OptionIsTrue( arguments, "warnings-as-errors" ) ) {
        pPolicy->flags |= TOOL_OUTPUT_FLAG_WARNINGS_AS_ERRORS;
    }
    return tool_status_t::OK;
}

CYPHER_NODISCARD bool_t MakeOutputPath(
    string_view_t input,
    string_view_t cookedExtension,
    text_buffer_t *pOutput ) noexcept
{
    const path_write_result_t measured = StringPath_ReplaceExtension(
        input,
        cookedExtension,
        nullptr,
        0u );
    if ( measured.cchRequired == 0u ||
         ( measured.status != path_status_t::OK &&
           measured.status != path_status_t::OUTPUT_TRUNCATED ) ||
         !TextBuffer_Resize( pOutput, measured.cchRequired ) ) {
        return CY_FALSE;
    }
    const path_write_result_t written = StringPath_ReplaceExtension(
        input,
        cookedExtension,
        TextBuffer_Data( pOutput ),
        TextBuffer_Capacity( pOutput ) + 1u );
    return written.status == path_status_t::OK &&
           written.cchWritten == measured.cchRequired;
}

struct discovered_inputs_t {
    discovered_inputs_t() noexcept = default;
    vector_t<string_view_t> paths{};
    string_pool_t *pStrings{ nullptr };

    CYPHER_NO_COPY_MOVE( discovered_inputs_t );

    ~discovered_inputs_t() noexcept
    {
        StringPool_Destroy( pStrings );
    }
};

CYPHER_NODISCARD bool_t InitDiscoveredInputs(
    discovered_inputs_t *pInputs ) noexcept
{
    if ( pInputs == nullptr ) {
        return CY_FALSE;
    }
    string_pool_desc_t poolDesc{};
    poolDesc.pAllocator = Allocator_GetSystem();
    poolDesc.nInitialBuckets = 256u;
    poolDesc.cbInitialBlock = 16u * CY_KIB;
    pInputs->pStrings = StringPool_Create( poolDesc );
    return pInputs->pStrings != nullptr &&
           Vector_Init( &pInputs->paths, Allocator_GetSystem(), 256u );
}

CYPHER_NODISCARD tool_status_t AddDiscoveredInput(
    discovered_inputs_t *pInputs,
    string_view_t path ) noexcept
{
    if ( pInputs == nullptr || pInputs->pStrings == nullptr ||
         !StringView_IsValid( path ) || path.cchLength == 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( pInputs->paths.nCount >=
         CY_RESOURCE_COMPILER_MAX_DISCOVERED_INPUTS ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }
    const char *pInterned = StringPool_Intern( pInputs->pStrings, path );
    if ( pInterned == nullptr ||
         !Vector_PushBack(
             &pInputs->paths,
             string_view_t{ pInterned, path.cchLength } ) ) {
        return tool_status_t::OUT_OF_MEMORY;
    }
    return tool_status_t::OK;
}

CYPHER_NODISCARD bool_t InputContainsWildcard(
    string_view_t input ) noexcept
{
    return StringView_FindChar( input, '*' ) != CY_STRING_VIEW_NPOS ||
           StringView_FindChar( input, '?' ) != CY_STRING_VIEW_NPOS ||
           StringView_FindChar( input, '[' ) != CY_STRING_VIEW_NPOS;
}

CYPHER_NODISCARD bool_t InputPatternIsCanonical(
    string_view_t pattern ) noexcept
{
    if ( !StringView_IsValid( pattern ) || pattern.cchLength == 0u ||
         pattern.cchLength > CY_VFS_MAX_VIRTUAL_PATH ||
         pattern.pData[0] == '/' || pattern.pData[0] == '\\' ) {
        return CY_FALSE;
    }

    usize iSegmentStart = 0u;
    for ( usize iByte = 0u; iByte <= pattern.cchLength; ++iByte ) {
        if ( iByte == pattern.cchLength || pattern.pData[iByte] == '/' ) {
            const usize cchSegment = iByte - iSegmentStart;
            if ( cchSegment == 0u ||
                 ( cchSegment == 1u && pattern.pData[iSegmentStart] == '.' ) ||
                 ( cchSegment == 2u && pattern.pData[iSegmentStart] == '.' &&
                   pattern.pData[iSegmentStart + 1u] == '.' ) ) {
                return CY_FALSE;
            }
            iSegmentStart = iByte + 1u;
            continue;
        }

        const char ch = pattern.pData[iByte];
        const u8 value = static_cast<u8>( ch );
        if ( value <= 0x20u || value >= 0x7fu || ch == '\\' || ch == ':' ||
             ch == '"' || ch == '<' || ch == '>' || ch == '|' ||
             ( ch >= 'A' && ch <= 'Z' ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD usize FirstWildcard( string_view_t pattern ) noexcept
{
    for ( usize iByte = 0u; iByte < pattern.cchLength; ++iByte ) {
        const char ch = pattern.pData[iByte];
        if ( ch == '*' || ch == '?' || ch == '[' ) {
            return iByte;
        }
    }
    return CY_STRING_VIEW_NPOS;
}

CYPHER_NODISCARD string_view_t PatternEnumerationRoot(
    string_view_t pattern ) noexcept
{
    const usize iWildcard = FirstWildcard( pattern );
    if ( iWildcard == CY_STRING_VIEW_NPOS ) {
        return {};
    }
    const string_view_t prefix = StringView_Prefix( pattern, iWildcard );
    const usize iSeparator = StringView_FindLastChar( prefix, '/' );
    return iSeparator == CY_STRING_VIEW_NPOS
        ? string_view_t{}
        : StringView_Prefix( pattern, iSeparator );
}

CYPHER_NODISCARD tool_status_t ToolStatusFromVfs(
    vfs_status_t status ) noexcept
{
    switch ( status ) {
        case vfs_status_t::OK: return tool_status_t::OK;
        case vfs_status_t::INVALID_ARGUMENT:
        case vfs_status_t::INVALID_PATH: return tool_status_t::INVALID_ARGUMENT;
        case vfs_status_t::NOT_FOUND:
        case vfs_status_t::NOT_A_FILE:
        case vfs_status_t::NOT_A_DIRECTORY: return tool_status_t::NOT_FOUND;
        case vfs_status_t::SIZE_LIMIT: return tool_status_t::CAPACITY_EXCEEDED;
        case vfs_status_t::OUT_OF_MEMORY: return tool_status_t::OUT_OF_MEMORY;
        case vfs_status_t::CANCELLED: return tool_status_t::CANCELLED;
        case vfs_status_t::IO_ERROR:
        case vfs_status_t::UNSUPPORTED: return tool_status_t::IO_ERROR;
    }
    return tool_status_t::INTERNAL_ERROR;
}

struct discovery_visit_t {
    discovered_inputs_t *pInputs{ nullptr };
    const tool_compiler_registry_t *pRegistry{ nullptr };
    const tool_host_t *pHost{ nullptr };
    string_view_t pattern{};
    flags32_t matchFlags{ PATH_MATCH_FLAG_NONE };
    tool_status_t status{ tool_status_t::OK };
    bool_t bMatchPattern{ CY_FALSE };
};

bool_t DiscoverVisit(
    string_view_t virtualPath,
    const vfs_file_info_t &info,
    void *pUserData ) noexcept
{
    auto *pVisit = static_cast<discovery_visit_t *>( pUserData );
    if ( pVisit == nullptr || pVisit->pInputs == nullptr ||
         pVisit->pRegistry == nullptr ) {
        return CY_FALSE;
    }
    if ( ToolHost_IsCancellationRequested( pVisit->pHost ) ) {
        pVisit->status = tool_status_t::CANCELLED;
        return CY_FALSE;
    }
    if ( info.type != vfs_entry_type_t::FILE ||
         ( pVisit->bMatchPattern &&
           !PathMatch_Wildcard(
               virtualPath,
               pVisit->pattern,
               pVisit->matchFlags ) ) ) {
        return CY_TRUE;
    }

    const tool_compiler_desc_t *pCompiler = nullptr;
    const tool_status_t ownership = ToolCompilerRegistry_FindForInput(
        pVisit->pRegistry,
        virtualPath,
        &pCompiler );
    if ( ownership == tool_status_t::NOT_FOUND ) {
        return CY_TRUE;
    }
    if ( ToolStatus_Failed( ownership ) || pCompiler == nullptr ) {
        pVisit->status = ownership;
        return CY_FALSE;
    }

    pVisit->status = AddDiscoveredInput( pVisit->pInputs, virtualPath );
    return ToolStatus_Succeeded( pVisit->status );
}

CYPHER_NODISCARD tool_status_t EnumerateInput(
    const vfs_t *pVfs,
    const tool_compiler_registry_t *pRegistry,
    const tool_host_t *pHost,
    string_view_t root,
    string_view_t pattern,
    bool_t bRecursive,
    discovered_inputs_t *pInputs ) noexcept
{
    discovery_visit_t visit{};
    visit.pInputs = pInputs;
    visit.pRegistry = pRegistry;
    visit.pHost = pHost;
    visit.pattern = pattern;
    visit.bMatchPattern = pattern.cchLength != 0u;
    const vfs_status_t vfsStatus = Vfs_Enumerate(
        pVfs,
        root,
        bRecursive,
        &DiscoverVisit,
        &visit );
    if ( vfsStatus == vfs_status_t::CANCELLED ) {
        return ToolStatus_Failed( visit.status )
            ? visit.status
            : tool_status_t::CANCELLED;
    }
    return vfsStatus == vfs_status_t::OK
        ? visit.status
        : ToolStatusFromVfs( vfsStatus );
}

CYPHER_NODISCARD tool_status_t DiscoverInputSpec(
    const vfs_t *pVfs,
    const tool_compiler_registry_t *pRegistry,
    const tool_host_t *pHost,
    string_view_t spec,
    bool_t bRecursive,
    discovered_inputs_t *pInputs ) noexcept
{
    const usize nBefore = pInputs->paths.nCount;
    if ( InputContainsWildcard( spec ) ) {
        if ( !InputPatternIsCanonical( spec ) ) {
            return tool_status_t::INVALID_ARGUMENT;
        }
        const string_view_t root = PatternEnumerationRoot( spec );
        const usize iWildcard = FirstWildcard( spec );
        const bool_t bPatternContainsSubdirectory =
            iWildcard != CY_STRING_VIEW_NPOS &&
            StringView_FindChar( spec, '/', iWildcard ) !=
                CY_STRING_VIEW_NPOS;
        const tool_status_t status = EnumerateInput(
            pVfs,
            pRegistry,
            pHost,
            root,
            spec,
            bRecursive || bPatternContainsSubdirectory,
            pInputs );
        if ( ToolStatus_Failed( status ) ) {
            return status;
        }
        return pInputs->paths.nCount != nBefore
            ? tool_status_t::OK
            : tool_status_t::NOT_FOUND;
    }

    if ( StringView_Equals( spec, ResourceCompilerText( "." ) ) ) {
        const tool_status_t status = EnumerateInput(
            pVfs,
            pRegistry,
            pHost,
            {},
            {},
            bRecursive,
            pInputs );
        if ( ToolStatus_Failed( status ) ) {
            return status;
        }
        return pInputs->paths.nCount != nBefore
            ? tool_status_t::OK
            : tool_status_t::NOT_FOUND;
    }
    if ( !Vfs_IsCanonicalPath( spec ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    vfs_file_info_t info{};
    const vfs_status_t vfsStatus = Vfs_Stat( pVfs, spec, &info );
    if ( vfsStatus != vfs_status_t::OK ) {
        return ToolStatusFromVfs( vfsStatus );
    }
    if ( info.type == vfs_entry_type_t::FILE ) {
        return AddDiscoveredInput( pInputs, spec );
    }
    if ( info.type != vfs_entry_type_t::DIRECTORY ) {
        return tool_status_t::UNSUPPORTED;
    }

    const tool_status_t status = EnumerateInput(
        pVfs,
        pRegistry,
        pHost,
        spec,
        {},
        bRecursive,
        pInputs );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    return pInputs->paths.nCount != nBefore
        ? tool_status_t::OK
        : tool_status_t::NOT_FOUND;
}

void SortAndDeduplicateInputs( discovered_inputs_t *pInputs ) noexcept
{
    Sort_Unstable( Vector_Span( &pInputs->paths ) );
    usize iWrite = 0u;
    for ( usize iRead = 0u; iRead < pInputs->paths.nCount; ++iRead ) {
        if ( iWrite != 0u && StringView_Equals(
                 pInputs->paths.pData[iWrite - 1u],
                 pInputs->paths.pData[iRead] ) ) {
            continue;
        }
        pInputs->paths.pData[iWrite++] = pInputs->paths.pData[iRead];
    }
    pInputs->paths.nCount = iWrite;
}

CYPHER_NODISCARD tool_status_t DiscoverInputs(
    const tool_cli_parse_result_t &arguments,
    const vfs_t *pVfs,
    const tool_compiler_registry_t *pRegistry,
    const tool_host_t &host,
    discovered_inputs_t *pInputs ) noexcept
{
    const bool_t bRecursive = OptionIsTrue( arguments, "recursive" );
    const usize nOptionInputs = ToolOptionSet_CountValues(
        &arguments.options,
        ResourceCompilerText( "input" ) );
    if ( arguments.nInputs == 0u && nOptionInputs == 0u ) {
        EmitHostDiagnostic(
            host,
            1u,
            CY_RESOURCE_COMPILER_DIAGNOSTIC_CONTEXT,
            tool_diagnostic_category_t::COMMAND_LINE,
            ResourceCompilerText( "At least one input resource is required." ) );
        return tool_status_t::INVALID_ARGUMENT;
    }

    for ( usize iInput = 0u;
          iInput < arguments.nInputs + nOptionInputs;
          ++iInput ) {
        const string_view_t spec = iInput < arguments.nInputs
            ? arguments.pInputs[iInput]
            : ToolOptionSet_FindAt(
                  &arguments.options,
                  ResourceCompilerText( "input" ),
                  iInput - arguments.nInputs )->value;
        const tool_status_t status = DiscoverInputSpec(
            pVfs,
            pRegistry,
            &host,
            spec,
            bRecursive,
            pInputs );
        if ( ToolStatus_Failed( status ) ) {
            EmitHostDiagnostic(
                host,
                1u,
                CY_RESOURCE_COMPILER_DIAGNOSTIC_INPUT,
                status == tool_status_t::INVALID_ARGUMENT
                    ? tool_diagnostic_category_t::COMMAND_LINE
                    : tool_diagnostic_category_t::FILESYSTEM,
                status == tool_status_t::NOT_FOUND
                    ? ResourceCompilerText(
                          "Input did not resolve to a supported resource. Use -r for descendants and quote wildcard patterns." )
                    : ResourceCompilerText(
                          "Input discovery failed; paths must be canonical virtual resource paths below --source-root." ),
                spec );
            return status;
        }
    }

    SortAndDeduplicateInputs( pInputs );
    return pInputs->paths.nCount != 0u
        ? tool_status_t::OK
        : tool_status_t::NOT_FOUND;
}

struct batch_host_state_t {
    const tool_host_t *pOuter{ nullptr };
    tool_report_t *pAggregate{ nullptr };
};

void BatchDiagnosticCallback(
    const tool_diagnostic_t &diagnostic,
    void *pUserData ) noexcept
{
    const auto *pState = static_cast<const batch_host_state_t *>( pUserData );
    ToolHost_EmitDiagnostic( pState != nullptr ? pState->pOuter : nullptr, diagnostic );
}

void BatchEventCallback( const tool_event_t &event, void *pUserData ) noexcept
{
    const auto *pState = static_cast<const batch_host_state_t *>( pUserData );
    ToolHost_EmitEvent( pState != nullptr ? pState->pOuter : nullptr, event );
}

void BatchDependencyCallback(
    const tool_dependency_t &dependency,
    void *pUserData ) noexcept
{
    const auto *pState = static_cast<const batch_host_state_t *>( pUserData );
    ToolHost_EmitDependency( pState != nullptr ? pState->pOuter : nullptr, dependency );
}

void BatchArtifactCallback(
    const tool_artifact_t &artifact,
    void *pUserData ) noexcept
{
    const auto *pState = static_cast<const batch_host_state_t *>( pUserData );
    ToolHost_EmitArtifact( pState != nullptr ? pState->pOuter : nullptr, artifact );
}

void BatchReportCallback( const tool_report_t &report, void *pUserData ) noexcept
{
    auto *pState = static_cast<batch_host_state_t *>( pUserData );
    if ( pState == nullptr || pState->pAggregate == nullptr ) {
        return;
    }
    tool_report_t &aggregate = *pState->pAggregate;
    aggregate.nInputsProcessed += report.nInputsProcessed;
    aggregate.nSucceeded += report.nSucceeded;
    aggregate.nFailed += report.nFailed;
    aggregate.nCacheHits += report.nCacheHits;
    aggregate.nCacheMisses += report.nCacheMisses;
    aggregate.nWarnings += report.nWarnings;
    aggregate.nErrors += report.nErrors;
    aggregate.nArtifacts += report.nArtifacts;
    aggregate.cbRead += report.cbRead;
    aggregate.cbWritten += report.cbWritten;
}

bool_t BatchTextCallback( string_view_t text, void *pUserData ) noexcept
{
    const auto *pState = static_cast<const batch_host_state_t *>( pUserData );
    return pState != nullptr &&
           ToolStatus_Succeeded( ToolHost_WriteText( pState->pOuter, text ) );
}

CYPHER_NODISCARD tool_host_t MakeBatchHost(
    const tool_host_t &outer,
    batch_host_state_t *pState ) noexcept
{
    tool_host_t host{};
    host.pfnDiagnostic = &BatchDiagnosticCallback;
    host.pfnProgress = nullptr;
    host.pfnEvent = &BatchEventCallback;
    host.pfnDependency = &BatchDependencyCallback;
    host.pfnArtifact = &BatchArtifactCallback;
    host.pfnReport = &BatchReportCallback;
    host.pfnText = &BatchTextCallback;
    host.cancellation = outer.cancellation;
    host.pUserData = pState;
    host.flags = TOOL_HOST_FLAG_NONE;
    return host;
}

void EmitBatchProgress(
    const tool_host_t &host,
    tool_operation_id_t operationId,
    tool_sequence_t sequence,
    tool_progress_state_t state,
    tool_status_t status,
    u64 nCompleted,
    u64 nTotal,
    string_view_t title,
    string_view_t detail ) noexcept
{
    tool_progress_t progress{};
    progress.operationId = operationId;
    progress.sequence = sequence;
    progress.state = state;
    progress.unit = tool_progress_unit_t::ITEMS;
    progress.status = status;
    progress.nCompleted = nCompleted;
    progress.nTotal = nTotal;
    progress.timestamp = Cy_TimerNowTicks();
    progress.title = title;
    progress.detail = detail;
    ToolHost_EmitProgress( &host, progress );
}

void EmitMessage(
    const tool_host_t &host,
    tool_sequence_t sequence,
    string_view_t name,
    string_view_t message ) noexcept
{
    const tool_event_t event{
        1u,
        CY_TOOL_INVALID_OPERATION_ID,
        sequence,
        tool_event_kind_t::MESSAGE,
        tool_status_t::OK,
        Cy_TimerNowTicks(),
        name,
        message
    };
    ToolHost_EmitEvent( &host, event );
}

CYPHER_NODISCARD tool_status_t EmitCompilerDescription(
    const tool_compiler_desc_t &compiler,
    const tool_host_t &host ) noexcept
{
    char details[1024]{};
    const string_format_result_t formatted = StringFormat_Printf(
        details,
        sizeof( details ),
        "id=%.*s  type=%.*s  api=%u  version=%u  output=%.*s",
        static_cast<int>( compiler.id.cchLength ),
        compiler.id.pData,
        static_cast<int>( compiler.resourceType.cchLength ),
        compiler.resourceType.pData,
        compiler.nApiVersion,
        compiler.nCompilerVersion,
        static_cast<int>( compiler.cookedExtension.cchLength ),
        compiler.cookedExtension.pData );
    if ( formatted.status != string_format_status_t::OK ) {
        return tool_status_t::INTERNAL_ERROR;
    }
    EmitMessage(
        host,
        1u,
        compiler.displayName,
        { details, formatted.cchWritten } );

    char extensions[1024]{};
    usize cchExtensions = 0u;
    string_builder_t builder{};
    if ( !StringBuilder_Init( &builder, extensions, sizeof( extensions ) ) ) {
        return tool_status_t::INTERNAL_ERROR;
    }
    for ( usize iExtension = 0u;
          iExtension < compiler.nSourceExtensions;
          ++iExtension ) {
        if ( iExtension != 0u ) {
            (void)StringBuilder_Append( &builder, { ", ", 2u } );
        }
        (void)StringBuilder_Append(
            &builder,
            compiler.pSourceExtensions[iExtension] );
    }
    cchExtensions = builder.cchLength;
    if ( builder.status != string_builder_status_t::OK ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }
    EmitMessage(
        host,
        2u,
        ResourceCompilerText( "source-extensions" ),
        { extensions, cchExtensions } );

    const bool_t bDeterministic =
        ( compiler.flags & TOOL_COMPILER_FLAG_DETERMINISTIC ) != 0u;
    const bool_t bThreadSafe =
        ( compiler.flags & TOOL_COMPILER_FLAG_THREAD_SAFE ) != 0u;
    const bool_t bIncremental =
        ( compiler.flags & TOOL_COMPILER_FLAG_INCREMENTAL ) != 0u;
    const bool_t bValidate =
        ( compiler.flags & TOOL_COMPILER_FLAG_SUPPORTS_VALIDATE ) != 0u;
    const bool_t bDryRun =
        ( compiler.flags & TOOL_COMPILER_FLAG_SUPPORTS_DRY_RUN ) != 0u;
    const string_format_result_t capabilityFormat = StringFormat_Printf(
        details,
        sizeof( details ),
        "deterministic=%s  thread-safe=%s  incremental=%s  validate=%s  dry-run=%s",
        bDeterministic ? "yes" : "no",
        bThreadSafe ? "yes" : "no",
        bIncremental ? "yes" : "no",
        bValidate ? "yes" : "no",
        bDryRun ? "yes" : "no" );
    if ( capabilityFormat.status != string_format_status_t::OK ) {
        return tool_status_t::INTERNAL_ERROR;
    }
    EmitMessage(
        host,
        3u,
        ResourceCompilerText( "capabilities" ),
        { details, capabilityFormat.cchWritten } );
    return tool_status_t::OK;
}

CYPHER_NODISCARD tool_status_t ExecuteInspection(
    const tool_cli_parse_result_t &arguments,
    const tool_host_t &host,
    resource_compiler_state_t *pState ) noexcept
{
    if ( StringView_Equals(
             arguments.pCommand->name,
             ResourceCompilerText( "completion" ) ) ) {
        if ( arguments.nInputs != 1u ||
             !StringView_Equals(
                 arguments.pInputs[0],
                 ResourceCompilerText( "zsh" ) ) ) {
            EmitHostDiagnostic(
                host,
                1u,
                CY_RESOURCE_COMPILER_DIAGNOSTIC_CONTEXT,
                tool_diagnostic_category_t::COMMAND_LINE,
                ResourceCompilerText(
                    "Exactly one supported shell is required; version 1 supports 'zsh'." ) );
            return tool_status_t::INVALID_ARGUMENT;
        }
        return ToolHost_WriteText(
            &host,
            ResourceCompilerText( g_zshCompletion ) );
    }

    if ( StringView_Equals(
             arguments.pCommand->name,
             ResourceCompilerText( "list-compilers" ) ) ) {
        for ( usize iCompiler = 0u;
              iCompiler < pState->registry.nCount;
              ++iCompiler ) {
            const tool_compiler_desc_t *pCompiler =
                ToolCompilerRegistry_At( &pState->registry, iCompiler );
            if ( pCompiler == nullptr ) {
                return tool_status_t::INTERNAL_ERROR;
            }
            char details[512]{};
            const string_format_result_t formatted = StringFormat_Printf(
                details,
                sizeof( details ),
                "id=%.*s  type=%.*s  version=%u  output=%.*s",
                static_cast<int>( pCompiler->id.cchLength ),
                pCompiler->id.pData,
                static_cast<int>( pCompiler->resourceType.cchLength ),
                pCompiler->resourceType.pData,
                pCompiler->nCompilerVersion,
                static_cast<int>( pCompiler->cookedExtension.cchLength ),
                pCompiler->cookedExtension.pData );
            if ( formatted.status != string_format_status_t::OK ) {
                return tool_status_t::INTERNAL_ERROR;
            }
            EmitMessage(
                host,
                iCompiler + 1u,
                pCompiler->displayName,
                { details, formatted.cchWritten } );
        }
        return tool_status_t::OK;
    }

    if ( StringView_Equals(
             arguments.pCommand->name,
             ResourceCompilerText( "describe-compiler" ) ) ) {
        if ( arguments.nInputs != 1u ) {
            EmitHostDiagnostic(
                host,
                1u,
                CY_RESOURCE_COMPILER_DIAGNOSTIC_CONTEXT,
                tool_diagnostic_category_t::COMMAND_LINE,
                ResourceCompilerText(
                    "Exactly one compiler ID is required." ) );
            return tool_status_t::INVALID_ARGUMENT;
        }
        const tool_compiler_desc_t *pCompiler =
            ToolCompilerRegistry_FindById(
                &pState->registry,
                arguments.pInputs[0] );
        if ( pCompiler == nullptr ) {
            EmitHostDiagnostic(
                host,
                1u,
                CY_RESOURCE_COMPILER_DIAGNOSTIC_DISPATCH,
                tool_diagnostic_category_t::COMPILER,
                ResourceCompilerText( "Compiler ID is not registered." ),
                arguments.pInputs[0] );
            return tool_status_t::NOT_FOUND;
        }
        return EmitCompilerDescription( *pCompiler, host );
    }

    if ( StringView_Equals(
             arguments.pCommand->name,
             ResourceCompilerText( "list-formats" ) ) ) {
        tool_sequence_t sequence = 1u;
        for ( usize iCompiler = 0u;
              iCompiler < pState->registry.nCount;
              ++iCompiler ) {
            const tool_compiler_desc_t *pCompiler =
                ToolCompilerRegistry_At( &pState->registry, iCompiler );
            if ( pCompiler == nullptr ) {
                return tool_status_t::INTERNAL_ERROR;
            }
            for ( usize iExtension = 0u;
                  iExtension < pCompiler->nSourceExtensions;
                  ++iExtension ) {
                char details[512]{};
                const string_format_result_t formatted = StringFormat_Printf(
                    details,
                    sizeof( details ),
                    "%.*s -> %.*s  compiler=%.*s  resource=%.*s",
                    static_cast<int>(
                        pCompiler->pSourceExtensions[iExtension].cchLength ),
                    pCompiler->pSourceExtensions[iExtension].pData,
                    static_cast<int>( pCompiler->cookedExtension.cchLength ),
                    pCompiler->cookedExtension.pData,
                    static_cast<int>( pCompiler->id.cchLength ),
                    pCompiler->id.pData,
                    static_cast<int>( pCompiler->resourceType.cchLength ),
                    pCompiler->resourceType.pData );
                if ( formatted.status != string_format_status_t::OK ) {
                    return tool_status_t::INTERNAL_ERROR;
                }
                EmitMessage(
                    host,
                    sequence++,
                    ResourceCompilerText( "format" ),
                    { details, formatted.cchWritten } );
            }
        }
        return tool_status_t::OK;
    }
    return tool_status_t::INVALID_COMMAND;
}

CYPHER_NODISCARD tool_status_t Execute(
    const tool_cli_parse_result_t &arguments,
    const tool_host_t &host,
    const tool_output_policy_t &outputPolicy,
    void *pUserData ) noexcept
{
    auto *pState = static_cast<resource_compiler_state_t *>( pUserData );
    if ( pState == nullptr || arguments.pCommand == nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( !StringView_Equals(
             arguments.pCommand->name,
             ResourceCompilerText( "compile" ) ) &&
         !StringView_Equals(
             arguments.pCommand->name,
             ResourceCompilerText( "validate" ) ) ) {
        return ExecuteInspection( arguments, host, pState );
    }
    tool_target_t target{};
    tool_profile_t profile = tool_profile_t::UNKNOWN;
    if ( !ToolTarget_Parse( OptionValue( arguments, "target" ), &target ) ||
         !ToolProfile_Parse( OptionValue( arguments, "profile" ), &profile ) ) {
        EmitHostDiagnostic(
            host,
            1u,
            CY_RESOURCE_COMPILER_DIAGNOSTIC_CONTEXT,
            tool_diagnostic_category_t::COMMAND_LINE,
            ResourceCompilerText( "Target or build profile is invalid." ) );
        return tool_status_t::INVALID_CONFIGURATION;
    }

    u64 nJobs64 = 0u;
    const string_view_t jobsText = OptionValue( arguments, "jobs" );
    const string_parse_result_t jobsParsed = StringParse_U64(
        jobsText,
        {},
        &nJobs64 );
    if ( !StringParse_Succeeded( jobsParsed ) ||
         jobsParsed.cchConsumed != jobsText.cchLength ||
         nJobs64 == 0u || nJobs64 > CY_U32_MAX ) {
        EmitHostDiagnostic(
            host,
            1u,
            CY_RESOURCE_COMPILER_DIAGNOSTIC_CONTEXT,
            tool_diagnostic_category_t::COMMAND_LINE,
            ResourceCompilerText( "Worker count must be between 1 and 4294967295." ) );
        return tool_status_t::INVALID_CONFIGURATION;
    }

    const cy_system_info_t *pSystemInfo = Cy_SystemInfoGet();
    const string_view_t workingDirectory = pSystemInfo != nullptr
        ? StringView_FromCString( pSystemInfo->process.szWorkingDirectory )
        : ResourceCompilerText( "." );
    const string_view_t sourceRoot = OptionValue( arguments, "source-root" );
    const string_view_t outputRoot = OptionValue( arguments, "output-root" );

    vfs_directory_t sourceDirectory{};
    const vfs_status_t directoryStatus = VfsDirectory_Init(
        &sourceDirectory,
        sourceRoot );
    if ( directoryStatus != vfs_status_t::OK ) {
        EmitHostDiagnostic(
            host,
            1u,
            CY_RESOURCE_COMPILER_DIAGNOSTIC_CONTEXT,
            tool_diagnostic_category_t::FILESYSTEM,
            ResourceCompilerText(
                "Source root could not be opened as a VFS directory." ),
            sourceRoot );
        return ToolStatusFromVfs( directoryStatus );
    }
    struct directory_scope_t {
        vfs_directory_t *pDirectory{ nullptr };
        ~directory_scope_t() noexcept
        {
            VfsDirectory_Shutdown( pDirectory );
        }
    } directoryScope{ &sourceDirectory };
    const vfs_t sourceVfs = VfsDirectory_Make( &sourceDirectory );
    if ( !Vfs_IsValid( &sourceVfs ) ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }

    discovered_inputs_t discovered{};
    if ( !InitDiscoveredInputs( &discovered ) ) {
        return tool_status_t::OUT_OF_MEMORY;
    }
    tool_status_t status = DiscoverInputs(
        arguments,
        &sourceVfs,
        &pState->registry,
        host,
        &discovered );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }

    tool_context_t context{};
    context.applicationId = g_application.id;
    context.workingDirectory = workingDirectory;
    context.sourceRoot = sourceRoot;
    context.outputRoot = outputRoot;
    context.pSourceVfs = &sourceVfs;
    context.target = target;
    context.profile = profile;
    context.nWorkerCount = static_cast<u32>( nJobs64 );
    context.flags = TOOL_CONTEXT_FLAG_AUTOMATION |
                    TOOL_CONTEXT_FLAG_REPRODUCIBLE;

    flags32_t invocationFlags = TOOL_INVOCATION_FLAG_NONE;
    const bool_t bValidate = StringView_Equals(
        arguments.pCommand->name,
        ResourceCompilerText( "validate" ) );
    if ( bValidate ) {
        invocationFlags |= TOOL_INVOCATION_FLAG_DRY_RUN;
    }
    if ( OptionIsTrue( arguments, "keep-going" ) ) {
        invocationFlags |= TOOL_INVOCATION_FLAG_KEEP_GOING;
    }

    tool_session_t session{};
    ToolSession_Init( &session );
    if ( ToolSession_Begin( &session ) != tool_status_t::OK ) {
        return tool_status_t::INTERNAL_ERROR;
    }

    const tool_operation_id_t batchOperationId =
        ToolSession_NextOperationId( &session );
    if ( batchOperationId == CY_TOOL_INVALID_OPERATION_ID ) {
        return tool_status_t::INTERNAL_ERROR;
    }
    tool_sequence_t progressSequence = ToolSession_NextSequence( &session );
    const string_view_t progressTitle = bValidate
        ? ResourceCompilerText( "Validate resources" )
        : ResourceCompilerText( "Compile resources" );
    const timer_tick_t nStartTicks = Cy_TimerNowTicks();
    tool_report_t aggregate{};
    aggregate.operationId = batchOperationId;
    aggregate.nStartTicks = nStartTicks;
    aggregate.nInputsDiscovered = discovered.paths.nCount;

    batch_host_state_t batchHostState{ &host, &aggregate };
    const tool_host_t batchHost = MakeBatchHost( host, &batchHostState );
    tool_status_t aggregateStatus = tool_status_t::OK;
    EmitBatchProgress(
        host,
        batchOperationId,
        progressSequence++,
        tool_progress_state_t::BEGIN,
        tool_status_t::OK,
        0u,
        discovered.paths.nCount,
        progressTitle,
        ResourceCompilerText( "Discover inputs" ) );

    for ( usize iInput = 0u; iInput < discovered.paths.nCount; ++iInput ) {
        const string_view_t input = discovered.paths.pData[iInput];
        if ( ToolHost_IsCancellationRequested( &host ) ) {
            aggregateStatus = tool_status_t::CANCELLED;
            break;
        }
        const tool_operation_id_t operationId =
            ToolSession_NextOperationId( &session );
        const tool_compiler_desc_t *pCompiler = nullptr;
        status = ToolCompilerRegistry_FindForInput(
            &pState->registry,
            input,
            &pCompiler );
        if ( ToolStatus_Failed( status ) || pCompiler == nullptr ) {
            EmitHostDiagnostic(
                host,
                operationId,
                CY_RESOURCE_COMPILER_DIAGNOSTIC_DISPATCH,
                tool_diagnostic_category_t::COMPILER,
                status == tool_status_t::NOT_FOUND
                    ? ResourceCompilerText( "No registered compiler accepts this input." )
                    : ResourceCompilerText( "Compiler ownership for this input is ambiguous or invalid." ),
                input );
            if ( ToolStatus_Succeeded( aggregateStatus ) ) {
                aggregateStatus = status == tool_status_t::NOT_FOUND
                    ? tool_status_t::UNSUPPORTED
                    : status;
            }
            ++aggregate.nInputsProcessed;
            ++aggregate.nFailed;
            ++aggregate.nErrors;
            if ( ( invocationFlags & TOOL_INVOCATION_FLAG_KEEP_GOING ) == 0u ) {
                break;
            }
            if ( aggregate.nInputsProcessed < aggregate.nInputsDiscovered ) {
                EmitBatchProgress(
                    host,
                    batchOperationId,
                    progressSequence++,
                    tool_progress_state_t::UPDATE,
                    tool_status_t::OK,
                    aggregate.nInputsProcessed,
                    aggregate.nInputsDiscovered,
                    progressTitle,
                    input );
            }
            continue;
        }

        text_buffer_t output{};
        if ( !TextBuffer_Init( &output, Allocator_GetSystem() ) ||
             !MakeOutputPath( input, pCompiler->cookedExtension, &output ) ) {
            EmitHostDiagnostic(
                host,
                operationId,
                CY_RESOURCE_COMPILER_DIAGNOSTIC_OUTPUT_PATH,
                tool_diagnostic_category_t::FILESYSTEM,
                ResourceCompilerText( "Cooked output path could not be derived." ),
                input );
            if ( ToolStatus_Succeeded( aggregateStatus ) ) {
                aggregateStatus = tool_status_t::OUT_OF_MEMORY;
            }
            ++aggregate.nInputsProcessed;
            ++aggregate.nFailed;
            ++aggregate.nErrors;
            if ( ( invocationFlags & TOOL_INVOCATION_FLAG_KEEP_GOING ) == 0u ) {
                break;
            }
            if ( aggregate.nInputsProcessed < aggregate.nInputsDiscovered ) {
                EmitBatchProgress(
                    host,
                    batchOperationId,
                    progressSequence++,
                    tool_progress_state_t::UPDATE,
                    tool_status_t::OK,
                    aggregate.nInputsProcessed,
                    aggregate.nInputsDiscovered,
                    progressTitle,
                    input );
            }
            continue;
        }

        tool_invocation_t invocation{
            &g_application,
            arguments.pCommand,
            &context,
            &arguments.options,
            &input,
            1u,
            &batchHost,
            outputPolicy,
            invocationFlags
        };

        const tool_compile_request_t request{
            &invocation,
            operationId,
            input,
            bValidate ? string_view_t{} : TextBuffer_View( &output ),
            pCompiler->resourceType
        };
        tool_report_t report{};
        status = ToolCompiler_Execute( *pCompiler, request, &report );
        if ( ToolStatus_Failed( status ) ) {
            if ( ToolStatus_Succeeded( aggregateStatus ) ) {
                aggregateStatus = status;
            }
            if ( ( invocationFlags & TOOL_INVOCATION_FLAG_KEEP_GOING ) == 0u ) {
                break;
            }
        }
        if ( aggregate.nInputsProcessed < aggregate.nInputsDiscovered ) {
            EmitBatchProgress(
                host,
                batchOperationId,
                progressSequence++,
                tool_progress_state_t::UPDATE,
                tool_status_t::OK,
                aggregate.nInputsProcessed,
                aggregate.nInputsDiscovered,
                progressTitle,
                input );
        }
    }

    aggregate.status = aggregateStatus;
    aggregate.nSkipped = aggregate.nInputsDiscovered -
        aggregate.nSucceeded - aggregate.nFailed;
    aggregate.nEndTicks = Cy_TimerNowTicks();
    if ( aggregate.nEndTicks < aggregate.nStartTicks ) {
        aggregate.nEndTicks = aggregate.nStartTicks;
    }

    tool_progress_state_t finalProgressState =
        tool_progress_state_t::COMPLETE;
    string_view_t finalProgressDetail = bValidate
        ? ResourceCompilerText( "Validated" )
        : ResourceCompilerText( "Compiled" );
    if ( aggregateStatus == tool_status_t::CANCELLED ) {
        finalProgressState = tool_progress_state_t::CANCELLED;
        finalProgressDetail = ResourceCompilerText( "Cancelled" );
    } else if ( ToolStatus_Failed( aggregateStatus ) ) {
        finalProgressState = tool_progress_state_t::FAILED;
        finalProgressDetail = ResourceCompilerText( "Failed" );
    }
    EmitBatchProgress(
        host,
        batchOperationId,
        progressSequence,
        finalProgressState,
        aggregateStatus,
        aggregate.nInputsProcessed,
        aggregate.nInputsDiscovered,
        progressTitle,
        finalProgressDetail );
    ToolHost_EmitReport( &host, aggregate );
    (void)ToolSession_Finish( &session, aggregateStatus );
    return aggregateStatus;
}

} // namespace

tool_exit_code_t CypherResourceCompiler_Run(
    i32 argc,
    const char *const *pArgv ) noexcept
{
    resource_compiler_state_t state{};
    tool_status_t status = ToolCompilerRegistry_Init(
        &state.registry,
        state.pCompilerStorage,
        CYPHER_ARRAY_COUNT( state.pCompilerStorage ) );
    if ( ToolStatus_Succeeded( status ) ) {
        status = ToolCompilerRegistry_Register(
            &state.registry,
            CypherShaderCompiler_Descriptor() );
    }
    if ( ToolStatus_Succeeded( status ) ) {
        status = ToolCompilerRegistry_Register(
            &state.registry,
            CypherTextureCompiler_Descriptor() );
    }
    if ( ToolStatus_Succeeded( status ) ) {
        status = ToolCompilerRegistry_Register(
            &state.registry,
            CypherMaterialCompiler_Descriptor() );
    }
    if ( ToolStatus_Failed( status ) ) {
        return ToolStatus_ExitCode( status );
    }

    const tool_cli_run_desc_t runDesc{
        &g_application,
        g_commands,
        CYPHER_ARRAY_COUNT( g_commands ),
        ResourceCompilerText( "1.0.0" ),
        &ResolveOutputPolicy,
        &Execute,
        &state,
        &g_presentation
    };
    tool_cli_run_options_t options{};
    options.output.flags = TOOL_OUTPUT_FLAG_COLOR;
    options.bWriteStartup = CY_FALSE;
    return ToolCliRunner_Run( runDesc, argc, pArgv, options );
}

} // namespace cypher::tools
