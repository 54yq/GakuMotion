#pragma once

#include "../GkGuiOverlay.hpp"
#include "../GkBoneOverride.hpp"
#include "../GkIdolController.hpp"
#include "GkGuiThemeColors.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void GuiOverlay::DrawBoneTab(const GuiContext& ctx)
	{
		if (!ctx.player)
		{
			ImGui::TextDisabled("播放器尚未就绪");
			return;
		}
		MotionPlayer& player = *ctx.player;
		IdolController& controller = player.controller;

		// ---- 顶部基础控制栏 ----
		ImGui::Text("角色状态: Actor=%p | 绑定骨骼: %zu/%zu",
		            static_cast<void*>(controller.currentBinding.actor),
		            controller.currentBinding.boundBoneCount,
		            controller.currentBinding.boneTransforms.size());
		ImGui::SameLine();
		bool rest = controller.useRestRotations;
		if (ImGui::Checkbox("静息姿态对齐 (useRestRotations)", &rest))
		{
			controller.useRestRotations = rest;
		}

		// ---- Target actor ------------------------------------------------
		if (ctx.knownActorCount > 1)
		{
			ImGui::Separator();
			ImGui::Text("驱动目标: 场景里有 %u 个角色，"
			            "当前驱动 [全部] —— 其余角色不会动但会占CPU",
			            ctx.knownActorCount);
			ImGui::SameLine();
			if (ImGui::Button("取消选择（驱动全部）"))
			{
				if (ctx.fnClearTargetActor) ctx.fnClearTargetActor(ctx.self);
			}
			ImGui::SameLine();
			if (ImGui::Button("下一个角色") && ctx.fnNextTargetActor)
			{
				ctx.fnNextTargetActor(ctx.self);
			}
			ImGui::SameLine();
			if (ImGui::Button("选为当前画面角色"))
			{
				if (ctx.fnPickFocusedActor) ctx.fnPickFocusedActor(ctx.self);
			}
		}

		// ---- Initial position (extends the old Y-only height trim) --------
		ImGui::Separator();
		ImGui::TextUnformatted("初始位置偏移（场景坐标，单位：米）");
		if (controller.rootHeightCalibration.enabled)
		{
			ImGui::Checkbox("录制高度校准", &controller.rootHeightCalibrationEnabled);
			ImGui::SameLine();
			ImGui::Text("匹配骨架: %+.2f mm", controller.currentBinding.calibratedRootHeight * 1000.0f);
		}
		ImGui::TextWrapped("X / Z 调整水平位置，Y 调整高度。拖动 X 或 Z 可调整与镜头的距离；Ctrl+单击可输入数值。");
		if (!initialPositionEditing)
		{
			const auto offset = controller.initialPositionOffset;
			initialPositionEdit[0] = offset.x;
			initialPositionEdit[1] = offset.y;
			initialPositionEdit[2] = offset.z;
		}
		const char* labels[] = {"X 位置", "Y 高度", "Z 位置"};
		bool changed = false, editing = false;
		for (int axis = 0; axis < 3; ++axis)
		{
			ImGui::PushID(axis);
			ImGui::Text("%-7s", labels[axis]);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(70.0f);
			changed |= ImGui::InputFloat("##input", &initialPositionEdit[axis], 0.0f, 0.0f, "%.2f");
			editing |= ImGui::IsItemActive();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(180.0f);
			changed |= ImGui::SliderFloat("##slider", &initialPositionEdit[axis], -10.0f, 10.0f, "%.2f m");
			editing |= ImGui::IsItemActive();
			ImGui::SameLine();
			if (ImGui::Button("归零"))
			{
				initialPositionEdit[axis] = 0;
				changed = true;
			}
			ImGui::PopID();
		}
		initialPositionEditing = editing;
		if (ImGui::Button("三轴归零"))
		{
			initialPositionEdit[0] = initialPositionEdit[1] = initialPositionEdit[2] = 0;
			changed = true;
		}
		ImGui::SameLine();
		if (changed && ctx.fnSetInitialPosition)
			ctx.fnSetInitialPosition(
				ctx.self, Vector3(initialPositionEdit[0], initialPositionEdit[1], initialPositionEdit[2]));
		if (ImGui::Button("保存初始位置") && ctx.fnSaveInitialPosition) ctx.fnSaveInitialPosition(ctx.self);
		ImGui::TextDisabled("作用于当前驱动角色；预览、播放和暂停时生效，切歌保留，停止后恢复场景原位。");
		if (!ctx.initialPositionNotice.empty()) ImGui::TextWrapped("%s", ctx.initialPositionNotice.c_str());

		DrawBoneOverridesSection(ctx);
	}

	inline void GuiOverlay::DrawBoneOverridesSection(const GuiContext& ctx)
	{
		if (ctx.masterOverridesEnabled)
		{
			bool enabled = *ctx.masterOverridesEnabled;
			if (ImGui::Checkbox("覆盖总开关", &enabled) && ctx.fnToggleOverrides)
			{
				ctx.fnToggleOverrides(ctx.self);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("重新载入文件") && ctx.fnReloadOverrides)
		{
			ctx.fnReloadOverrides(ctx.self);
			saveStatusNotice = "已重新从磁盘载入覆盖文件";
		}

		ImGui::SameLine();
		if (ImGui::Button("保存至当前曲目") && ctx.fnSaveOverridesToMotion)
		{
			if (ctx.fnSaveOverridesToMotion(ctx.self))
			{
				saveStatusNotice = "已成功写入当前曲目 bone_overrides.json";
			}
			else
			{
				saveStatusNotice = "保存失败：无法写入当前曲目目录";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("保存至全局") && ctx.fnSaveOverridesToGlobal)
		{
			if (ctx.fnSaveOverridesToGlobal(ctx.self))
			{
				saveStatusNotice = "已成功写入全局 bone_overrides.json";
			}
			else
			{
				saveStatusNotice = "保存失败：无法写入全局文件";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("全部重置") && ctx.fnResetAllOverrides)
		{
			ctx.fnResetAllOverrides(ctx.self);
			saveStatusNotice = "已清空全部在体微调覆盖";
		}

		if (!saveStatusNotice.empty())
		{
			const bool failed = saveStatusNotice.find("失败") != std::string::npos;
			ImGui::PushStyleColor(ImGuiCol_Text, failed ? GuiTheme::ErrorColor() : GuiTheme::SuccessColor());
			ImGui::TextWrapped("● %s", saveStatusNotice.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::Separator();

		// ---- 主体两栏布局：左侧骨骼树状选择，右侧微调滑条 ----
		float contentHeight = 320.0f;
		ImGui::Columns(2, "##bone_columns", true);
		static bool s_columnsSet = false;
		if (!s_columnsSet)
		{
			ImGui::SetColumnWidth(0, 220.0f);
			s_columnsSet = true;
		}

		// ---- 左栏：部位树状折叠骨骼选择 ----
		ImGui::BeginChild("##bone_selector_child", ImVec2(0, contentHeight), true);
		ImGui::TextColored(GuiTheme::InfoColor(), "【骨骼部位选择】");
		ImGui::Separator();

		struct BoneItem
		{
			int hid;
			const char* name;
			const char* labelZh;
		};
		struct BoneGroup
		{
			const char* title;
			std::vector<BoneItem> items;
		};

		static const BoneGroup groups[] = {
			{
				"躯干 (Torso)", {
					{0, "Hips", "骨盆 (Hips)"},
					{7, "Spine", "脊柱 (Spine)"},
					{8, "Chest", "胸腔 (Chest)"}
				}
			},
			{
				"头颈 (Head & Neck)", {
					{9, "Neck", "颈部 (Neck)"},
					{10, "Head", "头部 (Head)"}
				}
			},
			{
				"上肢双臂 (Arms)", {
					{11, "LeftShoulder", "左肩 (L Shoulder)"},
					{12, "RightShoulder", "右肩 (R Shoulder)"},
					{13, "LeftUpperArm", "左上臂 (L UpperArm)"},
					{14, "RightUpperArm", "右上臂 (R UpperArm)"},
					{15, "LeftLowerArm", "左小臂/手肘 (L Forearm)"},
					{16, "RightLowerArm", "右小臂/手肘 (R Forearm)"},
					{17, "LeftHand", "左手 (L Hand)"},
					{18, "RightHand", "右手 (R Hand)"}
				}
			},
			{
				"下肢双腿 (Legs)", {
					{1, "LeftUpperLeg", "左大腿 (L Thigh)"},
					{2, "RightUpperLeg", "右大腿 (R Thigh)"},
					{3, "LeftLowerLeg", "左小腿/膝盖 (L Calf)"},
					{4, "RightLowerLeg", "右小腿/膝盖 (R Calf)"},
					{5, "LeftFoot", "左足踝 (L Foot)"},
					{6, "RightFoot", "右足踝 (R Foot)"},
					{19, "LeftToes", "左脚趾 (L Toes)"},
					{20, "RightToes", "右脚趾 (R Toes)"}
				}
			},
			{
				"左手手指 (L Fingers)", {
					{24, "LeftThumbProximal", "左拇指根"}, {25, "LeftThumbIntermediate", "左拇指中"},
					{26, "LeftThumbDistal", "左拇指尖"},
					{27, "LeftIndexProximal", "左食指根"}, {28, "LeftIndexIntermediate", "左食指中"},
					{29, "LeftIndexDistal", "左食指尖"},
					{30, "LeftMiddleProximal", "左中指根"}, {31, "LeftMiddleIntermediate", "左中指中"},
					{32, "LeftMiddleDistal", "左中指尖"},
					{33, "LeftRingProximal", "左无名根"}, {34, "LeftRingIntermediate", "左无名中"},
					{35, "LeftRingDistal", "左无名尖"},
					{36, "LeftLittleProximal", "左小指根"}, {37, "LeftLittleIntermediate", "左小指中"},
					{38, "LeftLittleDistal", "左小指尖"}
				}
			},
			{
				"右手手指 (R Fingers)", {
					{39, "RightThumbProximal", "右拇指根"}, {40, "RightThumbIntermediate", "右拇指中"},
					{41, "RightThumbDistal", "右拇指尖"},
					{42, "RightIndexProximal", "右食指根"}, {43, "RightIndexIntermediate", "右食指中"},
					{44, "RightIndexDistal", "右食指尖"},
					{45, "RightMiddleProximal", "右中指根"}, {46, "RightMiddleIntermediate", "右中指中"},
					{47, "RightMiddleDistal", "右中指尖"},
					{48, "RightRingProximal", "右无名根"}, {49, "RightRingIntermediate", "右无名中"},
					{50, "RightRingDistal", "右无名尖"},
					{51, "RightLittleProximal", "右小指根"}, {52, "RightLittleIntermediate", "右小指中"},
					{53, "RightLittleDistal", "右小指尖"}
				}
			}
		};

		for (const auto& grp : groups)
		{
			if (ImGui::TreeNode(grp.title))
			{
				for (const auto& b : grp.items)
				{
					bool isSelected = (selectedBoneHid == b.hid);
					float curP = 0.0f, curY = 0.0f, curR = 0.0f;
					bool curW = false, curE = true;
					bool hasOverride = ctx.fnGetBoneOverride
						                   ? ctx.fnGetBoneOverride(ctx.self, b.hid, b.name, &curP, &curY, &curR,
						                                           &curW, &curE)
						                   : false;

					char itemLabel[128]{};
					if (hasOverride)
					{
						std::snprintf(itemLabel, sizeof(itemLabel), "* %s##%d", b.labelZh, b.hid);
					}
					else
					{
						std::snprintf(itemLabel, sizeof(itemLabel), "  %s##%d", b.labelZh, b.hid);
					}

					if (hasOverride) ImGui::PushStyleColor(ImGuiCol_Text, GuiTheme::SuccessColor());
					if (ImGui::Selectable(itemLabel, isSelected))
					{
						selectedBoneHid = b.hid;
						selectedBoneName = b.name;
					}
					if (hasOverride) ImGui::PopStyleColor();
				}
				ImGui::TreePop();
			}
		}
		ImGui::EndChild();

		ImGui::NextColumn();

		// ---- 右栏：选定骨骼详细微调 Inspector ----
		ImGui::BeginChild("##bone_inspector_child", ImVec2(0, contentHeight), true);
		ImGui::TextColored(GuiTheme::HighlightColor(), "【骨骼微调】%s (hid=%d)", selectedBoneName.c_str(),
		                   selectedBoneHid);
		ImGui::Separator();

		// 读取当前骨骼数值
		float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
		bool spaceWorld = false, enabled = true;
		bool isOverridden = ctx.fnGetBoneOverride
			                    ? ctx.fnGetBoneOverride(ctx.self, selectedBoneHid, selectedBoneName.c_str(), &pitch,
			                                            &yaw, &roll, &spaceWorld, &enabled)
			                    : false;

		// 对称骨骼检测
		int symmHid = BoneOverrideSet::GetSymmetricalHumanoidId(selectedBoneHid);
		if (symmHid >= 0)
		{
			std::string symmName = BoneOverrideSet::GetBoneNameByHumanoidId(symmHid);
			ImGui::Checkbox("对称联动 (Symmetrical)", &symmetricalMode);
			ImGui::SameLine();
			ImGui::TextDisabled("-> 镜像: %s (hid=%d)", symmName.c_str(), symmHid);
		}
		else
		{
			ImGui::TextDisabled("(无对称配对骨骼)");
		}

		// 空间模式单选
		int spaceChoice = spaceWorld ? 1 : 0;
		ImGui::Text("空间模式:");
		ImGui::SameLine();
		if (ImGui::RadioButton("局部后乘 (Local)", &spaceChoice, 0))
		{
			spaceWorld = false;
			if (ctx.fnSetBoneOverride)
			{
				ctx.fnSetBoneOverride(ctx.self, selectedBoneHid, selectedBoneName.c_str(), pitch, yaw, roll,
				                      spaceWorld, enabled, symmetricalMode);
			}
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("世界左乘 (World)", &spaceChoice, 1))
		{
			spaceWorld = true;
			if (ctx.fnSetBoneOverride)
			{
				ctx.fnSetBoneOverride(ctx.self, selectedBoneHid, selectedBoneName.c_str(), pitch, yaw, roll,
				                      spaceWorld, enabled, symmetricalMode);
			}
		}

		ImGui::Separator();

		// 三轴滑条调整 (Pitch / Yaw / Roll)
		bool sliderChanged = false;
		ImGui::Text("Pitch (X 轴):");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(65.0f);
		sliderChanged |= ImGui::InputFloat("##PitchInput", &pitch, 0.0f, 0.0f, "%.1f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		sliderChanged |= ImGui::SliderFloat("##PitchSlider", &pitch, -180.0f, 180.0f, "%.1f°");
		ImGui::SameLine();
		if (ImGui::Button("0°##ZeroP"))
		{
			pitch = 0.0f;
			sliderChanged = true;
		}

		ImGui::Text("Yaw   (Y 轴):");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(65.0f);
		sliderChanged |= ImGui::InputFloat("##YawInput", &yaw, 0.0f, 0.0f, "%.1f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		sliderChanged |= ImGui::SliderFloat("##YawSlider", &yaw, -180.0f, 180.0f, "%.1f°");
		ImGui::SameLine();
		if (ImGui::Button("0°##ZeroY"))
		{
			yaw = 0.0f;
			sliderChanged = true;
		}

		ImGui::Text("Roll  (Z 轴):");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(65.0f);
		sliderChanged |= ImGui::InputFloat("##RollInput", &roll, 0.0f, 0.0f, "%.1f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		sliderChanged |= ImGui::SliderFloat("##RollSlider", &roll, -180.0f, 180.0f, "%.1f°");
		ImGui::SameLine();
		if (ImGui::Button("0°##ZeroR"))
		{
			roll = 0.0f;
			sliderChanged = true;
		}

		// 快速微调步进按钮
		ImGui::Text("快速步进:");
		ImGui::SameLine();
		if (ImGui::Button("-5°##P"))
		{
			pitch -= 5.0f;
			sliderChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("+5°##P"))
		{
			pitch += 5.0f;
			sliderChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("-5°##R"))
		{
			roll -= 5.0f;
			sliderChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("+5°##R"))
		{
			roll += 5.0f;
			sliderChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("全部归零##AllZero"))
		{
			pitch = yaw = roll = 0.0f;
			sliderChanged = true;
		}

		if (sliderChanged && ctx.fnSetBoneOverride)
		{
			ctx.fnSetBoneOverride(ctx.self, selectedBoneHid, selectedBoneName.c_str(), pitch, yaw, roll, spaceWorld,
			                      enabled, symmetricalMode);
		}

		ImGui::Separator();
		if (isOverridden)
		{
			ImGui::TextColored(GuiTheme::SuccessColor(), "当前覆盖状态: 已注入微调 (Pitch=%.1f, Yaw=%.1f, Roll=%.1f)",
			                   pitch, yaw, roll);
			if (ImGui::Button("恢复默认值 (Reset Bone)") && ctx.fnResetBoneOverride)
			{
				ctx.fnResetBoneOverride(ctx.self, selectedBoneHid, selectedBoneName.c_str(), symmetricalMode);
			}
		}
		else
		{
			ImGui::TextDisabled("当前状态: 原始 VMD 动作（无覆盖）");
		}

		ImGui::EndChild();
		ImGui::Columns(1);

		// ---- 底部：已生效覆盖汇总列表 ----
		if (ctx.overrides)
		{
			ImGui::Text("当前内存活跃覆盖表: 共 %zu 条条目", ctx.overrides->offsets.size());
			ImGui::BeginChild("##active_override_summary", ImVec2(0, 100), true);
			for (const auto& entry : ctx.overrides->offsets)
			{
				ImGui::BulletText("%s | space=%s | euler=(%.1f°, %.1f°, %.1f°) | %s",
				                  entry.Label().c_str(),
				                  entry.spaceWorld ? "world" : "local",
				                  entry.eulerDeg.x, entry.eulerDeg.y, entry.eulerDeg.z,
				                  entry.enabled ? "生效" : "[禁用]");
			}
			if (ctx.overrides->offsets.empty())
			{
				ImGui::TextDisabled("（当前无任何微调条目，角色使用纯净 VMD 姿态）");
			}
			ImGui::EndChild();
		}
	}
} // namespace GakuMotion
#endif
