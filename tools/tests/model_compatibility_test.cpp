#include "model_profile.h"
#include "postProcess.h"
#include <iostream>
#include <stdexcept>
#include <chrono>

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    // A one-class prediction ending in a score of exactly 1 must not be
    // reinterpreted as an XYXY/class-ID record.
    const float oneClass[]{10, 20, 4, 6, 0.8f, 1.0f};
    auto detections = postProcessYoloDML(oneClass, {1, 6}, 1, 0.2f, 0.5f, 10,
        nullptr, YoloOutputLayout::PredictionMajor, true);
    require(detections.size() == 1 && detections[0].classId == 0, "one-class layout");
    require(detections[0].box == cv::Rect(8, 17, 4, 6), "one-class box decoding");
    require(std::abs(detections[0].confidence - 0.8f) < 1e-6f, "objectness multiplication");

    const float feature[]{10,20,4,6,0.1f,0.9f};
    detections = postProcessYoloDML(feature, {6,1}, 2, 0.2f, 0.5f, 10,
        nullptr, YoloOutputLayout::FeatureMajor, false);
    require(detections.size() == 1 && detections[0].classId == 1, "feature-major two-class output");

    const float v5[]{10,20,4,6,0.5f,0.1f,0.2f,0.8f,0.3f};
    detections = postProcessYoloDML(v5, {1,9}, 4, 0.2f, 0.5f, 10,
        nullptr, YoloOutputLayout::PredictionMajor, true);
    require(detections.size() == 1 && detections[0].classId == 2 &&
        std::abs(detections[0].confidence - 0.4f) < 1e-6f, "four-class YOLOv5");

    const float logits[7]{};
    auto raw = decodeV5Head(logits, 1, 1, 1, 7, 8, 8, {10, 20});
    require(raw[0] == 4 && raw[1] == 4 && raw[2] == 10 && raw[3] == 20 && raw[4] == 0.5f,
        "YOLOv5 grid, sigmoid and anchor decoding");
    bool rejected = false;
    try { decodeV5Head(logits, 1, 1, 1, 7, 8, 8, {}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "missing anchors must fail");

    std::vector<float> x(5 * 6, 0.0f);
    for (int i = 0; i < 5; ++i) { x[i*6+4] = 0.75f; x[i*6+5] = 0.8f; }
    auto decoded = decodeYoloX(x.data(), 5, 6, 16, 16, {8,16});
    require(decoded[6] == 8 && decoded[7] == 0 && decoded[8] == 8, "YOLOX grid order");
    require(decoded[24+2] == 16 && decoded[24+4] == 0.75f, "YOLOX stride and probability preservation");
    rejected = false;
    try { decodeYoloX(x.data(), 4, 6, 16, 16, {8,16}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "YOLOX row count must match grid");

    // Isolated fixtures: tests must not depend on the user's current model choices.
    const auto folder = std::filesystem::temp_directory_path() /
        ("sunone-model-contract-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    const auto bodyModel = folder / "body.onnx";
    auto bodySidecar = bodyModel; bodySidecar += L".ini";
    { std::ofstream out(bodySidecar); out << "layout=feature\nclasses=2\nclass_player=1\nclass_head=0\n"; }
    const auto profile = loadModelProfile(bodyModel);
    require(profile.layout == "feature" && profile.classes == 2 && profile.player == 1 && profile.head == 0,
        "per-model reversed class mapping");
    const auto unicodeModel = folder / std::filesystem::path(L"测试模型.onnx");
    auto unicodeSidecar = unicodeModel; unicodeSidecar += L".ini";
    { std::ofstream out(unicodeSidecar); out << "classes=2\nclass_player=-1\nclass_head=-1\n"; }
    const auto chinese = loadModelProfile(unicodeModel);
    require(chinese.classes == 2 && chinese.player == -1, "Unicode sidecar and unknown class semantics");
    std::filesystem::remove(bodySidecar);
    std::filesystem::remove(unicodeSidecar);
    std::filesystem::remove(folder);
    std::cout << "Model compatibility tests passed\n";
}
