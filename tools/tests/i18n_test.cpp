#include "overlay/i18n.h"
#include <iostream>
#include <stdexcept>
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main()
{
    try
    {
        OverlayI18n::SetLanguage("zh-CN");
        check(std::string(OverlayI18n::T("AI Model")) == u8"AI 模型", "Chinese menu translation is missing");
        check(OverlayI18n::Label("Apply and save for this model").find("###Apply and save for this model") != std::string::npos,
            "Language switch must preserve ImGui widget IDs");
        check(OverlayI18n::Label("##model") == "##model", "Hidden control IDs must never change");
        check(OverlayI18n::Label("Model###stable") == u8"模型###stable", "Explicit stable widget IDs must survive translation");
        check(std::string(OverlayI18n::T("tbody")) == "tbody", "Unknown model metadata must remain unchanged");
        check(OverlayI18n::Display("Depth engine exported: C:/models/test.engine") == u8"深度引擎已导出：C:/models/test.engine",
            "Dynamic messages must translate the prefix and preserve the path");
        check(std::string(OverlayI18n::T("Friendly head")) == u8"友方头部" &&
            std::string(OverlayI18n::T("Enemy body")) == u8"敌方身体", "Team/anatomy role translations missing");
        check(OverlayI18n::Display("Friendly targeting 2") == u8"友方瞄准 2" &&
            OverlayI18n::Display("Enemy targeting") == u8"敌方瞄准", "Aim hotkey labels must translate including numbered rows");
        OverlayI18n::SetLanguage("en");
        check(std::string(OverlayI18n::T("AI Model")) == "AI Model", "English must restore original text");
        check(OverlayI18n::Label("Apply and save for this model") == "Apply and save for this model", "English widget label changed");
        OverlayI18n::SetLanguage("invalid");
        check(std::string(OverlayI18n::T("AI Model")) == "AI Model", "Unknown language must safely fall back to English");
        std::cout << "Overlay localization tests passed\n";
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
