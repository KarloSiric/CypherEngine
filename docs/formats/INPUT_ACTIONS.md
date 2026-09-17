<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/INPUT_ACTIONS.md
//  Purpose: Defines the proposed action-map and user-binding format family.
//  Details: The proposal separates source-controlled action intent, cooked runtime
//           tables, and writable user overrides across keyboard, mouse, and gamepad.
//
//  History:
//  - Created by Karlo Siric on 2026-09-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Cypher Input Actions and Bindings

**Status:** Proposal. The System event layer exists; the Input runtime, compiler,
source schemas, cooked resource, and user-binding store described here do not.

**Proposed family:**

| Role | Identity | Ownership |
| --- | --- | --- |
| Developer action map | `.cyinput`, `cypher.input` V1 | Source controlled and packaged through the cooker |
| Cooked action map | `.cyinput_c`, `CYIN` V1 in CYRS V1 | Read-only runtime resource |
| User overrides | `.cybindings`, `cypher.input_bindings` V1 | Writable user profile; never packaged or cooked |

## Table of Contents

1. [Why This Is Not Named CyKeyMap](#1-why-this-is-not-named-cykeymap)
2. [Current Engine State](#2-current-engine-state)
3. [Ownership Model](#3-ownership-model)
4. [Core Concepts](#4-core-concepts)
5. [Canonical Control Names](#5-canonical-control-names)
6. [Developer `.cyinput` Source](#6-developer-cyinput-source)
7. [Cooked `CYIN` Resource](#7-cooked-cyin-resource)
8. [User `.cybindings` Overrides](#8-user-cybindings-overrides)
9. [Resolution and Merge Order](#9-resolution-and-merge-order)
10. [Conflict Policy](#10-conflict-policy)
11. [Device and Player Policy](#11-device-and-player-policy)
12. [Focus and Device-Loss Rules](#12-focus-and-device-loss-rules)
13. [Accessibility](#13-accessibility)
14. [Limits](#14-limits)
15. [Compiler Validation](#15-compiler-validation)
16. [Runtime Contract](#16-runtime-contract)
17. [Diagnostics and Tooling](#17-diagnostics-and-tooling)
18. [CYKV 2 Interaction](#18-cykv-2-interaction)
19. [Non-goals](#19-non-goals)
20. [Implementation Sequence](#20-implementation-sequence)
21. [Acceptance Criteria](#21-acceptance-criteria)
22. [Open Decisions](#22-open-decisions)

## 1. Why This Is Not Named CyKeyMap

A key map suggests a table from keyboard keys to commands. A complete engine input
contract also needs:

- mouse buttons, motion, and wheel axes;
- gamepad buttons, triggers, and sticks;
- digital, one-dimensional, and two-dimensional actions;
- chords and composite axes;
- press, release, hold, repeat, and threshold triggers;
- dead zones, response curves, inversion, and scaling;
- gameplay, menu, console, spectator, and editor contexts;
- user rebinding and unbinding;
- device glyphs and accessibility policy;
- multiple local players and device assignment.

`CyKeyMap` can remain a UI phrase for a keyboard-shortcut editor if desired. The
serialized engine family should use input/action terminology.

## 2. Current Engine State

The current [System public event contract](../../src/CypherSystem/CypherSystem_Public.h)
emits stable raw events for:

- physical keyboard controls;
- text input as a separate stream;
- mouse motion;
- mouse buttons;
- mouse wheel movement.

The distinction between physical key input and text/IME input is intentional. A
physical `W` control may drive forward movement regardless of keyboard layout,
while text input must preserve the user's layout and composition behavior.

Numeric System key-enum values are implementation details and must not be stored
in source files, cooked resources, settings, replays, or network messages.

Current missing runtime pieces include:

- action evaluation;
- binding maps;
- context stacks;
- gamepad events;
- device hotplug;
- user rebinding;
- input compiler;
- cooked action-map resource;
- replayable action frames.

[`src/CypherInput`](../../src/CypherInput) and
[`src/CypherTools/CypherInputCompiler`](../../src/CypherTools/CypherInputCompiler)
are currently scaffold folders. The shared
[InputSystem reservation](../../src/CypherCommon/InputSystem/README.md),
[tool-suite requirements](../tool_suite.md), and
[subsystem source catalog](../subsystem_source_catalog.md) establish the intended
responsibility without providing an implementation. This proposal must be
implemented only as real gameplay and editor consumers are connected.

## 3. Ownership Model

```text
CypherSystem
    emits normalized physical device events
        |
        v
CypherInput
    tracks device state
    resolves contexts and bindings
    applies triggers and processors
    publishes named action state
        |
        +--> gameplay simulation
        +--> camera
        +--> UI routing
        +--> replay/action-frame capture

CypherInputCompiler
    reads .cyinput
    validates action/context/binding contracts
    emits .cyinput_c / CYIN

User settings/profile layer
    reads and atomically writes .cybindings
    merges sparse overrides over cooked defaults
```

System owns platform event translation. Input owns actions and bindings. Gameplay
consumes actions and should not ask SDL or Qt for raw keys. UI text fields consume
the separate text-input stream.

## 4. Core Concepts

### 4.1 Action

An action is a stable semantic intent such as:

- `player.move`
- `player.look`
- `player.jump`
- `player.fire_primary`
- `player.interact`
- `ui.accept`
- `ui.cancel`
- `console.toggle`

Action names are canonical lowercase dotted identifiers. Runtime and cooked data
may use a stable hash for fast lookup, but tools retain and verify the complete
name to detect collisions.

Action value types:

| Type | Meaning | Example |
| --- | --- | --- |
| `digital` | Inactive or active | Jump, fire, accept |
| `axis1d` | One signed scalar | Move forward/back, trigger pressure |
| `axis2d` | Two signed components | Move vector, look delta, UI navigation |

An initial V1 should avoid three-dimensional actions. A flight editor camera can
combine named one- and two-dimensional actions without requiring an `axis3d`
serialization contract.

### 4.2 Context

A context is a named set of active action bindings, such as:

- `gameplay`
- `gameplay.dead`
- `spectator`
- `menu`
- `console`
- `binding_capture`
- `tile_editor.viewport`
- `tile_editor.text_entry`

The file declares context relationships and policies. Runtime code activates and
deactivates contexts according to game and UI state. Context activation is not a
CYKV expression and does not read CVars.

### 4.3 Binding

A binding maps one or more controls to an action. It includes:

- stable binding ID or name;
- device class;
- control path;
- trigger;
- optional chord modifiers;
- optional composite contribution;
- processors;
- consuming or pass-through policy;
- display/glyph information when needed.

### 4.4 Trigger

Candidate V1 triggers:

- `down`: active while a digital control is held;
- `press`: one event on inactive-to-active transition;
- `release`: one event on active-to-inactive transition;
- `repeat`: bounded UI-style repeat after delay;
- `threshold`: analog crossing with hysteresis;
- `hold`: fires after a bounded duration;
- `tap`: press and release inside a bounded duration.

Complex sequences and fighting-game command recognizers should be separate
gameplay systems until required.

### 4.5 Processor

Candidate V1 processors:

- scale;
- invert;
- clamp;
- axial dead zone;
- radial dead zone;
- normalized response curve;
- sensitivity;
- one-dimensional contribution to a composite axis.

All numeric processor values must be finite. Dead zones and curve parameters have
explicit ranges. Processor order is part of the binding contract.

## 5. Canonical Control Names

Source files use canonical string tokens, never native integer codes.

Examples:

```text
keyboard.w
keyboard.space
keyboard.left_shift
keyboard.escape
mouse.left
mouse.right
mouse.middle
mouse.delta_x
mouse.delta_y
mouse.wheel_x
mouse.wheel_y
gamepad.south
gamepad.east
gamepad.west
gamepad.north
gamepad.left_shoulder
gamepad.right_shoulder
gamepad.left_trigger
gamepad.right_trigger
gamepad.left_stick_x
gamepad.left_stick_y
gamepad.right_stick_x
gamepad.right_stick_y
gamepad.dpad_up
gamepad.dpad_down
gamepad.dpad_left
gamepad.dpad_right
```

Canonical names describe logical controls within a standardized device class.
The platform backend maps native controls into that vocabulary.

Rules:

- lowercase ASCII;
- dotted device namespace;
- stable across platforms;
- no transient SDL instance identifiers;
- no localized display name in the identity;
- no glyph filename in the identity;
- aliases resolved by tools and rewritten canonically;
- unknown controls are errors for the declared target/device support matrix.

Text characters are not controls. A binding cannot target `text.a`; text input is
routed through the platform text/IME contract.

## 6. Developer `.cyinput` Source

### 6.1 Header

```cykv
@cykv 1
@schema "cypher.input" 1
```

The first source generation should use implemented CYKV 1. CYKV 2 includes and
constants can be admitted in a later schema generation after dependency handling
exists in the compiler.

### 6.2 Illustrative document

This example is a proposed schema, not accepted input today:

```cykv
@cykv 1
@schema "cypher.input" 1

{
    map_id = "reap.default"

    actions = {
        player.move = {
            type = "axis2d"
            display = "input.action.player_move"
            rebindable = true
            simulation = true
        }
        player.look = {
            type = "axis2d"
            display = "input.action.player_look"
            rebindable = true
            simulation = true
        }
        player.jump = {
            type = "digital"
            display = "input.action.player_jump"
            rebindable = true
            simulation = true
        }
        console.toggle = {
            type = "digital"
            display = "input.action.console_toggle"
            rebindable = true
            simulation = false
        }
    }

    contexts = {
        gameplay = {
            priority = 100
            consume = true
            actions = [
                "player.move",
                "player.look",
                "player.jump"
            ]
        }
        console = {
            priority = 1000
            consume = true
            actions = ["console.toggle"]
        }
    }

    schemes = {
        keyboard_mouse = {
            device_classes = ["keyboard", "mouse"]
            bindings = [
                {
                    id = "move.forward"
                    action = "player.move"
                    context = "gameplay"
                    control = "keyboard.w"
                    composite = { axis = "y", scale = 1 }
                    trigger = "down"
                },
                {
                    id = "move.back"
                    action = "player.move"
                    context = "gameplay"
                    control = "keyboard.s"
                    composite = { axis = "y", scale = -1 }
                    trigger = "down"
                },
                {
                    id = "look.mouse"
                    action = "player.look"
                    context = "gameplay"
                    controls = ["mouse.delta_x", "mouse.delta_y"]
                    processors = [
                        { type = "scale2d", value = [0.08, -0.08] }
                    ]
                },
                {
                    id = "jump.space"
                    action = "player.jump"
                    context = "gameplay"
                    control = "keyboard.space"
                    trigger = "press"
                }
            ]
        }
    }
}
```

### 6.3 Action fields

Candidate action fields:

| Field | Required | Meaning |
| --- | --- | --- |
| `type` | Yes | `digital`, `axis1d`, or `axis2d` |
| `display` | Yes | Localization key, not literal translated text |
| `rebindable` | No | Defaults true |
| `simulation` | No | Action affects deterministic gameplay input |
| `ui` | No | Action participates in UI navigation |
| `combine` | No | How multiple active bindings combine |
| `clamp` | No | Final legal value range |

Candidate combine policies:

- `latest`
- `maximum_magnitude`
- `sum_clamped`
- `vector_normalized`

A schema must reject policies incompatible with an action's value type.

### 6.4 Context fields

Candidate context fields:

| Field | Required | Meaning |
| --- | --- | --- |
| `priority` | Yes | Deterministic resolution order |
| `consume` | No | Default binding consumption policy |
| `actions` | Yes | Actions legal in this context |
| `exclusive_with` | No | Contexts declared mutually exclusive |
| `parent` | No | Shared policy only; runtime activation remains explicit |

Context inheritance, if admitted, needs cycle detection and one clear override
policy. V1 may omit it and require complete records.

### 6.5 Scheme fields

A scheme defines default bindings for a compatible device-class combination.
Candidate examples:

- `keyboard_mouse`
- `gamepad`
- `left_handed_keyboard_mouse`
- `accessible_single_stick`

The project selects default schemes by platform and device capabilities. A scheme
is not tied to a transient connected-device instance.

## 7. Cooked `CYIN` Resource

Proposed identity:

- extension: `.cyinput_c`;
- resource FourCC: `CYIN`;
- resource version: V1;
- container: CYRS V1.

Candidate chunks:

```text
INMD, alignment 8: header and fixed action/context/scheme/binding records
INST, alignment 1: canonical names, controls, and localization keys
INPR, alignment 4: variable processor and trigger records
```

The final split must follow actual bounded lookup and streaming needs. A small
input map may remain one metadata chunk plus strings.

`CYIN` should persist:

- source and resolved dependency hash;
- stable action IDs and canonical names;
- action type and flags;
- context IDs, priorities, and policies;
- scheme IDs and compatible device classes;
- stable binding IDs;
- canonical controls;
- trigger records;
- chord/composite records;
- ordered processor records;
- conflict-policy metadata needed at runtime;
- localization keys or stable references;
- no native window, SDL, Qt, or device handles.

Tables are sorted canonically. Stable hashes accelerate lookup, while full names
remain available to validate collisions during cooking and inspection.

The cooked format must not contain:

- transient device instance IDs;
- native scancodes as persistent identity;
- user-specific bindings;
- active context state;
- current input state;
- rumble commands;
- text composition buffers;
- network packet layout.

## 8. User `.cybindings` Overrides

User binding data is writable and sparse. It records changes relative to the
compiled defaults instead of copying the entire developer map.

Illustrative schema:

```cykv
@cykv 1
@schema "cypher.input_bindings" 1

{
    input_map = "reap.default"
    base_contract_hash = hex"00112233445566778899aabbccddeeff"
    profile = "default"

    overrides = [
        {
            binding = "jump.space"
            control = "keyboard.left_alt"
        },
        {
            binding = "console.toggle"
            unbound = true
        }
    ]

    calibration = {
        gamepad = {
            left_stick_dead_zone = 0.18
            right_stick_dead_zone = 0.12
        }
    }
}
```

The final schema should support:

- sparse binding replacement;
- explicit unbind tombstones;
- added user bindings when an action permits them;
- sensitivity, inversion, dead-zone, and curve overrides;
- optional device-profile selector;
- base contract hash;
- stable action and binding names/IDs;
- orphan preservation after a game update.

Unknown or orphaned overrides are retained for UI repair and downgrade scenarios
but ignored by runtime evaluation. They produce a bounded diagnostic.

`.cybindings` should not allow includes, macros, build conditionals, or arbitrary
project source paths. A settings UI must be able to load, edit, and atomically
rewrite it without dependency resolution.

## 9. Resolution and Merge Order

Proposed order:

```text
compiled project defaults
    -> target/platform default scheme
    -> device-class default scheme
    -> optional accessibility preset
    -> user profile overrides and unbind tombstones
    -> session-only device assignment
```

Each stage has one explicit replacement policy. Arrays are not merged by numeric
index. Stable action and binding identities drive overrides.

If `base_contract_hash` no longer matches:

1. resolve every override by stable identity;
2. preserve unknown records as orphans;
3. ignore records whose action type became incompatible;
4. report migration results to the settings UI;
5. never discard the previous user file until the replacement is completely
   written and flushed.

## 10. Conflict Policy

A compiler checks conflicts within all context combinations that can be active
together.

Default rules:

- Two exact consuming bindings for the same control in simultaneously active
  contexts are errors unless explicit sharing is declared.
- An exact conflict inside one context is an error.
- Chord-prefix overlap is a warning unless resolution timing is explicit.
- Analog threshold overlap is a warning or error depending on whether both
  actions consume the control.
- Conflicts between contexts declared mutually exclusive are allowed.
- Pass-through actions may intentionally share controls.
- Text entry owns text events independently of physical action bindings.

A conflict diagnostic names both actions, both bindings, both contexts, the
control, the target scheme, and the rule that failed.

Runtime resolution order is deterministic:

1. context priority;
2. explicit binding priority if V1 includes it;
3. chord specificity;
4. canonical binding identity as a final stable tie breaker;
5. conflict error where policy forbids a tie.

## 11. Device and Player Policy

V1 implementation order should be:

1. keyboard and mouse;
2. abstract SDL gamepad controls;
3. device hotplug and reconnect;
4. glyph-family selection;
5. multiple local-player device assignment.

Gamepad source identity uses abstract standardized controls such as
`gamepad.south`, not vendor button numbers. A device mapping layer resolves the
physical hardware.

User device-specific overrides may use a stable mapping GUID and capability set.
They must not store transient runtime instance IDs.

Assignment of a particular connected gamepad to local player two is session or
profile state. It does not belong in a packaged action map.

## 12. Focus and Device-Loss Rules

Losing application focus, changing context ownership, disconnecting a device, or
overflowing an input queue must not leave actions stuck.

The runtime must:

- synthesize release transitions for active controls it can no longer observe;
- clear accumulated relative motion;
- terminate pending hold/tap/repeat triggers;
- reset chord state;
- publish a bounded cancellation reason for diagnostics/replay;
- prevent a console or text field from leaking the activation key into gameplay;
- establish one frame boundary for the reset.

These rules are part of the runtime contract and need synthetic-event tests before
platform integration is considered complete.

## 13. Accessibility

The format family should support accessibility through explicit reusable policies,
not special cases hidden in input code.

Candidate capabilities:

- full rebinding;
- multiple bindings per action;
- toggle instead of hold where gameplay permits;
- hold-duration adjustment;
- repeat-delay and repeat-rate adjustment;
- axis inversion;
- sensitivity and response curve;
- dead-zone calibration;
- chord simplification;
- one-handed schemes;
- simultaneous keyboard/mouse and gamepad use;
- localized action names and correct device glyphs;
- conflict explanations and automatic safe suggestions.

Competitive or deterministic simulation may constrain some transforms. The
manual and UI must state those restrictions rather than silently discarding user
settings.

## 14. Limits

Initial proposed limits:

| Resource | Proposed maximum |
| --- | ---: |
| Actions | 512 |
| Contexts | 64 |
| Schemes | 16 |
| Bindings per scheme | 2,048 |
| Controls in a chord | 4 |
| Controls in a composite | 4 |
| Processors per binding | 8 |
| Triggers per binding | 4 |
| Action/context/binding identifier | 64 bytes |
| Canonical control token | 96 bytes |
| Localization key | 128 bytes |
| Source document | 4 MiB |
| Cooked resource | 8 MiB |
| User override records | 2,048 |

These are design starting points. The first real REAP action set should measure
actual counts before constants are frozen.

## 15. Compiler Validation

`CypherInputCompiler` should validate:

- exact schema and language version;
- canonical unique action, context, scheme, and binding IDs;
- stable hash collisions through complete-name comparison;
- action value types;
- context references and exclusivity graph;
- scheme device-class compatibility;
- canonical control tokens;
- trigger compatibility with control and action types;
- chord and composite shape;
- processor type, order, finite values, and ranges;
- conflict policy across compatible active contexts;
- localization-key shape;
- all count and byte limits;
- deterministic table ordering;
- target capability support;
- complete dependency records if CYKV 2 includes are later allowed.

Compiler output publication is transactional. Validation-only mode does not write
an artifact. A failed cook preserves the previous valid output.

## 16. Runtime Contract

Runtime APIs should expose semantic state such as:

```text
Input_GetDigital(action)
Input_GetAxis1D(action)
Input_GetAxis2D(action)
Input_WasPressed(action)
Input_WasReleased(action)
Input_EnableContext(context)
Input_DisableContext(context)
```

The exact C-style API will be designed with the first gameplay consumer. It should
use generation-checked handles or stable IDs and avoid string lookup in the frame
hot path.

For deterministic simulation, an action frame records the resolved simulation
actions after device processing and before gameplay consumption. It should not
record raw SDL events or native device identifiers. Replay and network encoding
are separate versioned contracts built from the simulation-action subset.

## 17. Diagnostics and Tooling

Required tool operations:

- validate a `.cyinput` file;
- compile `.cyinput` to `.cyinput_c`;
- inspect `CYIN` tables and hashes;
- list actions, contexts, schemes, and bindings;
- simulate a context stack and find conflicts;
- explain why a raw control did or did not produce an action;
- migrate `.cybindings` against a new contract hash;
- show orphaned overrides;
- restore one binding, one action, one scheme, or all defaults;
- capture a new binding without leaking it into active gameplay.

Diagnostics use stable codes and identify exact source fields. The settings UI
must display source conflict and user conflict separately.

## 18. CYKV 2 Interaction

Developer-authored `.cyinput` may eventually use CYKV 2 fragments and typed
constants for shared policies. The input compiler must record every resolved
fragment as a dependency and include the resolved build context in source identity.

Writable `.cybindings` should remain a self-contained document. It should reject
includes, constants, macros, and build conditionals even if the language parser
can represent them.

Non-finite values remain forbidden for:

- sensitivities;
- scales;
- dead zones;
- thresholds;
- response curves;
- hold/tap/repeat durations;
- action values.

## 19. Non-goals

This format family does not define:

- text or IME contents;
- console command syntax;
- editor menu-command implementation;
- raw OS event structures;
- SDL numeric codes;
- Qt key enums;
- rumble effect timelines;
- haptic waveforms;
- voice input;
- network packet layout;
- replay container layout;
- input prediction or rollback protocol;
- anti-cheat policy;
- runtime context activation logic;
- platform account identity.

Some of those areas may later consume the same stable action names.

## 20. Implementation Sequence

1. Freeze and test the existing System keyboard/text/mouse event contract.
2. Add pure keyboard/mouse device-state accumulation and focus-loss recovery.
3. Define a small runtime action/context/binding API for the free camera and REAP
   movement.
4. Author the first action set in tests without creating a permanent source format.
5. Measure real action, context, and binding requirements.
6. Freeze `cypher.input` V1 and implement its typed decoder.
7. Implement conflict validation and the headless input compiler.
8. Freeze `CYIN` V1 and add resource loading.
9. Replace temporary runtime tables with loaded `CYIN`.
10. Add `.cybindings` and atomic user-profile persistence.
11. Build the rebinding/conflict UI.
12. Add SDL gamepad normalization and hotplug.
13. Add deterministic action-frame capture for replay/network work.

This sequence keeps the file contract driven by working input behavior.

## 21. Acceptance Criteria

The V1 family is complete only when:

- keyboard and mouse raw state is proven by synthetic tests;
- gamepad claims match implemented events and hotplug behavior;
- action/context/binding semantics are exact;
- source and cooked versions are explicit;
- all IDs and control names are stable and collision checked;
- the compiler detects invalid types, ranges, references, and conflicts;
- cooked output is deterministic;
- runtime action lookup uses the cooked resource;
- focus/device loss cannot leave an action active;
- user overrides merge by stable identity;
- user files save atomically;
- contract-hash migration preserves orphans;
- the UI can capture, clear, restore, and explain bindings;
- diagnostics identify both sides of conflicts;
- malformed source, cooked, and user files fail transactionally;
- benchmarks cover event accumulation, binding evaluation, and action lookup.

## 22. Open Decisions

- Final source extension: `.cyinput` is recommended but not frozen.
- Whether user files use `.cybindings` or live inside a broader profile format.
- Whether editor application shortcuts share the same schema or use a smaller
  `cypher.tool_keymap` schema.
- Exact stable-ID hash and collision policy.
- Whether binding IDs are authored or derived from action/context/control.
- Whether contexts support inheritance in V1.
- Exact chord timing and chord-prefix policy.
- Which processors are required by the first controller implementation.
- How glyph sets and localized control names are selected.
- Whether action-frame capture becomes part of `CypherInput` or a replay module.
- Final count and byte limits after measuring REAP and Tile Editor action sets.
