//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Variant.h
//  Purpose: Declares CypherCommon Tier1 Variant support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_VARIANT_H
#define CYPHER_COMMON_TIER1_VARIANT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Variant

Small runtime variant declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum class variant_type_t : u32 {
    Null = 0u,
    Bool,
    I32,
    U32,
    I64,
    U64,
    F32,
    F64,
    String
};

struct variant_t;

variant_type_t Variant_GetType( const variant_t *pVariant );
void Variant_Clear( variant_t *pVariant );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_VARIANT_H
