//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ToolGestures.cpp
//  Purpose: Implements the viewport gestures of the creation and modelling
//           tools over the map edit adapters.
//  Details: Picking uses the published wireframe: surfaces by casting the
//           event's ray through its faces, mesh edges and vertices by their
//           projected distance to the cursor (presentation points carry the
//           authored vertex IDs). Placement snaps to the grid unless Ctrl or
//           Command is held, as the transform tools do.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_ToolGestures.h"
#include "CypherMapGui_EntityLook.h"
#include "CypherMapGui_Instances.h"
#include "CypherMapGui_Falloff.h"
#include "CypherMapGui_Game.h"
#include "CypherMapGui_Layers.h"
#include "CypherMapGui_Notice.h"

#include "CypherMap_Edit.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_ObjectCreate.h"
#include "CypherMap_TopologyEdit.h"
#include "CypherMap_MeshEdit.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_MeshPaint.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <memory>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;
namespace
{
using vec3 = math::vec3d_t;
using id_t = geo::geometry_source_id_t;

constexpr f64 kPickPixels = 8.0;
constexpr f64 kEdgePixels = 6.0;

vec3 Add( vec3 a, vec3 b ) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
vec3 Sub( vec3 a, vec3 b ) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
vec3 Mul( vec3 a, f64 s ) noexcept { return { a.x * s, a.y * s, a.z * s }; }
f64 Dot( vec3 a, vec3 b ) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
vec3 Cross( vec3 a, vec3 b ) noexcept { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
f64 Length( vec3 a ) noexcept { return std::sqrt( Dot( a, a ) ); }
vec3 Unit( vec3 a ) noexcept { const f64 l = Length( a ); return l > 1e-12 ? Mul( a, 1.0 / l ) : vec3{}; }
f64 Comp( vec3 p, u32 axis ) noexcept { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
void SetComp( vec3 &p, u32 axis, f64 value ) noexcept { ( axis == 0 ? p.x : axis == 1 ? p.y : p.z ) = value; }
vec3 AxisVector( u32 axis, f64 value = 1.0 ) noexcept { vec3 v{}; SetComp( v, axis, value ); return v; }
u32 DepthAxis( u32 u, u32 v ) noexcept { return 3u - u - v; }
// The depth axis points toward the viewer in Top and Front (+Z, +X) and
// away from it in Side (U = X, V = Z: U x V = -Y).
f64 Outward( u32 u, u32 v ) noexcept { return u == 0 && v == 2 ? -1.0 : 1.0; }

struct surface_hit_t {
    u64 object{ 0 };
    u64 sideId{ 0 }, faceId{ 0 }, material{ 0 };
    map_wire_kind_t kind{ map_wire_kind_t::BRUSH };
    vec3 point{}, normal{};
};

using edge_hit_t = map_tool_edge_hit_t;
using edge_result_t = map_tool_edge_query_result_t;

// A mesh a displacement stroke reaches: its presentation points as they
// were when the stroke started, which points share an edge, and its faces
// in the wireframe (to refresh their normals as the surface moves).
struct sculpt_mesh_t {
    u64 id{ 0 };
    std::vector<vec3> original;
    std::vector<std::vector<u32>> neighbours;
    std::vector<u32> faces;
};

// The live document and its presentation stay untouched throughout a stroke.
// Preparation is replayed on a fresh edit copy at release; this copy supplies
// only preview geometry and stable source identities, never published metadata.
struct sculpt_stroke_t {
    map_document_t *document{ nullptr };
    map_wireframe_t wire{};
    const map_document_t *source{ nullptr };
    const void *view{ nullptr };
    u64 geometryRevision{}, selectionRevision{}, nextId{}, sourceNextId{};
    u64 preparedObject{}, preparedFace{};
    bool convert{ false };
    u32 cells{ 1u }, dabs{ 0u };
    f64 radius{ 64.0 }, step{}, smooth{};
    enum class mode_t { PUSH_PULL, SMOOTH, FLATTEN, INFLATE, PINCH, NOISE } mode{ mode_t::PUSH_PULL };
    enum class normal_t { AVERAGE, VIEW, Z, FACE } normal{ normal_t::AVERAGE };
    map_falloff_t falloff{};
    std::vector<u64> targets;
    std::vector<sculpt_mesh_t> meshes;
    ~sculpt_stroke_t() { MapWireframe_Shutdown( &wire ); delete document; }
};

// A provisional brush conversion is only a picking source. Confirmation
// converts a fresh copy of the current map; this snapshot is never published.
struct knife_candidate_t {
    map_document_t *document{ nullptr };
    geo::mesh_source_description_t description{};
    ~knife_candidate_t() { geo::MeshSourceDescription_Shutdown( &description ); delete document; }
};

// A value recipe, not recorded source component IDs. Extension repeats on
// the current rim; the affine already includes the original absolute pivot.
struct modelling_repeat_t {
    bool present{ false }, current{ false };
    map_component_transform_t kind{ map_component_transform_t::TRANSLATE };
    map_element_mode_t mode{ map_element_mode_t::OBJECTS };
    u64 object{ 0 }, geometryRevision{ 0 };
    const map_document_t *document{ nullptr };
    math::affine3d_t affine{ math::CY_AFFINE3D_IDENTITY };
    vec3 delta{};
    bool extrude{ false }, brush{ false };
};

// Staged input of the active tool, plus workspace pivot/workplane/repeat.
struct state_t {
    modelling_repeat_t repeat{};
    map_tool_t tool{ map_tool_t::COUNT };
    bool bDrag{ false };
    vec3 dragA{}, dragB{};
    f64 dragBase{ 0.0 }; // 3D Patch: height of the surface the rectangle is drawn on.
    std::vector<vec3> points;
    bool bPointsOrtho{ false };
    u32 pointsU{ 0 }, pointsV{ 1 };
    bool bPlane{ false };
    math::planed_t plane{};
    std::vector<geo::mesh_edit_knife_point_t> knife;
    u64 knifeMesh{ 0 };
    std::shared_ptr<knife_candidate_t> knifeCandidate;
    const map_document_t *knifeDocument{ nullptr };
    u64 knifeGeometryRevision{ 0 }, knifeSelectionRevision{ 0 }, knifeNextId{ 0 }, knifeSourceNextId{ 0 };
    bool bHover{ false };
    bool bHoverBlocked{ false }; // Knife: the hovered point cannot be reached from the last one.
    vec3 hover{};
    bool bLoop{ false };
    edge_hit_t loop{};
    bool bVertex{ false };
    u64 vertexObject{ 0 };
    bool bVertexMesh{ false };
    vec3 vertexFrom{}, vertexTo{};
    std::vector<id_t> vertexIds;
    std::vector<vec3> vertexColumn; // Vertex tool, 2D: the brush corners stacked under the grabbed one.
    u64 lastCorner{ 0 };
    u64 paintObject{ 0 };
    map_wire_kind_t paintKind{ map_wire_kind_t::MESH };
    const void *paintView{ nullptr };
    const map_document_t *paintDocument{ nullptr };
    u64 paintSelectionRevision{ 0 }, paintGeometryRevision{ 0 };
    f64 paintRadius{ 32.0 }, paintStrength{ 1.0 };
    u8 paintChannel{ 0 };
    QColor paintColor{};
    std::vector<vec3> dabs;
    // Terrain sculpting: the stroke works on a private copy of the field
    // (shared so that copies of this state stay cheap), and records its dabs
    // to replay on the map at release.
    struct field_copy_t {
        geo::heightfield_t field{};
        ~field_copy_t() { geo::HeightField_Shutdown( &field ); }
    };
    bool bTerrain{ false }, bTerrainHover{ false };
    u64 terrainId{ 0 };
    std::shared_ptr<field_copy_t> terrainWork;
    std::vector<geo::heightfield_brush_t> terrainDabs;
    vec3 terrainLast{}, terrainHover{};
    f64 terrainTarget{ 0.0 };
    // Extrude tool on mesh faces: push-pull along the pressed face's normal.
    bool bExtrude{ false };
    const void *extrudeView{ nullptr };
    const map_document_t *extrudeDocument{ nullptr };
    u64 extrudeSelectionRevision{ 0 }, extrudeGeometryRevision{ 0 };
    u64 extrudeMesh{ 0 };
    std::vector<u64> extrudeFaces;
    std::vector<id_t> extrudeVertices; // Unique vertices of a captured coplanar region.
    std::vector<std::pair<vec3, vec3>> extrudeBoundary;
    vec3 extrudeFrom{}, extrudeNormal{};
    f64 extrudeDistance{ 0.0 };
    bool bPivot{ false };
    vec3 pivot{};
    bool bWorkplane{ false };
    math::planed_t workplane{};
    vec3 workplanePoint{};
    // Component gizmo drag.
    bool bGizmo{ false }, bGizmoExtrude{ false };
    const void *gizmoView{ nullptr };
    const map_document_t *gizmoDocument{ nullptr };
    u64 gizmoSelectionRevision{ 0 }, gizmoGeometryRevision{ 0 };
    u64 gizmoObject{ 0 };
    map_element_mode_t gizmoMode{ map_element_mode_t::OBJECTS };
    int gizmoAxis{ -1 };          // -1: free (plane) drag, else world axis.
    QPointF gizmoPress{};
    vec3 gizmoPressRayOrigin{}, gizmoPressRayDirection{}, gizmoPressHit{};
    f64 gizmoT0{ 0.0 };
    math::planed_t gizmoScalePlane{};
    f64 gizmoScaleReference{ 0.0 };
    math::affine3d_t gizmoAffine{ math::CY_AFFINE3D_IDENTITY };
    vec3 gizmoDelta{};
    int gizmoHover{ -2 };         // -2: none, -1: centre, 0..2: axis.
    // Component drag (Select vertices/edges, or Translate edges): TrenchBroom's
    // vertex and edge tools. The grabbed handle's start, the vertices that
    // move with it, and the current snapped offset.
    bool bComponentDrag{ false }, bComponentMoved{ false }, bComponentExtend{ false };
    const map_document_t *dragDocument{ nullptr };
    u64 dragSelectionRevision{ 0 }, dragGeometryRevision{ 0 };
    u64 dragMesh{ 0 };
    id_t dragGrabbed{};
    id_t dragSnapVertex{}; // Stationary authored handle acquired in screen space.
    std::vector<id_t> dragVertices;
    std::vector<geo::mesh_edge_ref_t> dragEdges;
    std::vector<vec3> dragOrigins;
    // A press on a handle waits for the mouse to move before it drags: the
    // view's own click picks and selects first (occlusion, Shift/Ctrl), and
    // a plain click stays a click. The selection from before the press comes
    // back when the drag starts on one of its handles, so a multi-selection
    // moves together (a click without moving keeps just the clicked one).
    bool bGrabPending{ false }, bGrabHit{ false };
    const void *grabView{ nullptr };
    QPointF grabPress{};
    Qt::KeyboardModifiers grabModifiers{};
    u64 grabMesh{ 0 };
    vec3 grabPoint{};
    bool grabBrush{ false };
    id_t grabVertex{};
    geo::mesh_edge_ref_t grabEdge{};
    u64 priorMesh{ 0 };
    std::vector<id_t> priorVertices;
    std::vector<geo::mesh_edge_ref_t> priorEdges;
    vec3 dragGrab{}, dragDelta{};
    QPointF dragPress{};
    // Box selection of components, in the view it started in.
    bool bLasso{ false };
    const void *lassoView{ nullptr };
    QPointF lassoA{}, lassoB{};
    Qt::KeyboardModifiers lassoModifiers{};
    // Clip on a surface in 3D (Hammer 5, TrenchBroom): the cut line drawn
    // across a face, its points staying on that face's plane or locked onto
    // vertices. The clip plane holds the line and the face normal; a third
    // point tilts it through all three.
    bool bClipDraw{ false }, bClipPoint{ false }, bClipLine{ false }, bClipHover{ false }, bClipHoverVertex{ false };
    bool bClipThird{ false };     // Point 3 is placed: the plane runs through A, B and C.
    bool bClipPending{ false };   // A press on a staged line: a click adds point 3, a drag starts a new line.
    int clipEndpoint{ -1 };       // The staged point being dragged (0..2).
    vec3 clipA{}, clipB{}, clipC{}, clipHover{}, clipPress{};
    QPointF clipPressScreen{};
    math::planed_t clipFace{};    // The face plane points 1 and 2 lie on.
    math::planed_t clipFaceC{};   // Point 3's face plane.
    math::planed_t clipPressFace{};
    math::planed_t clipStaged{};  // The plane staged from them.
    // Displacement stroke: the surface moves in the views while painting
    // and lands in the map as one step on release.
    bool bSculpt{ false }, bSculptHover{ false };
    std::shared_ptr<sculpt_stroke_t> sculptWork;
    vec3 sculptLast{}, sculptHover{}, sculptNormal{};
    u32 sculptDabs{ 0 };
    // Edge Arc drag: the bulge, from the press point on the arc's plane.
    bool bArc{ false };
    const void *arcView{ nullptr };
    const map_document_t *arcDocument{ nullptr };
    u64 arcGeometryRevision{ 0 }, arcSelectionRevision{ 0 }, arcMesh{ 0 };
    map_element_mode_t arcMode{ map_element_mode_t::EDGES };
    std::vector<id_t> arcOrder;
    std::vector<vec3> arcChain;
    vec3 arcPress{}, arcNormal{}, arcBulge{};
    // Shear: the side of the selection's bounds under the cursor or being
    // dragged (its axis and which end), the bounds at the press, and how far
    // the side has slid from where it was grabbed.
    bool bShear{ false }, bShearHover{ false };
    const void *shearView{ nullptr };
    const map_document_t *shearDocument{ nullptr };
    u64 shearGeometryRevision{ 0 }, shearSelectionRevision{ 0 };
    map_element_mode_t shearMode{ map_element_mode_t::OBJECTS };
    std::vector<u64> shearRoots, shearAffected;
    bool shearTextureLock{ false };
    u32 shearAxis{ 0 };
    int shearSign{ 1 };
    map_bounds_t shearBounds{};
    vec3 shearFrom{}, shearSlide{};
};

std::unordered_map<const map_workspace_t *, state_t> g_states;

state_t &State( const map_workspace_t *ws )
{
    state_t &state = g_states[ws];
    if ( state.tool != ws->tool ) {
        // A tool switch drops staged input; pivot, workplane and repeat stay.
        state_t fresh{};
        fresh.repeat = state.repeat;
        fresh.bPivot = state.bPivot; fresh.pivot = state.pivot;
        fresh.bWorkplane = state.bWorkplane; fresh.workplane = state.workplane; fresh.workplanePoint = state.workplanePoint;
        fresh.tool = ws->tool;
        state = fresh;
    }
    return state;
}

bool Writable( const map_workspace_t *ws ) noexcept
{
    return ws != nullptr && ws->pGui != nullptr && ws->pDocument != nullptr && !ws->pDocument->bReadOnly &&
        !ws->editPreview.bActive && !EditorHistory_IsTransactionOpen( &ws->history );
}

bool SnapBypassed( Qt::KeyboardModifiers modifiers ) noexcept { return ( modifiers & ( Qt::ControlModifier | Qt::MetaModifier ) ) != 0; }

f64 Snap( const map_workspace_t *ws, f64 value, Qt::KeyboardModifiers modifiers ) noexcept
{
    if ( !ws->bSnapToGrid || SnapBypassed( modifiers ) || ws->gridSize <= 0.0 ) { return value; }
    return std::round( value / ws->gridSize ) * ws->gridSize;
}

vec3 SnapAll( const map_workspace_t *ws, vec3 p, Qt::KeyboardModifiers modifiers ) noexcept
{
    return { Snap( ws, p.x, modifiers ), Snap( ws, p.y, modifiers ), Snap( ws, p.z, modifiers ) };
}

// Snaps the two coordinates a plane leaves free when the plane is axial;
// a sloped plane keeps the exact point so it stays on the plane.
vec3 SnapOnPlane( const map_workspace_t *ws, vec3 p, vec3 normal, Qt::KeyboardModifiers modifiers ) noexcept
{
    for ( u32 axis = 0; axis < 3; ++axis ) {
        if ( std::fabs( Comp( normal, axis ) ) > 0.999999 ) {
            vec3 snapped = SnapAll( ws, p, modifiers );
            SetComp( snapped, axis, Comp( p, axis ) );
            return snapped;
        }
    }
    return p;
}

// The cursor on a 2D pane's plane at a chosen depth.
vec3 OrthoPoint( const map_tool_event_t &e, f64 depth ) noexcept
{
    vec3 p = e.rayOrigin;
    SetComp( p, DepthAxis( e.view.axisU, e.view.axisV ), depth );
    return p;
}

vec3 SnapUV( const map_workspace_t *ws, vec3 p, u32 u, u32 v, Qt::KeyboardModifiers modifiers ) noexcept
{
    SetComp( p, u, Snap( ws, Comp( p, u ), modifiers ) );
    SetComp( p, v, Snap( ws, Comp( p, v ), modifiers ) );
    return p;
}

bool RayPlane( vec3 origin, vec3 direction, const math::planed_t &plane, vec3 &out ) noexcept
{
    const f64 denominator = Dot( plane.normal, direction );
    if ( std::fabs( denominator ) < 1e-12 ) { return false; }
    const f64 t = -( Dot( plane.normal, origin ) + plane.d ) / denominator;
    if ( t < 0.0 || !std::isfinite( t ) ) { return false; }
    out = Add( origin, Mul( direction, t ) );
    return true;
}

// The nearest visible face the ray meets.
bool SurfaceHit( const map_workspace_t *ws, const map_tool_event_t &e, surface_hit_t &out, bool meshesOnly = false,
    const map_wireframe_t *pWire = nullptr ) noexcept
{
    const auto &wire = pWire != nullptr ? *pWire : ws->wire;
    f64 nearest = 1e300;
    bool found = false;
    for ( usize f = 0; f < wire.faces.nCount; ++f ) {
        const auto &face = wire.faces.pData[f];
        if ( face.nIndices < 3 ) { continue; }
        const auto *object = MapWireframe_FindObject( wire, face.id );
        if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) || ( meshesOnly && object->kind != map_wire_kind_t::MESH ) ) { continue; }
        const vec3 p0 = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex]];
        const f64 denominator = Dot( face.normal, e.rayDirection );
        if ( std::fabs( denominator ) < 1e-12 ) { continue; }
        const f64 t = Dot( face.normal, Sub( p0, e.rayOrigin ) ) / denominator;
        // A mesh lying on a brush face (a displacement grid on its base)
        // wins the tie: it is what the user sees and edits.
        const f64 tie = 1e-6 * std::max( 1.0, std::fabs( t ) );
        const bool better = t < nearest - tie || ( t <= nearest + tie && found && object->kind == map_wire_kind_t::MESH && out.kind != map_wire_kind_t::MESH );
        if ( t <= 0.0 || !better ) { continue; }
        const vec3 hit = Add( e.rayOrigin, Mul( e.rayDirection, t ) );
        // Point in polygon on the plane the normal is most perpendicular to.
        const f64 ax = std::fabs( face.normal.x ), ay = std::fabs( face.normal.y ), az = std::fabs( face.normal.z );
        const u32 drop = az >= ax && az >= ay ? 2u : ax >= ay ? 0u : 1u;
        const u32 i0 = drop == 0 ? 1u : 0u, i1 = drop == 2 ? 1u : 2u;
        bool inside = false;
        for ( u32 k = 0, j = face.nIndices - 1; k < face.nIndices; j = k++ ) {
            const vec3 a = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]];
            const vec3 b = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + j]];
            const f64 ay0 = Comp( a, i1 ), by0 = Comp( b, i1 );
            if ( ( ay0 > Comp( hit, i1 ) ) != ( by0 > Comp( hit, i1 ) ) &&
                 Comp( hit, i0 ) < ( Comp( b, i0 ) - Comp( a, i0 ) ) * ( Comp( hit, i1 ) - ay0 ) / ( by0 - ay0 ) + Comp( a, i0 ) ) { inside = !inside; }
        }
        if ( !inside ) { continue; }
        nearest = t; found = true;
        out.object = face.id; out.sideId = face.sideId; out.faceId = face.faceId; out.material = face.material;
        out.kind = object->kind; out.point = hit; out.normal = face.normal;
        // On an axis-aligned face take the plane's own coordinate instead of
        // the ray's rounding noise, so things placed on a top at z 128 land
        // at 128 and not 128.00000000000006 (which the map file would keep).
        for ( u32 axis = 0; axis < 3; ++axis ) {
            if ( std::fabs( Comp( face.normal, axis ) ) > 0.999999 ) { SetComp( out.point, axis, Comp( p0, axis ) ); }
        }
    }
    return found;
}

// Placement point: the 2D cursor at depth 0, or the 3D surface (the ground
// plane where there is none); snapped.
// The Workplane tool's plane, when one is set: 3D placement lands on it.
bool WorkplanePoint( const map_workspace_t *ws, const map_tool_event_t &e, vec3 &out ) noexcept
{
    const auto found = g_states.find( ws );
    if ( found == g_states.end() || !found->second.bWorkplane ) { return false; }
    const auto &plane = found->second.workplane;
    if ( !RayPlane( e.rayOrigin, e.rayDirection, plane, out ) ) { return false; }
    out = SnapOnPlane( ws, out, plane.normal, e.modifiers );
    return true;
}

bool PlacePoint( const map_workspace_t *ws, const map_tool_event_t &e, vec3 &out ) noexcept
{
    if ( e.view.bOrtho ) {
        out = SnapUV( ws, OrthoPoint( e, 0.0 ), e.view.axisU, e.view.axisV, e.modifiers );
        return true;
    }
    if ( WorkplanePoint( ws, e, out ) ) { return true; }
    surface_hit_t hit{};
    if ( SurfaceHit( ws, e, hit ) ) { out = SnapOnPlane( ws, hit.point, hit.normal, e.modifiers ); return true; }
    if ( RayPlane( e.rayOrigin, e.rayDirection, { { 0, 0, 1 }, 0.0 }, out ) ) { out = SnapOnPlane( ws, out, { 0, 0, 1 }, e.modifiers ); return true; }
    return false;
}

bool Project( const map_tool_view_t &view, vec3 world, QPointF &out ) noexcept
{
    return view.pfnProject != nullptr && view.pfnProject( view.pView, world, &out ) &&
        std::isfinite( out.x() ) && std::isfinite( out.y() );
}

f64 SegmentDistance( QPointF p, QPointF a, QPointF b, f64 &t ) noexcept
{
    const QPointF ab = b - a;
    const f64 length = ab.x() * ab.x() + ab.y() * ab.y();
    t = length > 1e-12 ? std::clamp( ( ( p - a ).x() * ab.x() + ( p - a ).y() * ab.y() ) / length, 0.0, 1.0 ) : 0.0;
    const QPointF q = a + ab * t;
    return std::hypot( p.x() - q.x(), p.y() - q.y() );
}

// The authored mesh edge nearest the cursor on screen, within tolerance.
edge_result_t NearestMeshEdge( const map_workspace_t *ws, const map_tool_event_t &e, f64 tolerance, edge_hit_t &out, u64 onlyMesh = 0,
    const math::planed_t *pOnPlane = nullptr ) noexcept
{
    out = {};
    if ( e.view.pfnPickEdge != nullptr ) {
        return e.view.pfnPickEdge( e.view.pView, { e.screen, tolerance, onlyMesh, pOnPlane }, &out );
    }
    // Compatibility for injected projection-only views in tool unit tests.
    // Actual Mason panes always use their authoritative picker above.
    const auto &wire = ws->wire;
    f64 best = tolerance;
    bool found = false;
    for ( usize o = 0; o < wire.objects.nCount; ++o ) {
        const auto &object = wire.objects.pData[o];
        if ( object.kind != map_wire_kind_t::MESH || ( onlyMesh != 0 && object.id != onlyMesh ) || !MapWorkspace_IsVisible( ws, object ) ) { continue; }
        for ( u32 l = 0; l < object.nLines; ++l ) {
            const auto &line = wire.lines.pData[object.iFirstLine + l];
            const u64 ia = wire.pointSourceIds.pData[line.iA], ib = wire.pointSourceIds.pData[line.iB];
            if ( ia == 0 || ib == 0 || ia == ib ) { continue; }
            QPointF a, b;
            const vec3 pa = wire.points.pData[line.iA], pb = wire.points.pData[line.iB];
            if ( pOnPlane != nullptr && ( std::fabs( Dot( pOnPlane->normal, pa ) + pOnPlane->d ) > 1e-3 || std::fabs( Dot( pOnPlane->normal, pb ) + pOnPlane->d ) > 1e-3 ) ) { continue; }
            if ( !Project( e.view, pa, a ) || !Project( e.view, pb, b ) ) { continue; }
            f64 t = 0.0;
            const f64 distance = SegmentDistance( e.screen, a, b, t );
            if ( distance >= best ) { continue; }
            best = distance; found = true;
            out.mesh = object.id;
            out.edge = geo::MeshEdgeRef_Make( { ia }, { ib } );
            // t runs from the canonical edge.a.
            out.t = out.edge.a.value == ia ? t : 1.0 - t;
            out.a = out.edge.a.value == ia ? pa : pb; out.b = out.edge.a.value == ia ? pb : pa;
        }
    }
    return found ? edge_result_t::HIT : edge_result_t::MISS;
}

struct point_hit_t { u64 object{ 0 }; u32 index{ 0 }; u64 sourceId{ 0 }; vec3 position{}; };

// The presentation point (brush corner or mesh vertex) of the given objects
// nearest the cursor, within tolerance.
template <typename Accept, typename AcceptPoint>
bool NearestPoint( const map_workspace_t *ws, const map_tool_event_t &e, f64 tolerance, Accept accept, AcceptPoint acceptPoint, point_hit_t &out ) noexcept
{
    const auto &wire = ws->wire;
    f64 best = tolerance;
    bool found = false;
    for ( usize o = 0; o < wire.objects.nCount; ++o ) {
        const auto &object = wire.objects.pData[o];
        if ( !accept( object ) || !MapWorkspace_IsVisible( ws, object ) ) { continue; }
        for ( u32 k = 0; k < object.nPoints; ++k ) {
            const u32 index = object.iFirstPoint + k;
            if ( !acceptPoint( wire.points.pData[index] ) ) { continue; }
            QPointF screen;
            if ( !Project( e.view, wire.points.pData[index], screen ) ) { continue; }
            const f64 distance = std::hypot( screen.x() - e.screen.x(), screen.y() - e.screen.y() );
            if ( distance >= best ) { continue; }
            best = distance; found = true;
            out.object = object.id; out.index = index; out.sourceId = wire.pointSourceIds.pData[index]; out.position = wire.points.pData[index];
        }
    }
    return found;
}

string_view_t Text( const map_workspace_t *ws, const char *path, const char *fallback ) noexcept
{
    return EditorSettings_Text( &ws->pGui->settings, path, StringView_FromCString( fallback ) );
}
f64 Real( const map_workspace_t *ws, const char *path, f64 fallback ) noexcept { return EditorSettings_Real( &ws->pGui->settings, path, fallback ); }

f64 Repeat( const map_workspace_t *ws ) noexcept
{
    map_bounds_t unit{}; MapBounds_AddPoint( unit, {} ); MapBounds_AddPoint( unit, { 1, 1, 1 } );
    const f64 repeat = MapWorkspace_PrimitiveDefaults( ws, unit ).worldUnitsPerUv;
    return std::isfinite( repeat ) && repeat > 0.0 ? repeat : 128.0;
}

// Writes a text setting to the narrowest attached scope.
bool WriteText( map_workspace_t *ws, const char *path, string_view_t text ) noexcept
{
    settings_registry_t *settings = &ws->pGui->settings;
    const auto *descriptor = EditorSettings_Find( settings, StringView_FromCString( path ) );
    if ( descriptor == nullptr ) { return false; }
    setting_value_t value{}; value.type = descriptor->type; value.text = text;
    for ( usize i = 0u; i < static_cast<usize>( settings_scope_t::DEFAULT ); ++i ) {
        if ( settings->scopes[i] != nullptr ) { return EditorSettings_Write( settings, static_cast<settings_scope_t>( i ), *descriptor, value ) == settings_registry_status_t::OK; }
    }
    return false;
}

bool SelectCreated( const map_workspace_t *ws ) noexcept { return EditorSettings_Bool( &ws->pGui->settings, "editor.map.select_created", CY_TRUE ) != CY_FALSE; }

bool Keep( const map_workspace_t &ws, vector_t<u64> *out ) noexcept
{
    if ( !Vector_Reserve( out, ws.selection.ids.nCount ) ) { return false; }
    for ( usize i = 0; i < ws.selection.ids.nCount; ++i ) { if ( !Vector_PushBack( out, ws.selection.ids.pData[i] ) ) { return false; } }
    return true;
}

// A click a tool cannot use says what it needs: a silent click reads as a
// broken tool.
void Hint( const map_workspace_t *ws, const QString &text ) noexcept { MapNotice_Post( ws, map_notice_t::WARNING, text ); }

// The created object becomes the selection (editor.map.select_created), or
// the selection stays.
bool Choose( const map_workspace_t &ws, vector_t<u64> *out, u64 created ) noexcept
{
    return SelectCreated( &ws ) ? Vector_PushBack( out, created ) != CY_FALSE : Keep( ws, out );
}

template <typename Edit>
bool Apply( map_workspace_t *ws, const char *label, Edit edit, const map_prepared_component_selection_t *prepared = nullptr ) noexcept
{
    return MapWorkspace_ApplyEdit( ws, label, []( void *context, map_document_t *copy, vector_t<u64> *out ) noexcept {
        return ( *static_cast<Edit *>( context ) )( copy, out );
    }, &edit, prepared );
}

template <typename Edit>
bool ApplyMetadata( map_workspace_t *ws, const char *label, Edit edit ) noexcept
{
    return MapWorkspace_ApplyMetadataEdit( ws, label, []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        return ( *static_cast<Edit *>( context ) )( copy, out, changed );
    }, &edit );
}

// Hammer 5's blocks are meshes: a mesh tool used on a brush makes it a mesh
// first (its ID and face IDs stay), unless
// editor.map.components_convert_brushes is off.
bool BrushToMesh( map_workspace_t *ws, u64 id ) noexcept
{
    if ( !Writable( ws ) || !EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, id );
    if ( object == nullptr || object->kind != map_wire_kind_t::BRUSH || !MapWorkspace_IsVisible( ws, *object ) ) { return false; }
    const bool done = Apply( ws, "Convert Brush to Mesh", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        vector_t<u64> roots{};
        if ( !Vector_Init( &roots, copy->pAllocator ) || !Keep( *ws, out ) ) { Vector_Shutdown( &roots ); return map_status_t::OUT_OF_MEMORY; }
        const auto status = MapMeshEdit_ConvertBrushes( copy, { &id, 1 }, &roots );
        Vector_Shutdown( &roots );
        return status;
    } );
    if ( done ) { MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Brush #%1 is now a mesh, for the %2 tool" ).arg( id ).arg( QString::fromLatin1( MapWorkspace_ToolName( ws->tool ) ) ) ); }
    return done;
}

// The mesh face under the cursor; a brush in front of it blocks it, or
// (convert) becomes a mesh.
bool MeshUnder( map_workspace_t *ws, const map_tool_event_t &e, surface_hit_t &hit, bool convert ) noexcept
{
    if ( !SurfaceHit( ws, e, hit ) ) { return false; }
    if ( hit.kind == map_wire_kind_t::MESH ) { return true; }
    if ( !convert || hit.kind != map_wire_kind_t::BRUSH || !BrushToMesh( ws, hit.object ) ) { return false; }
    return SurfaceHit( ws, e, hit ) && hit.kind == map_wire_kind_t::MESH;
}

/*
================
Placement tools: Entity, Decal, Overlay, Path
================
*/

bool PlaceEntity( map_workspace_t *ws, const char *label, string_view_t className, vec3 origin, std::vector<map_property_t> properties ) noexcept
{
    if ( !Writable( ws ) ) { return false; }
    return ApplyMetadata( ws, label, [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        u64 id = 0;
        auto status = MapCreate_Entity( copy, className, origin, { properties.data(), properties.size() }, &id );
        // The game's defaults for what the entity was not made with (CYMAP.md: every property, defaults included).
        if ( status == map_status_t::OK ) { status = MapGame_WriteDefaults( ws, copy, id, className ); }
        if ( status != map_status_t::OK ) { return status; }
        *changed = true;
        return Choose( *ws, out, id ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
}

bool EntityEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    vec3 p{};
    if ( e.kind == map_tool_event_kind_t::MOVE ) { s.bHover = PlacePoint( ws, e, p ); s.hover = p; return true; }
    if ( e.kind != map_tool_event_kind_t::PRESS ) { return false; }
    if ( !PlacePoint( ws, e, p ) ) { Hint( ws, QStringLiteral( "Entity: click on the grid in a 2D view, or on a surface or the ground in 3D" ) ); return true; }
    ( void )PlaceEntity( ws, "Place Entity", Text( ws, "editor.map.default_point_class", "info_player_start" ), p, {} );
    return true;
}

bool DecalEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e, bool overlay ) noexcept
{
    surface_hit_t hit{};
    const bool onSurface = SurfaceHit( ws, e, hit );
    // On the grid across the face, as entities are placed (Ctrl: freely).
    if ( onSurface ) { hit.point = SnapOnPlane( ws, hit.point, hit.normal, e.modifiers ); }
    if ( e.kind == map_tool_event_kind_t::MOVE ) { s.bHover = onSurface; s.hover = hit.point; return true; }
    if ( e.kind != map_tool_event_kind_t::PRESS ) { return false; }
    if ( !onSurface ) { Hint( ws, overlay ? QStringLiteral( "Overlay: click on a surface to lay it on" ) : QStringLiteral( "Decal: click on a surface to place it on" ) ); return true; }
    const auto material = Text( ws, "editor.map.default_material", "materials/dev/dev_grid.cymat" );
    if ( overlay ) {
        const std::string normal = std::to_string( hit.normal.x ) + " " + std::to_string( hit.normal.y ) + " " + std::to_string( hit.normal.z );
        ( void )PlaceEntity( ws, "Place Overlay", StringView_FromCString( "info_overlay" ), hit.point,
            { { StringView_FromCString( "material" ), material }, { StringView_FromCString( "basisnormal" ), { normal.data(), normal.size() } } } );
    } else {
        ( void )PlaceEntity( ws, "Place Decal", StringView_FromCString( "infodecal" ), hit.point, { { StringView_FromCString( "texture" ), material } } );
    }
    return true;
}

// Each click adds a path_corner; the previous corner targets it, so the
// chain is one walkable path. Confirm or Cancel starts a new chain.
bool PathEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    vec3 p{};
    if ( e.kind == map_tool_event_kind_t::MOVE ) { s.bHover = PlacePoint( ws, e, p ); s.hover = p; return true; }
    if ( e.kind == map_tool_event_kind_t::CONFIRM || e.kind == map_tool_event_kind_t::CANCEL ) {
        const bool had = s.lastCorner != 0 || !s.points.empty();
        s.lastCorner = 0; s.points.clear();
        return had;
    }
    if ( e.kind != map_tool_event_kind_t::PRESS ) { return false; }
    if ( !PlacePoint( ws, e, p ) ) { Hint( ws, QStringLiteral( "Path: click on the grid in a 2D view, or on a surface or the ground in 3D" ) ); return true; }
    if ( !Writable( ws ) ) { return true; }
    const u64 previous = s.lastCorner;
    u64 created = 0;
    const bool done = ApplyMetadata( ws, "Place Path Corner", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        auto status = MapCreate_Entity( copy, StringView_FromCString( "path_corner" ), p, {}, &created );
        const std::string name = "path_corner_" + std::to_string( created );
        if ( status == map_status_t::OK ) { status = MapEntity_SetString( copy, created, StringView_FromCString( "targetname" ), { name.data(), name.size() } ); }
        if ( status == map_status_t::OK && previous != 0 && MapEntityEdit_FindEntity( copy, previous ) != nullptr ) {
            status = MapEntity_SetString( copy, previous, StringView_FromCString( "target" ), { name.data(), name.size() } );
        }
        if ( status != map_status_t::OK ) { return status; }
        *changed = true;
        return Choose( *ws, out, created ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
    if ( done ) { s.lastCorner = created; s.points.push_back( p ); }
    return true;
}

/*
================
Point-chain tools: Polygon, Curve
================
*/

// Adds a point on the chain's plane: a 2D pane's grid at depth 0, or in 3D
// the workplane, the first surface clicked, or the ground.
bool AddChainPoint( const map_workspace_t *ws, state_t &s, const map_tool_event_t &e, vec3 &out ) noexcept
{
    if ( e.view.bOrtho ) {
        if ( !s.points.empty() && ( !s.bPointsOrtho || s.pointsU != e.view.axisU || s.pointsV != e.view.axisV ) ) { return false; }
        out = SnapUV( ws, OrthoPoint( e, 0.0 ), e.view.axisU, e.view.axisV, e.modifiers );
        if ( s.points.empty() ) { s.bPointsOrtho = true; s.pointsU = e.view.axisU; s.pointsV = e.view.axisV; }
        return true;
    }
    if ( !s.points.empty() && s.bPointsOrtho ) { return false; }
    if ( s.points.empty() ) {
        surface_hit_t hit{};
        if ( s.bWorkplane ) { s.plane = s.workplane; }
        else if ( SurfaceHit( ws, e, hit ) ) { s.plane = { hit.normal, -Dot( hit.normal, hit.point ) }; }
        else { s.plane = { { 0, 0, 1 }, 0.0 }; }
        s.bPlane = true; s.bPointsOrtho = false;
    }
    if ( !RayPlane( e.rayOrigin, e.rayDirection, s.plane, out ) ) { return false; }
    out = SnapOnPlane( ws, out, s.plane.normal, e.modifiers );
    return true;
}

// How far and which way a drawn polygon is extruded: along the 2D pane's
// depth, or in 3D off the surface toward the camera.
vec3 PolygonExtrusion( const map_workspace_t *ws, const state_t &s, vec3 eye ) noexcept
{
    const f64 depth = Real( ws, "editor.map.block_depth", 64.0 );
    if ( s.bPointsOrtho ) { return AxisVector( DepthAxis( s.pointsU, s.pointsV ), Outward( s.pointsU, s.pointsV ) * depth ); }
    vec3 normal = s.plane.normal;
    if ( !s.points.empty() && Dot( normal, Sub( eye, s.points[0] ) ) < 0.0 ) { normal = Mul( normal, -1.0 ); }
    return Mul( normal, depth );
}

bool FinishPolygon( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    if ( s.points.size() < 3 || !Writable( ws ) ) { return false; }
    const vec3 extrusion = PolygonExtrusion( ws, s, e.view.eye );
    const auto material = Text( ws, "editor.map.default_material", "materials/dev/dev_grid.cymat" );
    const f64 repeat = Repeat( ws );
    const bool done = Apply( ws, "Create Polygon Mesh", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        u64 id = 0;
        const auto status = MapCreate_PolygonMesh( copy, { s.points.data(), s.points.size() }, extrusion, material, repeat, &id );
        return status != map_status_t::OK ? status : Choose( *ws, out, id ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
    if ( done ) { s.points.clear(); s.bPlane = false; }
    return done;
}

// The Curve tool's shapes: a straight strip, a smooth sweep (a rectangle or
// round profile carried along a Catmull-Rom curve through the points), or a
// lathe (the points are a profile spun about the vertical through the
// first point).
bool FinishCurveImpl( map_workspace_t *ws, state_t &s, vec3 eye )
{
    if ( s.points.size() < 2 || !Writable( ws ) ) { return false; }
    const f64 width = Real( ws, "editor.map.curve_width", 32.0 ), height = Real( ws, "editor.map.curve_height", 16.0 );
    const auto material = Text( ws, "editor.map.default_material", "materials/dev/dev_grid.cymat" );
    const f64 repeat = Repeat( ws );
    const auto shape = Text( ws, "editor.map.curve_shape", "straight" );
    if ( StringView_Equals( shape, StringView_FromCString( "hollow_pipe" ) ) ) {
        if ( EditorSettings_Bool( &ws->pGui->settings, "editor.map.curve_closed", CY_FALSE ) != CY_FALSE ||
             !StringView_Equals( Text( ws, "editor.map.curve_output", "mesh" ), StringView_FromCString( "mesh" ) ) ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Hollow Pipe needs Mesh output and an open path. Turn off Closed loop; the path is retained." ) );
            return false;
        }
        const f64 radius = Real( ws, "editor.map.curve_outer_radius", 16.0 );
        const f64 wall = Real( ws, "editor.map.curve_wall_thickness", 4.0 );
        if ( !std::isfinite( radius ) || !std::isfinite( wall ) || wall <= 0.0 || wall >= radius ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Hollow Pipe needs a positive wall thickness smaller than its outer radius. Adjust the dimensions and confirm again." ) );
            return false;
        }
        const u32 sides = static_cast<u32>( std::clamp<i64>( EditorSettings_Integer( &ws->pGui->settings, "editor.map.cylinder_sides", 16 ), 3, 128 ) );
        const bool done = Apply( ws, "Create Hollow Pipe", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
            u64 id = 0;
            const auto status = MapCreate_HollowPipe( copy, { s.points.data(), s.points.size() }, radius, wall, sides, material, repeat, &id );
            return status != map_status_t::OK ? status : Choose( *ws, out, id ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
        } );
        if ( done ) { s.points.clear(); s.bPlane = false; }
        else if ( ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ) {
            MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Not enough memory to create the hollow pipe. The path is retained; confirm again or cancel." ) );
        } else {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Hollow Pipe could not be built: use separated points, gentler turns or fewer sides. The pipe is limited to 2048 faces. Adjust the retained path or dimensions and confirm again." ) );
        }
        return done;
    }
    const bool round = StringView_Equals( Text( ws, "editor.map.curve_profile", "rectangle" ), StringView_FromCString( "round" ) );
    const u32 sides = static_cast<u32>( std::clamp<i64>( EditorSettings_Integer( &ws->pGui->settings, "editor.map.cylinder_sides", 16 ), 3, 128 ) );
    const bool lathe = StringView_Equals( shape, StringView_FromCString( "lathe" ) );
    const bool walls = StringView_Equals( shape, StringView_FromCString( "walls" ) );
    // Walls stand toward the viewer of a 2D pane (up in Top), or along the
    // drawing plane's normal facing the 3D camera.
    const vec3 up = Unit( PolygonExtrusion( ws, s, eye ) );
    const auto joinText = Text( ws, "editor.map.wall_join", "miter" );
    const map_wall_join_t join = StringView_Equals( joinText, StringView_FromCString( "round" ) ) ? map_wall_join_t::ROUND :
                                 StringView_Equals( joinText, StringView_FromCString( "bevel" ) ) ? map_wall_join_t::BEVEL : map_wall_join_t::MITER;
    const bool smooth = StringView_Equals( shape, StringView_FromCString( "smooth" ) ) || round;
    // The profile: across the path (x) and up from it (y), counter-clockwise.
    std::vector<math::vec2d_t> profile;
    if ( round ) {
        for ( u32 i = 0; i < sides; ++i ) {
            const f64 a = 2.0 * M_PI * i / sides;
            profile.push_back( { 0.5 * width * std::cos( a ), 0.5 * width * std::sin( a ) } );
        }
    } else {
        profile = { { -0.5 * width, 0.0 }, { 0.5 * width, 0.0 }, { 0.5 * width, height }, { -0.5 * width, height } };
    }
    // Lathe: the axis is the view's vertical (Z in 3D) through the first point.
    std::vector<math::vec2d_t> lathed;
    vec3 axis{ 0, 0, 1 };
    if ( lathe ) {
        const u32 up = s.bPointsOrtho ? s.pointsV : 2u;
        axis = AxisVector( up, 1.0 );
        for ( const vec3 &p : s.points ) {
            vec3 offset = Sub( p, s.points[0] );
            const f64 along = Comp( offset, up );
            SetComp( offset, up, 0.0 );
            lathed.push_back( { Length( offset ), along } );
        }
    }
    const f64 angle = Real( ws, "editor.map.lathe_angle", 360.0 ) * M_PI / 180.0;
    const bool closed = EditorSettings_Bool( &ws->pGui->settings, "editor.map.curve_closed", CY_FALSE ) != CY_FALSE;
    if ( StringView_Equals( Text( ws, "editor.map.curve_output", "mesh" ), StringView_FromCString( "patch" ) ) ) {
        const bool arch = StringView_Equals( shape, StringView_FromCString( "smooth" ) );
        const bool made = Apply( ws, lathe ? "Create Patch Lathe" : "Create Curve Patch", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
            u64 id = 0;
            const map_status_t status = lathe ? MapCreate_PatchLathe( copy, { lathed.data(), lathed.size() }, true, s.points[0], axis, sides, material, &id )
                                              : MapCreate_PatchStrip( copy, { s.points.data(), s.points.size() }, arch, closed, Mul( up, height ), material, &id );
            return status != map_status_t::OK ? status : Choose( *ws, out, id ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
        } );
        if ( made ) { s.points.clear(); s.bPlane = false; }
        else { MapNotice_Post( ws, map_notice_t::ERROR, lathe ? QStringLiteral( "Patch lathe: the profile must stay on one side of the axis (the first point)" )
                                                              : QStringLiteral( "Curve patch: too many points for one patch row (128 spans at most), or a closed loop of fewer than 3 points" ) ); }
        return made;
    }
    const bool done = Apply( ws, walls ? "Create Walls" : lathe ? "Create Lathe" : smooth ? "Create Sweep" : "Create Curve Mesh", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        u64 id = 0;
        map_status_t status;
        if ( walls ) { status = MapCreate_Walls( copy, { s.points.data(), s.points.size() }, up, closed, width, height, join, material, repeat, &id ); }
        else if ( lathe ) { status = MapCreate_Lathe( copy, { lathed.data(), lathed.size() }, s.points[0], axis, angle, sides, material, repeat, &id ); }
        else if ( smooth ) { status = MapCreate_Sweep( copy, { s.points.data(), s.points.size() }, closed, { profile.data(), profile.size() }, true, material, repeat, &id ); }
        else { status = MapCreate_PathMesh( copy, { s.points.data(), s.points.size() }, width, height, material, repeat, &id ); }
        return status != map_status_t::OK ? status : Choose( *ws, out, id ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
    if ( done ) { s.points.clear(); s.bPlane = false; }
    else { MapNotice_Post( ws, map_notice_t::ERROR, walls ? QStringLiteral( "Walls: this path folds back on itself too tightly for the wall thickness" ) :
                                                    lathe ? QStringLiteral( "Lathe: the profile must stay on one side of the axis (the first point)" )
                                                          : QStringLiteral( "Curve: these points do not make a sweep (too tight a turn, or points on top of each other)" ) ); }
    return done;
}

bool FinishCurve( map_workspace_t *ws, state_t &s, vec3 eye ) noexcept
{
    try { return FinishCurveImpl( ws, s, eye ); }
    catch ( ... ) {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Not enough memory to prepare the curve. The path is retained; confirm again or cancel." ) );
        return false;
    }
}

bool ChainEventImpl( map_workspace_t *ws, state_t &s, const map_tool_event_t &e, bool polygon )
{
    switch ( e.kind ) {
        case map_tool_event_kind_t::MOVE: {
            vec3 p{};
            state_t probe = s; // Hover must not start a plane.
            s.bHover = AddChainPoint( ws, probe, e, p ); s.hover = p;
            return true;
        }
        case map_tool_event_kind_t::PRESS: {
            vec3 p{};
            if ( !AddChainPoint( ws, s, e, p ) ) {
                const QString tool = polygon ? QStringLiteral( "Polygon" ) : QStringLiteral( "Curve" );
                Hint( ws, s.points.empty() ? QStringLiteral( "%1: click on the grid in a 2D view, or on a surface or the ground in 3D" ).arg( tool )
                                           : QStringLiteral( "%1: keep clicking in the view the shape began in; Enter finishes it, Escape starts over" ).arg( tool ) );
                return true;
            }
            // Clicking the first point closes a polygon.
            QPointF first;
            if ( polygon && s.points.size() >= 3 && Project( e.view, s.points[0], first ) &&
                 std::hypot( first.x() - e.screen.x(), first.y() - e.screen.y() ) <= kPickPixels ) { ( void )FinishPolygon( ws, s, e ); return true; }
            if ( s.points.empty() || Length( Sub( p, s.points.back() ) ) > 1e-9 ) { s.points.push_back( p ); }
            return true;
        }
        case map_tool_event_kind_t::CONFIRM:
            if ( s.points.size() < ( polygon ? 3u : 2u ) ) {
                Hint( ws, polygon ? QStringLiteral( "Polygon: click at least three corners first; Enter (or clicking the first corner) finishes" )
                                  : QStringLiteral( "Curve: click at least two points first; Enter finishes" ) );
                return true;
            }
            // Confirm belongs to this staged chain even when validation or
            // allocation refuses construction. Do not pass it to another
            // viewport/global binding while retaining these points for retry.
            ( void )( polygon ? FinishPolygon( ws, s, e ) : FinishCurve( ws, s, e.view.eye ) );
            return true;
        case map_tool_event_kind_t::BACKSPACE:
            if ( s.points.empty() ) { return false; }
            s.points.pop_back(); if ( s.points.empty() ) { s.bPlane = false; }
            return true;
        case map_tool_event_kind_t::CANCEL:
            if ( s.points.empty() ) { return false; }
            s.points.clear(); s.bPlane = false;
            return true;
        case map_tool_event_kind_t::RELEASE: return !s.points.empty();
    }
    return false;
}

bool ChainEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e, bool polygon ) noexcept
{
    try { return ChainEventImpl( ws, s, e, polygon ); }
    catch ( ... ) {
        s.bHover = false;
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Not enough memory to prepare the point chain. Existing points are retained; retry or cancel." ) );
        return true;
    }
}

/*
================
Drag tools: Patch, Measure, Mirror, Clip on meshes
================
*/

// The point a drag tool tracks: the 2D cursor at depth 0, or in 3D the
// surface (Measure) or the ground (Patch).
bool DragPoint( const map_workspace_t *ws, const map_tool_event_t &e, bool surface, vec3 &out ) noexcept
{
    if ( e.view.bOrtho ) { out = SnapUV( ws, OrthoPoint( e, 0.0 ), e.view.axisU, e.view.axisV, e.modifiers ); return true; }
    surface_hit_t hit{};
    if ( surface && SurfaceHit( ws, e, hit ) ) { out = SnapOnPlane( ws, hit.point, hit.normal, e.modifiers ); return true; }
    if ( WorkplanePoint( ws, e, out ) ) { return true; }
    if ( !RayPlane( e.rayOrigin, e.rayDirection, { { 0, 0, 1 }, 0.0 }, out ) ) { return false; }
    out = SnapOnPlane( ws, out, { 0, 0, 1 }, e.modifiers );
    return true;
}

map_patch_shape_t PatchShape( const map_workspace_t *ws ) noexcept
{
    const auto text = Text( ws, "editor.map.patch_shape", "cylinder" );
    const char *names[]{ "plane", "cylinder", "cone", "sphere", "disc", "bevel", "torus" };
    static_assert( std::size( names ) == static_cast<usize>( map_patch_shape_t::COUNT ), "Shape names and enum diverged" );
    for ( u32 i = 0; i < std::size( names ); ++i ) { if ( StringView_Equals( text, StringView_FromCString( names[i] ) ) ) { return static_cast<map_patch_shape_t>( i ); } }
    return map_patch_shape_t::CYLINDER;
}

// The box a Patch drag describes: the dragged rectangle, as deep as the
// block depth along the pane's depth (flat shapes stay flat).
bool PatchBox( const map_workspace_t *ws, const state_t &s, math::aabbd_t &box, u32 &axis ) noexcept
{
    const u32 u = s.bPointsOrtho ? s.pointsU : 0u, v = s.bPointsOrtho ? s.pointsV : 1u;
    axis = DepthAxis( u, v );
    const auto shape = PatchShape( ws );
    // A sphere is as deep as the dragged square is wide; other solids take
    // the block depth.
    const f64 size = shape == map_patch_shape_t::SPHERE ? std::min( std::fabs( Comp( s.dragB, u ) - Comp( s.dragA, u ) ), std::fabs( Comp( s.dragB, v ) - Comp( s.dragA, v ) ) )
                                                        : Real( ws, "editor.map.block_depth", 64.0 );
    const f64 depth = shape == map_patch_shape_t::PLANE || shape == map_patch_shape_t::DISC ? 0.0 : size * ( s.bPointsOrtho ? Outward( u, v ) : 1.0 );
    box.minimum = { std::min( s.dragA.x, s.dragB.x ), std::min( s.dragA.y, s.dragB.y ), std::min( s.dragA.z, s.dragB.z ) };
    box.maximum = { std::max( s.dragA.x, s.dragB.x ), std::max( s.dragA.y, s.dragB.y ), std::max( s.dragA.z, s.dragB.z ) };
    SetComp( box.minimum, axis, std::min( Comp( s.dragA, axis ), Comp( s.dragA, axis ) + depth ) );
    SetComp( box.maximum, axis, std::max( Comp( s.dragA, axis ), Comp( s.dragA, axis ) + depth ) );
    return Comp( box.maximum, u ) - Comp( box.minimum, u ) > 0.0 && Comp( box.maximum, v ) - Comp( box.minimum, v ) > 0.0;
}

// A Patch rectangle corner. In 3D, as with Block, the rectangle lies on the
// upward-facing surface the press landed on (the workplane when one is set,
// else the ground).
bool PatchPoint( const map_workspace_t *ws, const state_t &s, const map_tool_event_t &e, vec3 &out ) noexcept
{
    if ( e.view.bOrtho ) { return DragPoint( ws, e, false, out ); }
    if ( WorkplanePoint( ws, e, out ) ) { return true; }
    if ( !RayPlane( e.rayOrigin, e.rayDirection, { { 0, 0, 1 }, -s.dragBase }, out ) ) { return false; }
    out = SnapOnPlane( ws, out, { 0, 0, 1 }, e.modifiers );
    return true;
}

bool PatchEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    vec3 p{};
    switch ( e.kind ) {
        case map_tool_event_kind_t::PRESS: {
            surface_hit_t hit{};
            s.dragBase = !e.view.bOrtho && SurfaceHit( ws, e, hit ) && hit.normal.z > 0.7 ? hit.point.z : 0.0;
            if ( PatchPoint( ws, s, e, p ) ) { s.bDrag = true; s.dragA = s.dragB = p; s.bPointsOrtho = e.view.bOrtho; s.pointsU = e.view.axisU; s.pointsV = e.view.axisV; }
            else { Hint( ws, QStringLiteral( "Patch: drag a rectangle on the grid in a 2D view, or on a surface or the ground in 3D" ) ); }
            return true;
        }
        case map_tool_event_kind_t::MOVE:
            if ( s.bDrag && e.bButtonDown && PatchPoint( ws, s, e, p ) ) { s.dragB = p; }
            return true;
        case map_tool_event_kind_t::RELEASE: {
            if ( !s.bDrag ) { return false; }
            s.bDrag = false;
            math::aabbd_t box{};
            u32 axis = 2;
            const auto shape = PatchShape( ws );
            if ( !PatchBox( ws, s, box, axis ) ) { Hint( ws, QStringLiteral( "Patch: drag a rectangle to size the patch (a click makes nothing)" ) ); return true; }
            if ( !Writable( ws ) ) { return true; }
            const auto segments = static_cast<u32>( EditorSettings_Integer( &ws->pGui->settings, "editor.map.cylinder_sides", 16 ) );
            const auto material = Text( ws, "editor.map.default_material", "materials/dev/dev_grid.cymat" );
            ( void )Apply( ws, "Create Patch", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
                u64 id = 0;
                const auto status = MapCreate_Patch( copy, shape, box, axis, segments, material, &id );
                return status != map_status_t::OK ? status : Choose( *ws, out, id ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
            } );
            return true;
        }
        case map_tool_event_kind_t::CANCEL: { const bool had = s.bDrag; s.bDrag = false; return had; }
        default: return false;
    }
}

bool MeasureEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    vec3 p{};
    switch ( e.kind ) {
        case map_tool_event_kind_t::PRESS:
            if ( DragPoint( ws, e, true, p ) ) { s.bDrag = true; s.dragA = s.dragB = p; s.points.assign( { p, p } ); }
            else { Hint( ws, QStringLiteral( "Measure: drag between two points on surfaces in 3D, or on the grid in a 2D view" ) ); }
            return true;
        case map_tool_event_kind_t::MOVE:
            if ( s.bDrag && e.bButtonDown && DragPoint( ws, e, true, p ) ) { s.dragB = p; s.points.assign( { s.dragA, p } ); }
            return true;
        case map_tool_event_kind_t::RELEASE: { const bool had = s.bDrag; s.bDrag = false; return had; }
        case map_tool_event_kind_t::CANCEL: { const bool had = !s.points.empty(); s.points.clear(); s.bDrag = false; return had; }
        default: return false;
    }
}

// A line drawn in a 2D pane and the plane through it along the depth axis.
math::planed_t LinePlane( vec3 a, vec3 b, u32 u, u32 v ) noexcept
{
    const f64 du = Comp( b, u ) - Comp( a, u ), dv = Comp( b, v ) - Comp( a, v );
    vec3 normal{}; SetComp( normal, u, -dv ); SetComp( normal, v, du );
    normal = Unit( normal );
    return { normal, -Dot( normal, a ) };
}

bool CanMirror( const map_workspace_t *ws ) noexcept
{
    return Writable( ws ) && ( ws->elementMode == map_element_mode_t::OBJECTS || ws->elementMode == map_element_mode_t::GROUPS ||
        ws->elementMode == map_element_mode_t::MESHES ) && MapWorkspace_CanEditSelection( ws );
}

bool MirrorEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    vec3 p{};
    if ( !e.view.bOrtho ) {
        if ( e.kind == map_tool_event_kind_t::PRESS ) { Hint( ws, QStringLiteral( "Mirror: drag the mirror line in a 2D view (Top, Front or Side); here a click selects" ) ); }
        return false; // The view still selects.
    }
    switch ( e.kind ) {
        case map_tool_event_kind_t::PRESS:
            s.bDrag = true; s.dragA = s.dragB = DragPoint( ws, e, false, p ) ? p : vec3{};
            s.pointsU = e.view.axisU; s.pointsV = e.view.axisV;
            return true;
        case map_tool_event_kind_t::MOVE:
            if ( s.bDrag && e.bButtonDown && DragPoint( ws, e, false, p ) ) { s.dragB = p; }
            return true;
        case map_tool_event_kind_t::RELEASE: {
            if ( !s.bDrag ) { return false; }
            s.bDrag = false;
            if ( !CanMirror( ws ) ) { Hint( ws, QStringLiteral( "Mirror: select objects first (Objects or Meshes mode), then drag the mirror line in a 2D view; Shift keeps the original" ) ); return true; }
            if ( Length( Sub( s.dragB, s.dragA ) ) <= 1e-9 ) { Hint( ws, QStringLiteral( "Mirror: drag a line in a 2D view; the selection flips across it" ) ); return true; }
            const auto plane = LinePlane( s.dragA, s.dragB, s.pointsU, s.pointsV );
            const bool lock = ws->bTextureLock != CY_FALSE;
            const bool copy = ( e.modifiers & Qt::ShiftModifier ) != 0; // Shift mirrors a copy, keeping the original.
            std::vector<u64> selected( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
            ( void )Apply( ws, copy ? "Mirror Copy" : "Mirror", [&]( map_document_t *map, vector_t<u64> *out ) noexcept {
                if ( !copy ) {
                    if ( !Keep( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
                    return MapTopologyEdit_Mirror( map, { selected.data(), selected.size() }, plane, lock );
                }
                vector_t<u64> clones{};
                if ( !Vector_Init( &clones, map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
                auto status = MapEdit_Duplicate( map, { selected.data(), selected.size() }, {}, &clones );
                if ( status == map_status_t::OK ) { status = MapTopologyEdit_Mirror( map, { clones.pData, clones.nCount }, plane, lock ); }
                for ( usize i = 0; status == map_status_t::OK && i < clones.nCount; ++i ) { if ( !Vector_PushBack( out, clones.pData[i] ) ) { status = map_status_t::OUT_OF_MEMORY; } }
                Vector_Shutdown( &clones );
                return status;
            } );
            return true;
        }
        case map_tool_event_kind_t::CANCEL: { const bool had = s.bDrag; s.bDrag = false; return had; }
        default: return false;
    }
}

/*
================
Surface tools: Eyedropper, Paint, Workplane, Pivot
================
*/

bool EyedropperEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    surface_hit_t hit{};
    const bool onSurface = SurfaceHit( ws, e, hit );
    if ( e.kind == map_tool_event_kind_t::MOVE ) { s.bHover = onSurface; s.hover = hit.point; return true; }
    if ( e.kind != map_tool_event_kind_t::PRESS ) { return false; }
    if ( !onSurface ) { Hint( ws, QStringLiteral( "Eyedropper: click on a surface to take its material" ) ); return true; }
    const auto path = hit.material != 0 ? MapMaterials_Path( &ws->pDocument->materials, hit.material ) : string_view_t{};
    if ( path.cchLength == 0 ) { Hint( ws, QStringLiteral( "Eyedropper: that surface has no material" ) ); return true; }
    if ( WriteText( ws, "editor.map.default_material", path ) ) {
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Eyedropper: the active material is now %1" ).arg( QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) ) ) );
    }
    return true;
}

// 0 for colour mode; otherwise the one channel the tool paints.
u8 PaintChannel( const map_workspace_t *ws ) noexcept
{
    const auto text = Text( ws, "editor.map.paint_channel", "color" );
    if ( StringView_Equals( text, StringView_FromCString( "red" ) ) ) { return geo::MESH_PAINT_CHANNEL_R; }
    if ( StringView_Equals( text, StringView_FromCString( "green" ) ) ) { return geo::MESH_PAINT_CHANNEL_G; }
    if ( StringView_Equals( text, StringView_FromCString( "blue" ) ) ) { return geo::MESH_PAINT_CHANNEL_B; }
    if ( StringView_Equals( text, StringView_FromCString( "alpha" ) ) ) { return geo::MESH_PAINT_CHANNEL_A; }
    return 0u;
}

QColor PaintColor( const map_workspace_t *ws ) noexcept
{
    const auto text = Text( ws, "editor.map.paint_color", "#e05a28" );
    const QColor color( QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) );
    return color.isValid() ? color : QColor( 224, 90, 40 );
}

// A paint stroke is input and an overlay until release. In particular, an
// abandoned stroke must not leave a brush converted or add an undo step.
void ResetPaint( state_t &s ) noexcept
{
    s.paintObject = 0; s.paintKind = map_wire_kind_t::MESH;
    s.paintView = nullptr; s.paintDocument = nullptr;
    s.paintSelectionRevision = s.paintGeometryRevision = 0;
    s.paintRadius = 32.0; s.paintStrength = 1.0;
    s.paintChannel = 0; s.paintColor = {};
    s.dabs.clear(); s.bHover = false; s.hover = {};
}

bool PaintContextMatches( const map_workspace_t *ws, const state_t &s ) noexcept
{
    if ( s.paintObject == 0 || !Writable( ws ) || ws->tool != map_tool_t::PAINT ||
         ws->pDocument != s.paintDocument || ws->selection.revision != s.paintSelectionRevision ||
         ws->pDocument->geometry.revision != s.paintGeometryRevision ||
         MapLayers_IsObjectLocked( ws, s.paintObject ) ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, s.paintObject );
    return object != nullptr && object->kind == s.paintKind && MapWorkspace_IsVisible( ws, *object ) &&
        ( s.paintKind == map_wire_kind_t::MESH || ( s.paintKind == map_wire_kind_t::BRUSH &&
          EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) != CY_FALSE ) );
}

bool PaintEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    if ( s.paintObject != 0 && !PaintContextMatches( ws, s ) ) {
        ResetPaint( s );
        MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
        if ( e.kind != map_tool_event_kind_t::PRESS ) { return true; }
    }
    if ( s.paintObject != 0 && e.kind != map_tool_event_kind_t::CANCEL && e.view.pView != s.paintView ) { return true; }
    switch ( e.kind ) {
        case map_tool_event_kind_t::MOVE: {
            surface_hit_t hit{};
            const bool onSurface = SurfaceHit( ws, e, hit ) && !MapLayers_IsObjectLocked( ws, hit.object ) &&
                ( hit.kind == map_wire_kind_t::MESH || ( hit.kind == map_wire_kind_t::BRUSH &&
                  EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) != CY_FALSE ) );
            s.bHover = onSurface && ( s.paintObject == 0 || hit.object == s.paintObject );
            s.hover = hit.point;
            if ( e.bButtonDown && s.paintObject != 0 && s.bHover &&
                 ( s.dabs.empty() || Length( Sub( hit.point, s.dabs.back() ) ) >= s.paintRadius * 0.25 ) ) {
                try { s.dabs.push_back( hit.point ); }
                catch ( ... ) {
                    ResetPaint( s );
                    MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Paint: not enough memory to record the stroke; the map has not changed" ) );
                    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
                }
            }
            return true;
        }
        case map_tool_event_kind_t::PRESS: {
            ResetPaint( s );
            surface_hit_t hit{};
            if ( !SurfaceHit( ws, e, hit ) || ( hit.kind != map_wire_kind_t::MESH && hit.kind != map_wire_kind_t::BRUSH ) ) {
                Hint( ws, QStringLiteral( "Paint: paint on a mesh or a brush surface" ) ); return true;
            }
            if ( MapLayers_IsObjectLocked( ws, hit.object ) ) {
                Hint( ws, QStringLiteral( "Paint: unlock the object's layer before painting it" ) ); return true;
            }
            if ( !Writable( ws ) ) { return true; }
            if ( hit.kind == map_wire_kind_t::BRUSH &&
                 EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) == CY_FALSE ) {
                Hint( ws, QStringLiteral( "Paint: enable brush conversion in the component settings to paint this brush" ) ); return true;
            }
            const f64 radius = Real( ws, "editor.map.paint_radius", 32.0 );
            const f64 strength = std::clamp( Real( ws, "editor.map.paint_strength", 1.0 ), 0.01, 1.0 );
            if ( !std::isfinite( radius ) || radius <= 0.0 || !std::isfinite( strength ) ) {
                Hint( ws, QStringLiteral( "Paint: choose a finite positive radius and strength" ) ); return true;
            }
            try { s.dabs.push_back( hit.point ); }
            catch ( ... ) {
                MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Paint: not enough memory to start the stroke; the map has not changed" ) ); return true;
            }
            s.paintObject = hit.object; s.paintKind = hit.kind; s.paintView = e.view.pView;
            s.paintDocument = ws->pDocument; s.paintGeometryRevision = ws->pDocument->geometry.revision;
            s.paintSelectionRevision = ws->selection.revision;
            s.paintRadius = radius; s.paintStrength = strength;
            s.paintChannel = PaintChannel( ws ); s.paintColor = PaintColor( ws );
            s.bHover = true; s.hover = hit.point;
            return true;
        }
        case map_tool_event_kind_t::RELEASE: {
            if ( s.paintObject == 0 || s.dabs.empty() ) { ResetPaint( s ); return true; }
            const u64 mesh = s.paintObject;
            const bool convert = s.paintKind == map_wire_kind_t::BRUSH;
            const auto dabs = std::move( s.dabs );
            const f64 radius = s.paintRadius, strength = s.paintStrength;
            // Corners start white: colour mode paints toward the paint colour,
            // a channel mode takes that channel down; Ctrl/Command paints back
            // toward white. The erasure modifier is read at release, as before.
            const bool erase = SnapBypassed( e.modifiers );
            const u8 channel = s.paintChannel;
            const QColor color = s.paintColor;
            ResetPaint( s );
            // The explicit changed flag is also useful for this geometry edit:
            // discard the private conversion unless painting changed a color.
            // A no-effect stroke must leave the authored brush a brush.
            const bool painted = ApplyMetadata( ws, erase ? "Erase Paint" : "Paint", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
                *changed = false;
                if ( !Keep( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
                if ( convert ) {
                    vector_t<u64> roots{};
                    if ( !Vector_Init( &roots, copy->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
                    const auto status = MapMeshEdit_ConvertBrushes( copy, { &mesh, 1 }, &roots );
                    Vector_Shutdown( &roots );
                    if ( status != map_status_t::OK ) { return status; }
                }
                const u64 paintRevision = copy->geometry.revision;
                if ( channel != 0u ) {
                    const auto status = MapMesh_PaintStroke( copy, mesh, { dabs.data(), dabs.size() }, radius, strength, channel, erase ? 255u : 0u );
                    if ( status != map_status_t::OK ) { return status; }
                } else {
                    const std::pair<u8, int> channels[]{ { geo::MESH_PAINT_CHANNEL_R, color.red() }, { geo::MESH_PAINT_CHANNEL_G, color.green() }, { geo::MESH_PAINT_CHANNEL_B, color.blue() } };
                    for ( const auto &[bit, value] : channels ) {
                        const auto status = MapMesh_PaintStroke( copy, mesh, { dabs.data(), dabs.size() }, radius, strength, bit, erase ? 255u : static_cast<u8>( value ) );
                        if ( status != map_status_t::OK ) { return status; }
                    }
                }
                *changed = copy->geometry.revision != paintRevision;
                return map_status_t::OK;
            } );
            if ( !painted ) {
                if ( ws->lastEdit.outcome == map_edit_outcome_t::UNCHANGED && ws->lastEdit.status == map_status_t::OK ) {
                    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Paint: no vertex colors changed; no corners were within the brush, or they already had the target color" ) );
                } else {
                    MapNotice_Post( ws, map_notice_t::ERROR, ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ?
                        QStringLiteral( "Paint: not enough memory to apply the stroke; the map has not changed" ) :
                        QStringLiteral( "Paint: the stroke could not be applied; the map has not changed" ) );
                }
            }
            MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
            return true;
        }
        case map_tool_event_kind_t::CANCEL: {
            const bool had = s.paintObject != 0; ResetPaint( s );
            if ( had ) { MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); }
            return had;
        }
        default: return false;
    }
}

bool WorkplaneEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    surface_hit_t hit{};
    if ( e.kind == map_tool_event_kind_t::MOVE ) { s.bHover = SurfaceHit( ws, e, hit ); s.hover = hit.point; return true; }
    if ( e.kind == map_tool_event_kind_t::CANCEL ) { const bool had = s.bWorkplane; s.bWorkplane = false; return had; }
    if ( e.kind != map_tool_event_kind_t::PRESS ) { return false; }
    if ( SurfaceHit( ws, e, hit ) ) {
        s.bWorkplane = true; s.workplane = { hit.normal, -Dot( hit.normal, hit.point ) };
        s.workplanePoint = SnapOnPlane( ws, hit.point, hit.normal, e.modifiers );
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Workplane set: Polygon, Entity, Patch, Path and Curve draw on it in 3D; Escape clears it" ) );
    } else {
        Hint( ws, QStringLiteral( "Workplane: click on a surface to draw on its plane" ) );
    }
    return true;
}

vec3 SelectionCenter( const map_workspace_t *ws ) noexcept
{
    map_bounds_t bounds{};
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        if ( const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] ) ) { MapBounds_AddBounds( bounds, object->bounds ); }
    }
    return bounds.bHas ? MapBounds_Center( bounds ) : vec3{};
}

// Where a Pivot click or drag puts the point: on the 2D pane's plane (the
// other coordinate kept from the pivot or the selection centre), or on the
// surface under the cursor in 3D.
bool PivotPoint( const map_workspace_t *ws, const state_t &s, const map_tool_event_t &e, vec3 &out ) noexcept
{
    if ( e.view.bOrtho ) {
        const u32 depth = DepthAxis( e.view.axisU, e.view.axisV );
        out = SnapUV( ws, OrthoPoint( e, Comp( s.bPivot ? s.pivot : SelectionCenter( ws ), depth ) ), e.view.axisU, e.view.axisV, e.modifiers );
        return true;
    }
    surface_hit_t hit{};
    if ( SurfaceHit( ws, e, hit ) ) { out = SnapOnPlane( ws, hit.point, hit.normal, e.modifiers ); return true; }
    return false;
}

// Click or drag to place the pivot Rotate, Scale, Rotate 90 and the flips
// turn about; Escape removes it (back to the selection's centre).
bool PivotEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    vec3 p{};
    switch ( e.kind ) {
        case map_tool_event_kind_t::CANCEL: {
            const bool had = s.bPivot; s.bPivot = false; s.bDrag = false;
            if ( had ) { MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); }
            return had;
        }
        case map_tool_event_kind_t::PRESS:
            if ( !PivotPoint( ws, s, e, p ) ) { Hint( ws, QStringLiteral( "Pivot: click on a surface in 3D, or anywhere in a 2D view" ) ); return true; }
            s.bPivot = true; s.pivot = p; s.bDrag = true;
            MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
            return true;
        case map_tool_event_kind_t::MOVE:
            if ( s.bDrag && e.bButtonDown && PivotPoint( ws, s, e, p ) ) { s.pivot = p; MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); return true; }
            s.bHover = PivotPoint( ws, s, e, s.hover );
            return true;
        case map_tool_event_kind_t::RELEASE: {
            const bool was = s.bDrag; s.bDrag = false; return was;
        }
        default: return false;
    }
}

/*
================
Terrain tool: sculpt heightfields with a round brush (Raise, Lower, Smooth,
Flatten to the height pressed). The stroke shows live on a copy of the
terrain; release replays its dabs on the map as one undo step.
================
*/

void DrawLine( QPainter &painter, const map_tool_view_t &view, vec3 a, vec3 b, const QColor &color, Qt::PenStyle style );

struct terrain_hit_t {
    const geo::heightfield_t *pField{ nullptr };
    geo::heightfield_ray_hit_t hit{};
};

// The nearest visible terrain under the cursor (the stroke's own copy for its terrain).
terrain_hit_t TerrainUnder( const map_workspace_t *ws, const state_t &s, const map_tool_event_t &e ) noexcept
{
    terrain_hit_t best{};
    const auto &fields = ws->pDocument->geometry.heightFields;
    for ( usize i = 0; i < fields.nCount; ++i ) {
        const geo::heightfield_t *field = fields.pData[i];
        const auto *object = MapWireframe_FindObject( ws->wire, field->sourceId.value );
        if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ) { continue; }
        if ( s.bTerrain && s.terrainWork != nullptr && field->sourceId.value == s.terrainId ) { field = &s.terrainWork->field; }
        geo::heightfield_ray_hit_t hit{};
        if ( geo::HeightField_TryRaycast( field, e.rayOrigin, e.rayDirection, 1.0e7, &hit ) && ( best.pField == nullptr || hit.t < best.hit.t ) ) { best = { field, hit }; }
    }
    return best;
}

geo::heightfield_brush_t TerrainBrush( const map_workspace_t *ws, const state_t &s, vec3 at, Qt::KeyboardModifiers modifiers ) noexcept
{
    const auto mode = Text( ws, "editor.map.terrain_mode", "raise" );
    const auto is = [&]( const char *name ) { return StringView_Equals( mode, StringView_FromCString( name ) ); };
    geo::heightfield_brush_t brush{};
    brush.centerX = at.x; brush.centerY = at.y;
    brush.radius = std::max( 1.0, Real( ws, "editor.map.terrain_radius", 256.0 ) );
    const f64 strength = std::clamp( Real( ws, "editor.map.terrain_strength", 25.0 ), 1.0, 100.0 ) / 100.0;
    if ( is( "smooth" ) ) { brush.op = geo::heightfield_brush_op_t::SMOOTH; brush.strength = strength; }
    else if ( is( "flatten" ) ) { brush.op = geo::heightfield_brush_op_t::FLATTEN; brush.strength = strength; brush.targetHeight = s.terrainTarget; }
    else {
        // Raise (Lower, or Ctrl/Command while raising, digs): a quarter of the radius per dab at full strength,
        // so a few strokes make a hill you can see across the terrain.
        const bool lower = is( "lower" ) != SnapBypassed( modifiers );
        brush.op = geo::heightfield_brush_op_t::RAISE; brush.amount = ( lower ? -1.0 : 1.0 ) * strength * brush.radius * 0.25;
    }
    return brush;
}

void TerrainDab( map_workspace_t *ws, state_t &s, vec3 at, Qt::KeyboardModifiers modifiers ) noexcept
{
    const auto brush = TerrainBrush( ws, s, at, modifiers );
    if ( geo::HeightField_TryApplyBrush( &s.terrainWork->field, brush, nullptr ) != geo::geometry_status_t::OK ) { return; }
    s.terrainDabs.push_back( brush );
    if ( MapWireframe_RefreshTerrain( &ws->wire, s.terrainWork->field ) != map_status_t::OK ) { return; }
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
}

void TerrainRestore( map_workspace_t *ws, state_t &s ) noexcept
{
    s.bTerrain = false; s.terrainDabs.clear(); s.terrainWork.reset();
    if ( MapWireframe_Build( &ws->wire, *ws->pDocument ) != map_status_t::OK ) { CY_LOG_WRITE( Error, Editor, "Map views could not be rebuilt after a terrain stroke" ); }
    MapWorkspace_WireRebuilt( ws );
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
}

bool TerrainEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    switch ( e.kind ) {
        case map_tool_event_kind_t::PRESS: {
            const auto under = TerrainUnder( ws, s, e );
            if ( under.pField == nullptr ) {
                MapNotice_Post( ws, map_notice_t::WARNING, ws->pDocument->geometry.heightFields.nCount == 0
                    ? QStringLiteral( "Terrain: there is no terrain yet; Create Terrain in the tool's panel makes one" )
                    : QStringLiteral( "Terrain: sculpt on a terrain (none under the cursor)" ) );
                return true;
            }
            if ( !Writable( ws ) ) { return true; }
            auto work = std::make_shared<state_t::field_copy_t>();
            if ( geo::HeightField_TryClone( under.pField, ws->pDocument->pAllocator, &work->field ) != geo::geometry_status_t::OK ) { return true; }
            s.bTerrain = true; s.terrainId = under.pField->sourceId.value; s.terrainWork = std::move( work ); s.terrainDabs.clear();
            s.terrainTarget = under.hit.position.z - under.pField->origin.z; // Flatten levels to where the stroke began.
            s.terrainLast = under.hit.position; s.terrainHover = under.hit.position; s.bTerrainHover = true;
            TerrainDab( ws, s, under.hit.position, e.modifiers );
            return true;
        }
        case map_tool_event_kind_t::MOVE: {
            const auto under = TerrainUnder( ws, s, e );
            s.bTerrainHover = under.pField != nullptr; s.terrainHover = under.hit.position;
            if ( s.bTerrain && e.bButtonDown && under.pField != nullptr && under.pField->sourceId.value == s.terrainId ) {
                // Dabs a quarter of the radius apart along the drag, so a fast stroke leaves no gaps.
                const f64 spacing = std::max( 1.0, Real( ws, "editor.map.terrain_radius", 256.0 ) * 0.25 );
                const vec3 delta = Sub( under.hit.position, s.terrainLast );
                const f64 travel = std::hypot( delta.x, delta.y );
                const int dabs = std::min( 64, static_cast<int>( travel / spacing ) );
                for ( int i = 1; i <= dabs; ++i ) { TerrainDab( ws, s, Add( s.terrainLast, Mul( delta, static_cast<f64>( i ) / dabs ) ), e.modifiers ); }
                if ( dabs > 0 ) { s.terrainLast = under.hit.position; }
            }
            MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
            return true;
        }
        case map_tool_event_kind_t::RELEASE: {
            if ( !s.bTerrain ) { return false; }
            const u64 id = s.terrainId;
            const auto dabs = s.terrainDabs;
            const auto mode = Text( ws, "editor.map.terrain_mode", "raise" );
            const std::string label = "Terrain " + std::string( mode.pData, mode.cchLength );
            s.bTerrain = false; s.terrainWork.reset(); s.terrainDabs.clear();
            const bool done = !dabs.empty() && Apply( ws, label.c_str(), [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
                if ( !Keep( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
                const auto *field = geo::GeometryDocument_FindHeightField( &copy->geometry, { id } );
                if ( field == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
                geo::heightfield_t edited{};
                auto status = geo::HeightField_TryClone( field, copy->pAllocator, &edited );
                for ( usize i = 0; status == geo::geometry_status_t::OK && i < dabs.size(); ++i ) { status = geo::HeightField_TryApplyBrush( &edited, dabs[i], nullptr ); }
                if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryDocument_TryReplaceHeightField( &copy->geometry, &edited ); }
                geo::HeightField_Shutdown( &edited );
                if ( status != geo::geometry_status_t::OK || copy->geometry.revision == CY_U64_MAX ) { return map_status_t::GEOMETRY_FAILED; }
                ++copy->geometry.revision; // Replacing a field does not count as a revision by itself (as the terrain transforms do).
                return map_status_t::OK;
            } );
            if ( !done ) { TerrainRestore( ws, s ); }
            return true;
        }
        case map_tool_event_kind_t::CANCEL: {
            if ( !s.bTerrain ) { return false; }
            TerrainRestore( ws, s );
            return true;
        }
        default: return false;
    }
}

// The brush footprint, draped over the terrain under the cursor.
void TerrainPaint( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    if ( !s.bTerrainHover || view.bOrtho ) { return; }
    const geo::heightfield_t *field = nullptr;
    const auto &fields = ws->pDocument->geometry.heightFields;
    for ( usize i = 0; i < fields.nCount && field == nullptr; ++i ) {
        const auto *candidate = s.bTerrain && s.terrainWork != nullptr && fields.pData[i]->sourceId.value == s.terrainId ? &s.terrainWork->field : fields.pData[i];
        f64 h = 0.0;
        if ( geo::HeightField_TryHeightAt( candidate, s.terrainHover.x, s.terrainHover.y, &h ) ) { field = candidate; }
    }
    if ( field == nullptr ) { return; }
    const f64 radius = std::max( 1.0, Real( ws, "editor.map.terrain_radius", 256.0 ) );
    vec3 previous{};
    for ( int i = 0; i <= 48; ++i ) {
        const f64 a = 2.0 * M_PI * i / 48.0;
        vec3 p{ s.terrainHover.x + radius * std::cos( a ), s.terrainHover.y + radius * std::sin( a ), s.terrainHover.z };
        f64 h = 0.0;
        if ( geo::HeightField_TryHeightAt( field, p.x, p.y, &h ) ) { p.z = h; } // World height.
        if ( i > 0 ) { DrawLine( painter, view, previous, p, QColor( 240, 200, 80 ), Qt::SolidLine ); }
        previous = p;
    }
}

/*
================
Extrude tool on mesh faces (Hammer 5's face push-pull): press a face (it,
or the face selection holding it), drag along its normal, release. Out
extrudes; in moves the faces. Brush faces keep the views' own push-pull.
================
*/

void DrawLine( QPainter &painter, const map_tool_view_t &view, vec3 a, vec3 b, const QColor &color, Qt::PenStyle style );

// Extrusion previews own only an overlay. Discarding a capture must not
// change the authored map, the selection, or the persistent pivot/workplane.
void ResetExtrude( state_t &s ) noexcept
{
    s.bExtrude = false;
    s.extrudeView = nullptr; s.extrudeDocument = nullptr;
    s.extrudeSelectionRevision = s.extrudeGeometryRevision = 0;
    s.extrudeMesh = 0; s.extrudeFaces.clear(); s.extrudeVertices.clear(); s.extrudeBoundary.clear();
    s.extrudeFrom = s.extrudeNormal = {}; s.extrudeDistance = 0.0;
}

bool ExtrudeContextMatches( const map_workspace_t *ws, const state_t &s ) noexcept
{
    if ( !s.bExtrude || !Writable( ws ) || ws->tool != map_tool_t::EXTRUDE ||
         ws->pDocument != s.extrudeDocument || ws->selection.revision != s.extrudeSelectionRevision ||
         ws->pDocument->geometry.revision != s.extrudeGeometryRevision ||
         ws->selectedMeshFaceObject != s.extrudeMesh || MapLayers_IsObjectLocked( ws, s.extrudeMesh ) ) { return false; }
    const auto faces = MapWorkspace_MeshFaces( ws );
    return !s.extrudeFaces.empty() && faces.nCount == s.extrudeFaces.size() &&
           std::equal( s.extrudeFaces.begin(), s.extrudeFaces.end(), faces.pData );
}

// Admission uses the same Core operation as commit on a disposable document,
// once at pickup. Drag samples remain overlay-only. This avoids promising a
// region preview for disconnected or noncoplanar faces that Core will refuse.
map_status_t PrepareExtrudeRegion( const map_workspace_t *ws, u64 mesh, const std::vector<u64> &faces,
    std::vector<id_t> &vertices, std::vector<std::pair<vec3, vec3>> &boundary, vec3 &normal )
{
    map_document_t *copy = nullptr;
    auto status = MapEdit_Clone( ws->pDocument, &copy );
    const std::unique_ptr<map_document_t> candidate( copy );
    if ( status != map_status_t::OK ) { return status; }
    const f64 probe = std::max( 1.0, ws->pDocument->geometryPolicy.numerical.fMinimumEdgeLength * 2.0 );
    status = MapTopologyEdit_ExtrudeFaceRegionBy( copy, mesh, { faces.data(), faces.size() }, probe );
    if ( status != map_status_t::OK ) { return status; }
    const auto *source = geo::GeometryDocument_FindMesh( &ws->pDocument->geometry, { mesh } );
    if ( source == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    struct snapshot_t {
        geo::mesh_source_description_t value{};
        ~snapshot_t() { geo::MeshSourceDescription_Shutdown( &value ); }
    } snapshot;
    auto described = geo::MeshSourceDescription_Init( &snapshot.value, ws->pGui->pAllocator, { mesh } );
    if ( described == geo::geometry_status_t::OK ) { described = geo::MeshSource_TryDescribe( source, &snapshot.value ); }
    if ( described != geo::geometry_status_t::OK ) {
        return described == geo::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED;
    }
    struct edge_t { u64 a{}, b{}; vec3 from{}, to{}; };
    std::vector<edge_t> edges;
    for ( const u64 id : faces ) {
        // TryDescribe orders faces by persistent ID. A large region should
        // not rescan the entire mesh for every selected cap.
        const auto *begin = snapshot.value.faces.pData;
        const auto *end = begin + snapshot.value.faces.nCount;
        const auto *selected = std::lower_bound( begin, end, id,
            []( const geo::mesh_source_face_t &face, u64 value ) { return face.sourceId.value < value; } );
        if ( selected == end || selected->sourceId.value != id ) { return map_status_t::UNKNOWN_OBJECT; }
        const auto point = [&]( u32 k ) -> const geo::mesh_source_vertex_t & {
            return snapshot.value.vertices.pData[snapshot.value.corners.pData[selected->iFirstCorner + k].iVertex];
        };
        if ( id == faces.front() ) {
            vec3 area{}; const vec3 origin = point( 0 ).position;
            for ( u32 k = 1; k + 1 < selected->cCorners; ++k ) {
                area = Add( area, Cross( Sub( point( k ).position, origin ), Sub( point( k + 1 ).position, origin ) ) );
            }
            normal = Unit( area );
        }
        for ( u32 k = 0; k < selected->cCorners; ++k ) {
            const auto &a = point( k ), &b = point( ( k + 1 ) % selected->cCorners );
            vertices.push_back( a.sourceId );
            edges.push_back( { std::min( a.sourceId.value, b.sourceId.value ), std::max( a.sourceId.value, b.sourceId.value ), a.position, b.position } );
        }
    }
    std::sort( vertices.begin(), vertices.end(), []( id_t a, id_t b ) { return a.value < b.value; } );
    vertices.erase( std::unique( vertices.begin(), vertices.end(), []( id_t a, id_t b ) { return a.value == b.value; } ), vertices.end() );
    std::sort( edges.begin(), edges.end(), []( const edge_t &a, const edge_t &b ) { return a.a != b.a ? a.a < b.a : a.b < b.b; } );
    for ( usize i = 0; i < edges.size(); ) {
        usize end = i + 1;
        while ( end < edges.size() && edges[end].a == edges[i].a && edges[end].b == edges[i].b ) { ++end; }
        if ( end - i == 1 ) { boundary.push_back( { edges[i].from, edges[i].to } ); }
        i = end;
    }
    return map_status_t::OK;
}

bool SampleExtrudeDistance( const map_workspace_t *ws, const state_t &s, const map_tool_event_t &e, f64 &distance ) noexcept
{
    // The point of the pressed normal line nearest the cursor ray. Sampling
    // must not notify: release retires the capture before any observer runs.
    const vec3 n = s.extrudeNormal, d = e.rayDirection, w = Sub( s.extrudeFrom, e.rayOrigin );
    const f64 a = Dot( n, n ), b = Dot( n, d ), c = Dot( d, d ), den = a * c - b * b;
    if ( !std::isfinite( den ) || std::fabs( den ) < 1e-9 ) { return false; }
    distance = Snap( ws, ( b * Dot( d, w ) - c * Dot( n, w ) ) / den, e.modifiers );
    return std::isfinite( distance );
}

bool ExtrudeEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    if ( s.bExtrude && !ExtrudeContextMatches( ws, s ) ) {
        ResetExtrude( s );
        MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
        // Late input belongs to the discarded capture, not a new native drag.
        if ( e.kind != map_tool_event_kind_t::PRESS ) { return true; }
    }
    if ( s.bExtrude && e.kind != map_tool_event_kind_t::CANCEL && e.view.pView != s.extrudeView ) { return true; }
    switch ( e.kind ) {
        case map_tool_event_kind_t::PRESS: {
            ResetExtrude( s );
            surface_hit_t hit{};
            if ( !SurfaceHit( ws, e, hit ) ) { Hint( ws, QStringLiteral( "Extrude: press on a face and drag it out (or in) along its normal" ) ); return false; }
            if ( MapLayers_IsObjectLocked( ws, hit.object ) ) {
                Hint( ws, QStringLiteral( "Extrude: unlock this object's layer before editing it" ) );
                return true;
            }
            if ( hit.kind != map_wire_kind_t::MESH || hit.faceId == 0 ) { return false; } // Native brush push-pull.
            if ( !Writable( ws ) ) { return true; }
            const map_document_t *pressDocument = ws->pDocument;
            const u64 pressGeometryRevision = ws->pDocument->geometry.revision;
            const auto current = MapWorkspace_MeshFaces( ws );
            const bool sameMesh = ws->selectedMeshFaceObject == hit.object && current.nCount != 0;
            const bool selected = sameMesh && MapWorkspace_IsMeshFaceSelected( ws, hit.object, hit.faceId );
            const bool add = ( e.modifiers & Qt::ShiftModifier ) != 0 && current.nCount != 0;
            if ( add && !sameMesh ) {
                Hint( ws, QStringLiteral( "Extrude: add faces within the selected mesh, or click without Shift to choose another mesh" ) );
                return true;
            }
            // Prepare the capture before selection publication. A refused
            // Shift-add or allocation must never fall through to replacement.
            std::vector<u64> next;
            std::vector<id_t> vertices;
            std::vector<std::pair<vec3, vec3>> boundary;
            vec3 normal = hit.normal;
            try {
                if ( selected || add ) { next.assign( current.pData, current.pData + current.nCount ); }
                if ( !selected ) { next.push_back( hit.faceId ); }
                std::sort( next.begin(), next.end() );
                if ( next.size() > 1 ) {
                    const auto status = PrepareExtrudeRegion( ws, hit.object, next, vertices, boundary, normal );
                    if ( status != map_status_t::OK ) {
                        MapNotice_Post( ws, status == map_status_t::OUT_OF_MEMORY ? map_notice_t::ERROR : map_notice_t::WARNING,
                            status == map_status_t::OUT_OF_MEMORY ? QStringLiteral( "Extrude: not enough memory to prepare the face region" ) :
                            QStringLiteral( "Extrude: select one connected coplanar face region with matching normals" ) );
                        return true;
                    }
                }
            } catch ( ... ) {
                MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Extrude: not enough memory to prepare the selected faces" ) );
                return true;
            }
            const u64 active = selected ? ws->selectedMeshFaceId : hit.faceId;
            if ( !MapWorkspace_SetMeshFaces( ws, hit.object, { next.data(), next.size() }, active ) ) {
                Hint( ws, QStringLiteral( "Extrude: the face selection could not be prepared; nothing was extruded" ) );
                return true;
            }
            // Selection observers may change the context. The hit is not
            // permission to extrude unless its own mesh and face were admitted.
            if ( !Writable( ws ) || ws->tool != map_tool_t::EXTRUDE || ws->pDocument != pressDocument ||
                 ws->pDocument->geometry.revision != pressGeometryRevision || MapLayers_IsObjectLocked( ws, hit.object ) ) { return true; }
            const auto faces = MapWorkspace_MeshFaces( ws );
            if ( ws->selectedMeshFaceObject != hit.object || !MapWorkspace_IsMeshFaceSelected( ws, hit.object, hit.faceId ) ||
                 faces.nCount != next.size() || !std::equal( next.begin(), next.end(), faces.pData ) ) { return true; }
            s.extrudeFaces.swap( next );
            s.extrudeVertices.swap( vertices ); s.extrudeBoundary.swap( boundary );
            s.bExtrude = true; s.extrudeMesh = hit.object; s.extrudeFrom = hit.point; s.extrudeNormal = normal; s.extrudeDistance = 0.0;
            s.extrudeView = e.view.pView; s.extrudeDocument = ws->pDocument;
            s.extrudeSelectionRevision = ws->selection.revision; s.extrudeGeometryRevision = ws->pDocument->geometry.revision;
            return true;
        }
        case map_tool_event_kind_t::MOVE: {
            if ( !s.bExtrude ) { return false; }
            if ( !e.bButtonDown ) { return true; }
            f64 distance = 0.0;
            ( void )SampleExtrudeDistance( ws, s, e, distance );
            s.extrudeDistance = std::isfinite( distance ) ? distance : 0.0;
            MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
            return true;
        }
        case map_tool_event_kind_t::RELEASE: {
            if ( !s.bExtrude ) { return false; }
            // Release can be farther than the last MOVE. It must describe the
            // same final geometry that the pointer and the preview indicate.
            f64 distance = 0.0;
            if ( !SampleExtrudeDistance( ws, s, e, distance ) ) {
                ResetExtrude( s ); MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); return true;
            }
            const u64 mesh = s.extrudeMesh;
            const vec3 normal = s.extrudeNormal;
            // Take the IDs before publication invokes view listeners. No
            // callback can invalidate the local input of this edit.
            const std::vector<u64> faces = std::move( s.extrudeFaces );
            const std::vector<id_t> vertices = std::move( s.extrudeVertices );
            ResetExtrude( s );
            if ( std::fabs( distance ) < 1e-6 ) { MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); return true; } // A click picks the face.
            const bool done = Apply( ws, distance > 0.0 ? "Extrude Faces" : "Push Faces", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
                if ( !Keep( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
                if ( faces.size() > 1 ) {
                    return distance > 0 ? MapTopologyEdit_ExtrudeFaceRegionBy( copy, mesh, { faces.data(), faces.size() }, distance ) :
                        MapTopologyEdit_MoveVertices( copy, mesh, { vertices.data(), vertices.size() }, Mul( normal, distance ) );
                }
                return MapTopologyEdit_ExtrudeFaceBy( copy, mesh, faces.front(), Mul( normal, distance ) );
            } );
            // Publication preserves the cap set and active face before the
            // after-snapshot is captured; do not change its active face here.
            if ( !done && ( ws->lastEdit.outcome != map_edit_outcome_t::UNCHANGED || ws->lastEdit.status != map_status_t::OK ) ) {
                MapNotice_Post( ws, map_notice_t::ERROR, ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ?
                QStringLiteral( "Extrude: not enough memory to apply the edit; the map has not changed" ) :
                QStringLiteral( "Extrude: the edit could not be applied; the map has not changed" ) );
            }
            MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
            return true;
        }
        case map_tool_event_kind_t::CANCEL: { const bool had = s.bExtrude; ResetExtrude( s ); if ( had ) { MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); } return had; }
        default: return false;
    }
}

// The dragged faces where they would land, and their distance.
void ExtrudePaint( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    if ( !ExtrudeContextMatches( ws, s ) || std::fabs( s.extrudeDistance ) < 1e-6 ) { return; }
    const QColor colour( 240, 200, 80 );
    for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) {
        const auto &face = ws->wire.faces.pData[f];
        if ( face.id != s.extrudeMesh || std::find( s.extrudeFaces.begin(), s.extrudeFaces.end(), face.faceId ) == s.extrudeFaces.end() ) { continue; }
        const vec3 offset = Mul( s.extrudeNormal, s.extrudeDistance );
        for ( u32 k = 0; k < face.nIndices; ++k ) {
            const vec3 a = ws->wire.points.pData[ws->wire.faceIndices.pData[face.iFirstIndex + k]];
            const vec3 b = ws->wire.points.pData[ws->wire.faceIndices.pData[face.iFirstIndex + ( k + 1 ) % face.nIndices]];
            DrawLine( painter, view, Add( a, offset ), Add( b, offset ), colour, Qt::SolidLine );
            if ( s.extrudeFaces.size() == 1 ) { DrawLine( painter, view, a, Add( a, offset ), colour, Qt::DashLine ); }
        }
    }
    const vec3 offset = Mul( s.extrudeNormal, s.extrudeDistance );
    for ( const auto &edge : s.extrudeBoundary ) {
        DrawLine( painter, view, edge.first, Add( edge.first, offset ), colour, Qt::DashLine );
        DrawLine( painter, view, edge.second, Add( edge.second, offset ), colour, Qt::DashLine );
    }
    QPointF at;
    if ( Project( view, Add( s.extrudeFrom, Mul( s.extrudeNormal, s.extrudeDistance ) ), at ) ) {
        painter.setPen( colour );
        painter.drawText( at + QPointF( 10, -10 ), QStringLiteral( "%1%2" ).arg( s.extrudeDistance > 0 ? QStringLiteral( "+" ) : QString() ).arg( s.extrudeDistance, 0, 'g', 6 ) );
    }
}

/*
================
Topology tools: Knife, Loop Cut, Vertex
================
*/

// The knife's cut plane between two points holds the view ray through each,
// so the cut lands exactly under the straight line drawn between them.
bool KnifeCutPlane( const map_tool_view_t &view, vec3 from, vec3 to, math::planed_t &plane ) noexcept
{
    vec3 normal = view.bOrtho ? Cross( Sub( to, from ), AxisVector( DepthAxis( view.axisU, view.axisV ) ) )
                              : Cross( Sub( from, view.eye ), Sub( to, view.eye ) );
    const f64 length = Length( normal );
    if ( !( length > 1e-12 ) ) { return false; }
    normal = Mul( normal, 1.0 / length );
    plane = { normal, -Dot( normal, from ) };
    return true;
}

// One authored face of a mesh as the views have it: corner identities and
// positions in winding order.
struct knife_face_t {
    u64 faceId{ 0 };
    std::vector<u64> ids;
    std::vector<vec3> points;
};

const geo::mesh_source_t *KnifeCandidateMesh( const knife_candidate_t *candidate, u64 mesh ) noexcept
{
    return candidate != nullptr && candidate->document != nullptr ?
        geo::GeometryDocument_FindMesh( &candidate->document->geometry, { mesh } ) : nullptr;
}

void ResetKnife( state_t &s ) noexcept
{
    s.knife.clear(); s.points.clear(); s.knifeMesh = 0;
    s.knifeCandidate.reset(); s.knifeDocument = nullptr;
    s.knifeGeometryRevision = s.knifeSelectionRevision = s.knifeNextId = s.knifeSourceNextId = 0;
    s.bHover = s.bHoverBlocked = false; s.hover = {};
}

bool KnifeContextMatches( const map_workspace_t *ws, const state_t &s ) noexcept
{
    if ( s.knifeMesh == 0 || !Writable( ws ) || ws->tool != s.tool ||
         ( s.tool != map_tool_t::KNIFE && s.tool != map_tool_t::EDGE_CUT ) ||
         ws->pDocument != s.knifeDocument || ws->pDocument->geometry.revision != s.knifeGeometryRevision ||
         ws->selection.revision != s.knifeSelectionRevision || MapLayers_IsObjectLocked( ws, s.knifeMesh ) ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, s.knifeMesh );
    if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ) { return false; }
    if ( !s.knifeCandidate ) { return object->kind == map_wire_kind_t::MESH; }
    return object->kind == map_wire_kind_t::BRUSH && ws->pDocument->nextId == s.knifeNextId &&
        ws->pDocument->geometry.sourceIds.allocator.next.value == s.knifeSourceNextId &&
        EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) != CY_FALSE;
}

map_status_t PrepareKnifeCandidate( const map_workspace_t *ws, u64 mesh, knife_candidate_t &candidate ) noexcept
{
    auto status = MapEdit_Clone( ws->pDocument, &candidate.document );
    if ( status != map_status_t::OK ) { return status; }
    vector_t<u64> roots{};
    if ( !Vector_Init( &roots, candidate.document->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    status = MapMeshEdit_ConvertBrushes( candidate.document, { &mesh, 1u }, &roots );
    Vector_Shutdown( &roots );
    if ( status != map_status_t::OK ) { return status; }
    const auto *source = KnifeCandidateMesh( &candidate, mesh );
    if ( source == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    auto described = geo::MeshSourceDescription_Init( &candidate.description, candidate.document->pAllocator, { mesh } );
    if ( described == geo::geometry_status_t::OK ) { described = geo::MeshSource_TryDescribe( source, &candidate.description ); }
    return described == geo::geometry_status_t::OK ? map_status_t::OK : described == geo::geometry_status_t::ALLOCATION_FAILED ?
        map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED;
}

// Metadata can change while a path is staged. Verify only geometry and IDs;
// current material/UV/retained fields belong to the fresh confirmation copy.
map_status_t CheckPreparedMeshGeometry( const geo::mesh_source_description_t &prior, const geo::mesh_source_t *source ) noexcept
{
    if ( source == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    geo::mesh_source_description_t fresh{};
    auto status = geo::MeshSourceDescription_Init( &fresh, source->mesh.pAllocator, source->sourceId );
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSource_TryDescribe( source, &fresh ); }
    bool same = status == geo::geometry_status_t::OK;
    same = same && fresh.sourceId.value == prior.sourceId.value && fresh.vertices.nCount == prior.vertices.nCount &&
        fresh.faces.nCount == prior.faces.nCount && fresh.corners.nCount == prior.corners.nCount;
    for ( usize i = 0u; same && i < prior.vertices.nCount; ++i ) {
        same = fresh.vertices.pData[i].sourceId.value == prior.vertices.pData[i].sourceId.value &&
            math::Vec3d_EqualsExact( fresh.vertices.pData[i].position, prior.vertices.pData[i].position );
    }
    for ( usize i = 0u; same && i < prior.faces.nCount; ++i ) {
        same = fresh.faces.pData[i].sourceId.value == prior.faces.pData[i].sourceId.value &&
            fresh.faces.pData[i].iFirstCorner == prior.faces.pData[i].iFirstCorner && fresh.faces.pData[i].cCorners == prior.faces.pData[i].cCorners;
    }
    for ( usize i = 0u; same && i < prior.corners.nCount; ++i ) { same = fresh.corners.pData[i].iVertex == prior.corners.pData[i].iVertex; }
    geo::MeshSourceDescription_Shutdown( &fresh );
    return status == geo::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY :
        same ? map_status_t::OK : map_status_t::GEOMETRY_FAILED;
}

std::vector<knife_face_t> KnifeFaces( const map_workspace_t *ws, u64 mesh, const knife_candidate_t *candidate = nullptr )
{
    std::vector<knife_face_t> faces;
    if ( candidate != nullptr ) {
        const auto &desc = candidate->description;
        for ( usize f = 0u; f < desc.faces.nCount; ++f ) {
            const auto &face = desc.faces.pData[f]; knife_face_t out{ face.sourceId.value, {}, {} };
            for ( u32 k = 0u; k < face.cCorners; ++k ) {
                const auto &vertex = desc.vertices.pData[desc.corners.pData[face.iFirstCorner + k].iVertex];
                out.ids.push_back( vertex.sourceId.value ); out.points.push_back( vertex.position );
            }
            faces.push_back( std::move( out ) );
        }
        return faces;
    }
    const auto &wire = ws->wire;
    for ( usize f = 0; f < wire.faces.nCount; ++f ) {
        const auto &face = wire.faces.pData[f];
        if ( face.id != mesh || face.nIndices < 3 ) { continue; }
        knife_face_t out{ face.faceId, {}, {} };
        bool authored = true;
        for ( u32 k = 0; k < face.nIndices && authored; ++k ) {
            const u32 index = wire.faceIndices.pData[face.iFirstIndex + k];
            authored = wire.pointSourceIds.pData[index] != 0;
            out.ids.push_back( wire.pointSourceIds.pData[index] );
            out.points.push_back( wire.points.pData[index] );
        }
        if ( authored ) { faces.push_back( std::move( out ) ); } // A modifier's evaluated faces carry no identity.
    }
    return faces;
}

bool FaceHasEdge( const knife_face_t &face, u64 a, u64 b ) noexcept
{
    for ( usize k = 0; k < face.ids.size(); ++k ) {
        const u64 u = face.ids[k], v = face.ids[( k + 1 ) % face.ids.size()];
        if ( ( u == a && v == b ) || ( u == b && v == a ) ) { return true; }
    }
    return false;
}

bool FaceHasVertex( const knife_face_t &face, u64 vertex ) noexcept
{
    return std::find( face.ids.begin(), face.ids.end(), vertex ) != face.ids.end();
}

std::vector<usize> KnifePointFaces( const std::vector<knife_face_t> &faces, const geo::mesh_edit_knife_point_t &point )
{
    std::vector<usize> out;
    for ( usize f = 0; f < faces.size(); ++f ) {
        const bool on = point.kind == geo::mesh_knife_point_kind_t::FACE ? faces[f].faceId == point.faceId.value
                      : point.kind == geo::mesh_knife_point_kind_t::EDGE ? FaceHasEdge( faces[f], point.vertexId.value, point.otherVertexId.value )
                                                                         : FaceHasVertex( faces[f], point.vertexId.value );
        if ( on ) { out.push_back( f ); }
    }
    return out;
}

// Fills in the edge and corner crossings between two knife points on one
// mesh by walking face to face along the cut plane, so a click on one face
// and a click several faces away cut everything between them, as Blender's
// knife does. Points that already share a face need nothing in between.
// False when the walk cannot reach `to` (the cut would leave the surface).
bool KnifeBridge( const map_workspace_t *ws, const map_tool_view_t &view, u64 mesh, const geo::mesh_edit_knife_point_t &from,
                  const geo::mesh_edit_knife_point_t &to, std::vector<geo::mesh_edit_knife_point_t> &between,
                  const knife_candidate_t *candidate = nullptr )
{
    using knife_point_t = geo::mesh_edit_knife_point_t;
    between.clear();
    const std::vector<knife_face_t> faces = KnifeFaces( ws, mesh, candidate );
    const std::vector<usize> fromFaces = KnifePointFaces( faces, from ), toFaces = KnifePointFaces( faces, to );
    const auto reaches = [&]( usize face ) { return std::find( toFaces.begin(), toFaces.end(), face ) != toFaces.end(); };
    for ( usize face : fromFaces ) {
        if ( reaches( face ) ) { return true; }
    }
    math::planed_t plane{};
    const vec3 along = Sub( to.position, from.position );
    const f64 span = Dot( along, along );
    if ( span <= 1e-12 || !KnifeCutPlane( view, from.position, to.position, plane ) ) { return false; }
    // Progress runs 0 at `from` to 1 at `to`; every crossing must advance.
    const auto progress = [&]( vec3 p ) { return Dot( Sub( p, from.position ), along ) / span; };
    const auto side = [&]( vec3 p ) { return Dot( plane.normal, p ) + plane.d; };
    const f64 onPlane = 1e-6 * std::max( 1.0, std::sqrt( span ) );
    // The nearest forward crossing of a face's boundary, skipping the edge or
    // corner the walk came in through.
    const auto exitOf = [&]( const knife_face_t &face, u64 entryA, u64 entryB, u64 entryVertex, f64 reached, knife_point_t &exit, f64 &exitProgress ) {
        bool found = false;
        exitProgress = 0.0;
        const usize n = face.ids.size();
        for ( usize k = 0; k < n; ++k ) {
            const u64 ia = face.ids[k], ib = face.ids[( k + 1 ) % n];
            const vec3 pa = face.points[k], pb = face.points[( k + 1 ) % n];
            const f64 da = side( pa ), db = side( pb );
            knife_point_t candidate{};
            if ( std::fabs( da ) <= onPlane ) {
                if ( ia == entryVertex || ia == entryA || ia == entryB ) { continue; }
                candidate.kind = geo::mesh_knife_point_kind_t::VERTEX; candidate.vertexId = { ia }; candidate.position = pa;
            } else if ( std::fabs( db ) > onPlane && ( da < 0.0 ) != ( db < 0.0 ) ) {
                if ( ( ia == entryA && ib == entryB ) || ( ia == entryB && ib == entryA ) ) { continue; }
                const f64 s = da / ( da - db );
                const vec3 point = Add( pa, Mul( Sub( pb, pa ), s ) );
                if ( s < 0.01 || s > 0.99 ) {
                    // The geometry takes no edge points at an edge's ends: that is the corner.
                    const bool nearA = s < 0.5;
                    if ( ( nearA ? ia : ib ) == entryVertex ) { continue; }
                    candidate.kind = geo::mesh_knife_point_kind_t::VERTEX; candidate.vertexId = { nearA ? ia : ib }; candidate.position = nearA ? pa : pb;
                } else {
                    const geo::mesh_edge_ref_t edge = geo::MeshEdgeRef_Make( { ia }, { ib } );
                    candidate.kind = geo::mesh_knife_point_kind_t::EDGE; candidate.vertexId = edge.a; candidate.otherVertexId = edge.b;
                    candidate.t = edge.a.value == ia ? s : 1.0 - s; candidate.position = point;
                }
            } else {
                continue;
            }
            const f64 p = progress( candidate.position );
            if ( p <= reached + 1e-9 || ( found && p >= exitProgress ) ) { continue; }
            exit = candidate; exitProgress = p; found = true;
        }
        return found;
    };
    for ( usize start : fromFaces ) {
        std::vector<knife_point_t> path;
        std::vector<usize> visited{ start };
        usize current = start;
        u64 entryA = from.kind == geo::mesh_knife_point_kind_t::EDGE ? from.vertexId.value : 0;
        u64 entryB = from.kind == geo::mesh_knife_point_kind_t::EDGE ? from.otherVertexId.value : 0;
        u64 entryVertex = from.kind == geo::mesh_knife_point_kind_t::VERTEX ? from.vertexId.value : 0;
        f64 reached = 0.0;
        for ( int step = 0; step < 512; ++step ) {
            if ( reaches( current ) ) { between = std::move( path ); return true; }
            knife_point_t exit{};
            f64 exitProgress = 0.0;
            if ( !exitOf( faces[current], entryA, entryB, entryVertex, reached, exit, exitProgress ) || exitProgress > 1.0 + 1e-6 ) { break; }
            path.push_back( exit );
            reached = exitProgress;
            usize next = faces.size();
            if ( exit.kind == geo::mesh_knife_point_kind_t::EDGE ) {
                for ( usize f = 0; f < faces.size() && next == faces.size(); ++f ) {
                    if ( f != current && FaceHasEdge( faces[f], exit.vertexId.value, exit.otherVertexId.value ) ) { next = f; }
                }
                entryA = exit.vertexId.value; entryB = exit.otherVertexId.value; entryVertex = 0;
            } else {
                // Through a corner: on into the face around it that goes on toward `to`.
                for ( usize f = 0; f < faces.size() && next == faces.size(); ++f ) {
                    if ( f == current || !FaceHasVertex( faces[f], exit.vertexId.value ) ||
                         std::find( visited.begin(), visited.end(), f ) != visited.end() ) { continue; }
                    knife_point_t ahead{};
                    f64 aheadProgress = 0.0;
                    if ( reaches( f ) || exitOf( faces[f], 0, 0, exit.vertexId.value, reached, ahead, aheadProgress ) ) { next = f; }
                }
                entryA = 0; entryB = 0; entryVertex = exit.vertexId.value;
            }
            if ( next == faces.size() || std::find( visited.begin(), visited.end(), next ) != visited.end() ) { break; }
            visited.push_back( next );
            current = next;
        }
    }
    return false;
}

// A knife end inside a face would leave a dangling edge. The cut carries on
// along its line to that face's boundary instead, so two clicks inside a face
// cut straight across it, the way the line between them is drawn.
bool ExtendKnifeEnd( const std::vector<knife_face_t> &faces, const geo::mesh_edit_knife_point_t &end, vec3 direction,
                     geo::mesh_edit_knife_point_t &out ) noexcept
{
    const auto face = std::find_if( faces.begin(), faces.end(), [&]( const knife_face_t &f ) { return f.faceId == end.faceId.value; } );
    if ( face == faces.end() ) { return false; }
    // The face's normal (Newell), and the direction laid into its plane.
    vec3 normal{};
    const usize n = face->points.size();
    for ( usize k = 0; k < n; ++k ) {
        const vec3 a = face->points[k], b = face->points[( k + 1 ) % n];
        normal.x += ( a.y - b.y ) * ( a.z + b.z ); normal.y += ( a.z - b.z ) * ( a.x + b.x ); normal.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    if ( Length( normal ) <= 1e-12 ) { return false; }
    normal = Unit( normal );
    direction = Sub( direction, Mul( normal, Dot( normal, direction ) ) );
    if ( Length( direction ) <= 1e-12 ) { return false; }
    f64 best = 0.0;
    bool found = false;
    for ( usize k = 0; k < n; ++k ) {
        const u64 ia = face->ids[k], ib = face->ids[( k + 1 ) % n];
        const vec3 a = face->points[k], b = face->points[( k + 1 ) % n];
        const vec3 edge = Sub( b, a );
        const f64 denominator = Dot( Cross( direction, edge ), normal );
        if ( std::fabs( denominator ) <= 1e-12 ) { continue; } // Parallel to this side.
        const f64 s = -Dot( Cross( Sub( end.position, a ), edge ), normal ) / denominator;
        const vec3 hit = Add( end.position, Mul( direction, s ) );
        const f64 u = Dot( Sub( hit, a ), edge ) / Dot( edge, edge );
        if ( s <= 1e-9 || u < -1e-9 || u > 1.0 + 1e-9 || ( found && s >= best ) ) { continue; }
        best = s; found = true;
        out = {};
        if ( u < 0.01 || u > 0.99 ) {
            out.kind = geo::mesh_knife_point_kind_t::VERTEX; out.vertexId = { u < 0.5 ? ia : ib }; out.position = u < 0.5 ? a : b;
        } else {
            const geo::mesh_edge_ref_t ref = geo::MeshEdgeRef_Make( { ia }, { ib } );
            out.kind = geo::mesh_knife_point_kind_t::EDGE; out.vertexId = ref.a; out.otherVertexId = ref.b;
            out.t = ref.a.value == ia ? u : 1.0 - u; out.position = hit;
        }
    }
    return found;
}

// Knife keeps its wider corner snap, but a 3D corner must be a visible
// endpoint. Validate incident authored edges at the projected endpoint using
// the same viewport query; a raw presentation point alone is not a visible hit.
edge_result_t KnifeEdge( const map_workspace_t *ws, const map_tool_event_t &e, f64 tolerance, edge_hit_t &out,
    u64 mesh, const math::planed_t *plane, const knife_candidate_t *candidate ) noexcept
{
    if ( candidate == nullptr ) { return NearestMeshEdge( ws, e, tolerance, out, mesh, plane ); }
    out = {};
    if ( e.view.pfnPickEdge == nullptr ) { return edge_result_t::FAILED; }
    map_tool_edge_query_t query{ e.screen, tolerance, mesh, plane };
    query.pCandidateMesh = KnifeCandidateMesh( candidate, mesh );
    return query.pCandidateMesh != nullptr ? e.view.pfnPickEdge( e.view.pView, query, &out ) : edge_result_t::FAILED;
}

edge_result_t KnifeVertex( const map_workspace_t *ws, const map_tool_event_t &e, u64 onlyMesh,
    const math::planed_t *plane, point_hit_t &out, const knife_candidate_t *candidate = nullptr ) noexcept
{
    const auto onPlane = [&]( vec3 p ) { return plane == nullptr || std::fabs( Dot( plane->normal, p ) + plane->d ) <= 1e-3; };
    if ( candidate != nullptr ) {
        out = {};
        const auto *source = KnifeCandidateMesh( candidate, onlyMesh );
        if ( source == nullptr || e.view.pfnPickEdge == nullptr ) { return edge_result_t::FAILED; }
        const auto &desc = candidate->description;
        f64 best = kPickPixels;
        for ( usize v = 0u; v < desc.vertices.nCount; ++v ) {
            const auto &vertex = desc.vertices.pData[v]; QPointF screen;
            if ( !onPlane( vertex.position ) || !Project( e.view, vertex.position, screen ) ) { continue; }
            const f64 distance = std::hypot( screen.x() - e.screen.x(), screen.y() - e.screen.y() );
            if ( distance >= best ) { continue; }
            bool visible = false;
            for ( usize f = 0u; f < desc.faces.nCount && !visible; ++f ) {
                const auto &face = desc.faces.pData[f];
                for ( u32 k = 0u; k < face.cCorners && !visible; ++k ) {
                    const u32 a = desc.corners.pData[face.iFirstCorner + k].iVertex;
                    const u32 b = desc.corners.pData[face.iFirstCorner + ( k + 1u ) % face.cCorners].iVertex;
                    if ( a != v && b != v ) { continue; }
                    const auto edge = geo::MeshEdgeRef_Make( desc.vertices.pData[a].sourceId, desc.vertices.pData[b].sourceId );
                    map_tool_edge_query_t query{ screen, 1e-6, onlyMesh, nullptr, &edge, 1u }; query.pCandidateMesh = source;
                    edge_hit_t hit{}; const auto result = e.view.pfnPickEdge( e.view.pView, query, &hit );
                    if ( result == edge_result_t::FAILED ) { out = {}; return result; }
                    visible = result == edge_result_t::HIT &&
                        ( ( hit.edge.a.value == vertex.sourceId.value && hit.t <= 1e-6 ) ||
                          ( hit.edge.b.value == vertex.sourceId.value && hit.t >= 1.0 - 1e-6 ) );
                }
            }
            if ( visible ) { best = distance; out = { onlyMesh, 0u, vertex.sourceId.value, vertex.position }; }
        }
        return out.sourceId != 0u ? edge_result_t::HIT : edge_result_t::MISS;
    }
    const auto accepts = [&]( const map_wire_object_t &object ) { return object.kind == map_wire_kind_t::MESH && ( onlyMesh == 0u || object.id == onlyMesh ); };
    if ( e.view.pfnPickEdge == nullptr || e.view.bOrtho ) {
        return NearestPoint( ws, e, kPickPixels, accepts, onPlane, out ) && out.sourceId != 0u ? edge_result_t::HIT : edge_result_t::MISS;
    }
    out = {};
    f64 best = kPickPixels;
    const auto &wire = ws->wire;
    for ( usize i = 0u; i < wire.objects.nCount; ++i ) {
        const auto &object = wire.objects.pData[i];
        if ( !accepts( object ) || !MapWorkspace_IsVisible( ws, object ) ) { continue; }
        for ( u32 k = 0u; k < object.nPoints; ++k ) {
            const u32 index = object.iFirstPoint + k;
            const u64 id = wire.pointSourceIds.pData[index];
            const vec3 position = wire.points.pData[index];
            QPointF screen;
            if ( id == 0u || !onPlane( position ) || !Project( e.view, position, screen ) ) { continue; }
            const f64 distance = std::hypot( screen.x() - e.screen.x(), screen.y() - e.screen.y() );
            if ( distance >= best ) { continue; }
            bool visible = false;
            for ( u32 l = 0u; l < object.nLines; ++l ) {
                const auto &line = wire.lines.pData[object.iFirstLine + l];
                const u64 a = wire.pointSourceIds.pData[line.iA], b = wire.pointSourceIds.pData[line.iB];
                if ( a == 0u || b == 0u || a == b || ( a != id && b != id ) ) { continue; }
                const auto incident = geo::MeshEdgeRef_Make( { a }, { b } );
                edge_hit_t hit{};
                const auto result = e.view.pfnPickEdge( e.view.pView, { screen, 1e-6, object.id, nullptr, &incident, 1u }, &hit );
                if ( result == edge_result_t::FAILED ) { out = {}; return result; }
                // A projected-degenerate edge may return its other, nearer
                // endpoint. That does not make this rear corner visible.
                visible = result == edge_result_t::HIT &&
                    ( ( hit.edge.a.value == id && hit.t <= 1e-6 ) || ( hit.edge.b.value == id && hit.t >= 1.0 - 1e-6 ) );
                if ( visible ) { break; }
            }
            if ( visible ) { best = distance; out = { object.id, index, id, position }; }
        }
    }
    return out.sourceId != 0u ? edge_result_t::HIT : edge_result_t::MISS;
}

// The Edge Cut tool is the knife with every point on an edge or corner.
bool EdgeCutPoint( const map_workspace_t *ws, const state_t &s, const map_tool_event_t &e, geo::mesh_edit_knife_point_t &point,
    u64 &mesh, const knife_candidate_t *candidate = nullptr ) noexcept
{
    // The face under the cursor filters what a 2D pane stacks on one pixel.
    surface_hit_t hit{};
    const u64 candidateMesh = candidate != nullptr ? candidate->description.sourceId.value : 0u;
    const bool surface = SurfaceHit( ws, e, hit, candidate == nullptr ) && ( s.knifeMesh == 0 || hit.object == s.knifeMesh ) &&
        ( candidateMesh == 0u || hit.object == candidateMesh );
    const math::planed_t facePlane{ hit.normal, -Dot( hit.normal, hit.point ) };
    const math::planed_t *plane = surface ? &facePlane : nullptr;
    const u64 only = candidateMesh != 0u ? candidateMesh : s.knifeMesh != 0 ? s.knifeMesh : surface ? hit.object : 0;
    point_hit_t vertex{};
    edge_hit_t edge{};
    const auto edgeResult = KnifeEdge( ws, e, kEdgePixels, edge, only, plane, candidate );
    if ( edgeResult == edge_result_t::FAILED ) { return false; }
    const auto vertexResult = KnifeVertex( ws, e, only, plane, vertex, candidate );
    if ( vertexResult == edge_result_t::FAILED ) { return false; }
    if ( vertexResult == edge_result_t::HIT ) {
        point = {}; point.kind = geo::mesh_knife_point_kind_t::VERTEX; point.vertexId = { vertex.sourceId }; point.position = vertex.position;
        mesh = vertex.object;
        return true;
    }
    if ( edgeResult != edge_result_t::HIT ) { return false; }
    const f64 t = SnapBypassed( e.modifiers ) ? 0.5 : edge.t;
    if ( t <= 0.01 || t >= 0.99 ) { return false; }
    point = {}; point.kind = geo::mesh_knife_point_kind_t::EDGE; point.vertexId = edge.edge.a; point.otherVertexId = edge.edge.b;
    point.t = t; point.position = Add( edge.a, Mul( Sub( edge.b, edge.a ), t ) );
    mesh = edge.mesh;
    return true;
}

// Where a click would put the knife's next point: a corner, a point on an
// edge, or (Knife only) a point inside the face under the cursor. Hover and
// click use this one function, so the preview shows where a click lands.
bool KnifePointAt( map_workspace_t *ws, const state_t &s, const map_tool_event_t &e, bool edgesOnly,
                   geo::mesh_edit_knife_point_t &point, u64 &mesh, const knife_candidate_t *candidate ) noexcept
{
    // Check visibility-query failure before a vertex/face shortcut or a brush
    // conversion. In particular FAILED must not become an interior-face cut.
    if ( e.view.pfnPickEdge != nullptr ) {
        edge_hit_t probe{};
        if ( KnifeEdge( ws, e, kPickPixels, probe, candidate != nullptr ? candidate->description.sourceId.value : s.knifeMesh,
                nullptr, candidate ) == edge_result_t::FAILED ) { return false; }
    }
    if ( edgesOnly ) { return EdgeCutPoint( ws, s, e, point, mesh, candidate ); }
    surface_hit_t hit{};
    const bool surface = candidate != nullptr ?
        ( SurfaceHit( ws, e, hit ) && hit.kind == map_wire_kind_t::BRUSH && hit.object == candidate->description.sourceId.value ) :
        MeshUnder( ws, e, hit, false );
    if ( !surface || ( s.knifeMesh != 0 && hit.object != s.knifeMesh ) ) {
        // Exactly on an edge the ray sits on the border of both faces
        // and lands in neither: take the edge or corner under the cursor.
        return EdgeCutPoint( ws, s, e, point, mesh, candidate ) && ( s.knifeMesh == 0 || mesh == s.knifeMesh );
    }
    point = {};
    point_hit_t vertex{};
    edge_hit_t edge{};
    mesh = hit.object;
    // Only vertices and edges of the clicked face's plane: in a 2D
    // pane a corner behind it projects to the same pixel.
    const math::planed_t facePlane{ hit.normal, -Dot( hit.normal, hit.point ) };
    const u64 target = mesh;
    const auto edgeResult = KnifeEdge( ws, e, kEdgePixels, edge, target, &facePlane, candidate );
    if ( edgeResult == edge_result_t::FAILED ) { return false; }
    const auto vertexResult = KnifeVertex( ws, e, target, &facePlane, vertex, candidate );
    if ( vertexResult == edge_result_t::FAILED ) { return false; }
    if ( vertexResult == edge_result_t::HIT ) {
        point.kind = geo::mesh_knife_point_kind_t::VERTEX; point.vertexId = { vertex.sourceId }; point.position = vertex.position;
    } else if ( edgeResult == edge_result_t::HIT && edge.t > 0.01 && edge.t < 0.99 ) {
        point.kind = geo::mesh_knife_point_kind_t::EDGE; point.vertexId = edge.edge.a; point.otherVertexId = edge.edge.b;
        point.t = edge.t; point.position = Add( edge.a, Mul( Sub( edge.b, edge.a ), edge.t ) );
    } else {
        point.kind = geo::mesh_knife_point_kind_t::FACE; point.faceId = { candidate != nullptr ? hit.sideId : hit.faceId }; point.position = hit.point;
    }
    return true;
}

bool KnifeEventImpl( map_workspace_t *ws, state_t &s, const map_tool_event_t &e, bool edgesOnly )
{
    const QString name = edgesOnly ? QStringLiteral( "Edge Cut" ) : QStringLiteral( "Knife" );
    if ( s.knifeMesh != 0u && !KnifeContextMatches( ws, s ) ) {
        ResetKnife( s );
        Hint( ws, QStringLiteral( "%1: the target changed; start a new cut" ).arg( name ) );
        if ( e.kind != map_tool_event_kind_t::PRESS ) { return true; }
    }
    switch ( e.kind ) {
        case map_tool_event_kind_t::MOVE: {
            geo::mesh_edit_knife_point_t point{}; u64 mesh = 0;
            s.bHover = KnifePointAt( ws, s, e, edgesOnly, point, mesh, s.knifeCandidate.get() );
            s.hover = point.position;
            std::vector<geo::mesh_edit_knife_point_t> between;
            s.bHoverBlocked = s.bHover && !s.knife.empty() &&
                ( mesh != s.knifeMesh || !KnifeBridge( ws, e.view, mesh, s.knife.back(), point, between, s.knifeCandidate.get() ) );
            return true;
        }
        case map_tool_event_kind_t::PRESS: {
            if ( !Writable( ws ) ) { return true; }
            const auto *document = ws->pDocument;
            const u64 geometryRevision = document->geometry.revision, selectionRevision = ws->selection.revision;
            const u64 nextId = document->nextId, sourceNextId = document->geometry.sourceIds.allocator.next.value;
            auto candidate = s.knifeCandidate;
            if ( s.knifeMesh == 0u ) {
                surface_hit_t surface{};
                if ( SurfaceHit( ws, e, surface ) && surface.kind == map_wire_kind_t::BRUSH ) {
                    if ( MapLayers_IsObjectLocked( ws, surface.object ) ) {
                        Hint( ws, QStringLiteral( "%1: unlock the object's layer before cutting it" ).arg( name ) ); return true;
                    }
                    if ( EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) == CY_FALSE ) {
                        Hint( ws, QStringLiteral( "%1: enable brush conversion in the component settings to cut this brush" ).arg( name ) ); return true;
                    }
                    if ( e.view.pfnPickEdge == nullptr ) {
                        Hint( ws, QStringLiteral( "%1: this view cannot query the brush's edges" ).arg( name ) ); return true;
                    }
                    candidate = std::make_shared<knife_candidate_t>();
                    const auto prepared = PrepareKnifeCandidate( ws, surface.object, *candidate );
                    if ( prepared != map_status_t::OK ) {
                        MapNotice_Post( ws, map_notice_t::ERROR, prepared == map_status_t::OUT_OF_MEMORY ?
                            QStringLiteral( "%1: not enough memory to prepare the cut; the map has not changed" ).arg( name ) :
                            QStringLiteral( "%1: this brush could not be prepared for cutting; the map has not changed" ).arg( name ) );
                        return true;
                    }
                }
            }
            geo::mesh_edit_knife_point_t point{}; u64 mesh = 0;
            if ( !KnifePointAt( ws, s, e, edgesOnly, point, mesh, candidate.get() ) ) {
                Hint( ws, edgesOnly ? QStringLiteral( "Edge Cut: click on edges or corners; Enter cuts between them" ) :
                    s.knifeMesh == 0u ? QStringLiteral( "Knife: click across a mesh or brush; Enter cuts, Escape cancels" ) :
                    QStringLiteral( "Knife: keep the cut on the same object; Enter cuts, Backspace removes the last point" ) );
                return true;
            }
            if ( MapLayers_IsObjectLocked( ws, mesh ) ) {
                Hint( ws, QStringLiteral( "%1: unlock the object's layer before cutting it" ).arg( name ) ); return true;
            }
            if ( ws->pDocument != document || document->geometry.revision != geometryRevision ||
                 ws->selection.revision != selectionRevision || document->nextId != nextId ||
                 document->geometry.sourceIds.allocator.next.value != sourceNextId ) { return true; }
            std::vector<geo::mesh_edit_knife_point_t> between;
            if ( !s.knife.empty() && ( mesh != s.knifeMesh ||
                 !KnifeBridge( ws, e.view, mesh, s.knife.back(), point, between, candidate.get() ) ) ) {
                Hint( ws, QStringLiteral( "%1: a straight cut cannot reach that point across these faces; choose another point" ).arg( name ) ); return true;
            }
            // Prepare both arrays before publication: allocation failure must
            // not leave a crossing in one array and no matching overlay point.
            auto path = s.knife; auto points = s.points;
            for ( const auto &crossing : between ) { path.push_back( crossing ); points.push_back( crossing.position ); }
            path.push_back( point ); points.push_back( point.position );
            s.knife.swap( path ); s.points.swap( points ); s.knifeMesh = mesh; s.knifeCandidate = std::move( candidate );
            s.knifeDocument = document; s.knifeGeometryRevision = geometryRevision; s.knifeSelectionRevision = selectionRevision;
            s.knifeNextId = nextId; s.knifeSourceNextId = sourceNextId; s.bHoverBlocked = false;
            return true;
        }
        case map_tool_event_kind_t::CONFIRM: {
            if ( s.knife.size() < 2u ) {
                if ( !s.knife.empty() ) { Hint( ws, QStringLiteral( "%1: click a second point, then Enter" ).arg( name ) ); return true; }
                return false;
            }
            if ( !KnifeContextMatches( ws, s ) ) { ResetKnife( s ); return true; }
            auto path = s.knife; const u64 mesh = s.knifeMesh;
            const auto candidate = s.knifeCandidate;
            if ( !edgesOnly ) {
                const auto faces = KnifeFaces( ws, mesh, candidate.get() );
                geo::mesh_edit_knife_point_t boundary{};
                if ( path.front().kind == geo::mesh_knife_point_kind_t::FACE &&
                     ExtendKnifeEnd( faces, path.front(), Sub( path[0].position, path[1].position ), boundary ) ) { path.front() = boundary; }
                if ( path.back().kind == geo::mesh_knife_point_kind_t::FACE &&
                     ExtendKnifeEnd( faces, path.back(), Sub( path.back().position, path[path.size() - 2u].position ), boundary ) ) { path.back() = boundary; }
            }
            bool contextChanged = false;
            const bool done = ApplyMetadata( ws, edgesOnly ? "Edge Cut" : "Knife Cut",
                [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
                    *changed = false;
                    if ( !KnifeContextMatches( ws, s ) ) { contextChanged = true; return map_status_t::INVALID_ARGUMENT; }
                    if ( !Keep( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
                    if ( candidate ) {
                        vector_t<u64> roots{};
                        if ( !Vector_Init( &roots, copy->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
                        auto status = MapMeshEdit_ConvertBrushes( copy, { &mesh, 1u }, &roots );
                        Vector_Shutdown( &roots );
                        if ( status == map_status_t::OK ) {
                            status = CheckPreparedMeshGeometry( candidate->description, geo::GeometryDocument_FindMesh( &copy->geometry, { mesh } ) );
                        }
                        if ( status != map_status_t::OK ) { return status; }
                    }
                    if ( !KnifeContextMatches( ws, s ) ) { contextChanged = true; return map_status_t::INVALID_ARGUMENT; }
                    const u64 revision = copy->geometry.revision; // Provisional conversion alone is not an edit.
                    const auto status = MapTopologyEdit_Knife( copy, mesh, { path.data(), path.size() } );
                    if ( status == map_status_t::OK ) { *changed = copy->geometry.revision != revision; }
                    return status;
                } );
            if ( done ) { ResetKnife( s ); }
            else if ( contextChanged ) { ResetKnife( s ); Hint( ws, QStringLiteral( "%1: the target changed; the cut was canceled" ).arg( name ) ); }
            else if ( ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ) {
                MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "%1: not enough memory to apply the cut; the map has not changed; Enter retries" ).arg( name ) );
            } else if ( ws->lastEdit.outcome == map_edit_outcome_t::UNCHANGED && ws->lastEdit.status == map_status_t::OK ) {
                Hint( ws, QStringLiteral( "%1: this path does not change the geometry; the map has not changed" ).arg( name ) );
            } else {
                MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "%1: this path could not be cut; use flat faces and avoid crossing the cut; Backspace removes the last point" ).arg( name ) );
            }
            return true;
        }
        case map_tool_event_kind_t::BACKSPACE:
            if ( s.knife.empty() ) { return false; }
            s.knife.pop_back(); s.points.pop_back(); if ( s.knife.empty() ) { ResetKnife( s ); }
            return true;
        case map_tool_event_kind_t::CANCEL: {
            const bool had = !s.knife.empty(); ResetKnife( s ); return had;
        }
        default: return false;
    }
}

bool KnifeEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e, bool edgesOnly ) noexcept
{
    try { return KnifeEventImpl( ws, s, e, edgesOnly ); }
    catch ( ... ) {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Knife: not enough memory to prepare the cut; the map has not changed" ) );
        return true;
    }
}

bool LoopCutEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    if ( e.kind == map_tool_event_kind_t::MOVE ) {
        s.bLoop = NearestMeshEdge( ws, e, kPickPixels, s.loop ) == edge_result_t::HIT;
        if ( s.bLoop && MapLayers_IsObjectLocked( ws, s.loop.mesh ) ) { s.bLoop = false; }
        if ( s.bLoop && SnapBypassed( e.modifiers ) ) { s.loop.t = 0.5; } // Ctrl/Command centres the cut.
        return true;
    }
    if ( e.kind != map_tool_event_kind_t::PRESS ) { return false; }
    s.bLoop = false;
    if ( !Writable( ws ) ) { return true; }
    const auto *document = ws->pDocument;
    const u64 geometryRevision = document->geometry.revision, selectionRevision = ws->selection.revision;
    edge_hit_t edge{};
    const auto edgeResult = NearestMeshEdge( ws, e, kPickPixels, edge );
    if ( edgeResult == edge_result_t::FAILED ) {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Loop Cut: visible edges could not be queried; the map has not changed" ) );
        return true;
    }
    u64 target = edge.mesh;
    const bool convert = edgeResult == edge_result_t::MISS;
    if ( edgeResult == edge_result_t::MISS ) {
        surface_hit_t hit{};
        if ( !SurfaceHit( ws, e, hit ) || hit.kind != map_wire_kind_t::BRUSH ) {
            Hint( ws, QStringLiteral( "Loop Cut: click on (or near) an edge; the cut runs round the faces on either side of it" ) );
            return true;
        }
        target = hit.object;
    }
    if ( MapLayers_IsObjectLocked( ws, target ) ) {
        Hint( ws, QStringLiteral( "Loop Cut: unlock the object's layer before cutting it" ) ); return true;
    }
    if ( convert && EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) == CY_FALSE ) {
        Hint( ws, QStringLiteral( "Loop Cut: enable brush conversion in the component settings to cut this brush" ) ); return true;
    }
    if ( convert && e.view.pfnPickEdge == nullptr ) {
        // Only native panes can validate a provisional mesh against the live
        // scene's clipping and occlusion. Projection-only injected views still
        // support existing meshes, but must not guess a converted brush edge.
        Hint( ws, QStringLiteral( "Loop Cut: this view cannot query the brush's edges" ) ); return true;
    }
    const auto contextMatches = [&]() noexcept {
        if ( !Writable( ws ) || ws->tool != map_tool_t::LOOP_CUT || ws->pDocument != document ||
             ws->pDocument->geometry.revision != geometryRevision || ws->selection.revision != selectionRevision ||
             MapLayers_IsObjectLocked( ws, target ) ) { return false; }
        const auto *object = MapWireframe_FindObject( ws->wire, target );
        return object != nullptr && object->kind == ( convert ? map_wire_kind_t::BRUSH : map_wire_kind_t::MESH ) &&
            MapWorkspace_IsVisible( ws, *object ) && ( !convert ||
                EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) != CY_FALSE );
    };
    if ( !contextMatches() ) { return true; }
    auto candidateResult = edge_result_t::HIT;
    bool topologyAttempted = false, contextChanged = false;
    // Conversion, edge identity resolution and cutting share this one private
    // copy. A miss or rejected ring cannot publish conversion by itself.
    const bool cut = ApplyMetadata( ws, "Loop Cut", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        *changed = false;
        if ( !contextMatches() ) { contextChanged = true; return map_status_t::INVALID_ARGUMENT; }
        if ( !Keep( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        if ( convert ) {
            vector_t<u64> roots{};
            if ( !Vector_Init( &roots, copy->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
            const auto status = MapMeshEdit_ConvertBrushes( copy, { &target, 1 }, &roots );
            Vector_Shutdown( &roots );
            if ( status != map_status_t::OK ) { return status; }
            const auto *candidate = geo::GeometryDocument_FindMesh( &copy->geometry, { target } );
            if ( candidate == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
            map_tool_edge_query_t query{ e.screen, kPickPixels, target };
            query.pCandidateMesh = candidate;
            candidateResult = e.view.pfnPickEdge( e.view.pView, query, &edge );
            if ( candidateResult == edge_result_t::MISS ) { return map_status_t::OK; }
            if ( candidateResult == edge_result_t::FAILED || edge.mesh != target || !std::isfinite( edge.t ) ) {
                candidateResult = edge_result_t::FAILED; return map_status_t::INVALID_ARGUMENT;
            }
        }
        if ( !contextMatches() ) { contextChanged = true; return map_status_t::INVALID_ARGUMENT; }
        const u64 cutRevision = copy->geometry.revision; // After any provisional conversion.
        const f64 t = SnapBypassed( e.modifiers ) ? 0.5 : std::clamp( edge.t, 0.05, 0.95 );
        topologyAttempted = true;
        const auto status = MapTopologyEdit_LoopCut( copy, target, edge.edge, t );
        if ( status == map_status_t::OK ) { *changed = copy->geometry.revision != cutRevision; }
        return status;
    } );
    if ( !cut ) {
        if ( ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ) {
            MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Loop Cut: not enough memory to apply the cut; the map has not changed" ) );
        } else if ( contextChanged ) {
            Hint( ws, QStringLiteral( "Loop Cut: the target changed while preparing the cut; the cut was canceled" ) );
        } else if ( candidateResult == edge_result_t::MISS ) {
            Hint( ws, QStringLiteral( "Loop Cut: click closer to an edge; the map has not changed" ) );
        } else if ( candidateResult == edge_result_t::FAILED ) {
            MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Loop Cut: visible edges could not be queried; the map has not changed" ) );
        } else if ( topologyAttempted && ws->lastEdit.status == map_status_t::GEOMETRY_FAILED ) {
            MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Loop Cut: this edge could not be cut as a ring of flat four-sided faces; the map has not changed" ) );
        } else if ( ws->lastEdit.outcome != map_edit_outcome_t::UNCHANGED || ws->lastEdit.status != map_status_t::OK ) {
            MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Loop Cut: the cut could not be applied; the map has not changed" ) );
        }
    }
    return true;
}

// Where the free (centre) handle is on the floor plane through `anchor`, or
// straight above or below it with Alt; an edge-on floor falls back to the
// upright plane facing the camera (as UpdateComponentDrag).
bool FreeGizmoPoint( const map_tool_event_t &e, vec3 anchor, bool vertical, vec3 &out ) noexcept
{
    if ( vertical || std::fabs( e.rayDirection.z ) < 0.2 ) {
        vec3 facing{ -e.rayDirection.x, -e.rayDirection.y, 0.0 };
        if ( Length( facing ) < 1e-6 ) { facing = { 1, 0, 0 }; }
        facing = Mul( facing, 1.0 / Length( facing ) );
        if ( !RayPlane( e.rayOrigin, e.rayDirection, { facing, -Dot( facing, anchor ) }, out ) ) { return false; }
        if ( vertical ) { out.x = anchor.x; out.y = anchor.y; } else { out.z = anchor.z; }
        return true;
    }
    return RayPlane( e.rayOrigin, e.rayDirection, { { 0, 0, 1 }, -anchor.z }, out );
}

// The vertices one drag moves: the grabbed one, or the whole component
// selection of its mesh when it is part of it.
std::vector<id_t> DraggedVertices( const map_workspace_t *ws, u64 mesh, u64 grabbed )
{
    std::vector<id_t> out;
    const auto &components = ws->meshSelection;
    bool inSelection = false;
    if ( components.meshId.value == mesh ) {
        for ( usize i = 0; i < components.vertices.nCount; ++i ) { inSelection |= components.vertices.pData[i].value == grabbed; }
        for ( usize i = 0; i < components.edges.nCount; ++i ) {
            inSelection |= components.edges.pData[i].a.value == grabbed || components.edges.pData[i].b.value == grabbed;
        }
    }
    if ( !inSelection ) { out.push_back( { grabbed } ); return out; }
    std::vector<u64> ids;
    for ( usize i = 0; i < components.vertices.nCount; ++i ) { ids.push_back( components.vertices.pData[i].value ); }
    for ( usize i = 0; i < components.edges.nCount; ++i ) { ids.push_back( components.edges.pData[i].a.value ); ids.push_back( components.edges.pData[i].b.value ); }
    std::sort( ids.begin(), ids.end() ); ids.erase( std::unique( ids.begin(), ids.end() ), ids.end() );
    for ( u64 id : ids ) { out.push_back( { id } ); }
    return out;
}

// What a 2D view shows stacked on `at` in one object: its distinct corners
// (brush) or vertex IDs (mesh) with the same position across the view.
std::vector<vec3> ColumnPoints( const map_workspace_t *ws, u64 object, vec3 at, const map_tool_view_t &view )
{
    std::vector<vec3> out;
    const auto *o = MapWireframe_FindObject( ws->wire, object );
    for ( u32 k = 0; o != nullptr && view.bOrtho && k < o->nPoints; ++k ) {
        const vec3 p = ws->wire.points.pData[o->iFirstPoint + k];
        if ( std::fabs( Comp( p, view.axisU ) - Comp( at, view.axisU ) ) > 1e-6 || std::fabs( Comp( p, view.axisV ) - Comp( at, view.axisV ) ) > 1e-6 ) { continue; }
        if ( std::none_of( out.begin(), out.end(), [&]( vec3 q ) { return Length( Sub( p, q ) ) <= 1e-6; } ) ) { out.push_back( p ); }
    }
    return out;
}

std::vector<id_t> ColumnVertices( const map_workspace_t *ws, u64 mesh, vec3 at, const map_tool_view_t &view )
{
    std::vector<id_t> out;
    const auto *o = MapWireframe_FindObject( ws->wire, mesh );
    for ( u32 k = 0; o != nullptr && view.bOrtho && k < o->nPoints; ++k ) {
        const u64 id = ws->wire.pointSourceIds.pData[o->iFirstPoint + k];
        const vec3 p = ws->wire.points.pData[o->iFirstPoint + k];
        if ( id == 0 || std::fabs( Comp( p, view.axisU ) - Comp( at, view.axisU ) ) > 1e-6 || std::fabs( Comp( p, view.axisV ) - Comp( at, view.axisV ) ) > 1e-6 ) { continue; }
        if ( std::none_of( out.begin(), out.end(), [&]( id_t v ) { return v.value == id; } ) ) { out.push_back( { id } ); }
    }
    return out;
}

bool VertexEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    const auto selectedGeometry = [ws]( const map_wire_object_t &o ) {
        return ( o.kind == map_wire_kind_t::BRUSH || o.kind == map_wire_kind_t::MESH ) && EditorSelection_Contains( &ws->selection, o.id ) != CY_FALSE;
    };
    const auto anyPoint = []( vec3 ) { return true; };
    const auto dragPoint = [&]( vec3 &out ) {
        if ( e.view.bOrtho ) {
            out = SnapUV( ws, OrthoPoint( e, Comp( s.vertexFrom, DepthAxis( e.view.axisU, e.view.axisV ) ) ), e.view.axisU, e.view.axisV, e.modifiers );
            return true;
        }
        // In 3D, as every other drag: across the floor plane through the
        // vertex, straight up or down with Alt held during the drag.
        const bool vertical = ( e.modifiers & Qt::AltModifier ) != 0;
        if ( !FreeGizmoPoint( e, s.vertexFrom, vertical, out ) ) { return false; }
        out = SnapAll( ws, out, e.modifiers );
        if ( vertical ) { out.x = s.vertexFrom.x; out.y = s.vertexFrom.y; } else { out.z = s.vertexFrom.z; }
        return true;
    };
    switch ( e.kind ) {
        case map_tool_event_kind_t::MOVE: {
            if ( s.bVertex && e.bButtonDown ) { vec3 p{}; if ( dragPoint( p ) ) { s.vertexTo = p; } return true; }
            point_hit_t hit{};
            s.bHover = NearestPoint( ws, e, kPickPixels, selectedGeometry, anyPoint, hit ); s.hover = hit.position;
            return true;
        }
        case map_tool_event_kind_t::PRESS: {
            point_hit_t hit{};
            // Elsewhere the click selects, as with the Selection tool.
            if ( !NearestPoint( ws, e, kPickPixels, selectedGeometry, anyPoint, hit ) ) {
                if ( ws->selection.ids.nCount == 0 ) { Hint( ws, QStringLiteral( "Vertex: select brushes or meshes (click one); their corners become handles to drag" ) ); }
                return false;
            }
            const auto *object = MapWireframe_FindObject( ws->wire, hit.object );
            s.bVertex = true; s.vertexObject = hit.object; s.bVertexMesh = object != nullptr && object->kind == map_wire_kind_t::MESH;
            s.vertexFrom = s.vertexTo = hit.position;
            s.vertexIds = s.bVertexMesh && hit.sourceId != 0 ? DraggedVertices( ws, hit.object, hit.sourceId ) : std::vector<id_t>{};
            // A 2D handle stands for every vertex stacked under it: they move
            // together, so walls stay upright (Hammer). A grabbed selection
            // moves as selected.
            s.vertexColumn.clear();
            if ( e.view.bOrtho && s.bVertexMesh && s.vertexIds.size() == 1u ) { s.vertexIds = ColumnVertices( ws, hit.object, hit.position, e.view ); }
            if ( e.view.bOrtho && !s.bVertexMesh ) { s.vertexColumn = ColumnPoints( ws, hit.object, hit.position, e.view ); }
            return true;
        }
        case map_tool_event_kind_t::RELEASE: {
            if ( !s.bVertex ) { return false; }
            s.bVertex = false;
            const vec3 offset = Sub( s.vertexTo, s.vertexFrom );
            if ( Length( offset ) <= 1e-9 || !Writable( ws ) ) { return true; }
            const u64 object = s.vertexObject; const vec3 from = s.vertexFrom, to = s.vertexTo;
            const auto ids = s.vertexIds; const bool mesh = s.bVertexMesh;
            const auto column = s.vertexColumn;
            // Vertex clumping: a corner shared by several selected brushes
            // moves in all of them, so joined brushes stay sealed.
            std::vector<u64> brushes;
            for ( usize i = 0; !mesh && i < ws->selection.ids.nCount; ++i ) {
                if ( geo::GeometryDocument_FindBrush( &ws->pDocument->geometry, { ws->selection.ids.pData[i] } ) != nullptr ) { brushes.push_back( ws->selection.ids.pData[i] ); }
            }
            const bool clump = brushes.size() > 1 && std::find( brushes.begin(), brushes.end(), object ) != brushes.end();
            const bool moved = Apply( ws, mesh ? "Move Vertices" : clump ? "Move Brush Vertices" : "Move Brush Vertex", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
                if ( !Keep( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
                if ( mesh ) { return MapTopologyEdit_MoveVertices( copy, object, { ids.data(), ids.size() }, offset ); }
                if ( clump ) {
                    // Each stacked corner moves in every brush that shares it.
                    map_status_t status = map_status_t::OK;
                    for ( const vec3 corner : column.size() > 1u ? column : std::vector<vec3>{ from } ) {
                        if ( status == map_status_t::OK ) { status = MapTopologyEdit_MoveBrushVertexClump( copy, { brushes.data(), brushes.size() }, corner, Add( corner, offset ), nullptr ); }
                    }
                    return status;
                }
                return column.size() > 1u ? MapTopologyEdit_MoveBrushVertices( copy, object, { column.data(), column.size() }, offset )
                                          : MapTopologyEdit_MoveBrushVertex( copy, object, from, to );
            } );
            if ( !moved ) {
                Hint( ws, mesh ? QStringLiteral( "Vertex: that move would fold or flatten a face away; the vertices stay" )
                               : QStringLiteral( "Vertex: a brush must stay convex; that move would dent it (make it a mesh for free-form shapes)" ) );
            }
            return true;
        }
        case map_tool_event_kind_t::CANCEL: { const bool had = s.bVertex; s.bVertex = false; return had; }
        default: return false;
    }
}

/*
================
Painting
================
*/

QColor Accent() noexcept { return QColor( 255, 196, 48 ); }
QColor Hover() noexcept { return QColor( 96, 220, 120 ); }

void DrawPoint( QPainter &painter, const map_tool_view_t &view, vec3 p, const QColor &color, f64 size = 6.0 )
{
    QPointF screen;
    if ( !Project( view, p, screen ) ) { return; }
    painter.fillRect( QRectF( screen.x() - size * 0.5, screen.y() - size * 0.5, size, size ), color );
}

void DrawLine( QPainter &painter, const map_tool_view_t &view, vec3 a, vec3 b, const QColor &color, Qt::PenStyle style = Qt::SolidLine )
{
    QPointF p, q;
    if ( !Project( view, a, p ) || !Project( view, b, q ) ) { return; }
    painter.setPen( QPen( color, 1.5, style ) );
    painter.drawLine( p, q );
}

void DrawLabel( QPainter &painter, const map_tool_view_t &view, vec3 at, const QString &text, const QColor &color )
{
    QPointF screen;
    if ( !Project( view, at, screen ) ) { return; }
    painter.setPen( color );
    painter.drawText( screen + QPointF( 10, -8 ), text );
}

void DrawChain( QPainter &painter, const map_tool_view_t &view, const state_t &s, bool closed )
{
    // The knife marks a point it would refuse in red before the click.
    const bool blocked = s.bHoverBlocked && ( s.tool == map_tool_t::KNIFE || s.tool == map_tool_t::EDGE_CUT );
    const QColor refused( 235, 70, 60 );
    for ( usize i = 0; i + 1 < s.points.size(); ++i ) { DrawLine( painter, view, s.points[i], s.points[i + 1], Accent() ); }
    if ( !s.points.empty() && s.bHover ) {
        DrawLine( painter, view, s.points.back(), s.hover, blocked ? refused : Accent(), Qt::DashLine );
        if ( closed && s.points.size() >= 2 ) { DrawLine( painter, view, s.hover, s.points.front(), Accent(), Qt::DotLine ); }
    }
    for ( const vec3 &p : s.points ) { DrawPoint( painter, view, p, Accent() ); }
    if ( s.bHover ) { DrawPoint( painter, view, s.hover, blocked ? refused : Hover(), 5.0 ); }
}

// Where Enter will carry an end inside a face on to (ExtendKnifeEnd): dotted,
// so the run-on is seen before the cut is made. The open end follows the
// hovered point, as the next click would.
void KnifeEndsPaint( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view )
{
    if ( s.knife.empty() || s.knifeMesh == 0 ) { return; }
    const std::vector<knife_face_t> faces = KnifeFaces( ws, s.knifeMesh, s.knifeCandidate.get() );
    geo::mesh_edit_knife_point_t boundary{};
    const auto &first = s.knife.front();
    const vec3 next = s.knife.size() > 1 ? s.knife[1].position : s.hover;
    if ( first.kind == geo::mesh_knife_point_kind_t::FACE && ( s.knife.size() > 1 || s.bHover ) &&
         ExtendKnifeEnd( faces, first, Sub( first.position, next ), boundary ) ) {
        DrawLine( painter, view, first.position, boundary.position, Accent(), Qt::DotLine );
    }
    const auto &last = s.knife.back();
    if ( s.knife.size() > 1 && last.kind == geo::mesh_knife_point_kind_t::FACE && !s.bHover &&
         ExtendKnifeEnd( faces, last, Sub( last.position, s.knife[s.knife.size() - 2].position ), boundary ) ) {
        DrawLine( painter, view, last.position, boundary.position, Accent(), Qt::DotLine );
    }
}

// A circle of world radius around p, in the plane facing the view.
void DrawCircle( QPainter &painter, const map_tool_view_t &view, vec3 p, f64 radius, const QColor &color )
{
    vec3 a{}, b{};
    if ( view.bOrtho ) { a = AxisVector( view.axisU ); b = AxisVector( view.axisV ); }
    else {
        const vec3 forward = Unit( Sub( p, view.eye ) );
        a = Unit( Cross( forward, { 0, 0, 1 } ) );
        if ( Length( a ) < 0.5 ) { a = { 1, 0, 0 }; }
        b = Unit( Cross( a, forward ) );
    }
    QPainterPath path;
    for ( int i = 0; i <= 32; ++i ) {
        const f64 angle = i * 2.0 * 3.14159265358979323846 / 32.0;
        QPointF screen;
        if ( !Project( view, Add( p, Add( Mul( a, std::cos( angle ) * radius ), Mul( b, std::sin( angle ) * radius ) ) ), screen ) ) { return; }
        if ( i == 0 ) { path.moveTo( screen ); } else { path.lineTo( screen ); }
    }
    painter.setPen( QPen( color, 1.5 ) );
    painter.setBrush( Qt::NoBrush );
    painter.drawPath( path );
}

// The pivot as a screen-sized crosshair and ring, readable at any zoom; a
// dashed ghost marks the selection centre the transforms use without one.
void DrawPivotMarker( QPainter &painter, const map_tool_view_t &view, vec3 p, bool ghost ) noexcept
{
    QPointF c;
    if ( !Project( view, p, c ) ) { return; }
    const QColor color = ghost ? QColor( 255, 150, 40, 150 ) : QColor( 255, 150, 40 );
    painter.save();
    painter.setRenderHint( QPainter::Antialiasing, true );
    painter.setPen( QPen( QColor( 15, 15, 18, 200 ), 4.0 ) ); // Dark halo over any background.
    painter.setBrush( Qt::NoBrush );
    painter.drawEllipse( c, 8.0, 8.0 );
    painter.setPen( QPen( color, 2.0, ghost ? Qt::DashLine : Qt::SolidLine ) );
    painter.drawEllipse( c, 8.0, 8.0 );
    for ( const QPointF d : { QPointF( 1, 0 ), QPointF( -1, 0 ), QPointF( 0, 1 ), QPointF( 0, -1 ) } ) {
        painter.drawLine( c + d * 4.0, c + d * 14.0 );
    }
    painter.setPen( color );
    painter.drawText( c + QPointF( 14.0, -10.0 ), ghost ? QStringLiteral( "Pivot (selection centre)" ) : QStringLiteral( "Pivot" ) );
    painter.restore();
}

/*
================
Clip on a surface (3D): Hammer 5 and TrenchBroom place clip points on the
faces in the camera view, locking onto vertices; a line drawn across a face
cuts straight through, perpendicular to that face, and a third point (as in
TrenchBroom) tilts the cut through all three
================
*/

bool SurfaceClipApplies( const map_workspace_t *ws ) noexcept
{
    return ws->tool == map_tool_t::CLIP && StringView_Equals( Text( ws, "editor.map.clip_3d_placement", "surface" ), StringView_FromCString( "surface" ) );
}

// A point on the face plane, snapped: the two coordinates across the
// plane's dominant axis land on the grid, the third keeps it on the plane.
vec3 ClipSnap( const map_workspace_t *ws, vec3 p, const math::planed_t &face, Qt::KeyboardModifiers modifiers ) noexcept
{
    u32 axis = 0;
    for ( u32 k = 1; k < 3; ++k ) { if ( std::fabs( Comp( face.normal, k ) ) > std::fabs( Comp( face.normal, axis ) ) ) { axis = k; } }
    vec3 snapped = SnapAll( ws, p, modifiers );
    const f64 rest = face.d + Dot( face.normal, snapped ) - Comp( face.normal, axis ) * Comp( snapped, axis );
    SetComp( snapped, axis, -rest / Comp( face.normal, axis ) );
    return snapped;
}

// Hammer 5 clips between vertices: near a vertex of the visible geometry
// (on screen) the point lands exactly on it, unless a surface hides the
// vertex from the camera. Ctrl/Command places it freely.
constexpr f64 kClipVertexPixels = 10.0;
bool ClipVertex( const map_workspace_t *ws, const map_tool_event_t &e, vec3 &out ) noexcept
{
    if ( SnapBypassed( e.modifiers ) ) { return false; }
    const auto &wire = ws->wire;
    std::vector<std::pair<f64, vec3>> near;
    for ( usize o = 0; o < wire.objects.nCount; ++o ) {
        const auto &object = wire.objects.pData[o];
        if ( object.kind == map_wire_kind_t::ENTITY || !MapWorkspace_IsVisible( ws, object ) ) { continue; }
        for ( u32 k = 0; k < object.nPoints; ++k ) {
            const vec3 p = wire.points.pData[object.iFirstPoint + k];
            QPointF screen;
            if ( !Project( e.view, p, screen ) ) { continue; }
            const f64 distance = std::hypot( screen.x() - e.screen.x(), screen.y() - e.screen.y() );
            if ( distance <= kClipVertexPixels ) { near.push_back( { distance, p } ); }
        }
    }
    std::sort( near.begin(), near.end(), []( const auto &a, const auto &b ) { return a.first < b.first; } );
    for ( const auto &[distance, p] : near ) {
        // Visible: nothing is hit between the eye and the vertex.
        const f64 depth = Length( Sub( p, e.view.eye ) );
        if ( depth < 1e-9 ) { continue; }
        map_tool_event_t towards = e;
        towards.rayOrigin = e.view.eye; towards.rayDirection = Mul( Sub( p, e.view.eye ), 1.0 / depth );
        surface_hit_t hit{};
        if ( SurfaceHit( ws, towards, hit ) && Length( Sub( hit.point, e.view.eye ) ) < depth - std::max( 0.5, depth * 1e-4 ) ) { continue; }
        out = p;
        return true;
    }
    return false;
}

// How a two-point cut stands: perpendicular to a face both points lie on
// (two corners of the top face cut straight down), the one closest to the
// face the first point was put on; else to that face.
vec3 ClipUpright( const map_workspace_t *ws, const state_t &s ) noexcept
{
    const vec3 fallback = s.clipFace.normal;
    const auto &wire = ws->wire;
    vec3 best = fallback;
    f64 bestDot = -1.0;
    for ( usize f = 0; f < wire.faces.nCount; ++f ) {
        const auto &face = wire.faces.pData[f];
        if ( face.nIndices < 3 ) { continue; }
        const auto *object = MapWireframe_FindObject( wire, face.id );
        if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ) { continue; }
        const vec3 on = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex]];
        const f64 scale = std::max( { 1.0, Length( Sub( s.clipA, on ) ), Length( Sub( s.clipB, on ) ) } );
        if ( std::fabs( Dot( face.normal, Sub( s.clipA, on ) ) ) > 1e-6 * scale || std::fabs( Dot( face.normal, Sub( s.clipB, on ) ) ) > 1e-6 * scale ) { continue; }
        const f64 dot = std::fabs( Dot( face.normal, fallback ) );
        if ( dot > bestDot ) { bestDot = dot; best = face.normal; }
    }
    return best;
}

// The surface point under the cursor (any visible surface), or a vertex
// near it. A vertex off every surface (a corner on the silhouette) takes
// a plane facing the camera.
bool ClipSurface( const map_workspace_t *ws, const map_tool_event_t &e, vec3 &point, math::planed_t &face, bool *pVertexOut = nullptr ) noexcept
{
    if ( pVertexOut != nullptr ) { *pVertexOut = false; }
    if ( e.view.bOrtho ) { return false; }
    surface_hit_t hit{};
    const bool surface = SurfaceHit( ws, e, hit ) && Length( hit.normal ) >= 0.5;
    vec3 vertex{};
    const bool snapped = ClipVertex( ws, e, vertex );
    if ( !surface && !snapped ) { return false; }
    const vec3 normal = surface ? Unit( hit.normal ) : Unit( Mul( e.rayDirection, -1.0 ) );
    face = { normal, -Dot( normal, snapped ? vertex : hit.point ) };
    point = snapped ? vertex : ClipSnap( ws, hit.point, face, e.modifiers );
    if ( pVertexOut != nullptr ) { *pVertexOut = snapped; }
    return true;
}

// Where a dragged point goes: a vertex near the cursor, else the point on
// its own face plane.
bool ClipOnFace( const map_workspace_t *ws, const map_tool_event_t &e, const math::planed_t &face, vec3 &point ) noexcept
{
    if ( ClipVertex( ws, e, point ) ) { return true; }
    vec3 on{};
    if ( !RayPlane( e.rayOrigin, e.rayDirection, face, on ) ) { return false; }
    point = ClipSnap( ws, on, face, e.modifiers );
    return true;
}

// A first point put on an edge lies on both of its faces, so the line runs
// on whichever of them the cursor moves onto: a drag from the top's front
// edge across the top cuts straight down, not tilted through the front.
void ClipFollowFace( const map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    vec3 point{};
    math::planed_t under{};
    if ( !ClipSurface( ws, e, point, under ) ) { return; }
    if ( std::fabs( Dot( under.normal, s.clipA ) + under.d ) <= 1e-6 * std::max( 1.0, Length( s.clipA ) ) ) { s.clipFace = under; }
}

// The staged clip is still the one drawn here (not replaced numerically,
// from a 2D pane, or cleared).
bool ClipLineStaged( const map_workspace_t *ws, const state_t &s ) noexcept
{
    const auto &p = ws->editPreview;
    return s.bClipLine && p.bActive && p.bClip && !p.clipGuide.bHas && math::Vec3d_EqualsExact( p.clipPlane.normal, s.clipStaged.normal ) && p.clipPlane.d == s.clipStaged.d;
}

void InvalidateSurfaceClip( map_workspace_t *ws, state_t &s ) noexcept
{
    // Keep the anchors editable, but never let Enter commit an older plane
    // after the current points collapse, become collinear or lose their ray.
    s.clipStaged = { { 0, 0, 0 }, 0 };
    s.bClipLine = true;
    MapWorkspace_SetClipPreview( ws, s.clipStaged );
}

bool StageSurfaceClip( map_workspace_t *ws, state_t &s ) noexcept
{
    const vec3 line = Sub( s.clipB, s.clipA );
    const vec3 upright = Unit( Cross( line, ClipUpright( ws, s ) ) );
    if ( Length( line ) < 1e-6 || Length( upright ) < 0.5 ) { InvalidateSurfaceClip( ws, s ); return false; }
    vec3 normal = upright;
    if ( s.bClipThird ) {
        // Through all three points, keeping the side the two-point cut
        // called front (Keep Front/Back stay the same halves).
        normal = Unit( Cross( line, Sub( s.clipC, s.clipA ) ) );
        if ( Length( normal ) < 0.5 ) { InvalidateSurfaceClip( ws, s ); return false; } // Point 3 on the line.
        if ( Dot( normal, upright ) < 0.0 ) { normal = Mul( normal, -1.0 ); }
    }
    s.clipStaged = { normal, -Dot( normal, s.clipA ) };
    s.bClipLine = true;
    MapWorkspace_SetClipPreview( ws, s.clipStaged );
    return true;
}

// Nothing to cut yet: a click picks what to cut, as the Clip tool needs a
// selection.
bool ClipPick( map_workspace_t *ws, const map_tool_event_t &e ) noexcept
{
    surface_hit_t hit{};
    if ( e.view.bOrtho || !SurfaceHit( ws, e, hit ) ) { return false; }
    MapWorkspace_Select( ws, hit.object, MAP_SELECT_REPLACE );
    if ( MapWorkspace_CanClipSelection( ws ) ) {
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Clip: object selected - now draw the cut across a face, or click from vertex to vertex" ) );
    }
    return true;
}

bool SurfaceClipEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    if ( !MapWorkspace_CanClipSelection( ws ) ) {
        s.bClipDraw = s.bClipPoint = s.bClipPending = s.bClipThird = s.bClipHover = false; s.clipEndpoint = -1;
        return e.kind == map_tool_event_kind_t::PRESS && ClipPick( ws, e );
    }
    switch ( e.kind ) {
        case map_tool_event_kind_t::PRESS: {
            // A staged 2D line's endpoints are the view's to drag.
            const auto &guide = ws->editPreview.clipGuide;
            for ( int i = 0; ws->editPreview.bActive && guide.bHas && i < 2; ++i ) {
                QPointF screen;
                if ( Project( e.view, guide.points[i], screen ) && std::hypot( screen.x() - e.screen.x(), screen.y() - e.screen.y() ) <= 12.0 ) { return false; }
            }
            // One of this line's points: drag it along its face.
            if ( ClipLineStaged( ws, s ) ) {
                for ( int i = 0; i < ( s.bClipThird ? 3 : 2 ); ++i ) {
                    QPointF screen;
                    if ( Project( e.view, i == 0 ? s.clipA : i == 1 ? s.clipB : s.clipC, screen ) &&
                         std::hypot( screen.x() - e.screen.x(), screen.y() - e.screen.y() ) <= 10.0 ) {
                        s.clipEndpoint = i; return true;
                    }
                }
            }
            vec3 point{};
            math::planed_t face{};
            if ( !ClipSurface( ws, e, point, face ) ) { return false; } // Off every surface: the construction plane.
            if ( s.bClipPoint && !s.bClipDraw ) {
                // Second click: the line from the first point.
                vec3 onFirst{};
                ClipFollowFace( ws, s, e );
                s.bClipPoint = false; s.clipB = ClipOnFace( ws, e, s.clipFace, onFirst ) ? onFirst : point;
                if ( !StageSurfaceClip( ws, s ) ) { s.bClipLine = false; }
                return true;
            }
            if ( ClipLineStaged( ws, s ) ) {
                // A click adds point 3 (TrenchBroom); a drag draws a new line.
                s.bClipPending = true; s.clipPress = point; s.clipPressFace = face; s.clipPressScreen = e.screen;
                return true;
            }
            if ( ws->editPreview.bActive && ws->editPreview.bClip ) { MapWorkspace_ClearEditPreview( ws ); }
            s.bClipDraw = true; s.bClipLine = false; s.bClipThird = false; s.clipA = s.clipB = point; s.clipFace = face;
            return true;
        }
        case map_tool_event_kind_t::MOVE: {
            vec3 point{};
            if ( s.clipEndpoint >= 0 && e.bButtonDown ) {
                // Each point stays on the plane of the face it was put on.
                if ( ClipOnFace( ws, e, s.clipEndpoint == 2 ? s.clipFaceC : s.clipFace, point ) ) {
                    ( s.clipEndpoint == 0 ? s.clipA : s.clipEndpoint == 1 ? s.clipB : s.clipC ) = point;
                    ( void )StageSurfaceClip( ws, s );
                } else { InvalidateSurfaceClip( ws, s ); }
                return true;
            }
            if ( s.bClipPending && e.bButtonDown ) {
                if ( std::hypot( e.screen.x() - s.clipPressScreen.x(), e.screen.y() - s.clipPressScreen.y() ) > 3.0 ) {
                    s.bClipPending = false;
                    MapWorkspace_ClearEditPreview( ws );
                    s.bClipDraw = true; s.bClipLine = false; s.bClipThird = false; s.clipA = s.clipB = s.clipPress; s.clipFace = s.clipPressFace;
                    if ( ClipOnFace( ws, e, s.clipFace, point ) ) { s.clipB = point; }
                }
                return true;
            }
            if ( s.bClipDraw && e.bButtonDown ) {
                ClipFollowFace( ws, s, e );
                if ( ClipOnFace( ws, e, s.clipFace, point ) ) { s.clipB = point; }
                return true;
            }
            math::planed_t face{};
            bool vertex = false;
            s.bClipHover = !e.bButtonDown && ClipSurface( ws, e, s.clipHover, face, &vertex );
            s.bClipHoverVertex = s.bClipHover && vertex;
            if ( s.bClipPoint ) { ClipFollowFace( ws, s, e ); }
            if ( s.bClipPoint && ClipOnFace( ws, e, s.clipFace, point ) ) { s.clipHover = point; s.bClipHover = true; }
            return false;
        }
        case map_tool_event_kind_t::RELEASE:
            if ( s.clipEndpoint >= 0 ) { s.clipEndpoint = -1; return true; }
            if ( s.bClipPending ) {
                s.bClipPending = false;
                if ( !s.bClipThird ) {
                    s.clipC = s.clipPress; s.clipFaceC = s.clipPressFace; s.bClipThird = true;
                    if ( !StageSurfaceClip( ws, s ) ) {
                        s.bClipThird = false; ( void )StageSurfaceClip( ws, s );
                        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Clip: point 3 lies on the line through points 1 and 2" ) );
                    }
                } else {
                    // Three points already: this click starts a new line.
                    MapWorkspace_ClearEditPreview( ws );
                    s.bClipThird = false; s.bClipLine = false;
                    s.clipA = s.clipB = s.clipPress; s.clipFace = s.clipPressFace; s.bClipPoint = true;
                }
                return true;
            }
            if ( !s.bClipDraw ) { return false; }
            s.bClipDraw = false;
            // A click places the first point; the next click finishes the line.
            if ( Length( Sub( s.clipB, s.clipA ) ) < 1e-6 ) { s.bClipPoint = true; return true; }
            if ( !StageSurfaceClip( ws, s ) ) { s.bClipLine = false; }
            return true;
        case map_tool_event_kind_t::CANCEL:
            if ( s.bClipDraw || s.bClipPoint || s.bClipPending || s.clipEndpoint >= 0 ) {
                s.bClipDraw = s.bClipPoint = s.bClipPending = false; s.clipEndpoint = -1; return true;
            }
            return false; // A staged plane: the view's Escape clears it.
        case map_tool_event_kind_t::BACKSPACE:
            if ( s.bClipPoint ) { s.bClipPoint = false; return true; }
            if ( s.bClipThird && ClipLineStaged( ws, s ) ) { s.bClipThird = false; ( void )StageSurfaceClip( ws, s ); return true; }
            return false;
        default: return false;
    }
}

void SurfaceClipPaint( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    const bool line = s.bClipDraw || ClipLineStaged( ws, s );
    const bool third = line && !s.bClipDraw && s.bClipThird;
    if ( line ) {
        DrawLine( painter, view, s.clipA, s.clipB, Accent() );
        if ( third ) {
            DrawLine( painter, view, s.clipB, s.clipC, Accent() ); DrawLine( painter, view, s.clipC, s.clipA, Accent() );
        } else {
            const vec3 d = Sub( s.clipB, s.clipA );
            DrawLine( painter, view, Sub( s.clipA, Mul( d, 2.0 ) ), Add( s.clipB, Mul( d, 2.0 ) ), Accent(), Qt::DashLine );
        }
    }
    if ( s.bClipPoint && s.bClipHover ) { DrawLine( painter, view, s.clipA, s.clipHover, Accent(), Qt::DashLine ); }
    const auto marker = [&]( vec3 p, const QString &label ) {
        QPointF c;
        if ( !Project( view, p, c ) ) { return; }
        painter.setPen( QPen( QColor( 12, 12, 16 ), 1.5 ) ); painter.setBrush( Accent() );
        painter.drawEllipse( c, 5.0, 5.0 ); painter.setBrush( Qt::NoBrush );
        painter.setPen( Accent() ); painter.drawText( c + QPointF( 8, -8 ), label );
    };
    if ( line || s.bClipPoint ) { marker( s.clipA, QStringLiteral( "1" ) ); }
    if ( line ) { marker( s.clipB, QStringLiteral( "2" ) ); }
    if ( third ) { marker( s.clipC, QStringLiteral( "3" ) ); }
    if ( s.bClipHover && !s.bClipDraw ) {
        QPointF c;
        if ( Project( view, s.clipHover, c ) ) {
            painter.setPen( QPen( Hover(), 1.5 ) );
            if ( s.bClipHoverVertex ) { painter.setBrush( Qt::NoBrush ); painter.drawEllipse( c, 7.0, 7.0 ); } // Locked onto a vertex.
            painter.drawLine( c + QPointF( -7, 0 ), c + QPointF( 7, 0 ) ); painter.drawLine( c + QPointF( 0, -7 ), c + QPointF( 0, 7 ) );
        }
    }
}

/*
================
Displacement (Hammer 5's Paint Displacement): sculpt a private presentation
with a brush. Conversion, face slicing and vertex positions publish together
as one undo step; discarding the preview never needs a live-wire rebuild.
================
*/

// Selection and scope are sampled once on press. Locks are admission rules as
// well as capture guards; a nearby locked mesh must never receive a dab.
bool SculptTarget( const map_workspace_t *ws, const map_wire_object_t &object ) noexcept
{
    if ( object.kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( ws, object ) || MapLayers_IsObjectLocked( ws, object.id ) ) { return false; }
    return StringView_Equals( Text( ws, "editor.map.displace_target", "selected" ), StringView_FromCString( "everything" ) ) ||
           ws->selection.ids.nCount == 0 || EditorSelection_Contains( &ws->selection, object.id );
}

void ResetSculpt( state_t &s ) noexcept
{
    s.sculptWork.reset(); s.bSculpt = s.bSculptHover = false;
    s.sculptLast = s.sculptHover = s.sculptNormal = {}; s.sculptDabs = 0;
}

bool SculptContextMatches( const map_workspace_t *ws, const sculpt_stroke_t &stroke ) noexcept
{
    if ( !Writable( ws ) || ws->tool != map_tool_t::DISPLACE || ws->pDocument != stroke.source ||
         ws->pDocument->geometry.revision != stroke.geometryRevision || ws->selection.revision != stroke.selectionRevision ||
         ws->pDocument->nextId != stroke.nextId || ws->pDocument->geometry.sourceIds.allocator.next.value != stroke.sourceNextId ||
         ( stroke.convert && EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) == CY_FALSE ) ) { return false; }
    for ( const u64 id : stroke.targets ) {
        const auto *object = MapWireframe_FindObject( ws->wire, id );
        const auto kind = stroke.convert && id == stroke.preparedObject ? map_wire_kind_t::BRUSH : map_wire_kind_t::MESH;
        if ( object == nullptr || object->kind != kind || !MapWorkspace_IsVisible( ws, *object ) || MapLayers_IsObjectLocked( ws, id ) ) { return false; }
    }
    return true;
}

map_status_t PrepareSculptDocument( map_document_t *copy, const sculpt_stroke_t &stroke ) noexcept
{
    if ( stroke.convert ) {
        vector_t<u64> roots{};
        if ( !Vector_Init( &roots, copy->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
        const auto status = MapMeshEdit_ConvertBrushes( copy, { &stroke.preparedObject, 1u }, &roots );
        Vector_Shutdown( &roots );
        if ( status != map_status_t::OK ) { return status; }
    }
    // Slice the existing face, retaining its root, owner, layer, adjacent-edge
    // connectivity and corner/retained attributes. Do not extract a new grid.
    return stroke.cells > 1u ? MapMeshEdit_QuadSliceFace( copy, stroke.preparedObject, stroke.preparedFace, stroke.cells, stroke.cells ) : map_status_t::OK;
}

map_status_t PrepareSculpt( const map_workspace_t *ws, const map_tool_event_t &e, const surface_hit_t &hit, sculpt_stroke_t &stroke )
{
    stroke.source = ws->pDocument; stroke.view = e.view.pView;
    stroke.geometryRevision = ws->pDocument->geometry.revision; stroke.selectionRevision = ws->selection.revision;
    stroke.nextId = ws->pDocument->nextId; stroke.sourceNextId = ws->pDocument->geometry.sourceIds.allocator.next.value;
    stroke.preparedObject = hit.object; stroke.preparedFace = hit.kind == map_wire_kind_t::BRUSH ? hit.sideId : hit.faceId;
    stroke.convert = hit.kind == map_wire_kind_t::BRUSH;
    stroke.radius = Real( ws, "editor.map.displace_radius", 64.0 );
    const f64 strength = Real( ws, "editor.map.displace_strength", 50.0 ), smooth = Real( ws, "editor.map.displace_smooth", 50.0 );
    if ( !std::isfinite( stroke.radius ) || stroke.radius <= 0.0 || !std::isfinite( strength ) || !std::isfinite( smooth ) ) { return map_status_t::INVALID_ARGUMENT; }
    stroke.radius = std::max( 1.0, stroke.radius );
    stroke.step = std::clamp( strength, 1.0, 100.0 ) / 100.0 * stroke.radius * 0.2;
    stroke.smooth = std::clamp( smooth, 0.0, 100.0 ) / 100.0;
    const auto mode = Text( ws, "editor.map.displace_mode", "push_pull" ), normal = Text( ws, "editor.map.displace_normal", "average" );
    const auto is = []( string_view_t value, const char *name ) { return StringView_Equals( value, StringView_FromCString( name ) ); };
    if ( is( mode, "smooth" ) ) { stroke.mode = sculpt_stroke_t::mode_t::SMOOTH; }
    else if ( is( mode, "flatten" ) ) { stroke.mode = sculpt_stroke_t::mode_t::FLATTEN; }
    else if ( is( mode, "inflate" ) ) { stroke.mode = sculpt_stroke_t::mode_t::INFLATE; }
    else if ( is( mode, "pinch" ) ) { stroke.mode = sculpt_stroke_t::mode_t::PINCH; }
    else if ( is( mode, "noise" ) ) { stroke.mode = sculpt_stroke_t::mode_t::NOISE; }
    if ( is( normal, "view" ) ) { stroke.normal = sculpt_stroke_t::normal_t::VIEW; }
    else if ( is( normal, "z" ) ) { stroke.normal = sculpt_stroke_t::normal_t::Z; }
    else if ( !is( normal, "average" ) ) { stroke.normal = sculpt_stroke_t::normal_t::FACE; }
    stroke.falloff = MapFalloff_Make( Text( ws, "editor.map.displace_falloff", "smooth" ), Text( ws, "editor.map.displace_curve", MAP_FALLOFF_CURVE_DEFAULT ) );
    for ( usize i = 0u; i < ws->wire.objects.nCount; ++i ) {
        const auto &object = ws->wire.objects.pData[i];
        if ( SculptTarget( ws, object ) || ( stroke.convert && object.id == hit.object ) ) { stroke.targets.push_back( object.id ); }
    }
    if ( EditorSettings_Bool( &ws->pGui->settings, "editor.map.displace_auto_tessellate", CY_TRUE ) != CY_FALSE ) {
        for ( usize i = 0u; i < ws->wire.faces.nCount; ++i ) {
            const auto &face = ws->wire.faces.pData[i];
            if ( face.id != hit.object || ( stroke.convert ? face.sideId : face.faceId ) != stroke.preparedFace || stroke.preparedFace == 0u ) { continue; }
            f64 longest = 0.0;
            for ( u32 k = 0u; k < face.nIndices; ++k ) {
                longest = std::max( longest, Length( Sub( ws->wire.points.pData[ws->wire.faceIndices.pData[face.iFirstIndex + k]],
                    ws->wire.points.pData[ws->wire.faceIndices.pData[face.iFirstIndex + ( k + 1u ) % face.nIndices]] ) ) );
            }
            if ( face.nIndices == 4u && longest > stroke.radius * 1.01 ) {
                const f64 cells = longest / std::max( 1.0, stroke.radius * 0.25 );
                const auto power = static_cast<u32>( std::clamp( std::ceil( std::log2( std::max( 2.0, cells ) ) ), 1.0, 4.0 ) );
                stroke.cells = 1u << power;
            }
            break;
        }
    }
    auto status = MapEdit_Clone( ws->pDocument, &stroke.document );
    if ( status == map_status_t::OK ) { status = PrepareSculptDocument( stroke.document, stroke ); }
    if ( status == map_status_t::OK && !MapWireframe_Init( &stroke.wire, ws->pGui->pAllocator ) ) { status = map_status_t::OUT_OF_MEMORY; }
    if ( status == map_status_t::OK ) { status = MapWireframe_Build( &stroke.wire, *stroke.document ); }
    return status;
}

usize SculptMesh( sculpt_stroke_t &stroke, const map_wire_object_t &object )
{
    for ( usize i = 0u; i < stroke.meshes.size(); ++i ) { if ( stroke.meshes[i].id == object.id ) { return i; } }
    const auto &wire = stroke.wire;
    sculpt_mesh_t mesh{}; mesh.id = object.id;
    mesh.original.assign( wire.points.pData + object.iFirstPoint, wire.points.pData + object.iFirstPoint + object.nPoints );
    mesh.neighbours.resize( object.nPoints );
    for ( u32 l = 0u; l < object.nLines; ++l ) {
        const auto &line = wire.lines.pData[object.iFirstLine + l];
        if ( line.iA < object.iFirstPoint || line.iB < object.iFirstPoint ) { continue; }
        const u32 a = line.iA - object.iFirstPoint, b = line.iB - object.iFirstPoint;
        if ( a < object.nPoints && b < object.nPoints ) { mesh.neighbours[a].push_back( b ); mesh.neighbours[b].push_back( a ); }
    }
    for ( usize f = 0u; f < wire.faces.nCount; ++f ) { if ( wire.faces.pData[f].id == object.id ) { mesh.faces.push_back( static_cast<u32>( f ) ); } }
    stroke.meshes.push_back( std::move( mesh ) );
    return stroke.meshes.size() - 1u;
}

vec3 FaceNormalOf( const map_wireframe_t &wire, const map_wire_face_t &face ) noexcept
{
    vec3 n{};
    for ( u32 k = 0u; k < face.nIndices; ++k ) {
        const vec3 a = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]];
        const vec3 b = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + ( k + 1u ) % face.nIndices]];
        n = Add( n, Cross( a, b ) );
    }
    return Unit( n );
}

void SculptDab( sculpt_stroke_t &stroke, const map_tool_event_t &e, vec3 p, vec3 hitNormal )
{
    auto &wire = stroke.wire;
    const f64 sign = SnapBypassed( e.modifiers ) ? -1.0 : 1.0;
    ++stroke.dabs;
    // Indices survive subsequent mesh snapshot growth. Stored pointers did not:
    // a dab spanning two meshes could reallocate and dereference the first one.
    std::vector<std::pair<usize, u32>> reached;
    for ( usize o = 0u; o < wire.objects.nCount; ++o ) {
        const auto &object = wire.objects.pData[o];
        if ( object.kind != map_wire_kind_t::MESH || !object.bounds.bHas ||
             std::find( stroke.targets.begin(), stroke.targets.end(), object.id ) == stroke.targets.end() ) { continue; }
        const auto &box = object.bounds.box;
        const vec3 nearest{ std::clamp( p.x, box.minimum.x, box.maximum.x ), std::clamp( p.y, box.minimum.y, box.maximum.y ), std::clamp( p.z, box.minimum.z, box.maximum.z ) };
        if ( Length( Sub( nearest, p ) ) <= stroke.radius ) { reached.emplace_back( SculptMesh( stroke, object ), static_cast<u32>( o ) ); }
    }
    if ( reached.empty() ) { return; }
    vec3 normal = hitNormal;
    if ( stroke.normal == sculpt_stroke_t::normal_t::VIEW ) { normal = Mul( e.rayDirection, -1.0 ); }
    else if ( stroke.normal == sculpt_stroke_t::normal_t::Z ) { normal = { 0, 0, 1 }; }
    else if ( stroke.normal == sculpt_stroke_t::normal_t::AVERAGE ) {
        vec3 sum{};
        for ( const auto &[m, o] : reached ) {
            for ( u32 f : stroke.meshes[m].faces ) {
                const auto &face = wire.faces.pData[f];
                for ( u32 k = 0u; k < face.nIndices; ++k ) {
                    if ( Length( Sub( wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]], p ) ) < stroke.radius ) { sum = Add( sum, face.normal ); break; }
                }
            }
        }
        if ( Length( sum ) > 1e-9 ) { normal = Unit( sum ); }
    }
    normal = Unit( normal );
    for ( const auto &[m, o] : reached ) {
        const auto &mesh = stroke.meshes[m]; auto &object = wire.objects.pData[o];
        vec3 *points = wire.points.pData + object.iFirstPoint;
        const std::vector<vec3> before( points, points + object.nPoints );
        std::vector<vec3> vertexNormals( object.nPoints );
        for ( u32 f : mesh.faces ) {
            const auto &face = wire.faces.pData[f];
            for ( u32 k = 0u; k < face.nIndices; ++k ) {
                const u32 i = wire.faceIndices.pData[face.iFirstIndex + k] - object.iFirstPoint;
                if ( i < object.nPoints ) { vertexNormals[i] = Add( vertexNormals[i], face.normal ); }
            }
        }
        for ( u32 i = 0u; i < object.nPoints; ++i ) {
            const u64 sourceId = wire.pointSourceIds.pData[object.iFirstPoint + i];
            if ( sourceId == 0u ) { continue; } // Generated modifier points follow the authored cage on release.
            const f64 distance = Length( Sub( before[i], p ) );
            if ( distance >= stroke.radius ) { continue; }
            const f64 w = MapFalloff_At( stroke.falloff, distance / stroke.radius );
            vec3 &point = points[i];
            switch ( stroke.mode ) {
                case sculpt_stroke_t::mode_t::SMOOTH: {
                    if ( mesh.neighbours[i].empty() ) { break; }
                    vec3 average{};
                    for ( u32 n : mesh.neighbours[i] ) { average = Add( average, before[n] ); }
                    average = Mul( average, 1.0 / static_cast<f64>( mesh.neighbours[i].size() ) );
                    point = Add( before[i], Mul( Sub( average, before[i] ), w * stroke.smooth ) ); break;
                }
                case sculpt_stroke_t::mode_t::FLATTEN:
                    point = Sub( before[i], Mul( normal, Dot( Sub( before[i], p ), normal ) * w * 0.5 ) ); break;
                case sculpt_stroke_t::mode_t::INFLATE:
                    point = Add( before[i], Mul( Unit( vertexNormals[i] ), stroke.step * w * sign ) ); break;
                case sculpt_stroke_t::mode_t::PINCH: {
                    vec3 toward = Sub( p, before[i] ); toward = Sub( toward, Mul( normal, Dot( toward, normal ) ) );
                    point = Add( before[i], Mul( toward, 0.15 * w * sign ) ); break;
                }
                case sculpt_stroke_t::mode_t::NOISE: {
                    u64 h = ( sourceId + 0x9E3779B97F4A7C15ull * stroke.dabs ) * 0xBF58476D1CE4E5B9ull; h ^= h >> 31;
                    const f64 noise = static_cast<f64>( h % 2001u ) / 1000.0 - 1.0;
                    point = Add( before[i], Mul( normal, stroke.step * w * noise ) ); break;
                }
                default: point = Add( before[i], Mul( normal, stroke.step * w * sign ) ); break;
            }
        }
        for ( u32 f : mesh.faces ) { wire.faces.pData[f].normal = FaceNormalOf( wire, wire.faces.pData[f] ); }
        object.bounds = {};
        for ( u32 i = 0u; i < object.nPoints; ++i ) { MapBounds_AddPoint( object.bounds, points[i] ); }
    }
    wire.bounds = {};
    for ( usize i = 0u; i < wire.objects.nCount; ++i ) { MapBounds_AddBounds( wire.bounds, wire.objects.pData[i].bounds ); }
}

// Compare preparation geometry only. Materials, UVs, paint and retained data
// belong to the fresh release copy, and must not be copied back from press.
map_status_t CheckSculptGeometry( const geo::mesh_source_t *before, const geo::mesh_source_t *fresh ) noexcept
{
    if ( before == nullptr || fresh == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    struct description_t { geo::mesh_source_description_t value{}; ~description_t() { geo::MeshSourceDescription_Shutdown( &value ); } } snapshot;
    auto status = geo::MeshSourceDescription_Init( &snapshot.value, before->mesh.pAllocator, before->sourceId );
    if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSource_TryDescribe( before, &snapshot.value ); }
    if ( status != geo::geometry_status_t::OK ) { return status == geo::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED; }
    return CheckPreparedMeshGeometry( snapshot.value, fresh );
}

bool DisplaceEventImpl( map_workspace_t *ws, state_t &s, const map_tool_event_t &e )
{
    if ( s.sculptWork && !SculptContextMatches( ws, *s.sculptWork ) ) {
        ResetSculpt( s ); MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
        if ( e.kind != map_tool_event_kind_t::PRESS ) { return true; }
    }
    if ( s.sculptWork && e.kind != map_tool_event_kind_t::CANCEL && e.view.pView != s.sculptWork->view ) { return true; }
    if ( e.kind == map_tool_event_kind_t::PRESS ) { ResetSculpt( s ); }
    surface_hit_t hit{};
    const bool surface = SurfaceHit( ws, e, hit, false, s.sculptWork ? &s.sculptWork->wire : nullptr );
    const auto eligible = [&]() {
        if ( !surface || MapLayers_IsObjectLocked( ws, hit.object ) ) { return false; }
        if ( s.sculptWork ) { return std::find( s.sculptWork->targets.begin(), s.sculptWork->targets.end(), hit.object ) != s.sculptWork->targets.end(); }
        return ( hit.kind == map_wire_kind_t::MESH || hit.kind == map_wire_kind_t::BRUSH ) &&
            ( ws->selection.ids.nCount == 0u || EditorSelection_Contains( &ws->selection, hit.object ) ||
              StringView_Equals( Text( ws, "editor.map.displace_target", "selected" ), StringView_FromCString( "everything" ) ) );
    };
    switch ( e.kind ) {
        case map_tool_event_kind_t::PRESS: {
            ResetSculpt( s );
            if ( !surface || !eligible() ) { Hint( ws, QStringLiteral( "Displace: paint on an unlocked object within the tool's selection scope" ) ); return true; }
            if ( !Writable( ws ) ) { return true; }
            if ( hit.kind == map_wire_kind_t::BRUSH && EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) == CY_FALSE ) {
                Hint( ws, QStringLiteral( "Displace: enable brush conversion in the component settings to sculpt this brush" ) ); return true;
            }
            auto stroke = std::make_shared<sculpt_stroke_t>();
            const auto status = PrepareSculpt( ws, e, hit, *stroke );
            if ( status != map_status_t::OK ) {
                MapNotice_Post( ws, map_notice_t::ERROR, status == map_status_t::OUT_OF_MEMORY ?
                    QStringLiteral( "Displace: not enough memory to prepare the stroke; the map has not changed" ) :
                    QStringLiteral( "Displace: the surface could not be prepared (%1); the map has not changed" ).arg( QString::fromLatin1( MapDocument_StatusName( status ) ) ) ); return true;
            }
            if ( !SculptContextMatches( ws, *stroke ) ) { return true; }
            SculptDab( *stroke, e, hit.point, hit.normal );
            s.sculptWork = std::move( stroke ); s.bSculpt = s.bSculptHover = true;
            s.sculptLast = s.sculptHover = hit.point; s.sculptNormal = hit.normal;
            MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); return true;
        }
        case map_tool_event_kind_t::MOVE: {
            s.bSculptHover = eligible(); s.sculptHover = hit.point; s.sculptNormal = hit.normal;
            if ( s.sculptWork && !e.bButtonDown ) { ResetSculpt( s ); MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); return true; }
            if ( s.sculptWork && s.bSculptHover ) {
                const f64 travel = Length( Sub( hit.point, s.sculptLast ) ), spacing = s.sculptWork->radius * 0.2;
                if ( std::isfinite( travel ) && travel >= spacing ) {
                    const int dabs = static_cast<int>( std::min( 64.0, std::floor( travel / spacing ) ) );
                    for ( int i = 1; i <= dabs; ++i ) {
                        SculptDab( *s.sculptWork, e, Add( s.sculptLast, Mul( Sub( hit.point, s.sculptLast ), static_cast<f64>( i ) / dabs ) ), hit.normal );
                    }
                    s.sculptLast = hit.point; MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
                }
            }
            return true;
        }
        case map_tool_event_kind_t::RELEASE: {
            if ( !s.sculptWork ) { return true; }
            const auto stroke = s.sculptWork;
            struct moved_t { u64 mesh; std::vector<id_t> ids; std::vector<vec3> positions; };
            std::vector<moved_t> moved;
            for ( const auto &mesh : stroke->meshes ) {
                const auto *object = MapWireframe_FindObject( stroke->wire, mesh.id );
                if ( object == nullptr ) { continue; }
                moved_t entry{ mesh.id, {}, {} };
                for ( u32 i = 0u; i < object->nPoints && i < mesh.original.size(); ++i ) {
                    const vec3 now = stroke->wire.points.pData[object->iFirstPoint + i];
                    const u64 id = stroke->wire.pointSourceIds.pData[object->iFirstPoint + i];
                    if ( id == 0u || Length( Sub( now, mesh.original[i] ) ) <= 1e-9 ) { continue; }
                    entry.ids.push_back( { id } ); entry.positions.push_back( now );
                }
                if ( !entry.ids.empty() ) { moved.push_back( std::move( entry ) ); }
            }
            ResetSculpt( s ); // Release the global capture before synchronous edit notifications.
            if ( moved.empty() ) { MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); return true; }
            const bool done = ApplyMetadata( ws, "Displace", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
                *changed = false;
                if ( !SculptContextMatches( ws, *stroke ) ) { return map_status_t::INVALID_ARGUMENT; }
                if ( !Keep( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
                auto status = PrepareSculptDocument( copy, *stroke );
                if ( status != map_status_t::OK ) { return status; }
                const u64 revision = copy->geometry.revision;
                for ( const auto &entry : moved ) {
                    status = CheckSculptGeometry( geo::GeometryDocument_FindMesh( &stroke->document->geometry, { entry.mesh } ),
                        geo::GeometryDocument_FindMesh( &copy->geometry, { entry.mesh } ) );
                    if ( status == map_status_t::OK ) { status = MapTopologyEdit_SetVertexPositions( copy, entry.mesh, { entry.ids.data(), entry.ids.size() }, { entry.positions.data(), entry.positions.size() } ); }
                    if ( status != map_status_t::OK ) { return status; }
                }
                *changed = copy->geometry.revision != revision;
                return map_status_t::OK;
            } );
            if ( !done && !( ws->lastEdit.status == map_status_t::OK && ws->lastEdit.outcome == map_edit_outcome_t::UNCHANGED ) ) {
                MapNotice_Post( ws, map_notice_t::ERROR, ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ?
                    QStringLiteral( "Displace: not enough memory to apply the stroke; the map has not changed" ) :
                    QStringLiteral( "Displace: the stroke could not be applied; the map has not changed" ) );
            }
            MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); return true;
        }
        case map_tool_event_kind_t::CANCEL: {
            const bool had = bool( s.sculptWork ); ResetSculpt( s );
            if ( had ) { MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); } return had;
        }
        default: return false;
    }
}

bool DisplaceEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    try { return DisplaceEventImpl( ws, s, e ); }
    catch ( ... ) {
        ResetSculpt( s );
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Displace: not enough memory to record the stroke; the map has not changed" ) );
        MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); return true;
    }
}

void DisplacePaint( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    if ( !s.bSculptHover ) { return; }
    const f64 radius = s.sculptWork ? s.sculptWork->radius : std::max( 1.0, Real( ws, "editor.map.displace_radius", 64.0 ) );
    const QColor ring( 255, 214, 64 );
    // The brush lies on the surface: a ring in the plane of the face under it.
    const vec3 n = Unit( s.sculptNormal );
    const vec3 a = Unit( Cross( n, std::fabs( n.z ) < 0.9 ? vec3{ 0, 0, 1 } : vec3{ 1, 0, 0 } ) ), b = Cross( n, a );
    constexpr int kSegments = 48;
    for ( int i = 0; i < kSegments; ++i ) {
        const f64 t0 = 2.0 * M_PI * i / kSegments, t1 = 2.0 * M_PI * ( i + 1 ) / kSegments;
        const vec3 p0 = Add( s.sculptHover, Add( Mul( a, radius * std::cos( t0 ) ), Mul( b, radius * std::sin( t0 ) ) ) );
        const vec3 p1 = Add( s.sculptHover, Add( Mul( a, radius * std::cos( t1 ) ), Mul( b, radius * std::sin( t1 ) ) ) );
        DrawLine( painter, view, p0, p1, ring, ( i % 2 ) == 0 ? Qt::SolidLine : Qt::DashLine );
    }
    DrawLine( painter, view, s.sculptHover, Add( s.sculptHover, Mul( n, radius * 0.35 ) ), ring );
    DrawPoint( painter, view, s.sculptHover, ring, 5.0 );
}

/*
================
Component drag: grab vertices and edges and pull them (TrenchBroom's vertex
and edge tools, NetRadiant's V and E modes), and box-select them
================
*/

bool VertexAt( const geo::mesh_source_t *mesh, id_t id, vec3 &out ) noexcept; // Below, with the gizmo.

bool EdgeDragExtends( const map_workspace_t *ws ) noexcept
{
    return ws->elementMode == map_element_mode_t::EDGES &&
           StringView_Equals( Text( ws, "editor.map.edge_drag_action", "move" ), StringView_FromCString( "extend" ) );
}

bool ComponentDragApplies( const map_workspace_t *ws ) noexcept
{
    return Writable( ws ) && ( ws->tool == map_tool_t::SELECT ||
           ( ws->tool == map_tool_t::TRANSLATE && ws->elementMode == map_element_mode_t::EDGES ) ) &&
           ( ws->elementMode == map_element_mode_t::VERTICES || ws->elementMode == map_element_mode_t::EDGES ) &&
           !ws->editPreview.bActive && !EditorHistory_IsTransactionOpen( &ws->history );
}

bool SelectedForHandles( const map_workspace_t *ws, u64 id ) noexcept
{
    return id != 0 && ( id == ws->meshSelection.meshId.value || EditorSelection_Contains( &ws->selection, id ) );
}

// Hidden behind a face of the scene, seen from the 3D view's eye.
bool Occluded( const map_workspace_t *ws, const map_tool_event_t &e, vec3 p ) noexcept
{
    if ( e.view.bOrtho ) { return false; }
    const vec3 toPoint = Sub( p, e.view.eye );
    const f64 distance = Length( toPoint );
    if ( distance <= 1e-6 ) { return false; }
    map_tool_event_t probe = e;
    probe.rayOrigin = e.view.eye; probe.rayDirection = Mul( toPoint, 1.0 / distance );
    surface_hit_t hit{};
    return SurfaceHit( ws, probe, hit ) && Length( Sub( hit.point, e.view.eye ) ) < distance - std::max( 0.5, distance * 1e-4 );
}

struct handle_hit_t {
    u64 object{ 0 };
    id_t vertex{};
    vec3 position{};
    bool brush{ false };
};

// The vertex handle under the cursor: handles of the selected meshes and
// brushes first (occluded ones too, as they are drawn), then visible
// vertices of other meshes.
bool VertexHandleAt( const map_workspace_t *ws, const map_tool_event_t &e, handle_hit_t &out ) noexcept
{
    constexpr f64 kRadius = 9.0;
    f64 best = kRadius * kRadius;
    bool found = false;
    for ( usize o = 0; o < ws->wire.objects.nCount; ++o ) {
        const auto &object = ws->wire.objects.pData[o];
        const bool handles = SelectedForHandles( ws, object.id );
        if ( !MapWorkspace_IsVisible( ws, object ) || ( object.kind != map_wire_kind_t::MESH && !( handles && object.kind == map_wire_kind_t::BRUSH ) ) ) { continue; }
        for ( u32 k = 0; k < object.nPoints; ++k ) {
            // Points without identity draw a modifier's result, not the cage.
            if ( object.kind == map_wire_kind_t::MESH && ws->wire.pointSourceIds.pData[object.iFirstPoint + k] == 0 ) { continue; }
            const vec3 p = ws->wire.points.pData[object.iFirstPoint + k];
            QPointF screen;
            if ( !Project( e.view, p, screen ) ) { continue; }
            // Selected handles win ties: they are the large ones.
            const f64 d = std::pow( screen.x() - e.screen.x(), 2 ) + std::pow( screen.y() - e.screen.y(), 2 ) - ( handles ? 4.0 : 0.0 );
            if ( d >= best || ( !handles && Occluded( ws, e, p ) ) ) { continue; }
            best = d; found = true;
            out.object = object.id; out.position = p; out.brush = object.kind == map_wire_kind_t::BRUSH;
            out.vertex = { object.kind == map_wire_kind_t::MESH ? ws->wire.pointSourceIds.pData[object.iFirstPoint + k] : 0u };
        }
    }
    return found;
}

bool MeshVertexAt( const map_workspace_t *ws, u64 mesh, vec3 at, id_t &out ) noexcept
{
    const auto *object = MapWireframe_FindObject( ws->wire, mesh );
    for ( u32 k = 0; object != nullptr && k < object->nPoints; ++k ) {
        if ( ws->wire.pointSourceIds.pData[object->iFirstPoint + k] == 0 ) { continue; }
        if ( Length( Sub( ws->wire.points.pData[object->iFirstPoint + k], at ) ) <= 1e-6 ) { out = { ws->wire.pointSourceIds.pData[object->iFirstPoint + k] }; return true; }
    }
    return false;
}

bool VertexSelected( const map_workspace_t *ws, u64 mesh, id_t vertex ) noexcept
{
    return ws->meshSelection.meshId.value == mesh && geo::MeshSelection_HasVertex( &ws->meshSelection, vertex );
}

// Starts a drag of what is selected on `mesh`, grabbed at `grab`.
void BeginComponentDrag( const map_workspace_t *ws, state_t &s, const map_tool_event_t &e, u64 mesh, vec3 grab, id_t grabbed )
{
    s.bComponentDrag = true; s.bComponentMoved = false;
    s.dragDocument = ws->pDocument; s.dragSelectionRevision = ws->selection.revision;
    s.dragGeometryRevision = ws->pDocument->geometry.revision;
    s.dragMesh = mesh; s.dragGrab = grab; s.dragGrabbed = grabbed; s.dragSnapVertex = {}; s.dragDelta = {}; s.dragPress = e.screen;
    s.dragVertices.clear(); s.dragEdges.clear(); s.dragOrigins.clear();
    const auto *source = geo::GeometryDocument_FindMesh( &ws->pDocument->geometry, { mesh } );
    if ( ws->elementMode == map_element_mode_t::EDGES ) {
        s.dragEdges.assign( ws->meshSelection.edges.pData, ws->meshSelection.edges.pData + ws->meshSelection.edges.nCount );
        std::vector<u64> ends;
        for ( const auto &edge : s.dragEdges ) { ends.push_back( edge.a.value ); ends.push_back( edge.b.value ); }
        std::sort( ends.begin(), ends.end() ); ends.erase( std::unique( ends.begin(), ends.end() ), ends.end() );
        for ( u64 id : ends ) { s.dragVertices.push_back( { id } ); }
    } else {
        s.dragVertices.assign( ws->meshSelection.vertices.pData, ws->meshSelection.vertices.pData + ws->meshSelection.vertices.nCount );
    }
    for ( const id_t &id : s.dragVertices ) { vec3 p{}; ( void )VertexAt( source, id, p ); s.dragOrigins.push_back( p ); }
}

// Use the same nine-pixel authored handles as pickup, restricted to stationary
// vertices of this mesh and the active movement constraint. Selected cage
// handles are drawn through faces; generated modifier points cannot be targets.
bool ComponentVertexTarget( const map_workspace_t *ws, const state_t &s, const map_tool_event_t &e,
    vec3 planeNormal, bool vertical, vec3 &target, id_t &vertex ) noexcept
{
    if ( ws->elementMode != map_element_mode_t::VERTICES || !s.dragEdges.empty() ||
         SnapBypassed( e.modifiers ) || ( e.modifiers & Qt::ShiftModifier ) != 0 ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, s.dragMesh );
    if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ) { return false; }
    f64 best = 9.0 * 9.0;
    for ( u32 k = 0; k < object->nPoints; ++k ) {
        const auto index = object->iFirstPoint + k;
        const u64 id = ws->wire.pointSourceIds.pData[index];
        if ( id == 0 || std::any_of( s.dragVertices.begin(), s.dragVertices.end(),
             [&]( id_t moving ) { return moving.value == id; } ) ) { continue; }
        const vec3 point = ws->wire.points.pData[index], offset = Sub( point, s.dragGrab );
        if ( vertical ? std::hypot( offset.x, offset.y ) > 1e-6 : std::fabs( Dot( planeNormal, offset ) ) > 1e-6 ) { continue; }
        QPointF screen;
        if ( !Project( e.view, point, screen ) ) { continue; }
        const f64 dx = screen.x() - e.screen.x(), dy = screen.y() - e.screen.y(), distance = dx * dx + dy * dy;
        if ( !std::isfinite( distance ) || distance > best ||
             ( distance == best && vertex.value != 0 && id >= vertex.value ) ) { continue; }
        best = distance; target = point; vertex = { id };
    }
    return vertex.value != 0;
}

// The offset the drag describes: on the 2D pane's plane; in 3D on the
// horizontal plane through the handle, or vertically with Alt. Moves snap
// to the grid step by step (Shift: the handle lands on grid points, Ctrl:
// no snapping). A nearby stationary vertex handle takes precedence over normal
// grid snapping; Shift absolute-grid placement and Ctrl/Command bypass it.
void UpdateComponentDrag( const map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    s.dragDelta = {}; s.dragSnapVertex = {};
    vec3 target{}, planeNormal{ 0, 0, 1 };
    bool vertical = false;
    if ( e.view.bOrtho ) {
        const u32 depth = DepthAxis( e.view.axisU, e.view.axisV );
        planeNormal = AxisVector( depth );
        target = OrthoPoint( e, Comp( s.dragGrab, depth ) );
    } else {
        // TrenchBroom: vertices slide on the horizontal plane through the
        // handle; holding Alt during the drag moves them vertically (Alt at
        // the press is camera orbit, decided before the drag starts). When
        // the floor plane is nearly edge-on, the upright plane facing the
        // camera takes over so the handle cannot fly off to the horizon.
        vertical = ( e.modifiers & Qt::AltModifier ) != 0;
        const bool across = vertical || std::fabs( e.rayDirection.z ) < 0.2;
        if ( across ) {
            // Face the captured handle's eye direction, not the changing
            // cursor ray. The fallback plane must remain fixed during a drag.
            vec3 facing{ e.view.eye.x - s.dragGrab.x, e.view.eye.y - s.dragGrab.y, 0.0 };
            if ( Length( facing ) < 1e-6 ) { facing = { 1, 0, 0 }; }
            facing = Mul( facing, 1.0 / Length( facing ) );
            planeNormal = facing;
            if ( !RayPlane( e.rayOrigin, e.rayDirection, { facing, -Dot( facing, s.dragGrab ) }, target ) ) { return; }
            if ( vertical ) { target.x = s.dragGrab.x; target.y = s.dragGrab.y; }
        } else if ( !RayPlane( e.rayOrigin, e.rayDirection, { { 0, 0, 1 }, -s.dragGrab.z }, target ) ) {
            return;
        }
    }
    const bool acquired = ComponentVertexTarget( ws, s, e, planeNormal, vertical, target, s.dragSnapVertex );
    vec3 delta = Sub( target, s.dragGrab );
    if ( !acquired && !SnapBypassed( e.modifiers ) ) {
        delta = ( e.modifiers & Qt::ShiftModifier ) != 0 ? Sub( SnapAll( ws, target, e.modifiers ), s.dragGrab ) : SnapAll( ws, delta, e.modifiers );
    }
    if ( e.view.bOrtho ) { SetComp( delta, DepthAxis( e.view.axisU, e.view.axisV ), 0.0 ); }
    s.dragDelta = delta;
    s.bComponentMoved = s.bComponentMoved || std::hypot( e.screen.x() - s.dragPress.x(), e.screen.y() - s.dragPress.y() ) > 3.0;
}

void RecordModellingRepeat( map_workspace_t *ws, map_component_transform_t kind, u64 object, bool brush,
    const math::affine3d_t &affine, vec3 delta, bool extrude ) noexcept
{
    const auto found = g_states.find( ws );
    if ( found == g_states.end() || ws->pDocument == nullptr ) { return; }
    found->second.repeat = { true, true, kind, ws->elementMode, object, ws->pDocument->geometry.revision,
                            ws->pDocument, affine, delta, extrude, brush };
}

void CommitComponentDrag( map_workspace_t *ws, state_t &s, const map_tool_event_t &e )
{
    s.bComponentDrag = false;
    const u64 mesh = s.dragMesh;
    if ( !s.bComponentMoved || Length( s.dragDelta ) <= 1e-9 ) { return; }
    const auto ids = s.dragVertices;
    const auto edges = s.dragEdges;
    const vec3 delta = s.dragDelta;
    const bool extend = s.bComponentExtend && !edges.empty();
    // Weld in one topology transaction at the stationary targets. Moving
    // onto them first creates zero-length edges that validation must reject.
    struct commit_storage_t {
        vector_t<id_t> pairs{};
        geo::mesh_selection_t next{};
        ~commit_storage_t() { Vector_Shutdown( &pairs ); geo::MeshSelection_Shutdown( &next ); }
    } storage;
    auto &pairs = storage.pairs;
    auto &next = storage.next;
    if ( !Vector_Init( &pairs, ws->pGui->pAllocator ) ||
         ( edges.empty() && !Vector_Reserve( &pairs, ids.size() * 2 ) ) ) {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Not enough memory to prepare the component edit. The map has not changed." ) ); return;
    }
    if ( ws->elementMode == map_element_mode_t::VERTICES ) {
        const auto *object = MapWireframe_FindObject( ws->wire, mesh );
        for ( usize i = 0; i < ids.size(); ++i ) {
            const vec3 destination = Add( s.dragOrigins[i], delta );
            id_t target{};
            for ( u32 k = 0; object != nullptr && k < object->nPoints; ++k ) {
                const u64 id = ws->wire.pointSourceIds.pData[object->iFirstPoint + k];
                if ( id == 0 || std::any_of( ids.begin(), ids.end(), [&]( id_t v ) { return v.value == id; } ) ||
                     Length( Sub( ws->wire.points.pData[object->iFirstPoint + k], destination ) ) > 1e-6 ) { continue; }
                if ( target.value != 0 && target.value != id ) {
                    MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "More than one vertex occupies the weld target. Select an unambiguous target first." ) ); return;
                }
                target = { id };
            }
            if ( target.value != 0 ) {
                ( void )Vector_PushBack( &pairs, target ); ( void )Vector_PushBack( &pairs, ids[i] );
            }
        }
        if ( pairs.nCount != 0 && pairs.nCount != ids.size() * 2 ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Only part of the selection reaches a weld target. Select matching vertex columns before welding." ) ); return;
        }
    }
    const bool weld = pairs.nCount != 0;
    if ( geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { mesh } ) != geo::geometry_status_t::OK ) {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Not enough memory to prepare the component selection. The map has not changed." ) ); return;
    }
    const map_prepared_component_selection_t prepared{ &next, extend ? geo::mesh_edge_ref_t{} : ws->selectedMeshEdgeSeed,
        edges.empty() && e.view.bOrtho ? static_cast<int>( DepthAxis( e.view.axisU, e.view.axisV ) ) : -1 };
    const bool done = Apply( ws, extend ? "Extend Edges" : weld ? "Weld Vertices" : edges.empty() ? "Move Vertices" : "Move Edges",
        [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
            if ( !Keep( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
            if ( extend ) { return MapTopologyEdit_ExtendEdgesBy( copy, mesh, { edges.data(), edges.size() }, delta, &next.edges ); }
            if ( !edges.empty() ) {
                for ( const auto edge : edges ) {
                    if ( geo::MeshSelection_TryAddEdge( &next, edge ) != geo::geometry_status_t::OK ) { return map_status_t::OUT_OF_MEMORY; }
                }
            } else {
                for ( usize i = 0; i < ids.size(); ++i ) {
                    if ( geo::MeshSelection_TryAddVertex( &next, weld ? pairs.pData[i * 2] : ids[i] ) != geo::geometry_status_t::OK ) { return map_status_t::OUT_OF_MEMORY; }
                }
            }
            return weld ? MapTopologyEdit_WeldVertexPairs( copy, mesh, { pairs.pData, pairs.nCount } ) :
                MapTopologyEdit_MoveVertices( copy, mesh, { ids.data(), ids.size() }, delta );
        }, &prepared );
    if ( done && !weld ) {
        RecordModellingRepeat( ws, map_component_transform_t::TRANSLATE, mesh, false,
            math::Affine3d_FromTranslation( delta ), delta, extend );
    }
    if ( !done && extend ) {
        MapNotice_Post( ws, map_notice_t::WARNING, ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ?
            QStringLiteral( "Not enough memory to extend this edge. The map has not changed." ) :
            QStringLiteral( "Extend needs an open boundary edge and a non-collapsed new face. Use a Quad or remove a face to open a closed mesh." ) );
    }
    if ( !done && weld ) {
        MapNotice_Post( ws, ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ? map_notice_t::ERROR : map_notice_t::WARNING,
            ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ?
            QStringLiteral( "Not enough memory to weld these vertices. The map has not changed." ) :
            QStringLiteral( "These weld targets would collapse or invalidate the surface. The map has not changed." ) );
    }
}

// Modified component gestures stay on the mesh that owns the selection.
void ComponentOwnerHint( const map_workspace_t *ws ) noexcept
{
    Hint( ws, QStringLiteral( "Components are selected on one mesh at a time. Click without Shift/Ctrl/Command to switch meshes." ) );
}

// Plain lasso replaces within the current mesh/root when possible, otherwise
// choosing the most eligible components, then the lowest object ID. Shift
// unions within one mesh; Ctrl/Command subtracts there and takes precedence.
void SelectInLasso( map_workspace_t *ws, const state_t &s, const map_tool_event_t &e ) noexcept
{
    const QRectF box = QRectF( s.lassoA, s.lassoB ).normalized();
    const bool edgeMode = ws->elementMode == map_element_mode_t::EDGES;
    const bool remove = SnapBypassed( s.lassoModifiers );
    const bool add = !remove && ( s.lassoModifiers & Qt::ShiftModifier ) != 0;
    const usize currentCount = edgeMode ? ws->meshSelection.edges.nCount : ws->meshSelection.vertices.nCount;
    const u64 owner = currentCount != 0u ? ws->meshSelection.meshId.value : 0u;
    if ( remove && owner == 0u ) { return; }
    struct selection_t {
        geo::mesh_selection_t value{};
        ~selection_t() { geo::MeshSelection_Shutdown( &value ); }
    } chosen, result;
    const auto *allocator = ws->pGui->pAllocator;
    const auto noMemory = [&]() {
        Hint( ws, QStringLiteral( "Not enough memory to select components. The selection has not changed." ) );
    };
    usize bestCount = 0u;
    int bestPriority = -1;
    bool foreignCandidate = false;
    for ( usize o = 0u; o < ws->wire.objects.nCount; ++o ) {
        const auto &object = ws->wire.objects.pData[o];
        if ( object.kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( ws, object ) ) { continue; }
        selection_t candidate;
        if ( geo::MeshSelection_Init( &candidate.value, allocator, { object.id } ) != geo::geometry_status_t::OK ) { noMemory(); return; }
        for ( u32 k = 0u; k < object.nPoints; ++k ) {
            const u32 index = object.iFirstPoint + k;
            const u64 id = ws->wire.pointSourceIds.pData[index];
            if ( id == 0u ) { continue; }
            const vec3 p = ws->wire.points.pData[index];
            QPointF screen;
            if ( !Project( e.view, p, screen ) || !box.contains( screen ) ) { continue; }
            if ( !SelectedForHandles( ws, object.id ) && Occluded( ws, e, p ) ) { continue; }
            if ( geo::MeshSelection_TryAddVertex( &candidate.value, { id } ) != geo::geometry_status_t::OK ) { noMemory(); return; }
        }
        if ( edgeMode ) {
            // Count distinct complete edges, not just endpoints in the box.
            for ( u32 l = 0u; l < object.nLines; ++l ) {
                const auto &line = ws->wire.lines.pData[object.iFirstLine + l];
                const id_t a{ ws->wire.pointSourceIds.pData[line.iA] }, b{ ws->wire.pointSourceIds.pData[line.iB] };
                if ( a.value == 0u || a.value == b.value || !geo::MeshSelection_HasVertex( &candidate.value, a ) ||
                     !geo::MeshSelection_HasVertex( &candidate.value, b ) ) { continue; }
                if ( geo::MeshSelection_TryAddEdge( &candidate.value, geo::MeshEdgeRef_Make( a, b ) ) != geo::geometry_status_t::OK ) { noMemory(); return; }
            }
        }
        const usize count = edgeMode ? candidate.value.edges.nCount : candidate.value.vertices.nCount;
        if ( count == 0u ) { continue; }
        if ( ( add || remove ) && owner != 0u && object.id != owner ) { foreignCandidate = true; continue; }
        const int priority = object.id == ws->meshSelection.meshId.value ? 2 :
            ws->selection.ids.nCount == 1u && object.id == ws->selection.ids.pData[0] ? 1 : 0;
        if ( chosen.value.meshId.value != 0u && ( priority < bestPriority ||
             ( priority == bestPriority && ( count < bestCount ||
               ( count == bestCount && object.id >= chosen.value.meshId.value ) ) ) ) ) { continue; }
        if ( chosen.value.faces.pAllocator == nullptr ) {
            if ( geo::MeshSelection_Init( &chosen.value, allocator, { object.id } ) != geo::geometry_status_t::OK ) { noMemory(); return; }
        } else { geo::MeshSelection_Clear( &chosen.value ); chosen.value.meshId = { object.id }; }
        const bool copied = edgeMode ? Vector_Append( &chosen.value.edges, { candidate.value.edges.pData, candidate.value.edges.nCount } ) != CY_FALSE :
            Vector_Append( &chosen.value.vertices, { candidate.value.vertices.pData, candidate.value.vertices.nCount } ) != CY_FALSE;
        if ( !copied ) { noMemory(); return; }
        bestCount = count; bestPriority = priority;
    }
    const u64 mesh = chosen.value.meshId.value;
    if ( mesh == 0u ) {
        if ( foreignCandidate ) { ComponentOwnerHint( ws ); }
        return;
    }
    if ( geo::MeshSelection_Init( &result.value, allocator, { mesh } ) != geo::geometry_status_t::OK ) { noMemory(); return; }
    if ( ( add || remove ) && mesh == ws->meshSelection.meshId.value ) {
        const bool copied = edgeMode ? Vector_Append( &result.value.edges, { ws->meshSelection.edges.pData, ws->meshSelection.edges.nCount } ) != CY_FALSE :
            Vector_Append( &result.value.vertices, { ws->meshSelection.vertices.pData, ws->meshSelection.vertices.nCount } ) != CY_FALSE;
        if ( !copied ) { noMemory(); return; }
    }
    bool changed = false;
    if ( edgeMode ) {
        for ( usize i = 0u; i < chosen.value.edges.nCount; ++i ) {
            const auto edge = chosen.value.edges.pData[i];
            if ( remove ) { changed |= geo::MeshSelection_RemoveEdge( &result.value, edge ); }
            else if ( !geo::MeshSelection_HasEdge( &result.value, edge ) ) {
                if ( geo::MeshSelection_TryAddEdge( &result.value, edge ) != geo::geometry_status_t::OK ) { noMemory(); return; }
                changed = true;
            }
        }
        if ( !changed ) { return; }
        if ( result.value.edges.nCount == 0u ) { MapWorkspace_ClearMeshEdges( ws ); return; }
        ( void )MapWorkspace_SetMeshComponents( ws, mesh, {}, { result.value.edges.pData, result.value.edges.nCount } );
        return;
    }
    for ( usize i = 0u; i < chosen.value.vertices.nCount; ++i ) {
        const auto vertex = chosen.value.vertices.pData[i];
        if ( remove ) { changed |= geo::MeshSelection_RemoveVertex( &result.value, vertex ); }
        else if ( !geo::MeshSelection_HasVertex( &result.value, vertex ) ) {
            if ( geo::MeshSelection_TryAddVertex( &result.value, vertex ) != geo::geometry_status_t::OK ) { noMemory(); return; }
            changed = true;
        }
    }
    if ( !changed ) { return; }
    if ( result.value.vertices.nCount == 0u ) { MapWorkspace_ClearMeshVertices( ws ); return; }
    const int depthAxis = e.view.bOrtho ? static_cast<int>( DepthAxis( e.view.axisU, e.view.axisV ) ) : -1;
    const bool sameProjection = ( !add && !remove ) || ws->meshSelection.vertices.nCount == 0u ||
        ( ws->meshVertexColumnRevision == ws->selection.revision && ws->meshVertexColumnAxis == depthAxis );
    if ( MapWorkspace_SetMeshComponents( ws, mesh, { result.value.vertices.pData, result.value.vertices.nCount }, {} ) ) {
        ws->meshVertexColumnAxis = sameProjection ? depthAxis : -1;
        ws->meshVertexColumnRevision = ws->selection.revision;
    }
}

// How far from the press a selected component still counts as grabbed: the
// view's click picked it there, with its own (slightly different) radius.
constexpr f64 kGrabPixels = 10.0;

// The vertex of `ids` on `mesh` nearest the press, within the grab radius.
// In a 2D pane several vertices share a pixel; the selected one is meant.
bool NearestVertexOf( const map_workspace_t *ws, const map_tool_event_t &e, QPointF at, u64 mesh, const std::vector<id_t> &ids, id_t &out,
    vec3 &position ) noexcept
{
    const auto *object = MapWireframe_FindObject( ws->wire, mesh );
    f64 best = kGrabPixels * kGrabPixels;
    bool found = false;
    for ( u32 k = 0; object != nullptr && k < object->nPoints; ++k ) {
        const u64 id = ws->wire.pointSourceIds.pData[object->iFirstPoint + k];
        if ( id == 0 || std::none_of( ids.begin(), ids.end(), [&]( id_t v ) { return v.value == id; } ) ) { continue; }
        const vec3 p = ws->wire.points.pData[object->iFirstPoint + k];
        QPointF screen;
        if ( !Project( e.view, p, screen ) ) { continue; }
        const f64 d = std::pow( screen.x() - at.x(), 2 ) + std::pow( screen.y() - at.y(), 2 );
        if ( d < best ) { best = d; found = true; out = { id }; position = p; }
    }
    return found;
}

// The edge of `edges` on `mesh` nearest the press, and the grabbed point on it.
edge_result_t NearestEdgeOf( const map_workspace_t *ws, const map_tool_event_t &e, QPointF at, u64 mesh, const std::vector<geo::mesh_edge_ref_t> &edges,
    geo::mesh_edge_ref_t &out, vec3 &grab ) noexcept
{
    if ( mesh == 0u || edges.empty() ) { return edge_result_t::MISS; }
    if ( e.view.pfnPickEdge != nullptr ) {
        edge_hit_t hit{};
        const auto result = e.view.pfnPickEdge( e.view.pView, { at, kGrabPixels, mesh, nullptr, edges.data(), edges.size() }, &hit );
        if ( result == edge_result_t::HIT ) { out = hit.edge; grab = Add( hit.a, Mul( Sub( hit.b, hit.a ), hit.t ) ); }
        return result;
    }
    // Synthetic test views without a viewport query keep their old behavior.
    const auto *source = geo::GeometryDocument_FindMesh( &ws->pDocument->geometry, { mesh } );
    f64 best = kGrabPixels;
    bool found = false;
    for ( const auto &edge : edges ) {
        vec3 pa{}, pb{};
        QPointF a, b;
        if ( !VertexAt( source, edge.a, pa ) || !VertexAt( source, edge.b, pb ) || !Project( e.view, pa, a ) || !Project( e.view, pb, b ) ) { continue; }
        f64 t = 0.0;
        const f64 distance = SegmentDistance( at, a, b, t );
        if ( distance >= best ) { continue; }
        best = distance; found = true; out = edge; grab = Add( pa, Mul( Sub( pb, pa ), t ) );
    }
    return found ? edge_result_t::HIT : edge_result_t::MISS;
}

// The press waited for movement; now the drag starts. What moves: the
// selection from before the press when the press was on one of its handles
// (a plain press reselected just the clicked one), else the selection the
// view's click left under the press, else the handle under the press,
// selected the way TrenchBroom does (a drag on an unselected handle selects
// it). False when the press grabbed nothing (empty space, Ctrl toggled the
// handle off, the brush may not become a mesh): the drag box-selects.
bool StartGrabbedDrag( map_workspace_t *ws, state_t &s, const map_tool_event_t &e, bool &captureInvalidated )
{
    s.bGrabPending = false;
    captureInvalidated = false;
    const bool toggle = SnapBypassed( s.grabModifiers ), add = !toggle && ( s.grabModifiers & Qt::ShiftModifier ) != 0;
    const u64 current = ws->meshSelection.meshId.value;
    const bool hasComponents = ws->elementMode == map_element_mode_t::VERTICES ?
        ws->meshSelection.vertices.nCount != 0u : ws->meshSelection.edges.nCount != 0u;
    if ( ( toggle || add ) && hasComponents && s.bGrabHit && s.grabMesh != current ) {
        // The click was refused under the one-mesh policy. Movement must not
        // turn it into a different selection or convert a foreign brush.
        ComponentOwnerHint( ws ); captureInvalidated = true; return false;
    }
    vec3 grab{};
    if ( ws->elementMode == map_element_mode_t::VERTICES ) {
        std::vector<id_t> selected( ws->meshSelection.vertices.pData, ws->meshSelection.vertices.pData + ws->meshSelection.vertices.nCount );
        id_t vertex{};
        if ( !toggle && !add && s.priorVertices.size() > 1 && NearestVertexOf( ws, e, s.grabPress, s.priorMesh, s.priorVertices, vertex, grab ) ) {
            if ( !MapWorkspace_SetMeshComponents( ws, s.priorMesh, { s.priorVertices.data(), s.priorVertices.size() }, {} ) ) { captureInvalidated = true; return false; }
        } else if ( !NearestVertexOf( ws, e, s.grabPress, current, selected, vertex, grab ) ) {
            if ( toggle || !s.bGrabHit ) { return false; }
            // A brush corner: the view's click converts a clicked brush, a
            // corner just outside its pick still needs it.
            const auto *object = MapWireframe_FindObject( ws->wire, s.grabMesh );
            if ( object == nullptr || ( object->kind == map_wire_kind_t::BRUSH && !MapWorkspace_ConvertBrushForComponents( ws, s.grabMesh ) ) ) { return false; }
            vertex = s.grabVertex; grab = s.grabPoint;
            if ( s.grabBrush && !MeshVertexAt( ws, s.grabMesh, s.grabPoint, vertex ) ) { return false; }
            std::vector<id_t> next;
            if ( add && ws->meshSelection.meshId.value == s.grabMesh ) { next.assign( ws->meshSelection.vertices.pData, ws->meshSelection.vertices.pData + ws->meshSelection.vertices.nCount ); }
            // In 2D the handle is the whole stacked column (see the views' click).
            const std::vector<id_t> column = e.view.bOrtho ? ColumnVertices( ws, s.grabMesh, s.grabPoint, e.view ) : std::vector<id_t>{ vertex };
            for ( const id_t v : column.empty() ? std::vector<id_t>{ vertex } : column ) {
                if ( std::none_of( next.begin(), next.end(), [&]( id_t other ) { return other.value == v.value; } ) ) { next.push_back( v ); }
            }
            std::sort( next.begin(), next.end(), []( id_t a, id_t b ) { return a.value < b.value; } );
            if ( !MapWorkspace_SetMeshComponents( ws, s.grabMesh, { next.data(), next.size() }, {} ) ) { captureInvalidated = true; return false; }
        }
        BeginComponentDrag( ws, s, e, ws->meshSelection.meshId.value, grab, vertex );
    } else {
        std::vector<geo::mesh_edge_ref_t> selected( ws->meshSelection.edges.pData, ws->meshSelection.edges.pData + ws->meshSelection.edges.nCount );
        geo::mesh_edge_ref_t edge{};
        const auto prior = !toggle && !add && s.priorEdges.size() > 1
            ? NearestEdgeOf( ws, e, s.grabPress, s.priorMesh, s.priorEdges, edge, grab ) : edge_result_t::MISS;
        if ( prior == edge_result_t::FAILED ) { captureInvalidated = true; return false; }
        if ( prior == edge_result_t::HIT ) {
            if ( !MapWorkspace_SetMeshComponents( ws, s.priorMesh, {}, { s.priorEdges.data(), s.priorEdges.size() } ) ) { captureInvalidated = true; return false; }
        } else {
            const auto selectedHit = NearestEdgeOf( ws, e, s.grabPress, current, selected, edge, grab );
            if ( selectedHit == edge_result_t::FAILED ) { captureInvalidated = true; return false; }
            if ( selectedHit == edge_result_t::MISS ) {
                if ( toggle || !s.bGrabHit ) { return false; }
                if ( e.view.pfnPickEdge != nullptr ) {
                    // Revalidate the original pickup before restoring it;
                    // a cached press hit is not proof that it is still visible.
                    edge_hit_t captured{};
                    const auto result = e.view.pfnPickEdge( e.view.pView,
                        { s.grabPress, kGrabPixels, s.grabMesh, nullptr, &s.grabEdge, 1u }, &captured );
                    if ( result != edge_result_t::HIT ) { captureInvalidated = true; return false; }
                    grab = Add( captured.a, Mul( Sub( captured.b, captured.a ), captured.t ) );
                } else { grab = s.grabPoint; }
                if ( !MapWorkspace_SelectMeshEdge( ws, s.grabMesh, s.grabEdge, add ? MAP_SELECT_ADD : MAP_SELECT_REPLACE ) ) { captureInvalidated = true; return false; }
            }
        }
        BeginComponentDrag( ws, s, e, ws->meshSelection.meshId.value, grab, {} );
    }
    s.dragPress = s.grabPress;
    return true;
}

bool ComponentDragEventImpl( map_workspace_t *ws, state_t &s, const map_tool_event_t &e )
{
    if ( ( s.bComponentDrag || s.bGrabPending ) && ( ws->pDocument != s.dragDocument || ws->selection.revision != s.dragSelectionRevision ||
        ws->pDocument == nullptr || ws->pDocument->geometry.revision != s.dragGeometryRevision ) ) {
        s.bComponentDrag = s.bGrabPending = false; s.dragDelta = {}; return false;
    }
    if ( s.bComponentDrag && e.kind != map_tool_event_kind_t::CANCEL && e.view.pView != s.grabView ) { return false; }
    switch ( e.kind ) {
        case map_tool_event_kind_t::PRESS: {
            // Note the press, the selection before it, and the handle under it;
            // the view's own click then picks and selects (its occlusion rules,
            // Shift adds, Ctrl toggles, a clicked brush converts). Whether this
            // becomes a drag of what is under the press or a box selection is
            // decided once the mouse moves.
            s.bLasso = false; s.bGrabPending = true; s.bGrabHit = false;
            s.grabView = e.view.pView; s.grabPress = e.screen; s.grabModifiers = e.modifiers;
            s.bComponentExtend = EdgeDragExtends( ws );
            s.dragDocument = ws->pDocument; s.dragSelectionRevision = ws->selection.revision;
            s.dragGeometryRevision = ws->pDocument->geometry.revision;
            s.priorMesh = ws->meshSelection.meshId.value;
            s.priorVertices.assign( ws->meshSelection.vertices.pData, ws->meshSelection.vertices.pData + ws->meshSelection.vertices.nCount );
            s.priorEdges.assign( ws->meshSelection.edges.pData, ws->meshSelection.edges.pData + ws->meshSelection.edges.nCount );
            if ( ws->elementMode == map_element_mode_t::VERTICES ) {
                handle_hit_t hit{};
                if ( VertexHandleAt( ws, e, hit ) ) {
                    s.bGrabHit = true;
                    s.grabMesh = hit.object; s.grabPoint = hit.position; s.grabBrush = hit.brush; s.grabVertex = hit.vertex;
                }
            } else {
                edge_hit_t edge{};
                const auto result = NearestMeshEdge( ws, e, kEdgePixels, edge );
                if ( result == edge_result_t::FAILED ) { s.bGrabPending = false; return true; }
                if ( result == edge_result_t::HIT && ( e.view.pfnPickEdge != nullptr ||
                     !Occluded( ws, e, Add( edge.a, Mul( Sub( edge.b, edge.a ), edge.t ) ) ) ) ) {
                    s.bGrabHit = true;
                    s.grabMesh = edge.mesh; s.grabEdge = edge.edge; s.grabBrush = false; s.grabPoint = Add( edge.a, Mul( Sub( edge.b, edge.a ), edge.t ) );
                }
            }
            return false;
        }
        case map_tool_event_kind_t::MOVE:
            if ( s.bGrabPending ) {
                if ( !e.bButtonDown || e.view.pView != s.grabView ) { s.bGrabPending = false; return false; }
                if ( std::hypot( e.screen.x() - s.grabPress.x(), e.screen.y() - s.grabPress.y() ) <= 3.0 ) { return true; }
                bool captureInvalidated = false;
                if ( StartGrabbedDrag( ws, s, e, captureInvalidated ) ) { UpdateComponentDrag( ws, s, e ); return true; }
                if ( captureInvalidated ) { return true; } // An invalidated pickup must not become an empty-space lasso.
                s.bLasso = true; s.lassoView = s.grabView; s.lassoA = s.grabPress; s.lassoB = e.screen; s.lassoModifiers = s.grabModifiers;
                return true;
            }
            if ( s.bComponentDrag && e.bButtonDown ) { UpdateComponentDrag( ws, s, e ); return true; }
            if ( s.bLasso && e.bButtonDown && e.view.pView == s.lassoView ) { s.lassoB = e.screen; return true; }
            if ( s.bLasso && !e.bButtonDown ) { s.bLasso = false; }
            return false;
        case map_tool_event_kind_t::RELEASE:
            if ( s.bGrabPending ) {
                s.bGrabPending = false;
                // Only an unmodified body click may adopt another object.
                // Modified component misses retain the current editing context;
                // this fallback must not undo the native picker's refusal.
                const bool picked = ws->elementMode == map_element_mode_t::VERTICES ? ws->meshSelection.vertices.nCount != 0 : ws->meshSelection.edges.nCount != 0;
                const bool modified = ( s.grabModifiers & ( Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier ) ) != 0;
                surface_hit_t hit{};
                if ( !modified && !picked && !s.bGrabHit && SurfaceHit( ws, e, hit ) && ( hit.kind == map_wire_kind_t::MESH || hit.kind == map_wire_kind_t::BRUSH ) &&
                     !EditorSelection_Contains( &ws->selection, hit.object ) ) {
                    MapWorkspace_Select( ws, hit.object, MAP_SELECT_REPLACE );
                    return true;
                }
                return false;
            }
            if ( s.bComponentDrag ) { UpdateComponentDrag( ws, s, e ); CommitComponentDrag( ws, s, e ); return true; }
            if ( s.bLasso ) {
                s.bLasso = false;
                if ( e.view.pView == s.lassoView && std::hypot( s.lassoB.x() - s.lassoA.x(), s.lassoB.y() - s.lassoA.y() ) >= 4.0 ) { SelectInLasso( ws, s, e ); return true; }
            }
            return false;
        case map_tool_event_kind_t::CANCEL:
            if ( s.bComponentDrag || s.bLasso || s.bGrabPending ) { s.bComponentDrag = false; s.bLasso = false; s.bGrabPending = false; return true; }
            return false;
        default: return false;
    }
}

bool ComponentDragEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    try { return ComponentDragEventImpl( ws, s, e ); }
    catch ( ... ) {
        s.bComponentDrag = s.bComponentMoved = s.bGrabPending = s.bLasso = false;
        s.dragDelta = {}; s.dragSnapVertex = {}; s.dragDocument = nullptr;
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Not enough memory to record the component gesture. The drag was cancelled." ) );
        MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
        return true; // A failed pickup must never fall through into lasso.
    }
}

void ComponentDragPaint( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    if ( s.bLasso && view.pView == s.lassoView && std::hypot( s.lassoB.x() - s.lassoA.x(), s.lassoB.y() - s.lassoA.y() ) >= 4.0 ) {
        painter.setPen( QPen( QColor( 120, 200, 255 ), 1.0, Qt::DashLine ) );
        painter.setBrush( QColor( 120, 200, 255, 30 ) );
        painter.drawRect( QRectF( s.lassoA, s.lassoB ).normalized() );
        painter.setBrush( Qt::NoBrush );
    }
    if ( !s.bComponentDrag || !s.bComponentMoved ) { return; }
    // The mesh lines that touch the moving vertices, where they will be, and
    // a guide from the grabbed handle's start to its new place.
    const auto moving = [&]( u64 id ) { return std::any_of( s.dragVertices.begin(), s.dragVertices.end(), [&]( id_t v ) { return v.value == id; } ); };
    const auto *object = MapWireframe_FindObject( ws->wire, s.dragMesh );
    if ( s.bComponentExtend ) {
        const auto *mesh = geo::GeometryDocument_FindMesh( &ws->pDocument->geometry, { s.dragMesh } );
        for ( const auto edge : s.dragEdges ) {
            vec3 a{}, b{};
            if ( !VertexAt( mesh, edge.a, a ) || !VertexAt( mesh, edge.b, b ) ) { continue; }
            const vec3 nextA = Add( a, s.dragDelta ), nextB = Add( b, s.dragDelta );
            QPolygonF face;
            bool visible = true;
            for ( const vec3 point : { a, b, nextB, nextA } ) {
                QPointF at; if ( !Project( view, point, at ) ) { visible = false; break; } face << at;
            }
            if ( visible && !view.bOrtho ) {
                painter.setPen( Qt::NoPen ); painter.setBrush( QColor( 105, 185, 225, 32 ) );
                painter.drawPolygon( face ); painter.setBrush( Qt::NoBrush );
            }
            DrawLine( painter, view, a, nextA, Hover() ); DrawLine( painter, view, b, nextB, Hover() );
            DrawLine( painter, view, nextA, nextB, Accent() );
        }
    } else for ( u32 l = 0; object != nullptr && l < object->nLines; ++l ) {
        const auto &line = ws->wire.lines.pData[object->iFirstLine + l];
        const bool movesA = moving( ws->wire.pointSourceIds.pData[line.iA] ), movesB = moving( ws->wire.pointSourceIds.pData[line.iB] );
        if ( !movesA && !movesB ) { continue; }
        const vec3 a = ws->wire.points.pData[line.iA], b = ws->wire.points.pData[line.iB];
        DrawLine( painter, view, movesA ? Add( a, s.dragDelta ) : a, movesB ? Add( b, s.dragDelta ) : b, Accent() );
    }
    for ( const vec3 &origin : s.dragOrigins ) {
        QPointF c;
        if ( Project( view, Add( origin, s.dragDelta ), c ) ) {
            painter.setPen( QPen( QColor( 12, 12, 16 ), 1.5 ) ); painter.setBrush( Accent() );
            painter.drawEllipse( c, 5.5, 5.5 ); painter.setBrush( Qt::NoBrush );
        }
    }
    DrawLine( painter, view, s.dragGrab, Add( s.dragGrab, s.dragDelta ), QColor( 255, 220, 60 ), Qt::DashLine );
    const vec3 at = Add( s.dragGrab, s.dragDelta );
    QPointF acquired;
    if ( s.dragSnapVertex.value != 0 && Project( view, at, acquired ) ) {
        painter.setPen( QPen( Accent(), 1.5 ) ); painter.setBrush( Qt::NoBrush ); painter.drawEllipse( acquired, 9.0, 9.0 );
    }
    QString label = QStringLiteral( "%1  %2  %3   (Δ %4  %5  %6)" ).arg( at.x, 0, 'f', 1 ).arg( at.y, 0, 'f', 1 ).arg( at.z, 0, 'f', 1 )
        .arg( s.dragDelta.x, 0, 'f', 1 ).arg( s.dragDelta.y, 0, 'f', 1 ).arg( s.dragDelta.z, 0, 'f', 1 );
    if ( s.dragSnapVertex.value != 0 ) { label += QStringLiteral( "  · Vertex target" ); }
    DrawLabel( painter, view, at, label, QColor( 255, 230, 120 ) );
}

/*
================
Live previews: the polygon's prism, the patch's surface
================
*/

// The solid the polygon will become: its outline (with the hover point)
// lifted by the extrusion, joined at every corner.
void PolygonPreview( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    std::vector<vec3> outline = s.points;
    if ( s.bHover ) { outline.push_back( s.hover ); }
    if ( outline.size() < 2 ) { return; }
    const vec3 lift = PolygonExtrusion( ws, s, view.eye );
    const QColor color( 255, 196, 48, 150 );
    for ( usize i = 0; i < outline.size(); ++i ) {
        const vec3 a = outline[i], b = outline[( i + 1 ) % outline.size()];
        if ( i + 1 < outline.size() || outline.size() >= 3 ) { DrawLine( painter, view, Add( a, lift ), Add( b, lift ), color, Qt::DashLine ); }
        DrawLine( painter, view, a, Add( a, lift ), color, Qt::DotLine );
    }
}

// The patch the drag will create, as its iso-parameter lines. False when the
// box cannot hold the shape yet (the outline is drawn instead).
bool PatchPreview( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    math::aabbd_t box{};
    u32 axis = 2;
    if ( !PatchBox( ws, s, box, axis ) ) { return false; }
    const auto segments = static_cast<u32>( EditorSettings_Integer( &ws->pGui->settings, "editor.map.cylinder_sides", 16 ) );
    geo::geometry_source_id_allocator_t ids{ { 2 } };
    geo::patch_surface_t patch{};
    const bool built = MapCreate_BuildPatch( PatchShape( ws ), box, axis, segments, 0u, { 1 }, &ids, ws->pGui->pAllocator, &patch ) == geo::geometry_status_t::OK;
    if ( built ) {
        constexpr u32 kLines = 12, kSteps = 24;
        const QColor color( 255, 196, 48, 170 );
        for ( u32 line = 0; line <= kLines; ++line ) {
            const f64 a = static_cast<f64>( line ) / kLines;
            for ( u32 step = 0; step < kSteps; ++step ) {
                const f64 b0 = static_cast<f64>( step ) / kSteps, b1 = static_cast<f64>( step + 1 ) / kSteps;
                DrawLine( painter, view, geo::Patch_Evaluate( &patch, a, b0 ).position, geo::Patch_Evaluate( &patch, a, b1 ).position, color );
                DrawLine( painter, view, geo::Patch_Evaluate( &patch, b0, a ).position, geo::Patch_Evaluate( &patch, b1, a ).position, color );
            }
        }
    }
    geo::Patch_Shutdown( &patch );
    return built;
}

/*
================
Component gizmo: move, rotate and scale selected vertices, edges and faces
================
*/

enum class gizmo_kind_t : u8 { TRANSLATE, ROTATE, SCALE };
constexpr f64 kScaleGizmoRadiusPixels = 60.0;
constexpr f64 kScaleAxisRadiusPixels = 96.0;
constexpr f64 kScaleAxisHalfPixels = 4.0;
constexpr f64 kScaleAxisPickPixels = 7.0;

struct gizmo_target_t {
    u64 mesh{ 0 };
    std::vector<id_t> vertices;
    std::vector<geo::mesh_edge_ref_t> edges;
    u64 face{ 0 };                 // The one mesh face Shift-drag extrudes.
    bool bFaces{ false };          // Faces mode: the face selection survives the edit by itself.
    u64 brush{ 0 }, brushSide{ 0 }; // Brush face (Faces mode, Translate tool).
    std::vector<vec3> positions;
    vec3 pivot{};
};

gizmo_kind_t GizmoKind( const map_workspace_t *ws ) noexcept
{
    return ws->tool == map_tool_t::ROTATE ? gizmo_kind_t::ROTATE : ws->tool == map_tool_t::SCALE ? gizmo_kind_t::SCALE : gizmo_kind_t::TRANSLATE;
}

bool VertexAt( const geo::mesh_source_t *mesh, id_t id, vec3 &out ) noexcept
{
    geo::geometry_mesh_vertex_handle_t handle{};
    if ( mesh == nullptr || !geo::MeshSource_TryFindVertex( mesh, id, &handle ) ) { return false; }
    const auto *record = GenerationPool_Get( &mesh->mesh.vertices, handle );
    if ( record == nullptr ) { return false; }
    out = record->position;
    return true;
}

// What a component transform moves, from the session's component selection.
bool ComponentTarget( const map_workspace_t *ws, gizmo_kind_t kind, gizmo_target_t &t ) noexcept
{
    try {
        if ( !Writable( ws ) || ws->editPreview.bActive || EditorHistory_IsTransactionOpen( &ws->history ) ) { return false; }
        const auto mode = ws->elementMode;
        if ( mode == map_element_mode_t::VERTICES && MapWorkspace_HasMeshVertices( ws ) ) {
            t.mesh = ws->meshSelection.meshId.value;
            t.vertices.assign( ws->meshSelection.vertices.pData, ws->meshSelection.vertices.pData + ws->meshSelection.vertices.nCount );
        } else if ( mode == map_element_mode_t::EDGES && MapWorkspace_HasMeshEdges( ws ) ) {
            t.mesh = ws->meshSelection.meshId.value;
            t.edges.assign( ws->meshSelection.edges.pData, ws->meshSelection.edges.pData + ws->meshSelection.edges.nCount );
            std::vector<u64> ids;
            for ( const auto &edge : t.edges ) { ids.push_back( edge.a.value ); ids.push_back( edge.b.value ); }
            std::sort( ids.begin(), ids.end() ); ids.erase( std::unique( ids.begin(), ids.end() ), ids.end() );
            for ( u64 id : ids ) { t.vertices.push_back( { id } ); }
        } else if ( mode == map_element_mode_t::FACES && MapWorkspace_HasMeshFace( ws ) ) {
            // Every selected face's corners, once each.
            t.mesh = ws->selectedMeshFaceObject; t.bFaces = true;
            t.face = MapWorkspace_MeshFaces( ws ).nCount == 1 ? ws->selectedMeshFaceId : 0;
            std::vector<u64> corners;
            for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) {
                const auto &face = ws->wire.faces.pData[f];
                if ( face.id != t.mesh || !MapWorkspace_IsMeshFaceSelected( ws, face.id, face.faceId ) ) { continue; }
                for ( u32 k = 0; k < face.nIndices; ++k ) { corners.push_back( ws->wire.pointSourceIds.pData[ws->wire.faceIndices.pData[face.iFirstIndex + k]] ); }
            }
            std::sort( corners.begin(), corners.end() ); corners.erase( std::unique( corners.begin(), corners.end() ), corners.end() );
            for ( u64 id : corners ) { t.vertices.push_back( { id } ); }
        } else if ( mode == map_element_mode_t::FACES && kind == gizmo_kind_t::TRANSLATE && MapWorkspace_HasBrushFace( ws ) ) {
            // Brush faces only translate: a rotated or scaled side would leave
            // its neighbours non-planar.
            t.brush = ws->selectedBrushFaceObject; t.brushSide = ws->selectedBrushFaceSide;
            for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) {
                const auto &face = ws->wire.faces.pData[f];
                if ( face.id != t.brush || face.sideId != t.brushSide ) { continue; }
                for ( u32 k = 0; k < face.nIndices; ++k ) { t.positions.push_back( ws->wire.points.pData[ws->wire.faceIndices.pData[face.iFirstIndex + k]] ); }
            }
        } else {
            return false;
        }
        if ( t.mesh != 0 ) {
            const auto *mesh = geo::GeometryDocument_FindMesh( &ws->pDocument->geometry, { t.mesh } );
            for ( const id_t &id : t.vertices ) { vec3 p{}; if ( VertexAt( mesh, id, p ) ) { t.positions.push_back( p ); } }
        }
        if ( t.positions.empty() ) { return false; }
        for ( const vec3 &p : t.positions ) { t.pivot = Add( t.pivot, p ); }
        t.pivot = Mul( t.pivot, 1.0 / static_cast<f64>( t.positions.size() ) );
        return true;
    } catch ( ... ) { return false; }
}

// Edge selection exposes move handles directly; the explicit transform
// tools keep their own gizmos. Other Select modes retain their view controls.
bool GizmoTarget( const map_workspace_t *ws, gizmo_target_t &t ) noexcept
{
    const bool selectedEdges = ws->tool == map_tool_t::SELECT && ws->elementMode == map_element_mode_t::EDGES;
    if ( !selectedEdges && ws->tool != map_tool_t::TRANSLATE && ws->tool != map_tool_t::ROTATE && ws->tool != map_tool_t::SCALE ) { return false; }
    return ComponentTarget( ws, GizmoKind( ws ), t );
}

// Rotate and Scale turn about the Pivot tool's point when one is placed.
bool GizmoTargetFor( const map_workspace_t *ws, const state_t &s, gizmo_target_t &t ) noexcept
{
    if ( !GizmoTarget( ws, t ) ) { return false; }
    if ( s.bPivot && GizmoKind( ws ) != gizmo_kind_t::TRANSLATE ) { t.pivot = s.pivot; }
    return true;
}

bool GizmoApplies( const map_workspace_t *ws ) noexcept
{
    gizmo_target_t target{};
    return ws != nullptr && ws->pDocument != nullptr && GizmoTarget( ws, target );
}

f64 GizmoLength( const map_tool_view_t &view, vec3 pivot ) noexcept
{
    return view.bOrtho ? 72.0 / std::max( view.pixelsPerUnit, 1e-9 ) : std::max( 1.0, Length( Sub( pivot, view.eye ) ) * 0.15 );
}

bool AxisInView( const map_tool_view_t &view, int axis ) noexcept
{
    return !view.bOrtho || static_cast<u32>( axis ) == view.axisU || static_cast<u32>( axis ) == view.axisV;
}

struct scale_axis_layout_t {
    bool visible{ false };
    QPointF tip{};
    f64 projectedLength{ 0.0 };
};
struct scale_layout_t {
    QPointF centre{};
    f64 reference{ 0.0 };
    scale_axis_layout_t axes[3]{};
};

// Square tips are screen-space controls outside the unchanged uniform ring.
// Their shafts are only guides, so a ring pickup cannot become an axis pickup.
// This one layout supplies painting, hover and press; indistinguishable axes
// expose only the less foreshortened control instead of two conflicting tips.
bool ScaleLayout( const map_tool_view_t &view, vec3 pivot, scale_layout_t &out ) noexcept
{
    if ( !Project( view, pivot, out.centre ) ) { return false; }
    out.reference = GizmoLength( view, pivot );
    if ( !std::isfinite( out.reference ) || out.reference <= 0.0 ) { return false; }
    for ( int axis = 0; axis < 3; ++axis ) {
        if ( !AxisInView( view, axis ) ) { continue; }
        QPointF endpoint;
        if ( !Project( view, Add( pivot, AxisVector( axis, out.reference ) ), endpoint ) ) { continue; }
        const QPointF projected = endpoint - out.centre;
        const f64 length = std::hypot( projected.x(), projected.y() );
        if ( !std::isfinite( length ) || length < 14.0 ) { continue; }
        const QPointF direction = projected / length;
        const QPointF tip = out.centre + direction * kScaleAxisRadiusPixels;
        if ( !view.bOrtho ) {
            // Along one world line, perspective is q(t)=a*t/(1+b*t),
            // with t measured in reference lengths. The half/full samples
            // recover that mapping without another view API. A normalized
            // screen tip beyond the vanishing point has no positive t.
            QPointF halfway;
            if ( !Project( view, Add( pivot, AxisVector( axis, out.reference * 0.5 ) ), halfway ) ) { continue; }
            const QPointF halfDelta = halfway - out.centre;
            const f64 half = halfDelta.x() * direction.x() + halfDelta.y() * direction.y();
            if ( !std::isfinite( half ) || half <= 0.0 || length - half <= length * 1e-10 ) { continue; }
            const f64 b = ( 2.0 * half - length ) / ( length - half );
            const f64 a = length * ( 1.0 + b );
            const f64 denominator = a - b * kScaleAxisRadiusPixels;
            if ( !std::isfinite( denominator ) || denominator <= 1e-8 ) { continue; }
            const f64 parameter = kScaleAxisRadiusPixels / denominator;
            const vec3 worldTip = Add( pivot, AxisVector( axis, out.reference * parameter ) );
            QPointF check;
            if ( !std::isfinite( parameter ) || parameter <= 0.0 || !Project( view, worldTip, check ) ||
                 QLineF( check, tip ).length() > 0.01 ) { continue; }
            const vec3 ray = Unit( Sub( worldTip, view.eye ) );
            const f64 axial = Comp( ray, axis );
            if ( 1.0 - axial * axial < 1e-8 ) { continue; }
        }
        auto &control = out.axes[axis];
        control.visible = true; control.projectedLength = length;
        control.tip = tip;
    }
    for ( int axis = 0; axis < 3; ++axis ) {
        if ( !out.axes[axis].visible ) { continue; }
        for ( int other = axis + 1; other < 3; ++other ) {
            if ( !out.axes[other].visible ||
                 std::abs( out.axes[axis].tip.x() - out.axes[other].tip.x() ) > 2.0 * kScaleAxisPickPixels ||
                 std::abs( out.axes[axis].tip.y() - out.axes[other].tip.y() ) > 2.0 * kScaleAxisPickPixels ) { continue; }
            if ( out.axes[other].projectedLength > out.axes[axis].projectedLength ) { out.axes[axis].visible = false; break; }
            out.axes[other].visible = false;
        }
    }
    return true;
}

void ResetGizmo( state_t &s ) noexcept
{
    s.bGizmo = s.bGizmoExtrude = false;
    s.gizmoView = nullptr; s.gizmoDocument = nullptr; s.gizmoObject = 0;
    s.gizmoSelectionRevision = s.gizmoGeometryRevision = 0;
    s.gizmoScalePlane = {}; s.gizmoScaleReference = 0.0;
    s.gizmoAffine = math::CY_AFFINE3D_IDENTITY; s.gizmoDelta = {};
    s.gizmoHover = -2;
}

// Notification-time check: no selection reconstruction or allocation. A
// temporary lock, mode change or invalid target cannot revive on late release.
bool GizmoContextMatches( const map_workspace_t *ws, const state_t &s ) noexcept
{
    if ( !s.bGizmo || !Writable( ws ) || ws->tool != s.tool || ws->elementMode != s.gizmoMode ||
         ws->pDocument != s.gizmoDocument || ws->pDocument->geometry.revision != s.gizmoGeometryRevision ||
         ws->selection.revision != s.gizmoSelectionRevision || ws->selection.ids.nCount != 1 ||
         ws->selection.ids.pData[0] != s.gizmoObject || MapLayers_IsObjectLocked( ws, s.gizmoObject ) ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, s.gizmoObject );
    return object != nullptr && MapWorkspace_IsVisible( ws, *object );
}

bool ScaleAxisPoint( const map_tool_event_t &e, const math::planed_t &plane, vec3 &point ) noexcept
{
    const f64 denominator = Dot( plane.normal, e.rayDirection );
    QPointF projected;
    return std::isfinite( denominator ) && std::fabs( denominator ) > 1e-8 &&
        RayPlane( e.rayOrigin, e.rayDirection, plane, point ) &&
        std::isfinite( point.x ) && std::isfinite( point.y ) && std::isfinite( point.z ) &&
        Project( e.view, point, projected ); // Includes the native camera's near plane.
}

// The same world-space circles are painted and picked. Orthographic panes
// expose the ring facing the pane; perspective exposes all usable axes.
f64 GizmoRingRadius( const map_tool_view_t &view, vec3 pivot ) noexcept
{
    return GizmoLength( view, pivot ) * ( 60.0 / 72.0 );
}

bool RingInView( const map_tool_view_t &view, vec3 pivot, int axis ) noexcept
{
    if ( view.bOrtho ) { return static_cast<u32>( axis ) == DepthAxis( view.axisU, view.axisV ); }
    // An edge-on circle cannot provide a stable ray/rotation-plane pickup.
    return std::fabs( Comp( Unit( Sub( view.eye, pivot ) ), static_cast<u32>( axis ) ) ) > 0.04;
}

vec3 RingPoint( vec3 pivot, u32 axis, f64 radius, f64 angle ) noexcept
{
    return Add( pivot, Add( AxisVector( ( axis + 1 ) % 3, radius * std::cos( angle ) ),
                            AxisVector( ( axis + 2 ) % 3, radius * std::sin( angle ) ) ) );
}

// -2: no handle, -1: centre/free scale, 0..2: axis handle or rotation ring.
int GizmoHandleAt( const map_workspace_t *ws, const map_tool_view_t &view, vec3 pivot, QPointF screen ) noexcept
{
    QPointF centre;
    if ( !Project( view, pivot, centre ) ) { return -2; }
    const f64 toCentre = std::hypot( screen.x() - centre.x(), screen.y() - centre.y() );
    if ( GizmoKind( ws ) == gizmo_kind_t::SCALE ) {
        scale_layout_t layout{};
        if ( !ScaleLayout( view, pivot, layout ) ) { return -2; }
        for ( int axis = 0; axis < 3; ++axis ) {
            if ( layout.axes[axis].visible && std::abs( screen.x() - layout.axes[axis].tip.x() ) <= kScaleAxisPickPixels &&
                 std::abs( screen.y() - layout.axes[axis].tip.y() ) <= kScaleAxisPickPixels ) { return axis; }
        }
        // Apart from the outer tips, only the painted ring and pivot are
        // handles. The empty interior stays available for component picking.
        const bool hitsPivot = std::abs( screen.x() - centre.x() ) <= 7.0 && std::abs( screen.y() - centre.y() ) <= 7.0;
        return hitsPivot || std::abs( toCentre - kScaleGizmoRadiusPixels ) <= 7.0 ? -1 : -2;
    }
    if ( GizmoKind( ws ) == gizmo_kind_t::ROTATE ) {
        int best = -2; f64 bestDistance = 7.0;
        const f64 radius = GizmoRingRadius( view, pivot );
        for ( int axis = 0; axis < 3; ++axis ) {
            if ( !RingInView( view, pivot, axis ) ) { continue; }
            for ( int segment = 0; segment < 64; ++segment ) {
                constexpr f64 step = 2.0 * 3.14159265358979323846 / 64.0;
                QPointF a, b;
                if ( !Project( view, RingPoint( pivot, axis, radius, segment * step ), a ) ||
                     !Project( view, RingPoint( pivot, axis, radius, ( segment + 1 ) * step ), b ) ) { continue; }
                f64 t = 0.0;
                const f64 distance = SegmentDistance( screen, a, b, t );
                if ( distance < bestDistance ) { bestDistance = distance; best = axis; }
            }
        }
        return best;
    }
    if ( toCentre <= 9.0 ) { return -1; }
    const f64 length = GizmoLength( view, pivot );
    int best = -2; f64 bestDistance = 7.0;
    for ( int axis = 0; axis < 3; ++axis ) {
        if ( !AxisInView( view, axis ) ) { continue; }
        QPointF end;
        if ( !Project( view, Add( pivot, AxisVector( static_cast<u32>( axis ), length ) ), end ) ) { continue; }
        f64 t = 0.0;
        const f64 distance = SegmentDistance( screen, centre, end, t );
        if ( distance < bestDistance ) { bestDistance = distance; best = axis; }
    }
    return best;
}

// Closest parameter on the line pivot + axis * t to the ray.
f64 AxisParameter( vec3 pivot, vec3 axis, vec3 origin, vec3 direction ) noexcept
{
    const vec3 w = Sub( pivot, origin );
    const f64 b = Dot( axis, direction ), d = Dot( axis, w ), e = Dot( direction, w );
    const f64 denominator = 1.0 - b * b;
    return std::fabs( denominator ) < 1e-9 ? 0.0 : ( b * e - d ) / denominator;
}

math::affine3d_t About( vec3 pivot, const math::affine3d_t &linear ) noexcept
{
    return math::Affine3d_Multiply( math::Affine3d_FromTranslation( pivot ),
        math::Affine3d_Multiply( linear, math::Affine3d_FromTranslation( Mul( pivot, -1.0 ) ) ) );
}

math::affine3d_t RotationAbout( u32 axis, f64 radians ) noexcept
{
    const f64 c = std::cos( radians ), s = std::sin( radians );
    if ( axis == 0 ) { return math::Affine3d_FromColumns( { 1, 0, 0 }, { 0, c, s }, { 0, -s, c }, {} ); }
    if ( axis == 1 ) { return math::Affine3d_FromColumns( { c, 0, -s }, { 0, 1, 0 }, { s, 0, c }, {} ); }
    return math::Affine3d_FromColumns( { c, s, 0 }, { -s, c, 0 }, { 0, 0, 1 }, {} );
}

// The transform the current drag describes.
void GizmoUpdate( const map_workspace_t *ws, state_t &s, const map_tool_event_t &e, const gizmo_target_t &t ) noexcept
{
    // An invalid current sample must not leave an earlier transform available
    // for release to commit (for example, rotating through the pivot).
    s.gizmoAffine = math::CY_AFFINE3D_IDENTITY;
    s.gizmoDelta = {};
    const auto kind = GizmoKind( ws );
    const bool bypass = SnapBypassed( e.modifiers );
    if ( kind == gizmo_kind_t::TRANSLATE ) {
        vec3 delta{};
        if ( e.view.bOrtho ) {
            delta = Sub( e.rayOrigin, s.gizmoPressRayOrigin );
            SetComp( delta, DepthAxis( e.view.axisU, e.view.axisV ), 0.0 );
            if ( s.gizmoAxis >= 0 ) { const f64 keep = Comp( delta, static_cast<u32>( s.gizmoAxis ) ); delta = AxisVector( static_cast<u32>( s.gizmoAxis ), keep ); }
        } else if ( s.gizmoAxis >= 0 ) {
            const vec3 axis = AxisVector( static_cast<u32>( s.gizmoAxis ) );
            delta = Mul( axis, AxisParameter( t.pivot, axis, e.rayOrigin, e.rayDirection ) - s.gizmoT0 );
        } else {
            // The centre moves like a component drag: on the floor plane
            // through the pivot, vertically with Alt held during the drag.
            vec3 hit{};
            if ( FreeGizmoPoint( e, s.gizmoPressHit, ( e.modifiers & Qt::AltModifier ) != 0, hit ) ) { delta = Sub( hit, s.gizmoPressHit ); }
        }
        delta = SnapAll( ws, delta, e.modifiers );
        s.gizmoDelta = delta;
        s.gizmoAffine = math::Affine3d_FromTranslation( delta );
        return;
    }
    QPointF centre;
    if ( !Project( e.view, t.pivot, centre ) ) { return; }
    if ( kind == gizmo_kind_t::ROTATE ) {
        if ( s.gizmoAxis < 0 ) { return; }
        const u32 axis = static_cast<u32>( s.gizmoAxis );
        const vec3 normal = AxisVector( axis );
        vec3 p0{}, p1{};
        const math::planed_t plane{ normal, -Dot( normal, t.pivot ) };
        if ( !RayPlane( s.gizmoPressRayOrigin, s.gizmoPressRayDirection, plane, p0 ) ||
             !RayPlane( e.rayOrigin, e.rayDirection, plane, p1 ) ) { return; }
        const vec3 r0 = Sub( p0, t.pivot ), r1 = Sub( p1, t.pivot );
        if ( Length( r0 ) <= 1e-9 || Length( r1 ) <= 1e-9 ) { return; }
        f64 degrees = std::atan2( Dot( normal, Cross( r0, r1 ) ), Dot( r0, r1 ) ) * 180.0 / 3.14159265358979323846;
        if ( !bypass && ws->angleSnap > 0.0 ) { degrees = std::round( degrees / ws->angleSnap ) * ws->angleSnap; }
        s.gizmoAffine = About( t.pivot, RotationAbout( axis, degrees * 3.14159265358979323846 / 180.0 ) );
        return;
    }
    // A click (or tiny pointer jitter) must remain identity even when the
    // scale increment does not divide 1, for example a 0.3 snap step.
    if ( QLineF( s.gizmoPress, e.screen ).length() < 3.0 ) { return; }
    if ( s.gizmoAxis >= 0 ) {
        // The reference is fixed at pickup, independent of selection extent
        // and distance from a custom pivot. Flat edges and one vertex remain
        // usable; no division by a cursor radius or near-zero component span.
        vec3 point{};
        if ( !ScaleAxisPoint( e, s.gizmoScalePlane, point ) || s.gizmoScaleReference <= 0.0 ) { return; }
        const u32 axis = static_cast<u32>( s.gizmoAxis );
        f64 factor = 1.0 + Comp( Sub( point, s.gizmoPressHit ), axis ) / s.gizmoScaleReference;
        if ( !std::isfinite( factor ) ) { return; }
        if ( !bypass && ws->scaleSnap > 0.0 ) { factor = std::round( factor / ws->scaleSnap ) * ws->scaleSnap; }
        if ( !std::isfinite( factor ) ) { return; }
        vec3 scale{ 1.0, 1.0, 1.0 }; SetComp( scale, axis, std::max( factor, 0.01 ) );
        s.gizmoAffine = About( t.pivot, math::Affine3d_FromScale( scale ) );
        return;
    }
    const f64 r0 = std::hypot( s.gizmoPress.x() - centre.x(), s.gizmoPress.y() - centre.y() );
    const f64 r1 = std::hypot( e.screen.x() - centre.x(), e.screen.y() - centre.y() );
    f64 factor = r0 > 1.0 ? r1 / r0 : 1.0;
    if ( !bypass && ws->scaleSnap > 0.0 ) { factor = std::max( ws->scaleSnap, std::round( factor / ws->scaleSnap ) * ws->scaleSnap ); }
    factor = std::max( factor, 0.01 );
    vec3 scale{ factor, factor, factor };
    if ( e.view.bOrtho ) { SetComp( scale, DepthAxis( e.view.axisU, e.view.axisV ), 1.0 ); }
    s.gizmoAffine = About( t.pivot, math::Affine3d_FromScale( scale ) );
}

// One undo step: the transform, or with extrude a face extrusion or an
// edge extension by the selected transform.
bool CommitComponents( map_workspace_t *ws, gizmo_kind_t kind, const gizmo_target_t &t, const math::affine3d_t &affine, vec3 delta, bool extrude ) noexcept
{
    extrude = extrude && ( !t.edges.empty() || ( kind == gizmo_kind_t::TRANSLATE && t.face != 0 ) );
    if ( math::Affine3d_NearlyEquals( affine, math::CY_AFFINE3D_IDENTITY, 1e-12, 0.0 ) && !( extrude && Length( delta ) > 1e-9 ) ) { return false; }
    try { ( void )State( ws ); } catch ( ... ) { return false; }
    const char *label = extrude && !t.edges.empty() ? "Extend Edges" : extrude && t.face != 0 ? "Extrude Face" :
                        kind == gizmo_kind_t::ROTATE ? "Rotate Components" : kind == gizmo_kind_t::SCALE ? "Scale Components" : "Move Components";
    // Prepare selection storage before publishing geometry, so an allocation
    // failure cannot leave a successful extension without its next edge.
    geo::mesh_selection_t next{};
    const bool restore = t.mesh != 0 && !t.bFaces;
    if ( restore && geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { t.mesh } ) != geo::geometry_status_t::OK ) { return false; }
    const map_prepared_component_selection_t prepared{ restore ? &next : nullptr, extrude ? geo::mesh_edge_ref_t{} : ws->selectedMeshEdgeSeed,
        restore && t.edges.empty() && ws->meshVertexColumnRevision == ws->selection.revision ? ws->meshVertexColumnAxis : -1 };
    const auto keep = [&]( vector_t<u64> *out ) { return Vector_PushBack( out, t.mesh != 0 ? t.mesh : t.brush ) != CY_FALSE; };
    const u64 editSerial = ws->lastEdit.serial;
    const bool done = Apply( ws, label, [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        if ( !keep( out ) ) { return map_status_t::OUT_OF_MEMORY; }
        if ( t.brush != 0 ) { return MapTopologyEdit_MoveBrushFace( copy, t.brush, t.brushSide, delta ); }
        if ( extrude && t.face != 0 ) { return MapTopologyEdit_ExtrudeFaceBy( copy, t.mesh, t.face, delta ); }
        if ( extrude && !t.edges.empty() ) {
            return MapTopologyEdit_ExtendEdgesTransformed( copy, t.mesh, { t.edges.data(), t.edges.size() }, affine, &next.edges );
        }
        if ( restore ) {
            if ( t.edges.empty() ) {
                for ( const auto id : t.vertices ) {
                    if ( geo::MeshSelection_TryAddVertex( &next, id ) != geo::geometry_status_t::OK ) { return map_status_t::OUT_OF_MEMORY; }
                }
            } else {
                for ( const auto edge : t.edges ) {
                    if ( geo::MeshSelection_TryAddEdge( &next, edge ) != geo::geometry_status_t::OK ) { return map_status_t::OUT_OF_MEMORY; }
                }
            }
        }
        return MapTopologyEdit_TransformVertices( copy, t.mesh, { t.vertices.data(), t.vertices.size() }, affine );
    }, restore ? &prepared : nullptr );
    geo::MeshSelection_Shutdown( &next );
    if ( done ) {
        RecordModellingRepeat( ws, static_cast<map_component_transform_t>( kind ), t.mesh != 0 ? t.mesh : t.brush,
            t.brush != 0, affine, delta, extrude );
    }
    if ( !done && extrude && !t.edges.empty() ) {
        MapNotice_Post( ws, map_notice_t::WARNING, ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ?
            QStringLiteral( "Not enough memory to extend this edge. The map has not changed." ) :
            QStringLiteral( "Extend needs an open boundary edge and a non-collapsed new face. Use a Quad or remove a face to open a closed mesh." ) );
    } else if ( !done && kind == gizmo_kind_t::SCALE && !extrude && ws->lastEdit.serial != editSerial &&
                ws->lastEdit.outcome == map_edit_outcome_t::FAILED &&
                ws->lastEdit.status != map_status_t::MODIFIER_TRANSFORM_UNSUPPORTED &&
                ws->lastEdit.status != map_status_t::PREFAB_TRANSFORM_UNSUPPORTED ) {
        const bool memory = ws->lastEdit.status == map_status_t::OUT_OF_MEMORY;
        MapNotice_Post( ws, memory ? map_notice_t::ERROR : map_notice_t::WARNING, memory ?
            QStringLiteral( "Not enough memory to scale the selected components. No edit was published." ) :
            QStringLiteral( "The selected components could not be scaled. No edit was published." ) );
    }

    return done;
}

bool GizmoCommit( map_workspace_t *ws, const state_t &s, const map_tool_event_t &e, const gizmo_target_t &t ) noexcept
{
    ( void )e;
    return CommitComponents( ws, GizmoKind( ws ), t, s.gizmoAffine, s.gizmoDelta, s.bGizmoExtrude );
}

bool GizmoEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    if ( s.bGizmo && !GizmoContextMatches( ws, s ) ) {
        ResetGizmo( s );
        return false;
    }
    if ( s.bGizmo && e.kind != map_tool_event_kind_t::CANCEL && e.view.pView != s.gizmoView ) { return false; }
    gizmo_target_t t{};
    if ( !GizmoTargetFor( ws, s, t ) ) { s.bGizmo = false; return false; }
    if ( !s.bGizmo && ws->tool == map_tool_t::SELECT &&
         ( e.kind == map_tool_event_kind_t::PRESS || e.kind == map_tool_event_kind_t::MOVE ) ) {
        // A shaft can lie along the selected edge itself. Select must keep
        // direct edge pulls usable there; explicit Translate prioritises axes.
        edge_hit_t edge{};
        const auto result = NearestMeshEdge( ws, e, kEdgePixels, edge );
        if ( result == edge_result_t::FAILED ) { s.gizmoHover = -2; return true; }
        if ( result == edge_result_t::HIT && ( e.view.pfnPickEdge != nullptr ||
             !Occluded( ws, e, Add( edge.a, Mul( Sub( edge.b, edge.a ), edge.t ) ) ) ) ) {
            s.gizmoHover = -2;
            return false;
        }
    }
    switch ( e.kind ) {
        case map_tool_event_kind_t::MOVE:
            if ( s.bGizmo && e.bButtonDown ) { GizmoUpdate( ws, s, e, t ); return true; }
            s.gizmoHover = GizmoHandleAt( ws, e.view, t.pivot, e.screen );
            return false; // Hover highlighting stays the view's.
        case map_tool_event_kind_t::PRESS: {
            // In Select, the centre can coincide with an edge midpoint.
            // Modified clicks must still add/toggle that edge, not grab it.
            if ( ws->tool == map_tool_t::SELECT &&
                 ( e.modifiers & ( Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier ) ) != 0 ) { return false; }
            const int handle = GizmoHandleAt( ws, e.view, t.pivot, e.screen );
            if ( handle == -2 ) { return false; } // Elsewhere: picking as usual.
            const bool axisScale = GizmoKind( ws ) == gizmo_kind_t::SCALE && handle >= 0;
            math::planed_t scalePlane{}; vec3 scalePress{}; f64 scaleReference = 0.0;
            if ( axisScale ) {
                scale_layout_t layout{};
                if ( !ScaleLayout( e.view, t.pivot, layout ) || !layout.axes[handle].visible ) { return true; }
                const vec3 axis = AxisVector( handle );
                const vec3 normal = Sub( e.rayDirection, Mul( axis, Dot( e.rayDirection, axis ) ) );
                const f64 length = Length( normal );
                if ( !std::isfinite( length ) || length < 1e-8 ) { return true; }
                scalePlane.normal = Mul( normal, 1.0 / length ); scalePlane.d = -Dot( scalePlane.normal, t.pivot );
                if ( !ScaleAxisPoint( e, scalePlane, scalePress ) ) { return true; }
                scaleReference = layout.reference;
            }
            s.bGizmo = true; s.gizmoAxis = handle; s.gizmoPress = e.screen;
            s.gizmoView = e.view.pView; s.gizmoDocument = ws->pDocument;
            s.gizmoObject = t.mesh != 0 ? t.mesh : t.brush; s.gizmoMode = ws->elementMode;
            s.gizmoSelectionRevision = ws->selection.revision; s.gizmoGeometryRevision = ws->pDocument->geometry.revision;
            s.bGizmoExtrude = ( ( e.modifiers & Qt::ShiftModifier ) != 0 &&
                ( !t.edges.empty() || ( GizmoKind( ws ) == gizmo_kind_t::TRANSLATE && t.face != 0 ) ) ) ||
                ( !t.edges.empty() && EdgeDragExtends( ws ) );
            s.gizmoPressRayOrigin = e.rayOrigin; s.gizmoPressRayDirection = e.rayDirection;
            s.gizmoAffine = math::CY_AFFINE3D_IDENTITY; s.gizmoDelta = {};
            if ( !axisScale && !e.view.bOrtho && handle >= 0 ) {
                s.gizmoT0 = AxisParameter( t.pivot, AxisVector( static_cast<u32>( handle ) ), e.rayOrigin, e.rayDirection );
            }
            if ( axisScale ) { s.gizmoScalePlane = scalePlane; s.gizmoScaleReference = scaleReference; s.gizmoPressHit = scalePress; }
            else if ( e.view.bOrtho || !FreeGizmoPoint( e, t.pivot, false, s.gizmoPressHit ) ) { s.gizmoPressHit = t.pivot; }
            return true;
        }
        case map_tool_event_kind_t::RELEASE: {
            if ( !s.bGizmo ) { return false; }
            GizmoUpdate( ws, s, e, t );
            s.bGizmo = false;
            ( void )GizmoCommit( ws, s, e, t );
            s.gizmoAffine = math::CY_AFFINE3D_IDENTITY; s.gizmoDelta = {};
            return true;
        }
        case map_tool_event_kind_t::CANCEL:
            if ( !s.bGizmo ) { return false; }
            s.bGizmo = false; s.gizmoAffine = math::CY_AFFINE3D_IDENTITY; s.gizmoDelta = {};
            return true;
        default: return false;
    }
}

void GizmoPaint( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    gizmo_target_t t{};
    if ( !GizmoTargetFor( ws, s, t ) ) { return; }
    const auto kind = GizmoKind( ws );
    // Extension keeps the source cage fixed and joins it to the new rim.
    // Ordinary transforms deform only lines incident to selected vertices.
    if ( s.bGizmo ) {
        const auto moved = [&]( vec3 p ) { return math::Affine3d_TransformPoint( s.gizmoAffine, p ); };
        if ( t.mesh != 0 && s.bGizmoExtrude && !t.edges.empty() ) {
            const auto *mesh = geo::GeometryDocument_FindMesh( &ws->pDocument->geometry, { t.mesh } );
            for ( const auto &edge : t.edges ) {
                vec3 a{}, b{};
                if ( !VertexAt( mesh, edge.a, a ) || !VertexAt( mesh, edge.b, b ) ) { continue; }
                const vec3 nextA = moved( a ), nextB = moved( b );
                QPolygonF face;
                bool visible = true;
                for ( const vec3 p : { a, b, nextB, nextA } ) {
                    QPointF at; if ( !Project( view, p, at ) ) { visible = false; break; } face << at;
                }
                if ( visible && !view.bOrtho ) {
                    painter.setPen( Qt::NoPen ); painter.setBrush( QColor( 105, 185, 225, 32 ) );
                    painter.drawPolygon( face ); painter.setBrush( Qt::NoBrush );
                }
                DrawLine( painter, view, a, nextA, Hover() ); DrawLine( painter, view, b, nextB, Hover() );
                DrawLine( painter, view, nextA, nextB, Accent() );
            }
        } else if ( t.mesh != 0 ) {
            const auto *object = MapWireframe_FindObject( ws->wire, t.mesh );
            for ( u32 l = 0; object != nullptr && l < object->nLines; ++l ) {
                const auto &line = ws->wire.lines.pData[object->iFirstLine + l];
                const u64 a = ws->wire.pointSourceIds.pData[line.iA], b = ws->wire.pointSourceIds.pData[line.iB];
                const bool movesA = std::any_of( t.vertices.begin(), t.vertices.end(), [&]( id_t v ) { return v.value == a; } );
                const bool movesB = std::any_of( t.vertices.begin(), t.vertices.end(), [&]( id_t v ) { return v.value == b; } );
                if ( !movesA && !movesB ) { continue; }
                const vec3 pa = ws->wire.points.pData[line.iA], pb = ws->wire.points.pData[line.iB];
                DrawLine( painter, view, movesA ? moved( pa ) : pa, movesB ? moved( pb ) : pb, Accent() );
            }
        } else {
            for ( usize i = 0; i < t.positions.size(); ++i ) { DrawLine( painter, view, moved( t.positions[i] ), moved( t.positions[( i + 1 ) % t.positions.size()] ), Accent() ); }
        }
        for ( const vec3 &p : t.positions ) { DrawPoint( painter, view, moved( p ), Accent(), 3.0 ); }
    }
    const vec3 handlePivot = s.bGizmo && kind == gizmo_kind_t::TRANSLATE ? Add( t.pivot, s.gizmoDelta ) : t.pivot;
    QPointF centre;
    if ( !Project( view, handlePivot, centre ) ) { return; }
    const QColor axisColors[3]{ QColor( 230, 85, 85 ), QColor( 100, 205, 100 ), QColor( 90, 155, 245 ) };
    if ( kind == gizmo_kind_t::TRANSLATE ) {
        const f64 length = GizmoLength( view, handlePivot );
        if ( s.bGizmo && s.gizmoAxis >= 0 ) {
            const vec3 axis = AxisVector( s.gizmoAxis, length * 8.0 );
            DrawLine( painter, view, Sub( t.pivot, axis ), Add( t.pivot, axis ), axisColors[s.gizmoAxis], Qt::DashLine );
        }
        for ( int axis = 0; axis < 3; ++axis ) {
            if ( !AxisInView( view, axis ) ) { continue; }
            const QColor color = ( s.bGizmo ? s.gizmoAxis : s.gizmoHover ) == axis ? Accent() : axisColors[axis];
            QPointF end;
            if ( !Project( view, Add( handlePivot, AxisVector( static_cast<u32>( axis ), length ) ), end ) ) { continue; }
            const QLineF shaft( centre, end );
            if ( shaft.length() < 9.0 ) { continue; }
            const QPointF direction = ( end - centre ) / shaft.length(), side( -direction.y(), direction.x() );
            painter.setPen( QPen( color, 1.5 ) ); painter.drawLine( centre, end - direction * 6.0 );
            painter.setPen( Qt::NoPen ); painter.setBrush( color );
            painter.drawPolygon( QPolygonF{ end, end - direction * 9.0 + side * 3.5, end - direction * 9.0 - side * 3.5 } );
            painter.setBrush( Qt::NoBrush );
        }
        const QColor middle = ( s.bGizmo ? s.gizmoAxis : s.gizmoHover ) == -1 ? Accent() : QColor( 220, 220, 220 );
        painter.setPen( QPen( middle, 1.0 ) ); painter.setBrush( Qt::NoBrush );
        painter.drawRect( QRectF( centre.x() - 4, centre.y() - 4, 8, 8 ) );
    } else if ( kind == gizmo_kind_t::ROTATE ) {
        const f64 radius = GizmoRingRadius( view, t.pivot );
        for ( int axis = 0; axis < 3; ++axis ) {
            if ( !RingInView( view, t.pivot, axis ) ) { continue; }
            const bool active = ( s.bGizmo ? s.gizmoAxis : s.gizmoHover ) == axis;
            painter.setPen( QPen( active ? Accent() : axisColors[axis], active ? 1.75 : 1.25 ) );
            for ( int segment = 0; segment < 64; ++segment ) {
                constexpr f64 step = 2.0 * 3.14159265358979323846 / 64.0;
                QPointF a, b;
                if ( Project( view, RingPoint( t.pivot, axis, radius, segment * step ), a ) &&
                     Project( view, RingPoint( t.pivot, axis, radius, ( segment + 1 ) * step ), b ) ) { painter.drawLine( a, b ); }
            }
        }
        painter.fillRect( QRectF( centre.x() - 2, centre.y() - 2, 4, 4 ), QColor( 220, 220, 220 ) );
    } else {
        scale_layout_t layout{};
        if ( !ScaleLayout( view, t.pivot, layout ) ) { return; }
        const int active = s.bGizmo ? s.gizmoAxis : s.gizmoHover;
        for ( int axis = 0; axis < 3; ++axis ) {
            if ( !layout.axes[axis].visible ) { continue; }
            const QPointF tip = layout.axes[axis].tip;
            const QPointF direction = ( tip - centre ) / kScaleAxisRadiusPixels;
            const QColor ink = active == axis ? Accent() : axisColors[axis];
            painter.setPen( QPen( ink, active == axis ? 1.75 : 1.25 ) ); painter.setBrush( Qt::NoBrush );
            // Leave the uniform ring unobscured at each guide crossing.
            painter.drawLine( centre + direction * 12.0, centre + direction * ( kScaleGizmoRadiusPixels - 8.0 ) );
            painter.drawLine( centre + direction * ( kScaleGizmoRadiusPixels + 8.0 ), tip );
            painter.setBrush( ink );
            painter.drawRect( QRectF( tip.x() - kScaleAxisHalfPixels, tip.y() - kScaleAxisHalfPixels,
                                     kScaleAxisHalfPixels * 2.0, kScaleAxisHalfPixels * 2.0 ) );
            painter.setBrush( Qt::NoBrush );
            painter.drawText( tip + QPointF( 7, -7 ), QString( QChar( "XYZ"[axis] ) ) );
        }
        const QColor color = active == -1 ? Accent() : QColor( 230, 230, 230 );
        painter.setPen( QPen( color, 1.5, Qt::DashLine ) ); painter.setBrush( Qt::NoBrush );
        painter.drawEllipse( centre, kScaleGizmoRadiusPixels, kScaleGizmoRadiusPixels );
        painter.fillRect( QRectF( centre.x() - 3, centre.y() - 3, 6, 6 ), color );
    }
    if ( s.bGizmo && s.bGizmoExtrude ) {
        painter.setPen( Accent() ); painter.drawText( centre + QPointF( 12, 20 ), QStringLiteral( "Extend" ) );
    }

}
/*
================
Edge Arc: bend a selected open chain of edges into a circular arc
================
*/

void ResetArc( state_t &s ) noexcept
{
    s.bArc = false; s.arcView = nullptr; s.arcDocument = nullptr;
    s.arcGeometryRevision = s.arcSelectionRevision = s.arcMesh = 0;
    s.arcOrder.clear(); s.arcChain.clear(); s.arcPress = s.arcNormal = s.arcBulge = {};
}

bool ArcContextMatches( const map_workspace_t *ws, const state_t &s ) noexcept
{
    if ( !s.bArc || !Writable( ws ) || ws->tool != map_tool_t::EDGE_ARC || ws->elementMode != s.arcMode ||
         ws->pDocument != s.arcDocument || ws->pDocument->geometry.revision != s.arcGeometryRevision ||
         ws->selection.revision != s.arcSelectionRevision || ws->meshSelection.meshId.value != s.arcMesh ||
         MapLayers_IsObjectLocked( ws, s.arcMesh ) ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, s.arcMesh );
    return object != nullptr && object->kind == map_wire_kind_t::MESH && MapWorkspace_IsVisible( ws, *object );
}

// The selected edges as one open chain, ends first to last; false for a
// loop, a branch or more than one piece.
bool ArcChain( const map_workspace_t *ws, std::vector<id_t> &order, std::vector<vec3> &positions )
{
    order.clear(); positions.clear();
    if ( !Writable( ws ) || !MapWorkspace_HasMeshEdges( ws ) ) { return false; }
    const auto &edges = ws->meshSelection.edges;
    std::vector<std::pair<u64, u64>> links;
    for ( usize i = 0; i < edges.nCount; ++i ) { links.push_back( { edges.pData[i].a.value, edges.pData[i].b.value } ); }
    const auto degree = [&]( u64 v ) { usize n = 0; for ( const auto &l : links ) { n += ( l.first == v ) + ( l.second == v ); } return n; };
    u64 start = 0;
    for ( const auto &l : links ) {
        for ( u64 v : { l.first, l.second } ) {
            const usize d = degree( v );
            if ( d > 2 ) { return false; }
            if ( d == 1 && ( start == 0 || v < start ) ) { start = v; }
        }
    }
    if ( start == 0 ) { return false; } // A closed loop has no ends.
    std::vector<bool> used( links.size(), false );
    order.push_back( { start } );
    for ( usize step = 0; step < links.size(); ++step ) {
        const u64 at = order.back().value;
        bool advanced = false;
        for ( usize i = 0; i < links.size() && !advanced; ++i ) {
            if ( used[i] || ( links[i].first != at && links[i].second != at ) ) { continue; }
            used[i] = true; advanced = true;
            order.push_back( { links[i].first == at ? links[i].second : links[i].first } );
        }
        if ( !advanced ) { return false; }
    }
    if ( order.size() < 3 ) { return false; } // One edge has nothing to bend.
    const auto *mesh = geo::GeometryDocument_FindMesh( &ws->pDocument->geometry, ws->meshSelection.meshId );
    for ( const id_t &id : order ) { vec3 p{}; if ( !VertexAt( mesh, id, p ) ) { return false; } positions.push_back( p ); }
    return true;
}

// Where each chain vertex goes for a bulge vector (perpendicular to the
// chord, its length the arc's height): spaced along the arc as they were
// along the chain.
std::vector<vec3> ArcPositions( const std::vector<vec3> &chain, vec3 bulge )
{
    std::vector<vec3> out = chain;
    const vec3 a = chain.front(), b = chain.back();
    const vec3 chord = Sub( b, a );
    const f64 length = Length( chord ), h = Length( bulge );
    if ( length <= 1e-9 ) { return out; }
    std::vector<f64> along( chain.size(), 0.0 );
    for ( usize i = 1; i < chain.size(); ++i ) { along[i] = along[i - 1] + Length( Sub( chain[i], chain[i - 1] ) ); }
    const vec3 u = Mul( chord, 1.0 / length ), middle = Mul( Add( a, b ), 0.5 );
    for ( usize i = 1; i + 1 < chain.size(); ++i ) {
        const f64 f = along.back() > 0.0 ? along[i] / along.back() : static_cast<f64>( i ) / static_cast<f64>( chain.size() - 1 );
        if ( h <= 1e-9 ) { out[i] = Add( a, Mul( chord, f ) ); continue; } // Flat: straight along the chord.
        const vec3 n = Mul( bulge, 1.0 / h );
        const f64 radius = ( length * length * 0.25 + h * h ) / ( 2.0 * h );
        const f64 half = h <= radius ? std::asin( std::min( 1.0, length * 0.5 / radius ) ) : 3.14159265358979323846 - std::asin( std::min( 1.0, length * 0.5 / radius ) );
        const vec3 centre = Add( middle, Mul( n, h - radius ) );
        const f64 angle = -half + 2.0 * half * f;
        out[i] = Add( centre, Mul( Add( Mul( u, std::sin( angle ) ), Mul( n, std::cos( angle ) ) ), radius ) );
    }
    return out;
}

bool ArcEventImpl( map_workspace_t *ws, state_t &s, const map_tool_event_t &e )
{
    if ( s.bArc && e.kind != map_tool_event_kind_t::CANCEL && e.view.pView != s.arcView ) { return true; }
    if ( s.bArc && !ArcContextMatches( ws, s ) ) { ResetArc( s ); return true; }
    switch ( e.kind ) {
        case map_tool_event_kind_t::PRESS: {
            if ( s.bArc ) { return true; }
            if ( !Writable( ws ) ) { return true; }
            std::vector<id_t> order;
            std::vector<vec3> chain;
            // Picking happens in the tool: a click on an edge outside the
            // chain picks it, Shift+click adds one; a drag elsewhere bends.
            edge_hit_t edge{};
            const auto edgeResult = NearestMeshEdge( ws, e, kPickPixels, edge );
            if ( edgeResult == edge_result_t::FAILED ) { return true; }
            const bool onEdge = edgeResult == edge_result_t::HIT;
            if ( ( onEdge && MapLayers_IsObjectLocked( ws, edge.mesh ) ) ||
                 ( !onEdge && MapLayers_IsObjectLocked( ws, ws->meshSelection.meshId.value ) ) ) {
                Hint( ws, QStringLiteral( "Edge Arc: unlock the selected mesh's layer before bending it" ) ); return true;
            }
            const bool add = ( e.modifiers & Qt::ShiftModifier ) != 0;
            const bool chained = ArcChain( ws, order, chain );
            bool picked = false;
            for ( usize i = 0; onEdge && ws->meshSelection.meshId.value == edge.mesh && i < ws->meshSelection.edges.nCount; ++i ) {
                const auto &selected = ws->meshSelection.edges.pData[i];
                picked |= ( selected.a.value == edge.edge.a.value && selected.b.value == edge.edge.b.value ) ||
                          ( selected.a.value == edge.edge.b.value && selected.b.value == edge.edge.a.value );
            }
            if ( onEdge && ( add || !picked ) ) {
                if ( ws->elementMode != map_element_mode_t::EDGES ) { MapWorkspace_SetElementMode( ws, map_element_mode_t::EDGES ); }
                if ( MapWorkspace_SelectMeshEdge( ws, edge.mesh, edge.edge, add ? MAP_SELECT_ADD : MAP_SELECT_REPLACE ) ) {
                    const usize count = ws->meshSelection.edges.nCount;
                    MapNotice_Post( ws, map_notice_t::INFO, count < 2 ? QStringLiteral( "Edge Arc: Shift+click the next edges of the chain, then drag to bend it" )
                                                                      : QStringLiteral( "Edge Arc: %1 edges picked; drag to bend them (Shift+click adds more)" ).arg( count ) );
                }
                return true;
            }
            if ( !chained ) {
                // One edge has no corner between its ends to move.
                Hint( ws, ws->meshSelection.edges.nCount == 1u
                    ? QStringLiteral( "Edge Arc: one edge has no corner to bend; Shift+click the next edge along the chain (or Subdivide this edge first), then drag" )
                    : QStringLiteral( "Edge Arc: click an edge of a mesh, Shift+click the next ones along a chain, then drag to bend it" ) );
                return true;
            }
            const vec3 middle = Mul( Add( chain.front(), chain.back() ), 0.5 );
            s.arcNormal = Mul( e.rayDirection, -1.0 );
            if ( !RayPlane( e.rayOrigin, e.rayDirection, { s.arcNormal, -Dot( s.arcNormal, middle ) }, s.arcPress ) ) { return true; }
            s.arcOrder = std::move( order ); s.arcChain = std::move( chain );
            s.arcView = e.view.pView; s.arcDocument = ws->pDocument; s.arcMesh = ws->meshSelection.meshId.value;
            s.arcGeometryRevision = ws->pDocument->geometry.revision; s.arcSelectionRevision = ws->selection.revision; s.arcMode = ws->elementMode;
            s.bArc = true; s.arcBulge = {};
            return true;
        }
        case map_tool_event_kind_t::MOVE:
        case map_tool_event_kind_t::RELEASE: {
            if ( !s.bArc ) { return false; }
            if ( e.kind == map_tool_event_kind_t::MOVE && !e.bButtonDown ) { ResetArc( s ); return true; }
            const auto &chain = s.arcChain;
            const vec3 middle = Mul( Add( chain.front(), chain.back() ), 0.5 );
            vec3 hit{};
            // An invalid final ray must not commit the last valid preview.
            s.arcBulge = {};
            if ( !RayPlane( e.rayOrigin, e.rayDirection, { s.arcNormal, -Dot( s.arcNormal, middle ) }, hit ) ) {
                if ( e.kind == map_tool_event_kind_t::RELEASE ) { ResetArc( s ); }
                return true;
            }
            {
                // Only the part of the drag across the chord bends it.
                const vec3 chord = Sub( chain.back(), chain.front() );
                const f64 length = Length( chord );
                vec3 drag = Sub( hit, s.arcPress );
                if ( length > 1e-9 ) { const vec3 u = Mul( chord, 1.0 / length ); drag = Sub( drag, Mul( u, Dot( drag, u ) ) ); }
                const f64 h = Length( drag );
                const f64 snapped = Snap( ws, h, e.modifiers );
                s.arcBulge = h > 1e-9 ? Mul( drag, snapped / h ) : vec3{};
            }
            if ( e.kind == map_tool_event_kind_t::MOVE ) { return true; }
            if ( Length( s.arcBulge ) <= 1e-9 ) { ResetArc( s ); return true; }
            const auto positions = ArcPositions( chain, s.arcBulge );
            const u64 mesh = s.arcMesh;
            const std::vector<id_t> inner( s.arcOrder.begin() + 1, s.arcOrder.end() - 1 );
            const std::vector<vec3> moved( positions.begin() + 1, positions.end() - 1 );
            // Retire before publication synchronously calls workspace listeners.
            ResetArc( s );
            const bool done = Apply( ws, "Edge Arc", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
                return Vector_PushBack( out, mesh ) ? MapTopologyEdit_SetVertexPositions( copy, mesh, { inner.data(), inner.size() }, { moved.data(), moved.size() } )
                                                   : map_status_t::OUT_OF_MEMORY;
            } );
            if ( !done && ws->lastEdit.outcome != map_edit_outcome_t::UNCHANGED ) {
                MapNotice_Post( ws, ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ? map_notice_t::ERROR : map_notice_t::WARNING,
                    ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ? QStringLiteral( "Edge Arc: not enough memory; the map has not changed" ) :
                    QStringLiteral( "Edge Arc: that bend would take faces out of flat; bend edges within their faces' plane (a border of a flat sheet, in a 2D view)" ) );
            }
            return true;
        }
        case map_tool_event_kind_t::CANCEL: {
            const bool had = s.bArc; ResetArc( s ); return had;
        }
        default: return false;
    }
}

bool ArcEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    try { return ArcEventImpl( ws, s, e ); }
    catch ( ... ) { ResetArc( s ); MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Edge Arc: not enough memory; the map has not changed" ) ); return true; }
}

void ArcPaint( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    try {
        std::vector<id_t> order;
        std::vector<vec3> chain;
        if ( s.bArc ) {
            if ( !ArcContextMatches( ws, s ) ) { return; }
            chain = s.arcChain;
        } else if ( !ArcChain( ws, order, chain ) ) { return; }
        const auto shown = s.bArc ? ArcPositions( chain, s.arcBulge ) : chain;
        for ( usize i = 0; i + 1 < shown.size(); ++i ) { DrawLine( painter, view, shown[i], shown[i + 1], Accent(), s.bArc ? Qt::SolidLine : Qt::DashLine ); }
        DrawPoint( painter, view, shown.front(), Accent() ); DrawPoint( painter, view, shown.back(), Accent() );
        if ( s.bArc ) { DrawLabel( painter, view, Mul( Add( shown.front(), shown.back() ), 0.5 ), QString::number( Length( s.arcBulge ), 'f', 1 ), Accent() ); }
    } catch ( ... ) {} // An unavailable overlay never publishes geometry.
}

} // namespace

bool MapToolGestures_PlacePoint( const map_workspace_t *ws, const map_tool_event_t &e, math::vec3d_t *pPointOut ) noexcept
{
    return ws != nullptr && pPointOut != nullptr && ws->pDocument != nullptr && PlacePoint( ws, e, *pPointOut );
}

bool MapToolGestures_SurfacePoint( const map_workspace_t *ws, const map_tool_event_t &e, math::vec3d_t *pPointOut, math::vec3d_t *pNormalOut ) noexcept
{
    surface_hit_t hit{};
    if ( ws == nullptr || ws->pDocument == nullptr || e.view.bOrtho || !SurfaceHit( ws, e, hit ) ) { return false; }
    if ( pPointOut != nullptr ) { *pPointOut = hit.point; }
    if ( pNormalOut != nullptr ) { *pNormalOut = hit.normal; }
    return true;
}

void MapToolGestures_SetPivot( map_workspace_t *ws, math::vec3d_t point ) noexcept
{
    if ( ws == nullptr ) { return; }
    auto &s = g_states[ws];
    // A captured rotation was measured from the old pivot's plane. Do not
    // apply its press rays to a different centre on release.
    s.bGizmo = false; s.gizmoAffine = math::CY_AFFINE3D_IDENTITY; s.gizmoDelta = {};
    s.bPivot = true; s.pivot = point;
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
}

bool MapToolGestures_CanTransformComponents( const map_workspace_t *ws, map_component_transform_t kind ) noexcept
{
    try {
        gizmo_target_t t{};
        return ws != nullptr && ws->pDocument != nullptr && ws->pGui != nullptr && ComponentTarget( ws, static_cast<gizmo_kind_t>( kind ), t );
    } catch ( ... ) { return false; }
}

bool MapToolGestures_TransformComponents( map_workspace_t *ws, map_component_transform_t kind, math::vec3d_t values, map_component_pivot_t where ) noexcept
{
    try {
        gizmo_target_t t{};
        const auto gizmo = static_cast<gizmo_kind_t>( kind );
        if ( !MapToolGestures_CanTransformComponents( ws, kind ) || !std::isfinite( values.x ) || !std::isfinite( values.y ) || !std::isfinite( values.z ) ||
             !ComponentTarget( ws, gizmo, t ) ) { return false; }
        vec3 pivot = where == map_component_pivot_t::WORLD_ORIGIN ? vec3{} : t.pivot;
        if ( const auto found = g_states.find( ws ); where == map_component_pivot_t::PIVOT_POINT && found != g_states.end() && found->second.bPivot ) { pivot = found->second.pivot; }
        math::affine3d_t affine = math::CY_AFFINE3D_IDENTITY;
        if ( gizmo == gizmo_kind_t::TRANSLATE ) {
            affine = math::Affine3d_FromTranslation( values );
        } else if ( gizmo == gizmo_kind_t::ROTATE ) {
            // Z * Y * X, as the whole-object Rotate panel (MapEdit_Rotate).
            constexpr f64 kRadians = 3.14159265358979323846 / 180.0;
            const auto rotation = math::Affine3d_Multiply( RotationAbout( 2, values.z * kRadians ),
                math::Affine3d_Multiply( RotationAbout( 1, values.y * kRadians ), RotationAbout( 0, values.x * kRadians ) ) );
            affine = About( pivot, rotation );
        } else {
            if ( values.x <= 0.0 || values.y <= 0.0 || values.z <= 0.0 ) { return false; }
            affine = About( pivot, math::Affine3d_FromScale( values ) );
        }
        return CommitComponents( ws, gizmo, t, affine, values, false );
    } catch ( ... ) { return false; }
}

bool MapToolGestures_HasModellingRepeat( const map_workspace_t *ws ) noexcept
{
    const auto found = g_states.find( ws );
    return found != g_states.end() && found->second.repeat.present;
}

void MapToolGestures_SupersedeModellingRepeat( const map_workspace_t *ws ) noexcept
{
    const auto found = g_states.find( ws );
    if ( found != g_states.end() ) { found->second.repeat = {}; }
}

void MapToolGestures_HistoryWillRestore( map_workspace_t *ws, const map_document_t *preparedDocument ) noexcept
{
    const auto found = g_states.find( ws );
    if ( found == g_states.end() || !found->second.repeat.present || preparedDocument == nullptr ) { return; }
    auto &recipe = found->second.repeat;
    recipe.document = preparedDocument; recipe.geometryRevision = preparedDocument->geometry.revision; recipe.current = true;
}

bool MapToolGestures_RepeatModelling( map_workspace_t *ws ) noexcept
{
    try {
        const auto found = g_states.find( ws );
        if ( found == g_states.end() || !found->second.repeat.present ) { return false; }
        const modelling_repeat_t recipe = found->second.repeat;
        const auto *object = ws != nullptr ? MapWireframe_FindObject( ws->wire, recipe.object ) : nullptr;
        if ( !recipe.current || !Writable( ws ) || recipe.document != ws->pDocument ||
             recipe.geometryRevision != ws->pDocument->geometry.revision || recipe.mode != ws->elementMode ||
             MapLayers_IsObjectLocked( ws, recipe.object ) || ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ) ) {
            Hint( ws, QStringLiteral( "Repeat needs the current editable component selection from this modelling operation." ) ); return false;
        }
        // Repeating during an unfinished gesture must not commit its preview
        // or reinterpret its pending source selection.
        const auto &state = State( ws );
        if ( state.bGrabPending || state.bComponentDrag || state.bGizmo || state.bExtrude || state.paintObject != 0 ||
             state.sculptWork || state.knifeMesh != 0 || state.bArc || state.bDrag || state.bLasso || state.bVertex ||
             state.bTerrain || state.bShear || state.bClipLine || state.bClipDraw || state.bClipPoint || !state.points.empty() ) { return false; }
        gizmo_target_t target{};
        const auto kind = static_cast<gizmo_kind_t>( recipe.kind );
        if ( !ComponentTarget( ws, kind, target ) || ( recipe.brush ? target.brush : target.mesh ) != recipe.object ) {
            Hint( ws, QStringLiteral( "Repeat needs the same component mode and mesh. Select its current rim or components first." ) ); return false;
        }
        return CommitComponents( ws, kind, target, recipe.affine, recipe.delta, recipe.extrude );
    } catch ( ... ) {
        Hint( ws, QStringLiteral( "Not enough memory to repeat this modelling operation. The map has not changed." ) );
        return false;
    }
}

/*
================
Shear (TrenchBroom's Shear tool): drag a side of the selection's bounds
along itself; the opposite side stays and everything between leans.
================
*/

bool CanShear( const map_workspace_t *ws ) noexcept
{
    return Writable( ws ) && ( ws->elementMode == map_element_mode_t::OBJECTS || ws->elementMode == map_element_mode_t::GROUPS ||
        ws->elementMode == map_element_mode_t::MESHES ) && MapWorkspace_CanTurnSelection( ws );
}

void ResetShear( state_t &s ) noexcept
{
    s.bShear = s.bShearHover = false; s.shearView = nullptr; s.shearDocument = nullptr;
    s.shearGeometryRevision = s.shearSelectionRevision = 0;
    s.shearRoots.clear(); s.shearAffected.clear();
    s.shearBounds = {}; s.shearFrom = s.shearSlide = {};
}

// Notification-time validation is allocation-free: the exact hierarchy
// closure was prepared once at pickup. Hidden children still follow a visible
// parent, but locking any affected child cancels the whole transform.
bool ShearContextMatches( const map_workspace_t *ws, const state_t &s ) noexcept
{
    if ( !s.bShear || !Writable( ws ) || ws->tool != map_tool_t::SHEAR || ws->elementMode != s.shearMode ||
         ws->pDocument != s.shearDocument || ws->pDocument->geometry.revision != s.shearGeometryRevision ||
         ws->selection.revision != s.shearSelectionRevision || ws->selection.ids.nCount != s.shearRoots.size() ) { return false; }
    for ( usize i = 0; i < s.shearRoots.size(); ++i ) {
        if ( ws->selection.ids.pData[i] != s.shearRoots[i] ) { return false; }
        const auto *object = MapWireframe_FindObject( ws->wire, s.shearRoots[i] );
        if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ) { return false; }
    }
    for ( const u64 id : s.shearAffected ) { if ( MapLayers_IsObjectLocked( ws, id ) ) { return false; } }
    return true;
}

map_bounds_t ShearBox( const map_workspace_t *ws ) noexcept
{
    map_bounds_t bounds{};
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        if ( const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] ) ) { MapBounds_AddBounds( bounds, object->bounds ); }
    }
    return bounds;
}

// The side of `box` under the cursor: in 2D within a few pixels of one of
// the outline's four edges (and not past its ends); in 3D the face the ray
// enters through.
bool ShearSide( const map_bounds_t &box, const map_tool_event_t &e, u32 &axis, int &sign ) noexcept
{
    if ( !box.bHas ) { return false; }
    const vec3 &lo = box.box.minimum, &hi = box.box.maximum;
    if ( e.view.bOrtho ) {
        const u32 u = e.view.axisU, v = e.view.axisV;
        const f64 reach = 6.0 / std::max( e.view.pixelsPerUnit, 1e-9 );
        f64 best = reach;
        bool found = false;
        // The edges at u = lo/hi run along v, and the other way round.
        for ( u32 k = 0; k < 2; ++k ) {
            const u32 across = k == 0 ? u : v, along = k == 0 ? v : u;
            const f64 c = Comp( e.rayOrigin, across ), a = Comp( e.rayOrigin, along );
            if ( a < Comp( lo, along ) - reach || a > Comp( hi, along ) + reach ) { continue; }
            for ( int side = -1; side <= 1; side += 2 ) {
                const f64 d = std::fabs( c - ( side < 0 ? Comp( lo, across ) : Comp( hi, across ) ) );
                if ( d <= best ) { best = d; axis = across; sign = side; found = true; }
            }
        }
        return found;
    }
    // Slab test that remembers the entry face.
    f64 near = 0.0, far = 1e300;
    int enter = -1, enterSign = 1;
    for ( u32 a = 0; a < 3; ++a ) {
        const f64 o = Comp( e.rayOrigin, a ), d = Comp( e.rayDirection, a );
        if ( std::fabs( d ) < 1e-12 ) {
            if ( o < Comp( lo, a ) || o > Comp( hi, a ) ) { return false; }
            continue;
        }
        f64 t0 = ( Comp( lo, a ) - o ) / d, t1 = ( Comp( hi, a ) - o ) / d;
        int s0 = -1;
        if ( t0 > t1 ) { std::swap( t0, t1 ); s0 = 1; }
        if ( t0 > near ) { near = t0; enter = static_cast<int>( a ); enterSign = s0; }
        far = std::min( far, t1 );
        if ( near > far ) { return false; }
    }
    if ( enter < 0 ) { return false; } // The eye is inside the box.
    axis = static_cast<u32>( enter );
    sign = enterSign;
    return true;
}

// p' = p + perUnit * (p[axis] - pivot[axis]): the grabbed side moves by the
// slide, the opposite side (through the pivot) stays.
struct shear_map_t {
    u32 axis{ 0 };
    vec3 perUnit{}, pivot{};
};

shear_map_t ShearMap( const state_t &s ) noexcept
{
    const auto &box = s.shearBounds.box;
    const f64 lo = Comp( box.minimum, s.shearAxis ), hi = Comp( box.maximum, s.shearAxis );
    const f64 extent = s.shearSign > 0 ? hi - lo : lo - hi;
    shear_map_t t{ s.shearAxis, extent != 0.0 ? Mul( s.shearSlide, 1.0 / extent ) : vec3{}, MapBounds_Center( s.shearBounds ) };
    SetComp( t.perUnit, s.shearAxis, 0.0 );
    SetComp( t.pivot, s.shearAxis, s.shearSign > 0 ? lo : hi );
    return t;
}

vec3 Sheared( const shear_map_t &t, vec3 p ) noexcept { return Add( p, Mul( t.perUnit, Comp( p, t.axis ) - Comp( t.pivot, t.axis ) ) ); }

// The cursor on the grabbed side's plane (2D: on the pane's plane).
bool ShearPoint( const state_t &s, const map_tool_event_t &e, vec3 &out ) noexcept
{
    if ( e.view.bOrtho ) { out = OrthoPoint( e, 0.0 ); return true; }
    const f64 c = s.shearSign > 0 ? Comp( s.shearBounds.box.maximum, s.shearAxis ) : Comp( s.shearBounds.box.minimum, s.shearAxis );
    vec3 normal{};
    SetComp( normal, s.shearAxis, 1.0 );
    return RayPlane( e.rayOrigin, e.rayDirection, { normal, -c }, out );
}

// Both MOVE and RELEASE sample the same captured side plane. A bad
// sample clears the preview, so release cannot reuse an earlier displacement.
bool UpdateShear( const map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    s.shearSlide = {};
    vec3 p{};
    if ( !ShearPoint( s, e, p ) || !std::isfinite( p.x ) || !std::isfinite( p.y ) || !std::isfinite( p.z ) ) { return false; }
    vec3 delta = Sub( p, s.shearFrom );
    SetComp( delta, s.shearAxis, 0.0 );
    // Alt can be pressed after pickup to slide vertically; Alt at pickup
    // belongs to camera navigation, so this constraint remains live.
    if ( !e.view.bOrtho && s.shearAxis != 2u ) {
        if ( ( e.modifiers & Qt::AltModifier ) != 0 ) { delta = { 0.0, 0.0, delta.z }; }
        else { delta.z = 0.0; }
    }
    s.shearSlide = SnapAll( ws, delta, e.modifiers );
    if ( !std::isfinite( s.shearSlide.x ) || !std::isfinite( s.shearSlide.y ) || !std::isfinite( s.shearSlide.z ) ) {
        s.shearSlide = {}; return false;
    }
    return true;
}

bool ShearEventImpl( map_workspace_t *ws, state_t &s, const map_tool_event_t &e )
{
    if ( s.bShear && e.kind != map_tool_event_kind_t::CANCEL && e.view.pView != s.shearView ) { return true; }
    if ( s.bShear && !ShearContextMatches( ws, s ) ) { ResetShear( s ); return true; }
    switch ( e.kind ) {
        case map_tool_event_kind_t::PRESS: {
            if ( s.bShear ) { return true; }
            const map_bounds_t box = ShearBox( ws );
            u32 axis = 0;
            int sign = 1;
            if ( !ShearSide( box, e, axis, sign ) ) {
                if ( ws->selection.ids.nCount == 0 ) { Hint( ws, QStringLiteral( "Shear: select something, then drag a side of its bounds" ) ); }
                return false; // Elsewhere a click selects.
            }
            if ( !CanShear( ws ) ) {
                Hint( ws, QStringLiteral( "Shear: select unlocked brushes, meshes, patches or entities in Objects, Groups or Meshes mode" ) );
                return true;
            }
            if ( !( Comp( box.box.maximum, axis ) - Comp( box.box.minimum, axis ) > 1e-9 ) ) {
                Hint( ws, QStringLiteral( "Shear: the selection is flat across that side; grab another" ) );
                return true;
            }
            std::vector<u64> roots( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
            vector_t<u64> affected{};
            const auto status = Vector_Init( &affected, ws->pGui->pAllocator ) ?
                MapEdit_CollectAffectedObjects( ws->pDocument, { roots.data(), roots.size() }, &affected ) : map_status_t::OUT_OF_MEMORY;
            if ( status != map_status_t::OK ) {
                MapNotice_Post( ws, status == map_status_t::OUT_OF_MEMORY ? map_notice_t::ERROR : map_notice_t::WARNING,
                    status == map_status_t::OUT_OF_MEMORY ? QStringLiteral( "Shear: not enough memory; the map has not changed" ) :
                    QStringLiteral( "Shear: the selected hierarchy could not be prepared; the map has not changed" ) );
                return true;
            }
            for ( usize i = 0; i < affected.nCount; ++i ) {
                const u64 id = affected.pData[i];
                const auto *object = MapWireframe_FindObject( ws->wire, id );
                if ( MapLayers_IsObjectLocked( ws, id ) ) {
                    Hint( ws, QStringLiteral( "Shear: unlock every affected object's layer before dragging" ) ); return true;
                }
                if ( ( object != nullptr && ( object->kind == map_wire_kind_t::PREFAB || object->kind == map_wire_kind_t::TERRAIN ) ) ||
                     geo::GeometryDocument_FindHeightField( &ws->pDocument->geometry, { id } ) != nullptr ) {
                    Hint( ws, QStringLiteral( "Shear: prefab instances and terrain cannot be sheared; the selection has not changed" ) ); return true;
                }
            }
            std::vector<u64> capturedAffected( affected.pData, affected.pData + affected.nCount );
            s.shearBounds = box; s.shearAxis = axis; s.shearSign = sign; s.shearSlide = {};
            vec3 point{};
            if ( !ShearPoint( s, e, point ) || !std::isfinite( point.x ) || !std::isfinite( point.y ) || !std::isfinite( point.z ) ) {
                ResetShear( s ); return true;
            }
            s.shearRoots = std::move( roots ); s.shearAffected = std::move( capturedAffected );
            s.shearFrom = point; s.shearView = e.view.pView; s.shearDocument = ws->pDocument;
            s.shearGeometryRevision = ws->pDocument->geometry.revision; s.shearSelectionRevision = ws->selection.revision;
            s.shearMode = ws->elementMode; s.shearTextureLock = ws->bTextureLock != CY_FALSE;
            s.bShear = true; s.bShearHover = true;
            return true;
        }
        case map_tool_event_kind_t::MOVE: {
            if ( !s.bShear ) {
                const bool was = s.bShearHover;
                s.bShearHover = ws->selection.ids.nCount > 0 && ShearSide( ShearBox( ws ), e, s.shearAxis, s.shearSign );
                return s.bShearHover || was;
            }
            if ( !e.bButtonDown ) { ResetShear( s ); return true; }
            ( void )UpdateShear( ws, s, e );
            return true;
        }
        case map_tool_event_kind_t::RELEASE: {
            if ( !s.bShear ) { return false; }
            if ( !UpdateShear( ws, s, e ) || Length( s.shearSlide ) <= 1e-9 ) { ResetShear( s ); return true; }
            const shear_map_t transform = ShearMap( s );
            const bool textureLock = s.shearTextureLock;
            const std::vector<u64> selected = std::move( s.shearRoots );
            // No live capture survives synchronous edit/history notification.
            ResetShear( s );
            const bool done = Apply( ws, "Shear", [&]( map_document_t *map, vector_t<u64> *out ) noexcept {
                if ( !Vector_Reserve( out, selected.size() ) ) { return map_status_t::OUT_OF_MEMORY; }
                for ( const u64 id : selected ) { if ( !Vector_PushBack( out, id ) ) { return map_status_t::OUT_OF_MEMORY; } }
                return MapEdit_Shear( map, { selected.data(), selected.size() }, transform.axis, transform.perUnit, transform.pivot, textureLock );
            } );
            if ( !done && ws->lastEdit.outcome != map_edit_outcome_t::UNCHANGED ) {
                MapNotice_Post( ws, ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ? map_notice_t::ERROR : map_notice_t::WARNING,
                    ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ? QStringLiteral( "Shear: not enough memory; the map has not changed" ) :
                    QStringLiteral( "Shear: this transform is not supported by the selected geometry; the map has not changed" ) );
            }
            return true;
        }
        case map_tool_event_kind_t::CANCEL: {
            const bool had = s.bShear; ResetShear( s ); return had;
        }
        default: return false;
    }
}

bool ShearEvent( map_workspace_t *ws, state_t &s, const map_tool_event_t &e ) noexcept
{
    try { return ShearEventImpl( ws, s, e ); }
    catch ( ... ) { ResetShear( s ); MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Shear: not enough memory; the map has not changed" ) ); return true; }
}

// The bounds, the side under the cursor, and while dragging the sheared
// bounds and selection over the originals.
void ShearPaint( const map_workspace_t *ws, const state_t &s, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    if ( s.bShear && !ShearContextMatches( ws, s ) ) { return; }
    const map_bounds_t box = s.bShear ? s.shearBounds : ShearBox( ws );
    if ( !box.bHas ) { return; }
    const shear_map_t t = ShearMap( s );
    const vec3 &lo = box.box.minimum, &hi = box.box.maximum;
    const auto corner = [&]( u32 i ) noexcept { return vec3{ ( i & 1u ) ? hi.x : lo.x, ( i & 2u ) ? hi.y : lo.y, ( i & 4u ) ? hi.z : lo.z }; };
    const u32 sideBit = 1u << s.shearAxis;
    const auto onSide = [&]( u32 i ) noexcept { return ( ( i & sideBit ) != 0 ) == ( s.shearSign > 0 ); };
    const QColor ghost( 160, 160, 160 );
    if ( s.bShear ) {
        for ( const u64 id : s.shearRoots ) {
            const auto *object = MapWireframe_FindObject( ws->wire, id );
            if ( object == nullptr ) { continue; }
            for ( u32 k = 0; k < object->nLines; ++k ) {
                const auto &line = ws->wire.lines.pData[object->iFirstLine + k];
                DrawLine( painter, view, Sheared( t, ws->wire.points.pData[line.iA] ), Sheared( t, ws->wire.points.pData[line.iB] ), Accent() );
            }
        }
    }
    for ( u32 i = 0; i < 8u; ++i ) {
        for ( u32 bit = 1u; bit < 8u; bit <<= 1u ) {
            if ( ( i & bit ) != 0 ) { continue; }
            const u32 j = i | bit;
            const bool side = ( s.bShear || s.bShearHover ) && onSide( i ) && onSide( j );
            if ( s.bShear ) {
                DrawLine( painter, view, corner( i ), corner( j ), ghost, Qt::DashLine );
                DrawLine( painter, view, Sheared( t, corner( i ) ), Sheared( t, corner( j ) ), side ? Hover() : Accent(), side ? Qt::SolidLine : Qt::DashLine );
            } else {
                DrawLine( painter, view, corner( i ), corner( j ), side ? Hover() : ghost, side ? Qt::SolidLine : Qt::DotLine );
            }
        }
    }
    if ( s.bShear ) {
        vec3 centre = MapBounds_Center( box );
        SetComp( centre, s.shearAxis, s.shearSign > 0 ? Comp( hi, s.shearAxis ) : Comp( lo, s.shearAxis ) );
        DrawLabel( painter, view, Sheared( t, centre ),
                   QStringLiteral( "slide %1, %2, %3" ).arg( s.shearSlide.x, 0, 'f', 1 ).arg( s.shearSlide.y, 0, 'f', 1 ).arg( s.shearSlide.z, 0, 'f', 1 ), Accent() );
    }
}

bool MapToolGestures_Implements( map_tool_t tool ) noexcept
{
    switch ( tool ) {
        case map_tool_t::ENTITY: case map_tool_t::DECAL: case map_tool_t::OVERLAY: case map_tool_t::PATH:
        case map_tool_t::POLYGON: case map_tool_t::PATCH: case map_tool_t::CURVE: case map_tool_t::MEASURE:
        case map_tool_t::EYEDROPPER: case map_tool_t::MIRROR: case map_tool_t::PAINT: case map_tool_t::KNIFE:
        case map_tool_t::LOOP_CUT: case map_tool_t::VERTEX: case map_tool_t::PIVOT: case map_tool_t::WORKPLANE:
        case map_tool_t::EDGE_CUT: case map_tool_t::EDGE_ARC: case map_tool_t::DISPLACE: case map_tool_t::TERRAIN:
        case map_tool_t::SHEAR:
            return true;
        default: return false;
    }
}

bool MapToolGestures_Owns( const map_workspace_t *ws, bool bOrthoPane ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->pGui == nullptr ) { return false; }
    if ( GizmoApplies( ws ) || ComponentDragApplies( ws ) ) { return true; }
    if ( ws->tool == map_tool_t::MIRROR ) { return true; } // The mirror axis is drawn in a 2D pane; 3D says so.
    if ( ws->tool == map_tool_t::CLIP ) { return !bOrthoPane && SurfaceClipApplies( ws ); } // 2D: the view's own clip line.
    if ( ws->tool == map_tool_t::EXTRUDE ) { return !bOrthoPane; } // 3D mesh faces here; brush faces, and the 2D panes, stay with the views.
    return MapToolGestures_Implements( ws->tool );
}

void MapToolGestures_CompletePick( map_workspace_t *ws, const void *view ) noexcept
{
    const auto found = g_states.find( ws );
    if ( ws == nullptr || ws->pDocument == nullptr || found == g_states.end() ) { return; }
    auto &s = found->second;
    if ( !s.bGrabPending || s.grabView != view ) { return; }
    s.dragDocument = ws->pDocument; s.dragSelectionRevision = ws->selection.revision;
    s.dragGeometryRevision = ws->pDocument->geometry.revision;
}

bool MapToolGestures_ValidateCaptures( map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr ) { return false; }
    const auto found = g_states.find( ws );
    if ( found == g_states.end() ) { return false; }
    auto &s = found->second;
    bool changed = false;
    if ( s.paintObject != 0 && !PaintContextMatches( ws, s ) ) { ResetPaint( s ); changed = true; }
    if ( s.knifeMesh != 0 && !KnifeContextMatches( ws, s ) ) { ResetKnife( s ); changed = true; }
    if ( s.sculptWork && !SculptContextMatches( ws, *s.sculptWork ) ) { ResetSculpt( s ); changed = true; }
    if ( s.bArc && !ArcContextMatches( ws, s ) ) { ResetArc( s ); changed = true; }
    if ( s.bShear && !ShearContextMatches( ws, s ) ) { ResetShear( s ); changed = true; }
    if ( s.bGizmo && !GizmoContextMatches( ws, s ) ) { ResetGizmo( s ); changed = true; }
    if ( s.repeat.present && ( s.repeat.document != ws->pDocument || ws->pDocument == nullptr ||
         s.repeat.geometryRevision != ws->pDocument->geometry.revision ) ) { s.repeat.current = false; }
    return changed;
}

const map_wireframe_t *MapToolGestures_SculptPreviewWire( const map_workspace_t *ws ) noexcept
{
    const auto found = g_states.find( ws );
    if ( found == g_states.end() || !found->second.sculptWork || !SculptContextMatches( ws, *found->second.sculptWork ) ) { return nullptr; }
    return &found->second.sculptWork->wire;
}

bool MapToolGestures_CancelViewDrag( map_workspace_t *ws, const void *view ) noexcept
{
    if ( ws == nullptr || view == nullptr ) { return false; }
    const auto found = g_states.find( ws );
    if ( found == g_states.end() ) { return false; }
    auto &s = found->second;
    bool cancelled = false;
    if ( s.sculptWork && s.sculptWork->view == view ) { ResetSculpt( s ); cancelled = true; }
    if ( s.arcView == view ) { cancelled = cancelled || s.bArc; ResetArc( s ); }
    if ( s.shearView == view ) { cancelled = cancelled || s.bShear; ResetShear( s ); }
    if ( s.paintView == view ) {
        cancelled = cancelled || s.paintObject != 0;
        ResetPaint( s );
    }
    if ( s.extrudeView == view ) {
        cancelled = cancelled || s.bExtrude;
        ResetExtrude( s );
    }
    if ( s.gizmoView == view ) {
        cancelled = cancelled || s.bGizmo;
        ResetGizmo( s );
    }
    if ( s.grabView == view ) {
        cancelled = cancelled || s.bComponentDrag || s.bGrabPending;
        s.bComponentDrag = s.bComponentMoved = s.bComponentExtend = false;
        s.bGrabPending = s.bGrabHit = false;
        s.grabView = nullptr; s.dragDocument = nullptr;
        s.dragSelectionRevision = s.dragGeometryRevision = 0;
        s.dragDelta = {}; s.dragGrabbed = {}; s.dragSnapVertex = {}; s.dragMesh = s.priorMesh = 0;
        s.dragVertices.clear(); s.dragEdges.clear(); s.dragOrigins.clear();
        s.priorVertices.clear(); s.priorEdges.clear();
    }
    if ( s.lassoView == view ) {
        cancelled = cancelled || s.bLasso;
        s.bLasso = false; s.lassoView = nullptr;
        s.lassoA = s.lassoB = {};
    }
    // These captures only draw overlays. Do not clear a workspace preview
    // owned by a staged Block/Clip, or dispatch the active tool's Escape.
    if ( cancelled ) { MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); }
    return cancelled;
}

bool MapToolGestures_Event( map_workspace_t *ws, const map_tool_event_t &e ) noexcept
{
    if ( e.kind == map_tool_event_kind_t::MOVE && !e.bButtonDown && MapToolGestures_CancelViewDrag( ws, e.view.pView ) ) { return true; }
    if ( !MapToolGestures_Owns( ws, e.view.bOrtho ) ) { return false; }
    state_t *state = nullptr;
    try { state = &State( ws ); }
    catch ( ... ) {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Not enough memory to start the tool gesture." ) );
        return true;
    }
    state_t &s = *state;
    // A captured drag owns subsequent samples, even when it crosses a gizmo.
    // On press, an actual gizmo handle wins; elsewhere Translate can grab an edge.
    if ( s.bGizmo ) { return GizmoEvent( ws, s, e ); }
    if ( s.bComponentDrag || s.bLasso || s.bGrabPending ) { return ComponentDragEvent( ws, s, e ); }
    if ( GizmoApplies( ws ) && GizmoEvent( ws, s, e ) ) { return true; }
    if ( ComponentDragApplies( ws ) ) { return ComponentDragEvent( ws, s, e ); }
    switch ( ws->tool ) {
        case map_tool_t::CLIP: return SurfaceClipEvent( ws, s, e );
        case map_tool_t::DISPLACE: return DisplaceEvent( ws, s, e );
        case map_tool_t::EXTRUDE: return ExtrudeEvent( ws, s, e );
        case map_tool_t::TERRAIN: return TerrainEvent( ws, s, e );
        case map_tool_t::ENTITY: return EntityEvent( ws, s, e );
        case map_tool_t::DECAL: return DecalEvent( ws, s, e, false );
        case map_tool_t::OVERLAY: return DecalEvent( ws, s, e, true );
        case map_tool_t::PATH: return PathEvent( ws, s, e );
        case map_tool_t::POLYGON: return ChainEvent( ws, s, e, true );
        case map_tool_t::CURVE: return ChainEvent( ws, s, e, false );
        case map_tool_t::PATCH: return PatchEvent( ws, s, e );
        case map_tool_t::MEASURE: return MeasureEvent( ws, s, e );
        case map_tool_t::MIRROR: return MirrorEvent( ws, s, e );
        case map_tool_t::SHEAR: return ShearEvent( ws, s, e );
        case map_tool_t::EYEDROPPER: return EyedropperEvent( ws, s, e );
        case map_tool_t::PAINT: return PaintEvent( ws, s, e );
        case map_tool_t::WORKPLANE: return WorkplaneEvent( ws, s, e );
        case map_tool_t::PIVOT: return PivotEvent( ws, s, e );
        case map_tool_t::KNIFE: return KnifeEvent( ws, s, e, false );
        case map_tool_t::EDGE_CUT: return KnifeEvent( ws, s, e, true );
        case map_tool_t::EDGE_ARC: return ArcEvent( ws, s, e );
        case map_tool_t::LOOP_CUT: return LoopCutEvent( ws, s, e );
        case map_tool_t::VERTEX: return VertexEvent( ws, s, e );
        default: return false;
    }
}

namespace
{
// The Clip tool's staged plane, read the way TrenchBroom and Hammer show it:
// what the clip throws away is a red ghost, the plane is a translucent sheet
// across the selection in 3D, and each half says whether it stays.
void ClipOverlay( const map_workspace_t *ws, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    const auto &preview = ws->editPreview;
    if ( ws->tool != map_tool_t::CLIP || !preview.bActive || !preview.bClip || preview.status != map_status_t::OK ) { return; }
    const vec3 n = preview.clipPlane.normal;
    const f64 d = preview.clipPlane.d;
    const auto mode = preview.clipMode;
    const QColor removed( 240, 72, 60, 210 ), kept( 96, 220, 120 ), sheet( 110, 190, 255 );
    map_bounds_t bounds{};
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object == nullptr ) { continue; }
        MapBounds_AddBounds( bounds, object->bounds );
        if ( mode == map_brush_clip_mode_t::BOTH ) { continue; }
        // Every edge, or its part, on the side that goes away.
        const f64 keepSign = mode == map_brush_clip_mode_t::FRONT ? 1.0 : -1.0;
        for ( u32 l = 0; l < object->nLines; ++l ) {
            const auto &line = ws->wire.lines.pData[object->iFirstLine + l];
            const vec3 a = ws->wire.points.pData[line.iA], b = ws->wire.points.pData[line.iB];
            const f64 da = keepSign * ( Dot( n, a ) + d ), db = keepSign * ( Dot( n, b ) + d );
            if ( da >= -1e-6 && db >= -1e-6 ) { continue; }
            if ( da < 0 && db < 0 ) { DrawLine( painter, view, a, b, removed, Qt::DashLine ); continue; }
            const vec3 cut = Add( a, Mul( Sub( b, a ), da / ( da - db ) ) );
            DrawLine( painter, view, da < 0 ? a : b, cut, removed, Qt::DashLine );
        }
    }
    if ( !bounds.bHas ) { return; }
    const vec3 lo = bounds.box.minimum, hi = bounds.box.maximum, centre = MapBounds_Center( bounds );
    if ( !view.bOrtho ) {
        // The plane where it crosses the selection's box (a little larger).
        const f64 pad = std::max( 8.0, ws->gridSize );
        const vec3 a{ lo.x - pad, lo.y - pad, lo.z - pad }, b{ hi.x + pad, hi.y + pad, hi.z + pad };
        const vec3 corners[8]{ { a.x, a.y, a.z }, { b.x, a.y, a.z }, { b.x, b.y, a.z }, { a.x, b.y, a.z },
                               { a.x, a.y, b.z }, { b.x, a.y, b.z }, { b.x, b.y, b.z }, { a.x, b.y, b.z } };
        constexpr int kEdges[12][2]{ { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        std::vector<vec3> hits;
        for ( const auto &edge : kEdges ) {
            const vec3 p = corners[edge[0]], q = corners[edge[1]];
            const f64 dp = Dot( n, p ) + d, dq = Dot( n, q ) + d;
            if ( ( dp > 0 ) == ( dq > 0 ) || std::fabs( dp - dq ) < 1e-12 ) { continue; }
            hits.push_back( Add( p, Mul( Sub( q, p ), dp / ( dp - dq ) ) ) );
        }
        if ( hits.size() >= 3 ) {
            vec3 middle{};
            for ( const vec3 &h : hits ) { middle = Add( middle, h ); }
            middle = Mul( middle, 1.0 / static_cast<f64>( hits.size() ) );
            const vec3 u = Unit( Sub( hits[0], middle ) ), v = Cross( n, u );
            std::sort( hits.begin(), hits.end(), [&]( vec3 x, vec3 y ) {
                return std::atan2( Dot( Sub( x, middle ), v ), Dot( Sub( x, middle ), u ) ) < std::atan2( Dot( Sub( y, middle ), v ), Dot( Sub( y, middle ), u ) );
            } );
            QPolygonF polygon;
            bool visible = true;
            for ( const vec3 &h : hits ) { QPointF screen; visible = visible && Project( view, h, screen ); polygon << screen; }
            if ( visible ) {
                painter.setPen( QPen( sheet, 1.5 ) );
                painter.setBrush( QColor( sheet.red(), sheet.green(), sheet.blue(), 46 ) );
                painter.drawPolygon( polygon );
                painter.setBrush( Qt::NoBrush );
            }
        }
    }
    // Which half stays, written on each half, clear of the plane. Where the
    // plane faces the view the halves overlap: one tag says what stays.
    const auto tag = [&]( QPointF at, const QString &text, const QColor &color ) {
        const QFontMetricsF metrics( painter.font() );
        const QRectF box( at.x() - metrics.horizontalAdvance( text ) * 0.5 - 6, at.y() - metrics.height() * 0.5 - 3,
                          metrics.horizontalAdvance( text ) + 12, metrics.height() + 6 );
        painter.setPen( QPen( color, 1.0 ) ); painter.setBrush( QColor( 12, 12, 16, 190 ) );
        painter.drawRoundedRect( box, 4, 4 ); painter.setBrush( Qt::NoBrush );
        painter.drawText( box, Qt::AlignCenter, text );
    };
    const vec3 onPlane = Sub( centre, Mul( n, Dot( n, centre ) + d ) );
    const f64 reach = 0.25 * std::max( { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 16.0 } );
    QPointF c, f;
    if ( !Project( view, onPlane, c ) || !Project( view, Add( onPlane, Mul( n, reach ) ), f ) ) { return; }
    const bool keepFront = mode != map_brush_clip_mode_t::BACK, keepBack = mode != map_brush_clip_mode_t::FRONT;
    QPointF along = f - c;
    const f64 length = std::hypot( along.x(), along.y() );
    if ( length < 14.0 ) {
        tag( c, keepFront && keepBack ? QStringLiteral( "KEEP BOTH" ) : keepFront ? QStringLiteral( "KEEP FRONT +" ) : QStringLiteral( "KEEP BACK −" ), kept );
        return;
    }
    along /= length;
    const f64 offset = std::max( length, 56.0 );
    tag( c + along * offset, keepFront ? QStringLiteral( "KEEP" ) : QStringLiteral( "REMOVE" ), keepFront ? kept : removed );
    tag( c - along * offset, keepBack ? QStringLiteral( "KEEP" ) : QStringLiteral( "REMOVE" ), keepBack ? kept : removed );
}
} // namespace

void MapToolGestures_Paint( const map_workspace_t *ws, QPainter &painter, const map_tool_view_t &view ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr ) { return; }
    const auto found = g_states.find( ws );
    if ( ws->pDocument != nullptr ) {
        // The staged clip is workspace state: it shows before any gesture here.
        painter.save();
        painter.setRenderHint( QPainter::Antialiasing, true );
        ClipOverlay( ws, painter, view );
        // In 3D, a mesh shown through its modifiers (smoothed, mirrored...)
        // keeps its cage visible while its vertices or edges are edited, as
        // in Hammer 5: the cage is what the handles move.
        if ( !view.bOrtho && ( ws->elementMode == map_element_mode_t::VERTICES || ws->elementMode == map_element_mode_t::EDGES ) ) {
            for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
                const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
                if ( object == nullptr || object->kind != map_wire_kind_t::MESH || object->nPoints == 0 ||
                     ws->wire.pointSourceIds.pData[object->iFirstPoint + object->nPoints - 1] != 0 ) { continue; } // Not modified.
                for ( u32 l = 0; l < object->nLines; ++l ) {
                    const auto &line = ws->wire.lines.pData[object->iFirstLine + l];
                    if ( ws->wire.pointSourceIds.pData[line.iA] == 0 || ws->wire.pointSourceIds.pData[line.iB] == 0 ) { continue; }
                    DrawLine( painter, view, ws->wire.points.pData[line.iA], ws->wire.points.pData[line.iB], QColor( 112, 206, 112 ) );
                }
            }
        }
        painter.restore();
    }
    if ( ws->pDocument != nullptr && GizmoApplies( ws ) ) {
        // The gizmo shows as soon as components are selected, before any event.
        // Tool changes discard transient input, not the placed pivot. Paint
        // the same centre State() will use when the first press arrives.
        state_t idle{};
        if ( found != g_states.end() ) { idle.bPivot = found->second.bPivot; idle.pivot = found->second.pivot; }
        const state_t &current = found != g_states.end() && found->second.tool == ws->tool ? found->second : idle;
        painter.save();
        painter.setRenderHint( QPainter::Antialiasing, true );
        if ( current.bPivot ) { DrawPivotMarker( painter, view, current.pivot, false ); }
        GizmoPaint( ws, current, painter, view );
        if ( current.bComponentDrag || current.bLasso ) { ComponentDragPaint( ws, current, painter, view ); }
        painter.restore();
        return;
    }
    if ( found == g_states.end() ) { return; }
    const state_t &s = found->second;
    painter.save();
    painter.setRenderHint( QPainter::Antialiasing, true );
    // Persistent markers are drawn whatever the tool.
    if ( s.bComponentDrag || s.bLasso ) { ComponentDragPaint( ws, s, painter, view ); }
    if ( ws->tool == map_tool_t::EXTRUDE ) { ExtrudePaint( ws, s, painter, view ); }
    if ( ws->tool == map_tool_t::TERRAIN ) { TerrainPaint( ws, s, painter, view ); }
    if ( s.bPivot ) { DrawPivotMarker( painter, view, s.pivot, false ); }
    else if ( ws->tool == map_tool_t::PIVOT && ws->selection.ids.nCount != 0 ) { DrawPivotMarker( painter, view, SelectionCenter( ws ), true ); }
    if ( s.bWorkplane && !view.bOrtho ) {
        // The construction plane 3D placement uses, while it is set: strong
        // in the tools that draw on it, faint otherwise.
        const bool drawing = ws->tool == map_tool_t::WORKPLANE || ws->tool == map_tool_t::POLYGON || ws->tool == map_tool_t::ENTITY ||
                             ws->tool == map_tool_t::PATCH || ws->tool == map_tool_t::PATH || ws->tool == map_tool_t::CURVE;
        const vec3 n = s.workplane.normal;
        vec3 a = Unit( Cross( n, std::fabs( n.z ) < 0.9 ? vec3{ 0, 0, 1 } : vec3{ 1, 0, 0 } ) ), b = Unit( Cross( n, a ) );
        const f64 step = ws->gridSize > 0.0 ? ws->gridSize : 16.0;
        const QColor color( 120, 170, 255, drawing ? 170 : 60 );
        constexpr int kHalf = 8;
        for ( int i = -kHalf; i <= kHalf; ++i ) {
            DrawLine( painter, view, Add( s.workplanePoint, Add( Mul( a, i * step ), Mul( b, -kHalf * step ) ) ),
                Add( s.workplanePoint, Add( Mul( a, i * step ), Mul( b, kHalf * step ) ) ), color );
            DrawLine( painter, view, Add( s.workplanePoint, Add( Mul( b, i * step ), Mul( a, -kHalf * step ) ) ),
                Add( s.workplanePoint, Add( Mul( b, i * step ), Mul( a, kHalf * step ) ) ), color );
        }
        if ( drawing ) { DrawLabel( painter, view, s.workplanePoint, QStringLiteral( "Workplane" ), color ); }
    }
    if ( s.tool != ws->tool || !MapToolGestures_Owns( ws, view.bOrtho ) ) { painter.restore(); return; }
    switch ( ws->tool ) {
        case map_tool_t::ENTITY: case map_tool_t::DECAL: case map_tool_t::OVERLAY: case map_tool_t::EYEDROPPER: case map_tool_t::WORKPLANE:
            if ( s.bHover ) { DrawPoint( painter, view, s.hover, Hover(), 7.0 ); }
            if ( s.bHover && ws->tool == map_tool_t::ENTITY ) {
                const auto name = Text( ws, "editor.map.default_point_class", "info_player_start" );
                DrawLabel( painter, view, s.hover, QString::fromUtf8( name.pData, static_cast<qsizetype>( name.cchLength ) ), Hover() );
            }
            break;
        case map_tool_t::PATH:
            DrawChain( painter, view, s, false );
            break;
        case map_tool_t::POLYGON:
            DrawChain( painter, view, s, true );
            PolygonPreview( ws, s, painter, view );
            break;
        case map_tool_t::CURVE: case map_tool_t::EDGE_CUT: DrawChain( painter, view, s, false ); break;
        case map_tool_t::KNIFE:
            DrawChain( painter, view, s, false );
            // The optional run-on overlay builds face rings. A failed preview
            // allocation must not terminate the editor or alter staged input.
            try { KnifeEndsPaint( ws, s, painter, view ); } catch ( ... ) {}
            break;
        case map_tool_t::EDGE_ARC: ArcPaint( ws, s, painter, view ); break;
        case map_tool_t::MEASURE:
            if ( s.points.size() == 2 ) {
                const vec3 a = s.points[0], b = s.points[1], d = Sub( b, a );
                DrawLine( painter, view, a, b, Accent() ); DrawPoint( painter, view, a, Accent() ); DrawPoint( painter, view, b, Accent() );
                DrawLabel( painter, view, b, QStringLiteral( "%1 u  (%2, %3, %4)" ).arg( Length( d ), 0, 'f', 2 ).arg( d.x, 0, 'f', 1 ).arg( d.y, 0, 'f', 1 ).arg( d.z, 0, 'f', 1 ), Accent() );
            }
            break;
        case map_tool_t::PATCH:
            if ( s.bDrag && !PatchPreview( ws, s, painter, view ) ) {
                const u32 u = s.bPointsOrtho ? s.pointsU : 0u;
                vec3 c1 = s.dragA, c2 = s.dragA, c3 = s.dragB, c4 = s.dragB;
                SetComp( c2, u, Comp( s.dragB, u ) ); SetComp( c4, u, Comp( s.dragA, u ) );
                DrawLine( painter, view, c1, c2, Accent() ); DrawLine( painter, view, c2, c3, Accent() );
                DrawLine( painter, view, c3, c4, Accent() ); DrawLine( painter, view, c4, c1, Accent() );
            }
            break;
        case map_tool_t::CLIP: SurfaceClipPaint( ws, s, painter, view ); break;
        case map_tool_t::DISPLACE: DisplacePaint( ws, s, painter, view ); break;
        case map_tool_t::SHEAR: ShearPaint( ws, s, painter, view ); break;
        case map_tool_t::MIRROR:
            if ( s.bDrag ) {
                DrawLine( painter, view, s.dragA, s.dragB, Accent() );
                const vec3 d = Sub( s.dragB, s.dragA );
                DrawLine( painter, view, Sub( s.dragA, Mul( d, 4.0 ) ), Add( s.dragB, Mul( d, 4.0 ) ), Accent(), Qt::DashLine );
            }
            break;
        case map_tool_t::PAINT:
            {
                // The stroke so far: it lands on release, as one undo step.
                if ( s.paintObject != 0 && !PaintContextMatches( ws, s ) ) { break; }
                const f64 radius = s.paintObject != 0 ? s.paintRadius : Real( ws, "editor.map.paint_radius", 32.0 );
                for ( const vec3 &dab : s.dabs ) { DrawCircle( painter, view, dab, radius, s.paintColor ); }
                if ( s.bHover ) { DrawCircle( painter, view, s.hover, radius, Hover() ); }
                for ( const vec3 &p : s.dabs ) { DrawPoint( painter, view, p, Accent(), 3.0 ); }
            }
            break;
        case map_tool_t::LOOP_CUT:
            if ( s.bLoop ) {
                DrawLine( painter, view, s.loop.a, s.loop.b, Hover() );
                DrawPoint( painter, view, Add( s.loop.a, Mul( Sub( s.loop.b, s.loop.a ), std::clamp( s.loop.t, 0.05, 0.95 ) ) ), Accent(), 7.0 );
            }
            break;
        case map_tool_t::VERTEX: {
            for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
                const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
                if ( object == nullptr || ( object->kind != map_wire_kind_t::BRUSH && object->kind != map_wire_kind_t::MESH ) ) { continue; }
                for ( u32 k = 0; k < object->nPoints; ++k ) { DrawPoint( painter, view, ws->wire.points.pData[object->iFirstPoint + k], QColor( 230, 230, 230 ), 5.0 ); }
            }
            if ( s.bHover && !s.bVertex ) { DrawPoint( painter, view, s.hover, Hover(), 8.0 ); }
            if ( s.bVertex ) { DrawLine( painter, view, s.vertexFrom, s.vertexTo, Accent(), Qt::DashLine ); DrawPoint( painter, view, s.vertexTo, Accent(), 8.0 ); }
            break;
        }
        case map_tool_t::PIVOT:
            if ( s.bHover ) { DrawPoint( painter, view, s.hover, Hover(), 6.0 ); }
            break;
        default: break;
    }
    painter.restore();
}

void MapToolGestures_Release( const map_workspace_t *ws ) noexcept { g_states.erase( ws ); }

bool MapToolGestures_Pivot( const map_workspace_t *ws, vec3 *pPivotOut ) noexcept
{
    const auto found = g_states.find( ws );
    if ( found == g_states.end() || !found->second.bPivot ) { return false; }
    if ( pPivotOut != nullptr ) { *pPivotOut = found->second.pivot; }
    return true;
}

void MapToolGestures_ClearPivot( map_workspace_t *ws ) noexcept
{
    const auto found = g_states.find( ws );
    if ( found != g_states.end() && found->second.bPivot ) {
        auto &s = found->second;
        s.bGizmo = false; s.gizmoAffine = math::CY_AFFINE3D_IDENTITY; s.gizmoDelta = {};
        s.bPivot = false; MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
    }
}

} // namespace cypher::editor::map
