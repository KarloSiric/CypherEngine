//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Modifiers.cpp
//  Purpose: Implements mesh modifier stacks in the map record.
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Modifiers.h"
#include "CypherGeometry_DocumentMeshes.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <iterator>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;

namespace
{
string_view_t SV( const char *p ) noexcept { return StringView_FromCString( p ); }

constexpr const char *kTypes[]{ "mirror", "linear_array", "radial_array", "bend", "taper", "subdivide" };

key_value_t *Record( const map_document_t *map, u64 id, map_layer_store_t **ppStore ) noexcept
{
    // Finding mutates nothing; the document API is not const-qualified.
    return MapDocument_FindObject( const_cast<map_document_t *>( map ), id, ppStore );
}

f64 Real( const key_value_t *object, const char *member, f64 fallback ) noexcept
{
    f64 value = 0.0; i64 whole = 0;
    if ( KeyValue_GetF64( KeyValue_Find( object, SV( member ) ), &value ) ) { return value; }
    if ( KeyValue_GetI64( KeyValue_Find( object, SV( member ) ), &whole ) ) { return static_cast<f64>( whole ); }
    return fallback;
}
u32 Count( const key_value_t *object, const char *member, u32 fallback ) noexcept
{
    i64 value = 0;
    return KeyValue_GetI64( KeyValue_Find( object, SV( member ) ), &value ) && value >= 0 && value <= 1000000 ? static_cast<u32>( value ) : fallback;
}
bool Flag( const key_value_t *object, const char *member, bool fallback ) noexcept
{
    bool_t value = CY_FALSE;
    return KeyValue_GetBool( KeyValue_Find( object, SV( member ) ), &value ) ? value != CY_FALSE : fallback;
}
math::vec3d_t Vector( const key_value_t *object, const char *member, math::vec3d_t fallback ) noexcept
{
    const key_value_t *array = KeyValue_Find( object, SV( member ) );
    if ( KeyValue_ChildCount( array ) != 3 ) { return fallback; }
    f64 v[3]{};
    for ( usize i = 0; i < 3; ++i ) {
        i64 whole = 0;
        if ( !KeyValue_GetF64( KeyValue_ChildAt( array, i ), &v[i] ) ) {
            if ( !KeyValue_GetI64( KeyValue_ChildAt( array, i ), &whole ) ) { return fallback; }
            v[i] = static_cast<f64>( whole );
        }
    }
    return { v[0], v[1], v[2] };
}
std::string Text( const key_value_t *object, const char *member )
{
    string_view_t text{};
    return KeyValue_GetString( KeyValue_Find( object, SV( member ) ), &text ) ? std::string( text.pData, text.cchLength ) : std::string();
}
// CYMAP.md writes axes as "x", "y" or "z".
u32 AxisOf( const key_value_t *object, const char *member, u32 fallback )
{
    const std::string axis = Text( object, member );
    return axis == "x" ? 0u : axis == "y" ? 1u : axis == "z" ? 2u : fallback;
}
const char *AxisName( u32 axis ) noexcept { return axis == 0 ? "x" : axis == 1 ? "y" : "z"; }
constexpr f64 kDegrees = 180.0 / 3.14159265358979323846;

bool SameStage( const geo::modifier_t &a, const geo::modifier_t &b ) noexcept
{
    const auto vec = []( math::vec3d_t x, math::vec3d_t y ) { return x.x == y.x && x.y == y.y && x.z == y.z; };
    return a.kind == b.kind && a.bEnabled == b.bEnabled && a.axis == b.axis && a.alongAxis == b.alongAxis && a.offset == b.offset &&
           a.weldDistance == b.weldDistance && a.bWeld == b.bWeld && a.cCopies == b.cCopies && vec( a.step, b.step ) && vec( a.origin, b.origin ) &&
           vec( a.direction, b.direction ) && a.angle == b.angle && a.factor == b.factor && a.subdivision.scheme == b.subdivision.scheme &&
           a.subdivision.cLevels == b.subdivision.cLevels && a.subdivision.boundary == b.subdivision.boundary &&
           a.subdivision.bHardEdgesAreCreases == b.subdivision.bHardEdgesAreCreases;
}

// Writing: one member per meaningful field of the stage's type.
struct writer_t {
    key_value_document_t *document;
    key_value_t *object;
    bool ok{ true };
    void Text( const char *member, const char *value ) {
        key_value_t *node = ok ? KeyValue_ObjectInsert( document, object, SV( member ), key_value_type_t::STRING ) : nullptr;
        ok = node != nullptr && KeyValue_SetString( document, node, SV( value ) );
    }
    void Real( const char *member, f64 value ) {
        key_value_t *node = ok ? KeyValue_ObjectInsert( document, object, SV( member ), key_value_type_t::F64 ) : nullptr;
        ok = node != nullptr && KeyValue_SetF64( document, node, value );
    }
    void Count( const char *member, u32 value ) {
        key_value_t *node = ok ? KeyValue_ObjectInsert( document, object, SV( member ), key_value_type_t::I64 ) : nullptr;
        ok = node != nullptr && KeyValue_SetI64( document, node, value );
    }
    void Flag( const char *member, bool value ) {
        key_value_t *node = ok ? KeyValue_ObjectInsert( document, object, SV( member ), key_value_type_t::BOOL ) : nullptr;
        ok = node != nullptr && KeyValue_SetBool( document, node, value ? CY_TRUE : CY_FALSE );
    }
    void Axis( const char *member, u32 axis ) { Text( member, AxisName( axis ) ); }
    void Vector( const char *member, math::vec3d_t value ) {
        key_value_t *array = ok ? KeyValue_ObjectInsert( document, object, SV( member ), key_value_type_t::ARRAY ) : nullptr;
        ok = array != nullptr;
        for ( f64 c : { value.x, value.y, value.z } ) {
            key_value_t *node = ok ? KeyValue_ArrayAppend( document, array, key_value_type_t::F64 ) : nullptr;
            ok = node != nullptr && KeyValue_SetF64( document, node, c );
        }
    }
};

bool WriteStage( key_value_document_t *document, key_value_t *array, const geo::modifier_t &m ) noexcept
{
    writer_t w{ document, KeyValue_ArrayAppend( document, array, key_value_type_t::OBJECT ) };
    w.ok = w.object != nullptr;
    w.Text( "type", MapModifiers_TypeName( m.kind ) );
    w.Flag( "enabled", m.bEnabled );
    // Members as CYMAP.md 6.4 names them: axes as letters, angles in degrees.
    switch ( m.kind ) {
        case geo::modifier_kind_t::MIRROR:
            w.Axis( "axis", m.axis ); w.Real( "offset", m.offset ); w.Flag( "weld", m.bWeld ); w.Real( "weld_distance", m.weldDistance ); break;
        case geo::modifier_kind_t::LINEAR_ARRAY:
            w.Count( "copies", m.cCopies ); w.Vector( "step", m.step ); w.Flag( "weld", m.bWeld ); w.Real( "weld_distance", m.weldDistance ); break;
        case geo::modifier_kind_t::RADIAL_ARRAY:
            w.Count( "copies", m.cCopies ); w.Vector( "origin", m.origin ); w.Vector( "direction", m.direction ); w.Real( "angle", m.angle * kDegrees );
            w.Flag( "weld", m.bWeld ); w.Real( "weld_distance", m.weldDistance ); break;
        case geo::modifier_kind_t::BEND:
            w.Axis( "axis", m.axis ); w.Axis( "along", m.alongAxis ); w.Vector( "origin", m.origin ); w.Real( "angle", m.angle * kDegrees ); break;
        case geo::modifier_kind_t::TAPER:
            w.Axis( "along", m.alongAxis ); w.Vector( "origin", m.origin ); w.Real( "factor", m.factor ); break;
        case geo::modifier_kind_t::SUBDIVIDE:
            w.Count( "levels", m.subdivision.cLevels );
            w.Text( "scheme", m.subdivision.scheme == geo::subdivision_scheme_t::LINEAR ? "linear" : "catmull_clark" );
            w.Text( "boundary", m.subdivision.boundary == geo::subdivision_boundary_t::SMOOTH ? "smooth" : "sharp" );
            w.Flag( "hard_edges_crease", m.subdivision.bHardEdgesAreCreases ); break;
    }
    return w.ok;
}

map_status_t Status( geo::geometry_status_t status ) noexcept
{
    if ( status == geo::geometry_status_t::OK ) { return map_status_t::OK; }
    if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
    if ( status == geo::geometry_status_t::LIMIT_EXCEEDED || status == geo::geometry_status_t::INSUFFICIENT_CAPACITY ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}

geo::geometry_status_t StackOf( const std::vector<geo::modifier_t> &stages, const allocator_t *allocator, geo::modifier_stack_t &stack ) noexcept
{
    const auto initialized = geo::ModifierStack_Init( &stack, allocator );
    if ( initialized != geo::geometry_status_t::OK ) { return initialized; }
    for ( const auto &stage : stages ) {
        if ( stage.bEnabled ) {
            const auto pushed = geo::ModifierStack_TryPush( &stack, stage );
            if ( pushed != geo::geometry_status_t::OK ) { return pushed; }
        }
    }
    return geo::geometry_status_t::OK;
}
} // namespace

const char *MapModifiers_TypeName( geo::modifier_kind_t kind ) noexcept
{
    const auto index = static_cast<usize>( kind );
    return index < std::size( kTypes ) ? kTypes[index] : "unknown";
}

bool MapModifiers_HasRetainedStack( const map_document_t *map, u64 mesh ) noexcept
{
    if ( map == nullptr || mesh == 0u ) { return false; }
    const auto *member = KeyValue_Find( Record( map, mesh, nullptr ), SV( "modifiers" ) );
    return member != nullptr && ( KeyValue_Type( member ) != key_value_type_t::ARRAY || KeyValue_ChildCount( member ) != 0u );
}

map_status_t MapModifiers_Read( const map_document_t *map, u64 mesh, std::vector<geo::modifier_t> &stagesOut )
{
    stagesOut.clear();
    if ( map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( geo::GeometryDocument_FindMesh( &map->geometry, { mesh } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    const key_value_t *record = Record( map, mesh, nullptr );
    const key_value_t *stages = KeyValue_Find( record, SV( "modifiers" ) ); // CYMAP.md 6.4: an array of stage objects.
    for ( usize i = 0; i < KeyValue_ChildCount( stages ); ++i ) {
        const key_value_t *s = KeyValue_ChildAt( stages, i );
        const std::string type = Text( s, "type" );
        usize kind = std::size( kTypes );
        for ( usize k = 0; k < std::size( kTypes ); ++k ) { if ( type == kTypes[k] ) { kind = k; } }
        if ( kind == std::size( kTypes ) ) { continue; } // Unknown types are kept in the file and skipped here.
        geo::modifier_t m{};
        m.kind = static_cast<geo::modifier_kind_t>( kind );
        m.bEnabled = Flag( s, "enabled", true );
        const bool shaping = m.kind == geo::modifier_kind_t::BEND || m.kind == geo::modifier_kind_t::TAPER;
        m.axis = AxisOf( s, "axis", m.kind == geo::modifier_kind_t::BEND ? 2u : 0u ); m.alongAxis = AxisOf( s, "along", shaping ? ( m.kind == geo::modifier_kind_t::BEND ? 0u : 2u ) : 0u );
        m.offset = Real( s, "offset", 0.0 );
        m.bWeld = Flag( s, "weld", true ); m.weldDistance = Real( s, "weld_distance", 1e-6 );
        m.cCopies = Count( s, "copies", 2 );
        m.step = Vector( s, "step", { 64, 0, 0 } ); m.origin = Vector( s, "origin", {} ); m.direction = Vector( s, "direction", { 0, 0, 1 } );
        m.angle = Real( s, "angle", m.kind == geo::modifier_kind_t::RADIAL_ARRAY ? 360.0 : 0.0 ) / kDegrees; m.factor = Real( s, "factor", 1.0 );
        m.subdivision.cLevels = Count( s, "levels", 1 );
        const std::string scheme = Text( s, "scheme" );
        m.subdivision.scheme = scheme == "linear" ? geo::subdivision_scheme_t::LINEAR : geo::subdivision_scheme_t::CATMULL_CLARK;
        if ( scheme == "loop" ) { m.bEnabled = false; } // Loop subdivision is not evaluated yet: kept, shown as the cage.
        m.subdivision.boundary = Text( s, "boundary" ) == "smooth" ? geo::subdivision_boundary_t::SMOOTH : geo::subdivision_boundary_t::PIN_CORNERS;
        m.subdivision.bHardEdgesAreCreases = Flag( s, "hard_edges_crease", true );
        stagesOut.push_back( m );
    }
    return map_status_t::OK;
}

namespace
{
// bBump: a whole-mesh transform has already advanced the revision once, so
// its stack rewrite must not count as a second edit.
map_status_t WriteStages( map_document_t *map, u64 mesh, const std::vector<geo::modifier_t> &stages, bool *pChangedOut, bool bBump,
    bool preserveStageMembers = false )
{
    if ( pChangedOut == nullptr || map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    *pChangedOut = false;
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( stages.size() > geo::kModifierStagesMax ) { return map_status_t::LIMIT_EXCEEDED; }
    for ( const auto &stage : stages ) { if ( geo::Modifier_Validate( stage ) != geo::geometry_status_t::OK ) { return map_status_t::INVALID_ARGUMENT; } }
    std::vector<geo::modifier_t> current;
    auto result = MapModifiers_Read( map, mesh, current );
    if ( result != map_status_t::OK ) { return result; }
    if ( current.size() == stages.size() ) {
        bool same = true;
        for ( usize i = 0; same && i < stages.size(); ++i ) { same = SameStage( current[i], stages[i] ); }
        if ( same ) { return map_status_t::OK; }
    }
    // The stack as a standalone member, then into the record (made for a
    // mesh that has none yet).
    key_value_document_desc_t desc{}; desc.pAllocator = map->pAllocator;
    key_value_document_t *scratch = KeyValue_CreateDocument( desc );
    struct scratch_scope_t { key_value_document_t *document; ~scratch_scope_t() { KeyValue_DestroyDocument( document ); } } ownedScratch{ scratch };
    if ( scratch == nullptr || !KeyValue_SetRootType( scratch, key_value_type_t::OBJECT ) ) { return map_status_t::OUT_OF_MEMORY; }
    key_value_t *root = KeyValue_Root( scratch );
    bool ok = true;
    key_value_t *id = KeyValue_ObjectInsert( scratch, root, SV( "id" ), key_value_type_t::U64 );
    ok = id != nullptr && KeyValue_SetU64( scratch, id, mesh );
    key_value_t *modifiers = ok ? KeyValue_ObjectInsert( scratch, root, SV( "modifiers" ), key_value_type_t::ARRAY ) : nullptr;
    key_value_t *array = modifiers;
    ok = ok && array != nullptr;
    const auto *oldStages = KeyValue_Find( Record( map, mesh, nullptr ), SV( "modifiers" ) );
    for ( usize i = 0; ok && i < stages.size(); ++i ) {
        ok = WriteStage( scratch, array, stages[i] );
        if ( ok && preserveStageMembers ) {
            // Transform admission guarantees the same known stages in the
            // same order. Preserve retained members beside the fields we own.
            const auto *oldStage = KeyValue_ChildAt( oldStages, i );
            auto *newStage = KeyValue_ChildAt( array, i );
            ok = KeyValue_SetComments( scratch, newStage, KeyValue_Comments( oldStage ) );
            for ( usize k = 0; ok && k < KeyValue_ChildCount( oldStage ); ++k ) {
                const auto *member = KeyValue_ChildAt( oldStage, k );
                auto *written = KeyValue_Find( newStage, KeyValue_Name( member ) );
                ok = written ? KeyValue_SetComments( scratch, written, KeyValue_Comments( member ) ) != CY_FALSE
                             : KeyValue_CloneInto( scratch, newStage, member ) != nullptr;
            }
        }
    }
    // Stage types this editor does not know stay in the file (CYMAP.md 6.4).
    usize kept = 0;
    {
        const key_value_t *old = KeyValue_Find( Record( map, mesh, nullptr ), SV( "modifiers" ) );
        for ( usize i = 0; ok && i < KeyValue_ChildCount( old ); ++i ) {
            const key_value_t *stage = KeyValue_ChildAt( old, i );
            const std::string type = Text( stage, "type" );
            if ( std::any_of( std::begin( kTypes ), std::end( kTypes ), [&]( const char *k ) { return type == k; } ) ) { continue; }
            ok = KeyValue_CloneInto( scratch, array, stage ) != nullptr; ++kept;
        }
    }
    const bool empty = stages.empty() && kept == 0;
    if ( !ok ) { return map_status_t::OUT_OF_MEMORY; }
    map_layer_store_t *layerStore = nullptr;
    key_value_t *record = Record( map, mesh, &layerStore );
    if ( record == nullptr ) {
        result = empty ? map_status_t::OK : MapDocument_InsertGeometryRecord( map, map_geometry_kind_t::MESH, root );
    } else {
        key_value_document_t *document = layerStore->store.pDocument;
        if ( key_value_t *old = KeyValue_Find( record, SV( "modifiers" ) ); old != nullptr && !KeyValue_Remove( document, record, old ) ) { result = map_status_t::OUT_OF_MEMORY; }
        if ( result == map_status_t::OK && !empty && KeyValue_CloneInto( document, record, modifiers ) == nullptr ) { result = map_status_t::OUT_OF_MEMORY; }
    }
    if ( result == map_status_t::OK ) {
        *pChangedOut = true;
        if ( bBump ) { ++map->geometry.revision; } // The views rebuild the evaluated surface.
    }
    return result;
}
} // namespace

map_status_t MapModifiers_Write( map_document_t *map, u64 mesh, const std::vector<geo::modifier_t> &stages, bool *pChangedOut )
{
    return WriteStages( map, mesh, stages, pChangedOut, true );
}

map_status_t MapModifiers_Evaluate( const map_document_t *map, u64 mesh, geo::modifier_result_t *pResult ) noexcept
{
    if ( map == nullptr || pResult == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    std::vector<geo::modifier_t> stages;
    map_status_t result;
    try { result = MapModifiers_Read( map, mesh, stages ); }
    catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
    if ( result != map_status_t::OK ) { return result; }
    bool any = false;
    for ( const auto &stage : stages ) { any = any || stage.bEnabled; }
    if ( !any ) { return map_status_t::UNKNOWN_OBJECT; }
    geo::modifier_stack_t stack{};
    const auto stackStatus = StackOf( stages, map->pAllocator, stack );
    if ( stackStatus != geo::geometry_status_t::OK ) {
        geo::ModifierStack_Shutdown( &stack );
        return stackStatus == geo::geometry_status_t::INVALID_ARGUMENT ? map_status_t::INVALID_ARGUMENT : Status( stackStatus );
    }
    const auto status = geo::ModifierStack_TryEvaluate( geo::GeometryDocument_FindMesh( &map->geometry, { mesh } ), &stack, pResult );
    geo::ModifierStack_Shutdown( &stack );
    return Status( status );
}

map_status_t MapModifiers_Apply( map_document_t *map, u64 mesh ) noexcept
{
    if ( map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    std::vector<geo::modifier_t> stages;
    map_status_t result;
    try { result = MapModifiers_Read( map, mesh, stages ); }
    catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
    if ( result != map_status_t::OK ) { return result; }
    if ( stages.empty() ) { return map_status_t::INVALID_ARGUMENT; }
    geo::modifier_stack_t stack{};
    const auto stackStatus = StackOf( stages, map->pAllocator, stack );
    if ( stackStatus != geo::geometry_status_t::OK ) {
        geo::ModifierStack_Shutdown( &stack );
        return stackStatus == geo::geometry_status_t::INVALID_ARGUMENT ? map_status_t::INVALID_ARGUMENT : Status( stackStatus );
    }
    geo::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    geo::mesh_source_t collapsed{};
    auto status = stack.stages.nCount == 0 ? geo::geometry_status_t::OK
        : geo::ModifierStack_TryCollapse( geo::GeometryDocument_FindMesh( &map->geometry, { mesh } ), &stack, map->pAllocator, &ids, &collapsed );
    if ( status == geo::geometry_status_t::OK && stack.stages.nCount != 0 ) { status = geo::GeometryDocument_TryReplaceMesh( &map->geometry, &collapsed ); }
    geo::MeshSource_Shutdown( &collapsed );
    geo::ModifierStack_Shutdown( &stack );
    result = Status( status );
    if ( result != map_status_t::OK ) { return result; }
    map->nextId = std::max( map->nextId, ids.next.value );
    bool changed = false;
    return MapModifiers_Write( map, mesh, {}, &changed );
}

u32 MapModifiers_SubdivisionLevel( const map_document_t *map, u64 mesh ) noexcept
{
    std::vector<geo::modifier_t> stages;
    if ( MapModifiers_Read( map, mesh, stages ) != map_status_t::OK ) { return 0; }
    for ( const auto &stage : stages ) { if ( stage.kind == geo::modifier_kind_t::SUBDIVIDE && stage.bEnabled ) { return stage.subdivision.cLevels; } }
    return 0;
}

map_status_t MapModifiers_SetSubdivisionLevel( map_document_t *map, u64 mesh, u32 levels, bool *pChangedOut )
{
    if ( pChangedOut == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    *pChangedOut = false;
    if ( levels > geo::kSubdivisionLevelsMax ) { return map_status_t::INVALID_ARGUMENT; }
    std::vector<geo::modifier_t> stages;
    auto result = MapModifiers_Read( map, mesh, stages );
    if ( result != map_status_t::OK ) { return result; }
    auto at = std::find_if( stages.begin(), stages.end(), []( const geo::modifier_t &m ) { return m.kind == geo::modifier_kind_t::SUBDIVIDE; } );
    if ( levels == 0 ) {
        if ( at == stages.end() ) { return map_status_t::OK; }
        stages.erase( at );
    } else if ( at == stages.end() ) {
        geo::modifier_t m{};
        m.kind = geo::modifier_kind_t::SUBDIVIDE;
        m.subdivision.cLevels = levels;
        m.subdivision.scheme = geo::subdivision_scheme_t::CATMULL_CLARK;
        m.subdivision.boundary = geo::subdivision_boundary_t::PIN_CORNERS;
        m.subdivision.bHardEdgesAreCreases = true; // Authored hard edges stay sharp, as Subdivide bakes them.
        stages.push_back( m );
    } else {
        at->subdivision.cLevels = levels; at->bEnabled = true;
    }
    return MapModifiers_Write( map, mesh, stages, pChangedOut );
}

namespace
{
constexpr f64 kTransformTolerance = 1e-12;
struct modifier_transform_t {
    math::vec3d_t columns[3]{};
    math::vec3d_t unit[3]{};
    f64 length[3]{};
    u32 axis[3]{ CY_U32_MAX, CY_U32_MAX, CY_U32_MAX };
    f64 sign[3]{};
    bool similarity{};
};
f64 Coord( math::vec3d_t v, u32 axis ) noexcept { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
bool SameScale( f64 a, f64 b ) noexcept { return std::fabs( a - b ) <= kTransformTolerance * std::max( a, b ); }
bool Identity( const math::affine3d_t &affine ) noexcept
{
    for ( usize i = 0; i < 12; ++i ) { if ( affine.m[i] != math::CY_AFFINE3D_IDENTITY.m[i] ) { return false; } }
    return true;
}
bool DescribeTransform( const math::affine3d_t &affine, modifier_transform_t &out ) noexcept
{
    for ( const f64 value : affine.m ) { if ( !std::isfinite( value ) ) { return false; } }
    for ( u32 i = 0; i < 3; ++i ) {
        out.columns[i] = math::Affine3d_TransformDirection( affine, { i == 0 ? 1.0 : 0.0, i == 1 ? 1.0 : 0.0, i == 2 ? 1.0 : 0.0 } );
        out.length[i] = std::hypot( out.columns[i].x, out.columns[i].y, out.columns[i].z );
        if ( !( out.length[i] > 0 ) || !std::isfinite( out.length[i] ) ) { return false; }
        out.unit[i] = math::Vec3d_Scale( out.columns[i], 1.0 / out.length[i] );
        for ( u32 k = 0; k < 3; ++k ) {
            bool aligned = true;
            for ( u32 j = 0; j < 3; ++j ) { if ( j != k && std::fabs( Coord( out.unit[i], j ) ) > kTransformTolerance ) { aligned = false; } }
            if ( aligned ) { out.axis[i] = k; out.sign[i] = Coord( out.unit[i], k ) > 0 ? 1 : -1; break; }
        }
    }
    // Whole-map transforms preserve orientation (positive scales, turns and
    // shear). Reflections need an additional winding policy, so are refused.
    const f64 determinant = math::Vec3d_Dot( out.unit[0], math::Vec3d_Cross( out.unit[1], out.unit[2] ) );
    if ( !( determinant > kTransformTolerance ) ) { return false; }
    out.similarity = SameScale( out.length[0], out.length[1] ) && SameScale( out.length[0], out.length[2] ) &&
        std::fabs( math::Vec3d_Dot( out.unit[0], out.unit[1] ) ) <= kTransformTolerance &&
        std::fabs( math::Vec3d_Dot( out.unit[0], out.unit[2] ) ) <= kTransformTolerance &&
        std::fabs( math::Vec3d_Dot( out.unit[1], out.unit[2] ) ) <= kTransformTolerance;
    return true;
}
// An axis and its perpendicular plane must remain separate. Merely mapping
// the axis is insufficient: a shear can send other coordinates into it.
bool SeparatePlane( const modifier_transform_t &transform, u32 axis ) noexcept
{
    if ( transform.axis[axis] == CY_U32_MAX ) { return false; }
    for ( u32 other = 0; other < 3; ++other ) {
        if ( other != axis && std::fabs( Coord( transform.unit[other], transform.axis[axis] ) ) > kTransformTolerance ) { return false; }
    }
    return true;
}
map_status_t PrepareTransformedStages( const map_document_t *map, u64 mesh, const math::affine3d_t &affine,
    std::vector<geo::modifier_t> &stages )
{
    if ( map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( geo::GeometryDocument_FindMesh( &map->geometry, { mesh } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    if ( Identity( affine ) ) { return map_status_t::OK; }
    const auto unsupported = map_status_t::MODIFIER_TRANSFORM_UNSUPPORTED;
    const auto *raw = KeyValue_Find( Record( map, mesh, nullptr ), SV( "modifiers" ) );
    if ( raw == nullptr ) { return map_status_t::OK; }
    if ( KeyValue_Type( raw ) != key_value_type_t::ARRAY ) { return unsupported; }
    if ( KeyValue_ChildCount( raw ) > geo::kModifierStagesMax ) { return map_status_t::LIMIT_EXCEEDED; }
    // Unknown/unevaluated stages retain their exact record and order. We
    // cannot prove how they follow a transform, even when currently disabled.
    for ( usize i = 0; i < KeyValue_ChildCount( raw ); ++i ) {
        const auto *stage = KeyValue_ChildAt( raw, i ); string_view_t type{};
        if ( !KeyValue_GetString( KeyValue_Find( stage, SV( "type" ) ), &type ) ) { return unsupported; }
        bool known = false;
        for ( const auto *name : kTypes ) { known |= StringView_Equals( type, SV( name ) ); }
        if ( !known ) { return unsupported; }
        if ( StringView_Equals( type, SV( "subdivide" ) ) ) {
            const auto *node = KeyValue_Find( stage, SV( "scheme" ) ); string_view_t scheme{};
            if ( node != nullptr && ( !KeyValue_GetString( node, &scheme ) ||
                 ( !StringView_Equals( scheme, SV( "linear" ) ) && !StringView_Equals( scheme, SV( "catmull_clark" ) ) ) ) ) { return unsupported; }
        }
    }
    auto status = MapModifiers_Read( map, mesh, stages );
    if ( status != map_status_t::OK || stages.empty() ) { return status; }
    modifier_transform_t transform{};
    if ( !DescribeTransform( affine, transform ) ) { return unsupported; }
    for ( auto &m : stages ) {
        if ( geo::Modifier_Validate( m ) != geo::geometry_status_t::OK ) { return map_status_t::INVALID_ARGUMENT; }
        // Disabled known stages follow the cage too. Re-enabling one must not
        // revive its old world-space plane, direction or origin.
        switch ( m.kind ) {
            case geo::modifier_kind_t::MIRROR: {
                if ( !SeparatePlane( transform, m.axis ) || ( m.bWeld && m.weldDistance > 0 && !transform.similarity ) ) { return unsupported; }
                const auto on = math::Affine3d_TransformPoint( affine, { m.axis == 0 ? m.offset : 0.0, m.axis == 1 ? m.offset : 0.0, m.axis == 2 ? m.offset : 0.0 } );
                m.axis = transform.axis[m.axis]; m.offset = Coord( on, m.axis );
                if ( m.bWeld ) { m.weldDistance *= transform.length[0]; }
                break;
            }
            case geo::modifier_kind_t::LINEAR_ARRAY:
                if ( m.bWeld && m.weldDistance > 0 && !transform.similarity ) { return unsupported; }
                m.step = math::Affine3d_TransformDirection( affine, m.step );
                if ( m.bWeld ) { m.weldDistance *= transform.length[0]; }
                break;
            case geo::modifier_kind_t::RADIAL_ARRAY: {
                if ( !transform.similarity ) { return unsupported; }
                m.origin = math::Affine3d_TransformPoint( affine, m.origin );
                const auto direction = math::Affine3d_TransformDirection( affine, m.direction );
                const f64 length = std::hypot( direction.x, direction.y, direction.z );
                if ( !( length > 0 ) || !std::isfinite( length ) ) { return unsupported; }
                m.direction = math::Vec3d_Scale( direction, 1.0 / length );
                if ( m.bWeld ) { m.weldDistance *= transform.length[0]; }
                break;
            }
            case geo::modifier_kind_t::BEND: {
                const u32 side = 3u - m.axis - m.alongAxis;
                if ( transform.axis[m.axis] == CY_U32_MAX || transform.axis[m.alongAxis] == CY_U32_MAX ||
                     transform.axis[side] == CY_U32_MAX || !SameScale( transform.length[m.alongAxis], transform.length[side] ) ) { return unsupported; }
                m.origin = math::Affine3d_TransformPoint( affine, m.origin );
                m.angle *= transform.sign[side];
                m.axis = transform.axis[m.axis]; m.alongAxis = transform.axis[m.alongAxis];
                break;
            }
            case geo::modifier_kind_t::TAPER:
                if ( !SeparatePlane( transform, m.alongAxis ) || transform.sign[m.alongAxis] < 0 ) { return unsupported; }
                m.origin = math::Affine3d_TransformPoint( affine, m.origin );
                m.alongAxis = transform.axis[m.alongAxis];
                break;
            case geo::modifier_kind_t::SUBDIVIDE: break; // Affine combinations of the cage points.
        }
        if ( geo::Modifier_Validate( m ) != geo::geometry_status_t::OK ) { return unsupported; }
    }
    return map_status_t::OK;
}
} // namespace

map_status_t MapModifiers_CheckTransform( const map_document_t *map, u64 mesh, const math::affine3d_t &affine ) noexcept
{
    try {
        std::vector<geo::modifier_t> stages;
        return PrepareTransformedStages( map, mesh, affine, stages );
    } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
}

map_status_t MapModifiers_Transform( map_document_t *map, u64 mesh, const math::affine3d_t &affine ) noexcept
{
    if ( map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    try {
        std::vector<geo::modifier_t> stages;
        const auto status = PrepareTransformedStages( map, mesh, affine, stages );
        if ( status != map_status_t::OK || stages.empty() ) { return status; }
        bool changed = false;
        return WriteStages( map, mesh, stages, &changed, false, true );
    } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
}

} // namespace cypher::editor::map
