//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SurfaceSerialization.cpp
//  Purpose: Implements the schema-4 patch and heightfield sections.
//  Details: Objects are rebuilt through their own constructors and then
//           published through the document store, so every loaded object
//           passes the same validation and identity rules as an edited one.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SurfaceSerialization.h"
#include "CypherGeometry_DocumentSurfaces.h"

#include <cstring>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

geometry_serialization_result_t Fail( geometry_serialization_status_t status, const char *pField = nullptr,
                                      usize iElement = CY_INVALID_SIZE ) noexcept
{
    geometry_serialization_result_t r{};
    r.status = status;
    r.iElement = iElement;
    if ( pField != nullptr ) {
        const usize cch = std::strlen( pField );
        const usize n = cch < sizeof( r.field ) - 1u ? cch : sizeof( r.field ) - 1u;
        std::memcpy( r.field, pField, n );
        r.field[n] = '\0';
    }
    return r;
}

geometry_serialization_status_t MapStatus( geometry_status_t st ) noexcept
{
    switch ( st ) {
        case geometry_status_t::ALLOCATION_FAILED: return geometry_serialization_status_t::OUT_OF_MEMORY;
        case geometry_status_t::LIMIT_EXCEEDED: return geometry_serialization_status_t::LIMIT_EXCEEDED;
        case geometry_status_t::IDENTITY_CONFLICT: return geometry_serialization_status_t::DUPLICATE_SOURCE_ID;
        case geometry_status_t::NUMERIC_FAILURE: return geometry_serialization_status_t::VALUE_OUT_OF_RANGE;
        default: return geometry_serialization_status_t::CORRUPT_DOCUMENT;
    }
}

const char *BasisName( patch_basis_t basis ) noexcept
{
    return basis == patch_basis_t::BICUBIC_BEZIER ? "bicubic_bezier" : "biquadratic_bezier";
}

// ---- writing ----

struct writer_t {
    key_value_document_t *pDoc;
    bool bOk{ true };
    key_value_t *Insert( key_value_t *pParent, const char *pName, key_value_type_t type ) noexcept
    {
        key_value_t *p = bOk ? KeyValue_ObjectInsert( pDoc, pParent, StringView_FromCString( pName ), type ) : nullptr;
        bOk = bOk && p != nullptr;
        return p;
    }
    void U64( key_value_t *pParent, const char *pName, u64 v ) noexcept
    {
        key_value_t *p = Insert( pParent, pName, key_value_type_t::U64 );
        bOk = bOk && KeyValue_SetU64( pDoc, p, v );
    }
    void F64( key_value_t *pParent, const char *pName, f64 v ) noexcept
    {
        key_value_t *p = Insert( pParent, pName, key_value_type_t::F64 );
        bOk = bOk && KeyValue_SetF64( pDoc, p, v );
    }
    void Str( key_value_t *pParent, const char *pName, const char *pValue ) noexcept
    {
        key_value_t *p = Insert( pParent, pName, key_value_type_t::STRING );
        bOk = bOk && KeyValue_SetString( pDoc, p, StringView_FromCString( pValue ) );
    }
    void AppendU64( key_value_t *pArray, u64 v ) noexcept
    {
        key_value_t *p = bOk ? KeyValue_ArrayAppend( pDoc, pArray, key_value_type_t::U64 ) : nullptr;
        bOk = bOk && p != nullptr && KeyValue_SetU64( pDoc, p, v );
    }
    void AppendF64( key_value_t *pArray, f64 v ) noexcept
    {
        key_value_t *p = bOk ? KeyValue_ArrayAppend( pDoc, pArray, key_value_type_t::F64 ) : nullptr;
        bOk = bOk && p != nullptr && KeyValue_SetF64( pDoc, p, v );
    }
    key_value_t *Object( key_value_t *pArray ) noexcept
    {
        key_value_t *p = bOk ? KeyValue_ArrayAppend( pDoc, pArray, key_value_type_t::OBJECT ) : nullptr;
        bOk = bOk && p != nullptr;
        return p;
    }
};

void WritePatch( writer_t &w, key_value_t *pArray, const patch_surface_t &p ) noexcept
{
    key_value_t *pObj = w.Object( pArray );
    w.U64( pObj, "source_id", p.sourceId.value );
    w.Str( pObj, "basis", BasisName( p.basis ) );
    w.U64( pObj, "columns", p.cColumns );
    w.U64( pObj, "rows", p.cRows );
    w.U64( pObj, "material", p.materialId );
    key_value_t *pIds = w.Insert( pObj, "control_ids", key_value_type_t::ARRAY );
    key_value_t *pPositions = w.Insert( pObj, "positions", key_value_type_t::ARRAY );
    key_value_t *pUvs = w.Insert( pObj, "uvs", key_value_type_t::ARRAY );
    for ( usize i = 0u; w.bOk && i < p.controls.nCount; ++i ) {
        const patch_control_t &c = p.controls.pData[i];
        w.AppendU64( pIds, c.sourceId.value );
        w.AppendF64( pPositions, c.position.x );
        w.AppendF64( pPositions, c.position.y );
        w.AppendF64( pPositions, c.position.z );
        w.AppendF64( pUvs, c.uv.x );
        w.AppendF64( pUvs, c.uv.y );
    }
}

void WriteHeightField( writer_t &w, key_value_t *pArray, const heightfield_t &f ) noexcept
{
    key_value_t *pObj = w.Object( pArray );
    w.U64( pObj, "source_id", f.sourceId.value );
    key_value_t *pOrigin = w.Insert( pObj, "origin", key_value_type_t::ARRAY );
    w.AppendF64( pOrigin, f.origin.x );
    w.AppendF64( pOrigin, f.origin.y );
    w.AppendF64( pOrigin, f.origin.z );
    w.F64( pObj, "cell_size", f.cellSize );
    w.U64( pObj, "cells_x", f.cCellsX );
    w.U64( pObj, "cells_y", f.cCellsY );
    w.U64( pObj, "tile_cells", f.tileCells );
    key_value_t *pTiles = w.Insert( pObj, "tile_ids", key_value_type_t::ARRAY );
    for ( usize i = 0u; w.bOk && i < f.tiles.nCount; ++i ) { w.AppendU64( pTiles, f.tiles.pData[i].sourceId.value ); }
    key_value_t *pHeights = w.Insert( pObj, "heights", key_value_type_t::ARRAY );
    for ( usize i = 0u; w.bOk && i < f.heights.nCount; ++i ) { w.AppendF64( pHeights, f.heights.pData[i] ); }
    key_value_t *pHoles = w.Insert( pObj, "hole_cells", key_value_type_t::ARRAY );
    for ( usize i = 0u; w.bOk && i < f.holes.nCount; ++i ) {
        if ( f.holes.pData[i] != 0u ) { w.AppendU64( pHoles, i ); }
    }
}

// ---- reading ----

bool ReadU64( const key_value_t *pParent, const char *pName, u64 *pOut ) noexcept
{
    const key_value_t *p = KeyValue_Find( pParent, StringView_FromCString( pName ) );
    return p != nullptr && KeyValue_Type( p ) == key_value_type_t::U64 && KeyValue_GetU64( p, pOut );
}

bool ReadF64( const key_value_t *pParent, const char *pName, f64 *pOut ) noexcept
{
    const key_value_t *p = KeyValue_Find( pParent, StringView_FromCString( pName ) );
    return p != nullptr && KeyValue_Type( p ) == key_value_type_t::F64 && KeyValue_GetF64( p, pOut );
}

// An array field of exactly cExpected elements (any count when cExpected is
// CY_INVALID_SIZE), all of `type`.
const key_value_t *ReadArray( const key_value_t *pParent, const char *pName, key_value_type_t type, usize cExpected ) noexcept
{
    const key_value_t *p = KeyValue_Find( pParent, StringView_FromCString( pName ) );
    if ( p == nullptr || KeyValue_Type( p ) != key_value_type_t::ARRAY ) { return nullptr; }
    const usize n = KeyValue_ChildCount( p );
    if ( cExpected != CY_INVALID_SIZE && n != cExpected ) { return nullptr; }
    for ( usize i = 0u; i < n; ++i ) {
        if ( KeyValue_Type( KeyValue_ChildAt( p, i ) ) != type ) { return nullptr; }
    }
    return p;
}

u64 U64At( const key_value_t *pArray, usize i ) noexcept
{
    u64 v = 0u;
    (void)KeyValue_GetU64( KeyValue_ChildAt( pArray, i ), &v );
    return v;
}

f64 F64At( const key_value_t *pArray, usize i ) noexcept
{
    f64 v = 0.0;
    (void)KeyValue_GetF64( KeyValue_ChildAt( pArray, i ), &v );
    return v;
}

bool Claimed( const geometry_document_t *pDoc, u64 id ) noexcept
{
    return HashSet_Contains( &pDoc->sourceIds.claimedIds, geometry_source_id_t{ id } );
}

geometry_serialization_result_t ReadPatch( const key_value_t *pObj, usize iPatch, geometry_document_t *pDoc, bool bRequireClaimed ) noexcept
{
    if ( KeyValue_Type( pObj ) != key_value_type_t::OBJECT ) {
        return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "patches[]", iPatch );
    }
    u64 id = 0u, cColumns = 0u, cRows = 0u, material = 0u;
    string_view_t basisName{};
    const key_value_t *pBasis = KeyValue_Find( pObj, StringView_FromCString( "basis" ) );
    if ( !ReadU64( pObj, "source_id", &id ) || !ReadU64( pObj, "columns", &cColumns ) || !ReadU64( pObj, "rows", &cRows ) ||
         !ReadU64( pObj, "material", &material ) || pBasis == nullptr || KeyValue_Type( pBasis ) != key_value_type_t::STRING ||
         !KeyValue_GetString( pBasis, &basisName ) ) {
        return Fail( geometry_serialization_status_t::MISSING_FIELD, "patches[]", iPatch );
    }
    patch_basis_t basis{};
    if ( StringView_Equals( basisName, StringView_FromCString( "biquadratic_bezier" ) ) ) {
        basis = patch_basis_t::BIQUADRATIC_BEZIER;
    } else if ( StringView_Equals( basisName, StringView_FromCString( "bicubic_bezier" ) ) ) {
        basis = patch_basis_t::BICUBIC_BEZIER;
    } else {
        return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "patches[].basis", iPatch );
    }
    if ( id == 0u || material > CY_U32_MAX || !Patch_IsValidAxisCount( basis, static_cast<u32>( cColumns < CY_U32_MAX ? cColumns : 0u ) ) ||
         !Patch_IsValidAxisCount( basis, static_cast<u32>( cRows < CY_U32_MAX ? cRows : 0u ) ) ||
         cColumns * cRows > kPatchControlsMax ) {
        return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "patches[]", iPatch );
    }
    const usize n = static_cast<usize>( cColumns * cRows );
    const key_value_t *pIds = ReadArray( pObj, "control_ids", key_value_type_t::U64, n );
    const key_value_t *pPositions = ReadArray( pObj, "positions", key_value_type_t::F64, 3u * n );
    const key_value_t *pUvs = ReadArray( pObj, "uvs", key_value_type_t::F64, 2u * n );
    if ( pIds == nullptr || pPositions == nullptr || pUvs == nullptr ) {
        return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "patches[].controls", iPatch );
    }
    if ( bRequireClaimed ) {
        bool bClaimed = Claimed( pDoc, id );
        for ( usize i = 0u; bClaimed && i < n; ++i ) { bClaimed = Claimed( pDoc, U64At( pIds, i ) ); }
        if ( !bClaimed ) { return Fail( geometry_serialization_status_t::INVALID_SOURCE_ID, "patches[]", iPatch ); }
    }
    patch_surface_t patch{};
    geometry_status_t st = Patch_Init( &patch, pDoc->pAllocator, basis, static_cast<u32>( cColumns ), static_cast<u32>( cRows ),
                                       geometry_source_id_t{ id } );
    patch.materialId = static_cast<u32>( material );
    for ( u32 r = 0u; st == geometry_status_t::OK && r < cRows; ++r ) {
        for ( u32 c = 0u; st == geometry_status_t::OK && c < cColumns; ++c ) {
            const usize i = static_cast<usize>( r ) * cColumns + c;
            patch_control_t control{};
            control.sourceId.value = U64At( pIds, i );
            control.position = math::Vec3d_Make( F64At( pPositions, 3u * i ), F64At( pPositions, 3u * i + 1u ), F64At( pPositions, 3u * i + 2u ) );
            control.uv = math::vec2d_t{ F64At( pUvs, 2u * i ), F64At( pUvs, 2u * i + 1u ) };
            st = Patch_TrySetControl( &patch, c, r, control );
        }
    }
    if ( st == geometry_status_t::OK ) { st = GeometryDocument_TryAddPatch( pDoc, &patch ); }
    Patch_Shutdown( &patch );
    return st == geometry_status_t::OK ? geometry_serialization_result_t{} : Fail( MapStatus( st ), "patches[]", iPatch );
}

geometry_serialization_result_t ReadHeightField( const key_value_t *pObj, usize iField, geometry_document_t *pDoc,
                                                 bool bRequireClaimed ) noexcept
{
    if ( KeyValue_Type( pObj ) != key_value_type_t::OBJECT ) {
        return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "heightfields[]", iField );
    }
    u64 id = 0u, cCellsX = 0u, cCellsY = 0u, tileCells = 0u;
    f64 cellSize = 0.0;
    const key_value_t *pOrigin = ReadArray( pObj, "origin", key_value_type_t::F64, 3u );
    if ( !ReadU64( pObj, "source_id", &id ) || !ReadU64( pObj, "cells_x", &cCellsX ) || !ReadU64( pObj, "cells_y", &cCellsY ) ||
         !ReadU64( pObj, "tile_cells", &tileCells ) || !ReadF64( pObj, "cell_size", &cellSize ) || pOrigin == nullptr ) {
        return Fail( geometry_serialization_status_t::MISSING_FIELD, "heightfields[]", iField );
    }
    if ( id == 0u || cCellsX == 0u || cCellsY == 0u || tileCells == 0u || cCellsX > CY_U32_MAX || cCellsY > CY_U32_MAX ||
         tileCells > kHeightFieldTileCellsMax || ( cCellsX + 1u ) * ( cCellsY + 1u ) > kHeightFieldSamplesMax ) {
        return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "heightfields[]", iField );
    }
    // The ID allocator only supplies placeholder tile IDs; the file's IDs
    // replace them below.
    geometry_source_id_allocator_t placeholder{};
    heightfield_t field{};
    geometry_status_t st = HeightField_TryInit(
        &field, pDoc->pAllocator, math::Vec3d_Make( F64At( pOrigin, 0u ), F64At( pOrigin, 1u ), F64At( pOrigin, 2u ) ), cellSize,
        static_cast<u32>( cCellsX ), static_cast<u32>( cCellsY ), static_cast<u32>( tileCells ), geometry_source_id_t{ id }, &placeholder );
    if ( st != geometry_status_t::OK ) {
        return Fail( st == geometry_status_t::ALLOCATION_FAILED ? geometry_serialization_status_t::OUT_OF_MEMORY
                                                                : geometry_serialization_status_t::VALUE_OUT_OF_RANGE,
                     "heightfields[]", iField );
    }
    const key_value_t *pTiles = ReadArray( pObj, "tile_ids", key_value_type_t::U64, field.tiles.nCount );
    const key_value_t *pHeights = ReadArray( pObj, "heights", key_value_type_t::F64, field.heights.nCount );
    const key_value_t *pHoles = ReadArray( pObj, "hole_cells", key_value_type_t::U64, CY_INVALID_SIZE );
    geometry_serialization_result_t r{};
    if ( pTiles == nullptr || pHeights == nullptr || pHoles == nullptr ) {
        r = Fail( geometry_serialization_status_t::TYPE_MISMATCH, "heightfields[].data", iField );
    }
    for ( usize i = 0u; r.status == geometry_serialization_status_t::OK && i < field.tiles.nCount; ++i ) {
        field.tiles.pData[i].sourceId.value = U64At( pTiles, i );
    }
    for ( usize i = 0u; r.status == geometry_serialization_status_t::OK && i < field.heights.nCount; ++i ) {
        field.heights.pData[i] = F64At( pHeights, i );
    }
    for ( usize i = 0u; r.status == geometry_serialization_status_t::OK && i < KeyValue_ChildCount( pHoles ); ++i ) {
        const u64 cell = U64At( pHoles, i );
        if ( cell >= field.holes.nCount ) {
            r = Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "heightfields[].hole_cells", iField );
        } else {
            field.holes.pData[cell] = 1u;
        }
    }
    if ( r.status == geometry_serialization_status_t::OK && bRequireClaimed ) {
        bool bClaimed = Claimed( pDoc, id );
        for ( usize i = 0u; bClaimed && i < field.tiles.nCount; ++i ) { bClaimed = Claimed( pDoc, field.tiles.pData[i].sourceId.value ); }
        if ( !bClaimed ) { r = Fail( geometry_serialization_status_t::INVALID_SOURCE_ID, "heightfields[]", iField ); }
    }
    if ( r.status == geometry_serialization_status_t::OK ) {
        st = GeometryDocument_TryAddHeightField( pDoc, &field );
        if ( st != geometry_status_t::OK ) { r = Fail( MapStatus( st ), "heightfields[]", iField ); }
    }
    HeightField_Shutdown( &field );
    return r;
}

} // namespace

geometry_serialization_result_t SurfaceSerialization_TryCollectIds(
    const geometry_document_t *pDocument,
    const allocator_t *pScratchAllocator,
    vector_t<geometry_source_id_t> *pIdsOut ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) || pIdsOut == nullptr ) {
        return Fail( geometry_serialization_status_t::INVALID_ARGUMENT );
    }
    auto push = [&]( geometry_source_id_t id ) noexcept {
        return GeometrySourceIdRegistry_Contains( &pDocument->sourceIds, id ) ? ( Vector_PushBack( pIdsOut, id ) ? 0 : 1 ) : 2;
    };
    for ( usize i = 0u; i < GeometryDocument_PatchCount( pDocument ); ++i ) {
        const patch_surface_t *p = GeometryDocument_PatchAt( pDocument, i );
        const patch_validation_result_t v = Patch_Validate( p, pScratchAllocator );
        if ( v.fault != patch_fault_t::NONE ) {
            return Fail( v.fault == patch_fault_t::VALIDATION_INCOMPLETE ? geometry_serialization_status_t::OUT_OF_MEMORY
                                                                         : geometry_serialization_status_t::CORRUPT_DOCUMENT,
                         "patches[]", i );
        }
        int rc = push( p->sourceId );
        for ( usize k = 0u; rc == 0 && k < p->controls.nCount; ++k ) { rc = push( p->controls.pData[k].sourceId ); }
        if ( rc != 0 ) {
            return Fail( rc == 1 ? geometry_serialization_status_t::OUT_OF_MEMORY : geometry_serialization_status_t::CORRUPT_DOCUMENT,
                         "patches[].source_ids", i );
        }
    }
    for ( usize i = 0u; i < GeometryDocument_HeightFieldCount( pDocument ); ++i ) {
        const heightfield_t *f = GeometryDocument_HeightFieldAt( pDocument, i );
        const heightfield_validation_result_t v = HeightField_Validate( f, pScratchAllocator );
        if ( v.fault != heightfield_fault_t::NONE ) {
            return Fail( v.fault == heightfield_fault_t::VALIDATION_INCOMPLETE ? geometry_serialization_status_t::OUT_OF_MEMORY
                                                                               : geometry_serialization_status_t::CORRUPT_DOCUMENT,
                         "heightfields[]", i );
        }
        int rc = push( f->sourceId );
        for ( usize k = 0u; rc == 0 && k < f->tiles.nCount; ++k ) { rc = push( f->tiles.pData[k].sourceId ); }
        if ( rc != 0 ) {
            return Fail( rc == 1 ? geometry_serialization_status_t::OUT_OF_MEMORY : geometry_serialization_status_t::CORRUPT_DOCUMENT,
                         "heightfields[].source_ids", i );
        }
    }
    return {};
}

geometry_serialization_result_t SurfaceSerialization_TryWrite(
    key_value_document_t *pKvDocument,
    key_value_t *pRoot,
    const geometry_document_t *pDocument ) noexcept
{
    if ( pKvDocument == nullptr || pRoot == nullptr || !GeometryDocument_IsInitialized( pDocument ) ) {
        return Fail( geometry_serialization_status_t::INVALID_ARGUMENT );
    }
    writer_t w{ pKvDocument };
    if ( GeometryDocument_PatchCount( pDocument ) > 0u ) {
        key_value_t *pPatches = w.Insert( pRoot, "patches", key_value_type_t::ARRAY );
        for ( usize i = 0u; w.bOk && i < GeometryDocument_PatchCount( pDocument ); ++i ) {
            WritePatch( w, pPatches, *GeometryDocument_PatchAt( pDocument, i ) );
        }
        if ( !w.bOk ) { return Fail( geometry_serialization_status_t::OUT_OF_MEMORY, "patches" ); }
    }
    if ( GeometryDocument_HeightFieldCount( pDocument ) > 0u ) {
        key_value_t *pFields = w.Insert( pRoot, "heightfields", key_value_type_t::ARRAY );
        for ( usize i = 0u; w.bOk && i < GeometryDocument_HeightFieldCount( pDocument ); ++i ) {
            WriteHeightField( w, pFields, *GeometryDocument_HeightFieldAt( pDocument, i ) );
        }
        if ( !w.bOk ) { return Fail( geometry_serialization_status_t::OUT_OF_MEMORY, "heightfields" ); }
    }
    return {};
}

geometry_serialization_result_t SurfaceSerialization_TryRead(
    const key_value_t *pRoot,
    geometry_document_t *pDocument,
    bool bRequireClaimed ) noexcept
{
    if ( pRoot == nullptr || !GeometryDocument_IsInitialized( pDocument ) ) {
        return Fail( geometry_serialization_status_t::INVALID_ARGUMENT );
    }
    const key_value_t *pPatches = KeyValue_Find( pRoot, StringView_FromCString( "patches" ) );
    if ( pPatches != nullptr ) {
        if ( KeyValue_Type( pPatches ) != key_value_type_t::ARRAY ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "patches" );
        }
        if ( KeyValue_ChildCount( pPatches ) > kGeometryDocumentPatchesMax ) {
            return Fail( geometry_serialization_status_t::LIMIT_EXCEEDED, "patches" );
        }
        for ( usize i = 0u; i < KeyValue_ChildCount( pPatches ); ++i ) {
            const geometry_serialization_result_t r = ReadPatch( KeyValue_ChildAt( pPatches, i ), i, pDocument, bRequireClaimed );
            if ( r.status != geometry_serialization_status_t::OK ) { return r; }
        }
    }
    const key_value_t *pFields = KeyValue_Find( pRoot, StringView_FromCString( "heightfields" ) );
    if ( pFields != nullptr ) {
        if ( KeyValue_Type( pFields ) != key_value_type_t::ARRAY ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "heightfields" );
        }
        if ( KeyValue_ChildCount( pFields ) > kGeometryDocumentHeightFieldsMax ) {
            return Fail( geometry_serialization_status_t::LIMIT_EXCEEDED, "heightfields" );
        }
        for ( usize i = 0u; i < KeyValue_ChildCount( pFields ); ++i ) {
            const geometry_serialization_result_t r = ReadHeightField( KeyValue_ChildAt( pFields, i ), i, pDocument, bRequireClaimed );
            if ( r.status != geometry_serialization_status_t::OK ) { return r; }
        }
    }
    return {};
}

} // namespace cypher::editor::geometry
