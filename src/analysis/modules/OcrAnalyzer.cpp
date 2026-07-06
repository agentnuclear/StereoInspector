#include "OcrAnalyzer.h"
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>

#ifdef HAS_TESSERACT
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#endif

OcrAnalyzer::OcrAnalyzer() : BaseAnalyzer("OCR", false) {
#ifdef HAS_TESSERACT
    auto* api = new tesseract::TessBaseAPI();
    if (api->Init(nullptr, "eng") == 0) {
        m_tesseract = api;
        m_enabled = true;
        spdlog::info("Tesseract OCR initialized");
    } else {
        delete api;
        spdlog::warn("Failed to initialize Tesseract OCR");
    }
#endif
}

void OcrAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
#ifdef HAS_TESSERACT
    if (!m_tesseract) return;

    auto* api = static_cast<tesseract::TessBaseAPI*>(m_tesseract);

    auto doOcr = [api](const cv::Mat& img) -> std::string {
        cv::Mat gray;
        if (img.channels() == 3) {
            cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = img;
        }

        api->SetImage(gray.data, gray.cols, gray.rows, 1, (int)gray.step);
        char* outText = api->GetUTF8Text();
        std::string result(outText);
        delete[] outText;
        return result;
    };

    std::string textL = doOcr(leftEye);
    std::string textR = doOcr(rightEye);

    if (textL.empty() && textR.empty()) {
        result.ocrTextMismatches = 0;
        return;
    }

    auto normalize = [](std::string s) -> std::string {
        s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };

    std::string normL = normalize(textL);
    std::string normR = normalize(textR);

    if (normL.empty() && normR.empty()) {
        result.ocrTextMismatches = 0;
        return;
    }

    size_t maxLen = std::max(normL.length(), normR.length());
    if (maxLen == 0) {
        result.ocrTextMismatches = 0;
        return;
    }

    size_t dist = 0;
    size_t minLen = std::min(normL.length(), normR.length());
    for (size_t i = 0; i < minLen; i++) {
        if (normL[i] != normR[i]) dist++;
    }
    dist += (maxLen - minLen);

    result.ocrTextMismatches = (int)dist;
#else
    (void)leftEye;
    (void)rightEye;
    result.ocrTextMismatches = 0;
#endif
}
