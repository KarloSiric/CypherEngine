//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_RBTree_Tests.cpp
//  Purpose: Tests allocator-backed red-black ordered trees.
//  Details: Validates ordering, parent links, root color, red-node rules, equal black
//           heights, stable nodes, lower bounds, duplicates, erasure, and allocation failure.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_RBTree.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct tree_validation_t {
    bool_t bValid{ CY_TRUE };
    usize nNodes{ 0u };
    usize nBlackHeight{ 1u };
};

tree_validation_t ValidateSubtree(
    const rb_tree_node_t<u32, u32> *pNode,
    const rb_tree_node_t<u32, u32> *pParent,
    bool_t bHasMinimum,
    u32 nMinimum,
    bool_t bHasMaximum,
    u32 nMaximum ) noexcept
{
    if ( pNode == nullptr ) {
        return {};
    }

    tree_validation_t result{};
    if ( pNode->pParent != pParent ||
         ( bHasMinimum && pNode->key <= nMinimum ) ||
         ( bHasMaximum && pNode->key >= nMaximum ) ) {
        result.bValid = CY_FALSE;
        return result;
    }
    if ( pNode->color == rb_tree_color_t::RED &&
         ( ( pNode->pLeft != nullptr &&
             pNode->pLeft->color == rb_tree_color_t::RED ) ||
           ( pNode->pRight != nullptr &&
             pNode->pRight->color == rb_tree_color_t::RED ) ) ) {
        result.bValid = CY_FALSE;
        return result;
    }

    const tree_validation_t left = ValidateSubtree(
        pNode->pLeft,
        pNode,
        bHasMinimum,
        nMinimum,
        CY_TRUE,
        pNode->key );
    const tree_validation_t right = ValidateSubtree(
        pNode->pRight,
        pNode,
        CY_TRUE,
        pNode->key,
        bHasMaximum,
        nMaximum );
    result.bValid = left.bValid &&
                    right.bValid &&
                    left.nBlackHeight == right.nBlackHeight;
    result.nNodes = left.nNodes + right.nNodes + 1u;
    result.nBlackHeight = left.nBlackHeight +
        ( pNode->color == rb_tree_color_t::BLACK ? 1u : 0u );
    return result;
}

bool_t ValidateTree( const rb_tree_t<u32, u32> &tree ) noexcept
{
    if ( tree.pRoot == nullptr ) {
        return tree.nCount == 0u;
    }
    if ( tree.pRoot->color != rb_tree_color_t::BLACK ||
         tree.pRoot->pParent != nullptr ) {
        return CY_FALSE;
    }
    const tree_validation_t validation = ValidateSubtree(
        tree.pRoot,
        nullptr,
        CY_FALSE,
        0u,
        CY_FALSE,
        0u );
    return validation.bValid && validation.nNodes == tree.nCount;
}

void *FailTreeAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

} // namespace

TEST_CASE( "RBTree preserves all balancing invariants through insertion",
           "[CypherCommon][Tier1][RBTree]" )
{
    rb_tree_t<u32, u32> tree{};
    REQUIRE( RBTree_Init( &tree, Allocator_GetSystem() ) );

    for ( u32 iKey = 1u; iKey <= 256u; ++iKey ) {
        const u32 nKey = ( iKey * 73u ) % 257u;
        const rb_tree_insert_result_t<u32, u32> result =
            RBTree_Insert( &tree, nKey, nKey * 10u );
        REQUIRE( result.bInserted );
        REQUIRE( result.pNode != nullptr );
        if ( ( iKey & 15u ) == 0u ) {
            REQUIRE( ValidateTree( tree ) );
        }
    }
    REQUIRE( RBTree_Count( &tree ) == 256u );
    REQUIRE( ValidateTree( tree ) );

    u32 nExpected = 1u;
    for ( const rb_tree_node_t<u32, u32> *pNode = RBTree_First( tree.pRoot );
          pNode != nullptr;
          pNode = RBTree_Next( pNode ) ) {
        REQUIRE( pNode->key == nExpected );
        REQUIRE( pNode->value == nExpected * 10u );
        ++nExpected;
    }
    REQUIRE( nExpected == 257u );

    const rb_tree_insert_result_t<u32, u32> duplicate =
        RBTree_Insert( &tree, 42u, 9999u );
    REQUIRE_FALSE( duplicate.bInserted );
    REQUIRE( duplicate.pNode->value == 420u );
    REQUIRE( RBTree_LowerBound( &tree, 42u )->key == 42u );
    REQUIRE( RBTree_LowerBound( &tree, 0u )->key == 1u );
    REQUIRE( RBTree_LowerBound( &tree, 257u ) == nullptr );
}

TEST_CASE( "RBTree erasure preserves invariants for every structural case",
           "[CypherCommon][Tier1][RBTree]" )
{
    rb_tree_t<u32, u32> tree{};
    REQUIRE( RBTree_Init( &tree, Allocator_GetSystem() ) );
    for ( u32 nKey = 1u; nKey <= 256u; ++nKey ) {
        REQUIRE( RBTree_Insert( &tree, nKey, nKey ).bInserted );
    }

    for ( u32 nKey = 1u; nKey <= 255u; nKey += 2u ) {
        REQUIRE( RBTree_Erase( &tree, nKey ) );
        REQUIRE( ValidateTree( tree ) );
    }
    for ( u32 nKey = 2u; nKey <= 256u; nKey += 2u ) {
        REQUIRE( RBTree_Erase( &tree, nKey ) );
        REQUIRE( ValidateTree( tree ) );
    }
    REQUIRE( RBTree_IsEmpty( &tree ) );
    REQUIRE_FALSE( RBTree_Erase( &tree, 9u ) );
}

TEST_CASE( "RBTree keeps unrelated node addresses stable",
           "[CypherCommon][Tier1][RBTree]" )
{
    rb_tree_t<u32, u32> tree{};
    REQUIRE( RBTree_Init( &tree, Allocator_GetSystem() ) );
    for ( u32 nKey = 0u; nKey < 64u; ++nKey ) {
        REQUIRE( RBTree_Insert( &tree, nKey, nKey + 100u ).bInserted );
    }
    rb_tree_node_t<u32, u32> *pStable = RBTree_Find( &tree, 31u );
    REQUIRE( pStable != nullptr );

    for ( u32 nKey = 0u; nKey < 64u; nKey += 2u ) {
        if ( nKey != 31u ) {
            REQUIRE( RBTree_Erase( &tree, nKey ) );
        }
    }
    REQUIRE( RBTree_Find( &tree, 31u ) == pStable );
    REQUIRE( pStable->value == 131u );
    REQUIRE( ValidateTree( tree ) );
}

TEST_CASE( "RBTree allocation failure leaves an empty valid tree",
           "[CypherCommon][Tier1][RBTree]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    rb_tree_t<u32, u32> tree{};
    REQUIRE( RBTree_Init( &tree, &allocator ) );
    allocator.pfnAllocate = FailTreeAllocation;

    const rb_tree_insert_result_t<u32, u32> result =
        RBTree_Insert( &tree, 1u, 2u );
    REQUIRE( result.pNode == nullptr );
    REQUIRE_FALSE( result.bInserted );
    REQUIRE( RBTree_IsEmpty( &tree ) );
    REQUIRE( RBTree_IsValid( &tree ) );

    allocator.pfnAllocate = Allocator_GetSystem()->pfnAllocate;
}
