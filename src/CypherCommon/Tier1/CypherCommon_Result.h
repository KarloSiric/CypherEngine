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
    error_code_t error{ CY_ERROR_OK }; // Packed domain/code status; zero means success.
};

template <typename value_t>
struct value_result_t {
    value_t value{};                    // Meaningful only when error reports success.
    error_code_t error{ CY_ERROR_OK };  // Packed domain/code status for the operation.
};

// Creates a successful operation result.
CYPHER_NODISCARD constexpr result_t Result_Success() noexcept
{
    return { CY_ERROR_OK };
}

// Creates a failed operation result. error must encode a failure.
CYPHER_NODISCARD constexpr result_t Result_Failure(
    error_code_t error ) noexcept
{
    const bool_t bFailure = Cy_ErrorFailed( error );
    CY_ASSERT_MSG( bFailure, "Result_Failure requires a failed error code." );
    return { bFailure ? error : Cy_ErrorMake( common_error_t::ERR_FAILED ) };
}

// Creates a successful value result by copying one value.
template <typename value_t>
CYPHER_NODISCARD constexpr value_result_t<value_t> ValueResult_Success(
    const value_t &value ) noexcept
{
    return { value, CY_ERROR_OK };
}

// Creates a failed value result with a default value that must not be consumed.
template <typename value_t>
CYPHER_NODISCARD constexpr value_result_t<value_t> ValueResult_Failure(
    error_code_t error ) noexcept
{
    const bool_t bFailure = Cy_ErrorFailed( error );
    CY_ASSERT_MSG( bFailure, "ValueResult_Failure requires a failed error code." );
    return {
        {},
        bFailure ? error : Cy_ErrorMake( common_error_t::ERR_FAILED )
    };
}

CYPHER_NODISCARD constexpr bool_t Result_Succeeded( result_t result ) noexcept
{
    return Cy_ErrorSucceeded( result.error );
}

CYPHER_NODISCARD constexpr bool_t Result_Failed( result_t result ) noexcept
{
    return Cy_ErrorFailed( result.error );
}

// Returns the packed status code without changing the result.
CYPHER_NODISCARD constexpr error_code_t Result_ErrorCode(
    result_t result ) noexcept
{
    return result.error;
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

template <typename value_t>
CYPHER_NODISCARD constexpr error_code_t Result_ErrorCode(
    const value_result_t<value_t> &result ) noexcept
{
    return result.error;
}

// Returns the value address only when the operation succeeded.
template <typename value_t>
CYPHER_NODISCARD constexpr value_t *ValueResult_Get(
    value_result_t<value_t> *pResult ) noexcept
{
    if ( pResult == nullptr || Result_Failed( *pResult ) ) {
        return nullptr;
    }
    return &pResult->value;
}

// Returns the read-only value address only when the operation succeeded.
template <typename value_t>
CYPHER_NODISCARD constexpr const value_t *ValueResult_Get(
    const value_result_t<value_t> *pResult ) noexcept
{
    if ( pResult == nullptr || Result_Failed( *pResult ) ) {
        return nullptr;
    }
    return &pResult->value;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RESULT_H
