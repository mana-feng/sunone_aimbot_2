#pragma once
#include <atomic>
#include <string>
#include <string_view>
#include <unordered_map>
#include "i18n_catalog.h"

namespace OverlayI18n
{
inline std::atomic<bool> chinese{false};
inline void SetLanguage(const std::string& language) { chinese.store(language == "zh-CN", std::memory_order_relaxed); }
inline const char* T(const char* text)
{
    if (!text || !chinese.load(std::memory_order_relaxed)) return text;
    const auto& catalog = Catalog();
    const auto found = catalog.find(text);
    return found == catalog.end() ? text : found->second.c_str();
}

// Dynamic messages can contain a translated prefix and an untouched filename.
// Original external metadata is displayed through the Raw text helpers instead.
inline std::string Display(const char* text)
{
    if (!text) return {};
    const char* translated = T(text);
    if (translated != text || !chinese.load(std::memory_order_relaxed)) return translated;
    const std::string value(text);
    for (const char* base : {"Screenshot", "Targeting", "Enemy targeting", "Friendly targeting", "Shoot", "Zoom", "Exit", "Pause", "Reload Config", "Open Overlay", "Monitor"})
    {
        const std::string prefix = std::string(base) + " ";
        if (value.compare(0, prefix.size(), prefix) == 0 && value.size() > prefix.size() &&
            value.find_first_not_of("0123456789", prefix.size()) == std::string::npos)
            return std::string(T(base)) + " " + value.substr(prefix.size());
    }
    size_t best = 0;
    const std::string* prefix = nullptr;
    for (const auto& item : Catalog())
    {
        const auto& key = item.first;
        if (key.size() > best && key.size() > 2 && key.compare(key.size() - 2, 2, ": ") == 0 &&
            value.compare(0, key.size(), key) == 0)
        {
            best = key.size();
            prefix = &item.second;
        }
    }
    return prefix ? *prefix + Display(value.c_str() + best) : value;
}

inline std::string Label(const char* text)
{
    const std::string original(text ? text : "");
    if (original.rfind("##", 0) == 0) return original;
    const auto hidden = original.find("##");
    const auto visible = original.substr(0, hidden);
    const auto translated = Display(visible.c_str());
    if (translated == visible) return original;
    const auto stable = original.find("###");
    return translated + (stable == std::string::npos ? "###" + original : original.substr(stable));
}
}
