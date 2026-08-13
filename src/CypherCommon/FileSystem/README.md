<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/FileSystem/README.md
//  Purpose: Documents the CypherCommon FileSystem folder.
//  Details: FileSystem contains public VFS interfaces, mount descriptors, file
//           flags, stream contracts, and package-facing declarations.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# FileSystem

`FileSystem` owns the provider-neutral read-only VFS contract and reusable VFS
providers that can be linked independently.

- `Cypher::VirtualFileSystem` exposes canonical virtual paths and callback-based
  read, stat, enumerate, and diagnostic-path operations.
- `Cypher::VfsDirectory` adapts one native loose-file root to that contract for
  development tools, tests, and source asset hosts.

Package mounts, native file watches, asynchronous streaming, writable project
filesystems, and runtime mount policy remain separate owning modules. A consumer
that only needs VFS reads must not depend on the directory provider or native
filesystem paths.
