<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/subsystem_source_catalog.md
//  Purpose: Catalogs planned implementation units for runtime subsystems.
//  Details: Records concrete source ownership before files are introduced, while
//           preventing empty placeholders and arbitrary file-count targets from
//           being mistaken for implemented architecture.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine Subsystem Source Catalog

This is the planning inventory for source units under `src/`. It answers which
responsibilities belong in each subsystem and gives stable candidate basenames.
It is not a request to create every file immediately.

## Source-unit policy

- There is no minimum file count. File count measures partitioning, not engine
  capability or quality.
- A planned unit becomes a real `.h`/`.cpp` pair only when its contract,
  ownership, dependencies, tests, and first consumer are understood.
- Header-only code is reserved for templates, compile-time code, or measured
  inline hot paths. Ordinary implementation stays in `.cpp` files.
- Public contracts expose Cypher types and handles. Backend headers, third-party
  types, OS handles, and mutable implementation state remain private.
- New units enter CMake explicitly and arrive with correctness coverage.
  Performance-sensitive units also receive representative benchmarks.
- A subsystem may later split one listed unit into several files when the split
  creates a real ownership or build boundary.
- Empty compilation units are prohibited. Planned names remain in this catalog
  until implementation begins.

Common naming pattern:

```text
Cypher<Module>.h                 public umbrella when one is useful
Cypher<Module>_Types.h           shared data-only contracts
Cypher<Module>_Error.h           stable module error/result definitions
Cypher<Module>_<Feature>.h/.cpp  one coherent feature implementation
Internal/                       private implementation details
Backends/                       replaceable platform or API implementations
```

## `CypherCommon`

Common owns reusable foundations and cross-subsystem contracts, not complete
renderer, world, physics, audio, or editor implementations.

```text
Tier0/                 platform/compiler primitives, diagnostics, atomics, threads
Tier1/                 text, containers, allocation, parsing, hashing, VFS primitives
Tier2/                 schemas, typed documents, configuration and resource descriptors
Mathlib/               scalar math, matrices, quaternions, geometry primitives
Formats/               stable source/cooked headers and format validation contracts
Security/              cryptographic wrappers and secret-lifetime contracts
ToolFramework/         headless tool commands, diagnostics, progress and reports
Public/                versioned cross-module interfaces when a binary boundary exists
```

Gate: code moves into Common only after at least two independent consumers need
the same contract or primitive.

## `CypherPlatform`

```text
CypherPlatform_API              CypherPlatform_Types
CypherPlatform_Error            CypherPlatform_Runtime
CypherPlatform_Window           CypherPlatform_Display
CypherPlatform_EventPump        CypherPlatform_Clipboard
CypherPlatform_Cursor           CypherPlatform_DynamicLibrary
CypherPlatform_Process          CypherPlatform_Environment
CypherPlatform_Path             CypherPlatform_VirtualMemory
CypherPlatform_Thread           CypherPlatform_CrashContext
Backends/SDL3                   Backends/Win32
Backends/Linux                  Backends/MacOS
```

First slice: process startup, SDL3 window/events, dynamic libraries, paths, and
virtual-memory calls with no OS types crossing the public boundary.

## `CypherSystem`

```text
CypherSystem_API                CypherSystem_Types
CypherSystem_Error              CypherSystem_State
CypherSystem_Startup            CypherSystem_Shutdown
CypherSystem_Frame              CypherSystem_Lifecycle
CypherSystem_Service            CypherSystem_ServiceRegistry
CypherSystem_Module             CypherSystem_ModuleRegistry
CypherSystem_Event              CypherSystem_Clock
CypherSystem_Paths              CypherSystem_BuildInfo
CypherSystem_CommandLine        CypherSystem_Recovery
```

First slice: explicit initialization order and reverse-order shutdown for the
minimum playable runtime.

## `CypherMemory`

```text
CypherMemory_API                CypherMemory_Types
CypherMemory_Error              CypherMemory_SystemAllocator
CypherMemory_VirtualAllocator   CypherMemory_LinearArena
CypherMemory_StackArena         CypherMemory_FrameArena
CypherMemory_Scratch            CypherMemory_Pool
CypherMemory_FreeList           CypherMemory_Slab
CypherMemory_Ring               CypherMemory_Bucket
CypherMemory_Tag                CypherMemory_Budget
CypherMemory_Tracker            CypherMemory_Snapshot
CypherMemory_Leak               CypherMemory_Guard
CypherMemory_Report             CypherMemory_ThreadContext
```

First slice: system allocator, virtual memory, permanent/frame arenas, tags,
high-water statistics, and leak reporting.

## `CypherLog`

```text
CypherLog_API                   CypherLog_Types
CypherLog_Error                 CypherLog_Channel
CypherLog_Record                CypherLog_Format
CypherLog_Filter                CypherLog_Router
CypherLog_Sink                  CypherLog_FileSink
CypherLog_TerminalSink          CypherLog_DebugSink
CypherLog_Buffer                CypherLog_AsyncQueue
CypherLog_Session               CypherLog_Report
```

First slice: structured records routed synchronously to terminal and rotating
file sinks; asynchronous logging is deferred until profiling proves a need.

## `CypherFileSystem`

```text
CypherFileSystem_API            CypherFileSystem_Types
CypherFileSystem_Error          CypherFileSystem_Path
CypherFileSystem_File           CypherFileSystem_Stream
CypherFileSystem_Directory      CypherFileSystem_Search
CypherFileSystem_Mount          CypherFileSystem_MountTable
CypherFileSystem_VFS            CypherFileSystem_NativeBackend
CypherFileSystem_Overlay        CypherFileSystem_PakBackend
CypherFileSystem_Cache          CypherFileSystem_Async
CypherFileSystem_Watch          CypherFileSystem_Transaction
CypherFileSystem_Stats          CypherFileSystem_Diagnostics
```

First slice: normalized virtual paths, ordered mounts, bounded reads, directory
enumeration, and source/output roots shared by runtime and tools.

## `CypherPak`

```text
CypherPak_API                   CypherPak_Types
CypherPak_Error                 CypherPak_Format
CypherPak_Header                CypherPak_Index
CypherPak_Directory             CypherPak_Entry
CypherPak_Reader                CypherPak_Writer
CypherPak_Builder               CypherPak_Extractor
CypherPak_Compression           CypherPak_Encryption
CypherPak_Hash                  CypherPak_Signature
CypherPak_Verify                CypherPak_Mount
CypherPak_Stream                CypherPak_Report
```

First slice: deterministic archive index, reader/writer, hashes, compression,
and VFS mount support. Encryption and signatures require the security model.

## `CypherResource`

```text
CypherResource_API              CypherResource_Types
CypherResource_Error            CypherResource_ID
CypherResource_Handle           CypherResource_Type
CypherResource_State            CypherResource_Record
CypherResource_Table            CypherResource_Registry
CypherResource_Request          CypherResource_Loader
CypherResource_Dependency       CypherResource_Cache
CypherResource_Residency        CypherResource_Budget
CypherResource_Stream           CypherResource_Transaction
CypherResource_HotReload        CypherResource_Diagnostics
CypherResource_Manager          CypherResource_Serialization
```

First slice: generational handles, records, synchronous cooked-resource loads,
dependency ownership, typed lookup, unloading, and failure diagnostics.

## `CypherRender`

```text
CypherRender_API                CypherRender_Types
CypherRender_Error              CypherRender_Handle
CypherRender_Device             CypherRender_Context
CypherRender_Swapchain          CypherRender_Frame
CypherRender_Command            CypherRender_CommandList
CypherRender_Buffer             CypherRender_Texture
CypherRender_Sampler            CypherRender_Shader
CypherRender_Pipeline           CypherRender_Descriptor
CypherRender_Mesh               CypherRender_Material
CypherRender_Camera             CypherRender_View
CypherRender_Scene              CypherRender_DrawList
CypherRender_RenderGraph        CypherRender_Pass
CypherRender_Light              CypherRender_Shadow
CypherRender_Sky                CypherRender_Decal
CypherRender_Particle           CypherRender_DebugDraw
CypherRender_Capture            CypherRender_Stats
Backends/Software              Backends/OpenGL
Backends/Vulkan
```

First slice: renderer-neutral handles and commands, a software reference
backend, one camera, static meshes, textures, materials, and debug geometry.
OpenGL follows the reference path; Vulkan remains deferred.

## `CypherWorld`

```text
CypherWorld_API                 CypherWorld_Types
CypherWorld_Error               CypherWorld_Handle
CypherWorld_Runtime             CypherWorld_Loader
CypherWorld_Map                 CypherWorld_Scene
CypherWorld_Object              CypherWorld_Transform
CypherWorld_Hierarchy           CypherWorld_Cell
CypherWorld_Sector              CypherWorld_Portal
CypherWorld_Visibility          CypherWorld_SpatialIndex
CypherWorld_Query               CypherWorld_Trace
CypherWorld_Streaming           CypherWorld_Spawn
CypherWorld_Environment         CypherWorld_Lighting
CypherWorld_Decal               CypherWorld_Debug
CypherWorld_Serialization       CypherWorld_Stats
```

First slice: load one cooked map, own static placements and spawn records, query
world bounds, and submit visible render objects.

## `CypherEntity`

```text
CypherEntity_API                CypherEntity_Types
CypherEntity_Error              CypherEntity_ID
CypherEntity_Handle             CypherEntity_Record
CypherEntity_Registry           CypherEntity_Lifecycle
CypherEntity_Component          CypherEntity_ComponentType
CypherEntity_Storage            CypherEntity_Query
CypherEntity_Transform          CypherEntity_Hierarchy
CypherEntity_Prefab             CypherEntity_Spawn
CypherEntity_Event              CypherEntity_Serialization
CypherEntity_Replication        CypherEntity_Debug
```

First slice: identity, lifetime, transforms, explicit component storage, basic
queries, and prefab instantiation. Do not commit to an archetype ECS prematurely.

## `CypherPhysics`

```text
CypherPhysics_API               CypherPhysics_Types
CypherPhysics_Error             CypherPhysics_World
CypherPhysics_Body              CypherPhysics_Shape
CypherPhysics_Collider          CypherPhysics_Material
CypherPhysics_Broadphase        CypherPhysics_Narrowphase
CypherPhysics_Contact           CypherPhysics_Manifold
CypherPhysics_Island            CypherPhysics_Solver
CypherPhysics_Constraint        CypherPhysics_Joint
CypherPhysics_Character         CypherPhysics_Movement
CypherPhysics_Trigger           CypherPhysics_Query
CypherPhysics_Trace             CypherPhysics_Integration
CypherPhysics_Sleep             CypherPhysics_DebugDraw
CypherPhysics_Serialization     CypherPhysics_Stats
```

First slice: deterministic-enough player movement, static collision, traces,
triggers, broadphase, contacts, and debug draw before general rigid bodies.

## `CypherAudio`

```text
CypherAudio_API                 CypherAudio_Types
CypherAudio_Error               CypherAudio_Device
CypherAudio_Backend             CypherAudio_Mixer
CypherAudio_Voice               CypherAudio_Source
CypherAudio_Buffer              CypherAudio_Stream
CypherAudio_Bus                 CypherAudio_Effect
CypherAudio_Listener            CypherAudio_Spatial
CypherAudio_Attenuation         CypherAudio_Occlusion
CypherAudio_Reverb              CypherAudio_Music
CypherAudio_Event               CypherAudio_Bank
CypherAudio_Resource            CypherAudio_Capture
CypherAudio_Debug               CypherAudio_Stats
```

First slice: backend device, mixer, voices, decoded buffers, streaming music,
listener/source spatialization, buses, and runtime diagnostics.

## `CypherAnimation`

```text
CypherAnimation_API             CypherAnimation_Types
CypherAnimation_Error           CypherAnimation_Skeleton
CypherAnimation_Bone            CypherAnimation_Pose
CypherAnimation_Clip            CypherAnimation_Track
CypherAnimation_Keyframe        CypherAnimation_Sample
CypherAnimation_Blend           CypherAnimation_Layer
CypherAnimation_State           CypherAnimation_StateMachine
CypherAnimation_Graph           CypherAnimation_Parameter
CypherAnimation_Event           CypherAnimation_IK
CypherAnimation_Retarget        CypherAnimation_RootMotion
CypherAnimation_Skinning        CypherAnimation_Compression
CypherAnimation_Resource        CypherAnimation_Debug
```

First slice: skeleton/clip loading, sampled local poses, hierarchy evaluation,
two-clip blending, events, root motion, and renderer skinning data.

## `CypherAI`

```text
CypherAI_API                    CypherAI_Types
CypherAI_Error                  CypherAI_World
CypherAI_Agent                  CypherAI_Navigation
CypherAI_NavMesh                CypherAI_NavQuery
CypherAI_Path                   CypherAI_Pathfinding
CypherAI_Perception             CypherAI_Vision
CypherAI_Hearing                CypherAI_Memory
CypherAI_Blackboard             CypherAI_Goal
CypherAI_Task                   CypherAI_StateMachine
CypherAI_Behavior               CypherAI_Tactical
CypherAI_Cover                  CypherAI_Squad
CypherAI_Director               CypherAI_Debug
```

First slice: navigation graph, path queries, perception, memory, explicit state
machines, and combat tasks for one enemy type.

## `CypherNetwork`

```text
CypherNetwork_API               CypherNetwork_Types
CypherNetwork_Error             CypherNetwork_Address
CypherNetwork_Socket            CypherNetwork_Transport
CypherNetwork_Packet            CypherNetwork_Message
CypherNetwork_BitStream         CypherNetwork_Sequence
CypherNetwork_Channel           CypherNetwork_Reliability
CypherNetwork_Fragment          CypherNetwork_Connection
CypherNetwork_Handshake         CypherNetwork_Session
CypherNetwork_Command           CypherNetwork_Snapshot
CypherNetwork_Delta             CypherNetwork_Replication
CypherNetwork_Prediction        CypherNetwork_Interpolation
CypherNetwork_Rate              CypherNetwork_Compression
CypherNetwork_Crypto            CypherNetwork_Replay
CypherNetwork_Simulation        CypherNetwork_Diagnostics
```

First slice: UDP transport, handshake, sequence/ack tracking, packet-loss
simulation, reliable commands, and unreliable snapshots.

## `CypherInput`

```text
CypherInput_API                 CypherInput_Types
CypherInput_Error               CypherInput_System
CypherInput_Device              CypherInput_Keyboard
CypherInput_Mouse               CypherInput_Controller
CypherInput_Event               CypherInput_State
CypherInput_Action              CypherInput_Binding
CypherInput_Context             CypherInput_Map
CypherInput_Text                CypherInput_Cursor
CypherInput_Rumble              CypherInput_Replay
CypherInput_Debug
```

First slice: SDL3 event translation, raw state, action bindings, contexts, mouse
capture, and deterministic user-command generation.

## `CypherScript`

```text
CypherScript_API                CypherScript_Types
CypherScript_Error              CypherScript_Runtime
CypherScript_VM                 CypherScript_Module
CypherScript_Value              CypherScript_Stack
CypherScript_Function           CypherScript_Table
CypherScript_Native             CypherScript_Binding
CypherScript_Event              CypherScript_Scheduler
CypherScript_Coroutine          CypherScript_Resource
CypherScript_Serialization      CypherScript_HotReload
CypherScript_Sandbox            CypherScript_Debug
```

First slice: Lua runtime ownership, module loading through VFS, explicit native
bindings, errors with source locations, events, and controlled reload.

## `CypherCommand`

```text
CypherCommand_API               CypherCommand_Types
CypherCommand_Error             CypherCommand_Registry
CypherCommand_Descriptor        CypherCommand_Arguments
CypherCommand_Parser            CypherCommand_Execute
CypherCommand_Buffer            CypherCommand_Queue
CypherCommand_Alias             CypherCommand_Completion
CypherCommand_History           CypherCommand_Permission
```

## `CypherCVar`

```text
CypherCVar_API                  CypherCVar_Types
CypherCVar_Error                CypherCVar_Registry
CypherCVar_Descriptor           CypherCVar_Value
CypherCVar_Parse                CypherCVar_Format
CypherCVar_Callback             CypherCVar_Archive
CypherCVar_Replication          CypherCVar_Permission
CypherCVar_Completion           CypherCVar_Report
```

## `CypherConsole`

```text
CypherConsole_API               CypherConsole_Types
CypherConsole_Error             CypherConsole_Runtime
CypherConsole_Model             CypherConsole_Line
CypherConsole_Buffer            CypherConsole_Filter
CypherConsole_History           CypherConsole_Input
CypherConsole_Completion        CypherConsole_Render
CypherConsole_Remote            CypherConsole_Report
```

## `CypherConfig`

```text
CypherConfig_API                CypherConfig_Types
CypherConfig_Error              CypherConfig_Source
CypherConfig_Layer              CypherConfig_Parse
CypherConfig_Execute            CypherConfig_Startup
CypherConfig_User               CypherConfig_Archive
CypherConfig_Migration          CypherConfig_Watch
CypherConfig_Report
```

Command, CVar, Console, and Config first slice: register commands and typed CVars,
execute startup/user cfg files, expose completion/history, and render a basic
Source-style developer console.

## `CypherProfile`

```text
CypherProfile_API               CypherProfile_Types
CypherProfile_Error             CypherProfile_Clock
CypherProfile_Zone              CypherProfile_Thread
CypherProfile_Frame             CypherProfile_Counter
CypherProfile_Memory            CypherProfile_GPU
CypherProfile_Registry          CypherProfile_Buffer
CypherProfile_Capture           CypherProfile_Serialize
CypherProfile_Telemetry         CypherProfile_Overlay
CypherProfile_Report
```

First slice: nested CPU zones, frame timings, counters, thread buffers, captures,
and an in-engine overlay; GPU timings follow the render backend.

## `CypherClient`

```text
CypherClient_API                CypherClient_Types
CypherClient_Error              CypherClient_State
CypherClient_Startup            CypherClient_Frame
CypherClient_Input              CypherClient_UserCommand
CypherClient_Prediction         CypherClient_Interpolation
CypherClient_View               CypherClient_Camera
CypherClient_Render             CypherClient_Effects
CypherClient_HUD                CypherClient_Menu
CypherClient_Console            CypherClient_Network
CypherClient_Demo               CypherClient_Debug
```

## `CypherServer`

```text
CypherServer_API                CypherServer_Types
CypherServer_Error              CypherServer_State
CypherServer_Startup            CypherServer_Frame
CypherServer_Client             CypherServer_Connection
CypherServer_UserCommand        CypherServer_Simulation
CypherServer_World              CypherServer_Snapshot
CypherServer_Replication        CypherServer_Visibility
CypherServer_GameBridge         CypherServer_Command
CypherServer_Demo               CypherServer_Debug
```

## `CypherGame`

```text
CypherGame_API                  CypherGame_Types
CypherGame_Error                CypherGame_Module
CypherGame_State                CypherGame_Rules
CypherGame_Player               CypherGame_Weapon
CypherGame_Projectile           CypherGame_Damage
CypherGame_Item                 CypherGame_Trigger
CypherGame_Spawn                CypherGame_Enemy
CypherGame_Wave                 CypherGame_Objective
CypherGame_Event                CypherGame_Save
CypherGame_Debug
```

Client/server/game first slice: one listen-server session, movement, one weapon,
one enemy, damage, death, respawn, snapshots, and a minimal HUD.

## `CypherEngine`

`CypherEngine` is the executable/product host, not another general-purpose
subsystem. Most implementation belongs in the modules above.

```text
CypherEngine_Main               CypherEngine_Host
CypherEngine_CommandLine        CypherEngine_Application
CypherEngine_Bootstrap          CypherEngine_Runtime
CypherEngine_Product            CypherEngine_BuildInfo
CypherEngine_Dedicated          CypherEngine_ToolHost
```

First slice: parse process options, construct the system host, select client,
dedicated-server, or tool mode, and return a stable process exit code.

## `CypherTools`

```text
ToolFramework/                  shared tool process contracts
CypherResourceCompiler/         compiler registry and resource dispatch
CypherShaderCompiler/           shader validation and cooking
CypherTextureCompiler/          texture import, mip generation and cooking
CypherMaterialCompiler/         material validation and dependency cooking
CypherMeshCompiler/             mesh import, optimization and cooking
CypherAnimationCompiler/        skeleton/clip validation and cooking
CypherAudioCompiler/            audio conversion and bank building
CypherMapCompiler/              authored map to cooked world pipeline
CypherPackageTool/              package build, list, verify and extract
CypherAssetInspector/           cooked-resource inspection and diagnostics
CypherBuild/                    project-level asset/build orchestration
```

Each compiler is a library first and a ResourceCompiler registration second.
The command-line executable must not contain format-specific cooking logic.

## `CypherEditor`

```text
Framework/                      Qt application, commands, documents and docking
Viewport/                       render surface, camera, selection and gizmos
AssetBrowser/                   VFS-backed source/cooked asset browsing
Inspector/                      reflection/schema-backed property editing
Undo/                           transactions, history and recovery
Preview/                        engine-hosted material/model/world previews
Mason/                          world and map authoring workspaces
Picasso/                        texture and material authoring workspaces
Animation/                      skeleton, clip and graph authoring
Particle/                       particle graph and preview tools
Physics/                        collision and physics test workspace
Audio/                          event, mixer, spatial and bank workspace
Navigation/                     navmesh and AI authoring workspace
Cinematics/                     timeline, camera and sequence workspace
Console/                        logs, commands, diagnostics and profiler views
```

No editor workspace begins until its headless runtime/compiler contracts can be
tested without Qt. Picasso therefore starts only after texture/material formats,
compilers, resource loading, and a preview render path exist.

## Implementation order

1. Stabilize Common primitives and explicit CMake boundaries.
2. Complete VFS, cooked resource loading, and shader/texture/material compilers.
3. Establish the engine host and minimum renderer-neutral runtime contracts.
4. Build the software reference renderer, then the OpenGL backend.
5. Load one cooked world and implement entity, input, physics, audio, scripting,
   client, and server vertical slices around one playable arena.
6. Start Picasso only when it can consume real formats and render a real preview.
7. Start Mason after world serialization, picking, transforms, rendering, undo,
   and compiler round trips exist headlessly.

The catalog should be revised when implementation disproves an assumption. It
must never be bulk-materialized merely to make the repository appear larger.
