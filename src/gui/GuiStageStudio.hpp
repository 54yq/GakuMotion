#pragma once

#include "../GkGuiOverlay.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
    inline void DrawStageStudio(GuiOverlay& overlay, const GuiContext& ctx)
    {
        ImGui::PushID("stage_studio");
        if (!overlay.studioDraftReady || overlay.studioDraftRevision != ctx.studioRevision)
        {
            if (!overlay.studioDraftReady || overlay.studioLastSnapshot.lighting != ctx.studioSettings.lighting)
                overlay.studioDraft.lighting = ctx.studioSettings.lighting;
            if (!overlay.studioDraftReady || overlay.studioLastSnapshot.video != ctx.studioSettings.video)
            {
                overlay.studioDraft.video = ctx.studioSettings.video;
                std::snprintf(overlay.studioVideoPath, sizeof(overlay.studioVideoPath), "%s", ctx.studioSettings.video.path.c_str());
            }
            overlay.studioLastSnapshot = ctx.studioSettings;
            overlay.studioDraftRevision = ctx.studioRevision;
            overlay.studioDraftReady = true;
        }
        ImGui::TextUnformatted("摄影布光与本地视频（实验）");
        ImGui::TextWrapped("分别启用并应用所需功能。设置会跨区域和重启保留，仅在有效摄影场景生效；切换预设后可再次应用。");
        if (!ctx.injectionAllowed) ImGui::TextDisabled("当前可编辑并保存预设，进入有效摄影场景后应用。");
        if (ImGui::BeginTabBar("##studio_controls"))
        {
            if (ImGui::BeginTabItem("布光"))
            {
                auto& l = overlay.studioDraft.lighting;
                ImGui::Checkbox("启用自定义布光", &l.enabled);
                ImGui::Checkbox("人物风格光", &l.actorStyle);
                ImGui::SliderFloat("人物受光强度", &l.actorIntensity, 0, 4, "%.2f");
                ImGui::SliderFloat("人物轮廓光", &l.rimPower, 0, 8, "%.2f");
                const auto light = [](const char* heading, const char* color, const char* intensity, const char* yaw,
                    const char* pitch, std::array<float, 3>& rgb, float& strength, float& horizontal, float& vertical, bool open) {
                    if (ImGui::CollapsingHeader(heading, open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
                    {
                        ImGui::ColorEdit3(color, rgb.data());
                        ImGui::SliderFloat(intensity, &strength, 0, 8, "%.2f");
                        ImGui::SliderFloat(yaw, &horizontal, -180, 180, "%.1f deg");
                        ImGui::SliderFloat(pitch, &vertical, -90, 90, "%.1f deg");
                    }
                };
                light("主光", "主光颜色", "主光强度", "主光水平角", "主光俯仰角", l.keyColor, l.keyIntensity, l.keyYaw, l.keyPitch, true);
                light("补光", "补光颜色", "补光强度", "补光水平角", "补光俯仰角", l.fillColor, l.fillIntensity, l.fillYaw, l.fillPitch, false);
                light("轮廓灯", "轮廓灯颜色", "轮廓灯强度", "轮廓灯水平角", "轮廓灯俯仰角", l.rimColor, l.rimIntensity, l.rimYaw, l.rimPitch, false);
                if (ImGui::CollapsingHeader("已有背景装饰灯"))
                {
                    ImGui::Checkbox("调整背景装饰灯", &l.decalEnabled);
                    ImGui::ColorEdit3("装饰灯染色", l.decalTint.data());
                    ImGui::SliderFloat("装饰灯强度倍率", &l.decalIntensityScale, 0, 10, "%.2f");
                }
                ImGui::BeginDisabled(!ctx.fnSetStudioSettings);
                if (ImGui::Button("应用布光设置"))
                {
                    auto next = ctx.studioSettings;
                    next.lighting = l;
                    ctx.fnSetStudioSettings(ctx.self, next);
                }
                ImGui::SameLine();
                if (ImGui::Button("停用自定义布光"))
                {
                    auto next = ctx.studioSettings;
                    next.lighting.enabled = false;
                    l.enabled = false;
                    ctx.fnSetStudioSettings(ctx.self, next);
                }
                ImGui::EndDisabled();
                if (!ctx.studioLightingNotice.empty()) ImGui::TextWrapped("%s", ctx.studioLightingNotice.c_str());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("视频输入/幕布实验"))
            {
                auto& v = overlay.studioDraft.video;
                ImGui::TextWrapped("选择本地视频，在摄影中使用自有幕布。画面适配与游戏原生大屏的衔接仍在实验阶段。");
                ImGui::InputText("本地视频路径", overlay.studioVideoPath, sizeof(overlay.studioVideoPath));
                v.path = overlay.studioVideoPath;
                ImGui::BeginDisabled(overlay.browsePendingKind >= 0 || (!ctx.fnBeginPickFile && !ctx.fnPickFile));
                if (ImGui::Button("浏览本地视频")) overlay.BrowseStudioVideo(ctx);
                ImGui::EndDisabled();
                if (!overlay.browseNotice.empty()) ImGui::TextWrapped("%s", overlay.browseNotice.c_str());
                ImGui::Checkbox("启用视频输入", &v.enabled);
                ImGui::Checkbox("暂停视频", &v.paused);
                ImGui::SameLine();
                ImGui::Checkbox("循环视频", &v.loop);
                ImGui::SliderFloat("幕布距离", &v.distance, .25f, 100, "%.2f m");
                ImGui::SliderFloat("幕布宽度", &v.width, .1f, 100, "%.2f m");
                ImGui::SliderFloat("幕布水平偏移", &v.offsetX, -50, 50, "%.2f m");
                ImGui::SliderFloat("幕布垂直偏移", &v.offsetY, -50, 50, "%.2f m");
                auto next = ctx.studioSettings;
                next.video = v;
                std::string error;
                const bool valid = next.Validate(error);
                ImGui::BeginDisabled(!ctx.fnSetStudioSettings || !valid);
                if (ImGui::Button("应用视频设置")) ctx.fnSetStudioSettings(ctx.self, next);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!ctx.fnSetStudioSettings);
                if (ImGui::Button("停用视频输入"))
                {
                    auto disabled = ctx.studioSettings;
                    disabled.video.enabled = false;
                    v.enabled = false;
                    ctx.fnSetStudioSettings(ctx.self, disabled);
                }
                ImGui::EndDisabled();
                if (!valid) ImGui::TextWrapped("%s", error.c_str());
                if (!ctx.studioVideoNotice.empty()) ImGui::TextWrapped("%s", ctx.studioVideoNotice.c_str());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        ImGui::BeginDisabled(!ctx.fnResetStudioSettings);
        if (ImGui::Button("恢复布光与视频默认值"))
        {
            overlay.studioDraftReady = false;
            ctx.fnResetStudioSettings(ctx.self);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}
#endif
