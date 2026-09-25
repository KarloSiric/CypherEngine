# Surfaces

The non-brush, non-mesh authored surfaces - patches, height fields, curve networks and planar regions - and their procedural generators: curves, patch primitives, sweeps and displacements.

All headers here are included by basename; the folder is only for people. See the [Geometry index](../README.md) for the whole layout.

## Files

| Header | Purpose |
| --- | --- |
| `CypherGeometry_CurveNetwork` | The CurveNetwork source representation: nodes, curve segments between them, and paths chaining segments |
| `CypherGeometry_CurveSampling` | Arc-length sampling of CurveNetwork paths with rotation-minimizing frames (RMF) |
| `CypherGeometry_Displacement` | Displacement surfaces (Source-engine style) |
| `CypherGeometry_HeightField` | The HeightField source representation: a regular grid of height samples over the XY plane (Z up), a per-cell hole mask, and tiles that carry identity and dirty state |
| `CypherGeometry_Patch` | The Patch source representation: a rectangular grid of control points interpreted as a tensor-product Bézier surface (biquadratic or bicubic), with per-control UVs and source... |
| `CypherGeometry_PatchPrimitives` | Generators that publish canonical biquadratic Patch sources |
| `CypherGeometry_PatchResize` | Control-grid shrinking for patches: merging two neighbouring sub-patch columns (or rows) into one, keeping the surface as close to its previous shape as a coarser grid allows |
| `CypherGeometry_PlanarRegion` | The PlanarRegion source representation: one or more polygons with holes lying in a shared plane frame |
| `CypherGeometry_PlanarRegionValidation` | Structural and geometric validation for PlanarRegion |
| `CypherGeometry_Sweep` | Profile sweeps along CurveNetwork paths and lathe (surface of revolution) generation into EditableMesh |

## Contracts

Ownership contracts carried over from the module folders that were merged into this one.

### Representations / Patch

#### Owns

Patch type, control cage, basis, tessellation parameters, source IDs, and authored surface attributes.

#### Does not own

Final triangle topology or viewport interaction state.

#### First acceptance gate

Represent and deterministically sample one bounded quadratic patch while retaining control-point provenance.

### Representations / HeightField

#### Owns

Persistent bounded height samples, tile/sample identity, hole masks, geometry
bounds, deterministic sampling, and conversion into validated surface geometry.

#### Does not own

Terrain biomes, paint layers, foliage, streaming policy, gameplay tags, physics
objects, or navigation behavior. Those systems consume geometry snapshots or
source mappings through adapters.

#### First acceptance gate

Store a small tiled height field with one hole, edit a bounded sample region, and
tessellate only the dirty tiles with deterministic boundary stitching and source
mapping.

### Representations / CurveNetwork

#### Owns

Persistent editable curves, network connectivity, basis-specific control data,
stable source identity, parameter domains, and checked immutable views used by
sweeps, lofts, rails, roads, pipes, and other retained path geometry.

#### Does not own

Camera, scripted-sequence, entity, traffic, or gameplay-path semantics. A host
may reference a CurveNetwork from those systems without moving their ownership
into Geometry.

#### First acceptance gate

Store and evaluate one connected cubic curve path, split it without changing its
shape, and preserve source identity and parameter provenance through round-trip
serialization.

### Representations / PlanarRegion

#### Owns

Persistent planar regions containing one or more polygons, outer contours,
holes, stable component identity, and plane-frame provenance.

#### Does not own

3D shell adjacency or triangulated render output.

#### First acceptance gate

Represent a disconnected region containing a polygon with a hole, validate
winding and containment, and round-trip it without losing polygon or contour
identity.

### Procedural

#### Owns

Parametric curves, sweeps, patches, subdivision, and displacement evaluation into neutral fragments or checked representations.

#### Does not own

Universal fields on base mesh records or permanent host scene ownership.

#### First acceptance gate

Each family has an isolated parameter schema, evaluator, limits, conversion contract, and deterministic fixture.

### Procedural / Curves

#### Owns

CurveNetwork evaluation from immutable source views, arc-length tables,
parameter sampling, and stable frames for geometry generation.

#### Does not own

Persistent control points, knots, rational weights, basis/type, parameter-domain
identity, or editor path-tool interaction. Those authored fields belong to the
CurveNetwork source representation.

#### First acceptance gate

Evaluate bounded curves and stable frames at canonical parameters.

### Procedural / Patches

#### Owns

Patch generators and checked conversion to the Patch source representation.

#### Does not own

Patch viewport control manipulation.

#### First acceptance gate

Generate canonical planar and curved patch fixtures.

### Procedural / Sweeps

#### Owns

Pure profile-along-path, lathe, and loft evaluation with seam, frame,
tessellation, and provenance policy. It evaluates supplied parameters; it does
not decide whether those parameters are transient command inputs or retained by
a Modifier/source recipe.

#### Does not own

Arbitrary Boolean cleanup.

#### First acceptance gate

Sweep one profile without frame flips and retain profile/path provenance.

### Procedural / Displacement

#### Owns

Bounded displacement modifier descriptors, sampling, refinement policy, and
source mapping over immutable inputs.

#### Does not own

Persistent HeightField tiles, samples, holes, or identity; terrain gameplay
semantics; or sculpt-brush input.

#### First acceptance gate

Displace a fixture deterministically within vertex and scratch budgets.
