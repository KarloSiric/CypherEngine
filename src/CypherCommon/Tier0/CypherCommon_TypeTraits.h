//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_TypeTraits.h
//  Purpose: Declares the small type-trait vocabulary used by Common containers.
//  Details: These are aliases over standard traits, not a replacement trait engine.
//           Keep additions driven by concrete container or ABI requirements.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_TYPETRAITS_H
#define CYPHER_COMMON_TIER0_TYPETRAITS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Type Traits

Thin aliases around standard C++ traits so engine code has one vocabulary.
================
*/

#include "CypherCommon_Annotations.h"

#include <type_traits>

namespace cypher::common
{

// Type transformation aliases.
template <typename type_t>
using remove_reference_t = typename std::remove_reference<type_t>::type;

template <typename type_t>
using remove_const_t = typename std::remove_const<type_t>::type;

template <typename type_t>
using remove_volatile_t = typename std::remove_volatile<type_t>::type;

template <typename type_t>
using remove_cv_t = typename std::remove_cv<type_t>::type;

template <typename type_t>
using remove_cvref_t = typename std::remove_cvref<type_t>::type;

template <typename type_t>
using remove_pointer_t = typename std::remove_pointer<type_t>::type;

template <typename type_t>
using decay_t = typename std::decay<type_t>::type;

template <typename type_t>
using underlying_type_t = typename std::underlying_type<type_t>::type;

template <typename type_t>
using make_signed_t = typename std::make_signed<type_t>::type;

template <typename type_t>
using make_unsigned_t = typename std::make_unsigned<type_t>::type;

template <bool bCondition, typename true_t, typename false_t>
using conditional_t = typename std::conditional<bCondition, true_t, false_t>::type;

template <bool bCondition, typename type_t = void>
using enable_if_t = typename std::enable_if<bCondition, type_t>::type;

template <typename... types_t>
using common_type_t = typename std::common_type<types_t...>::type;

// Boolean property aliases used in constraints and static assertions.
template <typename type_a_t, typename type_b_t>
inline constexpr bool is_same_v = std::is_same_v<type_a_t, type_b_t>;

template <typename type_t>
inline constexpr bool is_void_v = std::is_void_v<type_t>;

template <typename type_t>
inline constexpr bool is_integral_v = std::is_integral_v<type_t>;

template <typename type_t>
inline constexpr bool is_floating_point_v = std::is_floating_point_v<type_t>;

template <typename type_t>
inline constexpr bool is_arithmetic_v = std::is_arithmetic_v<type_t>;

template <typename type_t>
inline constexpr bool is_signed_v = std::is_signed_v<type_t>;

template <typename type_t>
inline constexpr bool is_unsigned_v = std::is_unsigned_v<type_t>;

template <typename type_t>
inline constexpr bool is_enum_v = std::is_enum_v<type_t>;

template <typename type_t>
inline constexpr bool is_pointer_v = std::is_pointer_v<type_t>;

template <typename type_t>
inline constexpr bool is_array_v = std::is_array_v<type_t>;

template <typename type_t>
inline constexpr bool is_lvalue_reference_v = std::is_lvalue_reference_v<type_t>;

template <typename type_t>
inline constexpr bool is_rvalue_reference_v = std::is_rvalue_reference_v<type_t>;

template <typename type_t>
inline constexpr bool is_scalar_v = std::is_scalar_v<type_t>;

template <typename type_t>
inline constexpr bool is_object_v = std::is_object_v<type_t>;

template <typename type_t>
inline constexpr bool is_trivially_copyable_v = std::is_trivially_copyable_v<type_t>;

template <typename type_t>
inline constexpr bool is_standard_layout_v = std::is_standard_layout_v<type_t>;

template <typename type_t>
inline constexpr bool is_trivial_v = std::is_trivial_v<type_t>;

template <typename type_t>
inline constexpr bool is_default_constructible_v = std::is_default_constructible_v<type_t>;

template <typename type_t>
inline constexpr bool is_copy_constructible_v = std::is_copy_constructible_v<type_t>;

template <typename type_t>
inline constexpr bool is_move_constructible_v = std::is_move_constructible_v<type_t>;

template <typename type_t>
inline constexpr bool is_copy_assignable_v = std::is_copy_assignable_v<type_t>;

template <typename type_t>
inline constexpr bool is_move_assignable_v = std::is_move_assignable_v<type_t>;

template <typename type_t>
inline constexpr bool is_destructible_v = std::is_destructible_v<type_t>;

template <typename type_t>
inline constexpr bool is_trivially_default_constructible_v = std::is_trivially_default_constructible_v<type_t>;

template <typename type_t>
inline constexpr bool is_trivially_destructible_v = std::is_trivially_destructible_v<type_t>;

template <typename type_t>
inline constexpr bool is_nothrow_move_constructible_v = std::is_nothrow_move_constructible_v<type_t>;

template <typename type_t>
inline constexpr bool is_nothrow_move_assignable_v = std::is_nothrow_move_assignable_v<type_t>;

// Containers may specialize this trait only after proving byte relocation is
// valid for the type. Trivially copyable types are safe by default.
template <typename type_t>
struct is_trivially_relocatable : std::bool_constant<std::is_trivially_copyable_v<type_t>> {
};

template <typename type_t>
inline constexpr bool is_trivially_relocatable_v = is_trivially_relocatable<type_t>::value;

// Converts an enum value to its declared underlying integer type.
template <typename enum_t>
CYPHER_NODISCARD constexpr underlying_type_t<enum_t> Cy_ToUnderlying( enum_t value ) noexcept
{
    static_assert( is_enum_v<enum_t>, "Cy_ToUnderlying requires an enum type." );
    return static_cast<underlying_type_t<enum_t>>( value );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_TYPETRAITS_H
