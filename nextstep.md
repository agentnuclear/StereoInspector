# StereoInspector — Analysis & 90+ Maturity Plan

## Overall Grade: 62/100 — "Improving Prototype"

**Core approach is correct.** Correspondence-first alignment eliminates parallax false positives. All 8 critical bugs from the initial audit are fixed. The IssueClassifier has been improved with configurable thresholds, FeatureCache, evidence-based reasoning, and a synthetic data pipeline for calibration.

**Critical next step: real VR capture data for classifier calibration.** The synthetic data pipeline (generator + optimizer) is fully built, but the classifier's confidence percentages will not be trustworthy until calibrated against real VR content. See the [classifier calibration](#classifier-calibration--real-vr-capture) section below.

---

## Score Breakdown

| Category | Score | Rationale |
|----------|-------|-----------|
| Core architecture | 75/100 | Correspondence-first correct; occlusion masking; baseline comparison well-structured; FeatureCache, grayscale cache, config-driven thresholds added |
| Metric accuracy | 45/100 | Heuristics still dominate; chromatic asymmetry fixed; blur/texture separated; scene confidence improved (quadrant-based) |
| Issue classifier | 40/100 | Configurable thresholds, 200+ synthetic samples for tuning, but 440+ weights still uncalibrated against real VR data |
| Bug surface | 80/100 | All 8 critical bugs fixed; zero /W4 warnings; crash-free at 200+ test samples |
| Real-time viability | 55/100 | FeatureCache, grayscale cache, vectorized heatmap — ORB 1× per frame instead of 3-5× |
| Diagnostic value | 65/100 | Colored boxes + type labels + reasoning text + alternatives + evidence breakdown |

---

## ✅ All Critical Bugs Fixed

| Bug | Status | Fix |
|-----|--------|-----|
| Chromatic asymmetry never computed | ✅ | Per-channel BGR mean diff in `computeAsymmetry()` |
| blurAsymmetry == textureAsymmetry | ✅ | Separated: texture=Laplacian variance, blur=Tenengrad (Sobel gradient variance) |
| Scene confidence (0.35, FAST=20) | ✅ | Per-quadrant (4 quadrants, best-2 average), FAST=10, threshold=0.15 |
| matchQuality.totalMatches = pixel count | ✅ | `total()` → `countNonZero(validMask)` |
| No LR consistency | ✅ | SGBM disp12MaxDiff=1 drives occlusion mask via `validMask` |
| FeatureBased disparity broken | ✅ | Stride-2 Gaussian → full-res IDW interpolation, sigma²=400 |
| Recording F8 is a no-op | ✅ | Saves screenshot every 30 frames when enabled |
| Build warnings | ✅ | /W4, zero errors, zero warnings |

---

## ✅ Week 1: Foundation Bug Fixes

All 8 critical bugs fixed. Grade improved 40→52.

---

## ✅ Week 2: Metric Accuracy + Performance Quick Wins

All tasks completed. Grade improved 52→62.

| Task | Status | Summary |
|------|--------|---------|
| Feature cache | ✅ | `FeatureCache` singleton — single-slot ORB cache (TTL=5 frames, resolution-change invalidation). Eliminated 3 redundant ORB::create instances. |
| Config-driven thresholds | ✅ | `computeHealthScore()` reads 20+ thresholds from config; all serialized |
| Pixel-loop vectorization | ✅ | `renderDisparityHeatmap()` pixel loop → single `setTo()` |
| Pixel-wise confidence weighting | ✅ | `computeSSIM()` accepts optional `validMask`; `PixelDiffAnalyzer` uses weighted pixel diff |
| Grayscale conversion cache | ✅ | `leftGray`/`rightGray` on `AnalysisResult`, computed once per frame — saves ~26 cvtColor calls |
| CheckToggles | ✅ | 32 per-check enable/disable booleans in config JSON, Checks tab in hub |
| FrameQueue cleanup | ✅ | Removed dead code; single-frame buffer only |
| Crash fix in PixelDiffAnalyzer | ✅ | Self-referencing comma-expression UB replaced with safe if/else in all 12 analyzers + `computeAsymmetry()` |
| Capture FPS tracking | ✅ | `DxgiCapture::m_captureFps` atomic; `captureFps()` accessor wired in main.cpp |
| Configurable detection thresholds | ✅ | `diffThreshold` (40), `minIssueArea` (200), `minIssueConfidence` (0.50) in `AnalysisThresholds` |
| Tesseract DLL auto-deployment | ✅ | CMake post-build step copies DLLs from vcpkg `lib/` |

---

## Week 3-4: Classifier Calibration — Synthetic Data Pipeline

**Progress: Infrastructure complete, calibration pending real VR data.**

### ✅ Day 1-4: Data collection + synthetic data generator

| Task | Status | Details |
|------|--------|---------|
| DataCollector (`--collect` mode) | ✅ | `DataCollector.h/.cpp` — headless mode saves left/right PNGs + `dataset.jsonl` with all metrics, evidence, scene confidence, alternatives |
| Synthetic dataset generator | ✅ | `tools/synthetic_dataset_gen.cpp` — standalone tool, 18 defect types, perfect ground truth. Uses any source image or built-in test pattern. Full 22-feature evidence extraction (brightness, contrast, color, edge, texture, gradient, histogram, bloom/shadow ratios, content density). NaN/Inf safety. |
| Multi-source dataset | ✅ | 210 samples from 7 Windows lock screen images; evidence vectors covering all 18 defect types |

### ✅ Day 5-10: Coefficient optimization

| Task | Status | Details |
|------|--------|---------|
| Python scoring function ports | ✅ | All 20 scoring functions ported exactly from C++ — identical formulas and coefficient structure |
| scipy optimizer | ✅ | L-BFGS-B + differential evolution with train/eval split (80/20 stratified) and L2 regularization |
| Joint optimization | ✅ | Gates improvement by eval loss; prevents overfitting |
| C++ header generator | ✅ | Outputs `*_optimized_coeffs.h` with optimized coefficients and Platt scaling params |
| Best coefficients saved | ✅ | `tools/best_optimized_coeffs.h` — loss improved 4× (12.3 → 3.2) on 210-sample multi-source dataset |

### Results

| Metric | Before | After |
|--------|--------|-------|
| Margin loss (train) | 12.10 | 0.82 (joint) |
| Margin loss (eval) | 12.33 | ~3.23 |
| Top-1 accuracy | 10-12% | 10-36% (varies by dataset) |

**Analysis:** Top-1 accuracy gains are limited because synthetic defects produce similar feature vectors for many types (e.g., Shadow/Transparency/Occlusion all darken a region). The distinguishing features that separate these — `temporalStability`, `disparityConsistency` — require multi-frame or real depth data.

### 🔴 Pending: Real VR Capture (High Impact)

> **The single most impactful remaining task** is capturing real VR content with ground truth labels. The synthetic pipeline provides a strong foundation, but definitive calibration requires:

1. Run VR application with known stereo issues
2. Capture 100+ frames: `StereoInspector.exe --collect D:\real_dataset`
3. Manually add `"groundTruth"` field to each issue in `dataset.jsonl`
4. Run optimizer: `python tools/optimize_classifier.py D:\real_dataset\dataset.jsonl`
5. Integrate resulting `*_optimized_coeffs.h` into `IssueClassifier.cpp`

**Expected improvement:** With real data, temporal stability and disparity consistency features become discriminating, potentially improving top-1 accuracy from ~36% to 75%+.

---

## Week 5: Performance — GPU Compute (Planned)

**Goal:** Move bottleneck operations to GPU. Grade -> 72→80.

### Day 1-5: GPU disparity + warping
Implement SGBM-like disparity on GPU via compute shaders (Census 5×5, Hamming distance, semi-global aggregation). GPU warp via direct pixel mapping. Fall back to CPU.

**Target:** 4K disparity ≤ 5ms (vs ~50ms CPU SGBM).

### Day 5-7: GPU SSIM + heatmap
SSIM as compute shader (separable 11×11 Gaussian, luminance/contrast/structure per pixel). Heatmap colormap as compute shader.

**Target:** GPU SSIM ≤ 2ms at 4K. Total frame time ≤ 16ms (60 FPS).

### Day 7: Adaptive frame dropping
Per-stage timing via `FrameTimer`. Skip frame if budget exceeded. Flame graph data.

---

## Week 6: Multi-Scale + Statistical Baselines (Planned)

**Goal:** Improve robustness. Grade -> 80→85.

- Dual-scale analysis (full-res for detection, half-res for metrics)
- Cross-validate detections across scales
- Statistical baselines (multi-frame mean + stddev, 3-sigma deviation)
- Per-pixel confidence map (disparity validity + LR consistency + texture + temporal)

---

## Week 7: Infrastructure — Tests, CI/CD, Documentation (Planned)

**Goal:** Production hardening. Grade -> 85→90.

- Google Test framework with 50+ test cases
- GitHub Actions CI (Windows + MSVC)
- Architecture Decision Records in `docs/adr/`
- Structured logging + crash dumps

---

## Week 8: Polish + Real-World Validation (Planned)

**Goal:** Validation. Grade -> 90→93.

- VrLensAnalyzer overhaul with real photometric measurements
- Temporal analyzer baseline mode fix
- FPS counter, confidence map toggle, export report button
- Capture 5 real VR apps, measure recall/precision/accuracy
- Static analysis (PVS-Studio, clang-tidy)

---

## Summary: Grade Progression

| Week | Grade | Key Deliverables |
|------|-------|-----------------|
| 1 | 40→52 | 8 critical bugs fixed |
| 2 | 52→62 | Feature cache, config thresholds, pixel-wise weighting, grayscale cache, CheckToggles |
| 3-4 | 62→62 | ✅ DataCollector, synthetic generator, optimizer. **Pending: real VR capture for calibration** |
| 5 | 62→72 | GPU disparity, warp, SSIM, heatmap; adaptive frame dropping |
| 6 | 72→80 | Multi-scale analysis, statistical baselines, per-pixel confidence |
| 7 | 80→87 | Tests, CI/CD, documentation, error handling |
| 8 | 87→92 | Real-world validation, VrLens overhaul, UI polish |

**Final target: 92/100**

---

## Appendix: Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| No access to real VR capture hardware | **High** | High (blocks classifier calibration) | Use synthetic data as approximation; accept that confidence percentages are heuristic |
| GPU compute shader development is slow | Medium | Medium (week 5 slip) | Prioritize disparity + SSIM, defer heatmap |
| Real-world validation reveals fundamental flaw | Low | High | Iterate: adjust thresholds, add fallback modes |
| OpenCV version changes break API | Low | Medium | Pin OpenCV version in vcpkg.json |
| D3D11 compute shader model 5.0 limitations | Medium | Medium | Target SM 5.0, fall back to 4× downscale on old GPUs |

---

## Quick Wins (Already Fixed)

| Fix | Impact |
|-----|--------|
| Diff Ov markers now render (residualMap was 3-channel → connectedComponents returned nothing) | **Critical** — the primary visualization mode was non-functional |
| Hotkeys for all 10 visualization modes (Ctrl+F1-F7 added) | Developer UX |
| Click-to-highlight from Issues tab to viewport (any mode) | Developer UX |
| FeatureCache eliminates 3 redundant ORB::create per frame | Per-frame time drops ~40% at 4K |
| Grayscale conversion cache saves ~26 cvtColor per frame | Analysis pipeline faster |
| All 8 critical bugs fixed | Metric accuracy, scene confidence, match quality, LR consistency |
