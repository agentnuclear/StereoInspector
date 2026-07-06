# StereoInspector — Analysis & 90+ Maturity Plan

## Overall Grade: 40/100 — "Promising Prototype"

**Core approach is correct.** Correspondence-first alignment eliminates parallax false positives. **But numerical outputs are not trustworthy.** The pipeline is built on uncalibrated heuristics, dead code, and bugs that silently corrupt results.

As a *qualitative* visual debugging tool, it already provides value. The Diff Ov view with colored boxes, type labels, and reasoning text genuinely helps developers spot stereo defects. But every confidence percentage and integrity score is a heuristic, not a real probability.

---

## Score Breakdown

| Category | Score | Rationale |
|----------|-------|-----------|
| Core architecture | 70/100 | Correspondence-first is correct; occlusion masking; baseline comparison is well-structured |
| Metric accuracy | 35/100 | Heuristics everywhere, no ground-truth calibration, chromatic asymmetry is dead code |
| Issue classifier | 30/100 | 440+ untuned weights, 0.70 threshold is a guess, no statistical foundation |
| Bug surface | 25/100 | 8 active bugs; blurAsymmetry aliased; FeatureBased broken; matchQuality lies |
| Real-time viability | 40/100 | ORB runs 3-5x per frame, no feature caching, pixel loops in heatmap |
| Diagnostic value | 55/100 | Colored boxes + type labels + reasoning text help a developer understand issues |

---

## Critical Bugs (Destroying Accuracy)

1. **Chromatic asymmetry never computed** — declared, serialized, stored in baseline, but `computeAsymmetry()` never sets it. Remains 0.0 forever.
2. **blurAsymmetry == textureAsymmetry** — aliased to same Laplacian variance value, health score double-counts them.
3. **Scene confidence is a blunt club** — 0.35 threshold gates ALL analysis. FAST at threshold 20 fails on smooth surfaces.
4. **IssueClassifier: 440 untuned weights** — 22 functions x ~20 features, coefficients are guesses.
5. **FeatureBased disparity broken** — Gaussian blob nearest-neighbor, non-physical disparity maps.
6. **matchQuality.totalMatches = pixel count** — reports `disparity.total()`, not actual match count.
7. **Recording hotkey is a no-op** — F8 toggles `g_recording`, never read anywhere.
8. **No LR consistency** — only `disp12MaxDiff=1` in SGBM, occlusion mask is crude `absdiff > 40`.

---

# 90+ Maturity Plan (8 Weeks)

## ~~Week 1: Foundation Bug Fixes~~ ✅ COMPLETED

All 8 critical bugs fixed. Grade improved 40→52.

| Bug | Status | Summary |
|-----|--------|---------|
| Chromatic asymmetry | ✅ | Per-channel BGR mean diff now computed in `computeAsymmetry()` |
| Blur == Texture | ✅ | Separated: texture=Laplacian variance, blur=Tenengrad (Sobel gradient variance) |
| Scene confidence (0.35, FAST=20) | ✅ | Per-quadrant (4 quadrants, best-2 average), FAST=10, threshold=0.15 |
| matchQuality.totalMatches | ✅ | `total()` → `countNonZero(validMask)` |
| No LR consistency | ✅ | SGBM disp12MaxDiff=1 now drives occlusion mask via `validMask` |
| FeatureBased disparity | ✅ | Stride-2 Gaussian → full-res IDW interpolation, sigma²=400 |
| Recording F8 no-op | ✅ | Saves screenshot every 30 frames when `g_recording` is true |
| Build warnings | ✅ | /W4, zero errors, zero warnings |

---

## Week 2: Metric Accuracy + Performance Quick Wins

**Goal:** Strengthen metrics, eliminate redundant work. Grade -> 52->60.

### Day 1-2: Feature cache

**New file:** `src/analysis/FeatureCache.h`, `src/analysis/FeatureCache.cpp`

**Tasks:**
- Create `FeatureCache` singleton/class:
  - Stores ORB keypoints + descriptors + grayscale frame hash (`std::hash<std::string>` over first 32x32 pixels).
  - On lookup: compare frame hash, return cached if hit, else compute and cache.
  - TTL: invalidate after 5 frames or when resolution changes.
- Modify `OrbAnalyzer`, `StereoOffsetAnalyzer`, `StereoCorrespondence::FeatureBased`, `Visualizer::renderFeatureMatch()` to use `FeatureCache::getOrb(frame)`.
- Modify `StereoCorrespondence::compute()` to use cached features when method=FeatureBased.

**Files to modify:**
- `src/analysis/modules/OrbAnalyzer.cpp`
- `src/analysis/modules/StereoOffsetAnalyzer.cpp`
- `src/analysis/modules/StereoCorrespondence.cpp`
- `src/visualization/Visualizer.cpp`
- `CMakeLists.txt` (add new files)

**Success criteria:**
- ORB detects once per frame instead of 3-5x.
- Measured at 4K: per-frame analysis time drops by >=40%.

### Day 2-3: Config-driven thresholds + pixel-loop fixes

**Files:** `src/analysis/Analyzer.cpp` (lines 688-759, `computeHealthScore()`), `src/visualization/Visualizer.cpp` (lines 390-427, `renderDisparityHeatmap()`)

**Tasks:**
- Replace `computeHealthScore()` hardcoded thresholds with `m_impl->m_config.thresholds.*` lookups:
  - `0.88/0.75` -> `m_config.thresholds.ssimWarning / m_config.thresholds.ssimFail`
  - `3.0/8.0` -> `pixelDiffWarning / pixelDiffFail`
  - etc. for all 20+ thresholds.
- Vectorize `renderDisparityHeatmap()`: replace the pixel-wise double loop with a single `cv::Mat` operation:
  ```cpp
  cv::Mat invalidMask;
  cv::bitwise_not(dm.validMap, invalidMask);
  dispVis.setTo(cv::Scalar(40, 40, 50), invalidMask);
  ```

**Success criteria:**
- Editing `config.json` thresholds changes health score behavior without recompilation.
- `renderDisparityHeatmap` runs 10-100x faster (frame time matters for 4K).

### Day 3-4: Pixel-wise confidence weighting

**Files:** `src/analysis/Analyzer.cpp` (around lines 499-544, `analyzeFrame()`)

**Tasks:**
- After correspondence, create a `confidenceWeight` map: blend of disparity `validMap` + LR-consistent mask + scene content density (Sobel magnitude > threshold).
- Modify SSIM computation to accept weight map:
  - Current: `cv::Mat::ones()` everywhere.
  - New: `cv::Mat weight = confidenceWeight.clone()`.
  - Compute weighted SSIM: for each 11x11 block, multiply by mean weight.
- Modify pixel diff computation: instead of `countNonZero(absdiff > 10)`, compute weighted pixel diff: `sum(weight * (absdiff > 10)) / sum(weight)`.
- Store weight map in `AnalysisResult` for downstream use.

**Success criteria:**
- Low-confidence regions (disparity invalid, occluded, low texture) contribute less to SSIM/pixeldiff.
- High-confidence regions dominate the score.
- Overall score changes by <=10% on typical frames (sanity check).

### Day 4-5: Grayscale conversion cache + FrameQueue cleanup

**Files:** `src/analysis/Analyzer.cpp`, `src/core/Frame.h/.cpp`, `src/analysis/modules/` (all analyzers)

**Tasks:**
- Add `cv::Mat leftGray, rightGray` to `AnalysisResult` (or pass through frame context).
- In `analyzeFrame()`, convert to grayscale once at the top.
- Modify all analyzers that do `cvtColor(BGR2GRAY)` to accept optional grayscale input.
- Remove `FrameQueue` dead code from `Frame.h/.cpp`.
- Either implement proper multi-frame buffer or remove `FrameQueue` entirely.

**Success criteria:**
- Grayscale conversion runs once per frame instead of 5-10x.
- `FrameQueue` removed (or properly implemented with 3-frame depth).
- Builds clean.

---

## Week 3-4: Classifier Calibration -- Heuristic Optimization

**Goal:** Systematically tune IssueClassifier coefficients. Grade -> 60->72.

### Day 1-4: Ground truth dataset collection

**Tasks:**
- Build a data collection mode: `StereoInspector --collect PATH` that runs the full pipeline and saves for each frame:
  - Left + right eye PNGs
  - `detectIssues()` output (bounding boxes, types, confidences, evidence values)
  - All 20 `RegionFeatures` per issue
  - User can label: "correct type" / "wrong type" / "false positive" / "missed detection"
  - Output as JSONL (one JSON object per frame with embedded arrays).
- Collect 30+ real VR captures with known stereo defects (or synthesize using image manipulation).
- Generate synthetic ground truth: take stereo photos/videos, intentionally introduce asymmetric defects (bloom, shadow, texture, missing geometry) at known locations.

**Success criteria:**
- >=100 labeled issue regions across >=30 unique frames.
- Dataset covers at least 15 of 22 issue types.
- Each entry has ground-truth type + bounding box + severity.

### Day 5-10: Coefficient optimization

**New file:** `tools/optimize_classifier.py` (standalone Python script)

**Tasks:**
- Port the 22 scoring functions to Python (using the same formulas).
- Load ground truth dataset.
- Define loss function:
  - For each scoring function, `loss = (1.0 - score_on_positive) + score_on_negative` where score_on_positive = mean score for ground-truth examples of that type, score_on_negative = mean score for non-examples.
- Use `scipy.optimize.minimize` or grid search to find optimal coefficients.
- Constraints: coefficients stay within [0, 5], bonus terms within [0, 0.5].
- 5-fold cross-validation.
- Generate updated C++ coefficients as a header file snippet.

**Files to modify:**
- `src/analysis/IssueClassifier.cpp` -- replace hardcoded coefficients with optimized values.
- `tools/optimize_classifier.py` -- new file.

**Success criteria:**
- Per-type precision (top-1 accuracy) >=75% on held-out test set.
- Current baseline: estimate ~50-60%.
- False positive rate <=20%.

### Day 10-12: Confidence calibration

**Files:** `src/analysis/IssueClassifier.cpp` (lines 356-454, `classify()`)

**Tasks:**
- Replace the arbitrary `m_confidenceThreshold = 0.70f` with calibrated thresholds per issue type.
- Use Platt scaling on the ground truth dataset: fit `P(y=1 | score) = 1 / (1 + exp(A * score + B))` for each type.
- Per-type thresholds: choose threshold where precision >= 0.80 on validation set.
- Update `selectBest()` to use calibrated probabilities instead of raw scores.
- Add `calibratedScore` field to `DetectedIssue` (the Platt-scaled probability).

**Success criteria:**
- "87% confidence" on a TextureDifference actually means P(correct) ~= 0.87.
- Per-type calibration curves documented.
- Confidence percentages become real probabilities.

### Day 13-14: Reasoning text improvement

**File:** `src/analysis/IssueClassifier.cpp` (line ~400, `reasoningText`)

**Tasks:**
- Replace generic reasoning text with evidence-driven formatted output showing:
  - Top 3 evidence values that contributed most to the decision.
  - Comparison of top-2 alternative classifications.
  - Key discriminators (e.g., "Edge ratio 0.35 ruled out BloomDifference").
- Add severity qualifier: "High confidence (92%) -- strong bloom + shadow asymmetry" vs "Marginal (55%) -- weak evidence, could be reflection."

**Success criteria:**
- Reasoning text tells the developer *why*, not just *what*.
- Evidence breakdown helps users trust (or discount) the classification.

---

## Week 5: Performance -- GPU Compute

**Goal:** Move bottleneck operations to GPU. Grade -> 72->80.

### Day 1-5: GPU disparity + warping

**New files:** `src/compute/DisparityShader.hlsl`, `src/compute/WarpShader.hlsl`, `src/compute/ComputePipeline.h/.cpp`

**Tasks:**
- Implement SGBM-like disparity on GPU using compute shaders:
  - Census transform (5x5).
  - Hamming distance along scanlines.
  - Semi-global aggregation (8 directions, simplified to 4 for performance).
  - Sub-pixel interpolation.
- Implement warp/remap as compute shader (direct pixel mapping).
- Implement occlusion mask computation on GPU.
- Create `ComputePipeline` class that manages D3D11 compute shaders, buffers, UAVs.
- Fall back to CPU implementation if GPU compute not available.

**Success criteria:**
- GPU disparity at 4K <= 5ms (vs ~50ms+ on CPU with SGBM).
- GPU warp <= 1ms.
- Numerical results within 2% of CPU SGBM.

### Day 5-7: GPU SSIM + heatmap

**Tasks:**
- Implement SSIM as compute shader:
  - 11x11 Gaussian blur (separable horizontal + vertical) as two passes.
  - Luminance, contrast, structure terms per pixel.
  - Reduce to single SSIM score.
- Implement heatmap colormap as compute shader.
- Keep CPU fallback for both.

**Success criteria:**
- GPU SSIM at 4K <= 2ms.
- GPU heatmap <= 0.5ms.
- Frame time target: <= 16ms total (60 FPS analysis) at 4K.

### Day 7: Performance profiling + frame dropping

**File:** `src/analysis/Analyzer.cpp` (line ~360, `processLoop()`)

**Tasks:**
- Add `FrameTimer` class that tracks per-stage timing.
- Implement adaptive frame dropping: if analysis takes longer than a configurable budget (default 33ms for 30FPS), skip next frame.
- Log per-stage timing to spdlog debug for profiling.
- Generate flame graph data: stage name + duration for each frame.

**Success criteria:**
- Analysis never accumulates backlog > 1 frame.
- Frame dropping is visually smooth (skipped frames don't cause jitter).
- Timing data available for further optimization.

---

## Week 6: Multi-Scale + Statistical Baselines

**Goal:** Improve robustness and reduce noise. Grade -> 80->85.

### Day 1-3: Multi-scale analysis

**File:** `src/analysis/Analyzer.cpp` (around `analyzeFrame()`)

**Tasks:**
- Run the pipeline at two scales:
  - Scale 1: full resolution for issue detection + localization.
  - Scale 2: 2x downscale for metric computation (SSIM, pixeldiff, histograms).
- Cross-validate issue detections: if an issue is detected at both scales with high confidence, increase its confidence. If only at one scale, apply penalty.
- Merge issue maps via `RegionMerger` at both scales.

**Success criteria:**
- False positive rate drops by >=30% (issues detected at only one scale are filtered).
- Per-frame metric computation is faster (metrics on half-res).
- Edge cases: thin features (< 2px at full res) still detected.

### Day 3-5: Statistical baselines

**File:** `src/analysis/Analyzer.cpp` (lines 256-358, `compareWithBaseline()`)

**Tasks:**
- Change baseline from single-snapshot to multi-frame model:
  - On sync capture, collect N frames (default 30, ~1 second at 30fps).
  - For each metric, compute mean + stddev across the N frames.
  - Store as `baselineMean[metric]` and `baselineStd[metric]`.
- Deviation calculation changes:
  - Old: `severity = |current - ref| / tolerance`.
  - New: `severity = |current - mean| / max(std * Z, minTolerance)` where Z=3 (3-sigma).
  - Much more robust to normal scene variation.
- Show baseline variance in UI: "SSIM: 0.942 +/- 0.008" instead of "SSIM: 0.942".
- Store baseline statistics in JSON config for persistence across sessions.

**Success criteria:**
- Fewer false-positive deviations on normal scene variation.
- Baseline accurately captures typical metric noise floor.
- Integrity score is stable (+/-5%) across consecutive frames of same scene.

### Day 5-7: Per-pixel confidence map

**File:** New field in `AnalysisResult`, rendered in overlay

**Tasks:**
- Add `cv::Mat confidenceMap` to `AnalysisResult` (CV_8UC1, 0-255).
- Each pixel's confidence = blend of:
  - 40%: disparity validity (whether disparity is valid at this pixel).
  - 30%: LR consistency (lower if L->R and R->L disagree).
  - 20%: texture strength (Sobel magnitude, higher texture = more reliable comparison).
  - 10%: temporal stability (variance over last 5 frames).
- Resize to 64x64 and overlay as a semi-transparent heatmap in a new "Confidence" visualization mode (Ctrl+F8).
- Use confidence map to weight issue detection: pixels with confidence < 50 contribute 0 to issue area/severity.

**Success criteria:**
- Low-confidence visualization clearly shows where the system trusts its own output.
- Issues detected in low-confidence regions are de-prioritized.
- User can visually identify unreliable analysis regions.

---

## Week 7: Infrastructure -- Tests, CI/CD, Documentation

**Goal:** Production hardening. Grade -> 85->90.

### Day 1-3: Test suite

**New files:** `tests/` directory with `CMakeLists.txt`, `test_analyzer.cpp`, `test_correspondence.cpp`, `test_classifier.cpp`, `test_merger.cpp`, `test_config.cpp`

**Tasks:**
- Set up Google Test framework.
- Test correspondence:
  - Synthetic stereo pair (shifted checkerboard), verify disparity = known shift.
  - Occlusion mask correctness.
  - LR consistency.
- Test classifier:
  - Known feature vectors -> expected classification.
  - All 22 types exercise at least one test case each.
  - Edge cases: empty region, zero-area, all features zero.
- Test merger:
  - Overlapping regions merge correctly.
  - Non-overlapping regions kept separate.
  - Invalid region filtering.
- Test config:
  - Round-trip serialization: default -> toJson -> fromJson -> fields match.
  - Missing JSON fields use defaults.
  - `sceneConfidenceThreshold` properly persisted.
- Test scene confidence:
  - Uniform dark frame -> unreliable.
  - Bright textured frame -> reliable.
  - Dark edges + bright center -> reliable (post-fix).

**Success criteria:**
- >=50 test cases.
- All tests pass on CI.
- Code coverage >=60% for core logic (analyzer, classifier, merger, correspondence).

### Day 3-4: CI/CD

**New files:** `.github/workflows/build.yml`, `.github/workflows/test.yml`

**Tasks:**
- GitHub Actions CI:
  - Build on Windows with MSVC.
  - Install dependencies via vcpkg.
  - Run tests.
  - Build both Release and Debug.
- Add badge to README.md.
- PR checks: build + test must pass.

**Success criteria:**
- Green CI badge.
- Full build + test cycle <= 15 minutes.
- Automated release artifact on tag push.

### Day 4-5: Documentation overhaul

**Files:** `README.md`, `guide.md`, `devdoc.md`

**Tasks:**
- API documentation: document all config fields with allowed ranges and defaults.
- Architecture decision records (ADRs) in `docs/adr/`:
  - ADR-001: Correspondence-first approach.
  - ADR-002: Issue classifier design (heuristic + ML hybrid).
  - ADR-003: Baseline comparison methodology.
- Update guide.md with new features (GPU compute, confidence map, multi-scale).
- Add troubleshooting section for common issues.

**Success criteria:**
- A new user can set up and use the tool in <= 10 minutes from README.
- All config fields documented with example values.
- Devs can understand the architecture from devdoc.md + ADRs.

### Day 5-7: Error handling + logging

**Files:** `src/capture/DxgiCapture.cpp`, `src/logging/ReportGenerator.cpp`, `src/main.cpp`

**Tasks:**
- Fix `ReportGenerator::writeJsonEntry()`: use append-mode file writes instead of read-all-then-write. Open in append, seek to insert before `]`, write new entry. Or switch to per-session JSON files.
- Add structured logging: each stage logs start/end with timestamps.
- Add crash handler: `SetUnhandledExceptionFilter` to save last frame + result to crash dump.
- DXGI capture error recovery: if `AcquireNextFrame` fails with unknown error, log details and attempt reinit after 3 consecutive failures.

**Success criteria:**
- JSON log never corrupts on crash (tested with kill -9 / taskkill).
- Crash dump captures last analysis context.
- DXGI capture survives monitor mode changes, sleep/resume.

---

## Week 8: Polish + Real-World Validation

**Goal:** Smooth edges, validate against real VR content. Grade -> 90->93.

### Day 1-2: VrLensAnalyzer overhaul

**Files:** `src/analysis/modules/VrLensAnalyzer.cpp/.h`

**Tasks:**
- Replace crude approximations with photometric measurements:
  - Lens distortion: capture checkerboard through lenses, measure radial distortion coefficients.
  - Foveation: render test pattern with known detail at center vs periphery, compute MTF ratio.
  - God rays: measure point-spread function from bright point source.
- Note: these require specific test patterns. Add a "calibration mode" that guides the user through capturing test patterns for accurate VR lens measurements.
- Document limitations: lens metrics are qualitative without per-headset calibration.

**Success criteria:**
- Lens distortion metric correlates with actual radial distortion (R-squared >= 0.7).
- Foveation metric distinguishes foveated vs non-foveated rendering.
- God ray metric correlates with known lens glare scenarios.

### Day 2-3: Temporal analyzer baseline mode fix

**Files:** `src/analysis/modules/TemporalAnalyzer.cpp`, `src/analysis/Analyzer.cpp`

**Tasks:**
- Track `stereoIntegrityScore` instead of (or in addition to) `stereoHealthScore` when baseline is active.
- Update `TemporalAnalyzer::pushFrame()` to record the correct score based on which branch was taken.
- Reset temporal history on baseline capture/clear.

**Success criteria:**
- Temporal stability/deviations track correctly in baseline mode.
- No stale health score persists after baseline capture.

### Day 3-4: UI/UX polish

**Files:** `src/overlay/Overlay.cpp`, `src/main.cpp`

**Tasks:**
- Add FPS counter overlay (analysis FPS + capture FPS, properly wired).
- Add confidence map toggle (Ctrl+F8).
- Add "Export Report" button that generates full HTML report with screenshots.
- Add configuration panel within the overlay (edit thresholds live).
- Toolbar: show active baseline metrics inline.

**Success criteria:**
- User can operate the tool without touching config.json for common adjustments.
- Report export produces useful standalone document.
- FPS counters are accurate.

### Day 4-6: Real-world validation

**Tasks:**
- Capture 5 real VR applications with known stereo issues.
- Run the tool, compare detected issues against ground truth.
- Measure:
  - Recall: of all known defects, what fraction does the tool detect?
  - Precision: of all reported issues, what fraction are real?
  - Classification accuracy: of real issues, what fraction get the correct type label?
- Iterate on thresholds based on findings.
- Document validation results.

**Success criteria:**
- Recall >= 80% on known defects (currently unknown, estimate ~50%).
- Precision >= 70% (currently unknown, estimate ~40-50%).
- Classification accuracy >= 75% (currently unknown, estimate ~50-60%).
- Validation results published in `docs/validation.md`.

### Day 6-7: Static analysis + final cleanup

**Tasks:**
- Run PVS-Studio or MSVC Code Analysis (`/analyze`) on full codebase.
- Fix all warnings at `/W4` level.
- Run `clang-tidy` for modern C++ best practices.
- Remove all dead code (`FrameQueue`, `VrLensAnalyzer` old code).
- Final performance pass: ensure no O(n-squared) or unnecessary allocations in hot paths.

**Success criteria:**
- Zero warnings at `/W4`.
- Zero clang-tidy warnings.
- All dead code removed (or properly flagged with `[[deprecated]]`).
- Build time <= 30 seconds incremental.

---

## Summary: Grade Progression

| Week | Grade | Key Deliverables |
|------|-------|-----------------|
| 1 | 40->52 | 8 critical bugs fixed |
| 2 | 52->60 | Feature cache, config thresholds, pixel-wise weighting, grayscale cache |
| 3-4 | 60->72 | Classifier calibration with ground truth, confidence calibration |
| 5 | 72->80 | GPU disparity, warp, SSIM, heatmap; frame dropping |
| 6 | 80->85 | Multi-scale analysis, statistical baselines, per-pixel confidence |
| 7 | 85->90 | Tests, CI/CD, documentation, error handling |
| 8 | 90->93 | VrLens validation, temporal fix, UI polish, real-world validation |

**Final target: 93/100**

---

## Appendix: Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Ground truth collection takes longer than expected | Medium | High (blocker for week 3-4) | Start collecting now, use synthetic data as fallback |
| GPU compute shader development is slow | Medium | Medium (week 5 slip) | Prioritize disparity + SSIM, defer heatmap |
| Real-world validation reveals fundamental flaw | Low | High | Iterate: adjust thresholds, add fallback modes |
| OpenCV version changes break API | Low | Medium | Pin OpenCV version in vcpkg.json |
| D3D11 compute shader model 5.0 limitations | Medium | Medium | Target SM 5.0, fall back to 4x downscale on old GPUs |

---

## Quick Wins (Already Fixed)

| Fix | Impact |
|-----|--------|
| Diff Ov markers now render (residualMap was 3-channel -> connectedComponents returned nothing) | **Critical** -- the primary visualization mode was non-functional |
| Hotkeys for all 10 visualization modes (Ctrl+F1-F7 added) | Developer UX |
| Click-to-highlight from Issues tab to viewport (any mode) | Developer UX |
