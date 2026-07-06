# Stereo Inspector User Guide

## Overview

Stereo Inspector is a Windows desktop application that analyzes side-by-side VR
stereo output in real time using a **correspondence-first** approach. Instead of
naively comparing left vs right pixels (which treats binocular parallax as a
difference), it first computes dense stereo correspondence (disparity), warps
the right eye to the left coordinate system, then compares aligned images. This
eliminates false positives from parallax and detects genuine rendering defects.

It captures your desktop via DXGI Desktop Duplication, splits frames into
left/right eye regions, runs 14 analysis modules on warped (aligned) images,
and displays results via a transparent always-on-top DirectX 11 overlay with
6 independent multi-window panels.

---

## Quick Start

### 1. Build & Run

**Prerequisites**:
- Windows 10/11 with DirectX 11 compatible GPU
- Visual Studio 2022 with "Desktop development with C++" workload
- [VC++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe)
- Git

**Step 1 — Install dependencies** (one time):
```powershell
.\setup.ps1
```
This installs vcpkg, downloads and builds OpenCV, spdlog, and nlohmann-json,
then downloads Dear ImGui source files into `deps/imgui/`.

> You can optionally pass `-InstallTesseract` to enable OCR support.

**Step 2 — Configure and build**:
```powershell
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build . --config Release
```

**Step 3 — Run**:
```powershell
cd Release
StereoInspector.exe
```

The application opens a transparent overlay across your screen and immediately
begins capturing and analyzing stereo content.

**Smart Layout Detection**: Auto-detects stereo layout (horizontal side-by-side,
over/under, wide, half-res) using histogram correlation, edge similarity, and
separator darkness scoring.

---

## Consolidated Overlay Layout

The overlay is organized into **3 main elements** for a cleaner, more
readable experience:

### 1. Hub Window (main control panel)

A single resizable window with **4 tabs**:

| Tab | Contents |
|-----|----------|
| **Status** | Sync/Clear buttons, visualization mode buttons (Normal/Diff Ov/Heatmap/Edges/Disp/Feat/Hist/Blur/L), then expandable Frame Details (L2), Raw Details (L3) |
| **Metrics** | Per-metric comparison against baseline (Base, Cur, Δ columns) grouped into collapsible sections: Alignment (L1), Disparity & Asymmetry (L2), All Metrics (L3). Raw values shown when no baseline |
| **Issues** | Baseline deviations with colored severity tags (FAIL/WARN/OK), region issues grouped by type into collapsible categories with click-to-select and hover tooltips |
| **Graphs** | 8 sparkline graphs: Health, SSIM, Disp, Bright (row 1); Stability, AlignSSIM, Flicker, DispStab (row 2) |

### 2. Visualization Window

An independent resizable window showing the current mode's rendered output
(Normal, Diff Ov, Heatmap, Disp, Edges, etc.). Can be dragged to a different
monitor.

### Progressive Disclosure (Level 1 / 2 / 3)

| Level | Visibility | Contents |
|-------|-----------|----------|
| **L1** | Always visible | Tab headers, controls |
| **L2** | One click (default open) | Frame Details, Alignment metrics, issue categories |
| **L3** | One click (collapsed) | Raw debug details, all 10+ metrics |

Press **F1** to toggle overlay visibility on/off.

---

## Status & Scene Confidence

The **Status** panel shows the current stereo health:

| Status | Color | Meaning |
|--------|-------|---------|
| **SAFE** | Green | Stereo within baseline tolerance (integrity ≥ 80) |
| **WARNING** | Orange | Metric deviations detected (integrity 50–79) |
| **DE-SYNC** | Red | Significant stereo breakdown (integrity < 50) |
| **UNKNOWN** | Gray | Scene too dark/featureless for reliable analysis |

**Scene Confidence** (shown as a percentage) evaluates each frame across 5
metrics (luminance, edge density, texture variance, feature count, entropy).
When confidence is below 0.35 (configurable via `sceneConfidenceThreshold`),
the frame is classified as **UNKNOWN** — baseline comparison is skipped, sync
capture is refused, and metrics are displayed as indicative only. This prevents
false positives during loading screens, fades, and black transitions.

---

## Stereo Model Baseline (Sync)

Stereo Inspector captures a complete **Stereo Model Baseline** — a profile of 20
reference metrics (SSIM, PixDiff, Brightness, Histogram, Edge, Occlusion, Disparity
stats, all asymmetry metrics, StereoOffset) captured at sync time. Each subsequent
frame is compared against this baseline to compute deviations.

### How to Sync

1. Wait for properly synchronized stereo content.
2. Press **F9** or click **"Sync (Capture Model)"** in the Controls panel.
3. The app analyzes the pending frames fully on the next iteration.
4. A green notification appears: "Stereo Model Captured" with frame number,
   confidence, and timestamp.
5. If the scene is too dark/featureless, sync is refused with a red notification.

### How Comparison Works

Each metric is compared against its baseline value with a per-metric tolerance.
If the deviation exceeds the tolerance, a `MetricDeviation` entry is created
(warning at 50% of tolerance, fail at 100%). An overall deviation score (0–1,
weighted average) determines the integrity score:
- **≤ 20% deviation** → SAFE
- **20–50% deviation** → WARNING
- **> 50% deviation** → DESYNC

### Clearing Baseline

Press **F9** again or click **"Clear Baseline"** to remove the baseline.
This also resets metric history and temporal analysis state.

---

---

> **Note**: The overlay is always interactive (click-friendly). There is no
> click-through mode — the overlay handles all input so you can click buttons,
> resize windows, and drag panels at any time.

## Issue Detection

The app uses an **intelligent multi-feature classifier** to detect and classify
rendering issues:

1. `absdiff(left, warpedRight)` generates a grayscale residual.
2. Threshold at `diffThreshold` (configurable, default 40) → binary mask → morphological cleanup.
3. `connectedComponentsWithStats` finds contiguous regions ≥ `minIssueArea` (configurable, default 200 pixels).
4. **IssueClassifier** extracts 22 features per region (brightness, contrast,
   edge ratio, texture variance, gradient, histogram, color, bloom, shadow,
   content density, position, size) and scores all 20 issue types.
5. The type with the highest confidence is selected (minimum `minIssueConfidence`
   threshold, configurable, default 0.50, otherwise **LowConfidence**).
6. **RegionMerger** clusters overlapping regions via IoU/centroid-distance
   and removes lens-boundary, vignette, and tiny false positives.

**Issue types** (20 total):

| Category | Types |
|----------|-------|
| Lighting | LightingDifference, ShadowDifference, BloomDifference, ReflectionDifference |
| Texture | TextureDifference, MaterialDifference, TransparencyDifference, EdgeDifference |
| Geometry | MissingGeometry, ExtraGeometry, MissingObject, MissingParticle, MissingUI |
| Text | TextDifference |
| Offset | StereoOffset |
| Occlusion | OcclusionDifference |
| Post-Process | PostProcessDifference |
| Lens | LensBoundary |
| Temporal | TemporalDifference |
| Disparity | DepthDisparityError |
| Low Confidence | LowConfidence (when no type scores ≥ threshold) |

**Issues panel** shows each region with its type color, confidence percentage,
and pixel area. Hover for top 3 alternatives. Click to reveal full reasoning
text and evidence breakdown in the detail panel below.

---

## Visualization Modes

### Normal (F1)
Shows the full side-by-side frame with mini trend graphs overlaid at the
bottom-left corner (Health, SSIM, PixDiff, Brightness Δ).

### Stereo Difference Overlay (F3)
Displays the left eye with color-coded bounding boxes around each detected
issue region. Each box is labeled with the issue type and confidence percentage.
Each of the 22 issue types has a distinct color (HSV-based palette). Invalid
regions (lens boundary, vignette) are drawn with reduced opacity.

### Difference Heatmap (F4)
Renders the aligned residual difference map (left vs warped-right) using a
custom green→yellow→red colormap:
- **Green**: < 8% difference (matching)
- **Yellow**: 8–30% difference (moderate)
- **Red**: > 30% difference (mismatch)

### Disparity Heatmap (Ctrl+F7)
Visualizes the dense disparity map using the JET colormap. Invalid disparity
pixels are shown in dark gray. Overlay text shows mean disparity, range, and
invalid ratio. Useful for verifying that stereo correspondence is working
correctly.

### Edge Comparison (Ctrl+F1)
Canny edge detection on both eyes overlaid: red = left edges, green = right
edges, yellow = overlapping edges.

### Feature Match Overlay (Ctrl+F2)
ORB feature matches drawn as lines between corresponding points in left/right
eyes.

### Histogram View (Ctrl+F3)
Overlaid grayscale histograms: blue = left eye, green = right eye.

### Blur Map (Ctrl+F4)
Laplacian variance heatmap (Inferno colormap): bright = sharp, dark = blurry.

### Blink Left (Ctrl+F5) / Blink Right (Ctrl+F6)
Show a single eye full-screen with text label.

---

## Hotkeys

| Key | Function |
|-----|----------|
| **F1** | Toggle overlay visibility on/off |
| **F2** | Freeze/unfreeze analysis |
| **F3** | Stereo Difference Overlay (issue bounding boxes) |
| **F4** | Difference Heatmap (aligned residual) |
| **F5** | Toggle Normal / Heatmap |
| **F6** | Take screenshot |
| **F7** | Start/stop logging (CSV + JSON) |
| **F8** | Start/stop recording |
| **F9** | Set baseline (sync — capture stereo model) |
| **Ctrl+F1** | Edge Comparison |
| **Ctrl+F2** | Feature Match Overlay |
| **Ctrl+F3** | Histogram View |
| **Ctrl+F4** | Blur Map |
| **Ctrl+F5** | Blink Left (show left eye full screen) |
| **Ctrl+F6** | Blink Right (show right eye full screen) |
| **Ctrl+F7** | Disparity Heatmap |

---

## Live Graphs

The **Live Graphs** panel shows 8 sparkline trend graphs covering the last
300 frames (reset on baseline sync):

**Row 1** (primary metrics):
- **Health** (green, 0–100) — overall stereo integrity score
- **SSIM** (blue, 0–1) — aligned structural similarity
- **Disp** (orange, 0–200) — mean disparity in pixels
- **Bright** (purple, 0–1) — brightness delta

**Row 2** (temporal metrics):
- **Stability** (teal, 0–1) — temporal stability score
- **AlignSSIM** (light blue, 0–1) — aligned SSIM from residual
- **Flicker** (red, 0–1) — frame-to-frame luminance flicker
- **DispStab** (yellow, 0–1) — disparity stability over time

---

## Headless Data Collection (`--collect`)

Run without the GUI overlay to build ground-truth datasets:

```powershell
StereoInspector.exe --collect D:\dataset
```

Outputs per frame:
- `frame_NNNN_left.png` / `frame_NNNN_right.png` — raw eye images
- `dataset.jsonl` — one JSON object per frame with all metrics, detected issues,
  evidence vectors (22 features), scene confidence, and alternatives

**To label data for classifier tuning**, add `"groundTruth"` field to each issue:
```json
{"type": "Bloom Difference", "groundTruth": "Bloom Difference", ...}
```

---

## Synthetic Data Generator

The `gen_synthetic` tool creates labeled stereo defect datasets from any image:

```powershell
# Build
cmake --build build --config Release --target gen_synthetic

# Generate 200 samples from test pattern
gen_synthetic testpattern D:\dataset --samples 200 --size 1920x1080

# Generate from a photo
gen_synthetic C:\photo.jpg D:\dataset --samples 100
```

Each sample: left eye (shifted), right eye (shifted + defect injected at random
location). Output: PNGs + `dataset.jsonl` with perfect `groundTruth` labels.

**18 defect types**: Lighting, Shadow, Bloom, Reflection, Texture, Material,
Transparency, Edge, Missing/Extra Geometry, Missing Object/Particle/UI, Text,
Stereo Offset, Occlusion, Post-Process, Lens Boundary.

---

## Classifier Optimization

The `optimize_classifier.py` script tunes classifier coefficients against labeled data:

```powershell
python tools/optimize_classifier.py D:\dataset\dataset.jsonl
```

Requires: Python 3, numpy, scipy. Output: `*_optimized_coeffs.h` with tuned
coefficients and per-type Platt scaling parameters.

> **⚠️ Important**: The classifier currently uses synthetic-data coefficients.
> For trustworthy confidence percentages, capture data from **real VR content**
> and re-run optimization. See `tools/best_optimized_coeffs.h` for the best
> available coefficients.

---

## Configuring Thresholds

Edit `config.json` (auto-created on first launch):

```json
{
    "sceneConfidenceThreshold": 0.15,
    "diffThreshold": 40,
    "minIssueArea": 200,
    "minIssueConfidence": 0.50,
    "thresholds": {
        "ssimWarning": 0.85,
        "ssimFail": 0.70,
        ...
    }
}
```

**Detection thresholds:**
| Field | Default | Description |
|-------|---------|-------------|
| `diffThreshold` | 40 | Grayscale intensity threshold for residual mask (higher = fewer detections) |
| `minIssueArea` | 200 | Minimum connected-component pixel area |
| `minIssueConfidence` | 0.50 | Minimum classifier confidence to retain an issue (was 0.70) |
| `sceneConfidenceThreshold` | 0.15 | Was 0.35 — controls how conservative scene evaluation is. Lower = more tolerant of dark/featureless scenes. |

All thresholds are reloaded at app start from `config.json`.

---

## Logging

Press **F7** to start/stop logging. Creates:

| File | Format | Contents |
|------|--------|----------|
| `stereo_inspector_log.csv` | CSV | All metrics per frame |
| `stereo_inspector_log.json` | JSON | Same data as structured JSON array |
| `screenshots/` | PNG | Auto-capture on FAIL frames |
| `stereo_inspector_report.html` | HTML | Summary report with Chart.js |

Each log entry now includes IntegrityScore, StereoStatus, BaselineActive,
BaselineFrame, and deviations array per metric.

---

## Analysis Modules Explained

### SSIM (Structural Similarity Index)
Compares luminance, contrast, and structure between aligned left/warped-right
images. Output: 0–1 (1 = identical).

### Absolute Pixel Difference
Counts pixels where |L − alignedR| > 10. Output: percentage.

### Histogram Comparison
256-bin grayscale histogram correlation via `cv::compareHist(CORREL)`.

### Edge Detection (Canny)
Canny edges at 50/150 thresholds, Jaccard index (intersection / union).

### ORB Feature Matching
500 ORB keypoints per eye, brute-force Hamming matching, distance filter.

### Optical Flow
200 good features tracked via Lucas-Kanade, average displacement.

### Blur Detection (Laplacian Variance)
Laplacian variance delta, normalized by larger value.

### More Modules
Brightness, Contrast, Bloom, Shadow, Stereo Offset, OCR (optional), VR Lens
Analysis. All computed on aligned (warped) images.

---

## Performance Tips

- **Target FPS**: Adjust `targetFps` in `config.json`.
- **Scene confidence**: Lower `sceneConfidenceThreshold` (0.2–0.3) for more
  aggressive analysis during dark scenes.
- **Capture FPS**: Displayed in the overlay — shows actual capture rate from DXGI.
  `captureFps` tracks the rate at which frames are acquired (independent of analysis FPS).
- **Disable analyzers**: Use the **Checks** tab in the Hub window to toggle
  individual analyzers on/off without recompilation (32 CheckToggles).
- **OCR is disabled by default**: Enable only if needed.

---

## Troubleshooting

| Problem | Likely Cause | Solution |
|---------|-------------|----------|
| Overlay not appearing | DX11 device creation failed | Check GPU drivers |
| "DXGI device lost" | Monitor resolution changed | Re-launch the app |
| No capture / black frame | DXGI Duplication not supported | Use Windows 10+, non-VM GPU |
| Low FPS | High resolution + all analyzers | Lower `targetFps`, disable OCR |
| Hotkeys not working | Window focus | Click on a non-overlay window first, then back |
| Sync always refused | Scene too dark/featureless | Increase scene confidence or wait for proper content |

---

## Tools Reference

| Tool | Location | Purpose |
|------|----------|---------|
| `gen_synthetic` | Built from `tools/synthetic_dataset_gen.cpp` | Generate labeled synthetic stereo defect datasets |
| `optimize_classifier.py` | `tools/optimize_classifier.py` | Tune classifier coefficients against labeled data |
| `best_optimized_coeffs.h` | `tools/best_optimized_coeffs.h` | Best available optimized coefficients |

---

## Data Files Reference

| File | Purpose |
|------|---------|
| `config.json` | Application configuration |
| `stereo_inspector.log` | Application log |
| `stereo_inspector_log.csv` | Session metrics (CSV) |
| `stereo_inspector_log.json` | Session metrics (JSON) |
| `stereo_inspector_report.html` | Analysis report |
| `stereo_inspector_layout.ini` | ImGui panel layout |
| `screenshots/*.png` | Auto-captured FAIL frames |
| `dataset.jsonl` | Data collection output (from `--collect`) |
