# Original editor test textures

`grid.png`, `brick.png`, and `hazard.png` are original, deterministic 128×128
RGBA test patterns authored for this project. They contain no downloaded game
art. They are development materials for checking texture orientation, repeats,
sRGB sampling, stairs, and slot assignments.

Each `.cytex` recipe points to its PNG, declares a color texture with sRGB
encoding, and requests mipmaps. Matching `.cymat` recipes live in
`assets/materials/dev/` and use the shared `tile_surface` shader.

To add a material, copy a matching PNG/`.cytex`/`.cymat` trio under the asset root,
change both resource references, then refresh and cook it in Project Materials.
Use the numeric map slot to bind it and paint or apply it to selected cells.
The slot reference is stored in `.cymap`; the cooked bytes are kept separately.
