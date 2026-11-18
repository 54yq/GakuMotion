#pragma once

#include "GuiOverlayCommon.hpp"
#include "../GkGuiOverlay.hpp"
#include "../GkIdolController.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void DrawMotionExperiments(const GuiContext& ctx)
	{
		if (!ctx.player)
		{
			ImGui::TextDisabled("播放器尚未就绪");
			return;
		}
		const MotionPlayer& player = *ctx.player;
		const IdolController& controller = player.controller;
		bool mikuMouthFit = ctx.config && ctx.config->facialMikuMouthFit >= 0
			? ctx.config->facialMikuMouthFit != 0 : controller.facialMikuMouthFit;
		ImGui::BeginDisabled(!ctx.config);
		if (ImGui::Checkbox("实验：四角色 Miku 嘴形拟合", &mikuMouthFit) && ctx.config)
			ctx.config->facialMikuMouthFit = mikuMouthFit ? 1 : 0;
		ImGui::EndDisabled();
		ImGui::TextWrapped("默认启用，自动识别千奈、麻央、莉波、燕的 base-0000 脸型。需要保留原始五元音的新导入曲目；旧曲目或身份不符时沿用原适配。切换会重新绑定表情，当前为实验效果，尚未完成实机视觉验收。");
		const unsigned fittedSnapshot = controller.guiMikuFitSnapshot.load(std::memory_order_relaxed);
		const unsigned fittedRole = fittedSnapshot >> 8;
		const auto fittedDecision = static_cast<MikuFitDecision>(fittedSnapshot & 0xff);
		const char* fittedStatus = "尚未接管";
		if (!mikuMouthFit) fittedStatus = "已关闭";
		else if (player.state != PlaybackState::Stopped) switch (fittedDecision)
		{
		case MikuFitDecision::Applied: fittedStatus = "已应用"; break;
		case MikuFitDecision::MissingRawSource: fittedStatus = "已回退：旧曲目缺原始五元音"; break;
		case MikuFitDecision::InvalidRawSource: fittedStatus = "已回退：源数据不完整"; break;
		case MikuFitDecision::UnsupportedSource: fittedStatus = "已回退：源嘴形未覆盖"; break;
		case MikuFitDecision::NativeAuthored: fittedStatus = "已回退：保留显式原生嘴形"; break;
		case MikuFitDecision::UnknownFace: fittedStatus = "已回退：实际脸型未匹配"; break;
		case MikuFitDecision::CharacterMismatch: fittedStatus = "已回退：角色身份不一致"; break;
		case MikuFitDecision::MissingOutput: fittedStatus = "已回退：目标通道不完整"; break;
		case MikuFitDecision::OutsideDomain: fittedStatus = "已回退：源混合超出拟合范围"; break;
		case MikuFitDecision::InvalidModel: fittedStatus = "已回退：拟合数据无效"; break;
		default: break;
		}
		ImGui::TextWrapped("嘴形拟合状态：%s", fittedStatus);
		if (fittedRole > 0 && fittedRole <= kMikuMouthFits.size())
			ImGui::TextWrapped("最近识别脸型：%s（base-0000）", FormatIdolDisplayName(kMikuMouthFits[fittedRole-1].character).c_str());
	}
} // namespace GakuMotion
#endif
