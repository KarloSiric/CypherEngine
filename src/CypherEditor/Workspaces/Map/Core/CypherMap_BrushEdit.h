//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Map-owned convex brush operations over the existing clipping/CSG backend.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_EDITOR_MAP_BRUSH_EDIT_H
#define CYPHER_EDITOR_MAP_BRUSH_EDIT_H
#pragma once

#include "CypherMap_Document.h"

namespace cypher::editor::map
{

// These operations accept brush roots only. Entities, meshes, patches and
// terrains require their own operation semantics; they are never approximated
// by brush bounds. Duplicate IDs in a selection are rejected.
CYPHER_NODISCARD common::bool_t MapBrushEdit_CanEdit( const map_document_t *pMap,
    common::span_t<const common::u64> ids ) noexcept;

// PRIVATE working-copy operations: clone with MapEdit_Clone, then discard the
// copy on failure. A batch can have completed earlier brushes before failing.
// No file I/O occurs. pRootsOut must be initialized and receives the complete
// resulting selection only on success.
//
// Hollow builds convex wall fragments using BrushCSG_TryHollow and its exact
// attribute-adoption path. Every fragment/side gets a fresh ID. The original
// brush is replaced; host metadata, layer, entity ownership and per-face
// residual fields follow the backend's explicit side provenance.
CYPHER_NODISCARD map_status_t MapBrushEdit_Hollow( map_document_t *pMap,
    common::span_t<const common::u64> ids, common::f64 wallThickness,
    common::vector_t<common::u64> *pRootsOut ) noexcept;

// Keeps the nonpositive half-space n.p + d <= 0 of a NORMALIZED plane.
// Root and retained side IDs survive. New cut faces use cutMaterial and an
// independent planar UV projection. A plane retaining the complete brush is
// an exact no-op; entirely removed/zero-volume brushes reject the batch.
// pChangedOut (optional) distinguishes productive cuts from all-no-op batches.
CYPHER_NODISCARD map_status_t MapBrushEdit_Clip( map_document_t *pMap,
    common::span_t<const common::u64> ids, math::planed_t plane,
    common::string_view_t cutMaterial, common::vector_t<common::u64> *pRootsOut,
    common::bool_t *pChangedOut = nullptr ) noexcept;

enum class map_brush_clip_mode_t : common::u8 { BACK, FRONT, BOTH };
// BACK retains n.p+d<=0, FRONT retains n.p+d>=0, BOTH splits intersected
// brushes. Wholly discarded brushes are removed; wholly retained brushes
// and unsplit BOTH brushes are exact no-ops. BACK/FRONT keep surviving root
// and side identities. Productive BOTH gives each half fresh root/side IDs,
// target placement/root metadata and inherited surface ancestry. Cut faces
// use cutMaterial with an independent nondegenerate UV projection. This is
// the private-copy/output contract above; legacy Clip keeps its reject-on-
// removal behavior for existing callers.
CYPHER_NODISCARD map_status_t MapBrushEdit_ClipMode( map_document_t *pMap,
    common::span_t<const common::u64> ids, math::planed_t plane,
    map_brush_clip_mode_t mode, common::string_view_t cutMaterial,
    common::vector_t<common::u64> *pRootsOut, common::bool_t *pChangedOut = nullptr ) noexcept;

// Subtracts one retained cutter from every target brush. Cutter membership in
// the target selection is invalid. Disjoint/touching targets keep their exact
// identities; contained targets are deleted; productive targets are replaced
// by fresh convex fragments. Fragments inherit target placement/root metadata,
// and each face's material, UV and residual fields follow its exact target or
// cutter ancestor. RootsOut contains surviving targets/fragments on success;
// cutter selection is the host's choice. All-no-op batches retain revision and
// next ID. RootsOut and pChangedOut are unchanged on failure.
CYPHER_NODISCARD map_status_t MapBrushEdit_Subtract( map_document_t *pMap,
    common::span_t<const common::u64> targetIds, common::u64 cutterId,
    common::vector_t<common::u64> *pRootsOut, common::bool_t *pChangedOut = nullptr ) noexcept;

// Merges exactly two distinct authored brush roots only when their actual
// union is convex. Both must have explicit, matching layer/entity placement.
// Disjoint or nonconvex unions reject; gaps are never filled by a hull.
// The single result and all of its sides receive fresh identities. Root/name/
// custom metadata follows ids[0], including when ids[1] contains it; each
// concrete surface and residual face fields follow the backend's exact
// operand-side identity. Hull construction prefers A for equivalent support
// planes; containment copies the containing operand (B for equal brushes).
// Uses the private-copy contract above; pRootsOut is unchanged on failure.
CYPHER_NODISCARD map_status_t MapBrushEdit_Merge( map_document_t *pMap,
    common::span_t<const common::u64> ids, common::vector_t<common::u64> *pRootsOut ) noexcept;

} // namespace cypher::editor::map
#endif
