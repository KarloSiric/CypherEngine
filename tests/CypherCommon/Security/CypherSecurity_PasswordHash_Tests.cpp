//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Security/CypherSecurity_PasswordHash_Tests.cpp
//  Purpose: Tests Argon2id password hashing contracts.
//  Details: Correct, incorrect, malformed, and policy-upgrade paths ensure account
//           authentication cannot accidentally use ordinary engine hashes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity.h"
#include "CypherCommon_StringView.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;
using namespace cypher::security;

TEST_CASE( "Argon2id hashes verify and report stronger policy requirements",
           "[CypherSecurity][PasswordHash]" )
{
    const string_view_t password =
        StringView_FromCString( "correct horse battery staple" );
    const string_view_t wrongPassword =
        StringView_FromCString( "correct horse battery stapler" );

    password_hash_t hash{};
    REQUIRE(
        PasswordHash_Create(
            password,
            password_hash_profile_t::INTERACTIVE,
            &hash ) == security_status_t::OK );
    REQUIRE( hash.encoded[0] == '$' );
    REQUIRE(
        PasswordHash_Verify( password, hash ) == security_status_t::OK );
    REQUIRE(
        PasswordHash_Verify( wrongPassword, hash ) ==
        security_status_t::AUTHENTICATION_FAILED );

    bool_t bNeedsRehash = CY_TRUE;
    REQUIRE(
        PasswordHash_CheckRehash(
            hash,
            password_hash_profile_t::INTERACTIVE,
            &bNeedsRehash ) == security_status_t::OK );
    REQUIRE_FALSE( bNeedsRehash );

    REQUIRE(
        PasswordHash_CheckRehash(
            hash,
            password_hash_profile_t::MODERATE,
            &bNeedsRehash ) == security_status_t::OK );
    REQUIRE( bNeedsRehash );
}

TEST_CASE( "Argon2id wrapper distinguishes malformed encodings",
           "[CypherSecurity][PasswordHash]" )
{
    constexpr char malformedText[] = "$argon2id$malformed";
    password_hash_t malformed{};
    for ( usize iChar = 0u; iChar < sizeof( malformedText ); ++iChar ) {
        malformed.encoded[iChar] = malformedText[iChar];
    }

    const string_view_t password = StringView_FromCString( "password" );
    REQUIRE(
        PasswordHash_Verify( password, malformed ) ==
        security_status_t::INVALID_ENCODING );

    bool_t bNeedsRehash = CY_FALSE;
    REQUIRE(
        PasswordHash_CheckRehash(
            malformed,
            password_hash_profile_t::INTERACTIVE,
            &bNeedsRehash ) == security_status_t::INVALID_ENCODING );
}

TEST_CASE( "Argon2id wrapper safely supports an empty password range",
           "[CypherSecurity][PasswordHash][Empty]" )
{
    password_hash_t hash{};
    REQUIRE(
        PasswordHash_Create(
            {},
            password_hash_profile_t::INTERACTIVE,
            &hash ) == security_status_t::OK );
    REQUIRE( PasswordHash_Verify( {}, hash ) == security_status_t::OK );
    REQUIRE(
        PasswordHash_Verify(
            StringView_FromCString( "not-empty" ),
            hash ) == security_status_t::AUTHENTICATION_FAILED );
}
