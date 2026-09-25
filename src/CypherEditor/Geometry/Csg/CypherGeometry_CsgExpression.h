//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgExpression.h
//  Purpose: Declares the CSG region decision: for an operator, which
//           labelled cells of which operand survive into the result, and
//           which survive turned inside out.
//  Details: One table decides every operator, so they cannot drift apart:
//
//             operand/label   UNION  INTERSECT  A-B    SYM-DIFF  CLIP
//             A outside       keep   -          keep   keep      keep
//             A inside        -      keep       -      flip      -
//             A shared-same   keep   keep       -      -         keep
//             A shared-opp    -      -          keep   -         keep
//             B outside       keep   -          -      keep      -
//             B inside        -      keep       flip   flip      -
//             B shared-*      -      -          -      -         -
//
//           Shared pieces exist once in each operand, so at most one copy
//           (A's) is ever kept: flush faces facing the same way bound the
//           union and the intersection; faces touching face to face (opposite
//           facing) vanish from a union - the solids merge - but still bound
//           A - B, where B takes no volume from A there.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_EXPRESSION_H
#define CYPHER_EDITOR_GEOMETRY_CSG_EXPRESSION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgTypes.h"

namespace cypher::editor::geometry
{

enum class csg_decision_t : common::u8 {
    DROP = 0u,
    KEEP,
    FLIP // keep with reversed winding
};

CYPHER_NODISCARD csg_decision_t CsgExpression_Decide( csg_operator_t op, common::u32 iOperand, csg_label_t label ) noexcept;

// Whether the operator needs each operand closed (a winding number of an
// open operand means nothing). CLIP needs only B closed.
CYPHER_NODISCARD bool CsgExpression_NeedsClosed( csg_operator_t op, common::u32 iOperand ) noexcept;

// Whether the result is expected to be a closed solid (all but CLIP).
CYPHER_NODISCARD bool CsgExpression_ResultClosed( csg_operator_t op ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_EXPRESSION_H
