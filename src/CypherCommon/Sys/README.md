<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Sys/README.md
//  Purpose: Documents the CypherCommon Sys folder.
//  Details: Sys contains low-level platform-facing contracts and wrappers shared
//           by runtime, tools, and editor code.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Sys

`Sys` is for platform-neutral system contracts.

Expected files include process, environment, dynamic library, system path, timer,
CPU, and OS identity wrappers. Platform-specific implementation belongs in the
owning backend source files.
