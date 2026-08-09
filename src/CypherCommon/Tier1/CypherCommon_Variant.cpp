//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Variant.cpp
//  Purpose: Implements the compact non-owning primitive variant.
//  Details: Variant preserves exact stored types and borrowed range lifetimes. It
//           performs no coercion, allocation, ownership transfer, or locale work.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Variant.h"

namespace cypher::common
{

variant_t Variant_Empty() noexcept
{
    return {};
}

variant_t Variant_FromBool( bool_t value ) noexcept
{
    variant_t result{};
    result.type = variant_type_t::BOOL;
    result.data.bValue = value;
    return result;
}

variant_t Variant_FromI64( i64 value ) noexcept
{
    variant_t result{};
    result.type = variant_type_t::I64;
    result.data.iValue = value;
    return result;
}

variant_t Variant_FromU64( u64 value ) noexcept
{
    variant_t result{};
    result.type = variant_type_t::U64;
    result.data.uValue = value;
    return result;
}

variant_t Variant_FromF64( f64 value ) noexcept
{
    variant_t result{};
    result.type = variant_type_t::F64;
    result.data.flValue = value;
    return result;
}

variant_t Variant_FromString( string_view_t value ) noexcept
{
    const bool_t bValidValue = StringView_IsValid( value );
    CY_ASSERT_MSG(
        bValidValue,
        "Variant_FromString requires a valid borrowed string view." );
    if ( !bValidValue ) {
        return {};
    }

    variant_t result{};
    result.type = variant_type_t::STRING_VIEW;
    result.data.stringValue = { value.pData, value.cchLength };
    return result;
}

variant_t Variant_FromBytes( const_byte_span_t value ) noexcept
{
    const bool_t bValidValue = Span_IsValid( value );
    CY_ASSERT_MSG(
        bValidValue,
        "Variant_FromBytes requires a valid borrowed byte span." );
    if ( !bValidValue ) {
        return {};
    }

    variant_t result{};
    result.type = variant_type_t::BYTE_VIEW;
    result.data.byteValue = { value.pData, value.nCount };
    return result;
}

variant_t Variant_FromPointer( void *pValue ) noexcept
{
    variant_t result{};
    result.type = variant_type_t::POINTER;
    result.data.pValue = pValue;
    return result;
}

bool_t Variant_IsValid( variant_t value ) noexcept
{
    switch ( value.type ) {
    case variant_type_t::EMPTY:
    case variant_type_t::BOOL:
    case variant_type_t::I64:
    case variant_type_t::U64:
    case variant_type_t::F64:
    case variant_type_t::POINTER:
        return CY_TRUE;
    case variant_type_t::STRING_VIEW:
        return value.data.stringValue.pData != nullptr ||
               value.data.stringValue.cchLength == 0u;
    case variant_type_t::BYTE_VIEW:
        return value.data.byteValue.pData != nullptr ||
               value.data.byteValue.cbSize == 0u;
    default:
        return CY_FALSE;
    }
}

bool_t Variant_IsEmpty( variant_t value ) noexcept
{
    const bool_t bValidValue = Variant_IsValid( value );
    CY_ASSERT_MSG( bValidValue, "Variant_IsEmpty requires a valid variant." );
    return bValidValue && value.type == variant_type_t::EMPTY;
}

variant_type_t Variant_Type( variant_t value ) noexcept
{
    const bool_t bValidValue = Variant_IsValid( value );
    CY_ASSERT_MSG( bValidValue, "Variant_Type requires a valid variant." );
    return bValidValue ? value.type : variant_type_t::EMPTY;
}

void Variant_Reset( variant_t *pValue ) noexcept
{
    const bool_t bValidDestination = pValue != nullptr;
    CY_ASSERT_MSG(
        bValidDestination,
        "Variant_Reset requires a destination." );
    if ( bValidDestination ) {
        *pValue = {};
    }
}

bool_t Variant_GetBool( variant_t value, bool_t *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Variant_GetBool requires an output." );
    if ( !bValidOutput || !Variant_IsValid( value ) ||
         value.type != variant_type_t::BOOL ) {
        return CY_FALSE;
    }
    *pOut = value.data.bValue;
    return CY_TRUE;
}

bool_t Variant_GetI64( variant_t value, i64 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Variant_GetI64 requires an output." );
    if ( !bValidOutput || !Variant_IsValid( value ) ||
         value.type != variant_type_t::I64 ) {
        return CY_FALSE;
    }
    *pOut = value.data.iValue;
    return CY_TRUE;
}

bool_t Variant_GetU64( variant_t value, u64 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Variant_GetU64 requires an output." );
    if ( !bValidOutput || !Variant_IsValid( value ) ||
         value.type != variant_type_t::U64 ) {
        return CY_FALSE;
    }
    *pOut = value.data.uValue;
    return CY_TRUE;
}

bool_t Variant_GetF64( variant_t value, f64 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Variant_GetF64 requires an output." );
    if ( !bValidOutput || !Variant_IsValid( value ) ||
         value.type != variant_type_t::F64 ) {
        return CY_FALSE;
    }
    *pOut = value.data.flValue;
    return CY_TRUE;
}

bool_t Variant_GetString(
    variant_t value,
    string_view_t *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Variant_GetString requires an output." );
    if ( !bValidOutput || !Variant_IsValid( value ) ||
         value.type != variant_type_t::STRING_VIEW ) {
        return CY_FALSE;
    }
    *pOut = {
        value.data.stringValue.pData,
        value.data.stringValue.cchLength
    };
    return CY_TRUE;
}

bool_t Variant_GetBytes(
    variant_t value,
    const_byte_span_t *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Variant_GetBytes requires an output." );
    if ( !bValidOutput || !Variant_IsValid( value ) ||
         value.type != variant_type_t::BYTE_VIEW ) {
        return CY_FALSE;
    }
    *pOut = {
        value.data.byteValue.pData,
        value.data.byteValue.cbSize
    };
    return CY_TRUE;
}

bool_t Variant_GetPointer( variant_t value, void **ppOut ) noexcept
{
    const bool_t bValidOutput = ppOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Variant_GetPointer requires an output." );
    if ( !bValidOutput || !Variant_IsValid( value ) ||
         value.type != variant_type_t::POINTER ) {
        return CY_FALSE;
    }
    *ppOut = value.data.pValue;
    return CY_TRUE;
}

bool_t Variant_Equals( variant_t left, variant_t right ) noexcept
{
    const bool_t bValidValues =
        Variant_IsValid( left ) && Variant_IsValid( right );
    CY_ASSERT_MSG(
        bValidValues,
        "Variant_Equals requires valid variants." );
    if ( !bValidValues || left.type != right.type ) {
        return CY_FALSE;
    }

    switch ( left.type ) {
    case variant_type_t::EMPTY:
        return CY_TRUE;
    case variant_type_t::BOOL:
        return left.data.bValue == right.data.bValue;
    case variant_type_t::I64:
        return left.data.iValue == right.data.iValue;
    case variant_type_t::U64:
        return left.data.uValue == right.data.uValue;
    case variant_type_t::F64:
        return left.data.flValue == right.data.flValue;
    case variant_type_t::STRING_VIEW:
        return StringView_Equals(
            { left.data.stringValue.pData,
              left.data.stringValue.cchLength },
            { right.data.stringValue.pData,
              right.data.stringValue.cchLength } );
    case variant_type_t::BYTE_VIEW:
        return left.data.byteValue.cbSize == right.data.byteValue.cbSize &&
               Cy_MemEqual(
                   left.data.byteValue.pData,
                   right.data.byteValue.pData,
                   left.data.byteValue.cbSize );
    case variant_type_t::POINTER:
        return left.data.pValue == right.data.pValue;
    default:
        return CY_FALSE;
    }
}

} // namespace cypher::common
