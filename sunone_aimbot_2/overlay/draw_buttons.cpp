#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <string>
#include <vector>

#include "overlay/i18n_imgui.h"
#include "imgui/imgui.h"
#include "sunone_aimbot_2.h"
#include "overlay.h"
#include "overlay/config_dirty.h"
#include "overlay/ui_sections.h"

namespace
{
int findKeyIndexByName(const std::string& keyName)
{
    for (size_t k = 0; k < key_names.size(); ++k)
    {
        if (key_names[k] == keyName)
            return static_cast<int>(k);
    }
    return 0;
}

bool drawButtonBindingRows(const char* rowLabel, std::vector<std::string>& bindings, bool keepAtLeastOne)
{
    if (key_names_cstrs.empty())
    {
        OverlayI18n::TextDisabled("No key list available.");
        return false;
    }

    bool changed = false;
    if (bindings.empty() && keepAtLeastOne)
    {
        bindings.push_back("None");
        changed = true;
    }

    for (size_t i = 0; i < bindings.size();)
    {
        std::string& currentKeyName = bindings[i];
        int currentIndex = findKeyIndexByName(currentKeyName);
        const std::string indexedLabel = (bindings.size() > 1)
            ? std::string(rowLabel) + " " + std::to_string(i + 1)
            : std::string(rowLabel);

        ImGui::PushID(static_cast<int>(i));

        const auto row = OverlayUI::BeginSettingRow(indexedLabel.c_str());
        const float actionBtnW = ImGui::GetFrameHeight();
        float comboWidth = row.controlWidth - (actionBtnW * 2.0f + 7.0f);
        if (comboWidth < 1.0f)
            comboWidth = 1.0f;
        ImGui::SetNextItemWidth(comboWidth);

        if (OverlayI18n::Combo("##value", &currentIndex, key_names_cstrs.data(), static_cast<int>(key_names_cstrs.size())))
        {
            currentKeyName = key_names[currentIndex];
            changed = true;
        }

        ImGui::SameLine(0.0f, 4.0f);
        if (OverlayI18n::Button("+", ImVec2(actionBtnW, 0.0f)))
        {
            bindings.insert(bindings.begin() + static_cast<std::vector<std::string>::difference_type>(i + 1), "None");
            changed = true;
        }

        ImGui::SameLine(0.0f, 3.0f);
        bool removedCurrent = false;
        if (OverlayI18n::Button("-", ImVec2(actionBtnW, 0.0f)))
        {
            if (bindings.size() <= 1 && keepAtLeastOne)
            {
                bindings[0] = "None";
            }
            else
            {
                bindings.erase(bindings.begin() + static_cast<std::vector<std::string>::difference_type>(i));
                removedCurrent = true;
            }
            changed = true;
        }

        OverlayUI::EndSettingRow(row);
        ImGui::PopID();

        if (removedCurrent)
            continue;

        ++i;
    }

    return changed;
}

void drawBindingRowsAndMarkDirty(const char* rowLabel, std::vector<std::string>& bindings, bool keepAtLeastOne = true)
{
    if (drawButtonBindingRows(rowLabel, bindings, keepAtLeastOne))
        OverlayConfig_MarkDirty();
}
}

void draw_buttons()
{
    if (OverlayUI::BeginSection("Hotkeys", "buttons_section_hotkeys"))
    {
        drawBindingRowsAndMarkDirty("Enemy targeting", config.button_targeting);
        drawBindingRowsAndMarkDirty("Friendly targeting", config.button_targeting_friendly);
        OverlayI18n::TextWrapped("Hold the corresponding key to aim at that team's assigned body/head classes. Holding both pauses aiming.");
        OverlayI18n::TextWrapped("Assign enemy and friendly classes in AI Model. Ignored classes are never targeted.");
        if (config.auto_aim)
            OverlayI18n::TextWrapped("Auto Aim targets enemies without a key. Hold friendly targeting to override; release to return to enemies.");
        bool overlap = false;
        for (const auto& enemyKey : config.button_targeting)
            for (const auto& friendlyKey : config.button_targeting_friendly)
                if (enemyKey != "None" && !enemyKey.empty() && enemyKey == friendlyKey) overlap = true;
        if (overlap)
            OverlayI18n::TextWrapped("Enemy and friendly bindings share a key. That key will pause aiming; choose different keys.");
        OverlayI18n::Text("Current aim team: %s", config.aim_team == AimTeam::Friendly ? "Friendly" :
            config.aim_team == AimTeam::Enemy ? "Enemy" : "Inactive");
        drawBindingRowsAndMarkDirty("Shoot", config.button_shoot);
        drawBindingRowsAndMarkDirty("Zoom", config.button_zoom);
        drawBindingRowsAndMarkDirty("Exit", config.button_exit);
        drawBindingRowsAndMarkDirty("Pause", config.button_pause);
        drawBindingRowsAndMarkDirty("Reload Config", config.button_reload_config);
        drawBindingRowsAndMarkDirty("Open Overlay", config.button_open_overlay);

        const auto row = OverlayUI::BeginSettingRow("Arrow Key Options");
        if (OverlayI18n::Checkbox("##value", &config.enable_arrows_settings))
            OverlayConfig_MarkDirty();
        OverlayUI::EndSettingRow(row);
        OverlayUI::EndSection();
    }
}
