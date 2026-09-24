//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Repair.h
//  Purpose: Declares explicit, previewable repair of polygon soups and
//           editable meshes.
//  Details: ARCHITECTURE.md: "Repair is separate and undoable. Open loops
//           are not silently fan-capped; concave, holed, and non-planar
//           boundaries require a checked repair choice." Accordingly:
//
//             - nothing here runs unless its step is enabled in the plan;
//             - every step reports exactly what it changed;
//             - repair never mutates its input. The result is a new soup
//               (or mesh built through Sanitation) that a host previews and
//               then commits through a Transaction, which is what makes the
//               repair undoable.
//
//           Steps, applied in this fixed order (each depends on the last):
//             1. weld            — merge vertices within fWeldDistance;
//             2. degenerate      — drop faces with < 3 distinct corners or
//                                  area below fMinimumFaceArea;
//             3. duplicates      — drop faces whose vertex set repeats an
//                                  earlier face (either winding);
//             4. orientation     — make neighbouring faces agree across
//                                  manifold edges, then turn closed shells
//                                  outward (positive signed volume) and
//                                  open shells toward their majority;
//             5. hole filling    — cap boundary loops that are planar within
//                                  fPlanarityTolerance, ear-clipped in their
//                                  plane. Non-planar or ambiguous loops are
//                                  counted and left open.
//
//           Cases that are reported, never "fixed": non-orientable
//           components (a Möbius strip has no consistent orientation) and
//           non-manifold edges (orientation does not propagate across them).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_REPAIR_H
#define CYPHER_EDITOR_GEOMETRY_REPAIR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_EditableMesh.h"
#include "CypherGeometry_Sanitation.h"

namespace cypher::editor::geometry
{

struct repair_plan_t {
    bool bWeld{ false };
    common::f64 fWeldDistance{ 0.0 };
    bool bRemoveDegenerateFaces{ false };
    common::f64 fMinimumFaceArea{ 1.0e-12 };
    bool bRemoveDuplicateFaces{ false };
    bool bOrientConsistently{ false };
    bool bFillHoles{ false };
    common::f64 fPlanarityTolerance{ 1.0e-7 };
};

struct repair_report_t {
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
    common::u32 cVerticesWelded{ 0u };
    common::u32 cDegenerateFacesRemoved{ 0u };
    common::u32 cDuplicateFacesRemoved{ 0u };
    common::u32 cFacesFlipped{ 0u };
    common::u32 cComponents{ 0u };
    common::u32 cNonOrientableComponents{ 0u };
    common::u32 cNonManifoldEdges{ 0u };
    common::u32 cHolesFilled{ 0u };
    common::u32 cHolesLeftOpen{ 0u };   // non-planar or ambiguous boundary loops
    common::u32 cCapTriangles{ 0u };
};

// Applies plan to pIn, writing the repaired soup to pOut (initialized and
// empty). Face source IDs and groups survive for kept faces; cap faces get
// an invalid source ID and group 0. Failure-atomic: pOut stays empty.
CYPHER_NODISCARD repair_report_t Repair_TryRepairSoup(
    const polygon_soup_t *pIn,
    const repair_plan_t &plan,
    polygon_soup_t *pOut ) noexcept;

// Convenience preview: mesh -> soup -> repair -> Sanitation -> pMeshOut
// (zero-initialized). pSanitationOut (optional) receives the rebuild report
// so a host can tell whether the repaired result is now closed/manifold.
// The input mesh is not modified.
CYPHER_NODISCARD repair_report_t Repair_TryPreviewMesh(
    const editable_mesh_t *pMesh,
    const repair_plan_t &plan,
    const sanitation_policy_t &rebuildPolicy,
    const common::allocator_t *pAllocator,
    editable_mesh_t *pMeshOut,
    sanitation_report_t *pSanitationOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_REPAIR_H
