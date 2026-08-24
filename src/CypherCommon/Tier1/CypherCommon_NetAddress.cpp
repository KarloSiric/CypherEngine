//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_NetAddress.cpp
//  Purpose: Implements portable resolved IPv4 and IPv6 endpoint values.
//  Details: Numeric parsing and formatting use OS address conversion only. No DNS,
//           socket creation, interface-name lookup, or network initialization occurs.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_NetAddress.h"

#include "CypherCommon_Char.h"

#if CYPHER_PLATFORM_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
#endif

namespace cypher::common
{

namespace
{

constexpr usize CY_NET_ADDRESS_TEXT_CAPACITY = 128u; // Numeric host plus scope and NUL.

bool_t ParseDecimalU32( string_view_t text, u32 nMax, u32 &valueOut ) noexcept
{
    if ( !StringView_IsValid( text ) || text.cchLength == 0u ) {
        return CY_FALSE;
    }
    u32 nValue = 0u;
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        const u8 nDigit = Char_DigitValueAscii( text.pData[iByte] );
        // Check before multiply-add so the maximum accepted value is exact.
        if ( nDigit == CY_CHAR_INVALID_DIGIT_VALUE ||
             nValue > ( nMax - nDigit ) / 10u ) {
            return CY_FALSE;
        }
        nValue = nValue * 10u + nDigit;
    }
    valueOut = nValue;
    return CY_TRUE;
}

usize FormatDecimalU32( u32 value, char *pDest ) noexcept
{
    char reverse[10]{};
    usize cch = 0u;
    do {
        reverse[cch++] = static_cast<char>( '0' + value % 10u );
        value /= 10u;
    } while ( value != 0u );
    for ( usize iDigit = 0u; iDigit < cch; ++iDigit ) {
        pDest[iDigit] = reverse[cch - 1u - iDigit];
    }
    return cch;
}

bool_t CopyBoundedAddressText(
    string_view_t text,
    char ( &dest )[CY_NET_ADDRESS_TEXT_CAPACITY] ) noexcept
{
    if ( !StringView_IsValid( text ) || text.cchLength == 0u ||
         text.cchLength >= CY_NET_ADDRESS_TEXT_CAPACITY ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        if ( text.pData[iByte] == '\0' ||
             Char_IsWhitespaceAscii( text.pData[iByte] ) ) {
            return CY_FALSE;
        }
        dest[iByte] = text.pData[iByte];
    }
    dest[text.cchLength] = '\0';
    return CY_TRUE;
}

} // namespace

net_address_t NetAddress_AnyIpv4( u16 nPort ) noexcept
{
    net_address_t address{};
    address.nPort = nPort;
    address.family = net_address_family_t::IPV4;
    return address;
}

net_address_t NetAddress_LoopbackIpv4( u16 nPort ) noexcept
{
    net_address_t address = NetAddress_AnyIpv4( nPort );
    address.address[0] = 127u;
    address.address[3] = 1u;
    return address;
}

net_address_t NetAddress_LoopbackIpv6( u16 nPort ) noexcept
{
    net_address_t address{};
    address.address[15] = 1u;
    address.nPort = nPort;
    address.family = net_address_family_t::IPV6;
    return address;
}

bool_t NetAddress_Parse(
    string_view_t text,
    u16 nDefaultPort,
    net_address_t *pAddressOut ) noexcept
{
    if ( pAddressOut == nullptr || !StringView_IsValid( text ) ||
         text.cchLength == 0u ) {
        return CY_FALSE;
    }
    *pAddressOut = {};

    string_view_t host = text;
    string_view_t port{};
    bool_t bBracketed = CY_FALSE;
    if ( text.pData[0] == '[' ) {
        // Brackets are required when an IPv6 literal is followed by a port.
        bBracketed = CY_TRUE;
        usize iClose = 1u;
        while ( iClose < text.cchLength && text.pData[iClose] != ']' ) {
            ++iClose;
        }
        if ( iClose >= text.cchLength || iClose == 1u ) {
            return CY_FALSE;
        }
        host = { text.pData + 1u, iClose - 1u };
        if ( iClose + 1u < text.cchLength ) {
            if ( text.pData[iClose + 1u] != ':' || iClose + 2u >= text.cchLength ) {
                return CY_FALSE;
            }
            port = { text.pData + iClose + 2u, text.cchLength - iClose - 2u };
        }
    } else {
        usize cColons = 0u;
        usize iColon = CY_STRING_VIEW_NPOS;
        for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
            if ( text.pData[iByte] == ':' ) {
                ++cColons;
                iColon = iByte;
            }
        }
        // One colon is host:port; multiple colons are an unbracketed IPv6 host
        // with no separately parseable port.
        if ( cColons == 1u ) {
            if ( iColon == 0u || iColon + 1u >= text.cchLength ) {
                return CY_FALSE;
            }
            host = { text.pData, iColon };
            port = { text.pData + iColon + 1u, text.cchLength - iColon - 1u };
        }
    }

    u32 nPort = nDefaultPort;
    if ( port.cchLength > 0u && !ParseDecimalU32( port, CY_U16_MAX, nPort ) ) {
        return CY_FALSE;
    }

    u32 nScopeId = 0u;
    usize iScope = CY_STRING_VIEW_NPOS;
    for ( usize iByte = 0u; iByte < host.cchLength; ++iByte ) {
        if ( host.pData[iByte] == '%' ) {
            if ( iScope != CY_STRING_VIEW_NPOS ) {
                return CY_FALSE;
            }
            iScope = iByte;
        }
    }
    if ( iScope != CY_STRING_VIEW_NPOS ) {
        // This low-level value accepts numeric IPv6 scope IDs only. Interface
        // names require platform lookup and belong in a higher networking layer.
        const string_view_t scope{
            host.pData + iScope + 1u,
            host.cchLength - iScope - 1u
        };
        if ( iScope == 0u || !ParseDecimalU32( scope, CY_U32_MAX, nScopeId ) ) {
            return CY_FALSE;
        }
        host.cchLength = iScope;
    }

    char hostText[CY_NET_ADDRESS_TEXT_CAPACITY]{};
    if ( !CopyBoundedAddressText( host, hostText ) ) {
        return CY_FALSE;
    }

    net_address_t parsed{};
    // inet_pton performs numeric conversion only; this path never blocks on DNS.
    const bool_t bIpv4 = !bBracketed && iScope == CY_STRING_VIEW_NPOS &&
        inet_pton( AF_INET, hostText, parsed.address ) == 1;
    if ( bIpv4 ) {
        parsed.family = net_address_family_t::IPV4;
    } else if ( inet_pton( AF_INET6, hostText, parsed.address ) == 1 ) {
        parsed.family = net_address_family_t::IPV6;
        parsed.nScopeId = nScopeId;
    } else {
        return CY_FALSE;
    }
    parsed.nPort = static_cast<u16>( nPort );
    *pAddressOut = parsed;
    return CY_TRUE;
}

usize NetAddress_Format(
    net_address_t address,
    bool_t bIncludePort,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( pDest == nullptr && cchDest != 0u ) {
        return 0u;
    }
    if ( pDest != nullptr && cchDest > 0u ) {
        pDest[0] = '\0';
    }
    if ( !NetAddress_IsValid( address ) ) {
        return 0u;
    }

    char host[INET6_ADDRSTRLEN]{};
    const i32 nFamily = address.family == net_address_family_t::IPV4
        ? AF_INET
        : AF_INET6;
    if ( inet_ntop( nFamily, address.address, host, sizeof( host ) ) == nullptr ) {
        return 0u;
    }

    usize cchHost = 0u;
    while ( host[cchHost] != '\0' ) {
        ++cchHost;
    }
    char scope[10]{};
    const usize cchScope = address.family == net_address_family_t::IPV6 &&
        address.nScopeId != 0u
            ? FormatDecimalU32( address.nScopeId, scope )
            : 0u;
    char port[5]{};
    const usize cchPort = bIncludePort
        ? FormatDecimalU32( address.nPort, port )
        : 0u;
    const bool_t bBrackets = bIncludePort &&
        address.family == net_address_family_t::IPV6;
    const usize cchRequired = cchHost +
        ( cchScope > 0u ? cchScope + 1u : 0u ) +
        ( bBrackets ? 2u : 0u ) +
        ( bIncludePort ? cchPort + 1u : 0u );

    // Return the complete required length while writing a terminated prefix when
    // the destination is short.
    const usize cchCapacity = cchDest > 0u ? cchDest - 1u : 0u;
    usize cchWritten = 0u;
    auto writeByte = [&]( char ch ) noexcept {
        if ( cchWritten < cchCapacity ) {
            pDest[cchWritten++] = ch;
        }
    };
    if ( bBrackets ) {
        writeByte( '[' );
    }
    for ( usize iByte = 0u; iByte < cchHost; ++iByte ) {
        writeByte( host[iByte] );
    }
    if ( cchScope > 0u ) {
        writeByte( '%' );
        for ( usize iByte = 0u; iByte < cchScope; ++iByte ) {
            writeByte( scope[iByte] );
        }
    }
    if ( bBrackets ) {
        writeByte( ']' );
    }
    if ( bIncludePort ) {
        writeByte( ':' );
        for ( usize iByte = 0u; iByte < cchPort; ++iByte ) {
            writeByte( port[iByte] );
        }
    }
    if ( pDest != nullptr ) {
        pDest[cchWritten] = '\0';
    }
    return cchRequired;
}

bool_t NetAddress_IsValid( net_address_t address ) noexcept
{
    if ( address.family == net_address_family_t::IPV4 ) {
        if ( address.nScopeId != 0u ) {
            return CY_FALSE;
        }
        for ( usize iByte = 4u; iByte < 16u; ++iByte ) {
            if ( address.address[iByte] != 0u ) {
                return CY_FALSE;
            }
        }
        return CY_TRUE;
    }
    return address.family == net_address_family_t::IPV6;
}

bool_t NetAddress_IsLoopback( net_address_t address ) noexcept
{
    if ( !NetAddress_IsValid( address ) ) {
        return CY_FALSE;
    }
    if ( address.family == net_address_family_t::IPV4 ) {
        return address.address[0] == 127u;
    }
    for ( usize iByte = 0u; iByte < 15u; ++iByte ) {
        if ( address.address[iByte] != 0u ) {
            return CY_FALSE;
        }
    }
    return address.address[15] == 1u;
}

bool_t NetAddress_IsMulticast( net_address_t address ) noexcept
{
    if ( !NetAddress_IsValid( address ) ) {
        return CY_FALSE;
    }
    return address.family == net_address_family_t::IPV4
        ? ( address.address[0] >= 224u && address.address[0] <= 239u )
        : address.address[0] == 0xFFu;
}

bool_t NetAddress_Equals( net_address_t left, net_address_t right ) noexcept
{
    return left.nPort == right.nPort && NetAddress_HostEquals( left, right );
}

bool_t NetAddress_HostEquals( net_address_t left, net_address_t right ) noexcept
{
    if ( !NetAddress_IsValid( left ) || !NetAddress_IsValid( right ) ||
         left.family != right.family || left.nScopeId != right.nScopeId ) {
        return CY_FALSE;
    }
    const usize cbAddress = left.family == net_address_family_t::IPV4 ? 4u : 16u;
    return Cy_MemCompare( left.address, right.address, cbAddress ) == 0;
}

} // namespace cypher::common
