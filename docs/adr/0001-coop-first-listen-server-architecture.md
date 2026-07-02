<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/adr/0001-coop-first-listen-server-architecture.md
//  Purpose: Documents 0001 coop first listen server architecture.
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

# ADR 0001 — Coop-First Listen-Server Architecture

## Status
Accepted

## Context
REAP is an intentionally learn-first, solo-built but production-leaning project.  
The first serious release goal is a stable 4-player cooperative arena survival loop, not a single-player prototype with networking bolted on later.

## Decision
- The runtime is split into two product layers:
  - `rengine`: reusable runtime services.
  - `rgame`: REAP gameplay implementation.
- `rengine` must not depend on `rgame`.
- `rgame` may depend on `rengine`.
- First v1 networking target is listen-server, host-authoritative simulation.
- The release-default limit is 4 players.
- Networking is considered a v1 architecture requirement, not a post-launch feature.
- Client-side prediction is optional in v1 if it improves stability and readability to keep simple at first.

## Why this decision
- A coop-first architecture ensures game state, replication points, and session flow are correct from day one.
- Listen-server provides practical multiplayer with lower startup complexity than dedicated infrastructure.
- The dependency boundary prevents accidental coupling and keeps engine runtime reusable.

## Consequences
- Core runtime APIs (startup, frame, input, logging, asset/runtime services) must remain session-aware enough for host/client separation.
- Game state designs should include explicit identifiers for match, wave, player, and enemy ownership from early stages.
- Tooling and scripting/VM work are delayed until baseline coop loop is playable and stable.
- Future architecture evolution (dedicated-server, richer networking, advanced tooling) can grow from this boundary without core rewrites.

## Alternatives considered
- Single-player-first architecture with later networking.
  - Rejected: causes state ownership and replayed logic to be rewritten in later milestones.
- Dedicated-server-first architecture.
  - Deferred: too much infrastructure for first shipping milestone in this solo context.
- No-network v1.
  - Rejected: not aligned with target scope and scope statement.

## Follow-up
- Add a second ADR when transport/protocol decisions are locked (snapshot cadence, packet priorities, host migration strategy if later introduced).
