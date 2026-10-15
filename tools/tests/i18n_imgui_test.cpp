#include "overlay/i18n_imgui.h"
#include "imgui/imgui_internal.h"
#include <iostream>
#include <stdexcept>
#include <cstring>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main()
{
    ImGui::CreateContext();
    try
    {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1200, 900);
        io.DeltaTime = 1.0f / 60;
        io.Fonts->AddFontDefault();
        unsigned char* pixels = nullptr;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(900, 800));
        ImGui::Begin("Localization test");
        OverlayI18n::SetLanguage("en");
        const auto englishId = ImGui::GetID(OverlayI18n::Label("Apply and save for this model").c_str());
        OverlayI18n::SetLanguage("zh-CN");
        check(englishId == ImGui::GetID(OverlayI18n::Label("Apply and save for this model").c_str()),
            "Actual ImGui ID changes when switching languages");
        ImGui::LogToBuffer();
        OverlayI18n::Text("Status: %s", "ready");
        OverlayI18n::TextWrappedRaw("%s: %s", "names", "head");
        OverlayI18n::TextColored(ImVec4(1,1,1,1), "Active Tracks: %d", 3);
        OverlayI18n::TextWrapped("%s", "Depth engine exported: C:/models/test.engine");
        const std::string log = ImGui::GetCurrentContext()->LogBuffer.c_str();
        check(log.find(u8"状态：就绪") != std::string::npos, "Formatted status arguments must translate");
        check(log.find("names: head") != std::string::npos, "Original ONNX metadata was translated");
        check(log.find(u8"活跃轨迹：3") != std::string::npos, "Numeric formatting broken");
        check(log.find(u8"深度引擎已导出：C:/models/test.engine") != std::string::npos, "Dynamic path message broken");
        if (ImGui::BeginCombo("##raw_preview", nullptr, ImGuiComboFlags_CustomPreview)) ImGui::EndCombo();
        OverlayI18n::DrawRawComboPreview("0 : enemy##red");
        check(std::string(ImGui::GetCurrentContext()->LogBuffer.c_str()).find("0 : enemy##red") != std::string::npos,
            "Raw class preview must preserve ## in model labels");
        ImGui::LogFinish();
        int selected = 0;
        const char* options[] = {"monitor", "window"};
        OverlayI18n::Combo("Capture", &selected, options, 2);
        OverlayI18n::Combo("Capture2", &selected, "monitor\0window\0");
        OverlayI18n::Combo("Capture3", &selected,
            [](void* data, int index)->const char* { return static_cast<const char**>(data)[index]; }, options, 2);
        check(selected == 0, "Rendering translated options changed selection without input");
        ImGui::End();
        ImGui::Render();
        check(ImGui::GetDrawData()->Valid, "Headless Overlay rendering failed");
        ImGui::DestroyContext();
        std::cout << "ImGui localization rendering and stable-ID tests passed\n";
    }
    catch (const std::exception& e)
    {
        ImGui::DestroyContext();
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
