#pragma once
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "i18n.h"
#include <tuple>
#include <utility>
#include <vector>

namespace OverlayI18n
{
template<class T> inline T TranslateArg(T value) { return value; }
inline std::string TranslateArg(const char* value) { return Display(value); }
inline std::string TranslateArg(char* value) { return Display(value); }
template<class T> inline T PrintArg(const T& value) { return value; }
inline const char* PrintArg(const std::string& value) { return value.c_str(); }

#define OVERLAY_TRANSLATED_TEXT(Name) \
template<class... Args> inline void Name(const char* format, Args... args) { \
    auto values = std::make_tuple(TranslateArg(args)...); \
    const auto translated = Display(format); \
    std::apply([&](const auto&... items) { ImGui::Name(translated.c_str(), PrintArg(items)...); }, values); \
}
OVERLAY_TRANSLATED_TEXT(Text)
OVERLAY_TRANSLATED_TEXT(TextWrapped)
OVERLAY_TRANSLATED_TEXT(TextDisabled)
#undef OVERLAY_TRANSLATED_TEXT

template<class... Args> inline void TextWrappedRaw(const char* format, Args... args)
{ ImGui::TextWrapped(T(format), args...); }
template<class... Args> inline void TextColored(const ImVec4& color, const char* format, Args... args)
{
    auto values = std::make_tuple(TranslateArg(args)...);
    const auto translated = Display(format);
    std::apply([&](const auto&... items) { ImGui::TextColored(color, translated.c_str(), PrintArg(items)...); }, values);
}
inline void TextUnformatted(const char* text, const char* end = nullptr)
{
    const std::string original = end ? std::string(text, end) : std::string(text ? text : "");
    const auto translated = Display(original.c_str());
    ImGui::TextUnformatted(translated.c_str());
}

// ImGui's ### suffix retains the original hash when visible labels change.
#define OVERLAY_TRANSLATED_WIDGET(Name) \
template<class... Args> inline auto Name(const char* label, Args&&... args) { \
    const auto translated = Label(label); \
    return ImGui::Name(translated.c_str(), std::forward<Args>(args)...); \
}
OVERLAY_TRANSLATED_WIDGET(Button)
OVERLAY_TRANSLATED_WIDGET(Checkbox)
OVERLAY_TRANSLATED_WIDGET(Selectable)
OVERLAY_TRANSLATED_WIDGET(TreeNode)
OVERLAY_TRANSLATED_WIDGET(TableSetupColumn)
OVERLAY_TRANSLATED_WIDGET(InputInt)
OVERLAY_TRANSLATED_WIDGET(InputText)
OVERLAY_TRANSLATED_WIDGET(SliderInt)
OVERLAY_TRANSLATED_WIDGET(SliderFloat)
OVERLAY_TRANSLATED_WIDGET(PlotLines)
#undef OVERLAY_TRANSLATED_WIDGET

inline bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags flags = 0)
{
    const auto visible = Display(preview);
    return ImGui::BeginCombo(Label(label).c_str(), visible.c_str(), flags);
}
inline bool Combo(const char* label, int* current, const char* const items[], int count, int height = -1)
{
    std::vector<std::string> text;
    text.reserve(count);
    for (int i = 0; i < count; ++i) text.push_back(Display(items[i]));
    std::vector<const char*> pointers;
    for (const auto& item : text) pointers.push_back(item.c_str());
    return ImGui::Combo(Label(label).c_str(), current, pointers.data(), count, height);
}
inline bool Combo(const char* label, int* current, const char* items, int height = -1)
{
    std::vector<const char*> list;
    for (const char* item = items; *item; item += std::char_traits<char>::length(item) + 1) list.push_back(item);
    return Combo(label, current, list.data(), static_cast<int>(list.size()), height);
}
inline bool Combo(const char* label, int* current, const char* (*getter)(void*, int), void* data, int count, int height = -1)
{
    std::vector<std::string> values;
    values.reserve(count);
    for (int i = 0; i < count; ++i) { const char* value = getter(data, i); values.emplace_back(value ? value : ""); }
    std::vector<const char*> pointers;
    for (const auto& value : values) pointers.push_back(value.c_str());
    return Combo(label, current, pointers.data(), count, height);
}

inline void DrawRawComboPreview(const char* value)
{
    if (ImGui::BeginComboPreview())
    {
        ImGui::TextUnformatted(value);
        ImGui::EndComboPreview();
    }
}

}
