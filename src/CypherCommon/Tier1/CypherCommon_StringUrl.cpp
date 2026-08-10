//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringUrl.cpp
//  Purpose: Implements bounded URI parsing and percent conversion.
//  Details: Parsing follows the RFC 3986 component layout without DNS, HTTP, IDN,
//           normalization, or trust decisions. Returned component views borrow input.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringUrl.h"

#include "CypherCommon_Char.h"

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_URL_ENCODE_FLAG_MASK =
    URL_ENCODE_FLAG_SPACE_AS_PLUS |
    URL_ENCODE_FLAG_PRESERVE_SLASH |
    URL_ENCODE_FLAG_UPPERCASE_HEX;

constexpr flags32_t CY_URL_DECODE_FLAG_MASK =
    URL_DECODE_FLAG_PLUS_AS_SPACE |
    URL_DECODE_FLAG_REJECT_NUL;

struct url_text_writer_t {
    char *pDest{ nullptr };
    usize cchCapacity{ 0u };
    usize cchWritten{ 0u };
    usize cchRequired{ 0u };
    bool_t bOverflow{ CY_FALSE };
};

bool_t IsUrlDelimiter( char ch ) noexcept
{
    return ch == '/' || ch == '?' || ch == '#';
}

bool_t IsUrlTextByteValid( u8 value ) noexcept
{
    return value > 0x20u && value != 0x7Fu;
}

url_result_t InvalidUrlResult(
    url_status_t status,
    usize iError,
    usize cchConsumed = 0u ) noexcept
{
    return { status, cchConsumed, 0u, 0u, iError };
}

bool_t ValidatePercentEscapes(
    string_view_t text,
    usize iStart,
    usize iEnd,
    usize &iErrorOut ) noexcept
{
    for ( usize iByte = iStart; iByte < iEnd; ++iByte ) {
        const u8 value = static_cast<u8>( text.pData[iByte] );
        if ( !IsUrlTextByteValid( value ) ) {
            iErrorOut = iByte;
            return CY_FALSE;
        }
        if ( text.pData[iByte] != '%' ) {
            continue;
        }
        if ( iByte + 2u >= iEnd ||
             !Char_IsHexDigitAscii( text.pData[iByte + 1u] ) ||
             !Char_IsHexDigitAscii( text.pData[iByte + 2u] ) ) {
            iErrorOut = iByte;
            return CY_FALSE;
        }
        iByte += 2u;
    }
    return CY_TRUE;
}

void UrlWriter_Write(
    url_text_writer_t &writer,
    const char *pText,
    usize cchText ) noexcept
{
    if ( cchText > CY_USIZE_MAX - writer.cchRequired ) {
        writer.bOverflow = CY_TRUE;
        return;
    }
    writer.cchRequired += cchText;
    if ( cchText <= writer.cchCapacity - writer.cchWritten ) {
        for ( usize iByte = 0u; iByte < cchText; ++iByte ) {
            writer.pDest[writer.cchWritten + iByte] = pText[iByte];
        }
        writer.cchWritten += cchText;
    }
}

url_result_t FinishUrlWriter(
    url_text_writer_t &writer,
    usize cchConsumed ) noexcept
{
    if ( writer.pDest != nullptr ) {
        writer.pDest[writer.cchWritten] = '\0';
    }
    if ( writer.bOverflow ) {
        return {
            url_status_t::INVALID_ARGUMENT,
            cchConsumed,
            writer.cchWritten,
            CY_USIZE_MAX,
            CY_STRING_VIEW_NPOS
        };
    }
    return {
        writer.cchWritten == writer.cchRequired
            ? url_status_t::OK
            : url_status_t::OUTPUT_TRUNCATED,
        cchConsumed,
        writer.cchWritten,
        writer.cchRequired,
        CY_STRING_VIEW_NPOS
    };
}

} // namespace

bool_t StringUrl_IsUnreservedByte( u8 value ) noexcept
{
    const char ch = static_cast<char>( value );
    return Char_IsAlphaNumericAscii( ch ) ||
           ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

bool_t StringUrl_IsValidScheme( string_view_t scheme ) noexcept
{
    if ( !StringView_IsValid( scheme ) || scheme.cchLength == 0u ||
         !Char_IsAlphaAscii( scheme.pData[0] ) ) {
        return CY_FALSE;
    }
    for ( usize iByte = 1u; iByte < scheme.cchLength; ++iByte ) {
        const char ch = scheme.pData[iByte];
        if ( !Char_IsAlphaNumericAscii( ch ) && ch != '+' && ch != '-' && ch != '.' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

url_result_t StringUrl_Parse( string_view_t url, url_parts_t *pPartsOut ) noexcept
{
    if ( !StringView_IsValid( url ) || pPartsOut == nullptr ) {
        return InvalidUrlResult( url_status_t::INVALID_ARGUMENT, CY_STRING_VIEW_NPOS );
    }
    *pPartsOut = {};

    usize iError = CY_STRING_VIEW_NPOS;
    if ( !ValidatePercentEscapes( url, 0u, url.cchLength, iError ) ) {
        return InvalidUrlResult( url_status_t::INVALID_URL, iError, iError );
    }

    usize iCursor = 0u;
    usize iSchemeEnd = CY_STRING_VIEW_NPOS;
    for ( usize iByte = 0u; iByte < url.cchLength; ++iByte ) {
        const char ch = url.pData[iByte];
        if ( ch == ':' ) {
            iSchemeEnd = iByte;
            break;
        }
        if ( IsUrlDelimiter( ch ) ) {
            break;
        }
    }
    if ( iSchemeEnd != CY_STRING_VIEW_NPOS ) {
        const string_view_t scheme{ url.pData, iSchemeEnd };
        if ( !StringUrl_IsValidScheme( scheme ) ) {
            return InvalidUrlResult( url_status_t::INVALID_URL, 0u, 0u );
        }
        pPartsOut->scheme = scheme;
        iCursor = iSchemeEnd + 1u;
    }

    if ( iCursor + 1u < url.cchLength &&
         url.pData[iCursor] == '/' && url.pData[iCursor + 1u] == '/' ) {
        pPartsOut->bHasAuthority = CY_TRUE;
        const usize iAuthorityStart = iCursor + 2u;
        usize iAuthorityEnd = iAuthorityStart;
        while ( iAuthorityEnd < url.cchLength &&
                url.pData[iAuthorityEnd] != '/' &&
                url.pData[iAuthorityEnd] != '?' &&
                url.pData[iAuthorityEnd] != '#' ) {
            ++iAuthorityEnd;
        }

        usize iHostStart = iAuthorityStart;
        for ( usize iByte = iAuthorityStart; iByte < iAuthorityEnd; ++iByte ) {
            if ( url.pData[iByte] == '@' ) {
                pPartsOut->userInfo = {
                    url.pData + iAuthorityStart,
                    iByte - iAuthorityStart
                };
                iHostStart = iByte + 1u;
            }
        }

        if ( iHostStart < iAuthorityEnd && url.pData[iHostStart] == '[' ) {
            usize iClose = iHostStart + 1u;
            while ( iClose < iAuthorityEnd && url.pData[iClose] != ']' ) {
                ++iClose;
            }
            if ( iClose >= iAuthorityEnd || iClose == iHostStart + 1u ) {
                return InvalidUrlResult(
                    url_status_t::INVALID_URL,
                    iHostStart,
                    iHostStart );
            }
            pPartsOut->host = {
                url.pData + iHostStart + 1u,
                iClose - iHostStart - 1u
            };
            if ( iClose + 1u < iAuthorityEnd ) {
                if ( url.pData[iClose + 1u] != ':' || iClose + 2u >= iAuthorityEnd ) {
                    return InvalidUrlResult(
                        url_status_t::INVALID_URL,
                        iClose + 1u,
                        iClose + 1u );
                }
                pPartsOut->port = {
                    url.pData + iClose + 2u,
                    iAuthorityEnd - iClose - 2u
                };
            }
        } else {
            usize iColon = CY_STRING_VIEW_NPOS;
            for ( usize iByte = iHostStart; iByte < iAuthorityEnd; ++iByte ) {
                if ( url.pData[iByte] == ':' ) {
                    if ( iColon != CY_STRING_VIEW_NPOS ) {
                        return InvalidUrlResult(
                            url_status_t::INVALID_URL,
                            iByte,
                            iByte );
                    }
                    iColon = iByte;
                }
            }
            const usize iHostEnd = iColon == CY_STRING_VIEW_NPOS
                ? iAuthorityEnd
                : iColon;
            pPartsOut->host = {
                url.pData + iHostStart,
                iHostEnd - iHostStart
            };
            if ( iColon != CY_STRING_VIEW_NPOS ) {
                if ( iColon + 1u >= iAuthorityEnd ) {
                    return InvalidUrlResult(
                        url_status_t::INVALID_URL,
                        iColon,
                        iColon );
                }
                pPartsOut->port = {
                    url.pData + iColon + 1u,
                    iAuthorityEnd - iColon - 1u
                };
            }
        }

        for ( usize iByte = 0u; iByte < pPartsOut->port.cchLength; ++iByte ) {
            if ( !Char_IsDigitAscii( pPartsOut->port.pData[iByte] ) ) {
                const usize iPortError = static_cast<usize>(
                    pPartsOut->port.pData + iByte - url.pData );
                return InvalidUrlResult(
                    url_status_t::INVALID_URL,
                    iPortError,
                    iPortError );
            }
        }
        iCursor = iAuthorityEnd;
    }

    const usize iPathStart = iCursor;
    while ( iCursor < url.cchLength &&
            url.pData[iCursor] != '?' && url.pData[iCursor] != '#' ) {
        ++iCursor;
    }
    pPartsOut->path = iCursor > iPathStart
        ? string_view_t{ url.pData + iPathStart, iCursor - iPathStart }
        : string_view_t{};

    if ( iCursor < url.cchLength && url.pData[iCursor] == '?' ) {
        const usize iQueryStart = ++iCursor;
        while ( iCursor < url.cchLength && url.pData[iCursor] != '#' ) {
            ++iCursor;
        }
        pPartsOut->query = { url.pData + iQueryStart, iCursor - iQueryStart };
    }
    if ( iCursor < url.cchLength && url.pData[iCursor] == '#' ) {
        const usize iFragmentStart = ++iCursor;
        pPartsOut->fragment = {
            url.pData + iFragmentStart,
            url.cchLength - iFragmentStart
        };
        iCursor = url.cchLength;
    }

    return {
        url_status_t::OK,
        iCursor,
        0u,
        0u,
        CY_STRING_VIEW_NPOS
    };
}

bool_t StringUrl_HostEquals(
    const url_parts_t &parts,
    string_view_t expectedHost ) noexcept
{
    return StringView_IsValid( parts.host ) && StringView_IsValid( expectedHost ) &&
           StringView_EqualsInsensitiveAscii( parts.host, expectedHost );
}

url_result_t StringUrl_PercentEncode(
    const_byte_span_t source,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDest = pDest != nullptr || cchDest == 0u;
    if ( !Span_IsValid( source ) || !bValidDest ||
         ( flags & ~CY_URL_ENCODE_FLAG_MASK ) != 0u ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return InvalidUrlResult( url_status_t::INVALID_ARGUMENT, CY_STRING_VIEW_NPOS );
    }

    url_text_writer_t writer{
        pDest,
        cchDest > 0u ? cchDest - 1u : 0u,
        0u,
        0u,
        CY_FALSE
    };
    const char *pHex = ( flags & URL_ENCODE_FLAG_UPPERCASE_HEX ) != 0u
        ? "0123456789ABCDEF"
        : "0123456789abcdef";

    for ( usize iByte = 0u; iByte < source.nCount; ++iByte ) {
        const u8 value = source.pData[iByte];
        if ( StringUrl_IsUnreservedByte( value ) ||
             ( value == static_cast<u8>( '/' ) &&
               ( flags & URL_ENCODE_FLAG_PRESERVE_SLASH ) != 0u ) ) {
            const char ch = static_cast<char>( value );
            UrlWriter_Write( writer, &ch, 1u );
        } else if ( value == static_cast<u8>( ' ' ) &&
                    ( flags & URL_ENCODE_FLAG_SPACE_AS_PLUS ) != 0u ) {
            constexpr char plus = '+';
            UrlWriter_Write( writer, &plus, 1u );
        } else {
            const char escaped[]{
                '%',
                pHex[( value >> 4u ) & 0x0Fu],
                pHex[value & 0x0Fu]
            };
            UrlWriter_Write( writer, escaped, sizeof( escaped ) );
        }
    }
    return FinishUrlWriter( writer, source.nCount );
}

url_result_t StringUrl_PercentDecode(
    string_view_t source,
    flags32_t flags,
    byte_span_t dest ) noexcept
{
    if ( !StringView_IsValid( source ) || !Span_IsValid( dest ) ||
         ( flags & ~CY_URL_DECODE_FLAG_MASK ) != 0u ) {
        return InvalidUrlResult( url_status_t::INVALID_ARGUMENT, CY_STRING_VIEW_NPOS );
    }

    usize cbWritten = 0u;
    usize cbRequired = 0u;
    for ( usize iByte = 0u; iByte < source.cchLength; ++iByte ) {
        const usize iSourceByte = iByte;
        u8 value = static_cast<u8>( source.pData[iByte] );
        if ( source.pData[iByte] == '%' ) {
            if ( iByte + 2u >= source.cchLength ||
                 !Char_IsHexDigitAscii( source.pData[iByte + 1u] ) ||
                 !Char_IsHexDigitAscii( source.pData[iByte + 2u] ) ) {
                return {
                    url_status_t::INVALID_ESCAPE,
                    iByte,
                    cbWritten,
                    cbRequired,
                    iByte
                };
            }
            value = static_cast<u8>(
                ( Char_HexValueAscii( source.pData[iByte + 1u] ) << 4u ) |
                Char_HexValueAscii( source.pData[iByte + 2u] ) );
            iByte += 2u;
        } else if ( source.pData[iByte] == '+' &&
                    ( flags & URL_DECODE_FLAG_PLUS_AS_SPACE ) != 0u ) {
            value = static_cast<u8>( ' ' );
        }

        if ( value == 0u && ( flags & URL_DECODE_FLAG_REJECT_NUL ) != 0u ) {
            return {
                url_status_t::INVALID_URL,
                iSourceByte,
                cbWritten,
                cbRequired,
                iSourceByte
            };
        }
        if ( cbRequired == CY_USIZE_MAX ) {
            return InvalidUrlResult( url_status_t::INVALID_ARGUMENT, iByte, iByte );
        }
        ++cbRequired;
        if ( cbWritten < dest.nCount ) {
            dest.pData[cbWritten++] = value;
        }
    }

    return {
        cbWritten == cbRequired
            ? url_status_t::OK
            : url_status_t::OUTPUT_TRUNCATED,
        source.cchLength,
        cbWritten,
        cbRequired,
        CY_STRING_VIEW_NPOS
    };
}

} // namespace cypher::common
