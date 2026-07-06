# Stereo Inspector — Developer Documentation

> **Classifier calibration requires real VR capture data.** The synthetic data pipeline (generator + optimizer) provides approximate coefficients, but definitive calibration — where "87% confidence" means P(correct) ≈ 0.87 — requires capturing real VR content with ground truth labels. See the [Classifier Calibration](#classifier-calibration--real-vr-capture) section.

## Architecture Overview

```
┌────────────────────────────────────────────────────────────────────┐
│                        main.cpp (Entry Point)                       │
│  Creates all components, wires callbacks, runs the main loop        │
└────────────────────────────────────────────────────────────────────┘
         │                    │                    │
         ▼                    ▼                    ▼
┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│   DxgiCapture     │  │ AnalyzerPipeline  │  │     Overlay       │
│  (capture thread) │  │ (analysis thread) │  │  (render thread)  │
│                   │  │                   │  │                   │
│  DXGI Desktop     │  │ 14 × IStereo-    │  │ DirectX 11        │
│  Duplication API  │──▶ Analyzer modules  │  │ Dear ImGui        │
│  → LatestFrameBuf │  │ → AnalysisResult  │  │ Transparent       │
└──────────────────┘  └──────────────────┘  │ always-on-top      │
         │                    │              └──────────────────┘
         │                    │                       │
         ▼                    ▼                       ▼
┌────────────────────────────────────────────────────────────────────┐
│  LatestFrameBuffer (lock-free single-slot shared state)             │
│                                                                     │
│  Visualizer — converts frames + results into visualization images   │
│  StereoLogger — writes CSV, JSON, screenshots                       │
│  ReportGenerator — builds HTML report with Chart.js                 │
│  InputManager — RegisterHotKey-based hotkey dispatch                │
│  Config — JSON file (config.json) with all thresholds               │
└────────────────────────────────────────────────────────────────────┘
```

### Threading Model

Three threads run concurrently:

| Thread | Class | Role | Rate |
|--------|-------|------|------|
| **Capture** | `DxgiCapture` | Acquires frames via DXGI, splits into L/R, stores in `LatestFrameBuffer` | Up to monitor refresh rate |
| **Analysis** | `AnalyzerPipeline` | Reads latest frame, runs all analyzers, computes health score | Limited by CPU (typically 30–90 FPS) |
| **Main** | `main.cpp` / `Overlay` | Renders ImGui overlay, processes hotkeys, handles logging | 60–144 FPS (monitor refresh) |

The capture and analysis threads are **decoupled** via `LatestFrameBuffer`, which
stores only the most recent frame. The analysis thread always works on the
latest available frame, never queuing. This ensures the overlay always shows
the most current result without accumulating latency.

---

## File Map

```
StereoInspector/
├── CMakeLists.txt              # Build system (C++20, OpenCV, DX11, ImGui, spdlog, json)
├── setup.ps1                   # Dependency installer (vcpkg + imgui download)
├── config.json                 # User configuration (auto-created)
├── .gitignore
├── guide.md                    # User guide (you are here)
├── devdoc.md                   # Developer documentation
└── src/
    ├── main.cpp                # Entry point, wires all components
    ├── core/
    │   ├── Types.h             # Enums, structs: AnalysisResult, CaptureFrame, FrameTime, etc.
    │   ├── Frame.h             # FrameQueue, LatestFrameBuffer declarations
    │   └── Frame.cpp           # Thread-safe frame buffer implementations
    ├── config/
    │   ├── Config.h            # AppConfig, StereoRegion, Thresholds, LogConfig
    │   └── Config.cpp          # JSON serialization/deserialization
    ├── capture/
    │   ├── DxgiCapture.h       # DXGI Desktop Duplication capture class
    │   └── DxgiCapture.cpp     # D3D11 device, DuplicateOutput, AcquireNextFrame loop
    ├── analysis/
    │   ├── IStereoAnalyzer.h   # Virtual interface for all analyzers
    │   ├── IStereoAnalyzer.cpp # BaseAnalyzer implementation
    │   ├── Analyzer.h          # AnalyzerPipeline — orchestrator
    │   ├── Analyzer.cpp        # Thread loop, scoring, health computation
    │   └── modules/
    │       ├── SsimAnalyzer.*           # Structural Similarity Index
    │       ├── PixelDiffAnalyzer.*      # Absolute pixel difference %
    │       ├── HistogramAnalyzer.*      # Histogram correlation
    │       ├── EdgeAnalyzer.*           # Canny edge Jaccard index
    │       ├── OrbAnalyzer.*            # ORB feature matching count
    │       ├── OpticalFlowAnalyzer.*    # Lucas-Kanade optical flow magnitude
    │       ├── BlurAnalyzer.*           # Laplacian variance delta
    │       ├── BrightnessAnalyzer.*     # Mean intensity delta
    │       ├── ContrastAnalyzer.*       # Std dev intensity delta
    │       ├── IssueClassifier.h     # Intelligent multi-feature issue classifier
    │       ├── IssueClassifier.cpp   # Feature extraction + 22-type scoring
    │       ├── RegionMerger.h        # Region overlap clustering + filtering
    │       ├── RegionMerger.cpp      # IoU merging, lens/border/vignette filters
    │       ├── BloomAnalyzer.*          # Over-bright region proportion delta
    │       ├── ShadowAnalyzer.*         # Dark region proportion delta
    │       ├── StereoDetector.*         # Smart stereo layout detection
    │       ├── VrLensAnalyzer.*         # VR-specific metrics (lens, foveation, god rays)
    │       ├── StereoOffsetAnalyzer.*   # Horizontal feature disparity
    │       └── OcrAnalyzer.*            # Tesseract text comparison (optional)
    ├── overlay/
    │   ├── Overlay.h           # Transparent DX11 + ImGui overlay
    │   └── Overlay.cpp         # Window creation, D3D11 init, ImGui rendering
    ├── visualization/
    │   ├── Visualizer.h        # 8 visualization mode renderers
    │   └── Visualizer.cpp      # OpenCV-based visual processing
    ├── logging/
    │   ├── Logger.h            # CSV + JSON logger with auto-screenshot
    │   ├── Logger.cpp          # File I/O, threaded-safe writes
    │   ├── ReportGenerator.h   # HTML report builder
    │   └── ReportGenerator.cpp # Chart.js graph, summary stats, embedded screenshots
    └── input/
        ├── InputManager.h      # Hotkey registration and dispatch
        └── InputManager.cpp    # Win32 message loop, RegisterHotKey
```

---

## Core Data Structures

### `Types.h` — Central Type Definitions

```cpp
enum class VisualizationMode {
    Normal,
    DifferenceHeatmap,
    StereoDifferenceOverlay,
    DisparityHeatmap,
    EdgeComparison,
    FeatureMatchOverlay,
    HistogramView,
    BlurMap,
    BlinkLeft,
    BlinkRight
};

enum class StereoLayout {
    Unknown,
    SideBySide,
    OverUnder,
    SideBySide_Wide,
    SideBySide_HalfRes,
    Custom
};

enum class FrameStatus { PASS, WARNING, FAIL };
enum class StereoStatus { SAFE, WARNING, DESYNC, UNKNOWN };
```

**Key new structs:**

| Struct | Purpose |
|--------|---------|
| `DisparityMetrics` | Dense disparity statistics: mean, median, std, range, invalid ratio, smoothness, vertical asymmetry, raw disparity map |
| `MatchQualityMetrics` | Correspondence confidence: inlier ratio, confidence 0–1, total/inlier matches |
| `ResidualMetrics` | Metrics on aligned images: SSIM, PixDiff, Brightness, Edge, Histogram, Occlusion ratio |
| `AsymmetryMetrics` | Stereo rendering asymmetry: lighting, bloom, shadow, texture, chromatic, contrast, post-process, geometry |
| `TemporalMetrics` | Frame-to-frame consistency: flicker score, temporal SSIM, stability, disparity stability |
| `SceneConfidence` | Per-frame scene reliability: 5-scores + overall (0–1) + reliable flag |
| `MetricDeviation` | Baseline comparison entry: metric name, base/cur value, delta, severity, tolerance |
| `StereoModelBaseline` | Full baseline profile with 20 reference metrics + overall deviation tracking |
| `DetectedIssue` | Classified region issue: type, confidence 0–1, bounding box, area, severity, `ClassificationEvidence` (13 fields), `alternatives` vector, `reasoningText`, `isInvalidRegion` flag |

**`MetricHistory`** — Rolling window for trend graphs (MAX_HISTORY = 300):

| Field | Type | Description |
|-------|------|-------------|
| `healthScore` | `HistoryBuffer` | Integrity/health score |
| `ssim` | `HistoryBuffer` | SSIM |
| `pixelDiff` | `HistoryBuffer` | Pixel diff % |
| `brightnessDelta` | `HistoryBuffer` | Brightness delta |
| `stereoOffset` | `HistoryBuffer` | Stereo offset |
| `alignmentSSIM` | `HistoryBuffer` | Aligned residual SSIM |
| `disparityMean` | `HistoryBuffer` | Mean disparity |
| `temporalStability` | `HistoryBuffer` | Temporal stability |
| `flickerScore` | `HistoryBuffer` | Flicker score |
| `disparityStability` | `HistoryBuffer` | Disparity stability |

**`CaptureFrame`** — Raw data from capture thread:

| Field | Type | Description |
|-------|------|-------------|
| `frame` | cv::Mat | Full side-by-side frame (BGR) |
| `leftEye` | cv::Mat | Left half region |
| `rightEye` | cv::Mat | Right half region |
| `frameNumber` | int64_t | Monotonically increasing ID |
| `width` / `height` | int | Dimensions |
| `detectedLayout` | StereoLayout | Auto-detected layout |
| `splitPoint` | int | Pixel column/row where the split occurred |
| `captureTime` | time_point | When captured |

### `Frame.h` — Thread-Safe Buffers

**`FrameQueue`** (bounded queue, mutex-protected):
- Fixed-size queue (default 3) for producer-consumer patterns.
- `push()` returns false if full (non-blocking).
- `pop()` blocks if empty (returns nullptr).

**`LatestFrameBuffer`** (single-slot, mutex-protected):
- `store()` replaces the current frame.
- `load()` returns the latest frame (or nullptr).
- Used for decoupled capture→analysis pipeline where only the latest frame matters.

---

## Configuration Module

### `Config.h / Config.cpp`

**`AppConfig`** is the top-level configuration object with three nested configs:

**`StereoRegion`** controls how the frame is split:
- `autoSplit: true` → Splits at `frame.cols / 2`.
- `autoSplit: false` → Uses explicit rectangle; right eye is at `x + width + 1`.

**`AnalysisThresholds`** has 14 pairs of `Warning`/`Fail` thresholds (one per metric).
Each pair defines two boundaries used in health score computation.

**Detection thresholds** (added post-audit):
- `diffThreshold` (default 40) — grayscale residual threshold for issue detection mask
- `minIssueArea` (default 200) — minimum pixel area for connected components
- `minIssueConfidence` (default 0.50) — minimum classifier confidence to retain an issue

**`CheckToggles`** (added Week 2): 32 per-check enable/disable booleans organized
by category (Image Analysis, Correspondence, Asymmetry, Issue Detection, Scoring).
Serialized in config JSON.

**`LogConfig`** controls output:
- File paths for CSV, JSON, screenshots, report.
- `autoScreenshotOnFail` / `autoScreenshotOnWarning` — conditional capture.
- `maxScreenshots` — safety limit.

Serialization is via `nlohmann::json` with `toJson()` / `fromJson()` / `saveToFile()` /
`loadFromFile()`. Missing fields fall back to defaults.

---

## Capture Module

### `DxgiCapture.h / DxgiCapture.cpp`

Uses the **Windows Desktop Duplication API** (IDXGIOutputDuplication) for low-level
screen capture without a mirror driver.

**Initialization flow** (`initDxgi()`):
1. `D3D11CreateDevice` — creates D3D11 device + context.
2. Query `IDXGIDevice` → `IDXGIAdapter` → `IDXGIOutput`.
3. `IDXGIOutput1::DuplicateOutput` — creates the duplication interface.
4. Reads `DXGI_OUTPUT_DESC` for resolution.

**Capture loop** (`captureLoop()`):
1. `AcquireNextFrame(16ms timeout)` — waits up to 16ms for a new frame.
2. On timeout: sleep 1ms, retry.
3. On `DXGI_ERROR_ACCESS_LOST` / `DEVICE_REMOVED`: reinitialize.
4. On success: convert `IDXGIResource` → `ID3D11Texture2D` → staging → `cv::Mat`.
5. Split frame via `splitFrame()` using `StereoRegion` config or `StereoDetector`.
6. Store in `LatestFrameBuffer`.
7. `ReleaseFrame()`.

**Stereo splitting** (`splitFrame()`):
- If `config.stereoRegion.autoSplit` is true, delegates to `StereoDetector::detect()`
  which intelligently determines the split position (see StereoDetector section).
- If `autoSplit` is false, uses the manual rectangle from config.
- `splitFrame()` operates in the capture thread once per acquired frame.

**Frame conversion** (`dxgiToMat()`):
- Creates a staging texture with `D3D11_USAGE_STAGING | D3D11_CPU_ACCESS_READ`.
- Copies GPU resource → staging via `CopyResource()`.
- `Map()` + `memcpy` row-by-row to `cv::Mat` (CV_8UC4).
- Converts BGRA → BGR.

---

## Analysis Pipeline

### `Analyzer.h / Analyzer.cpp`

**`AnalyzerPipeline`** orchestrates all analysis using a **correspondence-first** approach:

**`analyzeFrame(left, right)` — Correspondence-First Pipeline**:

0. **FeatureCache update**: Update frame number for cache invalidation.
1. **Stereo Correspondence**: `StereoCorrespondence::compute(left, right)` computes
   dense disparity (SGBM default), warps right eye to left coordinate system,
   generates occlusion mask.
2. **Grayscale Precompute**: `leftGray` + `rightGray` computed once, stored
   in `AnalysisResult`. All downstream analyzers use cached grayscale.
3. **Aligned Analysis**: All registered analyzers run on `(left, warpedRight)` —
   the warped/aligned images. This eliminates false positives from binocular
   parallax (differences that are expected stereo depth).
4. **Disparity Metrics**: Statistics on the disparity map (mean, std, range,
   invalid ratio, smoothness, vertical asymmetry).
5. **Match Quality**: Correspondence confidence, inlier ratio.
6. **Asymmetry Metrics**: Lighting, bloom, shadow, texture, chromatic, contrast,
   post-process asymmetry computed from aligned images.
7. **Scene Confidence**: Per-quadrant evaluation (4 quadrants, best-2 average).
   Uses FAST-10 corner detection. Threshold configurable (default 0.15).
8. **Health Score**: Config-driven thresholds from `AnalysisThresholds`.
9. **Issue Detection**:
   a. Connected components on aligned residual diff map (`absdiff(left, warpedRight)`),
      thresholded at configurable `diffThreshold` (default 40), minimum area
      configurable `minIssueArea` (default 200 pixels).
   b. **IssueClassifier::classify()** extracts 22 features from each region
      (brightness, contrast, edge ratio, texture variance, gradient, histogram,
      color presence, bloom ratio, shadow ratio, content density, position,
      aspect ratio, size), scores all 20 issue types, picks the highest with
      confidence ≥ `minIssueConfidence` (default 0.50, otherwise LowConfidence).
   c. **RegionMerger::merge()** clusters overlapping regions via IoU +
      centroid-distance check; highest-confidence classification wins.
   d. **RegionMerger::filterInvalid()** removes lens-boundary false positives,
      vignette artifacts, and tiny/small regions.
10. **Baseline Comparison**: Compare metrics against captured baseline.

**Scene Confidence** (`computeSceneConfidence()`):

Evaluated every frame using 5 weighted metrics on both eyes:
- Luminance (20%) — penalize very dark/bright
- Edge density via Sobel (20%) — scaled by 10×
- Texture variance via Laplacian (20%) — scaled by 20×
- Feature count via FAST (20%) — features per 1K pixels
- Shannon entropy (20%) — histogram uniformity

If `overall < sceneConfidenceThreshold` (default 0.35), the frame is UNKNOWN —
baseline comparison skipped, sync capture refused.

**Baseline Comparison** (`compareWithBaseline()`):

When a baseline is active, 20 metrics are compared individually:
- Each metric has a per-metric tolerance and weight.
- Deviation severity = `min(1.0, absDelta / tolerance)`.
- Metrics with severity > 0.2 generate a `MetricDeviation` entry.
- Weighted average of severities → `overallDeviation` (0–1).
- Integrity score = `100 × (1 − overallDeviation)`.
- Status: SAFE (≥ 80), WARNING (50–79), DESYNC (< 50).

**Health Score Fallback** (`computeHealthScore()`):

When no baseline is active, uses the legacy weighted penalty system:
- Starts at 100, penalizes by metric severity.
- Primary weight on residual/aligned metrics, secondary on disparity health,
  tertiary on asymmetry defects, temporal, and drift.

### `IStereoAnalyzer.h` — Analyzer Interface

Every analyzer inherits from `BaseAnalyzer` (which implements `IStereoAnalyzer`):

```cpp
class IStereoAnalyzer {
public:
    virtual ~IStereoAnalyzer() = default;
    virtual std::string name() const = 0;
    virtual void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye,
                         AnalysisResult& result) = 0;
    virtual bool enabled() const;
    virtual void setEnabled(bool enabled);
};
```

This makes adding a new analyzer trivial:
1. Create a new class inheriting `BaseAnalyzer`.
2. Implement `analyze()` — read from `leftEye`/`rightEye`, write to `result`.
3. Register in `main.cpp` with `registerAnalyzer()`.

---

## Analysis Modules

### 1. SsimAnalyzer

Full SSIM (Structural Similarity Index) computation:

1. Convert both eyes to grayscale, then to CV_32F.
2. Gaussian blur (11×11, σ=1.5) → means.
3. Compute mean products, variances, covariance.
4. SSIM map = `(2·μ₁·μ₂ + C1)(2·σ₁₂ + C2) / (μ₁² + μ₂² + C1)(σ₁² + σ₂² + C2)`.
5. Return mean across all color channels.

Constants: `C1 = 6.5025` (=(0.01·255)²), `C2 = 58.5225` (=(0.03·255)²).

### 2. PixelDiffAnalyzer

Simple absolute difference:
1. `absdiff(left, right)` → grayscale.
2. Count non-zero pixels where diff > 10.
3. Return percentage: `diffPixels / totalPixels × 100`.

### 3. HistogramAnalyzer

1. 256-bin grayscale histogram for each eye.
2. Normalize both to L1 norm.
3. `compareHist(CORREL)` → correlation coefficient (0–1, clamped).

### 4. EdgeAnalyzer

1. Canny edge detection (thresholds 50/150) on both eyes.
2. Bitwise AND and OR of edge maps.
3. Jaccard index: `intersection / union` (0–1).

### 5. OrbAnalyzer

1. Detect 500 ORB keypoints per eye.
2. Compute BRIEF descriptors.
3. Brute-force Hamming matching.
4. Filter good matches: `distance ≤ max(2 × minDist, 30)`.
5. Count good matches.

### 6. OpticalFlowAnalyzer

1. `goodFeaturesToTrack(200)` on left eye.
2. `calcOpticalFlowPyrLK(left→right)`.
3. Average displacement magnitude of successful tracks.

### 7. BlurAnalyzer

1. Laplacian filter (CV_64F) on both eyes.
2. Compute variance of the response.
3. Delta = `|variance_L − variance_R| / max(variance_L, variance_R)`.
4. Higher delta = more asymmetric blur.

### 8. BrightnessAnalyzer

Normalized difference in mean grayscale intensity:
`|mean(L) − mean(R)| / 255`.

### 9. ContrastAnalyzer

Normalized difference in standard deviation:
`|stddev(L) − stddev(R)| / 255`.

### 10. ChromaticAberrationAnalyzer

1. Split BGR channels.
2. `absdiff(red, blue)` for each eye.
3. Mean of the difference → normalized CA score.
4. Delta = `|CA_left − CA_right|`.

### 11. BloomAnalyzer

1. Heavy Gaussian blur (21×21, σ=10).
2. Threshold at 200 → binary bright regions.
3. Proportion of bright pixels.
4. Delta = `|bloom_L − bloom_R|`.

### 12. ShadowAnalyzer

Same approach as bloom, but:
- Threshold at 50 (inverted).
- Measures proportion of dark pixels.

### 13. StereoOffsetAnalyzer

1. 300 ORB features per eye.
2. Brute-force matching, Lowe's ratio filter.
3. For good matches, compute horizontal disparity: `|kpL.x − kpR.x|`.
4. Average disparity across all matches.

### 14. OcrAnalyzer

Optional Tesseract integration:
1. Only compiled with `HAS_TESSERACT=1`.
2. Initialize Tesseract with `eng` language.
3. `SetImage()` on grayscale version of each eye.
4. `GetUTF8Text()` → normalize (lowercase, remove whitespace).
5. Character-by-character comparison → count mismatches.

When Tesseract is unavailable, the analyzer is disabled by default and returns 0.

### 15. StereoDetector

Not part of the analysis pipeline — used directly by `DxgiCapture::splitFrame()`
to determine stereo layout when `autoSplit: true`.

**Algorithm**:
1. Searches candidate split positions at 25%, 33%, 50%, 66%, and 75% of the
   frame width (±10% around each candidate in 2% steps).
2. For each candidate, computes a combined score:
   - **Histogram correlation** (60% weight): Splits the frame at the candidate
     position, computes the grayscale histogram for each half, and measures
     correlation via `cv::compareHist(CORREL)`.
   - **Edge Jaccard similarity** (40% weight): Canny edge detection on both
     halves, then `intersection / union` of the binary edge maps.
   - **Separator darkness bonus**: Samples pixels along the candidate split
     line — if the average intensity is low (dark boundary typical of SBS
     padding), a bonus is added.
3. Picks the candidate with the highest combined score.
4. Maps the winning split position to a `StereoLayout` enum:
   - 50% → `SideBySide`
   - 33% or 66% → `SideBySide_Wide`
   - 25% or 75% → `SideBySide_HalfRes`
   - Vertical splits → `OverUnder`
   - Other → `Custom`
5. Confidence is stored in `StereoDetectionResult.confidence` (0–1).

**Re-detection**: Runs every 30 captured frames to adapt to layout changes.

### 16. VrLensAnalyzer

Registered as a standard analyzer for VR-specific metrics when a VR layout is
detected. Computes three metrics:

**Lens Distortion Difference** (`lensDistortionDelta`):
1. Divides each eye into 16 radial bands from center to edge.
2. Computes edge density (Canny + mean) in each band.
3. Compares the radial edge density profile between left and right eyes.
4. Reports the sum of absolute differences — higher = asymmetric lens distortion.

**Foveation Asymmetry** (`foveationAsymmetry`):
1. Divides each eye into a center circle (radius = 30% of eye height) and
   periphery (outside).
2. Computes Laplacian variance in center vs periphery for both eyes.
3. Computes center/periphery ratio for each eye.
4. Reports absolute difference of ratios between eyes — higher = asymmetric
   foveated rendering.

**God Ray / Glare Difference** (`godRayDifference`):
1. Thresholds each eye at 200 to find bright regions.
2. Dilates those regions to estimate light spread/glow.
3. Computes proportion of dilated bright pixels for each eye.
4. Reports absolute difference — higher = asymmetric god rays/glare.

All three metrics are deliberately forgivable (measuring asymmetry, not absolute
values) to avoid flagging intentional VR rendering techniques like pre-warp or
fixed foveation.

---

## IssueClassifier Module

### `IssueClassifier.h / IssueClassifier.cpp`

Replaces the old heuristic region classification with an intelligent multi-feature
scoring system supporting 20 issue types:

```
LightingDifference, ShadowDifference, BloomDifference, ReflectionDifference,
TextureDifference, MaterialDifference, TransparencyDifference, EdgeDifference,
MissingGeometry, ExtraGeometry, MissingObject, MissingParticle, MissingUI,
TextDifference, StereoOffset, DepthDisparityError, OcclusionDifference,
PostProcessDifference, TemporalDifference, LensBoundary
```

**`extractFeatures(leftGray, rightGray, box)`** — extracts 20 features per region:

| Feature | Method |
|---------|--------|
| brightnessDiff | Mean grayscale delta (0–1) |
| contrastDiff | Std dev delta (0–1) |
| colorDiff | Mean color channel delta (B,G,R average) |
| edgeSimilarity | Canny Jaccard index within region |
| textureSimilarity | Laplacian variance ratio |
| gradientConsistency | Sobel gradient correlation |
| histogramSimilarity | 256-bin HISTCORREL within region |
| featureMatchDensity | ORB matches per pixel in region |
| disparityConsistency | SGBM disparity variance |
| edgeRatio | Edge pixel proportion |
| meanDiff | Mean residual diff |
| bloomRatio | Bright (>200) pixel proportion delta |
| shadowRatio | Dark (<50) pixel proportion delta |
| left/rightContentDensity | Edge content density per eye |
| hasColor | True if saturation > 30 in any channel |
| nearBorder/nearCenter | Region position flags |
| regionSize, aspectRatio, posX/Y | Geometric properties |

**`classify(features)`** — scores all 20 types using dedicated scoring functions:

```cpp
float scoreLighting(const RegionFeatures& f);
float scoreShadow(const RegionFeatures& f);
// ... one per type
```

Each scorer returns 0–1 confidence. The type with the highest confidence is
selected. If no score exceeds `minIssueConfidence` (configurable, default 0.50),
the region is classified as `LowConfidence` (with the best score clamped at 0.55).
All scores > 0.10 are added to the `alternatives` vector for explainability.

**`buildReasoningText(type, features)`** — generates human-readable explanation:
```
Classification:
  TextureMismatch

Evidence:
  Brightness Diff: 0.22 — Moderate
  Contrast Diff: 0.15 — Moderate
  Texture Similarity: 0.45 — Low (primary factor)
  Gradient Consistency: 0.60 — Moderate
  Content Density L/R: 0.85 / 0.30 — Asymmetric

Alternatives:
  LightingDifference: 0.35
  EdgeMisalignment: 0.22
```

### `ClassificationEvidence` struct

Embedded in each `DetectedIssue`, provides the raw data behind the reasoning:
13 float metrics + 3 boolean flags. Displayed in the Issues tab detail panel
when a region is selected.

---

## RegionMerger Module

### `RegionMerger.h / RegionMerger.cpp`

Post-processes raw component regions to reduce noise and false positives.

**`merge(issues)`** — clusters overlapping regions:
1. For each pair `(a, b)`, checks overlap via IoU (`intersection / union`) or
   centroid-distance (`|c1 − c2| < max(w1,w2) * 0.5`).
2. Merged region uses the larger bounding box, higher confidence, and max severity.
3. Handles multi-region merges when A overlaps B and B overlaps C.

**`filterInvalid(issues, frameW, frameH, disparityValid)`** — removes:
- **Lens boundary**: Region center within 3% of any frame edge.
- **Vignette**: Radius from center > 40% of diagonal (radial falloff artifacts).
- **Too small**: Area < `config.minArea` (default 50).
- **Near-border**: Region edge within 1% of any frame border.

Invalid regions are marked with `isInvalidRegion = true` and `invalidReason`
string. They remain in the list but are rendered with reduced opacity.

**Merge configuration** (`MergeConfig` struct):
| Field | Default | Description |
|-------|---------|-------------|
| `iouThreshold` | 0.30 | IoU threshold for overlap detection |
| `centroidDistanceRatio` | 0.50 | Centroid distance as fraction of max dimension |
| `borderMarginRatio` | 0.01 | Fraction of frame for near-border detection |
| `lensMarginRatio` | 0.03 | Fraction of frame for lens-boundary detection |
| `vignetteRadiusRatio` | 0.40 | Fraction of diagonal for vignette filtering |
| `smallRegionThreshold` | 100 | Area below which regions are invalid |
| `minArea` | 50 | Minimum component area before filtering |

---

## Visualization Module

### `Visualizer.h / Visualizer.cpp`

The `Visualizer` class renders visualization frames using OpenCV. 10 modes:

| Mode | Method | Technique |
|------|--------|-----------|
| Normal | `renderNormal()` | Full frame + mini graph overlays |
| Diff Overlay | `renderStereoDifferenceOverlay()` | Left eye + color-coded issue bounding boxes |
| Heatmap | `renderDifferenceHeatmap()` | Aligned residual `absdiff(L,warpedR)` → custom green-yellow-red colormap |
| Disparity Heatmap | `renderDisparityHeatmap()` | Dense disparity map → JET colormap; invalid pixels grayed |
| Edge Comparison | `renderEdgeComparison()` | Canny on both → composite (R=left, G=right) |
| Feature Match | `renderFeatureMatch()` | `drawMatches()` with ORB |
| Histogram View | `renderHistogramView()` | `calcHist` → line plot (B=left, G=right) |
| Blur Map | `renderBlurMap()` | Laplacian → `COLORMAP_INFERNO` |
| Blink L/R | `renderBlink()` | Single eye + label |

**Key implementation details:**
- `renderDifferenceHeatmap()` uses the aligned `result.residualDiffMap` (computed
  in `detectIssues()`) rather than raw `absdiff(L,R)`, so the heatmap shows
  genuine differences after disparity compensation.
- `renderStereoDifferenceOverlay()` draws colored bounding boxes from
  `result.detectedIssues`, each with a confidence label. Each of the 22 issue
  types has a distinct color defined in `renderIssueBoxes()` (HSV-based palette).
  Invalid regions are drawn with reduced opacity and a dashed-style label.
- `renderDisparityHeatmap()` uses `result.disparity.disparityMap` (raw float
  disparity) and `result.disparity.validMap` — invalid pixels are shown in dark
  gray, valid disparity uses JET colormap with stats overlay.

---

## Overlay Module

### `Overlay.h / Overlay.cpp`

The overlay is the user-facing component — a **transparent, always-on-top, click-through**
window rendered with DirectX 11 and Dear ImGui.

**Window creation**:
- `WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW`
- `LWA_COLORKEY` with black (RGB(0,0,0)) as transparent color.
- Full-screen size (`GetSystemMetrics(SM_CXSCREEN)` etc.).

**D3D11 pipeline**:
1. Create D3D11 device + context.
2. Create swap chain (2 buffers, BGRA8_UNORM, 144Hz refresh rate).
3. Create render target view from back buffer.

**ImGui initialization**:
- `ImGui_ImplWin32_Init(hwnd)` for Win32 input.
- `ImGui_ImplDX11_Init(device, context)` for DX11 rendering.
- Style: dark theme, no title bar, always auto-resize.

**Render loop** (`renderFrame()`):
1. Clear RT to transparent (0,0,0,0).
2. If `m_captureSafe` flag is set, skip ImGui rendering entirely (prevents
   overlay from contaminating captured screenshots).
3. `ImGui_ImplDX11_NewFrame()` + `ImGui_ImplWin32_NewFrame()` + `ImGui::NewFrame()`.
4. `renderWindows()` — tabbed Hub + Visualization window + sync popups.
5. Update visualization texture (non-Normal modes) via `updateVizTexture()`.
6. `ImGui::Render()` + `ImGui_ImplDX11_RenderDrawData()`.
7. `swapChain->Present(1, 0)` — vsync on.

**Consolidated Layout** (`renderWindows()`):

2 top-level windows, replacing the old 6-panel multi-window:

| Window | Method | Description |
|--------|--------|-------------|
| Hub Window | `renderHubWindow()` | Main tabbed window with 4 tabs: Status, Metrics, Issues, Graphs. Size/position persisted via layout.ini |
| Visualization | `renderVisualizationWindow()` | Independent window showing the current viz mode's rendered output (same as before) |

**Hub Window** (`renderHubWindow()`):

Single window with `ImGuiTabBar` containing 4 tabs:

| Tab | Method | Contents |
|-----|--------|----------|
| Status | `renderStatusTab()` | Controls area (Sync/Clear buttons + viz mode buttons), then Frame Details (L2 collapsing header) and Raw Details (L3 collapsing header) |
| Metrics | `renderMetricsTab()` | Metrics grouped into collapsible sections: Alignment (L1, default open), Disparity & Asymmetry (L2), All Metrics (L3). Shows Base/Cur/Δ per metric when baseline active, raw values when no baseline |
| Issues | `renderIssuesTab()` | Baseline deviations (severity FAIL/WARN/OK with colored tags). Region issues listed individually with type color, confidence %, and area. Hover tooltip shows top 3 alternatives. Click to select — detail panel at bottom shows full reasoning text and evidence breakdown |
| Graphs | `renderGraphsTab()` | 8 sparkline graphs (2 rows × 4 cols) via `renderGraphPanel()`, showing last N frames count |

**Progressive Disclosure** (L1/L2/L3):

- **Level 1** (summary bar + tab headers): Always visible, gives instant status.
- **Level 2** (default-open collapsing headers): Frame Details, Alignment metrics,
  issue categories — one click to expand.
- **Level 3** (collapsed by default): Raw Details (debug metrics), All Metrics
  (all 10+ comparison rows).

**Visualization Texture Upload** (`updateVizTexture()`):

Converts `cv::Mat` → `ID3D11Texture2D` → `ID3D11ShaderResourceView` for
display via `ImGui::Image()`. Supports BGR3/Gray/RGBA formats. Texture is
recreated on resize, updated via `UpdateSubresource` otherwise.

**Issue Selection**: Clicking an issue in the Issues tab sets
`m_selectedIssueIndex`. Clicking on the visualization image inverse-maps
coordinates to find the nearest issue by center distance.

**Sync Feedback**: `showSyncFeedback()` and `showSyncRefused()` display
timed green/red popups (5-second display with fade-out) showing capture
status, frame number, confidence, and timestamp.

**Layout Persistence**: Hub window position/size auto-saved to
`stereo_inspector_layout.ini` (ImGui .ini file). Reset Layout deletes the
file and resets to defaults.

**Callbacks**:

| Callback | Type | Purpose |
|----------|------|---------|
| `HistoryCallback` | `MetricHistory()` | Rolling window for live graphs |
| `LayoutCallback` | `StereoLayout()` | Detected layout for display |
| `SyncCallback` | `void()` | User clicked Sync button |
| `ClearBaselineCallback` | `void()` | User clicked Clear Baseline |

---

## Logging Module

### `Logger.h / Logger.cpp`

**`StereoLogger`** handles persistent storage of analysis results.

**`start()`**: Opens CSV file, writes header, creates screenshot directory.

**`stop()`**: Closes CSV, logs summary count.

**`logFrame(result, screenshotPath)`**:
1. Stores result in `m_results` vector.
2. Writes CSV row: timestamp, all 14 metric fields, health score, status, path.
3. Appends JSON entry (maintains valid `[ ... ]` array across writes).

**`captureScreenshot(frame, result)`**:
1. Checks if capture is needed (FAIL + `autoScreenshotOnFail`, etc.).
2. Generates filename: `YYYYMMDD_HHMMSS_frameN.png`.
3. `cv::imwrite()` to `screenshotDir`.
4. Tracks count against `maxScreenshots`.

### `ReportGenerator.h / ReportGenerator.cpp`

Generates a self-contained HTML report:

**`buildHtml(results, screenshotPaths)`**:
1. Computes summary statistics: avg/min/max health, avg SSIM, avg pixel diff,
   PASS/WARN/FAIL counts.
2. Renders summary cards (CSS grid).
3. Embeds a Chart.js line chart using `buildHealthChart()`:
   - X-axis: frame index.
   - Y-axis: health score (primary, blue line) and SSIM×100 (secondary, green line).
   - Dual Y-axes, fill under lines, tension=0.4.
4. Creates a table of all frames (sampled if >500) with status badges and issues.
5. Embeds screenshot images.

The report is a single HTML file with inline CSS and JS (Chart.js loaded from CDN).
No external dependencies beyond a browser.

---

## Input Module

### `InputManager.h / InputManager.cpp`

**`InputManager`** handles global hotkeys via `RegisterHotKey()`.

**Initialization**:
1. Registers a hidden Win32 window class (`StereoSpectorInput`).
2. Creates the hidden window.
3. Stores `this` pointer in `GWLP_USERDATA` for static `WndProc` access.

**Hotkey registration**:
```cpp
inputManager.registerHotkey(id, VK_F1, [&]() { /* toggle overlay */ });
```
- Calls `RegisterHotKey(hwnd, id, MOD_NOREPEAT, vk)`.
- Stores the callback in an `unordered_map<int, pair<UINT, callback>>`.

**`processMessages()`**: Pumps the message loop for the hidden window.
`WM_HOTKEY` messages are dispatched to callbacks.

**Pre-defined hotkeys** (registered in `main.cpp`):

| ID | Key | Action |
|----|-----|--------|
| 1 | F1 | Toggle overlay visibility |
| 2 | F2 | Toggle freeze |
| 3 | F3 | Switch to Stereo Difference Overlay |
| 4 | F4 | Switch to Difference Heatmap |
| 5 | F5 | Toggle Normal/Heatmap |
| 6 | F6 | Trigger screenshot |
| 7 | F7 | Toggle logging |
| 8 | F8 | Toggle recording |
| 9 | F9 | Set baseline (sync) — captures stereo model |
| 10 | F10 | Toggle interactive/click-through mode |

---

## Main Entry Point

### `main.cpp`

`WinMain` is the entry point. It:

1. **Setup**: Initializes spdlog (console + file), loads config.
2. **Create components**: FrameBuffer, Overlay, InputManager, Logger, ReportGenerator,
   AnalyzerPipeline, DxgiCapture.
3. **Register analyzers**: Instantiates and registers all 14 (or 15 with OCR) analyzers.
4. **Wire callbacks**: Overlay gets lambdas for latest result, time, frame,
   history, layout, sync, and clear-baseline.
5. **Register hotkeys**: Maps F1–F9 to toggle actions.
6. **Start threads**: Capture → `capture.start(buffer)`, Analysis → `analyzer.start(buffer)`.
7. **Main loop**:
   - Process input messages.
   - Render overlay frame (skipped if frozen).
   - Handle screenshot triggers.
   - Handle logging.
   - FPS tracking (1-second windows).
   - Frame rate limiting to `targetFps`.
8. **Shutdown**: Stop capture → stop analysis → stop logging → generate report →
   shutdown overlay → shutdown input manager.

---

## StereoCorrespondence Module

### `StereoCorrespondence.h / .cpp`

The core module that enables the correspondence-first pipeline. Computes dense
disparity between left and right eyes using one of four methods:

| Method | Speed | Quality | Use Case |
|--------|-------|---------|----------|
| `StereoSGBM` (default) | Medium | Good | General purpose |
| `StereoBM` | Fast | Fair | High frame rate needs |
| `OpticalFlow` (Farneback) | Slow | Moderate | When stereo rectification is unknown |
| `FeatureBased` | Medium | Sparse | Debugging / verification |

**`compute(left, right)` → `CorrespondenceResult`**:

1. Converts to grayscale, optionally downscales.
2. Runs selected stereo method → raw disparity map.
3. Filters disparity (speckle removal, uniqueness check, LR consistency).
4. Computes `warpedRight` by inverse mapping through disparity.
5. Computes `occlusionMask` via LR-check consistency.
6. Returns statistics via `computeDisparityStats()`.

**`DisparityMap`** struct: raw float `disparity`, per-pixel `confidence`,
binary `validMask`, and pre-computed stats (mean, median, std, min, max,
invalid ratio).

## TemporalAnalyzer Module

### `TemporalAnalyzer.h / .cpp`

Tracks frame-to-frame consistency for flicker detection, stability scoring,
and scene cut detection:

- **Flicker score**: Measures inter-frame luminance variance over last N frames.
- **Temporal stability**: Consistency of health/integrity metrics over time.
- **Disparity stability**: Frame-to-frame disparity mean consistency.
- **Scene cut detection**: Sudden changes in disparity/luminance trigger reset.
- Maintains last 60 frames of history in a deque.
- `reset()` clears history (called on baseline sync).

## StereoDetector Module

### `StereoDetector.h / .cpp`

Used by `DxgiCapture::splitFrame()` for automatic stereo layout detection.
Algorithm:
1. Searches candidate splits at 25%, 33%, 50%, 66%, 75% ±10% (2% steps).
2. Each candidate scored on: histogram correlation (60%), edge Jaccard (40%),
   separator darkness bonus.
3. Winning candidate mapped to `StereoLayout` enum.
4. Re-detects every 30 frames.

---

## DataCollector Module

### `DataCollector.h / DataCollector.cpp`

A headless data collection mode activated via `--collect PATH` on the command line.
Used to build ground-truth datasets for classifier optimization.

**Workflow**:
1. `StereoInspector.exe --collect D:\dataset` runs the full analysis pipeline
   without the GUI overlay.
2. For each frame, saves:
   - `frame_NNNN_left.png` / `frame_NNNN_right.png` — raw eye images
   - `dataset.jsonl` — one JSON object per frame containing:
     - frame number, left/right filenames
     - all metric outputs (SSIM, pixDiff, disparity stats, etc.)
     - all detected issues with bounding boxes, types, confidences, evidence (22 features)
     - scene confidence and alternatives list

**Labeling**: Users add a `"groundTruth"` field to each issue in `dataset.jsonl`
with the correct `IssueType` string. The `optimize_classifier.py` script reads
this field to tune coefficients.

### Classes

`CollectionFrame` — stores one frame's worth of data:
- `frameNumber`, `leftPath`, `rightPath`
- `metrics` — all scalar analysis metrics
- `issues` — vector of detected issues with evidence and type/confidence
- `sceneConfidence`, `healthScore`, `stereoStatus`

`DataCollector` — singleton managing output:
- `beginCollection(path)` — opens JSONL file, creates output directory
- `collectFrame(frame, result)` — saves PNGs, appends JSONL entry
- `endCollection()` — closes files, prints summary

---

## Tools

The `tools/` directory contains utilities for classifier calibration.

### `synthetic_dataset_gen.cpp`

Standalone tool (links only OpenCV + nlohmann_json) that generates labeled synthetic
stereo defect datasets from any source image.

**Build**: `cmake --build build --config Release --target gen_synthetic`

**Usage**:
```
gen_synthetic testpattern D:\dataset --samples 200 --size 1920x1080
gen_synthetic C:\photo.jpg D:\dataset --samples 100
```

**Defect types covered** (18 of 20 — Temporal and Depth/Disparity not simulable
from single frame):
- Lighting, Shadow, Bloom, Reflection, Texture, Material, Transparency, Edge
- Missing/Extra Geometry, Missing Object/Particle/UI
- Text, Stereo Offset, Occlusion, Post-Process, Lens Boundary

**Evidence computation**: For each injected defect, extracts the same 22 features
as the C++ `extractEvidence()` function:
- `brightnessDiff`, `contrastDiff`, `colorDiff` — per-channel mean/std deltas
- `edgeSimilarity` — Canny Jaccard index within region
- `edgeRatio` — edge pixel proportion delta
- `textureSimilarity` — Laplacian variance ratio
- `gradientConsistency` — Sobel gradient orientation correlation
- `histogramSimilarity` — 32-bin HISTCMP_CORREL
- `meanDiff`, `bloomRatio`, `shadowRatio` — diff and pixel proportion deltas
- `leftContentDensity`, `rightContentDensity` — content density per eye
- `regionSize`, `regionAspectRatio`, `regionPosX/Y`
- `nearBorder`, `nearCenter`, `hasColor`
- `featureMatchDensity`, `disparityConsistency`, `temporalStability` (set to
  defaults — not computable from single frame)

**NaN safety**: All computations guarded against zero-division and NaN;
post-processing clamps any remaining NaN/Inf values to 0.

### `optimize_classifier.py`

Python 3 script that ports all 20 scoring functions from `IssueClassifier.cpp`
and optimizes coefficients against labeled data.

**Dependencies**: `numpy`, `scipy`

**Usage**: `python tools/optimize_classifier.py dataset.jsonl`

**Pipeline**:
1. Load JSONL dataset (reads `issues` arrays from each frame entry)
2. Filter labeled issues (those with `groundTruth` field)
3. Stratified train/eval split (80/20 by type)
4. Per-type optimization:
   - L-BFGS-B (local) with L2 regularization against reference coefficients
   - Differential evolution (global) for broader search
   - Both gated by eval loss improvement
5. Joint optimization (all types simultaneously) — accepted only if eval loss improves
6. Platt scaling fit: `P(y=1|score) = 1/(1+exp(a*score+b))` per type
7. Generate `*_optimized_coeffs.h` with optimized coefficients + Platt params

**Output header format**:
```cpp
struct OptimizedCoefficients {
    static constexpr float kLighting_Difference[] = { 2.005f, 0.621f, ... };
    // ...
    static float applyPlattScaling(int typeIndex, float rawScore);
};
```

### `best_optimized_coeffs.h`

Best-known optimized coefficients from 210-sample multi-source dataset
(7 Windows lock screen images × 30 samples each). Achieves 4× loss reduction
(12.3 → 3.2) but requires real VR capture data for definitive calibration.

---

## Classifier Calibration & Real VR Capture

> **⚠️ This is the single most impactful remaining work.** The classifier's confidence
> percentages are heuristic until calibrated against real VR content.

**Why synthetic data is insufficient**:
- Shadow, Transparency, and Occlusion defects produce very similar feature vectors
  when applied at pixel level (all darken a region)
- The key discriminating features — `temporalStability` and `disparityConsistency` —
  require multi-frame or real depth data
- Synthetic defects don't reproduce real VR artifacts (lens distortion, god rays,
  foveation asymmetry, fresnel ringing)

**Required workflow**:
1. Run VR application with known stereo issues
2. Capture: `StereoInspector.exe --collect D:\real_dataset`
3. Label: add `"groundTruth"` field to each issue in `dataset.jsonl`
4. Optimize: `python tools/optimize_classifier.py D:\real_dataset\dataset.jsonl`
5. Integrate the generated `*_optimized_coeffs.h` into `IssueClassifier.cpp`

**Expected results with real data**:
- Top-1 accuracy: from ~36% (synthetic) to 75%+ (real)
- Platt-calibrated confidences: "87% confidence" means P(correct) ≈ 0.87
- Per-type thresholds replace the single global `minIssueConfidence`

---

## Known Limitations

1. **Classifier uncalibrated** — see above. All confidence percentages are heuristic.
2. **Single monitor only** — captures primary monitor (adapter 0, output 0).
3. **No HDR support** — uses BGRA8 format; HDR surfaces may fail.
4. **No GPU analysis** — All analysis runs on CPU.
5. **Logging is append-only** — no purging or rotation.
6. **Report screenshots are absolute paths** — not portable across machines.
7. **FrameQueue removed** — single-frame buffer; no queuing for backlog resilience.

---

## CMake Build System

### `CMakeLists.txt`

**Requirements**:
- CMake 3.20+ (bundled with Visual Studio 2022)
- C++20
- MSVC v143 (`MultiThreadedDLL` runtime)
- Visual Studio 2022 with "Desktop development with C++" workload

**Dependencies**:

| Library | vcpkg package | CMake target | Purpose |
|---------|---------------|--------------|---------|
| OpenCV | `opencv4[highgui]` | `OpenCV::*` | Image processing, analysis, visualization |
| spdlog | `spdlog` | `spdlog::spdlog` | Logging |
| nlohmann-json | `nlohmann-json` | `nlohmann_json::nlohmann_json` | Config serialization |
| Dear ImGui | Downloaded by `setup.ps1` | Compiled directly | UI overlay |
| Tesseract (opt) | `tesseract` | `Tesseract::*` | OCR text comparison |

> The base `opencv4` package includes core, imgproc, imgcodecs, video,
> features2d, calib3d, flann, dnn, objdetect, photo, stitching, videoio
> modules. Only `[highgui]` needs to be specified as an extra feature.

**System link libraries**: d3d11, dxgi, dxguid, d2d1, dwrite, gdi32,
user32, shell32, comctl32, Imm32, version, Winmm, comdlg32, ole32,
oleaut32, Shlwapi, UxTheme.

**Compile options**: `/W4`, `/utf-8`, `/MP` (multi-process compilation),
`/Oi` (intrinsic functions), `/fp:fast`.

**Dear ImGui integration**: The CMake build checks for
`deps/imgui/imgui.h`. If present, it compiles ImGui source files
(`imgui.cpp`, `imgui_draw.cpp`, `imgui_widgets.cpp`, `imgui_tables.cpp`)
and the Win32 + DX11 backends (`imgui_impl_win32.cpp`, `imgui_impl_dx11.cpp`).

**Build command** (from project root):
```powershell
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build . --config Release
```

The toolchain file auto-configures include paths, library paths, and DLL
deployment for all vcpkg-managed dependencies.

---

## Extending the Application

### Adding a New Analyzer

1. Create `MyAnalyzer.h` and `MyAnalyzer.cpp` in `src/analysis/modules/`:

```cpp
class MyAnalyzer : public BaseAnalyzer {
public:
    MyAnalyzer() : BaseAnalyzer("MyAnalysis", true) {}
    void analyze(const cv::Mat& left, const cv::Mat& right, AnalysisResult& result) override {
        // Compute something, store in result
    }
};
```

2. In `main.cpp`, register it:
```cpp
analyzer.registerAnalyzer(std::make_unique<MyAnalyzer>());
```

3. Add thresholds to `Config.h` and `AnalysisThresholds`.
4. Add penalty to `computeHealthScore()` in `Analyzer.cpp`.
5. Add display to `renderOverlay()` in `Overlay.cpp`.

### Adding a Visualization Mode

1. Add enum value to `VisualizationMode` in `Types.h`.
2. Add `renderMyMode()` in `Visualizer.h/.cpp`.
3. Add case in `Visualizer::render()`.
4. Add hotkey in `main.cpp`.
5. Optionally add menu toggle in overlay.

### Adding a Log Field

1. Add field to `AnalysisResult` in `Types.h`.
2. Add column to `writeCsvHeader()` / `writeCsvRow()` in `Logger.cpp`.
3. Add field to JSON entry in `writeJsonEntry()`.
4. Add column to HTML table in `ReportGenerator.cpp`.

---

## Performance Considerations

### Capture Pipeline
- DXGI `AcquireNextFrame` with 16ms timeout.
- Frame conversion requires GPU→CPU copy (staging texture + memcpy).
- At 1920×1080, a frame transfer is ~8MB (BGRA → 1920×1080×4 bytes).

### Analysis Pipeline
- The slowest analyzers are SSIM (full convolution) and Optical Flow (pyramid LK).
- ORB and StereoOffset both run feature detection (can be combined in the future).
- Total analysis time per frame on a modern CPU: ~5–25ms depending on resolution.

### Optimization Strategies
- **Resolution scaling**: Downscale eyes by 2× before analysis (add `cv::resize()`).
- **Feature reuse**: ORB detects features twice (OrbAnalyzer + StereoOffsetAnalyzer).
  These could share a single detection.
- **Pipeline batching**: Some analyzers compute intermediate results (e.g., grayscale
  conversion) that could be cached.
- **SIMD**: OpenCV already uses SIMD internally. Adding explicit vectorization for
  custom loops may help.
- **Skip on steady state**: If health score is stable for N frames, skip heavy analyzers.

### Memory
- `LatestFrameBuffer` holds 1 full frame + 2 eye crops (~3× 8MB = 24MB at 1080p).
- Analysis results are small (~500 bytes each).
- `StereoLogger` stores all results in memory until stop (~200KB per 1000 frames).

---

## Error Handling & Robustness

- **DXGI device lost**: Automatically reinitializes the duplication interface.
- **Config parse failure**: Falls back to defaults with a warning log.
- **Analyzer exceptions**: Caught per-module; one failed analyzer does not crash
  the pipeline.
- **File write failures**: Logged but non-fatal.
- **Tesseract init failure**: Disables OCR silently (unless compiled without it).
- **Window creation failure**: Application exits with error code.

---

## Setup Script

The `setup.ps1` PowerShell script automates dependency installation:

1. **vcpkg** — cloned from GitHub (if not present), bootstrapped.
2. **OpenCV** — `opencv4[highgui]` via vcpkg (builds from source, ~5 min).
3. **spdlog** — via vcpkg (prebuilt binary).
4. **nlohmann-json** — via vcpkg (header-only).
5. **Dear ImGui** — downloaded from GitHub, extracted to `deps/imgui/`.
6. **config.json** — created with default thresholds if missing.
7. **Tesseract (optional)** — via vcpkg if `-InstallTesseract` flag is used.

### Structure after setup

```
StereoInspector/
├── deps/imgui/          # Dear ImGui source files + backends
├── build/               # CMake build output (created by user)
├── vcpkg/               # vcpkg package manager (system-wide)
└── src/                 # Application source code
```

## Dependencies

| Dependency | Version | Purpose | Source |
|-----------|---------|---------|--------|
| OpenCV | 4.12.0 | Image processing | vcpkg |
| spdlog | 1.17.0 | Logging | vcpkg |
| nlohmann/json | 3.12.0 | Config serialization | vcpkg |
| Dear ImGui | 1.91.0 | UI overlay | GitHub (manual) |
| Tesseract (opt) | 5.x | OCR | vcpkg |
| DirectX 11 | — | Graphics | Windows SDK (inbox) |
| DXGI | — | Desktop capture | Windows SDK (inbox) |

---

## Build Variants

| Config | Flags | Use Case |
|--------|-------|----------|
| Debug | No optimizations, debug symbols | Development |
| Release | `/O2`, `NDEBUG` | Production |
| RelWithDebInfo | `/O2`, debug symbols | Profiling |
| MinSizeRel | `/O1` | Size-constrained |

**Command-line build**:
```powershell
cmake --build . --config Release
```

**Visual Studio**: Open `build/StereoInspector.sln`, select configuration from
the dropdown, and press F7.

### Tesseract (OCR)

To enable OCR support:

1. Install Tesseract: `.\setup.ps1 -InstallTesseract`
2. Rebuild: the build system auto-detects it via
   `find_package(Tesseract QUIET)` and sets `HAS_TESSERACT=1`.
3. Set `"enableOcr": true` in `config.json`.

Without Tesseract, the OCR analyzer registers as disabled and returns 0 for
all frames.
