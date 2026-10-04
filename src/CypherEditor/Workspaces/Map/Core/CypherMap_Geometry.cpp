//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Geometry.cpp
//  Purpose: Implements the map's readable geometry codec and material table.
//  Details: Planes are written as `[nx, ny, nz, distance]` with n . p =
//           distance (the library stores n . p + d = 0, so distance = -d).
//           Hand-written planes whose normal is not unit length are
//           normalised on read; normals already within 1e-9 of unit length
//           are kept bit-exact so saving never drifts.
//
//           Surfacing is stored per face. A brush whose faces share one
//           surface record in memory is written with the values on every
//           face and reads back with one record per face - the same surfaces,
//           and the Hammer model of a face owning its own texture mapping.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Geometry.h"

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace cypher::editor::map
{

using namespace cypher::common;
namespace geo = cypher::editor::geometry;

namespace
{

constexpr f64 kPi = 3.14159265358979323846;

CYPHER_NODISCARD string_view_t SV( const char *pText ) noexcept
{
    return StringView_FromCString( pText );
}

// An ID member: a nonzero u64, or a positive integer a person typed without
// the `u` suffix.
CYPHER_NODISCARD bool_t ReadId( const key_value_t *pValue, u64 &idOut ) noexcept
{
    i64 nSigned = 0;
    if ( KeyValue_GetU64( pValue, &idOut ) ) { return idOut != 0u; }
    if ( KeyValue_GetI64( pValue, &nSigned ) && nSigned > 0 ) {
        idOut = static_cast<u64>( nSigned );
        return CY_TRUE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ReadNumber( const key_value_t *pValue, f64 &valueOut ) noexcept
{
    i64 nSigned = 0;
    u64 nUnsigned = 0u;
    if ( KeyValue_GetF64( pValue, &valueOut ) ) { return std::isfinite( valueOut ); }
    if ( KeyValue_GetI64( pValue, &nSigned ) ) { valueOut = static_cast<f64>( nSigned ); return CY_TRUE; }
    if ( KeyValue_GetU64( pValue, &nUnsigned ) ) { valueOut = static_cast<f64>( nUnsigned ); return CY_TRUE; }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ReadCount( const key_value_t *pValue, u64 &countOut ) noexcept
{
    i64 nSigned = 0;
    if ( KeyValue_GetU64( pValue, &countOut ) ) { return CY_TRUE; }
    if ( KeyValue_GetI64( pValue, &nSigned ) && nSigned >= 0 ) {
        countOut = static_cast<u64>( nSigned );
        return CY_TRUE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ReadNumbers( const key_value_t *pValue, f64 *pOut, usize nCount ) noexcept
{
    if ( KeyValue_Type( pValue ) != key_value_type_t::ARRAY || KeyValue_ChildCount( pValue ) != nCount ) { return CY_FALSE; }
    for ( usize i = 0u; i < nCount; ++i ) {
        if ( !ReadNumber( KeyValue_ChildAt( pValue, i ), pOut[i] ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadVec3( const key_value_t *pValue, math::vec3d_t &out ) noexcept
{
    f64 v[3]{};
    if ( !ReadNumbers( pValue, v, 3u ) ) { return CY_FALSE; }
    out = math::Vec3d_Make( v[0], v[1], v[2] );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadVec2( const key_value_t *pValue, math::vec2d_t &out ) noexcept
{
    f64 v[2]{};
    if ( !ReadNumbers( pValue, v, 2u ) ) { return CY_FALSE; }
    out = math::vec2d_t{ v[0], v[1] };
    return CY_TRUE;
}

// Optional member: absent keeps the default; present but wrong fails.
template <typename read_fn_t, typename value_t>
CYPHER_NODISCARD bool_t ReadOptional( const key_value_t *pObject, const char *pName, value_t &value, read_fn_t read ) noexcept
{
    const key_value_t *pMember = KeyValue_Find( pObject, SV( pName ) );
    return pMember == nullptr || read( pMember, value );
}

CYPHER_NODISCARD bool_t HexDigit( char c, u32 &out ) noexcept
{
    if ( c >= '0' && c <= '9' ) { out = static_cast<u32>( c - '0' ); return CY_TRUE; }
    if ( c >= 'a' && c <= 'f' ) { out = static_cast<u32>( c - 'a' + 10 ); return CY_TRUE; }
    if ( c >= 'A' && c <= 'F' ) { out = static_cast<u32>( c - 'A' + 10 ); return CY_TRUE; }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ReadColor( const key_value_t *pValue, u32 &rgbaOut ) noexcept
{
    string_view_t text{};
    if ( !KeyValue_GetString( pValue, &text ) || ( text.cchLength != 7u && text.cchLength != 9u ) || text.pData[0] != '#' ) {
        return CY_FALSE;
    }
    u32 value = 0u;
    for ( usize i = 1u; i < text.cchLength; ++i ) {
        u32 digit = 0u;
        if ( !HexDigit( text.pData[i], digit ) ) { return CY_FALSE; }
        value = ( value << 4u ) | digit;
    }
    rgbaOut = text.cchLength == 7u ? ( value << 8u ) | 0xFFu : value;
    return CY_TRUE;
}

f64 DegreesFromRadians( f64 radians ) noexcept
{
    // Rounded to MAP_ANGLE_ROUNDING so reading the degrees back and
    // converting again lands on the same written value: saves never drift.
    const f64 scale = 1.0 / MAP_ANGLE_ROUNDING;
    return MapReal( std::round( radians * ( 180.0 / kPi ) * scale ) / scale );
}

// ---------------------------------------------------------------------------
// Writing helpers
// ---------------------------------------------------------------------------

struct out_t {
    key_value_document_t *pDocument;
    bool_t bOk{ CY_TRUE };

    key_value_t *Insert( key_value_t *pObject, const char *pName, key_value_type_t type ) noexcept
    {
        if ( !bOk ) { return nullptr; }
        key_value_t *pNode = KeyValue_ObjectInsert( pDocument, pObject, SV( pName ), type );
        bOk = pNode != nullptr;
        return pNode;
    }
    key_value_t *Append( key_value_t *pArray, key_value_type_t type ) noexcept
    {
        if ( !bOk ) { return nullptr; }
        key_value_t *pNode = KeyValue_ArrayAppend( pDocument, pArray, type );
        bOk = pNode != nullptr;
        return pNode;
    }
    void SetReal( key_value_t *pNode, f64 value ) noexcept { bOk = bOk && pNode != nullptr && KeyValue_SetF64( pDocument, pNode, MapReal( value ) ); }
    void SetU64( key_value_t *pNode, u64 value ) noexcept { bOk = bOk && pNode != nullptr && KeyValue_SetU64( pDocument, pNode, value ); }
    void SetI64( key_value_t *pNode, i64 value ) noexcept { bOk = bOk && pNode != nullptr && KeyValue_SetI64( pDocument, pNode, value ); }
    void SetBool( key_value_t *pNode, bool_t value ) noexcept { bOk = bOk && pNode != nullptr && KeyValue_SetBool( pDocument, pNode, value ); }
    void SetText( key_value_t *pNode, string_view_t value ) noexcept { bOk = bOk && pNode != nullptr && KeyValue_SetString( pDocument, pNode, value ); }

    void Real( key_value_t *pObject, const char *pName, f64 value ) noexcept { SetReal( Insert( pObject, pName, key_value_type_t::NULL_VALUE ), value ); }
    void Id( key_value_t *pObject, const char *pName, u64 value ) noexcept { SetU64( Insert( pObject, pName, key_value_type_t::NULL_VALUE ), value ); }
    void Int( key_value_t *pObject, const char *pName, i64 value ) noexcept { SetI64( Insert( pObject, pName, key_value_type_t::NULL_VALUE ), value ); }
    void Text( key_value_t *pObject, const char *pName, string_view_t value ) noexcept { SetText( Insert( pObject, pName, key_value_type_t::NULL_VALUE ), value ); }
    void Bool( key_value_t *pObject, const char *pName, bool_t value ) noexcept { SetBool( Insert( pObject, pName, key_value_type_t::NULL_VALUE ), value ); }

    void Reals( key_value_t *pArray, const f64 *pValues, usize nCount ) noexcept
    {
        for ( usize i = 0u; i < nCount; ++i ) { SetReal( Append( pArray, key_value_type_t::NULL_VALUE ), pValues[i] ); }
    }
    void RealsMember( key_value_t *pObject, const char *pName, const f64 *pValues, usize nCount ) noexcept
    {
        key_value_t *pArray = Insert( pObject, pName, key_value_type_t::ARRAY );
        if ( pArray != nullptr ) { Reals( pArray, pValues, nCount ); }
    }
    void Vec3( key_value_t *pObject, const char *pName, math::vec3d_t v ) noexcept
    {
        const f64 values[3]{ v.x, v.y, v.z };
        RealsMember( pObject, pName, values, 3u );
    }
    void Vec2( key_value_t *pObject, const char *pName, math::vec2d_t v ) noexcept
    {
        const f64 values[2]{ v.x, v.y };
        RealsMember( pObject, pName, values, 2u );
    }
    void Color( key_value_t *pNode, u32 rgba ) noexcept
    {
        char text[10]{};
        if ( ( rgba & 0xFFu ) == 0xFFu ) {
            std::snprintf( text, sizeof( text ), "#%06x", rgba >> 8u );
        } else {
            std::snprintf( text, sizeof( text ), "#%08x", rgba );
        }
        SetText( pNode, SV( text ) );
    }
};

CYPHER_NODISCARD bool_t Vec3Equal( math::vec3d_t a, math::vec3d_t b ) noexcept
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// ---------------------------------------------------------------------------
// ID helpers
// ---------------------------------------------------------------------------

// One ID slot in a record: a member, or an element of an ID array.
template <typename visit_t>
CYPHER_NODISCARD bool_t VisitIdSlots( map_geometry_kind_t kind, key_value_t *pRecord, visit_t &&visit ) noexcept
{
    // visit( pParent, pName, pSlot ) returns false to stop. pSlot is null
    // when a member is absent.
    if ( !visit( pRecord, "id", KeyValue_Find( pRecord, SV( "id" ) ) ) ) { return CY_FALSE; }
    const auto visitArray = [&]( const char *pArrayName ) noexcept {
        key_value_t *pArray = KeyValue_Find( pRecord, SV( pArrayName ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
            if ( !visit( pArray, nullptr, KeyValue_ChildAt( pArray, i ) ) ) { return CY_FALSE; }
        }
        return CY_TRUE;
    };
    const auto visitFaces = [&]() noexcept {
        key_value_t *pFaces = KeyValue_Find( pRecord, SV( "faces" ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pFaces ); ++i ) {
            key_value_t *pFace = KeyValue_ChildAt( pFaces, i );
            if ( KeyValue_Type( pFace ) == key_value_type_t::OBJECT && !visit( pFace, "id", KeyValue_Find( pFace, SV( "id" ) ) ) ) {
                return CY_FALSE;
            }
        }
        return CY_TRUE;
    };
    switch ( kind ) {
        case map_geometry_kind_t::BRUSH: return visitFaces();
        case map_geometry_kind_t::MESH: return visitArray( "vertex_ids" ) && visitFaces();
        case map_geometry_kind_t::PATCH: return visitArray( "control_ids" );
        case map_geometry_kind_t::TERRAIN: return visitArray( "tile_ids" );
        case map_geometry_kind_t::COUNT: break;
    }
    return CY_TRUE;
}

// Number of IDs an ID array must hold, from the record's own shape; 0 when
// the shape is unreadable (the record will be kept verbatim anyway).
CYPHER_NODISCARD usize RequiredIdCount( map_geometry_kind_t kind, const key_value_t *pRecord ) noexcept
{
    u64 a = 0u, b = 0u, t = 0u;
    switch ( kind ) {
        case map_geometry_kind_t::MESH: return KeyValue_ChildCount( KeyValue_Find( pRecord, SV( "vertices" ) ) );
        case map_geometry_kind_t::PATCH:
            if ( ReadCount( KeyValue_Find( pRecord, SV( "columns" ) ), a ) && ReadCount( KeyValue_Find( pRecord, SV( "rows" ) ), b ) &&
                 a <= geo::kPatchControlsPerAxisMax && b <= geo::kPatchControlsPerAxisMax ) {
                return static_cast<usize>( a * b );
            }
            return 0u;
        case map_geometry_kind_t::TERRAIN: {
            const key_value_t *pCells = KeyValue_Find( pRecord, SV( "cells" ) );
            if ( KeyValue_ChildCount( pCells ) == 2u && ReadCount( KeyValue_ChildAt( pCells, 0u ), a ) &&
                 ReadCount( KeyValue_ChildAt( pCells, 1u ), b ) && ReadCount( KeyValue_Find( pRecord, SV( "tile_cells" ) ), t ) &&
                 t != 0u && a % t == 0u && b % t == 0u && a <= geo::kHeightFieldCellsPerAxisMax && b <= geo::kHeightFieldCellsPerAxisMax ) {
                return static_cast<usize>( ( a / t ) * ( b / t ) );
            }
            return 0u;
        }
        default: return 0u;
    }
}

CYPHER_NODISCARD const char *IdArrayName( map_geometry_kind_t kind ) noexcept
{
    switch ( kind ) {
        case map_geometry_kind_t::MESH: return "vertex_ids";
        case map_geometry_kind_t::PATCH: return "control_ids";
        case map_geometry_kind_t::TERRAIN: return "tile_ids";
        default: return nullptr;
    }
}

CYPHER_NODISCARD map_geometry_read_result_t Result( map_geometry_read_status_t status, u64 id, const char *pReason ) noexcept
{
    return { status, id, pReason };
}

CYPHER_NODISCARD map_geometry_read_result_t FromGeometryStatus( geo::geometry_status_t status, u64 id, const char *pReason ) noexcept
{
    if ( status == geo::geometry_status_t::OK ) { return Result( map_geometry_read_status_t::OK, id, "" ); }
    if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { return Result( map_geometry_read_status_t::OUT_OF_MEMORY, id, pReason ); }
    if ( status == geo::geometry_status_t::IDENTITY_CONFLICT ) { return Result( map_geometry_read_status_t::IDENTITY_CONFLICT, id, pReason ); }
    return Result( map_geometry_read_status_t::INVALID, id, pReason );
}

// ---------------------------------------------------------------------------
// Readers
// ---------------------------------------------------------------------------

CYPHER_NODISCARD map_geometry_read_result_t ReadBrush(
    const key_value_t *pRecord,
    map_materials_t *pMaterials,
    geo::geometry_document_t *pGeometry,
    const geo::geometry_policy_t &policy ) noexcept
{
    u64 id = 0u;
    if ( !ReadId( KeyValue_Find( pRecord, SV( "id" ) ), id ) ) { return Result( map_geometry_read_status_t::INVALID, 0u, "id" ); }
    const key_value_t *pFaces = KeyValue_Find( pRecord, SV( "faces" ) );
    const usize nFaces = KeyValue_Type( pFaces ) == key_value_type_t::ARRAY ? KeyValue_ChildCount( pFaces ) : 0u;
    if ( nFaces < MAP_BRUSH_FACES_MIN || nFaces > MAP_BRUSH_FACES_MAX ) { return Result( map_geometry_read_status_t::INVALID, id, "faces" ); }

    const allocator_t *pAllocator = pGeometry->pAllocator;
    geo::brush_solid_t solid{};
    geo::geometry_brush_side_attribute_store_t store{};
    geo::geometry_status_t status = geo::BrushSolid_Init( &solid, pAllocator, geo::geometry_source_id_t{ id } );
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushSolid_TryReserve( &solid, policy.limits, nFaces ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushSideAttributeStore_Init( &store, pAllocator ); }
    map_geometry_read_result_t result = FromGeometryStatus( status, id, "faces" );
    for ( usize i = 0u; result.status == map_geometry_read_status_t::OK && i < nFaces; ++i ) {
        const key_value_t *pFace = KeyValue_ChildAt( pFaces, i );
        u64 faceId = 0u;
        f64 plane[4]{};
        if ( KeyValue_Type( pFace ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( pFace, SV( "id" ) ), faceId ) ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "faces[].id" );
            break;
        }
        if ( !ReadNumbers( KeyValue_Find( pFace, SV( "plane" ) ), plane, 4u ) ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "faces[].plane" );
            break;
        }
        math::vec3d_t normal = math::Vec3d_Make( plane[0], plane[1], plane[2] );
        f64 distance = plane[3];
        const f64 length = std::sqrt( normal.x * normal.x + normal.y * normal.y + normal.z * normal.z );
        if ( !( length > 1.0e-12 ) ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "faces[].plane" );
            break;
        }
        if ( std::abs( length - 1.0 ) > 1.0e-9 ) {
            // A hand-written normal such as [ 0, 0, 2 ]; exact unit normals stay bit-identical.
            normal = math::Vec3d_Make( normal.x / length, normal.y / length, normal.z / length );
            distance /= length;
        }

        geo::geometry_brush_side_attributes_t attributes = geo::BrushSideAttributes_MakeDefault();
        math::planar_uv_mappingd_t &uv = attributes.uvProjection;
        uv.normal = normal;
        uv.origin = math::CY_VEC3D_ZERO;
        uv.worldUnitsPerUv = math::vec2d_t{ 1.0, 1.0 };
        uv.offset = math::vec2d_t{ 0.0, 0.0 };
        uv.rotationRadians = 0.0;
        f64 rotationDegrees = 0.0;
        string_view_t material{};
        const key_value_t *pMaterial = KeyValue_Find( pFace, SV( "material" ) );
        const bool_t bUv = ReadVec3( KeyValue_Find( pFace, SV( "uv_u" ) ), uv.uAxis ) && ReadVec3( KeyValue_Find( pFace, SV( "uv_v" ) ), uv.vAxis ) &&
                           ReadOptional( pFace, "uv_origin", uv.origin, ReadVec3 ) && ReadOptional( pFace, "uv_normal", uv.normal, ReadVec3 ) &&
                           ReadOptional( pFace, "uv_size", uv.worldUnitsPerUv, ReadVec2 ) &&
                           ReadOptional( pFace, "uv_rotation", rotationDegrees, ReadNumber ) &&
                           ReadOptional( pFace, "uv_offset", uv.offset, ReadVec2 );
        if ( !bUv || ( pMaterial != nullptr && !KeyValue_GetString( pMaterial, &material ) ) ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "faces[].uv" );
            break;
        }
        uv.rotationRadians = rotationDegrees * ( kPi / 180.0 );
        if ( !MapMaterials_Intern( pMaterials, material, &attributes.material.value ) ) {
            result = Result( material.cchLength > MAP_MATERIAL_PATH_MAX ? map_geometry_read_status_t::INVALID : map_geometry_read_status_t::OUT_OF_MEMORY,
                             id, "faces[].material" );
            break;
        }
        usize iAttribute = 0u;
        status = geo::BrushSideAttributeStore_TryAppend( &store, policy, attributes, &iAttribute );
        if ( status != geo::geometry_status_t::OK ) {
            result = FromGeometryStatus( status, id, "faces[].uv" );
            break;
        }
        geo::brush_solid_side_t side{};
        side.plane.normal = normal;
        side.plane.d = -distance;
        side.sourceId = geo::geometry_source_id_t{ faceId };
        side.iAttributeIndex = static_cast<decltype( side.iAttributeIndex )>( iAttribute );
        status = geo::BrushSolid_TryAddSide( &solid, policy.limits, side, nullptr );
        if ( status != geo::geometry_status_t::OK ) { result = FromGeometryStatus( status, id, "faces" ); }
    }
    if ( result.status == map_geometry_read_status_t::OK ) {
        result = FromGeometryStatus( geo::GeometryDocument_TryAddBrushWithAttributes( pGeometry, &solid, &store ), id, "faces" );
    }
    geo::BrushSideAttributeStore_Shutdown( &store );
    geo::BrushSolid_Shutdown( &solid );
    return result;
}

CYPHER_NODISCARD map_geometry_read_result_t ReadMesh(
    const key_value_t *pRecord,
    map_materials_t *pMaterials,
    geo::geometry_document_t *pGeometry ) noexcept
{
    u64 id = 0u;
    if ( !ReadId( KeyValue_Find( pRecord, SV( "id" ) ), id ) ) { return Result( map_geometry_read_status_t::INVALID, 0u, "id" ); }
    const key_value_t *pVertices = KeyValue_Find( pRecord, SV( "vertices" ) );
    const key_value_t *pVertexIds = KeyValue_Find( pRecord, SV( "vertex_ids" ) );
    const key_value_t *pFaces = KeyValue_Find( pRecord, SV( "faces" ) );
    const usize nVertices = KeyValue_ChildCount( pVertices );
    if ( KeyValue_Type( pVertices ) != key_value_type_t::ARRAY || KeyValue_Type( pFaces ) != key_value_type_t::ARRAY ||
         KeyValue_Type( pVertexIds ) != key_value_type_t::ARRAY || KeyValue_ChildCount( pVertexIds ) != nVertices ||
         nVertices > geo::kMeshSourceVerticesMax ) {
        return Result( map_geometry_read_status_t::INVALID, id, "vertices" );
    }
    const allocator_t *pAllocator = pGeometry->pAllocator;
    geo::mesh_source_description_t desc{};
    geo::mesh_source_t mesh{};
    map_geometry_read_result_t result = FromGeometryStatus(
        geo::MeshSourceDescription_Init( &desc, pAllocator, geo::geometry_source_id_t{ id } ), id, "vertices" );
    for ( usize i = 0u; result.status == map_geometry_read_status_t::OK && i < nVertices; ++i ) {
        math::vec3d_t position{};
        u64 vertexId = 0u;
        if ( !ReadVec3( KeyValue_ChildAt( pVertices, i ), position ) || !ReadId( KeyValue_ChildAt( pVertexIds, i ), vertexId ) ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "vertices" );
            break;
        }
        result = FromGeometryStatus( geo::MeshSourceDescription_TryAddVertex( &desc, position, geo::geometry_source_id_t{ vertexId }, nullptr ),
                                     id, "vertices" );
    }
    u32 indices[MAP_MESH_FACE_VERTICES_MAX]{};
    for ( usize iFace = 0u; result.status == map_geometry_read_status_t::OK && iFace < KeyValue_ChildCount( pFaces ); ++iFace ) {
        const key_value_t *pFace = KeyValue_ChildAt( pFaces, iFace );
        const key_value_t *pCorners = KeyValue_Find( pFace, SV( "vertices" ) );
        const usize nCorners = KeyValue_ChildCount( pCorners );
        u64 faceId = 0u;
        if ( KeyValue_Type( pFace ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( pFace, SV( "id" ) ), faceId ) ||
             KeyValue_Type( pCorners ) != key_value_type_t::ARRAY || nCorners < 3u || nCorners > MAP_MESH_FACE_VERTICES_MAX ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "faces[]" );
            break;
        }
        for ( usize k = 0u; k < nCorners; ++k ) {
            u64 index = 0u;
            if ( !ReadCount( KeyValue_ChildAt( pCorners, k ), index ) || index >= nVertices ) {
                result = Result( map_geometry_read_status_t::INVALID, id, "faces[].vertices" );
                break;
            }
            indices[k] = static_cast<u32>( index );
        }
        if ( result.status != map_geometry_read_status_t::OK ) { break; }
        geo::mesh_face_attributes_t attributes{};
        string_view_t material{};
        u64 smoothing = 1u;
        const key_value_t *pMaterial = KeyValue_Find( pFace, SV( "material" ) );
        if ( ( pMaterial != nullptr && !KeyValue_GetString( pMaterial, &material ) ) ||
             !ReadOptional( pFace, "smoothing", smoothing, ReadCount ) || smoothing > CY_U32_MAX ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "faces[]" );
            break;
        }
        if ( !MapMaterials_Intern( pMaterials, material, &attributes.material.value ) ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "faces[].material" );
            break;
        }
        attributes.smoothingGroups = static_cast<u32>( smoothing );
        u32 iNewFace = 0u;
        result = FromGeometryStatus( geo::MeshSourceDescription_TryAddFace( &desc, { indices, nCorners }, geo::geometry_source_id_t{ faceId },
                                                                           attributes, &iNewFace ),
                                     id, "faces[]" );
        if ( result.status != map_geometry_read_status_t::OK ) { break; }
        const usize iFirstCorner = desc.faces.pData[iNewFace].iFirstCorner;
        const key_value_t *pUv = KeyValue_Find( pFace, SV( "uv" ) );
        const key_value_t *pUv2 = KeyValue_Find( pFace, SV( "uv2" ) );
        const key_value_t *pColors = KeyValue_Find( pFace, SV( "colors" ) );
        if ( ( pUv != nullptr && KeyValue_ChildCount( pUv ) != nCorners ) || ( pUv2 != nullptr && KeyValue_ChildCount( pUv2 ) != nCorners ) ||
             ( pColors != nullptr && KeyValue_ChildCount( pColors ) != nCorners ) ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "faces[].uv" );
            break;
        }
        for ( usize k = 0u; k < nCorners; ++k ) {
            geo::mesh_corner_attributes_t &corner = desc.corners.pData[iFirstCorner + k].attributes;
            if ( ( pUv != nullptr && !ReadVec2( KeyValue_ChildAt( pUv, k ), corner.uv0 ) ) ||
                 ( pUv2 != nullptr && !ReadVec2( KeyValue_ChildAt( pUv2, k ), corner.uv1 ) ) ||
                 ( pColors != nullptr && !ReadColor( KeyValue_ChildAt( pColors, k ), corner.colorRgba ) ) ) {
                result = Result( map_geometry_read_status_t::INVALID, id, "faces[].uv" );
                break;
            }
        }
    }
    const key_value_t *pEdges = KeyValue_Find( pRecord, SV( "edges" ) );
    for ( usize i = 0u; result.status == map_geometry_read_status_t::OK && i < KeyValue_ChildCount( pEdges ); ++i ) {
        const key_value_t *pEdge = KeyValue_ChildAt( pEdges, i );
        const key_value_t *pEnds = KeyValue_Find( pEdge, SV( "vertices" ) );
        u64 a = 0u, b = 0u;
        bool_t bHard = CY_FALSE, bSeam = CY_FALSE;
        f64 crease = 0.0;
        if ( KeyValue_ChildCount( pEnds ) != 2u || !ReadCount( KeyValue_ChildAt( pEnds, 0u ), a ) || !ReadCount( KeyValue_ChildAt( pEnds, 1u ), b ) ||
             a >= nVertices || b >= nVertices || a == b ||
             !ReadOptional( pEdge, "hard", bHard, []( const key_value_t *p, bool_t &v ) noexcept { return KeyValue_GetBool( p, &v ); } ) ||
             !ReadOptional( pEdge, "seam", bSeam, []( const key_value_t *p, bool_t &v ) noexcept { return KeyValue_GetBool( p, &v ); } ) ||
             !ReadOptional( pEdge, "crease", crease, ReadNumber ) || crease < 0.0 || crease > 1.0 ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "edges[]" );
            break;
        }
        geo::mesh_edge_attributes_t edge{};
        const u32 hard = bHard ? static_cast<u32>( geo::MESH_EDGE_FLAG_HARD ) : 0u;
        const u32 seam = bSeam ? static_cast<u32>( geo::MESH_EDGE_FLAG_SEAM ) : 0u;
        edge.flags = static_cast<u8>( hard | seam );
        result = FromGeometryStatus( geo::MeshSourceDescription_TrySetEdge( &desc, static_cast<u32>( a < b ? a : b ), static_cast<u32>( a < b ? b : a ),
                                                                           edge, crease ),
                                     id, "edges[]" );
    }
    if ( result.status == map_geometry_read_status_t::OK ) {
        result = FromGeometryStatus( geo::MeshSource_TryBuild( &desc, pAllocator, &mesh ), id, "mesh" );
    }
    if ( result.status == map_geometry_read_status_t::OK ) {
        result = FromGeometryStatus( geo::GeometryDocument_TryAddMesh( pGeometry, &mesh ), id, "mesh" );
    }
    geo::MeshSource_Shutdown( &mesh );
    geo::MeshSourceDescription_Shutdown( &desc );
    return result;
}

CYPHER_NODISCARD map_geometry_read_result_t ReadPatch(
    const key_value_t *pRecord,
    map_materials_t *pMaterials,
    geo::geometry_document_t *pGeometry ) noexcept
{
    u64 id = 0u, columns = 0u, rows = 0u;
    string_view_t basisText{};
    if ( !ReadId( KeyValue_Find( pRecord, SV( "id" ) ), id ) ) { return Result( map_geometry_read_status_t::INVALID, 0u, "id" ); }
    if ( !KeyValue_GetString( KeyValue_Find( pRecord, SV( "basis" ) ), &basisText ) ||
         !ReadCount( KeyValue_Find( pRecord, SV( "columns" ) ), columns ) || !ReadCount( KeyValue_Find( pRecord, SV( "rows" ) ), rows ) ||
         columns > geo::kPatchControlsPerAxisMax || rows > geo::kPatchControlsPerAxisMax ) {
        return Result( map_geometry_read_status_t::INVALID, id, "basis" );
    }
    geo::patch_basis_t basis{};
    if ( StringView_Equals( basisText, SV( "quadratic" ) ) ) {
        basis = geo::patch_basis_t::BIQUADRATIC_BEZIER;
    } else if ( StringView_Equals( basisText, SV( "cubic" ) ) ) {
        basis = geo::patch_basis_t::BICUBIC_BEZIER;
    } else {
        return Result( map_geometry_read_status_t::INVALID, id, "basis" );
    }
    const key_value_t *pControls = KeyValue_Find( pRecord, SV( "controls" ) );
    const key_value_t *pIds = KeyValue_Find( pRecord, SV( "control_ids" ) );
    if ( KeyValue_ChildCount( pControls ) != rows || KeyValue_ChildCount( pIds ) != rows * columns ) {
        return Result( map_geometry_read_status_t::INVALID, id, "controls" );
    }
    string_view_t material{};
    u64 materialRef = 0u;
    const key_value_t *pMaterial = KeyValue_Find( pRecord, SV( "material" ) );
    if ( ( pMaterial != nullptr && !KeyValue_GetString( pMaterial, &material ) ) || !MapMaterials_Intern( pMaterials, material, &materialRef ) ||
         materialRef > CY_U32_MAX ) {
        return Result( map_geometry_read_status_t::INVALID, id, "material" );
    }
    geo::patch_surface_t patch{};
    map_geometry_read_result_t result = FromGeometryStatus(
        geo::Patch_Init( &patch, pGeometry->pAllocator, basis, static_cast<u32>( columns ), static_cast<u32>( rows ), geo::geometry_source_id_t{ id } ),
        id, "basis" );
    patch.materialId = static_cast<u32>( materialRef );
    for ( u32 r = 0u; result.status == map_geometry_read_status_t::OK && r < rows; ++r ) {
        const key_value_t *pRow = KeyValue_ChildAt( pControls, r );
        if ( KeyValue_ChildCount( pRow ) != columns ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "controls" );
            break;
        }
        for ( u32 c = 0u; result.status == map_geometry_read_status_t::OK && c < columns; ++c ) {
            f64 v[5]{};
            u64 controlId = 0u;
            if ( !ReadNumbers( KeyValue_ChildAt( pRow, c ), v, 5u ) ||
                 !ReadId( KeyValue_ChildAt( pIds, static_cast<usize>( r ) * columns + c ), controlId ) ) {
                result = Result( map_geometry_read_status_t::INVALID, id, "controls" );
                break;
            }
            geo::patch_control_t control{};
            control.position = math::Vec3d_Make( v[0], v[1], v[2] );
            control.uv = math::vec2d_t{ v[3], v[4] };
            control.sourceId = geo::geometry_source_id_t{ controlId };
            result = FromGeometryStatus( geo::Patch_TrySetControl( &patch, c, r, control ), id, "controls" );
        }
    }
    if ( result.status == map_geometry_read_status_t::OK ) {
        result = FromGeometryStatus( geo::GeometryDocument_TryAddPatch( pGeometry, &patch ), id, "controls" );
    }
    geo::Patch_Shutdown( &patch );
    return result;
}

CYPHER_NODISCARD map_geometry_read_result_t ReadTerrain( const key_value_t *pRecord, geo::geometry_document_t *pGeometry ) noexcept
{
    u64 id = 0u, cellsX = 0u, cellsY = 0u, tileCells = 0u;
    f64 cellSize = 0.0;
    math::vec3d_t origin{};
    if ( !ReadId( KeyValue_Find( pRecord, SV( "id" ) ), id ) ) { return Result( map_geometry_read_status_t::INVALID, 0u, "id" ); }
    const key_value_t *pCells = KeyValue_Find( pRecord, SV( "cells" ) );
    if ( !ReadVec3( KeyValue_Find( pRecord, SV( "origin" ) ), origin ) || !ReadNumber( KeyValue_Find( pRecord, SV( "cell_size" ) ), cellSize ) ||
         KeyValue_ChildCount( pCells ) != 2u || !ReadCount( KeyValue_ChildAt( pCells, 0u ), cellsX ) ||
         !ReadCount( KeyValue_ChildAt( pCells, 1u ), cellsY ) || !ReadCount( KeyValue_Find( pRecord, SV( "tile_cells" ) ), tileCells ) ||
         cellsX == 0u || cellsY == 0u || tileCells == 0u || cellsX > geo::kHeightFieldCellsPerAxisMax || cellsY > geo::kHeightFieldCellsPerAxisMax ||
         tileCells > geo::kHeightFieldTileCellsMax || ( cellsX + 1u ) * ( cellsY + 1u ) > geo::kHeightFieldSamplesMax ) {
        return Result( map_geometry_read_status_t::INVALID, id, "cells" );
    }
    const key_value_t *pHeights = KeyValue_Find( pRecord, SV( "heights" ) );
    const key_value_t *pTileIds = KeyValue_Find( pRecord, SV( "tile_ids" ) );
    const key_value_t *pHoles = KeyValue_Find( pRecord, SV( "holes" ) );
    if ( KeyValue_ChildCount( pHeights ) != cellsY + 1u || ( pHoles != nullptr && KeyValue_Type( pHoles ) != key_value_type_t::ARRAY ) ) {
        return Result( map_geometry_read_status_t::INVALID, id, "heights" );
    }
    // The placeholder allocator only supplies tile IDs the file replaces.
    geo::geometry_source_id_allocator_t placeholder{};
    geo::heightfield_t field{};
    map_geometry_read_result_t result = FromGeometryStatus(
        geo::HeightField_TryInit( &field, pGeometry->pAllocator, origin, cellSize, static_cast<u32>( cellsX ), static_cast<u32>( cellsY ),
                                  static_cast<u32>( tileCells ), geo::geometry_source_id_t{ id }, &placeholder ),
        id, "cells" );
    if ( result.status == map_geometry_read_status_t::OK && KeyValue_ChildCount( pTileIds ) != field.tiles.nCount ) {
        result = Result( map_geometry_read_status_t::INVALID, id, "tile_ids" );
    }
    for ( usize i = 0u; result.status == map_geometry_read_status_t::OK && i < field.tiles.nCount; ++i ) {
        if ( !ReadId( KeyValue_ChildAt( pTileIds, i ), field.tiles.pData[i].sourceId.value ) ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "tile_ids" );
        }
    }
    const usize nSamplesX = static_cast<usize>( cellsX ) + 1u;
    for ( usize y = 0u; result.status == map_geometry_read_status_t::OK && y <= cellsY; ++y ) {
        const key_value_t *pRow = KeyValue_ChildAt( pHeights, y );
        if ( KeyValue_ChildCount( pRow ) != nSamplesX ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "heights" );
            break;
        }
        for ( usize x = 0u; x < nSamplesX; ++x ) {
            if ( !ReadNumber( KeyValue_ChildAt( pRow, x ), field.heights.pData[y * nSamplesX + x] ) ) {
                result = Result( map_geometry_read_status_t::INVALID, id, "heights" );
                break;
            }
        }
    }
    for ( usize i = 0u; result.status == map_geometry_read_status_t::OK && i < KeyValue_ChildCount( pHoles ); ++i ) {
        const key_value_t *pHole = KeyValue_ChildAt( pHoles, i );
        u64 x = 0u, y = 0u;
        if ( KeyValue_ChildCount( pHole ) != 2u || !ReadCount( KeyValue_ChildAt( pHole, 0u ), x ) || !ReadCount( KeyValue_ChildAt( pHole, 1u ), y ) ||
             x >= cellsX || y >= cellsY ) {
            result = Result( map_geometry_read_status_t::INVALID, id, "holes" );
            break;
        }
        field.holes.pData[y * cellsX + x] = 1u;
    }
    if ( result.status == map_geometry_read_status_t::OK ) {
        result = FromGeometryStatus( geo::GeometryDocument_TryAddHeightField( pGeometry, &field ), id, "heights" );
    }
    geo::HeightField_Shutdown( &field );
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

f64 MapReal( f64 value ) noexcept
{
    return value == 0.0 ? 0.0 : value;
}

const char *MapGeometry_SectionName( map_geometry_kind_t kind ) noexcept
{
    switch ( kind ) {
        case map_geometry_kind_t::BRUSH: return "brushes";
        case map_geometry_kind_t::MESH: return "meshes";
        case map_geometry_kind_t::PATCH: return "patches";
        case map_geometry_kind_t::TERRAIN: return "terrains";
        case map_geometry_kind_t::COUNT: break;
    }
    return "";
}

const char *MapGeometry_DataMember( map_geometry_kind_t kind ) noexcept
{
    switch ( kind ) {
        case map_geometry_kind_t::BRUSH: return "faces";
        case map_geometry_kind_t::MESH: return "vertices";
        case map_geometry_kind_t::PATCH: return "controls";
        case map_geometry_kind_t::TERRAIN: return "heights";
        case map_geometry_kind_t::COUNT: break;
    }
    return "";
}

bool_t MapMaterials_Init( map_materials_t *pMaterials, const allocator_t *pAllocator ) noexcept
{
    CY_ASSERT( pMaterials != nullptr && pAllocator != nullptr );
    return Vector_Init( &pMaterials->text, pAllocator ) && Vector_Init( &pMaterials->entries, pAllocator ) &&
           Vector_Init( &pMaterials->sorted, pAllocator );
}

void MapMaterials_Shutdown( map_materials_t *pMaterials ) noexcept
{
    if ( pMaterials == nullptr ) { return; }
    Vector_Shutdown( &pMaterials->sorted );
    Vector_Shutdown( &pMaterials->entries );
    Vector_Shutdown( &pMaterials->text );
}

string_view_t MapMaterials_Path( const map_materials_t *pMaterials, u64 ref ) noexcept
{
    if ( pMaterials == nullptr || ref == 0u || ref > Vector_Count( &pMaterials->entries ) ) { return {}; }
    const map_material_entry_t &entry = pMaterials->entries.pData[ref - 1u];
    return { pMaterials->text.pData + entry.iOffset, entry.cchLength };
}

bool_t MapMaterials_Intern( map_materials_t *pMaterials, string_view_t path, u64 *pRefOut ) noexcept
{
    CY_ASSERT( pMaterials != nullptr && pRefOut != nullptr );
    *pRefOut = 0u;
    if ( path.cchLength == 0u ) { return CY_TRUE; }
    if ( path.cchLength > MAP_MATERIAL_PATH_MAX ) { return CY_FALSE; }
    usize iLow = 0u;
    usize iHigh = Vector_Count( &pMaterials->sorted );
    while ( iLow < iHigh ) {
        const usize iMid = iLow + ( iHigh - iLow ) / 2u;
        const i32 comparison = StringView_Compare( MapMaterials_Path( pMaterials, pMaterials->sorted.pData[iMid] + 1u ), path );
        if ( comparison == 0 ) {
            *pRefOut = pMaterials->sorted.pData[iMid] + 1u;
            return CY_TRUE;
        }
        if ( comparison < 0 ) { iLow = iMid + 1u; } else { iHigh = iMid; }
    }
    const map_material_entry_t entry{ static_cast<u32>( Vector_Count( &pMaterials->text ) ), static_cast<u32>( path.cchLength ) };
    const u32 iEntry = static_cast<u32>( Vector_Count( &pMaterials->entries ) );
    if ( !Vector_Append( &pMaterials->text, span_t<const char>{ path.pData, path.cchLength } ) ) { return CY_FALSE; }
    if ( !Vector_PushBack( &pMaterials->entries, entry ) ) { return CY_FALSE; }
    if ( !Vector_Insert( &pMaterials->sorted, iLow, iEntry ) ) {
        Vector_PopBack( &pMaterials->entries );
        return CY_FALSE;
    }
    *pRefOut = static_cast<u64>( iEntry ) + 1u;
    return CY_TRUE;
}

bool_t MapGeometry_CollectIds( map_geometry_kind_t kind, const key_value_t *pRecord, vector_t<u64> &idsOut ) noexcept
{
    bool_t bOk = CY_TRUE;
    // The visitor only reads; the cast lets one walker serve reading and assigning.
    ( void )VisitIdSlots( kind, const_cast<key_value_t *>( pRecord ), [&]( key_value_t *, const char *, key_value_t *pSlot ) noexcept {
        u64 id = 0u;
        if ( pSlot != nullptr && ReadId( pSlot, id ) ) { bOk = Vector_PushBack( &idsOut, id ); }
        return bOk;
    } );
    return bOk;
}

usize MapGeometry_AssignIds( map_geometry_kind_t kind, key_value_document_t *pDocument, key_value_t *pRecord, u64 *pNextId ) noexcept
{
    CY_ASSERT( pDocument != nullptr && pRecord != nullptr && pNextId != nullptr );
    usize nAssigned = 0u;
    bool_t bOk = CY_TRUE;
    // Pad an ID array a person left short: new vertices, controls, or tiles.
    if ( const char *pArrayName = IdArrayName( kind ) ) {
        const usize nRequired = RequiredIdCount( kind, pRecord );
        key_value_t *pArray = KeyValue_Find( pRecord, SV( pArrayName ) );
        if ( pArray == nullptr && nRequired != 0u ) {
            pArray = KeyValue_ObjectInsert( pDocument, pRecord, SV( pArrayName ), key_value_type_t::ARRAY );
            bOk = pArray != nullptr;
        }
        while ( bOk && KeyValue_Type( pArray ) == key_value_type_t::ARRAY && KeyValue_ChildCount( pArray ) < nRequired ) {
            key_value_t *pSlot = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::NULL_VALUE );
            bOk = pSlot != nullptr && KeyValue_SetU64( pDocument, pSlot, 0u );
        }
    }
    bOk = bOk && VisitIdSlots( kind, pRecord, [&]( key_value_t *pParent, const char *pName, key_value_t *pSlot ) noexcept {
        u64 id = 0u;
        if ( pSlot != nullptr && ReadId( pSlot, id ) ) { return CY_TRUE; }
        if ( pSlot == nullptr ) {
            pSlot = KeyValue_ObjectInsert( pDocument, pParent, SV( pName ), key_value_type_t::NULL_VALUE );
            if ( pSlot == nullptr ) { return CY_FALSE; }
        }
        ++nAssigned;
        return KeyValue_SetU64( pDocument, pSlot, ( *pNextId )++ );
    } );
    return bOk ? nAssigned : CY_INVALID_SIZE;
}

bool_t MapGeometry_ForEachId( map_geometry_kind_t kind, key_value_t *pRecord, map_id_slot_fn pfnVisit, void *pContext ) noexcept
{
    CY_ASSERT( pRecord != nullptr && pfnVisit != nullptr );
    return VisitIdSlots( kind, pRecord, [&]( key_value_t *, const char *, key_value_t *pSlot ) noexcept {
        u64 id = 0u;
        return pSlot == nullptr || !ReadId( pSlot, id ) || pfnVisit( pContext, pSlot, id );
    } );
}

map_geometry_read_result_t MapGeometry_Read(
    map_geometry_kind_t kind,
    const key_value_t *pRecord,
    map_materials_t *pMaterials,
    geo::geometry_document_t *pGeometry,
    const geo::geometry_policy_t &policy ) noexcept
{
    CY_ASSERT( pMaterials != nullptr && pGeometry != nullptr );
    if ( KeyValue_Type( pRecord ) != key_value_type_t::OBJECT ) { return Result( map_geometry_read_status_t::INVALID, 0u, "object" ); }
    switch ( kind ) {
        case map_geometry_kind_t::BRUSH: return ReadBrush( pRecord, pMaterials, pGeometry, policy );
        case map_geometry_kind_t::MESH: return ReadMesh( pRecord, pMaterials, pGeometry );
        case map_geometry_kind_t::PATCH: return ReadPatch( pRecord, pMaterials, pGeometry );
        case map_geometry_kind_t::TERRAIN: return ReadTerrain( pRecord, pGeometry );
        case map_geometry_kind_t::COUNT: break;
    }
    return Result( map_geometry_read_status_t::INVALID, 0u, "kind" );
}

namespace
{

constexpr const char *kBrushMembers[]{ "faces" };
constexpr const char *kMeshMembers[]{ "vertices", "vertex_ids", "faces", "edges" };
constexpr const char *kPatchMembers[]{ "basis", "columns", "rows", "material", "controls", "control_ids" };
constexpr const char *kTerrainMembers[]{ "origin", "cell_size", "cells", "tile_cells", "heights", "holes", "tile_ids" };
constexpr const char *kBrushFaceMembers[]{ "id", "plane", "material", "uv_u", "uv_v", "uv_origin", "uv_normal", "uv_size", "uv_rotation", "uv_offset" };
constexpr const char *kMeshFaceMembers[]{ "id", "vertices", "material", "smoothing", "uv", "uv2", "colors" };
// Map-owned brush-face members (CYMAP.md 6.3): the geometry library does
// not store them, so they travel with the face's residual record and are
// written in this order and spelling, before members nobody knows.
constexpr const char *kBrushFaceExtras[]{ "lightmap_scale", "smoothing" };

CYPHER_NODISCARD span_t<const char *const> GeometryMembers( map_geometry_kind_t kind ) noexcept
{
    switch ( kind ) {
        case map_geometry_kind_t::BRUSH: return { kBrushMembers, std::size( kBrushMembers ) };
        case map_geometry_kind_t::MESH: return { kMeshMembers, std::size( kMeshMembers ) };
        case map_geometry_kind_t::PATCH: return { kPatchMembers, std::size( kPatchMembers ) };
        case map_geometry_kind_t::TERRAIN: return { kTerrainMembers, std::size( kTerrainMembers ) };
        case map_geometry_kind_t::COUNT: break;
    }
    return {};
}

CYPHER_NODISCARD span_t<const char *const> FaceMembers( map_geometry_kind_t kind ) noexcept
{
    if ( kind == map_geometry_kind_t::BRUSH ) { return { kBrushFaceMembers, std::size( kBrushFaceMembers ) }; }
    if ( kind == map_geometry_kind_t::MESH ) { return { kMeshFaceMembers, std::size( kMeshFaceMembers ) }; }
    return {};
}

CYPHER_NODISCARD bool_t Listed( string_view_t name, span_t<const char *const> names ) noexcept
{
    for ( usize i = 0u; i < names.nCount; ++i ) {
        if ( StringView_Equals( name, SV( names.pData[i] ) ) ) { return CY_TRUE; }
    }
    return CY_FALSE;
}

} // namespace

bool_t MapGeometry_IsGeometryMember( map_geometry_kind_t kind, string_view_t name ) noexcept
{
    return Listed( name, GeometryMembers( kind ) );
}

bool_t MapGeometry_StripRecord( map_geometry_kind_t kind, key_value_document_t *pDocument, key_value_t *pRecord ) noexcept
{
    const span_t<const char *const> members = GeometryMembers( kind );
    const span_t<const char *const> faceMembers = FaceMembers( kind );
    for ( usize i = 0u; i < members.nCount; ++i ) {
        key_value_t *pMember = KeyValue_Find( pRecord, SV( members.pData[i] ) );
        if ( pMember == nullptr ) { continue; }
        if ( faceMembers.nCount != 0u && StringView_Equals( SV( members.pData[i] ), SV( "faces" ) ) ) {
            // Keep only faces with members this build does not know, and on
            // them only those members and the id that ties them to the face.
            for ( usize iFace = KeyValue_ChildCount( pMember ); iFace != 0u; --iFace ) {
                key_value_t *pFace = KeyValue_ChildAt( pMember, iFace - 1u );
                for ( usize k = KeyValue_ChildCount( pFace ); k != 0u; --k ) {
                    key_value_t *pFaceMember = KeyValue_ChildAt( pFace, k - 1u );
                    const string_view_t name = KeyValue_Name( pFaceMember );
                    if ( Listed( name, faceMembers ) && !StringView_Equals( name, SV( "id" ) ) &&
                         !KeyValue_Remove( pDocument, pFace, pFaceMember ) ) {
                        return CY_FALSE;
                    }
                }
                if ( KeyValue_ChildCount( pFace ) <= 1u && !KeyValue_Remove( pDocument, pMember, pFace ) ) { return CY_FALSE; }
            }
            if ( KeyValue_ChildCount( pMember ) != 0u ) { continue; }
        }
        if ( !KeyValue_Remove( pDocument, pRecord, pMember ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

bool_t MapGeometry_MergeResidual( map_geometry_kind_t kind, key_value_document_t *pDocument, key_value_t *pOut, const key_value_t *pRecord ) noexcept
{
    const span_t<const char *const> faceMembers = FaceMembers( kind );
    const key_value_t *pResidual = KeyValue_Find( pRecord, SV( "faces" ) );
    if ( faceMembers.nCount == 0u || KeyValue_ChildCount( pResidual ) == 0u ) { return CY_TRUE; }
    key_value_t *pFaces = KeyValue_Find( pOut, SV( "faces" ) );
    for ( usize i = 0u; i < KeyValue_ChildCount( pResidual ); ++i ) {
        const key_value_t *pExtra = KeyValue_ChildAt( pResidual, i );
        u64 id = 0u;
        if ( !ReadId( KeyValue_Find( pExtra, SV( "id" ) ), id ) ) { continue; }
        // A face a tool removed takes its extra members with it.
        for ( usize k = 0u; k < KeyValue_ChildCount( pFaces ); ++k ) {
            key_value_t *pFace = KeyValue_ChildAt( pFaces, k );
            u64 faceId = 0u;
            if ( !ReadId( KeyValue_Find( pFace, SV( "id" ) ), faceId ) || faceId != id ) { continue; }
            const span_t<const char *const> extras = kind == map_geometry_kind_t::BRUSH
                ? span_t<const char *const>{ kBrushFaceExtras, std::size( kBrushFaceExtras ) }
                : span_t<const char *const>{};
            out_t out{ pDocument };
            for ( usize e = 0u; e < extras.nCount; ++e ) {
                const key_value_t *pMember = KeyValue_Find( pExtra, SV( extras.pData[e] ) );
                f64 number = 0.0;
                u64 bits = 0u;
                if ( pMember == nullptr ) { continue; }
                if ( StringView_Equals( SV( extras.pData[e] ), SV( "lightmap_scale" ) ) && ReadNumber( pMember, number ) ) {
                    out.Real( pFace, extras.pData[e], number );
                } else if ( StringView_Equals( SV( extras.pData[e] ), SV( "smoothing" ) ) && ReadCount( pMember, bits ) ) {
                    out.Id( pFace, extras.pData[e], bits );
                } else if ( KeyValue_CloneInto( pDocument, pFace, pMember ) == nullptr ) {
                    return CY_FALSE;
                }
                if ( !out.bOk ) { return CY_FALSE; }
            }
            for ( usize m = 0u; m < KeyValue_ChildCount( pExtra ); ++m ) {
                const key_value_t *pMember = KeyValue_ChildAt( pExtra, m );
                const string_view_t name = KeyValue_Name( pMember );
                if ( !Listed( name, faceMembers ) && !Listed( name, extras ) && KeyValue_CloneInto( pDocument, pFace, pMember ) == nullptr ) {
                    return CY_FALSE;
                }
            }
            break;
        }
    }
    return CY_TRUE;
}

void MapBounds_AddPoint( map_bounds_t &bounds, math::vec3d_t point ) noexcept
{
    if ( !bounds.bHas ) {
        bounds.box.minimum = point;
        bounds.box.maximum = point;
        bounds.bHas = CY_TRUE;
        return;
    }
    math::aabbd_t &box = bounds.box;
    box.minimum = math::Vec3d_Make( point.x < box.minimum.x ? point.x : box.minimum.x, point.y < box.minimum.y ? point.y : box.minimum.y,
                                    point.z < box.minimum.z ? point.z : box.minimum.z );
    box.maximum = math::Vec3d_Make( point.x > box.maximum.x ? point.x : box.maximum.x, point.y > box.maximum.y ? point.y : box.maximum.y,
                                    point.z > box.maximum.z ? point.z : box.maximum.z );
}

void MapBounds_AddBounds( map_bounds_t &bounds, const map_bounds_t &other ) noexcept
{
    if ( !other.bHas ) { return; }
    MapBounds_AddPoint( bounds, other.box.minimum );
    MapBounds_AddPoint( bounds, other.box.maximum );
}

math::vec3d_t MapBounds_Center( const map_bounds_t &bounds ) noexcept
{
    return math::Vec3d_Make( ( bounds.box.minimum.x + bounds.box.maximum.x ) * 0.5, ( bounds.box.minimum.y + bounds.box.maximum.y ) * 0.5,
                             ( bounds.box.minimum.z + bounds.box.maximum.z ) * 0.5 );
}

bool_t MapGeometry_WriteBounds( key_value_document_t *pDocument, key_value_t *pObject, const char *pName, const map_bounds_t &bounds ) noexcept
{
    out_t out{ pDocument };
    const f64 values[6]{ bounds.box.minimum.x, bounds.box.minimum.y, bounds.box.minimum.z,
                         bounds.box.maximum.x, bounds.box.maximum.y, bounds.box.maximum.z };
    out.RealsMember( pObject, pName, values, 6u );
    return out.bOk;
}

map_bounds_t MapGeometry_BrushBounds( const geo::brush_source_t &brush, const geo::geometry_policy_t &policy,
    const allocator_t *pAllocator, geo::geometry_status_t *pStatusOut ) noexcept
{
    map_bounds_t bounds{};
    geo::brush_boundary_t boundary{};
    auto status = geo::BrushBoundary_Init( &boundary, pAllocator );
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &boundary, &brush.solid, policy ); }
    if ( status == geo::geometry_status_t::OK ) {
        for ( usize i = 0u; i < Vector_Count( &boundary.vertices ); ++i ) { MapBounds_AddPoint( bounds, boundary.vertices.pData[i] ); }
    }
    if ( pStatusOut ) { *pStatusOut = status; }
    geo::BrushBoundary_Shutdown( &boundary );
    return bounds;
}

map_bounds_t MapGeometry_MeshBounds( const geo::mesh_source_t &mesh, const allocator_t *pAllocator,
    usize *pnVertices, usize *pnFaces, geo::geometry_status_t *pStatusOut ) noexcept
{
    map_bounds_t bounds{};
    geo::mesh_source_description_t desc{};
    auto status = geo::MeshSourceDescription_Init( &desc, pAllocator, mesh.sourceId );
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSource_TryDescribe( &mesh, &desc ); }
    if ( status == geo::geometry_status_t::OK ) {
        for ( usize i = 0u; i < Vector_Count( &desc.vertices ); ++i ) { MapBounds_AddPoint( bounds, desc.vertices.pData[i].position ); }
    }
    if ( pStatusOut ) { *pStatusOut = status; }
    if ( pnVertices != nullptr ) { *pnVertices = Vector_Count( &desc.vertices ); }
    if ( pnFaces != nullptr ) { *pnFaces = Vector_Count( &desc.faces ); }
    geo::MeshSourceDescription_Shutdown( &desc );
    return bounds;
}

map_bounds_t MapGeometry_PatchBounds( const geo::patch_surface_t &patch ) noexcept
{
    map_bounds_t bounds{};
    for ( usize i = 0u; i < Vector_Count( &patch.controls ); ++i ) { MapBounds_AddPoint( bounds, patch.controls.pData[i].position ); }
    return bounds;
}

map_bounds_t MapGeometry_TerrainFootprint( const geo::heightfield_t &terrain ) noexcept
{
    map_bounds_t bounds{};
    MapBounds_AddPoint( bounds, terrain.origin );
    MapBounds_AddPoint( bounds, math::Vec3d_Make( terrain.origin.x + terrain.cellSize * terrain.cCellsX,
                                                  terrain.origin.y + terrain.cellSize * terrain.cCellsY, terrain.origin.z ) );
    return bounds;
}

map_bounds_t MapGeometry_TerrainBounds( const geo::heightfield_t &terrain ) noexcept
{
    map_bounds_t bounds = MapGeometry_TerrainFootprint( terrain );
    for ( usize i = 0u; i < Vector_Count( &terrain.heights ); ++i ) {
        MapBounds_AddPoint( bounds, math::Vec3d_Make( terrain.origin.x, terrain.origin.y, terrain.origin.z + terrain.heights.pData[i] ) );
    }
    return bounds;
}

bool_t MapGeometry_WriteBrush(
    const geo::brush_source_t &brush,
    const map_materials_t &materials,
    key_value_document_t *pDocument,
    key_value_t *pOut ) noexcept
{
    CY_ASSERT( pDocument != nullptr && pOut != nullptr );
    out_t out{ pDocument };
    key_value_t *pFaces = out.Insert( pOut, "faces", key_value_type_t::ARRAY );
    for ( usize i = 0u; out.bOk && i < Vector_Count( &brush.solid.sides ); ++i ) {
        const geo::brush_solid_side_t &side = brush.solid.sides.pData[i];
        CY_ASSERT_MSG( side.iAttributeIndex < Vector_Count( &brush.attributes.records ), "Brush sources bind every side to a record" );
        const geo::geometry_brush_side_attributes_t &attributes = brush.attributes.records.pData[side.iAttributeIndex];
        const math::planar_uv_mappingd_t &uv = attributes.uvProjection;
        key_value_t *pFace = out.Append( pFaces, key_value_type_t::OBJECT );
        out.Id( pFace, "id", side.sourceId.value );
        const f64 plane[4]{ side.plane.normal.x, side.plane.normal.y, side.plane.normal.z, -side.plane.d };
        out.RealsMember( pFace, "plane", plane, 4u );
        const string_view_t material = MapMaterials_Path( &materials, attributes.material.value );
        if ( material.cchLength != 0u ) { out.Text( pFace, "material", material ); }
        out.Vec3( pFace, "uv_u", uv.uAxis );
        out.Vec3( pFace, "uv_v", uv.vAxis );
        if ( !Vec3Equal( uv.origin, math::CY_VEC3D_ZERO ) ) { out.Vec3( pFace, "uv_origin", uv.origin ); }
        if ( !Vec3Equal( uv.normal, side.plane.normal ) ) { out.Vec3( pFace, "uv_normal", uv.normal ); }
        if ( uv.worldUnitsPerUv.x != 1.0 || uv.worldUnitsPerUv.y != 1.0 ) { out.Vec2( pFace, "uv_size", uv.worldUnitsPerUv ); }
        if ( uv.rotationRadians != 0.0 ) { out.Real( pFace, "uv_rotation", DegreesFromRadians( uv.rotationRadians ) ); }
        if ( uv.offset.x != 0.0 || uv.offset.y != 0.0 ) { out.Vec2( pFace, "uv_offset", uv.offset ); }
    }
    return out.bOk;
}

bool_t MapGeometry_WriteMesh(
    const geo::mesh_source_t &mesh,
    const map_materials_t &materials,
    const allocator_t *pAllocator,
    key_value_document_t *pDocument,
    key_value_t *pOut ) noexcept
{
    CY_ASSERT( pDocument != nullptr && pOut != nullptr );
    geo::mesh_source_description_t desc{};
    if ( geo::MeshSourceDescription_Init( &desc, pAllocator, mesh.sourceId ) != geo::geometry_status_t::OK ||
         geo::MeshSource_TryDescribe( &mesh, &desc ) != geo::geometry_status_t::OK ) {
        geo::MeshSourceDescription_Shutdown( &desc );
        return CY_FALSE;
    }
    out_t out{ pDocument };
    key_value_t *pVertices = out.Insert( pOut, "vertices", key_value_type_t::ARRAY );
    for ( usize i = 0u; out.bOk && i < Vector_Count( &desc.vertices ); ++i ) {
        const math::vec3d_t p = desc.vertices.pData[i].position;
        const f64 values[3]{ p.x, p.y, p.z };
        out.Reals( out.Append( pVertices, key_value_type_t::ARRAY ), values, 3u );
    }
    key_value_t *pVertexIds = out.Insert( pOut, "vertex_ids", key_value_type_t::ARRAY );
    for ( usize i = 0u; out.bOk && i < Vector_Count( &desc.vertices ); ++i ) {
        out.SetU64( out.Append( pVertexIds, key_value_type_t::NULL_VALUE ), desc.vertices.pData[i].sourceId.value );
    }
    key_value_t *pFaces = out.Insert( pOut, "faces", key_value_type_t::ARRAY );
    for ( usize iFace = 0u; out.bOk && iFace < Vector_Count( &desc.faces ); ++iFace ) {
        const geo::mesh_source_face_t &face = desc.faces.pData[iFace];
        key_value_t *pFace = out.Append( pFaces, key_value_type_t::OBJECT );
        out.Id( pFace, "id", face.sourceId.value );
        key_value_t *pCorners = out.Insert( pFace, "vertices", key_value_type_t::ARRAY );
        bool_t bUv = CY_FALSE, bUv2 = CY_FALSE, bColors = CY_FALSE;
        for ( u32 k = 0u; out.bOk && k < face.cCorners; ++k ) {
            const geo::mesh_source_corner_t &corner = desc.corners.pData[face.iFirstCorner + k];
            out.SetI64( out.Append( pCorners, key_value_type_t::NULL_VALUE ), static_cast<i64>( corner.iVertex ) );
            bUv = bUv || corner.attributes.uv0.x != 0.0 || corner.attributes.uv0.y != 0.0;
            bUv2 = bUv2 || corner.attributes.uv1.x != 0.0 || corner.attributes.uv1.y != 0.0;
            bColors = bColors || corner.attributes.colorRgba != 0xFFFFFFFFu;
        }
        const string_view_t material = MapMaterials_Path( &materials, face.attributes.material.value );
        if ( material.cchLength != 0u ) { out.Text( pFace, "material", material ); }
        if ( face.attributes.smoothingGroups != 1u ) { out.Id( pFace, "smoothing", face.attributes.smoothingGroups ); }
        const auto channel = [&]( const char *pName, bool_t bWrite, u32 which ) noexcept {
            if ( !bWrite ) { return; }
            key_value_t *pArray = out.Insert( pFace, pName, key_value_type_t::ARRAY );
            for ( u32 k = 0u; out.bOk && k < face.cCorners; ++k ) {
                const geo::mesh_corner_attributes_t &attributes = desc.corners.pData[face.iFirstCorner + k].attributes;
                if ( which == 2u ) {
                    out.Color( out.Append( pArray, key_value_type_t::NULL_VALUE ), attributes.colorRgba );
                } else {
                    const math::vec2d_t uv = which == 0u ? attributes.uv0 : attributes.uv1;
                    const f64 values[2]{ uv.x, uv.y };
                    out.Reals( out.Append( pArray, key_value_type_t::ARRAY ), values, 2u );
                }
            }
        };
        channel( "uv", bUv, 0u );
        channel( "uv2", bUv2, 1u );
        channel( "colors", bColors, 2u );
    }
    if ( out.bOk && Vector_Count( &desc.edges ) != 0u ) {
        key_value_t *pEdges = out.Insert( pOut, "edges", key_value_type_t::ARRAY );
        for ( usize i = 0u; out.bOk && i < Vector_Count( &desc.edges ); ++i ) {
            const geo::mesh_source_edge_t &edge = desc.edges.pData[i];
            key_value_t *pEdge = out.Append( pEdges, key_value_type_t::OBJECT );
            key_value_t *pEnds = out.Insert( pEdge, "vertices", key_value_type_t::ARRAY );
            out.SetI64( out.Append( pEnds, key_value_type_t::NULL_VALUE ), static_cast<i64>( edge.iVertexA ) );
            out.SetI64( out.Append( pEnds, key_value_type_t::NULL_VALUE ), static_cast<i64>( edge.iVertexB ) );
            if ( ( edge.attributes.flags & geo::MESH_EDGE_FLAG_HARD ) != 0u ) { out.Bool( pEdge, "hard", CY_TRUE ); }
            if ( ( edge.attributes.flags & geo::MESH_EDGE_FLAG_SEAM ) != 0u ) { out.Bool( pEdge, "seam", CY_TRUE ); }
            if ( edge.creaseWeight != 0.0 ) { out.Real( pEdge, "crease", edge.creaseWeight ); }
        }
    }
    geo::MeshSourceDescription_Shutdown( &desc );
    return out.bOk;
}

bool_t MapGeometry_WritePatch(
    const geo::patch_surface_t &patch,
    const map_materials_t &materials,
    key_value_document_t *pDocument,
    key_value_t *pOut ) noexcept
{
    CY_ASSERT( pDocument != nullptr && pOut != nullptr );
    out_t out{ pDocument };
    out.Text( pOut, "basis", SV( patch.basis == geo::patch_basis_t::BICUBIC_BEZIER ? "cubic" : "quadratic" ) );
    out.Int( pOut, "columns", static_cast<i64>( patch.cColumns ) );
    out.Int( pOut, "rows", static_cast<i64>( patch.cRows ) );
    const string_view_t material = MapMaterials_Path( &materials, patch.materialId );
    if ( material.cchLength != 0u ) { out.Text( pOut, "material", material ); }
    key_value_t *pControls = out.Insert( pOut, "controls", key_value_type_t::ARRAY );
    for ( u32 r = 0u; out.bOk && r < patch.cRows; ++r ) {
        key_value_t *pRow = out.Append( pControls, key_value_type_t::ARRAY );
        for ( u32 c = 0u; out.bOk && c < patch.cColumns; ++c ) {
            const geo::patch_control_t &control = patch.controls.pData[static_cast<usize>( r ) * patch.cColumns + c];
            const f64 values[5]{ control.position.x, control.position.y, control.position.z, control.uv.x, control.uv.y };
            out.Reals( out.Append( pRow, key_value_type_t::ARRAY ), values, 5u );
        }
    }
    key_value_t *pIds = out.Insert( pOut, "control_ids", key_value_type_t::ARRAY );
    for ( usize i = 0u; out.bOk && i < Vector_Count( &patch.controls ); ++i ) {
        out.SetU64( out.Append( pIds, key_value_type_t::NULL_VALUE ), patch.controls.pData[i].sourceId.value );
    }
    return out.bOk;
}

bool_t MapGeometry_WriteTerrain(
    const geo::heightfield_t &terrain,
    key_value_document_t *pDocument,
    key_value_t *pOut ) noexcept
{
    CY_ASSERT( pDocument != nullptr && pOut != nullptr );
    out_t out{ pDocument };
    out.Vec3( pOut, "origin", terrain.origin );
    out.Real( pOut, "cell_size", terrain.cellSize );
    key_value_t *pCells = out.Insert( pOut, "cells", key_value_type_t::ARRAY );
    out.SetI64( out.Append( pCells, key_value_type_t::NULL_VALUE ), static_cast<i64>( terrain.cCellsX ) );
    out.SetI64( out.Append( pCells, key_value_type_t::NULL_VALUE ), static_cast<i64>( terrain.cCellsY ) );
    out.Int( pOut, "tile_cells", static_cast<i64>( terrain.tileCells ) );
    const usize nSamplesX = static_cast<usize>( terrain.cCellsX ) + 1u;
    key_value_t *pHeights = out.Insert( pOut, "heights", key_value_type_t::ARRAY );
    for ( usize y = 0u; out.bOk && y <= terrain.cCellsY; ++y ) {
        out.Reals( out.Append( pHeights, key_value_type_t::ARRAY ), terrain.heights.pData + y * nSamplesX, nSamplesX );
    }
    bool_t bAnyHole = CY_FALSE;
    for ( usize i = 0u; i < Vector_Count( &terrain.holes ) && !bAnyHole; ++i ) { bAnyHole = terrain.holes.pData[i] != 0u; }
    if ( bAnyHole ) {
        key_value_t *pHoles = out.Insert( pOut, "holes", key_value_type_t::ARRAY );
        for ( usize y = 0u; out.bOk && y < terrain.cCellsY; ++y ) {
            for ( usize x = 0u; out.bOk && x < terrain.cCellsX; ++x ) {
                if ( terrain.holes.pData[y * terrain.cCellsX + x] == 0u ) { continue; }
                key_value_t *pHole = out.Append( pHoles, key_value_type_t::ARRAY );
                out.SetI64( out.Append( pHole, key_value_type_t::NULL_VALUE ), static_cast<i64>( x ) );
                out.SetI64( out.Append( pHole, key_value_type_t::NULL_VALUE ), static_cast<i64>( y ) );
            }
        }
    }
    key_value_t *pTiles = out.Insert( pOut, "tile_ids", key_value_type_t::ARRAY );
    for ( usize i = 0u; out.bOk && i < Vector_Count( &terrain.tiles ); ++i ) {
        out.SetU64( out.Append( pTiles, key_value_type_t::NULL_VALUE ), terrain.tiles.pData[i].sourceId.value );
    }
    return out.bOk;
}

} // namespace cypher::editor::map
