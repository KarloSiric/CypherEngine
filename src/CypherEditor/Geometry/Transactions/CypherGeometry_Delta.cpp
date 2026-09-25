//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Delta.cpp
//  Purpose: Implements typed geometry change records with inverse computation.
//  Details: Delta application modifies document storage directly. Inverse
//           computation deep-copies brush data so the inverse is independent
//           of the original delta's lifetime. Change sets use a simple linear
//           scan for duplicate rejection — the number of distinct brushes
//           affected by one transaction is typically single digits.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Delta.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_ReplacementIdentity.h"

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Delta lifecycle
// ---------------------------------------------------------------------------

void GeometryDelta_Shutdown(
    geometry_delta_t *pDelta ) noexcept
{
    if ( pDelta == nullptr ) {
        return;
    }

    if ( pDelta->kind == geometry_delta_kind_t::BRUSH_ADDED ||
         pDelta->kind == geometry_delta_kind_t::BRUSH_REMOVED ) {
        BrushSolid_Shutdown( &pDelta->brushData );
    }
    else if ( pDelta->kind == geometry_delta_kind_t::BRUSH_REPLACED ) {
        BrushSolid_Shutdown( &pDelta->brushData );
        BrushSolid_Shutdown( &pDelta->newBrushData );
    }

    BrushSideAttributeStore_Shutdown( &pDelta->attributes );
    BrushSideAttributeStore_Shutdown( &pDelta->newAttributes );
    pDelta->oldAttribute = {};
    pDelta->newAttribute = {};

    pDelta->kind = geometry_delta_kind_t::INVALID;
    pDelta->brushId = {};
    pDelta->sideIndex = 0u;
    pDelta->oldPlane = {};
    pDelta->newPlane = {};
    pDelta->brushData.sourceId = {};
    pDelta->newBrushData.sourceId = {};
}

namespace
{

// Copies a delta record table when it was captured (initialized); leaves
// the destination uninitialized otherwise.
geometry_status_t CopyStoreIfPresent(
    const geometry_brush_side_attribute_store_t &source,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits,
    geometry_brush_side_attribute_store_t *pOut ) noexcept
{
    if ( source.records.pAllocator == nullptr ) { return geometry_status_t::OK; }
    geometry_status_t st = BrushSideAttributeStore_Init( pOut, pAllocator );
    if ( st == geometry_status_t::OK ) { st = BrushSideAttributeStore_TryCopyFrom( pOut, &source, limits ); }
    if ( st != geometry_status_t::OK ) { BrushSideAttributeStore_Shutdown( pOut ); }
    return st;
}

} // namespace

geometry_status_t GeometryDelta_TryComputeInverse(
    const geometry_delta_t *pDelta,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits,
    geometry_delta_t *pInverseOut ) noexcept
{
    if ( pDelta == nullptr || pAllocator == nullptr ||
         pInverseOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pInverseOut->kind != geometry_delta_kind_t::INVALID ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }

    switch ( pDelta->kind ) {
        case geometry_delta_kind_t::BRUSH_ADDED: {
            // Inverse of adding a brush is removing it. We deep-copy the
            // brush data so the inverse can restore the exact brush on redo.
            brush_solid_t copy{};
            const geometry_status_t copyStatus = BrushSolid_DeepCopy(
                &copy, &pDelta->brushData, pAllocator, limits );
            if ( copyStatus != geometry_status_t::OK ) {
                return copyStatus;
            }

            geometry_brush_side_attribute_store_t records{};
            const geometry_status_t recordStatus = CopyStoreIfPresent( pDelta->attributes, pAllocator, limits, &records );
            if ( recordStatus != geometry_status_t::OK ) {
                BrushSolid_Shutdown( &copy );
                return recordStatus;
            }

            pInverseOut->kind = geometry_delta_kind_t::BRUSH_REMOVED;
            pInverseOut->brushId = pDelta->brushId;
            pInverseOut->brushData.sourceId = copy.sourceId;
            common::Vector_Move(
                &pInverseOut->brushData.sides, &copy.sides );
            common::Vector_Move( &pInverseOut->attributes.records, &records.records );
            return geometry_status_t::OK;
        }

        case geometry_delta_kind_t::BRUSH_REMOVED: {
            // Inverse of removing a brush is adding it back.
            brush_solid_t copy{};
            const geometry_status_t copyStatus = BrushSolid_DeepCopy(
                &copy, &pDelta->brushData, pAllocator, limits );
            if ( copyStatus != geometry_status_t::OK ) {
                return copyStatus;
            }

            geometry_brush_side_attribute_store_t records{};
            const geometry_status_t recordStatus = CopyStoreIfPresent( pDelta->attributes, pAllocator, limits, &records );
            if ( recordStatus != geometry_status_t::OK ) {
                BrushSolid_Shutdown( &copy );
                return recordStatus;
            }

            pInverseOut->kind = geometry_delta_kind_t::BRUSH_ADDED;
            pInverseOut->brushId = pDelta->brushId;
            pInverseOut->brushData.sourceId = copy.sourceId;
            common::Vector_Move(
                &pInverseOut->brushData.sides, &copy.sides );
            common::Vector_Move( &pInverseOut->attributes.records, &records.records );
            return geometry_status_t::OK;
        }

        case geometry_delta_kind_t::BRUSH_SIDE_PLANE_CHANGED: {
            pInverseOut->kind = geometry_delta_kind_t::BRUSH_SIDE_PLANE_CHANGED;
            pInverseOut->brushId = pDelta->brushId;
            pInverseOut->sideIndex = pDelta->sideIndex;
            pInverseOut->oldPlane = pDelta->newPlane;
            pInverseOut->newPlane = pDelta->oldPlane;
            return geometry_status_t::OK;
        }

        case geometry_delta_kind_t::BRUSH_REPLACED: {
            // Inverse swaps old and new brush states.
            brush_solid_t oldCopy{};
            const geometry_status_t oldCopyStatus = BrushSolid_DeepCopy(
                &oldCopy, &pDelta->newBrushData, pAllocator, limits );
            if ( oldCopyStatus != geometry_status_t::OK ) {
                return oldCopyStatus;
            }

            brush_solid_t newCopy{};
            const geometry_status_t newCopyStatus = BrushSolid_DeepCopy(
                &newCopy, &pDelta->brushData, pAllocator, limits );
            if ( newCopyStatus != geometry_status_t::OK ) {
                BrushSolid_Shutdown( &oldCopy );
                return newCopyStatus;
            }

            // Records swap the same way (each copied only if captured).
            geometry_brush_side_attribute_store_t oldRecords{}, newRecords{};
            geometry_status_t recordStatus = CopyStoreIfPresent( pDelta->newAttributes, pAllocator, limits, &oldRecords );
            if ( recordStatus == geometry_status_t::OK ) {
                recordStatus = CopyStoreIfPresent( pDelta->attributes, pAllocator, limits, &newRecords );
            }
            if ( recordStatus != geometry_status_t::OK ) {
                BrushSideAttributeStore_Shutdown( &oldRecords );
                BrushSolid_Shutdown( &oldCopy );
                BrushSolid_Shutdown( &newCopy );
                return recordStatus;
            }

            pInverseOut->kind = geometry_delta_kind_t::BRUSH_REPLACED;
            pInverseOut->brushId = pDelta->brushId;
            pInverseOut->brushData.sourceId = oldCopy.sourceId;
            common::Vector_Move(
                &pInverseOut->brushData.sides, &oldCopy.sides );
            pInverseOut->newBrushData.sourceId = newCopy.sourceId;
            common::Vector_Move(
                &pInverseOut->newBrushData.sides, &newCopy.sides );
            common::Vector_Move( &pInverseOut->attributes.records, &oldRecords.records );
            common::Vector_Move( &pInverseOut->newAttributes.records, &newRecords.records );
            return geometry_status_t::OK;
        }

        case geometry_delta_kind_t::BRUSH_ATTRIBUTE_CHANGED: {
            pInverseOut->kind = geometry_delta_kind_t::BRUSH_ATTRIBUTE_CHANGED;
            pInverseOut->brushId = pDelta->brushId;
            pInverseOut->sideIndex = pDelta->sideIndex;
            pInverseOut->oldAttribute = pDelta->newAttribute;
            pInverseOut->newAttribute = pDelta->oldAttribute;
            return geometry_status_t::OK;
        }

        default:
            return geometry_status_t::INVALID_ARGUMENT;
    }
}

geometry_status_t GeometryDelta_TryApplyToDocument(
    const geometry_delta_t *pDelta,
    geometry_document_t *pDocument ) noexcept
{
    if ( pDelta == nullptr || !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    switch ( pDelta->kind ) {
        case geometry_delta_kind_t::BRUSH_ADDED: {
            return GeometryDocument_TryAddBrushWithAttributes(
                pDocument, &pDelta->brushData,
                pDelta->attributes.records.pAllocator != nullptr ? &pDelta->attributes : nullptr );
        }

        case geometry_delta_kind_t::BRUSH_REMOVED: {
            return GeometryDocument_TryRemoveBrush(
                pDocument, pDelta->brushId );
        }

        case geometry_delta_kind_t::BRUSH_SIDE_PLANE_CHANGED: {
            brush_solid_t *pBrush = GeometryDocument_FindBrushMutable(
                pDocument, pDelta->brushId );
            if ( pBrush == nullptr ) {
                return geometry_status_t::INVALID_ARGUMENT;
            }
            return BrushSolid_TrySetSidePlane(
                pBrush, pDelta->sideIndex, pDelta->newPlane );
        }

        case geometry_delta_kind_t::BRUSH_REPLACED: {
            brush_solid_t *pBrush = GeometryDocument_FindBrushMutable(
                pDocument, pDelta->brushId );
            if ( pBrush == nullptr ) {
                return geometry_status_t::INVALID_ARGUMENT;
            }

            bool bRegistryChanges = false;
            geometry_source_id_registry_t preparedRegistry{};
            const geometry_status_t identityStatus =
                GeometryReplacementIdentity_TryPrepare(
                    pDocument,
                    pDelta->brushId,
                    pBrush,
                    &pDelta->newBrushData,
                    &preparedRegistry,
                    &bRegistryChanges );
            if ( identityStatus != geometry_status_t::OK ) {
                return identityStatus;
            }

            // Build the full authored record privately. This covers planes,
            // persistent side identities, and attribute bindings equally,
            // including replacements whose side count happens to match.
            brush_solid_t replacement{};
            const geometry_status_t copyStatus = BrushSolid_DeepCopy(
                &replacement,
                &pDelta->newBrushData,
                pDocument->pAllocator,
                pDocument->policy.limits );
            if ( copyStatus != geometry_status_t::OK ) {
                if ( bRegistryChanges ) {
                    GeometrySourceIdRegistry_Shutdown(
                        &preparedRegistry );
                }
                return copyStatus;
            }
            // The records after the edit: the captured ones, or the brush's
            // current ones, extended to cover the new sides.
            geometry_brush_side_attribute_store_t *pLiveRecords =
                GeometryDocument_FindBrushAttributesMutable( pDocument, pDelta->brushId );
            geometry_brush_side_attribute_store_t newRecords{};
            const geometry_status_t recordStatus = pLiveRecords == nullptr
                ? geometry_status_t::CORRUPT_STATE
                : BrushAttributes_TryBuildCovering(
                      &pDelta->newBrushData,
                      pDelta->newAttributes.records.pAllocator != nullptr ? &pDelta->newAttributes : pLiveRecords,
                      pDocument->pAllocator, pDocument->policy, &newRecords );
            if ( recordStatus != geometry_status_t::OK ) {
                BrushSolid_Shutdown( &replacement );
                if ( bRegistryChanges ) {
                    GeometrySourceIdRegistry_Shutdown( &preparedRegistry );
                }
                return recordStatus;
            }

            // All fallible work is complete. The two ownership transfers are
            // allocation-free and therefore publish one indivisible state.
            if ( bRegistryChanges ) {
                GeometryReplacementIdentity_Publish(
                    pDocument, &preparedRegistry );
            }
            BrushSolid_Shutdown( pBrush );
            pBrush->sourceId = replacement.sourceId;
            common::Vector_Move(
                &pBrush->sides, &replacement.sides );
            replacement.sourceId = GEOMETRY_SOURCE_ID_INVALID;
            BrushSideAttributeStore_Shutdown( pLiveRecords );
            common::Vector_Move( &pLiveRecords->records, &newRecords.records );

            if ( bRegistryChanges ) {
                GeometrySourceIdRegistry_Shutdown( &preparedRegistry );
            }
            return geometry_status_t::OK;
        }

        case geometry_delta_kind_t::BRUSH_ATTRIBUTE_CHANGED: {
            geometry_brush_side_attribute_store_t *pRecords =
                GeometryDocument_FindBrushAttributesMutable( pDocument, pDelta->brushId );
            if ( pRecords == nullptr ) {
                return geometry_status_t::INVALID_ARGUMENT;
            }
            return BrushSideAttributeStore_TrySet(
                pRecords, pDocument->policy.numerical, pDelta->sideIndex, pDelta->newAttribute );
        }

        default:
            return geometry_status_t::INVALID_ARGUMENT;
    }
}

geometry_status_t GeometryDelta_TryMakeAttributeChange(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    common::usize iRecord,
    const geometry_brush_side_attributes_t &newValue,
    geometry_delta_t *pDeltaOut ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( pDeltaOut == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( pDeltaOut->kind != geometry_delta_kind_t::INVALID ) { return geometry_status_t::ALREADY_INITIALIZED; }
    const geometry_brush_side_attribute_store_t *pRecords = GeometryDocument_FindBrushAttributes( pDocument, brushId );
    geometry_brush_side_attributes_t oldValue{};
    if ( pRecords == nullptr || BrushSideAttributeStore_TryGet( pRecords, iRecord, &oldValue ) != geometry_status_t::OK ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_status_t valueStatus = BrushSideAttributes_Validate( pDocument->policy.numerical, newValue );
    if ( valueStatus != geometry_status_t::OK ) { return valueStatus; }
    pDeltaOut->kind = geometry_delta_kind_t::BRUSH_ATTRIBUTE_CHANGED;
    pDeltaOut->brushId = brushId;
    pDeltaOut->sideIndex = iRecord;
    pDeltaOut->oldAttribute = oldValue;
    pDeltaOut->newAttribute = newValue;
    return geometry_status_t::OK;
}

geometry_status_t GeometryDelta_TryCaptureBrush(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    geometry_delta_kind_t kind,
    const common::allocator_t *pAllocator,
    geometry_delta_t *pDeltaOut ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( pDeltaOut == nullptr || pAllocator == nullptr ||
         ( kind != geometry_delta_kind_t::BRUSH_ADDED && kind != geometry_delta_kind_t::BRUSH_REMOVED ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pDeltaOut->kind != geometry_delta_kind_t::INVALID ) { return geometry_status_t::ALREADY_INITIALIZED; }
    const brush_solid_t *pSolid = GeometryDocument_FindBrush( pDocument, brushId );
    const geometry_brush_side_attribute_store_t *pRecords = GeometryDocument_FindBrushAttributes( pDocument, brushId );
    if ( pSolid == nullptr || pRecords == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    brush_solid_t copy{};
    geometry_status_t st = BrushSolid_DeepCopy( &copy, pSolid, pAllocator, pDocument->policy.limits );
    if ( st != geometry_status_t::OK ) { return st; }
    geometry_brush_side_attribute_store_t records{};
    st = CopyStoreIfPresent( *pRecords, pAllocator, pDocument->policy.limits, &records );
    if ( st != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &copy );
        return st;
    }
    pDeltaOut->kind = kind;
    pDeltaOut->brushId = brushId;
    pDeltaOut->brushData.sourceId = copy.sourceId;
    common::Vector_Move( &pDeltaOut->brushData.sides, &copy.sides );
    common::Vector_Move( &pDeltaOut->attributes.records, &records.records );
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Change set
// ---------------------------------------------------------------------------

geometry_status_t GeometryChangeset_Init(
    geometry_changeset_t *pChangeset,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( pChangeset == nullptr || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pChangeset->affectedBrushIds.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }

    if ( !common::Vector_Init(
             &pChangeset->affectedBrushIds, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void GeometryChangeset_Shutdown(
    geometry_changeset_t *pChangeset ) noexcept
{
    if ( pChangeset == nullptr ) {
        return;
    }
    common::Vector_Shutdown( &pChangeset->affectedBrushIds );
}

geometry_status_t GeometryChangeset_TryAddBrush(
    geometry_changeset_t *pChangeset,
    geometry_source_id_t brushId ) noexcept
{
    if ( pChangeset == nullptr ||
         pChangeset->affectedBrushIds.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( brushId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Linear duplicate check — affected brush count per transaction is small.
    for ( common::usize i = 0u;
          i < pChangeset->affectedBrushIds.nCount; ++i ) {
        if ( pChangeset->affectedBrushIds.pData[i].value == brushId.value ) {
            return geometry_status_t::OK;
        }
    }

    if ( !common::Vector_PushBack(
             &pChangeset->affectedBrushIds, brushId ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

common::usize GeometryChangeset_Count(
    const geometry_changeset_t *pChangeset ) noexcept
{
    if ( pChangeset == nullptr ||
         pChangeset->affectedBrushIds.pAllocator == nullptr ) {
        return 0u;
    }
    return common::Vector_Count( &pChangeset->affectedBrushIds );
}

bool GeometryChangeset_Contains(
    const geometry_changeset_t *pChangeset,
    geometry_source_id_t brushId ) noexcept
{
    if ( pChangeset == nullptr ||
         pChangeset->affectedBrushIds.pAllocator == nullptr ||
         !GeometrySourceId_IsValid( brushId ) ) {
        return false;
    }

    for ( common::usize i = 0u;
          i < pChangeset->affectedBrushIds.nCount; ++i ) {
        if ( pChangeset->affectedBrushIds.pData[i].value == brushId.value ) {
            return true;
        }
    }
    return false;
}

} // namespace cypher::editor::geometry
