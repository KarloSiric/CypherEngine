//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueWriterInternal.h
//  Purpose: Shares the private native-CYKV and strict-JSON writer core.
//  Details: Both public formats use identical tree traversal and bounded output
//           accounting while selecting their own punctuation and escape grammar.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_KEYVALUEWRITERINTERNAL_H
#define CYPHER_COMMON_TIER1_KEYVALUEWRITERINTERNAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_KeyValueWriter.h"

namespace cypher::common
{

CYPHER_NODISCARD key_value_write_result_t KeyValue_InternalWriteText(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    char *pDest,
    usize cchDest,
    bool_t bStrictJson ) noexcept;

CYPHER_NODISCARD key_value_write_result_t KeyValue_InternalWriteTextToSink(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    key_value_write_fn_t pfnWrite,
    void *pUserData,
    bool_t bStrictJson ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEWRITERINTERNAL_H
