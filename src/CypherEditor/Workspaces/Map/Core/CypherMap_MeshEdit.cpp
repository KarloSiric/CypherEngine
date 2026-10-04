//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Validated brush conversion and mesh modeling edits on private maps.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_MeshEdit.h"

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_MeshSourceModeling.h"
#include "CypherGeometry_MeshSelection.h"
#include "CypherGeometry_MeshTransaction.h"
#include "CypherMath_UV.h"

#include <algorithm>
#include <cmath>

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
    if ( status == geo::geometry_status_t::LIMIT_EXCEEDED || status == geo::geometry_status_t::INSUFFICIENT_CAPACITY ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}
map_status_t Check( const map_document_t *map, span_t<const u64> ids, bool brushes ) noexcept
{
    if ( !map || !map->pAllocator || !Span_IsValid( ids ) || ids.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    for ( usize i = 0; i < ids.nCount; ++i ) {
        if ( brushes ? !geo::GeometryDocument_FindBrush( &map->geometry, { ids.pData[i] } )
                     : !geo::GeometryDocument_FindMesh( &map->geometry, { ids.pData[i] } ) ) { return map_status_t::UNKNOWN_OBJECT; }
        for ( usize k = 0; k < i; ++k ) { if ( ids.pData[k] == ids.pData[i] ) { return map_status_t::INVALID_ARGUMENT; } }
    }
    return map_status_t::OK;
}
struct boundary_t { geo::brush_boundary_t value{}; ~boundary_t() { geo::BrushBoundary_Shutdown( &value ); } };
struct description_t { geo::mesh_source_description_t value{}; ~description_t() { geo::MeshSourceDescription_Shutdown( &value ); } };
struct source_t { geo::mesh_source_t value{}; ~source_t() { geo::MeshSource_Shutdown( &value ); } };
struct transaction_t { geo::geometry_mesh_transaction_t value{}; ~transaction_t() { geo::GeometryMeshTransaction_Cancel( &value ); } };
struct delta_t { geo::geometry_mesh_delta_t value{}; ~delta_t() { geo::GeometryMeshDelta_Shutdown( &value ); } };
struct provenance_t { geo::mesh_edit_provenance_t value{}; ~provenance_t() { geo::MeshEditProvenance_Shutdown( &value ); } };
struct lineage_t { geo::mesh_edit_lineage_t value{}; ~lineage_t() { geo::MeshEditLineage_Shutdown( &value ); } };
struct residual_t {
    key_value_document_t *document{};
    map_chunk_t *chunk{};
    key_value_t *record{}, *parent{}, *container{};
    ~residual_t() { KeyValue_DestroyDocument( document ); }
};
key_value_t *Parent( key_value_t *root, const key_value_t *child, usize depth = 0 ) noexcept
{
    if ( depth >= CY_KEY_VALUE_MAX_DEPTH ) { return nullptr; }
    for ( usize i = 0; i < KeyValue_ChildCount( root ); ++i ) {
        auto *item = KeyValue_ChildAt( root, i );
        if ( item == child ) { return root; }
        if ( auto *found = Parent( item, child, depth + 1 ) ) { return found; }
    }
    return nullptr;
}
const key_value_t *Face( const key_value_t *record, u64 id ) noexcept
{
    const auto *faces = KeyValue_Find( record, SV( "faces" ) );
    for ( usize i = 0; i < KeyValue_ChildCount( faces ); ++i ) {
        const auto *face = KeyValue_ChildAt( faces, i ); u64 found = 0;
        if ( KeyValue_GetU64( KeyValue_Find( face, SV( "id" ) ), &found ) && found == id ) { return face; }
    }
    return nullptr;
}
bool Count( const key_value_t *node, u64 &value ) noexcept
{
    i64 signedValue = 0;
    if ( KeyValue_GetU64( node, &value ) ) { return true; }
    if ( KeyValue_GetI64( node, &signedValue ) && signedValue >= 0 ) { value = static_cast<u64>( signedValue ); return true; }
    return false;
}
map_status_t PrepareResidual( map_document_t *map, u64 id, residual_t &out ) noexcept
{
    out.record = MapDocument_FindObject( map, id, &out.chunk );
    if ( !out.record ) { return map_status_t::OK; }
    if ( !out.chunk ) { return map_status_t::GEOMETRY_FAILED; }
    out.parent = Parent( KeyValue_Root( out.chunk->store.pDocument ), out.record );
    out.container = Parent( KeyValue_Root( out.chunk->store.pDocument ), out.parent );
    if ( !out.parent || !out.container || KeyValue_Type( out.parent ) != key_value_type_t::ARRAY ||
         !StringView_Equals( KeyValue_Name( out.parent ), SV( "brushes" ) ) || KeyValue_Type( out.container ) != key_value_type_t::OBJECT ) { return map_status_t::GEOMETRY_FAILED; }
    key_value_document_desc_t desc{}; desc.pAllocator = map->pAllocator;
    out.document = KeyValue_CreateDocument( desc );
    if ( !out.document || !KeyValue_SetRootType( out.document, key_value_type_t::OBJECT ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto *copy = KeyValue_Root( out.document );
    for ( usize i = 0; i < KeyValue_ChildCount( out.record ); ++i ) {
        if ( !KeyValue_CloneInto( out.document, copy, KeyValue_ChildAt( out.record, i ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    if ( !MapGeometry_StripRecord( map_geometry_kind_t::BRUSH, out.document, copy ) ) { return map_status_t::OUT_OF_MEMORY; }
    // Brush-unknown data can acquire meaning in the destination schema. It
    // must not be overwritten, or interpreted as authored mesh geometry.
    for ( const char *name : { "vertices", "vertex_ids", "edges" } ) {
        if ( KeyValue_Find( copy, SV( name ) ) ) { return map_status_t::INVALID_ARGUMENT; }
    }
    const auto *faces = KeyValue_Find( copy, SV( "faces" ) );
    for ( usize i = 0; i < KeyValue_ChildCount( faces ); ++i ) {
        const auto *face = KeyValue_ChildAt( faces, i );
        for ( const char *name : { "vertices", "uv", "uv2", "colors" } ) {
            if ( KeyValue_Find( face, SV( name ) ) ) { return map_status_t::INVALID_ARGUMENT; }
        }
        u64 smoothing = 0;
        const auto *node = KeyValue_Find( face, SV( "smoothing" ) );
        if ( node && ( !Count( node, smoothing ) || smoothing > CY_U32_MAX ) ) { return map_status_t::INVALID_ARGUMENT; }
    }
    return map_status_t::OK;
}
map_status_t PublishResidual( residual_t &residual ) noexcept
{
    if ( !residual.record ) { return map_status_t::OK; }
    auto *doc = residual.chunk->store.pDocument;
    auto *meshes = KeyValue_Find( residual.container, SV( "meshes" ) );
    if ( meshes && KeyValue_Type( meshes ) != key_value_type_t::ARRAY ) { return map_status_t::INVALID_ARGUMENT; }
    if ( !meshes ) { meshes = KeyValue_ObjectInsert( doc, residual.container, SV( "meshes" ), key_value_type_t::ARRAY ); }
    if ( !meshes || !MapGeometry_StripRecord( map_geometry_kind_t::MESH, residual.document, KeyValue_Root( residual.document ) ) ||
         !KeyValue_CloneInto( doc, meshes, KeyValue_Root( residual.document ) ) || !KeyValue_Remove( doc, residual.parent, residual.record ) ) { return map_status_t::OUT_OF_MEMORY; }
    return map_status_t::OK;
}
map_status_t ConvertOne( map_document_t *map, u64 id, geo::geometry_source_id_allocator_t &ids ) noexcept
{
    residual_t residual;
    auto result = PrepareResidual( map, id, residual );
    if ( result != map_status_t::OK ) { return result; }
    const auto *brush = geo::GeometryDocument_FindBrush( &map->geometry, { id } );
    const auto *attributes = geo::GeometryDocument_FindBrushAttributes( &map->geometry, { id } );
    if ( !brush || !attributes || brush->sides.nCount > MAP_BRUSH_FACES_MAX ) { return map_status_t::GEOMETRY_FAILED; }
    boundary_t boundary; description_t description; source_t source;
    auto status = geo::BrushBoundary_Init( &boundary.value, map->pAllocator );
    if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &boundary.value, brush, map->geometryPolicy ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSourceDescription_Init( &description.value, map->pAllocator, { id } ); }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    if ( boundary.value.vertices.nCount > geo::kMeshSourceVerticesMax || boundary.value.faces.nCount > geo::kMeshSourceFacesMax ||
         boundary.value.faceVertexIndices.nCount > geo::kMeshSourceCornersMax ) { return map_status_t::LIMIT_EXCEEDED; }
    for ( usize i = 0; i < boundary.value.vertices.nCount; ++i ) {
        const auto fresh = geo::GeometrySourceIdAllocator_Allocate( &ids );
        if ( fresh.status != geo::geometry_status_t::OK ) { return Status( fresh.status ); }
        status = geo::MeshSourceDescription_TryAddVertex( &description.value, boundary.value.vertices.pData[i], fresh.id, nullptr );
        if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    }
    for ( usize i = 0; i < boundary.value.faces.nCount; ++i ) {
        const auto &face = boundary.value.faces.pData[i];
        if ( face.cVertices > MAP_MESH_FACE_VERTICES_MAX || face.iSide >= brush->sides.nCount ) { return map_status_t::LIMIT_EXCEEDED; }
        const auto &side = brush->sides.pData[face.iSide];
        geo::geometry_brush_side_attributes_t surface{};
        status = geo::BrushSideAttributeStore_TryGet( attributes, side.iAttributeIndex, &surface );
        if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
        geo::mesh_face_attributes_t meshSurface{}; meshSurface.material = surface.material;
        // Plane-defined brushes shade flat unless explicitly authored otherwise.
        meshSurface.smoothingGroups = 0;
        const auto *extra = Face( KeyValue_Root( residual.document ), side.sourceId.value );
        if ( const auto *node = KeyValue_Find( extra, SV( "smoothing" ) ) ) {
            u64 smoothing = 0;
            if ( !Count( node, smoothing ) || smoothing > CY_U32_MAX ) { return map_status_t::INVALID_ARGUMENT; }
            meshSurface.smoothingGroups = static_cast<u32>( smoothing );
        }
        u32 index = 0;
        status = geo::MeshSourceDescription_TryAddFace( &description.value,
            { boundary.value.faceVertexIndices.pData + face.iFirstIndex, face.cVertices }, side.sourceId, meshSurface, &index );
        if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
        const auto &outputFace = description.value.faces.pData[index];
        for ( u32 k = 0; k < outputFace.cCorners; ++k ) {
            auto &corner = description.value.corners.pData[outputFace.iFirstCorner + k];
            if ( !math::Uvd_TryProjectPlanarPoint( surface.uvProjection, description.value.vertices.pData[corner.iVertex].position,
                    map->geometryPolicy.numerical.fAbsoluteDistanceTolerance, &corner.attributes.uv0 ) ) { return map_status_t::GEOMETRY_FAILED; }
        }
    }
    status = geo::MeshSource_TryBuild( &description.value, map->pAllocator, &source.value );
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    // Identity is untyped: retiring the old brush admits the same root and
    // side identities as this mesh's root and faces. No live foreign ID is
    // overwritten, and allocation/adoption uses the checked document path.
    status = geo::GeometryDocument_TryRemoveBrush( &map->geometry, { id } );
    if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryDocument_TryAddMesh( &map->geometry, &source.value ); }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    return PublishResidual( residual );
}
map_status_t FlipOne( map_document_t *map, u64 id ) noexcept
{
    transaction_t transaction; delta_t delta; description_t description;
    auto status = geo::GeometryMeshTransaction_Begin( &transaction.value, &map->geometry, { id } );
    if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryMeshDelta_Init( &delta.value, map->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSourceDescription_Init( &description.value, map->pAllocator, { id } ); }
    auto *working = geo::GeometryMeshTransaction_Working( &transaction.value );
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSource_TryDescribe( working, &description.value ); }
    vector_t<geo::geometry_source_id_t> faces{};
    if ( status == geo::geometry_status_t::OK && !Vector_Init( &faces, map->pAllocator, description.value.faces.nCount ) ) { status = geo::geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0; status == geo::geometry_status_t::OK && i < description.value.faces.nCount; ++i ) {
        if ( !Vector_PushBack( &faces, description.value.faces.pData[i].sourceId ) ) { status = geo::geometry_status_t::ALLOCATION_FAILED; }
    }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSourceEdit_TryFlipFaces( working, { faces.pData, faces.nCount }, nullptr ); }
    bool changed = false;
    if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryMeshTransaction_Commit( &transaction.value, &delta.value, nullptr, &changed ); }
    Vector_Shutdown( &faces );
    return Status( status );
}
map_status_t PrepareEditedResidual( map_document_t *map, u64 id,
    const geo::mesh_source_description_t &after, const geo::mesh_edit_lineage_t &lineage, residual_t &output ) noexcept
{
    output.record = MapDocument_FindObject( map, id, &output.chunk );
    if ( !output.record ) { return map_status_t::OK; }
    if ( !output.chunk ) { return map_status_t::GEOMETRY_FAILED; }
    output.parent = Parent( KeyValue_Root( output.chunk->store.pDocument ), output.record );
    if ( !output.parent || KeyValue_Type( output.parent ) != key_value_type_t::ARRAY ||
         !StringView_Equals( KeyValue_Name( output.parent ), SV( "meshes" ) ) ) { return map_status_t::GEOMETRY_FAILED; }
    key_value_document_desc_t desc{}; desc.pAllocator = map->pAllocator;
    output.document = KeyValue_CreateDocument( desc );
    if ( !output.document || !KeyValue_SetRootType( output.document, key_value_type_t::OBJECT ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto *record = KeyValue_Root( output.document );
    for ( usize i = 0; i < KeyValue_ChildCount( output.record ); ++i ) {
        const auto *child = KeyValue_ChildAt( output.record, i );
        if ( !StringView_Equals( KeyValue_Name( child ), SV( "faces" ) ) && !KeyValue_CloneInto( output.document, record, child ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    auto *faces = KeyValue_ObjectInsert( output.document, record, SV( "faces" ), key_value_type_t::ARRAY );
    if ( !faces ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0; i < after.faces.nCount; ++i ) {
        const u64 destination = after.faces.pData[i].sourceId.value;
        // Retained faces keep their records directly. New face records are
        // copied only through explicit backend parent IDs, never positions.
        const key_value_t *extra = Face( output.record, destination );
        if ( !extra ) {
            u64 parentId = 0;
            for ( usize k = 0; k < lineage.faces.nCount; ++k ) {
                if ( lineage.faces.pData[k].childId.value == destination ) { parentId = lineage.faces.pData[k].parentId.value; break; }
            }
            if ( parentId ) { extra = Face( output.record, parentId ); }
        }
        if ( !extra ) { continue; } // No residual authored data to inherit.
        auto *copy = KeyValue_CloneInto( output.document, faces, extra );
        if ( !copy || !KeyValue_SetU64( output.document, KeyValue_Find( copy, SV( "id" ) ), destination ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    return MapGeometry_StripRecord( map_geometry_kind_t::MESH, output.document, record ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}
bool HasFace( const geo::mesh_source_description_t &description, u64 id ) noexcept
{
    // Descriptions produced by TryDescribe are canonical and sorted by ID.
    if ( description.faces.nCount == 0 ) { return false; }
    const auto *begin = description.faces.pData;
    const auto *end = begin + description.faces.nCount;
    const auto *found = std::lower_bound( begin, end, id,
        []( const geo::mesh_source_face_t &face, u64 value ) noexcept { return face.sourceId.value < value; } );
    return found != end && found->sourceId.value == id;
}
bool HasValidFaceLineage( const geo::mesh_source_description_t &before,
    const geo::mesh_source_description_t &after, const geo::mesh_edit_lineage_t &lineage ) noexcept
{
    // Every fresh face must have an explicit parent in the baseline, even
    // if that parent carries no unknown authored data to inherit.
    for ( usize i = 0; i < after.faces.nCount; ++i ) {
        const u64 child = after.faces.pData[i].sourceId.value;
        if ( HasFace( before, child ) ) { continue; }
        u64 parent = 0;
        for ( usize k = 0; k < lineage.faces.nCount; ++k ) {
            if ( lineage.faces.pData[k].childId.value == child ) { parent = lineage.faces.pData[k].parentId.value; break; }
        }
        if ( !HasFace( before, parent ) ) { return false; }
    }
    return true;
}
map_status_t PublishEditedResidual( residual_t &residual ) noexcept
{
    if ( residual.record &&
         ( !KeyValue_CloneInto( residual.chunk->store.pDocument, residual.parent, KeyValue_Root( residual.document ) ) ||
           !KeyValue_Remove( residual.chunk->store.pDocument, residual.parent, residual.record ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    return map_status_t::OK;
}
bool NeedsTriangulation( const geo::mesh_source_t *source ) noexcept
{
    // This only predicts fresh ID allocation. Begin still validates and
    // describes the source, including already triangulated selections.
    if ( !geo::MeshSource_IsInitialized( source ) ) { return true; }
    bool needed = false;
    (void)GenerationPool_ForEach( &source->mesh.faces,
        [&]( geo::geometry_mesh_face_handle_t, const geo::mesh_face_record_t &face ) noexcept -> bool_t {
            const auto *loop = GenerationPool_Get( &source->mesh.loops, face.hOuterLoop );
            needed = !loop || loop->cHalfEdges != 3;
            return !needed;
        } );
    return needed;
}
map_status_t TriangulateOne( map_document_t *map, u64 id ) noexcept
{
    transaction_t transaction; delta_t delta; description_t after; provenance_t provenance; lineage_t lineage; residual_t residual;
    auto status = geo::GeometryMeshTransaction_Begin( &transaction.value, &map->geometry, { id } );
    if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryMeshDelta_Init( &delta.value, map->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshEditProvenance_Init( &provenance.value, map->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshEditLineage_Init( &lineage.value, map->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSourceDescription_Init( &after.value, map->pAllocator, { id } ); }
    auto *working = geo::GeometryMeshTransaction_Working( &transaction.value );
    if ( status == geo::geometry_status_t::OK ) {
        bool allTriangles = true;
        for ( usize i = 0; i < transaction.value.baseline.faces.nCount; ++i ) { allTriangles &= transaction.value.baseline.faces.pData[i].cCorners == 3; }
        if ( allTriangles ) { return map_status_t::OK; }
    }
    geo::mesh_edit_report_t report{}; report.pProvenance = &provenance.value;
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSourceEdit_TryTriangulate( working, {}, nullptr, &report ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryMeshTransaction_TryAssignIds( &transaction.value, nullptr ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshEditProvenance_TryToLineage( &provenance.value, working, &lineage.value ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSource_TryDescribe( working, &after.value ); }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    if ( !HasValidFaceLineage( transaction.value.baseline, after.value, lineage.value ) ) { return map_status_t::GEOMETRY_FAILED; }
    const bool noOp = geo::MeshSourceDescription_Equal( &transaction.value.baseline, &after.value );
    if ( !noOp ) {
        const auto prepared = PrepareEditedResidual( map, id, after.value, lineage.value, residual );
        if ( prepared != map_status_t::OK ) { return prepared; }
    }
    bool changed = false;
    status = geo::GeometryMeshTransaction_Commit( &transaction.value, &delta.value, nullptr, &changed );
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    return changed ? PublishEditedResidual( residual ) : map_status_t::OK;
}
enum class face_model_operation_t { EXTRUDE, INSET, QUAD_SLICE };
map_status_t ModelFace( map_document_t *map, u64 meshId, u64 faceId, face_model_operation_t operation,
    f64 amount = 0.0, u32 cU = 0, u32 cV = 0 ) noexcept
{
    const auto checked = Check( map, { &meshId, 1 }, false );
    if ( checked != map_status_t::OK ) { return checked; }
    const bool slice = operation == face_model_operation_t::QUAD_SLICE;
    if ( faceId == 0 || ( slice ? cU == 0 || cV == 0 || cU > geo::kMeshQuadSliceCutsMax || cV > geo::kMeshQuadSliceCutsMax
                              : !std::isfinite( amount ) || amount <= 0.0 ) ) { return map_status_t::INVALID_ARGUMENT; }
    const auto *source = geo::GeometryDocument_FindMesh( &map->geometry, { meshId } );
    geo::geometry_mesh_face_handle_t face{};
    if ( !geo::MeshSource_TryFindFace( source, { faceId }, &face ) ) {
        return map_status_t::UNKNOWN_OBJECT;
    }
    if ( slice ) {
        const auto *record = GenerationPool_Get( &source->mesh.faces, face );
        const auto *loop = record ? GenerationPool_Get( &source->mesh.loops, record->hOuterLoop ) : nullptr;
        if ( !loop || loop->cHalfEdges != 4 ) { return map_status_t::INVALID_ARGUMENT; }
        if ( cU == 1 && cV == 1 ) { return map_status_t::OK; }
    }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ||
         map->geometry.revision == CY_U64_MAX ) { return map_status_t::LIMIT_EXCEEDED; }

    transaction_t transaction; delta_t delta; description_t after; provenance_t provenance; lineage_t lineage; residual_t residual;
    auto status = geo::GeometryMeshTransaction_Begin( &transaction.value, &map->geometry, { meshId } );
    if ( status == geo::geometry_status_t::OK && map->nextId > 1 ) {
        // Map-only objects share this namespace. Raise only the private
        // transaction cursor; a failed topology edit cannot move either
        // published high-water mark before commit.
        status = geo::GeometrySourceIdAllocator_AdvancePast( &transaction.value.tentativeIds, { map->nextId - 1 } );
    }
    if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryMeshDelta_Init( &delta.value, map->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshEditProvenance_Init( &provenance.value, map->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshEditLineage_Init( &lineage.value, map->pAllocator ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSourceDescription_Init( &after.value, map->pAllocator, { meshId } ); }
    auto *working = geo::GeometryMeshTransaction_Working( &transaction.value );
    geo::mesh_edit_report_t report{}; report.pProvenance = &provenance.value;
    if ( status == geo::geometry_status_t::OK ) {
        if ( slice ) {
            const geo::geometry_source_id_t selected{ faceId };
            status = geo::MeshSourceEdit_TryQuadSlice( working, { &selected, 1 }, cU, cV, &report );
        } else {
            status = operation == face_model_operation_t::INSET ? geo::MeshSourceEdit_TryInsetFace( working, { faceId }, amount, &report )
                                                               : geo::MeshSourceEdit_TryExtrudeFace( working, { faceId }, amount, &report );
        }
    }
    if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryMeshTransaction_TryAssignIds( &transaction.value, nullptr ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshEditProvenance_TryToLineage( &provenance.value, working, &lineage.value ); }
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSource_TryDescribe( working, &after.value ); }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    if ( !HasFace( after.value, faceId ) || !HasValidFaceLineage( transaction.value.baseline, after.value, lineage.value ) ) {
        return map_status_t::GEOMETRY_FAILED;
    }
    if ( geo::MeshSourceDescription_Equal( &transaction.value.baseline, &after.value ) ) { return map_status_t::OK; }
    const auto prepared = PrepareEditedResidual( map, meshId, after.value, lineage.value, residual );
    if ( prepared != map_status_t::OK ) { return prepared; }
    bool changed = false;
    status = geo::GeometryMeshTransaction_Commit( &transaction.value, &delta.value, nullptr, &changed );
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    if ( !changed ) { return map_status_t::OK; }
    const auto published = PublishEditedResidual( residual );
    if ( published != map_status_t::OK ) { return published; }
    const auto next = map->geometry.sourceIds.allocator.next.value;
    map->nextId = next == 0 ? 0 : std::max( map->nextId, next );
    return map_status_t::OK;
}
} // namespace

map_status_t MapMeshEdit_ConvertBrushes( map_document_t *map, span_t<const u64> selected, vector_t<u64> *rootsOut ) noexcept
{
    auto status = Check( map, selected, true );
    if ( status != map_status_t::OK ) { return status; }
    if ( !rootsOut || !Vector_IsValid( rootsOut ) || !Allocator_IsValid( rootsOut->pAllocator ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) || map->geometry.revision == CY_U64_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    vector_t<u64> roots{};
    if ( !Vector_Init( &roots, map->pAllocator, selected.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0; i < selected.nCount; ++i ) {
        if ( !Vector_PushBack( &roots, selected.pData[i] ) ) { Vector_Shutdown( &roots ); return map_status_t::OUT_OF_MEMORY; }
    }
    geo::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    for ( usize i = 0; status == map_status_t::OK && i < roots.nCount; ++i ) { status = ConvertOne( map, roots.pData[i], ids ); }
    if ( status == map_status_t::OK ) {
        map->nextId = ids.next.value; ++map->geometry.revision;
        Vector_Shutdown( rootsOut ); Vector_Move( rootsOut, &roots );
    }
    Vector_Shutdown( &roots ); return status;
}

map_status_t MapMeshEdit_FlipNormals( map_document_t *map, span_t<const u64> selected ) noexcept
{
    auto status = Check( map, selected, false );
    if ( status != map_status_t::OK ) { return status; }
    const auto revision = map->geometry.revision;
    if ( revision == CY_U64_MAX || selected.nCount > CY_U64_MAX - revision ) { return map_status_t::LIMIT_EXCEEDED; }
    for ( usize i = 0; status == map_status_t::OK && i < selected.nCount; ++i ) { status = FlipOne( map, selected.pData[i] ); }
    if ( status == map_status_t::OK ) { map->geometry.revision = revision + 1; }
    return status;
}

map_status_t MapMeshEdit_Triangulate( map_document_t *map, span_t<const u64> selected ) noexcept
{
    auto status = Check( map, selected, false );
    if ( status != map_status_t::OK ) { return status; }
    bool needsIds = false;
    for ( usize i = 0; !needsIds && i < selected.nCount; ++i ) {
        needsIds = NeedsTriangulation( geo::GeometryDocument_FindMesh( &map->geometry, { selected.pData[i] } ) );
    }
    const auto revision = map->geometry.revision;
    if ( needsIds ) {
        if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ||
             revision == CY_U64_MAX || selected.nCount > CY_U64_MAX - revision ) { return map_status_t::LIMIT_EXCEEDED; }
        // Map-only objects and retired reservations share this namespace.
        // Raise the registry before Begin captures its tentative cursor so
        // no new triangle can reuse an ID below the map's high-water mark.
        if ( map->nextId > 1 ) {
            status = Status( geo::GeometrySourceIdAllocator_AdvancePast( &map->geometry.sourceIds.allocator, { map->nextId - 1 } ) );
            if ( status != map_status_t::OK ) { return status; }
        }
    }
    for ( usize i = 0; status == map_status_t::OK && i < selected.nCount; ++i ) { status = TriangulateOne( map, selected.pData[i] ); }
    if ( status == map_status_t::OK && map->geometry.revision != revision ) {
        map->geometry.revision = revision + 1;
        const auto next = map->geometry.sourceIds.allocator.next.value;
        map->nextId = map->nextId == 0 || next == 0 ? 0 : std::max( map->nextId, next );
    }
    return status;
}

map_status_t MapMeshEdit_ExtrudeFace( map_document_t *map, u64 meshId, u64 faceId, f64 distance ) noexcept
{
    return ModelFace( map, meshId, faceId, face_model_operation_t::EXTRUDE, distance );
}

map_status_t MapMeshEdit_InsetFace( map_document_t *map, u64 meshId, u64 faceId, f64 margin ) noexcept
{
    return ModelFace( map, meshId, faceId, face_model_operation_t::INSET, margin );
}

map_status_t MapMeshEdit_QuadSliceFace( map_document_t *map, u64 meshId, u64 faceId, u32 cU, u32 cV ) noexcept
{
    return ModelFace( map, meshId, faceId, face_model_operation_t::QUAD_SLICE, 0.0, cU, cV );
}
} // namespace cypher::editor::map
