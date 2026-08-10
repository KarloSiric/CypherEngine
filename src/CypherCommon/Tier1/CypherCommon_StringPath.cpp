//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringPath.cpp
//  Purpose: Implements allocation-free lexical path manipulation.
//  Details: Path text is normalized without touching the filesystem. Dot-component
//           resolution uses bounded rescans so the API needs no hidden allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringPath.h"

#include "CypherCommon_Char.h"

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_PATH_NORMALIZE_FLAG_MASK =
    PATH_NORMALIZE_FLAG_COLLAPSE_SEPARATORS |
    PATH_NORMALIZE_FLAG_RESOLVE_DOT |
    PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT |
    PATH_NORMALIZE_FLAG_LOWERCASE_ASCII |
    PATH_NORMALIZE_FLAG_KEEP_TRAILING_SLASH |
    PATH_NORMALIZE_FLAG_REJECT_ABSOLUTE |
    PATH_NORMALIZE_FLAG_REJECT_ABOVE_ROOT;

struct path_root_t {
    usize iRootNameEnd{ 0u };
    usize iComponentStart{ 0u };
    usize cRootSeparators{ 0u };
    bool_t bAbsolute{ CY_FALSE };
};

struct path_component_t {
    usize iStart{ 0u };
    usize cchLength{ 0u };
    usize cPrecedingSeparators{ 0u };
};

struct path_writer_t {
    char *pDest{ nullptr };
    usize cchCapacity{ 0u };
    usize cchWritten{ 0u };
    usize cchRequired{ 0u };
    bool_t bOverflow{ CY_FALSE };
};

path_style_t ResolvePathStyle( path_style_t style ) noexcept
{
    if ( style != path_style_t::NATIVE ) {
        return style;
    }
#if CYPHER_PLATFORM_WINDOWS
    return path_style_t::WINDOWS;
#else
    return path_style_t::POSIX;
#endif
}

bool_t PathStyleIsValid( path_style_t style ) noexcept
{
    return style == path_style_t::VIRTUAL ||
           style == path_style_t::POSIX ||
           style == path_style_t::WINDOWS ||
           style == path_style_t::NATIVE;
}

bool_t PathViewIsValid( string_view_t path ) noexcept
{
    return StringView_IsValid( path );
}

bool_t IsAsciiDrivePrefix( string_view_t path ) noexcept
{
    return path.cchLength >= 2u &&
           Char_IsAlphaAscii( path.pData[0] ) &&
           path.pData[1] == ':';
}

usize FindEmbeddedNul( string_view_t text ) noexcept
{
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        if ( text.pData[iByte] == '\0' ) {
            return iByte;
        }
    }
    return CY_STRING_VIEW_NPOS;
}

path_root_t AnalyzePathRoot( string_view_t path, path_style_t requestedStyle ) noexcept
{
    path_root_t root{};
    const path_style_t style = ResolvePathStyle( requestedStyle );

    if ( style == path_style_t::WINDOWS && IsAsciiDrivePrefix( path ) ) {
        root.iRootNameEnd = 2u;
        root.iComponentStart = 2u;
        while ( root.iComponentStart < path.cchLength &&
                StringPath_IsSeparator( path.pData[root.iComponentStart] ) ) {
            ++root.iComponentStart;
        }
        root.cRootSeparators = root.iComponentStart - root.iRootNameEnd;
        root.bAbsolute = root.cRootSeparators > 0u;
        return root;
    }

    if ( style == path_style_t::WINDOWS && path.cchLength >= 2u &&
         StringPath_IsSeparator( path.pData[0] ) &&
         StringPath_IsSeparator( path.pData[1] ) ) {
        usize iServerStart = 2u;
        while ( iServerStart < path.cchLength &&
                StringPath_IsSeparator( path.pData[iServerStart] ) ) {
            ++iServerStart;
        }
        usize iServerEnd = iServerStart;
        while ( iServerEnd < path.cchLength &&
                !StringPath_IsSeparator( path.pData[iServerEnd] ) ) {
            ++iServerEnd;
        }
        usize iShareStart = iServerEnd;
        while ( iShareStart < path.cchLength &&
                StringPath_IsSeparator( path.pData[iShareStart] ) ) {
            ++iShareStart;
        }
        usize iShareEnd = iShareStart;
        while ( iShareEnd < path.cchLength &&
                !StringPath_IsSeparator( path.pData[iShareEnd] ) ) {
            ++iShareEnd;
        }

        if ( iServerEnd > iServerStart && iShareEnd > iShareStart ) {
            root.iRootNameEnd = iShareEnd;
            root.iComponentStart = iShareEnd;
            while ( root.iComponentStart < path.cchLength &&
                    StringPath_IsSeparator( path.pData[root.iComponentStart] ) ) {
                ++root.iComponentStart;
            }
            root.cRootSeparators = root.iComponentStart - root.iRootNameEnd;
            root.bAbsolute = CY_TRUE;
            return root;
        }
    }

    if ( path.cchLength > 0u && StringPath_IsSeparator( path.pData[0] ) ) {
        root.bAbsolute = CY_TRUE;
        while ( root.iComponentStart < path.cchLength &&
                StringPath_IsSeparator( path.pData[root.iComponentStart] ) ) {
            ++root.iComponentStart;
        }
        root.cRootSeparators = root.iComponentStart;
    }
    return root;
}

bool_t NextPathComponent(
    string_view_t path,
    usize &iCursor,
    path_component_t &componentOut ) noexcept
{
    const usize iSeparatorStart = iCursor;
    while ( iCursor < path.cchLength &&
            StringPath_IsSeparator( path.pData[iCursor] ) ) {
        ++iCursor;
    }
    const usize iStart = iCursor;
    while ( iCursor < path.cchLength &&
            !StringPath_IsSeparator( path.pData[iCursor] ) ) {
        ++iCursor;
    }
    if ( iStart == iCursor ) {
        return CY_FALSE;
    }
    componentOut = {
        iStart,
        iCursor - iStart,
        iStart - iSeparatorStart
    };
    return CY_TRUE;
}

bool_t ComponentEqualsLiteral(
    string_view_t path,
    const path_component_t &component,
    const char *pLiteral,
    usize cchLiteral ) noexcept
{
    if ( component.cchLength != cchLiteral ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < cchLiteral; ++iByte ) {
        if ( path.pData[component.iStart + iByte] != pLiteral[iByte] ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t IsResolvedDot(
    string_view_t path,
    const path_component_t &component,
    flags32_t flags ) noexcept
{
    return ( flags & PATH_NORMALIZE_FLAG_RESOLVE_DOT ) != 0u &&
           ComponentEqualsLiteral( path, component, ".", 1u );
}

bool_t IsResolvedDotDot(
    string_view_t path,
    const path_component_t &component,
    flags32_t flags ) noexcept
{
    return ( flags & PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT ) != 0u &&
           ComponentEqualsLiteral( path, component, "..", 2u );
}

bool_t ComponentIsCanceledByFutureParent(
    string_view_t path,
    usize iAfterComponent,
    flags32_t flags ) noexcept
{
    if ( ( flags & PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT ) == 0u ) {
        return CY_FALSE;
    }

    usize nPendingComponents = 0u;
    usize iCursor = iAfterComponent;
    path_component_t component{};
    while ( NextPathComponent( path, iCursor, component ) ) {
        if ( IsResolvedDot( path, component, flags ) ) {
            continue;
        }
        if ( IsResolvedDotDot( path, component, flags ) ) {
            if ( nPendingComponents == 0u ) {
                return CY_TRUE;
            }
            --nPendingComponents;
        } else {
            ++nPendingComponents;
        }
    }
    return CY_FALSE;
}

path_status_t ValidateParentTraversal(
    string_view_t path,
    const path_root_t &root,
    flags32_t flags,
    usize &iErrorOut ) noexcept
{
    if ( ( flags & PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT ) == 0u ) {
        return path_status_t::OK;
    }

    usize nDepth = 0u;
    usize iCursor = root.iComponentStart;
    path_component_t component{};
    while ( NextPathComponent( path, iCursor, component ) ) {
        if ( IsResolvedDot( path, component, flags ) ) {
            continue;
        }
        if ( IsResolvedDotDot( path, component, flags ) ) {
            if ( nDepth > 0u ) {
                --nDepth;
            } else if ( ( flags & PATH_NORMALIZE_FLAG_REJECT_ABOVE_ROOT ) != 0u ) {
                iErrorOut = component.iStart;
                return path_status_t::ABOVE_ROOT;
            }
        } else {
            ++nDepth;
        }
    }
    return path_status_t::OK;
}

void PathWriter_WriteByte( path_writer_t &writer, char ch ) noexcept
{
    if ( writer.cchRequired == CY_USIZE_MAX ) {
        writer.bOverflow = CY_TRUE;
        return;
    }
    ++writer.cchRequired;
    if ( writer.cchWritten < writer.cchCapacity ) {
        writer.pDest[writer.cchWritten++] = ch;
    }
}

void PathWriter_WriteText(
    path_writer_t &writer,
    string_view_t text,
    char chSeparator,
    bool_t bLowercaseAscii ) noexcept
{
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        char ch = text.pData[iByte];
        if ( StringPath_IsSeparator( ch ) ) {
            ch = chSeparator;
        } else if ( bLowercaseAscii ) {
            ch = Char_ToLowerAscii( ch );
        }
        PathWriter_WriteByte( writer, ch );
    }
}

void PathWriter_WriteRawText(
    path_writer_t &writer,
    string_view_t text ) noexcept
{
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        PathWriter_WriteByte( writer, text.pData[iByte] );
    }
}

void PathWriter_WriteSeparators(
    path_writer_t &writer,
    usize cSeparators,
    bool_t bCollapse,
    char chSeparator ) noexcept
{
    const usize cWrite = bCollapse && cSeparators > 0u ? 1u : cSeparators;
    for ( usize iSeparator = 0u; iSeparator < cWrite; ++iSeparator ) {
        PathWriter_WriteByte( writer, chSeparator );
    }
}

path_writer_t MakePathWriter( char *pDest, usize cchDest ) noexcept
{
    return {
        pDest,
        cchDest > 0u ? cchDest - 1u : 0u,
        0u,
        0u,
        CY_FALSE
    };
}

path_write_result_t FinishPathWrite(
    path_writer_t &writer,
    path_status_t failure = path_status_t::OK,
    usize iError = CY_STRING_VIEW_NPOS ) noexcept
{
    if ( writer.pDest != nullptr ) {
        writer.pDest[writer.cchWritten] = '\0';
    }
    if ( writer.bOverflow ) {
        return {
            path_status_t::INVALID_ARGUMENT,
            writer.cchWritten,
            CY_USIZE_MAX,
            iError
        };
    }
    const path_status_t status = failure != path_status_t::OK
        ? failure
        : ( writer.cchWritten == writer.cchRequired
            ? path_status_t::OK
            : path_status_t::OUTPUT_TRUNCATED );
    return { status, writer.cchWritten, writer.cchRequired, iError };
}

path_write_result_t InvalidPathWrite(
    path_status_t status,
    char *pDest,
    usize cchDest,
    usize iError = CY_STRING_VIEW_NPOS ) noexcept
{
    if ( pDest != nullptr && cchDest > 0u ) {
        pDest[0] = '\0';
    }
    return { status, 0u, 0u, iError };
}

bool_t PathComponentEquals(
    string_view_t pathA,
    const path_component_t &componentA,
    string_view_t pathB,
    const path_component_t &componentB,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    if ( componentA.cchLength != componentB.cchLength ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < componentA.cchLength; ++iByte ) {
        char chA = pathA.pData[componentA.iStart + iByte];
        char chB = pathB.pData[componentB.iStart + iByte];
        if ( bCaseInsensitiveAscii ) {
            chA = Char_ToLowerAscii( chA );
            chB = Char_ToLowerAscii( chB );
        }
        if ( chA != chB ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t RootNamesEqual(
    string_view_t path,
    const path_root_t &pathRoot,
    string_view_t base,
    const path_root_t &baseRoot,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    if ( pathRoot.iRootNameEnd != baseRoot.iRootNameEnd ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < pathRoot.iRootNameEnd; ++iByte ) {
        char chPath = path.pData[iByte];
        char chBase = base.pData[iByte];
        if ( StringPath_IsSeparator( chPath ) && StringPath_IsSeparator( chBase ) ) {
            continue;
        }
        if ( bCaseInsensitiveAscii ) {
            chPath = Char_ToLowerAscii( chPath );
            chBase = Char_ToLowerAscii( chBase );
        }
        if ( chPath != chBase ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

} // namespace

char StringPath_Separator( path_style_t style ) noexcept
{
    if ( !PathStyleIsValid( style ) ) {
        return '\0';
    }
    return ResolvePathStyle( style ) == path_style_t::WINDOWS ? '\\' : '/';
}

bool_t StringPath_IsSeparator( char ch ) noexcept
{
    return ch == '/' || ch == '\\';
}

bool_t StringPath_IsAbsolute( string_view_t path, path_style_t style ) noexcept
{
    if ( !PathViewIsValid( path ) || !PathStyleIsValid( style ) || path.cchLength == 0u ) {
        return CY_FALSE;
    }
    const path_style_t resolvedStyle = ResolvePathStyle( style );
    if ( resolvedStyle == path_style_t::VIRTUAL && IsAsciiDrivePrefix( path ) ) {
        return CY_TRUE;
    }
    return AnalyzePathRoot( path, resolvedStyle ).bAbsolute;
}

bool_t StringPath_HasRootName( string_view_t path, path_style_t style ) noexcept
{
    if ( !PathViewIsValid( path ) || !PathStyleIsValid( style ) ||
         ResolvePathStyle( style ) != path_style_t::WINDOWS ) {
        return CY_FALSE;
    }
    return AnalyzePathRoot( path, style ).iRootNameEnd > 0u;
}

bool_t StringPath_HasTrailingSeparator( string_view_t path ) noexcept
{
    return PathViewIsValid( path ) && path.cchLength > 0u &&
           StringPath_IsSeparator( path.pData[path.cchLength - 1u] );
}

string_view_t StringPath_RootName( string_view_t path, path_style_t style ) noexcept
{
    if ( !PathViewIsValid( path ) || !PathStyleIsValid( style ) ||
         ResolvePathStyle( style ) != path_style_t::WINDOWS ) {
        return {};
    }
    return { path.pData, AnalyzePathRoot( path, style ).iRootNameEnd };
}

string_view_t StringPath_Parent( string_view_t path ) noexcept
{
    if ( !PathViewIsValid( path ) || path.cchLength == 0u ) {
        return {};
    }

    usize iEnd = path.cchLength;
    if ( StringPath_IsSeparator( path.pData[iEnd - 1u] ) ) {
        while ( iEnd > 0u && StringPath_IsSeparator( path.pData[iEnd - 1u] ) ) {
            --iEnd;
        }
        if ( iEnd == 0u ) {
            return { path.pData, 1u };
        }
        if ( iEnd == 2u && IsAsciiDrivePrefix( { path.pData, iEnd } ) ) {
            return { path.pData, path.cchLength };
        }
        return { path.pData, iEnd };
    }

    usize iSeparator = iEnd;
    while ( iSeparator > 0u && !StringPath_IsSeparator( path.pData[iSeparator - 1u] ) ) {
        --iSeparator;
    }
    if ( iSeparator == 0u ) {
        return {};
    }

    usize iParentEnd = iSeparator - 1u;
    while ( iParentEnd > 0u && StringPath_IsSeparator( path.pData[iParentEnd - 1u] ) ) {
        --iParentEnd;
    }
    if ( iParentEnd == 0u ) {
        return { path.pData, 1u };
    }
    if ( iParentEnd == 2u && IsAsciiDrivePrefix( { path.pData, iParentEnd } ) ) {
        return { path.pData, iSeparator };
    }
    return { path.pData, iParentEnd };
}

string_view_t StringPath_FileName( string_view_t path ) noexcept
{
    if ( !PathViewIsValid( path ) || path.cchLength == 0u ||
         StringPath_HasTrailingSeparator( path ) ) {
        return {};
    }

    usize iStart = path.cchLength;
    while ( iStart > 0u && !StringPath_IsSeparator( path.pData[iStart - 1u] ) ) {
        --iStart;
    }
    return { path.pData + iStart, path.cchLength - iStart };
}

string_view_t StringPath_Stem( string_view_t path ) noexcept
{
    const string_view_t fileName = StringPath_FileName( path );
    if ( fileName.cchLength == 0u ) {
        return fileName;
    }
    if ( StringView_Equals( fileName, StringView_FromCString( "." ) ) ||
         StringView_Equals( fileName, StringView_FromCString( ".." ) ) ) {
        return fileName;
    }
    usize iDot = fileName.cchLength;
    while ( iDot > 0u && fileName.pData[iDot - 1u] != '.' ) {
        --iDot;
    }
    if ( iDot <= 1u ) {
        return fileName;
    }
    return { fileName.pData, iDot - 1u };
}

string_view_t StringPath_Extension( string_view_t path ) noexcept
{
    const string_view_t fileName = StringPath_FileName( path );
    if ( fileName.cchLength == 0u ) {
        return {};
    }
    if ( StringView_Equals( fileName, StringView_FromCString( "." ) ) ||
         StringView_Equals( fileName, StringView_FromCString( ".." ) ) ) {
        return {};
    }
    usize iDot = fileName.cchLength;
    while ( iDot > 0u && fileName.pData[iDot - 1u] != '.' ) {
        --iDot;
    }
    if ( iDot <= 1u ) {
        return {};
    }
    return { fileName.pData + iDot - 1u, fileName.cchLength - iDot + 1u };
}

bool_t StringPath_HasExtension(
    string_view_t path,
    string_view_t extension,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    if ( !PathViewIsValid( path ) || !PathViewIsValid( extension ) ) {
        return CY_FALSE;
    }
    const string_view_t actual = StringPath_Extension( path );
    if ( extension.cchLength == 0u ) {
        return actual.cchLength == 0u;
    }
    if ( extension.pData[0] == '.' ) {
        return bCaseInsensitiveAscii
            ? StringView_EqualsInsensitiveAscii( actual, extension )
            : StringView_Equals( actual, extension );
    }
    if ( actual.cchLength != extension.cchLength + 1u ) {
        return CY_FALSE;
    }
    const string_view_t actualWithoutDot{ actual.pData + 1u, actual.cchLength - 1u };
    return bCaseInsensitiveAscii
        ? StringView_EqualsInsensitiveAscii( actualWithoutDot, extension )
        : StringView_Equals( actualWithoutDot, extension );
}

path_write_result_t StringPath_Normalize(
    string_view_t path,
    path_style_t style,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDest = pDest != nullptr || cchDest == 0u;
    if ( !PathViewIsValid( path ) || !PathStyleIsValid( style ) || !bValidDest ||
         ( flags & ~CY_PATH_NORMALIZE_FLAG_MASK ) != 0u ) {
        return InvalidPathWrite( path_status_t::INVALID_ARGUMENT, pDest, cchDest );
    }

    const usize iNul = FindEmbeddedNul( path );
    if ( iNul != CY_STRING_VIEW_NPOS ) {
        return InvalidPathWrite( path_status_t::INVALID_PATH, pDest, cchDest, iNul );
    }

    const path_style_t resolvedStyle = ResolvePathStyle( style );
    if ( resolvedStyle == path_style_t::VIRTUAL && IsAsciiDrivePrefix( path ) ) {
        const path_status_t status = ( flags & PATH_NORMALIZE_FLAG_REJECT_ABSOLUTE ) != 0u
            ? path_status_t::ABSOLUTE_PATH_REJECTED
            : path_status_t::INVALID_PATH;
        return InvalidPathWrite( status, pDest, cchDest, 0u );
    }

    const path_root_t root = AnalyzePathRoot( path, resolvedStyle );
    if ( root.bAbsolute && ( flags & PATH_NORMALIZE_FLAG_REJECT_ABSOLUTE ) != 0u ) {
        return InvalidPathWrite(
            path_status_t::ABSOLUTE_PATH_REJECTED,
            pDest,
            cchDest,
            0u );
    }

    usize iTraversalError = CY_STRING_VIEW_NPOS;
    const path_status_t traversalStatus = ValidateParentTraversal(
        path,
        root,
        flags,
        iTraversalError );
    if ( traversalStatus != path_status_t::OK ) {
        return InvalidPathWrite( traversalStatus, pDest, cchDest, iTraversalError );
    }

    path_writer_t writer = MakePathWriter( pDest, cchDest );
    const char chSeparator = StringPath_Separator( resolvedStyle );
    const bool_t bCollapse =
        ( flags & PATH_NORMALIZE_FLAG_COLLAPSE_SEPARATORS ) != 0u;
    const bool_t bLowercase =
        ( flags & PATH_NORMALIZE_FLAG_LOWERCASE_ASCII ) != 0u;

    if ( root.iRootNameEnd > 0u ) {
        PathWriter_WriteText(
            writer,
            { path.pData, root.iRootNameEnd },
            chSeparator,
            bLowercase );
    }
    if ( root.bAbsolute && root.cRootSeparators > 0u ) {
        PathWriter_WriteSeparators(
            writer,
            root.cRootSeparators,
            bCollapse,
            chSeparator );
    }

    usize nDepth = 0u;
    usize iCursor = root.iComponentStart;
    path_component_t component{};
    bool_t bEmittedComponent = CY_FALSE;
    while ( NextPathComponent( path, iCursor, component ) ) {
        if ( IsResolvedDot( path, component, flags ) ) {
            continue;
        }

        bool_t bEmit = CY_TRUE;
        if ( IsResolvedDotDot( path, component, flags ) ) {
            if ( nDepth > 0u ) {
                --nDepth;
                bEmit = CY_FALSE;
            } else if ( root.bAbsolute ) {
                bEmit = CY_FALSE;
            }
        } else {
            ++nDepth;
            bEmit = !ComponentIsCanceledByFutureParent(
                path,
                iCursor,
                flags );
        }

        if ( !bEmit ) {
            continue;
        }
        if ( bEmittedComponent ) {
            PathWriter_WriteSeparators(
                writer,
                component.cPrecedingSeparators > 0u
                    ? component.cPrecedingSeparators
                    : 1u,
                bCollapse,
                chSeparator );
        }
        PathWriter_WriteText(
            writer,
            { path.pData + component.iStart, component.cchLength },
            chSeparator,
            bLowercase );
        bEmittedComponent = CY_TRUE;
    }

    if ( bEmittedComponent && StringPath_HasTrailingSeparator( path ) &&
         ( flags & PATH_NORMALIZE_FLAG_KEEP_TRAILING_SLASH ) != 0u ) {
        usize cTrailing = 0u;
        usize iTrailing = path.cchLength;
        while ( iTrailing > root.iComponentStart &&
                StringPath_IsSeparator( path.pData[iTrailing - 1u] ) ) {
            --iTrailing;
            ++cTrailing;
        }
        PathWriter_WriteSeparators( writer, cTrailing, bCollapse, chSeparator );
    }

    return FinishPathWrite( writer );
}

path_write_result_t StringPath_Join(
    string_view_t left,
    string_view_t right,
    path_style_t style,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDest = pDest != nullptr || cchDest == 0u;
    if ( !PathViewIsValid( left ) || !PathViewIsValid( right ) ||
         !PathStyleIsValid( style ) || !bValidDest ) {
        return InvalidPathWrite( path_status_t::INVALID_ARGUMENT, pDest, cchDest );
    }
    const usize iLeftNul = FindEmbeddedNul( left );
    const usize iRightNul = FindEmbeddedNul( right );
    if ( iLeftNul != CY_STRING_VIEW_NPOS || iRightNul != CY_STRING_VIEW_NPOS ) {
        return InvalidPathWrite(
            path_status_t::INVALID_PATH,
            pDest,
            cchDest,
            iLeftNul != CY_STRING_VIEW_NPOS ? iLeftNul : iRightNul );
    }
    if ( StringPath_IsAbsolute( right, style ) ||
         StringPath_HasRootName( right, style ) ) {
        return InvalidPathWrite( path_status_t::INVALID_PATH, pDest, cchDest, 0u );
    }

    path_writer_t writer = MakePathWriter( pDest, cchDest );
    const char chSeparator = StringPath_Separator( style );
    PathWriter_WriteText( writer, left, chSeparator, CY_FALSE );
    if ( left.cchLength > 0u && right.cchLength > 0u &&
         !StringPath_IsSeparator( left.pData[left.cchLength - 1u] ) ) {
        PathWriter_WriteByte( writer, chSeparator );
    }
    PathWriter_WriteText( writer, right, chSeparator, CY_FALSE );
    return FinishPathWrite( writer );
}

path_write_result_t StringPath_ReplaceExtension(
    string_view_t path,
    string_view_t extension,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDest = pDest != nullptr || cchDest == 0u;
    if ( !PathViewIsValid( path ) || !PathViewIsValid( extension ) || !bValidDest ) {
        return InvalidPathWrite( path_status_t::INVALID_ARGUMENT, pDest, cchDest );
    }
    const usize iPathNul = FindEmbeddedNul( path );
    const usize iExtensionNul = FindEmbeddedNul( extension );
    if ( iPathNul != CY_STRING_VIEW_NPOS || iExtensionNul != CY_STRING_VIEW_NPOS ) {
        return InvalidPathWrite(
            path_status_t::INVALID_PATH,
            pDest,
            cchDest,
            iPathNul != CY_STRING_VIEW_NPOS ? iPathNul : iExtensionNul );
    }
    for ( usize iByte = 0u; iByte < extension.cchLength; ++iByte ) {
        if ( StringPath_IsSeparator( extension.pData[iByte] ) ) {
            return InvalidPathWrite( path_status_t::INVALID_PATH, pDest, cchDest, iByte );
        }
    }

    const string_view_t fileName = StringPath_FileName( path );
    if ( fileName.cchLength == 0u ) {
        return InvalidPathWrite( path_status_t::INVALID_PATH, pDest, cchDest );
    }
    const string_view_t currentExtension = StringPath_Extension( path );
    const usize cchBase = currentExtension.cchLength > 0u
        ? path.cchLength - currentExtension.cchLength
        : path.cchLength;

    path_writer_t writer = MakePathWriter( pDest, cchDest );
    PathWriter_WriteRawText( writer, { path.pData, cchBase } );
    if ( extension.cchLength > 0u ) {
        if ( extension.pData[0] != '.' ) {
            PathWriter_WriteByte( writer, '.' );
        }
        PathWriter_WriteRawText( writer, extension );
    }
    return FinishPathWrite( writer );
}

path_write_result_t StringPath_RemoveExtension(
    string_view_t path,
    char *pDest,
    usize cchDest ) noexcept
{
    return StringPath_ReplaceExtension( path, {}, pDest, cchDest );
}

path_write_result_t StringPath_MakeRelative(
    string_view_t path,
    string_view_t base,
    path_style_t style,
    bool_t bCaseInsensitiveAscii,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDest = pDest != nullptr || cchDest == 0u;
    if ( !PathViewIsValid( path ) || !PathViewIsValid( base ) ||
         !PathStyleIsValid( style ) || !bValidDest ) {
        return InvalidPathWrite( path_status_t::INVALID_ARGUMENT, pDest, cchDest );
    }
    const usize iPathNul = FindEmbeddedNul( path );
    const usize iBaseNul = FindEmbeddedNul( base );
    if ( iPathNul != CY_STRING_VIEW_NPOS || iBaseNul != CY_STRING_VIEW_NPOS ) {
        return InvalidPathWrite(
            path_status_t::INVALID_PATH,
            pDest,
            cchDest,
            iPathNul != CY_STRING_VIEW_NPOS ? iPathNul : iBaseNul );
    }

    const path_root_t pathRoot = AnalyzePathRoot( path, style );
    const path_root_t baseRoot = AnalyzePathRoot( base, style );
    if ( pathRoot.bAbsolute != baseRoot.bAbsolute ||
         !RootNamesEqual(
             path,
             pathRoot,
             base,
             baseRoot,
             bCaseInsensitiveAscii ) ) {
        return InvalidPathWrite( path_status_t::INVALID_PATH, pDest, cchDest );
    }

    usize iPathCursor = pathRoot.iComponentStart;
    usize iBaseCursor = baseRoot.iComponentStart;
    usize iPathRemaining = iPathCursor;
    usize iBaseRemaining = iBaseCursor;
    path_component_t pathComponent{};
    path_component_t baseComponent{};

    for ( ;; ) {
        const usize iPathBefore = iPathCursor;
        const usize iBaseBefore = iBaseCursor;
        const bool_t bHasPath = NextPathComponent( path, iPathCursor, pathComponent );
        const bool_t bHasBase = NextPathComponent( base, iBaseCursor, baseComponent );
        if ( !bHasPath || !bHasBase ||
             !PathComponentEquals(
                 path,
                 pathComponent,
                 base,
                 baseComponent,
                 bCaseInsensitiveAscii ) ) {
            iPathRemaining = iPathBefore;
            iBaseRemaining = iBaseBefore;
            break;
        }
        iPathRemaining = iPathCursor;
        iBaseRemaining = iBaseCursor;
    }

    usize cBaseComponents = 0u;
    usize iCountCursor = iBaseRemaining;
    while ( NextPathComponent( base, iCountCursor, baseComponent ) ) {
        ++cBaseComponents;
    }

    path_writer_t writer = MakePathWriter( pDest, cchDest );
    const char chSeparator = StringPath_Separator( style );
    bool_t bHasOutput = CY_FALSE;
    for ( usize iParent = 0u; iParent < cBaseComponents; ++iParent ) {
        if ( bHasOutput ) {
            PathWriter_WriteByte( writer, chSeparator );
        }
        PathWriter_WriteText(
            writer,
            StringView_FromCString( ".." ),
            chSeparator,
            CY_FALSE );
        bHasOutput = CY_TRUE;
    }

    usize iOutputCursor = iPathRemaining;
    while ( NextPathComponent( path, iOutputCursor, pathComponent ) ) {
        if ( bHasOutput ) {
            PathWriter_WriteByte( writer, chSeparator );
        }
        PathWriter_WriteText(
            writer,
            { path.pData + pathComponent.iStart, pathComponent.cchLength },
            chSeparator,
            CY_FALSE );
        bHasOutput = CY_TRUE;
    }
    if ( !bHasOutput ) {
        PathWriter_WriteByte( writer, '.' );
    }
    return FinishPathWrite( writer );
}

} // namespace cypher::common
