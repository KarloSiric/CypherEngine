# Operations

Cross-representation authoring tools: primitives, modifier stacks, snapping and workplanes, planar extrude, UV alignment and unwrapping, and vertex painting.

All headers here are included by basename; the folder is only for people. See the [Geometry index](../README.md) for the whole layout.

## Files

| Header | Purpose |
| --- | --- |
| `CypherGeometry_MeshPaint` | The blend-painting brush for editable meshes (Hammer's Paint tool): painting per-corner color channels, which blend materials read as blend weights |
| `CypherGeometry_MeshSurfacing` | Surfacing operations for editable meshes: UV projections, material and smoothing-group assignment, and automatic hard-edge marking |
| `CypherGeometry_MeshUvIslands` | Source-ID-addressed UV seam editing and deterministic UV-island discovery for editable mesh sources |
| `CypherGeometry_MeshUvTransform` | The face-edit UV tools for editable meshes (Hammer's Face Edit sheet): justify and fit, shift / rotate / scale about a pivot, align-to-face re-projection, and hotspot texturing |
| `CypherGeometry_ModifierStack` | Modifier stacks: an ordered list of non-destructive operations (mirror, linear array, radial array, bend, taper, subdivide) evaluated over an authored mesh, with provenance, and... |
| `CypherGeometry_PlanarExtrude` | Polygon extrusion: a PlanarRegion (polygons with holes) swept along its frame normal into a closed EditableMesh |
| `CypherGeometry_Primitive` | The primitive creation front end: one parameter record for every shape the create tools offer (box, ramp, pyramid, cylinder, cone, sphere, torus, plane, arch, stairs), built as... |
| `CypherGeometry_Snap` | Deterministic snapping functions for geometry editing |
| `CypherGeometry_UvAlignment` | UV alignment and justification operations for brush side attributes |
| `CypherGeometry_UvTools` | The UV-view manipulations for a face's planar projection |
| `CypherGeometry_Workplane` | Workplanes: a local grid frame that tools draw, snap, and place on - Hammer's "set workplane to face" for building on sloped surfaces |

## Contracts

Ownership contracts carried over from the module folders that were merged into this one.

### Operations

#### Owns

Atomic transforms, cuts, topology edits, modeling operations, and representation conversions composed through transactions.

#### Does not own

Tool input, gizmo drawing, global undo, or operations that publish partially invalid topology.

#### First acceptance gate

Every operation declares preconditions, affected representations, attribute propagation, remap output, validation, and inverse behavior.

### Operations / Conversion

#### Owns

Brush-to-mesh, face-to-patch, polygon extrusion, triangulation, and coplanar face merge conversions.

#### Does not own

Silent representation replacement. Conversion is explicit and may be destructive only through a transaction.

#### First acceptance gate

Convert a brush box to an editable mesh while preserving side, face, material, UV, and document provenance.

### Constraints

#### Owns

Pure deterministic resolution of grid, angle, axis, vertex, edge, face, surface,
normal-direction, and user-defined geometry constraints from bounded candidate
sets. Ranking declares distance spaces, priority, stable tie-breaking, and the
exact result/provenance used by preview transactions.

Snapping changes a proposed coordinate or transform. It never implies a weld or
any other topology mutation.

#### Does not own

Pointer gestures, active workplane/tool state, viewport pixels, hotkeys, gizmo
rendering, sticky UI behavior, or candidate discovery. The host supplies mode
and gesture context; Spatial supplies geometry candidates.

#### First acceptance gate

Resolve bounded grid and component candidates idempotently with deterministic
negative-half and equal-distance tie behavior across input permutations.

### Primitives

#### Owns

Deterministic parameter records and generators for planes, boxes, wedges, prisms, pyramids, cylinders, cones, spheres, arches, and stairs.

Initial descriptors are transient pure inputs that emit checked Brush, Mesh,
PlanarRegion, or Patch data. Retaining a creation recipe as an authored source
requires a later explicit `ProceduralRecipe` contract; a parameterized creation
dialog alone does not make the descriptor persistent geometry.

Low-level generators depend only on Kernel and checked representation builders.
An architectural creation command that needs clipping or CSG belongs in the
later operation layer even when its parameter record remains here.

#### Does not own

Document mutation, serialization of unapproved retained recipes, or thousands of
hard-coded shape algorithms. Large shape libraries are data-driven templates
built from tested generators.

#### First acceptance gate

Generate one canonical six-plane box at valid and rejected parameter boundaries.
Wedge, arch, and stair creation close later architectural-operation gates.

### Modifiers

#### Owns

Optional retained recipes for non-destructive mirror, linear/radial array, bend,
taper, and procedural evaluation, if an exercised authoring workflow justifies
persistent parameter ownership. A recipe records versioned inputs, source
dependencies, policy, and evaluation provenance.

#### Does not own

The underlying procedural evaluator, base mesh adjacency, transactional
bake/collapse, or host scene-object stacks.

#### First acceptance gate

After retained recipes are approved, evaluate a mirror and bounded array
reproducibly through Procedural and collapse them transactionally through
Operations while preserving provenance.
