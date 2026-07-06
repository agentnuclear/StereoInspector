# StereoInspector

Real-time VR stereo inconsistency detection tool for Windows. Captures side-by-side VR output via DXGI Desktop Duplication, computes dense stereo correspondence, warps the right eye to the left coordinate system, and compares aligned images to detect genuine rendering defects — without false positives from natural binocular parallax.

![Grade: 62/100](https://img.shields.io/badge/maturity-62%2F100-yellow) ![Build](https://img.shields.io/badge/build-passing-brightgreen) ![Platform](https://img.shields.io/badge/platform-Windows-blue)

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

# Headless data collection mode:
StereoInspector.exe --collect D:\dataset
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
│  Issues tab     │    │  20 types    │    │  + 14 analyzers │
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
| F8 | Start/stop recording (screenshot every 30 frames) |
| F9 | Set baseline (capture stereo model) |

---

## Issue Classification (20 Types)

Lighting, Shadow, Bloom, Reflection, Texture, Material, Transparency, Edge, Missing/Extra Geometry, Missing Object/Particle/UI, Text, Stereo Offset, Depth/Disparity Error, Occlusion, Post-Process, Temporal, Lens Boundary, Low Confidence.

Each issue is scored with heuristic features (brightness diff, contrast diff, edge similarity, texture similarity, etc.) and classified using scoring functions with configurable coefficients. Click any issue in the Issues tab to highlight it on the viewport.

---

## Tools

The `tools/` directory contains two utilities for classifier improvement:

### `tools/synthetic_dataset_gen.cpp`
Standalone tool (links OpenCV + nlohmann_json only) that creates labeled synthetic stereo defect datasets from any source image. Outputs PNGs + JSONL with perfect ground truth labels.

```
gen_synthetic testpattern D:\dataset --samples 200 --size 1920x1080
gen_synthetic C:\photo.jpg D:\dataset --samples 100
```

### `tools/optimize_classifier.py`
Python script ports all 20 scoring functions, loads JSONL datasets with ground truth labels, runs scipy-based optimization (L-BFGS-B + differential evolution) with train/eval splitting and L2 regularization. Outputs a ready-to-include C++ header with tuned coefficients.

```
python tools/optimize_classifier.py dataset.jsonl
```

### `tools/best_optimized_coeffs.h`
Best optimized coefficients from a 210-sample multi-source dataset. These improve margin-based loss 4x but require real VR capture data for definitive calibration.

---

## Classifier Calibration & Real VR Capture

> **⚠️ Important:** The classifier currently uses heuristic coefficients tuned on synthetic data. To achieve calibrated, trustworthy confidence percentages — where "87% confidence" actually means P(correct) ≈ 0.87 — the system needs **real VR capture data** with ground-truth labels.

The synthetic data generator and optimizer provide a strong foundation, but the definitive calibration requires:
1. Capturing 100+ frames from real VR applications (via `--collect` mode)
2. Manually labeling each detected issue with its correct type
3. Re-running `optimize_classifier.py` against the real dataset

This is the single most impactful improvement remaining for the project. See [devdoc.md](devdoc.md) and [nextstep.md](nextstep.md) for details.

---

## Current Status: 62/100 — Improving Prototype

**8 critical bugs from the initial audit are fixed.** The correspondence-first approach is correct, and the Diff Ov view with colored bounding boxes helps developers spot stereo defects. Recent improvements include:

- Configurable detection thresholds (diffThreshold, minIssueArea, minIssueConfidence)
- FeatureCache (ORB computed once per frame instead of 3-5×)
- Pixel-wise confidence weighting for SSIM and pixel diff
- Grayscale conversion cache (saves ~26 cvtColor calls per frame)
- Headless data collection mode (`--collect`)
- Synthetic data generator + Python optimizer for classifier calibration
- Capture FPS tracking (fixed dead variable)
- Tesseract DLL auto-deployment
- CheckToggles (32 per-check enable/disable booleans)
- Config-driven health score thresholds

### Remaining Issues
- Classifier coefficients need real VR data for calibration (see above)
- No GPU acceleration — all analysis runs on CPU
- Limited test coverage
- No CI/CD pipeline
- FrameQueue removed; single-frame buffer only

See [nextstep.md](nextstep.md) for the full improvement roadmap.

---

## License

MIT
