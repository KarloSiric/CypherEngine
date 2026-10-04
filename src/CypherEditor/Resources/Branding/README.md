# Mason branding

`mason_source.png` is the exact 1254 × 1254 PNG supplied by Karlo on
2026-10-04. Its artwork, proportions, colours and background are preserved.
Keep this original for provenance. The production icon uses
`mason_borderless_source.png`: the outer blue surround was removed at Karlo's
request, leaving the dark rounded tile and transparent corners. Keep the
trowel, grid, orange frame and bricks as the Mason mark; do not substitute
editor tool icons or theme colours.

Source SHA-256:
`6b1bd6c3d19efe428210c4dc147702578a3b11fd92eeae9f3b1a312fa155f97c`

Borderless source SHA-256:
`1e451ff243f7283c26a5c0e89235343f1ba9cf8490196a82b09ce0c3b228256c`

The borderless source was edited with the built-in image-generation tool on
2026-10-04, using the supplied high-resolution PNG and Karlo's tight thumbnail
as the crop reference. This is an edited asset, not a claim of pixel-identical
background extraction. The original PNG remains intact. The edit prompt was:

> Use case: background-extraction. Asset type: existing Mason editor application
> icon, transparent PNG. Image 1 is the high-resolution edit target; Image 2 is
> the user's tight borderless example and crop reference. Remove ONLY the outer
> blue rectangular surround/padding outside the dark rounded-square icon in
> Image 1. Keep the entire dark rounded-square tile, inner blue grid, orange
> incomplete frame and orange bricks, and the original trowel exactly unchanged
> in shape, proportion, colors, lighting and texture. Do not redesign, redraw,
> simplify, add a shadow, glow, outline, frame or extra border. Crop/reframe
> tightly to the rounded-square tile so it fills the square canvas, as in Image
> 2. Outside the rounded corners must be genuine transparent alpha, not blue,
> black, white or a checkerboard. Preserve the original artwork including all
> orange frame edges and trowel. Produce one high-resolution square icon
> suitable for generating 1024, 512 and64px application icons.

The packaged derivatives are:

| File | Purpose |
| --- | --- |
| `mason_64.png` | Small Qt application/window icons |
| `mason.png` | 512 × 512 Qt logo, startup screen and welcome window |
| `mason_1024.png` | High DPI Qt application icons |
| `mason.icns` | macOS application bundle, Finder and Dock |

`Mason_ApplicationIcon()` provides the shared Qt icon. The application, main
window, welcome window, startup window and About dialog all use it. The startup
artwork also draws `mason.png` directly. The welcome and startup logos are 96
logical pixels so the tighter crop remains compact. The obsolete, unused SVG
logo is removed.
Windows and Linux use the shared Qt runtime icon; native executable/desktop
icon packaging for those platforms is not configured yet.

To regenerate the derivatives on macOS, run from this directory:

```sh
sips --resampleHeightWidth 64 64 mason_borderless_source.png --out mason_64.png
sips --resampleHeightWidth 512 512 mason_borderless_source.png --out mason.png
sips --resampleHeightWidth 1024 1024 mason_borderless_source.png --out mason_1024.png

mason_icon_tmp=$(mktemp -d)
mkdir "$mason_icon_tmp/mason.iconset"
while read -r name size; do
    sips --resampleHeightWidth "$size" "$size" mason_borderless_source.png \
        --out "$mason_icon_tmp/mason.iconset/$name.png"
done <<'SIZES'
icon_16x16 16
icon_16x16@2x 32
icon_32x32 32
icon_32x32@2x 64
icon_128x128 128
icon_128x128@2x 256
icon_256x256 256
icon_256x256@2x 512
icon_512x512 512
icon_512x512@2x 1024
SIZES
iconutil -c icns -o mason.icns "$mason_icon_tmp/mason.iconset"
rm -r "$mason_icon_tmp"
```

The build embeds the PNGs through `CypherEditorResources.qrc` and copies
`mason.icns` into `Mason.app/Contents/Resources`. These size/format exports are
deterministic resamples of the borderless source; regeneration does not require
another image-generation call.
