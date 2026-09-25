//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSerialization.cpp
//  Purpose: Implements deterministic CYKV persistence for brush geometry.
//  Details: Save builds a key-value tree from the document's brush source
//           data and writes it with deterministic formatting. Load parses
//           CYKV text, validates the header and every field, then
//           constructs a geometry document from the validated data.
//
//           The wire format stores planes as four separate f64 fields
//           (normal_x, normal_y, normal_z, d) so round-trip fidelity is
//           exact — no intermediate string-to-float conversion is needed
//           beyond what CYKV f64 already guarantees.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushSerialization.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_SurfaceSerialization.h"
#include "CypherGeometry_MeshSerialization.h"

#include "CypherCommon_Sort.h"

#include <limits>

namespace cypher::editor::geometry
{

namespace
{

using namespace cypher::common;

// Lifetime wrapper for a key-value document that destroys on scope exit.
struct kv_document_owner_t {
    key_value_document_t *pDocument{ nullptr };

    ~kv_document_owner_t() noexcept
    {
        KeyValue_DestroyDocument( pDocument );
    }
};

string_view_t SV( const char *pText ) noexcept
{
    return StringView_FromCString( pText );
}

void CopyField(
    geometry_serialization_result_t &result,
    const char *pField ) noexcept
{
    if ( pField == nullptr ) {
        return;
    }
    const string_view_t sv = SV( pField );
    usize cch = sv.cchLength;
    if ( cch >= sizeof( result.field ) ) {
        cch = sizeof( result.field ) - 1u;
    }
    for ( usize i = 0u; i < cch; ++i ) {
        result.field[i] = pField[i];
    }
    result.field[cch] = '\0';
}

geometry_serialization_result_t Failure(
    geometry_serialization_status_t status,
    const char *pField = nullptr,
    usize iElement = CY_INVALID_SIZE ) noexcept
{
    geometry_serialization_result_t result{};
    result.status = status;
    result.iElement = iElement;
    CopyField( result, pField );
    return result;
}

geometry_serialization_status_t MapGeometryStatus(
    geometry_status_t status ) noexcept
{
    switch ( status ) {
        case geometry_status_t::OK:
            return geometry_serialization_status_t::OK;
        case geometry_status_t::ALLOCATION_FAILED:
            return geometry_serialization_status_t::OUT_OF_MEMORY;
        case geometry_status_t::LIMIT_EXCEEDED:
        case geometry_status_t::INSUFFICIENT_CAPACITY:
            return geometry_serialization_status_t::LIMIT_EXCEEDED;
        case geometry_status_t::IDENTITY_CONFLICT:
            return geometry_serialization_status_t::DUPLICATE_SOURCE_ID;
        default:
            return geometry_serialization_status_t::DOCUMENT_INIT_FAILED;
    }
}

bool IsDefaultPolicy( const geometry_policy_t &policy ) noexcept
{
    const geometry_policy_t expected{};
    return policy.numerical.fCoordinateMagnitudeLimit ==
               expected.numerical.fCoordinateMagnitudeLimit &&
           policy.numerical.fAbsoluteDistanceTolerance ==
               expected.numerical.fAbsoluteDistanceTolerance &&
           policy.numerical.fRelativeDistanceTolerance ==
               expected.numerical.fRelativeDistanceTolerance &&
           policy.numerical.fMinimumEdgeLength ==
               expected.numerical.fMinimumEdgeLength &&
           policy.numerical.fMinimumFaceArea ==
               expected.numerical.fMinimumFaceArea &&
           policy.numerical.fAngularToleranceRadians ==
               expected.numerical.fAngularToleranceRadians &&
           policy.numerical.fPlanarityTolerance ==
               expected.numerical.fPlanarityTolerance &&
           policy.numerical.fCoplanarDistanceTolerance ==
               expected.numerical.fCoplanarDistanceTolerance &&
           policy.numerical.fUnitNormalTolerance ==
               expected.numerical.fUnitNormalTolerance &&
           policy.numerical.fSnapDistance ==
               expected.numerical.fSnapDistance &&
           policy.numerical.fWeldDistance ==
               expected.numerical.fWeldDistance &&
           policy.numerical.fCanonicalQuantization ==
               expected.numerical.fCanonicalQuantization &&
           policy.limits.cBrushesMax == expected.limits.cBrushesMax &&
           policy.limits.cBrushSidesMax == expected.limits.cBrushSidesMax &&
           policy.limits.cBrushSidesPerBrushMax ==
               expected.limits.cBrushSidesPerBrushMax &&
           policy.limits.cVerticesMax == expected.limits.cVerticesMax &&
           policy.limits.cHalfEdgesMax == expected.limits.cHalfEdgesMax &&
           policy.limits.cEdgesMax == expected.limits.cEdgesMax &&
           policy.limits.cLoopsMax == expected.limits.cLoopsMax &&
           policy.limits.cFacesMax == expected.limits.cFacesMax &&
           policy.limits.cShellsMax == expected.limits.cShellsMax &&
           policy.limits.cIntersectionEventsMax ==
               expected.limits.cIntersectionEventsMax &&
           policy.limits.cJournalRecordsMax ==
               expected.limits.cJournalRecordsMax &&
           policy.limits.cDiagnosticsMax ==
               expected.limits.cDiagnosticsMax &&
           policy.limits.cTraversalDepthMax ==
               expected.limits.cTraversalDepthMax &&
           policy.limits.cbScratchMax == expected.limits.cbScratchMax;
}

bool IsCanonicalDocumentDestination(
    const geometry_document_t &document ) noexcept
{
    const geometry_source_id_registry_t &registry = document.sourceIds;
    return document.brushes.pData == nullptr &&
           document.brushes.nCount == 0u &&
           document.brushes.nCapacity == 0u &&
           document.brushes.pAllocator == nullptr &&
           document.meshes.pData == nullptr &&
           document.meshes.nCount == 0u &&
           document.meshes.nCapacity == 0u &&
           document.meshes.pAllocator == nullptr &&
           registry.claimedIds.pSlots == nullptr &&
           registry.claimedIds.nCount == 0u &&
           registry.claimedIds.nCapacity == 0u &&
           registry.claimedIds.pAllocator == nullptr &&
           registry.liveIds.pSlots == nullptr &&
           registry.liveIds.nCount == 0u &&
           registry.liveIds.nCapacity == 0u &&
           registry.liveIds.pAllocator == nullptr &&
           registry.allocator.next.value == 1u &&
           registry.cEntriesMax == 0u &&
           registry.pAllocator == nullptr &&
           !registry.bLoadRegistrationOpen &&
           IsDefaultPolicy( document.policy ) &&
           document.revision == GEOMETRY_REVISION_INITIAL &&
           document.pAllocator == nullptr;
}

struct source_id_less_t {
    bool operator()(
        const geometry_source_id_t &left,
        const geometry_source_id_t &right ) const noexcept
    {
        return left.value < right.value;
    }
};

bool HasDuplicateSortedSourceIds(
    const vector_t<geometry_source_id_t> &ids ) noexcept
{
    for ( usize i = 1u; i < ids.nCount; ++i ) {
        if ( ids.pData[i - 1u].value == ids.pData[i].value ) {
            return true;
        }
    }
    return false;
}

geometry_serialization_result_t CollectAndValidateDocumentIds(
    const geometry_document_t *pDocument,
    const allocator_t *pScratchAllocator,
    vector_t<geometry_source_id_t> *pClaimedOut ) noexcept
{
    const bool bPolicyValid = GeometryPolicy_IsValid(
        pDocument->policy );
    const u64 cSourceEntriesMax = bPolicyValid
        ? GeometryDocument_SourceIdCapacity( pDocument->policy )
        : 0u;
    if ( !Vector_IsValid( &pDocument->brushes ) ||
         pDocument->brushes.pAllocator != pDocument->pAllocator ||
         !Vector_IsValid( &pDocument->meshes ) ||
         pDocument->meshes.pAllocator != pDocument->pAllocator ||
         !bPolicyValid ||
         !GeometrySourceIdRegistry_ValidateDeep( &pDocument->sourceIds ) ||
         pDocument->sourceIds.pAllocator != pDocument->pAllocator ||
         pDocument->sourceIds.bLoadRegistrationOpen ||
         cSourceEntriesMax > CY_USIZE_MAX ||
         pDocument->sourceIds.cEntriesMax !=
             static_cast<usize>( cSourceEntriesMax ) ) {
        return Failure(
            geometry_serialization_status_t::CORRUPT_DOCUMENT );
    }

    if ( pDocument->brushes.nCount >
         GEOMETRY_SERIALIZATION_MAX_BRUSHES ) {
        return Failure(
            geometry_serialization_status_t::LIMIT_EXCEEDED,
            "brushes" );
    }
    if ( static_cast<u64>( pDocument->brushes.nCount ) >
         pDocument->policy.limits.cBrushesMax ) {
        return Failure(
            geometry_serialization_status_t::CORRUPT_DOCUMENT,
            "brushes" );
    }

    const usize cClaimed =
        GeometrySourceIdRegistry_ClaimedCount( &pDocument->sourceIds );
    if ( cClaimed > GEOMETRY_SERIALIZATION_MAX_CLAIMED_SOURCE_IDS ) {
        return Failure(
            geometry_serialization_status_t::LIMIT_EXCEEDED,
            "claimed_source_ids" );
    }
    if ( !Vector_Init( pClaimedOut, pScratchAllocator, cClaimed ) ) {
        return Failure( geometry_serialization_status_t::OUT_OF_MEMORY );
    }

    usize iSlot = 0u;
    while ( const auto *pSlot = HashTable_NextOccupied(
                &pDocument->sourceIds.claimedIds, &iSlot ) ) {
        const geometry_source_id_t *pId = HashTable_SlotKey( pSlot );
        if ( pId == nullptr || !GeometrySourceId_IsValid( *pId ) ) {
            return Failure(
                geometry_serialization_status_t::CORRUPT_DOCUMENT,
                "claimed_source_ids" );
        }
        if ( !Vector_PushBack( pClaimedOut, *pId ) ) {
            return Failure( geometry_serialization_status_t::OUT_OF_MEMORY );
        }
    }
    if ( pClaimedOut->nCount != cClaimed ) {
        return Failure(
            geometry_serialization_status_t::CORRUPT_DOCUMENT,
            "claimed_source_ids" );
    }

    Sort_Unstable( Vector_Span( pClaimedOut ), source_id_less_t{} );
    if ( HasDuplicateSortedSourceIds( *pClaimedOut ) ) {
        return Failure(
            geometry_serialization_status_t::CORRUPT_DOCUMENT,
            "claimed_source_ids" );
    }

    vector_t<geometry_source_id_t> geometryIds{};
    const usize cLive =
        GeometrySourceIdRegistry_Count( &pDocument->sourceIds );
    if ( !Vector_Init( &geometryIds, pScratchAllocator, cLive ) ) {
        return Failure( geometry_serialization_status_t::OUT_OF_MEMORY );
    }

    usize cGeometryIds = 0u;
    usize cSidesTotal = 0u;
    for ( usize iBrush = 0u;
          iBrush < pDocument->brushes.nCount;
          ++iBrush ) {
        const brush_solid_t *pBrush = pDocument->brushes.pData[iBrush];
        if ( pBrush == nullptr ||
             !GeometrySourceId_IsValid( pBrush->sourceId ) ||
             !Vector_IsValid( &pBrush->sides ) ||
             pBrush->sides.pAllocator != pDocument->pAllocator ) {
            return Failure(
                geometry_serialization_status_t::CORRUPT_DOCUMENT,
                "brushes[]", iBrush );
        }

        const usize cSides = pBrush->sides.nCount;
        if ( cSides > GEOMETRY_SERIALIZATION_MAX_SIDES_PER_BRUSH ) {
            return Failure(
                geometry_serialization_status_t::LIMIT_EXCEEDED,
                "brushes[].sides", iBrush );
        }
        if ( static_cast<u64>( cSides ) >
                 pDocument->policy.limits.cBrushSidesPerBrushMax ||
             cSides == CY_USIZE_MAX ||
             cGeometryIds > CY_USIZE_MAX - 1u - cSides ||
             cSidesTotal > CY_USIZE_MAX - cSides ) {
            return Failure(
                geometry_serialization_status_t::CORRUPT_DOCUMENT,
                "brushes[].sides", iBrush );
        }
        cGeometryIds += cSides + 1u;
        cSidesTotal += cSides;

        if ( !GeometrySourceIdRegistry_Contains(
                 &pDocument->sourceIds, pBrush->sourceId ) ) {
            return Failure(
                geometry_serialization_status_t::CORRUPT_DOCUMENT,
                "brushes[].source_id", iBrush );
        }
        if ( !Vector_PushBack( &geometryIds, pBrush->sourceId ) ) {
            return Failure( geometry_serialization_status_t::OUT_OF_MEMORY );
        }

        for ( usize iSide = 0u; iSide < cSides; ++iSide ) {
            const brush_solid_side_t &side = pBrush->sides.pData[iSide];
            if ( !GeometrySourceId_IsValid( side.sourceId ) ||
                 !GeometrySourceIdRegistry_Contains(
                     &pDocument->sourceIds, side.sourceId ) ) {
                return Failure(
                    geometry_serialization_status_t::CORRUPT_DOCUMENT,
                    "brushes[].sides[].source_id", iSide );
            }
            if ( !math::Planed_IsFinite( side.plane ) ) {
                return Failure(
                    geometry_serialization_status_t::CORRUPT_DOCUMENT,
                    "brushes[].sides[].plane", iSide );
            }
            if ( !math::Planed_IsNormalized(
                     side.plane,
                     pDocument->policy.numerical.fUnitNormalTolerance ) ) {
                return Failure(
                    geometry_serialization_status_t::CORRUPT_DOCUMENT,
                    "brushes[].sides[].plane", iSide );
            }
            if ( !Vector_PushBack( &geometryIds, side.sourceId ) ) {
                return Failure(
                    geometry_serialization_status_t::OUT_OF_MEMORY );
            }
        }
    }

    u64 cMeshVertices = 0u;
    u64 cMeshHalfEdges = 0u;
    u64 cMeshEdges = 0u;
    u64 cMeshLoops = 0u;
    u64 cMeshFaces = 0u;
    u64 cMeshShells = 0u;

    // Mesh identities are live document IDs too. Accumulate the topology
    // counts from the exact canonical sources being serialized so a corrupt
    // over-policy document cannot produce a file that its own policy refuses
    // to load.
    for ( usize iMesh = 0u; iMesh < pDocument->meshes.nCount; ++iMesh ) {
        const mesh_source_t *pMesh = pDocument->meshes.pData[iMesh];
        if ( pMesh == nullptr ) {
            return Failure(
                geometry_serialization_status_t::CORRUPT_DOCUMENT,
                "meshes[]", iMesh );
        }
        const mesh_source_validation_t validation =
            MeshSource_Validate( pMesh, pScratchAllocator );
        if ( validation.fault != mesh_source_fault_t::NONE ) {
            return Failure(
                validation.fault == mesh_source_fault_t::VALIDATION_INCOMPLETE
                    ? geometry_serialization_status_t::OUT_OF_MEMORY
                    : geometry_serialization_status_t::CORRUPT_DOCUMENT,
                "meshes[]", iMesh );
        }

        const u64 cVertices = static_cast<u64>(
            GenerationPool_Count( &pMesh->mesh.vertices ) );
        const u64 cHalfEdges = static_cast<u64>(
            GenerationPool_Count( &pMesh->mesh.halfEdges ) );
        const u64 cEdges = static_cast<u64>(
            GenerationPool_Count( &pMesh->mesh.edges ) );
        const u64 cLoops = static_cast<u64>(
            GenerationPool_Count( &pMesh->mesh.loops ) );
        const u64 cFaces = static_cast<u64>(
            GenerationPool_Count( &pMesh->mesh.faces ) );
        const u64 cShells = static_cast<u64>(
            GenerationPool_Count( &pMesh->mesh.shells ) );
        constexpr u64 C_U64_MAX = ~static_cast<u64>( 0u );
        if ( cMeshVertices > C_U64_MAX - cVertices ||
             cMeshHalfEdges > C_U64_MAX - cHalfEdges ||
             cMeshEdges > C_U64_MAX - cEdges ||
             cMeshLoops > C_U64_MAX - cLoops ||
             cMeshFaces > C_U64_MAX - cFaces ||
             cMeshShells > C_U64_MAX - cShells ) {
            return Failure(
                geometry_serialization_status_t::CORRUPT_DOCUMENT,
                "meshes[]", iMesh );
        }
        cMeshVertices += cVertices;
        cMeshHalfEdges += cHalfEdges;
        cMeshEdges += cEdges;
        cMeshLoops += cLoops;
        cMeshFaces += cFaces;
        cMeshShells += cShells;

        const usize cBefore = geometryIds.nCount;
        if ( MeshSource_TryCollectSourceIds( pMesh, &geometryIds ) != geometry_status_t::OK ) {
            return Failure(
                geometry_serialization_status_t::CORRUPT_DOCUMENT,
                "meshes[]", iMesh );
        }
        for ( usize k = cBefore; k < geometryIds.nCount; ++k ) {
            if ( !GeometrySourceIdRegistry_Contains(
                     &pDocument->sourceIds, geometryIds.pData[k] ) ) {
                return Failure(
                    geometry_serialization_status_t::CORRUPT_DOCUMENT,
                    "meshes[].source_ids", iMesh );
            }
        }
        const usize cMeshIds = geometryIds.nCount - cBefore;
        if ( cGeometryIds > CY_USIZE_MAX - cMeshIds ) {
            return Failure(
                geometry_serialization_status_t::CORRUPT_DOCUMENT,
                "meshes[].source_ids", iMesh );
        }
        cGeometryIds += cMeshIds;
    }

    // Patch and heightfield identities (schema 4) are live document IDs too.
    {
        const usize cBefore = geometryIds.nCount;
        const geometry_serialization_result_t surfaceResult =
            SurfaceSerialization_TryCollectIds( pDocument, pScratchAllocator, &geometryIds );
        if ( surfaceResult.status != geometry_serialization_status_t::OK ) {
            return surfaceResult;
        }
        cGeometryIds += geometryIds.nCount - cBefore;
    }

    if ( static_cast<u64>( cSidesTotal ) >
             pDocument->policy.limits.cBrushSidesMax ||
         cMeshVertices > pDocument->policy.limits.cVerticesMax ||
         cMeshHalfEdges > pDocument->policy.limits.cHalfEdgesMax ||
         cMeshEdges > pDocument->policy.limits.cEdgesMax ||
         cMeshLoops > pDocument->policy.limits.cLoopsMax ||
         cMeshFaces > pDocument->policy.limits.cFacesMax ||
         cMeshShells > pDocument->policy.limits.cShellsMax ||
         GeometrySourceIdRegistry_Count( &pDocument->sourceIds ) !=
             cGeometryIds ) {
        return Failure(
            geometry_serialization_status_t::CORRUPT_DOCUMENT );
    }

    Sort_Unstable( Vector_Span( &geometryIds ), source_id_less_t{} );
    if ( HasDuplicateSortedSourceIds( geometryIds ) ) {
        return Failure(
            geometry_serialization_status_t::CORRUPT_DOCUMENT,
            "brushes[].source_id" );
    }

    return {};
}

geometry_serialization_result_t ValidateBrushContainerBudgets(
    const key_value_t *pBrushes,
    const geometry_policy_t &policy ) noexcept
{
    const usize cBrushes = KeyValue_ChildCount( pBrushes );
    if ( cBrushes > GEOMETRY_SERIALIZATION_MAX_BRUSHES ||
         static_cast<u64>( cBrushes ) > policy.limits.cBrushesMax ) {
        return Failure(
            geometry_serialization_status_t::LIMIT_EXCEEDED,
            "brushes" );
    }

    usize cSidesTotal = 0u;
    for ( usize iBrush = 0u; iBrush < cBrushes; ++iBrush ) {
        const key_value_t *pBrush = KeyValue_ChildAt(
            pBrushes, iBrush );
        if ( pBrush == nullptr ||
             KeyValue_Type( pBrush ) != key_value_type_t::OBJECT ) {
            return Failure(
                geometry_serialization_status_t::TYPE_MISMATCH,
                "brushes[]", iBrush );
        }

        const key_value_t *pSides = KeyValue_Find(
            pBrush, SV( "sides" ) );
        if ( pSides == nullptr ) {
            return Failure(
                geometry_serialization_status_t::MISSING_FIELD,
                "brushes[].sides", iBrush );
        }
        if ( KeyValue_Type( pSides ) != key_value_type_t::ARRAY ) {
            return Failure(
                geometry_serialization_status_t::TYPE_MISMATCH,
                "brushes[].sides", iBrush );
        }

        const usize cSides = KeyValue_ChildCount( pSides );
        if ( cSides > GEOMETRY_SERIALIZATION_MAX_SIDES_PER_BRUSH ||
             static_cast<u64>( cSides ) >
                 policy.limits.cBrushSidesPerBrushMax ||
             cSidesTotal > CY_USIZE_MAX - cSides ) {
            return Failure(
                geometry_serialization_status_t::LIMIT_EXCEEDED,
                "brushes[].sides", iBrush );
        }
        cSidesTotal += cSides;
        if ( static_cast<u64>( cSidesTotal ) >
             policy.limits.cBrushSidesMax ) {
            return Failure(
                geometry_serialization_status_t::LIMIT_EXCEEDED,
                "brushes[].sides", iBrush );
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Save helpers — insert typed values into the key-value tree
// ---------------------------------------------------------------------------

key_value_t *InsertArray(
    key_value_document_t *pDoc,
    key_value_t *pParent,
    const char *pName ) noexcept
{
    return KeyValue_ObjectInsert(
        pDoc, pParent, SV( pName ), key_value_type_t::ARRAY );
}

bool_t InsertU64(
    key_value_document_t *pDoc,
    key_value_t *pParent,
    const char *pName,
    u64 value ) noexcept
{
    key_value_t *pNode = KeyValue_ObjectInsert(
        pDoc, pParent, SV( pName ), key_value_type_t::U64 );
    if ( pNode == nullptr ) {
        return CY_FALSE;
    }
    return KeyValue_SetU64( pDoc, pNode, value );
}

bool_t InsertF64(
    key_value_document_t *pDoc,
    key_value_t *pParent,
    const char *pName,
    f64 value ) noexcept
{
    key_value_t *pNode = KeyValue_ObjectInsert(
        pDoc, pParent, SV( pName ), key_value_type_t::F64 );
    if ( pNode == nullptr ) {
        return CY_FALSE;
    }
    return KeyValue_SetF64( pDoc, pNode, value );
}

bool_t AppendU64(
    key_value_document_t *pDoc,
    key_value_t *pArray,
    u64 value ) noexcept
{
    key_value_t *pNode = KeyValue_ArrayAppend(
        pDoc, pArray, key_value_type_t::U64 );
    return pNode != nullptr && KeyValue_SetU64( pDoc, pNode, value );
}

// Writes one brush_solid_side_t as an object element appended to an array.
bool_t WriteSide(
    key_value_document_t *pDoc,
    key_value_t *pSidesArray,
    const brush_solid_side_t &side ) noexcept
{
    key_value_t *pSide = KeyValue_ArrayAppend(
        pDoc, pSidesArray, key_value_type_t::OBJECT );
    if ( pSide == nullptr ) {
        return CY_FALSE;
    }

    if ( !InsertU64( pDoc, pSide, "source_id", side.sourceId.value ) ||
         !InsertF64( pDoc, pSide, "normal_x", side.plane.normal.x ) ||
         !InsertF64( pDoc, pSide, "normal_y", side.plane.normal.y ) ||
         !InsertF64( pDoc, pSide, "normal_z", side.plane.normal.z ) ||
         !InsertF64( pDoc, pSide, "d", side.plane.d ) ||
         !InsertU64( pDoc, pSide, "attribute_index",
                     side.iAttributeIndex ) ) {
        return CY_FALSE;
    }

    return CY_TRUE;
}

bool_t AppendF64(
    key_value_document_t *pDoc,
    key_value_t *pArray,
    f64 value ) noexcept
{
    key_value_t *pNode = KeyValue_ArrayAppend(
        pDoc, pArray, key_value_type_t::F64 );
    return pNode != nullptr && KeyValue_SetF64( pDoc, pNode, value );
}

bool_t InsertF64Array(
    key_value_document_t *pDoc,
    key_value_t *pParent,
    const char *pName,
    const f64 *pValues,
    usize cValues ) noexcept
{
    key_value_t *pArray = InsertArray( pDoc, pParent, pName );
    if ( pArray == nullptr ) {
        return CY_FALSE;
    }
    for ( usize i = 0u; i < cValues; ++i ) {
        if ( !AppendF64( pDoc, pArray, pValues[i] ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

// Writes one side record (schema 4) as an object appended to an array.
bool_t WriteSurface(
    key_value_document_t *pDoc,
    key_value_t *pSurfaces,
    const geometry_brush_side_attributes_t &record ) noexcept
{
    key_value_t *pObj = KeyValue_ArrayAppend( pDoc, pSurfaces, key_value_type_t::OBJECT );
    if ( pObj == nullptr ) {
        return CY_FALSE;
    }
    const math::planar_uv_mappingd_t &m = record.uvProjection;
    const f64 origin[3] = { m.origin.x, m.origin.y, m.origin.z };
    const f64 uAxis[3] = { m.uAxis.x, m.uAxis.y, m.uAxis.z };
    const f64 vAxis[3] = { m.vAxis.x, m.vAxis.y, m.vAxis.z };
    const f64 normal[3] = { m.normal.x, m.normal.y, m.normal.z };
    const f64 scale[2] = { m.worldUnitsPerUv.x, m.worldUnitsPerUv.y };
    const f64 offset[2] = { m.offset.x, m.offset.y };
    return InsertU64( pDoc, pObj, "material", record.material.value ) &&
           InsertF64Array( pDoc, pObj, "uv_origin", origin, 3u ) &&
           InsertF64Array( pDoc, pObj, "uv_u_axis", uAxis, 3u ) &&
           InsertF64Array( pDoc, pObj, "uv_v_axis", vAxis, 3u ) &&
           InsertF64Array( pDoc, pObj, "uv_normal", normal, 3u ) &&
           InsertF64Array( pDoc, pObj, "uv_world_units_per_uv", scale, 2u ) &&
           InsertF64( pDoc, pObj, "uv_rotation", m.rotationRadians ) &&
           InsertF64Array( pDoc, pObj, "uv_offset", offset, 2u );
}

// Writes one brush_solid_t as an object element appended to an array.
bool_t WriteBrush(
    key_value_document_t *pDoc,
    key_value_t *pBrushesArray,
    const brush_solid_t *pBrush,
    const geometry_brush_side_attribute_store_t *pSurfaces ) noexcept
{
    key_value_t *pBrushObj = KeyValue_ArrayAppend(
        pDoc, pBrushesArray, key_value_type_t::OBJECT );
    if ( pBrushObj == nullptr ) {
        return CY_FALSE;
    }

    if ( !InsertU64( pDoc, pBrushObj, "source_id",
                     pBrush->sourceId.value ) ) {
        return CY_FALSE;
    }

    key_value_t *pSides = InsertArray( pDoc, pBrushObj, "sides" );
    if ( pSides == nullptr ) {
        return CY_FALSE;
    }

    const usize cSides = BrushSolid_SideCount( pBrush );
    for ( usize i = 0u; i < cSides; ++i ) {
        brush_solid_side_t side{};
        if ( BrushSolid_TryGetSide( pBrush, i, &side ) !=
             geometry_status_t::OK ) {
            return CY_FALSE;
        }
        if ( !WriteSide( pDoc, pSides, side ) ) {
            return CY_FALSE;
        }
    }

    // Side records (schema 4), in record order so attribute_index keeps
    // addressing the same record after a round trip.
    key_value_t *pSurfaceArray = InsertArray( pDoc, pBrushObj, "surfaces" );
    if ( pSurfaceArray == nullptr ) {
        return CY_FALSE;
    }
    const usize cRecords = pSurfaces != nullptr ? BrushSideAttributeStore_Count( pSurfaces ) : 0u;
    for ( usize i = 0u; i < cRecords; ++i ) {
        geometry_brush_side_attributes_t record{};
        if ( BrushSideAttributeStore_TryGet( pSurfaces, i, &record ) != geometry_status_t::OK ||
             !WriteSurface( pDoc, pSurfaceArray, record ) ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

// Reads a fixed-length f64 array field.
bool ReadF64Array(
    const key_value_t *pParent,
    const char *pName,
    f64 *pOut,
    usize cValues ) noexcept
{
    const key_value_t *pArray = KeyValue_Find( pParent, SV( pName ) );
    if ( pArray == nullptr || KeyValue_Type( pArray ) != key_value_type_t::ARRAY ||
         KeyValue_ChildCount( pArray ) != cValues ) {
        return false;
    }
    for ( usize i = 0u; i < cValues; ++i ) {
        const key_value_t *pNode = KeyValue_ChildAt( pArray, i );
        if ( pNode == nullptr || KeyValue_Type( pNode ) != key_value_type_t::F64 ||
             !KeyValue_GetF64( pNode, &pOut[i] ) ) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Load helpers — read typed values from the key-value tree
// ---------------------------------------------------------------------------

geometry_serialization_status_t ReadRequiredU64(
    const key_value_t *pParent,
    const char *pName,
    u64 *pValueOut ) noexcept
{
    const key_value_t *pNode = KeyValue_Find( pParent, SV( pName ) );
    if ( pNode == nullptr ) {
        return geometry_serialization_status_t::MISSING_FIELD;
    }
    return KeyValue_Type( pNode ) == key_value_type_t::U64 &&
           KeyValue_GetU64( pNode, pValueOut )
        ? geometry_serialization_status_t::OK
        : geometry_serialization_status_t::TYPE_MISMATCH;
}

geometry_serialization_status_t ReadRequiredF64(
    const key_value_t *pParent,
    const char *pName,
    f64 *pValueOut ) noexcept
{
    const key_value_t *pNode = KeyValue_Find( pParent, SV( pName ) );
    if ( pNode == nullptr ) {
        return geometry_serialization_status_t::MISSING_FIELD;
    }
    return KeyValue_Type( pNode ) == key_value_type_t::F64 &&
           KeyValue_GetF64( pNode, pValueOut )
        ? geometry_serialization_status_t::OK
        : geometry_serialization_status_t::TYPE_MISMATCH;
}

// Reads one side object and fills a brush_solid_side_t. Returns the
// appropriate failure status on missing or out-of-range fields.
geometry_serialization_result_t ReadSide(
    const key_value_t *pSideObj,
    brush_solid_side_t *pSideOut,
    usize iSide,
    const geometry_policy_t &policy ) noexcept
{
    if ( KeyValue_Type( pSideObj ) != key_value_type_t::OBJECT ) {
        return Failure(
            geometry_serialization_status_t::TYPE_MISMATCH,
            "brushes[].sides[]", iSide );
    }

    u64 sourceIdVal = 0u;
    geometry_serialization_status_t readStatus =
        ReadRequiredU64( pSideObj, "source_id", &sourceIdVal );
    if ( readStatus != geometry_serialization_status_t::OK ) {
        return Failure(
            readStatus,
            "brushes[].sides[].source_id", iSide );
    }
    if ( sourceIdVal == 0u ) {
        return Failure(
            geometry_serialization_status_t::INVALID_SOURCE_ID,
            "brushes[].sides[].source_id", iSide );
    }

    f64 nx = 0.0, ny = 0.0, nz = 0.0, d = 0.0;
    struct plane_field_t {
        const char *pName;
        f64 *pValue;
    };
    const plane_field_t planeFields[]{
        { "normal_x", &nx },
        { "normal_y", &ny },
        { "normal_z", &nz },
        { "d", &d }
    };
    for ( const plane_field_t &field : planeFields ) {
        readStatus = ReadRequiredF64(
            pSideObj, field.pName, field.pValue );
        if ( readStatus != geometry_serialization_status_t::OK ) {
            return Failure(
                readStatus,
                "brushes[].sides[].plane", iSide );
        }
    }

    const math::planed_t plane = math::Planed_Make(
        math::Vec3d_Make( nx, ny, nz ), d );

    if ( !math::Planed_IsFinite( plane ) ) {
        return Failure(
            geometry_serialization_status_t::VALUE_OUT_OF_RANGE,
            "brushes[].sides[].plane", iSide );
    }
    if ( !math::Planed_IsNormalized(
             plane, policy.numerical.fUnitNormalTolerance ) ) {
        return Failure(
            geometry_serialization_status_t::PLANE_NOT_NORMALIZED,
            "brushes[].sides[].plane", iSide );
    }

    u64 attrIndex = 0u;
    readStatus = ReadRequiredU64(
        pSideObj, "attribute_index", &attrIndex );
    if ( readStatus != geometry_serialization_status_t::OK ) {
        return Failure(
            readStatus,
            "brushes[].sides[].attribute_index", iSide );
    }
    if ( attrIndex > static_cast<u64>( CY_U32_MAX ) ) {
        return Failure(
            geometry_serialization_status_t::VALUE_OUT_OF_RANGE,
            "brushes[].sides[].attribute_index", iSide );
    }

    pSideOut->plane = plane;
    pSideOut->sourceId.value = sourceIdVal;
    pSideOut->iAttributeIndex = static_cast<u32>( attrIndex );

    return {};
}

// Reads one brush object from the key-value tree and constructs a
// brush_solid_t. On failure, shuts down any partially built brush.
geometry_serialization_result_t ReadBrush(
    const key_value_t *pBrushObj,
    brush_solid_t *pBrushOut,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    usize iBrush ) noexcept
{
    if ( KeyValue_Type( pBrushObj ) != key_value_type_t::OBJECT ) {
        return Failure(
            geometry_serialization_status_t::TYPE_MISMATCH,
            "brushes[]", iBrush );
    }

    u64 brushIdVal = 0u;
    const geometry_serialization_status_t sourceReadStatus =
        ReadRequiredU64( pBrushObj, "source_id", &brushIdVal );
    if ( sourceReadStatus != geometry_serialization_status_t::OK ) {
        return Failure(
            sourceReadStatus,
            "brushes[].source_id", iBrush );
    }
    if ( brushIdVal == 0u ) {
        return Failure(
            geometry_serialization_status_t::INVALID_SOURCE_ID,
            "brushes[].source_id", iBrush );
    }

    const key_value_t *pSides = KeyValue_Find( pBrushObj, SV( "sides" ) );
    if ( pSides == nullptr ) {
        return Failure(
            geometry_serialization_status_t::MISSING_FIELD,
            "brushes[].sides", iBrush );
    }
    if ( KeyValue_Type( pSides ) != key_value_type_t::ARRAY ) {
        return Failure(
            geometry_serialization_status_t::TYPE_MISMATCH,
            "brushes[].sides", iBrush );
    }

    const usize cSides = KeyValue_ChildCount( pSides );
    if ( cSides > GEOMETRY_SERIALIZATION_MAX_SIDES_PER_BRUSH ) {
        return Failure(
            geometry_serialization_status_t::LIMIT_EXCEEDED,
            "brushes[].sides", iBrush );
    }

    geometry_source_id_t brushId{};
    brushId.value = brushIdVal;

    geometry_status_t status = BrushSolid_Init(
        pBrushOut, pAllocator, brushId );
    if ( status != geometry_status_t::OK ) {
        return Failure(
            geometry_serialization_status_t::DOCUMENT_INIT_FAILED,
            "brushes[]", iBrush );
    }

    status = BrushSolid_TryReserve( pBrushOut, policy.limits, cSides );
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( pBrushOut );
        return Failure(
            MapGeometryStatus( status ),
            "brushes[].sides", iBrush );
    }

    for ( usize i = 0u; i < cSides; ++i ) {
        const key_value_t *pSideObj = KeyValue_ChildAt( pSides, i );

        brush_solid_side_t side{};
        const geometry_serialization_result_t sideResult =
            ReadSide( pSideObj, &side, i, policy );
        if ( sideResult.status !=
             geometry_serialization_status_t::OK ) {
            BrushSolid_Shutdown( pBrushOut );
            return sideResult;
        }

        status = BrushSolid_TryAddSide(
            pBrushOut, policy.limits, side, nullptr );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( pBrushOut );
            return Failure(
                MapGeometryStatus( status ),
                "brushes[].sides[]", i );
        }
    }

    return {};
}

// Reads a brush's side records (schema 4) into *pStoreOut (initialized
// here). Records are validated as they enter the store.
geometry_serialization_result_t ReadSurfaces(
    const key_value_t *pBrushObj,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    usize iBrush,
    geometry_brush_side_attribute_store_t *pStoreOut ) noexcept
{
    const key_value_t *pSurfaces = KeyValue_Find( pBrushObj, SV( "surfaces" ) );
    if ( pSurfaces == nullptr ) {
        return Failure( geometry_serialization_status_t::MISSING_FIELD, "brushes[].surfaces", iBrush );
    }
    if ( KeyValue_Type( pSurfaces ) != key_value_type_t::ARRAY ) {
        return Failure( geometry_serialization_status_t::TYPE_MISMATCH, "brushes[].surfaces", iBrush );
    }
    const usize cRecords = KeyValue_ChildCount( pSurfaces );
    if ( cRecords > GEOMETRY_SERIALIZATION_MAX_SIDES_PER_BRUSH ) {
        return Failure( geometry_serialization_status_t::LIMIT_EXCEEDED, "brushes[].surfaces", iBrush );
    }
    if ( BrushSideAttributeStore_Init( pStoreOut, pAllocator ) != geometry_status_t::OK ||
         BrushSideAttributeStore_TryReserve( pStoreOut, policy.limits, cRecords ) != geometry_status_t::OK ) {
        BrushSideAttributeStore_Shutdown( pStoreOut );
        return Failure( geometry_serialization_status_t::OUT_OF_MEMORY, "brushes[].surfaces", iBrush );
    }
    for ( usize i = 0u; i < cRecords; ++i ) {
        const key_value_t *pObj = KeyValue_ChildAt( pSurfaces, i );
        geometry_brush_side_attributes_t record{};
        u64 material = 0u;
        f64 origin[3], uAxis[3], vAxis[3], normal[3], scale[2], offset[2], rotation = 0.0;
        const bool bShape = pObj != nullptr && KeyValue_Type( pObj ) == key_value_type_t::OBJECT &&
                            ReadRequiredU64( pObj, "material", &material ) == geometry_serialization_status_t::OK &&
                            ReadF64Array( pObj, "uv_origin", origin, 3u ) && ReadF64Array( pObj, "uv_u_axis", uAxis, 3u ) &&
                            ReadF64Array( pObj, "uv_v_axis", vAxis, 3u ) && ReadF64Array( pObj, "uv_normal", normal, 3u ) &&
                            ReadF64Array( pObj, "uv_world_units_per_uv", scale, 2u ) &&
                            ReadRequiredF64( pObj, "uv_rotation", &rotation ) == geometry_serialization_status_t::OK &&
                            ReadF64Array( pObj, "uv_offset", offset, 2u );
        if ( !bShape ) {
            BrushSideAttributeStore_Shutdown( pStoreOut );
            return Failure( geometry_serialization_status_t::TYPE_MISMATCH, "brushes[].surfaces[]", i );
        }
        record.material.value = material;
        math::planar_uv_mappingd_t &m = record.uvProjection;
        m.origin = math::Vec3d_Make( origin[0], origin[1], origin[2] );
        m.uAxis = math::Vec3d_Make( uAxis[0], uAxis[1], uAxis[2] );
        m.vAxis = math::Vec3d_Make( vAxis[0], vAxis[1], vAxis[2] );
        m.normal = math::Vec3d_Make( normal[0], normal[1], normal[2] );
        m.worldUnitsPerUv = math::vec2d_t{ scale[0], scale[1] };
        m.rotationRadians = rotation;
        m.offset = math::vec2d_t{ offset[0], offset[1] };
        if ( BrushSideAttributeStore_TryAppend( pStoreOut, policy, record, nullptr ) != geometry_status_t::OK ) {
            BrushSideAttributeStore_Shutdown( pStoreOut );
            return Failure( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "brushes[].surfaces[]", i );
        }
    }
    return {};
}

geometry_serialization_result_t ReadVersion2IdentityState(
    const key_value_t *pRoot,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_t *pNextSourceIdOut,
    vector_t<geometry_source_id_t> *pClaimedIdsOut ) noexcept
{
    u64 nextSourceIdValue = 0u;
    const geometry_serialization_status_t nextReadStatus =
        ReadRequiredU64(
            pRoot, "next_source_id", &nextSourceIdValue );
    if ( nextReadStatus != geometry_serialization_status_t::OK ) {
        return Failure( nextReadStatus, "next_source_id" );
    }

    const key_value_t *pClaimedIds =
        KeyValue_Find( pRoot, SV( "claimed_source_ids" ) );
    if ( pClaimedIds == nullptr ) {
        return Failure(
            geometry_serialization_status_t::MISSING_FIELD,
            "claimed_source_ids" );
    }
    if ( KeyValue_Type( pClaimedIds ) != key_value_type_t::ARRAY ) {
        return Failure(
            geometry_serialization_status_t::TYPE_MISMATCH,
            "claimed_source_ids" );
    }

    const usize cClaimedIds = KeyValue_ChildCount( pClaimedIds );
    const u64 cDocumentIdsMax = GeometryDocument_SourceIdCapacity( policy );
    if ( cClaimedIds > GEOMETRY_SERIALIZATION_MAX_CLAIMED_SOURCE_IDS ||
         static_cast<u64>( cClaimedIds ) > cDocumentIdsMax ) {
        return Failure(
            geometry_serialization_status_t::LIMIT_EXCEEDED,
            "claimed_source_ids" );
    }
    if ( !Vector_Init(
             pClaimedIdsOut, pAllocator, cClaimedIds ) ) {
        return Failure( geometry_serialization_status_t::OUT_OF_MEMORY );
    }

    for ( usize i = 0u; i < cClaimedIds; ++i ) {
        const key_value_t *pIdNode =
            KeyValue_ChildAt( pClaimedIds, i );
        u64 value = 0u;
        if ( pIdNode == nullptr ||
             KeyValue_Type( pIdNode ) != key_value_type_t::U64 ||
             !KeyValue_GetU64( pIdNode, &value ) ) {
            return Failure(
                geometry_serialization_status_t::TYPE_MISMATCH,
                "claimed_source_ids", i );
        }
        if ( value == 0u ) {
            return Failure(
                geometry_serialization_status_t::INVALID_SOURCE_ID,
                "claimed_source_ids", i );
        }
        if ( nextSourceIdValue != 0u && value >= nextSourceIdValue ) {
            return Failure(
                geometry_serialization_status_t::VALUE_OUT_OF_RANGE,
                "claimed_source_ids", i );
        }
        if ( !Vector_PushBack(
                 pClaimedIdsOut, geometry_source_id_t{ value } ) ) {
            return Failure( geometry_serialization_status_t::OUT_OF_MEMORY );
        }
    }

    Sort_Unstable(
        Vector_Span( pClaimedIdsOut ), source_id_less_t{} );
    if ( HasDuplicateSortedSourceIds( *pClaimedIdsOut ) ) {
        return Failure(
            geometry_serialization_status_t::DUPLICATE_SOURCE_ID,
            "claimed_source_ids" );
    }

    pNextSourceIdOut->value = nextSourceIdValue;
    return {};
}

geometry_serialization_result_t PrepareVersion2IdentityRegistry(
    geometry_document_t *pDocument,
    geometry_source_id_t nextSourceId,
    const vector_t<geometry_source_id_t> &claimedIds ) noexcept
{
    const usize cEntriesMax = pDocument->sourceIds.cEntriesMax;
    GeometrySourceIdRegistry_Shutdown( &pDocument->sourceIds );

    geometry_status_t status = GeometrySourceIdRegistry_Init(
        &pDocument->sourceIds,
        pDocument->pAllocator,
        cEntriesMax,
        claimedIds.nCount,
        nextSourceId );
    if ( status != geometry_status_t::OK ) {
        return Failure( MapGeometryStatus( status ) );
    }

    for ( usize i = 0u; i < claimedIds.nCount; ++i ) {
        status = GeometrySourceIdRegistry_Register(
            &pDocument->sourceIds, claimedIds.pData[i] );
        if ( status != geometry_status_t::OK ) {
            return Failure(
                MapGeometryStatus( status ),
                "claimed_source_ids", i );
        }
    }
    for ( usize i = 0u; i < claimedIds.nCount; ++i ) {
        status = GeometrySourceIdRegistry_Release(
            &pDocument->sourceIds, claimedIds.pData[i] );
        if ( status != geometry_status_t::OK ) {
            return Failure(
                geometry_serialization_status_t::DOCUMENT_INIT_FAILED,
                "claimed_source_ids", i );
        }
    }
    status = GeometrySourceIdRegistry_SealLoadedIds(
        &pDocument->sourceIds );
    if ( status != geometry_status_t::OK ||
         !GeometrySourceIdRegistry_ValidateDeep(
             &pDocument->sourceIds ) ) {
        return Failure(
            geometry_serialization_status_t::DOCUMENT_INIT_FAILED,
            "claimed_source_ids" );
    }
    return {};
}

bool BrushIdsAreClaimed(
    const brush_solid_t &brush,
    const geometry_source_id_registry_t &registry ) noexcept
{
    if ( !HashSet_Contains( &registry.claimedIds, brush.sourceId ) ) {
        return false;
    }
    for ( usize i = 0u; i < brush.sides.nCount; ++i ) {
        if ( !HashSet_Contains(
                 &registry.claimedIds,
                 brush.sides.pData[i].sourceId ) ) {
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

geometry_serialization_result_t GeometrySerialization_SaveToText(
    const geometry_document_t *pDocument,
    text_buffer_t *pTextOut ) noexcept
{
    if ( pDocument == nullptr || pTextOut == nullptr ||
         !TextBuffer_IsValid( pTextOut ) ||
         !Allocator_IsValid( pTextOut->pAllocator ) ) {
        return Failure(
            geometry_serialization_status_t::INVALID_ARGUMENT );
    }
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return Failure(
            geometry_serialization_status_t::NOT_INITIALIZED );
    }

    vector_t<geometry_source_id_t> claimedIds{};
    const geometry_serialization_result_t validationResult =
        CollectAndValidateDocumentIds(
            pDocument, pTextOut->pAllocator, &claimedIds );
    if ( validationResult.status != geometry_serialization_status_t::OK ) {
        return validationResult;
    }

    // Build the key-value tree from the document's brush data.
    key_value_document_desc_t documentDesc{};
    documentDesc.pAllocator = pTextOut->pAllocator;
    documentDesc.nInitialNodes = 128u;
    documentDesc.cbInitialStrings = 4u * CY_KIB;

    kv_document_owner_t owner{
        KeyValue_CreateDocument( documentDesc )
    };
    if ( owner.pDocument == nullptr ) {
        return Failure(
            geometry_serialization_status_t::OUT_OF_MEMORY );
    }

    if ( !KeyValue_SetDocumentHeader(
             owner.pDocument,
             {
                 CYKV_LANGUAGE_VERSION,
                 SV( GEOMETRY_SCHEMA_ID ),
                 GEOMETRY_SCHEMA_VERSION
             } ) ||
         !KeyValue_SetRootType(
             owner.pDocument,
             key_value_type_t::OBJECT ) ) {
        return Failure(
            geometry_serialization_status_t::OUT_OF_MEMORY );
    }

    key_value_t *pRoot = KeyValue_Root( owner.pDocument );

    if ( !InsertU64(
             owner.pDocument,
             pRoot,
             "next_source_id",
             pDocument->sourceIds.allocator.next.value ) ) {
        return Failure(
            geometry_serialization_status_t::OUT_OF_MEMORY,
            "next_source_id" );
    }

    key_value_t *pClaimedIds = InsertArray(
        owner.pDocument, pRoot, "claimed_source_ids" );
    if ( pClaimedIds == nullptr ) {
        return Failure(
            geometry_serialization_status_t::OUT_OF_MEMORY,
            "claimed_source_ids" );
    }
    for ( usize i = 0u; i < claimedIds.nCount; ++i ) {
        if ( !AppendU64(
                 owner.pDocument,
                 pClaimedIds,
                 claimedIds.pData[i].value ) ) {
            return Failure(
                geometry_serialization_status_t::OUT_OF_MEMORY,
                "claimed_source_ids", i );
        }
    }

    // Brushes array.
    key_value_t *pBrushes = InsertArray(
        owner.pDocument, pRoot, "brushes" );
    if ( pBrushes == nullptr ) {
        return Failure(
            geometry_serialization_status_t::OUT_OF_MEMORY,
            "brushes" );
    }

    const usize cBrushes = GeometryDocument_BrushCount( pDocument );
    for ( usize i = 0u; i < cBrushes; ++i ) {
        const brush_solid_t *pBrush = pDocument->brushes.pData[i];
        if ( !WriteBrush( owner.pDocument, pBrushes, pBrush,
                          GeometryDocument_BrushAttributesAt( pDocument, i ) ) ) {
            return Failure(
                geometry_serialization_status_t::OUT_OF_MEMORY,
                "brushes[]", i );
        }
    }

    // Meshes array (schema 3).
    const geometry_serialization_result_t meshResult =
        MeshSerialization_TryWriteMeshes(
            owner.pDocument, pRoot, pDocument, pTextOut->pAllocator );
    if ( meshResult.status != geometry_serialization_status_t::OK ) {
        return meshResult;
    }

    // Patches and heightfields (schema 4).
    const geometry_serialization_result_t surfaceResult =
        SurfaceSerialization_TryWrite( owner.pDocument, pRoot, pDocument );
    if ( surfaceResult.status != geometry_serialization_status_t::OK ) {
        return surfaceResult;
    }

    // Emit deterministic text.
    key_value_write_options_t writeOptions{};
    writeOptions.flags = KEY_VALUE_WRITE_FLAG_CANONICAL |
                         KEY_VALUE_WRITE_FLAG_PRETTY |
                         KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE;
    writeOptions.nIndentSpaces = 2u;
    writeOptions.nMaxDepth = 8u;

    // First pass: measure required size.
    const key_value_write_result_t measured = KeyValue_WriteText(
        pRoot, writeOptions, nullptr, 0u );
    if ( measured.status != key_value_write_status_t::OUTPUT_TRUNCATED ) {
        geometry_serialization_result_t result = Failure(
            measured.status == key_value_write_status_t::OUT_OF_MEMORY
                ? geometry_serialization_status_t::OUT_OF_MEMORY
                : geometry_serialization_status_t::CYKV_WRITE_FAILED );
        result.writeStatus = measured.status;
        return result;
    }
    if ( measured.cchRequired > GEOMETRY_SERIALIZATION_MAX_TEXT_BYTES ) {
        return Failure(
            geometry_serialization_status_t::LIMIT_EXCEEDED );
    }

    // Second pass: write into a buffer.
    text_buffer_t pending{};
    if ( !TextBuffer_Init(
             &pending, pTextOut->pAllocator,
             measured.cchRequired ) ||
         !TextBuffer_Resize( &pending, measured.cchRequired ) ) {
        return Failure(
            geometry_serialization_status_t::OUT_OF_MEMORY );
    }

    const key_value_write_result_t written = KeyValue_WriteText(
        pRoot, writeOptions,
        TextBuffer_Data( &pending ),
        TextBuffer_Capacity( &pending ) + 1u );
    if ( written.status != key_value_write_status_t::OK ||
         written.cchWritten != measured.cchRequired ) {
        geometry_serialization_result_t result = Failure(
            written.status == key_value_write_status_t::OUT_OF_MEMORY
                ? geometry_serialization_status_t::OUT_OF_MEMORY
                : geometry_serialization_status_t::CYKV_WRITE_FAILED );
        result.writeStatus = written.status;
        return result;
    }

    if ( !TextBuffer_Assign( pTextOut, TextBuffer_View( &pending ) ) ) {
        return Failure(
            geometry_serialization_status_t::OUT_OF_MEMORY );
    }

    geometry_serialization_result_t result{};
    result.cchText = written.cchWritten;
    return result;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

geometry_serialization_result_t GeometrySerialization_LoadFromText(
    string_view_t text,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_document_t *pDocumentOut ) noexcept
{
    if ( !StringView_IsValid( text ) || text.cchLength == 0u ||
         !Allocator_IsValid( pAllocator ) ||
         pDocumentOut == nullptr ||
         !GeometryPolicy_IsValid( policy ) ) {
        return Failure(
            geometry_serialization_status_t::INVALID_ARGUMENT );
    }
    if ( !IsCanonicalDocumentDestination( *pDocumentOut ) ) {
        return Failure(
            geometry_serialization_status_t::DESTINATION_NOT_EMPTY );
    }

    // Parse the CYKV text with bounded budgets.
    key_value_document_desc_t documentDesc{};
    documentDesc.pAllocator = pAllocator;

    kv_document_owner_t owner{
        KeyValue_CreateDocument( documentDesc )
    };
    if ( owner.pDocument == nullptr ) {
        return Failure(
            geometry_serialization_status_t::OUT_OF_MEMORY );
    }

    key_value_parse_options_t parseOptions{};
    parseOptions.cbMaxInput = GEOMETRY_SERIALIZATION_MAX_TEXT_BYTES;
    parseOptions.nMaxDepth = 8u;
    parseOptions.nMaxNodes =
        GEOMETRY_SERIALIZATION_MAX_BRUSHES *
        ( GEOMETRY_SERIALIZATION_MAX_SIDES_PER_BRUSH * 7u + 3u ) + 4u +
        GEOMETRY_SERIALIZATION_MAX_MESH_NODES;
    parseOptions.nMaxContainerValues =
        GEOMETRY_SERIALIZATION_MAX_CLAIMED_SOURCE_IDS;
    parseOptions.cbMaxStringData = 16u * CY_MIB;

    const key_value_parse_result_t parsed = KeyValue_ParseText(
        text, parseOptions, owner.pDocument );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        geometry_serialization_status_t status =
            geometry_serialization_status_t::CYKV_PARSE_FAILED;
        switch ( parsed.status ) {
            case key_value_parse_status_t::INPUT_LIMIT:
            case key_value_parse_status_t::DEPTH_LIMIT:
            case key_value_parse_status_t::NODE_LIMIT:
            case key_value_parse_status_t::CONTAINER_LIMIT:
            case key_value_parse_status_t::STRING_LIMIT:
                status =
                    geometry_serialization_status_t::LIMIT_EXCEEDED;
                break;
            case key_value_parse_status_t::OUT_OF_MEMORY:
                status =
                    geometry_serialization_status_t::OUT_OF_MEMORY;
                break;
            default:
                break;
        }
        geometry_serialization_result_t result = Failure( status );
        result.parseStatus = parsed.status;
        return result;
    }

    // Validate header.
    const key_value_document_header_t header =
        KeyValue_DocumentHeader( owner.pDocument );
    if ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION ) {
        return Failure(
            geometry_serialization_status_t::HEADER_MISMATCH,
            "@cykv" );
    }
    if ( !StringView_Equals(
             header.schemaId, SV( GEOMETRY_SCHEMA_ID ) ) ) {
        return Failure(
            geometry_serialization_status_t::SCHEMA_MISMATCH,
            "@schema" );
    }
    if ( header.nSchemaVersion <
             GEOMETRY_SCHEMA_VERSION_OLDEST_SUPPORTED ||
         header.nSchemaVersion > GEOMETRY_SCHEMA_VERSION ) {
        return Failure(
            geometry_serialization_status_t::VERSION_UNSUPPORTED,
            "@schema" );
    }

    // Read root object.
    const key_value_t *pRoot = KeyValue_Root( owner.pDocument );
    if ( pRoot == nullptr ||
         KeyValue_Type( pRoot ) != key_value_type_t::OBJECT ) {
        return Failure(
            geometry_serialization_status_t::TYPE_MISMATCH,
            "root" );
    }

    // Reject document-wide mesh budgets before initializing the destination,
    // loading identity state, or constructing brushes. The mesh reader repeats
    // this check because it is also a public incremental entry point.
    if ( header.nSchemaVersion >= GEOMETRY_SCHEMA_VERSION_MESHES &&
         KeyValue_Find( pRoot, SV( "meshes" ) ) != nullptr ) {
        const geometry_serialization_result_t meshBudgetResult =
            MeshSerialization_TryPreflightReadBudgets(
                pRoot, policy );
        if ( meshBudgetResult.status !=
             geometry_serialization_status_t::OK ) {
            return meshBudgetResult;
        }
    }

    geometry_source_id_t persistedNextSourceId{};
    vector_t<geometry_source_id_t> persistedClaimedIds{};
    if ( header.nSchemaVersion >= 2u ) {
        const geometry_serialization_result_t identityResult =
            ReadVersion2IdentityState(
                pRoot,
                pAllocator,
                policy,
                &persistedNextSourceId,
                &persistedClaimedIds );
        if ( identityResult.status !=
             geometry_serialization_status_t::OK ) {
            return identityResult;
        }
    }

    const key_value_t *pBrushes = KeyValue_Find( pRoot, SV( "brushes" ) );
    if ( pBrushes == nullptr ) {
        return Failure(
            geometry_serialization_status_t::MISSING_FIELD,
            "brushes" );
    }
    if ( KeyValue_Type( pBrushes ) != key_value_type_t::ARRAY ) {
        return Failure(
            geometry_serialization_status_t::TYPE_MISMATCH,
            "brushes" );
    }

    const usize cBrushes = KeyValue_ChildCount( pBrushes );
    const geometry_serialization_result_t budgetResult =
        ValidateBrushContainerBudgets( pBrushes, policy );
    if ( budgetResult.status != geometry_serialization_status_t::OK ) {
        return budgetResult;
    }

    // Initialize the output document.
    geometry_status_t geoStatus = GeometryDocument_Init(
        pDocumentOut, pAllocator, policy );
    if ( geoStatus != geometry_status_t::OK ) {
        return Failure(
            MapGeometryStatus( geoStatus ) );
    }

    if ( header.nSchemaVersion >= 2u ) {
        const geometry_serialization_result_t identityResult =
            PrepareVersion2IdentityRegistry(
                pDocumentOut,
                persistedNextSourceId,
                persistedClaimedIds );
        if ( identityResult.status !=
             geometry_serialization_status_t::OK ) {
            GeometryDocument_Shutdown( pDocumentOut );
            return identityResult;
        }
    }

    // Read each brush.
    for ( usize i = 0u; i < cBrushes; ++i ) {
        const key_value_t *pBrushObj =
            KeyValue_ChildAt( pBrushes, i );

        brush_solid_t brush{};
        const geometry_serialization_result_t brushResult =
            ReadBrush( pBrushObj, &brush, pAllocator, policy, i );
        if ( brushResult.status !=
             geometry_serialization_status_t::OK ) {
            GeometryDocument_Shutdown( pDocumentOut );
            return brushResult;
        }

        if ( header.nSchemaVersion >= 2u &&
             !BrushIdsAreClaimed(
                 brush, pDocumentOut->sourceIds ) ) {
            BrushSolid_Shutdown( &brush );
            GeometryDocument_Shutdown( pDocumentOut );
            return Failure(
                geometry_serialization_status_t::INVALID_SOURCE_ID,
                "brushes[]", i );
        }

        // Side records (schema 4); older files get default records.
        geometry_brush_side_attribute_store_t surfaces{};
        if ( header.nSchemaVersion >= GEOMETRY_SCHEMA_VERSION_SURFACES ) {
            const geometry_serialization_result_t surfaceResult =
                ReadSurfaces( pBrushObj, pAllocator, policy, i, &surfaces );
            if ( surfaceResult.status != geometry_serialization_status_t::OK ) {
                BrushSolid_Shutdown( &brush );
                GeometryDocument_Shutdown( pDocumentOut );
                return surfaceResult;
            }
            // Every side must address one of its brush's own records.
            if ( !BrushAttributes_Covers( &brush, &surfaces ) ) {
                BrushSideAttributeStore_Shutdown( &surfaces );
                BrushSolid_Shutdown( &brush );
                GeometryDocument_Shutdown( pDocumentOut );
                return Failure( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "brushes[].sides[].attribute_index", i );
            }
        }

        // Add the deserialized brush to the document. This deep-copies
        // it into document-owned storage.
        geoStatus = GeometryDocument_TryAddBrushWithAttributes(
            pDocumentOut, &brush,
            header.nSchemaVersion >= GEOMETRY_SCHEMA_VERSION_SURFACES ? &surfaces : nullptr );
        BrushSideAttributeStore_Shutdown( &surfaces );
        BrushSolid_Shutdown( &brush );

        if ( geoStatus != geometry_status_t::OK ) {
            GeometryDocument_Shutdown( pDocumentOut );
            return Failure(
                MapGeometryStatus( geoStatus ),
                "brushes[]", i );
        }
    }

    // Meshes (schema 3). The section is optional: its absence means the
    // document has no meshes, so hand-written or older-writer v3 files
    // without it stay loadable.
    if ( header.nSchemaVersion >= GEOMETRY_SCHEMA_VERSION_MESHES &&
         KeyValue_Find( pRoot, SV( "meshes" ) ) != nullptr ) {
        const geometry_serialization_result_t meshResult =
            MeshSerialization_TryReadMeshes(
                pRoot, pDocumentOut, header.nSchemaVersion >= 2u );
        if ( meshResult.status != geometry_serialization_status_t::OK ) {
            GeometryDocument_Shutdown( pDocumentOut );
            return meshResult;
        }
    }

    // Patches and heightfields (schema 4), each section optional.
    if ( header.nSchemaVersion >= GEOMETRY_SCHEMA_VERSION_SURFACES ) {
        const geometry_serialization_result_t surfaceResult =
            SurfaceSerialization_TryRead( pRoot, pDocumentOut, header.nSchemaVersion >= 2u );
        if ( surfaceResult.status != geometry_serialization_status_t::OK ) {
            GeometryDocument_Shutdown( pDocumentOut );
            return surfaceResult;
        }
    }

    geoStatus = GeometrySourceIdRegistry_SealLoadedIds(
        &pDocumentOut->sourceIds );
    if ( geoStatus != geometry_status_t::OK ||
         !GeometrySourceIdRegistry_ValidateDeep(
             &pDocumentOut->sourceIds ) ) {
        GeometryDocument_Shutdown( pDocumentOut );
        return Failure(
            geometry_serialization_status_t::DOCUMENT_INIT_FAILED );
    }

    geometry_serialization_result_t result{};
    result.cchText = text.cchLength;
    return result;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

const char *GeometrySerialization_StatusName(
    geometry_serialization_status_t status ) noexcept
{
    switch ( status ) {
        case geometry_serialization_status_t::OK:
            return "OK";
        case geometry_serialization_status_t::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case geometry_serialization_status_t::DESTINATION_NOT_EMPTY:
            return "DESTINATION_NOT_EMPTY";
        case geometry_serialization_status_t::NOT_INITIALIZED:
            return "NOT_INITIALIZED";
        case geometry_serialization_status_t::LIMIT_EXCEEDED:
            return "LIMIT_EXCEEDED";
        case geometry_serialization_status_t::OUT_OF_MEMORY:
            return "OUT_OF_MEMORY";
        case geometry_serialization_status_t::CYKV_WRITE_FAILED:
            return "CYKV_WRITE_FAILED";
        case geometry_serialization_status_t::CYKV_PARSE_FAILED:
            return "CYKV_PARSE_FAILED";
        case geometry_serialization_status_t::HEADER_MISMATCH:
            return "HEADER_MISMATCH";
        case geometry_serialization_status_t::SCHEMA_MISMATCH:
            return "SCHEMA_MISMATCH";
        case geometry_serialization_status_t::VERSION_UNSUPPORTED:
            return "VERSION_UNSUPPORTED";
        case geometry_serialization_status_t::MISSING_FIELD:
            return "MISSING_FIELD";
        case geometry_serialization_status_t::TYPE_MISMATCH:
            return "TYPE_MISMATCH";
        case geometry_serialization_status_t::VALUE_OUT_OF_RANGE:
            return "VALUE_OUT_OF_RANGE";
        case geometry_serialization_status_t::INVALID_SOURCE_ID:
            return "INVALID_SOURCE_ID";
        case geometry_serialization_status_t::DUPLICATE_SOURCE_ID:
            return "DUPLICATE_SOURCE_ID";
        case geometry_serialization_status_t::PLANE_NOT_NORMALIZED:
            return "PLANE_NOT_NORMALIZED";
        case geometry_serialization_status_t::CORRUPT_DOCUMENT:
            return "CORRUPT_DOCUMENT";
        case geometry_serialization_status_t::DOCUMENT_INIT_FAILED:
            return "DOCUMENT_INIT_FAILED";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor::geometry
