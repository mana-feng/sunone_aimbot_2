#pragma once

#include "class_roles.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Sidecars are optional for unambiguous, decoded YOLO tensors. Raw heads must
// specify their decoding contract; output shape alone cannot identify anchors.
struct ModelProfile
{
    std::string layout;
    std::string output;
    std::string names;
    int classes = 0;
    int objectness = -1;
    int player = -2; // -2: preserve user configuration, -1: absent/disabled
    int head = -2;
    std::vector<ModelClassRole> classRoles; // Empty only for legacy sidecars.
    int stride = 32;
    float inputScale = 1.0f / 255.0f;
    std::string colorOrder = "rgb";
    std::vector<int> strides{8, 16, 32};
    std::map<std::string, std::vector<float>> anchors;
};

inline std::string modelTrim(std::string s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

inline std::vector<float> modelNumbers(std::string s)
{
    std::replace(s.begin(), s.end(), ',', ' ');
    std::istringstream stream(s);
    std::vector<float> values;
    float value;
    while (stream >> value)
    {
        if (!std::isfinite(value) || value <= 0) throw std::runtime_error("Invalid model profile number");
        values.push_back(value);
    }
    if (!stream.eof() || values.empty()) throw std::runtime_error("Invalid model profile number list");
    return values;
}

inline int modelInteger(const std::string& s)
{
    size_t end = 0;
    const int value = std::stoi(s, &end);
    if (end != s.size()) throw std::runtime_error("Invalid model profile integer");
    return value;
}

inline ModelProfile loadModelProfile(const std::filesystem::path& model)
{
    ModelProfile p;
    auto path = model;
    path += L".ini";
    if (!std::filesystem::exists(path)) return p;
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot read model sidecar");
    std::string line;
    while (std::getline(stream, line))
    {
        line = modelTrim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) throw std::runtime_error("Invalid model sidecar line");
        const auto key = modelTrim(line.substr(0, pos));
        const auto value = modelTrim(line.substr(pos + 1));
        if (key == "layout") p.layout = value;
        else if (key == "output") p.output = value;
        else if (key == "names") p.names = value;
        else if (key == "classes") p.classes = modelInteger(value);
        else if (key == "objectness") p.objectness = modelInteger(value);
        else if (key == "class_player") p.player = modelInteger(value);
        else if (key == "class_head") p.head = modelInteger(value);
        else if (key == "class_roles")
        {
            if (value.empty() || value.back() == ',') throw std::runtime_error("Empty model class role");
            p.classRoles.clear();
            std::istringstream roles(value);
            std::string token;
            while (std::getline(roles, token, ','))
            {
                p.classRoles.push_back(parseModelClassRole(modelTrim(token)));
                if (p.classRoles.size() > 10000) throw std::runtime_error("Too many model class roles");
            }
        }
        else if (key == "stride") p.stride = modelInteger(value);
        else if (key == "color_order") p.colorOrder = value;
        else if (key == "input_scale")
        {
            const auto values = modelNumbers(value);
            if (values.size() != 1 || values[0] > 255) throw std::runtime_error("Invalid input scale");
            p.inputScale = values[0];
        }
        else if (key == "strides")
        {
            p.strides.clear();
            for (float n : modelNumbers(value))
            {
                if (n > 1024 || std::floor(n) != n) throw std::runtime_error("Invalid model stride");
                p.strides.push_back(static_cast<int>(n));
            }
        }
        else if (key.rfind("anchors.", 0) == 0) p.anchors[key.substr(8)] = modelNumbers(value);
        else if (key != "source_sha256")
            throw std::runtime_error("Unknown model sidecar key: " + key);
    }
    if (p.classes < 0 || p.classes > 10000 || p.objectness < -1 || p.objectness > 1 ||
        p.player < -2 || p.head < -2 || p.stride < 1 || p.stride > 1024)
        throw std::runtime_error("Model sidecar value out of range");
    if (!p.classRoles.empty() && p.classes > 0) validateModelClassRoles(p.classRoles, p.classes);
    if (!p.layout.empty() && p.layout != "feature" && p.layout != "prediction" &&
        p.layout != "xyxy" && p.layout != "yolov5_raw" && p.layout != "yolox_raw")
        throw std::runtime_error("Unknown model layout: " + p.layout);
    if (p.colorOrder != "rgb" && p.colorOrder != "bgr") throw std::runtime_error("Invalid model color order");
    return p;
}

inline float modelSigmoid(float value)
{
    if (value >= 0) return 1.0f / (1.0f + std::exp(-value));
    const float e = std::exp(value);
    return e / (1.0f + e);
}

// [anchors, H, W, 5+C] logits -> [N, 5+C] pixel-space predictions.
inline std::vector<float> decodeV5Head(const float* src, int anchors, int h, int w,
    int features, int inputH, int inputW, const std::vector<float>& anchorPixels)
{
    if (!src || anchors <= 0 || h <= 0 || w <= 0 || features < 6 ||
        inputH % h || inputW % w || anchorPixels.size() != static_cast<size_t>(anchors) * 2)
        throw std::runtime_error("Invalid YOLOv5 raw head or missing anchors");
    const float sx = static_cast<float>(inputW) / w;
    const float sy = static_cast<float>(inputH) / h;
    const size_t count = static_cast<size_t>(anchors) * h * w * features;
    std::vector<float> result(count);
    for (int a = 0; a < anchors; ++a)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const size_t offset = ((static_cast<size_t>(a) * h + y) * w + x) * features;
                float* out = result.data() + offset;
                for (int c = 0; c < features; ++c) out[c] = modelSigmoid(src[offset + c]);
                out[0] = (out[0] * 2 - 0.5f + x) * sx;
                out[1] = (out[1] * 2 - 0.5f + y) * sy;
                out[2] = out[2] * out[2] * 4 * anchorPixels[a * 2];
                out[3] = out[3] * out[3] * 4 * anchorPixels[a * 2 + 1];
            }
    return result;
}

// YOLOX export without decode_in_inference: box logits + sigmoid probabilities.
inline std::vector<float> decodeYoloX(const float* src, int rows, int features,
    int inputH, int inputW, const std::vector<int>& strides)
{
    if (!src || rows <= 0 || features < 6) throw std::runtime_error("Invalid YOLOX output");
    size_t expected = 0;
    for (int s : strides)
    {
        if (s <= 0 || inputH % s || inputW % s) throw std::runtime_error("Invalid YOLOX stride/input size");
        expected += static_cast<size_t>(inputH / s) * (inputW / s);
    }
    if (expected != static_cast<size_t>(rows)) throw std::runtime_error("YOLOX grid count does not match output");
    std::vector<float> result(src, src + static_cast<size_t>(rows) * features);
    size_t row = 0;
    for (int s : strides)
        for (int y = 0; y < inputH / s; ++y)
            for (int x = 0; x < inputW / s; ++x, ++row)
            {
                float* out = result.data() + row * features;
                out[0] = (out[0] + x) * s;
                out[1] = (out[1] + y) * s;
                out[2] = std::exp(out[2]) * s;
                out[3] = std::exp(out[3]) * s;
            }
    return result;
}
