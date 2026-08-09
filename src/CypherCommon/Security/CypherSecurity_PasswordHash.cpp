//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_PasswordHash.cpp
//  Purpose: Implements Argon2id password hashing and verification.
//  Details: Profiles map to libsodium's audited resource policies and malformed
//           encodings are distinguished from ordinary authentication failure.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity_PasswordHash.h"
#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"
#include "CypherCommon_String.h"

#include <sodium.h>

namespace cypher::security
{

namespace
{

constexpr char g_emptyPassword = '\0';

struct password_hash_limits_t {
    unsigned long long nOperations;
    usize cbMemory;
};

CYPHER_NODISCARD bool_t PasswordHash_ProfileLimits(
    password_hash_profile_t profile,
    password_hash_limits_t &limitsOut ) noexcept
{
    switch ( profile ) {
        case password_hash_profile_t::INTERACTIVE:
            limitsOut = {
                crypto_pwhash_OPSLIMIT_INTERACTIVE,
                crypto_pwhash_MEMLIMIT_INTERACTIVE
            };
            return CY_TRUE;
        case password_hash_profile_t::MODERATE:
            limitsOut = {
                crypto_pwhash_OPSLIMIT_MODERATE,
                crypto_pwhash_MEMLIMIT_MODERATE
            };
            return CY_TRUE;
        case password_hash_profile_t::SENSITIVE:
            limitsOut = {
                crypto_pwhash_OPSLIMIT_SENSITIVE,
                crypto_pwhash_MEMLIMIT_SENSITIVE
            };
            return CY_TRUE;
        default:
            return CY_FALSE;
    }
}

CYPHER_NODISCARD bool_t PasswordHash_EncodingIsTerminated(
    const password_hash_t &hash ) noexcept
{
    return common::Cy_strnlen(
               hash.encoded,
               CY_PASSWORD_HASH_STRING_CAPACITY ) <
           CY_PASSWORD_HASH_STRING_CAPACITY;
}

CYPHER_NODISCARD const char *PasswordHash_Input(
    string_view_t password ) noexcept
{
    return password.cchLength == 0u ? &g_emptyPassword : password.pData;
}

} // namespace

static_assert(
    CY_PASSWORD_HASH_STRING_CAPACITY >= crypto_pwhash_STRBYTES,
    "Cypher password hash storage is smaller than libsodium's encoded form." );

security_status_t PasswordHash_Create(
    string_view_t password,
    password_hash_profile_t profile,
    password_hash_t *pHashOut ) noexcept
{
    const bool_t bValidPassword = StringView_IsValid( password );
    const bool_t bValidPasswordSize =
        password.cchLength <= static_cast<usize>( crypto_pwhash_PASSWD_MAX );
    const bool_t bValidOutput = pHashOut != nullptr;
    password_hash_limits_t limits{};
    const bool_t bValidProfile = PasswordHash_ProfileLimits( profile, limits );
    CY_ASSERT_MSG( bValidPassword, "Password hashing requires a valid byte range." );
    CY_ASSERT_MSG( bValidPasswordSize, "Password length exceeds the backend limit." );
    CY_ASSERT_MSG( bValidOutput, "Password hashing requires output storage." );
    CY_ASSERT_MSG( bValidProfile, "Password hashing profile is invalid." );
    if ( !bValidPassword || !bValidPasswordSize ||
         !bValidOutput || !bValidProfile ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    password_hash_t encoded{};
    const int result = crypto_pwhash_str_alg(
        encoded.encoded,
        PasswordHash_Input( password ),
        static_cast<unsigned long long>( password.cchLength ),
        limits.nOperations,
        limits.cbMemory,
        crypto_pwhash_ALG_DEFAULT );
    if ( result != 0 ) {
        return security_status_t::OPERATION_FAILED;
    }

    *pHashOut = encoded;
    return security_status_t::OK;
}

security_status_t PasswordHash_Verify(
    string_view_t password,
    const password_hash_t &hash ) noexcept
{
    const bool_t bValidPassword = StringView_IsValid( password );
    const bool_t bValidPasswordSize =
        password.cchLength <= static_cast<usize>( crypto_pwhash_PASSWD_MAX );
    const bool_t bTerminatedHash = PasswordHash_EncodingIsTerminated( hash );
    CY_ASSERT_MSG( bValidPassword, "Password verification requires valid bytes." );
    CY_ASSERT_MSG( bValidPasswordSize, "Password length exceeds the backend limit." );
    CY_ASSERT_MSG( bTerminatedHash, "Password hash encoding is not terminated." );
    if ( !bValidPassword || !bValidPasswordSize || !bTerminatedHash ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    password_hash_limits_t validationLimits{};
    const bool_t bHaveValidationLimits = PasswordHash_ProfileLimits(
        password_hash_profile_t::INTERACTIVE,
        validationLimits );
    CY_ASSERT( bHaveValidationLimits );
    if ( crypto_pwhash_str_needs_rehash(
             hash.encoded,
             validationLimits.nOperations,
             validationLimits.cbMemory ) < 0 ) {
        return security_status_t::INVALID_ENCODING;
    }

    return crypto_pwhash_str_verify(
               hash.encoded,
               PasswordHash_Input( password ),
               static_cast<unsigned long long>( password.cchLength ) ) == 0
        ? security_status_t::OK
        : security_status_t::AUTHENTICATION_FAILED;
}

security_status_t PasswordHash_CheckRehash(
    const password_hash_t &hash,
    password_hash_profile_t profile,
    bool_t *pNeedsRehashOut ) noexcept
{
    const bool_t bValidOutput = pNeedsRehashOut != nullptr;
    const bool_t bTerminatedHash = PasswordHash_EncodingIsTerminated( hash );
    password_hash_limits_t limits{};
    const bool_t bValidProfile = PasswordHash_ProfileLimits( profile, limits );
    CY_ASSERT_MSG( bValidOutput, "Password rehash check requires output storage." );
    CY_ASSERT_MSG( bTerminatedHash, "Password hash encoding is not terminated." );
    CY_ASSERT_MSG( bValidProfile, "Password hashing profile is invalid." );
    if ( !bValidOutput || !bTerminatedHash || !bValidProfile ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( !Security_IsReady() ) {
        return security_status_t::BACKEND_UNAVAILABLE;
    }

    const int result = crypto_pwhash_str_needs_rehash(
        hash.encoded,
        limits.nOperations,
        limits.cbMemory );
    if ( result < 0 ) {
        return security_status_t::INVALID_ENCODING;
    }
    *pNeedsRehashOut = result != 0;
    return security_status_t::OK;
}

} // namespace cypher::security
