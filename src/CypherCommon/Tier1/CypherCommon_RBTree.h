//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_RBTree.h
//  Purpose: Declares allocator-backed red-black ordered trees.
//  Details: RBTree is the canonical ordered associative backend. It offers stable
//           node addresses and logarithmic lookup at the cost of per-node allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RBTREE_H
#define CYPHER_COMMON_TIER1_RBTREE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Functor.h"

namespace cypher::common
{

enum class rb_tree_color_t : u8 {
    RED = 0u,
    BLACK
};

template <typename key_t, typename value_t>
struct rb_tree_node_t {
    rb_tree_node_t *pParent{ nullptr };
    rb_tree_node_t *pLeft{ nullptr };
    rb_tree_node_t *pRight{ nullptr };
    rb_tree_color_t color{ rb_tree_color_t::RED };
    key_t key{};
    value_t value{};
};

template <typename key_t, typename value_t, typename compare_t = less_t<key_t>>
struct rb_tree_t {
    rb_tree_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( rb_tree_t );

    rb_tree_node_t<key_t, value_t> *pRoot{ nullptr };
    usize nCount{ 0u };
    const allocator_t *pAllocator{ nullptr };
    compare_t compare{};
};

template <typename key_t, typename value_t>
struct rb_tree_insert_result_t {
    rb_tree_node_t<key_t, value_t> *pNode{ nullptr };
    bool_t bInserted{ CY_FALSE };
};

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD bool_t RBTree_Init(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    const allocator_t *pAllocator,
    compare_t compare = {} ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
void RBTree_Shutdown(
    rb_tree_t<key_t, value_t, compare_t> *pTree ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
void RBTree_Clear(
    rb_tree_t<key_t, value_t, compare_t> *pTree ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD rb_tree_insert_result_t<key_t, value_t> RBTree_Insert(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key,
    const value_t &value ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD rb_tree_node_t<key_t, value_t> *RBTree_Find(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD const rb_tree_node_t<key_t, value_t> *RBTree_Find(
    const rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD rb_tree_node_t<key_t, value_t> *RBTree_LowerBound(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD const rb_tree_node_t<key_t, value_t> *RBTree_LowerBound(
    const rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD bool_t RBTree_Erase(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t>
CYPHER_NODISCARD rb_tree_node_t<key_t, value_t> *RBTree_First(
    rb_tree_node_t<key_t, value_t> *pRoot ) noexcept;

template <typename key_t, typename value_t>
CYPHER_NODISCARD const rb_tree_node_t<key_t, value_t> *RBTree_First(
    const rb_tree_node_t<key_t, value_t> *pRoot ) noexcept;

template <typename key_t, typename value_t>
CYPHER_NODISCARD rb_tree_node_t<key_t, value_t> *RBTree_Next(
    rb_tree_node_t<key_t, value_t> *pNode ) noexcept;

template <typename key_t, typename value_t>
CYPHER_NODISCARD const rb_tree_node_t<key_t, value_t> *RBTree_Next(
    const rb_tree_node_t<key_t, value_t> *pNode ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD usize RBTree_Count(
    const rb_tree_t<key_t, value_t, compare_t> *pTree ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD bool_t RBTree_IsEmpty(
    const rb_tree_t<key_t, value_t, compare_t> *pTree ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RBTREE_H
