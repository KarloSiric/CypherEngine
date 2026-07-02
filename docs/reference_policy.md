<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/reference_policy.md
//  Purpose: Documents reference policy.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-04-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Engine Reference Policy

CypherEngine is structurally inspired by classic engine architecture, especially
idTech/Quake, GoldSrc/Source, and early CryEngine.

That inspiration is allowed and intentional.

## What is allowed

- studying subsystem boundaries
- studying gameplay/runtime architecture
- studying editor/runtime/tool separation
- studying naming conventions
- studying public interface layers
- studying patterns like:
  - command buffer
  - cvars
  - snapshots
  - BSP handling
  - VM boundaries
  - client/server split
  - resource compilers
  - world/map loading
  - editor object/property workflows
- re-deriving systems in original CypherEngine code

## What is not allowed by default

- copying source files wholesale
- rename-only ports
- blending copied code into original modules without provenance
- assuming license concerns disappear because the architecture is similar
- copying leaked/proprietary code or assets
- using non-commercial or leaked source trees as implementation sources
- copying code from reference repositories into CypherEngine because the
  architecture is useful

## Working rule

We can be heavily inspired by the shape and discipline of classic engines.

We still write CypherEngine’s implementation ourselves unless a later deliberate
decision is documented and license-reviewed.

If a reference tree is legally unclear, leaked, proprietary, or non-commercial,
it is study-only. Use it to understand boundaries, naming, update order,
toolchain shape, diagnostics, and subsystem responsibilities. Do not paste,
port, or mechanically translate its implementation.
