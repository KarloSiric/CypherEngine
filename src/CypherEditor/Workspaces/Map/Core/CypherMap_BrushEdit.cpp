//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Convex brush commands compose validated clipping, hollowing and provenance
// adoption. The editor publishes the private map only after the whole batch.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_BrushEdit.h"

#include "CypherGeometry_BrushClip.h"
#include "CypherGeometry_BrushCsgAdoption.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentBrushReplacement.h"
#include "CypherCommon/Mathlib/CypherMath_UV.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;
namespace
{

string_view_t SV( const char *p ) noexcept { return StringView_FromCString( p ); }
map_status_t Status( geo::geometry_status_t status ) noexcept
{
    if ( status == geo::geometry_status_t::OK ) { return map_status_t::OK; }
    if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
    if ( status == geo::geometry_status_t::LIMIT_EXCEEDED ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}

map_status_t Check( const map_document_t *pMap, span_t<const u64> ids ) noexcept
{
    if ( !pMap || !pMap->pAllocator || !Span_IsValid( ids ) || ids.nCount == 0u ) { return map_status_t::INVALID_ARGUMENT; }
    if ( pMap->bReadOnly ) { return map_status_t::READ_ONLY; }
    for ( usize i = 0u; i < ids.nCount; ++i ) {
        if ( !geo::GeometryDocument_FindBrush( &pMap->geometry, { ids.pData[i] } ) ) { return map_status_t::UNKNOWN_OBJECT; }
        for ( usize k = 0u; k < i; ++k ) { if ( ids.pData[k] == ids.pData[i] ) { return map_status_t::INVALID_ARGUMENT; } }
    }
    return map_status_t::OK;
}

bool MaterialValid( string_view_t path ) noexcept
{
    if ( !StringView_IsValid( path ) || path.cchLength > MAP_MATERIAL_PATH_MAX ) { return false; }
    for ( usize i = 0u; i < path.cchLength; ++i ) {
        if ( static_cast<unsigned char>( path.pData[i] ) < 0x20u || path.pData[i] == '\\' ) { return false; }
    }
    return true;
}

geo::geometry_source_id_allocator_t IdAllocator( const map_document_t *pMap ) noexcept
{
    return { { std::max( pMap->nextId, pMap->geometry.sourceIds.allocator.next.value ) } };
}

key_value_t *FindParent( key_value_t *pRoot, const key_value_t *pChild, usize depth = 0u ) noexcept
{
    if ( depth >= CY_KEY_VALUE_MAX_DEPTH ) { return nullptr; }
    for ( usize i = 0u; i < KeyValue_ChildCount( pRoot ); ++i ) {
        auto *pItem = KeyValue_ChildAt( pRoot, i );
        if ( pItem == pChild ) { return pRoot; }
        if ( auto *pFound = FindParent( pItem, pChild, depth + 1u ) ) { return pFound; }
    }
    return nullptr;
}

struct source_t {
    geo::brush_source_t value{};
    ~source_t() { geo::BrushSource_Shutdown( &value ); }
};
struct boundary_t {
    geo::brush_boundary_t value{};
    ~boundary_t() { geo::BrushBoundary_Shutdown( &value ); }
};
struct solid_t {
    geo::brush_solid_t value{};
    ~solid_t() { geo::BrushSolid_Shutdown( &value ); }
};
struct hollow_result_t {
    geo::brush_csg_subtract_result_t raw{};
    geo::brush_csg_adoption_result_t adopted{};
    ~hollow_result_t()
    {
        geo::BrushCsgAdoptionResult_Shutdown( &adopted );
        geo::BrushCSGSubtractResult_Shutdown( &raw );
    }
};
struct kv_document_t {
    key_value_document_t *pDocument{};
    ~kv_document_t() { KeyValue_DestroyDocument( pDocument ); }
    bool Init( const allocator_t *pAllocator ) noexcept
    {
        key_value_document_desc_t desc{}; desc.pAllocator = pAllocator;
        pDocument = KeyValue_CreateDocument( desc );
        return pDocument && KeyValue_SetRootType( pDocument, key_value_type_t::OBJECT );
    }
};

// A geometry replacement may produce several roots. Retained record nodes
// cannot be borrowed after removing the source, so own their complete subtree
// first. The original record container and placement are stable map metadata.
struct retained_t {
    kv_document_t owned{};
    map_chunk_t *pChunk{};
    key_value_t *pParent{};
    u32 iLayer{};
    u64 owner{};
};

map_status_t Retain( map_document_t *pMap, u64 id, retained_t *pOut ) noexcept
{
    for ( usize i = 0u; i < pMap->geometryRecords.nCount; ++i ) {
        const auto &record = pMap->geometryRecords.pData[i];
        if ( record.id == id ) { pOut->iLayer = record.iLayer; pOut->owner = record.owner; break; }
    }
    if ( pOut->iLayer >= pMap->layers.nCount ) { return map_status_t::UNKNOWN_LAYER; }
    const auto *pRecord = MapDocument_FindObject( pMap, id, &pOut->pChunk );
    if ( !pRecord ) { return map_status_t::OK; } // Unsaved geometry has no retained fields yet.
    if ( !pOut->pChunk ) { return map_status_t::GEOMETRY_FAILED; }
    pOut->pParent = FindParent( KeyValue_Root( pOut->pChunk->store.pDocument ), pRecord );
    if ( !pOut->pParent || KeyValue_Type( pOut->pParent ) != key_value_type_t::ARRAY ) { return map_status_t::GEOMETRY_FAILED; }
    if ( !pOut->owned.Init( pMap->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto *pRoot = KeyValue_Root( pOut->owned.pDocument );
    for ( usize i = 0u; i < KeyValue_ChildCount( pRecord ); ++i ) {
        if ( !KeyValue_CloneInto( pOut->owned.pDocument, pRoot, KeyValue_ChildAt( pRecord, i ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    return map_status_t::OK;
}

const key_value_t *FindFace( const key_value_t *pRecord, u64 sideId ) noexcept
{
    const auto *pFaces = KeyValue_Find( pRecord, SV( "faces" ) );
    for ( usize i = 0u; i < KeyValue_ChildCount( pFaces ); ++i ) {
        const auto *pFace = KeyValue_ChildAt( pFaces, i ); u64 id = 0u;
        if ( KeyValue_GetU64( KeyValue_Find( pFace, SV( "id" ) ), &id ) && id == sideId ) { return pFace; }
    }
    return nullptr;
}

// Unsaved geometry normally needs no CYKV record. If a Boolean transfers
// unknown face data onto it, create a minimal map-owned record in a working
// container. Save will place world geometry by its actual bounds as usual;
// the global working chunk is only an in-memory anchor, not a file operation.
map_status_t FragmentAnchor( map_document_t *pMap, const retained_t &retained,
    map_chunk_t **ppChunk, key_value_t **ppParent ) noexcept
{
    map_chunk_t *pChunk = nullptr; key_value_t *pContainer = nullptr;
    if ( retained.owner ) {
        pContainer = MapDocument_FindObject( pMap, retained.owner, &pChunk );
        if ( !pContainer || !pChunk || KeyValue_Type( pContainer ) != key_value_type_t::OBJECT ) { return map_status_t::UNKNOWN_OBJECT; }
    } else {
        const auto layer = SV( pMap->layers.pData[retained.iLayer].id );
        const auto cell = MapCell_Global();
        for ( usize i = 0; i < pMap->chunks.nCount; ++i ) {
            auto *pCandidate = pMap->chunks.pData[i];
            if ( !pCandidate->bDamaged && StringView_Equals( SV( pCandidate->layer ), layer ) && MapCell_Equals( pCandidate->cell, cell ) ) { pChunk = pCandidate; break; }
        }
        if ( !pChunk ) {
            void *pMemory = Allocator_Allocate( pMap->pAllocator, sizeof( map_chunk_t ), alignof( map_chunk_t ) );
            if ( !pMemory ) { return map_status_t::OUT_OF_MEMORY; }
            auto *pPending = new ( pMemory ) map_chunk_t{};
            std::memcpy( pPending->layer, layer.pData, layer.cchLength ); pPending->cell = cell;
            const auto status = SettingsDocument_Init( &pPending->store, pMap->pAllocator,
                { SV( MAP_CHUNK_SCHEMA_ID ), MAP_SCHEMA_VERSION, MAP_SCHEMA_VERSION } );
            if ( status != settings_document_status_t::OK || !Vector_PushBack( &pMap->chunks, pPending ) ) {
                pPending->~map_chunk_t(); Allocator_Free( pMap->pAllocator, pMemory, sizeof( map_chunk_t ), alignof( map_chunk_t ) );
                return map_status_t::OUT_OF_MEMORY;
            }
            pChunk = pPending;
        }
        pContainer = KeyValue_Root( pChunk->store.pDocument );
    }
    auto *pParent = KeyValue_Find( pContainer, SV( "brushes" ) );
    if ( pParent && KeyValue_Type( pParent ) != key_value_type_t::ARRAY ) { return map_status_t::GEOMETRY_FAILED; }
    if ( !pParent ) { pParent = KeyValue_ObjectInsert( pChunk->store.pDocument, pContainer, SV( "brushes" ), key_value_type_t::ARRAY ); }
    if ( !pParent ) { return map_status_t::OUT_OF_MEMORY; }
    *ppChunk = pChunk; *ppParent = pParent; return map_status_t::OK;
}

map_status_t PublishFragmentData( map_document_t *pMap, const retained_t &retained,
    const key_value_t *pRecord ) noexcept
{
    if ( !pRecord ) { return map_status_t::OK; }
    auto *pChunk = retained.pChunk; auto *pParent = retained.pParent;
    if ( !pChunk || !pParent ) {
        const auto status = FragmentAnchor( pMap, retained, &pChunk, &pParent );
        if ( status != map_status_t::OK ) { return status; }
    }
    return KeyValue_CloneInto( pChunk->store.pDocument, pParent, pRecord )
        ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

template <typename Resolve>
map_status_t RetainFragmentData( map_document_t *pMap, const retained_t &retained,
    const geo::brush_source_t &source, bool mayHaveAdditionalResidual, Resolve resolve,
    kv_document_t *pPrepared = nullptr ) noexcept
{
    if ( !retained.owned.pDocument && !mayHaveAdditionalResidual ) { return map_status_t::OK; }
    kv_document_t output{}, residual{};
    if ( !output.Init( pMap->pAllocator ) || !residual.Init( pMap->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto *pRecord = KeyValue_Root( output.pDocument );
    auto *pId = KeyValue_ObjectInsert( output.pDocument, pRecord, SV( "id" ), key_value_type_t::U64 );
    if ( !pId || !KeyValue_SetU64( output.pDocument, pId, source.solid.sourceId.value ) ||
         !MapGeometry_WriteBrush( source, pMap->materials, output.pDocument, pRecord ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto *pOriginal = KeyValue_Root( retained.owned.pDocument );
    for ( usize i = 0u; i < KeyValue_ChildCount( pOriginal ); ++i ) {
        const auto *pChild = KeyValue_ChildAt( pOriginal, i ); const auto name = KeyValue_Name( pChild );
        if ( !StringView_Equals( name, SV( "id" ) ) && !MapGeometry_IsGeometryMember( map_geometry_kind_t::BRUSH, name ) &&
             !KeyValue_CloneInto( output.pDocument, pRecord, pChild ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    auto *pFaces = KeyValue_ObjectInsert( residual.pDocument, KeyValue_Root( residual.pDocument ), SV( "faces" ), key_value_type_t::ARRAY );
    if ( !pFaces ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < source.solid.sides.nCount; ++i ) {
        const auto destination = source.solid.sides.pData[i].sourceId;
        const key_value_t *pExtra = nullptr;
        const auto status = resolve( destination, &pExtra );
        if ( status != map_status_t::OK ) { return status; }
        if ( pExtra ) {
            auto *pCopy = KeyValue_CloneInto( residual.pDocument, pFaces, pExtra );
            if ( !pCopy || !KeyValue_SetU64( residual.pDocument, KeyValue_Find( pCopy, SV( "id" ) ), destination.value ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
    }
    if ( !MapGeometry_MergeResidual( map_geometry_kind_t::BRUSH, output.pDocument, pRecord, KeyValue_Root( residual.pDocument ) ) ||
         !MapGeometry_StripRecord( map_geometry_kind_t::BRUSH, output.pDocument, pRecord ) ) { return map_status_t::OUT_OF_MEMORY; }
    // Strip removes canonical face fields and empty face records. If only id
    // remains, an unsaved fragment needs no new metadata container at all.
    if ( !retained.owned.pDocument && KeyValue_ChildCount( pRecord ) <= 1u ) { return map_status_t::OK; }
    if ( pPrepared ) {
        pPrepared->pDocument = output.pDocument; output.pDocument = nullptr;
        return map_status_t::OK;
    }
    return PublishFragmentData( pMap, retained, pRecord );
}

map_status_t RetainFragment( map_document_t *pMap, const retained_t &retained,
    const geo::brush_source_t &source, const geo::brush_csg_adoption_result_t &adopted,
    const retained_t *pCutter = nullptr ) noexcept
{
    const bool additional = pCutter && KeyValue_ChildCount( KeyValue_Find( KeyValue_Root( pCutter->owned.pDocument ), SV( "faces" ) ) ) != 0;
    return RetainFragmentData( pMap, retained, source, additional,
        [&]( geo::geometry_source_id_t destination, const key_value_t **ppExtra ) noexcept {
            const auto *pOrigin = geo::BrushCsgAdoption_FindSideProvenance( &adopted, destination );
            if ( !pOrigin || pOrigin->destinationBrushId.value != source.solid.sourceId.value ) { return map_status_t::GEOMETRY_FAILED; }
            // Hollow resolves both operands against its original brush;
            // subtraction independently owns the cutter's residual subtree.
            const auto *pRecord = pCutter && pOrigin->operand == geo::brush_csg_operand_t::SUBTRAHEND_B
                ? KeyValue_Root( pCutter->owned.pDocument ) : KeyValue_Root( retained.owned.pDocument );
            *ppExtra = FindFace( pRecord, pOrigin->sourceSideId.value ); return map_status_t::OK;
        } );
}

map_status_t AdoptFragments( map_document_t *pMap, u64 id, const geo::brush_csg_adoption_result_t &adopted,
    vector_t<u64> *pRoots, const retained_t *pCutter = nullptr ) noexcept
{
    if ( adopted.cFragments == 0u ) { return map_status_t::GEOMETRY_FAILED; }
    for ( usize i = 0; i < adopted.cFragments; ++i ) {
        if ( adopted.pFragments[i].sides.nCount > MAP_BRUSH_FACES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    }
    retained_t retained{};
    auto mapStatus = Retain( pMap, id, &retained );
    if ( mapStatus == map_status_t::OK ) { mapStatus = MapDocument_RemoveObject( pMap, id ); }
    for ( usize i = 0u; mapStatus == map_status_t::OK && i < adopted.cFragments; ++i ) {
        source_t fragment{};
        auto status = geo::BrushSource_TryBuildWithStore( &adopted.pFragments[i], &adopted.pAttributeStores[i],
            pMap->pAllocator, pMap->geometryPolicy, &fragment.value );
        if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryDocument_TryAddBrushSource( &pMap->geometry, &fragment.value ); }
        mapStatus = Status( status ); const u64 rootId = fragment.value.solid.sourceId.value;
        if ( mapStatus == map_status_t::OK ) { mapStatus = MapDocument_SetGeometryLayer( pMap, rootId, SV( pMap->layers.pData[retained.iLayer].id ) ); }
        if ( mapStatus == map_status_t::OK && retained.owner ) { mapStatus = MapDocument_SetGeometryOwner( pMap, rootId, retained.owner ); }
        if ( mapStatus == map_status_t::OK ) { mapStatus = RetainFragment( pMap, retained, fragment.value, adopted, pCutter ); }
        if ( mapStatus == map_status_t::OK && !Vector_PushBack( pRoots, rootId ) ) { mapStatus = map_status_t::OUT_OF_MEMORY; }
    }
    return mapStatus;
}

map_status_t HollowOne( map_document_t *pMap, u64 id, f64 thickness, vector_t<u64> *pRoots ) noexcept
{
    if ( pMap->nextId == 0u || geo::GeometrySourceIdAllocator_IsExhausted( &pMap->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    source_t original{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &pMap->geometry, { id }, pMap->pAllocator, &original.value );
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    auto allocator = IdAllocator( pMap ); hollow_result_t result{};
    status = geo::BrushCSG_TryHollow( &original.value.solid, thickness, pMap->pAllocator, &allocator, pMap->geometryPolicy, &result.raw );
    if ( status == geo::geometry_status_t::OK ) {
        // Hollow provenance deliberately resolves both the outside and inset
        // operands against the original authored brush after inset destruction.
        status = geo::BrushCsgAdoption_TryPrepareSubtraction( &result.raw,
            &original.value.solid, &original.value.attributes, &original.value.solid, &original.value.attributes,
            pMap->pAllocator, &allocator, pMap->geometryPolicy, &result.adopted );
    }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    auto mapStatus = AdoptFragments( pMap, id, result.adopted, pRoots );
    if ( mapStatus == map_status_t::OK ) { pMap->nextId = allocator.next.value; }
    return mapStatus;
}

map_status_t SubtractOne( map_document_t *pMap, u64 id, const source_t &cutter, const boundary_t &cutterBoundary,
    retained_t *pRetainedCutter, bool *pCutterRetained, vector_t<u64> *pRoots, bool_t *pChanged ) noexcept
{
    source_t target{}; boundary_t targetBoundary{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &pMap->geometry, { id }, pMap->pAllocator, &target.value );
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_Init( &targetBoundary.value, pMap->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &targetBoundary.value, &target.value.solid, pMap->geometryPolicy ); }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    const auto classification = geo::BrushCSG_Classify( &target.value.solid, &targetBoundary.value,
        &cutter.value.solid, &cutterBoundary.value, pMap->geometryPolicy.numerical.fCoplanarDistanceTolerance );
    if ( classification == geo::brush_csg_classification_t::INVALID ) { return map_status_t::GEOMETRY_FAILED; }
    if ( classification == geo::brush_csg_classification_t::DISJOINT || classification == geo::brush_csg_classification_t::TOUCHING ) {
        return Vector_PushBack( pRoots, id ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    }
    if ( classification == geo::brush_csg_classification_t::A_INSIDE_B ) {
        const auto mapStatus = MapDocument_RemoveObject( pMap, id );
        if ( mapStatus == map_status_t::OK ) { *pChanged = CY_TRUE; }
        return mapStatus;
    }
    if ( pMap->nextId == 0u || geo::GeometrySourceIdAllocator_IsExhausted( &pMap->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    if ( !*pCutterRetained ) {
        const auto retainedStatus = Retain( pMap, cutter.value.solid.sourceId.value, pRetainedCutter );
        if ( retainedStatus != map_status_t::OK ) { return retainedStatus; }
        *pCutterRetained = true;
    }
    auto allocator = IdAllocator( pMap ); hollow_result_t result{};
    status = geo::BrushCSG_TrySubtract( &target.value.solid, &cutter.value.solid,
        pMap->pAllocator, &allocator, pMap->geometryPolicy, &result.raw );
    if ( status == geo::geometry_status_t::OK ) {
        status = geo::BrushCsgAdoption_TryPrepareSubtraction( &result.raw,
            &target.value.solid, &target.value.attributes, &cutter.value.solid, &cutter.value.attributes,
            pMap->pAllocator, &allocator, pMap->geometryPolicy, &result.adopted );
    }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    const auto mapStatus = AdoptFragments( pMap, id, result.adopted, pRoots, pRetainedCutter );
    if ( mapStatus == map_status_t::OK ) { pMap->nextId = allocator.next.value; *pChanged = CY_TRUE; }
    return mapStatus;
}

bool HasSide( const geo::brush_solid_t *pBrush, geo::geometry_source_id_t id ) noexcept
{
    for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) { if ( pBrush->sides.pData[i].sourceId.value == id.value ) { return true; } }
    return false;
}

struct clip_ancestry_t { geo::geometry_source_id_t destination{}, source{}; }; // Zero source is a new cut face.

map_status_t PrepareClip( map_document_t *pMap, const geo::brush_source_t &original, math::planed_t plane,
    u64 materialRef, geo::geometry_source_id_allocator_t *pIds, bool fresh, source_t *pOut,
    vector_t<clip_ancestry_t> *pAncestry ) noexcept
{
    solid_t clipped{};
    auto status = geo::BrushSolid_DeepCopy( &clipped.value, &original.solid, pMap->pAllocator, pMap->geometryPolicy.limits );
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushClip_TryClip( &clipped.value, plane, pIds, pMap->geometryPolicy ); }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    if ( clipped.value.sides.nCount > MAP_BRUSH_FACES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    status = geo::BrushSource_TryBuildDefault( &clipped.value, pMap->pAllocator, pMap->geometryPolicy, &pOut->value );
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    if ( !Vector_Init( pAncestry, pMap->pAllocator, clipped.value.sides.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    if ( fresh ) {
        const auto root = geo::GeometrySourceIdAllocator_Allocate( pIds );
        if ( root.status != geo::geometry_status_t::OK ) { return Status( root.status ); }
        pOut->value.solid.sourceId = root.id;
    }
    for ( usize i = 0; i < clipped.value.sides.nCount; ++i ) {
        const auto &oldSide = clipped.value.sides.pData[i]; const geo::brush_solid_side_t *pAncestor = nullptr;
        for ( usize j = 0; j < original.solid.sides.nCount; ++j ) {
            if ( original.solid.sides.pData[j].sourceId.value == oldSide.sourceId.value ) { pAncestor = &original.solid.sides.pData[j]; break; }
        }
        geo::geometry_brush_side_attributes_t attributes{};
        if ( pAncestor ) {
            status = geo::BrushSideAttributeStore_TryGet( &original.attributes, pAncestor->iAttributeIndex, &attributes );
        } else {
            attributes.material.value = materialRef;
            const auto normal = oldSide.plane.normal;
            const math::vec3d_t up = std::abs( normal.z ) > 0.9 ? math::vec3d_t{ 0, 1, 0 } : math::vec3d_t{ 0, 0, 1 };
            if ( !math::Uvd_TryBuildPlanarMapping( {}, normal, up, { 128, 128 }, 0, {},
                pMap->geometryPolicy.numerical.fAbsoluteDistanceTolerance, &attributes.uvProjection ) ) { return map_status_t::GEOMETRY_FAILED; }
        }
        if ( status == geo::geometry_status_t::OK ) { status = geo::BrushSideAttributeStore_TrySet( &pOut->value.attributes, pMap->geometryPolicy.numerical, i, attributes ); }
        if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
        auto &side = pOut->value.solid.sides.pData[i];
        if ( fresh ) {
            const auto id = geo::GeometrySourceIdAllocator_Allocate( pIds );
            if ( id.status != geo::geometry_status_t::OK ) { return Status( id.status ); }
            side.sourceId = id.id;
        }
        if ( !Vector_PushBack( pAncestry, clip_ancestry_t{ side.sourceId, pAncestor ? pAncestor->sourceId : geo::GEOMETRY_SOURCE_ID_INVALID } ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    return map_status_t::OK;
}

map_status_t AddClipHalf( map_document_t *pMap, const retained_t &retained, const source_t &half,
    const vector_t<clip_ancestry_t> &ancestry, vector_t<u64> *pRoots ) noexcept
{
    auto result = Status( geo::GeometryDocument_TryAddBrushSource( &pMap->geometry, &half.value ) );
    const u64 root = half.value.solid.sourceId.value;
    if ( result == map_status_t::OK ) { result = MapDocument_SetGeometryLayer( pMap, root, SV( pMap->layers.pData[retained.iLayer].id ) ); }
    if ( result == map_status_t::OK && retained.owner ) { result = MapDocument_SetGeometryOwner( pMap, root, retained.owner ); }
    if ( result == map_status_t::OK ) {
        result = RetainFragmentData( pMap, retained, half.value, false,
            [&]( geo::geometry_source_id_t destination, const key_value_t **ppExtra ) noexcept {
                for ( usize i = 0; i < ancestry.nCount; ++i ) {
                    if ( ancestry.pData[i].destination.value == destination.value ) {
                        *ppExtra = FindFace( KeyValue_Root( retained.owned.pDocument ), ancestry.pData[i].source.value ); return map_status_t::OK;
                    }
                }
                return map_status_t::GEOMETRY_FAILED;
            } );
    }
    if ( result == map_status_t::OK && !Vector_PushBack( pRoots, root ) ) { result = map_status_t::OUT_OF_MEMORY; }
    return result;
}

map_status_t ClipModeOne( map_document_t *pMap, u64 id, math::planed_t plane, map_brush_clip_mode_t mode,
    string_view_t material, vector_t<u64> *pRoots, bool_t *pChanged ) noexcept
{
    source_t original{}; boundary_t boundary{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &pMap->geometry, { id }, pMap->pAllocator, &original.value );
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_Init( &boundary.value, pMap->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &boundary.value, &original.value.solid, pMap->geometryPolicy ); }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    const auto classification = geo::BrushClip_Classify( &boundary.value, plane, pMap->geometryPolicy.numerical );
    if ( classification == geo::brush_clip_classification_t::INVALID ) { return map_status_t::INVALID_ARGUMENT; }
    if ( classification != geo::brush_clip_classification_t::INTERSECTS ) {
        const bool discarded = ( mode == map_brush_clip_mode_t::BACK && classification == geo::brush_clip_classification_t::ALL_FRONT ) ||
            ( mode == map_brush_clip_mode_t::FRONT && classification == geo::brush_clip_classification_t::ALL_BACK );
        if ( discarded ) {
            const auto result = MapDocument_RemoveObject( pMap, id ); if ( result == map_status_t::OK ) { *pChanged = CY_TRUE; } return result;
        }
        return Vector_PushBack( pRoots, id ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    }
    if ( pMap->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &pMap->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    u64 ref = 0; auto result = MapDocument_MaterialRef( pMap, material, &ref );
    if ( result != map_status_t::OK ) { return result; }
    auto ids = IdAllocator( pMap ); source_t back{}, front{}; vector_t<clip_ancestry_t> backAncestry{}, frontAncestry{};
    if ( mode != map_brush_clip_mode_t::FRONT ) { result = PrepareClip( pMap, original.value, plane, ref, &ids, mode == map_brush_clip_mode_t::BOTH, &back, &backAncestry ); }
    if ( result == map_status_t::OK && mode != map_brush_clip_mode_t::BACK ) {
        result = PrepareClip( pMap, original.value, math::Planed_Flip( plane ), ref, &ids, mode == map_brush_clip_mode_t::BOTH, &front, &frontAncestry );
    }
    if ( result != map_status_t::OK ) { return result; }
    if ( mode == map_brush_clip_mode_t::BOTH ) {
        retained_t retained{}; result = Retain( pMap, id, &retained );
        if ( result == map_status_t::OK ) { result = MapDocument_RemoveObject( pMap, id ); }
        if ( result == map_status_t::OK ) { result = AddClipHalf( pMap, retained, back, backAncestry, pRoots ); }
        if ( result == map_status_t::OK ) { result = AddClipHalf( pMap, retained, front, frontAncestry, pRoots ); }
    } else {
        const auto &half = mode == map_brush_clip_mode_t::BACK ? back : front;
        const geo::geometry_source_id_t source{ id };
        result = Status( geo::GeometryDocument_TryReplaceBrushSourcesExact( &pMap->geometry, { &source, 1 }, { &half.value, 1 } ) );
        if ( result == map_status_t::OK && !Vector_PushBack( pRoots, id ) ) { result = map_status_t::OUT_OF_MEMORY; }
    }
    if ( result == map_status_t::OK ) { pMap->nextId = ids.next.value; *pChanged = CY_TRUE; }
    return result;
}

map_status_t ClipOne( map_document_t *pMap, u64 id, math::planed_t plane, string_view_t material, bool_t *pChanged ) noexcept
{
    source_t clipped{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &pMap->geometry, { id }, pMap->pAllocator, &clipped.value );
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    auto allocator = IdAllocator( pMap ); const u64 nextBefore = allocator.next.value;
    status = geo::BrushClip_TryClip( &clipped.value.solid, plane, &allocator, pMap->geometryPolicy );
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    if ( allocator.next.value == nextBefore ) { return map_status_t::OK; }
    if ( pMap->nextId == 0u || geo::GeometrySourceIdAllocator_IsExhausted( &pMap->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    u64 materialRef = 0u;
    auto mapStatus = MapDocument_MaterialRef( pMap, material, &materialRef );
    if ( mapStatus != map_status_t::OK ) { return mapStatus; }
    const auto *pOriginal = geo::GeometryDocument_FindBrush( &pMap->geometry, { id } );
    bool foundCut = false;
    for ( usize i = 0u; i < clipped.value.solid.sides.nCount; ++i ) {
        auto &side = clipped.value.solid.sides.pData[i];
        if ( HasSide( pOriginal, side.sourceId ) ) { continue; }
        if ( foundCut ) { return map_status_t::GEOMETRY_FAILED; }
        foundCut = true;
        geo::geometry_brush_side_attributes_t attributes{}; attributes.material.value = materialRef;
        const auto normal = side.plane.normal;
        const math::vec3d_t up = std::abs( normal.z ) > 0.9 ? math::vec3d_t{ 0, 1, 0 } : math::vec3d_t{ 0, 0, 1 };
        if ( !math::Uvd_TryBuildPlanarMapping( {}, normal, up, { 128, 128 }, 0, {},
            pMap->geometryPolicy.numerical.fAbsoluteDistanceTolerance, &attributes.uvProjection ) ) { return map_status_t::GEOMETRY_FAILED; }
        usize iAttribute = 0u;
        status = geo::BrushSideAttributeStore_TryAppend( &clipped.value.attributes, pMap->geometryPolicy, attributes, &iAttribute );
        if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
        side.iAttributeIndex = static_cast<u32>( iAttribute );
    }
    if ( !foundCut ) { return map_status_t::GEOMETRY_FAILED; }
    const geo::geometry_source_id_t sourceId{ id };
    status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &pMap->geometry, { &sourceId, 1u }, { &clipped.value, 1u } );
    if ( status == geo::geometry_status_t::OK ) { pMap->nextId = allocator.next.value; *pChanged = CY_TRUE; }
    // Retained root metadata is unchanged. The codec merges residual fields
    // onto surviving side IDs; pruned sides do not reappear when saving.
    return Status( status );
}

const map_geometry_record_t *Placement( const map_document_t *pMap, u64 id ) noexcept
{
    for ( usize i = 0; i < pMap->geometryRecords.nCount; ++i ) {
        if ( pMap->geometryRecords.pData[i].id == id ) { return &pMap->geometryRecords.pData[i]; }
    }
    return nullptr;
}

map_status_t MergeStatus( geo::geometry_status_t status ) noexcept
{
    // The ID allocator reports exhausted identity space as insufficient
    // capacity. It is an authoring limit, not a malformed geometric union.
    return status == geo::geometry_status_t::INSUFFICIENT_CAPACITY ? map_status_t::LIMIT_EXCEEDED : Status( status );
}

// Existing hull support planes alone do not prove a convex union: two
// overlapping clipped cubes can supply every cube support plane while leaving
// an interior notch. Prove H\A is covered by B using the backend's convex
// subtraction fragments. A convex fragment is contained in convex B exactly
// when every reconstructed vertex satisfies all of B's half-spaces.
map_status_t CheckMergeUnion( map_document_t *pMap, const geo::brush_solid_t &hull,
    const geo::brush_solid_t &a, const geo::brush_boundary_t &boundaryA,
    const geo::brush_solid_t &b, geo::geometry_source_id_allocator_t scratchIds ) noexcept
{
    boundary_t hullBoundary{};
    auto status = geo::BrushBoundary_Init( &hullBoundary.value, pMap->pAllocator );
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &hullBoundary.value, &hull, pMap->geometryPolicy ); }
    if ( status != geo::geometry_status_t::OK ) { return MergeStatus( status ); }
    const auto tolerance = pMap->geometryPolicy.numerical.fCoplanarDistanceTolerance;
    const auto relationship = geo::BrushCSG_Classify( &hull, &hullBoundary.value, &a, &boundaryA, tolerance );
    if ( relationship == geo::brush_csg_classification_t::INVALID ) { return map_status_t::GEOMETRY_FAILED; }
    if ( relationship == geo::brush_csg_classification_t::A_INSIDE_B ) { return map_status_t::OK; }
    hollow_result_t difference{};
    status = geo::BrushCSG_TrySubtract( &hull, &a, pMap->pAllocator, &scratchIds, pMap->geometryPolicy, &difference.raw );
    if ( status != geo::geometry_status_t::OK ) { return MergeStatus( status ); }
    if ( difference.raw.cFragments == 0 ) { return map_status_t::GEOMETRY_FAILED; }
    for ( usize i = 0; i < difference.raw.cFragments; ++i ) {
        boundary_t fragment{};
        status = geo::BrushBoundary_Init( &fragment.value, pMap->pAllocator );
        if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &fragment.value, &difference.raw.fragments[i], pMap->geometryPolicy ); }
        if ( status != geo::geometry_status_t::OK ) { return MergeStatus( status ); }
        for ( usize j = 0; j < fragment.value.vertices.nCount; ++j ) {
            for ( usize k = 0; k < b.sides.nCount; ++k ) {
                const auto distance = math::Planed_SignedDistance( b.sides.pData[k].plane, fragment.value.vertices.pData[j] );
                if ( !std::isfinite( distance ) || distance > tolerance ) { return map_status_t::GEOMETRY_FAILED; }
            }
        }
    }
    return map_status_t::OK;
}

struct merge_ancestry_t { geo::geometry_source_id_t destination{}, source{}; bool fromB{}; };

map_status_t PrepareMergeSource( map_document_t *pMap, const geo::brush_solid_t &raw,
    const source_t &a, const source_t &b, geo::geometry_source_id_allocator_t *pIds,
    source_t *pOut, vector_t<merge_ancestry_t> *pAncestry ) noexcept
{
    if ( raw.sides.nCount > MAP_BRUSH_FACES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    auto status = geo::BrushSource_TryBuildDefault( &raw, pMap->pAllocator, pMap->geometryPolicy, &pOut->value );
    if ( status != geo::geometry_status_t::OK ) { return MergeStatus( status ); }
    if ( !Vector_Init( pAncestry, pMap->pAllocator, raw.sides.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto root = geo::GeometrySourceIdAllocator_Allocate( pIds );
    if ( root.status != geo::geometry_status_t::OK ) { return MergeStatus( root.status ); }
    pOut->value.solid.sourceId = root.id;
    for ( usize i = 0; i < raw.sides.nCount; ++i ) {
        const auto inheritedId = raw.sides.pData[i].sourceId;
        const geo::brush_solid_side_t *pAncestor = nullptr;
        const source_t *pSource = nullptr;
        for ( const auto *pOperand : { &a, &b } ) {
            for ( usize j = 0; j < pOperand->value.solid.sides.nCount; ++j ) {
                const auto &side = pOperand->value.solid.sides.pData[j];
                if ( side.sourceId.value != inheritedId.value ) { continue; }
                // A document side ID must resolve to exactly one ancestor,
                // including equivalent planes with different local indices.
                if ( pAncestor ) { return map_status_t::GEOMETRY_FAILED; }
                pAncestor = &side; pSource = pOperand;
            }
        }
        if ( !pAncestor || !pSource ) { return map_status_t::GEOMETRY_FAILED; }
        geo::geometry_brush_side_attributes_t attributes{};
        status = geo::BrushSideAttributeStore_TryGet( &pSource->value.attributes, pAncestor->iAttributeIndex, &attributes );
        if ( status == geo::geometry_status_t::OK ) { status = geo::BrushSideAttributeStore_TrySet( &pOut->value.attributes, pMap->geometryPolicy.numerical, i, attributes ); }
        if ( status != geo::geometry_status_t::OK ) { return MergeStatus( status ); }
        const auto fresh = geo::GeometrySourceIdAllocator_Allocate( pIds );
        if ( fresh.status != geo::geometry_status_t::OK ) { return MergeStatus( fresh.status ); }
        pOut->value.solid.sides.pData[i].sourceId = fresh.id;
        if ( !Vector_PushBack( pAncestry, merge_ancestry_t{ fresh.id, inheritedId, pSource == &b } ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    return map_status_t::OK;
}

} // namespace

bool_t MapBrushEdit_CanEdit( const map_document_t *pMap, span_t<const u64> ids ) noexcept
{
    return Check( pMap, ids ) == map_status_t::OK ? CY_TRUE : CY_FALSE;
}

map_status_t MapBrushEdit_Hollow( map_document_t *pMap, span_t<const u64> ids, f64 thickness, vector_t<u64> *pRootsOut ) noexcept
{
    auto status = Check( pMap, ids );
    if ( status != map_status_t::OK ) { return status; }
    if ( !pRootsOut || !Vector_IsValid( pRootsOut ) || !Allocator_IsValid( pRootsOut->pAllocator ) || !std::isfinite( thickness ) || thickness <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    vector_t<u64> roots{};
    if ( !Vector_Init( &roots, pMap->pAllocator, ids.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < ids.nCount; ++i ) {
        status = HollowOne( pMap, ids.pData[i], thickness, &roots );
        if ( status != map_status_t::OK ) { return status; }
    }
    ++pMap->geometry.revision;
    Vector_Shutdown( pRootsOut ); Vector_Move( pRootsOut, &roots ); return map_status_t::OK;
}

map_status_t MapBrushEdit_Clip( map_document_t *pMap, span_t<const u64> ids, math::planed_t plane,
    string_view_t cutMaterial, vector_t<u64> *pRootsOut, bool_t *pChangedOut ) noexcept
{
    auto status = Check( pMap, ids );
    if ( status != map_status_t::OK ) { return status; }
    if ( !pRootsOut || !Vector_IsValid( pRootsOut ) || !Allocator_IsValid( pRootsOut->pAllocator ) || !MaterialValid( cutMaterial ) ) { return map_status_t::INVALID_ARGUMENT; }
    vector_t<u64> roots{};
    if ( !Vector_Init( &roots, pMap->pAllocator, ids.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    bool_t changed = CY_FALSE;
    for ( usize i = 0u; i < ids.nCount; ++i ) {
        status = ClipOne( pMap, ids.pData[i], plane, cutMaterial, &changed );
        if ( status != map_status_t::OK ) { return status; }
        if ( !Vector_PushBack( &roots, ids.pData[i] ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    if ( changed ) { ++pMap->geometry.revision; }
    Vector_Shutdown( pRootsOut ); Vector_Move( pRootsOut, &roots );
    if ( pChangedOut ) { *pChangedOut = changed; }
    return map_status_t::OK;
}

map_status_t MapBrushEdit_Subtract( map_document_t *pMap, span_t<const u64> ids, u64 cutterId,
    vector_t<u64> *pRootsOut, bool_t *pChangedOut ) noexcept
{
    auto mapStatus = Check( pMap, ids );
    if ( mapStatus != map_status_t::OK ) { return mapStatus; }
    if ( !pRootsOut || !Vector_IsValid( pRootsOut ) || !Allocator_IsValid( pRootsOut->pAllocator ) ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < ids.nCount; ++i ) { if ( ids.pData[i] == cutterId ) { return map_status_t::INVALID_ARGUMENT; } }
    if ( !geo::GeometryDocument_FindBrush( &pMap->geometry, { cutterId } ) ) { return map_status_t::UNKNOWN_OBJECT; }
    // The cutter is owned independently: document arrays can move when target
    // fragments are adopted, but the same authored cutter must serve the batch.
    source_t cutter{}; boundary_t boundary{}; retained_t retainedCutter{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &pMap->geometry, { cutterId }, pMap->pAllocator, &cutter.value );
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_Init( &boundary.value, pMap->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &boundary.value, &cutter.value.solid, pMap->geometryPolicy ); }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    vector_t<u64> roots{};
    if ( !Vector_Init( &roots, pMap->pAllocator, ids.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    bool cutterRetained = false; bool_t changed = CY_FALSE;
    for ( usize i = 0u; i < ids.nCount; ++i ) {
        mapStatus = SubtractOne( pMap, ids.pData[i], cutter, boundary, &retainedCutter, &cutterRetained, &roots, &changed );
        if ( mapStatus != map_status_t::OK ) { return mapStatus; }
    }
    if ( changed ) { ++pMap->geometry.revision; }
    Vector_Shutdown( pRootsOut ); Vector_Move( pRootsOut, &roots );
    if ( pChangedOut ) { *pChangedOut = changed; }
    return map_status_t::OK;
}

map_status_t MapBrushEdit_ClipMode( map_document_t *pMap, span_t<const u64> ids, math::planed_t plane,
    map_brush_clip_mode_t mode, string_view_t material, vector_t<u64> *pRootsOut, bool_t *pChangedOut ) noexcept
{
    auto status = Check( pMap, ids );
    if ( status != map_status_t::OK ) { return status; }
    if ( !pRootsOut || !Vector_IsValid( pRootsOut ) || !Allocator_IsValid( pRootsOut->pAllocator ) ||
        mode > map_brush_clip_mode_t::BOTH || !MaterialValid( material ) ) { return map_status_t::INVALID_ARGUMENT; }
    vector_t<u64> roots{};
    if ( !Vector_Init( &roots, pMap->pAllocator, ids.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    bool_t changed = CY_FALSE;
    for ( usize i = 0; i < ids.nCount; ++i ) {
        status = ClipModeOne( pMap, ids.pData[i], plane, mode, material, &roots, &changed );
        if ( status != map_status_t::OK ) { return status; }
    }
    if ( changed ) { ++pMap->geometry.revision; }
    Vector_Shutdown( pRootsOut ); Vector_Move( pRootsOut, &roots );
    if ( pChangedOut ) { *pChangedOut = changed; }
    return map_status_t::OK;
}

map_status_t MapBrushEdit_Merge( map_document_t *pMap, span_t<const u64> ids, vector_t<u64> *pRootsOut ) noexcept
{
    auto result = Check( pMap, ids );
    if ( result != map_status_t::OK ) { return result; }
    if ( ids.nCount != 2 || !pRootsOut || !Vector_IsValid( pRootsOut ) || !Allocator_IsValid( pRootsOut->pAllocator ) ) { return map_status_t::INVALID_ARGUMENT; }
    const auto *pA = Placement( pMap, ids.pData[0] ), *pB = Placement( pMap, ids.pData[1] );
    if ( !pA || !pB ) { return map_status_t::GEOMETRY_FAILED; }
    if ( pA->iLayer >= pMap->layers.nCount || pB->iLayer >= pMap->layers.nCount ) { return map_status_t::UNKNOWN_LAYER; }
    if ( pA->iLayer != pB->iLayer || pA->owner != pB->owner ) { return map_status_t::INVALID_ARGUMENT; }
    if ( pMap->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &pMap->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    source_t a{}, b{}, merged{}; boundary_t boundaryA{}, boundaryB{}; solid_t raw{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &pMap->geometry, { ids.pData[0] }, pMap->pAllocator, &a.value );
    if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryDocument_TryCopyBrushSource( &pMap->geometry, { ids.pData[1] }, pMap->pAllocator, &b.value ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_Init( &boundaryA.value, pMap->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &boundaryA.value, &a.value.solid, pMap->geometryPolicy ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_Init( &boundaryB.value, pMap->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &boundaryB.value, &b.value.solid, pMap->geometryPolicy ); }
    if ( status != geo::geometry_status_t::OK ) { return MergeStatus( status ); }
    const auto classification = geo::BrushCSG_Classify( &a.value.solid, &boundaryA.value, &b.value.solid, &boundaryB.value,
        pMap->geometryPolicy.numerical.fCoplanarDistanceTolerance );
    if ( classification == geo::brush_csg_classification_t::INVALID || classification == geo::brush_csg_classification_t::DISJOINT ) { return map_status_t::GEOMETRY_FAILED; }
    auto sourceIds = IdAllocator( pMap );
    status = geo::BrushCSG_TryMerge( &a.value.solid, &boundaryA.value, &b.value.solid, &boundaryB.value,
        pMap->pAllocator, &sourceIds, pMap->geometryPolicy, &raw.value );
    if ( status != geo::geometry_status_t::OK ) { return MergeStatus( status ); }
    if ( raw.value.sides.nCount > MAP_BRUSH_FACES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    result = CheckMergeUnion( pMap, raw.value, a.value.solid, boundaryA.value, b.value.solid, sourceIds );
    if ( result != map_status_t::OK ) { return result; }
    vector_t<merge_ancestry_t> ancestry{};
    result = PrepareMergeSource( pMap, raw.value, a, b, &sourceIds, &merged, &ancestry );
    if ( result != map_status_t::OK ) { return result; }
    retained_t retainedA{}, retainedB{}; kv_document_t metadata{};
    result = Retain( pMap, ids.pData[0], &retainedA );
    if ( result == map_status_t::OK ) { result = Retain( pMap, ids.pData[1], &retainedB ); }
    if ( result == map_status_t::OK ) {
        const bool extra = KeyValue_ChildCount( KeyValue_Find( KeyValue_Root( retainedB.owned.pDocument ), SV( "faces" ) ) ) != 0;
        result = RetainFragmentData( pMap, retainedA, merged.value, extra,
            [&]( geo::geometry_source_id_t destination, const key_value_t **ppExtra ) noexcept {
                for ( usize i = 0; i < ancestry.nCount; ++i ) {
                    const auto &origin = ancestry.pData[i];
                    if ( origin.destination.value != destination.value ) { continue; }
                    const auto *pRecord = KeyValue_Root( origin.fromB ? retainedB.owned.pDocument : retainedA.owned.pDocument );
                    *ppExtra = FindFace( pRecord, origin.source.value ); return map_status_t::OK;
                }
                return map_status_t::GEOMETRY_FAILED;
            }, &metadata );
    }
    if ( result != map_status_t::OK ) { return result; }
    vector_t<u64> roots{};
    if ( !Vector_Init( &roots, pMap->pAllocator, 1 ) || !Vector_PushBack( &roots, merged.value.solid.sourceId.value ) ) { return map_status_t::OUT_OF_MEMORY; }
    // Geometry, identities, surface ancestry and retained fields are prepared
    // before either source is removed. Remaining publication failures affect
    // only the discardable working copy, never the caller's live document.
    result = MapDocument_RemoveObject( pMap, ids.pData[0] );
    if ( result == map_status_t::OK ) { result = MapDocument_RemoveObject( pMap, ids.pData[1] ); }
    if ( result == map_status_t::OK ) { result = Status( geo::GeometryDocument_TryAddBrushSource( &pMap->geometry, &merged.value ) ); }
    const u64 root = merged.value.solid.sourceId.value;
    if ( result == map_status_t::OK ) { result = MapDocument_SetGeometryLayer( pMap, root, SV( pMap->layers.pData[retainedA.iLayer].id ) ); }
    if ( result == map_status_t::OK && retainedA.owner ) { result = MapDocument_SetGeometryOwner( pMap, root, retainedA.owner ); }
    if ( result == map_status_t::OK ) { result = PublishFragmentData( pMap, retainedA, KeyValue_Root( metadata.pDocument ) ); }
    if ( result != map_status_t::OK ) { return result; }
    pMap->nextId = sourceIds.next.value; ++pMap->geometry.revision;
    Vector_Shutdown( pRootsOut ); Vector_Move( pRootsOut, &roots ); return map_status_t::OK;
}

} // namespace cypher::editor::map
