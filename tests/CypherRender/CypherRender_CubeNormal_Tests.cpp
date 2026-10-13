#include "CypherRender_CubeNormal.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <limits>

namespace sample = ::cypher::samples::render;
namespace math = ::cypher::math;

TEST_CASE( "sample packs inverse-transpose columns for nonuniform and sheared transforms",
    "[render][normal]" )
{
    const math::affine3_t transform = math::Affine3_FromColumns(
        { 2.0f, 0.0f, 0.0f }, { 1.0f, 3.0f, 0.0f },
        { 0.0f, 0.0f, 0.5f }, { 7.0f, -4.0f, 2.0f } );
    sample::cube_normal_columns_t packed{};
    bool mirrored = true;
    REQUIRE( sample::CubeNormal_TryPack( transform, &packed, &mirrored ) );
    CHECK_FALSE( mirrored );
    REQUIRE( sizeof( packed ) == 48u );
    for ( unsigned column = 0u; column < 3u; ++column ) {
        CHECK( packed.columns[column][3] == 0.0f );
    }

    const math::mat3_t normalMatrix = math::Mat3_FromColumns(
        { packed.columns[0][0], packed.columns[0][1], packed.columns[0][2] },
        { packed.columns[1][0], packed.columns[1][1], packed.columns[1][2] },
        { packed.columns[2][0], packed.columns[2][1], packed.columns[2][2] } );
    const math::vec3_t tangent{ 1.0f, -1.0f, 0.0f };
    const math::vec3_t normal{ 1.0f, 1.0f, 0.0f };
    const math::vec3_t worldTangent = math::Affine3_TransformDirection( transform, tangent );
    const math::vec3_t worldNormal = math::Mat3_TransformVector( normalMatrix, normal );
    CHECK( std::abs( math::Vec3_Dot( worldNormal, worldTangent ) ) < 0.00001f );
    CHECK( std::abs( worldNormal.x - 0.5f ) < 0.00001f );
    CHECK( std::abs( worldNormal.y - 1.0f / 6.0f ) < 0.00001f );
    const math::vec3_t incorrectNormal = math::Affine3_TransformDirection( transform, normal );
    CHECK( std::abs( math::Vec3_Dot( incorrectNormal, worldTangent ) ) > 1.0f );
}

TEST_CASE( "sample normal packing reports reversed handedness only for odd reflections",
    "[render][normal]" )
{
    sample::cube_normal_columns_t packed{};
    bool mirrored = false;
    REQUIRE( sample::CubeNormal_TryPack(
        math::Affine3_FromScale( { -2.0f, 3.0f, 4.0f } ),
        &packed, &mirrored ) );
    CHECK( mirrored );

    mirrored = true;
    REQUIRE( sample::CubeNormal_TryPack(
        math::Affine3_FromScale( { -2.0f, -3.0f, 4.0f } ),
        &packed, &mirrored ) );
    CHECK_FALSE( mirrored );

    // The optional output does not change existing two-argument call sites.
    CHECK( sample::CubeNormal_TryPack( math::CY_AFFINE3_IDENTITY, &packed ) );
}

TEST_CASE( "sample normal packing rejects invalid transforms without changing output",
    "[render][normal]" )
{
    sample::cube_normal_columns_t output{};
    for ( auto &column : output.columns ) {
        for ( float &component : column ) component = 42.0f;
    }
    const sample::cube_normal_columns_t before = output;
    bool mirrored = true;
    CHECK_FALSE( sample::CubeNormal_TryPack(
        math::Affine3_FromScale( { 1.0f, 0.0f, 1.0f } ), &output, &mirrored ) );
    CHECK( std::memcmp( &output, &before, sizeof( output ) ) == 0 );
    CHECK( mirrored );

    math::affine3_t nonfinite = math::CY_AFFINE3_IDENTITY;
    nonfinite.m[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE( sample::CubeNormal_TryPack( nonfinite, &output, &mirrored ) );
    CHECK( std::memcmp( &output, &before, sizeof( output ) ) == 0 );
    CHECK( mirrored );
    CHECK_FALSE( sample::CubeNormal_TryPack(
        math::CY_AFFINE3_IDENTITY, nullptr, &mirrored ) );
    CHECK( mirrored );
}
