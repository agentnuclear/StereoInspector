#include "SsimAnalyzer.h"
#include <opencv2/imgproc.hpp>

SsimAnalyzer::SsimAnalyzer() : BaseAnalyzer("SSIM", true) {}

double SsimAnalyzer::computeSSIM(const cv::Mat& img1, const cv::Mat& img2,
                                  cv::InputArray validMask) {
    const double C1 = 6.5025, C2 = 58.5225;

    cv::Mat img1f, img2f;
    img1.convertTo(img1f, CV_32F);
    img2.convertTo(img2f, CV_32F);

    cv::Mat mu1, mu2, mu1_sq, mu2_sq, mu1_mu2;
    cv::Mat sigma1_sq, sigma2_sq, sigma12;

    cv::GaussianBlur(img1f, mu1, cv::Size(11, 11), 1.5);
    cv::GaussianBlur(img2f, mu2, cv::Size(11, 11), 1.5);

    cv::multiply(mu1, mu1, mu1_sq);
    cv::multiply(mu2, mu2, mu2_sq);
    cv::multiply(mu1, mu2, mu1_mu2);

    cv::multiply(img1f, img1f, sigma1_sq);
    cv::GaussianBlur(sigma1_sq, sigma1_sq, cv::Size(11, 11), 1.5);
    sigma1_sq -= mu1_sq;

    cv::multiply(img2f, img2f, sigma2_sq);
    cv::GaussianBlur(sigma2_sq, sigma2_sq, cv::Size(11, 11), 1.5);
    sigma2_sq -= mu2_sq;

    cv::multiply(img1f, img2f, sigma12);
    cv::GaussianBlur(sigma12, sigma12, cv::Size(11, 11), 1.5);
    sigma12 -= mu1_mu2;

    cv::Mat num, den, t1, t2, t3, t4, ssim_map;
    t1 = 2.0 * mu1_mu2 + C1;
    t2 = 2.0 * sigma12 + C2;
    t3 = mu1_sq + mu2_sq + C1;
    t4 = sigma1_sq + sigma2_sq + C2;

    cv::multiply(t1, t2, num);
    cv::multiply(t3, t4, den);
    cv::divide(num, den, ssim_map);

    cv::Scalar mssim = cv::mean(ssim_map, validMask);
    return mssim[0];
}

void SsimAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    cv::Mat leftGray = (!result.leftGray.empty()) ? result.leftGray
        : (leftEye.channels() == 3 ? (cv::cvtColor(leftEye, leftGray, cv::COLOR_BGR2GRAY), leftGray) : leftEye.clone());
    cv::Mat rightGray = (!result.rightGray.empty()) ? result.rightGray
        : (rightEye.channels() == 3 ? (cv::cvtColor(rightEye, rightGray, cv::COLOR_BGR2GRAY), rightGray) : rightEye.clone());

    cv::Mat validMask;
    if (!result.disparity.validMap.empty()) {
        cv::resize(result.disparity.validMap, validMask, leftGray.size(), 0, 0, cv::INTER_NEAREST);
    }
    result.ssim = computeSSIM(leftGray, rightGray, validMask);
}
