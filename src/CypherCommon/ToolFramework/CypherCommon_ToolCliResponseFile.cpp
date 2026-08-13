//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliResponseFile.cpp
//  Purpose: Implements bounded UTF-8 response-file tokenization and expansion.
//  Details: Whitespace separates arguments, double quotes preserve whitespace,
//           backslash escapes quote/backslash, and optional # or // comments run
//           to the end of a physical line outside quoted arguments.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolCliResponseFile.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_FileIo.h"
#include "CypherCommon_MemoryOps.h"
#include "CypherCommon_StringPath.h"

namespace cypher::common
{
namespace
{

struct response_expand_context_t {
    const tool_cli_response_file_options_t *pOptions{ nullptr };
    tool_cli_response_file_result_t *pResult{ nullptr };
    vector_t<owned_allocation_t> activePaths{};
    usize cbExpandedText{ 0u };
};

bool_t OptionsAreValid( const tool_cli_response_file_options_t &options ) noexcept
{
    return options.nMaxDepth != 0u &&
           options.nMaxArguments != 0u &&
           options.cbMaxExpandedText != 0u;
}

bool_t ResultIsInitialized(
    const tool_cli_response_file_result_t *pResult ) noexcept
{
    return pResult != nullptr &&
           Vector_IsValid( &pResult->ownedArguments ) &&
           Vector_IsValid( &pResult->arguments ) &&
           TextBuffer_IsValid( &pResult->errorPath );
}

void SetParseError(
    tool_cli_response_file_result_t *pResult,
    string_view_t path,
    usize nLine,
    usize nColumn ) noexcept
{
    (void)TextBuffer_Assign( &pResult->errorPath, path );
    pResult->nErrorLine = nLine;
    pResult->nErrorColumn = nColumn;
}

void FreeOwnedText( owned_allocation_t *pAllocation ) noexcept
{
    if ( pAllocation == nullptr || pAllocation->pData == nullptr ) {
        return;
    }
    Allocator_FreeOwned( pAllocation );
}

void FreeOwnedTexts( vector_t<owned_allocation_t> *pAllocations ) noexcept
{
    if ( pAllocations == nullptr || !Vector_IsValid( pAllocations ) ) {
        return;
    }
    for ( usize i = 0u; i < pAllocations->nCount; ++i ) {
        FreeOwnedText( &pAllocations->pData[i] );
    }
    Vector_Clear( pAllocations );
}

tool_status_t CopyOwnedText(
    const allocator_t *pAllocator,
    string_view_t text,
    owned_allocation_t *pAllocationOut ) noexcept
{
    if ( !Allocator_IsValid( pAllocator ) || !StringView_IsValid( text ) ||
         pAllocationOut == nullptr || text.cchLength == CY_USIZE_MAX ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    const usize cbAllocation = text.cchLength + 1u;
    void *pData = Allocator_Allocate(
        pAllocator,
        cbAllocation,
        alignof( char ) );
    if ( pData == nullptr ) {
        return tool_status_t::OUT_OF_MEMORY;
    }
    if ( text.cchLength != 0u ) {
        Cy_MemCopy( pData, text.pData, text.cchLength );
    }
    static_cast<char *>( pData )[text.cchLength] = '\0';
    pAllocationOut->pData = pData;
    pAllocationOut->cbSize = cbAllocation;
    pAllocationOut->nAlignment = alignof( char );
    pAllocationOut->pAllocator = pAllocator;
    return tool_status_t::OK;
}

string_view_t OwnedTextView( const owned_allocation_t &allocation ) noexcept
{
    if ( allocation.pData == nullptr || allocation.cbSize == 0u ) {
        return {};
    }
    return {
        static_cast<const char *>( allocation.pData ),
        allocation.cbSize - 1u
    };
}

bool_t IsCommentStart(
    string_view_t source,
    usize iByte,
    bool_t bAllowComments ) noexcept
{
    if ( !bAllowComments || iByte >= source.cchLength ) {
        return CY_FALSE;
    }
    if ( source.pData[iByte] == '#' ) {
        return CY_TRUE;
    }
    return source.pData[iByte] == '/' && iByte + 1u < source.cchLength &&
           source.pData[iByte + 1u] == '/';
}

void AdvanceLocation( char ch, usize &nLine, usize &nColumn ) noexcept
{
    if ( ch == '\n' ) {
        ++nLine;
        nColumn = 1u;
    } else {
        ++nColumn;
    }
}

tool_status_t AppendOwnedArgument(
    response_expand_context_t &context,
    string_view_t argument ) noexcept
{
    tool_cli_response_file_result_t &result = *context.pResult;
    if ( result.arguments.nCount >= context.pOptions->nMaxArguments ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }
    if ( argument.cchLength == CY_USIZE_MAX ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }
    const usize cbArgument = argument.cchLength + 1u;
    if ( cbArgument >
         context.pOptions->cbMaxExpandedText - context.cbExpandedText ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }

    owned_allocation_t owned{};
    tool_status_t status = CopyOwnedText(
        result.errorPath.pAllocator,
        argument,
        &owned );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    if ( !Vector_PushBackMove(
             &result.ownedArguments,
             static_cast<owned_allocation_t &&>( owned ) ) ) {
        FreeOwnedText( &owned );
        return tool_status_t::OUT_OF_MEMORY;
    }
    const owned_allocation_t &stored =
        result.ownedArguments.pData[result.ownedArguments.nCount - 1u];
    if ( Vector_PushBack( &result.arguments, OwnedTextView( stored ) ) ) {
        context.cbExpandedText += cbArgument;
        return tool_status_t::OK;
    }
    FreeOwnedText( &result.ownedArguments.pData[
        result.ownedArguments.nCount - 1u] );
    Vector_PopBack( &result.ownedArguments );
    return tool_status_t::OUT_OF_MEMORY;
}

bool_t PathIsActive(
    const response_expand_context_t &context,
    string_view_t path ) noexcept
{
    for ( usize i = 0u; i < context.activePaths.nCount; ++i ) {
        if ( StringView_Equals(
                 OwnedTextView( context.activePaths.pData[i] ),
                 path ) ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

tool_status_t PushActivePath(
    response_expand_context_t &context,
    string_view_t path ) noexcept
{
    owned_allocation_t owned{};
    const tool_status_t status = CopyOwnedText(
        context.pResult->errorPath.pAllocator,
        path,
        &owned );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    if ( !Vector_PushBackMove(
             &context.activePaths,
             static_cast<owned_allocation_t &&>( owned ) ) ) {
        FreeOwnedText( &owned );
        return tool_status_t::OUT_OF_MEMORY;
    }
    return tool_status_t::OK;
}

void PopActivePath( response_expand_context_t &context ) noexcept
{
    if ( context.activePaths.nCount != 0u ) {
        FreeOwnedText( &context.activePaths.pData[
            context.activePaths.nCount - 1u] );
        Vector_PopBack( &context.activePaths );
    }
}

tool_status_t ExpandArgument(
    response_expand_context_t &context,
    string_view_t argument,
    string_view_t baseDirectory,
    usize nDepth ) noexcept;

tool_status_t ExpandResponseFile(
    response_expand_context_t &context,
    string_view_t path,
    usize nDepth ) noexcept;

tool_status_t ExpandResponseReference(
    response_expand_context_t &context,
    string_view_t reference,
    string_view_t baseDirectory,
    usize nDepth ) noexcept
{
    if ( reference.cchLength == 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( baseDirectory.cchLength == 0u ||
         StringPath_IsAbsolute( reference, path_style_t::NATIVE ) ||
         StringPath_HasRootName( reference, path_style_t::NATIVE ) ) {
        return ExpandResponseFile( context, reference, nDepth );
    }

    const path_write_result_t measured = StringPath_Join(
        baseDirectory,
        reference,
        path_style_t::NATIVE,
        nullptr,
        0u );
    if ( measured.status != path_status_t::OUTPUT_TRUNCATED &&
         measured.status != path_status_t::OK ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    text_buffer_t joined{};
    if ( !TextBuffer_Init(
             &joined,
             context.pResult->errorPath.pAllocator ) ||
         !TextBuffer_Resize( &joined, measured.cchRequired ) ) {
        return tool_status_t::OUT_OF_MEMORY;
    }
    const path_write_result_t written = StringPath_Join(
        baseDirectory,
        reference,
        path_style_t::NATIVE,
        joined.pData,
        joined.cchCapacity + 1u );
    if ( written.status != path_status_t::OK ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    return ExpandResponseFile(
        context,
        TextBuffer_View( &joined ),
        nDepth );
}

tool_status_t TokenizeAndExpand(
    response_expand_context_t &context,
    string_view_t source,
    string_view_t path,
    usize nDepth ) noexcept
{
    text_buffer_t token{};
    if ( !TextBuffer_Init( &token, context.pResult->errorPath.pAllocator ) ) {
        return tool_status_t::OUT_OF_MEMORY;
    }

    usize iByte = 0u;
    usize nLine = 1u;
    usize nColumn = 1u;
    while ( iByte < source.cchLength ) {
        while ( iByte < source.cchLength ) {
            const char ch = source.pData[iByte];
            if ( IsCommentStart( source, iByte, context.pOptions->bAllowComments ) ) {
                while ( iByte < source.cchLength && source.pData[iByte] != '\n' ) {
                    AdvanceLocation( source.pData[iByte], nLine, nColumn );
                    ++iByte;
                }
                continue;
            }
            if ( !Char_IsWhitespaceAscii( ch ) ) {
                break;
            }
            AdvanceLocation( ch, nLine, nColumn );
            ++iByte;
        }
        if ( iByte >= source.cchLength ) {
            break;
        }

        TextBuffer_Clear( &token );
        const usize nTokenLine = nLine;
        const usize nTokenColumn = nColumn;
        bool_t bQuoted = CY_FALSE;
        bool_t bSawContent = CY_FALSE;
        while ( iByte < source.cchLength ) {
            const char ch = source.pData[iByte];
            if ( !bQuoted && Char_IsWhitespaceAscii( ch ) ) {
                break;
            }
            if ( !bQuoted &&
                 IsCommentStart( source, iByte, context.pOptions->bAllowComments ) ) {
                break;
            }
            if ( ch == '"' ) {
                bQuoted = !bQuoted;
                bSawContent = CY_TRUE;
                AdvanceLocation( ch, nLine, nColumn );
                ++iByte;
                continue;
            }
            if ( ch == '\\' && iByte + 1u < source.cchLength &&
                 ( source.pData[iByte + 1u] == '\\' ||
                   source.pData[iByte + 1u] == '"' ) ) {
                AdvanceLocation( ch, nLine, nColumn );
                ++iByte;
            }
            if ( !TextBuffer_AppendChar( &token, source.pData[iByte] ) ) {
                return tool_status_t::OUT_OF_MEMORY;
            }
            bSawContent = CY_TRUE;
            AdvanceLocation( source.pData[iByte], nLine, nColumn );
            ++iByte;
        }

        if ( bQuoted ) {
            SetParseError(
                context.pResult,
                path,
                nTokenLine,
                nTokenColumn );
            return tool_status_t::INVALID_CONFIGURATION;
        }
        if ( bSawContent ) {
            const tool_status_t status = ExpandArgument(
                context,
                TextBuffer_View( &token ),
                StringPath_Parent( path ),
                nDepth );
            if ( ToolStatus_Failed( status ) ) {
                if ( context.pResult->nErrorLine == 0u ) {
                    SetParseError(
                        context.pResult,
                        path,
                        nTokenLine,
                        nTokenColumn );
                }
                return status;
            }
        }
    }
    return tool_status_t::OK;
}

tool_status_t ExpandResponseFile(
    response_expand_context_t &context,
    string_view_t path,
    usize nDepth ) noexcept
{
    if ( nDepth > context.pOptions->nMaxDepth ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }

    const flags32_t normalizeFlags =
        PATH_NORMALIZE_FLAG_COLLAPSE_SEPARATORS |
        PATH_NORMALIZE_FLAG_RESOLVE_DOT |
        PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT;
    const path_write_result_t measured = StringPath_Normalize(
        path,
        path_style_t::NATIVE,
        normalizeFlags,
        nullptr,
        0u );
    if ( measured.status != path_status_t::OK &&
         measured.status != path_status_t::OUTPUT_TRUNCATED ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    text_buffer_t normalized{};
    if ( !TextBuffer_Init(
             &normalized,
             context.pResult->errorPath.pAllocator ) ||
         !TextBuffer_Resize( &normalized, measured.cchRequired ) ) {
        return tool_status_t::OUT_OF_MEMORY;
    }
    const path_write_result_t written = StringPath_Normalize(
        path,
        path_style_t::NATIVE,
        normalizeFlags,
        normalized.pData,
        normalized.cchCapacity + 1u );
    if ( written.status != path_status_t::OK ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    const string_view_t normalizedPath = TextBuffer_View( &normalized );

    if ( PathIsActive( context, normalizedPath ) ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }

    blob_t contents{};
    if ( !Blob_Init( &contents, context.pResult->errorPath.pAllocator ) ) {
        return tool_status_t::OUT_OF_MEMORY;
    }
    if ( !FileIo_ReadAllNative( normalizedPath, &contents ) ) {
        return tool_status_t::IO_ERROR;
    }
    if ( contents.cbSize > context.pOptions->cbMaxExpandedText ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }

    const tool_status_t pushStatus = PushActivePath( context, normalizedPath );
    if ( ToolStatus_Failed( pushStatus ) ) {
        return pushStatus;
    }
    const tool_status_t status = TokenizeAndExpand(
        context,
        {
            reinterpret_cast<const char *>( contents.pData ),
            contents.cbSize
        },
        OwnedTextView( context.activePaths.pData[
            context.activePaths.nCount - 1u] ),
        nDepth );
    PopActivePath( context );
    return status;
}

tool_status_t ExpandArgument(
    response_expand_context_t &context,
    string_view_t argument,
    string_view_t baseDirectory,
    usize nDepth ) noexcept
{
    if ( argument.cchLength > 1u && argument.pData[0] == '@' ) {
        return ExpandResponseReference(
            context,
            StringView_RemovePrefix( argument, 1u ),
            baseDirectory,
            nDepth + 1u );
    }
    return AppendOwnedArgument( context, argument );
}

} // namespace

tool_status_t ToolCliResponseFile_InitResult(
    tool_cli_response_file_result_t *pResult,
    const allocator_t *pAllocator ) noexcept
{
    if ( pResult == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( !Vector_Init( &pResult->ownedArguments, pAllocator ) ||
         !Vector_Init( &pResult->arguments, pAllocator ) ||
         !TextBuffer_Init( &pResult->errorPath, pAllocator ) ) {
        ToolCliResponseFile_ShutdownResult( pResult );
        return tool_status_t::OUT_OF_MEMORY;
    }
    return tool_status_t::OK;
}

void ToolCliResponseFile_ShutdownResult(
    tool_cli_response_file_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return;
    }
    FreeOwnedTexts( &pResult->ownedArguments );
    Vector_Shutdown( &pResult->ownedArguments );
    Vector_Shutdown( &pResult->arguments );
    TextBuffer_Shutdown( &pResult->errorPath );
    pResult->nErrorLine = 0u;
    pResult->nErrorColumn = 0u;
}

tool_status_t ToolCliResponseFile_Expand(
    span_t<const string_view_t> arguments,
    const tool_cli_response_file_options_t &options,
    tool_cli_response_file_result_t *pResult ) noexcept
{
    if ( !Span_IsValid( arguments ) || !OptionsAreValid( options ) ||
         !ResultIsInitialized( pResult ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    FreeOwnedTexts( &pResult->ownedArguments );
    Vector_Clear( &pResult->arguments );
    TextBuffer_Clear( &pResult->errorPath );
    pResult->nErrorLine = 0u;
    pResult->nErrorColumn = 0u;

    response_expand_context_t context{ &options, pResult, {}, 0u };
    if ( !Vector_Init( &context.activePaths, pResult->errorPath.pAllocator ) ) {
        return tool_status_t::OUT_OF_MEMORY;
    }
    for ( usize i = 0u; i < arguments.nCount; ++i ) {
        if ( !StringView_IsValid( arguments.pData[i] ) ) {
            return tool_status_t::INVALID_ARGUMENT;
        }
        const tool_status_t status = ExpandArgument(
            context,
            arguments.pData[i],
            {},
            0u );
        if ( ToolStatus_Failed( status ) ) {
            FreeOwnedTexts( &context.activePaths );
            return status;
        }
    }
    FreeOwnedTexts( &context.activePaths );
    return tool_status_t::OK;
}

} // namespace cypher::common
