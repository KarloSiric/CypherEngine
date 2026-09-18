# Attributes

## Owns

Two dependency levels with an explicit boundary:

- `Schema` owns typed domains, descriptors, values, opaque material references,
  UV projection records, and bounded representation storage;
- `Propagation` owns operation-specific interpolation, generated-face policy,
  texture locking, normals/tangents, smoothing, creases, conflicts, and
  provenance.

## Does not own

Material asset loading, shader binding, or texture-browser UI.

## First acceptance gate

Validate deterministic brush-side schema/storage first, then preserve material
and world-locked texture projection through a transactional brush-side drag and
deterministic tessellation.
