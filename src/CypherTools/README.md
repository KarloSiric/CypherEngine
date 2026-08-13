<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/README.md
//  Purpose: Defines ownership and naming for Cypher tool products.
//  Details: This source root holds executable products and product-specific core
//           libraries. Shared host-neutral contracts live in ToolFramework; the
//           authoritative capability inventory remains in docs/tool_suite.md.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherTools

`CypherTools` contains product code. A directory name represents a real public
tool or a focused compiler module, not a generic implementation category.

## Ownership

- `CypherCommon/ToolFramework` owns shared descriptors, invocations,
  diagnostics, progress, reports, CLI process mechanics, and host callbacks.
- `Cypher*Compiler` directories own format-specific import, validation, and
  cooking behavior.
- `CypherResourceCompiler` coordinates registered compiler modules; it does not
  contain every compiler implementation.
- `Mason` owns Qt application composition and workspaces. Compiler logic must
  remain callable without Qt.
- `CypherScope` and other focused applications own their user workflows while
  reusing the same neutral contracts.

Placeholder directories record accepted product boundaries only. They are not
build targets and do not imply implementation completion. Add a product to
`CMakeLists.txt` explicitly once it has real source files and focused tests.

The first registered products are `CypherShaderCompiler`, a host-neutral
`.cyshader` compiler module, and `CypherResourceCompiler`, its command-line host.
The CLI currently supports validation and compilation, live compiler and format
discovery, VFS-backed files/directories/wildcards, repeatable inputs, response
files, zsh completion generation, text or JSON records, terminal-aware ANSI
color, aggregate progress and reports, cancellation, and stable exit classes.

See [the tool suite inventory](../../docs/tool_suite.md) for product status,
delivery mode, and planned responsibility. See the
[ResourceCompiler CLI reference](../../docs/CYPHER_RESOURCE_COMPILER.md) for
the exact version 1 command contract and deliberately deferred capabilities.
