//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: examples/CypherRender/CypherRender_TileMap.cpp
//  Purpose: Previews authored Cypher tile maps through the public renderer.
//  Details: Loads a CYKV .cymap document, generates renderer-neutral floor and
//           wall boxes, and draws them with one shared cube mesh and CYSH shader.
//           The preview is a development bridge between CypherTileEditor and
//           the runtime renderer; it does not make the renderer understand maps.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon/FileSystem/CypherCommon_VfsDirectory.h"
#include "CypherCommon/Formats/CypherCommon_CookedMaterial.h"
#include "CypherCommon/Mathlib/CypherMath_Matrix4.h"
#include "CypherCommon/Tier1/CypherCommon_FileIo.h"
#include "CypherRender/CypherRender_Draw.h"
#include "CypherRender/CypherRender_Pipeline.h"
#include "CypherRender/CypherRender_Public.h"
#include "CypherRender/CypherRender_Shader.h"
#include "CypherSystem/CypherSystem_Public.h"
#include "CypherTools/CypherTileEditor/Core/CypherTileCamera.h"
#include "CypherTools/CypherTileEditor/Core/CypherTileMapGeometry.h"
#include "CypherTools/CypherTileEditor/Core/CypherTileMapMaterials.h"
#include "CypherTools/CypherTileEditor/Core/CypherTileMaterialPreview.h"
#include "CypherTools/CypherTileEditor/Core/CypherTileMapSerialization.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <new>
#include <numbers>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef CYPHER_RENDER_TILE_MAP_SHADER_PATH
    #define CYPHER_RENDER_TILE_MAP_SHADER_PATH "cooked/render_smoke/cube.cyshader_c"
#endif

#ifndef CYPHER_RENDER_TILE_MAP_PATH
    #define CYPHER_RENDER_TILE_MAP_PATH ""
#endif

namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;
namespace tile = ::cypher::tools::tile_editor;

namespace
{

// Preview geometry deliberately matches the first-draw shader contract. Four
// vertices per face preserve hard normals, and every generated box reuses this
// one immutable mesh through a model transform.
struct preview_vertex_t {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert( sizeof( preview_vertex_t ) == 32u );
static_assert( offsetof( preview_vertex_t, normal ) == 12u );
static_assert( offsetof( preview_vertex_t, uv ) == 24u );

constexpr preview_vertex_t PREVIEW_CUBE_VERTICES[] = {
    { { 1, -1, -1 }, { 1, 0, 0 }, { 0, 0 } },
    { { 1, 1, -1 }, { 1, 0, 0 }, { 1, 0 } },
    { { 1, 1, 1 }, { 1, 0, 0 }, { 1, 1 } },
    { { 1, -1, 1 }, { 1, 0, 0 }, { 0, 1 } },
    { { -1, 1, -1 }, { -1, 0, 0 }, { 0, 0 } },
    { { -1, -1, -1 }, { -1, 0, 0 }, { 1, 0 } },
    { { -1, -1, 1 }, { -1, 0, 0 }, { 1, 1 } },
    { { -1, 1, 1 }, { -1, 0, 0 }, { 0, 1 } },
    { { 1, 1, -1 }, { 0, 1, 0 }, { 0, 0 } },
    { { -1, 1, -1 }, { 0, 1, 0 }, { 1, 0 } },
    { { -1, 1, 1 }, { 0, 1, 0 }, { 1, 1 } },
    { { 1, 1, 1 }, { 0, 1, 0 }, { 0, 1 } },
    { { -1, -1, -1 }, { 0, -1, 0 }, { 0, 0 } },
    { { 1, -1, -1 }, { 0, -1, 0 }, { 1, 0 } },
    { { 1, -1, 1 }, { 0, -1, 0 }, { 1, 1 } },
    { { -1, -1, 1 }, { 0, -1, 0 }, { 0, 1 } },
    { { -1, -1, 1 }, { 0, 0, 1 }, { 0, 0 } },
    { { 1, -1, 1 }, { 0, 0, 1 }, { 1, 0 } },
    { { 1, 1, 1 }, { 0, 0, 1 }, { 1, 1 } },
    { { -1, 1, 1 }, { 0, 0, 1 }, { 0, 1 } },
    { { -1, 1, -1 }, { 0, 0, -1 }, { 0, 0 } },
    { { 1, 1, -1 }, { 0, 0, -1 }, { 1, 0 } },
    { { 1, -1, -1 }, { 0, 0, -1 }, { 1, 1 } },
    { { -1, -1, -1 }, { 0, 0, -1 }, { 0, 1 } }
};

constexpr common::u16 PREVIEW_CUBE_INDICES[] = {
    0, 1, 2, 0, 2, 3,       4, 5, 6, 4, 6, 7,
    8, 9, 10, 8, 10, 11,    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23
};

// The first 208 bytes match cube.cyshader_c. tile_surface.cyshader_c extends
// that std140 Transforms block with uvScale; both pipelines share this buffer.
struct alignas( 16 ) preview_transforms_t {
    math::mat4_t model;
    math::mat4_t view;
    math::mat4_t projection;
    alignas( 16 ) float tint[4];
    alignas( 16 ) float uvScale[4]{ 1.0f, 1.0f, 0.0f, 0.0f };
};
static_assert( sizeof( preview_transforms_t ) == 224u );
static_assert( offsetof( preview_transforms_t, view ) == 64u );
static_assert( offsetof( preview_transforms_t, projection ) == 128u );
static_assert( offsetof( preview_transforms_t, tint ) == 192u );
static_assert( offsetof( preview_transforms_t, uvScale ) == 208u );

struct preview_options_t {
    const char *pMapPath{ CYPHER_RENDER_TILE_MAP_PATH };
    const char *pShaderPath{ CYPHER_RENDER_TILE_MAP_SHADER_PATH };
    const char *pAssetRoot{ "" };
    common::u32 nFrameLimit{ 0u }; // Zero keeps the preview interactive.
    bool bHidden{ false };
    bool bReload{ true };
    bool bHelp{ false };
    tile::tile_camera_settings_t cameraSettings{};
    bool bOrbitCamera{ false };
};

struct map_snapshot_t {
    tile::tile_map_document_t document{};
    tile::tile_map_geometry_t geometry{};
};

// CLI and VFS identities are UTF-8. Convert explicitly at filesystem
// boundaries so dependency discovery and loading agree on Windows paths.
std::filesystem::path PathFromUtf8( std::string_view text )
{
    if ( text.empty() ) return {};
    const auto *begin = reinterpret_cast<const char8_t *>( text.data() );
    return std::filesystem::path( begin, begin + text.size() );
}

std::string PathToUtf8( const std::filesystem::path &path )
{
    const std::u8string text = path.u8string();
    return { reinterpret_cast<const char *>( text.data() ), text.size() };
}

struct material_file_stamp_t {
    std::filesystem::path path{};
    std::filesystem::file_time_type writeTime{};
    std::uintmax_t byteSize{ 0u };
    bool readable{ false };
    bool operator==( const material_file_stamp_t & ) const = default;
};

struct preview_state_t {
    sys::window_t window{};
    render::render_shader_handle_t shader{};
    render::render_pipeline_handle_t pipeline{};
    render::render_shader_handle_t materialShader{};
    render::render_pipeline_handle_t materialPipeline{};
    tile::tile_material_preview_set_t materials{};
    render::render_vertex_input_handle_t vertexInput{};
    render::render_buffer_handle_t vertices{};
    render::render_buffer_handle_t indices{};
    render::render_buffer_handle_t transforms{};

    std::unique_ptr<map_snapshot_t> pMap{};
    std::filesystem::file_time_type observedMapWriteTime{};
    bool bHasObservedMapWriteTime{ false };
    std::vector<material_file_stamp_t> observedMaterialFiles{};
    double nextReloadCheckSeconds{ 0.0 };
    tile::tile_camera_t camera{};
    tile::tile_camera_mode_t modeBeforeDrag{ tile::tile_camera_mode_t::FLY };
    std::array<bool, static_cast<std::size_t>( sys::sys_key_t::COUNT )> heldKeys{};
    sys::sys_key_modifiers_t modifiers{ sys::SYS_KEYMODIFIER_NONE };
    bool bDragging{ false };
    bool bPanning{ false };
    sys::sys_mouse_button_t navigationButton{};
    bool bRelativeMouse{ false };
    bool bAutoOrbit{ false };
};

bool CheckRender( render::render_error_t result, const char *pOperation ) noexcept
{
    if ( result == render::render_error_t::OK ) return true;
    std::fprintf( stderr, "%s: %s (%s)\n", pOperation,
        render::R_ErrorName( result ), render::R_ErrorDescription( result ) );
    return false;
}

bool CheckSystem( sys::sys_error_t result, const char *pOperation ) noexcept
{
    if ( result == sys::sys_error_t::OK ) return true;
    std::fprintf( stderr, "%s: %s\n", pOperation, sys::Sys_ErrorName( result ) );
    return false;
}

bool ParseOptions( int argc, char **argv, preview_options_t &options ) noexcept
{
    struct camera_option_t {
        const char *name;
        float *value;
        float minimum, maximum, scale;
    };
    const camera_option_t cameraOptions[]{
        { "--camera-speed", &options.cameraSettings.moveSpeed, 0.05f, 100000.0f, 1.0f },
        { "--camera-sensitivity", &options.cameraSettings.lookSensitivity, 0.01f, 5.0f,
          std::numbers::pi_v<float> / 180.0f },
        { "--camera-fov", &options.cameraSettings.verticalFovDegrees, 20.0f, 120.0f, 1.0f },
        { "--camera-pan-sensitivity", &options.cameraSettings.panSensitivity, 0.1f, 5.0f, 1.0f },
        { "--camera-zoom-sensitivity", &options.cameraSettings.zoomSensitivity, 0.1f, 5.0f, 1.0f },
        { "--camera-fast-multiplier", &options.cameraSettings.fastMultiplier, 1.0f, 20.0f, 1.0f },
        { "--camera-slow-multiplier", &options.cameraSettings.slowMultiplier, 0.01f, 1.0f, 1.0f }
    };
    for ( int iArgument = 1; iArgument < argc; ++iArgument ) {
        const auto *cameraOption = std::find_if( std::begin( cameraOptions ), std::end( cameraOptions ),
            [&]( const camera_option_t &option ) { return std::strcmp( argv[iArgument], option.name ) == 0; } );
        if ( std::strcmp( argv[iArgument], "--hidden" ) == 0 ) {
            options.bHidden = true;
        } else if ( std::strcmp( argv[iArgument], "--no-reload" ) == 0 ) {
            options.bReload = false;
        } else if ( std::strcmp( argv[iArgument], "--help" ) == 0 ) {
            options.bHelp = true;
        } else if ( std::strcmp( argv[iArgument], "--invert-y" ) == 0 ) {
            options.cameraSettings.invertMouseY = true;
        } else if ( std::strcmp( argv[iArgument], "--camera-invert-wheel" ) == 0 ) {
            options.cameraSettings.invertWheel = true;
        } else if ( std::strcmp( argv[iArgument], "--orbit-camera" ) == 0 ) {
            options.bOrbitCamera = true;
        } else if ( cameraOption != std::end( cameraOptions ) ) {
            const char *pOption = argv[iArgument];
            if ( iArgument + 1 >= argc ) {
                std::fprintf( stderr, "%s requires a numeric value.\n", pOption );
                return false;
            }
            const char *pFirst = argv[++iArgument];
            const char *pLast = pFirst + std::strlen( pFirst );
            float value{};
            const auto parsed = std::from_chars( pFirst, pLast, value );
            const float minimum = cameraOption->minimum;
            const float maximum = cameraOption->maximum;
            if ( parsed.ec != std::errc{} || parsed.ptr != pLast ||
                 !std::isfinite( value ) || value < minimum || value > maximum ) {
                std::fprintf( stderr, "%s requires a finite number in [%g, %g].\n",
                    pOption, static_cast<double>( minimum ), static_cast<double>( maximum ) );
                return false;
            }
            *cameraOption->value = value * cameraOption->scale;
        } else if ( std::strcmp( argv[iArgument], "--map" ) == 0 &&
                    iArgument + 1 < argc ) {
            options.pMapPath = argv[++iArgument];
        } else if ( std::strcmp( argv[iArgument], "--shader" ) == 0 &&
                    iArgument + 1 < argc ) {
            options.pShaderPath = argv[++iArgument];
        } else if ( std::strcmp( argv[iArgument], "--asset-root" ) == 0 &&
                    iArgument + 1 < argc ) {
            options.pAssetRoot = argv[++iArgument];
        } else if ( std::strcmp( argv[iArgument], "--frames" ) == 0 &&
                    iArgument + 1 < argc ) {
            const char *pFirst = argv[++iArgument];
            const char *pLast = pFirst + std::strlen( pFirst );
            const auto parsed = std::from_chars(
                pFirst, pLast, options.nFrameLimit );
            if ( parsed.ec != std::errc{} || parsed.ptr != pLast ||
                 options.nFrameLimit == 0u ) {
                std::fprintf( stderr,
                    "--frames requires a positive 32-bit integer.\n" );
                return false;
            }
        } else {
            std::fprintf( stderr, "Unknown option or missing value: %s\n",
                argv[iArgument] );
            return false;
        }
    }

    if ( options.bHelp ) return true;
    if ( options.pMapPath == nullptr || options.pMapPath[0] == '\0' ) {
        std::fprintf( stderr, "A source map is required; pass --map PATH.\n" );
        return false;
    }
    if ( options.pShaderPath == nullptr || options.pShaderPath[0] == '\0' ) {
        std::fprintf( stderr, "A cooked shader is required; pass --shader PATH.\n" );
        return false;
    }

    // Hidden invocations are build-smoke runs: finite and independent of wall
    // clock file changes. Four frames exercise resize-independent frame reuse.
    if ( options.bHidden ) {
        if ( options.nFrameLimit == 0u ) options.nFrameLimit = 4u;
        options.bReload = false;
    }
    return true;
}

void DestroyMapSnapshot( map_snapshot_t *pSnapshot ) noexcept
{
    if ( pSnapshot == nullptr ) return;
    if ( pSnapshot->geometry.pAllocator != nullptr ) {
        tile::CypherTileMapGeometry_Shutdown( &pSnapshot->geometry );
    }
    if ( tile::CypherTileMapDocument_IsInitialized( &pSnapshot->document ) ) {
        tile::CypherTileMapDocument_Shutdown( &pSnapshot->document );
    }
}

void PrintMapLoadFailure(
    const char *pMapPath,
    const tile::tile_map_serialization_result_t &result ) noexcept
{
    std::fprintf( stderr, "Load map %s: %s", pMapPath,
        tile::CypherTileMapSerialization_StatusName( result.status ) );
    if ( result.field[0] != '\0' ) std::fprintf( stderr, " at %s", result.field );
    if ( result.iElement != common::CY_INVALID_SIZE ) {
        std::fprintf( stderr, "[%zu]", static_cast<std::size_t>( result.iElement ) );
    }
    if ( result.location.nLine != 0u ) {
        std::fprintf( stderr, " (line %u, column %u)",
            result.location.nLine, result.location.nColumn );
    }
    std::fputc( '\n', stderr );
}

bool ValidateMapForPreview( const tile::tile_map_document_t &document )
{
    tile::tile_map_validation_report_t report{};
    const tile::tile_map_document_status_t initResult =
        tile::CypherTileMapValidationReport_Init(
            &report, common::Allocator_GetSystem() );
    if ( initResult != tile::tile_map_document_status_t::OK ) {
        std::fprintf( stderr, "Initialize map validation: %s\n",
            tile::CypherTileMapDocument_StatusName( initResult ) );
        return false;
    }

    const tile::tile_map_document_status_t validateResult =
        tile::CypherTileMapDocument_Validate( &document, &report );
    if ( validateResult != tile::tile_map_document_status_t::OK ) {
        std::fprintf( stderr, "Validate map: %s\n",
            tile::CypherTileMapDocument_StatusName( validateResult ) );
        tile::CypherTileMapValidationReport_Shutdown( &report );
        return false;
    }

    const bool bValid =
        tile::CypherTileMapValidationReport_IsValid( &report );
    bool bRenderable = true;
    if ( !bValid ) {
        for ( common::usize iDiagnostic = 0u;
              iDiagnostic < common::Vector_Count( &report.diagnostics );
              ++iDiagnostic ) {
            const tile::tile_map_validation_diagnostic_t &diagnostic =
                report.diagnostics.pData[iDiagnostic];
            std::fprintf( stderr, "Map authoring warning: %s at cell (%d, %d)\n",
                tile::CypherTileMapValidation_CodeName( diagnostic.code ),
                diagnostic.cell.x,
                diagnostic.cell.y );
            switch ( diagnostic.code ) {
                case tile::tile_map_validation_code_t::MISSING_PLAYER_SPAWN:
                case tile::tile_map_validation_code_t::DUPLICATE_PLAYER_SPAWN:
                case tile::tile_map_validation_code_t::PLAYER_SPAWN_OUT_OF_BOUNDS:
                case tile::tile_map_validation_code_t::PLAYER_SPAWN_OUTSIDE_FLOOR:
                    // Gameplay marker diagnostics should stay visible to the
                    // author, but they do not make floor/wall geometry unsafe.
                    break;
                default:
                    bRenderable = false;
                    break;
            }
        }
    }
    tile::CypherTileMapValidationReport_Shutdown( &report );
    return bRenderable;
}

std::unique_ptr<map_snapshot_t> LoadMapSnapshot( const char *pMapPath )
{
    const common::allocator_t *pAllocator = common::Allocator_GetSystem();
    common::blob_t source{};
    if ( !common::Blob_Init( &source, pAllocator ) ) {
        std::fprintf( stderr, "Could not initialize map-file storage.\n" );
        return {};
    }
    const common::string_view_t nativePath =
        common::StringView_FromCString( pMapPath );
    if ( !common::FileIo_ReadAllNative( nativePath, &source ) ) {
        std::fprintf( stderr, "Could not read source map: %s\n", pMapPath );
        common::Blob_Shutdown( &source );
        return {};
    }
    if ( source.cbSize > tile::TILE_MAP_SERIALIZATION_MAX_TEXT_BYTES ) {
        std::fprintf( stderr, "Source map exceeds the %zu-byte preview limit: %s\n",
            static_cast<std::size_t>( tile::TILE_MAP_SERIALIZATION_MAX_TEXT_BYTES ),
            pMapPath );
        common::Blob_Shutdown( &source );
        return {};
    }

    std::unique_ptr<map_snapshot_t> pPending{
        new ( std::nothrow ) map_snapshot_t{} };
    if ( !pPending ) {
        std::fprintf( stderr, "Could not allocate a map snapshot.\n" );
        common::Blob_Shutdown( &source );
        return {};
    }

    const common::string_view_t text{
        reinterpret_cast<const char *>( source.pData ), source.cbSize };
    const tile::tile_map_serialization_result_t loadResult =
        tile::CypherTileMapSerialization_LoadFromText(
            text, pAllocator, &pPending->document );
    common::Blob_Shutdown( &source );
    if ( loadResult.status != tile::tile_map_serialization_status_t::OK ) {
        PrintMapLoadFailure( pMapPath, loadResult );
        DestroyMapSnapshot( pPending.get() );
        return {};
    }

    if ( !ValidateMapForPreview( pPending->document ) ) {
        std::fprintf( stderr,
            "Map is a valid authoring document but is not preview-ready: %s\n",
            pMapPath );
        DestroyMapSnapshot( pPending.get() );
        return {};
    }

    const tile::tile_map_document_status_t geometryInit =
        tile::CypherTileMapGeometry_Init( &pPending->geometry, pAllocator );
    if ( geometryInit != tile::tile_map_document_status_t::OK ) {
        std::fprintf( stderr, "Initialize map geometry: %s\n",
            tile::CypherTileMapDocument_StatusName( geometryInit ) );
        DestroyMapSnapshot( pPending.get() );
        return {};
    }
    const tile::tile_map_document_status_t buildResult =
        tile::CypherTileMapGeometry_Build(
            &pPending->document, {}, &pPending->geometry );
    if ( buildResult != tile::tile_map_document_status_t::OK ) {
        std::fprintf( stderr, "Build map geometry: %s\n",
            tile::CypherTileMapDocument_StatusName( buildResult ) );
        DestroyMapSnapshot( pPending.get() );
        return {};
    }
    return pPending;
}

void PrintMapSummary( const map_snapshot_t &snapshot, const char *pMapPath ) noexcept
{
    const common::usize nFloors = tile::CypherTileMapGeometry_CountKind(
        &snapshot.geometry, tile::tile_map_geometry_box_kind_t::FLOOR );
    const common::usize nWalls = tile::CypherTileMapGeometry_CountKind(
        &snapshot.geometry, tile::tile_map_geometry_box_kind_t::WALL );
    const common::usize nDoors = tile::CypherTileMapGeometry_CountKind(
        &snapshot.geometry, tile::tile_map_geometry_box_kind_t::DOOR );
    const common::usize nSteps = tile::CypherTileMapGeometry_CountKind(
        &snapshot.geometry, tile::tile_map_geometry_box_kind_t::STAIR );
    std::printf( "Tile map: %s\nGrid: %u x %u, cell %.3f, level %.3f\n"
                 "Generated boxes: %zu floors + %zu walls + %zu doors + %zu steps = "
                 "%zu draws/frame\n",
        pMapPath,
        snapshot.document.nWidth,
        snapshot.document.nHeight,
        static_cast<double>( snapshot.document.nCellSize ),
        static_cast<double>( snapshot.document.nLevelHeight ),
        static_cast<std::size_t>( nFloors ),
        static_cast<std::size_t>( nWalls ),
        static_cast<std::size_t>( nDoors ),
        static_cast<std::size_t>( nSteps ),
        static_cast<std::size_t>( common::Vector_Count( &snapshot.geometry.boxes ) ) );
}

void UpdateCameraForMap( preview_state_t &state, bool bFrame ) noexcept
{
    const auto &geometry = state.pMap->geometry;
    const math::vec3_t minimum = geometry.bHasBounds
        ? math::vec3_t{ geometry.boundsMinX, geometry.boundsMinY, geometry.boundsMinZ }
        : math::vec3_t{ -2.0f, -2.0f, -1.0f };
    const math::vec3_t maximum = geometry.bHasBounds
        ? math::vec3_t{ geometry.boundsMaxX, geometry.boundsMaxY, geometry.boundsMaxZ }
        : math::vec3_t{ 2.0f, 2.0f, 1.0f };
    if ( bFrame ) {
        const float aspect = state.window.height != 0u
            ? static_cast<float>( state.window.width ) / static_cast<float>( state.window.height )
            : 1280.0f / 800.0f;
        tile::CypherTileCamera_FrameBounds( state.camera, minimum, maximum, aspect, true );
    } else {
        tile::CypherTileCamera_UpdateBounds( state.camera, minimum, maximum );
    }
}

bool ObserveMapWriteTime( preview_state_t &state,
    const preview_options_t &options ) noexcept
{
    std::error_code error{};
    const auto writeTime = std::filesystem::last_write_time(
        PathFromUtf8( options.pMapPath ), error );
    if ( error ) return false;
    state.observedMapWriteTime = writeTime;
    state.bHasObservedMapWriteTime = true;
    return true;
}

bool ReloadPreviewMaterials( preview_state_t &state,
    const tile::tile_map_document_t &document,
    const preview_options_t &options );

bool ReloadMap( preview_state_t &state,
    const preview_options_t &options,
    bool bForce )
{
    std::error_code error{};
    const auto writeTime = std::filesystem::last_write_time(
        PathFromUtf8( options.pMapPath ), error );
    if ( error ) {
        if ( bForce ) {
            std::fprintf( stderr, "Could not inspect source map: %s\n",
                options.pMapPath );
        }
        return false;
    }
    if ( !bForce && state.bHasObservedMapWriteTime &&
         writeTime == state.observedMapWriteTime ) {
        return true;
    }

    // Record the attempted version even when invalid. The preview keeps the last
    // valid snapshot and retries after the editor writes another file version.
    state.observedMapWriteTime = writeTime;
    state.bHasObservedMapWriteTime = true;
    std::unique_ptr<map_snapshot_t> pPending =
        LoadMapSnapshot( options.pMapPath );
    if ( !pPending ) {
        std::fprintf( stderr,
            "Map reload rejected; continuing with the last valid snapshot.\n" );
        return false;
    }
    if ( !ReloadPreviewMaterials( state, pPending->document, options ) ) {
        DestroyMapSnapshot( pPending.get() );
        std::fprintf( stderr,
            "Map material reload rejected; continuing with the last valid snapshot.\n" );
        return false;
    }

    std::unique_ptr<map_snapshot_t> pPrevious = std::move( state.pMap );
    state.pMap = std::move( pPending );
    // Changing geometry changes the far plane, never the inspection pose.
    // F is the explicit request to relocate and reframe the camera.
    UpdateCameraForMap( state, false );
    PrintMapSummary( *state.pMap, options.pMapPath );
    DestroyMapSnapshot( pPrevious.get() );
    std::printf( "Reloaded tile map.\n" );
    return true;
}

bool LoadShader( const char *pNativePath,
    render::render_shader_handle_t &shader )
{
    const std::filesystem::path path =
        std::filesystem::absolute( PathFromUtf8( pNativePath ) );
    const std::string root = PathToUtf8( path.parent_path() );
    const std::string filename = PathToUtf8( path.filename() );
    common::vfs_directory_t directory{};
    const common::vfs_status_t mountResult = common::VfsDirectory_Init(
        &directory, { root.data(), root.size() } );
    if ( mountResult != common::vfs_status_t::OK ) {
        std::fprintf( stderr, "Shader directory: %s (%s)\n", root.c_str(),
            common::Vfs_StatusName( mountResult ) );
        return false;
    }

    common::blob_t bytes{};
    if ( !common::Blob_Init( &bytes, common::Allocator_GetSystem() ) ) {
        common::VfsDirectory_Shutdown( &directory );
        std::fprintf( stderr, "Could not initialize cooked shader storage.\n" );
        return false;
    }
    const common::vfs_t vfs = common::VfsDirectory_Make( &directory );
    constexpr common::usize MAX_SHADER_FILE_BYTES = 40u * 1024u * 1024u;
    const common::vfs_status_t readResult = common::Vfs_ReadAll(
        &vfs, { filename.data(), filename.size() },
        MAX_SHADER_FILE_BYTES, &bytes );
    common::VfsDirectory_Shutdown( &directory );
    if ( readResult != common::vfs_status_t::OK ) {
        std::fprintf( stderr, "Read cooked shader %s: %s\n", pNativePath,
            common::Vfs_StatusName( readResult ) );
        return false;
    }

    common::cooked_shader_view_t view{};
    const common::cooked_shader_result_t cookedResult =
        common::CookedShader_Read( common::Blob_Block( &bytes ), &view );
    if ( !common::CookedShader_Succeeded( cookedResult ) ) {
        std::fprintf( stderr, "Invalid cooked shader %s: %s\n", pNativePath,
            common::CookedShader_StatusName( cookedResult.status ) );
        return false;
    }

    const render::render_shader_desc_t description{
        &view, "Cypher tile-map preview shader" };
    return CheckRender(
        render::R_CreateShader( description, &shader ), "Create shader" );
}

bool CreateBuffer( render::render_buffer_handle_t &buffer,
    const void *pBytes,
    common::u64 cbSize,
    render::render_buffer_usage_flags_t usage,
    bool bStream,
    const char *pName ) noexcept
{
    render::render_buffer_desc_t description{};
    description.byteSize = cbSize;
    description.usage = usage;
    description.updatePolicy = bStream
        ? render::render_buffer_update_t::STREAM
        : render::render_buffer_update_t::IMMUTABLE;
    description.memory = bStream
        ? render::render_buffer_memory_t::UPLOAD
        : render::render_buffer_memory_t::DEVICE_LOCAL;
    description.debugName = pName;
    const render::render_buffer_data_t data{ pBytes, cbSize };
    return CheckRender(
        render::R_CreateBuffer( description, &data, &buffer ), pName );
}

bool CreatePreviewResources( preview_state_t &state,
    const char *pShaderPath )
{
    if ( !LoadShader( pShaderPath, state.shader ) ) return false;
    const preview_transforms_t initial{
        math::CY_MAT4_IDENTITY,
        math::CY_MAT4_IDENTITY,
        math::CY_MAT4_IDENTITY,
        { 1.0f, 1.0f, 1.0f, 1.0f }
    };
    if ( !CreateBuffer( state.vertices,
            PREVIEW_CUBE_VERTICES, sizeof( PREVIEW_CUBE_VERTICES ),
            render::R_BUFFER_USAGE_VERTEX, false,
            "Tile preview shared cube vertices" ) ||
         !CreateBuffer( state.indices,
            PREVIEW_CUBE_INDICES, sizeof( PREVIEW_CUBE_INDICES ),
            render::R_BUFFER_USAGE_INDEX, false,
            "Tile preview shared cube indices" ) ||
         !CreateBuffer( state.transforms,
            &initial, sizeof( initial ),
            render::R_BUFFER_USAGE_UNIFORM |
                render::R_BUFFER_USAGE_TRANSFER_DESTINATION,
            true, "Tile preview transform stream" ) ) {
        return false;
    }

    render::render_vertex_input_desc_t input{};
    input.layout.bindingCount = 1u;
    input.layout.bindings[0].stride = sizeof( preview_vertex_t );
    input.layout.attributeCount = 3u;
    input.layout.attributes[0] = {
        render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    input.layout.attributes[1] = {
        render::render_format_t::RGB32_FLOAT, 12u, 1u, 0u };
    input.layout.attributes[2] = {
        render::render_format_t::RG32_FLOAT, 24u, 2u, 0u };
    input.vertexBufferCount = 1u;
    input.vertexBuffers[0] = { state.vertices, 0u, 0u };
    input.indexBuffer = {
        state.indices, 0u, render::render_index_type_t::UINT16 };
    input.debugName = "Tile preview shared cube input";
    if ( !CheckRender(
            render::R_CreateVertexInput( input, &state.vertexInput ),
            "Create tile preview vertex input" ) ) {
        return false;
    }

    render::render_pipeline_desc_t pipeline{};
    pipeline.shader = state.shader;
    pipeline.vertexLayout = input.layout;
    pipeline.depthTest = true;
    pipeline.depthWrite = true;
    pipeline.cullBackFaces = true;
    pipeline.frontCounterClockwise = true;
    pipeline.alphaBlend = false;
    pipeline.uniformBlockName = "Transforms";
    pipeline.uniformBlockBytes = offsetof( preview_transforms_t, uvScale );
    pipeline.debugName = "Tile-map blockout pipeline";
    return CheckRender(
        render::R_CreateGraphicsPipeline( pipeline, &state.pipeline ),
        "Create tile-map pipeline" );
}

material_file_stamp_t InspectMaterialFile( const std::filesystem::path &path )
{
    material_file_stamp_t stamp{};
    stamp.path = path;
    std::error_code error;
    stamp.writeTime = std::filesystem::last_write_time( path, error );
    if ( error ) { stamp.writeTime = {}; return stamp; }
    stamp.byteSize = std::filesystem::file_size( path, error );
    if ( error ) { stamp.writeTime = {}; stamp.byteSize = 0u; return stamp; }
    stamp.readable = true;
    return stamp;
}

// Discover texture dependencies only when attempting a reload. Steady-state
// polling below stats the small dependency list without reading image payloads.
void ObserveMaterialFiles( preview_state_t &state,
    const tile::tile_map_document_t &document, const preview_options_t &options )
{
    std::vector<std::filesystem::path> paths;
    std::vector<material_file_stamp_t> materialReadStamps;
    const auto count = common::Vector_Count( &document.materialBindings );
    const std::filesystem::path root = PathFromUtf8( options.pAssetRoot );
    if ( count != 0u && !root.empty() ) {
        paths.push_back( root / "shaders/tile_surface.cyshader_c" );
        struct directory_scope_t {
            common::vfs_directory_t directory{};
            ~directory_scope_t() { common::VfsDirectory_Shutdown( &directory ); }
        } mounted;
        const std::string nativeRoot = PathToUtf8( root );
        const bool available = common::VfsDirectory_Init( &mounted.directory,
            { nativeRoot.data(), nativeRoot.size() } ) == common::vfs_status_t::OK;
        const auto vfs = common::VfsDirectory_Make( &mounted.directory );
        for ( common::usize i = 0u; i < count; ++i ) {
            const std::string resource = std::string( document.materialBindings.pData[i].path ) + "_c";
            paths.push_back( root / PathFromUtf8( resource ) );
            // Capture before decoding, so a material rewritten during dependency
            // discovery still triggers another pass with its new texture paths.
            materialReadStamps.push_back( InspectMaterialFile( paths.back() ) );
            if ( !available ) continue;
            common::blob_t bytes{};
            if ( !common::Blob_Init( &bytes, common::Allocator_GetSystem() ) ||
                 common::Vfs_ReadAll( &vfs, { resource.data(), resource.size() },
                     common::CY_MIB, &bytes ) != common::vfs_status_t::OK ) continue;
            common::cooked_material_view_t material{};
            if ( !common::CookedMaterial_Succeeded(
                    common::CookedMaterial_Read( common::Blob_Block( &bytes ), &material ) ) ) continue;
            for ( common::u32 j = 0u; j < material.nTextures; ++j ) {
                const auto &texture = material.textures[j].texture;
                paths.push_back( root / PathFromUtf8( std::string( texture.pData, texture.cchLength ) + "_c" ) );
            }
        }
    }
    std::sort( paths.begin(), paths.end() );
    paths.erase( std::unique( paths.begin(), paths.end() ), paths.end() );
    std::vector<material_file_stamp_t> observed;
    observed.reserve( paths.size() );
    for ( const auto &path : paths ) {
        const auto decoded = std::find_if( materialReadStamps.begin(), materialReadStamps.end(),
            [&path]( const auto &stamp ) { return stamp.path == path; } );
        observed.push_back( decoded != materialReadStamps.end() ? *decoded : InspectMaterialFile( path ) );
    }
    state.observedMaterialFiles.swap( observed );
}

bool MaterialFilesChanged( const preview_state_t &state )
{
    for ( const auto &previous : state.observedMaterialFiles ) {
        if ( InspectMaterialFile( previous.path ) != previous ) return true;
    }
    return false;
}

bool ReloadPreviewMaterials( preview_state_t &state,
    const tile::tile_map_document_t &document,
    const preview_options_t &options )
{
    // Record attempted versions before reading/uploading. A failed version logs
    // once, retains all previous GPU resources, and retries when a file changes.
    // New dependencies are watched even if their files have not been cooked yet.
    ObserveMaterialFiles( state, document, options );
    render::render_shader_handle_t pendingShader{};
    render::render_pipeline_handle_t pendingPipeline{};
    const std::filesystem::path root = PathFromUtf8( options.pAssetRoot );
    if ( common::Vector_Count( &document.materialBindings ) != 0u ) {
        if ( root.empty() ) {
            std::fprintf( stderr, "Map has material bindings; pass --asset-root COOKED_DIRECTORY.\n" );
            return false;
        }
        const std::string shaderPath = PathToUtf8( root / "shaders/tile_surface.cyshader_c" );
        if ( !LoadShader( shaderPath.c_str(), pendingShader ) ) return false;
        render::render_pipeline_info_t blockout{};
        if ( !CheckRender( render::R_GetGraphicsPipelineInfo( state.pipeline, &blockout ),
                "Inspect tile-map vertex layout" ) ) {
            (void)render::R_DestroyShader( pendingShader );
            return false;
        }
        render::render_pipeline_desc_t description{};
        description.shader = pendingShader;
        description.vertexLayout = blockout.vertexLayout;
        description.uniformBlockName = "Transforms";
        description.uniformBlockBytes = sizeof( preview_transforms_t );
        description.sampledTextureName = "base_color";
        description.debugName = "Tile-map textured material pipeline";
        if ( !CheckRender( render::R_CreateGraphicsPipeline( description, &pendingPipeline ),
                "Create tile-map material pipeline" ) ) {
            (void)render::R_DestroyShader( pendingShader );
            return false;
        }
    }
    tile::tile_material_preview_set_t pendingMaterials{};
    std::string error;
    if ( !tile::CypherTileMaterialPreview_Reload( pendingMaterials, document, root, error ) ) {
        if ( pendingPipeline.value != 0u ) (void)render::R_DestroyGraphicsPipeline( pendingPipeline );
        if ( pendingShader.value != 0u ) (void)render::R_DestroyShader( pendingShader );
        std::fprintf( stderr, "Tile-map materials: %s\n", error.c_str() );
        return false;
    }
    state.materials.records.swap( pendingMaterials.records );
    tile::CypherTileMaterialPreview_Shutdown( pendingMaterials );
    if ( state.materialPipeline.value != 0u ) (void)render::R_DestroyGraphicsPipeline( state.materialPipeline );
    if ( state.materialShader.value != 0u ) (void)render::R_DestroyShader( state.materialShader );
    state.materialPipeline = pendingPipeline;
    state.materialShader = pendingShader;
    if ( !state.materials.records.empty() ) {
        std::printf( "Loaded %zu cooked material slots from %s.\n",
            state.materials.records.size(), options.pAssetRoot );
    }
    return true;
}

void ReleaseNavigation( preview_state_t &state ) noexcept
{
    if ( state.bRelativeMouse ) {
        (void)CheckSystem( sys::Sys_SetRelativeMouseMode( state.window, false ),
            "Release preview mouse capture" );
        state.bRelativeMouse = false;
    }
    if ( state.bDragging ) tile::CypherTileCamera_SetMode( state.camera, state.modeBeforeDrag );
    state.bDragging = false;
    state.bPanning = false;
    state.navigationButton = sys::sys_mouse_button_t::NONE;
    state.heldKeys.fill( false );
    state.modifiers = sys::SYS_KEYMODIFIER_NONE;
}

bool Held( const preview_state_t &state, sys::sys_key_t key ) noexcept
{
    return state.heldKeys[static_cast<std::size_t>( key )];
}

void CameraAtPlayerSpawn( preview_state_t &state ) noexcept
{
    const auto &document = state.pMap->document;
    const auto *pSpawn = tile::CypherTileMapDocument_PlayerSpawn( &document );
    const auto *pCell = pSpawn == nullptr ? nullptr :
        tile::CypherTileMapDocument_CellAt( &document, pSpawn->cell );
    if ( pCell == nullptr || pCell->shape != tile::tile_map_cell_shape_t::FLAT ||
         !tile::CypherTileMapDocument_CellHasFloor( &document, pSpawn->cell ) ) {
        std::puts( "Camera at spawn requires a player spawn on a flat floor." );
        return;
    }
    ReleaseNavigation( state );
    state.bAutoOrbit = false;
    tile::CypherTileCamera_SetMode( state.camera, tile::tile_camera_mode_t::FLY );
    state.camera.position = {
        ( static_cast<float>( pSpawn->cell.x ) + 0.5f ) * document.nCellSize,
        ( static_cast<float>( pSpawn->cell.y ) + 0.5f ) * document.nCellSize,
        static_cast<float>( pCell->nFloorLevel ) * document.nLevelHeight +
            std::min( document.nLevelHeight * 0.6f, 1.7f )
    };
    state.camera.yawRadians = pSpawn->yawDegrees * std::numbers::pi_v<float> / 180.0f;
    state.camera.pitchRadians = 0.0f;
    std::puts( "Camera at player spawn; hold right mouse to navigate." );
}

void HandleInput( preview_state_t &state,
    const preview_options_t &options,
    const sys::sys_event_t &event )
{
    if ( event.type == sys::sys_event_type_t::QUIT_REQUESTED ) {
        ReleaseNavigation( state );
        sys::Sys_RequestQuit();
        return;
    }
    if ( event.type == sys::sys_event_type_t::WINDOW_FOCUS_LOST ||
         event.type == sys::sys_event_type_t::WINDOW_MINIMIZED ) {
        ReleaseNavigation( state );
        return;
    }
    if ( event.type == sys::sys_event_type_t::KEY ) {
        const auto &key = event.payload.key;
        state.modifiers = key.modifiers;
        const auto index = static_cast<std::size_t>( key.key );
        if ( index < state.heldKeys.size() ) {
            state.heldKeys[index] = key.action != sys::sys_input_action_t::RELEASED;
        }
        if ( key.action != sys::sys_input_action_t::PRESSED ) return;
        switch ( key.key ) {
            case sys::sys_key_t::ESCAPE:
                if ( state.bDragging || state.bPanning ) ReleaseNavigation( state );
                else sys::Sys_RequestQuit();
                break;
            case sys::sys_key_t::F:
                UpdateCameraForMap( state, true );
                break;
            case sys::sys_key_t::R:
                (void)ReloadMap( state, options, true );
                break;
            case sys::sys_key_t::G:
                CameraAtPlayerSpawn( state );
                break;
            case sys::sys_key_t::TAB:
                ReleaseNavigation( state );
                tile::CypherTileCamera_SetMode( state.camera,
                    state.camera.mode == tile::tile_camera_mode_t::FLY
                        ? tile::tile_camera_mode_t::ORBIT : tile::tile_camera_mode_t::FLY );
                std::printf( "Camera: %s\n", state.camera.mode == tile::tile_camera_mode_t::FLY
                    ? "Fly (hold right mouse to navigate)" : "Orbit" );
                break;
            case sys::sys_key_t::SPACE:
                state.bAutoOrbit = !state.bAutoOrbit;
                break;
            default:
                break;
        }
        return;
    }
    if ( event.type == sys::sys_event_type_t::MOUSE_BUTTON ) {
        const auto &button = event.payload.mouseButton;
        const bool bPressed = button.action == sys::sys_input_action_t::PRESSED;
        if ( button.button == sys::sys_mouse_button_t::RIGHT ||
             button.button == sys::sys_mouse_button_t::MIDDLE ) {
            if ( bPressed ) {
                ReleaseNavigation( state );
                state.navigationButton = button.button;
                state.modeBeforeDrag = state.camera.mode;
                state.bPanning = button.button == sys::sys_mouse_button_t::MIDDLE ||
                    ( button.modifiers & sys::SYS_KEYMODIFIER_SHIFT ) != 0u;
                if ( !state.bPanning && ( button.modifiers & sys::SYS_KEYMODIFIER_ALT ) != 0u ) {
                    tile::CypherTileCamera_SetMode( state.camera, tile::tile_camera_mode_t::ORBIT );
                }
                // Relative input remains usable at screen edges. If the backend
                // rejects capture, retain ordinary drag deltas as a fallback.
                state.bRelativeMouse = !state.bPanning && CheckSystem(
                    sys::Sys_SetRelativeMouseMode( state.window, true ), "Capture preview mouse" );
                state.bDragging = !state.bPanning;
                state.bAutoOrbit = false;
                state.modifiers = button.modifiers;
            } else if ( button.button == state.navigationButton ) ReleaseNavigation( state );
        }
        return;
    }
    if ( event.type == sys::sys_event_type_t::MOUSE_MOTION ) {
        if ( state.bDragging ) {
            tile::CypherTileCamera_ApplyLook( state.camera,
                event.payload.mouseMotion.deltaX, event.payload.mouseMotion.deltaY );
        } else if ( state.bPanning ) {
            tile::CypherTileCamera_Pan( state.camera,
                event.payload.mouseMotion.deltaX, event.payload.mouseMotion.deltaY,
                static_cast<float>( state.window.height ) );
        }
        return;
    }
    if ( event.type == sys::sys_event_type_t::MOUSE_WHEEL ) {
        tile::CypherTileCamera_Wheel( state.camera, event.payload.mouseWheel.y );
        if ( state.camera.mode == tile::tile_camera_mode_t::FLY ) {
            std::printf( "Camera speed: %.2f units/s\n", static_cast<double>( state.camera.settings.moveSpeed ) );
        }
    }
}

bool BuildCameraMatrices( const tile::tile_camera_t &camera,
    render::render_extent_t extent,
    math::mat4_t &viewOut,
    math::mat4_t &projectionOut ) noexcept
{
    if ( extent.width == 0u || extent.height == 0u ) return false;
    return tile::CypherTileCamera_BuildMatrices( camera,
        static_cast<float>( extent.width ) / static_cast<float>( extent.height ),
        viewOut, projectionOut );
}

bool DrawMap( preview_state_t &state,
    const math::mat4_t &view,
    const math::mat4_t &projection ) noexcept
{
    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = state.pipeline;
    draw.vertexInput = state.vertexInput;
    draw.uniformBuffer = state.transforms;
    draw.indexCount = 36u;
    draw.vertexCount = 24u;

    const common::usize nBoxes =
        common::Vector_Count( &state.pMap->geometry.boxes );
    for ( common::usize iBox = 0u; iBox < nBoxes; ++iBox ) {
        const tile::tile_map_geometry_box_t &box =
            state.pMap->geometry.boxes.pData[iBox];
        preview_transforms_t transforms{};
        transforms.model = math::Mat4_Multiply(
            math::Mat4_FromTranslation(
                { box.centerX, box.centerY, box.centerZ } ),
            math::Mat4_FromScale(
                { box.halfExtentX, box.halfExtentY, box.halfExtentZ } ) );
        transforms.view = view;
        transforms.projection = projection;
        const tile::tile_map_material_definition_t material =
            tile::CypherTileMapMaterial_Resolve( box.nMaterialSlot );
        const bool bDoor = box.kind ==
            tile::tile_map_geometry_box_kind_t::DOOR;
        const float kindScale = box.kind ==
            tile::tile_map_geometry_box_kind_t::WALL ? 0.82f : 1.0f;
        transforms.tint[0] = bDoor ? 0.92f : material.colorR * kindScale;
        transforms.tint[1] = bDoor ? 0.39f : material.colorG * kindScale;
        transforms.tint[2] = bDoor ? 0.10f : material.colorB * kindScale;
        transforms.tint[3] = 1.0f;
        const auto *boundMaterial = bDoor ? nullptr :
            tile::CypherTileMaterialPreview_Find( state.materials, box.nMaterialSlot );
        draw.pipeline = boundMaterial != nullptr ? state.materialPipeline : state.pipeline;
        draw.sampledTexture = boundMaterial != nullptr ? boundMaterial->texture :
            render::R_INVALID_TEXTURE;
        if ( boundMaterial != nullptr ) {
            for ( unsigned int component = 0u; component < 3u; ++component ) {
                transforms.tint[component] = boundMaterial->tint[component] * kindScale;
            }
            transforms.tint[3] = boundMaterial->tint[3];
            transforms.uvScale[0] = boundMaterial->uvScale[0];
            transforms.uvScale[1] = boundMaterial->uvScale[1];
        }
        if ( !CheckRender( render::R_UpdateBuffer(
                 state.transforms, 0u,
                 { &transforms, sizeof( transforms ) } ),
                 "Upload tile-map box transform" ) ||
             !CheckRender( render::R_DrawIndexed( draw ),
                 "Draw tile-map box" ) ) {
            return false;
        }
    }
    return true;
}

bool RunFrames( preview_state_t &state,
    const preview_options_t &options )
{
    const double started = sys::Sys_TimeNowSeconds();
    double previous = started;
    const double timeout = std::max(
        30.0, 10.0 + options.nFrameLimit / 15.0 );
    common::u32 nRenderedFrames = 0u;
    render::render_extent_t extent = render::R_GetInfo()->drawableExtent;

    while ( !sys::Sys_IsQuitRequested() &&
            !sys::Sys_WindowShouldClose( state.window ) ) {
        sys::Sys_PollWindowEvents( state.window );
        sys::sys_event_t event{};
        while ( sys::Sys_PollEvent( event ) ) {
            HandleInput( state, options, event );
        }
        if ( sys::Sys_IsQuitRequested() ||
             sys::Sys_WindowShouldClose( state.window ) ) {
            break;
        }

        const double now = sys::Sys_TimeNowSeconds();
        const float deltaSeconds = static_cast<float>(
            std::clamp( now - previous, 0.0, 0.1 ) );
        previous = now;
        if ( options.nFrameLimit != 0u && now - started > timeout ) {
            std::fprintf( stderr, "Timed out before completing %u frames.\n",
                options.nFrameLimit );
            return false;
        }
        if ( options.bReload && now >= state.nextReloadCheckSeconds ) {
            (void)ReloadMap( state, options, false );
            if ( MaterialFilesChanged( state ) ) {
                // A failed source map can prevent material loading altogether.
                // Acknowledge this attempt before re-reading it to avoid a
                // repeated error every poll until an input actually changes.
                for ( auto &observed : state.observedMaterialFiles ) {
                    observed = InspectMaterialFile( observed.path );
                }
                if ( ReloadMap( state, options, true ) ) {
                    std::printf( "Reloaded cooked map materials; camera preserved.\n" );
                } else {
                    std::fprintf( stderr,
                        "Cooked asset reload rejected; continuing with the last valid materials.\n" );
                }
            }
            state.nextReloadCheckSeconds = now + 0.5;
        }
        if ( state.window.minimized || state.window.width == 0u ||
             state.window.height == 0u ) {
            sys::Sys_SleepMilliseconds( 16u );
            continue;
        }
        if ( extent.width != state.window.width ||
             extent.height != state.window.height ) {
            extent = { state.window.width, state.window.height };
            if ( !CheckRender( render::R_Resize( extent ),
                    "Resize tile-map preview" ) ) {
                return false;
            }
        }

        if ( state.bDragging && state.camera.mode == tile::tile_camera_mode_t::FLY ) {
            tile::tile_camera_input_t input{};
            input.forward = static_cast<float>( Held( state, sys::sys_key_t::W ) ) -
                static_cast<float>( Held( state, sys::sys_key_t::S ) );
            input.right = static_cast<float>( Held( state, sys::sys_key_t::D ) ) -
                static_cast<float>( Held( state, sys::sys_key_t::A ) );
            input.up = static_cast<float>( Held( state, sys::sys_key_t::E ) ) -
                static_cast<float>( Held( state, sys::sys_key_t::Q ) );
            input.fast = ( state.modifiers & sys::SYS_KEYMODIFIER_SHIFT ) != 0u;
            input.slow = ( state.modifiers & sys::SYS_KEYMODIFIER_CONTROL ) != 0u;
            (void)tile::CypherTileCamera_Move( state.camera, input, deltaSeconds );
        }
        if ( state.bAutoOrbit ) {
            const auto previousMode = state.camera.mode;
            tile::CypherTileCamera_SetMode( state.camera, tile::tile_camera_mode_t::ORBIT );
            const float step = options.nFrameLimit == 0u ? deltaSeconds : 1.0f / 60.0f;
            tile::CypherTileCamera_ApplyLook( state.camera,
                -step * 0.16f / state.camera.settings.lookSensitivity, 0.0f );
            tile::CypherTileCamera_SetMode( state.camera, previousMode );
        }
        math::mat4_t view{};
        math::mat4_t projection{};
        if ( !BuildCameraMatrices(
                state.camera, extent, view, projection ) ) {
            std::fprintf( stderr,
                "Could not construct tile-map preview camera matrices.\n" );
            return false;
        }

        render::render_frame_info_t frame{};
        frame.frameIndex = nRenderedFrames;
        frame.deltaSeconds = deltaSeconds;
        frame.drawableExtent = extent;
        frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
        frame.clearColor = { 0.025f, 0.032f, 0.048f, 1.0f };
        if ( !CheckRender( render::R_BeginFrame( frame ), "Begin frame" ) ) {
            return false;
        }
        const bool bDrawOK = DrawMap( state, view, projection );
        const bool bEndOK = CheckRender(
            render::R_EndFrame(), "End frame" );
        if ( !bDrawOK || !bEndOK ) return false;

        ++nRenderedFrames;
        if ( options.nFrameLimit != 0u &&
             nRenderedFrames >= options.nFrameLimit ) {
            break;
        }
    }

    std::printf( "Rendered %u tile-map preview frames.\n", nRenderedFrames );
    return options.nFrameLimit == 0u ||
           nRenderedFrames == options.nFrameLimit;
}

bool Shutdown( preview_state_t &state ) noexcept
{
    bool bSuccess = true;
    if ( state.window.valid ) ReleaseNavigation( state );
    if ( render::R_IsInitialized() ) {
        if ( render::R_IsFrameActive() ) {
            bSuccess = CheckRender(
                render::R_EndFrame(), "Finish active frame" ) && bSuccess;
        }
        bSuccess = CheckRender(
            render::R_WaitIdle(), "Wait for renderer" ) && bSuccess;
        tile::CypherTileMaterialPreview_Shutdown( state.materials );
        if ( state.materialPipeline.value != 0u ) {
            bSuccess = CheckRender(
                render::R_DestroyGraphicsPipeline( state.materialPipeline ),
                "Destroy tile-map material pipeline" ) && bSuccess;
        }
        if ( state.pipeline.value != 0u ) {
            bSuccess = CheckRender(
                render::R_DestroyGraphicsPipeline( state.pipeline ),
                "Destroy tile-map pipeline" ) && bSuccess;
        }
        if ( state.vertexInput.value != 0u ) {
            bSuccess = CheckRender(
                render::R_DestroyVertexInput( state.vertexInput ),
                "Destroy tile-map vertex input" ) && bSuccess;
        }
        for ( const auto buffer : {
                  state.transforms, state.indices, state.vertices } ) {
            if ( buffer.value != 0u ) {
                bSuccess = CheckRender(
                    render::R_DestroyBuffer( buffer ),
                    "Destroy tile-map buffer" ) && bSuccess;
            }
        }
        if ( state.shader.value != 0u ) {
            bSuccess = CheckRender(
                render::R_DestroyShader( state.shader ),
                "Destroy tile-map shader" ) && bSuccess;
        }
        if ( state.materialShader.value != 0u ) {
            bSuccess = CheckRender(
                render::R_DestroyShader( state.materialShader ),
                "Destroy tile-map material shader" ) && bSuccess;
        }
        bSuccess = CheckRender(
            render::R_Shutdown(), "Shut down renderer" ) && bSuccess;
    }
    if ( state.window.valid ) {
        bSuccess = CheckSystem(
            sys::Sys_DestroyWindow( state.window ),
            "Destroy tile-map window" ) && bSuccess;
    }
    if ( sys::Sys_IsInitialized() ) {
        bSuccess = CheckSystem(
            sys::Sys_Shutdown(), "Shut down System" ) && bSuccess;
    }
    DestroyMapSnapshot( state.pMap.get() );
    state.pMap.reset();
    return bSuccess;
}

bool Run( int argc, char **argv,
    const preview_options_t &options,
    preview_state_t &state )
{
    state.pMap = LoadMapSnapshot( options.pMapPath );
    if ( !state.pMap ) return false;
    tile::CypherTileCamera_SetSettings( state.camera, options.cameraSettings );
    tile::CypherTileCamera_SetMode( state.camera, options.bOrbitCamera
        ? tile::tile_camera_mode_t::ORBIT : tile::tile_camera_mode_t::FLY );
    UpdateCameraForMap( state, true );
    PrintMapSummary( *state.pMap, options.pMapPath );
    (void)ObserveMapWriteTime( state, options );

    const sys::init_info_t init{
        argc, argv, "CypherTileMapPreview", "CypherEngine" };
    if ( !CheckSystem( sys::Sys_Init( init ), "Initialize System" ) ) {
        return false;
    }

    render::render_config_t config = render::R_DefaultConfig();
    config.backend = render::render_backend_t::OPENGL;
    config.presentMode = options.bHidden
        ? render::render_present_mode_t::IMMEDIATE
        : render::render_present_mode_t::FIFO;
    config.sRGBFramebuffer = false;
    config.requireAcceleration = false;
    config.optionalCapabilities = render::R_CAPABILITY_NONE;

    sys::window_desc_t window{};
    window.title = "CypherTileEditor | Runtime Map Preview";
    window.width = 1280u;
    window.height = 800u;
    if ( options.bHidden ) window.flags |= sys::SYS_WINDOW_HIDDEN;
    if ( !CheckRender(
            render::R_ConfigureWindow( config, window ), "Configure window" ) ||
         !CheckSystem(
            sys::Sys_CreateWindow( window, state.window ), "Create window" ) ||
         !CheckRender(
            render::R_Init( state.window, config ), "Initialize renderer" ) ||
         !CreatePreviewResources( state, options.pShaderPath ) ||
         !ReloadPreviewMaterials( state, state.pMap->document, options ) ) {
        return false;
    }

    UpdateCameraForMap( state, true );

    std::printf( "Cooked shader: %s\n"
                 "Hold RMB: mouse look + WASD, Q/E down/up, Shift fast, Ctrl slow.\n"
                 "Alt+RMB: orbit. MMB or Shift+RMB: pan. Wheel: fly speed/orbit zoom.\n"
                 "Tab switches Fly/Orbit, F frames, G goes to player spawn.\n"
                 "R reloads; map and cooked material changes reload automatically. Space auto-orbits.\n"
                 "Esc releases capture first, then exits.\n",
        options.pShaderPath );
    return RunFrames( state, options );
}

} // namespace

int main( int argc, char **argv )
{
    // QProcess captures stdout through a pipe, which otherwise makes the C
    // runtime block-buffer normal status messages until the preview exits.
    // Line buffering keeps the editor console synchronized with map reloads.
    (void)std::setvbuf( stdout, nullptr, _IOLBF, 0u );

    preview_options_t options{};
    if ( !ParseOptions( argc, argv, options ) ) return 2;
    if ( options.bHelp ) {
        std::puts(
            "Usage: cypher_tile_map_preview --map PATH [options]\n"
            "\n"
            "Options:\n"
            "  --shader PATH  Read this cooked .cyshader_c program.\n"
            "  --asset-root DIR        Cooked assets for bound .cymat/.cytex resources.\n"
            "  --frames N     Render exactly N deterministic frames, then exit.\n"
            "  --hidden       Hide the window; defaults to four frames.\n"
            "  --no-reload    Disable map and cooked material timestamp monitoring.\n"
            "  --camera-speed N        Movement in world units/s (0.05..100000).\n"
            "  --camera-sensitivity N  Mouse degrees/pixel (0.01..5).\n"
            "  --camera-fov N          Vertical field of view (20..120 degrees).\n"
            "  --camera-pan-sensitivity N   Pan multiplier (0.1..5).\n"
            "  --camera-zoom-sensitivity N  Wheel multiplier (0.1..5).\n"
            "  --camera-fast-multiplier N   Shift movement multiplier (1..20).\n"
            "  --camera-slow-multiplier N   Ctrl movement multiplier (0.01..1).\n"
            "  --camera-invert-wheel       Reverse wheel zoom/speed direction.\n"
            "  --invert-y              Invert mouse look vertically.\n"
            "  --orbit-camera          Start in orbit mode instead of fly.\n"
            "  --help                  Show this help text.\n"
            "\n"
            "Controls: hold RMB for mouse-look + WASD; Q/E world down/up,\n"
            "Press Shift after RMB for fast fly, Ctrl for slow. Alt+RMB orbit,\n"
            "MMB or Shift+RMB pan, Tab switch mode.\n"
            "Wheel changes fly speed/orbit zoom; F frames, G goes to spawn, R reloads,\n"
            "Space toggles auto-orbit; Escape releases capture, then quits." );
        return 0;
    }

    preview_state_t state{};
    bool bSuccess = false;
    try {
        bSuccess = Run( argc, argv, options, state );
    } catch ( const std::exception &error ) {
        std::fprintf( stderr, "Tile-map preview: %s\n", error.what() );
    }
    const bool bShutdownOK = Shutdown( state );
    return bSuccess && bShutdownOK ? 0 : 1;
}
