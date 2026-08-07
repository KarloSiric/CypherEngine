//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Result.h
//  Purpose: Declares generic operation and value result records.
//  Details: Results reuse Tier0 packed error codes so diagnostics retain subsystem
//           domains without introducing another incompatible error vocabulary.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RESULT_H
#define CYPHER_COMMON_TIER1_RESULT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct result_t {
    error_code_t error{ CY_ERROR_OK };
};

template <typename value_t>
struct value_result_t {
    value_t value{};
    error_code_t error{ CY_ERROR_OK };
};

CYPHER_NODISCARD constexpr bool_t Result_Succeeded( result_t result ) noexcept
{
    return Cy_ErrorSucceeded( result.error );
}

CYPHER_NODISCARD constexpr bool_t Result_Failed( result_t result ) noexcept
{
    return Cy_ErrorFailed( result.error );
}

template <typename value_t>
CYPHER_NODISCARD constexpr bool_t Result_Succeeded(
    const value_result_t<value_t> &result ) noexcept
{
    return Cy_ErrorSucceeded( result.error );
}

template <typename value_t>
CYPHER_NODISCARD constexpr bool_t Result_Failed(
    const value_result_t<value_t> &result ) noexcept
{
    return Cy_ErrorFailed( result.error );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RESULT_H
