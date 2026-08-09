//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Functor.inl
//  Purpose: Implements default comparison and hashing policies.
//  Details: Scalar and string keys receive deterministic value hashing. Custom
//           aggregate keys must provide an explicit hasher to avoid padding bugs.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_FUNCTOR_INL
#define CYPHER_COMMON_TIER1_FUNCTOR_INL
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <bit>

namespace cypher::common
{

namespace detail
{

template <typename type_t>
inline constexpr bool bUnsupportedHashKey = false;

// SplitMix64 finalization gives sequential scalar keys useful avalanche behavior.
CYPHER_NODISCARD constexpr hash64_t HashFunctor_Mix64( u64 nValue ) noexcept
{
    nValue += 0x9E3779B97F4A7C15ull;
    nValue = ( nValue ^ ( nValue >> 30u ) ) * 0xBF58476D1CE4E5B9ull;
    nValue = ( nValue ^ ( nValue >> 27u ) ) * 0x94D049BB133111EBull;
    return nValue ^ ( nValue >> 31u );
}

CYPHER_NODISCARD inline hash64_t HashFunctor_StringView(
    string_view_t value ) noexcept
{
    const bool_t bValidView = StringView_IsValid( value );
    CY_ASSERT_MSG( bValidView, "String hashing requires a valid string view." );
    if ( !bValidView ) {
        return HashFunctor_Mix64( 0u );
    }

    u64 nHash = 14695981039346656037ull;
    for ( usize i = 0u; i < value.cchLength; ++i ) {
        nHash ^= static_cast<u8>( value.pData[i] );
        nHash *= 1099511628211ull;
    }
    return HashFunctor_Mix64( nHash );
}

} // namespace detail

template <typename type_t>
constexpr bool_t less_t<type_t>::operator()(
    const type_t &left,
    const type_t &right ) const noexcept
{
    if constexpr ( is_same_v<remove_cv_t<type_t>, string_view_t> ) {
        return StringView_Compare( left, right ) < 0;
    } else {
        return left < right;
    }
}

template <typename type_t>
constexpr bool_t equal_t<type_t>::operator()(
    const type_t &left,
    const type_t &right ) const noexcept
{
    if constexpr ( is_same_v<remove_cv_t<type_t>, string_view_t> ) {
        return StringView_Equals( left, right );
    } else {
        return left == right;
    }
}

template <typename type_t>
hash64_t hash_functor_t<type_t>::operator()(
    const type_t &value ) const noexcept
{
    using key_t = remove_cv_t<type_t>;

    if constexpr ( is_same_v<key_t, string_view_t> ) {
        return detail::HashFunctor_StringView( value );
    } else if constexpr ( is_same_v<key_t, bool_t> ) {
        return detail::HashFunctor_Mix64( value ? 1u : 0u );
    } else if constexpr ( is_integral_v<key_t> ) {
        using unsigned_key_t = make_unsigned_t<key_t>;
        return detail::HashFunctor_Mix64(
            static_cast<u64>( static_cast<unsigned_key_t>( value ) ) );
    } else if constexpr ( is_enum_v<key_t> ) {
        using underlying_t = underlying_type_t<key_t>;
        using unsigned_key_t = make_unsigned_t<underlying_t>;
        return detail::HashFunctor_Mix64(
            static_cast<u64>(
                static_cast<unsigned_key_t>(
                    static_cast<underlying_t>( value ) ) ) );
    } else if constexpr ( is_same_v<key_t, f32> ) {
        const u32 nBits = value == 0.0f
            ? 0u
            : std::bit_cast<u32>( value );
        return detail::HashFunctor_Mix64( nBits );
    } else if constexpr ( is_same_v<key_t, f64> ) {
        const u64 nBits = value == 0.0
            ? 0u
            : std::bit_cast<u64>( value );
        return detail::HashFunctor_Mix64( nBits );
    } else if constexpr ( is_pointer_v<key_t> ) {
        return detail::HashFunctor_Mix64(
            static_cast<u64>( reinterpret_cast<uintptr>( value ) ) );
    } else {
        static_assert(
            detail::bUnsupportedHashKey<key_t>,
            "hash_functor_t supports scalar, pointer, enum, and string_view_t keys; provide a custom hasher for aggregate keys." );
        return 0u;
    }
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FUNCTOR_INL
