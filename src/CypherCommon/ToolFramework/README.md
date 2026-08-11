<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/README.md
//  Purpose: Documents the CypherCommon ToolFramework folder.
//  Details: ToolFramework contains editor-neutral application, command-line,
//           progress, compiler, and editor/runtime bridge contracts.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# ToolFramework

`ToolFramework` is for contracts shared by Mason and command-line tools.

Tool executables, importers, compilers, cookers, and GUI tool windows belong in
tool modules outside Common. Qt widgets, dock panels, and Mason implementation
must not enter this shared contract layer.
