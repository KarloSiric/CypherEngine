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
    // The CYKV 2 header, read before any dependency opens, so parts and
    // bases are checked against it before they are merged.
    char schemaId[CY_KEY_VALUE_SOURCE_DIAGNOSTIC_PATH_CAPACITY]{};
    usize cchSchemaId{ 0u };
    u32 nSchemaVersion{ 0u };
    bool_t bHasHeader{ CY_FALSE };
};

struct source_directive_t {
    key_value_dependency_kind_t kind{ key_value_dependency_kind_t::INCLUDE };
    string_view_t referencedPath{};
    string_view_t alias{};
    text_location_t location{};
    usize iEraseBegin{ 0u };
    usize iEraseEnd{ 0u };
    key_value_document_t *pResolvedDocument{ nullptr };
    // Folder includes: one directive per matching file, all sharing the
    // index of the first as iGroup. With a namespace, the first holds the
    // object keyed by file stem that the namespace binds.
    usize iGroup{ CY_USIZE_MAX };
    string_view_t stem{};
    char *pOwnedPath{ nullptr };
    usize cbOwnedPath{ 0u };
    key_value_document_t *pGroupDocument{ nullptr };
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
    source_record_t &source,
    string_view_t text,
    usize iAfterHeader ) noexcept
{
    const string_view_t sourcePath{ source.path, source.cchPath };
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
    if ( parsed.status == key_value_parse_status_t::OK ) {
        const key_value_document_header_t header = KeyValue_DocumentHeader( pProbeDocument );
        source.bHasHeader = header.schemaId.cchLength < sizeof( source.schemaId );
        if ( source.bHasHeader ) {
            Cy_MemCopy( source.schemaId, header.schemaId.pData, header.schemaId.cchLength );
            source.cchSchemaId = header.schemaId.cchLength;
            source.nSchemaVersion = header.nSchemaVersion;
        }
    }
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
    source_record_t &source,
    string_view_t text,
    usize &iDirectiveBegin,
    usize &iDirectiveEnd ) noexcept
{
    const string_view_t sourcePath{ source.path, source.cchPath };
    iDirectiveBegin = context.nDirectives;
    iDirectiveEnd = iDirectiveBegin;
    usize iByte = 0u;
    if ( !HeaderSelectsCykv2( text, iByte ) ) {
        return CY_TRUE;
    }
    if ( !ValidateCykv2HeaderBeforeDependencyIo(
             context,
             source,
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
            // Without `as name` the include merges a part into this document.
            usize iTail = iByte;
            SkipHorizontal( text, iLineEnd, iTail );
            if ( iTail == iLineEnd || text.pData[iTail] == '\r' || text.pData[iTail] == '/' ) {
                kind = key_value_dependency_kind_t::MERGE_INCLUDE;
            }
        }
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
        directive = source_directive_t{}; // The array is zero-filled; defaults matter (iGroup).
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

// The object a namespaced folder include binds: one member per file, named
// by the file's stem, holding that file's resolved root.
CYPHER_NODISCARD bool_t BuildFolderObject(
    source_context_t &context,
    source_record_t &source,
    usize iFirst,
    usize iDirectiveEnd ) noexcept
{
    source_directive_t &first = context.pDirectives[iFirst];
    key_value_document_desc_t desc{};
    desc.pAllocator = context.pAllocator;
    first.pGroupDocument = KeyValue_CreateDocument( desc );
    if ( first.pGroupDocument == nullptr || !KeyValue_SetRootType( first.pGroupDocument, key_value_type_t::OBJECT ) ) {
        Fail( context, key_value_source_status_t::OUT_OF_MEMORY, RecordPath( source ), first.referencedPath, first.location, first.kind );
        return CY_FALSE;
    }
    key_value_t *pRoot = KeyValue_Root( first.pGroupDocument );
    for ( usize i = iFirst; i < iDirectiveEnd; ++i ) {
        const source_directive_t &directive = context.pDirectives[i];
        if ( directive.iGroup != iFirst ) {
            continue;
        }
        if ( KeyValue_Find( pRoot, directive.stem ) != nullptr ) {
            Fail( context, key_value_source_status_t::DUPLICATE_ALIAS, RecordPath( source ), directive.referencedPath,
                  directive.location, directive.kind );
            return CY_FALSE;
        }
        key_value_t *pMember = KeyValue_ObjectInsert( first.pGroupDocument, pRoot, directive.stem, key_value_type_t::NULL_VALUE );
        if ( pMember == nullptr ||
             !KeyValue_InternalCopyValue( first.pGroupDocument, pMember, KeyValue_Root( directive.pResolvedDocument ) ) ) {
            Fail( context, key_value_source_status_t::OUT_OF_MEMORY, RecordPath( source ), directive.referencedPath,
                  directive.location, directive.kind );
            return CY_FALSE;
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
        if ( directive.kind != key_value_dependency_kind_t::INCLUDE ||
             ( directive.iGroup != CY_USIZE_MAX && directive.iGroup != iDirective ) ) {
            continue; // A folder include binds its namespace once, at its first file.
        }
        if ( directive.iGroup != CY_USIZE_MAX && !BuildFolderObject( context, source, iDirective, iDirectiveEnd ) ) {
            return CY_FALSE;
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
            KeyValue_Root( directive.pGroupDocument != nullptr ? directive.pGroupDocument : directive.pResolvedDocument )
        };
    }
    return CY_TRUE;
}

// ---------------------------------------------------------------------------
// Folder includes
// ---------------------------------------------------------------------------

// `*` matches any run of characters, including none; everything else
// matches itself, case-sensitively.
CYPHER_NODISCARD bool_t GlobMatch( string_view_t pattern, string_view_t name ) noexcept
{
    usize iPattern = 0u;
    usize iName = 0u;
    usize iStar = CY_USIZE_MAX;
    usize iMark = 0u;
    while ( iName < name.cchLength ) {
        if ( iPattern < pattern.cchLength && pattern.pData[iPattern] == '*' ) {
            iStar = iPattern++;
            iMark = iName;
            continue;
        }
        if ( iPattern < pattern.cchLength && pattern.pData[iPattern] == name.pData[iName] ) {
            ++iPattern;
            ++iName;
            continue;
        }
        if ( iStar == CY_USIZE_MAX ) {
            return CY_FALSE;
        }
        iPattern = iStar + 1u;
        iName = ++iMark;
    }
    while ( iPattern < pattern.cchLength && pattern.pData[iPattern] == '*' ) {
        ++iPattern;
    }
    return iPattern == pattern.cchLength;
}

struct listed_name_t {
    usize iOffset{ 0u };
    usize cchName{ 0u };
};

// The names a list callback emitted that the pattern matches.
struct name_list_t {
    const allocator_t *pAllocator{ nullptr };
    string_view_t pattern{};
    usize nMaxNames{ 0u };
    char *pText{ nullptr };
    usize cbText{ 0u };
    usize cbCapacity{ 0u };
    listed_name_t *pNames{ nullptr };
    usize nNames{ 0u };
    bool_t bTooMany{ CY_FALSE };
    bool_t bOutOfMemory{ CY_FALSE };

    CYPHER_NODISCARD string_view_t Name( usize i ) const noexcept { return { pText + pNames[i].iOffset, pNames[i].cchName }; }
};

void ReleaseNameList( name_list_t &list ) noexcept
{
    if ( list.pText != nullptr ) { Allocator_Free( list.pAllocator, list.pText, list.cbCapacity, alignof( char ) ); }
    if ( list.pNames != nullptr ) { Allocator_FreeArrayStorage( list.pAllocator, list.pNames, list.nMaxNames ); }
    list.pText = nullptr;
    list.pNames = nullptr;
}

bool_t EmitListedName( void *pEmitContext, string_view_t name ) noexcept
{
    auto &list = *static_cast<name_list_t *>( pEmitContext );
    if ( !StringView_IsValid( name ) || name.cchLength == 0u ) {
        return CY_TRUE;
    }
    for ( usize i = 0u; i < name.cchLength; ++i ) {
        const unsigned char ch = static_cast<unsigned char>( name.pData[i] );
        if ( ch < 0x20u || ch == 0x7fu || ch == '/' || ch == '\\' ) {
            return CY_TRUE; // A file name, not a path; anything else is not ours.
        }
    }
    // Hidden files stay out unless the pattern asks for them.
    if ( name.pData[0] == '.' && ( list.pattern.cchLength == 0u || list.pattern.pData[0] != '.' ) ) {
        return CY_TRUE;
    }
    if ( !GlobMatch( list.pattern, name ) ) {
        return CY_TRUE;
    }
    if ( list.nNames == list.nMaxNames ) {
        list.bTooMany = CY_TRUE;
        return CY_FALSE;
    }
    if ( list.pNames == nullptr ) {
        list.pNames = Allocator_AllocateArrayStorage<listed_name_t>( list.pAllocator, list.nMaxNames );
        if ( list.pNames == nullptr ) {
            list.bOutOfMemory = CY_TRUE;
            return CY_FALSE;
        }
    }
    if ( list.cbText + name.cchLength > list.cbCapacity ) {
        usize cbCapacity = list.cbCapacity == 0u ? 1024u : list.cbCapacity * 2u;
        while ( cbCapacity < list.cbText + name.cchLength ) { cbCapacity *= 2u; }
        char *pGrown = static_cast<char *>( Allocator_Allocate( list.pAllocator, cbCapacity, alignof( char ) ) );
        if ( pGrown == nullptr ) {
            list.bOutOfMemory = CY_TRUE;
            return CY_FALSE;
        }
        if ( list.cbText != 0u ) { Cy_MemCopy( pGrown, list.pText, list.cbText ); }
        if ( list.pText != nullptr ) { Allocator_Free( list.pAllocator, list.pText, list.cbCapacity, alignof( char ) ); }
        list.pText = pGrown;
        list.cbCapacity = cbCapacity;
    }
    Cy_MemCopy( list.pText + list.cbText, name.pData, name.cchLength );
    list.pNames[list.nNames++] = { list.cbText, name.cchLength };
    list.cbText += name.cchLength;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t NameLess( const name_list_t &list, const listed_name_t &a, const listed_name_t &b ) noexcept
{
    const usize cchCommon = a.cchName < b.cchName ? a.cchName : b.cchName;
    for ( usize i = 0u; i < cchCommon; ++i ) {
        const unsigned char ca = static_cast<unsigned char>( list.pText[a.iOffset + i] );
        const unsigned char cb = static_cast<unsigned char>( list.pText[b.iOffset + i] );
        if ( ca != cb ) { return ca < cb; }
    }
    return a.cchName < b.cchName;
}

// Byte order: the same on every machine whatever order the folder listed.
void SortNames( name_list_t &list ) noexcept
{
    for ( usize i = 1u; i < list.nNames; ++i ) {
        const listed_name_t entry = list.pNames[i];
        usize j = i;
        while ( j > 0u && NameLess( list, entry, list.pNames[j - 1u] ) ) {
            list.pNames[j] = list.pNames[j - 1u];
            --j;
        }
        list.pNames[j] = entry;
    }
}

// Replaces each folder include ("dir/*.cydf") with one include per matching
// file, in sorted order. Only the last path segment may hold `*`.
CYPHER_NODISCARD bool_t ExpandFolderIncludes(
    source_context_t &context,
    source_record_t &source,
    usize iDirectiveBegin,
    usize &iDirectiveEnd ) noexcept
{
    const string_view_t sourcePath = RecordPath( source );
    for ( usize i = iDirectiveBegin; i < iDirectiveEnd; ++i ) {
        const source_directive_t directive = context.pDirectives[i];
        const string_view_t path = directive.referencedPath;
        usize iSlash = CY_USIZE_MAX;
        usize iFirstStar = CY_USIZE_MAX;
        for ( usize k = 0u; k < path.cchLength; ++k ) {
            if ( path.pData[k] == '/' ) { iSlash = k; }
            if ( path.pData[k] == '*' && iFirstStar == CY_USIZE_MAX ) { iFirstStar = k; }
        }
        if ( iFirstStar == CY_USIZE_MAX ) {
            continue;
        }
        if ( directive.kind == key_value_dependency_kind_t::BASE || ( iSlash != CY_USIZE_MAX && iFirstStar < iSlash ) ) {
            Fail( context, key_value_source_status_t::INVALID_PATH, sourcePath, path, directive.location, directive.kind );
            return CY_FALSE;
        }
        if ( context.options.pfnList == nullptr ) {
            Fail( context, key_value_source_status_t::INVALID_ARGUMENT, sourcePath, path, directive.location, directive.kind );
            return CY_FALSE;
        }
        const string_view_t folder = iSlash == CY_USIZE_MAX ? string_view_t{ path.pData, 0u } : string_view_t{ path.pData, iSlash };
        name_list_t list{};
        list.pAllocator = context.pAllocator;
        list.pattern = iSlash == CY_USIZE_MAX ? path : string_view_t{ path.pData + iSlash + 1u, path.cchLength - iSlash - 1u };
        list.nMaxNames = context.options.nMaxEdges;
        const key_value_source_open_status_t listed = context.options.pfnList(
            sourcePath, folder, &EmitListedName, &list, context.options.pUserData );
        const auto fail = [&]( key_value_source_status_t status ) noexcept {
            ReleaseNameList( list );
            Fail( context, status, sourcePath, path, directive.location, directive.kind );
            return CY_FALSE;
        };
        if ( list.bOutOfMemory ) { return fail( key_value_source_status_t::OUT_OF_MEMORY ); }
        if ( list.bTooMany ) { return fail( key_value_source_status_t::EDGE_LIMIT ); }
        if ( listed != key_value_source_open_status_t::OK || list.nNames == 0u ) {
            // An empty match is an error: a misspelt folder or pattern must
            // not quietly include nothing.
            context.result.openStatus = listed != key_value_source_open_status_t::OK ? listed : key_value_source_open_status_t::NOT_FOUND;
            return fail( key_value_source_status_t::OPEN_FAILED );
        }
        SortNames( list );
        const usize nExtra = list.nNames - 1u;
        if ( nExtra > context.options.nMaxEdges - context.nDirectives ) { return fail( key_value_source_status_t::EDGE_LIMIT ); }
        for ( usize k = context.nDirectives; k > i + 1u; --k ) {
            context.pDirectives[k - 1u + nExtra] = context.pDirectives[k - 1u];
        }
        for ( usize n = 0u; n <= nExtra; ++n ) { context.pDirectives[i + n] = directive; }
        context.nDirectives += nExtra;
        iDirectiveEnd += nExtra;
        context.result.nDependencyEdges += nExtra;
        for ( usize n = 0u; n < list.nNames; ++n ) {
            source_directive_t &expanded = context.pDirectives[i + n];
            const string_view_t name = list.Name( n );
            const usize cchPrefix = folder.cchLength != 0u ? folder.cchLength + 1u : 0u;
            expanded.cbOwnedPath = cchPrefix + name.cchLength;
            expanded.pOwnedPath = static_cast<char *>( Allocator_Allocate( context.pAllocator, expanded.cbOwnedPath, alignof( char ) ) );
            if ( expanded.pOwnedPath == nullptr ) {
                expanded.cbOwnedPath = 0u;
                return fail( key_value_source_status_t::OUT_OF_MEMORY );
            }
            if ( cchPrefix != 0u ) {
                Cy_MemCopy( expanded.pOwnedPath, folder.pData, folder.cchLength );
                expanded.pOwnedPath[folder.cchLength] = '/';
            }
            Cy_MemCopy( expanded.pOwnedPath + cchPrefix, name.pData, name.cchLength );
            expanded.referencedPath = { expanded.pOwnedPath, expanded.cbOwnedPath };
            expanded.iGroup = i;
            usize cchStem = name.cchLength;
            while ( cchStem > 0u && name.pData[cchStem - 1u] != '.' ) { --cchStem; }
            cchStem = cchStem > 1u ? cchStem - 1u : name.cchLength; // "rifle.cydf" -> "rifle"; no dot keeps the name.
            expanded.stem = { expanded.pOwnedPath + cchPrefix, cchStem };
            if ( !IsValidReferencedPath( context, sourcePath, expanded.referencedPath, directive.location, directive.kind ) ) {
                ReleaseNameList( list );
                return CY_FALSE;
            }
        }
        ReleaseNameList( list );
        i += nExtra;
    }
    return CY_TRUE;
}

// Parts and bases become part of this document, so they must be documents
// of the same kind: the exact schema identity, checked before merging.
CYPHER_NODISCARD bool_t CheckComposedSchemas(
    source_context_t &context,
    source_record_t &source,
    usize iDirectiveBegin,
    usize iDirectiveEnd ) noexcept
{
    for ( usize i = iDirectiveBegin; i < iDirectiveEnd; ++i ) {
        const source_directive_t &directive = context.pDirectives[i];
        if ( directive.kind == key_value_dependency_kind_t::INCLUDE ) {
            continue;
        }
        const key_value_document_header_t header = KeyValue_DocumentHeader( directive.pResolvedDocument );
        if ( !source.bHasHeader || header.nSchemaVersion != source.nSchemaVersion ||
             !StringView_Equals( header.schemaId, { source.schemaId, source.cchSchemaId } ) ) {
            Fail( context, key_value_source_status_t::SCHEMA_MISMATCH, RecordPath( source ), directive.referencedPath,
                  directive.location, directive.kind );
            return CY_FALSE;
        }
    }
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
             source,
             text,
             iDirectiveBegin,
             iDirectiveEnd ) ||
         !ExpandFolderIncludes(
             context,
             source,
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
    if ( !CheckComposedSchemas(
             context,
             source,
             iDirectiveBegin,
             iDirectiveEnd ) ||
         !BuildIncludeSeeds(
             context,
             source,
             iDirectiveBegin,
             iDirectiveEnd,
             nIncludeSeeds ) ) {
        return CY_FALSE;
    }

    // Parts and bases in directive order; the parser merges and fills them
    // in, so #delete and += in this document can apply to base values.
    const usize nDirectives = iDirectiveEnd - iDirectiveBegin;
    const key_value_t **ppComposed = nDirectives != 0u
        ? Allocator_AllocateArrayStorage<const key_value_t *>( context.pAllocator, nDirectives * 2u )
        : nullptr;
    if ( nDirectives != 0u && ppComposed == nullptr ) {
        Fail( context, key_value_source_status_t::OUT_OF_MEMORY, RecordPath( source ) );
        return CY_FALSE;
    }
    key_value_composition_t composition{};
    composition.ppParts = ppComposed;
    composition.ppBases = ppComposed != nullptr ? ppComposed + nDirectives : nullptr;
    for ( usize iDirective = iDirectiveBegin; iDirective < iDirectiveEnd; ++iDirective ) {
        const source_directive_t &directive = context.pDirectives[iDirective];
        const key_value_t *pRoot = KeyValue_Root( directive.pResolvedDocument );
        if ( directive.kind == key_value_dependency_kind_t::MERGE_INCLUDE ) {
            ppComposed[composition.nParts++] = pRoot;
        } else if ( directive.kind == key_value_dependency_kind_t::BASE ) {
            ppComposed[nDirectives + composition.nBases++] = pRoot;
        }
    }

    char *pStripped = nullptr;
    const auto releaseComposed = [&]() noexcept {
        if ( ppComposed != nullptr ) { Allocator_FreeArrayStorage( context.pAllocator, ppComposed, nDirectives * 2u ); }
    };
    if ( !BuildStrippedSource(
             context,
             text,
             iDirectiveBegin,
             iDirectiveEnd,
             pStripped ) ) {
        releaseComposed();
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
            &composition,
            source.pDocument,
            CY_FALSE );
    releaseComposed();
    Allocator_Free(
        context.pAllocator,
        pStripped,
        text.cchLength + 1u,
        alignof( char ) );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        context.result.parseResult = parsed;
        // A conflict while merging a part or filling from a base names that
        // directive; anything else is the document's own parse failure.
        const bool_t bPart = parsed.status == key_value_parse_status_t::INCLUDE_CONFLICT && composition.iFailedPart != CY_USIZE_MAX;
        const bool_t bBase = parsed.status == key_value_parse_status_t::MERGE_CONFLICT && composition.iFailedBase != CY_USIZE_MAX;
        if ( bPart || bBase ) {
            const usize iWanted = bPart ? composition.iFailedPart : composition.iFailedBase;
            const key_value_dependency_kind_t kind = bPart ? key_value_dependency_kind_t::MERGE_INCLUDE : key_value_dependency_kind_t::BASE;
            usize iSeen = 0u;
            for ( usize iDirective = iDirectiveBegin; iDirective < iDirectiveEnd; ++iDirective ) {
                const source_directive_t &directive = context.pDirectives[iDirective];
                if ( directive.kind != kind || iSeen++ != iWanted ) { continue; }
                Fail( context, bPart ? key_value_source_status_t::INCLUDE_CONFLICT : key_value_source_status_t::MERGE_CONFLICT,
                      RecordPath( source ), directive.referencedPath, directive.location, kind );
                break;
            }
            return CY_FALSE;
        }
        Fail(
            context,
            key_value_source_status_t::PARSE_FAILED,
            RecordPath( source ),
            {},
            parsed.errorLocation );
        return CY_FALSE;
    }
    context.result.parseResult = parsed;
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
        for ( usize iDirective = 0u; iDirective < context.nDirectives; ++iDirective ) {
            source_directive_t &directive = context.pDirectives[iDirective];
            if ( directive.pOwnedPath != nullptr ) {
                Allocator_Free( context.pAllocator, directive.pOwnedPath, directive.cbOwnedPath, alignof( char ) );
            }
            if ( directive.pGroupDocument != nullptr ) {
                KeyValue_DestroyDocument( directive.pGroupDocument );
            }
        }
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
        case key_value_source_status_t::INCLUDE_CONFLICT: return "INCLUDE_CONFLICT";
    }
    return "UNKNOWN_KEY_VALUE_SOURCE_STATUS";
}

} // namespace cypher::common
