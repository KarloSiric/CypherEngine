//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Patch.cpp
//  Purpose: Implements Patch storage, de Casteljau evaluation, shape-
//           preserving refinement, axis operations, and validation.
//  Details: Evaluation is tensor de Casteljau with derivatives in
//           difference (hodograph) form rather than summed Bernstein
//           weights. The weighted-sum form turns a collapsed edge's exact
//           zero derivative into rounding noise with an arbitrary
//           direction, which then produced a wrong normal at cone apexes and
//           sphere poles; differences of identical controls are exactly 0.
//
//           Refinement (InsertColumn/InsertRow) uses de Casteljau at 0.5.
//           Subdivision is an identity on the surface, and each level only
//           averages neighbouring controls, so the refined patch differs
//           from the original by floating-point rounding alone - there is
//           no approximation error to accumulate across repeated edits.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Patch.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr u32 kMaxDegree = 3u;

// Relative threshold on |a x b| / (|a| |b|) (the sine of the angle between
// the partials) below which the analytic normal is treated as undefined.
// Collapsed patch edges produce an exactly zero partial, so this only has to
// separate "zero" from "tiny but real", not tune shading.
constexpr f64 kNormalSineMin = 1e-10;

bool IsValidBasis( patch_basis_t basis ) noexcept
{
    return basis == patch_basis_t::BIQUADRATIC_BEZIER ||
           basis == patch_basis_t::BICUBIC_BEZIER;
}

bool IsCanonicalEmpty( const patch_surface_t &patch ) noexcept
{
    return patch.controls.pData == nullptr && patch.controls.nCount == 0u &&
           patch.controls.nCapacity == 0u && patch.controls.pAllocator == nullptr &&
           patch.cColumns == 0u && patch.cRows == 0u &&
           patch.basis == patch_basis_t::BIQUADRATIC_BEZIER &&
           patch.materialId == 0u &&
           !GeometrySourceId_IsValid( patch.sourceId );
}

bool HasValidLayout( const patch_surface_t *pPatch ) noexcept
{
    if ( pPatch == nullptr || !Vector_IsValid( &pPatch->controls ) ||
         pPatch->controls.pAllocator == nullptr || !IsValidBasis( pPatch->basis ) ||
         !Patch_IsValidAxisCount( pPatch->basis, pPatch->cColumns ) ||
         !Patch_IsValidAxisCount( pPatch->basis, pPatch->cRows ) ) {
        return false;
    }
    const u64 cControls = static_cast<u64>( pPatch->cColumns ) *
                          static_cast<u64>( pPatch->cRows );
    return cControls <= kPatchControlsMax &&
           cControls == static_cast<u64>( pPatch->controls.nCount );
}

geometry_status_t RequireLayout( const patch_surface_t *pPatch ) noexcept
{
    if ( pPatch == nullptr || IsCanonicalEmpty( *pPatch ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    return HasValidLayout( pPatch ) ? geometry_status_t::OK
                                    : geometry_status_t::CORRUPT_STATE;
}

void ResetMetadata( patch_surface_t *pPatch ) noexcept
{
    pPatch->cColumns = 0u;
    pPatch->cRows = 0u;
    pPatch->basis = patch_basis_t::BIQUADRATIC_BEZIER;
    pPatch->materialId = 0u;
    pPatch->sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

void PublishPatch( patch_surface_t *pDestination, patch_surface_t *pSource ) noexcept
{
    Vector_Move( &pDestination->controls, &pSource->controls );
    pDestination->cColumns = pSource->cColumns;
    pDestination->cRows = pSource->cRows;
    pDestination->basis = pSource->basis;
    pDestination->materialId = pSource->materialId;
    pDestination->sourceId = pSource->sourceId;
    ResetMetadata( pSource );
}

bool PositionInRange( math::vec3d_t p ) noexcept
{
    return math::Vec3d_IsFinite( p ) && std::fabs( p.x ) <= kPatchCoordinateMax &&
           std::fabs( p.y ) <= kPatchCoordinateMax && std::fabs( p.z ) <= kPatchCoordinateMax;
}

bool UvInRange( math::vec2d_t uv ) noexcept
{
    return math::Vec2d_IsFinite( uv ) && std::fabs( uv.x ) <= kPatchCoordinateMax &&
           std::fabs( uv.y ) <= kPatchCoordinateMax;
}

bool SourceIdIsUsed(
    const patch_surface_t *pPatch,
    geometry_source_id_t id,
    u32 iIgnoredControl = CY_INVALID_INDEX ) noexcept
{
    if ( pPatch->sourceId.value == id.value ) { return true; }
    for ( usize i = 0u; i < pPatch->controls.nCount; ++i ) {
        if ( i != static_cast<usize>( iIgnoredControl ) &&
             pPatch->controls.pData[i].sourceId.value == id.value ) {
            return true;
        }
    }
    return false;
}

geometry_source_id_result_t AllocateUnusedSourceId(
    geometry_source_id_allocator_t *pIds,
    const patch_surface_t *pPatch ) noexcept
{
    // At most the patch ID and every live control ID can be skipped. The
    // monotonic allocator never returns the same candidate twice, so this
    // loop is bounded by controls.nCount + 2 attempts (or exhaustion).
    for ( usize iAttempt = 0u; iAttempt <= pPatch->controls.nCount + 1u; ++iAttempt ) {
        const geometry_source_id_result_t candidate =
            GeometrySourceIdAllocator_Allocate( pIds );
        if ( candidate.status != geometry_status_t::OK ||
             !SourceIdIsUsed( pPatch, candidate.id ) ) {
            return candidate;
        }
    }
    return { {}, geometry_status_t::IDENTITY_CONFLICT };
}

f64 Clamp01( f64 t ) noexcept
{
    // NaN compares false on both sides and would pass through; map it to 0
    // so a bad parameter yields a defined (if meaningless) sample.
    if ( !( t >= 0.0 ) ) { return 0.0; }
    return t > 1.0 ? 1.0 : t;
}

// Interpolation that is exact at both ends and for equal endpoints. The
// usual a + (b - a) t is not exact at t = 1, and (1 - t) a + t b is not
// exact when a == b; collapsed patch edges and shared sub-patch boundaries
// need both properties to stay bit-identical.
math::vec3d_t Lerp3( math::vec3d_t a, math::vec3d_t b, f64 t ) noexcept
{
    if ( math::Vec3d_EqualsExact( a, b ) ) { return a; }
    return math::Vec3d_Add( math::Vec3d_Scale( a, 1.0 - t ), math::Vec3d_Scale( b, t ) );
}

math::vec2d_t Lerp2( math::vec2d_t a, math::vec2d_t b, f64 t ) noexcept
{
    if ( a.x == b.x && a.y == b.y ) { return a; }
    return math::Vec2d_Add( math::Vec2d_Scale( a, 1.0 - t ), math::Vec2d_Scale( b, t ) );
}

// de Casteljau on n + 1 points; also returns the derivative from the last
// pair (hodograph form n (b1 - b0)). The difference form is what makes a
// collapsed edge report an exactly zero derivative: summing identical
// controls with cancelling Bernstein-derivative weights leaves rounding
// noise that looks like a real direction.
void Casteljau3( const math::vec3d_t *pIn, u32 n, f64 t, math::vec3d_t *pPoint, math::vec3d_t *pDerivative ) noexcept
{
    math::vec3d_t w[kMaxDegree + 1u];
    for ( u32 k = 0u; k <= n; ++k ) { w[k] = pIn[k]; }
    for ( u32 r = 1u; r < n; ++r ) {
        for ( u32 k = 0u; k + r <= n; ++k ) { w[k] = Lerp3( w[k], w[k + 1u], t ); }
    }
    if ( pDerivative ) { *pDerivative = math::Vec3d_Scale( math::Vec3d_Subtract( w[1], w[0] ), static_cast<f64>( n ) ); }
    *pPoint = Lerp3( w[0], w[1], t );
}

math::vec2d_t Casteljau2( const math::vec2d_t *pIn, u32 n, f64 t ) noexcept
{
    math::vec2d_t w[kMaxDegree + 1u];
    for ( u32 k = 0u; k <= n; ++k ) { w[k] = pIn[k]; }
    for ( u32 r = 1u; r <= n; ++r ) {
        for ( u32 k = 0u; k + r <= n; ++k ) { w[k] = Lerp2( w[k], w[k + 1u], t ); }
    }
    return w[0];
}

u32 GridIndex( const patch_surface_t *pPatch, u32 iColumn, u32 iRow ) noexcept
{
    return iRow * pPatch->cColumns + iColumn;
}

// Evaluates position, local partials, and UV of one sub-patch without the
// normal. Split out so the normal fallback can re-sample cheaply.
bool EvaluateRaw(
    const patch_surface_t *pPatch,
    u32 iSubU,
    u32 iSubV,
    f64 s,
    f64 t,
    patch_sample_t *pOut ) noexcept
{
    const u32 n = Patch_Degree( pPatch->basis );
    // Rows first (along s), then the column of row results (along t). dP/ds
    // is the t-interpolation of the rows' s-derivatives.
    math::vec3d_t rowPoint[kMaxDegree + 1u], rowDerivative[kMaxDegree + 1u];
    math::vec2d_t rowUv[kMaxDegree + 1u];
    for ( u32 j = 0u; j <= n; ++j ) {
        math::vec3d_t ctl[kMaxDegree + 1u];
        math::vec2d_t uvs[kMaxDegree + 1u];
        for ( u32 i = 0u; i <= n; ++i ) {
            const patch_control_t &c = pPatch->controls.pData[GridIndex( pPatch, iSubU * n + i, iSubV * n + j )];
            if ( !PositionInRange( c.position ) || !UvInRange( c.uv ) ) {
                *pOut = patch_sample_t{};
                return false;
            }
            ctl[i] = c.position;
            uvs[i] = c.uv;
        }
        Casteljau3( ctl, n, s, &rowPoint[j], &rowDerivative[j] );
        rowUv[j] = Casteljau2( uvs, n, s );
    }
    math::vec3d_t p{}, ps{}, pt{};
    Casteljau3( rowPoint, n, t, &p, &pt );
    Casteljau3( rowDerivative, n, t, &ps, nullptr );
    const math::vec2d_t uv = Casteljau2( rowUv, n, t );
    pOut->position = p;
    pOut->dPdu = ps;
    pOut->dPdv = pt;
    pOut->uv = uv;
    return math::Vec3d_IsFinite( pOut->position ) &&
           math::Vec3d_IsFinite( pOut->dPdu ) &&
           math::Vec3d_IsFinite( pOut->dPdv ) &&
           math::Vec2d_IsFinite( pOut->uv );
}

bool TryNormalFromPartials( math::vec3d_t a, math::vec3d_t b, math::vec3d_t *pNormal ) noexcept
{
    const math::vec3d_t n = math::Vec3d_Cross( a, b );
    const f64 scale = math::Vec3d_LengthSquared( a ) * math::Vec3d_LengthSquared( b );
    const f64 cross2 = math::Vec3d_LengthSquared( n );
    if ( !( scale > 0.0 ) || !std::isfinite( scale ) || !std::isfinite( cross2 ) ||
         cross2 <= kNormalSineMin * kNormalSineMin * scale ) {
        return false;
    }
    return math::Vec3d_TryNormalize( n, 0.0, pNormal, nullptr );
}

// Parameter-domain copy of axis data into a fresh grid for refinement.
struct axis_view_t {
    u32 cAlong;   // controls along the refined axis
    u32 cAcross;  // controls along the other axis
    bool bAlongU; // true: refine columns (u), false: refine rows (v)
};

u32 ViewIndex( const axis_view_t &view, u32 cAlong, u32 iAlong, u32 iAcross ) noexcept
{
    // Map (along, across) to row-major (column, row) of a grid whose along
    // extent is cAlong.
    if ( view.bAlongU ) { return iAcross * cAlong + iAlong; }
    return iAlong * view.cAcross + iAcross;
}

geometry_status_t TryInsertAlongAxis(
    patch_surface_t *pPatch,
    bool bAlongU,
    u32 iSub,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept
{
    const geometry_status_t layout = RequireLayout( pPatch );
    if ( layout != geometry_status_t::OK ) { return layout; }
    if ( pIdAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const u32 n = Patch_Degree( pPatch->basis );
    axis_view_t view{};
    view.bAlongU = bAlongU;
    view.cAlong = bAlongU ? pPatch->cColumns : pPatch->cRows;
    view.cAcross = bAlongU ? pPatch->cRows : pPatch->cColumns;
    const u32 cSub = ( view.cAlong - 1u ) / n;
    if ( iSub >= cSub ) { return geometry_status_t::INVALID_HANDLE; }

    const u32 cAlongNew = view.cAlong + n;
    if ( cAlongNew > kPatchControlsPerAxisMax ||
         static_cast<u64>( cAlongNew ) * view.cAcross > kPatchControlsMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const patch_validation_result_t validation =
        Patch_Validate( pPatch, pPatch->controls.pAllocator );
    if ( validation.fault == patch_fault_t::VALIDATION_INCOMPLETE ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( validation.fault != patch_fault_t::NONE &&
         validation.fault != patch_fault_t::COLLAPSED_SURFACE ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    vector_t<patch_control_t> next{};
    if ( !Vector_Init( &next, pPatch->controls.pAllocator ) ||
         !Vector_Resize( &next, static_cast<usize>( cAlongNew ) * view.cAcross ) ) {
        Vector_Shutdown( &next );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // IDs are allocated from a local copy and committed only on success.
    geometry_source_id_allocator_t ids = *pIdAllocator;
    const u32 iFirst = iSub * n; // first control of the refined sub-patch
    for ( u32 iAcross = 0u; iAcross < view.cAcross; ++iAcross ) {
        // Controls before the refined span are copied as-is.
        for ( u32 k = 0u; k < iFirst; ++k ) {
            next.pData[ViewIndex( view, cAlongNew, k, iAcross )] =
                pPatch->controls.pData[ViewIndex( view, view.cAlong, k, iAcross )];
        }

        // de Casteljau at 0.5 on positions and UVs of the refined span.
        math::vec3d_t pos[kMaxDegree + 1u][kMaxDegree + 1u];
        math::vec2d_t uv[kMaxDegree + 1u][kMaxDegree + 1u];
        geometry_source_id_t oldIds[kMaxDegree + 1u];
        for ( u32 k = 0u; k <= n; ++k ) {
            const patch_control_t &c = pPatch->controls.pData[ViewIndex( view, view.cAlong, iFirst + k, iAcross )];
            pos[0][k] = c.position;
            uv[0][k] = c.uv;
            oldIds[k] = c.sourceId;
        }
        for ( u32 r = 1u; r <= n; ++r ) {
            for ( u32 k = 0u; k + r <= n; ++k ) {
                pos[r][k] = math::Vec3d_Scale( math::Vec3d_Add( pos[r - 1u][k], pos[r - 1u][k + 1u] ), 0.5 );
                uv[r][k] = math::Vec2d_Scale( math::Vec2d_Add( uv[r - 1u][k], uv[r - 1u][k + 1u] ), 0.5 );
            }
        }
        // Left half: pos[k][0] for k = 0..n. Right half: pos[n-k][k].
        // The first half keeps the span's original IDs (like a split curve
        // segment keeps its ID on the first half); the shared midpoint and
        // the right half's interior controls are new.
        for ( u32 k = 0u; k <= n; ++k ) {
            patch_control_t c{};
            c.position = pos[k][0];
            c.uv = uv[k][0];
            if ( k < n ) {
                c.sourceId = oldIds[k];
            } else {
                const geometry_source_id_result_t id = AllocateUnusedSourceId( &ids, pPatch );
                if ( id.status != geometry_status_t::OK ) {
                    Vector_Shutdown( &next );
                    return id.status;
                }
                c.sourceId = id.id;
            }
            next.pData[ViewIndex( view, cAlongNew, iFirst + k, iAcross )] = c;
        }
        for ( u32 k = 1u; k <= n; ++k ) {
            patch_control_t c{};
            c.position = pos[n - k][k];
            c.uv = uv[n - k][k];
            if ( k == n ) {
                c.sourceId = oldIds[n];
            } else {
                const geometry_source_id_result_t id = AllocateUnusedSourceId( &ids, pPatch );
                if ( id.status != geometry_status_t::OK ) {
                    Vector_Shutdown( &next );
                    return id.status;
                }
                c.sourceId = id.id;
            }
            next.pData[ViewIndex( view, cAlongNew, iFirst + n + k, iAcross )] = c;
        }

        // Controls after the refined span shift by n.
        for ( u32 k = iFirst + n + 1u; k < view.cAlong; ++k ) {
            next.pData[ViewIndex( view, cAlongNew, k + n, iAcross )] =
                pPatch->controls.pData[ViewIndex( view, view.cAlong, k, iAcross )];
        }
    }

    Vector_Shutdown( &pPatch->controls );
    Vector_Move( &pPatch->controls, &next );
    if ( bAlongU ) {
        pPatch->cColumns = cAlongNew;
    } else {
        pPatch->cRows = cAlongNew;
    }
    *pIdAllocator = ids;
    return geometry_status_t::OK;
}

} // namespace

u32 Patch_Degree( patch_basis_t basis ) noexcept
{
    if ( basis == patch_basis_t::BIQUADRATIC_BEZIER ) { return 2u; }
    if ( basis == patch_basis_t::BICUBIC_BEZIER ) { return 3u; }
    return 0u;
}

bool Patch_IsValidAxisCount( patch_basis_t basis, u32 count ) noexcept
{
    if ( basis != patch_basis_t::BIQUADRATIC_BEZIER && basis != patch_basis_t::BICUBIC_BEZIER ) {
        return false;
    }
    const u32 n = Patch_Degree( basis );
    return count >= n + 1u && count <= kPatchControlsPerAxisMax && ( count - 1u ) % n == 0u;
}

bool Patch_IsInitialized( const patch_surface_t *pPatch ) noexcept
{
    return HasValidLayout( pPatch );
}

geometry_status_t Patch_Init(
    patch_surface_t *pPatch,
    const allocator_t *pAllocator,
    patch_basis_t basis,
    u32 cColumns,
    u32 cRows,
    geometry_source_id_t patchId ) noexcept
{
    if ( pPatch == nullptr || !Allocator_IsValid( pAllocator ) || !GeometrySourceId_IsValid( patchId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !IsCanonicalEmpty( *pPatch ) ) {
        return HasValidLayout( pPatch ) ? geometry_status_t::ALREADY_INITIALIZED
                                        : geometry_status_t::CORRUPT_STATE;
    }
    if ( !Patch_IsValidAxisCount( basis, cColumns ) || !Patch_IsValidAxisCount( basis, cRows ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( static_cast<u64>( cColumns ) * cRows > kPatchControlsMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    patch_surface_t next{};
    if ( !Vector_Init( &next.controls, pAllocator ) ||
         !Vector_Resize( &next.controls, static_cast<usize>( cColumns ) * cRows ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < next.controls.nCount; ++i ) {
        next.controls.pData[i] = patch_control_t{};
    }
    next.cColumns = cColumns;
    next.cRows = cRows;
    next.basis = basis;
    next.sourceId = patchId;
    PublishPatch( pPatch, &next );
    return geometry_status_t::OK;
}

geometry_status_t Patch_TryInitFlat(
    patch_surface_t *pPatch,
    const allocator_t *pAllocator,
    patch_basis_t basis,
    u32 cColumns,
    u32 cRows,
    math::vec3d_t origin,
    math::vec3d_t uAxis,
    math::vec3d_t vAxis,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept
{
    if ( pIdAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !math::Vec3d_IsFinite( origin ) || !math::Vec3d_IsFinite( uAxis ) || !math::Vec3d_IsFinite( vAxis ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( pPatch == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !IsCanonicalEmpty( *pPatch ) ) {
        return HasValidLayout( pPatch ) ? geometry_status_t::ALREADY_INITIALIZED
                                        : geometry_status_t::CORRUPT_STATE;
    }

    patch_surface_t next{};
    const geometry_status_t init = Patch_Init( &next, pAllocator, basis, cColumns, cRows, patchId );
    if ( init != geometry_status_t::OK ) { return init; }

    geometry_source_id_allocator_t ids = *pIdAllocator;
    for ( u32 r = 0u; r < cRows; ++r ) {
        for ( u32 c = 0u; c < cColumns; ++c ) {
            const f64 fu = static_cast<f64>( c ) / static_cast<f64>( cColumns - 1u );
            const f64 fv = static_cast<f64>( r ) / static_cast<f64>( cRows - 1u );
            patch_control_t ctl{};
            ctl.position = math::Vec3d_Add(
                origin, math::Vec3d_Add( math::Vec3d_Scale( uAxis, fu ), math::Vec3d_Scale( vAxis, fv ) ) );
            ctl.uv = math::vec2d_t{ fu, fv };
            if ( !PositionInRange( ctl.position ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
            const geometry_source_id_result_t id = AllocateUnusedSourceId( &ids, &next );
            if ( id.status != geometry_status_t::OK ) {
                return id.status;
            }
            ctl.sourceId = id.id;
            next.controls.pData[GridIndex( &next, c, r )] = ctl;
        }
    }
    PublishPatch( pPatch, &next );
    *pIdAllocator = ids;
    return geometry_status_t::OK;
}

void Patch_Shutdown( patch_surface_t *pPatch ) noexcept
{
    if ( pPatch == nullptr ) { return; }
    Vector_Shutdown( &pPatch->controls );
    ResetMetadata( pPatch );
}

geometry_status_t Patch_TryClone(
    const patch_surface_t *pSource,
    const allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept
{
    const geometry_status_t sourceLayout = RequireLayout( pSource );
    if ( sourceLayout != geometry_status_t::OK ) { return sourceLayout; }
    if ( pOut == nullptr || pOut == pSource ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !IsCanonicalEmpty( *pOut ) ) {
        return HasValidLayout( pOut ) ? geometry_status_t::ALREADY_INITIALIZED
                                      : geometry_status_t::CORRUPT_STATE;
    }
    patch_surface_t next{};
    const geometry_status_t init =
        Patch_Init( &next, pAllocator, pSource->basis, pSource->cColumns, pSource->cRows, pSource->sourceId );
    if ( init != geometry_status_t::OK ) { return init; }
    std::memcpy( next.controls.pData, pSource->controls.pData,
                 sizeof( patch_control_t ) * pSource->controls.nCount );
    next.materialId = pSource->materialId;
    PublishPatch( pOut, &next );
    return geometry_status_t::OK;
}

const patch_control_t *Patch_Control( const patch_surface_t *pPatch, u32 iColumn, u32 iRow ) noexcept
{
    if ( !HasValidLayout( pPatch ) || iColumn >= pPatch->cColumns || iRow >= pPatch->cRows ) {
        return nullptr;
    }
    return &pPatch->controls.pData[GridIndex( pPatch, iColumn, iRow )];
}

geometry_status_t Patch_TrySetControl(
    patch_surface_t *pPatch,
    u32 iColumn,
    u32 iRow,
    patch_control_t control ) noexcept
{
    const geometry_status_t layout = RequireLayout( pPatch );
    if ( layout != geometry_status_t::OK ) { return layout; }
    if ( iColumn >= pPatch->cColumns || iRow >= pPatch->cRows ) { return geometry_status_t::INVALID_HANDLE; }
    if ( !GeometrySourceId_IsValid( control.sourceId ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !PositionInRange( control.position ) || !UvInRange( control.uv ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    const u32 iControl = GridIndex( pPatch, iColumn, iRow );
    if ( SourceIdIsUsed( pPatch, control.sourceId, iControl ) ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }
    pPatch->controls.pData[iControl] = control;
    return geometry_status_t::OK;
}

u32 Patch_SubPatchColumns( const patch_surface_t *pPatch ) noexcept
{
    if ( !HasValidLayout( pPatch ) ) { return 0u; }
    return ( pPatch->cColumns - 1u ) / Patch_Degree( pPatch->basis );
}

u32 Patch_SubPatchRows( const patch_surface_t *pPatch ) noexcept
{
    if ( !HasValidLayout( pPatch ) ) { return 0u; }
    return ( pPatch->cRows - 1u ) / Patch_Degree( pPatch->basis );
}

u32 Patch_SubPatchControlIndex(
    const patch_surface_t *pPatch,
    u32 iSubU,
    u32 iSubV,
    u32 i,
    u32 j ) noexcept
{
    if ( !HasValidLayout( pPatch ) ) { return CY_INVALID_INDEX; }
    const u32 n = Patch_Degree( pPatch->basis );
    if ( iSubU >= Patch_SubPatchColumns( pPatch ) || iSubV >= Patch_SubPatchRows( pPatch ) || i > n || j > n ) {
        return CY_INVALID_INDEX;
    }
    return GridIndex( pPatch, iSubU * n + i, iSubV * n + j );
}

patch_sample_t Patch_EvaluateSubPatch(
    const patch_surface_t *pPatch,
    u32 iSubU,
    u32 iSubV,
    f64 s,
    f64 t ) noexcept
{
    patch_sample_t out{};
    if ( !HasValidLayout( pPatch ) || iSubU >= Patch_SubPatchColumns( pPatch ) ||
         iSubV >= Patch_SubPatchRows( pPatch ) ) {
        return out;
    }
    s = Clamp01( s );
    t = Clamp01( t );
    if ( !EvaluateRaw( pPatch, iSubU, iSubV, s, t, &out ) ) { return patch_sample_t{}; }
    if ( TryNormalFromPartials( out.dPdu, out.dPdv, &out.normal ) ) {
        out.bNormalValid = true;
        return out;
    }

    // Collapsed edge: one partial vanishes (all controls of that boundary
    // coincide). The surface normal still has a well-defined limit from the
    // interior, so sample a short, growing distance towards the sub-patch
    // centre. Deterministic: fixed steps, fixed direction.
    constexpr f64 kNudges[3] = { 1e-6, 1e-4, 1e-2 };
    for ( const f64 nudge : kNudges ) {
        patch_sample_t probe{};
        if ( !EvaluateRaw( pPatch, iSubU, iSubV,
                           s + ( 0.5 - s ) * nudge,
                           t + ( 0.5 - t ) * nudge, &probe ) ) {
            return patch_sample_t{};
        }
        if ( TryNormalFromPartials( probe.dPdu, probe.dPdv, &out.normal ) ) {
            out.bNormalValid = true;
            out.bNormalFromNeighbour = true;
            return out;
        }
    }
    out.normal = math::vec3d_t{};
    return out;
}

patch_sample_t Patch_Evaluate( const patch_surface_t *pPatch, f64 u, f64 v ) noexcept
{
    if ( !HasValidLayout( pPatch ) ) { return patch_sample_t{}; }
    const u32 cSubU = Patch_SubPatchColumns( pPatch );
    const u32 cSubV = Patch_SubPatchRows( pPatch );
    u = Clamp01( u );
    v = Clamp01( v );
    const f64 su = u * static_cast<f64>( cSubU );
    const f64 sv = v * static_cast<f64>( cSubV );
    u32 iSubU = static_cast<u32>( std::floor( su ) );
    u32 iSubV = static_cast<u32>( std::floor( sv ) );
    if ( iSubU >= cSubU ) { iSubU = cSubU - 1u; }
    if ( iSubV >= cSubV ) { iSubV = cSubV - 1u; }
    patch_sample_t out =
        Patch_EvaluateSubPatch( pPatch, iSubU, iSubV, su - static_cast<f64>( iSubU ), sv - static_cast<f64>( iSubV ) );
    // Chain rule: local parameter s = u * cSubU - iSubU.
    out.dPdu = math::Vec3d_Scale( out.dPdu, static_cast<f64>( cSubU ) );
    out.dPdv = math::Vec3d_Scale( out.dPdv, static_cast<f64>( cSubV ) );
    return out;
}

math::aabbd_t Patch_ControlBounds( const patch_surface_t *pPatch ) noexcept
{
    math::aabbd_t bounds = math::CY_AABBD_EMPTY;
    if ( !HasValidLayout( pPatch ) ) { return bounds; }
    for ( usize i = 0u; i < pPatch->controls.nCount; ++i ) {
        if ( !PositionInRange( pPatch->controls.pData[i].position ) ) {
            return math::CY_AABBD_EMPTY;
        }
        bounds = math::Aabbd_ExpandPoint( bounds, pPatch->controls.pData[i].position );
    }
    return bounds;
}

geometry_status_t Patch_TryInsertColumn(
    patch_surface_t *pPatch,
    u32 iSubU,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept
{
    return TryInsertAlongAxis( pPatch, true, iSubU, pIdAllocator );
}

geometry_status_t Patch_TryInsertRow(
    patch_surface_t *pPatch,
    u32 iSubV,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept
{
    return TryInsertAlongAxis( pPatch, false, iSubV, pIdAllocator );
}

geometry_status_t Patch_TryTranspose( patch_surface_t *pPatch ) noexcept
{
    const geometry_status_t layout = RequireLayout( pPatch );
    if ( layout != geometry_status_t::OK ) { return layout; }
    vector_t<patch_control_t> next{};
    if ( !Vector_Init( &next, pPatch->controls.pAllocator ) ||
         !Vector_Resize( &next, pPatch->controls.nCount ) ) {
        Vector_Shutdown( &next );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const u32 cColumns = pPatch->cColumns, cRows = pPatch->cRows;
    for ( u32 r = 0u; r < cRows; ++r ) {
        for ( u32 c = 0u; c < cColumns; ++c ) {
            // New grid has cRows columns: new (column = r, row = c).
            next.pData[c * cRows + r] = pPatch->controls.pData[r * cColumns + c];
        }
    }
    Vector_Shutdown( &pPatch->controls );
    Vector_Move( &pPatch->controls, &next );
    pPatch->cColumns = cRows;
    pPatch->cRows = cColumns;
    return geometry_status_t::OK;
}

void Patch_ReverseColumns( patch_surface_t *pPatch ) noexcept
{
    if ( !HasValidLayout( pPatch ) ) { return; }
    for ( u32 r = 0u; r < pPatch->cRows; ++r ) {
        patch_control_t *pRow = pPatch->controls.pData + static_cast<usize>( r ) * pPatch->cColumns;
        std::reverse( pRow, pRow + pPatch->cColumns );
    }
}

geometry_status_t Patch_TryTransform( patch_surface_t *pPatch, const math::affine3d_t &transform ) noexcept
{
    const geometry_status_t layout = RequireLayout( pPatch );
    if ( layout != geometry_status_t::OK ) { return layout; }
    if ( !math::Affine3d_IsFinite( transform ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    // Check pass first so a failing control leaves the patch untouched
    // without needing a scratch copy of the grid.
    for ( usize i = 0u; i < pPatch->controls.nCount; ++i ) {
        if ( !PositionInRange( math::Affine3d_TransformPoint( transform, pPatch->controls.pData[i].position ) ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    for ( usize i = 0u; i < pPatch->controls.nCount; ++i ) {
        pPatch->controls.pData[i].position =
            math::Affine3d_TransformPoint( transform, pPatch->controls.pData[i].position );
    }
    return geometry_status_t::OK;
}

patch_validation_result_t Patch_Validate( const patch_surface_t *pPatch, const allocator_t *pScratchAllocator ) noexcept
{
    patch_validation_result_t r{};
    if ( pPatch == nullptr || IsCanonicalEmpty( *pPatch ) ) {
        r.fault = patch_fault_t::NOT_INITIALIZED;
        return r;
    }
    const u64 cExpected = static_cast<u64>( pPatch->cColumns ) *
                          static_cast<u64>( pPatch->cRows );
    if ( !Vector_IsValid( &pPatch->controls ) || pPatch->controls.pAllocator == nullptr ||
         !Patch_IsValidAxisCount( pPatch->basis, pPatch->cColumns ) ||
         !Patch_IsValidAxisCount( pPatch->basis, pPatch->cRows ) ||
         cExpected > kPatchControlsMax ||
         cExpected != static_cast<u64>( pPatch->controls.nCount ) ) {
        r.fault = patch_fault_t::INVALID_DIMENSIONS;
        return r;
    }
    if ( !GeometrySourceId_IsValid( pPatch->sourceId ) ) {
        r.fault = patch_fault_t::INVALID_SOURCE_ID;
        return r;
    }
    const u32 cControls = static_cast<u32>( pPatch->controls.nCount );
    for ( u32 i = 0u; i < cControls; ++i ) {
        const patch_control_t &c = pPatch->controls.pData[i];
        if ( !GeometrySourceId_IsValid( c.sourceId ) ) {
            r.fault = patch_fault_t::INVALID_SOURCE_ID;
            r.iControl = i;
            return r;
        }
        if ( c.sourceId.value == pPatch->sourceId.value ) {
            r.fault = patch_fault_t::DUPLICATE_SOURCE_ID;
            r.iControl = i;
            return r;
        }
        if ( !math::Vec3d_IsFinite( c.position ) || !math::Vec2d_IsFinite( c.uv ) ) {
            r.fault = patch_fault_t::NON_FINITE;
            r.iControl = i;
            return r;
        }
        if ( !PositionInRange( c.position ) || !UvInRange( c.uv ) ) {
            r.fault = patch_fault_t::COORDINATE_RANGE;
            r.iControl = i;
            return r;
        }
    }

    // Duplicate IDs: sort (id, index) pairs so the reported control is the
    // later one in grid order, independent of the sort's internal order.
    struct id_entry_t {
        u64 id;
        u32 index;
    };
    vector_t<id_entry_t> entries{};
    if ( !Allocator_IsValid( pScratchAllocator ) || !Vector_Init( &entries, pScratchAllocator ) ||
         !Vector_Resize( &entries, cControls ) ) {
        Vector_Shutdown( &entries );
        // Scratch failure is reported as the patch being unverifiable rather
        // than silently passing.
        r.fault = patch_fault_t::VALIDATION_INCOMPLETE;
        return r;
    }
    for ( u32 i = 0u; i < cControls; ++i ) {
        entries.pData[i] = id_entry_t{ pPatch->controls.pData[i].sourceId.value, i };
    }
    std::sort( entries.pData, entries.pData + cControls, []( const id_entry_t &a, const id_entry_t &b ) {
        return a.id != b.id ? a.id < b.id : a.index < b.index;
    } );
    for ( u32 i = 1u; i < cControls; ++i ) {
        if ( entries.pData[i].id == entries.pData[i - 1u].id ) {
            r.fault = patch_fault_t::DUPLICATE_SOURCE_ID;
            r.iControl = entries.pData[i].index;
            Vector_Shutdown( &entries );
            return r;
        }
    }
    Vector_Shutdown( &entries );

    const math::vec3d_t first = pPatch->controls.pData[0].position;
    bool bAllSame = true;
    for ( u32 i = 1u; i < cControls && bAllSame; ++i ) {
        bAllSame = math::Vec3d_EqualsExact( pPatch->controls.pData[i].position, first );
    }
    if ( bAllSame ) { r.fault = patch_fault_t::COLLAPSED_SURFACE; }
    return r;
}

} // namespace cypher::editor::geometry
