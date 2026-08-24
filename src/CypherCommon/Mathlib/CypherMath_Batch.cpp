//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Batch.cpp
//  Purpose: Implements four-lane SIMD-friendly vector math.
//  Details: x64 uses baseline SSE2, ARM64 uses baseline NEON, and unsupported
//           targets retain the same behavior through scalar kernels.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Batch Implementation Notes

Batch operations process caller-owned contiguous ranges and permit vectorized implementations
only when alignment and aliasing contracts are satisfied.
================
*/

#include "CypherMath_Batch.h"

#include "CypherCommon_Assert.h"
#include "CypherCommon_Platform.h"

#if CYPHER_ARCH_X86_FAMILY
    #include <emmintrin.h>
#elif CYPHER_ARCH_ARM_FAMILY
    #include <arm_neon.h>
#endif

namespace cypher::math
{

namespace
{

bool_t BatchPointersValid( const void *pInput, const void *pOutput, usize cValues ) noexcept
{
    // Empty ranges permit null pointers, matching the scalar container APIs.
    return cValues == 0u || ( pInput != nullptr && pOutput != nullptr );
}

vec3_soa4_t Vec3Soa4_Transform(
    mat4_t matrix,
    vec3_soa4_t values,
    bool_t bPoint ) noexcept
{
    // Structure-of-arrays layout lets one 128-bit operation process the same
    // component for four independent vectors.
    vec3_soa4_t result{};

#if CYPHER_ARCH_X86_FAMILY
    const __m128 x = _mm_load_ps( values.x );
    const __m128 y = _mm_load_ps( values.y );
    const __m128 z = _mm_load_ps( values.z );

    __m128 outputX = _mm_add_ps(
        _mm_add_ps(
            _mm_mul_ps( _mm_set1_ps( matrix.m[0] ), x ),
            _mm_mul_ps( _mm_set1_ps( matrix.m[4] ), y ) ),
        _mm_mul_ps( _mm_set1_ps( matrix.m[8] ), z ) );
    __m128 outputY = _mm_add_ps(
        _mm_add_ps(
            _mm_mul_ps( _mm_set1_ps( matrix.m[1] ), x ),
            _mm_mul_ps( _mm_set1_ps( matrix.m[5] ), y ) ),
        _mm_mul_ps( _mm_set1_ps( matrix.m[9] ), z ) );
    __m128 outputZ = _mm_add_ps(
        _mm_add_ps(
            _mm_mul_ps( _mm_set1_ps( matrix.m[2] ), x ),
            _mm_mul_ps( _mm_set1_ps( matrix.m[6] ), y ) ),
        _mm_mul_ps( _mm_set1_ps( matrix.m[10] ), z ) );

    // Translation contributes to points (w=1) but not directions (w=0).
    if ( bPoint ) {
        outputX = _mm_add_ps( outputX, _mm_set1_ps( matrix.m[12] ) );
        outputY = _mm_add_ps( outputY, _mm_set1_ps( matrix.m[13] ) );
        outputZ = _mm_add_ps( outputZ, _mm_set1_ps( matrix.m[14] ) );
    }

    _mm_store_ps( result.x, outputX );
    _mm_store_ps( result.y, outputY );
    _mm_store_ps( result.z, outputZ );
#elif CYPHER_ARCH_ARM_FAMILY
    const float32x4_t x = vld1q_f32( values.x );
    const float32x4_t y = vld1q_f32( values.y );
    const float32x4_t z = vld1q_f32( values.z );

    float32x4_t outputX = vmulq_n_f32( x, matrix.m[0] );
    outputX = vmlaq_n_f32( outputX, y, matrix.m[4] );
    outputX = vmlaq_n_f32( outputX, z, matrix.m[8] );
    float32x4_t outputY = vmulq_n_f32( x, matrix.m[1] );
    outputY = vmlaq_n_f32( outputY, y, matrix.m[5] );
    outputY = vmlaq_n_f32( outputY, z, matrix.m[9] );
    float32x4_t outputZ = vmulq_n_f32( x, matrix.m[2] );
    outputZ = vmlaq_n_f32( outputZ, y, matrix.m[6] );
    outputZ = vmlaq_n_f32( outputZ, z, matrix.m[10] );

    if ( bPoint ) {
        outputX = vaddq_f32( outputX, vdupq_n_f32( matrix.m[12] ) );
        outputY = vaddq_f32( outputY, vdupq_n_f32( matrix.m[13] ) );
        outputZ = vaddq_f32( outputZ, vdupq_n_f32( matrix.m[14] ) );
    }

    vst1q_f32( result.x, outputX );
    vst1q_f32( result.y, outputY );
    vst1q_f32( result.z, outputZ );
#else
    for ( u32 i = 0u; i < CY_MATH_BATCH_LANES; ++i ) {
        const vec3_t value = Vec3_Make( values.x[i], values.y[i], values.z[i] );
        const vec3_t transformed = bPoint
            ? Mat4_TransformPointAffine( matrix, value )
            : Mat4_TransformDirection( matrix, value );
        result.x[i] = transformed.x;
        result.y[i] = transformed.y;
        result.z[i] = transformed.z;
    }
#endif

    return result;
}

#if CYPHER_ARCH_ARM_FAMILY
void Vec3Batch_TransformArm(
    mat4_t matrix,
    const vec3_t *pInput,
    vec3_t *pOutput,
    usize cValues,
    bool_t bPoint ) noexcept
{
    // vld3/vst3 transpose tightly packed AoS vec3 data directly into NEON lanes.
    usize i = 0u;
    for ( ; i + CY_MATH_BATCH_LANES <= cValues; i += CY_MATH_BATCH_LANES ) {
        const float32x4x3_t input = vld3q_f32(
            reinterpret_cast<const f32 *>( pInput + i ) );
        float32x4x3_t output{};
        output.val[0] = vmulq_n_f32( input.val[0], matrix.m[0] );
        output.val[0] = vmlaq_n_f32( output.val[0], input.val[1], matrix.m[4] );
        output.val[0] = vmlaq_n_f32( output.val[0], input.val[2], matrix.m[8] );
        output.val[1] = vmulq_n_f32( input.val[0], matrix.m[1] );
        output.val[1] = vmlaq_n_f32( output.val[1], input.val[1], matrix.m[5] );
        output.val[1] = vmlaq_n_f32( output.val[1], input.val[2], matrix.m[9] );
        output.val[2] = vmulq_n_f32( input.val[0], matrix.m[2] );
        output.val[2] = vmlaq_n_f32( output.val[2], input.val[1], matrix.m[6] );
        output.val[2] = vmlaq_n_f32( output.val[2], input.val[2], matrix.m[10] );
        if ( bPoint ) {
            output.val[0] = vaddq_f32(
                output.val[0], vdupq_n_f32( matrix.m[12] ) );
            output.val[1] = vaddq_f32(
                output.val[1], vdupq_n_f32( matrix.m[13] ) );
            output.val[2] = vaddq_f32(
                output.val[2], vdupq_n_f32( matrix.m[14] ) );
        }
        vst3q_f32( reinterpret_cast<f32 *>( pOutput + i ), output );
    }
    for ( ; i < cValues; ++i ) {
        pOutput[i] = bPoint
            ? Mat4_TransformPointAffine( matrix, pInput[i] )
            : Mat4_TransformDirection( matrix, pInput[i] );
    }
}
#endif

} // namespace

math_batch_backend_t MathBatch_CompiledBackend() noexcept
{
    // Backend selection is compile-time; no hidden runtime dispatch occurs here.
#if CYPHER_ARCH_X86_FAMILY
    return math_batch_backend_t::SSE2;
#elif CYPHER_ARCH_ARM_FAMILY
    return math_batch_backend_t::NEON;
#else
    return math_batch_backend_t::SCALAR;
#endif
}

vec3_soa4_t Vec3Soa4_Load( const vec3_t *pValues ) noexcept
{
    const bool_t bValidInput = pValues != nullptr;
    CY_ASSERT_MSG( bValidInput, "Vec3Soa4_Load requires four input vectors." );
    if ( !bValidInput ) {
        return {};
    }

    // Gather four AoS vectors into three aligned component lanes.
    vec3_soa4_t result{};
    for ( u32 i = 0u; i < CY_MATH_BATCH_LANES; ++i ) {
        result.x[i] = pValues[i].x;
        result.y[i] = pValues[i].y;
        result.z[i] = pValues[i].z;
    }
    return result;
}

void Vec3Soa4_Store( vec3_soa4_t values, vec3_t *pValues ) noexcept
{
    const bool_t bValidOutput = pValues != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Vec3Soa4_Store requires output storage." );
    if ( !bValidOutput ) {
        return;
    }

    for ( u32 i = 0u; i < CY_MATH_BATCH_LANES; ++i ) {
        pValues[i] = Vec3_Make( values.x[i], values.y[i], values.z[i] );
    }
}

vec3_soa4_t Vec3Soa4_Add( vec3_soa4_t a, vec3_soa4_t b ) noexcept
{
    vec3_soa4_t result{};
#if CYPHER_ARCH_X86_FAMILY
    _mm_store_ps( result.x, _mm_add_ps( _mm_load_ps( a.x ), _mm_load_ps( b.x ) ) );
    _mm_store_ps( result.y, _mm_add_ps( _mm_load_ps( a.y ), _mm_load_ps( b.y ) ) );
    _mm_store_ps( result.z, _mm_add_ps( _mm_load_ps( a.z ), _mm_load_ps( b.z ) ) );
#elif CYPHER_ARCH_ARM_FAMILY
    vst1q_f32( result.x, vaddq_f32( vld1q_f32( a.x ), vld1q_f32( b.x ) ) );
    vst1q_f32( result.y, vaddq_f32( vld1q_f32( a.y ), vld1q_f32( b.y ) ) );
    vst1q_f32( result.z, vaddq_f32( vld1q_f32( a.z ), vld1q_f32( b.z ) ) );
#else
    for ( u32 i = 0u; i < CY_MATH_BATCH_LANES; ++i ) {
        result.x[i] = a.x[i] + b.x[i];
        result.y[i] = a.y[i] + b.y[i];
        result.z[i] = a.z[i] + b.z[i];
    }
#endif
    return result;
}

vec3_soa4_t Vec3Soa4_Subtract( vec3_soa4_t a, vec3_soa4_t b ) noexcept
{
    vec3_soa4_t result{};
#if CYPHER_ARCH_X86_FAMILY
    _mm_store_ps( result.x, _mm_sub_ps( _mm_load_ps( a.x ), _mm_load_ps( b.x ) ) );
    _mm_store_ps( result.y, _mm_sub_ps( _mm_load_ps( a.y ), _mm_load_ps( b.y ) ) );
    _mm_store_ps( result.z, _mm_sub_ps( _mm_load_ps( a.z ), _mm_load_ps( b.z ) ) );
#elif CYPHER_ARCH_ARM_FAMILY
    vst1q_f32( result.x, vsubq_f32( vld1q_f32( a.x ), vld1q_f32( b.x ) ) );
    vst1q_f32( result.y, vsubq_f32( vld1q_f32( a.y ), vld1q_f32( b.y ) ) );
    vst1q_f32( result.z, vsubq_f32( vld1q_f32( a.z ), vld1q_f32( b.z ) ) );
#else
    for ( u32 i = 0u; i < CY_MATH_BATCH_LANES; ++i ) {
        result.x[i] = a.x[i] - b.x[i];
        result.y[i] = a.y[i] - b.y[i];
        result.z[i] = a.z[i] - b.z[i];
    }
#endif
    return result;
}

vec3_soa4_t Vec3Soa4_Scale( vec3_soa4_t values, f32 scale ) noexcept
{
    vec3_soa4_t result{};
#if CYPHER_ARCH_X86_FAMILY
    const __m128 scale4 = _mm_set1_ps( scale );
    _mm_store_ps( result.x, _mm_mul_ps( _mm_load_ps( values.x ), scale4 ) );
    _mm_store_ps( result.y, _mm_mul_ps( _mm_load_ps( values.y ), scale4 ) );
    _mm_store_ps( result.z, _mm_mul_ps( _mm_load_ps( values.z ), scale4 ) );
#elif CYPHER_ARCH_ARM_FAMILY
    vst1q_f32( result.x, vmulq_n_f32( vld1q_f32( values.x ), scale ) );
    vst1q_f32( result.y, vmulq_n_f32( vld1q_f32( values.y ), scale ) );
    vst1q_f32( result.z, vmulq_n_f32( vld1q_f32( values.z ), scale ) );
#else
    for ( u32 i = 0u; i < CY_MATH_BATCH_LANES; ++i ) {
        result.x[i] = values.x[i] * scale;
        result.y[i] = values.y[i] * scale;
        result.z[i] = values.z[i] * scale;
    }
#endif
    return result;
}

vec3_soa4_t Vec3Soa4_MulAdd( vec3_soa4_t a, vec3_soa4_t b, f32 scale ) noexcept
{
    return Vec3Soa4_Add( a, Vec3Soa4_Scale( b, scale ) );
}

f32_soa4_t Vec3Soa4_Dot( vec3_soa4_t a, vec3_soa4_t b ) noexcept
{
    f32_soa4_t result{};
#if CYPHER_ARCH_X86_FAMILY
    const __m128 productX = _mm_mul_ps( _mm_load_ps( a.x ), _mm_load_ps( b.x ) );
    const __m128 productY = _mm_mul_ps( _mm_load_ps( a.y ), _mm_load_ps( b.y ) );
    const __m128 productZ = _mm_mul_ps( _mm_load_ps( a.z ), _mm_load_ps( b.z ) );
    _mm_store_ps( result.lane, _mm_add_ps( _mm_add_ps( productX, productY ), productZ ) );
#elif CYPHER_ARCH_ARM_FAMILY
    float32x4_t dot = vmulq_f32( vld1q_f32( a.x ), vld1q_f32( b.x ) );
    dot = vmlaq_f32( dot, vld1q_f32( a.y ), vld1q_f32( b.y ) );
    dot = vmlaq_f32( dot, vld1q_f32( a.z ), vld1q_f32( b.z ) );
    vst1q_f32( result.lane, dot );
#else
    for ( u32 i = 0u; i < CY_MATH_BATCH_LANES; ++i ) {
        result.lane[i] = a.x[i] * b.x[i] + a.y[i] * b.y[i] + a.z[i] * b.z[i];
    }
#endif
    return result;
}

f32_soa4_t Vec3Soa4_LengthSquared( vec3_soa4_t values ) noexcept
{
    return Vec3Soa4_Dot( values, values );
}

vec3_soa4_t Vec3Soa4_TransformPointsAffine(
    mat4_t matrix,
    vec3_soa4_t points ) noexcept
{
    return Vec3Soa4_Transform( matrix, points, common::CY_TRUE );
}

vec3_soa4_t Vec3Soa4_TransformDirections(
    mat4_t matrix,
    vec3_soa4_t directions ) noexcept
{
    return Vec3Soa4_Transform( matrix, directions, common::CY_FALSE );
}

void Vec3Batch_TransformPointsAffine(
    mat4_t matrix,
    const vec3_t *pInput,
    vec3_t *pOutput,
    usize cValues ) noexcept
{
    const bool_t bValidPointers = BatchPointersValid( pInput, pOutput, cValues );
    CY_ASSERT_MSG( bValidPointers, "Vec3Batch_TransformPointsAffine received invalid storage." );
    if ( !bValidPointers ) {
        return;
    }

#if CYPHER_ARCH_ARM_FAMILY
    Vec3Batch_TransformArm(
        matrix, pInput, pOutput, cValues, common::CY_TRUE );
#else
    // Process complete SIMD groups, then finish the non-multiple-of-four tail.
    usize i = 0u;
    for ( ; i + CY_MATH_BATCH_LANES <= cValues; i += CY_MATH_BATCH_LANES ) {
        const vec3_soa4_t input = Vec3Soa4_Load( pInput + i );
        Vec3Soa4_Store( Vec3Soa4_TransformPointsAffine( matrix, input ), pOutput + i );
    }
    for ( ; i < cValues; ++i ) {
        pOutput[i] = Mat4_TransformPointAffine( matrix, pInput[i] );
    }
#endif
}

void Vec3Batch_TransformDirections(
    mat4_t matrix,
    const vec3_t *pInput,
    vec3_t *pOutput,
    usize cValues ) noexcept
{
    const bool_t bValidPointers = BatchPointersValid( pInput, pOutput, cValues );
    CY_ASSERT_MSG( bValidPointers, "Vec3Batch_TransformDirections received invalid storage." );
    if ( !bValidPointers ) {
        return;
    }

#if CYPHER_ARCH_ARM_FAMILY
    Vec3Batch_TransformArm(
        matrix, pInput, pOutput, cValues, common::CY_FALSE );
#else
    usize i = 0u;
    for ( ; i + CY_MATH_BATCH_LANES <= cValues; i += CY_MATH_BATCH_LANES ) {
        const vec3_soa4_t input = Vec3Soa4_Load( pInput + i );
        Vec3Soa4_Store( Vec3Soa4_TransformDirections( matrix, input ), pOutput + i );
    }
    for ( ; i < cValues; ++i ) {
        pOutput[i] = Mat4_TransformDirection( matrix, pInput[i] );
    }
#endif
}

void Vec3Batch_Dot(
    const vec3_t *pA,
    const vec3_t *pB,
    f32 *pOutput,
    usize cValues ) noexcept
{
    const bool_t bValidPointers = cValues == 0u ||
        ( pA != nullptr && pB != nullptr && pOutput != nullptr );
    CY_ASSERT_MSG( bValidPointers, "Vec3Batch_Dot received invalid storage." );
    if ( !bValidPointers ) {
        return;
    }

#if CYPHER_ARCH_ARM_FAMILY
    // Interleaved NEON loads avoid a separate scalar gather for packed vec3_t.
    usize i = 0u;
    for ( ; i + CY_MATH_BATCH_LANES <= cValues; i += CY_MATH_BATCH_LANES ) {
        const float32x4x3_t a = vld3q_f32(
            reinterpret_cast<const f32 *>( pA + i ) );
        const float32x4x3_t b = vld3q_f32(
            reinterpret_cast<const f32 *>( pB + i ) );
        float32x4_t dot = vmulq_f32( a.val[0], b.val[0] );
        dot = vmlaq_f32( dot, a.val[1], b.val[1] );
        dot = vmlaq_f32( dot, a.val[2], b.val[2] );
        vst1q_f32( pOutput + i, dot );
    }
#else
    usize i = 0u;
    for ( ; i + CY_MATH_BATCH_LANES <= cValues; i += CY_MATH_BATCH_LANES ) {
        const f32_soa4_t dot = Vec3Soa4_Dot(
            Vec3Soa4_Load( pA + i ),
            Vec3Soa4_Load( pB + i ) );
        for ( u32 lane = 0u; lane < CY_MATH_BATCH_LANES; ++lane ) {
            pOutput[i + lane] = dot.lane[lane];
        }
    }
#endif
    // Scalar tail handles arrays whose length is not divisible by four.
    for ( ; i < cValues; ++i ) {
        pOutput[i] = Vec3_Dot( pA[i], pB[i] );
    }
}

} // namespace cypher::math
