# StereoInspector

Real-time VR stereo inconsistency detection tool for Windows. Captures side-by-side VR output via DXGI Desktop Duplication, computes dense stereo correspondence, warps the right eye to the left coordinate system, and compares aligned images to detect genuine rendering defects — without false positives from natural binocular parallax.

![Grade: 40/100](https://img.shields.io/badge/maturity-40%2F100-yellow) ![Build](https://img.shields.io/badge/build-passing-brightgreen) ![Platform](https://img.shields.io/badge/platform-Windows-blue)

---

## Quick Start

**Prerequisites:** Windows 10/11, Visual Studio 2022 (C++ workload), Git.

```powershell
# One-time dependency install (OpenCV, spdlog, nlohmann-json, Dear ImGui)
.\setup.ps1

# Build
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="$env:USERPROFILE\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build . --config Release

# Run
cd Release
StereoInspector.exe
```

---

## How It Works

```
┌─────────────────┐    ┌──────────────┐    ┌─────────────────┐    ┌──────────────┐
│  DXGI Capture   │ →  │ Stereo Split │ →  │  Correspondence │ →  │   Warp R→L   │
│  (desktop dup)  │    │  (auto SBS)  │    │  (SGBM/BM/OF/F) │    │  (remap)      │
└─────────────────┘    └──────────────┘    └─────────────────┘    └──────┬───────┘
                                                                        │
┌─────────────────┐    ┌──────────────┐    ┌─────────────────┐          │
│  Overlay (DX11) │ ←  │  Classify &  │ ←  │  Aligned Diff   │ ←────────┘
│  10 viz modes   │    │  Merge       │    │  SSIM/pix/hist  │
│  Issues tab     │    │  22 types    │    │  + 14 analyzers │
└─────────────────┘    └──────────────┘    └─────────────────┘
```

The core insight: naive L vs R pixel comparison flags parallax as differences. By computing disparity, warping R→L, and comparing *aligned* images, the residual shows only genuine rendering inconsistencies.

---

## Visualization Modes

| Key | Mode | Description |
|-----|------|-------------|
| F1 | Normal | Full side-by-side with mini trend graphs |
| F3 | Stereo Difference Overlay | Left eye with color-coded issue bounding boxes |
| F4 | Difference Heatmap | Aligned residual (green→yellow→red colormap) |
| F5 | Toggle Normal / Heatmap | Quick switch |
| Ctrl+F1 | Edge Comparison | Canny edges: red=L, green=R, yellow=both |
| Ctrl+F2 | Feature Match Overlay | ORB feature matches as lines |
| Ctrl+F3 | Histogram View | Overlaid grayscale histograms (blue=L, green=R) |
| Ctrl+F4 | Blur Map | Laplacian variance heatmap (Inferno) |
| Ctrl+F5 | Blink Left | Left eye full screen |
| Ctrl+F6 | Blink Right | Right eye full screen |
| Ctrl+F7 | Disparity Heatmap | Dense disparity map (JET colormap) |

## Other Hotkeys

| Key | Function |
|-----|----------|
| F1 | Toggle overlay |
| F2 | Freeze/unfreeze analysis |
| F6 | Screenshot |
| F7 | Start/stop logging (CSV + JSON) |
| F8 | Start/stop recording |
| F9 | Set baseline (capture stereo model) |

---

## Issue Classification (22 Types)

Lighting, Shadow, Bloom, Reflection, Texture, Material, Transparency, Edge, Missing/Extra Geometry, Missing Object/Particle/UI, Text, Stereo Offset, Depth/Disparity Error, Occlusion, Post-Process, Temporal, Lens Boundary, Low Confidence.

Each issue is scored with heuristic features (brightness diff, contrast diff, edge similarity, texture similarity, etc.) and classified using hand-tuned scoring functions. Click any issue in the Issues tab to highlight it on the viewport.

---

## Current Status: 40/100 — Promising Prototype

This tool is **genuinely useful for qualitative visual debugging** — the Diff Ov view with colored bounding boxes and type labels helps developers spot stereo defects. However, the numerical outputs (confidence percentages, integrity scores) are **uncalibrated heuristics, not real probabilities**.

### Known Issues
- Chromatic asymmetry metric is declared but never computed
- Blur and texture asymmetry are aliased to the same value
- Scene confidence threshold (0.35) is overly conservative
- Issue classifier uses 440+ untuned heuristic weights
- Feature-based disparity method uses crude nearest-neighbor interpolation
- No feature cache — ORB runs 3-5× per frame unnecessarily
- Recording hotkey (F8) is a no-op

See [nextstep.md](nextstep.md) for full analysis and improvement roadmap.

---

## License

MIT
