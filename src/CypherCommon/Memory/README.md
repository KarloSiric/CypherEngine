<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Memory/README.md
//  Purpose: Documents the CypherCommon Memory folder.
//  Details: Memory contains common allocator interfaces, allocation descriptors,
//           memory-operation helpers, and memory diagnostic contracts.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Memory

`Memory` is for common memory contracts and small reusable memory primitives.

Allocator implementations, arenas, pools, and subsystem memory policy can live in
`CypherMemory`; shared allocator interfaces and descriptors can live here.
