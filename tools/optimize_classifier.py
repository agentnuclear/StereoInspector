#!/usr/bin/env python3
"""
Ground-truth classifier coefficient optimizer + Platt calibration.

Usage:
    # Collect data first (in C++ app):
    $ StereoInspector --collect ./dataset

    # Label ground truth by editing dataset.jsonl: add "groundTruth" field
    # to each issue entry with the correct IssueType string.

    # Run optimizer:
    $ python tools/optimize_classifier.py dataset/dataset.jsonl

    # Without ground truth labels, the script reports current performance only.
"""

import json
import math
import sys
import re
from copy import deepcopy
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple

import numpy as np

# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

ISSUE_TYPES = [
    "Lighting Difference", "Shadow Difference", "Bloom Difference",
    "Reflection Difference", "Texture Difference", "Material Difference",
    "Transparency Difference", "Edge Difference",
    "Missing Geometry", "Extra Geometry", "Missing Object",
    "Missing Particle", "Missing UI", "Text Difference",
    "Stereo Offset", "Depth/Disparity Error", "Occlusion Difference",
    "Post Process Difference", "Temporal Difference", "Lens Boundary",
]
TYPE_TO_IDX = {t: i for i, t in enumerate(ISSUE_TYPES)}


@dataclass
class Evidence:
    brightnessDiff: float = 0.0
    contrastDiff: float = 0.0
    colorDiff: float = 0.0
    edgeSimilarity: float = 1.0
    textureSimilarity: float = 1.0
    gradientConsistency: float = 1.0
    histogramSimilarity: float = 1.0
    featureMatchDensity: float = 0.0
    disparityConsistency: float = 1.0
    regionSize: float = 0.0
    regionAspectRatio: float = 1.0
    regionPosX: float = 0.0
    regionPosY: float = 0.0
    temporalStability: float = 1.0
    edgeRatio: float = 0.0
    meanDiff: float = 0.0
    bloomRatio: float = 0.0
    shadowRatio: float = 0.0
    leftContentDensity: float = 0.0
    rightContentDensity: float = 0.0
    hasColor: bool = False
    nearBorder: bool = False
    nearCenter: bool = False


@dataclass
class RegionFeatures:
    brightnessDiff: float = 0.0
    contrastDiff: float = 0.0
    meanLuminanceL: float = 0.0
    meanLuminanceR: float = 0.0
    stdLuminanceL: float = 0.0
    stdLuminanceR: float = 0.0
    colorDiff: float = 0.0
    edgeSimilarity: float = 1.0
    textureSimilarity: float = 1.0
    gradientConsistency: float = 1.0
    histogramSimilarity: float = 1.0
    featureMatchDensity: float = 0.0
    disparityConsistency: float = 1.0
    regionSize: float = 0.0
    regionAspectRatio: float = 1.0
    regionPosX: float = 0.0
    regionPosY: float = 0.0
    temporalStability: float = 1.0
    meanDiff: float = 0.0
    bloomRatio: float = 0.0
    shadowRatio: float = 0.0
    edgeRatio: float = 0.0
    leftContentDensity: float = 0.0
    rightContentDensity: float = 0.0
    hasColor: bool = False
    nearBorder: bool = False
    nearCenter: bool = False


def evidence_to_features(ev: Evidence, mean_lum_l: float = 0.5,
                         mean_lum_r: float = 0.5,
                         std_lum_l: float = 0.0, std_lum_r: float = 0.0) -> RegionFeatures:
    return RegionFeatures(
        brightnessDiff=ev.brightnessDiff,
        contrastDiff=ev.contrastDiff,
        meanLuminanceL=mean_lum_l,
        meanLuminanceR=mean_lum_r,
        stdLuminanceL=std_lum_l,
        stdLuminanceR=std_lum_r,
        colorDiff=ev.colorDiff,
        edgeSimilarity=ev.edgeSimilarity,
        textureSimilarity=ev.textureSimilarity,
        gradientConsistency=ev.gradientConsistency,
        histogramSimilarity=ev.histogramSimilarity,
        featureMatchDensity=ev.featureMatchDensity,
        disparityConsistency=ev.disparityConsistency,
        regionSize=ev.regionSize,
        regionAspectRatio=ev.regionAspectRatio,
        regionPosX=ev.regionPosX,
        regionPosY=ev.regionPosY,
        temporalStability=ev.temporalStability,
        meanDiff=ev.meanDiff,
        bloomRatio=ev.bloomRatio,
        shadowRatio=ev.shadowRatio,
        edgeRatio=ev.edgeRatio,
        leftContentDensity=ev.leftContentDensity,
        rightContentDensity=ev.rightContentDensity,
        hasColor=ev.hasColor,
        nearBorder=ev.nearBorder,
        nearCenter=ev.nearCenter,
    )


# ---------------------------------------------------------------------------
# Coefficient bundle: each scoring function stores its coefficients here.
# The optimizer tweaks these.
# ---------------------------------------------------------------------------

@dataclass
class Coeff:
    name: str
    default: float
    min_val: float = 0.0
    max_val: float = 5.0
    step: float = 0.05


SCORING_COEFFICIENTS: Dict[str, List[Coeff]] = {
    "Lighting Difference": [
        Coeff("brightnessWeight", 1.5, 0.0, 3.0),
        Coeff("contrastWeight", 0.5, 0.0, 2.0),
        Coeff("edgeSimBonus", 0.2, 0.0, 0.5),
        Coeff("textureSimBonus", 0.15, 0.0, 0.5),
        Coeff("histLowBonus", 0.15, 0.0, 0.5),
        Coeff("noBloomShadowBonus", 0.2, 0.0, 0.5),
        Coeff("edgeRatioPenalty", 0.5, 0.0, 2.0),
    ],
    "Shadow Difference": [
        Coeff("shadowWeight", 2.0, 0.0, 4.0),
        Coeff("brightnessWeight", 0.5, 0.0, 2.0),
        Coeff("shadowDominatesBonus", 0.2, 0.0, 0.5),
        Coeff("darkLuminanceBonus", 0.15, 0.0, 0.5),
        Coeff("textureSimBonus", 0.1, 0.0, 0.5),
    ],
    "Bloom Difference": [
        Coeff("bloomWeight", 2.0, 0.0, 4.0),
        Coeff("brightnessWeight", 0.3, 0.0, 2.0),
        Coeff("bloomDominatesBonus", 0.2, 0.0, 0.5),
        Coeff("brightLuminanceBonus", 0.15, 0.0, 0.5),
        Coeff("edgeSimBonus", 0.1, 0.0, 0.5),
    ],
    "Reflection Difference": [
        Coeff("colorWeight", 0.8, 0.0, 2.0),
        Coeff("brightnessWeight", 0.4, 0.0, 2.0),
        Coeff("edgeDissimWeight", 0.6, 0.0, 2.0),
        Coeff("gradientDissimWeight", 0.4, 0.0, 2.0),
        Coeff("histMidBonus", 0.2, 0.0, 0.5),
        Coeff("textureSimBonus", 0.1, 0.0, 0.5),
    ],
    "Texture Difference": [
        Coeff("textureDissimWeight", 1.5, 0.0, 3.0),
        Coeff("gradientDissimWeight", 0.3, 0.0, 2.0),
        Coeff("lowBrightnessDiffBonus", 0.15, 0.0, 0.5),
        Coeff("edgeMidBonus", 0.1, 0.0, 0.5),
        Coeff("lowEdgeRatioBonus", 0.1, 0.0, 0.5),
    ],
    "Material Difference": [
        Coeff("textureDissimWeight", 0.8, 0.0, 2.0),
        Coeff("contrastWeight", 0.6, 0.0, 2.0),
        Coeff("histDissimWeight", 0.4, 0.0, 2.0),
        Coeff("edgeMidBonus", 0.15, 0.0, 0.5),
        Coeff("colorDiffBonus", 0.1, 0.0, 0.5),
    ],
    "Transparency Difference": [
        Coeff("colorWeight", 0.5, 0.0, 2.0),
        Coeff("brightnessWeight", 0.3, 0.0, 2.0),
        Coeff("edgeDissimWeight", 0.4, 0.0, 2.0),
        Coeff("lowEdgeRatioBonus", 0.2, 0.0, 0.5),
        Coeff("histMidBonus", 0.15, 0.0, 0.5),
        Coeff("textureMidBonus", 0.1, 0.0, 0.5),
    ],
    "Edge Difference": [
        Coeff("edgeRatioWeight", 1.5, 0.0, 3.0),
        Coeff("edgeDissimWeight", 0.5, 0.0, 2.0),
        Coeff("lowBrightnessColorBonus", 0.2, 0.0, 0.5),
        Coeff("textureSimBonus", 0.1, 0.0, 0.5),
        Coeff("meanDiffPenalty", 0.5, 0.0, 2.0),
    ],
    "Missing Geometry": [
        Coeff("edgeDissimWeight", 0.6, 0.0, 2.0),
        Coeff("edgeRatioWeight", 0.8, 0.0, 2.0),
        Coeff("contentDensityDiffWeight", 0.5, 0.0, 2.0),
        Coeff("highEdgeRatioBonus", 0.2, 0.0, 0.5),
        Coeff("brightnessDiffBonus", 0.1, 0.0, 0.5),
    ],
    "Extra Geometry": [
        Coeff("edgeDissimWeight", 0.5, 0.0, 2.0),
        Coeff("edgeRatioWeight", 0.6, 0.0, 2.0),
        Coeff("contentDensityDiffWeight", 0.4, 0.0, 2.0),
        Coeff("leftDensityHigherBonus", 0.2, 0.0, 0.5),
        Coeff("lowColorDiffBonus", 0.1, 0.0, 0.5),
    ],
    "Missing Object": [
        Coeff("contentDensityDiffWeight", 1.5, 0.0, 3.0),
        Coeff("edgeDissimWeight", 0.4, 0.0, 2.0),
        Coeff("histDissimWeight", 0.3, 0.0, 2.0),
        Coeff("highEdgeRatioBonus", 0.2, 0.0, 0.5),
        Coeff("brightnessDiffBonus", 0.1, 0.0, 0.5),
        Coeff("lowTextureSimBonus", 0.1, 0.0, 0.5),
    ],
    "Missing Particle": [
        Coeff("contentDensityDiffWeight", 0.8, 0.0, 2.0),
        Coeff("edgeDissimWeight", 0.3, 0.0, 2.0),
        Coeff("smallRegionBonus", 0.3, 0.0, 0.5),
        Coeff("compactAspectBonus", 0.15, 0.0, 0.5),
        Coeff("midMeanDiffBonus", 0.1, 0.0, 0.5),
        Coeff("brightnessDiffBonus", 0.1, 0.0, 0.5),
    ],
    "Missing UI": [
        Coeff("contentDensityDiffWeight", 0.6, 0.0, 2.0),
        Coeff("edgeDissimWeight", 0.3, 0.0, 2.0),
        Coeff("nearBorderBonus", 0.2, 0.0, 0.5),
        Coeff("extremeAspectBonus", 0.15, 0.0, 0.5),
        Coeff("midEdgeRatioBonus", 0.1, 0.0, 0.5),
        Coeff("colorDiffBonus", 0.1, 0.0, 0.5),
    ],
    "Text Difference": [
        Coeff("edgeDissimWeight", 0.4, 0.0, 2.0),
        Coeff("edgeRatioWeight", 0.5, 0.0, 2.0),
        Coeff("smallRegionBonus", 0.2, 0.0, 0.5),
        Coeff("wideAspectBonus", 0.15, 0.0, 0.5),
        Coeff("brightnessDiffLowColorBonus", 0.1, 0.0, 0.5),
        Coeff("contrastDiffBonus", 0.1, 0.0, 0.5),
    ],
    "Stereo Offset": [
        Coeff("edgeRatioWeight", 0.5, 0.0, 2.0),
        Coeff("gradientDissimWeight", 0.4, 0.0, 2.0),
        Coeff("lowBrightnessColorBonus", 0.25, 0.0, 0.5),
        Coeff("textureSimBonus", 0.15, 0.0, 0.5),
        Coeff("edgeSimBonus", 0.1, 0.0, 0.5),
        Coeff("meanDiffPenalty", 0.3, 0.0, 2.0),
    ],
    "Depth/Disparity Error": [
        Coeff("disparityDissimWeight", 1.5, 0.0, 3.0),
        Coeff("gradientDissimWeight", 0.3, 0.0, 2.0),
        Coeff("edgeSimBonus", 0.15, 0.0, 0.5),
        Coeff("textureSimBonus", 0.1, 0.0, 0.5),
        Coeff("lowBrightnessDiffBonus", 0.1, 0.0, 0.5),
    ],
    "Occlusion Difference": [
        Coeff("edgeDissimWeight", 0.5, 0.0, 2.0),
        Coeff("edgeRatioWeight", 0.4, 0.0, 2.0),
        Coeff("nearBorderBonus", 0.2, 0.0, 0.5),
        Coeff("lowDisparityConsistencyBonus", 0.2, 0.0, 0.5),
        Coeff("midBrightnessDiffBonus", 0.1, 0.0, 0.5),
    ],
    "Post Process Difference": [
        Coeff("colorWeight", 0.6, 0.0, 2.0),
        Coeff("brightnessWeight", 0.3, 0.0, 2.0),
        Coeff("histDissimWeight", 0.4, 0.0, 2.0),
        Coeff("highEdgeSimBonus", 0.2, 0.0, 0.5),
        Coeff("highTextureSimBonus", 0.15, 0.0, 0.5),
        Coeff("highGradConsistencyBonus", 0.1, 0.0, 0.5),
    ],
    "Temporal Difference": [
        Coeff("temporalInstabilityWeight", 1.5, 0.0, 3.0),
        Coeff("lowBrightnessColorBonus", 0.2, 0.0, 0.5),
        Coeff("lowMeanDiffBonus", 0.15, 0.0, 0.5),
        Coeff("highEdgeSimBonus", 0.1, 0.0, 0.5),
    ],
    "Lens Boundary": [
        Coeff("nearBorderWeight", 0.4, 0.0, 1.0),
        Coeff("extremeAspectWeight", 0.2, 0.0, 0.5),
        Coeff("lowBrightnessColorBonus", 0.2, 0.0, 0.5),
        Coeff("verticalEdgeBonus", 0.15, 0.0, 0.5),
    ],
}


# ---------------------------------------------------------------------------
# Scoring functions: exact ports from IssueClassifier.cpp
# Each takes (f: RegionFeatures, c: list of float coefficients)
# ---------------------------------------------------------------------------

def score_lighting(f: RegionFeatures, c) -> float:
    s = f.brightnessDiff * c[0]
    s += f.contrastDiff * c[1]
    if f.edgeSimilarity > 0.7: s += c[2]
    if f.textureSimilarity > 0.7: s += c[3]
    if f.histogramSimilarity < 0.6: s += c[4]
    if f.bloomRatio < 0.1 and f.shadowRatio < 0.1: s += c[5]
    s -= f.edgeRatio * c[6]
    return max(0.0, min(1.0, s))


def score_shadow(f: RegionFeatures, c) -> float:
    s = f.shadowRatio * c[0]
    s += f.brightnessDiff * c[1]
    if f.shadowRatio > f.bloomRatio: s += c[2]
    if f.meanLuminanceL < 0.3 or f.meanLuminanceR < 0.3: s += c[3]
    if f.textureSimilarity > 0.6: s += c[4]
    return max(0.0, min(1.0, s))


def score_bloom(f: RegionFeatures, c) -> float:
    s = f.bloomRatio * c[0]
    s += f.brightnessDiff * c[1]
    if f.bloomRatio > f.shadowRatio: s += c[2]
    if f.meanLuminanceL > 0.7 or f.meanLuminanceR > 0.7: s += c[3]
    if f.edgeSimilarity > 0.6: s += c[4]
    return max(0.0, min(1.0, s))


def score_reflection(f: RegionFeatures, c) -> float:
    s = f.colorDiff * c[0] + f.brightnessDiff * c[1]
    s += (1.0 - f.edgeSimilarity) * c[2]
    s += (1.0 - f.gradientConsistency) * c[3]
    if 0.5 < f.histogramSimilarity < 0.85: s += c[4]
    if f.textureSimilarity > 0.5: s += c[5]
    return max(0.0, min(1.0, s))


def score_texture(f: RegionFeatures, c) -> float:
    s = (1.0 - f.textureSimilarity) * c[0]
    s += (1.0 - f.gradientConsistency) * c[1]
    if f.brightnessDiff < 0.1: s += c[2]
    if 0.5 < f.edgeSimilarity < 0.85: s += c[3]
    if f.edgeRatio < 0.1: s += c[4]
    return max(0.0, min(1.0, s))


def score_material(f: RegionFeatures, c) -> float:
    s = (1.0 - f.textureSimilarity) * c[0]
    s += abs(f.contrastDiff) * c[1]
    s += (1.0 - f.histogramSimilarity) * c[2]
    if 0.4 < f.edgeSimilarity < 0.8: s += c[3]
    if f.colorDiff > 0.05: s += c[4]
    return max(0.0, min(1.0, s))


def score_transparency(f: RegionFeatures, c) -> float:
    s = f.colorDiff * c[0] + f.brightnessDiff * c[1]
    s += (1.0 - f.edgeSimilarity) * c[2]
    if f.edgeRatio < 0.15: s += c[3]
    if 0.6 < f.histogramSimilarity < 0.9: s += c[4]
    if 0.4 < f.textureSimilarity < 0.8: s += c[5]
    return max(0.0, min(1.0, s))


def score_edge(f: RegionFeatures, c) -> float:
    s = f.edgeRatio * c[0]
    s += (1.0 - f.edgeSimilarity) * c[1]
    if f.brightnessDiff < 0.05 and f.colorDiff < 0.05: s += c[2]
    if f.textureSimilarity > 0.7: s += c[3]
    s -= f.meanDiff * c[4]
    return max(0.0, min(1.0, s))


def score_missing_geometry(f: RegionFeatures, c) -> float:
    s = (1.0 - f.edgeSimilarity) * c[0]
    s += f.edgeRatio * c[1]
    s += abs(f.leftContentDensity - f.rightContentDensity) * c[2]
    if f.edgeRatio > 0.3: s += c[3]
    if f.brightnessDiff > 0.1: s += c[4]
    return max(0.0, min(1.0, s))


def score_extra_geometry(f: RegionFeatures, c) -> float:
    s = (1.0 - f.edgeSimilarity) * c[0]
    s += f.edgeRatio * c[1]
    s += abs(f.leftContentDensity - f.rightContentDensity) * c[2]
    if f.edgeRatio > 0.3 and f.leftContentDensity > f.rightContentDensity: s += c[3]
    if f.colorDiff < 0.05: s += c[4]
    return max(0.0, min(1.0, s))


def score_missing_object(f: RegionFeatures, c) -> float:
    s = abs(f.leftContentDensity - f.rightContentDensity) * c[0]
    s += (1.0 - f.edgeSimilarity) * c[1]
    s += (1.0 - f.histogramSimilarity) * c[2]
    if f.edgeRatio > 0.4: s += c[3]
    if f.brightnessDiff > 0.2: s += c[4]
    if f.textureSimilarity < 0.3: s += c[5]
    return max(0.0, min(1.0, s))


def score_missing_particle(f: RegionFeatures, c) -> float:
    s = abs(f.leftContentDensity - f.rightContentDensity) * c[0]
    s += (1.0 - f.edgeSimilarity) * c[1]
    if f.regionSize < 2000.0: s += c[2]
    if 0.5 < f.regionAspectRatio < 2.0: s += c[3]
    if 0.1 < f.meanDiff < 0.4: s += c[4]
    if f.brightnessDiff > 0.1: s += c[5]
    return max(0.0, min(1.0, s))


def score_missing_ui(f: RegionFeatures, c) -> float:
    s = abs(f.leftContentDensity - f.rightContentDensity) * c[0]
    s += (1.0 - f.edgeSimilarity) * c[1]
    if f.nearBorder: s += c[2]
    if f.regionAspectRatio < 0.3 or f.regionAspectRatio > 3.0: s += c[3]
    if 0.2 < f.edgeRatio < 0.6: s += c[4]
    if f.colorDiff > 0.05: s += c[5]
    return max(0.0, min(1.0, s))


def score_text(f: RegionFeatures, c) -> float:
    s = (1.0 - f.edgeSimilarity) * c[0]
    s += f.edgeRatio * c[1]
    if f.regionSize < 5000.0: s += c[2]
    if f.regionAspectRatio > 1.5 or f.regionAspectRatio < 0.5: s += c[3]
    if f.brightnessDiff > 0.1 and f.colorDiff < 0.05: s += c[4]
    if f.contrastDiff > 0.1: s += c[5]
    return max(0.0, min(1.0, s))


def score_stereo_offset(f: RegionFeatures, c) -> float:
    s = f.edgeRatio * c[0]
    s += (1.0 - f.gradientConsistency) * c[1]
    if f.brightnessDiff < 0.03 and f.colorDiff < 0.03: s += c[2]
    if f.textureSimilarity > 0.8: s += c[3]
    if f.edgeSimilarity > 0.7: s += c[4]
    s -= f.meanDiff * c[5]
    return max(0.0, min(1.0, s))


def score_depth_disparity(f: RegionFeatures, c) -> float:
    s = (1.0 - f.disparityConsistency) * c[0]
    s += (1.0 - f.gradientConsistency) * c[1]
    if f.edgeSimilarity > 0.7: s += c[2]
    if f.textureSimilarity > 0.7: s += c[3]
    if f.brightnessDiff < 0.05: s += c[4]
    return max(0.0, min(1.0, s))


def score_occlusion(f: RegionFeatures, c) -> float:
    s = (1.0 - f.edgeSimilarity) * c[0]
    s += f.edgeRatio * c[1]
    if f.nearBorder: s += c[2]
    if f.disparityConsistency < 0.5: s += c[3]
    if 0.05 < f.brightnessDiff < 0.3: s += c[4]
    return max(0.0, min(1.0, s))


def score_post_process(f: RegionFeatures, c) -> float:
    s = f.colorDiff * c[0] + f.brightnessDiff * c[1]
    s += (1.0 - f.histogramSimilarity) * c[2]
    if f.edgeSimilarity > 0.8: s += c[3]
    if f.textureSimilarity > 0.8: s += c[4]
    if f.gradientConsistency > 0.8: s += c[5]
    return max(0.0, min(1.0, s))


def score_temporal(f: RegionFeatures, c) -> float:
    s = (1.0 - f.temporalStability) * c[0]
    if f.brightnessDiff < 0.03 and f.colorDiff < 0.03: s += c[1]
    if f.meanDiff < 0.05: s += c[2]
    if f.edgeSimilarity > 0.8: s += c[3]
    return max(0.0, min(1.0, s))


def score_lens_boundary(f: RegionFeatures, c) -> float:
    s = 0.0
    if f.nearBorder: s += c[0]
    if f.regionAspectRatio > 3.0 or f.regionAspectRatio < 0.2: s += c[1]
    if f.brightnessDiff < 0.05 and f.colorDiff < 0.05: s += c[2]
    if f.regionPosY < 0.1 or f.regionPosY > 0.9: s += c[3]
    return max(0.0, min(1.0, s))


SCORING_FUNCS: Dict[str, Callable] = {
    "Lighting Difference": score_lighting,
    "Shadow Difference": score_shadow,
    "Bloom Difference": score_bloom,
    "Reflection Difference": score_reflection,
    "Texture Difference": score_texture,
    "Material Difference": score_material,
    "Transparency Difference": score_transparency,
    "Edge Difference": score_edge,
    "Missing Geometry": score_missing_geometry,
    "Extra Geometry": score_extra_geometry,
    "Missing Object": score_missing_object,
    "Missing Particle": score_missing_particle,
    "Missing UI": score_missing_ui,
    "Text Difference": score_text,
    "Stereo Offset": score_stereo_offset,
    "Depth/Disparity Error": score_depth_disparity,
    "Occlusion Difference": score_occlusion,
    "Post Process Difference": score_post_process,
    "Temporal Difference": score_temporal,
    "Lens Boundary": score_lens_boundary,
}


# ---------------------------------------------------------------------------
# Classifier (Python replica)
# ---------------------------------------------------------------------------

def classify(features: RegionFeatures, coeffs: Dict[str, List[float]],
             confidence_threshold: float = 0.7) -> Tuple[str, float, List[Tuple[str, float]]]:
    scores: List[Tuple[str, float]] = []
    for tname in ISSUE_TYPES:
        func = SCORING_FUNCS[tname]
        c = coeffs.get(tname, [])
        sc = func(features, c)
        scores.append((tname, sc))

    scores.sort(key=lambda x: x[1], reverse=True)
    best_type, best_score = scores[0]

    alternatives = [(t, s) for t, s in scores[1:]
                    if s >= confidence_threshold * 0.5]

    if best_score >= confidence_threshold:
        pass
    elif best_score >= confidence_threshold * 0.7:
        pass
    else:
        best_type = "Low Confidence"
        best_score = best_score

    return best_type, best_score, alternatives


# ---------------------------------------------------------------------------
# Dataset loading
# ---------------------------------------------------------------------------

@dataclass
class LabeledIssue:
    evidence: Evidence
    mean_lum_l: float = 0.5
    mean_lum_r: float = 0.5
    std_lum_l: float = 0.0
    std_lum_r: float = 0.0
    predicted_type: str = ""
    predicted_confidence: float = 0.0
    ground_truth: Optional[str] = None  # set by user after collection


def load_dataset(jsonl_path: str) -> List[LabeledIssue]:
    issues: List[LabeledIssue] = []
    with open(jsonl_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            for issue_obj in obj.get("issues", []):
                ev = issue_obj.get("evidence", {})
                evidence = Evidence(
                    brightnessDiff=ev.get("brightnessDiff", 0.0),
                    contrastDiff=ev.get("contrastDiff", 0.0),
                    colorDiff=ev.get("colorDiff", 0.0),
                    edgeSimilarity=ev.get("edgeSimilarity", 1.0),
                    textureSimilarity=ev.get("textureSimilarity", 1.0),
                    gradientConsistency=ev.get("gradientConsistency", 1.0),
                    histogramSimilarity=ev.get("histogramSimilarity", 1.0),
                    featureMatchDensity=ev.get("featureMatchDensity", 0.0),
                    disparityConsistency=ev.get("disparityConsistency", 1.0),
                    regionSize=ev.get("regionSize", 0.0),
                    regionAspectRatio=ev.get("regionAspectRatio", 1.0),
                    regionPosX=ev.get("regionPosX", 0.0),
                    regionPosY=ev.get("regionPosY", 0.0),
                    temporalStability=ev.get("temporalStability", 1.0),
                    edgeRatio=ev.get("edgeRatio", 0.0),
                    meanDiff=ev.get("meanDiff", 0.0),
                    bloomRatio=ev.get("bloomRatio", 0.0),
                    shadowRatio=ev.get("shadowRatio", 0.0),
                    leftContentDensity=ev.get("leftContentDensity", 0.0),
                    rightContentDensity=ev.get("rightContentDensity", 0.0),
                    hasColor=ev.get("hasColor", False),
                    nearBorder=ev.get("nearBorder", False),
                    nearCenter=ev.get("nearCenter", False),
                )
                gt = issue_obj.get("groundTruth", None)
                li = LabeledIssue(
                    evidence=evidence,
                    predicted_type=issue_obj.get("type", "Unknown"),
                    predicted_confidence=issue_obj.get("confidence", 0.0),
                    ground_truth=gt,
                )
                issues.append(li)
    return issues


# ---------------------------------------------------------------------------
# Loss function
# ---------------------------------------------------------------------------

def compute_loss(issues: List[LabeledIssue],
                 coeffs: Dict[str, List[float]],
                 confidence_threshold: float = 0.7) -> float:
    total = 0.0
    count = 0
    for issue in issues:
        if issue.ground_truth is None or issue.ground_truth == "Low Confidence":
            continue
        f = evidence_to_features(issue.evidence, issue.mean_lum_l, issue.mean_lum_r,
                                 issue.std_lum_l, issue.std_lum_r)
        pred_type, pred_conf, alts = classify(f, coeffs, confidence_threshold)

        # Compute margin: score(ground_truth) - max(score(other_types))
        # We want the correct type to score highest
        gt_scores = []
        wrong_max = 0.0
        for tname in ISSUE_TYPES:
            func = SCORING_FUNCS[tname]
            c = coeffs.get(tname, [])
            sc = func(f, c)
            if tname == issue.ground_truth:
                gt_scores.append(sc)
            else:
                wrong_max = max(wrong_max, sc)

        gt_score = max(gt_scores) if gt_scores else 0.0
        margin = gt_score - wrong_max

        # Hinge-like loss: penalize negative margins heavily
        if margin < 0:
            total += 10.0 * (1.0 + abs(margin))
        elif margin < 0.15:
            total += 2.0 * (0.15 - margin)  # small gap is penalized lightly
        # else: margin >= 0.15 is good, no loss

        count += 1

    if count == 0:
        return 0.0
    return total / count


# ---------------------------------------------------------------------------
# Optimization
# ---------------------------------------------------------------------------

def get_default_coeffs() -> Dict[str, List[float]]:
    return {tname: [c.default for c in coeffs]
            for tname, coeffs in SCORING_COEFFICIENTS.items()}


def compute_type_loss(params: np.ndarray, tname: str,
                       issues: List[LabeledIssue],
                       coeff_defs: List[Coeff],
                       ref_coeffs: Dict[str, List[float]],
                       confidence_threshold: float) -> float:
    coeffs = deepcopy(ref_coeffs)
    coeffs[tname] = list(params)
    return compute_loss(issues, coeffs, confidence_threshold)


def run_scipy_optimize(issues: List[LabeledIssue],
                       ref_coeffs: Dict[str, List[float]],
                       confidence_threshold: float = 0.7) -> Dict[str, List[float]]:
    from scipy.optimize import minimize, differential_evolution

    # Split into train (80%) and eval (20%), stratified by ground_truth
    labeled = [i for i in issues if i.ground_truth is not None and i.ground_truth != "Low Confidence"]
    import random
    random.seed(42)
    random.shuffle(labeled)

    by_type: Dict[str, list] = {}
    for i in labeled:
        by_type.setdefault(i.ground_truth, []).append(i)
    train, eval_set = [], []
    for t, samples in by_type.items():
        n = len(samples)
        split = max(1, int(n * 0.8))
        train.extend(samples[:split])
        eval_set.extend(samples[split:])
    random.shuffle(train)

    best_coeffs = deepcopy(ref_coeffs)

    def compute_eval_loss():
        return compute_loss([i for i in issues if i in eval_set], best_coeffs, confidence_threshold)

    def compute_train_loss_custom(p, tname):
        c = deepcopy(ref_coeffs)
        c[tname] = list(p)
        # L2 regularization: penalize large deviations from reference
        reg = 0.01 * sum((c[tname][i] - ref_coeffs[tname][i]) ** 2 for i in range(len(c[tname])))
        return compute_loss(train, c, confidence_threshold) + reg

    best_loss = compute_loss(train, ref_coeffs, confidence_threshold)
    best_eval_loss = compute_loss([i for i in issues if i in eval_set], ref_coeffs, confidence_threshold)
    print(f"  Baseline train loss: {best_loss:.4f}, eval loss: {best_eval_loss:.4f}")

    # Phase 1: optimize each type independently with regularization
    for tname, coeff_list in SCORING_COEFFICIENTS.items():
        x0 = np.array(best_coeffs[tname], dtype=np.float64)
        bounds = [(c.min_val, c.max_val) for c in coeff_list]

        result = minimize(
            compute_train_loss_custom, x0,
            args=(tname,),
            method="L-BFGS-B", bounds=bounds,
            options={"maxiter": 200, "ftol": 1e-6})

        loss_after = compute_train_loss_custom(result.x, tname)
        if loss_after < best_loss:
            best_loss = loss_after
            best_coeffs[tname] = list(result.x)
            eval_l = compute_eval_loss()
            print(f"  {tname}: train={loss_after:.4f} eval={eval_l:.4f}")

        # Also try differential evolution
        de_result = differential_evolution(
            compute_train_loss_custom, bounds,
            args=(tname,),
            maxiter=30, popsize=15, seed=42, polish=True)

        de_loss = compute_train_loss_custom(de_result.x, tname)
        if de_loss < best_loss:
            best_loss = de_loss
            best_coeffs[tname] = list(de_result.x)
            eval_l = compute_eval_loss()
            print(f"  {tname}: train={de_loss:.4f} eval={eval_l:.4f} (DE)")

    # Only do joint optimization if it improves eval loss
    eval_before_joint = compute_eval_loss()
    print(f"  After per-type: eval loss={eval_before_joint:.4f}")

    # Phase 2: Joint optimization with strong regularization
    all_params = []
    all_bounds = []
    all_type_indices = []
    for tname, coeff_list in SCORING_COEFFICIENTS.items():
        idx = len(all_params)
        all_params.extend(best_coeffs[tname])
        all_bounds.extend([(c.min_val, c.max_val) for c in coeff_list])
        all_type_indices.append((tname, idx, len(coeff_list)))

    def joint_loss_reg(p):
        c = deepcopy(ref_coeffs)
        reg = 0.0
        for tname, start, n in all_type_indices:
            c[tname] = list(p[start:start + n])
            for i in range(n):
                reg += 0.005 * (c[tname][i] - ref_coeffs[tname][i]) ** 2
        return compute_loss(train, c, confidence_threshold) + reg

    result = minimize(joint_loss_reg, np.array(all_params, dtype=np.float64),
                      method="L-BFGS-B", bounds=all_bounds,
                      options={"maxiter": 300, "ftol": 1e-6})

    # Evaluate joint on eval set
    c_joint = deepcopy(ref_coeffs)
    for tname, start, n in all_type_indices:
        c_joint[tname] = list(result.x[start:start + n])
    eval_after_joint = compute_loss([i for i in issues if i in eval_set], c_joint, confidence_threshold)

    if eval_after_joint < eval_before_joint:
        best_coeffs = c_joint
        print(f"  Joint optimization improved eval: {eval_before_joint:.4f} -> {eval_after_joint:.4f}")
    else:
        print(f"  Joint optimization did not improve eval ({eval_after_joint:.4f} > {eval_before_joint:.4f}), keeping per-type coeffs")

    final_eval = compute_eval_loss()
    print(f"  Final eval loss: {final_eval:.4f}")
    return best_coeffs


def run_grid_search(issues: List[LabeledIssue],
                    ref_coeffs: Dict[str, List[float]],
                    confidence_threshold: float = 0.7) -> Dict[str, List[float]]:
    """Fallback if scipy is not available."""
    best_coeffs = deepcopy(ref_coeffs)
    best_loss = compute_loss(issues, ref_coeffs, confidence_threshold)
    print(f"  Baseline loss: {best_loss:.4f} (grid search fallback)")

    deltas = [-2, -1, 1, 2]
    for tname, coeff_list in SCORING_COEFFICIENTS.items():
        trial = deepcopy(best_coeffs)
        for ci, coeff_def in enumerate(coeff_list):
            old_val = trial[tname][ci]
            best_val = old_val
            improvement = True
            while improvement:
                improvement = False
                for delta in deltas:
                    new_val = best_val + delta * coeff_def.step
                    if new_val < coeff_def.min_val or new_val > coeff_def.max_val:
                        continue
                    trial[tname][ci] = new_val
                    loss = compute_loss(issues, trial, confidence_threshold)
                    if loss < best_loss:
                        best_loss = loss
                        best_val = new_val
                        improvement = True
                best_coeffs[tname][ci] = best_val
                trial[tname][ci] = best_val
            best_coeffs[tname][ci] = best_val

        if best_coeffs[tname] != ref_coeffs[tname]:
            print(f"  {tname}: updated (loss={best_loss:.4f})")

    # Fine-tune
    deltas = [-1, 1]
    for _ in range(3):
        for tname, coeff_list in SCORING_COEFFICIENTS.items():
            trial = deepcopy(best_coeffs)
            for ci, coeff_def in enumerate(coeff_list):
                old_val = trial[tname][ci]
                best_val = old_val
                for delta in deltas:
                    new_val = best_val + delta * coeff_def.step
                    if new_val < coeff_def.min_val or new_val > coeff_def.max_val:
                        continue
                    trial[tname][ci] = new_val
                    loss = compute_loss(issues, trial, confidence_threshold)
                    if loss < best_loss:
                        best_loss = loss
                        best_val = new_val
                best_coeffs[tname][ci] = best_val
                trial[tname][ci] = best_val

    print(f"  Final loss: {best_loss:.4f}")
    return best_coeffs


# ---------------------------------------------------------------------------
# Platt scaling
# ---------------------------------------------------------------------------

@dataclass
class PlattParams:
    a: float = 1.0  # slope
    b: float = 0.0  # intercept


def fit_platt_scaling(issues: List[LabeledIssue],
                      coeffs: Dict[str, List[float]],
                      confidence_threshold: float = 0.7) -> Dict[str, PlattParams]:
    """Fit sigmoid parameters per issue type: P(y=1|x) = 1/(1+exp(a*s + b))."""
    # For each type, collect (score, is_correct) pairs
    type_data: Dict[str, List[Tuple[float, int]]] = {t: [] for t in ISSUE_TYPES}

    for issue in issues:
        if issue.ground_truth is None:
            continue
        f = evidence_to_features(issue.evidence, issue.mean_lum_l, issue.mean_lum_r,
                                 issue.std_lum_l, issue.std_lum_r)
        for tname in ISSUE_TYPES:
            func = SCORING_FUNCS[tname]
            c = coeffs.get(tname, [])
            sc = func(f, c)
            is_correct = 1 if tname == issue.ground_truth else 0
            type_data[tname].append((sc, is_correct))

    params: Dict[str, PlattParams] = {}
    for tname, data in type_data.items():
        if len(data) < 5:
            params[tname] = PlattParams()
            continue

        scores = np.array([d[0] for d in data], dtype=np.float64)
        labels = np.array([d[1] for d in data], dtype=np.float64)

        # Simple method: fit logistic regression via scipy if available
        try:
            from scipy.optimize import minimize

            def neg_log_likelihood(p):
                a, b = p
                p_pred = 1.0 / (1.0 + np.exp(-(a * scores + b)))
                eps = 1e-12
                nll = -np.mean(labels * np.log(p_pred + eps)
                               + (1 - labels) * np.log(1 - p_pred + eps))
                return nll

            result = minimize(neg_log_likelihood, [1.0, 0.0], method="Nelder-Mead")
            if result.success:
                params[tname] = PlattParams(a=result.x[0], b=result.x[1])
            else:
                params[tname] = PlattParams()
        except ImportError:
            # Fallback: simple average calibration
            samples = [(s, l) for s, l in data if l == 1]
            if samples:
                mean_score = np.mean([s for s, _ in samples])
                # Stretch/shrink around the mean
                params[tname] = PlattParams(a=1.0 / max(mean_score, 0.1), b=0.0)
            else:
                params[tname] = PlattParams()

    return params


# ---------------------------------------------------------------------------
# Output generation
# ---------------------------------------------------------------------------

def generate_cpp_header(coeffs: Dict[str, List[float]],
                        platt_params: Dict[str, PlattParams],
                        confidence_threshold: float = 0.7) -> str:
    lines = [
        "// Auto-generated by optimize_classifier.py",
        "// DO NOT EDIT MANUALLY",
        "",
        "#pragma once",
        "#include <array>",
        "#include <cmath>",
        "",
        "struct OptimizedCoefficients {",
        f"    static constexpr float kConfidenceThreshold = {confidence_threshold}f;",
        "",
    ]

    # Generate coefficient arrays
    for tname, coeff_list in SCORING_COEFFICIENTS.items():
        c_array_name = tname.replace(" ", "_").replace("/", "_")
        vals = coeffs.get(tname, [c.default for c in coeff_list])
        val_str = ", ".join(f"{v:.3f}f" for v in vals)
        lines.append(f"    // {tname}")
        lines.append(f"    static constexpr float k{c_array_name}[] = {{ {val_str} }};")
        lines.append("")

    # Generate Platt params
    lines.append("    // Platt scaling parameters")
    lines.append("    struct PlattParam { float a; float b; };")
    lines.append("    static constexpr PlattParam kPlattParams[] = {")
    for tname in ISSUE_TYPES:
        pp = platt_params.get(tname, PlattParams())
        lines.append(f"        {{ {pp.a:.4f}f, {pp.b:.4f}f }},  // {tname}")
    lines.append("    };")
    lines.append("")

    lines.append("    static float applyPlattScaling(int typeIndex, float rawScore) {")
    lines.append("        if (typeIndex < 0 || typeIndex >= 20) return rawScore;")
    lines.append("        const auto& p = kPlattParams[typeIndex];")
    lines.append("        return 1.0f / (1.0f + std::exp(-(p.a * rawScore + p.b)));")
    lines.append("    }")
    lines.append("};")
    lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def evaluate(issues: List[LabeledIssue],
             coeffs: Dict[str, List[float]],
             confidence_threshold: float = 0.7) -> None:
    """Print evaluation metrics against ground truth."""
    labeled = [i for i in issues if i.ground_truth is not None
               and i.ground_truth != "Low Confidence"]
    total = len(labeled)
    if total == 0:
        print("  No ground-truth labels found. Add 'groundTruth' to issues in the JSONL.")
        print("  Evaluating predictions vs C++ classifier output only (no GT).")
        return

    correct = 0
    confusion: Dict[str, Dict[str, int]] = {}
    for t in ISSUE_TYPES + ["Low Confidence"]:
        confusion[t] = {}

    for issue in labeled:
        f = evidence_to_features(issue.evidence, issue.mean_lum_l, issue.mean_lum_r,
                                 issue.std_lum_l, issue.std_lum_r)
        pred_type, pred_conf, _ = classify(f, coeffs, confidence_threshold)

        if pred_type == issue.ground_truth:
            correct += 1

        gt = issue.ground_truth
        if gt not in confusion:
            confusion[gt] = {}
        if pred_type not in confusion[gt]:
            confusion[gt][pred_type] = 0
        confusion[gt][pred_type] += 1

    acc = correct / total if total > 0 else 0.0
    print(f"  Accuracy: {correct}/{total} = {acc * 100:.1f}%")

    # Per-type stats
    print("\n  Per-type accuracy:")
    for t in ISSUE_TYPES:
        row = confusion.get(t, {})
        total_t = sum(row.values())
        correct_t = row.get(t, 0)
        if total_t > 0:
            print(f"    {t:25s}: {correct_t}/{total_t} = {correct_t/total_t*100:.1f}%")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    jsonl_path = sys.argv[1]
    print(f"Loading dataset from {jsonl_path}...")
    issues = load_dataset(jsonl_path)
    print(f"  Loaded {len(issues)} issue instances")

    # Check for ground truth labels
    labeled = [i for i in issues if i.ground_truth is not None
               and i.ground_truth != "Low Confidence"]
    print(f"  Labeled (with ground truth): {len(labeled)}")

    coeffs = get_default_coeffs()
    confidence_threshold = 0.7

    if labeled:
        print("\nBaseline evaluation (original coefficients):")
        evaluate(issues, coeffs, confidence_threshold)

        print("\nRunning scipy optimization...")
        try:
            opt_coeffs = run_scipy_optimize(issues, coeffs, confidence_threshold)
        except ImportError:
            print("  scipy not available; falling back to grid search")
            opt_coeffs = run_grid_search(issues, coeffs, confidence_threshold)

        print("\nPost-optimization evaluation:")
        evaluate(issues, opt_coeffs, confidence_threshold)

        print("\nFitting Platt scaling...")
        try:
            import numpy as np
            platt = fit_platt_scaling(issues, opt_coeffs, confidence_threshold)
        except ImportError:
            print("  numpy not available; skipping Platt scaling")
            platt = {t: PlattParams() for t in ISSUE_TYPES}

        # Generate C++ header
        header = generate_cpp_header(opt_coeffs, platt, confidence_threshold)
        output_path = jsonl_path.replace(".jsonl", "_optimized_coeffs.h")
        with open(output_path, "w") as f:
            f.write(header)
        print(f"\nWrote optimized coefficients to {output_path}")
    else:
        print("\nNo ground truth labels found.")
        print("To label data, add 'groundTruth' field to issue entries in the JSONL.")
        print("Example:")
        print('  {"type": "Lighting Difference", "groundTruth": "Lighting Difference", ...}')

        # Still evaluate C++ predictions vs Python replica
        print("\nChecking Python replica vs C++ predictions:")
        mismatches = 0
        for i, issue in enumerate(issues):
            f = evidence_to_features(issue.evidence, issue.mean_lum_l, issue.mean_lum_r,
                                     issue.std_lum_l, issue.std_lum_r)
            pred_type, pred_conf, _ = classify(f, coeffs, confidence_threshold)
            if pred_type != issue.predicted_type:
                mismatches += 1
                if mismatches <= 5:
                    print(f"  Issue {i}: C++={issue.predicted_type} "
                          f"Python={pred_type}")
        if mismatches == 0:
            print("  All predictions match! Python replica is correct.")
        else:
            print(f"  {mismatches}/{len(issues)} mismatches "
                  f"(expected if C++ uses different evidence/features)")


if __name__ == "__main__":
    main()
