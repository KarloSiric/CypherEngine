//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Compile.cpp
//  Purpose: Implements Compile Map over the geometry cook pipeline.
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Compile.h"
#include "CypherGeometry_Snapshot.h"
#include "CypherGeometry_CookSurfaces.h"
#include "CypherGeometry_CookKeys.h"
#include "CypherGeometry_CookCollision.h"
#include "CypherGeometry_CookVisibility.h"
#include "CypherGeometry_CookNavigation.h"
#include "CypherGeometry_CookLighting.h"
#include "CypherGeometry_CookBatching.h"
#include "CypherGeometry_CompilerInterchange.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

#include <chrono>
#include <cmath>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;

namespace
{
// Why the cook skipped an object, in words a mapper can act on.
const char *Reason( geo::geometry_status_t status ) noexcept
{
    switch ( status ) {
        case geo::geometry_status_t::NON_MANIFOLD: return "non-manifold (an edge shared by more than two faces)";
        case geo::geometry_status_t::DEGENERATE: return "degenerate (a face with no area)";
        case geo::geometry_status_t::NON_PLANAR: return "a face that is not flat";
        case geo::geometry_status_t::SELF_INTERSECTING: return "it intersects itself";
        case geo::geometry_status_t::OPEN_VOLUME: return "open (not a closed solid)";
        case geo::geometry_status_t::INVALID_TOPOLOGY: return "broken topology";
        case geo::geometry_status_t::NUMERIC_FAILURE: return "coordinates out of range";
        case geo::geometry_status_t::LIMIT_EXCEEDED: return "too large";
        default: return "could not be tessellated";
    }
}

map_status_t Status( geo::geometry_status_t status ) noexcept
{
    if ( status == geo::geometry_status_t::OK ) { return map_status_t::OK; }
    if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
    if ( status == geo::geometry_status_t::INVALID_ARGUMENT ) { return map_status_t::INVALID_ARGUMENT; }
    if ( status == geo::geometry_status_t::LIMIT_EXCEEDED ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}

// Every product, owned together and released together.
struct products_t {
    geo::geometry_snapshot_t snapshot{};
    geo::cook_surface_soup_t soup{};
    geo::cook_key_set_t keys{};
    geo::cook_collision_t collision{};
    geo::cook_visibility_t visibility{};
    geo::cook_navigation_t navigation{};
    geo::cook_lighting_t lighting{};
    geo::cook_batching_t batching{};
    ~products_t()
    {
        geo::CookBatching_Shutdown( &batching ); geo::CookLighting_Shutdown( &lighting ); geo::CookNavigation_Shutdown( &navigation );
        geo::CookVisibility_Shutdown( &visibility ); geo::CookCollision_Shutdown( &collision ); geo::CookKeySet_Shutdown( &keys );
        geo::CookSurfaces_Shutdown( &soup ); geo::GeometrySnapshot_Shutdown( &snapshot );
    }
};
} // namespace

map_status_t MapCompile_Run( const map_document_t *map, const map_compile_options_t &options, map_compile_report_t &report )
{
    report = {};
    if ( map == nullptr || !std::isfinite( options.walkableSlopeDegrees ) || options.walkableSlopeDegrees < 0.0 || options.walkableSlopeDegrees > 90.0 ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    const auto start = std::chrono::steady_clock::now();
    const allocator_t *allocator = map->pAllocator;
    products_t p;
    auto status = geo::GeometrySnapshot_TakeFromDocument( &p.snapshot, &map->geometry );
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookSurfaces_Init( &p.soup, allocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookSurfaces_TryBuild( &p.snapshot, &p.soup ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookKeySet_Init( &p.keys, allocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookKeySet_TryBuild( &p.keys, &p.snapshot ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookCollision_Init( &p.collision, allocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookCollision_TryBuild( &p.snapshot, &p.keys, nullptr, &p.collision ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookVisibility_Init( &p.visibility, allocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookVisibility_TryBuild( &p.snapshot, &p.soup, &p.visibility ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookNavigation_Init( &p.navigation, allocator ); }
    if ( status == geo::geometry_status_t::OK ) {
        geo::cook_navigation_options_t navigation{};
        navigation.maxSlopeRadians = options.walkableSlopeDegrees * 3.14159265358979323846 / 180.0;
        status = geo::CookNavigation_TryBuild( &p.soup, navigation, &p.navigation );
    }
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookLighting_Init( &p.lighting, allocator ); }
    if ( status == geo::geometry_status_t::OK ) {
        geo::cook_lighting_options_t lighting{};
        lighting.texelsPerUnit = options.lightmapTexelsPerUnit; lighting.pageSize = options.lightmapPageSize;
        status = geo::CookLighting_TryBuild( &p.soup, lighting, &p.lighting );
    }
    if ( status == geo::geometry_status_t::OK ) { status = geo::CookBatching_Init( &p.batching, allocator ); }
    if ( status == geo::geometry_status_t::OK ) {
        geo::cook_batching_options_t batching{}; batching.chunkSize = options.batchChunkSize;
        status = geo::CookBatching_TryBuild( &p.soup, batching, &p.batching );
    }
    text_buffer_t text{};
    if ( status == geo::geometry_status_t::OK && !TextBuffer_Init( &text, allocator ) ) { status = geo::geometry_status_t::ALLOCATION_FAILED; }
    if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryInterchange_TryWrite( &p.keys, &p.soup, &text ); }
    if ( status == geo::geometry_status_t::OK ) { report.interchange.assign( text.pData != nullptr ? text.pData : "", text.cchLength ); }
    TextBuffer_Shutdown( &text );
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }

    report.objects = p.soup.objects.nCount; report.vertices = p.soup.positions.nCount; report.triangles = p.soup.triangles.nCount;
    report.collisionSurfaces = p.collision.surfaces.nCount; report.collisionTriangles = p.collision.indices.nCount / 3;
    report.collisionDiagnostics = p.collision.diagnostics.nCount;
    report.visibilityHulls = p.visibility.hulls.nCount; report.visibilityShells = p.visibility.shells.nCount; report.visibilityDetail = p.visibility.detail.nCount;
    report.walkableTriangles = p.navigation.indices.nCount / 3;
    report.lightmapCharts = p.lighting.charts.nCount; report.lightmapPages = p.lighting.cPages;
    report.batches = p.batching.batches.nCount;
    for ( usize i = 0; i < p.soup.problems.nCount; ++i ) {
        const auto &problem = p.soup.problems.pData[i];
        report.problems.push_back( "object #" + std::to_string( problem.objectId.value ) + ( problem.elementId.value != 0 ? " face #" + std::to_string( problem.elementId.value ) : std::string() ) +
                                   ": " + Reason( problem.status ) );
    }
    report.milliseconds = std::chrono::duration<f64, std::milli>( std::chrono::steady_clock::now() - start ).count();
    return map_status_t::OK;
}

} // namespace cypher::editor::map
