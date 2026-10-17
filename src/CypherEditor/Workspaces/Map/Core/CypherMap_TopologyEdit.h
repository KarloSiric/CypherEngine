//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_TopologyEdit.h
//  Purpose: Declares the map-level adapters for the mesh component tools
//           (Hammer's vertex, edge and face operations) and the remaining
//           whole-object operations: mirror and solidify.
//  Details: Each component edit addresses one authored mesh by persistent
//           source IDs and runs through MapMeshEdit_Apply, so new topology
//           receives fresh IDs above both high-water marks and residual face
//           records follow backend lineage. Nothing here allocates IDs or
//           revises the document on its own terms.
//
//           PRIVATE working-copy contract (as CypherMap_MeshEdit.h): clone
//           with MapEdit_Clone, discard the copy after any failure. A batch
//           can have changed earlier objects before failing. No I/O, no
//           editor history; the GUI layer publishes one undo step.
//
//  History:
//  - Created by Karlo Siric on 2026-10-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_TOPOLOGY_EDIT_H
#define CYPHER_EDITOR_MAP_TOPOLOGY_EDIT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"
#include "CypherGeometry_MeshSelection.h"
#include "CypherGeometry_MeshSourceTopology.h"

#include <vector>

namespace cypher::editor::map
{

using map_vertex_ids_t = common::span_t<const geometry::geometry_source_id_t>;
using map_edge_refs_t = common::span_t<const geometry::mesh_edge_ref_t>;

/*
================
Vertices
================
*/

// Merges selected vertices at their average. A depth axis (0..2) groups
// orthographic columns by depth, preserving thickness; -1 merges one group.
// Singleton depth groups stay unchanged. Faces collapsed by a weld are removed.
CYPHER_NODISCARD map_status_t MapTopologyEdit_MergeVertices( map_document_t *pMap, common::u64 meshId,
    map_vertex_ids_t vertices, int depthAxis = -1 ) noexcept;

// Interleaved target/source pairs. Weld directly at each stationary target;
// no invalid intermediate move onto a coincident vertex is published.
CYPHER_NODISCARD map_status_t MapTopologyEdit_WeldVertexPairs( map_document_t *pMap, common::u64 meshId,
    map_vertex_ids_t targetSourcePairs ) noexcept;

// Splits every face with exactly two selected, nonadjacent corners. This
// includes both surfaces when orthographic picking selects depth-stacked
// vertex columns. Adjacent pairs are skipped; more than two selected corners
// on any face is ambiguous and rejects the whole edit before mutation.
// Requires at least one new cut. Positions and vertex IDs remain unchanged.
CYPHER_NODISCARD map_status_t MapTopologyEdit_ConnectVertexSet( map_document_t *pMap, common::u64 meshId,
    map_vertex_ids_t vertices ) noexcept;

// Two-vertex convenience wrapper for ConnectVertexSet.
CYPHER_NODISCARD map_status_t MapTopologyEdit_ConnectVertices( map_document_t *pMap, common::u64 meshId,
    geometry::geometry_source_id_t a, geometry::geometry_source_id_t b ) noexcept;

// Moves each vertex to the nearest grid point. Faces stay as authored, so a
// snap that would bend a face out of its plane is the user's decision.
CYPHER_NODISCARD map_status_t MapTopologyEdit_SnapVertices( map_document_t *pMap, common::u64 meshId,
    map_vertex_ids_t vertices, common::f64 gridSize ) noexcept;

// Places three or more vertices on one circle: the plane through their
// centroid that fits them, at their average distance from it (Radial Align).
CYPHER_NODISCARD map_status_t MapTopologyEdit_RadialAlign( map_document_t *pMap, common::u64 meshId, map_vertex_ids_t vertices ) noexcept;

// Gives every face corner at each vertex the average of their material UVs,
// closing seams in the texture layout at those vertices (Weld UVs).
CYPHER_NODISCARD map_status_t MapTopologyEdit_WeldUvs( map_document_t *pMap, common::u64 meshId, map_vertex_ids_t vertices ) noexcept;

/*
================
Edges
================
*/

// Collapses each connected group of edges to the average of its vertices.
CYPHER_NODISCARD map_status_t MapTopologyEdit_CollapseEdges( map_document_t *pMap, common::u64 meshId, map_edge_refs_t edges ) noexcept;

// Welds two open edges together, pairing their nearest endpoints (edge Merge).
CYPHER_NODISCARD map_status_t MapTopologyEdit_SewEdges( map_document_t *pMap, common::u64 meshId,
    geometry::mesh_edge_ref_t first, geometry::mesh_edge_ref_t second ) noexcept;

// Removes interior edges, joining the faces on either side. Edges an earlier
// removal already took away are skipped; a boundary edge fails the batch.
CYPHER_NODISCARD map_status_t MapTopologyEdit_DissolveEdges( map_document_t *pMap, common::u64 meshId, map_edge_refs_t edges ) noexcept;

// Inserts a vertex at the midpoint of each edge.
CYPHER_NODISCARD map_status_t MapTopologyEdit_SplitEdges( map_document_t *pMap, common::u64 meshId, map_edge_refs_t edges ) noexcept;

// Splits every edge at its midpoint, then joins the two new midpoints of each
// face that received exactly two (Hammer's edge Connect, V). Two opposite
// edges of a quad therefore cut it in half. Inputs must be distinct live
// edges, each participating in at least one original face with exactly two
// selected edges. More than two on any face is ambiguous. Invalid selections
// reject before mutation; later failures follow the private-copy contract.
// Midpoints are addressed by assigned IDs, including on coincident shells.
CYPHER_NODISCARD map_status_t MapTopologyEdit_ConnectEdges( map_document_t *pMap, common::u64 meshId, map_edge_refs_t edges ) noexcept;

// Closes the boundary loop through each listed boundary edge with one face
// that takes the material of the face across that edge (Fill Hole, P).
CYPHER_NODISCARD map_status_t MapTopologyEdit_FillHoles( map_document_t *pMap, common::u64 meshId, map_edge_refs_t edges ) noexcept;

// Joins the two boundary loops through two boundary edges with a band of
// faces (Bridge, B). Endpoints pair by distance.
CYPHER_NODISCARD map_status_t MapTopologyEdit_BridgeEdges( map_document_t *pMap, common::u64 meshId,
    geometry::mesh_edge_ref_t first, geometry::mesh_edge_ref_t second ) noexcept;

// Chamfers (one segment) or rounds the edges; width is measured inside each
// adjacent face. A width the faces cannot hold is rejected unchanged.
CYPHER_NODISCARD map_status_t MapTopologyEdit_BevelEdges( map_document_t *pMap, common::u64 meshId, map_edge_refs_t edges,
    common::f64 width, common::u32 cSegments ) noexcept;

// NORMAL pulls boundary edges off the surface along the faces' normal
// (Extrude, X); EXTEND continues the surface outward in its own plane
// (Extend, N). One offset applies to all edges: the average direction.
enum class map_edge_extrude_t : common::u8 { NORMAL, EXTEND };
CYPHER_NODISCARD map_status_t MapTopologyEdit_ExtrudeEdges( map_document_t *pMap, common::u64 meshId, map_edge_refs_t edges,
    common::f64 distance, map_edge_extrude_t direction,
    common::vector_t<geometry::mesh_edge_ref_t> *pOuterEdgesOut = nullptr ) noexcept;

// HARD splits shading across the edges; SOFT also gives the two faces a
// shared smoothing group so they shade as one surface; DEFAULT removes the
// override and the faces' own smoothing groups decide.
enum class map_edge_normals_t : common::u8 { HARD, SOFT, DEFAULT };
CYPHER_NODISCARD map_status_t MapTopologyEdit_SetEdgeNormals( map_document_t *pMap, common::u64 meshId, map_edge_refs_t edges,
    map_edge_normals_t normals ) noexcept;

// Moves the vertices by one offset (the Vertex tool's drag of a component
// selection). Faces stay as authored.
CYPHER_NODISCARD map_status_t MapTopologyEdit_MoveVertices( map_document_t *pMap, common::u64 meshId, map_vertex_ids_t vertices,
    math::vec3d_t offset ) noexcept;

// Applies an affine transform to the vertices (the component gizmo's move,
// rotate and scale). Faces stay as authored.
CYPHER_NODISCARD map_status_t MapTopologyEdit_TransformVertices( map_document_t *pMap, common::u64 meshId, map_vertex_ids_t vertices,
    const math::affine3d_t &transform ) noexcept;

// Hammer's Shift-drag of a face: extrudes it along its normal by the part of
// `offset` along the normal (when positive), then moves the extruded cap by
// the rest. A drag into the solid only moves the face.
CYPHER_NODISCARD map_status_t MapTopologyEdit_ExtrudeFaceBy( map_document_t *pMap, common::u64 meshId, common::u64 faceId,
    math::vec3d_t offset ) noexcept;

// Extrudes two or more edge-connected, coplanar, consistently wound faces
// along their shared normal. Distance must exceed the minimum edge length.
// Caps retain their
// face IDs, individual corner UVs/colours, material/smoothing and edge data;
// only the region perimeter receives walls (no walls on internal edges).
// Walls inherit the adjacent selected face's lineage and endpoint colours.
// Each wall UV0/UV1 chart preserves its source edge and UV/world density;
// a degenerate UV edge uses 128 world units per repeat. Charts are independent,
// not continuous unwraps or packed lightmap islands.
// Disconnected/noncoplanar regions and inward pulls are not supported. This
// validates local topology, not intersections with unrelated geometry. The
// private working-copy failure contract above still applies.
CYPHER_NODISCARD map_status_t MapTopologyEdit_ExtrudeFaceRegionBy( map_document_t *pMap, common::u64 meshId,
    common::span_t<const common::u64> faceIds, common::f64 distance ) noexcept;

// Insets one connected coplanar, consistently wound face region by a measured
// perpendicular edge width. Two or more faces require one simple convex outer
// perimeter without holes; collinear subdivisions retain their order/fractions.
// Caps retain their face IDs and partition; strictly interior vertices retain
// their handles/IDs/positions. Only perimeter vertices get inset copies. One
// border quad per perimeter segment inherits the adjacent selected face's
// surfacing/lineage; new corner attributes use the existing parent-face transfer
// (per-face UV charts, not a new unwrap). Cap and outer edge flags/creases follow
// their authored edges. Offsets that fold/collapse any cap cell refuse.
// At most 1024 perimeter segments and 16384 selected corners are supported;
// larger operations return LIMIT_EXCEEDED without changing format limits.
// One face delegates the existing strictly-convex measured-width inset, whose
// collinear-corner restriction remains. No concave regions, holes or remeshing.
// Root/owner/layer and private working-copy failure rules above are unchanged.
CYPHER_NODISCARD map_status_t MapTopologyEdit_InsetFaceRegionByEdgeWidth( map_document_t *pMap, common::u64 meshId,
    common::span_t<const common::u64> faceIds, common::f64 width ) noexcept;

// Hammer's Shift-drag of open edges: extends them by `offset`.
CYPHER_NODISCARD map_status_t MapTopologyEdit_ExtendEdgesBy( map_document_t *pMap, common::u64 meshId, map_edge_refs_t edges,
    math::vec3d_t offset, common::vector_t<geometry::mesh_edge_ref_t> *pOuterEdgesOut = nullptr ) noexcept;

// Copies the selected boundary through an affine transform and joins it to
// the unchanged old edge. Supports continued edge pulls around a pivot.
// Optional initialized output is REPLACED with the new outer edges' stable
// endpoint IDs, canonical, sorted and unique, only on success. It stays unchanged on any
// failure. New IDs are resolved before the private transaction publishes.
CYPHER_NODISCARD map_status_t MapTopologyEdit_ExtendEdgesTransformed( map_document_t *pMap, common::u64 meshId,
    map_edge_refs_t edges, const math::affine3d_t &transform,
    common::vector_t<geometry::mesh_edge_ref_t> *pOuterEdgesOut = nullptr ) noexcept;

// Moves one brush face by `offset` (TrenchBroom/Hammer face drag on a convex
// brush): its corners move and the brush is rebuilt convex.
// Vertex clumping: the corner at `from` moves to `to` in every listed brush
// that has it, so joined brushes stay sealed; all or none move.
// *pMovedOut (optional) counts the brushes that had the corner.
CYPHER_NODISCARD map_status_t MapTopologyEdit_MoveBrushVertexClump( map_document_t *pMap, common::span_t<const common::u64> brushes,
    math::vec3d_t from, math::vec3d_t to, common::u32 *pMovedOut ) noexcept;

CYPHER_NODISCARD map_status_t MapTopologyEdit_MoveBrushFace( map_document_t *pMap, common::u64 brushId, common::u64 sideId,
    math::vec3d_t offset ) noexcept;

// Cuts new edges through the mesh along a path of knife points (vertices,
// points on edges, points inside faces), in order (Knife tool).
CYPHER_NODISCARD map_status_t MapTopologyEdit_Knife( map_document_t *pMap, common::u64 meshId,
    common::span_t<const geometry::mesh_edit_knife_point_t> path ) noexcept;

// Inserts an edge loop across the ring through `edge`, at fraction t along
// it (Loop Cut tool).
CYPHER_NODISCARD map_status_t MapTopologyEdit_LoopCut( map_document_t *pMap, common::u64 meshId, geometry::mesh_edge_ref_t edge,
    common::f64 t ) noexcept;

// Drags one brush vertex (found at `from`) to `to`, rebuilding the convex
// brush as TrenchBroom does. Side materials and UVs follow the closest
// original planes; sides the rebuild invents get fresh IDs.
CYPHER_NODISCARD map_status_t MapTopologyEdit_MoveBrushVertex( map_document_t *pMap, common::u64 brushId, math::vec3d_t from,
    math::vec3d_t to ) noexcept;

// Moves the brush vertices found at each of `from` by one offset and rebuilds
// the convex brush once (a 2D view's column of stacked corners). All or none.
CYPHER_NODISCARD map_status_t MapTopologyEdit_MoveBrushVertices( map_document_t *pMap, common::u64 brushId,
    common::span_t<const math::vec3d_t> from, math::vec3d_t offset ) noexcept;

/*
================
Faces
================
*/

// Merges a face's corners into one vertex at their average (face Collapse).
CYPHER_NODISCARD map_status_t MapTopologyEdit_CollapseFace( map_document_t *pMap, common::u64 meshId, common::u64 faceId ) noexcept;

// Disconnects a face from its neighbours within the same mesh, giving it its
// own vertices (face Detach/Split). Identity and surfacing stay.
CYPHER_NODISCARD map_status_t MapTopologyEdit_DetachFace( map_document_t *pMap, common::u64 meshId, common::u64 faceId ) noexcept;
// The same for several faces: they leave their neighbours together, staying
// joined to each other.
CYPHER_NODISCARD map_status_t MapTopologyEdit_DetachFaces( map_document_t *pMap, common::u64 meshId,
    common::span_t<const common::u64> faceIds ) noexcept;

/*
================
Whole meshes and objects
================
*/

// Relaxes every vertex toward the average of its neighbours.
CYPHER_NODISCARD map_status_t MapTopologyEdit_Smooth( map_document_t *pMap, common::span_t<const common::u64> meshes,
    common::f64 factor, common::u32 cIterations ) noexcept;

// Snaps every vertex of each mesh to the grid.
CYPHER_NODISCARD map_status_t MapTopologyEdit_SnapMeshes( map_document_t *pMap, common::span_t<const common::u64> meshes,
    common::f64 gridSize ) noexcept;

// Cuts each mesh along a plane (unit normal). BOTH keeps everything and only
// splits the crossed faces; FRONT keeps n . p + d > 0, BACK the other side,
// and both close the opening with a cap. The plane's faces inherit the
// material of the faces they split.
enum class map_slice_keep_t : common::u8 { BOTH, FRONT, BACK };
CYPHER_NODISCARD map_status_t MapTopologyEdit_SliceMeshes( map_document_t *pMap, common::span_t<const common::u64> meshes,
    math::planed_t plane, map_slice_keep_t keep ) noexcept;

// UV seams: edges where the texture layout may tear (a future UV editor
// cuts islands there). SET, CLEAR or TOGGLE on the listed edges.
enum class map_seam_edit_t : common::u8 { SET = 0u, CLEAR, TOGGLE };
CYPHER_NODISCARD map_status_t MapTopologyEdit_EditSeams( map_document_t *pMap, common::u64 meshId, map_edge_refs_t edges, map_seam_edit_t edit ) noexcept;

// The UV island holding `faceId`: faces joined across edges that are not
// seams and whose corner UVs agree. faceIdsOut receives them ascending.
CYPHER_NODISCARD map_status_t MapTopologyEdit_UvIsland( const map_document_t *pMap, common::u64 meshId, common::u64 faceId,
    std::vector<common::u64> &faceIdsOut, common::usize *pIslandCountOut ) noexcept;

// Repair Mesh: weld vertices within `weldDistance`, drop degenerate and
// duplicate faces, make the winding agree (closed shells outward) and cap
// flat holes. The mesh is rebuilt from its polygons, so its faces take the
// given material and world-aligned UVs. *pReportOut (optional) says what
// changed: welded, removed, flipped, holes filled and left open.
struct map_repair_report_t {
    common::u32 welded{ 0u }, degenerate{ 0u }, duplicates{ 0u }, flipped{ 0u }, holesFilled{ 0u }, holesOpen{ 0u }, nonManifoldEdges{ 0u };
};
CYPHER_NODISCARD map_status_t MapTopologyEdit_Repair( map_document_t *pMap, common::u64 meshId, common::f64 weldDistance,
    common::string_view_t material, common::f64 worldUnitsPerUv, map_repair_report_t *pReportOut, bool bFillHoles = true ) noexcept;
// What Repair would find, without changing anything (Check for Problems):
// vertices at the same place but not joined, degenerate and duplicate
// faces, faces wound against their neighbours, non-manifold edges. Open
// borders are not problems here (ramps and sheets are open on purpose).
CYPHER_NODISCARD map_status_t MapTopologyEdit_Inspect( const map_document_t *pMap, common::u64 meshId, map_repair_report_t *pReportOut ) noexcept;

// Hammer 5's face operations on a mesh's selected faces:
// - ExtractFaces copies them into a new mesh object (the originals stay);
//   *pNewIdOut receives its ID.
// - ThickenFaces extracts them and gives the copy `thickness` (Solidify),
//   a new closed slab; the original mesh is untouched.
// - CombineFaces merges each group of adjacent selected faces into one face
//   (their shared edges are dissolved); fewer than two adjacent faces is
//   rejected.
// - RemoveBadFaces collapses faces whose area is below `areaTolerance`;
//   *pRemovedOut counts them (none: a successful no-op).
CYPHER_NODISCARD map_status_t MapTopologyEdit_ExtractFaces( map_document_t *pMap, common::u64 meshId,
    common::span_t<const common::u64> faceIds, common::u64 *pNewIdOut ) noexcept;
CYPHER_NODISCARD map_status_t MapTopologyEdit_ThickenFaces( map_document_t *pMap, common::u64 meshId,
    common::span_t<const common::u64> faceIds, common::f64 thickness, common::u64 *pNewIdOut ) noexcept;
CYPHER_NODISCARD map_status_t MapTopologyEdit_CombineFaces( map_document_t *pMap, common::u64 meshId,
    common::span_t<const common::u64> faceIds ) noexcept;
CYPHER_NODISCARD map_status_t MapTopologyEdit_RemoveBadFaces( map_document_t *pMap, common::span_t<const common::u64> meshes,
    common::f64 areaTolerance, common::usize *pRemovedOut ) noexcept;

// The Clip tool on meshes, matching brush clipping (TrenchBroom, Hammer):
// FRONT keeps n . p + d >= 0 and BACK the other side. Closed input meshes
// receive caps; open modelling surfaces retain their open boundary. A mesh
// wholly on the discarded side is deleted, one wholly kept is untouched.
// BOTH splits every crossed mesh into two meshes (the
// back half is a new object with fresh IDs). pIdsOut (optional,
// initialized) receives the meshes that remain, for the selection.
// cap false (Hammer's "Cap new surfaces" off) leaves the cut open.
CYPHER_NODISCARD map_status_t MapTopologyEdit_ClipMeshes( map_document_t *pMap, common::span_t<const common::u64> meshes,
    math::planed_t plane, map_slice_keep_t keep, common::vector_t<common::u64> *pIdsOut, bool cap = true ) noexcept;

// What the Clip tool will leave of the given meshes of pSource, added to
// pPreview's geometry for presentation only, using the same clipping and
// cap policy as commit. BOTH produces both independent halves, including
// caps for closed inputs. Meshes the clip removes are skipped. IDs only
// need to be unique within pPreview.
CYPHER_NODISCARD map_status_t MapTopologyEdit_AddClipPreview( map_document_t *pPreview, const map_document_t *pSource,
    common::span_t<const common::u64> meshes, math::planed_t plane, map_slice_keep_t keep, bool cap = true ) noexcept;

// Replaces each mesh by one level of Catmull-Clark subdivision (open
// boundaries keep their corners). Materials, smoothing groups and corner
// UVs are interpolated per source face; original vertices keep their IDs,
// every face and new vertex gets a fresh one. A mesh whose record carries
// per-face data is rejected: its faces are all replaced, so that data
// would have no face left to belong to.
CYPHER_NODISCARD map_status_t MapTopologyEdit_Subdivide( map_document_t *pMap, common::span_t<const common::u64> meshes ) noexcept;
CYPHER_NODISCARD common::bool_t MapTopologyEdit_HasFaceRecords( map_document_t *pMap, common::u64 meshId ) noexcept;

// Thickens open meshes (a quad, a terrain cut-out) into closed solids. The
// original surface keeps its IDs; the offset layer and rim get fresh ones.
// Closed meshes are rejected: there is no inside to grow toward.
CYPHER_NODISCARD map_status_t MapTopologyEdit_Solidify( map_document_t *pMap, common::span_t<const common::u64> meshes,
    common::f64 thickness ) noexcept;

// Joins meshes into one object (Hammer's Merge, M, on whole meshes). The
// survivor is the lowest-ID mesh with an authored record, else the lowest
// ID: its root, layer, owner and metadata stay, the others' roots go. Every
// vertex and face identity and attribute survives exactly; nothing is
// welded. Donor face records move into the survivor's record. All meshes
// must share one owner. A retained nonempty or malformed modifier member on any
// input refuses with INVALID_ARGUMENT before mutation, including disabled and
// unknown stages; joining does not bake stacks. Missing/empty stacks are allowed.
// *pRetainedOut receives the survivor only on success.
CYPHER_NODISCARD map_status_t MapTopologyEdit_JoinMeshes( map_document_t *pMap, common::span_t<const common::u64> meshes,
    common::u64 *pRetainedOut ) noexcept;

// Separates each disconnected shell of a mesh into its own object (Hammer's
// Separate, Alt+N, on whole meshes). The shell holding the lowest face ID
// keeps the root; the others get fresh roots in the same layer and owner,
// with their face records. pRootsOut (initialized) receives the source and
// the new roots. A one-shell mesh has nothing to separate.
CYPHER_NODISCARD map_status_t MapTopologyEdit_SeparateShells( map_document_t *pMap, common::u64 meshId,
    common::vector_t<common::u64> *pRootsOut ) noexcept;

// Converts closed convex meshes back to brushes (Convert to Brush). Each
// face plane becomes a side keeping the face's material; its UV projection is
// fitted to the face's corner UVs (texture lock style), or world-aligned when
// the corners cannot be fitted. Root, layer, owner and metadata stay. A
// concave or open mesh, or one whose record carries per-face data, is
// rejected unchanged.
CYPHER_NODISCARD map_status_t MapTopologyEdit_MeshesToBrushes( map_document_t *pMap, common::span_t<const common::u64> meshes ) noexcept;

// Mesh Booleans on closed meshes, convex or not (the geometry library's
// exact mesh CSG):
//   UNION      merges them all into the lowest-ID mesh; the others go;
//   SUBTRACT   cuts the newest mesh (highest ID) out of the other one and
//              removes it;
//   INTERSECT  keeps what the two share and removes the other;
//   EXCLUSIVE  keeps what only one of the two covers (symmetric difference);
//   SPLIT      cuts the older mesh along the newer one's surface into the
//              part inside it and the part outside (a new mesh); the newer
//              mesh stays.
// Materials and UVs are carried through. pRootsOut (initialized) receives
// the result roots.
enum class map_boolean_t : common::u8 { UNION = 0u, SUBTRACT, INTERSECT, EXCLUSIVE, SPLIT };
CYPHER_NODISCARD map_status_t MapTopologyEdit_Boolean( map_document_t *pMap, common::span_t<const common::u64> meshes,
    map_boolean_t op, common::vector_t<common::u64> *pRootsOut ) noexcept;

// Reflects brushes, meshes and patches across a plane (unit normal). Every
// identity survives: brushes keep their sides, meshes their faces with
// winding reversed so they still face out. Texture lock mirrors brush UVs
// with the geometry. Entities and terrain are rejected: their orientation
// properties and height grids cannot be reflected faithfully.
CYPHER_NODISCARD map_status_t MapTopologyEdit_Mirror( map_document_t *pMap, common::span_t<const common::u64> ids,
    math::planed_t plane, bool textureLock ) noexcept;

// Mesh cleanup on whole meshes. Weld joins vertices closer than `tolerance`
// that share an edge (collapsing short edges); Merge by Distance joins any
// vertices that close, such as the two sides of a seam. Meshes with nothing
// to join keep their revision. *pMergedOut (optional) receives how many
// vertices were removed in total.
CYPHER_NODISCARD map_status_t MapTopologyEdit_Weld( map_document_t *pMap, common::span_t<const common::u64> meshes,
    common::f64 tolerance, common::usize *pMergedOut ) noexcept;
CYPHER_NODISCARD map_status_t MapTopologyEdit_MergeByDistance( map_document_t *pMap, common::span_t<const common::u64> meshes,
    common::f64 tolerance, common::usize *pMergedOut ) noexcept;

// A component set of one mesh: faces if any are given, else edges, else
// vertices.
struct map_mesh_components_t {
    map_vertex_ids_t vertices{};
    map_edge_refs_t edges{};
    common::span_t<const geometry::geometry_source_id_t> faces{};
};

// Moves the components' vertices `distance` along their normals (Offset):
// the selected faces' normals in face mode, all faces around each vertex
// otherwise. Positive is outward.
CYPHER_NODISCARD map_status_t MapTopologyEdit_OffsetComponents( map_document_t *pMap, common::u64 meshId,
    const map_mesh_components_t &components, common::f64 distance ) noexcept;

// Hammer's Flatten: projects the components' vertices onto their best-fit
// plane, the plane of their average normal, or one world-axis level.
// A flatten that would fold or collapse a face changes nothing.
enum class map_flatten_t : common::u8 { BEST_FIT = 0u, AVERAGE_NORMAL, AXIS_X, AXIS_Y, AXIS_Z };
CYPHER_NODISCARD map_status_t MapTopologyEdit_FlattenComponents( map_document_t *pMap, common::u64 meshId,
    const map_mesh_components_t &components, map_flatten_t plane ) noexcept;

// Slides the closed edge loop through edge a -> b toward one of its
// neighbouring loops: factor in (-1, 1), positive toward the face on the
// directed edge's left. Topology, UVs and IDs are unchanged.
CYPHER_NODISCARD map_status_t MapTopologyEdit_SlideEdgeLoop( map_document_t *pMap, common::u64 meshId,
    geometry::mesh_edge_ref_t directed, common::f64 factor ) noexcept;

// Deletes mesh faces, opening the mesh; edges and vertices no remaining face
// uses go too. Deleting every face is rejected.
CYPHER_NODISCARD map_status_t MapTopologyEdit_DeleteFaces( map_document_t *pMap, common::u64 meshId,
    common::span_t<const common::u64> faceIds ) noexcept;

// Joins two faces of one mesh with a tube of cSegments rings, removing both.
CYPHER_NODISCARD map_status_t MapTopologyEdit_BridgeFaces( map_document_t *pMap, common::u64 meshId, common::u64 faceA,
    common::u64 faceB, common::u32 cSegments ) noexcept;

// Extrudes a brush side into a separate brush (TrenchBroom's split extrude):
// outward the original stays and the new brush is the slab the face would
// sweep; inward the original keeps the inner part and the slab next to the
// face becomes the new brush. Its sides carry on the original surfaces. The
// new brush takes the original's layer and owner; *pNewOut receives it.
CYPHER_NODISCARD map_status_t MapTopologyEdit_ExtrudeBrushFaceToNew( map_document_t *pMap, common::u64 brushId, common::u64 sideId,
    common::f64 distance, common::u64 *pNewOut ) noexcept;

// Hammer 5's Mesh Projection: each vertex of `vertices` (every vertex of the
// mesh when empty) moves along `direction` onto the first surface of
// another object it meets, stopping `offset` short of it; vertices that
// meet nothing stay. *pMovedOut counts the vertices that moved.
CYPHER_NODISCARD map_status_t MapTopologyEdit_Project( map_document_t *pMap, common::u64 meshId, map_vertex_ids_t vertices, math::vec3d_t direction,
    common::f64 offset, common::usize *pMovedOut ) noexcept;

// Moves each vertex to its own position (positions parallel to vertices):
// the Edge Arc tool's result. Faces stay as authored.
CYPHER_NODISCARD map_status_t MapTopologyEdit_SetVertexPositions( map_document_t *pMap, common::u64 meshId, map_vertex_ids_t vertices,
    common::span_t<const math::vec3d_t> positions ) noexcept;

// Hammer's Snap Edge to Edge: welds the source edge onto the target edge,
// each source end merging into the nearer-pairing target end (which keeps its
// position). An end the two share stays.
CYPHER_NODISCARD map_status_t MapTopologyEdit_SnapEdgeToEdge( map_document_t *pMap, common::u64 meshId,
    geometry::mesh_edge_ref_t source, geometry::mesh_edge_ref_t target ) noexcept;

// Joins two open border chains of one mesh with a strip of cSegments rows:
// each path lists the vertices along its chain (v0..vm, m edges, either
// direction); both have the same number of edges and share no vertex.
CYPHER_NODISCARD map_status_t MapTopologyEdit_BridgeEdgeChains( map_document_t *pMap, common::u64 meshId, map_vertex_ids_t pathA,
    map_vertex_ids_t pathB, common::u32 cSegments ) noexcept;

// NetRadiant's Make Room: hollows convex brushes with the walls outside, so
// the space inside keeps the brush's size. pRootsOut (initialized) receives
// the wall brushes.
CYPHER_NODISCARD map_status_t MapTopologyEdit_MakeRoom( map_document_t *pMap, common::span_t<const common::u64> brushes,
    common::f64 thickness, common::vector_t<common::u64> *pRootsOut ) noexcept;

// Removes interior vertices, each turning the faces around it into one
// face (Blender's Dissolve Vertices). Border vertices are rejected.
CYPHER_NODISCARD map_status_t MapTopologyEdit_DissolveVertices( map_document_t *pMap, common::u64 meshId, map_vertex_ids_t vertices ) noexcept;

// Chamfers interior vertices: each corner is cut off `width` along its
// edges (at most 45% of an edge) and capped with one face.
CYPHER_NODISCARD map_status_t MapTopologyEdit_BevelVertices( map_document_t *pMap, common::u64 meshId, map_vertex_ids_t vertices,
    common::f64 width ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_TOPOLOGY_EDIT_H
