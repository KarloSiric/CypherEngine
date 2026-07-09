<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/function_pointer_policy.md
//  Purpose: Defines the function pointer and service-table policy.
//  Details: This document explains where C-style callback tables belong, how
//           subsystems should communicate, and when direct calls are preferred.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Function Pointer Policy

Function pointers are an important tool for CypherEngine, but they are not the
only way subsystems communicate.

The rule is:

```text
Use direct functions inside a subsystem.
Use data, handles, queues, and commands between systems.
Use function-pointer tables at stable C-style boundaries.
```

## Why Function Pointers Matter

C-style engines use function pointers because they make boundaries explicit
without requiring inheritance-heavy C++ designs.

They are useful for:

- replaceable backends
- subsystem service tables
- plugin entry points
- VM/native bridge calls
- allocator interfaces
- file stream callbacks
- command callbacks
- event callbacks
- dynamically loaded modules

They are not automatically faster or cleaner. They add one level of indirection,
so they should describe a real boundary.

## Communication Models

CypherEngine should use several communication models, depending on the case.

### Direct Calls

Use direct calls for normal local code.

```text
Cy_UtlVectorPushBack()
Cy_StrLen()
Cy_TimerNowTicks()
```

Direct calls are easy to read, easy to debug, and good for hot code when no
runtime replacement is needed.

### Handles and Descriptors

Use handles when one system owns data and another system references it.

```text
cy_texture_handle_t
cy_sound_handle_t
cy_entity_id_t
cy_resource_handle_t
```

This is the correct model for renderer, audio, resources, physics bodies,
entities, assets, and editor selections.

### Command Queues

Use command queues when one system should not mutate another system immediately.

Examples:

- renderer command submission
- audio command submission
- editor undo/redo commands
- asset reload requests
- job-system work packets

This keeps ownership clear and prevents random systems from reaching into each
other's runtime state.

### Event Queues

Use event queues for many-to-one or one-to-many notification.

Examples:

- input events
- filesystem watch events
- asset reload events
- window events
- editor selection changes
- network connection events

Events should carry plain data, IDs, and handles.

### Function-Pointer Tables

Use function-pointer tables for stable interfaces where an implementation can be
swapped.

Good examples:

```text
cy_allocator_i
    Alloc
    Realloc
    Free

cy_stream_i
    Read
    Write
    Seek
    Tell
    Close

cy_renderer_i
    BeginFrame
    Submit
    EndFrame
    CreateTexture
    DestroyTexture
```

These tables are useful because the caller only knows the contract, not the
implementation.

## What Function Pointers Are Not For

Do not use function pointers for every helper.

Bad pattern:

```text
StringLength through callback table
EndianSwap through callback table
VectorPush through callback table
```

Those are normal utility functions. Making them indirect only makes code harder
to follow.

## Where They Belong

Function-pointer interfaces belong in Common when multiple systems need the
contract.

Examples:

```text
CypherCommon/Memory/CyAllocator.h
CypherCommon/IO/CyStream.h
CypherCommon/Renderer/ICyRenderer.h
CypherCommon/FileSystem/ICyFileSystem.h
CypherCommon/Audio/ICyAudio.h
CypherCommon/Physics/ICyPhysics.h
CypherCommon/Network/ICyNetworkTransport.h
CypherCommon/Tools/ICyToolModule.h
CypherCommon/Editor/ICyEditorGame.h
```

Implementation belongs outside Common:

```text
CypherRenderer/OpenGL
CypherAudio/OpenAL
CypherPhysics
CypherFileSystem
CypherNetwork
Mason
```

## Subsystem Communication Rule

Subsystems should not freely call into each other's internals.

Preferred flow:

```text
Common type/interface
        ↓
owning subsystem API
        ↓
handle/descriptor/command/event
        ↓
consumer subsystem
```

Example:

```text
Game wants a sound.
Game sends sound handle + play descriptor to Audio.
Audio owns playback state.
Game does not edit OpenAL buffers.
```

Example:

```text
World owns entity placement.
Renderer receives renderable handles or draw packets.
Renderer does not own entity simulation.
```

Example:

```text
Editor selects an entity ID.
Inspector asks Reflection/Entity contracts for editable properties.
Editor does not mutate random engine memory directly.
```

## Cost Model

Function pointer calls have indirect-call cost and can reduce compiler inlining.

Use them where the boundary matters more than the local call cost:

- per-frame subsystem calls
- resource creation/destruction
- file operations
- command execution
- backend dispatch

Avoid them in tight scalar loops unless there is a measured reason.

Good:

```text
Renderer backend table called for high-level operations.
```

Bad:

```text
Per-vertex math operation called through a callback.
```

## Final Rule

Use the simplest communication model that preserves ownership.

```text
local code              direct function
owned data reference    handle
deferred work           command queue
notification            event queue
replaceable backend     function-pointer table
plugin/module bridge    function-pointer table
```

This gives CypherEngine the C-style explicitness of older engines without
turning every subsystem into a maze of callbacks.
