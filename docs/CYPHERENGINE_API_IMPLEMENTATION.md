<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/CYPHERENGINE_API_IMPLEMENTATION.md
//  Purpose: Documents CYPHERENGINE API IMPLEMENTATION.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-04-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine - API Implementation Guide
## Technical Documentation for Developers

---

**Version:** 0.1.0  
**Last Updated:** April 2026  
**Author:** Karlo Siric / REAP Project

*Technical documentation describing how the current CypherEngine runtime API is backed internally, how the current execution flow works, and where the largest implementation gaps still are.*

---

## Table of Contents

### PART I: INTRODUCTION AND CURRENT STATUS

#### 1. [Introduction](#1-introduction)
- [1.1 Purpose and Scope](#11-purpose-and-scope)
- [1.2 Target Audience](#12-target-audience)
- [1.3 Naming and Terminology](#13-naming-and-terminology)

#### 2. [Current Codebase Status](#2-current-codebase-status)
- [2.1 Repository State](#21-repository-state)
- [2.2 Implemented Runtime Modules](#22-implemented-runtime-modules)
- [2.3 What Is Still Missing](#23-what-is-still-missing)

#### 3. [Codebase Architecture Overview](#3-codebase-architecture-overview)
- [3.1 Current Source Layout](#31-current-source-layout)
- [3.2 Target Long-Term Layout](#32-target-long-term-layout)
- [3.3 Design Philosophy](#33-design-philosophy)

#### 4. [Program Execution Flow](#4-program-execution-flow)
- [4.1 Entry Through main.cpp](#41-entry-through-maincpp)
- [4.2 Host Lifecycle Flow](#42-host-lifecycle-flow)
- [4.3 Current Runtime Ownership Model](#43-current-runtime-ownership-model)

### PART II: IMPLEMENTED SYSTEMS

#### 5. [Common Foundation](#5-common-foundation)
- [5.1 com_foundation.h](#51-com_foundationh)
- [5.2 com_error.h](#52-com_errorh)
- [5.3 com_print.cpp](#53-com_printcpp)

#### 6. [Logging System](#6-logging-system)
- [6.1 Public API Shape](#61-public-api-shape)
- [6.2 Runtime State and Filtering](#62-runtime-state-and-filtering)
- [6.3 Strengths and Current Limitations](#63-strengths-and-current-limitations)

#### 7. [Platform System](#7-platform-system)
- [7.1 What Exists Today](#71-what-exists-today)
- [7.2 What SDL3 Will Change](#72-what-sdl3-will-change)

#### 8. [Host Runtime](#8-host-runtime)
- [8.1 Lifecycle Ownership](#81-lifecycle-ownership)
- [8.2 Frame Sequencing](#82-frame-sequencing)
- [8.3 Current Limitations](#83-current-limitations)

#### 9. [Renderer Contract](#9-renderer-contract)
- [9.1 Current Render Runtime State](#91-current-render-runtime-state)
- [9.2 Error-Coded Lifecycle](#92-error-coded-lifecycle)
- [9.3 Missing Backend Work](#93-missing-backend-work)

#### 10. [Command System](#10-command-system)
- [10.1 Registry Model](#101-registry-model)
- [10.2 Parsing and Dispatch](#102-parsing-and-dispatch)
- [10.3 Current Limitations](#103-current-limitations)

#### 11. [Cvar System](#11-cvar-system)
- [11.1 Registry and Cached Values](#111-registry-and-cached-values)
- [11.2 Flags and Mutation Rules](#112-flags-and-mutation-rules)
- [11.3 Current Limitations](#113-current-limitations)

#### 12. [Config System](#12-config-system)
- [12.1 File Loading](#121-file-loading)
- [12.2 Single-Line Execution](#122-single-line-execution)
- [12.3 Current Limitations](#123-current-limitations)

### PART III: NEXT IMPLEMENTATION WORK

#### 13. [Known Technical Debt](#13-known-technical-debt)
#### 14. [Next Implementation Order](#14-next-implementation-order)
#### 15. [Long-Term Tooling Direction](#15-long-term-tooling-direction)

---

## 1. Introduction

### 1.1 Purpose and Scope

This document explains how the current CypherEngine runtime API is implemented today.

It exists to answer:

- what the public API is backed by internally
- which subsystem owns what
- what the current execution flow looks like
- which parts are solid already
- which parts are still placeholders or early scaffolding

This is the implementation-oriented companion to [CYPHERENGINE_API_REFERENCE.md](/Users/karlosiric/Documents/MyProjects/REAP/docs/CYPHERENGINE_API_REFERENCE.md).

### 1.2 Target Audience

This document is meant for:

- the primary engine author
- future contributors
- future-you returning after a break
- anyone trying to understand why the code is shaped the way it is

### 1.3 Naming and Terminology

- `REAP` is the overall project/game
- `CypherEngine` is the native engine runtime
- current source still lives under `src/CypherEngine/`

---

## 2. Current Codebase Status

### 2.1 Repository State

Current source size is modest, but the core runtime foundation already exists.

High-level snapshot:

- roughly `3.2k` lines of source under `src/`
- CMake build is working
- host frame loop compiles and runs
- several core runtime subsystems are already separated instead of being collapsed into one file

### 2.2 Implemented Runtime Modules

Current implemented modules:

- `common`
- `log`
- `platform`
- `host`
- `render`
- `cmd`
- `cvar`
- `cfg`

### 2.3 What Is Still Missing

Major missing runtime layers:

- virtual filesystem
- SDL3 platform bootstrap
- OpenGL backend bootstrap
- resource/shader/texture loading path
- BSP runtime
- networking/client-server split
- VM path
- toolchain/runtime integration

---

## 3. Codebase Architecture Overview

### 3.1 Current Source Layout

Current code lives in `src/CypherEngine/` because the project is still in early foundation phase.

That is acceptable right now because subsystem boundaries already exist in the code:

- common foundation
- logging
- platform
- host lifecycle
- renderer contract
- textual command/config family

### 3.2 Target Long-Term Layout

Long-term target layout remains broader:

- `common`
- `renderer`
- `platform`
- `bsp`
- `network`
- `server`
- `client`
- `physics`
- `audio`
- `vm`
- `tools`
- `game`

### 3.3 Design Philosophy

Current implementation direction follows a few simple rules:

- build explicit subsystem seams early
- keep error returns visible
- avoid speculative abstraction before runtime pressure exists
- learn from Quake-era architecture without directly cloning old code

---

## 4. Program Execution Flow

### 4.1 Entry Through main.cpp

Source:

- [src/main.cpp](/Users/karlosiric/Documents/MyProjects/REAP/src/main.cpp)

Current role of `main.cpp`:

- initialize logging
- create host config/state
- drive the host frame loop
- shutdown host and logger

### 4.2 Host Lifecycle Flow

Current runtime flow is:

1. `main()` initializes `log`
2. `main()` calls `CypherHost_Init()`
3. `CypherHost_Init()` calls `render::CypherRender_Init()`
4. the main loop advances through:
   - `CypherHost_BeginFrame()`
   - `CypherHost_Update()`
   - `CypherHost_Render()`
   - `CypherHost_EndFrame()`
5. `CypherHost_Shutdown()` tears down the renderer
6. `main()` shuts down logging

### 4.3 Current Runtime Ownership Model

At the moment:

- `main.cpp` owns process entry and high-level loop
- `host` owns runtime stage transitions
- `render` owns render lifecycle state
- `cmd`, `cvar`, and `cfg` exist but are not yet fully wired into startup flow

---

## 5. Common Foundation

### 5.1 com_foundation.h

Source:

- [src/CypherEngine/CypherCommon/CypherCommon.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCommon/CypherCommon.h)

Implementation role:

- fixed-width types
- numeric constants
- sentinel runtime identifiers
- lightweight engine-wide naming consistency

### 5.2 com_error.h

Source:

- [src/CypherEngine/CypherCommon/CypherCommon_Error.h](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCommon/CypherCommon_Error.h)

Implementation role:

- defines the shared surfaced-error packing format
- lets typed subsystem-local errors stay local until they need to cross a subsystem boundary

Current strength:

- this is already a strong foundation decision for long-term maintainability

### 5.3 com_print.cpp

Source:

- [src/CypherEngine/CypherCommon/CypherCommon_Print.cpp](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCommon/CypherCommon_Print.cpp)

Implementation role:

- common formatted print path
- common surfaced-error print path

Current limitation:

- intentionally lightweight
- not a full console/log routing layer by itself

---

## 6. Logging System

### 6.1 Public API Shape

Source:

- [src/CypherEngine/CypherLog/CypherLog.cpp](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherLog/CypherLog.cpp)

The logger already exposes a clean foundation-style API:

- init/shutdown
- config get/set
- level/channel filtering
- record emit
- formatted emit

### 6.2 Runtime State and Filtering

Implementation behavior:

- logger owns a runtime config struct
- channel filtering uses a bitmask
- severity filtering happens before record construction when possible
- output can target console and optional file sink

### 6.3 Strengths and Current Limitations

Strengths:

- clean subsystem identity
- useful even at this early stage
- already supports structured growth

Current limitations:

- macro layer can still be cleaned up
- integration can become richer once more runtime systems exist

---

## 7. Platform System

### 7.1 What Exists Today

Source:

- [src/CypherEngine/CypherSystem/CypherSystem_Platform.cpp](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherSystem/CypherSystem_Platform.cpp)

Current implementation provides:

- platform detection
- compiler detection
- basename helper
- monotonic time helper
- local time helper

### 7.2 What SDL3 Will Change

The current platform layer is still pre-SDL.

Next stage should add:

- SDL3 initialization/shutdown
- window creation
- event processing
- input bridging
- GL context/platform seam support

---

## 8. Host Runtime

### 8.1 Lifecycle Ownership

Source:

- [src/CypherEngine/CypherHost/CypherHost.cpp](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherHost/CypherHost.cpp)

The host currently owns:

- runtime stage transitions
- running/paused/shutdown state
- frame timing accumulation
- renderer lifecycle integration

### 8.2 Frame Sequencing

Current sequence:

- begin frame
- update
- render
- end frame

This is a good early shape because it gives the future engine a stable top-level runtime rhythm.

### 8.3 Current Limitations

- no event pump yet
- no input ownership yet
- no broader subsystem orchestration yet

---

## 9. Renderer Contract

### 9.1 Current Render Runtime State

Source:

- [src/CypherEngine/CypherRender/CypherRender.cpp](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherRender/CypherRender.cpp)

Current renderer state is intentionally tiny:

- `initialized`
- `in_frame`

### 9.2 Error-Coded Lifecycle

The renderer currently validates:

- init called twice
- invalid viewport dimensions
- begin-frame without init
- render/end without active frame

That means the API contract exists before the backend exists.

### 9.3 Missing Backend Work

Still missing:

- SDL/OpenGL bootstrap
- real draw submission
- resource lifetime management
- shader pipeline
- texture pipeline

---

## 10. Command System

### 10.1 Registry Model

Source:

- [src/CypherEngine/CypherCommand/CypherCommand.cpp](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCommand/CypherCommand.cpp)

Current command system design:

- fixed-size registry
- linear lookup
- simple callback entry

This is fully acceptable for the current stage.

### 10.2 Parsing and Dispatch

Current parser behavior:

- tokenizes a mutable command buffer
- writes token pointers into `argv`
- dispatches by first token

### 10.3 Current Limitations

- no quoted-argument aware parsing
- no aliasing
- no completion/help console layer yet

---

## 11. Cvar System

### 11.1 Registry and Cached Values

Source:

- [src/CypherEngine/CypherCVar/CypherCVar.cpp](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherCVar/CypherCVar.cpp)

Current design:

- fixed-size registry
- per-cvar cached string/int/float/bool values
- direct linear search

### 11.2 Flags and Mutation Rules

Current flag model includes:

- archive
- readonly
- cheat
- dev
- modified

This is a good early policy surface for an engine project.

### 11.3 Current Limitations

- no change callbacks
- no persistence save path
- no VFS integration yet

---

## 12. Config System

### 12.1 File Loading

Source:

- [src/CypherEngine/CypherConfig/CypherConfig.cpp](/Users/karlosiric/Documents/MyProjects/REAP/src/CypherEngine/CypherConfig/CypherConfig.cpp)

Current config runtime:

- opens config files directly
- reads line by line
- routes each line through `CypherConfig_ExecuteLine()`
- supports nested `exec`

### 12.2 Single-Line Execution

`CypherConfig_ExecuteLine()` exists so that a single textual line can be executed using the same general rules as cfg file lines.

That makes it useful later for:

- developer console execution
- tests
- replay/debugging of individual config lines

### 12.3 Current Limitations

- still uses direct `std::fopen`
- still needs a filesystem seam before VFS work can land cleanly

---

## 13. Known Technical Debt

Current notable debt:

- no filesystem seam exists yet
- startup wiring still does not fully route through cfg/cmd/cvar
- renderer contract exists without real backend implementation
- docs will need ongoing upkeep as soon as `fs` and `SDL3` land

---

## 14. Next Implementation Order

The next correct implementation order is:

1. add `fs`
2. route `cfg` through `fs`
3. add `SDL3` bootstrap in `platform`
4. add `OpenGL` bootstrap in `render`
5. bring up a visible runtime loop
6. begin `Quake III BSP` runtime work

This keeps the engine growing bottom-up instead of skipping foundational seams.

---

## 15. Long-Term Tooling Direction

Long-term intended tooling includes:

- custom `rmdl` format
- custom `rpk` archive path if justified
- small purpose-built editor for REAP/CypherEngine workflows

Important rule:

- the runtime must demand the tool first
- the tool should solve a real workflow pain
- the project should avoid giant speculative tool work before the runtime path exists
