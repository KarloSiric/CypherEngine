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

/*
================
RB Tree Contract

The root is black, red nodes never have red children, and every root-to-leaf path has equal black
height. Insert and erase preserve those properties through local rotations. Node addresses remain
stable until that node is erased; Clear and Shutdown invalidate every node at once.
================
*/

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
    RED = 0u, // May not have a red parent or child.
    BLACK     // Contributes one unit to every descendant path's black height.
};

template <typename key_t, typename value_t>
struct rb_tree_node_t {
    rb_tree_node_t( const key_t &nodeKey, const value_t &nodeValue ) noexcept
        : key( nodeKey ), value( nodeValue )
    {
    }
    CYPHER_NO_COPY_MOVE( rb_tree_node_t );

    rb_tree_node_t *pParent{ nullptr };            // Null only for the root.
    rb_tree_node_t *pLeft{ nullptr };              // Keys ordered before this node.
    rb_tree_node_t *pRight{ nullptr };             // Keys ordered after this node.
    rb_tree_color_t color{ rb_tree_color_t::RED }; // Balancing metadata, not user-visible state.
    key_t key{};                                   // Immutable ordering identity after insertion.
    value_t value{};                               // Mapped payload owned by this node.
};

template <typename key_t, typename value_t, typename compare_t = less_t<key_t>>
struct rb_tree_t {
    rb_tree_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( rb_tree_t );
    ~rb_tree_t() noexcept;

    rb_tree_node_t<key_t, value_t> *pRoot{ nullptr }; // Top of the ordered tree.
    usize nCount{ 0u };                               // Number of allocated live nodes.
    const allocator_t *pAllocator{ nullptr };         // Allocates and releases individual nodes.
    compare_t compare{};                              // Strict weak ordering retained by the tree.
};

template <typename key_t, typename value_t>
struct rb_tree_insert_result_t {
    rb_tree_node_t<key_t, value_t> *pNode{ nullptr }; // Existing or newly inserted node.
    bool_t bInserted{ CY_FALSE };                     // False when an equivalent key already existed.
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
CYPHER_NODISCARD bool_t RBTree_IsValid(
    const rb_tree_t<key_t, value_t, compare_t> *pTree ) noexcept;

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

#ifndef CYPHER_COMMON_TIER1_RBTREE_INL
    #include "CypherCommon_RBTree.inl"
#endif

#endif // CYPHER_COMMON_TIER1_RBTREE_H
