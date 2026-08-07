//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_NameServiceAddress.h
//  Purpose: Declares unresolved host/service endpoint text.
//  Details: NameServiceAddress owns bounded ASCII/UTF-8 host and service strings for
//           later asynchronous DNS resolution by the networking subsystem.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_NAMESERVICEADDRESS_H
#define CYPHER_COMMON_TIER1_NAMESERVICEADDRESS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_FixedString.h"

namespace cypher::common
{

constexpr usize CY_NET_HOST_NAME_CAPACITY = 253u;
constexpr usize CY_NET_SERVICE_NAME_CAPACITY = 31u;

struct name_service_address_t {
    fixed_string_t<CY_NET_HOST_NAME_CAPACITY> host{};
    fixed_string_t<CY_NET_SERVICE_NAME_CAPACITY> service{};
    u16 nDefaultPort{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t NameServiceAddress_Parse(
    string_view_t text,
    u16 nDefaultPort,
    name_service_address_t *pAddressOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t NameServiceAddress_IsValid(
    const name_service_address_t &address ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize NameServiceAddress_Format(
    const name_service_address_t &address,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_NAMESERVICEADDRESS_H
