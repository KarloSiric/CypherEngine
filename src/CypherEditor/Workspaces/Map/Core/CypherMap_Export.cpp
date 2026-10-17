//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Export.cpp
//  Purpose: Implements Wavefront OBJ export.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Export.h"
#include "CypherMap_Edit.h"
#include "CypherMap_Geometry.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMap_Modifiers.h"
#include "CypherGeometry_Cook_RenderMesh.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_PatchTessellation.h"

#include <cstdio>
#include <memory>
#include <set>
#include <vector>
#include <new>
#include <algorithm>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;

namespace
{
map_status_t Status( geo::geometry_status_t status ) noexcept
{
    if ( status == geo::geometry_status_t::OK ) { return map_status_t::OK; }
    if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
    if ( status == geo::geometry_status_t::LIMIT_EXCEEDED || status == geo::geometry_status_t::INSUFFICIENT_CAPACITY ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}
struct mesh_export_t {
    geo::modifier_result_t evaluated{};
    geo::mesh_source_t built{};
    geo::render_mesh_t cooked{};
    ~mesh_export_t()
    {
        geo::RenderMesh_Shutdown( &cooked );
        geo::MeshSource_Shutdown( &built );
        geo::ModifierResult_Shutdown( &evaluated );
    }
};
struct patch_export_t {
    geo::patch_tessellation_t value{};
    ~patch_export_t() { geo::PatchTessellation_Shutdown( &value ); }
};

// The editor retains newer/unsupported stages while displaying their cage.
// Export must not mistake that display fallback for a successfully evaluated
// surface. Disabled retained stages intentionally have no effect.
bool StoredModifiersSupported( map_document_t *map, u64 mesh ) noexcept
{
    const auto *record = MapDocument_FindObject( map, mesh, nullptr );
    const auto *stages = KeyValue_Find( record, StringView_FromCString( "modifiers" ) );
    if ( stages == nullptr ) { return true; }
    if ( KeyValue_Type( stages ) != key_value_type_t::ARRAY ) { return false; }
    for ( usize i = 0; i < KeyValue_ChildCount( stages ); ++i ) {
        const auto *stage = KeyValue_ChildAt( stages, i );
        if ( KeyValue_Type( stage ) != key_value_type_t::OBJECT ) { return false; }
        const auto *enabled = KeyValue_Find( stage, StringView_FromCString( "enabled" ) );
        bool_t active = CY_TRUE;
        if ( enabled != nullptr && !KeyValue_GetBool( enabled, &active ) ) { return false; }
        if ( !active ) { continue; }
        string_view_t type{};
        if ( !KeyValue_GetString( KeyValue_Find( stage, StringView_FromCString( "type" ) ), &type ) ) { return false; }
        bool known = false;
        for ( usize k = 0; k <= static_cast<usize>( geo::modifier_kind_t::SUBDIVIDE ); ++k ) {
            known = known || StringView_Equals( type, StringView_FromCString( MapModifiers_TypeName( static_cast<geo::modifier_kind_t>( k ) ) ) );
        }
        if ( !known ) { return false; }
        if ( StringView_Equals( type, StringView_FromCString( "subdivide" ) ) ) {
            const auto *value = KeyValue_Find( stage, StringView_FromCString( "scheme" ) );
            string_view_t scheme{};
            if ( value != nullptr && ( !KeyValue_GetString( value, &scheme ) ||
                 ( scheme.cchLength != 0u && !StringView_Equals( scheme, StringView_FromCString( "default" ) ) &&
                   !StringView_Equals( scheme, StringView_FromCString( "linear" ) ) &&
                   !StringView_Equals( scheme, StringView_FromCString( "catmull_clark" ) ) ) ) ) { return false; }
        }
    }
    return true;
}
struct writer_t {
    std::string obj;
    std::set<std::string> materials;
    usize nextVertex{ 1 }; // OBJ indices are 1-based and shared by v, vt, vn here.
    void Line( const char *format, f64 a, f64 b, f64 c )
    {
        char buffer[128];
        std::snprintf( buffer, sizeof( buffer ), format, a, b, c );
        obj += buffer;
    }
    void Uv( f64 u, f64 v )
    {
        char buffer[96];
        std::snprintf( buffer, sizeof( buffer ), "vt %.6g %.6g\n", u, 1.0 - v ); // OBJ's V runs up.
        obj += buffer;
    }
};

std::string MaterialName( const map_document_t &map, u64 ref )
{
    const string_view_t path = MapMaterials_Path( &map.materials, ref );
    std::string name( path.pData != nullptr ? path.pData : "", path.cchLength );
    for ( char &c : name ) { if ( c == ' ' ) { c = '_'; } }
    return name.empty() ? std::string( "default" ) : name;
}

// One cooked mesh: v/vt/vn per render vertex, faces per material batch.
void WriteRenderMesh( writer_t &w, const map_document_t &map, const geo::render_mesh_t &mesh )
{
    for ( usize i = 0; i < mesh.vertices.nCount; ++i ) {
        const auto &v = mesh.vertices.pData[i];
        w.Line( "v %.6g %.6g %.6g\n", v.position[0], v.position[1], v.position[2] );
        w.Uv( v.uv0[0], v.uv0[1] );
        w.Line( "vn %.6g %.6g %.6g\n", v.normal[0], v.normal[1], v.normal[2] );
    }
    for ( usize b = 0; b < mesh.batches.nCount; ++b ) {
        const auto &batch = mesh.batches.pData[b];
        const std::string material = MaterialName( map, batch.material.value );
        w.materials.insert( material );
        w.obj += "usemtl " + material + "\n";
        for ( u32 i = 0; i + 2 < batch.cIndices; i += 3 ) {
            const usize a = w.nextVertex + mesh.indices.pData[batch.iFirstIndex + i], c = w.nextVertex + mesh.indices.pData[batch.iFirstIndex + i + 1],
                        d = w.nextVertex + mesh.indices.pData[batch.iFirstIndex + i + 2];
            char buffer[160];
            std::snprintf( buffer, sizeof( buffer ), "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\n", a, a, a, c, c, c, d, d, d );
            w.obj += buffer;
        }
    }
    w.nextVertex += mesh.vertices.nCount;
}
} // namespace

map_status_t MapExport_Obj( const map_document_t *map, const std::string &mtlFileName, std::string &objOut, std::string &mtlOut, map_export_report_t *pReport ) noexcept
try
{
    if ( map == nullptr || &objOut == &mtlOut || mtlFileName.empty() ||
         mtlFileName.find_first_of( "\r\n" ) != std::string::npos || mtlFileName.find( '\0' ) != std::string::npos ) { return map_status_t::INVALID_ARGUMENT; }
    // Brushes as meshes, on a private copy.
    map_document_t *raw = nullptr;
    auto result = MapEdit_Clone( map, &raw );
    if ( result != map_status_t::OK ) { return result; }
    std::unique_ptr<map_document_t> copy( raw );
    copy->bReadOnly = false;
    std::vector<u64> brushes;
    for ( usize i = 0; i < copy->geometry.brushes.nCount; ++i ) { brushes.push_back( copy->geometry.brushes.pData[i]->sourceId.value ); }
    if ( !brushes.empty() ) {
        vector_t<u64> roots{};
        result = Vector_Init( &roots, copy->pAllocator ) ? MapMeshEdit_ConvertBrushes( copy.get(), { brushes.data(), brushes.size() }, &roots ) : map_status_t::OUT_OF_MEMORY;
        Vector_Shutdown( &roots );
        if ( result != map_status_t::OK ) { return result; }
    }
    writer_t w;
    w.obj = "# Exported by Mason (CypherEngine). Map units, Z up.\nmtllib " + mtlFileName + "\n";
    map_export_report_t report{};
    // Evaluators deliberately leave generated vertices/faces anonymous. Give
    // those parts private IDs for source validation/cooking; export must never
    // advance either document cursor or silently discard the evaluated surface.
    const u64 geometryNext = copy->geometry.sourceIds.allocator.next.value;
    geo::geometry_source_id_allocator_t exportIds{ { copy->nextId == 0u || geometryNext == 0u
        ? 0u : std::max( copy->nextId, geometryNext ) } };
    for ( usize i = 0; i < copy->geometry.meshes.nCount; ++i ) {
        const auto *source = copy->geometry.meshes.pData[i];
        const u64 id = source->sourceId.value;
        if ( !StoredModifiersSupported( copy.get(), id ) ) { return map_status_t::INVALID_ARGUMENT; }
        // A modifier stack exports as it is seen.
        mesh_export_t prepared{};
        const geo::mesh_source_t *mesh = source;
        result = Status( geo::ModifierResult_Init( &prepared.evaluated, copy->pAllocator ) );
        if ( result != map_status_t::OK ) { return result; }
        result = MapModifiers_Evaluate( copy.get(), id, &prepared.evaluated );
        if ( result == map_status_t::OK ) {
            auto fresh = [&]( geo::geometry_source_id_t &part ) noexcept -> map_status_t {
                if ( geo::GeometrySourceId_IsValid( part ) ) { return map_status_t::OK; }
                const auto assigned = geo::GeometrySourceIdAllocator_Allocate( &exportIds );
                if ( assigned.status != geo::geometry_status_t::OK ) { return Status( assigned.status ); }
                part = assigned.id; return map_status_t::OK;
            };
            for ( usize v = 0; v < prepared.evaluated.mesh.vertices.nCount; ++v ) {
                result = fresh( prepared.evaluated.mesh.vertices.pData[v].sourceId );
                if ( result != map_status_t::OK ) { return result; }
            }
            for ( usize f = 0; f < prepared.evaluated.mesh.faces.nCount; ++f ) {
                result = fresh( prepared.evaluated.mesh.faces.pData[f].sourceId );
                if ( result != map_status_t::OK ) { return result; }
            }
            result = Status( geo::MeshSource_TryBuild( &prepared.evaluated.mesh, copy->pAllocator, &prepared.built ) );
            if ( result != map_status_t::OK ) { return result; }
            mesh = &prepared.built;
        } else if ( result != map_status_t::UNKNOWN_OBJECT ) { return result; } // No enabled stack is the only cage path.
        result = Status( geo::RenderMesh_Init( &prepared.cooked, copy->pAllocator ) );
        if ( result == map_status_t::OK ) { result = Status( geo::RenderMesh_TryCook( &mesh->mesh, &mesh->attributes, &prepared.cooked ) ); }
        if ( result != map_status_t::OK ) { return result; }
        w.obj += "o object_" + std::to_string( id ) + "\n";
        WriteRenderMesh( w, *copy, prepared.cooked );
        ++report.objects; report.triangles += prepared.cooked.indices.nCount / 3;
    }
    for ( usize i = 0; i < copy->geometry.patches.nCount; ++i ) {
        const auto *patch = copy->geometry.patches.pData[i];
        patch_export_t prepared{};
        auto &tess = prepared.value;
        geo::patch_tessellation_options_t options{}; options.fixedSubdivisions = 8;
        result = Status( geo::PatchTessellation_Init( &tess, copy->pAllocator ) );
        if ( result == map_status_t::OK ) { result = Status( geo::PatchTessellation_TryBuild( patch, options, &tess ) ); }
        if ( result != map_status_t::OK ) { return result; }
        w.obj += "o patch_" + std::to_string( patch->sourceId.value ) + "\n";
        for ( usize k = 0; k < tess.positions.nCount; ++k ) {
            const auto p = tess.positions.pData[k], n = tess.normals.pData[k];
            const auto uv = k < tess.uvs.nCount ? tess.uvs.pData[k] : math::vec2d_t{};
            w.Line( "v %.6g %.6g %.6g\n", p.x, p.y, p.z );
            w.Uv( uv.x, uv.y );
            w.Line( "vn %.6g %.6g %.6g\n", n.x, n.y, n.z );
        }
        const std::string material = MaterialName( *copy, patch->materialId );
        w.materials.insert( material );
        w.obj += "usemtl " + material + "\n";
        for ( usize k = 0; k + 2 < tess.indices.nCount; k += 3 ) {
            const usize a = w.nextVertex + tess.indices.pData[k], b = w.nextVertex + tess.indices.pData[k + 1], c = w.nextVertex + tess.indices.pData[k + 2];
            if ( a == b || b == c || a == c ) { continue; } // A collapsed pole.
            char buffer[160];
            std::snprintf( buffer, sizeof( buffer ), "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\n", a, a, a, b, b, b, c, c, c );
            w.obj += buffer;
            ++report.triangles;
        }
        w.nextVertex += tess.positions.nCount;
        ++report.objects;
    }
    report.skippedTerrain = copy->geometry.heightFields.nCount;
    for ( usize i = 0; i < copy->layerStores.nCount; ++i ) {
        const auto *store = copy->layerStores.pData[i];
        report.skippedInstances += KeyValue_ChildCount( KeyValue_Find( KeyValue_Root( store->store.pDocument ), StringView_FromCString( "prefabs" ) ) );
    }
    report.skipped = report.skippedTerrain + report.skippedInstances;
    std::string mtl = "# Materials of the exported map (paths as named in the map).\n";
    for ( const auto &material : w.materials ) { mtl += "newmtl " + material + "\nKd 0.8 0.8 0.8\n\n"; }
    // Only nonthrowing publication remains after both complete strings exist.
    objOut.swap( w.obj ); mtlOut.swap( mtl );
    if ( pReport != nullptr ) { *pReport = report; }
    return map_status_t::OK;
}
catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }

} // namespace cypher::editor::map
