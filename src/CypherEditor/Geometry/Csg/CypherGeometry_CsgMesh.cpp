//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgMesh.cpp
//  Purpose: Implements the general mesh Boolean pipeline.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgMesh.h"
#include "CypherGeometry_CsgBoundary.h"
#include "CypherGeometry_CsgBroadPhase.h"
#include "CypherGeometry_CsgCells.h"
#include "CypherGeometry_CsgClassify.h"
#include "CypherGeometry_CsgCleanup.h"
#include "CypherGeometry_CsgCoplanar.h"
#include "CypherGeometry_CsgCorefine.h"
#include "CypherGeometry_CsgExpression.h"
#include "CypherGeometry_CsgInput.h"
#include "CypherGeometry_CsgIntersections.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct pipeline_t {
    csg_operand_t a{}, b{};
    vector_t<csg_pair_t> pairs{};
    csg_intersection_t x{};
    vector_t<csg_refined_triangle_t> refined{};
    vector_t<csg_label_t> labels{};
    csg_cell_complex_t cells{};
    vector_t<csg_boundary_triangle_t> boundary{};

    bool Init( const allocator_t *pA ) noexcept
    {
        return CsgOperand_Init( &a, pA ) == geometry_status_t::OK && CsgOperand_Init( &b, pA ) == geometry_status_t::OK && Vector_Init( &pairs, pA ) &&
               CsgIntersection_Init( &x, pA ) == geometry_status_t::OK && Vector_Init( &refined, pA ) && Vector_Init( &labels, pA ) &&
               CsgCells_Init( &cells, pA ) == geometry_status_t::OK && Vector_Init( &boundary, pA );
    }
    void Shutdown() noexcept
    {
        CsgOperand_Shutdown( &a );
        CsgOperand_Shutdown( &b );
        CsgIntersection_Shutdown( &x );
        CsgCells_Shutdown( &cells );
    }
};

} // namespace

geometry_status_t CsgMesh_TryEvaluate( const mesh_source_t *pA, const mesh_source_t *pB, const csg_mesh_options_t &o, csg_mesh_result_t *pResult,
                                       csg_diagnostics_t *pDiag ) noexcept
{
    if ( pA == nullptr || pB == nullptr || pResult == nullptr || pResult->faceOperand.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( static_cast<u32>( o.op ) > static_cast<u32>( csg_operator_t::CLIP ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    csg_diagnostics_t localDiag{};
    csg_diagnostics_t &diag = pDiag != nullptr ? *pDiag : localDiag;
    diag = csg_diagnostics_t{};
    const allocator_t *pAlloc = pResult->faceOperand.pAllocator;
    CsgResult_Clear( pResult );
    pipeline_t p{};
    geometry_status_t st = p.Init( pAlloc ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
    auto fail = [&]( csg_stage_t stage ) noexcept {
        diag.status = st;
        if ( diag.stage == csg_stage_t::NONE ) { diag.stage = stage; }
        CsgResult_Clear( pResult );
        p.Shutdown();
        return st;
    };
    if ( st != geometry_status_t::OK ) { return fail( csg_stage_t::INPUT ); }

    // Input.
    st = CsgInput_TryFromMeshSource( pA, &p.a );
    if ( st == geometry_status_t::OK ) { st = CsgInput_TryFromMeshSource( pB, &p.b ); }
    if ( st == geometry_status_t::OK && o.quantizeStep > 0.0 ) {
        st = CsgInput_TryQuantize( &p.a, o.quantizeStep );
        if ( st == geometry_status_t::OK ) { st = CsgInput_TryQuantize( &p.b, o.quantizeStep ); }
    }
    if ( st == geometry_status_t::OK ) {
        CsgInput_Canonicalize( &p.a );
        CsgInput_Canonicalize( &p.b );
        if ( ( CsgExpression_NeedsClosed( o.op, kCsgOperandA ) && !p.a.bClosed ) || ( CsgExpression_NeedsClosed( o.op, kCsgOperandB ) && !p.b.bClosed ) ) {
            st = geometry_status_t::OPEN_VOLUME;
        }
    }
    if ( st != geometry_status_t::OK ) { return fail( csg_stage_t::INPUT ); }

    st = CsgBroadPhase_TryCollect( &p.a, &p.b, o.cPairsMax, &p.pairs );
    diag.cCandidatePairs = p.pairs.nCount;
    if ( st != geometry_status_t::OK ) { return fail( csg_stage_t::BROAD_PHASE ); }

    st = CsgIntersection_TryCompute( &p.a, &p.b, p.pairs, &p.x, &diag );
    if ( st != geometry_status_t::OK ) { return fail( csg_stage_t::INTERSECTION ); }

    usize cRefinedA = 0u;
    st = CsgCorefine_TryRefine( &p.a, &p.b, &p.x, &p.refined, &cRefinedA, &diag );
    if ( st != geometry_status_t::OK ) { return fail( csg_stage_t::COREFINEMENT ); }

    st = CsgCoplanar_TryMatch( p.refined, cRefinedA, &p.labels, nullptr );
    if ( st == geometry_status_t::OK ) { st = CsgCells_TryBuild( p.refined, cRefinedA, &p.cells ); }
    if ( st == geometry_status_t::OK ) {
        // CLIP never keeps B's surface, and B's cells would need A closed.
        const bool bClassifyB = o.op != csg_operator_t::CLIP;
        st = CsgClassify_TryLabel( p.refined, p.labels, &p.x, &p.a, &p.b, true, bClassifyB, &p.cells, &diag );
    }
    if ( st != geometry_status_t::OK ) { return fail( csg_stage_t::CLASSIFICATION ); }

    st = CsgBoundary_TryExtract( p.refined, &p.cells, p.x.positions, o.op, &p.boundary, nullptr );
    if ( st != geometry_status_t::OK ) { return fail( csg_stage_t::EXTRACTION ); }

    st = CsgReconstruct_TryBuild( p.boundary, &p.x, &p.a, &p.b, o.attributes, pResult, &diag );
    if ( st == geometry_status_t::OK ) { pResult->mesh.sourceId = p.a.rootId; }
    if ( st == geometry_status_t::OK && o.bRemoveRedundantVertices ) { st = CsgCleanup_TryRemoveRedundantVertices( pResult, nullptr ); }
    if ( st == geometry_status_t::OK ) { st = CsgCleanup_TryValidate( pResult, CsgExpression_ResultClosed( o.op ), &diag ); }
    if ( st != geometry_status_t::OK ) { return fail( csg_stage_t::RECONSTRUCTION ); }
    diag.status = geometry_status_t::OK;
    p.Shutdown();
    return geometry_status_t::OK;
}

geometry_status_t CsgMesh_TryBuild( const mesh_source_t *pA, const mesh_source_t *pB, const csg_mesh_options_t &options, const allocator_t *pAllocator,
                                    geometry_source_id_allocator_t *pIdAllocator, mesh_source_t *pOut, csg_diagnostics_t *pDiag ) noexcept
{
    if ( pIdAllocator == nullptr || pOut == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    csg_mesh_result_t r{};
    geometry_status_t st = CsgResult_Init( &r, pAllocator );
    if ( st == geometry_status_t::OK ) { st = CsgMesh_TryEvaluate( pA, pB, options, &r, pDiag ); }
    if ( st == geometry_status_t::OK && r.mesh.faces.nCount == 0u ) { st = geometry_status_t::DEGENERATE; }
    geometry_source_id_allocator_t ids = *pIdAllocator;
    auto fresh = [&]( geometry_source_id_t *pId ) noexcept {
        if ( st != geometry_status_t::OK || GeometrySourceId_IsValid( *pId ) ) { return; }
        const geometry_source_id_result_t n = GeometrySourceIdAllocator_Allocate( &ids );
        st = n.status;
        *pId = n.id;
    };
    fresh( &r.mesh.sourceId );
    for ( usize v = 0u; v < r.mesh.vertices.nCount; ++v ) { fresh( &r.mesh.vertices.pData[v].sourceId ); }
    for ( usize f = 0u; f < r.mesh.faces.nCount; ++f ) { fresh( &r.mesh.faces.pData[f].sourceId ); }
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryBuild( &r.mesh, pAllocator, pOut ); }
    if ( st == geometry_status_t::OK ) { *pIdAllocator = ids; }
    CsgResult_Shutdown( &r );
    return st;
}

} // namespace cypher::editor::geometry
