<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/build_guide.md
//  Purpose: Documents build guide.
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

# Build Guide

## Current build path

The repository currently builds through CMake.

`CypherEngine` currently builds as the `cypherengine` executable target.

```bash
cmake -S . -B build
cmake --build build
```

Current executable:

```bash
./build/bin/cypherengine
```

## Planned convenience layer

A top-level `build.sh` will be introduced as a thin wrapper so the day-to-day build flow stays simple over the life of the project.

That script should remain:
- thin
- explicit
- a wrapper around the real build system

It should not replace the real build configuration.

## Long-term build picture

The intended full project has multiple build bodies:

- engine runtime
- standalone `rvm`
- game scripts
- tools

That means the eventual top-level build flow must account for:
- runtime compilation
- VM compilation
- script compilation
- asset pipeline invocation

## Current rule

Use the simplest build path that supports the current milestone.
