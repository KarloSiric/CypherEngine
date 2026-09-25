//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSlice.cpp
//  Purpose: Implements cutting an editable mesh with a plane (slice / clip /
//           cap).
//  Details: Plan, then apply. The plan classifies vertices by side, gives
//           each crossed edge one new vertex, cuts each crossed face into
//           pieces, and (for capped clips) chains the opening into cap
//           outlines. Only then is the mesh touched, by one
//           MeshBoundary_ReplaceFaces call that removes the cut and
//           discarded faces and adds the kept pieces and caps.
//
//           Cutting one face: the face's loop, with the crossing points
//           inserted, is a ring of "nodes" on the + side, the - side, or on
//           the line. Walking the on-line nodes in order along the line
//           with an inside/outside parity yields the stretches inside the
//           face; each becomes a chord (a new edge). The pieces are then
//           traced over the loop edges and the chords, turning at each
//           on-line node to the first edge clockwise from the one arrived
//           on (which keeps the piece's interior on the left). Along the
//           line only four directions matter - along it either way, or off
//           it to either side - so the turn is decided from the sides and
//           the order along the line, without constructed angles.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSlice.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshRecordAccess.h"
#include "CypherGeometry_MeshSource.h"
#include "CypherGeometry_Planar_Frame.h"
#include "CypherGeometry_Planar_Triangulate.h"
#include "CypherGeometry_PlanarRegion.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace mesh_detail;
using math::vec3d_t;

namespace
{

// Node keys: an existing vertex's handle key, or this bit plus the index of
// a new (crossing) vertex. Vertex keys never set it (slot < 2^31).
constexpr u64 kNewBit = u64{ 1 } << 63;

// Directions at an on-line node, in 90-degree steps counter-clockwise:
// along +t, off to the + side, along -t, off to the - side.
enum : u32 { kEast = 0u, kNorth = 1u, kWest = 2u, kSouth = 3u };

struct node_t {
    mesh_boundary_corner_t corner{};
    vec3d_t p{};
    f64 param{ 0.0 };                  // position along the line
    u64 key{ 0u };
    i8 side{ 0 };
    u32 chordPlus{ CY_INVALID_INDEX }; // node reached along +t by a chord
    u32 chordMinus{ CY_INVALID_INDEX };
};

// A run of consecutive on-line nodes in the loop, as an interval along the
// line. It toggles inside/outside when the loop arrives from one side and
// leaves to the other.
struct event_t {
    u32 iLow{ 0u };
    u32 iHigh{ 0u };
    f64 low{ 0.0 };
    f64 high{ 0.0 };
    bool bToggles{ false };
};

struct piece_t {
    geometry_mesh_face_handle_t hSource{};
    u32 iFirst{ 0u }; // into the plan's corner arrays
    u32 cCorners{ 0u };
    f64 area{ 0.0 };
    i8 side{ 0 };
};

struct dart_t {
    u64 from{ 0u };
    u64 to{ 0u };
};

bool DartLess( const dart_t &a, const dart_t &b ) noexcept
{
    return a.from < b.from || ( a.from == b.from && a.to < b.to );
}

bool EventLess( const event_t &a, const event_t &b ) noexcept
{
    return a.low < b.low;
}

i8 SideOf( f64 distance, f64 tolerance ) noexcept
{
    return distance > tolerance ? i8{ 1 } : ( distance < -tolerance ? i8{ -1 } : i8{ 0 } );
}

struct plan_t {
    // By slot.
    vector_t<f64> distance{};
    vector_t<i8> side{};
    vector_t<u32> newOfEdge{};
    // New (crossing) vertices.
    vector_t<vec3d_t> newPositions{};
    // Faces.
    vector_t<geometry_mesh_face_handle_t> cut{};
    vector_t<geometry_mesh_face_handle_t> discarded{};
    vector_t<geometry_mesh_face_handle_t> keptWhole{};
    // Pieces, back to back.
    vector_t<mesh_boundary_corner_t> pieceCorners{};
    vector_t<u64> pieceKeys{};
    vector_t<i8> pieceSides{};
    vector_t<piece_t> pieces{};
    // Per-face scratch.
    vector_t<node_t> nodes{};
    vector_t<event_t> events{};
    vector_t<u8> usedLoop{};
    vector_t<u8> usedPlus{};
    vector_t<u8> usedMinus{};
    vector_t<u32> ring{};
    vector_t<vec3d_t> points{};
};

bool InitPlan( plan_t *pP, const allocator_t *pA ) noexcept
{
    return Vector_Init( &pP->distance, pA ) && Vector_Init( &pP->side, pA ) && Vector_Init( &pP->newOfEdge, pA ) &&
           Vector_Init( &pP->newPositions, pA ) && Vector_Init( &pP->cut, pA ) && Vector_Init( &pP->discarded, pA ) &&
           Vector_Init( &pP->keptWhole, pA ) && Vector_Init( &pP->pieceCorners, pA ) && Vector_Init( &pP->pieceKeys, pA ) &&
           Vector_Init( &pP->pieceSides, pA ) && Vector_Init( &pP->pieces, pA ) && Vector_Init( &pP->nodes, pA ) &&
           Vector_Init( &pP->events, pA ) && Vector_Init( &pP->usedLoop, pA ) && Vector_Init( &pP->usedPlus, pA ) &&
           Vector_Init( &pP->usedMinus, pA ) && Vector_Init( &pP->ring, pA ) && Vector_Init( &pP->points, pA );
}

f64 AreaOf( const vec3d_t *pPoints, u32 cPoints ) noexcept
{
    return 0.5 * std::sqrt( math::Vec3d_LengthSquared( NewellOf( pPoints, cPoints ) ) );
}

// The new vertex of a crossed edge, created on first use. Computed from the
// edge record's own half-edge so both faces of the edge get the same point.
geometry_status_t CrossingOf( const editable_mesh_t *pMesh, plan_t *pP, geometry_mesh_edge_handle_t hEdge, u32 *pOut ) noexcept
{
    u32 &slot = pP->newOfEdge.pData[hEdge.nSlot];
    if ( slot == CY_INVALID_INDEX ) {
        const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, hEdge );
        const mesh_half_edge_record_t *pH = pE ? He( pMesh, pE->hHalfEdge ) : nullptr;
        if ( pH == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        const geometry_mesh_vertex_handle_t a = pH->hOrigin, b = DestOf( pMesh, *pH );
        const f64 da = pP->distance.pData[a.nSlot], db = pP->distance.pData[b.nSlot];
        const vec3d_t pa = GenerationPool_Get( &pMesh->vertices, a )->position;
        const vec3d_t pb = GenerationPool_Get( &pMesh->vertices, b )->position;
        const vec3d_t p = math::Vec3d_Add( pa, math::Vec3d_Scale( math::Vec3d_Subtract( pb, pa ), da / ( da - db ) ) );
        if ( !math::Vec3d_IsFinite( p ) ) { return geometry_status_t::NUMERIC_FAILURE; }
        if ( pP->newPositions.nCount >= kMeshSourceVerticesMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
        if ( !Vector_PushBack( &pP->newPositions, p ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        slot = static_cast<u32>( pP->newPositions.nCount - 1u );
    }
    *pOut = slot;
    return geometry_status_t::OK;
}

// Direction from on-line node v to node w (see the enum above).
u32 DirectionTo( const plan_t &p, u32 v, u32 w ) noexcept
{
    const node_t &to = p.nodes.pData[w];
    if ( to.side > 0 ) { return kNorth; }
    if ( to.side < 0 ) { return kSouth; }
    return to.param > p.nodes.pData[v].param ? kEast : kWest;
}

// Cuts one crossed face into pieces and appends them to the plan.
geometry_status_t CutFace( const editable_mesh_t *pMesh, plan_t *pP, geometry_mesh_face_handle_t hFace, vec3d_t planeNormal ) noexcept
{
    const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, hFace );
    const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, pF->hOuterLoop );

    // The line's direction, chosen so the + side lies on its left as seen
    // with the face's own winding (N x t points to the + side).
    const vec3d_t t = math::Vec3d_Cross( planeNormal, pF->normal );
    const f64 tLength = std::sqrt( math::Vec3d_LengthSquared( t ) );
    if ( !( tLength > 1e-9 ) ) { return geometry_status_t::DEGENERATE; } // crossed yet parallel to the plane
    const vec3d_t tUnit = math::Vec3d_Scale( t, 1.0 / tLength );

    // Nodes: the loop with the crossing points inserted.
    Vector_Clear( &pP->nodes );
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        const geometry_mesh_vertex_handle_t a = pH->hOrigin, b = DestOf( pMesh, *pH );
        node_t corner{};
        corner.corner.hVertex = a;
        corner.p = GenerationPool_Get( &pMesh->vertices, a )->position;
        corner.side = pP->side.pData[a.nSlot];
        corner.key = Key( a );
        if ( !Vector_PushBack( &pP->nodes, corner ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        if ( corner.side * pP->side.pData[b.nSlot] < 0 ) {
            u32 iNew = 0u;
            const geometry_status_t s = CrossingOf( pMesh, pP, pH->hEdge, &iNew );
            if ( s != geometry_status_t::OK ) { return s; }
            node_t c{};
            c.corner.iNew = iNew;
            c.p = pP->newPositions.pData[iNew];
            c.key = kNewBit | iNew;
            if ( !Vector_PushBack( &pP->nodes, c ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        }
        h = pH->hNext;
    }
    const u32 n = static_cast<u32>( pP->nodes.nCount );
    node_t *pN = pP->nodes.pData;
    for ( u32 i = 0u; i < n; ++i ) { pN[i].param = math::Vec3d_Dot( pN[i].p, tUnit ); }

    // Events: runs of on-line nodes, walked from an off-line node.
    u32 iStart = 0u;
    while ( iStart < n && pN[iStart].side == 0 ) { ++iStart; }
    if ( iStart == n ) { return geometry_status_t::DEGENERATE; }
    Vector_Clear( &pP->events );
    for ( u32 k = 1u; k <= n; ) {
        const u32 i = ( iStart + k ) % n;
        if ( pN[i].side != 0 ) {
            ++k;
            continue;
        }
        const i8 sBefore = pN[( i + n - 1u ) % n].side;
        u32 cRun = 0u;
        f64 direction = 0.0;
        while ( pN[( i + cRun ) % n].side == 0 ) {
            if ( cRun > 0u ) {
                // A run must move one way along the line; doubling back
                // means the face folds onto itself there.
                const f64 step = pN[( i + cRun ) % n].param - pN[( i + cRun - 1u ) % n].param;
                if ( !( step != 0.0 ) || step * direction < 0.0 ) { return geometry_status_t::DEGENERATE; }
                direction = step;
            }
            ++cRun;
        }
        const u32 iFirst = i, iLast = ( i + cRun - 1u ) % n;
        event_t e{};
        e.iLow = pN[iFirst].param <= pN[iLast].param ? iFirst : iLast;
        e.iHigh = e.iLow == iFirst ? iLast : iFirst;
        e.low = pN[e.iLow].param;
        e.high = pN[e.iHigh].param;
        e.bToggles = sBefore != pN[( i + cRun ) % n].side;
        if ( !Vector_PushBack( &pP->events, e ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        k += cRun;
    }
    std::sort( pP->events.pData, pP->events.pData + pP->events.nCount, EventLess );

    // Parity walk along the line: stretches between events while inside
    // become chords. Coinciding events mean two boundary points meet on the
    // line (a non-simple face) - there is no consistent cut.
    bool bInside = false;
    u32 iPrevHigh = CY_INVALID_INDEX;
    u32 cChords = 0u;
    for ( usize k = 0u; k < pP->events.nCount; ++k ) {
        const event_t &e = pP->events.pData[k];
        if ( k > 0u && !( pP->events.pData[k - 1u].high < e.low ) ) { return geometry_status_t::DEGENERATE; }
        if ( bInside ) {
            pN[iPrevHigh].chordPlus = e.iLow;
            pN[e.iLow].chordMinus = iPrevHigh;
            ++cChords;
        }
        if ( e.bToggles ) { bInside = !bInside; }
        iPrevHigh = e.iHigh;
    }
    if ( bInside || cChords == 0u ) { return geometry_status_t::DEGENERATE; }

    // Trace the pieces. Darts: loop edge i (i -> i + 1), and each chord both
    // ways (+t from its low end, -t from its high end).
    if ( !Vector_Resize( &pP->usedLoop, n ) || !Vector_Resize( &pP->usedPlus, n ) || !Vector_Resize( &pP->usedMinus, n ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 i = 0u; i < n; ++i ) { pP->usedLoop.pData[i] = pP->usedPlus.pData[i] = pP->usedMinus.pData[i] = 0u; }
    enum : u32 { kLoop = 0u, kPlus = 1u, kMinus = 2u };
    auto targetOf = [&]( u32 kind, u32 from ) noexcept {
        return kind == kLoop ? ( from + 1u ) % n : ( kind == kPlus ? pN[from].chordPlus : pN[from].chordMinus );
    };
    auto usedFlag = [&]( u32 kind, u32 from ) noexcept -> u8 & {
        return kind == kLoop ? pP->usedLoop.pData[from] : ( kind == kPlus ? pP->usedPlus.pData[from] : pP->usedMinus.pData[from] );
    };
    const u32 cDarts = n + 2u * cChords;
    u32 cUsed = 0u;
    for ( u32 start = 0u; start < n; ++start ) {
        if ( pP->usedLoop.pData[start] != 0u ) { continue; }
        Vector_Clear( &pP->ring );
        u32 kind = kLoop, from = start;
        for ( ;; ) {
            u8 &used = usedFlag( kind, from );
            if ( used != 0u || ++cUsed > cDarts ) { return geometry_status_t::DEGENERATE; }
            used = 1u;
            if ( !Vector_PushBack( &pP->ring, from ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            const u32 v = targetOf( kind, from );
            u32 nextKind = kLoop;
            if ( pN[v].side == 0 ) {
                // First dart clockwise from the way back. A loop dart that
                // leaves to the same side we came from is taken last: the
                // chords exist only when the interior wraps around it.
                const u32 back = DirectionTo( *pP, v, from );
                auto turn = [&]( u32 direction ) noexcept {
                    const u32 cw = ( back + 4u - direction ) % 4u;
                    return cw == 0u ? 4u : cw;
                };
                u32 best = turn( DirectionTo( *pP, v, ( v + 1u ) % n ) );
                if ( pN[v].chordPlus != CY_INVALID_INDEX && !( kind == kMinus && pN[v].chordPlus == from ) &&
                     turn( kEast ) < best ) {
                    best = turn( kEast );
                    nextKind = kPlus;
                }
                if ( pN[v].chordMinus != CY_INVALID_INDEX && !( kind == kPlus && pN[v].chordMinus == from ) &&
                     turn( kWest ) < best ) {
                    best = turn( kWest );
                    nextKind = kMinus;
                }
            }
            kind = nextKind;
            from = v;
            if ( kind == kLoop && from == start ) { break; }
        }
        // Record the piece.
        const u32 cCorners = static_cast<u32>( pP->ring.nCount );
        if ( cCorners < 3u ) { return geometry_status_t::DEGENERATE; }
        if ( cCorners > kMeshSourceCornersPerFaceMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
        piece_t piece{};
        piece.hSource = hFace;
        piece.iFirst = static_cast<u32>( pP->pieceCorners.nCount );
        piece.cCorners = cCorners;
        if ( !Vector_Resize( &pP->points, cCorners ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        for ( u32 k = 0u; k < cCorners; ++k ) {
            const node_t &nd = pN[pP->ring.pData[k]];
            if ( piece.side == 0 ) { piece.side = nd.side; }
            pP->points.pData[k] = nd.p;
            if ( !Vector_PushBack( &pP->pieceCorners, nd.corner ) || !Vector_PushBack( &pP->pieceKeys, nd.key ) ||
                 !Vector_PushBack( &pP->pieceSides, nd.side ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
        if ( piece.side == 0 ) { return geometry_status_t::DEGENERATE; }
        piece.area = AreaOf( pP->points.pData, cCorners );
        if ( !Vector_PushBack( &pP->pieces, piece ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    }
    return cUsed == cDarts ? geometry_status_t::OK : geometry_status_t::DEGENERATE;
}

// Appends the darts of a face loop whose two ends are both on the plane.
bool AppendPlaneDarts( const editable_mesh_t *pMesh, const plan_t &p, geometry_mesh_face_handle_t hFace, vector_t<dart_t> *pOut ) noexcept
{
    const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, hFace );
    const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, pF->hOuterLoop );
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        const geometry_mesh_vertex_handle_t a = pH->hOrigin, b = DestOf( pMesh, *pH );
        if ( p.side.pData[a.nSlot] == 0 && p.side.pData[b.nSlot] == 0 &&
             !Vector_PushBack( pOut, dart_t{ Key( a ), Key( b ) } ) ) {
            return false;
        }
        h = pH->hNext;
    }
    return true;
}

bool AppendPieceDarts( const plan_t &p, const piece_t &piece, vector_t<dart_t> *pOut ) noexcept
{
    for ( u32 k = 0u; k < piece.cCorners; ++k ) {
        const u32 i = piece.iFirst + k, j = piece.iFirst + ( k + 1u ) % piece.cCorners;
        if ( p.pieceSides.pData[i] == 0 && p.pieceSides.pData[j] == 0 &&
             !Vector_PushBack( pOut, dart_t{ p.pieceKeys.pData[i], p.pieceKeys.pData[j] } ) ) {
            return false;
        }
    }
    return true;
}

mesh_boundary_corner_t CornerOfKey( u64 key ) noexcept
{
    mesh_boundary_corner_t c{};
    if ( ( key & kNewBit ) != 0u ) {
        c.iNew = static_cast<u32>( key & ~kNewBit );
    } else {
        c.hVertex.nSlot = static_cast<u32>( key >> 32 );
        c.hVertex.nGeneration = static_cast<u32>( key & 0xFFFFFFFFu );
    }
    return c;
}

vec3d_t PositionOfKey( const editable_mesh_t *pMesh, const plan_t &p, u64 key ) noexcept
{
    const mesh_boundary_corner_t c = CornerOfKey( key );
    return c.iNew != CY_INVALID_INDEX ? p.newPositions.pData[c.iNew]
                                      : GenerationPool_Get( &pMesh->vertices, c.hVertex )->position;
}

// Cap outlines: the discarded side's in-plane darts whose reverse the kept
// side still uses. Each becomes a cap edge running the discarded face's way,
// so the caps face the removed side. Appends cap rings (keys) and sizes.
geometry_status_t PlanCaps(
    const editable_mesh_t *pMesh,
    const plan_t &p,
    i8 keptSide,
    const math::planed_t &unitPlane,
    const allocator_t *pA,
    vector_t<u64> *pCapKeys,
    vector_t<u32> *pCapSizes ) noexcept
{
    vector_t<dart_t> kept{}, gone{}, cap{};
    if ( !Vector_Init( &kept, pA ) || !Vector_Init( &gone, pA ) || !Vector_Init( &cap, pA ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < p.keptWhole.nCount; ++i ) {
        if ( !AppendPlaneDarts( pMesh, p, p.keptWhole.pData[i], &kept ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    }
    for ( usize i = 0u; i < p.discarded.nCount; ++i ) {
        if ( !AppendPlaneDarts( pMesh, p, p.discarded.pData[i], &gone ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    }
    for ( usize i = 0u; i < p.pieces.nCount; ++i ) {
        if ( !AppendPieceDarts( p, p.pieces.pData[i], p.pieces.pData[i].side == keptSide ? &kept : &gone ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    std::sort( kept.pData, kept.pData + kept.nCount, DartLess );
    for ( usize i = 0u; i < gone.nCount; ++i ) {
        const dart_t reverse{ gone.pData[i].to, gone.pData[i].from };
        if ( std::binary_search( kept.pData, kept.pData + kept.nCount, reverse, DartLess ) &&
             !Vector_PushBack( &cap, gone.pData[i] ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    if ( cap.nCount == 0u ) { return geometry_status_t::OK; }
    std::sort( cap.pData, cap.pData + cap.nCount, DartLess );
    for ( usize i = 1u; i < cap.nCount; ++i ) {
        if ( cap.pData[i].from == cap.pData[i - 1u].from ) { return geometry_status_t::NON_MANIFOLD; }
    }

    // Chain the darts into outlines.
    vector_t<u8> visited{};
    vector_t<u64> outline{};
    vector_t<vec3d_t> pts{};
    vector_t<u32> outlineFirst{}, outlineSize{};
    vector_t<u64> outlineKeys{};
    vector_t<u8> isHole{};
    if ( !Vector_Init( &visited, pA ) || !Vector_Init( &outline, pA ) || !Vector_Init( &pts, pA ) ||
         !Vector_Init( &outlineFirst, pA ) || !Vector_Init( &outlineSize, pA ) || !Vector_Init( &outlineKeys, pA ) ||
         !Vector_Init( &isHole, pA ) || !Vector_Resize( &visited, cap.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cap.nCount; ++i ) { visited.pData[i] = 0u; }
    const vec3d_t capNormal = keptSide > 0 ? math::Vec3d_Negate( unitPlane.normal ) : unitPlane.normal;
    for ( usize start = 0u; start < cap.nCount; ++start ) {
        if ( visited.pData[start] != 0u ) { continue; }
        Vector_Clear( &outline );
        Vector_Clear( &pts );
        usize cur = start;
        for ( ;; ) {
            visited.pData[cur] = 1u;
            if ( !Vector_PushBack( &outline, cap.pData[cur].from ) ||
                 !Vector_PushBack( &pts, PositionOfKey( pMesh, p, cap.pData[cur].from ) ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
            const dart_t probe{ cap.pData[cur].to, 0u };
            const dart_t *pNext = std::lower_bound( cap.pData, cap.pData + cap.nCount, probe, DartLess );
            // An opening that does not close (clipping an open mesh) has no
            // outline to cap.
            if ( pNext == cap.pData + cap.nCount || pNext->from != probe.from ) { return geometry_status_t::DEGENERATE; }
            cur = static_cast<usize>( pNext - cap.pData );
            if ( cur == start ) { break; }
            if ( visited.pData[cur] != 0u ) { return geometry_status_t::NON_MANIFOLD; }
        }
        const vec3d_t newell = NewellOf( pts.pData, static_cast<u32>( pts.nCount ) );
        const f64 facing = math::Vec3d_Dot( newell, capNormal );
        if ( !FaceAreaDescribable( newell ) || facing == 0.0 ) { return geometry_status_t::DEGENERATE; }
        if ( !Vector_PushBack( &outlineFirst, static_cast<u32>( outlineKeys.nCount ) ) ||
             !Vector_PushBack( &outlineSize, static_cast<u32>( outline.nCount ) ) ||
             !Vector_PushBack( &isHole, facing < 0.0 ? u8{ 1u } : u8{ 0u } ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
        for ( usize k = 0u; k < outline.nCount; ++k ) {
            if ( !Vector_PushBack( &outlineKeys, outline.pData[k] ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        }
    }

    const usize cOutlines = outlineFirst.nCount;
    auto emitRing = [&]( usize o ) noexcept {
        for ( u32 k = 0u; k < outlineSize.pData[o]; ++k ) {
            if ( !Vector_PushBack( pCapKeys, outlineKeys.pData[outlineFirst.pData[o] + k] ) ) { return false; }
        }
        return Vector_PushBack( pCapSizes, outlineSize.pData[o] );
    };

    // An outline without holes and within the corner limit is one face.
    // Otherwise it is triangulated with the holes it contains, in the cap
    // plane's frame (normal toward the removed side, so outer outlines run
    // CCW and holes CW, as the triangulator expects).
    planar_frame_t frame{};
    const math::planed_t capPlane{ capNormal, keptSide > 0 ? -unitPlane.d : unitPlane.d };
    if ( PlanarFrame_TryFromPlane( capPlane, &frame ) != geometry_status_t::OK ) { return geometry_status_t::NUMERIC_FAILURE; }
    vector_t<math::vec2d_t> flat{};
    vector_t<f64> outerArea{};
    vector_t<u32> holeOwner{};
    if ( !Vector_Init( &flat, pA ) || !Vector_Init( &outerArea, pA ) || !Vector_Init( &holeOwner, pA ) ||
         !Vector_Resize( &flat, outlineKeys.nCount ) || !Vector_Resize( &outerArea, cOutlines ) ||
         !Vector_Resize( &holeOwner, cOutlines ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < outlineKeys.nCount; ++i ) {
        flat.pData[i] = PlanarFrame_Project( frame, PositionOfKey( pMesh, p, outlineKeys.pData[i] ) );
    }
    auto ringOf = [&]( usize o ) noexcept {
        return span_t<const math::vec2d_t>{ flat.pData + outlineFirst.pData[o], outlineSize.pData[o] };
    };
    for ( usize o = 0u; o < cOutlines; ++o ) { outerArea.pData[o] = std::fabs( Planar_RingSignedArea( ringOf( o ) ) ); }
    // Each hole belongs to the smallest outer outline around it.
    for ( usize o = 0u; o < cOutlines; ++o ) {
        holeOwner.pData[o] = CY_INVALID_INDEX;
        if ( isHole.pData[o] == 0u ) { continue; }
        const math::vec2d_t probe = flat.pData[outlineFirst.pData[o]];
        for ( usize q = 0u; q < cOutlines; ++q ) {
            if ( isHole.pData[q] != 0u || Planar_RingContains( ringOf( q ), probe ) == planar_containment_t::OUTSIDE ) { continue; }
            if ( holeOwner.pData[o] == CY_INVALID_INDEX || outerArea.pData[q] < outerArea.pData[holeOwner.pData[o]] ) {
                holeOwner.pData[o] = static_cast<u32>( q );
            }
        }
        if ( holeOwner.pData[o] == CY_INVALID_INDEX ) { return geometry_status_t::DEGENERATE; }
    }
    planar_region_t region{};
    geometry_status_t st = PlanarRegion_Init( &region, pA, frame, geometry_source_id_t{ 1u } );
    vector_t<u64> regionKeys{};
    vector_t<planar_triangle_t> triangles{};
    if ( st == geometry_status_t::OK && ( !Vector_Init( &regionKeys, pA ) || !Vector_Init( &triangles, pA ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    u64 nextId = 2u;
    for ( usize o = 0u; st == geometry_status_t::OK && o < cOutlines; ++o ) {
        if ( isHole.pData[o] != 0u ) { continue; }
        bool bHasHoles = false;
        for ( usize q = 0u; q < cOutlines; ++q ) { bHasHoles = bHasHoles || holeOwner.pData[q] == o; }
        if ( !bHasHoles && outlineSize.pData[o] <= kMeshSourceCornersPerFaceMax ) {
            if ( !emitRing( o ) ) { st = geometry_status_t::ALLOCATION_FAILED; }
            continue;
        }
        u32 iPolygon = 0u;
        st = PlanarRegion_TryAddPolygon( &region, geometry_source_id_t{ nextId }, geometry_source_id_t{ nextId + 1u }, ringOf( o ),
                                         &iPolygon );
        nextId += 2u;
        for ( u32 k = 0u; st == geometry_status_t::OK && k < outlineSize.pData[o]; ++k ) {
            if ( !Vector_PushBack( &regionKeys, outlineKeys.pData[outlineFirst.pData[o] + k] ) ) { st = geometry_status_t::ALLOCATION_FAILED; }
        }
        for ( usize q = 0u; st == geometry_status_t::OK && q < cOutlines; ++q ) {
            if ( holeOwner.pData[q] != o ) { continue; }
            st = PlanarRegion_TryAddHole( &region, geometry_source_id_t{ nextId++ }, ringOf( q ) );
            for ( u32 k = 0u; st == geometry_status_t::OK && k < outlineSize.pData[q]; ++k ) {
                if ( !Vector_PushBack( &regionKeys, outlineKeys.pData[outlineFirst.pData[q] + k] ) ) {
                    st = geometry_status_t::ALLOCATION_FAILED;
                }
            }
        }
        if ( st != geometry_status_t::OK ) { break; }
        Vector_Clear( &triangles );
        st = Planar_TryTriangulatePolygon( &region, iPolygon, &triangles );
        for ( usize k = 0u; st == geometry_status_t::OK && k < triangles.nCount; ++k ) {
            const planar_triangle_t &tri = triangles.pData[k];
            if ( !Vector_PushBack( pCapKeys, regionKeys.pData[tri.a] ) || !Vector_PushBack( pCapKeys, regionKeys.pData[tri.b] ) ||
                 !Vector_PushBack( pCapKeys, regionKeys.pData[tri.c] ) || !Vector_PushBack( pCapSizes, 3u ) ) {
                st = geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }
    PlanarRegion_Shutdown( &region );
    return st;
}

} // namespace

mesh_slice_result_t MeshSlice_ByPlane(
    editable_mesh_t *pMesh,
    const mesh_slice_params_t &params,
    vector_t<mesh_slice_face_t> *pFacesOut ) noexcept
{
    mesh_slice_result_t r{};
    auto fail = [&]( geometry_status_t s ) noexcept {
        r = mesh_slice_result_t{};
        r.status = s;
        return r;
    };
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return fail( geometry_status_t::NOT_INITIALIZED ); }
    if ( ( pFacesOut != nullptr && pFacesOut->pAllocator == nullptr ) || params.keep > mesh_slice_keep_t::BACK ||
         ( params.bCap && params.keep == mesh_slice_keep_t::BOTH ) ) {
        return fail( geometry_status_t::INVALID_ARGUMENT );
    }
    if ( !math::Planed_IsFinite( params.plane ) || !std::isfinite( params.onPlaneTolerance ) ) {
        return fail( geometry_status_t::NUMERIC_FAILURE );
    }
    if ( params.onPlaneTolerance < 0.0 ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
    math::planed_t unitPlane{};
    if ( !math::Planed_TryNormalize( params.plane, 1e-12, &unitPlane ) ) { return fail( geometry_status_t::DEGENERATE ); }
    const i8 keptSide = params.keep == mesh_slice_keep_t::FRONT ? i8{ 1 } : ( params.keep == mesh_slice_keep_t::BACK ? i8{ -1 } : i8{ 0 } );

    const allocator_t *pA = pMesh->pAllocator;
    plan_t plan{};
    if ( !InitPlan( &plan, pA ) || !Vector_Resize( &plan.distance, pMesh->vertices.cSlots ) ||
         !Vector_Resize( &plan.side, pMesh->vertices.cSlots ) || !Vector_Resize( &plan.newOfEdge, pMesh->edges.cSlots ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < plan.newOfEdge.nCount; ++i ) { plan.newOfEdge.pData[i] = CY_INVALID_INDEX; }
    bool bFinite = true;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t &v ) noexcept -> bool_t {
            const f64 d = math::Vec3d_Dot( unitPlane.normal, v.position ) + unitPlane.d;
            bFinite = bFinite && std::isfinite( d );
            plan.distance.pData[h.nSlot] = d;
            plan.side.pData[h.nSlot] = SideOf( d, params.onPlaneTolerance );
            return true;
        } );
    if ( !bFinite ) { return fail( geometry_status_t::NUMERIC_FAILURE ); }

    // Sort the faces: crossed (cut), wholly on the discarded side, kept.
    bool bAllocOk = true;
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hFace, const mesh_face_record_t &f ) noexcept -> bool_t {
            const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
            bool bPos = false, bNeg = false;
            geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
            for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
                const mesh_half_edge_record_t *pH = He( pMesh, h );
                const i8 s = plan.side.pData[pH->hOrigin.nSlot];
                bPos = bPos || s > 0;
                bNeg = bNeg || s < 0;
                h = pH->hNext;
            }
            // A face in the plane bounds the solid behind its normal.
            const i8 faceSide = bPos ? i8{ 1 } : ( bNeg ? i8{ -1 } : ( math::Vec3d_Dot( f.normal, unitPlane.normal ) > 0.0 ? i8{ -1 } : i8{ 1 } ) );
            vector_t<geometry_mesh_face_handle_t> *pList =
                ( bPos && bNeg ) ? &plan.cut : ( keptSide != 0 && faceSide != keptSide ? &plan.discarded : &plan.keptWhole );
            bAllocOk = bAllocOk && Vector_PushBack( pList, hFace );
            return true;
        } );
    if ( !bAllocOk ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    if ( plan.cut.nCount == 0u && plan.discarded.nCount == 0u ) {
        r.status = geometry_status_t::OK; // the plane misses the mesh, or the clip keeps it all
        return r;
    }

    for ( usize i = 0u; i < plan.cut.nCount; ++i ) {
        const geometry_status_t s = CutFace( pMesh, &plan, plan.cut.pData[i], unitPlane.normal );
        if ( s != geometry_status_t::OK ) { return fail( s ); }
    }

    // Rings: kept pieces (largest of each face marked), then caps.
    vector_t<mesh_boundary_corner_t> corners{};
    vector_t<u32> sizes{};
    vector_t<mesh_slice_face_t> meta{};
    vector_t<u64> capKeys{};
    vector_t<u32> capSizes{};
    if ( !Vector_Init( &corners, pA ) || !Vector_Init( &sizes, pA ) || !Vector_Init( &meta, pA ) || !Vector_Init( &capKeys, pA ) ||
         !Vector_Init( &capSizes, pA ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    // CutFace appends a face's pieces together, so each face is one run.
    u32 cPieces = 0u;
    auto isKept = [&]( const piece_t &piece ) noexcept { return keptSide == 0 || piece.side == keptSide; };
    for ( usize g = 0u; g < plan.pieces.nCount; ) {
        usize gEnd = g + 1u;
        while ( gEnd < plan.pieces.nCount && Same( plan.pieces.pData[gEnd].hSource, plan.pieces.pData[g].hSource ) ) { ++gEnd; }
        usize iLargest = CY_INVALID_SIZE; // ties go to the earlier piece
        for ( usize i = g; i < gEnd; ++i ) {
            if ( isKept( plan.pieces.pData[i] ) &&
                 ( iLargest == CY_INVALID_SIZE || plan.pieces.pData[i].area > plan.pieces.pData[iLargest].area ) ) {
                iLargest = i;
            }
        }
        for ( usize i = g; i < gEnd; ++i ) {
            const piece_t &piece = plan.pieces.pData[i];
            if ( !isKept( piece ) ) { continue; }
            for ( u32 k = 0u; k < piece.cCorners; ++k ) {
                if ( !Vector_PushBack( &corners, plan.pieceCorners.pData[piece.iFirst + k] ) ) {
                    return fail( geometry_status_t::ALLOCATION_FAILED );
                }
            }
            mesh_slice_face_t m{};
            m.hSource = piece.hSource;
            m.role = mesh_slice_face_role_t::PIECE;
            m.bLargestPiece = i == iLargest;
            if ( !Vector_PushBack( &sizes, piece.cCorners ) || !Vector_PushBack( &meta, m ) ) {
                return fail( geometry_status_t::ALLOCATION_FAILED );
            }
            ++cPieces;
        }
        g = gEnd;
    }
    if ( cPieces == 0u && plan.keptWhole.nCount == 0u ) { return fail( geometry_status_t::DEGENERATE ); } // clips everything
    if ( params.bCap ) {
        const geometry_status_t s = PlanCaps( pMesh, plan, keptSide, unitPlane, pA, &capKeys, &capSizes );
        if ( s != geometry_status_t::OK ) { return fail( s ); }
        for ( usize i = 0u; i < capKeys.nCount; ++i ) {
            if ( !Vector_PushBack( &corners, CornerOfKey( capKeys.pData[i] ) ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
        }
        for ( usize i = 0u; i < capSizes.nCount; ++i ) {
            mesh_slice_face_t m{};
            m.role = mesh_slice_face_role_t::CAP;
            if ( !Vector_PushBack( &sizes, capSizes.pData[i] ) || !Vector_PushBack( &meta, m ) ) {
                return fail( geometry_status_t::ALLOCATION_FAILED );
            }
        }
    }

    // Keep only the new vertices a ring uses (a clip drops none today, since
    // every crossing point bounds a kept piece, but ReplaceFaces rejects
    // unused ones, so do not rely on that).
    vector_t<u32> remap{};
    vector_t<vec3d_t> used{};
    if ( !Vector_Init( &remap, pA ) || !Vector_Init( &used, pA ) || !Vector_Resize( &remap, plan.newPositions.nCount ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < remap.nCount; ++i ) { remap.pData[i] = CY_INVALID_INDEX; }
    for ( usize i = 0u; i < corners.nCount; ++i ) {
        u32 &iNew = corners.pData[i].iNew;
        if ( iNew == CY_INVALID_INDEX ) { continue; }
        if ( remap.pData[iNew] == CY_INVALID_INDEX ) {
            if ( !Vector_PushBack( &used, plan.newPositions.pData[iNew] ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            remap.pData[iNew] = static_cast<u32>( used.nCount - 1u );
        }
        iNew = remap.pData[iNew];
    }

    vector_t<geometry_mesh_face_handle_t> remove{}, created{};
    if ( !Vector_Init( &remove, pA ) || !Vector_Init( &created, pA ) ||
         !Vector_Reserve( &remove, plan.cut.nCount + plan.discarded.nCount ) || !Vector_Reserve( &created, sizes.nCount ) ||
         ( pFacesOut != nullptr && !Vector_Reserve( pFacesOut, pFacesOut->nCount + sizes.nCount ) ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < plan.cut.nCount; ++i ) { (void)Vector_PushBack( &remove, plan.cut.pData[i] ); }
    for ( usize i = 0u; i < plan.discarded.nCount; ++i ) { (void)Vector_PushBack( &remove, plan.discarded.pData[i] ); }
    const mesh_boundary_replace_result_t rr = MeshBoundary_ReplaceFaces(
        pMesh, span_t<const geometry_mesh_face_handle_t>{ remove.pData, remove.nCount },
        span_t<const mesh_boundary_corner_t>{ corners.pData, corners.nCount }, span_t<const u32>{ sizes.pData, sizes.nCount },
        span_t<const vec3d_t>{ used.pData, used.nCount }, &created );
    if ( rr.status != geometry_status_t::OK ) { return fail( rr.status ); }

    if ( pFacesOut != nullptr ) {
        for ( usize i = 0u; i < created.nCount; ++i ) {
            mesh_slice_face_t m = meta.pData[i];
            m.hFace = created.pData[i];
            (void)Vector_PushBack( pFacesOut, m );
        }
    }
    r.cFacesCut = static_cast<u32>( plan.cut.nCount );
    r.cFacesDiscarded = static_cast<u32>( plan.discarded.nCount );
    r.cPieces = cPieces;
    r.cCapFaces = static_cast<u32>( capSizes.nCount );
    r.cVerticesCreated = rr.cVerticesCreated;
    r.status = geometry_status_t::OK;
    return r;
}

} // namespace cypher::editor::geometry
