//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Security/CypherSecurity_Encoding_Tests.cpp
//  Purpose: Tests strict hexadecimal and Base64 conversions.
//  Details: Known vectors, all variants, empty input, malformed syntax,
//           canonical trailing bits, and capacity behavior are covered.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace cypher::common;
using namespace cypher::security;

TEST_CASE( "CypherSecurity hexadecimal encoding is strict and reversible",
           "[CypherSecurity][Encoding][Hex]" )
{
    constexpr std::array<byte, 3u> binary{ 0x00u, 0xabu, 0xffu };
    std::array<char, 7u> encoded{};
    usize cchWritten = 0u;
    REQUIRE(
        SecurityHex_Encode(
            BinaryBlock_FromData( binary.data(), binary.size() ),
            encoded.data(),
            encoded.size(),
            &cchWritten ) == security_status_t::OK );
    REQUIRE( cchWritten == 6u );
    REQUIRE( StringView_FromCString( encoded.data() ).cchLength == 6u );
    REQUIRE( std::array<char, 7u>{ '0', '0', 'a', 'b', 'f', 'f', '\0' } == encoded );

    constexpr std::array<char, 6u> uppercase{ '0', '0', 'A', 'B', 'F', 'F' };
    std::array<byte, 3u> decoded{};
    usize cbWritten = 0u;
    REQUIRE(
        SecurityHex_Decode(
            StringView_FromRange( uppercase.data(), uppercase.size() ),
            decoded.data(),
            decoded.size(),
            &cbWritten ) == security_status_t::OK );
    REQUIRE( cbWritten == binary.size() );
    REQUIRE( decoded == binary );
}

TEST_CASE( "CypherSecurity hexadecimal rejects partial and malformed text",
           "[CypherSecurity][Encoding][Hex][Invalid]" )
{
    std::array<byte, 4u> output{};
    usize cbWritten = 55u;
    REQUIRE(
        SecurityHex_Decode(
            StringView_FromCString( "abc" ),
            output.data(),
            output.size(),
            &cbWritten ) == security_status_t::INVALID_ENCODING );
    REQUIRE( cbWritten == 55u );
    REQUIRE(
        SecurityHex_Decode(
            StringView_FromCString( "00 gg" ),
            output.data(),
            output.size(),
            &cbWritten ) == security_status_t::INVALID_ENCODING );

    constexpr std::array<byte, 2u> input{ 1u, 2u };
    std::array<char, 4u> tooSmall{};
    usize cchWritten = 77u;
    REQUIRE(
        SecurityHex_Encode(
            BinaryBlock_FromData( input.data(), input.size() ),
            tooSmall.data(),
            tooSmall.size(),
            &cchWritten ) == security_status_t::BUFFER_TOO_SMALL );
    REQUIRE( cchWritten == 77u );
}

TEST_CASE( "CypherSecurity Base64 supports standard and URL-safe variants",
           "[CypherSecurity][Encoding][Base64]" )
{
    constexpr std::array<byte, 1u> one{ 'f' };
    constexpr std::array<byte, 3u> urlBytes{ 0xfbu, 0xffu, 0x00u };

    struct vector_t {
        binary_block_t binary;
        base64_variant_t variant;
        const char *pExpected;
    };
    const std::array<vector_t, 4u> vectors{
        vector_t{ BinaryBlock_FromData( one.data(), one.size() ),
                  base64_variant_t::ORIGINAL, "Zg==" },
        vector_t{ BinaryBlock_FromData( one.data(), one.size() ),
                  base64_variant_t::ORIGINAL_NO_PADDING, "Zg" },
        vector_t{ BinaryBlock_FromData( urlBytes.data(), urlBytes.size() ),
                  base64_variant_t::URL_SAFE, "-_8A" },
        vector_t{ BinaryBlock_FromData( urlBytes.data(), urlBytes.size() ),
                  base64_variant_t::URL_SAFE_NO_PADDING, "-_8A" }
    };

    for ( const vector_t &vector : vectors ) {
        usize cchRequired = 0u;
        REQUIRE(
            SecurityBase64_EncodedSize(
                vector.binary.cbSize,
                vector.variant,
                &cchRequired ) );
        std::array<char, 16u> encoded{};
        usize cchWritten = 0u;
        REQUIRE(
            SecurityBase64_Encode(
                vector.binary,
                vector.variant,
                encoded.data(),
                encoded.size(),
                &cchWritten ) == security_status_t::OK );
        REQUIRE( cchRequired == cchWritten + 1u );
        REQUIRE(
            StringView_Equals(
                StringView_FromRange( encoded.data(), cchWritten ),
                StringView_FromCString( vector.pExpected ) ) );

        std::array<byte, 8u> decoded{};
        usize cbWritten = 0u;
        REQUIRE(
            SecurityBase64_Decode(
                StringView_FromRange( encoded.data(), cchWritten ),
                vector.variant,
                decoded.data(),
                decoded.size(),
                &cbWritten ) == security_status_t::OK );
        REQUIRE( cbWritten == vector.binary.cbSize );
        REQUIRE(
            Security_ConstantTimeEquals(
                decoded.data(),
                vector.binary.pData,
                cbWritten ) );
    }
}

TEST_CASE( "CypherSecurity Base64 rejects non-canonical or mismatched syntax",
           "[CypherSecurity][Encoding][Base64][Invalid]" )
{
    constexpr std::array<const char *, 5u> invalidPadded{
        "Zg",
        "Z===",
        "Zh==",
        "Zg== ",
        "Zg=_"
    };
    std::array<byte, 8u> output{};
    for ( const char *pEncoded : invalidPadded ) {
        usize cbWritten = 91u;
        REQUIRE(
            SecurityBase64_Decode(
                StringView_FromCString( pEncoded ),
                base64_variant_t::ORIGINAL,
                output.data(),
                output.size(),
                &cbWritten ) == security_status_t::INVALID_ENCODING );
        REQUIRE( cbWritten == 91u );
    }

    usize cbWritten = 0u;
    REQUIRE(
        SecurityBase64_Decode(
            StringView_FromCString( "Zg==" ),
            base64_variant_t::ORIGINAL_NO_PADDING,
            output.data(),
            output.size(),
            &cbWritten ) == security_status_t::INVALID_ENCODING );
    REQUIRE(
        SecurityBase64_Decode(
            StringView_FromCString( "+/8=" ),
            base64_variant_t::URL_SAFE,
            output.data(),
            output.size(),
            &cbWritten ) == security_status_t::INVALID_ENCODING );
}

TEST_CASE( "CypherSecurity encodings support empty input and exact capacity",
           "[CypherSecurity][Encoding][Empty]" )
{
    std::array<char, 1u> encoded{ 'x' };
    usize cchWritten = 99u;
    REQUIRE(
        SecurityHex_Encode(
            {}, encoded.data(), encoded.size(), &cchWritten ) ==
        security_status_t::OK );
    REQUIRE( cchWritten == 0u );
    REQUIRE( encoded[0] == '\0' );

    encoded[0] = 'x';
    REQUIRE(
        SecurityBase64_Encode(
            {},
            base64_variant_t::ORIGINAL,
            encoded.data(),
            encoded.size(),
            &cchWritten ) == security_status_t::OK );
    REQUIRE( cchWritten == 0u );
    REQUIRE( encoded[0] == '\0' );

    usize cbWritten = 99u;
    REQUIRE(
        SecurityHex_Decode(
            {}, nullptr, 0u, &cbWritten ) == security_status_t::OK );
    REQUIRE( cbWritten == 0u );
    cbWritten = 99u;
    REQUIRE(
        SecurityBase64_Decode(
            {},
            base64_variant_t::URL_SAFE_NO_PADDING,
            nullptr,
            0u,
            &cbWritten ) == security_status_t::OK );
    REQUIRE( cbWritten == 0u );
}

TEST_CASE( "CypherSecurity encoding size helpers validate complete syntax",
           "[CypherSecurity][Encoding][Size]" )
{
    usize cbSize = 73u;
    REQUIRE( SecurityHex_EncodedSize( 0u, &cbSize ) );
    REQUIRE( cbSize == 1u );
    REQUIRE( SecurityHex_EncodedSize( 8u, &cbSize ) );
    REQUIRE( cbSize == 17u );

    cbSize = 73u;
    REQUIRE_FALSE(
        SecurityHex_EncodedSize( ( CY_USIZE_MAX / 2u ) + 1u, &cbSize ) );
    REQUIRE( cbSize == 73u );
    REQUIRE_FALSE( SecurityHex_EncodedSize( 0u, nullptr ) );

    REQUIRE(
        SecurityHex_DecodedSize(
            StringView_FromCString( "00ffA5" ),
            &cbSize ) );
    REQUIRE( cbSize == 3u );
    REQUIRE_FALSE(
        SecurityHex_DecodedSize(
            StringView_FromCString( "abc" ),
            &cbSize ) );
    REQUIRE_FALSE(
        SecurityHex_DecodedSize(
            StringView_FromCString( "00xz" ),
            &cbSize ) );
    REQUIRE_FALSE( SecurityHex_DecodedSize( {}, nullptr ) );

    REQUIRE(
        SecurityBase64_DecodedSize(
            StringView_FromCString( "Zm9v" ),
            base64_variant_t::ORIGINAL,
            &cbSize ) );
    REQUIRE( cbSize == 3u );
    REQUIRE(
        SecurityBase64_DecodedSize(
            StringView_FromCString( "Zg" ),
            base64_variant_t::URL_SAFE_NO_PADDING,
            &cbSize ) );
    REQUIRE( cbSize == 1u );
    REQUIRE_FALSE(
        SecurityBase64_DecodedSize(
            StringView_FromCString( "Zg" ),
            base64_variant_t::ORIGINAL,
            &cbSize ) );
    REQUIRE_FALSE(
        SecurityBase64_DecodedSize(
            {},
            static_cast<base64_variant_t>( 0xFFu ),
            &cbSize ) );
}
