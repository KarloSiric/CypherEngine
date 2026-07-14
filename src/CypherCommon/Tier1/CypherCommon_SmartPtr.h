//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SmartPtr.h
//  Purpose: Declares CypherCommon Tier1 SmartPtr support.
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

#ifndef CYPHER_COMMON_TIER1_SMARTPTR_H
#define CYPHER_COMMON_TIER1_SMARTPTR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Smart Ptr

Engine smart pointer declarations.
================
*/

namespace cypher::common
{

template <typename type_t>
class smart_ptr_t;

template <typename type_t>
class weak_ptr_t;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SMARTPTR_H
