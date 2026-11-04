# Rolidan

**Fast IrfanView-style image viewer and editor for Linux: paste, annotate, adjust, save.**

Rolidan opens instantly, takes an image from the clipboard or a file, lets you mark it up
and fix it, and saves it or copies it back to the clipboard. It is written in C++20 with Qt 6.

## Features

- **Paste and go** – paste a screenshot with Ctrl+V, edit it, copy the result back with
  Ctrl+Alt+C or save it.
- **Screenshots** – the Screenshot button in the toolbar (or C) minimizes Rolidan, takes the
  screenshot and comes back with it already pasted. The arrow next to it chooses the whole
  screen or an area/window, the field after it a delay. Works on Wayland and X11 (GNOME,
  KDE and others through the desktop portal).
- **Editable annotations** – arrows, lines, polylines, rectangles, ellipses, polygons,
  freehand brush, highlighter, text with per-character formatting, speech bubbles,
  numbered markers and cover/pixelate boxes. Move, resize and freely rotate them with the
  mouse (Shift snaps to 15°). They stay editable until you flatten or export.
- **Retouching** – clone stamp with soft edges, eraser, blur and pixelate brushes,
  paint bucket, color picker.
- **Adjustments and filters** – enhance colors, brightness/contrast/gamma, hue/saturation,
  temperature, levels, grayscale, sepia, invert, threshold, posterize, blur, sharpen,
  denoise, add noise, clean up scanned documents. All with live preview.
- **Geometry** – crop, resize (px, cm, inches or percent of width and height, presets, dpi,
  Lanczos / Mitchell / Hermite / Triangle / Bell / B-spline / nearest neighbor, sharpen after),
  canvas size, rotate by 90° or by any angle in 0.1° steps,
  straighten along a drawn line or automatically by the text lines of a document.
- **Formats** – saves PNG, JPEG, WebP, BMP, ICO, TIFF, AVIF, HEIC, JPEG XL and QOI, with full
  JPEG and WebP encoder options (quality, chroma subsampling, progressive, lossless,
  near-lossless…) and a file size estimate. Also opens GIF, TGA, SVG, PSD, XCF, OpenEXR,
  JPEG 2000, camera RAW files (CR2, NEF, ARW, DNG…) and more.
- **Projects** – `.rolidan` files keep the image, all annotations and Markdown notes.
  Unsaved work is recovered after a crash.
- **Viewer** – step through a folder with PgUp/PgDn, the arrow keys in full screen or the
  mouse wheel, thumbnail strip, full screen (optionally enlarging small images), slideshow,
  recent files, image information with EXIF and GPS location, rename, copy or
  move to another folder, move to trash.
- **Printing** – fit to page, actual size at a chosen dpi or a percentage, orientation,
  margins and a live preview. Printing to PDF works without a printer.
- **Batch conversion** – convert, shrink and adjust many files at once.
- **Languages** – English, Czech, German, Spanish and Japanese built in.

- <img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/d50ca789-aad1-4249-aa7b-fb9d19eda421" />


## Install

Download a file from [Releases](../../releases). All of them include Qt and run on
x86_64 distributions with glibc 2.38 or newer: Ubuntu 24.04+, Linux Mint 22+, Debian 13,
Fedora 39+, openSUSE Tumbleweed, Arch, Manjaro and others.

| File | Distribution | How to use |
|---|---|---|
| `Rolidan-<version>-x86_64.AppImage` | any | `chmod +x Rolidan-*.AppImage`, then run it |
| `rolidan_<version>+bundled1_amd64.deb` | Ubuntu, Mint, Debian | `sudo apt install ./rolidan_*.deb` |
| `rolidan-<version>-1.bundled.x86_64.rpm` | Fedora, openSUSE | `sudo dnf install ./rolidan-*.rpm` or `sudo zypper install ./rolidan-*.rpm` |
| `rolidan-<version>-x86_64.tar.xz` | any | unpack, run `./rolidan`; `./install.sh` adds it to the menu |

The AppImage needs FUSE, which most desktops have. If it does not start, run it with
`--appimage-extract-and-run`.

## Usage

| Command | Effect |
|---|---|
| `rolidan` | empty window, waiting for Ctrl+V |
| `rolidan image.png` | open a file |
| `rolidan --paste` | paste the clipboard on start (handy for a global shortcut) |
| `grim -g "$(slurp)" - \| rolidan -` | read an image from standard input |

### Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Ctrl+V / Ctrl+Shift+V | Paste / New window from clipboard |
| Ctrl+C / Ctrl+Shift+C | Copy image / Copy image with annotations |
| Ctrl+Alt+C | Copy the whole result |
| Ctrl+O, Ctrl+S, Ctrl+Shift+S | Open, Save, Save As |
| Ctrl+P | Print |
| C | Capture screen |
| Ctrl+Alt+S | Save as project (.rolidan) |
| Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y | Undo, Redo |
| Ctrl+0, Ctrl+1, Ctrl+plus/minus | Fit, 100 %, zoom |
| Space + drag, middle button | Pan |
| R or > / L or < | Rotate right / left |
| H / V | Flip horizontally / vertically |
| Ctrl+Alt+R | Rotate by angle |
| Ctrl+Shift+K | Crop to selection |
| Ctrl+R / Ctrl+Alt+Shift+I | Resize image / Canvas size |
| Ctrl+Shift+E / B / U / L | Enhance colors / Brightness-contrast / Hue-saturation / Levels |
| Ctrl+H | Hide / show annotations |
| Ctrl+Alt+N | Notes panel |
| Ctrl+B / I / U | Bold / italic / underline while editing text |
| `[` / `]` | Brush size |
| PgDn / PgUp, Home / End | Next / previous, first / last image in the folder |
| F, Enter, F11 / F5 | Full screen / Slideshow |
| ← → in full screen | Previous / next image |
| Z | Enlarge small images in full screen |
| Ctrl+T | Thumbnails |
| Ctrl+I | Image information (EXIF, GPS) |
| F2 / F7 / F8 / Ctrl+Delete | Rename / Move to folder / Copy to folder / Move to trash |

Tools: **Shift+V** pointer · **M** select · **Y** crop · **T** text · **A** arrow · **Shift+L** line ·
**Shift+R** rectangle · **O** ellipse · **B** brush · **Shift+H** highlighter · **S** clone stamp ·
**E** eraser · **U** blur brush · **I** color picker · **K** cover · **N** numbered marker ·
**P** polyline · **G** polygon · **Q** speech bubble · **J** pixelate brush · **Shift+F** paint bucket ·
**W** straighten. Shift + letter always selects a tool.

All shortcuts can be changed in *File › Keyboard Shortcuts*.

## Building from source

Debian 13 / Ubuntu 25.04 or newer (Qt 6.8+):

```bash
sudo apt install build-essential cmake ninja-build pkgconf \
  qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-l10n-tools qt6-translations-l10n \
  qt6-image-formats-plugins qt6-wayland libjpeg62-turbo-dev libwebp-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/rolidan
```

Install with `sudo cmake --install build`, or build packages:

- `cmake --build build --target package` – `.deb` using the system Qt
- `packaging/build-release.sh build` – self-contained `.deb`, `.tar.xz`, AppImage and `.rpm`
  with Qt bundled, written to `build/dist/` (AppImage needs `squashfs-tools`, RPM needs `rpm`)

Run the tests with `cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure`.

## Translations

Source strings are in English; translations live in `i18n/*.ts`. After changing texts run
`cmake --build build --target update_translations`. An extra language can be added without
rebuilding: put `rolidan_<code>.qm` into `~/.local/share/rolidan/translations/`.

## License

Rolidan is free software under the [GNU General Public License v3.0](LICENSE).

The bundled `.deb` contains unmodified libraries from Debian, including Qt 6 (LGPL-3.0),
libjpeg-turbo and libwebp (BSD). Their licenses are installed in
`/usr/share/doc/rolidan/third-party/`.
