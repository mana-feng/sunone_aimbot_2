#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include "overlay/i18n_imgui.h"
#include "imgui/imgui.h"

#include "sunone_aimbot_2.h"
#include "include/other_tools.h"
#include "overlay.h"
#include "overlay/config_dirty.h"
#include "draw_settings.h"
#include "overlay/ui_sections.h"
#ifdef USE_CUDA
#include "overlay/export_progress_panel.h"
#include "trt_monitor.h"
#endif

std::string prev_backend = config.backend;
float prev_confidence_threshold = config.confidence_threshold;
float prev_nms_threshold = config.nms_threshold;
int prev_max_detections = config.max_detections;

static bool wasExporting = false;
static bool ai_state_initialized = false;

#ifndef USE_CUDA
namespace
{
std::string modelClassLabel(int id, const DetectorModelInfo& info)
{
    if (id < 0) return OverlayI18n::T("Disabled");
    auto name = info.classNames.find(id);
    return std::to_string(id) + " : " + (name == info.classNames.end() ? OverlayI18n::T("Unnamed class") : name->second);
}

// draw_ai is called while configMutex is held by the Overlay render loop.
void drawModelDetails()
{
    if (!dml_detector)
    {
        OverlayI18n::TextWrapped("Waiting for the ONNX detector to initialize.");
        return;
    }
    const auto info = dml_detector->getModelInfo();
    const bool loading = detector_model_changed.load();
    const bool current = !loading && dml_detector->isReady() && info.modelName == config.ai_model;
    if (loading) OverlayI18n::TextWrapped("Loading selected model... Class selection is temporarily disabled.");
    if (!info.loadError.empty() && info.failedModel == config.ai_model)
        OverlayI18n::TextWrapped("Model load failed: %s", info.loadError.c_str());
    if (info.modelName.empty()) return;
    if (info.modelName != config.ai_model)
        OverlayI18n::TextWrappedRaw("Selected model is not active. Information below belongs to: %s", info.modelName.c_str());

    if (OverlayUI::BeginSection("ONNX information", "ai_section_onnx_info"))
    {
        OverlayI18n::TextUnformatted("ONNX information");
        OverlayI18n::TextWrappedRaw("Loaded model: %s", info.modelName.c_str());
        OverlayI18n::TextWrapped("Provider: %s", info.provider.c_str());
        OverlayI18n::TextWrappedRaw("Input: %s", info.input.c_str());
        for (const auto& output : info.outputs) OverlayI18n::TextWrappedRaw("Output: %s", output.c_str());
        OverlayI18n::Text("Detection classes: %d", info.classes);
        OverlayI18n::TextWrapped("Class names source: %s", info.namesSource.c_str());
        if (OverlayI18n::TreeNode("All model classes"))
        {
            ImGui::BeginChild("class_list", ImVec2(0, 140), ImGuiChildFlags_Borders);
            ImGuiListClipper clipper;
            clipper.Begin(info.classes);
            while (clipper.Step())
                for (int id = clipper.DisplayStart; id < clipper.DisplayEnd; ++id)
                    ImGui::TextUnformatted(modelClassLabel(id, info).c_str());
            ImGui::EndChild();
            ImGui::TreePop();
        }
        if (OverlayI18n::TreeNode("ONNX custom metadata (read only)"))
        {
            if (info.metadata.empty()) OverlayI18n::TextUnformatted("No custom metadata stored in this model.");
            for (const auto& item : info.metadata)
                OverlayI18n::TextWrappedRaw("%s: %s", item.first.c_str(), item.second.c_str());
            ImGui::TreePop();
        }
        OverlayI18n::TextWrapped("Decoder settings: %s; objectness: %s", info.layout.c_str(), info.objectness ? "yes" : "no");
        OverlayI18n::TextWrapped("Preprocessing settings: %s; pixel scale %.8g", info.colorOrder.c_str(), info.inputScale);
        OverlayI18n::TextWrapped("Decoder/preprocessing settings come from the app or sidecar; they are not training parameters stored in ONNX.");
        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("Target classes", "ai_section_target_classes"))
    {
        OverlayI18n::TextUnformatted("Target classes");
        static std::string draftModel, saveMessage;
        static std::vector<ModelClassRole> draftRoles;
        static uint64_t observedRevision = 0;
        if (draftModel != info.modelName || observedRevision != config.model_class_revision)
        {
            draftModel = info.modelName;
            observedRevision = config.model_class_revision;
            draftRoles = config.model_class_roles;
            saveMessage.clear();
        }
        auto policy = config.targetClassPolicy();
        bool anyEnabled = false;
        for (auto team : {AimTeam::Enemy, AimTeam::Friendly})
        {
            policy.team = team;
            for (int id = 0; id < info.classes; ++id) anyEnabled |= policy.eligible(id, config.disable_headshot);
        }
        if (current && !anyEnabled)
            OverlayI18n::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "No target class enabled. Detections cannot trigger aiming.");
        if (config.disable_headshot)
            OverlayI18n::TextWrapped("Head targeting is disabled in the Target tab. A head selection alone will not trigger aiming.");
        OverlayI18n::TextWrapped("Assign each class manually. Names never determine friend or enemy. Multiple classes can share a role.");
        OverlayI18n::TextWrapped("Enemy and friendly hotkeys target their respective body/head roles. Ignored classes are never targeted.");
        OverlayI18n::TextWrapped("Selections take effect only after Apply and save for this model.");
        const char* roleLabels[] = { "Ignore", "Enemy body", "Enemy head", "Friendly body", "Friendly head" };
        ImGui::BeginDisabled(!current);
        const bool valid = draftRoles.size() == static_cast<size_t>(info.classes) && info.classes > 0;
        if (valid)
        {
            ImGui::BeginChild("class_role_list", ImVec2(0, 240), ImGuiChildFlags_Borders);
            if (ImGui::BeginTable("class_roles", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg))
            {
                OverlayI18n::TableSetupColumn("Class ID / original name");
                OverlayI18n::TableSetupColumn("Role");
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;
                clipper.Begin(info.classes);
                while (clipper.Step())
                    for (int id = clipper.DisplayStart; id < clipper.DisplayEnd; ++id)
                    {
                        ImGui::PushID(id);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(modelClassLabel(id, info).c_str());
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(modelClassLabel(id, info).c_str());
                            ImGui::EndTooltip();
                        }
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-1);
                        int role = static_cast<int>(draftRoles[id]);
                        if (OverlayI18n::Combo("##role", &role, roleLabels, 5)) draftRoles[id] = static_cast<ModelClassRole>(role);
                        ImGui::PopID();
                    }
                ImGui::EndTable();
            }
            ImGui::EndChild();
        }
        ImGui::BeginDisabled(!valid);
        if (OverlayI18n::Button("Apply and save for this model"))
        {
            try
            {
                saveModelClassRoles(std::filesystem::u8path("models") / std::filesystem::u8path(info.modelName),
                    info.classes, draftRoles);
                config.model_class_roles = draftRoles;
                config.class_player = firstModelClassWithRole(draftRoles, ModelClassRole::EnemyBody);
                config.class_head = firstModelClassWithRole(draftRoles, ModelClassRole::EnemyHead);
                observedRevision = ++config.model_class_revision;
                OverlayConfig_MarkDirty();
                saveMessage = "Applied. Choices saved in this model's .onnx.ini; no model reload is needed.";
            }
            catch (const std::exception& error)
            {
                saveMessage = std::string("Not applied: ") + error.what();
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!saveMessage.empty()) OverlayI18n::TextWrapped("%s", saveMessage.c_str());
        OverlayUI::EndSection();
    }
}
}
#endif

void draw_ai()
{
#ifdef USE_CUDA
    config.backend = "TRT";
#else
    config.backend = "DML";
#endif

    if (!ai_state_initialized)
    {
        prev_backend = config.backend;
        prev_confidence_threshold = config.confidence_threshold;
        prev_nms_threshold = config.nms_threshold;
        prev_max_detections = config.max_detections;
        ai_state_initialized = true;
    }

#ifdef USE_CUDA
    if (gIsTrtExporting)
    {
        OverlayExportUI::DrawTensorRtExportPanel(
            "ai_tensor_rt_export",
            "TensorRT engine export",
            "Compiling optimized AI inference engine",
            config.ai_model.c_str(),
            "Cancel export");
    }
#endif
    std::vector<std::string> availableModels = getAvailableModels();
    if (OverlayUI::BeginSection("Model", "ai_section_model"))
    {
        if (availableModels.empty())
        {
            OverlayI18n::Text("No models available in the 'models' folder.");
        }
        else
        {
            int currentModelIndex = 0;
            auto it = std::find(availableModels.begin(), availableModels.end(), config.ai_model);

            if (it != availableModels.end())
            {
                currentModelIndex = static_cast<int>(std::distance(availableModels.begin(), it));
            }

            std::vector<const char*> modelsItems;
            modelsItems.reserve(availableModels.size());

            for (const auto& modelName : availableModels)
            {
                modelsItems.push_back(modelName.c_str());
            }

            {
                const auto row = OverlayUI::BeginSettingRow("Model");
                if (OverlayI18n::Combo("##model", &currentModelIndex, modelsItems.data(), static_cast<int>(modelsItems.size())))
                {
                    if (config.ai_model != availableModels[currentModelIndex])
                    {
                        config.ai_model = availableModels[currentModelIndex];
                        OverlayConfig_MarkDirty();
                        detector_model_changed.store(true);
                    }
                }
                OverlayUI::EndSettingRow(row);
            }

            OverlayUI::TextRow(config.fixed_input_size ? "Fixed model size: Enabled" : "Fixed model size: Disabled",
                IM_COL32(188, 188, 188, 255));
        }
        OverlayUI::EndSection();
    }

#ifndef USE_CUDA
    drawModelDetails();
#endif

    if (OverlayUI::BeginSection("Detection", "ai_section_detection"))
    {
        {
            const auto row = OverlayUI::BeginSettingRow("Confidence Threshold");
            OverlayI18n::SliderFloat("##confidence_threshold", &config.confidence_threshold, 0.01f, 1.00f, "%.2f");
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("NMS Threshold");
            OverlayI18n::SliderFloat("##nms_threshold", &config.nms_threshold, 0.00f, 1.00f, "%.2f");
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("Max Detections");
            OverlayI18n::SliderInt("##max_detections", &config.max_detections, 1, 100);
            OverlayUI::EndSettingRow(row);
        }
        OverlayUI::EndSection();
    }

    if (prev_confidence_threshold != config.confidence_threshold ||
        prev_nms_threshold != config.nms_threshold ||
        prev_max_detections != config.max_detections)
    {
        prev_nms_threshold = config.nms_threshold;
        prev_confidence_threshold = config.confidence_threshold;
        prev_max_detections = config.max_detections;
        OverlayConfig_MarkDirty();
    }

    if (prev_backend != config.backend)
    {
        prev_backend = config.backend;
        detector_model_changed.store(true);
        OverlayConfig_MarkDirty();
    }
}
