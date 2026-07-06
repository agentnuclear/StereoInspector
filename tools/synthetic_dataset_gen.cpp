// Synthetic stereo defect dataset generator.
// Takes any source image, creates fake stereo pairs, injects asymmetric defects
// at known locations, and outputs PNGs + JSONL with perfect ground truth labels.
//
// Build: cmake --build build --config Release --target gen_synthetic
// Usage: gen_synthetic source.jpg output_dir [--samples N] [--size WxH]

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;
using json = nlohmann::json;

enum class DefectType {
    LightingDifference,
    ShadowDifference,
    BloomDifference,
    ReflectionDifference,
    TextureDifference,
    MaterialDifference,
    TransparencyDifference,
    EdgeDifference,
    MissingGeometry,
    ExtraGeometry,
    MissingObject,
    MissingParticle,
    MissingUI,
    TextDifference,
    StereoOffset,
    OcclusionDifference,
    PostProcessDifference,
    LensBoundary,
};

static const char* defectName(DefectType t) {
    switch (t) {
        case DefectType::LightingDifference:    return "Lighting Difference";
        case DefectType::ShadowDifference:      return "Shadow Difference";
        case DefectType::BloomDifference:       return "Bloom Difference";
        case DefectType::ReflectionDifference:  return "Reflection Difference";
        case DefectType::TextureDifference:     return "Texture Difference";
        case DefectType::MaterialDifference:    return "Material Difference";
        case DefectType::TransparencyDifference:return "Transparency Difference";
        case DefectType::EdgeDifference:        return "Edge Difference";
        case DefectType::MissingGeometry:       return "Missing Geometry";
        case DefectType::ExtraGeometry:         return "Extra Geometry";
        case DefectType::MissingObject:         return "Missing Object";
        case DefectType::MissingParticle:       return "Missing Particle";
        case DefectType::MissingUI:             return "Missing UI";
        case DefectType::TextDifference:        return "Text Difference";
        case DefectType::StereoOffset:          return "Stereo Offset";
        case DefectType::OcclusionDifference:   return "Occlusion Difference";
        case DefectType::PostProcessDifference: return "Post Process Difference";
        case DefectType::LensBoundary:          return "Lens Boundary";
    }
    return "Unknown";
}

static std::mt19937 rng(std::random_device{}());
static float randFloat(float min, float max) {
    return std::uniform_real_distribution<float>(min, max)(rng);
}
static int randInt(int min, int max) {
    return std::uniform_int_distribution<int>(min, max)(rng);
}
static cv::Rect randRegion(int imgW, int imgH, int minSize, int maxSize) {
    int w = randInt(minSize, maxSize);
    int h = randInt(minSize, maxSize);
    int x = randInt(0, std::max(1, imgW - w));
    int y = randInt(0, std::max(1, imgH - h));
    return cv::Rect(x, y, w, h);
}

// Each applier takes the right-eye image and its dimensions, applies the
// asymmetric defect, and returns the affected bounding box.
static cv::Rect doLightingDiff(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 40, W / 3);
    float delta = randFloat(-0.3f, 0.3f);
    cv::Mat roi = right(box);
    roi.convertTo(roi, -1, 1.0, delta * 255.0);
    return box;
}
static cv::Rect doShadowDiff(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 40, W / 3);
    float darken = randFloat(0.3f, 0.7f);
    right(box) *= darken;
    return box;
}
static cv::Rect doBloomDiff(cv::Mat& right, int W, int H) {
    int cx = randInt(W / 6, 5 * W / 6);
    int cy = randInt(H / 6, 5 * H / 6);
    int radius = randInt(20, 80);
    float intensity = randFloat(0.3f, 1.0f);
    cv::Mat glow(right.size(), right.type(), cv::Scalar::all(0));
    cv::circle(glow, cv::Point(cx, cy), radius, cv::Scalar::all(255.0 * intensity), -1);
    cv::GaussianBlur(glow, glow, cv::Size(0, 0), radius * 0.5);
    cv::add(right, glow, right);
    return cv::Rect(cx - radius, cy - radius, 2 * radius, 2 * radius) & cv::Rect(0, 0, W, H);
}
static cv::Rect doReflectionDiff(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 40, W / 3);
    float alpha = randFloat(0.15f, 0.45f);
    cv::Mat overlay(right.size(), right.type(), cv::Scalar(
        randFloat(150, 255), randFloat(150, 255), randFloat(150, 255)));
    cv::addWeighted(overlay(box), alpha, right(box), 1.0f - alpha, 0, right(box));
    return box;
}
static cv::Rect doTextureDiff(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 40, W / 3);
    int ksize = randInt(5, 15) | 1;
    cv::GaussianBlur(right(box), right(box), cv::Size(ksize, ksize), 0);
    return box;
}
static cv::Rect doMaterialDiff(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 40, W / 3);
    float gamma = randFloat(0.4f, 2.5f);
    cv::Mat lookUpTable(1, 256, CV_8U);
    uchar* p = lookUpTable.ptr();
    for (int i = 0; i < 256; i++)
        p[i] = cv::saturate_cast<uchar>(std::pow(i / 255.0, gamma) * 255.0);
    cv::LUT(right(box), lookUpTable, right(box));
    return box;
}
static cv::Rect doTransparencyDiff(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 40, W / 3);
    float alpha = randFloat(0.3f, 0.7f);
    right(box) *= alpha;
    return box;
}
static cv::Rect doEdgeDiff(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 40, W / 3);
    cv::Mat roi = right(box);
    cv::Mat blurred;
    cv::GaussianBlur(roi, blurred, cv::Size(0, 0), 3);
    float strength = randFloat(0.5f, 2.0f);
    cv::addWeighted(roi, 1.0f + strength, blurred, -strength, 0, roi);
    return box;
}
static cv::Rect doMissingGeometry(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 30, W / 4);
    right(box).setTo(cv::Scalar(0, 0, 0));
    return box;
}
static cv::Rect doExtraGeometry(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 30, W / 5);
    cv::Scalar color(randInt(50, 255), randInt(50, 255), randInt(50, 255));
    cv::rectangle(right, box, color, -1);
    cv::Mat roi = right(box);
    cv::putText(roi, "X", cv::Point(5, roi.rows / 2),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0), 2);
    return box;
}
static cv::Rect doMissingObject(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 60, W / 2);
    cv::Scalar avg = cv::mean(right(box));
    right(box).setTo(avg);
    cv::Mat noise(box.size(), CV_8UC3);
    cv::randn(noise, cv::Scalar::all(0), cv::Scalar::all(10));
    right(box) += noise;
    return box;
}
static cv::Rect doMissingParticle(cv::Mat& right, int W, int H) {
    int numDots = randInt(3, 15);
    int minR = std::max(1, W / 300);
    int maxR = std::max(2, W / 100);
    int totalW = 0, totalH = 0, minX = W, minY = H;
    for (int i = 0; i < numDots; i++) {
        int x = randInt(0, W - 1);
        int y = randInt(0, H - 1);
        int r = randInt(minR, maxR);
        cv::circle(right, cv::Point(x, y), r, cv::Scalar(0, 0, 0), -1);
        minX = std::min(minX, x - r);
        minY = std::min(minY, y - r);
        totalW = std::max(totalW, x + r - minX);
        totalH = std::max(totalH, y + r - minY);
    }
    return cv::Rect(minX, minY, totalW, totalH) & cv::Rect(0, 0, W, H);
}
static cv::Rect doMissingUI(cv::Mat& right, int W, int H) {
    int margin = W / 10;
    int x = randInt(0, 2) == 0 ? randInt(0, margin) : randInt(W - margin - 80, W - 80);
    int y = randInt(0, 2) == 0 ? randInt(0, margin) : randInt(H - margin - 30, H - 30);
    int w = randInt(40, 120);
    int h = randInt(16, 40);
    cv::Rect box(x, y, w, h);
    box &= cv::Rect(0, 0, W, H);
    if (box.width < 10 || box.height < 5) box = cv::Rect(5, 5, 60, 20);
    cv::Scalar bg(randInt(30, 80), randInt(30, 80), randInt(30, 80));
    cv::Scalar fg(randInt(180, 255), randInt(180, 255), randInt(180, 255));
    cv::Mat roi = right(box);
    cv::rectangle(roi, cv::Rect(0, 0, roi.cols, roi.rows), bg, -1);
    cv::putText(roi, "OK", cv::Point(10, roi.rows - 6), cv::FONT_HERSHEY_SIMPLEX, 0.5, fg, 1);
    return box;
}
static cv::Rect doTextDiff(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 40, W / 3);
    cv::Scalar color(randInt(0, 255), randInt(0, 255), randInt(0, 255));
    const char* texts[] = {"HELLO", "WORLD", "TEST", "ERROR", "WARN", "3D", "VR", "FPS"};
    const char* msg = texts[randInt(0, 7)];
    double scale = randFloat(0.5, 2.0);
    int thickness = randInt(1, 3);
    cv::putText(right(box), msg, cv::Point(5, box.height / 2),
                cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness);
    return box;
}
static cv::Rect doStereoOffset(cv::Mat& right, int W, int H) {
    (void)H;
    auto box = randRegion(W, H, 30, W / 5);
    int shift = randInt(5, 20);
    if (randInt(0, 1) == 0) shift = -shift;
    cv::Mat roi = right(box).clone();
    cv::Mat shifted;
    cv::Mat M = (cv::Mat_<double>(2, 3) << 1, 0, (double)shift, 0, 1, 0);
    cv::warpAffine(roi, shifted, M, roi.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    shifted.copyTo(right(box));
    return box;
}
static cv::Rect doOcclusionDiff(cv::Mat& right, int W, int H) {
    int side = randInt(0, 3);
    int sz = randInt(W / 20, W / 8);
    cv::Rect box;
    switch (side) {
        case 0: box = cv::Rect(0, randInt(0, H - sz), sz, sz); break;
        case 1: box = cv::Rect(W - sz, randInt(0, H - sz), sz, sz); break;
        case 2: box = cv::Rect(randInt(0, W - sz), 0, sz, sz); break;
        case 3: box = cv::Rect(randInt(0, W - sz), H - sz, sz, sz); break;
    }
    cv::Scalar color(randInt(20, 60), randInt(20, 60), randInt(20, 60));
    cv::Mat roi = right(box);
    cv::rectangle(roi, cv::Rect(0, 0, roi.cols, roi.rows), color, -1);
    return box;
}
static cv::Rect doPostProcessDiff(cv::Mat& right, int W, int H) {
    float rShift = randFloat(-0.1f, 0.1f);
    float gShift = randFloat(-0.1f, 0.1f);
    float bShift = randFloat(-0.1f, 0.1f);
    cv::Mat lookUpTable(1, 256, CV_8UC3);
    for (int i = 0; i < 256; i++) {
        lookUpTable.at<cv::Vec3b>(i) = cv::Vec3b(
            cv::saturate_cast<uchar>(i * (1.0f + bShift)),
            cv::saturate_cast<uchar>(i * (1.0f + gShift)),
            cv::saturate_cast<uchar>(i * (1.0f + rShift)));
    }
    cv::LUT(right, lookUpTable, right);
    return cv::Rect(0, 0, W, H);
}
static cv::Rect doLensBoundary(cv::Mat& right, int W, int H) {
    cv::Mat vignette(right.size(), CV_32FC1, cv::Scalar(1.0));
    cv::Point center(W / 2, H / 2);
    float maxDist = std::sqrt((float)(center.x * center.x + center.y * center.y));
    float strength = randFloat(0.3f, 0.7f);
    for (int y = 0; y < H; y++) {
        float* row = vignette.ptr<float>(y);
        for (int x = 0; x < W; x++) {
            float d = std::sqrt((float)((x - center.x) * (x - center.x) + (y - center.y) * (y - center.y)));
            row[x] = 1.0f - strength * std::max(0.0f, (d / maxDist - 0.5f) / 0.5f);
        }
    }
    cv::Mat fimg;
    right.convertTo(fimg, CV_32FC3);
    std::vector<cv::Mat> channels(3);
    cv::split(fimg, channels);
    for (auto& ch : channels) cv::multiply(ch, vignette, ch);
    cv::merge(channels, fimg);
    fimg.convertTo(right, CV_8UC3);
    return cv::Rect(0, 0, W, H);
}

// ---- Evidence computation ----
// Computes feature vector for the classifier optimizer.
static json computeEvidence(const cv::Mat& left, const cv::Mat& right,
                             const cv::Rect& box, int frameW, int frameH) {
    cv::Mat lGray, rGray;
    cv::cvtColor(left(box), lGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(right(box), rGray, cv::COLOR_BGR2GRAY);

    cv::Mat diff;
    cv::absdiff(lGray, rGray, diff);

    cv::Scalar mL, mR, sL, sR;
    cv::meanStdDev(lGray, mL, sL);
    cv::meanStdDev(rGray, mR, sR);

    float brightnessDiff = (float)std::abs(mL[0] - mR[0]) / 255.0f;
    float contrastDiff   = (float)std::abs(sL[0] - sR[0]) / 255.0f;
    float meanDiff       = (float)cv::mean(diff)[0] / 255.0f;

    // Color diff (per-channel BGR)
    float colorDiff = 0.0f;
    if (left.channels() == 3) {
        double cd = 0.0;
        for (int c = 0; c < 3; c++) {
            cv::Scalar m1 = cv::mean(left(box).reshape(1, left(box).rows).col(c));
            cv::Scalar m2 = cv::mean(right(box).reshape(1, right(box).rows).col(c));
            cd += std::abs(m1[0] - m2[0]);
        }
        colorDiff = (float)(cd / (3.0 * 255.0));
    }

    // Edge similarity (Jaccard on Canny)
    cv::Mat eL, eR;
    cv::Canny(lGray, eL, 50.0, 150.0);
    cv::Canny(rGray, eR, 50.0, 150.0);
    float edgeSim = 0.0f;
    {
        cv::Mat inter, uni;
        cv::bitwise_and(eL, eR, inter);
        cv::bitwise_or(eL, eR, uni);
        double countI = cv::countNonZero(inter);
        double countU = cv::countNonZero(uni);
        edgeSim = countU > 0 ? (float)(countI / countU) : 1.0f;
    }

    double edgePixL = (double)cv::countNonZero(eL);
    double edgePixR = (double)cv::countNonZero(eR);
    double totalPix = (double)(box.width * box.height);
    float edgeRatio = totalPix > 0 ? (float)(std::abs(edgePixL - edgePixR) / totalPix) : 0.0f;

    // Texture similarity (Laplacian variance ratio)
    cv::Mat lapL, lapR;
    cv::Laplacian(lGray, lapL, CV_32F);
    cv::Laplacian(rGray, lapR, CV_32F);
    cv::Scalar lmL, lmR, lsL, lsR;
    cv::meanStdDev(lapL, lmL, lsL);
    cv::meanStdDev(lapR, lmR, lsR);
    float maxVar = std::max((float)lsL[0], (float)lsR[0]);
    float texSim = maxVar > 1e-6f ? (float)(1.0 - std::abs(lsL[0] - lsR[0]) / maxVar) : 1.0f;

    // Histogram similarity (correlation)
    float histSim = 1.0f;
    {
        cv::Mat hL, hR;
        int hBins = 32;
        float range[] = {0, 256};
        const float* histRange = range;
        cv::calcHist(&lGray, 1, 0, cv::Mat(), hL, 1, &hBins, &histRange);
        cv::calcHist(&rGray, 1, 0, cv::Mat(), hR, 1, &hBins, &histRange);
        cv::normalize(hL, hL, 1.0, 0.0, cv::NORM_L1);
        cv::normalize(hR, hR, 1.0, 0.0, cv::NORM_L1);
        histSim = (float)std::max(0.0, cv::compareHist(hL, hR, cv::HISTCMP_CORREL));
    }

    // Gradient consistency (Sobel orientation correlation)
    float gradCons = 1.0f;
    {
        cv::Mat gxL, gyL, gxR, gyR;
        cv::Sobel(lGray, gxL, CV_32F, 1, 0, 3);
        cv::Sobel(lGray, gyL, CV_32F, 0, 1, 3);
        cv::Sobel(rGray, gxR, CV_32F, 1, 0, 3);
        cv::Sobel(rGray, gyR, CV_32F, 0, 1, 3);
        cv::Mat angleL, angleR;
        cv::phase(gxL, gyL, angleL);
        cv::phase(gxR, gyR, angleR);
        cv::Mat angleDiff;
        cv::absdiff(angleL, angleR, angleDiff);
        float pi = 3.14159265f;
        gradCons = 1.0f - (float)cv::mean(angleDiff)[0] / pi;
        gradCons = std::max(0.0f, std::min(1.0f, gradCons));
    }

    // Bloom/shadow ratios
    int brightPixL = 0, brightPixR = 0;
    int darkPixL = 0, darkPixR = 0;
    for (int y = 0; y < lGray.rows; y++) {
        for (int x = 0; x < lGray.cols; x++) {
            if (lGray.at<uchar>(y, x) > 200) brightPixL++;
            else if (lGray.at<uchar>(y, x) < 50) darkPixL++;
            if (rGray.at<uchar>(y, x) > 200) brightPixR++;
            else if (rGray.at<uchar>(y, x) < 50) darkPixR++;
        }
    }
    float bloomRatio = totalPix > 0 ? std::abs((float)brightPixL - (float)brightPixR) / (float)totalPix : 0.0f;
    float shadowRatio = totalPix > 0 ? std::abs((float)darkPixL - (float)darkPixR) / (float)totalPix : 0.0f;

    // Content density (pixels > 30)
    int contentL = 0, contentR = 0;
    for (int y = 0; y < lGray.rows; y++)
        for (int x = 0; x < lGray.cols; x++) {
            if (lGray.at<uchar>(y, x) > 30) contentL++;
            if (rGray.at<uchar>(y, x) > 30) contentR++;
        }
    float leftContentDensity  = totalPix > 0 ? (float)contentL / (float)totalPix : 0.0f;
    float rightContentDensity = totalPix > 0 ? (float)contentR / (float)totalPix : 0.0f;

    float posX = (float)box.x / (float)std::max(frameW, 1);
    float posY = (float)box.y / (float)std::max(frameH, 1);

    json ev;
    ev["brightnessDiff"]       = brightnessDiff;
    ev["contrastDiff"]         = contrastDiff;
    ev["colorDiff"]            = colorDiff;
    ev["edgeSimilarity"]       = edgeSim;
    ev["edgeRatio"]            = edgeRatio;
    ev["textureSimilarity"]    = texSim;
    ev["gradientConsistency"]  = gradCons;
    ev["histogramSimilarity"]  = histSim;
    ev["featureMatchDensity"]  = 0.0f;   // not computable from single frame
    ev["disparityConsistency"] = 1.0f;   // not computable from single frame
    ev["temporalStability"]    = 1.0f;   // not computable from single frame
    ev["regionSize"]           = (float)(box.width * box.height);
    ev["regionAspectRatio"]    = box.height > 0 ? (float)box.width / (float)box.height : 1.0f;
    ev["regionPosX"]           = posX;
    ev["regionPosY"]           = posY;
    ev["meanDiff"]             = meanDiff;
    ev["bloomRatio"]           = bloomRatio;
    ev["shadowRatio"]          = shadowRatio;
    ev["leftContentDensity"]   = leftContentDensity;
    ev["rightContentDensity"]  = rightContentDensity;
    ev["hasColor"]             = colorDiff > 0.02f;
    ev["nearBorder"]           = (posX < 0.05f || posX > 0.95f || posY < 0.05f || posY > 0.95f);
    ev["nearCenter"]           = (std::abs(posX - 0.5f) < 0.15f && std::abs(posY - 0.5f) < 0.15f);

    // Clamp NaN/Inf values to safe defaults
    for (auto it = ev.begin(); it != ev.end(); ++it) {
        if (it->is_number_float()) {
            float v = *it;
            if (std::isnan(v) || std::isinf(v)) *it = 0.0f;
        }
    }
    return ev;
}


// ---- Main ----

struct DefectApplier {
    DefectType type;
    cv::Rect (*apply)(cv::Mat&, int, int);
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <source_image> <output_dir> [--samples N] [--size WxH]\n";
        std::cerr << "  If source_image is 'testpattern', generates a synthetic test pattern.\n";
        return 1;
    }

    std::string srcPath = argv[1];
    std::string outDir = argv[2];
    int numSamples = 50;
    int targetW = 0, targetH = 0;

    for (int i = 3; i < argc; i++) {
        if (std::strcmp(argv[i], "--samples") == 0 && i + 1 < argc)
            numSamples = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            char dims[64];
            std::strncpy(dims, argv[++i], sizeof(dims) - 1);
            char* sep = std::strchr(dims, 'x');
            if (sep) {
                *sep = '\0';
                targetW = std::stoi(dims);
                targetH = std::stoi(sep + 1);
            }
        }
    }

    cv::Mat source;
    if (srcPath == "testpattern") {
        int w = targetW > 0 ? targetW : 1920;
        int h = targetH > 0 ? targetH : 1080;
        source = cv::Mat(h, w, CV_8UC3, cv::Scalar(60, 60, 70));
        for (int y = 0; y < h; y += 40)
            cv::line(source, cv::Point(0, y), cv::Point(w, y), cv::Scalar(80, 80, 90), 1);
        for (int x = 0; x < w; x += 40)
            cv::line(source, cv::Point(x, 0), cv::Point(x, h), cv::Scalar(80, 80, 90), 1);
        cv::circle(source, cv::Point(w / 4, h / 3), 100, cv::Scalar(200, 100, 50), -1);
        cv::circle(source, cv::Point(3 * w / 4, h / 3), 120, cv::Scalar(50, 150, 200), -1);
        cv::circle(source, cv::Point(w / 2, 2 * h / 3), 150, cv::Scalar(100, 200, 100), -1);
        cv::putText(source, "STEREO TEST", cv::Point(w / 3, h / 2),
                    cv::FONT_HERSHEY_SIMPLEX, 2.0, cv::Scalar(220, 220, 220), 3);
        for (int i = 0; i < 50; i++)
            cv::rectangle(source, cv::Rect(randInt(0, w - 5), randInt(0, h - 5), 4, 4),
                          cv::Scalar::all(randInt(100, 255)), -1);
    } else {
        source = cv::imread(srcPath);
        if (source.empty()) {
            std::cerr << "Failed to load image: " << srcPath << "\n";
            return 1;
        }
    }

    if (targetW > 0 && targetH > 0 && (source.cols != targetW || source.rows != targetH))
        cv::resize(source, source, cv::Size(targetW, targetH), 0, 0, cv::INTER_AREA);

    fs::create_directories(outDir);
    std::string jsonlPath = (fs::path(outDir) / "dataset.jsonl").string();
    std::ofstream jsonl(jsonlPath);
    if (!jsonl.is_open()) {
        std::cerr << "Failed to open: " << jsonlPath << "\n";
        return 1;
    }

    std::vector<DefectApplier> appliers = {
        {DefectType::LightingDifference,    doLightingDiff},
        {DefectType::ShadowDifference,      doShadowDiff},
        {DefectType::BloomDifference,       doBloomDiff},
        {DefectType::ReflectionDifference,  doReflectionDiff},
        {DefectType::TextureDifference,     doTextureDiff},
        {DefectType::MaterialDifference,    doMaterialDiff},
        {DefectType::TransparencyDifference,doTransparencyDiff},
        {DefectType::EdgeDifference,        doEdgeDiff},
        {DefectType::MissingGeometry,       doMissingGeometry},
        {DefectType::ExtraGeometry,         doExtraGeometry},
        {DefectType::MissingObject,         doMissingObject},
        {DefectType::MissingParticle,       doMissingParticle},
        {DefectType::MissingUI,             doMissingUI},
        {DefectType::TextDifference,        doTextDiff},
        {DefectType::StereoOffset,          doStereoOffset},
        {DefectType::OcclusionDifference,   doOcclusionDiff},
        {DefectType::PostProcessDifference, doPostProcessDiff},
        {DefectType::LensBoundary,          doLensBoundary},
    };

    std::cout << "Generating " << numSamples << " synthetic samples to " << outDir << "\n";

    for (int sample = 1; sample <= numSamples; sample++) {
        int idx = randInt(0, (int)appliers.size() - 1);
        auto& applier = appliers[idx];

        // Create left (clean, shifted) and right (defected) views
        int shift = randInt(5, 30) * (randInt(0, 1) == 0 ? 1 : -1);
        cv::Mat M = (cv::Mat_<double>(2, 3) << 1, 0, (double)shift, 0, 1, 0);
        cv::Mat left;
        cv::warpAffine(source, left, M, source.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        cv::Mat right = source.clone();

        cv::Rect box = applier.apply(right, source.cols, source.rows);
        box &= cv::Rect(0, 0, source.cols, source.rows);
        if (box.width < 5 || box.height < 5)
            box = cv::Rect(source.cols / 4, source.rows / 4, source.cols / 2, source.rows / 2);

        std::ostringstream stem;
        stem << "frame_" << std::setw(4) << std::setfill('0') << sample;
        std::string leftPath = stem.str() + "_left.png";
        std::string rightPath = stem.str() + "_right.png";

        cv::imwrite((fs::path(outDir) / leftPath).string(), left);
        cv::imwrite((fs::path(outDir) / rightPath).string(), right);

        cv::Mat diff;
        cv::absdiff(left(box), right(box), diff);
        cv::Mat grayDiff;
        cv::cvtColor(diff, grayDiff, cv::COLOR_BGR2GRAY);
        float severity = std::min(1.0f, (float)cv::mean(grayDiff)[0] / 128.0f);

        cv::Mat leftGray, rightGray;
        cv::cvtColor(left, leftGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(right, rightGray, cv::COLOR_BGR2GRAY);
        cv::Scalar meanLumL, stdLumL, meanLumR, stdLumR;
        cv::meanStdDev(leftGray, meanLumL, stdLumL);
        cv::meanStdDev(rightGray, meanLumR, stdLumR);

        json j;
        j["frame"] = sample;
        j["frameNumber"] = sample;
        j["left"] = leftPath;
        j["right"] = rightPath;

        json evidence = computeEvidence(left, right, box, source.cols, source.rows);

        json issue;
        issue["type"] = defectName(applier.type);
        issue["groundTruth"] = defectName(applier.type);
        issue["confidence"] = 1.0f;
        issue["severity"] = severity;
        issue["areaPixels"] = box.width * box.height;
        issue["boundingBox"] = {{"x", box.x}, {"y", box.y},
                                {"width", box.width}, {"height", box.height}};
        issue["evidence"] = evidence;
        j["mean_lum_l"] = (float)meanLumL[0] / 255.0f;
        j["mean_lum_r"] = (float)meanLumR[0] / 255.0f;
        j["std_lum_l"]  = (float)stdLumL[0] / 255.0f;
        j["std_lum_r"]  = (float)stdLumR[0] / 255.0f;
        j["issues"] = json::array({issue});

        jsonl << j.dump() << "\n";

        if (sample % 10 == 0)
            std::cout << "  Generated " << sample << "/" << numSamples << "\n";
    }

    std::cout << "Done. " << numSamples << " samples written to " << outDir << "\n";
    return 0;
}
