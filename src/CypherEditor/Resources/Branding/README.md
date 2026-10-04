# Mason branding

`mason_source.png` is the exact 1254 × 1254 PNG supplied by Karlo on
2026-10-04. Its artwork, proportions, colours and background are preserved.
Use this file as the authoritative source for all Mason branding; do not
recreate the logo with editor tool icons or theme colours.

Source SHA-256:
`6b1bd6c3d19efe428210c4dc147702578a3b11fd92eeae9f3b1a312fa155f97c`

The packaged derivatives are:

| File | Purpose |
| --- | --- |
| `mason_64.png` | Small Qt application/window icons |
| `mason.png` | 512 × 512 Qt logo, startup screen and welcome window |
| `mason_1024.png` | High DPI Qt application icons |
| `mason.icns` | macOS application bundle, Finder and Dock |

`Mason_ApplicationIcon()` provides the shared Qt icon. The application, main
window, welcome window, startup window and About dialog all use it. The startup
artwork also draws `mason.png` directly. The obsolete, unused SVG logo is removed.
Windows and Linux use the shared Qt runtime icon; native executable/desktop
icon packaging for those platforms is not configured yet.

To regenerate the derivatives on macOS, run from this directory:

```sh
sips --resampleHeightWidth 64 64 mason_source.png --out mason_64.png
sips --resampleHeightWidth 512 512 mason_source.png --out mason.png
sips --resampleHeightWidth 1024 1024 mason_source.png --out mason_1024.png

mason_icon_tmp=$(mktemp -d)
mkdir "$mason_icon_tmp/mason.iconset"
while read -r name size; do
    sips --resampleHeightWidth "$size" "$size" mason_source.png \
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
`mason.icns` into `Mason.app/Contents/Resources`. No image generation or visual
redesign is involved in producing these size/format variants.
