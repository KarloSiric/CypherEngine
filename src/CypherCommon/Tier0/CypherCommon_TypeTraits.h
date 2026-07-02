//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_TypeTraits.h
//  Purpose: Declares CypherCommon Tier0 TypeTraits support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_TYPETRAITS_H
#define CYPHER_COMMON_TIER0_TYPETRAITS_H
#pragma once

/*
================
CypherCommon Type Traits

Thin aliases around standard C++ traits so engine code has one vocabulary.
================
*/

#include <type_traits>

namespace cypher::common
{

template <typename type_t>
using remove_reference_t = typename std::remove_reference<type_t>::type;

template <typename type_t>
using remove_cv_t = typename std::remove_cv<type_t>::type;

template <typename type_t>
using remove_cvref_t = typename std::remove_cvref<type_t>::type;

template <typename type_t>
using underlying_type_t = typename std::underlying_type<type_t>::type;

template <typename type_t>
inline constexpr bool is_integral_v = std::is_integral_v<type_t>;

template <typename type_t>
inline constexpr bool is_floating_point_v = std::is_floating_point_v<type_t>;

template <typename type_t>
inline constexpr bool is_enum_v = std::is_enum_v<type_t>;

template <typename type_t>
inline constexpr bool is_pointer_v = std::is_pointer_v<type_t>;

template <typename type_t>
inline constexpr bool is_trivially_copyable_v = std::is_trivially_copyable_v<type_t>;

template <typename type_t>
inline constexpr bool is_standard_layout_v = std::is_standard_layout_v<type_t>;

template <typename type_t>
inline constexpr bool is_trivial_v = std::is_trivial_v<type_t>;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_TYPETRAITS_H
