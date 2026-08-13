<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/index.md
//  Purpose: Documents index.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-04-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine Documentation Hub

This is the navigation root for CypherEngine.

`CypherEngine` is the engine runtime and tools foundation. `REAP` is the current game direction being explored on top of it.

Read these in order when resuming work:

1. [current_status.md](current_status.md)
2. [master_plan.md](master_plan.md)
3. [development_phases.md](development_phases.md)
4. [roadmap.md](roadmap.md)
5. [project_structure.md](project_structure.md)
6. [architecture.md](architecture.md)
7. [cyphercommon_architecture.md](cyphercommon_architecture.md)
8. [function_pointer_policy.md](function_pointer_policy.md)
9. [subsystems.md](subsystems.md)
10. [toolchain_plan.md](toolchain_plan.md)
11. [tool_suite.md](tool_suite.md)
12. [source2_tooling_reference.md](source2_tooling_reference.md)
13. [map_authoring_and_mason.md](map_authoring_and_mason.md)
14. [formats/CYKV.md](formats/CYKV.md)
15. [reference_engine_lessons.md](reference_engine_lessons.md)
16. [security_model.md](security_model.md)

API docs:

- [CYPHERENGINE_API_REFERENCE.md](CYPHERENGINE_API_REFERENCE.md)
- [CYPHERENGINE_API_IMPLEMENTATION.md](CYPHERENGINE_API_IMPLEMENTATION.md)
- [CYPHER_RESOURCE_COMPILER.md](CYPHER_RESOURCE_COMPILER.md)

Reference docs:

- [build_guide.md](build_guide.md)
- [coding_style.md](coding_style.md)
- [reference_policy.md](reference_policy.md)
- [reference_engine_lessons.md](reference_engine_lessons.md)

Project memory:

- [../CHANGELOG.md](../CHANGELOG.md)
- [devlog/2026-04.md](devlog/2026-04.md)
- [adr/0001-coop-first-listen-server-architecture.md](adr/0001-coop-first-listen-server-architecture.md)

## What each document is for

- `current_status`
  - what is active now
  - what is done-for-now
  - what is intentionally deferred
- `master_plan`
  - the full long-term implementation schedule
  - concrete near-term dates
  - subsystem LOC ranges
  - runtime, toolchain, editor, and game progression
- `development_phases`
  - the current build order
  - what should be implemented next and why
- `roadmap`
  - compact version of the larger build sequence
- `project_structure`
  - the intended full repo layout
- `architecture`
  - top-level boundaries
  - ownership rules
- `cyphercommon_architecture`
  - Common/public layer definition
  - custom runtime foundation direction
  - public contract folders
  - function pointer policy
  - format and third-party dependency policy
- `function_pointer_policy`
  - C-style interface tables
  - subsystem communication rules
  - where direct calls, handles, command queues, event queues, and callback
    tables belong
- `subsystems`
  - what each module is responsible for
- `toolchain_plan`
  - how maps, models, archives, scripts, and tools should be introduced
- `tool_suite`
  - complete Qt 6 application and Mason workspace inventory
  - headless compiler, validator, inspector, and build-tool inventory
  - authoritative product, executable, library-target, and naming rules
  - Source 1 capability comparison and working Cypher product names
  - implementation gates and acceptance criteria for every tool class
- `source2_tooling_reference`
  - surveyed Source 2 authoring tools and source/cooked format families
  - architectural lessons for Mason, compilers, previews, and diagnostics
  - Source 2 capability-to-Cypher mapping and explicit scope exclusions
  - MASON long-form naming and vertical implementation order
- `map_authoring_and_mason`
  - CYKV-backed map authoring direction
  - editable and cooked format families
  - `.cymap`, `CypherMapCompiler`, and `.cymap_c` architecture
  - hybrid brush, mesh, BSP, visibility, and world-compilation policy
  - Mason workspaces, editing model, validation, testing, and build order
- `formats/CYKV`
  - normative CYKV 1 grammar and semantic rules
  - document headers, comments, scalar types, canonical output, and limits
  - boundary between Tier1 parsing and Tier2 schema validation
- `CYPHER_RESOURCE_COMPILER`
  - exact version 1 commands, parameters, output modes, and exit codes
  - source-to-cooked shader flow and response-file syntax
  - staged command and compiler roadmap without advertising unimplemented flags
- `formats/CYKV_SCHEMAS`
  - Tier2 descriptor, registry, validation, and diagnostic contracts
  - separate `cypher.project` and `cypher.settings` schemas with typed decoders
- `reference_engine_lessons`
  - architecture lessons from reference engines
  - legal boundary for study-only source trees
  - practical lessons for VFS, memory, resources, renderer, world, tools and editor
- `security_model`
  - cryptographic primitive choices and Cypher-owned contracts
  - secret ownership, nonce, key lifecycle, and failure rules
  - boundaries with networking, packages, tools, and anti-cheat policy
- `CYPHERENGINE_API_REFERENCE`
  - the public engine-facing API surface that currently exists
- `CYPHERENGINE_API_IMPLEMENTATION`
  - how the current API is backed internally and where it still needs work
