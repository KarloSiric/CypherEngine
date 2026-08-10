//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_NameServiceAddress.cpp
//  Purpose: Implements unresolved host and service endpoint text.
//  Details: Parsing owns bounded text for later asynchronous resolution. It performs
//           no DNS, interface lookup, socket access, or address-family selection.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_NameServiceAddress.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_Unicode.h"

namespace cypher::common
{

namespace
{

bool_t HostTextIsValid( string_view_t host ) noexcept
{
    if ( !StringView_IsValid( host ) || host.cchLength == 0u ||
         host.cchLength > CY_NET_HOST_NAME_CAPACITY ||
         Unicode_ValidateUtf8( host ).status != unicode_status_t::OK ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < host.cchLength; ++iByte ) {
        const char ch = host.pData[iByte];
        if ( ch == '\0' || Char_IsWhitespaceAscii( ch ) ||
             Char_IsControlAscii( ch ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t ServiceTextIsValid( string_view_t service ) noexcept
{
    if ( !StringView_IsValid( service ) ||
         service.cchLength > CY_NET_SERVICE_NAME_CAPACITY ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < service.cchLength; ++iByte ) {
        const char ch = service.pData[iByte];
        if ( !Char_IsAlphaNumericAscii( ch ) &&
             ch != '-' && ch != '_' && ch != '.' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

usize FormatPort( u16 nPort, char *pDest ) noexcept
{
    char reverse[5]{};
    usize cch = 0u;
    do {
        reverse[cch++] = static_cast<char>( '0' + nPort % 10u );
        nPort = static_cast<u16>( nPort / 10u );
    } while ( nPort != 0u );
    for ( usize iDigit = 0u; iDigit < cch; ++iDigit ) {
        pDest[iDigit] = reverse[cch - 1u - iDigit];
    }
    return cch;
}

} // namespace

bool_t NameServiceAddress_Parse(
    string_view_t text,
    u16 nDefaultPort,
    name_service_address_t *pAddressOut ) noexcept
{
    if ( pAddressOut == nullptr || !StringView_IsValid( text ) ||
         text.cchLength == 0u ) {
        return CY_FALSE;
    }
    *pAddressOut = {};

    string_view_t host = text;
    string_view_t service{};
    if ( text.pData[0] == '[' ) {
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
            service = {
                text.pData + iClose + 2u,
                text.cchLength - iClose - 2u
            };
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
        if ( cColons == 1u ) {
            if ( iColon == 0u || iColon + 1u >= text.cchLength ) {
                return CY_FALSE;
            }
            host = { text.pData, iColon };
            service = { text.pData + iColon + 1u, text.cchLength - iColon - 1u };
        }
    }

    if ( !HostTextIsValid( host ) || !ServiceTextIsValid( service ) ) {
        return CY_FALSE;
    }
    if ( FixedString_Assign( &pAddressOut->host, host ) != host.cchLength ||
         FixedString_Assign( &pAddressOut->service, service ) != service.cchLength ) {
        *pAddressOut = {};
        return CY_FALSE;
    }
    pAddressOut->nDefaultPort = nDefaultPort;
    return CY_TRUE;
}

bool_t NameServiceAddress_IsValid(
    const name_service_address_t &address ) noexcept
{
    return FixedString_IsValid( address.host ) &&
           FixedString_IsValid( address.service ) &&
           HostTextIsValid( FixedString_View( address.host ) ) &&
           ServiceTextIsValid( FixedString_View( address.service ) );
}

usize NameServiceAddress_Format(
    const name_service_address_t &address,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( pDest == nullptr && cchDest != 0u ) {
        return 0u;
    }
    if ( pDest != nullptr && cchDest > 0u ) {
        pDest[0] = '\0';
    }
    if ( !NameServiceAddress_IsValid( address ) ) {
        return 0u;
    }

    const string_view_t host = FixedString_View( address.host );
    const string_view_t service = FixedString_View( address.service );
    const bool_t bHostNeedsBrackets = StringView_FindChar( host, ':' ) != CY_STRING_VIEW_NPOS &&
        ( service.cchLength > 0u || address.nDefaultPort != 0u );
    char port[5]{};
    const usize cchPort = service.cchLength == 0u && address.nDefaultPort != 0u
        ? FormatPort( address.nDefaultPort, port )
        : 0u;
    const usize cchSuffix = service.cchLength > 0u
        ? service.cchLength + 1u
        : ( cchPort > 0u ? cchPort + 1u : 0u );
    const usize cchRequired = host.cchLength + cchSuffix +
        ( bHostNeedsBrackets ? 2u : 0u );
    const usize cchCapacity = cchDest > 0u ? cchDest - 1u : 0u;
    usize cchWritten = 0u;
    auto writeByte = [&]( char ch ) noexcept {
        if ( cchWritten < cchCapacity ) {
            pDest[cchWritten++] = ch;
        }
    };

    if ( bHostNeedsBrackets ) {
        writeByte( '[' );
    }
    for ( usize iByte = 0u; iByte < host.cchLength; ++iByte ) {
        writeByte( host.pData[iByte] );
    }
    if ( bHostNeedsBrackets ) {
        writeByte( ']' );
    }
    if ( cchSuffix > 0u ) {
        writeByte( ':' );
        if ( service.cchLength > 0u ) {
            for ( usize iByte = 0u; iByte < service.cchLength; ++iByte ) {
                writeByte( service.pData[iByte] );
            }
        } else {
            for ( usize iByte = 0u; iByte < cchPort; ++iByte ) {
                writeByte( port[iByte] );
            }
        }
    }
    if ( pDest != nullptr ) {
        pDest[cchWritten] = '\0';
    }
    return cchRequired;
}

} // namespace cypher::common
