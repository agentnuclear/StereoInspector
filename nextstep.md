# StereoInspector — Analysis & Next Steps

## Overall Grade: 40/100 — "Promising Prototype"

StereoInspector's **core approach is correct**: warp the right eye to the left coordinate system via disparity before comparing, eliminating parallax-driven false positives. That architectural decision is the single reason this tool has diagnostic value at all.

**But it is not yet quantitatively trustworthy.** The metric pipeline is built on uncalibrated heuristics, dead code, and several bugs that silently corrupt results. As a *qualitative* visual debugging tool for a VR developer who knows what to look for, it already provides genuine value — the Diff Ov view with colored bounding boxes, type labels, and reasoning text is useful for spotting stereo defects. But every numerical output (confidence percentages, integrity scores) should be treated with deep skepticism.

---

## Score Breakdown

| Category | Score | Rationale |
|----------|-------|-----------|
| Core architecture | 70/100 | Correspondence-first is correct; occlusion masking; baseline comparison with 20 metrics is well-structured |
| Metric accuracy | 35/100 | Heuristics everywhere, no ground-truth calibration, 440+ untuned coefficients; chromatic asymmetry is dead code |
| Issue classifier | 30/100 | Hand-crafted scoring functions with arbitrary weights. 0.70 confidence threshold is a guess. No statistical foundation |
| Bug surface | 25/100 | 8 active bugs (see below); blurAsymmetry aliased to textureAsymmetry; FeatureBased disparity broken; matchQuality lies |
| Real-time viability | 40/100 | ORB runs 3-5× per frame, pixel loops in heatmap rendering, no feature caching. 4K will struggle below 15fps |
| Diagnostic value | 55/100 | Colored boxes + type labels + reasoning text genuinely help a developer understand what's wrong |

---

## Critical Bugs (Destroying Accuracy)

### 1. Chromatic asymmetry never computed
`AsymmetryMetrics.chromaticAsymmetry` is declared, serialized, stored in the baseline, and tracked — but `computeAsymmetry()` never sets it. Remains `0.0` forever. Lens chromatic aberration differences between eyes are one of the most common VR stereo defects. **This is a VR-killer gap.**

### 2. blurAsymmetry == textureAsymmetry (aliased)
`computeAsymmetry()` sets both `result.asymmetry.blurAsymmetry` and `result.asymmetry.textureAsymmetry` to the same Laplacian variance ratio value. They're supposed to measure different phenomena. The health score uses both — double-counting the same metric under two names.

### 3. Scene confidence is a blunt club
The 0.35 threshold gates **all** analysis. A frame 80% dark with critical stereo content in the bright center is silently skipped. No per-region analysis. FAST corner detector (fixed threshold 20) fails on walls/sky → feature score = 0 → UNKNOWN.

### 4. IssueClassifier: 440 untuned weights
22 scoring functions × ~20 features each, with arbitrary coefficients (1.5x, 2.0x, thresholds like 0.15f, 0.3f). These are guesses. Zero calibration against known-good / known-bad stereo pairs. Confidence percentages are **heuristics that happen to range 0-1 — not probabilities.**

### 5. FeatureBased disparity is broken
Nearest-neighbor Gaussian blob interpolation (sigma=100, stride=2) produces non-physical disparity maps. Should be removed or replaced with a proper thin-plate spline.

### 6. matchQuality.totalMatches reports pixel count
`(int)corr.disparityMap.disparity.total()` gives total pixel count of the disparity map, not the number of valid feature matches. The variable name is fraudulent.

### 7. Recording hotkey does nothing
F8 toggles `g_recording` which is never read. The UI shows a recording indicator that performs zero work.

### 8. No LR consistency check
`disp12MaxDiff=1` in SGBM is the only cross-check. The occlusion mask is just `absdiff > 40` on grayscale warped images. Occlusion detection is crude.

---

## Design Weaknesses

- **No feature cache**: ORB features detected 3-5× per frame (OrbAnalyzer, StereoOffsetAnalyzer, StereoCorrespondence, Visualizer). Each detection is expensive. A shared cache would save significant CPU time.
- **All analyzers run every frame**: No caching, no dirty tracking. SSIM runs full 11×11 Gaussian convolution every frame. Grayscale conversion repeated many times.
- **renderDisparityHeatmap pixel loop**: Darkens invalid pixels in a double loop instead of using vectorized `cv::Mat` operations. Unnecessarily slow for large images.
- **FrameQueue is dead code**: Compiled but unused. `LatestFrameBuffer` (single-slot) is used throughout.
- **ReportGenerator JSON is O(n²)**: Reads entire file, removes trailing `]`, appends, writes all back. Corrupts on crash mid-write.
- **VrLensAnalyzer estimates are crude**: Radial edge density ≠ lens distortion. Center/periphery Laplacian variance ≠ foveation. Bright pixel glow ≠ god rays. Qualitative indicators at best.
- **InputManager stubs**: `triggerScreenshot()` and `toggleRecording()` set internal atomics, but `main.cpp` uses separate global atomics. No effect.
- **TemporalAnalyzer tracks stale `stereoHealthScore`**: When baseline is active, `computeHealthScore()` is not called, so `stereoHealthScore` retains its last value (or 100 default). Stability tracking is meaningless during baseline mode.

---

## Improvement Roadmap

### Phase 1 — Fix Bugs (Grade → ~55)
- [ ] Compute `chromaticAsymmetry` from per-channel disparity on aligned images
- [ ] Separate `blurAsymmetry` (Laplacian response stddev ratio) from `textureAsymmetry` (GLCM or Gabor energy ratio)
- [ ] Fix `matchQuality.totalMatches` to report actual valid match count
- [ ] Fix scene confidence: per-quadrant analysis (center 50% matters most), lower global threshold to 0.15
- [ ] Remove or fix `FeatureBased` disparity method
- [ ] Implement the recording feature or remove the hotkey + UI indicator
- [ ] Add LR consistency check: warp left→right AND right→left, keep only pixels where both agree

### Phase 2 — Strengthen Metrics (Grade → ~65)
- [ ] Feature cache: detect ORB once per frame, share across all consumers
- [ ] Pixel-wise confidence weighting: weight SSIM/pixeldiff by disparity validity + scene content density
- [ ] Replace `computeHealthScore()` hardcoded thresholds with config-driven values (hook up `m_config.thresholds`)
- [ ] Vectorize `renderDisparityHeatmap` pixel loop
- [ ] Replace `FrameQueue` with proper multi-frame buffer or remove dead code
- [ ] Wire InputManager stubs to actually call screenshot/recording

### Phase 3 — Calibrate Classifier (Grade → ~80)
- [ ] Collect 100+ labeled stereo defect images (real VR captures with known issues)
- [ ] Optimize IssueClassifier scoring coefficients against ground truth using actual ML (random forest or small CNN)
- [ ] Add per-pixel confidence map: every comparison output includes a pixel-wise confidence mask
- [ ] Multi-scale analysis: run pipeline at fullres and 2× downscale, cross-validate issue detections
- [ ] Statistical baseline: capture N frames, build mean+stddev model per metric instead of single snapshot

### Phase 4 — Production Ready (Grade → 90+)
- [ ] GPU compute: move disparity, warping, SSIM, and heatmap to compute shaders or CUDA
- [ ] Real-time automatic alignment quality: detect when correspondence is unreliable, fall back gracefully
- [ ] Profile-guided optimization: identify and eliminate hotspots in the analysis pipeline
- [ ] Test suite: automated regression tests with ground-truth stereo pairs to prevent metric regressions
- [ ] CI/CD pipeline: build + run tests on every commit

---

## Quick Wins (Already Fixed in Current Build)

| Fix | Impact |
|-----|--------|
| Diff Ov markers now render (residualMap was 3-channel → connectedComponents returned nothing) | **Critical** — the primary visualization mode was non-functional |
| Hotkeys for all 10 visualization modes (Ctrl+F1–F7 added) | Developer UX — no more clicking through menus |
| Click-to-highlight from Issues tab to viewport (any mode) | Developer UX — instantly locate a detected issue on screen |

---

## Summary

| Aspect | Verdict |
|--------|---------|
| Should you use it to find stereo bugs in VR? | **Yes, visually** — the Diff Ov overlay + issue type labels are genuinely useful |
| Should you trust the numerical scores? | **No** — confidence percentages and integrity scores are uncalibrated heuristics |
| Biggest gap for VR specifically | Chromatic asymmetry is non-functional. Lens/foveation/god-ray metrics are crude guesses |
| Single highest-impact fix | Calibrate IssueClassifier against real stereo defect data |
| Time to production quality | 3-6 months with dedicated engineering |
