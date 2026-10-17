//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Map commands compose existing geometry construction, transform, and
// publication APIs. The host publishes a private document only after success.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_Edit.h"
#include "CypherMap_Modifiers.h"
#include "CypherMap_Groups.h"
#include "CypherMap_Hierarchy.h"
#include "CypherMap_ObjectCreate.h"
#include "CypherMap_Wireframe.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushShapes.h"
#include "CypherGeometry_BrushTransform.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentBrushReplacement.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_Snapshot.h"
#include "CypherGeometry_Primitive.h"
#include "CypherCommon/Mathlib/CypherMath_Affine3.h"
#include "CypherCommon/Mathlib/CypherMath_UV.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;
namespace
{
string_view_t SV( const char *p ) noexcept { return StringView_FromCString( p ); }
bool Finite( math::vec3d_t v ) noexcept { return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z ); }
bool InRange( math::vec3d_t v ) noexcept
{
    return Finite( v ) && std::abs( v.x ) <= geo::kMeshSourceCoordinateMax &&
        std::abs( v.y ) <= geo::kMeshSourceCoordinateMax && std::abs( v.z ) <= geo::kMeshSourceCoordinateMax;
}
map_status_t Status( geo::geometry_status_t s ) noexcept
{
    if ( s == geo::geometry_status_t::OK ) { return map_status_t::OK; }
    if ( s == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
    if ( s == geo::geometry_status_t::LIMIT_EXCEEDED ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}
template <typename T> bool Copy( vector_t<T> &out, const vector_t<T> &src, const allocator_t *a ) noexcept
{
    if ( !Vector_Init( &out, a, src.nCount ) ) { return false; }
    for ( usize i = 0; i < src.nCount; ++i ) { if ( !Vector_PushBack( &out, src.pData[i] ) ) { return false; } }
    return true;
}
bool CloneStore( settings_document_t &out, const settings_document_t &src, const allocator_t *a ) noexcept
{
    if ( src.pDocument == nullptr ) { return true; }
    if ( SettingsDocument_Init( &out, a, src.identity ) != settings_document_status_t::OK ) { return false; }
    auto *dst = out.pDocument;
    if ( !KeyValue_SetDocumentHeader( dst, KeyValue_DocumentHeader( src.pDocument ) ) ) { return false; }
    auto *root = KeyValue_Root( dst );
    const auto *source = SettingsDocument_Root( &src );
    if ( !KeyValue_SetComments( dst, root, KeyValue_Comments( source ) ) ) { return false; }
    for ( usize i = 0; i < KeyValue_ChildCount( source ); ++i ) {
        if ( KeyValue_CloneInto( dst, root, KeyValue_ChildAt( source, i ) ) == nullptr ) { return false; }
    }
    out.nLoadedVersion = src.nLoadedVersion;
    return true;
}
map_status_t Check( const map_document_t *m, span_t<const u64> ids ) noexcept
{
    if ( m == nullptr || m->pAllocator == nullptr || !Span_IsValid( ids ) || ids.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    return m->bReadOnly ? map_status_t::READ_ONLY : map_status_t::OK;
}
map_geometry_kind_t Kind( const map_document_t *m, u64 id ) noexcept
{
    const geo::geometry_source_id_t sid{ id };
    if ( geo::GeometryDocument_FindBrush( &m->geometry, sid ) ) { return map_geometry_kind_t::BRUSH; }
    if ( geo::GeometryDocument_FindMesh( &m->geometry, sid ) ) { return map_geometry_kind_t::MESH; }
    if ( geo::GeometryDocument_FindPatch( &m->geometry, sid ) ) { return map_geometry_kind_t::PATCH; }
    if ( geo::GeometryDocument_FindHeightField( &m->geometry, sid ) ) { return map_geometry_kind_t::TERRAIN; }
    return map_geometry_kind_t::COUNT;
}
const map_geometry_record_t *Placement( const map_document_t *m, u64 id ) noexcept
{
    for ( usize i = 0; i < m->geometryRecords.nCount; ++i ) {
        if ( m->geometryRecords.pData[i].id == id ) { return &m->geometryRecords.pData[i]; }
    }
    return nullptr;
}
key_value_t *FindParent( key_value_t *root, const key_value_t *child, usize depth = 0 ) noexcept
{
    if ( depth >= CY_KEY_VALUE_MAX_DEPTH ) { return nullptr; }
    for ( usize i = 0; i < KeyValue_ChildCount( root ); ++i ) {
        auto *item = KeyValue_ChildAt( root, i );
        if ( item == child ) { return root; }
        if ( auto *found = FindParent( item, child, depth + 1 ) ) { return found; }
    }
    return nullptr;
}
bool ReadVec( const key_value_t *node, math::vec3d_t &out ) noexcept
{
    return KeyValue_Type( node ) == key_value_type_t::ARRAY && KeyValue_ChildCount( node ) == 3 &&
        KeyValue_GetF64( KeyValue_ChildAt( node, 0 ), &out.x ) && KeyValue_GetF64( KeyValue_ChildAt( node, 1 ), &out.y ) &&
        KeyValue_GetF64( KeyValue_ChildAt( node, 2 ), &out.z );
}
bool WriteVec( key_value_document_t *doc, key_value_t *record, const char *name, math::vec3d_t value ) noexcept
{
    auto *node = KeyValue_Find( record, SV( name ) );
    if ( node && !KeyValue_Remove( doc, record, node ) ) { return false; }
    node = KeyValue_ObjectInsert( doc, record, SV( name ), key_value_type_t::ARRAY );
    if ( !node ) { return false; }
    for ( f64 v : { value.x, value.y, value.z } ) {
        auto *item = KeyValue_ArrayAppend( doc, node, key_value_type_t::F64 );
        if ( !item || !KeyValue_SetF64( doc, item, v ) ) { return false; }
    }
    return true;
}
// Include each entity-owned object once. Selecting both owner and child must
// not apply a transform twice, and an entity clone must carry all its geometry.
map_status_t Expand( const map_document_t *m, span_t<const u64> ids, vector_t<u64> &out,
                     const allocator_t *allocator = nullptr ) noexcept
{
    if ( !Vector_Init( &out, allocator != nullptr ? allocator : m->pAllocator, ids.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto add = [&]( u64 id ) noexcept {
        for ( usize i = 0; i < out.nCount; ++i ) { if ( out.pData[i] == id ) { return true; } }
        return Vector_PushBack( &out, id ) != CY_FALSE;
    };
    for ( usize i = 0; i < ids.nCount; ++i ) {
        // FindObject's existing interface is mutable, but this lookup only
        // tests identity and never changes the retained record.
        if ( ids.pData[i] == 0 || ( Kind( m, ids.pData[i] ) == map_geometry_kind_t::COUNT &&
            !MapDocument_FindObject( const_cast<map_document_t *>( m ), ids.pData[i], nullptr ) ) ) { return map_status_t::UNKNOWN_OBJECT; }
        if ( !add( ids.pData[i] ) ) { return map_status_t::OUT_OF_MEMORY; }
        for ( usize k = 0; k < m->geometryRecords.nCount; ++k ) {
            if ( m->geometryRecords.pData[k].owner == ids.pData[i] && !add( m->geometryRecords.pData[k].id ) ) {
                return map_status_t::OUT_OF_MEMORY;
            }
        }
    }
    // Entities attached to an expanded one (CYMAP `parent`) come along with
    // what they own, at any depth: Hammer 5's hierarchy (CypherMap_Hierarchy.h).
    try {
        const auto links = MapHierarchy_Links( m );
        for ( usize i = 0; !links.empty() && i < out.nCount; ++i ) { // out grows as children join.
            for ( const auto &[child, parent] : links ) {
                if ( parent != out.pData[i] ) { continue; }
                if ( !add( child ) ) { return map_status_t::OUT_OF_MEMORY; }
                for ( usize k = 0; k < m->geometryRecords.nCount; ++k ) {
                    if ( m->geometryRecords.pData[k].owner == child && !add( m->geometryRecords.pData[k].id ) ) { return map_status_t::OUT_OF_MEMORY; }
                }
            }
        }
    } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
    return map_status_t::OK;
}
enum class transform_kind_t { TRANSLATE, SCALE, ROTATE, AFFINE }; // AFFINE: any linear part in `rotation`, about `pivot`.

// [pitch, yaw, roll] degrees (CYMAP.md 3: yaw about +Z, then pitch, positive
// looking down, then roll about the facing axis) after `rotation` turns
// them: R = rotation * Rz(yaw) Ry(pitch) Rx(roll), read back the same way.
math::vec3d_t TurnAngles( math::vec3d_t angles, const math::affine3d_t &rotation ) noexcept
{
    constexpr f64 kRad = 3.14159265358979323846 / 180.0;
    const f64 cp = std::cos( angles.x * kRad ), sp = std::sin( angles.x * kRad );
    const f64 cy = std::cos( angles.y * kRad ), sy = std::sin( angles.y * kRad );
    const f64 cr = std::cos( angles.z * kRad ), sr = std::sin( angles.z * kRad );
    // Row-major 3x3 of the entity's orientation.
    const f64 a[3][3]{ { cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr },
                       { sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr },
                       { -sp, cp * sr, cp * cr } };
    // The rotation's linear part (affine columns are column-major).
    f64 r[3][3];
    for ( int row = 0; row < 3; ++row ) { for ( int col = 0; col < 3; ++col ) { r[row][col] = rotation.m[col * 3 + row]; } }
    f64 o[3][3]{};
    for ( int row = 0; row < 3; ++row ) {
        for ( int col = 0; col < 3; ++col ) { for ( int k = 0; k < 3; ++k ) { o[row][col] += r[row][k] * a[k][col]; } }
    }
    const f64 pitch = std::asin( std::clamp( -o[2][0], -1.0, 1.0 ) );
    f64 yaw = 0.0, roll = 0.0;
    if ( std::fabs( std::cos( pitch ) ) > 1e-9 ) {
        yaw = std::atan2( o[1][0], o[0][0] );
        roll = std::atan2( o[2][1], o[2][2] );
    } else {
        yaw = std::atan2( -o[0][1], o[1][1] ); // Looking straight up or down: roll folds into yaw.
    }
    const auto tidy = []( f64 degrees ) { const f64 rounded = std::round( degrees * 1e9 ) / 1e9; return rounded == -180.0 ? 180.0 : rounded + 0.0; };
    return { tidy( pitch / kRad ), tidy( yaw / kRad ), tidy( roll / kRad ) };
}
struct transform_t { transform_kind_t kind; math::vec3d_t amount; math::vec3d_t pivot; math::affine3d_t rotation; bool lock; };
math::affine3d_t Affine( const transform_t &t ) noexcept
{
    if ( t.kind == transform_kind_t::TRANSLATE ) { return math::Affine3d_FromTranslation( t.amount ); }
    const auto linear = t.kind == transform_kind_t::SCALE ? math::Affine3d_FromScale( t.amount ) : t.rotation;
    return math::Affine3d_Multiply( math::Affine3d_FromTranslation( t.pivot ),
        math::Affine3d_Multiply( linear, math::Affine3d_FromTranslation( { -t.pivot.x, -t.pivot.y, -t.pivot.z } ) ) );
}
bool IsPrefabRecord( const key_value_t *record, const map_layer_store_t *store ) noexcept
{
    const auto *prefabs = store ? KeyValue_Find( SettingsDocument_Root( &store->store ), SV( "prefabs" ) ) : nullptr;
    for ( usize i = 0; i < KeyValue_ChildCount( prefabs ); ++i ) {
        if ( KeyValue_ChildAt( prefabs, i ) == record ) { return true; }
    }
    return false;
}
struct prefab_scale_t { math::vec3d_t origin{}, scale{ 1, 1, 1 }; bool scalar{ false }, omitScale{ false }; };
bool PrefabNumber( const key_value_t *node, f64 &out ) noexcept
{
    i64 signedValue{}; u64 unsignedValue{};
    if ( KeyValue_GetF64( node, &out ) ) { return true; }
    if ( KeyValue_GetI64( node, &signedValue ) ) { out = static_cast<f64>( signedValue ); return true; }
    if ( KeyValue_GetU64( node, &unsignedValue ) ) { out = static_cast<f64>( unsignedValue ); return true; }
    return false;
}
bool PrefabVector( const key_value_t *node, math::vec3d_t &out ) noexcept
{
    return KeyValue_Type( node ) == key_value_type_t::ARRAY && KeyValue_ChildCount( node ) == 3 &&
        PrefabNumber( KeyValue_ChildAt( node, 0 ), out.x ) && PrefabNumber( KeyValue_ChildAt( node, 1 ), out.y ) &&
        PrefabNumber( KeyValue_ChildAt( node, 2 ), out.z );
}
void PrefabSinCosDegrees( f64 degrees, f64 &sine, f64 &cosine ) noexcept
{
    // Authored quarter turns have exact zero entries. Never snap a nearby
    // angle: a small nonzero angle can create large shear under anisotropy.
    const f64 turn = std::remainder( degrees, 360.0 );
    if ( turn == 0 ) { sine = 0; cosine = 1; }
    else if ( turn == 90 ) { sine = 1; cosine = 0; }
    else if ( turn == -90 ) { sine = -1; cosine = 0; }
    else if ( turn == 180 || turn == -180 ) { sine = 0; cosine = -1; }
    else {
        constexpr f64 rad = 3.14159265358979323846 / 180.0;
        sine = std::sin( degrees * rad ); cosine = std::cos( degrees * rad );
    }
}
map_status_t PreparePrefabScale( const key_value_t *record, const transform_t &t, prefab_scale_t &out ) noexcept
{
    math::vec3d_t origin{}, angles{}, scale{ 1, 1, 1 };
    const auto *originNode = KeyValue_Find( record, SV( "origin" ) ), *anglesNode = KeyValue_Find( record, SV( "angles" ) );
    const auto *scaleNode = KeyValue_Find( record, SV( "scale" ) );
    if ( ( originNode && !PrefabVector( originNode, origin ) ) || ( anglesNode && !PrefabVector( anglesNode, angles ) ) ) { return map_status_t::INVALID_ARGUMENT; }
    const bool array = scaleNode && KeyValue_Type( scaleNode ) == key_value_type_t::ARRAY;
    if ( array ) {
        if ( !PrefabVector( scaleNode, scale ) ) { return map_status_t::INVALID_ARGUMENT; }
    } else if ( scaleNode ) {
        if ( !PrefabNumber( scaleNode, scale.x ) ) { return map_status_t::INVALID_ARGUMENT; }
        scale.y = scale.z = scale.x;
    }
    if ( !Finite( origin ) || !Finite( angles ) || !Finite( scale ) || scale.x <= 0 || scale.y <= 0 || scale.z <= 0 ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    f64 cp{}, sp{}, cy{}, sy{}, cr{}, sr{};
    PrefabSinCosDegrees( angles.x, sp, cp ); PrefabSinCosDegrees( angles.y, sy, cy ); PrefabSinCosDegrees( angles.z, sr, cr );
    // L = Rz(yaw) Ry(pitch) Rx(roll) diag(scale), matching instance display.
    // A L can retain R only when C = R^T A R is diagonal. Otherwise it
    // needs shear, which the linked-instance record cannot represent.
    const f64 r[3][3]{ { cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr },
                       { sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr }, { -sp, cp * sr, cp * cr } };
    const f64 factors[]{ t.amount.x, t.amount.y, t.amount.z };
    constexpr f64 epsilon = std::numeric_limits<f64>::epsilon();
    f64 local[3]{};
    // Exact uniform factors commute with every rotation without a dot product.
    const bool uniform = factors[0] == factors[1] && factors[1] == factors[2];
    if ( uniform ) { local[0] = local[1] = local[2] = factors[0]; }
    for ( int i = 0; !uniform && i < 3; ++i ) {
        for ( int j = 0; j < 3; ++j ) {
            f64 value = 0, magnitude = 0;
            for ( int k = 0; k < 3; ++k ) {
                const f64 term = r[k][i] * factors[k] * r[k][j];
                value += term; magnitude += std::fabs( term );
            }
            if ( !std::isfinite( value ) || !std::isfinite( magnitude ) ) { return map_status_t::INVALID_ARGUMENT; }
            if ( i == j ) {
                if ( value <= 0 ) { return map_status_t::INVALID_ARGUMENT; }
                local[i] = value;
            } else if ( std::fabs( value ) > 32 * epsilon * magnitude ) {
                return map_status_t::PREFAB_TRANSFORM_UNSUPPORTED;
            }
        }
    }
    for ( int row = 0; row < 3; ++row ) {
        for ( int column = 0; column < 3; ++column ) {
            const f64 actual = r[row][column] * local[column], expected = factors[row] * r[row][column];
            // A large unrelated axis must not mask error on a small entry.
            // Ambiguous near-cancellations are conservatively refused.
            const f64 tolerance = 64 * epsilon * std::max( std::fabs( actual ), std::fabs( expected ) );
            if ( !std::isfinite( actual ) || !std::isfinite( expected ) || std::fabs( actual - expected ) > tolerance ) {
                return map_status_t::PREFAB_TRANSFORM_UNSUPPORTED;
            }
        }
    }
    out.origin = math::Affine3d_TransformPoint( Affine( t ), origin );
    out.scale = { scale.x * local[0], scale.y * local[1], scale.z * local[2] };
    if ( !InRange( out.origin ) || !Finite( out.scale ) || out.scale.x <= 0 || out.scale.y <= 0 || out.scale.z <= 0 ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    out.scalar = !array && out.scale.x == out.scale.y && out.scale.y == out.scale.z;
    out.omitScale = !scaleNode && out.scale.x == 1 && out.scale.y == 1 && out.scale.z == 1;
    return map_status_t::OK;
}
bool WritePrefabScale( key_value_document_t *document, key_value_t *record, const prefab_scale_t &value ) noexcept
{
    if ( value.omitScale ) { return true; }
    auto *old = KeyValue_Find( record, SV( "scale" ) );
    const auto comments = KeyValue_Comments( old );
    if ( value.scalar ) {
        if ( old && !KeyValue_Remove( document, record, old ) ) { return false; }
        auto *node = KeyValue_ObjectInsert( document, record, SV( "scale" ), key_value_type_t::F64 );
        if ( !node || !KeyValue_SetF64( document, node, value.scale.x ) ) { return false; }
    } else if ( !WriteVec( document, record, "scale", value.scale ) ) { return false; }
    return KeyValue_SetComments( document, KeyValue_Find( record, SV( "scale" ) ), comments ) != CY_FALSE;
}
map_status_t TransformOne( map_document_t *m, u64 id, const transform_t &t ) noexcept
{
    const geo::geometry_source_id_t sid{ id };
    const auto affine = Affine( t );
    switch ( Kind( m, id ) ) {
        case map_geometry_kind_t::BRUSH: {
            geo::brush_source_t copy{};
            auto s = geo::GeometryDocument_TryCopyBrushSource( &m->geometry, sid, m->pAllocator, &copy );
            if ( s != geo::geometry_status_t::OK ) { return Status( s ); }
            s = t.kind == transform_kind_t::TRANSLATE ? geo::BrushTransform_TryTranslate( &copy.solid, t.amount ) :
                t.kind == transform_kind_t::SCALE ? geo::BrushTransform_TryScale( &copy.solid, t.pivot, t.amount ) :
                t.kind == transform_kind_t::AFFINE ? geo::BrushTransform_TryApplyAffine( &copy.solid, affine ) :
                geo::BrushTransform_TryRotate( &copy.solid, t.pivot, t.rotation );
            if ( s == geo::geometry_status_t::OK && t.lock ) {
                s = t.kind == transform_kind_t::TRANSLATE ? geo::BrushTransform_TextureLockTranslate( &copy.solid, &copy.attributes, t.amount, m->geometryPolicy ) :
                    t.kind == transform_kind_t::AFFINE ? geo::BrushTransform_TextureLockAffine( &copy.solid, &copy.attributes, affine, m->geometryPolicy ) :
                    t.kind == transform_kind_t::SCALE ? geo::BrushTransform_TextureLockScale( &copy.solid, &copy.attributes, t.pivot, t.amount, m->geometryPolicy ) :
                    geo::BrushTransform_TextureLockRotate( &copy.solid, &copy.attributes, t.pivot, t.rotation, m->geometryPolicy );
            }
            if ( s == geo::geometry_status_t::OK ) {
                geo::geometry_status_t boundsStatus{};
                const auto bounds = MapGeometry_BrushBounds( copy, m->geometryPolicy, m->pAllocator, &boundsStatus );
                if ( boundsStatus != geo::geometry_status_t::OK ) { s = boundsStatus; }
                else if ( !bounds.bHas || !InRange( bounds.box.minimum ) || !InRange( bounds.box.maximum ) ) { s = geo::geometry_status_t::INVALID_ARGUMENT; }
            }
            if ( s == geo::geometry_status_t::OK ) { s = geo::GeometryDocument_TryReplaceBrushSourcesExact( &m->geometry, { &sid, 1 }, { &copy, 1 } ); }
            geo::BrushSource_Shutdown( &copy );
            return Status( s );
        }
        case map_geometry_kind_t::MESH: {
            const auto admitted = MapModifiers_CheckTransform( m, id, affine );
            if ( admitted != map_status_t::OK ) { return admitted; }
            geo::mesh_source_description_t desc{};
            auto s = geo::MeshSourceDescription_Init( &desc, m->pAllocator, sid );
            if ( s == geo::geometry_status_t::OK ) { s = geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &m->geometry, sid ), &desc ); }
            for ( usize i = 0; s == geo::geometry_status_t::OK && i < desc.vertices.nCount; ++i ) {
                desc.vertices.pData[i].position = math::Affine3d_TransformPoint( affine, desc.vertices.pData[i].position );
                if ( !InRange( desc.vertices.pData[i].position ) ) { s = geo::geometry_status_t::INVALID_ARGUMENT; }
            }
            geo::mesh_source_t copy{};
            if ( s == geo::geometry_status_t::OK ) { s = geo::MeshSource_TryBuild( &desc, m->pAllocator, &copy ); }
            if ( s == geo::geometry_status_t::OK ) { s = geo::GeometryDocument_TryReplaceMesh( &m->geometry, &copy ); }
            geo::MeshSource_Shutdown( &copy ); geo::MeshSourceDescription_Shutdown( &desc );
            // A modifier stack's planes and origins are world positions: they move with the mesh.
            if ( s == geo::geometry_status_t::OK ) { return MapModifiers_Transform( m, id, affine ); }
            return Status( s );
        }
        case map_geometry_kind_t::PATCH: {
            geo::patch_surface_t copy{};
            auto s = geo::Patch_TryClone( geo::GeometryDocument_FindPatch( &m->geometry, sid ), m->pAllocator, &copy );
            if ( s == geo::geometry_status_t::OK ) { s = geo::Patch_TryTransform( &copy, affine ); }
            if ( s == geo::geometry_status_t::OK ) { s = geo::GeometryDocument_TryReplacePatch( &m->geometry, &copy ); }
            geo::Patch_Shutdown( &copy ); return Status( s );
        }
        case map_geometry_kind_t::TERRAIN: {
            // A heightfield remains horizontal with square XY cells. Arbitrary
            // rotation/nonuniform XY scale requires conversion to mesh first.
            if ( t.kind == transform_kind_t::ROTATE || t.kind == transform_kind_t::AFFINE || ( t.kind == transform_kind_t::SCALE && t.amount.x != t.amount.y ) ) {
                return map_status_t::INVALID_ARGUMENT;
            }
            geo::heightfield_t copy{};
            auto s = geo::HeightField_TryClone( geo::GeometryDocument_FindHeightField( &m->geometry, sid ), m->pAllocator, &copy );
            if ( s == geo::geometry_status_t::OK ) {
                copy.origin = math::Affine3d_TransformPoint( affine, copy.origin );
                if ( t.kind == transform_kind_t::SCALE ) {
                    copy.cellSize *= t.amount.x;
                    for ( usize i = 0; i < copy.heights.nCount; ++i ) { copy.heights.pData[i] *= t.amount.z; }
                }
                const auto b = MapGeometry_TerrainBounds( copy );
                if ( !b.bHas || !InRange( b.box.minimum ) || !InRange( b.box.maximum ) ) { s = geo::geometry_status_t::INVALID_ARGUMENT; }
            }
            if ( s == geo::geometry_status_t::OK ) { s = geo::GeometryDocument_TryReplaceHeightField( &m->geometry, &copy ); }
            geo::HeightField_Shutdown( &copy ); return Status( s );
        }
        case map_geometry_kind_t::COUNT: {
            map_layer_store_t *layerStore = nullptr;
            auto *record = MapDocument_FindObject( m, id, &layerStore );
            if ( !record || !layerStore ) { return map_status_t::INVALID_ARGUMENT; }
            if ( t.kind == transform_kind_t::SCALE && IsPrefabRecord( record, layerStore ) ) {
                prefab_scale_t scaled{};
                const auto status = PreparePrefabScale( record, t, scaled );
                if ( status != map_status_t::OK ) { return status; }
                return WriteVec( layerStore->store.pDocument, record, "origin", scaled.origin ) &&
                    WritePrefabScale( layerStore->store.pDocument, record, scaled ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
            }
            // Point entities follow the pivot without changing their class
            // dimensions. Rotation composes onto entity and instance angles.
            const key_value_t *originNode = KeyValue_Find( record, SV( "origin" ) );
            if ( originNode == nullptr ) { return map_status_t::OK; } // A logic entity has no place to move.
            math::vec3d_t origin{};
            if ( !ReadVec( originNode, origin ) ) { return map_status_t::INVALID_ARGUMENT; }
            origin = math::Affine3d_TransformPoint( affine, origin );
            if ( !InRange( origin ) ) { return map_status_t::INVALID_ARGUMENT; }
            if ( !WriteVec( layerStore->store.pDocument, record, "origin", origin ) ) { return map_status_t::OUT_OF_MEMORY; }
            if ( t.kind != transform_kind_t::ROTATE ) { return map_status_t::OK; }
            math::vec3d_t angles{};
            if ( const key_value_t *anglesNode = KeyValue_Find( record, SV( "angles" ) ); anglesNode != nullptr && !ReadVec( anglesNode, angles ) ) {
                return map_status_t::INVALID_ARGUMENT;
            }
            angles = TurnAngles( angles, t.rotation );
            // CYMAP.md 6.2: angles are omitted when zero.
            if ( std::fabs( angles.x ) < 1e-7 && std::fabs( angles.y ) < 1e-7 && std::fabs( angles.z ) < 1e-7 ) {
                if ( key_value_t *pOld = KeyValue_Find( record, SV( "angles" ) ) ) {
                    if ( !KeyValue_Remove( layerStore->store.pDocument, record, pOld ) ) { return map_status_t::OUT_OF_MEMORY; }
                }
                return map_status_t::OK;
            }
            return WriteVec( layerStore->store.pDocument, record, "angles", angles ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
        }
    }
    return map_status_t::INVALID_ARGUMENT;
}
map_status_t Transform( map_document_t *m, span_t<const u64> ids, const transform_t &t ) noexcept
{
    auto s = Check( m, ids );
    if ( s != map_status_t::OK || !Finite( t.amount ) || !Finite( t.pivot ) ) { return s == map_status_t::OK ? map_status_t::INVALID_ARGUMENT : s; }
    vector_t<u64> expanded{};
    s = Expand( m, ids, expanded );
    // A later mesh's unrepresentable live modifier must not leave an earlier
    // target transformed, even within the private working copy.
    for ( usize i = 0; s == map_status_t::OK && i < expanded.nCount; ++i ) {
        if ( Kind( m, expanded.pData[i] ) == map_geometry_kind_t::MESH ) {
            s = MapModifiers_CheckTransform( m, expanded.pData[i], Affine( t ) );
        } else if ( t.kind == transform_kind_t::SCALE && Kind( m, expanded.pData[i] ) == map_geometry_kind_t::COUNT ) {
            map_layer_store_t *store = nullptr;
            const auto *record = MapDocument_FindObject( m, expanded.pData[i], &store );
            if ( IsPrefabRecord( record, store ) ) { prefab_scale_t scaled{}; s = PreparePrefabScale( record, t, scaled ); }
        }
    }
    for ( usize i = 0; s == map_status_t::OK && i < expanded.nCount; ++i ) { s = TransformOne( m, expanded.pData[i], t ); }
    if ( s == map_status_t::OK ) { ++m->geometry.revision; }
    return s;
}
// Writes canonical geometry beside retained host fields, for duplication.
bool WriteGeometry( map_document_t *m, u64 id, map_geometry_kind_t kind, key_value_document_t *doc, key_value_t *out ) noexcept
{
    const geo::geometry_source_id_t sid{ id };
    // Geometry writers emit geometric members only; root identity belongs to
    // the map's record and must be present before the ID-remapping walk.
    auto *rootId = KeyValue_ObjectInsert( doc, out, SV( "id" ), key_value_type_t::U64 );
    if ( !rootId || !KeyValue_SetU64( doc, rootId, id ) ) { return false; }
    bool ok = false;
    if ( kind == map_geometry_kind_t::BRUSH ) {
        geo::brush_source_t copy{};
        if ( geo::GeometryDocument_TryCopyBrushSource( &m->geometry, sid, m->pAllocator, &copy ) == geo::geometry_status_t::OK ) {
            ok = MapGeometry_WriteBrush( copy, m->materials, doc, out );
        }
        geo::BrushSource_Shutdown( &copy );
    } else if ( kind == map_geometry_kind_t::MESH ) { ok = MapGeometry_WriteMesh( *geo::GeometryDocument_FindMesh( &m->geometry, sid ), m->materials, m->pAllocator, doc, out ); }
    else if ( kind == map_geometry_kind_t::PATCH ) { ok = MapGeometry_WritePatch( *geo::GeometryDocument_FindPatch( &m->geometry, sid ), m->materials, doc, out ); }
    else if ( kind == map_geometry_kind_t::TERRAIN ) { ok = MapGeometry_WriteTerrain( *geo::GeometryDocument_FindHeightField( &m->geometry, sid ), doc, out ); }
    if ( !ok ) { return false; }
    if ( const auto *old = MapDocument_FindObject( m, id, nullptr ) ) {
        for ( usize i = 0; i < KeyValue_ChildCount( old ); ++i ) {
            const auto *child = KeyValue_ChildAt( old, i );
            if ( !MapGeometry_IsGeometryMember( kind, KeyValue_Name( child ) ) && !StringView_Equals( KeyValue_Name( child ), SV( "id" ) ) ) {
                if ( !KeyValue_CloneInto( doc, out, child ) ) { return false; }
            }
        }
        if ( !MapGeometry_MergeResidual( kind, doc, out, old ) ) { return false; }
    }
    return true;
}
usize AddSaturated( usize a, usize b ) noexcept { return b > CY_USIZE_MAX - a ? CY_USIZE_MAX : a + b; }
template <typename T> usize Bytes( const vector_t<T> &v ) noexcept { return v.nCapacity * sizeof( T ); }
template <typename T, typename Tag> usize Bytes( const generation_pool_t<T, Tag> &v ) noexcept { return v.cSlots * sizeof( generation_pool_slot_t<T> ); }
} // namespace

map_status_t MapEdit_CollectAffectedObjects( const map_document_t *m, span_t<const u64> ids, vector_t<u64> *out ) noexcept
{
    if ( m == nullptr || !Allocator_IsValid( m->pAllocator ) || !Span_IsValid( ids ) ||
         !Vector_IsValid( out ) || !Allocator_IsValid( out->pAllocator ) ) { return map_status_t::INVALID_ARGUMENT; }
    vector_t<u64> candidate{};
    // The source span may borrow out's storage. Keep it alive until the
    // complete closure has been prepared, including an empty replacement.
    const auto status = ids.nCount == 0
        ? ( Vector_Init( &candidate, out->pAllocator ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY )
        : Expand( m, ids, candidate, out->pAllocator );
    if ( status != map_status_t::OK ) { return status; }
    Vector_Shutdown( out );
    Vector_Move( out, &candidate );
    return map_status_t::OK;
}

map_status_t MapEdit_Clone( const map_document_t *src, map_document_t **out ) noexcept
{
    if ( !src || !src->pAllocator || !out || *out ) { return map_status_t::INVALID_ARGUMENT; }
    auto *copy = new ( std::nothrow ) map_document_t{};
    if ( !copy ) { return map_status_t::OUT_OF_MEMORY; }
    copy->pAllocator = src->pAllocator;
    const auto fail = [&]( map_status_t s ) noexcept { delete copy; return s; };
    if ( !CloneStore( copy->root, src->root, src->pAllocator ) || !Copy( copy->layers, src->layers, src->pAllocator ) ||
        !Copy( copy->geometryRecords, src->geometryRecords, src->pAllocator ) || !Copy( copy->problems, src->problems, src->pAllocator ) ||
        !Copy( copy->materials.text, src->materials.text, src->pAllocator ) || !Copy( copy->materials.entries, src->materials.entries, src->pAllocator ) ||
        !Copy( copy->materials.sorted, src->materials.sorted, src->pAllocator ) || !Vector_Init( &copy->layerStores, src->pAllocator, src->layerStores.nCount ) ) {
        return fail( map_status_t::OUT_OF_MEMORY );
    }
    for ( usize i = 0; i < src->layerStores.nCount; ++i ) {
        void *memory = Allocator_Allocate( src->pAllocator, sizeof( map_layer_store_t ), alignof( map_layer_store_t ) );
        if ( !memory ) { return fail( map_status_t::OUT_OF_MEMORY ); }
        auto *layerStore = new ( memory ) map_layer_store_t{};
        // Publish ownership immediately: failure cleanup can now free it.
        if ( !Vector_PushBack( &copy->layerStores, layerStore ) ) {
            layerStore->~map_layer_store_t(); Allocator_Free( src->pAllocator, layerStore, sizeof( map_layer_store_t ), alignof( map_layer_store_t ) );
            return fail( map_status_t::OUT_OF_MEMORY );
        }
        const auto &old = *src->layerStores.pData[i];
        std::memcpy( layerStore->layer, old.layer, sizeof( layerStore->layer ) );
        if ( !CloneStore( layerStore->store, old.store, src->pAllocator ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    }
    geo::geometry_snapshot_t snapshot{};
    auto gs = geo::GeometrySnapshot_TakeFromDocument( &snapshot, &src->geometry );
    if ( gs != geo::geometry_status_t::OK ) { return fail( Status( gs ) ); }
    gs = geo::GeometrySourceIdRegistry_TryClone( &src->geometry.sourceIds, &copy->geometry.sourceIds );
    if ( gs != geo::geometry_status_t::OK ) { geo::GeometrySnapshot_Shutdown( &snapshot ); return fail( Status( gs ) ); }
    auto &g = copy->geometry;
    Vector_Move( &g.brushes, &snapshot.brushes ); Vector_Move( &g.brushAttributes, &snapshot.brushAttributes );
    Vector_Move( &g.meshes, &snapshot.meshes ); Vector_Move( &g.patches, &snapshot.patches ); Vector_Move( &g.heightFields, &snapshot.heightFields );
    g.pAllocator = src->pAllocator; g.policy = snapshot.policy; g.revision = snapshot.revision;
    geo::GeometrySnapshot_Shutdown( &snapshot );
    copy->geometryPolicy = src->geometryPolicy; copy->mapId = src->mapId;
    copy->nextId = src->nextId; copy->bReadOnly = src->bReadOnly; copy->bIdsAssigned = src->bIdsAssigned;
    std::memcpy( copy->creationLayer, src->creationLayer, sizeof( copy->creationLayer ) );
    *out = copy; return map_status_t::OK;
}

usize MapEdit_EstimateBytes( const map_document_t *m ) noexcept
{
    if ( !m ) { return 0; }
    usize n = sizeof( *m ) + Bytes( m->layerStores ) + Bytes( m->layers ) + Bytes( m->geometryRecords ) + Bytes( m->problems ) +
        Bytes( m->materials.text ) + Bytes( m->materials.entries ) + Bytes( m->materials.sorted ) + KeyValue_OwnedBytes( m->root.pDocument );
    for ( usize i = 0; i < m->layerStores.nCount; ++i ) { n = AddSaturated( n, sizeof( map_layer_store_t ) + KeyValue_OwnedBytes( m->layerStores.pData[i]->store.pDocument ) ); }
    const auto &g = m->geometry;
    n = AddSaturated( n, Bytes( g.brushes ) + Bytes( g.brushAttributes ) + Bytes( g.meshes ) + Bytes( g.patches ) + Bytes( g.heightFields ) );
    n = AddSaturated( n, ( g.sourceIds.claimedIds.nCapacity + g.sourceIds.liveIds.nCapacity ) * sizeof( *g.sourceIds.liveIds.pSlots ) );
    for ( usize i = 0; i < g.brushes.nCount; ++i ) { n = AddSaturated( n, sizeof( geo::brush_solid_t ) + Bytes( g.brushes.pData[i]->sides ) + sizeof( geo::geometry_brush_side_attribute_store_t ) + Bytes( g.brushAttributes.pData[i]->records ) ); }
    for ( usize i = 0; i < g.meshes.nCount; ++i ) {
        const auto &s = *g.meshes.pData[i]; const auto &mesh = s.mesh;
        n = AddSaturated( n, sizeof( s ) + Bytes( s.vertexIds ) + Bytes( s.faceIds ) + Bytes( s.attributes.corners ) + Bytes( s.attributes.faces ) + Bytes( s.attributes.edges ) +
            Bytes( mesh.vertices ) + Bytes( mesh.halfEdges ) + Bytes( mesh.edges ) + Bytes( mesh.loops ) + Bytes( mesh.faces ) + Bytes( mesh.shells ) );
    }
    for ( usize i = 0; i < g.patches.nCount; ++i ) { n = AddSaturated( n, sizeof( geo::patch_surface_t ) + Bytes( g.patches.pData[i]->controls ) ); }
    for ( usize i = 0; i < g.heightFields.nCount; ++i ) { const auto &f = *g.heightFields.pData[i]; n = AddSaturated( n, sizeof( f ) + Bytes( f.heights ) + Bytes( f.holes ) + Bytes( f.tiles ) ); }
    return n;
}

namespace
{
map_status_t CreatePrimitive( map_document_t *m, const map_primitive_desc_t &desc,
    string_view_t material, string_view_t layer, u64 *idOut, bool sphereMesh ) noexcept
{
    const auto bounds = desc.bounds;
    if ( !m || !m->pAllocator || !idOut || !InRange( bounds.minimum ) || !InRange( bounds.maximum ) ||
        desc.kind >= map_primitive_kind_t::COUNT || !std::isfinite( desc.worldUnitsPerUv ) || desc.worldUnitsPerUv <= 0.0 ||
        !StringView_IsValid( material ) || material.cchLength > MAP_MATERIAL_PATH_MAX || !StringView_IsValid( layer ) ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    const bool quad = desc.kind == map_primitive_kind_t::QUAD;
    if ( quad && desc.axis > 2u ) { return map_status_t::INVALID_ARGUMENT; }
    const f64 low[]{ bounds.minimum.x, bounds.minimum.y, bounds.minimum.z };
    const f64 high[]{ bounds.maximum.x, bounds.maximum.y, bounds.maximum.z };
    for ( u32 axis = 0; axis < 3u; ++axis ) {
        if ( quad && axis == desc.axis ? high[axis] < low[axis] : high[axis] <= low[axis] ) { return map_status_t::INVALID_ARGUMENT; }
    }
    for ( usize i = 0; i < material.cchLength; ++i ) {
        if ( static_cast<unsigned char>( material.pData[i] ) < 0x20u || material.pData[i] == '\\' ) { return map_status_t::INVALID_ARGUMENT; }
    }
    if ( m->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( sphereMesh && desc.kind != map_primitive_kind_t::SPHERE ) { return map_status_t::INVALID_ARGUMENT; }
    if ( sphereMesh && desc.sphereSubdivisions > 1u ) { return map_status_t::LIMIT_EXCEEDED; }
    if ( MapPrimitive_IsMesh( desc.kind ) ) {
        // Arch, stairs, spiral stairs, pipe, torus: one mesh each.
        if ( layer.cchLength == 0 && m->layers.nCount ) { layer = MapDocument_CreationLayer( m ); }
        if ( !MapDocument_HasLayer( m, layer ) ) { return map_status_t::UNKNOWN_LAYER; }
        u64 id = 0;
        auto result = MapCreate_BlockShape( m, desc, material, &id );
        if ( result == map_status_t::OK ) { result = MapDocument_SetGeometryLayer( m, id, layer ); }
        if ( result == map_status_t::OK ) { *idOut = id; }
        return result;
    }
    if ( quad || sphereMesh ) {
        if ( layer.cchLength == 0 && m->layers.nCount ) { layer = MapDocument_CreationLayer( m ); }
        if ( !MapDocument_HasLayer( m, layer ) ) { return map_status_t::UNKNOWN_LAYER; }
        if ( m->nextId == 0 || m->geometry.revision == CY_U64_MAX ||
             geo::GeometrySourceIdAllocator_IsExhausted( &m->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
        u64 ref = 0;
        auto result = MapDocument_MaterialRef( m, material, &ref );
        geo::geometry_fragment_t fragment{};
        if ( result == map_status_t::OK ) { result = Status( geo::GeometryFragment_Init( &fragment, m->pAllocator ) ); }
        geo::geometry_source_id_allocator_t allocator{ { std::max( m->nextId, m->geometry.sourceIds.allocator.next.value ) } };
        geo::geometry_primitive_t primitive{};
        primitive.kind = sphereMesh ? geo::geometry_primitive_kind_t::SPHERE : geo::geometry_primitive_kind_t::PLANE;
        primitive.axis = sphereMesh ? 2u : desc.axis; primitive.box = { bounds.minimum, bounds.maximum };
        if ( sphereMesh ) {
            primitive.cSides = desc.sphereSubdivisions == 0u ? 16u : 24u;
            primitive.cRings = desc.sphereSubdivisions == 0u ? 8u : 10u;
        }
        primitive.material.value = ref; primitive.worldUnitsPerUv = { desc.worldUnitsPerUv, desc.worldUnitsPerUv };
        if ( result == map_status_t::OK ) {
            result = Status( geo::Primitive_TryBuild( primitive, geo::geometry_primitive_output_t::MESH,
                m->geometryPolicy, &allocator, &fragment ) );
        }
        u64 id = 0;
        if ( result == map_status_t::OK ) {
            id = fragment.meshes.pData[0]->sourceId.value;
            result = Status( geo::GeometryFragment_TryInsert( &fragment, &m->geometry, nullptr ) );
        }
        if ( result == map_status_t::OK ) { m->nextId = allocator.next.value; result = MapDocument_SetGeometryLayer( m, id, layer ); }
        if ( result == map_status_t::OK ) { *idOut = id; ++m->geometry.revision; }
        geo::GeometryFragment_Shutdown( &fragment ); return result;
    }
    u32 faceCount = 0;
    switch ( desc.kind ) {
        case map_primitive_kind_t::BOX: faceCount = 6; break;
        case map_primitive_kind_t::WEDGE:
            if ( desc.wedgeCutAxis > 2 || desc.wedgeSlopeAxis > 2 || desc.wedgeCutAxis == desc.wedgeSlopeAxis ) { return map_status_t::INVALID_ARGUMENT; }
            faceCount = 5; break;
        case map_primitive_kind_t::CYLINDER:
        case map_primitive_kind_t::CONE:
            if ( desc.axis > 2 || desc.nSides < 3 || !std::isfinite( desc.coneTopRadiusRatio ) ||
                desc.coneTopRadiusRatio < 0.0 || desc.coneTopRadiusRatio > 1.0 ) { return map_status_t::INVALID_ARGUMENT; }
            if ( desc.nSides > 128 ) { return map_status_t::LIMIT_EXCEEDED; }
            faceCount = desc.nSides + ( desc.kind == map_primitive_kind_t::CONE && desc.coneTopRadiusRatio == 0.0 ? 1u : 2u );
            break;
        case map_primitive_kind_t::SPHERE:
            if ( desc.sphereSubdivisions > 1 ) { return map_status_t::LIMIT_EXCEEDED; }
            faceCount = desc.sphereSubdivisions == 0 ? 16u * 8u : 24u * 10u; break;
        default: return map_status_t::INVALID_ARGUMENT;
    }
    if ( faceCount > MAP_BRUSH_FACES_MAX || faceCount > m->geometryPolicy.limits.cBrushSidesPerBrushMax ) { return map_status_t::LIMIT_EXCEEDED; }
    if ( layer.cchLength == 0 && m->layers.nCount ) { layer = MapDocument_CreationLayer( m ); }
    if ( !MapDocument_HasLayer( m, layer ) ) { return map_status_t::UNKNOWN_LAYER; }
    if ( m->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &m->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    geo::geometry_source_id_allocator_t allocator{ { std::max( m->nextId, m->geometry.sourceIds.allocator.next.value ) } };
    const math::vec3d_t center{ ( bounds.minimum.x + bounds.maximum.x ) / 2, ( bounds.minimum.y + bounds.maximum.y ) / 2, ( bounds.minimum.z + bounds.maximum.z ) / 2 };
    const math::vec3d_t half{ ( bounds.maximum.x - bounds.minimum.x ) / 2, ( bounds.maximum.y - bounds.minimum.y ) / 2, ( bounds.maximum.z - bounds.minimum.z ) / 2 };
    geo::brush_solid_t solid{}; geo::brush_source_t source{};
    auto s = geo::geometry_status_t::INVALID_ARGUMENT;
    switch ( desc.kind ) {
        case map_primitive_kind_t::BOX:
            s = geo::BrushGenerator_TryMakeBox( &solid, m->pAllocator, m->geometryPolicy, &allocator, center, half ); break;
        case map_primitive_kind_t::WEDGE:
            s = geo::BrushGenerator_TryMakeWedge( &solid, m->pAllocator, m->geometryPolicy, &allocator, center, half, desc.wedgeCutAxis, desc.wedgeSlopeAxis ); break;
        case map_primitive_kind_t::CYLINDER:
            s = geo::BrushGenerator_TryMakeCylinder( &solid, m->pAllocator, m->geometryPolicy, &allocator, {}, 1.0, 1.0, desc.nSides, desc.axis ); break;
        case map_primitive_kind_t::CONE:
            s = geo::BrushGenerator_TryMakeCone( &solid, m->pAllocator, m->geometryPolicy, &allocator, {}, 1.0, desc.coneTopRadiusRatio, 1.0, desc.nSides, desc.axis ); break;
        case map_primitive_kind_t::SPHERE:
            // Latitude bands give the Block tool a round silhouette and
            // useful quad loops for component editing. Both detail levels
            // fit the 256-side brush limit; existing authored maps are not
            // regenerated when this construction default changes.
            s = geo::BrushShapes_TryMakeUvSphere( &solid, m->pAllocator, m->geometryPolicy, &allocator,
                { { -1, -1, -1 }, { 1, 1, 1 } }, desc.sphereSubdivisions == 0 ? 16u : 24u,
                desc.sphereSubdivisions == 0 ? 8u : 10u, geo::brush_circle_mode_t::VERTEX_ALIGNED ); break;
        default: break;
    }
    // The canonical generator defines a regular cross-section. Transform its
    // planes through the backend to fit the frame, including elliptical round
    // shapes; the inverse-transpose keeps each normal normalized.
    if ( desc.kind == map_primitive_kind_t::CYLINDER || desc.kind == map_primitive_kind_t::CONE || desc.kind == map_primitive_kind_t::SPHERE ) {
        if ( s == geo::geometry_status_t::OK ) { s = geo::BrushTransform_TryScale( &solid, {}, half ); }
        if ( s == geo::geometry_status_t::OK ) { s = geo::BrushTransform_TryTranslate( &solid, center ); }
    }
    if ( s == geo::geometry_status_t::OK ) { s = geo::BrushValidation_Deep( &solid, m->geometryPolicy, m->pAllocator ).status; }
    if ( s == geo::geometry_status_t::OK ) { s = geo::BrushSource_TryBuildDefault( &solid, m->pAllocator, m->geometryPolicy, &source ); }
    u64 ref = 0;
    auto result = Status( s );
    if ( result == map_status_t::OK ) { result = MapDocument_MaterialRef( m, material, &ref ); }
    if ( result == map_status_t::OK ) {
        for ( usize i = 0; i < source.attributes.records.nCount; ++i ) {
            auto &record = source.attributes.records.pData[i]; record.material.value = ref;
            const auto normal = source.solid.sides.pData[i].plane.normal;
            const math::vec3d_t up = std::abs( normal.z ) > 0.9 ? math::vec3d_t{ 0, 1, 0 } : math::vec3d_t{ 0, 0, 1 };
            // A usable projection on every face from the first brush: the
            // generic default XY mapping collapses on vertical side faces.
            if ( !math::Uvd_TryBuildPlanarMapping( {}, normal, up, { desc.worldUnitsPerUv, desc.worldUnitsPerUv }, 0, {},
                m->geometryPolicy.numerical.fAbsoluteDistanceTolerance, &record.uvProjection ) ) { result = map_status_t::GEOMETRY_FAILED; break; }
        }
        if ( result == map_status_t::OK ) { result = Status( geo::GeometryDocument_TryAddBrushSource( &m->geometry, &source ) ); }
    }
    const u64 id = solid.sourceId.value;
    if ( result == map_status_t::OK ) { m->nextId = allocator.next.value; result = MapDocument_SetGeometryLayer( m, id, layer ); }
    if ( result == map_status_t::OK ) { *idOut = id; ++m->geometry.revision; }
    geo::BrushSource_Shutdown( &source ); geo::BrushSolid_Shutdown( &solid ); return result;
}
}

map_status_t MapEdit_CreatePrimitive( map_document_t *m, const map_primitive_desc_t &desc,
    string_view_t material, string_view_t layer, u64 *idOut ) noexcept
{
    return CreatePrimitive( m, desc, material, layer, idOut, false );
}

map_status_t MapEdit_CreateSphereMesh( map_document_t *m, const map_primitive_desc_t &desc,
    string_view_t material, string_view_t layer, u64 *idOut ) noexcept
{
    return CreatePrimitive( m, desc, material, layer, idOut, true );
}

map_status_t MapEdit_CreateBox( map_document_t *m, math::aabbd_t bounds, string_view_t material, string_view_t layer, u64 *idOut ) noexcept
{
    map_primitive_desc_t desc{}; desc.bounds = bounds;
    return MapEdit_CreatePrimitive( m, desc, material, layer, idOut );
}

map_status_t MapEdit_Translate( map_document_t *m, span_t<const u64> ids, math::vec3d_t delta, bool lock ) noexcept
{ return Transform( m, ids, { transform_kind_t::TRANSLATE, delta, {}, math::CY_AFFINE3D_IDENTITY, lock } ); }
map_status_t MapEdit_Scale( map_document_t *m, span_t<const u64> ids, math::vec3d_t factors, math::vec3d_t pivot, bool lock ) noexcept
{
    if ( factors.x <= 0 || factors.y <= 0 || factors.z <= 0 ) { return map_status_t::INVALID_ARGUMENT; }
    return Transform( m, ids, { transform_kind_t::SCALE, factors, pivot, math::CY_AFFINE3D_IDENTITY, lock } );
}
map_status_t MapEdit_ResizeTransform( math::aabbd_t bounds, const map_bounds_resize_t &resize,
                                   math::vec3d_t *factorsOut, math::vec3d_t *pivotOut ) noexcept
{
    if ( !factorsOut || !pivotOut || factorsOut == pivotOut || !Finite( bounds.minimum ) || !Finite( bounds.maximum ) ||
         !Finite( resize.sides ) || !Finite( resize.delta ) ) { return map_status_t::INVALID_ARGUMENT; }
    const f64 minimum[]{ bounds.minimum.x, bounds.minimum.y, bounds.minimum.z };
    const f64 maximum[]{ bounds.maximum.x, bounds.maximum.y, bounds.maximum.z };
    const f64 sides[]{ resize.sides.x, resize.sides.y, resize.sides.z };
    const f64 delta[]{ resize.delta.x, resize.delta.y, resize.delta.z };
    f64 factors[]{ 1, 1, 1 }, pivot[3]{};
    bool active = false;
    for ( usize axis = 0; axis < 3; ++axis ) {
        const f64 extent = maximum[axis] - minimum[axis];
        if ( !std::isfinite( extent ) || extent < 0 || ( sides[axis] != -1 && sides[axis] != 0 && sides[axis] != 1 ) ||
             ( sides[axis] == 0 && delta[axis] != 0 ) ) { return map_status_t::INVALID_ARGUMENT; }
        pivot[axis] = minimum[axis] + extent * 0.5;
        if ( sides[axis] == 0 ) { continue; }
        active = true;
        const f64 resized = extent + sides[axis] * delta[axis] * ( resize.bFromCenter ? 2.0 : 1.0 );
        if ( extent <= 0 || !std::isfinite( resized ) || resized <= 0 ) { return map_status_t::INVALID_ARGUMENT; }
        factors[axis] = resized / extent;
        if ( !std::isfinite( factors[axis] ) || factors[axis] <= 0 ) { return map_status_t::INVALID_ARGUMENT; }
        if ( !resize.bFromCenter ) { pivot[axis] = sides[axis] > 0 ? minimum[axis] : maximum[axis]; }
    }
    if ( !active ) { return map_status_t::INVALID_ARGUMENT; }
    *factorsOut = { factors[0], factors[1], factors[2] };
    *pivotOut = { pivot[0], pivot[1], pivot[2] };
    return map_status_t::OK;
}
map_status_t MapEdit_Resize( map_document_t *m, span_t<const u64> ids, const map_bounds_resize_t &resize, bool lock ) noexcept
{
    auto status = Check( m, ids );
    if ( status != map_status_t::OK ) { return status; }
    // Use the identical reconstructed bounds shown in the viewports, including
    // plane-defined brushes and patch control nets. A failed build cannot
    // become an empty target or hide an allocation failure.
    map_wireframe_t original;
    if ( !MapWireframe_Init( &original, m->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    status = MapWireframe_Build( &original, *m );
    if ( status != map_status_t::OK ) { return status; }
    struct target_t { u64 id; math::vec3d_t factors, pivot; };
    vector_t<target_t> targets{};
    if ( !Vector_Init( &targets, m->pAllocator, ids.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    bool changed = false;
    for ( usize i = 0; i < ids.nCount; ++i ) {
        const u64 id = ids.pData[i];
        bool duplicate = false;
        for ( usize k = 0; k < targets.nCount; ++k ) { duplicate |= targets.pData[k].id == id; }
        if ( duplicate ) { continue; }
        const auto *object = MapWireframe_FindObject( original, id );
        if ( !object ) { return map_status_t::UNKNOWN_OBJECT; }
        if ( !object->bounds.bHas || ( object->kind != map_wire_kind_t::BRUSH && object->kind != map_wire_kind_t::MESH &&
             object->kind != map_wire_kind_t::PATCH ) ) { return map_status_t::INVALID_ARGUMENT; }
        target_t target{ id, {}, {} };
        status = MapEdit_ResizeTransform( object->bounds.box, resize, &target.factors, &target.pivot );
        if ( status != map_status_t::OK ) { return status; }
        changed |= target.factors.x != 1 || target.factors.y != 1 || target.factors.z != 1;
        if ( !Vector_PushBack( &targets, target ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    if ( !changed ) { return map_status_t::OK; }
    if ( m->geometry.revision == CY_U64_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    for ( usize i = 0; i < targets.nCount; ++i ) {
        const auto &target = targets.pData[i];
        if ( Kind( m, target.id ) == map_geometry_kind_t::MESH ) {
            status = MapModifiers_CheckTransform( m, target.id, Affine( { transform_kind_t::SCALE, target.factors, target.pivot, math::CY_AFFINE3D_IDENTITY, lock } ) );
            if ( status != map_status_t::OK ) { return status; }
        }
    }
    for ( usize i = 0; i < targets.nCount; ++i ) {
        const auto &target = targets.pData[i];
        if ( target.factors.x == 1 && target.factors.y == 1 && target.factors.z == 1 ) { continue; }
        status = TransformOne( m, target.id, { transform_kind_t::SCALE, target.factors, target.pivot, math::CY_AFFINE3D_IDENTITY, lock } );
        if ( status != map_status_t::OK ) { return status; }
    }
    ++m->geometry.revision;
    return map_status_t::OK;
}
map_status_t MapEdit_Shear( map_document_t *m, span_t<const u64> ids, u32 axis, math::vec3d_t slide, math::vec3d_t pivot, bool lock ) noexcept
{
    const f64 along[3]{ slide.x, slide.y, slide.z };
    if ( axis > 2u || along[axis] != 0.0 || !Finite( slide ) ) { return map_status_t::INVALID_ARGUMENT; }
    // Columns of I + slide * e_axis^T: the axis column gains the slide.
    math::affine3d_t linear = math::CY_AFFINE3D_IDENTITY;
    linear.m[axis * 3 + 0] += slide.x;
    linear.m[axis * 3 + 1] += slide.y;
    linear.m[axis * 3 + 2] += slide.z;
    return Transform( m, ids, { transform_kind_t::AFFINE, {}, pivot, linear, lock } );
}
map_status_t MapEdit_Rotate( map_document_t *m, span_t<const u64> ids, math::vec3d_t degrees, math::vec3d_t pivot, bool lock ) noexcept
{
    if ( !Finite( degrees ) ) { return map_status_t::INVALID_ARGUMENT; }
    constexpr f64 toRadians = 3.14159265358979323846 / 180.0;
    const f64 x = std::remainder( degrees.x, 360.0 ) * toRadians, y = std::remainder( degrees.y, 360.0 ) * toRadians, z = std::remainder( degrees.z, 360.0 ) * toRadians;
    const f64 cx = std::cos( x ), sx = std::sin( x ), cy = std::cos( y ), sy = std::sin( y ), cz = std::cos( z ), sz = std::sin( z );
    const auto rotation = math::Affine3d_FromColumns( { cz * cy, sz * cy, -sy }, { cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx },
        { cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx }, {} );
    return Transform( m, ids, { transform_kind_t::ROTATE, degrees, pivot, rotation, lock } );
}
map_status_t MapEdit_Delete( map_document_t *m, span_t<const u64> ids ) noexcept
{
    auto s = Check( m, ids ); if ( s != map_status_t::OK ) { return s; }
    vector_t<u64> expanded{}; s = Expand( m, ids, expanded );
    for ( usize i = 0; s == map_status_t::OK && i < expanded.nCount; ++i ) {
        s = MapDocument_RemoveObject( m, expanded.pData[i] );
        // An owner removed its children already.
        if ( s == map_status_t::UNKNOWN_OBJECT ) { s = map_status_t::OK; }
    }
    if ( s == map_status_t::OK ) { ++m->geometry.revision; } return s;
}

map_status_t MapEdit_Duplicate( map_document_t *m, span_t<const u64> ids, math::vec3d_t offset, vector_t<u64> *idsOut ) noexcept
{
    auto s = Check( m, ids );
    if ( s != map_status_t::OK ) { return s; }
    if ( !idsOut || !Vector_IsValid( idsOut ) || !Allocator_IsValid( idsOut->pAllocator ) || !Finite( offset ) ) { return map_status_t::INVALID_ARGUMENT; }
    Vector_Clear( idsOut );
    vector_t<u64> expanded{}; s = Expand( m, ids, expanded ); if ( s != map_status_t::OK ) { return s; }
    for ( usize i = 0; i < expanded.nCount; ++i ) {
        if ( Kind( m, expanded.pData[i] ) == map_geometry_kind_t::MESH ) {
            s = MapModifiers_CheckTransform( m, expanded.pData[i], math::Affine3d_FromTranslation( offset ) );
            if ( s != map_status_t::OK ) { return s; }
        }
    }
    struct copied_t { u64 oldId; u64 newId; };
    vector_t<copied_t> copied{};
    if ( !Vector_Init( &copied, m->pAllocator, expanded.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    // Entity records must precede their owned geometry irrespective of the
    // caller's selection order, so the new ownership reference can resolve.
    std::sort( expanded.pData, expanded.pData + expanded.nCount, [&]( u64 a, u64 b ) {
        const bool ownerA = Kind( m, a ) == map_geometry_kind_t::COUNT, ownerB = Kind( m, b ) == map_geometry_kind_t::COUNT;
        return ownerA != ownerB ? ownerA > ownerB : a < b;
    } );
    for ( usize i = 0; i < expanded.nCount; ++i ) {
        const u64 oldId = expanded.pData[i]; const auto kind = Kind( m, oldId );
        if ( m->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &m->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
        geo::geometry_source_id_allocator_t allocator{ { std::max( m->nextId, m->geometry.sourceIds.allocator.next.value ) } };
        u64 newId = 0;
        map_layer_store_t *oldStore = nullptr; auto *oldRecord = MapDocument_FindObject( m, oldId, &oldStore );
        if ( kind == map_geometry_kind_t::COUNT ) {
            // Point entities, prefab instances, notes and other origin-bearing
            // records retain their complete unknown fields. A generic record
            // with embedded IDs cannot be cloned safely by rewriting root only.
            if ( !oldRecord || !oldStore || KeyValue_Find( oldRecord, SV( "brushes" ) ) || KeyValue_Find( oldRecord, SV( "meshes" ) ) || KeyValue_Find( oldRecord, SV( "patches" ) ) ) {
                // Loaded brush entities retain stripped nested records. Copy
                // the owner without those; canonical children are copied below.
                if ( !oldRecord || !oldStore || !KeyValue_Find( oldRecord, SV( "class" ) ) ) { return map_status_t::INVALID_ARGUMENT; }
            }
            for ( auto childKind : { map_geometry_kind_t::BRUSH, map_geometry_kind_t::MESH, map_geometry_kind_t::PATCH } ) {
                const auto *children = KeyValue_Find( oldRecord, SV( MapGeometry_SectionName( childKind ) ) );
                for ( usize k = 0; k < KeyValue_ChildCount( children ); ++k ) {
                    const auto *child = KeyValue_ChildAt( children, k ); u64 childId = 0;
                    if ( !KeyValue_GetU64( KeyValue_Find( child, SV( "id" ) ), &childId ) || Kind( m, childId ) != childKind ) {
                        // A future/unreadable record cannot be silently removed
                        // from the new owner while copying only known children.
                        return map_status_t::INVALID_ARGUMENT;
                    }
                }
            }
            auto *parent = FindParent( KeyValue_Root( oldStore->store.pDocument ), oldRecord );
            auto *record = parent ? KeyValue_CloneInto( oldStore->store.pDocument, parent, oldRecord ) : nullptr;
            if ( !record ) { return map_status_t::OUT_OF_MEMORY; }
            for ( const char *section : { "brushes", "meshes", "patches" } ) {
                if ( auto *node = KeyValue_Find( record, SV( section ) ) ) { if ( !KeyValue_Remove( oldStore->store.pDocument, record, node ) ) { return map_status_t::OUT_OF_MEMORY; } }
            }
            const auto allocated = geo::GeometrySourceIdAllocator_Allocate( &allocator );
            if ( allocated.status != geo::geometry_status_t::OK ) { return Status( allocated.status ); }
            newId = allocated.id.value;
            if ( !KeyValue_SetU64( oldStore->store.pDocument, KeyValue_Find( record, SV( "id" ) ), newId ) ) { return map_status_t::OUT_OF_MEMORY; }
            m->nextId = allocator.next.value;
        } else {
            key_value_document_desc_t desc{}; desc.pAllocator = m->pAllocator;
            auto *doc = KeyValue_CreateDocument( desc );
            if ( !doc ) { return map_status_t::OUT_OF_MEMORY; }
            const auto cleanup = [&]() { KeyValue_DestroyDocument( doc ); };
            if ( !KeyValue_SetRootType( doc, key_value_type_t::OBJECT ) || !WriteGeometry( m, oldId, kind, doc, KeyValue_Root( doc ) ) ) { cleanup(); return map_status_t::OUT_OF_MEMORY; }
            struct remap_t { key_value_document_t *doc; geo::geometry_source_id_allocator_t *allocator; u64 oldRoot; u64 newRoot; } remap{ doc, &allocator, oldId, 0 };
            if ( !MapGeometry_ForEachId( kind, KeyValue_Root( doc ), []( void *ctx, key_value_t *slot, u64 old ) noexcept -> bool_t {
                auto &r = *static_cast<remap_t *>( ctx ); const auto fresh = geo::GeometrySourceIdAllocator_Allocate( r.allocator );
                if ( fresh.status != geo::geometry_status_t::OK ) { return CY_FALSE; }
                if ( old == r.oldRoot ) { r.newRoot = fresh.id.value; }
                return KeyValue_SetU64( r.doc, slot, fresh.id.value );
            }, &remap ) ) { cleanup(); return map_status_t::OUT_OF_MEMORY; }
            newId = remap.newRoot;
            const auto read = MapGeometry_Read( kind, KeyValue_Root( doc ), &m->materials, &m->geometry, m->geometryPolicy );
            if ( read.status != map_geometry_read_status_t::OK ) { cleanup(); return read.status == map_geometry_read_status_t::OUT_OF_MEMORY ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED; }
            m->nextId = allocator.next.value;
            const auto *placement = Placement( m, oldId );
            const u32 layerIndex = placement ? placement->iLayer : 0;
            u64 owner = placement ? placement->owner : 0;
            for ( usize k = 0; k < copied.nCount; ++k ) { if ( copied.pData[k].oldId == owner ) { owner = copied.pData[k].newId; break; } }
            s = MapDocument_SetGeometryLayer( m, newId, SV( m->layers.pData[layerIndex].id ) );
            if ( s == map_status_t::OK && owner ) { s = MapDocument_SetGeometryOwner( m, newId, owner ); }
            // Retain the copied metadata in its original record container.
            // Save rehomes it beside canonical geometry according to placement.
            if ( s == map_status_t::OK && oldRecord && oldStore ) {
                auto *parent = FindParent( KeyValue_Root( oldStore->store.pDocument ), oldRecord );
                if ( owner ) {
                    map_layer_store_t *ownerStore = nullptr; auto *ownerRecord = MapDocument_FindObject( m, owner, &ownerStore );
                    if ( ownerRecord && ownerStore ) {
                        oldStore = ownerStore; parent = KeyValue_Find( ownerRecord, SV( MapGeometry_SectionName( kind ) ) );
                        if ( !parent ) { parent = KeyValue_ObjectInsert( ownerStore->store.pDocument, ownerRecord, SV( MapGeometry_SectionName( kind ) ), key_value_type_t::ARRAY ); }
                    }
                }
                if ( !MapGeometry_StripRecord( kind, doc, KeyValue_Root( doc ) ) || !parent || !KeyValue_CloneInto( oldStore->store.pDocument, parent, KeyValue_Root( doc ) ) ) { s = map_status_t::OUT_OF_MEMORY; }
            }
            cleanup(); if ( s != map_status_t::OK ) { return s; }
        }
        if ( !Vector_PushBack( &copied, copied_t{ oldId, newId } ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    // Preserve the selection's root order, not implementation sorting.
    for ( usize k = 0; k < ids.nCount; ++k ) {
        for ( usize i = 0; i < copied.nCount; ++i ) {
            if ( copied.pData[i].oldId == ids.pData[k] && !Vector_PushBack( idsOut, copied.pData[i].newId ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
    }
    // Expanded children were copied already; use every copied root once to
    // translate, rather than expanding owners and transforming children twice.
    const transform_t t{ transform_kind_t::TRANSLATE, offset, {}, math::CY_AFFINE3D_IDENTITY, true };
    for ( usize i = 0; i < copied.nCount; ++i ) { s = TransformOne( m, copied.pData[i].newId, t ); if ( s != map_status_t::OK ) { return s; } }
    // Copied groups come along with the copies (CypherMap_Groups.h).
    std::vector<std::pair<u64, u64>> pairs;
    for ( usize i = 0; i < copied.nCount; ++i ) { pairs.emplace_back( copied.pData[i].oldId, copied.pData[i].newId ); }
    s = MapGroups_Copied( m, pairs, nullptr ); if ( s != map_status_t::OK ) { return s; }
    // Copied children hang from their copied parent.
    s = MapHierarchy_Copied( m, pairs ); if ( s != map_status_t::OK ) { return s; }
    ++m->geometry.revision; return map_status_t::OK;
}

} // namespace cypher::editor::map
