//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Regresses tight AABB framing with orientation and clipping intact.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileCamera.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <algorithm>

namespace tile = ::cypher::tools::tile_editor;
namespace math = ::cypher::math;

TEST_CASE( "Frame bounds fills the viewport with a long flat room while retaining orientation", "[tile][camera]" )
{
    const math::vec3_t minimum{ -40, -3, -0.25f }, maximum{ 40, 3, 2 };
    for ( float aspect : { 0.3f, 1.0f, 2.5f } ) {
        for ( float yaw : { 0.0f, 1.3f, 2.4f } ) {
            tile::tile_camera_t camera{};
            camera.yawRadians = yaw;
            camera.pitchRadians = -0.5f;
            tile::CypherTileCamera_FrameBounds( camera, minimum, maximum, aspect, false );
            CHECK( camera.yawRadians == yaw );
            CHECK( camera.pitchRadians == -0.5f );
            math::mat4_t view{}, projection{};
            REQUIRE( tile::CypherTileCamera_BuildMatrices( camera, aspect, view, projection ) );
            const auto clipFromWorld = math::Mat4_Multiply( projection, view );
            float maximumExtent = 0;
            float left = 1, right = -1, bottom = 1, top = -1;
            for ( int i = 0; i < 8; ++i ) {
                const auto clip = math::Mat4_TransformVector4( clipFromWorld,
                    { i & 1 ? maximum.x : minimum.x, i & 2 ? maximum.y : minimum.y,
                      i & 4 ? maximum.z : minimum.z, 1 } );
                REQUIRE( clip.w > 0 );
                CHECK( std::abs( clip.x / clip.w ) < 0.92f );
                CHECK( std::abs( clip.y / clip.w ) < 0.92f );
                CHECK( clip.z / clip.w > -1 );
                CHECK( clip.z / clip.w < 1 );
                left = std::min( left, clip.x / clip.w );
                right = std::max( right, clip.x / clip.w );
                bottom = std::min( bottom, clip.y / clip.w );
                top = std::max( top, clip.y / clip.w );
                maximumExtent = std::max( maximumExtent,
                    std::max( std::abs( clip.x / clip.w ), std::abs( clip.y / clip.w ) ) );
            }
            CHECK( maximumExtent > 0.85f );
            CHECK( std::abs( left + right ) < 0.0001f );
            CHECK( std::abs( bottom + top ) < 0.0001f );
            CHECK( camera.boundsCenter.x == 0 );
            CHECK( camera.boundsCenter.y == 0 );
            CHECK( camera.boundsCenter.z == 0.875f );
        }
    }
}

TEST_CASE( "Frame flat maps fills both vertical viewport edges instead of crowding the bottom", "[tile][camera]" )
{
    const math::vec3_t minimum{ 4, 8, -0.25f }, maximum{ 48, 36, 4 };
    for ( float aspect : { 2.0f, 2.5f, 3.0f } ) {
        tile::tile_camera_t camera{};
        tile::CypherTileCamera_FrameBounds( camera, minimum, maximum, aspect, true );
        math::mat4_t view{}, projection{};
        REQUIRE( tile::CypherTileCamera_BuildMatrices( camera, aspect, view, projection ) );
        const auto clipFromWorld = math::Mat4_Multiply( projection, view );
        float bottom = 1, top = -1;
        for ( int i = 0; i < 8; ++i ) {
            const auto clip = math::Mat4_TransformVector4( clipFromWorld,
                { i & 1 ? maximum.x : minimum.x, i & 2 ? maximum.y : minimum.y,
                  i & 4 ? maximum.z : minimum.z, 1 } );
            REQUIRE( clip.w > 0 );
            bottom = std::min( bottom, clip.y / clip.w );
            top = std::max( top, clip.y / clip.w );
            CHECK( std::abs( clip.x / clip.w ) < 0.92f );
            CHECK( clip.z / clip.w > -1 );
            CHECK( clip.z / clip.w < 1 );
        }
        CHECK( bottom < -0.89f );
        CHECK( bottom > -0.92f );
        CHECK( top > 0.89f );
        CHECK( top < 0.92f );
        CHECK( std::abs( bottom + top ) < 0.0001f );
        CHECK( camera.boundsCenter.x == 26 );
        CHECK( camera.boundsCenter.y == 22 );
        CHECK( camera.boundsCenter.z == 1.875f );
        // Orbit keeps the newly framed screen-center pivot even though it is
        // offset from the geometric center used for clipping bounds.
        const auto pivot = math::Vec3_Add( camera.position, math::Vec3_Scale(
            tile::CypherTileCamera_Forward( camera ), camera.orbitDistance ) );
        tile::CypherTileCamera_SetMode( camera, tile::tile_camera_mode_t::ORBIT );
        tile::CypherTileCamera_ApplyLook( camera, 10, 5 );
        const auto after = math::Vec3_Add( camera.position, math::Vec3_Scale(
            tile::CypherTileCamera_Forward( camera ), camera.orbitDistance ) );
        CHECK( math::Vec3_NearlyEquals( pivot, after, 0.00001f, 0.00001f ) );
    }
}
