# Rolidan

**Fast IrfanView-style image viewer and editor for Linux: paste, annotate, adjust, save.**

Rolidan opens instantly, takes an image from the clipboard or a file, lets you mark it up
and fix it, and saves it or copies it back to the clipboard. It is written in C++20 with Qt 6.

## Features

- **Paste and go** – paste a screenshot with Ctrl+V, edit it, copy the result back with
  Ctrl+Alt+C or save it.
- **Editable annotations** – arrows, lines, polylines, rectangles, ellipses, polygons,
  freehand brush, highlighter, text with per-character formatting, speech bubbles,
  numbered markers and cover/pixelate boxes. They stay editable until you flatten or export.
- **Retouching** – clone stamp with soft edges, eraser, blur and pixelate brushes,
  paint bucket, color picker.
- **Adjustments and filters** – enhance colors, brightness/contrast, hue/saturation,
  temperature, levels, grayscale, sepia, invert, threshold, posterize, blur, sharpen,
  denoise, add noise, clean up scanned documents. All with live preview.
- **Geometry** – crop, resize, canvas size, rotate by 90° or by any angle in 0.1° steps,
  straighten along a drawn line or automatically by the text lines of a document.
- **Formats** – PNG, JPEG, WebP, BMP and ICO, with full JPEG and WebP encoder options
  (quality, chroma subsampling, progressive, lossless, near-lossless…) and a file size estimate.
- **Projects** – `.rolidan` files keep the image, all annotations and Markdown notes.
  Unsaved work is recovered after a crash.
- **Browsing and batch** – step through all images in a folder with PgUp/PgDn, convert,
  shrink and adjust many files at once.
- **Languages** – English, Czech, German, Spanish and Japanese built in.

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/891096d0-8e26-4cf3-82f6-af04a726b2a0" />


## Install

Download `rolidan_<version>+bundled1_amd64.deb` from [Releases](../../releases) and run:

```bash
sudo apt install ./rolidan_0.2.0+bundled1_amd64.deb
```

The package includes its own Qt in `/opt/rolidan`, so it works on Ubuntu 24.04+,
Linux Mint 22+, Debian 13 and other distributions with glibc 2.39 or newer.

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
| Ctrl+Alt+S | Save as project (.rolidan) |
| Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y | Undo, Redo |
| Ctrl+0, Ctrl+1, Ctrl+plus/minus | Fit, 100 %, zoom |
| Space + drag, middle button | Pan |
| Ctrl+R / Ctrl+Shift+R | Rotate right / left |
| Ctrl+Alt+R | Rotate by angle |
| Ctrl+Shift+K | Crop to selection |
| Ctrl+Alt+I / Ctrl+Alt+Shift+I | Resize image / Canvas size |
| Ctrl+Shift+E / B / U / L | Enhance colors / Brightness-contrast / Hue-saturation / Levels |
| Ctrl+H | Hide / show annotations |
| Ctrl+Alt+N | Notes panel |
| Ctrl+B / I / U | Bold / italic / underline while editing text |
| `[` / `]` | Brush size |
| PgDn / PgUp, Home / End | Next / previous, first / last image in the folder |

Tools: **V** pointer · **M** select · **C** crop · **T** text · **A** arrow · **L** line ·
**R** rectangle · **O** ellipse · **B** brush · **H** highlighter · **S** clone stamp ·
**E** eraser · **U** blur brush · **I** color picker · **K** cover · **N** numbered marker ·
**P** polyline · **G** polygon · **Q** speech bubble · **J** pixelate brush · **F** paint bucket ·
**W** straighten

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
- `packaging/build-bundled-deb.sh build` – self-contained `.deb` with Qt bundled

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
