//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgExpression.cpp
//  Purpose: Implements the CSG region decision table.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgExpression.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

csg_decision_t CsgExpression_Decide( csg_operator_t op, u32 iOperand, csg_label_t label ) noexcept
{
    using d = csg_decision_t;
    using l = csg_label_t;
    const bool bA = iOperand == kCsgOperandA;
    // Rows: OUTSIDE, INSIDE, SHARED_SAME, SHARED_OPPOSITE.
    static constexpr d kA[5][4] = {
        { d::KEEP, d::DROP, d::KEEP, d::DROP }, // UNION
        { d::DROP, d::KEEP, d::KEEP, d::DROP }, // INTERSECTION
        { d::KEEP, d::DROP, d::DROP, d::KEEP }, // DIFFERENCE
        { d::KEEP, d::FLIP, d::DROP, d::DROP }, // SYMMETRIC_DIFFERENCE
        { d::KEEP, d::DROP, d::KEEP, d::KEEP }, // CLIP
    };
    static constexpr d kB[5][4] = {
        { d::KEEP, d::DROP, d::DROP, d::DROP },
        { d::DROP, d::KEEP, d::DROP, d::DROP },
        { d::DROP, d::FLIP, d::DROP, d::DROP },
        { d::KEEP, d::FLIP, d::DROP, d::DROP },
        { d::DROP, d::DROP, d::DROP, d::DROP },
    };
    const u32 row = static_cast<u32>( op );
    if ( row > 4u ) { return d::DROP; }
    u32 col = 0u;
    switch ( label ) {
    case l::OUTSIDE: col = 0u; break;
    case l::INSIDE: col = 1u; break;
    case l::SHARED_SAME: col = 2u; break;
    case l::SHARED_OPPOSITE: col = 3u; break;
    default: return d::DROP;
    }
    return bA ? kA[row][col] : kB[row][col];
}

bool CsgExpression_NeedsClosed( csg_operator_t op, u32 iOperand ) noexcept { return op != csg_operator_t::CLIP || iOperand == kCsgOperandB; }

bool CsgExpression_ResultClosed( csg_operator_t op ) noexcept { return op != csg_operator_t::CLIP; }

} // namespace cypher::editor::geometry
