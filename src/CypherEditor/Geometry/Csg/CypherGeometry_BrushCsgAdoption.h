//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCsgAdoption.h
//  Purpose: Declares document-ready brush subtraction result preparation.
//  Details: Raw convex fragments may repeat inherited identities and store
//           operand-local attribute indices. Adoption assigns every output
//           element a unique identity, copies concrete surface records, and
//           records exact operand ancestry before document publication.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSHCSGADOPTION_H
#define CYPHER_EDITOR_GEOMETRY_BRUSHCSGADOPTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_BrushSideStore.h"
#include "CypherGeometry_BrushCSG.h"

namespace cypher::editor::geometry
{

// One destination side has one exact source-side ancestor. Destination IDs
// are unique within the prepared result; source IDs may intentionally repeat
// because one input face can contribute to several convex fragments.
struct brush_csg_side_provenance_t {
    geometry_source_id_t destinationBrushId{};
    geometry_source_id_t destinationSideId{};
    common::u32 iDestinationAttribute{ 0u };

    brush_csg_operand_t operand{ brush_csg_operand_t::MINUEND_A };
    brush_csg_side_origin_t origin{ brush_csg_side_origin_t::INHERITED_A };
    geometry_source_id_t sourceBrushId{};
    geometry_source_id_t sourceSideId{};
    common::u32 iSourceAttribute{ 0u };
};

struct brush_csg_adoption_result_t {
    // Kept as a contiguous brush array so it can be passed directly to the
    // atomic document brush-set replacement primitive.
    brush_solid_t *pFragments{ nullptr };

    // Parallel per-fragment surface stores. pAttributeStores[i] belongs to
    // pFragments[i].
    geometry_brush_side_attribute_store_t *pAttributeStores{ nullptr };
    common::usize cFragments{ 0u };
    common::usize cCapacity{ 0u };
    common::vector_t<brush_csg_side_provenance_t> sideProvenance{};
    const common::allocator_t *pAllocator{ nullptr };
};

// Prepares a raw A − B result for adoption by a document-level command.
//
// Raw subtraction is deliberately representation-local: inherited A side IDs
// can occur in more than one fragment, while a side's attribute index belongs
// to either A's or B's store. This function resolves that ambiguity by:
//   - assigning fresh IDs to every output brush and side;
//   - copying the concrete source surface record into a per-fragment store;
//   - rewriting every side to its destination-local attribute index; and
//   - recording an ordered provenance entry for every output side.
//
// pResultOut must be canonical empty. pIdAllocator is staged and advances only
// after the entire result validates and publishes. Any failure leaves the raw
// fragments, operands, operand stores, allocator, and destination unchanged.
CYPHER_NODISCARD geometry_status_t BrushCsgAdoption_TryPrepareSubtraction(
    const brush_csg_subtract_result_t *pRawResult,
    const brush_solid_t *pBrushA,
    const geometry_brush_side_attribute_store_t *pAttributesA,
    const brush_solid_t *pBrushB,
    const geometry_brush_side_attribute_store_t *pAttributesB,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_csg_adoption_result_t *pResultOut ) noexcept;

void BrushCsgAdoptionResult_Shutdown(
    brush_csg_adoption_result_t *pResult ) noexcept;

CYPHER_NODISCARD const brush_csg_side_provenance_t *
BrushCsgAdoption_FindSideProvenance(
    const brush_csg_adoption_result_t *pResult,
    geometry_source_id_t destinationSideId ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSHCSGADOPTION_H
