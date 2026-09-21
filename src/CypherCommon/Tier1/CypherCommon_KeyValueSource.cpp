//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueSource.cpp
//  Purpose: Implements bounded transactional CYKV 2 dependency resolution.
//  Details: Includes become typed namespaces, bases fill missing object values,
//           and canonical source identities provide cycle and limit enforcement.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueSource.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_KeyValueInternal.h"
#include "CypherCommon_KeyValueParserInternal.h"
#include "CypherCommon_MemoryOps.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_Unicode.h"

namespace cypher::common
{

namespace
{

enum class source_record_state_t : u8 {
    EMPTY = 0u,
    RESOLVING,
    RESOLVED
};

struct source_record_t {
    char path[CY_KEY_VALUE_SOURCE_DIAGNOSTIC_PATH_CAPACITY]{};
    usize cchPath{ 0u };
    key_value_document_t *pDocument{ nullptr };
    source_record_state_t state{ source_record_state_t::EMPTY };
};

struct source_directive_t {
    key_value_dependency_kind_t kind{ key_value_dependency_kind_t::INCLUDE };
    string_view_t referencedPath{};
    string_view_t alias{};
    text_location_t location{};
    usize iEraseBegin{ 0u };
    usize iEraseEnd{ 0u };
    key_value_document_t *pResolvedDocument{ nullptr };
};

struct source_context_t {
    key_value_source_options_t options{};
    key_value_source_result_t result{};
    const allocator_t *pAllocator{ nullptr };
    key_value_document_t *pDestination{ nullptr };
    source_record_t *pSources{ nullptr };
    source_directive_t *pDirectives{ nullptr };
    key_value_definition_seed_t *pIncludeSeeds{ nullptr };
    usize nSources{ 0u };
    usize nDirectives{ 0u };
};

CYPHER_NODISCARD string_view_t RecordPath(
    const source_record_t &record ) noexcept
{
    return { record.path, record.cchPath };
}

void CopyDiagnosticPath(
    string_view_t path,
    char *pDest,
    usize &cchDest ) noexcept
{
    cchDest = 0u;
    if ( pDest == nullptr || !StringView_IsValid( path ) ) {
        return;
    }
    const usize cchCopy = path.cchLength <
            CY_KEY_VALUE_SOURCE_DIAGNOSTIC_PATH_CAPACITY - 1u
        ? path.cchLength
        : CY_KEY_VALUE_SOURCE_DIAGNOSTIC_PATH_CAPACITY - 1u;
    for ( usize iByte = 0u; iByte < cchCopy; ++iByte ) {
        const unsigned char ch = static_cast<unsigned char>( path.pData[iByte] );
        pDest[iByte] = ch < 0x20u || ch == 0x7fu
            ? '?'
            : path.pData[iByte];
    }
    pDest[cchCopy] = '\0';
    cchDest = cchCopy;
}

void Fail(
    source_context_t &context,
    key_value_source_status_t status,
    string_view_t sourcePath,
    string_view_t referencedPath = {},
    text_location_t location = {},
    key_value_dependency_kind_t kind =
        key_value_dependency_kind_t::INCLUDE ) noexcept
{
    if ( context.result.status != key_value_source_status_t::OK ) {
        return;
    }
    context.result.status = status;
    context.result.errorLocation = location;
    context.result.dependencyKind = kind;
    CopyDiagnosticPath(
        sourcePath,
        context.result.errorSourcePath,
        context.result.cchErrorSourcePath );
    CopyDiagnosticPath(
        referencedPath,
        context.result.errorReferencedPath,
        context.result.cchErrorReferencedPath );
}

void FailParse(
    source_context_t &context,
    string_view_t sourcePath,
    key_value_parse_status_t parseStatus,
    text_location_t location = {} ) noexcept
{
    context.result.parseResult.status = parseStatus;
    context.result.parseResult.errorLocation = location;
    Fail(
        context,
        key_value_source_status_t::PARSE_FAILED,
        sourcePath,
        {},
        location );
}

CYPHER_NODISCARD text_location_t TrackedSourceLocationAt(
    string_view_t text,
    usize iTarget,
    text_location_t &cursor ) noexcept
{
    if ( iTarget < cursor.iByte ) {
        text_location_t restarted{};
        return TrackedSourceLocationAt( text, iTarget, restarted );
    }

    // Match the core parser: columns count decoded Unicode scalars and byte
    // offsets retain their exact position in the UTF-8 source.
    while ( cursor.iByte < iTarget && cursor.iByte < text.cchLength ) {
        const char ch = text.pData[cursor.iByte];
        if ( ch == '\r' ) {
            cursor.iByte += cursor.iByte + 1u < text.cchLength &&
                     text.pData[cursor.iByte + 1u] == '\n'
                ? 2u
                : 1u;
            ++cursor.nLine;
            cursor.nColumn = 1u;
            continue;
        }
        if ( ch == '\n' ) {
            ++cursor.iByte;
            ++cursor.nLine;
            cursor.nColumn = 1u;
            continue;
        }

        unicode_code_point_t codePoint = 0u;
        const unicode_result_t decoded = Unicode_DecodeUtf8(
            {
                text.pData + cursor.iByte,
                text.cchLength - cursor.iByte
            },
            &codePoint );
        cursor.iByte += decoded.status == unicode_status_t::OK
            ? decoded.nInputConsumed
            : 1u;
        ++cursor.nColumn;
    }
    text_location_t location = cursor;
    location.iByte = iTarget;
    return location;
}

CYPHER_NODISCARD text_location_t SourceLocationAt(
    string_view_t text,
    usize iTarget ) noexcept
{
    text_location_t cursor{};
    return TrackedSourceLocationAt( text, iTarget, cursor );
}

CYPHER_NODISCARD bool_t ValidateSourceBeforeDependencyIo(
    source_context_t &context,
    string_view_t sourcePath,
    string_view_t text ) noexcept
{
    if ( text.cchLength > context.options.parseOptions.cbMaxInput ) {
        FailParse(
            context,
            sourcePath,
            key_value_parse_status_t::INPUT_LIMIT );
        return CY_FALSE;
    }

    const unicode_result_t utf8 = Unicode_ValidateUtf8( text );
    if ( utf8.status != unicode_status_t::OK ) {
        FailParse(
            context,
            sourcePath,
            key_value_parse_status_t::INVALID_ENCODING,
            SourceLocationAt( text, utf8.iError ) );
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        const char ch = text.pData[iByte];
        if ( ch == '\0' ||
             ( ch == '\r' &&
               ( iByte + 1u == text.cchLength ||
                 text.pData[iByte + 1u] != '\n' ) ) ) {
            FailParse(
                context,
                sourcePath,
                key_value_parse_status_t::INVALID_ENCODING,
                SourceLocationAt( text, iByte ) );
            return CY_FALSE;
        }
        if ( ch == '\r' ) ++iByte;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t IsIdentifierStart( char ch ) noexcept
{
    return Char_IsAlphaAscii( ch ) || ch == '_';
}

CYPHER_NODISCARD bool_t IsIdentifierBody( char ch ) noexcept
{
    return Char_IsAlphaNumericAscii( ch ) || ch == '_';
}

CYPHER_NODISCARD bool_t ParseOptionsAreValid(
    const key_value_parse_options_t &options ) noexcept
{
    constexpr flags32_t validFlags =
        KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS |
        KEY_VALUE_PARSE_FLAG_ALLOW_TRAILING_COMMA |
        KEY_VALUE_PARSE_FLAG_ALLOW_UNQUOTED_KEYS |
        KEY_VALUE_PARSE_FLAG_REJECT_DUPLICATE_KEYS |
        KEY_VALUE_PARSE_FLAG_ALLOW_ROOT_VALUE;
    return ( options.flags & ~validFlags ) == 0u &&
           options.cbMaxInput != 0u &&
           options.nMaxDepth != 0u &&
           options.nMaxDepth <= CY_KEY_VALUE_MAX_DEPTH &&
           options.nMaxNodes != 0u &&
           options.nMaxContainerValues != 0u &&
           options.nMaxCommentDepth != 0u &&
           options.cbMaxStringData != 0u;
}

CYPHER_NODISCARD bool_t IsCanonicalVirtualPath(
    source_context_t &context,
    string_view_t path,
    string_view_t requestingPath,
    string_view_t referencedPath,
    text_location_t location,
    key_value_dependency_kind_t kind ) noexcept
{
    if ( !StringView_IsValid( path ) || path.cchLength == 0u ) {
        Fail(
            context,
            key_value_source_status_t::INVALID_PATH,
            requestingPath,
            referencedPath,
            location,
            kind );
        return CY_FALSE;
    }
    if ( Unicode_ValidateUtf8( path ).status != unicode_status_t::OK ) {
        Fail(
            context,
            key_value_source_status_t::INVALID_PATH,
            requestingPath,
            referencedPath,
            location,
            kind );
        return CY_FALSE;
    }
    if ( path.cchLength > context.options.cchMaxPath ) {
        Fail(
            context,
            key_value_source_status_t::PATH_LIMIT,
            requestingPath,
            referencedPath,
            location,
            kind );
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < path.cchLength; ++iByte ) {
        const unsigned char ch = static_cast<unsigned char>( path.pData[iByte] );
        if ( ch < 0x20u || ch == 0x7fu || ch == '\\' ) {
            Fail(
                context,
                key_value_source_status_t::INVALID_PATH,
                requestingPath,
                referencedPath,
                location,
                kind );
            return CY_FALSE;
        }
    }

    char normalized[CY_KEY_VALUE_SOURCE_DIAGNOSTIC_PATH_CAPACITY]{};
    const path_write_result_t result = StringPath_Normalize(
        path,
        path_style_t::VIRTUAL,
        PATH_NORMALIZE_FLAG_COLLAPSE_SEPARATORS |
            PATH_NORMALIZE_FLAG_RESOLVE_DOT |
            PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT |
            PATH_NORMALIZE_FLAG_REJECT_ABSOLUTE |
            PATH_NORMALIZE_FLAG_REJECT_ABOVE_ROOT,
        normalized,
        context.options.cchMaxPath + 1u );
    if ( result.status != path_status_t::OK ||
         result.cchWritten != path.cchLength ||
         !StringView_Equals(
             path,
             { normalized, result.cchWritten } ) ) {
        Fail(
            context,
            key_value_source_status_t::INVALID_PATH,
            requestingPath,
            referencedPath,
            location,
            kind );
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t IsValidReferencedPath(
    source_context_t &context,
    string_view_t sourcePath,
    string_view_t referencedPath,
    text_location_t location,
    key_value_dependency_kind_t kind ) noexcept
{
    if ( !StringView_IsValid( referencedPath ) ||
         referencedPath.cchLength == 0u ||
         StringPath_IsAbsolute( referencedPath, path_style_t::VIRTUAL ) ) {
        Fail(
            context,
            key_value_source_status_t::INVALID_PATH,
            sourcePath,
            referencedPath,
            location,
            kind );
        return CY_FALSE;
    }
    if ( referencedPath.cchLength > context.options.cchMaxPath ) {
        Fail(
            context,
            key_value_source_status_t::PATH_LIMIT,
            sourcePath,
            referencedPath,
            location,
            kind );
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < referencedPath.cchLength; ++iByte ) {
        const unsigned char ch = static_cast<unsigned char>(
            referencedPath.pData[iByte] );
        if ( ch < 0x20u || ch == 0x7fu || ch == '\\' ) {
            Fail(
                context,
                key_value_source_status_t::INVALID_PATH,
                sourcePath,
                referencedPath,
                location,
                kind );
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t HeaderSelectsCykv2(
    string_view_t text,
    usize &iAfterHeader ) noexcept
{
    iAfterHeader = 0u;
    if ( !StringView_IsValid( text ) || text.cchLength < 8u ||
         text.pData[0] != '@' ) {
        return CY_FALSE;
    }

    usize iFirstEnd = 0u;
    while ( iFirstEnd < text.cchLength && text.pData[iFirstEnd] != '\n' ) {
        ++iFirstEnd;
    }
    if ( iFirstEnd == text.cchLength ) {
        return CY_FALSE;
    }
    usize iSecondEnd = iFirstEnd + 1u;
    while ( iSecondEnd < text.cchLength && text.pData[iSecondEnd] != '\n' ) {
        ++iSecondEnd;
    }
    if ( iSecondEnd == text.cchLength ) {
        return CY_FALSE;
    }
    iAfterHeader = iSecondEnd + 1u;

    usize iByte = 0u;
    constexpr const char prefix[] = "@cykv";
    for ( usize iPrefix = 0u; iPrefix < sizeof( prefix ) - 1u; ++iPrefix ) {
        if ( iByte == iFirstEnd || text.pData[iByte++] != prefix[iPrefix] ) {
            return CY_FALSE;
        }
    }
    if ( iByte == iFirstEnd ||
         ( text.pData[iByte] != ' ' && text.pData[iByte] != '\t' ) ) {
        return CY_FALSE;
    }
    while ( iByte < iFirstEnd &&
            ( text.pData[iByte] == ' ' || text.pData[iByte] == '\t' ) ) {
        ++iByte;
    }
    if ( iByte == iFirstEnd || text.pData[iByte++] != '2' ) {
        return CY_FALSE;
    }
    while ( iByte < iFirstEnd &&
            ( text.pData[iByte] == ' ' || text.pData[iByte] == '\t' ||
              text.pData[iByte] == '\r' ) ) {
        ++iByte;
    }
    return iByte == iFirstEnd;
}

CYPHER_NODISCARD bool_t ValidateCykv2HeaderBeforeDependencyIo(
    source_context_t &context,
    string_view_t sourcePath,
    string_view_t text,
    usize iAfterHeader ) noexcept
{
    if ( iAfterHeader > CY_USIZE_MAX - 3u ) {
        FailParse(
            context,
            sourcePath,
            key_value_parse_status_t::INPUT_LIMIT );
        return CY_FALSE;
    }
    const usize cchProbe = iAfterHeader + 2u;
    char *pProbe = static_cast<char *>( Allocator_Allocate(
        context.pAllocator,
        cchProbe + 1u,
        alignof( char ) ) );
    key_value_document_t *pProbeDocument = KeyValue_InternalCreateLike(
        context.pDestination,
        CY_FALSE );
    if ( pProbe == nullptr || pProbeDocument == nullptr ) {
        if ( pProbe != nullptr ) {
            Allocator_Free(
                context.pAllocator,
                pProbe,
                cchProbe + 1u,
                alignof( char ) );
        }
        if ( pProbeDocument != nullptr ) {
            KeyValue_DestroyDocument( pProbeDocument );
        }
        Fail(
            context,
            key_value_source_status_t::OUT_OF_MEMORY,
            sourcePath );
        return CY_FALSE;
    }

    Cy_MemCopy( pProbe, text.pData, iAfterHeader );
    pProbe[iAfterHeader] = '{';
    pProbe[iAfterHeader + 1u] = '}';
    pProbe[cchProbe] = '\0';
    key_value_parse_options_t probeOptions = context.options.parseOptions;
    if ( probeOptions.cbMaxInput < cchProbe ) {
        // The caller's limit applies to the real source, which was already
        // validated. Do not let the two synthetic root bytes turn a malformed
        // header-only source into a false INPUT_LIMIT diagnostic.
        probeOptions.cbMaxInput = cchProbe;
    }
    const key_value_parse_result_t parsed = KeyValue_ParseText(
        { pProbe, cchProbe },
        probeOptions,
        pProbeDocument );
    KeyValue_DestroyDocument( pProbeDocument );
    Allocator_Free(
        context.pAllocator,
        pProbe,
        cchProbe + 1u,
        alignof( char ) );
    if ( parsed.status == key_value_parse_status_t::OK ) {
        return CY_TRUE;
    }
    context.result.parseResult = parsed;
    Fail(
        context,
        key_value_source_status_t::PARSE_FAILED,
        sourcePath,
        {},
        parsed.errorLocation );
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t SkipPreambleTrivia(
    source_context_t &context,
    string_view_t sourcePath,
    string_view_t text,
    usize &iByte,
    text_location_t &locationCursor ) noexcept
{
    while ( iByte < text.cchLength ) {
        const char ch = text.pData[iByte];
        if ( ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ) {
            ++iByte;
            continue;
        }
        if ( ch != '/' || iByte + 1u >= text.cchLength ) {
            return CY_TRUE;
        }
        const char chNext = text.pData[iByte + 1u];
        if ( chNext != '/' && chNext != '*' ) {
            return CY_TRUE;
        }
        if ( ( context.options.parseOptions.flags &
               KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS ) == 0u ) {
            FailParse(
                context,
                sourcePath,
                key_value_parse_status_t::SYNTAX_ERROR,
                TrackedSourceLocationAt( text, iByte, locationCursor ) );
            return CY_FALSE;
        }
        if ( chNext == '/' ) {
            iByte += 2u;
            while ( iByte < text.cchLength && text.pData[iByte] != '\n' ) {
                ++iByte;
            }
            continue;
        }
        const usize iComment = iByte;
        iByte += 2u;
        usize nDepth = 1u;
        while ( iByte < text.cchLength && nDepth != 0u ) {
            if ( iByte + 1u < text.cchLength &&
                 text.pData[iByte] == '/' &&
                 text.pData[iByte + 1u] == '*' ) {
                if ( nDepth >=
                     context.options.parseOptions.nMaxCommentDepth ) {
                    FailParse(
                        context,
                        sourcePath,
                        key_value_parse_status_t::COMMENT_DEPTH_LIMIT,
                        TrackedSourceLocationAt(
                            text,
                            iByte,
                            locationCursor ) );
                    return CY_FALSE;
                }
                ++nDepth;
                iByte += 2u;
            } else if ( iByte + 1u < text.cchLength &&
                        text.pData[iByte] == '*' &&
                        text.pData[iByte + 1u] == '/' ) {
                --nDepth;
                iByte += 2u;
            } else {
                ++iByte;
            }
        }
        if ( nDepth != 0u ) {
            FailParse(
                context,
                sourcePath,
                key_value_parse_status_t::LEXER_ERROR,
                TrackedSourceLocationAt(
                    text,
                    iComment,
                    locationCursor ) );
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

void SkipHorizontal( string_view_t text, usize iEnd, usize &iByte ) noexcept
{
    while ( iByte < iEnd &&
            ( text.pData[iByte] == ' ' || text.pData[iByte] == '\t' ) ) {
        ++iByte;
    }
}

CYPHER_NODISCARD bool_t ParseQuotedPath(
    string_view_t text,
    usize iLineEnd,
    usize &iByte,
    string_view_t &path ) noexcept
{
    if ( iByte == iLineEnd || text.pData[iByte] != '"' ) {
        return CY_FALSE;
    }
    const usize iBegin = ++iByte;
    while ( iByte < iLineEnd && text.pData[iByte] != '"' ) {
        const unsigned char ch = static_cast<unsigned char>( text.pData[iByte] );
        if ( ch < 0x20u || ch == 0x7fu || text.pData[iByte] == '\\' ) {
            return CY_FALSE;
        }
        ++iByte;
    }
    if ( iByte == iLineEnd || iByte == iBegin ) {
        return CY_FALSE;
    }
    path = { text.pData + iBegin, iByte - iBegin };
    ++iByte;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t DirectiveTailIsValid(
    string_view_t text,
    usize iLineEnd,
    usize &iByte,
    bool_t bAllowComments ) noexcept
{
    SkipHorizontal( text, iLineEnd, iByte );
    if ( iByte < iLineEnd && text.pData[iByte] == '\r' &&
         iByte + 1u == iLineEnd ) {
        ++iByte;
    }
    if ( iByte == iLineEnd ) {
        return CY_TRUE;
    }
    if ( bAllowComments && iByte + 1u < iLineEnd &&
         text.pData[iByte] == '/' &&
         text.pData[iByte + 1u] == '/' ) {
        iByte = iLineEnd;
        return CY_TRUE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ScanDependencyDirectives(
    source_context_t &context,
    string_view_t sourcePath,
    string_view_t text,
    usize &iDirectiveBegin,
    usize &iDirectiveEnd ) noexcept
{
    iDirectiveBegin = context.nDirectives;
    iDirectiveEnd = iDirectiveBegin;
    usize iByte = 0u;
    if ( !HeaderSelectsCykv2( text, iByte ) ) {
        return CY_TRUE;
    }
    if ( !ValidateCykv2HeaderBeforeDependencyIo(
             context,
             sourcePath,
             text,
             iByte ) ) {
        return CY_FALSE;
    }
    usize nIncludeAliases = 0u;
    text_location_t locationCursor{};

    for ( ;; ) {
        if ( !SkipPreambleTrivia(
                 context,
                 sourcePath,
                 text,
                 iByte,
                 locationCursor ) ) {
            return CY_FALSE;
        }
        if ( iByte == text.cchLength || text.pData[iByte] != '#' ) {
            break;
        }
        const usize iHash = iByte++;
        const usize iKeyword = iByte;
        while ( iByte < text.cchLength &&
                Char_IsAlphaAscii( text.pData[iByte] ) ) {
            ++iByte;
        }
        const string_view_t keyword{
            text.pData + iKeyword,
            iByte - iKeyword
        };
        const text_location_t location = TrackedSourceLocationAt(
            text,
            iHash,
            locationCursor );
        if ( StringView_Equals(
                 keyword,
                 StringView_FromCString( "define" ) ) ) {
            break;
        }

        key_value_dependency_kind_t kind{};
        if ( StringView_Equals(
                 keyword,
                 StringView_FromCString( "include" ) ) ) {
            kind = key_value_dependency_kind_t::INCLUDE;
        } else if ( StringView_Equals(
                        keyword,
                        StringView_FromCString( "base" ) ) ) {
            kind = key_value_dependency_kind_t::BASE;
        } else {
            Fail(
                context,
                key_value_source_status_t::INVALID_DIRECTIVE,
                sourcePath,
                {},
                location );
            return CY_FALSE;
        }

        usize iLineEnd = iByte;
        while ( iLineEnd < text.cchLength && text.pData[iLineEnd] != '\n' ) {
            ++iLineEnd;
        }
        if ( iByte == iLineEnd ||
             ( text.pData[iByte] != ' ' && text.pData[iByte] != '\t' ) ) {
            Fail(
                context,
                key_value_source_status_t::INVALID_DIRECTIVE,
                sourcePath,
                {},
                location,
                kind );
            return CY_FALSE;
        }
        SkipHorizontal( text, iLineEnd, iByte );

        string_view_t referencedPath{};
        if ( !ParseQuotedPath( text, iLineEnd, iByte, referencedPath ) ||
             !IsValidReferencedPath(
                 context,
                 sourcePath,
                 referencedPath,
                 location,
                 kind ) ) {
            if ( context.result.status == key_value_source_status_t::OK ) {
                Fail(
                    context,
                    key_value_source_status_t::INVALID_DIRECTIVE,
                    sourcePath,
                    referencedPath,
                    location,
                    kind );
            }
            return CY_FALSE;
        }

        string_view_t alias{};
        if ( kind == key_value_dependency_kind_t::INCLUDE ) {
            if ( iByte == iLineEnd ||
                 ( text.pData[iByte] != ' ' && text.pData[iByte] != '\t' ) ) {
                Fail(
                    context,
                    key_value_source_status_t::INVALID_DIRECTIVE,
                    sourcePath,
                    referencedPath,
                    location,
                    kind );
                return CY_FALSE;
            }
            SkipHorizontal( text, iLineEnd, iByte );
            constexpr const char asKeyword[] = "as";
            if ( iLineEnd - iByte < sizeof( asKeyword ) - 1u ||
                 text.pData[iByte] != 'a' || text.pData[iByte + 1u] != 's' ) {
                Fail(
                    context,
                    key_value_source_status_t::INVALID_DIRECTIVE,
                    sourcePath,
                    referencedPath,
                    location,
                    kind );
                return CY_FALSE;
            }
            iByte += sizeof( asKeyword ) - 1u;
            if ( iByte == iLineEnd ||
                 ( text.pData[iByte] != ' ' && text.pData[iByte] != '\t' ) ) {
                Fail(
                    context,
                    key_value_source_status_t::INVALID_DIRECTIVE,
                    sourcePath,
                    referencedPath,
                    location,
                    kind );
                return CY_FALSE;
            }
            SkipHorizontal( text, iLineEnd, iByte );
            if ( iByte == iLineEnd || !IsIdentifierStart( text.pData[iByte] ) ) {
                Fail(
                    context,
                    key_value_source_status_t::INVALID_DIRECTIVE,
                    sourcePath,
                    referencedPath,
                    location,
                    kind );
                return CY_FALSE;
            }
            const usize iAlias = iByte++;
            while ( iByte < iLineEnd && IsIdentifierBody( text.pData[iByte] ) ) {
                ++iByte;
            }
            alias = { text.pData + iAlias, iByte - iAlias };
            if ( nIncludeAliases >=
                 context.options.parseOptions.nMaxDefinitions ) {
                context.result.parseResult.status =
                    key_value_parse_status_t::DEFINITION_LIMIT;
                context.result.parseResult.errorLocation = location;
                Fail(
                    context,
                    key_value_source_status_t::PARSE_FAILED,
                    sourcePath,
                    referencedPath,
                    location,
                    kind );
                return CY_FALSE;
            }
            for ( usize iPrevious = iDirectiveBegin;
                  iPrevious < context.nDirectives;
                  ++iPrevious ) {
                const source_directive_t &previous =
                    context.pDirectives[iPrevious];
                if ( previous.kind == key_value_dependency_kind_t::INCLUDE &&
                     StringView_Equals( previous.alias, alias ) ) {
                    Fail(
                        context,
                        key_value_source_status_t::DUPLICATE_ALIAS,
                        sourcePath,
                        referencedPath,
                        location,
                        kind );
                    return CY_FALSE;
                }
            }
            ++nIncludeAliases;
        }
        if ( !DirectiveTailIsValid(
                 text,
                 iLineEnd,
                 iByte,
                 ( context.options.parseOptions.flags &
                   KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS ) != 0u ) ) {
            Fail(
                context,
                key_value_source_status_t::INVALID_DIRECTIVE,
                sourcePath,
                referencedPath,
                location,
                kind );
            return CY_FALSE;
        }
        if ( context.nDirectives >= context.options.nMaxEdges ) {
            Fail(
                context,
                key_value_source_status_t::EDGE_LIMIT,
                sourcePath,
                referencedPath,
                location,
                kind );
            return CY_FALSE;
        }
        source_directive_t &directive =
            context.pDirectives[context.nDirectives++];
        directive.kind = kind;
        directive.referencedPath = referencedPath;
        directive.alias = alias;
        directive.location = location;
        directive.iEraseBegin = iHash;
        directive.iEraseEnd = iLineEnd;
        ++context.result.nDependencyEdges;
        iByte = iLineEnd < text.cchLength ? iLineEnd + 1u : iLineEnd;
    }
    iDirectiveEnd = context.nDirectives;
    return CY_TRUE;
}

CYPHER_NODISCARD usize FindSourceRecord(
    const source_context_t &context,
    string_view_t canonicalPath ) noexcept
{
    for ( usize iSource = 0u; iSource < context.nSources; ++iSource ) {
        if ( StringView_Equals(
                 RecordPath( context.pSources[iSource] ),
                 canonicalPath ) ) {
            return iSource;
        }
    }
    return CY_USIZE_MAX;
}

CYPHER_NODISCARD bool_t AddSourceRecord(
    source_context_t &context,
    string_view_t canonicalPath,
    string_view_t text,
    usize &iRecord ) noexcept
{
    if ( context.nSources >= context.options.nMaxSources ) {
        Fail(
            context,
            key_value_source_status_t::SOURCE_LIMIT,
            canonicalPath );
        return CY_FALSE;
    }
    if ( text.cchLength > context.options.cbMaxAggregateSources ||
         context.result.cbAggregateSources >
             context.options.cbMaxAggregateSources - text.cchLength ) {
        Fail(
            context,
            key_value_source_status_t::SOURCE_BYTE_LIMIT,
            canonicalPath );
        return CY_FALSE;
    }

    iRecord = context.nSources++;
    source_record_t &record = context.pSources[iRecord];
    Cy_MemCopy( record.path, canonicalPath.pData, canonicalPath.cchLength );
    record.path[canonicalPath.cchLength] = '\0';
    record.cchPath = canonicalPath.cchLength;
    record.pDocument = KeyValue_InternalCreateLike(
        context.pDestination,
        CY_FALSE );
    if ( record.pDocument == nullptr ) {
        Fail(
            context,
            key_value_source_status_t::OUT_OF_MEMORY,
            canonicalPath );
        return CY_FALSE;
    }
    record.state = source_record_state_t::RESOLVING;
    context.result.nUniqueSources = context.nSources;
    context.result.cbAggregateSources += text.cchLength;
    return CY_TRUE;
}

void ReleaseSourceView(
    source_context_t &context,
    key_value_source_view_t &source ) noexcept
{
    context.options.pfnRelease( &source, context.options.pUserData );
    source = {};
}

CYPHER_NODISCARD bool_t ResolveDocument(
    source_context_t &context,
    usize iRecord,
    string_view_t text,
    usize nDepth ) noexcept;

CYPHER_NODISCARD bool_t ResolveDependency(
    source_context_t &context,
    source_record_t &requestingSource,
    source_directive_t &directive,
    usize nDepth ) noexcept
{
    if ( nDepth >= context.options.nMaxDependencyDepth ) {
        Fail(
            context,
            key_value_source_status_t::DEPENDENCY_DEPTH_LIMIT,
            RecordPath( requestingSource ),
            directive.referencedPath,
            directive.location,
            directive.kind );
        return CY_FALSE;
    }

    key_value_source_view_t opened{};
    const key_value_source_open_status_t openStatus = context.options.pfnOpen(
        RecordPath( requestingSource ),
        directive.referencedPath,
        &opened,
        context.options.pUserData );
    if ( openStatus != key_value_source_open_status_t::OK ) {
        context.result.openStatus = openStatus;
        Fail(
            context,
            key_value_source_status_t::OPEN_FAILED,
            RecordPath( requestingSource ),
            directive.referencedPath,
            directive.location,
            directive.kind );
        return CY_FALSE;
    }

    if ( !StringView_IsValid( opened.text ) ||
         !IsCanonicalVirtualPath(
             context,
             opened.canonicalPath,
             RecordPath( requestingSource ),
             directive.referencedPath,
             directive.location,
             directive.kind ) ) {
        if ( context.result.status == key_value_source_status_t::OK ) {
            Fail(
                context,
                key_value_source_status_t::INVALID_ARGUMENT,
                RecordPath( requestingSource ),
                directive.referencedPath,
                directive.location,
                directive.kind );
        }
        ReleaseSourceView( context, opened );
        return CY_FALSE;
    }

    if ( context.options.pfnDependency != nullptr &&
         !context.options.pfnDependency(
             RecordPath( requestingSource ),
             opened.canonicalPath,
             directive.kind,
             context.options.pUserData ) ) {
        Fail(
            context,
            key_value_source_status_t::DEPENDENCY_SINK_FAILED,
            RecordPath( requestingSource ),
            directive.referencedPath,
            directive.location,
            directive.kind );
        ReleaseSourceView( context, opened );
        return CY_FALSE;
    }

    usize iDependency = FindSourceRecord( context, opened.canonicalPath );
    if ( iDependency != CY_USIZE_MAX ) {
        source_record_t &record = context.pSources[iDependency];
        if ( record.state == source_record_state_t::RESOLVING ) {
            Fail(
                context,
                key_value_source_status_t::DEPENDENCY_CYCLE,
                RecordPath( requestingSource ),
                directive.referencedPath,
                directive.location,
                directive.kind );
            ReleaseSourceView( context, opened );
            return CY_FALSE;
        }
        directive.pResolvedDocument = record.pDocument;
        ReleaseSourceView( context, opened );
        return CY_TRUE;
    }

    if ( !AddSourceRecord(
             context,
             opened.canonicalPath,
             opened.text,
             iDependency ) ) {
        ReleaseSourceView( context, opened );
        return CY_FALSE;
    }
    const bool_t bResolved = ResolveDocument(
        context,
        iDependency,
        opened.text,
        nDepth + 1u );
    directive.pResolvedDocument = bResolved
        ? context.pSources[iDependency].pDocument
        : nullptr;
    ReleaseSourceView( context, opened );
    return bResolved;
}

CYPHER_NODISCARD bool_t BuildStrippedSource(
    source_context_t &context,
    string_view_t text,
    usize iDirectiveBegin,
    usize iDirectiveEnd,
    char *&pCopy ) noexcept
{
    pCopy = static_cast<char *>( Allocator_Allocate(
        context.pAllocator,
        text.cchLength + 1u,
        alignof( char ) ) );
    if ( pCopy == nullptr ) {
        return CY_FALSE;
    }
    if ( text.cchLength != 0u ) {
        Cy_MemCopy( pCopy, text.pData, text.cchLength );
    }
    pCopy[text.cchLength] = '\0';
    for ( usize iDirective = iDirectiveBegin;
          iDirective < iDirectiveEnd;
          ++iDirective ) {
        const source_directive_t &directive =
            context.pDirectives[iDirective];
        for ( usize iByte = directive.iEraseBegin;
              iByte < directive.iEraseEnd;
              ++iByte ) {
            if ( pCopy[iByte] != '\n' && pCopy[iByte] != '\r' ) {
                pCopy[iByte] = ' ';
            }
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t BuildIncludeSeeds(
    source_context_t &context,
    source_record_t &source,
    usize iDirectiveBegin,
    usize iDirectiveEnd,
    usize &nSeeds ) noexcept
{
    nSeeds = 0u;
    for ( usize iDirective = iDirectiveBegin;
          iDirective < iDirectiveEnd;
          ++iDirective ) {
        source_directive_t &directive = context.pDirectives[iDirective];
        if ( directive.kind != key_value_dependency_kind_t::INCLUDE ) {
            continue;
        }
        for ( usize iSeed = 0u; iSeed < nSeeds; ++iSeed ) {
            if ( !StringView_Equals(
                     context.pIncludeSeeds[iSeed].name,
                     directive.alias ) ) {
                continue;
            }
            Fail(
                context,
                key_value_source_status_t::DUPLICATE_ALIAS,
                RecordPath( source ),
                directive.referencedPath,
                directive.location,
                directive.kind );
            return CY_FALSE;
        }
        context.pIncludeSeeds[nSeeds++] = {
            directive.alias,
            KeyValue_Root( directive.pResolvedDocument )
        };
    }
    return CY_TRUE;
}

struct cloned_value_cost_t {
    usize nNodes{ 0u };
    usize cbData{ 0u };
    usize nMaxContainerDepth{ 0u };
    bool_t bHasContainer{ CY_FALSE };
};

CYPHER_NODISCARD bool_t AddCost(
    usize &total,
    usize amount ) noexcept
{
    if ( amount > CY_USIZE_MAX - total ) return CY_FALSE;
    total += amount;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t MeasureClonedValue(
    const key_value_t *pValue,
    usize nRelativeDepth,
    cloned_value_cost_t &cost ) noexcept
{
    if ( !AddCost( cost.nNodes, 1u ) ) return CY_FALSE;
    if ( pValue->pName != nullptr &&
         ( pValue->cchName == CY_USIZE_MAX ||
           !AddCost( cost.cbData, pValue->cchName + 1u ) ) ) {
        return CY_FALSE;
    }
    if ( pValue->type == key_value_type_t::STRING ) {
        if ( pValue->value.bytes.cbSize == CY_USIZE_MAX ||
             !AddCost( cost.cbData, pValue->value.bytes.cbSize + 1u ) ) {
            return CY_FALSE;
        }
    } else if ( pValue->type == key_value_type_t::BINARY &&
                !AddCost( cost.cbData, pValue->value.bytes.cbSize ) ) {
        return CY_FALSE;
    }
    if ( pValue->type == key_value_type_t::OBJECT ||
         pValue->type == key_value_type_t::ARRAY ) {
        cost.bHasContainer = CY_TRUE;
        if ( nRelativeDepth > cost.nMaxContainerDepth ) {
            cost.nMaxContainerDepth = nRelativeDepth;
        }
    }
    for ( const key_value_t *pChild = pValue->pFirstChild;
          pChild != nullptr;
          pChild = pChild->pNext ) {
        if ( nRelativeDepth == CY_USIZE_MAX ||
             !MeasureClonedValue(
                 pChild,
                 nRelativeDepth + 1u,
                 cost ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

void FailMergeLimit(
    source_context_t &context,
    source_record_t &source,
    const source_directive_t &directive,
    key_value_parse_status_t status ) noexcept
{
    context.result.parseResult.status = status;
    context.result.parseResult.errorLocation = directive.location;
    context.result.parseResult.nNodesParsed =
        KeyValue_InternalNodeCount( source.pDocument );
    context.result.parseResult.cbStringData =
        KeyValue_InternalDataSize( source.pDocument );
    Fail(
        context,
        key_value_source_status_t::PARSE_FAILED,
        RecordPath( source ),
        directive.referencedPath,
        directive.location,
        directive.kind );
}

CYPHER_NODISCARD bool_t PreflightBaseClone(
    source_context_t &context,
    source_record_t &source,
    key_value_t *pContainer,
    const key_value_t *pBaseValue,
    usize nContainerDepth,
    const source_directive_t &directive ) noexcept
{
    cloned_value_cost_t cost{};
    if ( !MeasureClonedValue( pBaseValue, 0u, cost ) ) {
        FailMergeLimit(
            context,
            source,
            directive,
            key_value_parse_status_t::NODE_LIMIT );
        return CY_FALSE;
    }

    const key_value_parse_options_t &limits = context.options.parseOptions;
    const usize nNodes = KeyValue_InternalNodeCount( source.pDocument );
    const usize cbData = KeyValue_InternalDataSize( source.pDocument );
    if ( KeyValue_ChildCount( pContainer ) >= limits.nMaxContainerValues ) {
        FailMergeLimit(
            context,
            source,
            directive,
            key_value_parse_status_t::CONTAINER_LIMIT );
        return CY_FALSE;
    }
    if ( nNodes > limits.nMaxNodes ||
         cost.nNodes > limits.nMaxNodes - nNodes ) {
        FailMergeLimit(
            context,
            source,
            directive,
            key_value_parse_status_t::NODE_LIMIT );
        return CY_FALSE;
    }
    if ( cbData > limits.cbMaxStringData ||
         cost.cbData > limits.cbMaxStringData - cbData ) {
        FailMergeLimit(
            context,
            source,
            directive,
            key_value_parse_status_t::STRING_LIMIT );
        return CY_FALSE;
    }
    if ( cost.bHasContainer ) {
        if ( nContainerDepth >= limits.nMaxDepth ) {
            FailMergeLimit(
                context,
                source,
                directive,
                key_value_parse_status_t::DEPTH_LIMIT );
            return CY_FALSE;
        }
        const usize nValueDepth = nContainerDepth + 1u;
        if ( cost.nMaxContainerDepth > limits.nMaxDepth - nValueDepth ) {
            FailMergeLimit(
                context,
                source,
                directive,
                key_value_parse_status_t::DEPTH_LIMIT );
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD i32 CompareNames(
    string_view_t left,
    string_view_t right,
    bool_t bInsensitive ) noexcept
{
    const usize cchCommon = left.cchLength < right.cchLength
        ? left.cchLength
        : right.cchLength;
    for ( usize iByte = 0u; iByte < cchCommon; ++iByte ) {
        const unsigned char leftByte = static_cast<unsigned char>(
            bInsensitive ? Char_ToLowerAscii( left.pData[iByte] )
                         : left.pData[iByte] );
        const unsigned char rightByte = static_cast<unsigned char>(
            bInsensitive ? Char_ToLowerAscii( right.pData[iByte] )
                         : right.pData[iByte] );
        if ( leftByte < rightByte ) return -1;
        if ( leftByte > rightByte ) return 1;
    }
    if ( left.cchLength < right.cchLength ) return -1;
    if ( left.cchLength > right.cchLength ) return 1;
    return 0;
}

CYPHER_NODISCARD string_view_t InternalName(
    const key_value_t *pValue ) noexcept
{
    return { pValue->pName, pValue->cchName };
}

void SiftNameIndex(
    key_value_t **ppValues,
    usize iRoot,
    usize nValues,
    bool_t bInsensitive ) noexcept
{
    for ( ;; ) {
        const usize iLeft = iRoot * 2u + 1u;
        if ( iLeft >= nValues ) return;
        usize iLargest = iLeft;
        const usize iRight = iLeft + 1u;
        if ( iRight < nValues &&
             CompareNames(
                 InternalName( ppValues[iLeft] ),
                 InternalName( ppValues[iRight] ),
                 bInsensitive ) < 0 ) {
            iLargest = iRight;
        }
        if ( CompareNames(
                 InternalName( ppValues[iRoot] ),
                 InternalName( ppValues[iLargest] ),
                 bInsensitive ) >= 0 ) {
            return;
        }
        key_value_t *pSwap = ppValues[iRoot];
        ppValues[iRoot] = ppValues[iLargest];
        ppValues[iLargest] = pSwap;
        iRoot = iLargest;
    }
}

void SortNameIndex(
    key_value_t **ppValues,
    usize nValues,
    bool_t bInsensitive ) noexcept
{
    for ( usize iRoot = nValues / 2u; iRoot != 0u; ) {
        SiftNameIndex( ppValues, --iRoot, nValues, bInsensitive );
    }
    for ( usize iEnd = nValues; iEnd > 1u; ) {
        --iEnd;
        key_value_t *pSwap = ppValues[0];
        ppValues[0] = ppValues[iEnd];
        ppValues[iEnd] = pSwap;
        SiftNameIndex( ppValues, 0u, iEnd, bInsensitive );
    }
}

CYPHER_NODISCARD key_value_t *FindInNameIndex(
    key_value_t *const *ppValues,
    usize nValues,
    string_view_t name,
    bool_t bInsensitive ) noexcept
{
    usize iBegin = 0u;
    usize iEnd = nValues;
    while ( iBegin < iEnd ) {
        const usize iMiddle = iBegin + ( iEnd - iBegin ) / 2u;
        const i32 comparison = CompareNames(
            InternalName( ppValues[iMiddle] ),
            name,
            bInsensitive );
        if ( comparison < 0 ) {
            iBegin = iMiddle + 1u;
        } else if ( comparison > 0 ) {
            iEnd = iMiddle;
        } else {
            return ppValues[iMiddle];
        }
    }
    return nullptr;
}

CYPHER_NODISCARD bool_t MergeBaseObject(
    source_context_t &context,
    source_record_t &source,
    key_value_t *pLocal,
    const key_value_t *pBase,
    const source_directive_t &directive,
    usize nDepth ) noexcept
{
    const usize nLocalChildren = KeyValue_ChildCount( pLocal );
    key_value_t **ppLocalChildren = nullptr;
    if ( nLocalChildren != 0u ) {
        ppLocalChildren = Allocator_AllocateArrayStorage<key_value_t *>(
            context.pAllocator,
            nLocalChildren );
        if ( ppLocalChildren == nullptr ) {
            Fail(
                context,
                key_value_source_status_t::OUT_OF_MEMORY,
                RecordPath( source ),
                directive.referencedPath,
                directive.location,
                directive.kind );
            return CY_FALSE;
        }
        usize iLocal = 0u;
        for ( key_value_t *pChild = pLocal->pFirstChild;
              pChild != nullptr;
              pChild = pChild->pNext ) {
            ppLocalChildren[iLocal++] = pChild;
        }
        // Authored CYKV member identity is exact-case even when the destination
        // document offers case-insensitive lookup to generic consumers.
        SortNameIndex(
            ppLocalChildren,
            nLocalChildren,
            CY_FALSE );
    }

    bool_t bMerged = CY_TRUE;
    for ( const key_value_t *pBaseChild = pBase->pFirstChild;
          pBaseChild != nullptr;
          pBaseChild = pBaseChild->pNext ) {
        const string_view_t name = KeyValue_Name( pBaseChild );
        key_value_t *pLocalChild = FindInNameIndex(
            ppLocalChildren,
            nLocalChildren,
            name,
            CY_FALSE );
        if ( pLocalChild == nullptr ) {
            if ( !PreflightBaseClone(
                     context,
                     source,
                     pLocal,
                     pBaseChild,
                     nDepth,
                     directive ) ||
                 KeyValue_CloneInto(
                     source.pDocument,
                     pLocal,
                     pBaseChild ) == nullptr ) {
                if ( context.result.status == key_value_source_status_t::OK ) {
                    Fail(
                        context,
                        key_value_source_status_t::OUT_OF_MEMORY,
                        RecordPath( source ),
                        directive.referencedPath,
                        directive.location,
                        directive.kind );
                }
                bMerged = CY_FALSE;
                break;
            }
            continue;
        }

        const key_value_type_t localType = KeyValue_Type( pLocalChild );
        const key_value_type_t baseType = KeyValue_Type( pBaseChild );
        if ( localType != baseType ) {
            Fail(
                context,
                key_value_source_status_t::MERGE_CONFLICT,
                RecordPath( source ),
                directive.referencedPath,
                directive.location,
                directive.kind );
            bMerged = CY_FALSE;
            break;
        }
        if ( localType == key_value_type_t::OBJECT &&
             !MergeBaseObject(
                 context,
                 source,
                 pLocalChild,
                 pBaseChild,
                 directive,
                 nDepth + 1u ) ) {
            bMerged = CY_FALSE;
            break;
        }
        // Matching scalar and array values are complete local overrides.
    }
    if ( ppLocalChildren != nullptr ) {
        Allocator_FreeArrayStorage(
            context.pAllocator,
            ppLocalChildren,
            nLocalChildren );
    }
    return bMerged;
}

CYPHER_NODISCARD bool_t ApplyBases(
    source_context_t &context,
    source_record_t &source,
    usize iDirectiveBegin,
    usize iDirectiveEnd ) noexcept
{
    const key_value_document_header_t localHeader =
        KeyValue_DocumentHeader( source.pDocument );
    for ( usize iDirective = iDirectiveBegin;
          iDirective < iDirectiveEnd;
          ++iDirective ) {
        const source_directive_t &directive = context.pDirectives[iDirective];
        if ( directive.kind != key_value_dependency_kind_t::BASE ) {
            continue;
        }
        const key_value_document_header_t baseHeader =
            KeyValue_DocumentHeader( directive.pResolvedDocument );
        if ( localHeader.nSchemaVersion != baseHeader.nSchemaVersion ||
             !StringView_Equals( localHeader.schemaId, baseHeader.schemaId ) ) {
            Fail(
                context,
                key_value_source_status_t::SCHEMA_MISMATCH,
                RecordPath( source ),
                directive.referencedPath,
                directive.location,
                directive.kind );
            return CY_FALSE;
        }
        key_value_t *pLocalRoot = KeyValue_Root( source.pDocument );
        const key_value_t *pBaseRoot = KeyValue_Root(
            directive.pResolvedDocument );
        if ( KeyValue_Type( pLocalRoot ) != key_value_type_t::OBJECT ||
             KeyValue_Type( pBaseRoot ) != key_value_type_t::OBJECT ) {
            Fail(
                context,
                key_value_source_status_t::MERGE_CONFLICT,
                RecordPath( source ),
                directive.referencedPath,
                directive.location,
                directive.kind );
            return CY_FALSE;
        }
        if ( !MergeBaseObject(
                 context,
                 source,
                 pLocalRoot,
                 pBaseRoot,
                 directive,
                 0u ) ) {
            return CY_FALSE;
        }
    }
    context.result.parseResult.nNodesParsed =
        KeyValue_InternalNodeCount( source.pDocument );
    context.result.parseResult.cbStringData =
        KeyValue_InternalDataSize( source.pDocument );
    return CY_TRUE;
}

bool_t ResolveDocument(
    source_context_t &context,
    usize iRecord,
    string_view_t text,
    usize nDepth ) noexcept
{
    source_record_t &source = context.pSources[iRecord];
    if ( !ValidateSourceBeforeDependencyIo(
             context,
             RecordPath( source ),
             text ) ) {
        return CY_FALSE;
    }
    usize iDirectiveBegin = context.nDirectives;
    usize iDirectiveEnd = iDirectiveBegin;
    if ( !ScanDependencyDirectives(
             context,
             RecordPath( source ),
             text,
             iDirectiveBegin,
             iDirectiveEnd ) ) {
        return CY_FALSE;
    }

    // Resolve every direct edge before parsing so includes can seed typed names.
    for ( usize iDirective = iDirectiveBegin;
          iDirective < iDirectiveEnd;
          ++iDirective ) {
        if ( !ResolveDependency(
                 context,
                 source,
                 context.pDirectives[iDirective],
                 nDepth ) ) {
            return CY_FALSE;
        }
    }

    usize nIncludeSeeds = 0u;
    if ( !BuildIncludeSeeds(
             context,
             source,
             iDirectiveBegin,
             iDirectiveEnd,
             nIncludeSeeds ) ) {
        return CY_FALSE;
    }

    char *pStripped = nullptr;
    if ( !BuildStrippedSource(
             context,
             text,
             iDirectiveBegin,
             iDirectiveEnd,
             pStripped ) ) {
        Fail(
            context,
            key_value_source_status_t::OUT_OF_MEMORY,
            RecordPath( source ) );
        return CY_FALSE;
    }

    const key_value_parse_result_t parsed =
        KeyValue_InternalParseTextWithDefinitions(
            { pStripped, text.cchLength },
            context.options.parseOptions,
            context.pIncludeSeeds,
            nIncludeSeeds,
            source.pDocument,
            CY_FALSE );
    Allocator_Free(
        context.pAllocator,
        pStripped,
        text.cchLength + 1u,
        alignof( char ) );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        context.result.parseResult = parsed;
        Fail(
            context,
            key_value_source_status_t::PARSE_FAILED,
            RecordPath( source ),
            {},
            parsed.errorLocation );
        return CY_FALSE;
    }
    context.result.parseResult = parsed;

    if ( !ApplyBases(
             context,
             source,
             iDirectiveBegin,
             iDirectiveEnd ) ) {
        return CY_FALSE;
    }
    source.state = source_record_state_t::RESOLVED;
    return CY_TRUE;
}

void ShutdownContext( source_context_t &context ) noexcept
{
    if ( context.pSources != nullptr ) {
        for ( usize iSource = 0u; iSource < context.nSources; ++iSource ) {
            if ( context.pSources[iSource].pDocument != nullptr ) {
                KeyValue_DestroyDocument(
                    context.pSources[iSource].pDocument );
            }
        }
        Allocator_FreeArrayStorage(
            context.pAllocator,
            context.pSources,
            context.options.nMaxSources );
    }
    if ( context.pDirectives != nullptr ) {
        Allocator_FreeArrayStorage(
            context.pAllocator,
            context.pDirectives,
            context.options.nMaxEdges );
    }
    if ( context.pIncludeSeeds != nullptr ) {
        Allocator_FreeArrayStorage(
            context.pAllocator,
            context.pIncludeSeeds,
            context.options.nMaxEdges );
    }
    context.pSources = nullptr;
    context.pDirectives = nullptr;
    context.pIncludeSeeds = nullptr;
}

} // namespace

key_value_source_result_t KeyValue_ParseSource(
    string_view_t rootCanonicalPath,
    string_view_t rootText,
    const key_value_source_options_t &options,
    key_value_document_t *pDocument ) noexcept
{
    source_context_t context{};
    context.options = options;
    context.pAllocator = options.pScratchAllocator != nullptr
        ? options.pScratchAllocator
        : Allocator_GetSystem();
    context.pDestination = pDocument;

    if ( !StringView_IsValid( rootCanonicalPath ) ||
         !StringView_IsValid( rootText ) ||
         !KeyValue_InternalDocumentIsValid( pDocument ) ||
         !Allocator_IsValid( context.pAllocator ) ||
         !ParseOptionsAreValid( options.parseOptions ) ||
         options.pfnOpen == nullptr || options.pfnRelease == nullptr ||
         options.nMaxDependencyDepth == 0u ||
         options.nMaxDependencyDepth >
            CY_KEY_VALUE_SOURCE_MAX_DEPENDENCY_DEPTH ||
         options.nMaxSources == 0u ||
         options.nMaxSources > CY_KEY_VALUE_SOURCE_MAX_SOURCES ||
         options.nMaxEdges == 0u ||
         options.nMaxEdges > CY_KEY_VALUE_SOURCE_MAX_EDGES ||
         options.cbMaxAggregateSources == 0u ||
         options.cchMaxPath == 0u ||
         options.cchMaxPath >= CY_KEY_VALUE_SOURCE_DIAGNOSTIC_PATH_CAPACITY ) {
        context.result.status = key_value_source_status_t::INVALID_ARGUMENT;
        return context.result;
    }
    if ( !IsCanonicalVirtualPath(
             context,
             rootCanonicalPath,
             rootCanonicalPath,
             {},
             {},
             key_value_dependency_kind_t::INCLUDE ) ) {
        return context.result;
    }

    context.pSources = Allocator_AllocateArrayStorage<source_record_t>(
        context.pAllocator,
        options.nMaxSources );
    context.pDirectives = Allocator_AllocateArrayStorage<source_directive_t>(
        context.pAllocator,
        options.nMaxEdges );
    context.pIncludeSeeds =
        Allocator_AllocateArrayStorage<key_value_definition_seed_t>(
            context.pAllocator,
            options.nMaxEdges );
    if ( context.pSources == nullptr || context.pDirectives == nullptr ||
         context.pIncludeSeeds == nullptr ) {
        context.result.status = key_value_source_status_t::OUT_OF_MEMORY;
        ShutdownContext( context );
        return context.result;
    }
    Cy_MemSet(
        context.pSources,
        0u,
        sizeof( source_record_t ) * options.nMaxSources );
    Cy_MemSet(
        context.pDirectives,
        0u,
        sizeof( source_directive_t ) * options.nMaxEdges );
    Cy_MemSet(
        context.pIncludeSeeds,
        0u,
        sizeof( key_value_definition_seed_t ) * options.nMaxEdges );

    usize iRoot = 0u;
    if ( AddSourceRecord(
             context,
             rootCanonicalPath,
             rootText,
             iRoot ) &&
         ResolveDocument( context, iRoot, rootText, 0u ) ) {
        KeyValue_InternalMoveDocumentContents(
            pDocument,
            context.pSources[iRoot].pDocument );
    }
    ShutdownContext( context );
    return context.result;
}

const char *KeyValue_SourceStatusName(
    key_value_source_status_t status ) noexcept
{
    switch ( status ) {
        case key_value_source_status_t::OK: return "OK";
        case key_value_source_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case key_value_source_status_t::PATH_LIMIT: return "PATH_LIMIT";
        case key_value_source_status_t::INVALID_PATH: return "INVALID_PATH";
        case key_value_source_status_t::INVALID_DIRECTIVE: return "INVALID_DIRECTIVE";
        case key_value_source_status_t::OPEN_FAILED: return "OPEN_FAILED";
        case key_value_source_status_t::DEPENDENCY_CYCLE: return "DEPENDENCY_CYCLE";
        case key_value_source_status_t::DEPENDENCY_DEPTH_LIMIT:
            return "DEPENDENCY_DEPTH_LIMIT";
        case key_value_source_status_t::SOURCE_LIMIT: return "SOURCE_LIMIT";
        case key_value_source_status_t::EDGE_LIMIT: return "EDGE_LIMIT";
        case key_value_source_status_t::SOURCE_BYTE_LIMIT:
            return "SOURCE_BYTE_LIMIT";
        case key_value_source_status_t::DUPLICATE_ALIAS: return "DUPLICATE_ALIAS";
        case key_value_source_status_t::SCHEMA_MISMATCH: return "SCHEMA_MISMATCH";
        case key_value_source_status_t::MERGE_CONFLICT: return "MERGE_CONFLICT";
        case key_value_source_status_t::DEPENDENCY_SINK_FAILED:
            return "DEPENDENCY_SINK_FAILED";
        case key_value_source_status_t::PARSE_FAILED: return "PARSE_FAILED";
        case key_value_source_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    }
    return "UNKNOWN_KEY_VALUE_SOURCE_STATUS";
}

} // namespace cypher::common
