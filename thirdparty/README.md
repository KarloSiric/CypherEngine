<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: thirdparty/README.md
//  Purpose: Defines how external source and SDK dependencies enter CypherEngine.
//  Details: This policy keeps builds reproducible while preventing unreviewed source,
//           incompatible licenses, and unnecessary binary SDKs from accumulating in
//           the repository.
//
//  History:
//  - Created by Karlo Siric on 2026-08-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Third-Party Dependencies

CypherEngine uses three dependency acquisition paths. Every dependency remains
behind a Cypher-owned API; third-party types must not leak into stable engine,
game, tool, or plugin contracts.

## Vendored and pinned source

Small source integrations that require direct compilation or controlled patches
live here. Git submodule entries pin exact upstream commits.

| Dependency | Purpose | Source form |
| --- | --- | --- |
| GLAD | OpenGL 4.1 core loader | Generated source snapshot |
| Dear ImGui | Runtime diagnostics and development UI | `docking` submodule |
| cgltf | Focused glTF/GLB importer | Submodule |
| MikkTSpace | Consistent mesh tangent generation | Submodule |

Clone or restore these sources with:

```bash
git submodule update --init --recursive
```

Do not add CypherEngine copyright headers to upstream files. Local changes belong
in `thirdparty/patches/` and must record the upstream revision they modify.

## vcpkg packages

Compiled open-source libraries are declared in the repository root
`vcpkg.json`. The manifest baseline pins the vcpkg registry revision, and feature
groups prevent unrelated tools from inflating every runtime build.

Bootstrap the pinned vcpkg checkout with:

```bash
cmake -P cmake/CypherBootstrapVcpkg.cmake
```

Downloaded sources, package builds, and installed files live under `.deps/` or
`out/`; they are local build artifacts and are never committed.

## External SDKs

Qt 6, FMOD, and the Vulkan SDK are installed outside the repository and found by
their owning tool or backend. Their installers, source trees, headers, and binary
SDK packages must not be copied here. See `external/README.md` for the policy.

## Review requirements

Before a dependency becomes part of a shipping target, record its exact version,
license, linkage mode, notices, owning Cypher wrapper, supported platforms, and
update procedure in `THIRD_PARTY_NOTICES.md`.
