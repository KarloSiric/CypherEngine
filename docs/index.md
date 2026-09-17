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

1. [CYPHERENGINE_REFERENCE_MANUAL.md](CYPHERENGINE_REFERENCE_MANUAL.md)
2. [current_status.md](current_status.md)
3. [six_month_engine_plan.md](six_month_engine_plan.md)
4. [master_plan.md](master_plan.md)
5. [development_phases.md](development_phases.md)
6. [roadmap.md](roadmap.md)
7. [project_structure.md](project_structure.md)
8. [architecture.md](architecture.md)
9. [cyphercommon_architecture.md](cyphercommon_architecture.md)
10. [function_pointer_policy.md](function_pointer_policy.md)
11. [subsystems.md](subsystems.md)
12. [subsystem_source_catalog.md](subsystem_source_catalog.md)
13. [toolchain_plan.md](toolchain_plan.md)
14. [tool_suite.md](tool_suite.md)
15. [source2_tooling_reference.md](source2_tooling_reference.md)
16. [map_authoring_and_mason.md](map_authoring_and_mason.md)
17. [trenchbroom_editor_systems_research.md](trenchbroom_editor_systems_research.md)
18. [picasso_v1_design.md](picasso_v1_design.md)
19. [formats/CYKV.md](formats/CYKV.md)
20. [formats/CYKV_2_PROPOSAL.md](formats/CYKV_2_PROPOSAL.md)
21. [formats/INPUT_ACTIONS.md](formats/INPUT_ACTIONS.md)
22. [reference_engine_lessons.md](reference_engine_lessons.md)
23. [security_model.md](security_model.md)

API docs:

- [CYPHERENGINE_API_REFERENCE.md](CYPHERENGINE_API_REFERENCE.md)
- [CYPHERENGINE_API_IMPLEMENTATION.md](CYPHERENGINE_API_IMPLEMENTATION.md)
- [CYPHER_RESOURCE_COMPILER.md](CYPHER_RESOURCE_COMPILER.md)

Reference docs:

- [CYPHERENGINE_REFERENCE_MANUAL.md](CYPHERENGINE_REFERENCE_MANUAL.md)
- [build_guide.md](build_guide.md)
- [coding_style.md](coding_style.md)
- [reference_policy.md](reference_policy.md)
- [reference_engine_lessons.md](reference_engine_lessons.md)
- [trenchbroom_editor_systems_research.md](trenchbroom_editor_systems_research.md)
- [trenchbroom_geometry_algorithms.md](trenchbroom_geometry_algorithms.md)
- [trenchbroom_ui_command_inventory.md](trenchbroom_ui_command_inventory.md)
- [editor_comparison_beyond_trenchbroom.md](editor_comparison_beyond_trenchbroom.md)

Project memory:

- [Non-renderer pool validation, 2026-09-16](non_renderer_validation_2026-09-16.md)
- [Non-renderer runtime work log](non_renderer_work_log.md)

- [../CHANGELOG.md](../CHANGELOG.md)
- [devlog/2026-04.md](devlog/2026-04.md)
- [adr/0001-coop-first-listen-server-architecture.md](adr/0001-coop-first-listen-server-architecture.md)
- [adr/0002-common-runtime-tool-boundaries.md](adr/0002-common-runtime-tool-boundaries.md)
- [adr/0003-runtime-naming-and-target-ownership.md](adr/0003-runtime-naming-and-target-ownership.md)

## What each document is for

- `CYPHERENGINE_REFERENCE_MANUAL`
  - primary public reference for engine architecture, data lifecycle, formats,
    versions, limits, toolchain, diagnostics, security, and compatibility
  - complete format catalog separating implemented, partial, planned, and
    proposed behavior
  - contributor checklists, glossary, source-of-truth links, and external lessons
- `current_status`
  - what is active now
  - what is done-for-now
  - what is intentionally deferred
- `six_month_engine_plan`
  - evidence-backed audit of the current repository and subsystem folders
  - active renderer sequence from cooked shaders through the first cube and materials
  - vertical-slice dependencies, month gates, risks, and realistic outcomes
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
- `subsystem_source_catalog`
  - concrete planned implementation-unit names for every top-level subsystem
  - source-file creation gates and dependency ownership rules
  - vertical-slice order without empty placeholder compilation units
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
- `trenchbroom_editor_systems_research`
  - pinned source-level study of TrenchBroom's map-editing architecture,
    documented authoring surface, and implementation behavior
  - TileEditor and Mason capability mapping, staged implementation programs,
    subsystem contracts, build workflow, robustness requirements, and gates
  - explicit non-clean-room provenance and engineering boundary for studying
    `GPL-3.0-or-later` reference code; distribution still requires legal review
- `trenchbroom_geometry_algorithms`
  - exhaustive convex-solid, clipping, CSG, transform, topology-editing, shape,
    patch, UV, picking, snapping, transaction, and geometry-test audit
  - pinned implementation/test references plus an original Cypher architecture
    and verification plan
- `trenchbroom_ui_command_inventory`
  - exact menu, toolbar, panel, inspector, preference, dialog, context-menu,
    status, validation, compilation, and launch inventory
  - all 179 static user-facing commands with default bindings and pinned source
    references, plus contextual behavior and all audited pointer/UV gestures
- `editor_comparison_beyond_trenchbroom`
  - primary-source comparison with Hammer/Source 2, Dota's tile editor,
    GtkRadiant/Q3Radiant, J.A.C.K., Blender, and Unity ProBuilder
  - semantic tile, prefab, mesh, terrain, path, projector, build-diagnostic,
    collaboration, adoption, and deferral lessons for TileEditor and Mason
- `picasso_v1_design`
  - approved Qt 6 Texture and Material workspace composition
  - Hammer-influenced dark visual language and semantic accent roles
  - editor-core, compiler, resource, preview, and Qt ownership boundaries
  - V1 implementation gate, non-goals, and acceptance criteria
- `formats/CYKV`
  - normative CYKV 1 grammar and semantic rules
  - document headers, comments, scalar types, canonical output, and limits
  - boundary between Tier1 parsing and Tier2 schema validation
- `formats/CYKV_2_PROPOSAL`
  - researched proposal for bounded includes, base composition, typed constants,
    build conditionals, exact numeric intent, optional non-finite values,
    provenance, hashing, limits, and a self-identifying binary generation
  - explicit separation from implemented CYKV 1 behavior
- `formats/INPUT_ACTIONS`
  - proposed `.cyinput`, `.cyinput_c`/`CYIN`, and `.cybindings` family
  - action, context, control, trigger, processor, conflict, accessibility,
    gamepad, user-override, compiler, runtime, and migration contracts
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
