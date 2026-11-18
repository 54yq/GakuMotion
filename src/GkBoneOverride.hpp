#pragma once
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// =============================================================================
// GkBoneOverride.hpp — 全身骨骼旋转「自定义编辑」覆盖层
// =============================================================================
// 运行时契约（与 ARCHITECTURE.md §3 的骨骼数学契约严格对齐）：
//
//   基线姿态（现网公式）：
//       R_target = Q_actor × (R_vmd_world × R_rest_rel)
//
//   叠加覆盖后：
//       space = "local"（默认）：R_target = Q_actor × (R_vmd_world × R_rest_rel) × Q_offset
//                                 ↑ 后乘 = 在骨骼自身的局部旋转空间里追加偏移，
//                                   手感与现有 armOffsetRoll（上臂微调）一致，
//                                   不会因为 Actor 朝向变化而改变偏移方向。
//       space = "world"：        R_target = Q_offset × Q_actor × (R_vmd_world × R_rest_rel)
//                                 ↑ 左乘 = 世界空间追加旋转，适合"整体扳正朝向"类需求，
//                                   旋转轴固定于世界坐标，与 Actor 朝向无关。
//
// 文件 schema（外部编辑器 / 手工编辑后热加载）：
//   {
//     "enabled": true,                      // 该文件的条目是否参与合并
//     "overrides": [                        // 也接受 "bones" 作为别名
//       {
//         "bone": "LeftUpperArm",           // 与 .gkmotion 的骨骼名一致（可读别名）
//         "humanoid_id": 13,                // 主键优先；两者都给时以 id 为准
//         "enabled": true,                  // 单条开关
//         "space": "local",                 // "local"（默认）| "world"
//         "euler_deg": [-5.0, 0.0, 3.0],    // [pitch(X), yaw(Y), roll(Z)]
//         "quaternion": [0, 0, 0, 1]        // [x, y, z, w]，提供时优先于 euler_deg
//       }
//     ]
//   }
//   * euler_deg 亦接受 {"x":..,"y":..,"z":..} 或 {"pitch":..,"yaw":..,"roll":..}；
//   * quaternion 亦接受 {"x":..,"y":..,"z":..,"w":..}；
//   * 骨骼名含日文/中文时请以 UTF-8 保存（解析器已处理 BOM 与 \uXXXX）。
//
// 合并优先级（由高到低）：
//   1. 运行时动态条目（快捷键臂部微调等，不落盘）
//   2. 曲目级 <localRoot>/motions/<曲目>/bone_overrides.json
//   3. 全局 <localRoot>/GakuMotion/bone_overrides.json
//   4. 总开关 overrides.enabled = false 时全部失效，严格还原 VMD 原始姿态
// =============================================================================

#include "GkMotionData.hpp"
#include "GkJsonLite.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <filesystem>

namespace GakuMotion
{
	// -----------------------------------------------------------------------------
	// 单条骨骼旋转覆盖记录
	// -----------------------------------------------------------------------------
	struct BoneRotationOffset
	{
		int humanoidId{-1}; // Unity Humanoid 骨骼 id；-1 表示仅按名称匹配
		std::string boneName; // 骨骼名（对应 clip.bones[b].name，如 "LeftUpperArm"）
		bool enabled{true}; // 单条开关
		bool spaceWorld{false}; // false = 局部后乘（默认）；true = 世界左乘
		bool useQuaternion{false}; // true 时使用 quaternion 字段，否则使用 eulerDeg
		Vector3 eulerDeg{0.0f, 0.0f, 0.0f};
		Quaternion quaternion{0.0f, 0.0f, 0.0f, 1.0f};
		bool isDynamic{false}; // 运行时动态条目（不来自文件）

		// 解析为最终四元数
		Quaternion Resolve() const noexcept
		{
			if (useQuaternion) return quaternion.Normalized();
			return Quaternion::FromEulerDegreesXYZ(eulerDeg.x, eulerDeg.y, eulerDeg.z);
		}

		// 用于命名/日志
		std::string Label() const
		{
			char buf[96]{};
			if (humanoidId >= 0)
			{
				std::snprintf(buf, sizeof(buf), "hid=%d%s%s", humanoidId,
				              boneName.empty() ? "" : " ",
				              boneName.c_str());
			}
			else
			{
				std::snprintf(buf, sizeof(buf), "%s", boneName.c_str());
			}
			return std::string(buf);
		}
	};

	// -----------------------------------------------------------------------------
	// 覆盖集合
	// -----------------------------------------------------------------------------
	class BoneOverrideSet
	{
	public:
		bool enabled{true}; // 该集合自身是否参与合成
		std::string sourcePath; // 来源文件（空 = 动态/未加载）
		std::string sourceLabel{"<unset>"}; // 日志用标签
		std::string lastError; // 致命错误（解析失败等）
		std::vector<std::string> warnings; // 非致命告警（未知骨骼、字段缺失等）
		std::vector<BoneRotationOffset> offsets; // 文件来源的静态条目
		uint64_t revision{0}; // 每次内容变化自增，供下游做缓存失效

		bool Empty() const noexcept { return offsets.empty() && dynamicOffsets_.empty(); }
		size_t Size() const noexcept { return offsets.size() + dynamicOffsets_.size(); }
		size_t StaticSize() const noexcept { return offsets.size(); }
		size_t DynamicSize() const noexcept { return dynamicOffsets_.size(); }

		void Clear()
		{
			offsets.clear();
			dynamicOffsets_.clear();
			warnings.clear();
			lastError.clear();
			byId_.clear();
			byName_.clear();
			dynamicIndexBegin_ = 0;
			enabled = true;
			++revision;
		}

		// -------- 加载 --------
		bool LoadFromFile(const std::string& path)
		{
			sourcePath = path;
			sourceLabel = path;
			if (!std::filesystem::exists(MotionPathFromUtf8(path)))
			{
				// 「文件不存在」不是错误：调用方按「无覆盖」处理
				offsets.clear();
				warnings.clear();
				lastError.clear();
				enabled = true;
				RebuildIndex();
				++revision;
				return true;
			}
			std::string err;
			jsonlite::Value root;
			std::ifstream file(MotionPathFromUtf8(path), std::ios::binary);
			std::ostringstream text;
			text << file.rdbuf();
			if (!file.is_open() || !jsonlite::ParseText(text.str(), root, err))
			{
				lastError = err.empty() ? "cannot open bone overrides: " + path : err;
				return false;
			}
			return LoadFromValue(root, path);
		}

		bool LoadFromText(const std::string& text, const std::string& label)
		{
			sourcePath = label;
			sourceLabel = label;
			std::string err;
			jsonlite::Value root;
			if (!jsonlite::ParseText(text, root, err))
			{
				lastError = label + " -> " + err;
				return false;
			}
			return LoadFromValue(root, label);
		}

		// -------- 合并（*this 作为「更高优先级」并覆盖到 target） --------
		void MergeInto(BoneOverrideSet& target) const
		{
			if (!enabled) return;
			if (offsets.empty()) return;

			for (const auto& entry : offsets)
			{
				// 注意：被禁用的条目同样参与「键匹配」。它的语义是
				// 「在本层显式压制低优先级层的同键条目」，因此这里绝不能跳过，
				// 否则曲目级无法关掉全局里的某一条覆盖（离线测试已覆盖该用例）。
				// 同名/同 id 的记录，高优先级覆盖低优先级
				bool replaced = false;
				for (auto& existing : target.offsets)
				{
					if (SameKey(existing, entry))
					{
						existing = entry;
						replaced = true;
						break;
					}
				}
				if (!replaced) target.offsets.push_back(entry);
			}
		}

		// -------- 合成有效集合 --------
		// global: 全局文件；motion: 曲目级文件；masterEnabled: control.json / 快捷键总开关
		static void BuildEffective(const BoneOverrideSet& global,
		                           const BoneOverrideSet& motion,
		                           bool masterEnabled,
		                           BoneOverrideSet& out)
		{
			out.Clear();
			out.sourceLabel = "effective";

			if (masterEnabled)
			{
				if (global.enabled) global.MergeInto(out);
				if (motion.enabled) motion.MergeInto(out);
			}

			// 只有「存在至少一条启用条目」且总开关打开时才允许生效；
			// 全部被压制的集合应等价于「无覆盖」。
			bool hasEnabledEntry = false;
			for (const auto& e : out.offsets)
			{
				if (e.enabled)
				{
					hasEnabledEntry = true;
					break;
				}
			}
			out.enabled = masterEnabled && hasEnabledEntry;
			out.RebuildIndex();
			++out.revision;

			if (!global.lastError.empty()) out.lastError = global.lastError;
			if (!motion.lastError.empty())
			{
				if (!out.lastError.empty()) out.lastError += " | ";
				out.lastError += motion.lastError;
			}
			for (const auto& w : global.warnings) out.warnings.push_back(w);
			for (const auto& w : motion.warnings) out.warnings.push_back(w);
		}

		// -------- 运行时动态条目（快捷键实时微调，不落盘） --------
		void SetDynamic(std::vector<BoneRotationOffset> entries)
		{
			for (auto& e : entries) e.isDynamic = true;
			dynamicOffsets_ = std::move(entries);
			dynamicIndexBegin_ = offsets.size();
			RebuildIndex();
			++revision;
		}

		void ClearDynamic()
		{
			if (dynamicOffsets_.empty()) return;
			dynamicOffsets_.clear();
			dynamicIndexBegin_ = offsets.size();
			RebuildIndex();
			++revision;
		}

		// -------- 静态骨骼字典与对称骨骼映射 (MMDD 规范) --------
		static int GetSymmetricalHumanoidId(int hid) noexcept
		{
			switch (hid)
			{
			case 11: return 12; // LeftShoulder <-> RightShoulder
			case 12: return 11;
			case 13: return 14; // LeftUpperArm <-> RightUpperArm
			case 14: return 13;
			case 15: return 16; // LeftLowerArm <-> RightLowerArm
			case 16: return 15;
			case 17: return 18; // LeftHand <-> RightHand
			case 18: return 17;
			case 1: return 2; // LeftUpperLeg <-> RightUpperLeg
			case 2: return 1;
			case 3: return 4; // LeftLowerLeg <-> RightLowerLeg
			case 4: return 3;
			case 5: return 6; // LeftFoot <-> RightFoot
			case 6: return 5;
			case 19: return 20; // LeftToes <-> RightToes
			case 20: return 19;
			default:
				if (hid >= 24 && hid <= 38) return hid + 15; // LeftFingers <-> RightFingers
				if (hid >= 39 && hid <= 53) return hid - 15;
				return -1;
			}
		}

		static const char* GetBoneNameByHumanoidId(int hid) noexcept
		{
			switch (hid)
			{
			case 0: return "Hips";
			case 7: return "Spine";
			case 8: return "Chest";
			case 9: return "Neck";
			case 10: return "Head";
			case 11: return "LeftShoulder";
			case 12: return "RightShoulder";
			case 13: return "LeftUpperArm";
			case 14: return "RightUpperArm";
			case 15: return "LeftLowerArm";
			case 16: return "RightLowerArm";
			case 17: return "LeftHand";
			case 18: return "RightHand";
			case 1: return "LeftUpperLeg";
			case 2: return "RightUpperLeg";
			case 3: return "LeftLowerLeg";
			case 4: return "RightLowerLeg";
			case 5: return "LeftFoot";
			case 6: return "RightFoot";
			case 19: return "LeftToes";
			case 20: return "RightToes";
			case 24: return "LeftThumbProximal";
			case 25: return "LeftThumbIntermediate";
			case 26: return "LeftThumbDistal";
			case 27: return "LeftIndexProximal";
			case 28: return "LeftIndexIntermediate";
			case 29: return "LeftIndexDistal";
			case 30: return "LeftMiddleProximal";
			case 31: return "LeftMiddleIntermediate";
			case 32: return "LeftMiddleDistal";
			case 33: return "LeftRingProximal";
			case 34: return "LeftRingIntermediate";
			case 35: return "LeftRingDistal";
			case 36: return "LeftLittleProximal";
			case 37: return "LeftLittleIntermediate";
			case 38: return "LeftLittleDistal";
			case 39: return "RightThumbProximal";
			case 40: return "RightThumbIntermediate";
			case 41: return "RightThumbDistal";
			case 42: return "RightIndexProximal";
			case 43: return "RightIndexIntermediate";
			case 44: return "RightIndexDistal";
			case 45: return "RightMiddleProximal";
			case 46: return "RightMiddleIntermediate";
			case 47: return "RightMiddleDistal";
			case 48: return "RightRingProximal";
			case 49: return "RightRingIntermediate";
			case 50: return "RightRingDistal";
			case 51: return "RightLittleProximal";
			case 52: return "RightLittleIntermediate";
			case 53: return "RightLittleDistal";
			default: return "";
			}
		}

		// 内存中操作单条骨骼覆盖
		BoneRotationOffset& GetOrAddOffset(int humanoidId, const std::string& boneName)
		{
			for (auto& o : offsets)
			{
				if ((humanoidId >= 0 && o.humanoidId == humanoidId) ||
					(!boneName.empty() && o.boneName == boneName))
				{
					return o;
				}
			}
			BoneRotationOffset newEntry;
			newEntry.humanoidId = humanoidId;
			newEntry.boneName = boneName.empty() ? GetBoneNameByHumanoidId(humanoidId) : boneName;
			newEntry.enabled = true;
			newEntry.spaceWorld = false;
			offsets.push_back(newEntry);
			RebuildIndex();
			++revision;
			return offsets.back();
		}

		bool RemoveOffset(int humanoidId, const std::string& boneName)
		{
			for (auto it = offsets.begin(); it != offsets.end(); ++it)
			{
				if ((humanoidId >= 0 && it->humanoidId == humanoidId) ||
					(!boneName.empty() && it->boneName == boneName))
				{
					offsets.erase(it);
					RebuildIndex();
					++revision;
					return true;
				}
			}
			return false;
		}

		// JSON 序列化并保存到磁盘
		bool SaveToFile(const std::string& path) const
		{
			if (path.empty()) return false;
			const auto p = MotionPathFromUtf8(path);
			if (p.has_parent_path())
			{
				std::error_code ec;
				std::filesystem::create_directories(p.parent_path(), ec);
			}
#ifdef _WIN32
			FILE* f = nullptr;
			_wfopen_s(&f, p.c_str(), L"w");
#else
			FILE* f = fopen(p.c_str(), "w");
#endif
			if (!f) return false;
			fprintf(f, "{\n  \"enabled\": %s,\n  \"overrides\": [\n", enabled ? "true" : "false");
			for (size_t i = 0; i < offsets.size(); ++i)
			{
				const auto& o = offsets[i];
				fprintf(f, "    {\n");
				if (!o.boneName.empty())
				{
					fprintf(f, "      \"bone\": \"%s\",\n", o.boneName.c_str());
				}
				if (o.humanoidId >= 0)
				{
					fprintf(f, "      \"humanoid_id\": %d,\n", o.humanoidId);
				}
				fprintf(f, "      \"enabled\": %s,\n", o.enabled ? "true" : "false");
				fprintf(f, "      \"space\": \"%s\",\n", o.spaceWorld ? "world" : "local");
				if (o.useQuaternion)
				{
					fprintf(f, "      \"quaternion\": [%.6f, %.6f, %.6f, %.6f]\n",
					        o.quaternion.x, o.quaternion.y, o.quaternion.z, o.quaternion.w);
				}
				else
				{
					fprintf(f, "      \"euler_deg\": [%.2f, %.2f, %.2f]\n",
					        o.eulerDeg.x, o.eulerDeg.y, o.eulerDeg.z);
				}
				fprintf(f, "    }%s\n", (i + 1 < offsets.size()) ? "," : "");
			}
			fprintf(f, "  ]\n}\n");
			const bool written = std::ferror(f) == 0;
			return std::fclose(f) == 0 && written;
		}

		// -------- 查找（先按 humanoid id，再按骨骼名） --------
		const BoneRotationOffset* Find(int humanoidId, const std::string& boneName) const noexcept
		{
			if (humanoidId >= 0)
			{
				auto it = byId_.find(humanoidId);
				if (it != byId_.end())
				{
					const BoneRotationOffset* e = EntryAt(it->second);
					// 命中但被禁用 = 显式压制：直接返回「无覆盖」，
					// 不再回退到名称查找（否则会被低优先级层重新命中）。
					if (e) return e->enabled ? e : nullptr;
				}
			}
			if (!boneName.empty())
			{
				auto it = byName_.find(boneName);
				if (it != byName_.end())
				{
					const BoneRotationOffset* e = EntryAt(it->second);
					if (e && e->enabled) return e;
				}
			}
			return nullptr;
		}

		// -------- 便于日志/调试 --------
		std::string Describe() const
		{
			char buf[256]{};
			std::snprintf(buf, sizeof(buf),
			              "%s [enabled=%s static=%zu dynamic=%zu rev=%llu]",
			              sourceLabel.c_str(),
			              enabled ? "true" : "false",
			              offsets.size(),
			              dynamicOffsets_.size(),
			              static_cast<unsigned long long>(revision));
			return std::string(buf);
		}

	private:
		std::vector<BoneRotationOffset> dynamicOffsets_;
		std::unordered_map<int, size_t> byId_;
		std::unordered_map<std::string, size_t> byName_;
		size_t dynamicIndexBegin_{0};

		static bool SameKey(const BoneRotationOffset& a, const BoneRotationOffset& b) noexcept
		{
			if (a.humanoidId >= 0 && b.humanoidId >= 0) return a.humanoidId == b.humanoidId;
			if (!a.boneName.empty() && !b.boneName.empty()) return a.boneName == b.boneName;
			return false;
		}

		const BoneRotationOffset* EntryAt(size_t index) const noexcept
		{
			if (index < offsets.size()) return &offsets[index];
			size_t dyn = index - offsets.size();
			if (dyn < dynamicOffsets_.size()) return &dynamicOffsets_[dyn];
			return nullptr;
		}

	public:
		void RebuildIndex()
		{
			byId_.clear();
			byName_.clear();
			for (size_t i = 0; i < offsets.size(); ++i)
			{
				// 同时建立 id 与名称两套索引：带 id 的条目往往也带可读名，
				// 这样按名称回退查找依然可用。
				if (offsets[i].humanoidId >= 0) byId_[offsets[i].humanoidId] = i;
				if (!offsets[i].boneName.empty()) byName_[offsets[i].boneName] = i;
			}
			for (size_t i = 0; i < dynamicOffsets_.size(); ++i)
			{
				size_t slot = offsets.size() + i;
				if (dynamicOffsets_[i].humanoidId >= 0) byId_[dynamicOffsets_[i].humanoidId] = slot;
				if (!dynamicOffsets_[i].boneName.empty()) byName_[dynamicOffsets_[i].boneName] = slot;
			}
			dynamicIndexBegin_ = offsets.size();
		}

		// -------- 值解析工具 --------
		static bool ReadVector3(const jsonlite::Value& v, Vector3& out)
		{
			if (v.IsArray() && v.Size() >= 3)
			{
				out = Vector3(v.At(0)->AsFloat(0.0f), v.At(1)->AsFloat(0.0f), v.At(2)->AsFloat(0.0f));
				return true;
			}
			if (v.IsObject())
			{
				// 支持 {x,y,z} 与 {pitch,yaw,roll} 两种写法
				if (v.Find("x") && v.Find("y") && v.Find("z"))
				{
					out = Vector3(v.FloatPath("x", 0.0f), v.FloatPath("y", 0.0f), v.FloatPath("z", 0.0f));
					return true;
				}
				if (v.Find("pitch") || v.Find("yaw") || v.Find("roll"))
				{
					out = Vector3(v.FloatPath("pitch", v.FloatPath("x", 0.0f)),
					              v.FloatPath("yaw", v.FloatPath("y", 0.0f)),
					              v.FloatPath("roll", v.FloatPath("z", 0.0f)));
					return true;
				}
			}
			return false;
		}

		static bool ReadQuaternion(const jsonlite::Value& v, Quaternion& out)
		{
			if (v.IsArray() && v.Size() >= 4)
			{
				out = Quaternion(v.At(0)->AsFloat(0.0f),
				                 v.At(1)->AsFloat(0.0f),
				                 v.At(2)->AsFloat(0.0f),
				                 v.At(3)->AsFloat(1.0f));
				return true;
			}
			if (v.IsObject() && v.Find("x") && v.Find("y") && v.Find("z") && v.Find("w"))
			{
				out = Quaternion(v.FloatPath("x", 0.0f), v.FloatPath("y", 0.0f),
				                 v.FloatPath("z", 0.0f), v.FloatPath("w", 1.0f));
				return true;
			}
			return false;
		}

		bool LoadFromValue(const jsonlite::Value& root, const std::string& label)
		{
			offsets.clear();
			warnings.clear();
			lastError.clear();
			enabled = true;

			if (!root.IsObject())
			{
				lastError = label + " -> top-level must be an object ({ ... })";
				RebuildIndex();
				++revision;
				return false;
			}

			enabled = root.BoolPath("enabled", true);

			const jsonlite::Value* list = root.Find("overrides");
			if (!list) list = root.Find("bones");
			if (!list)
			{
				// 空文件/仅开关文件：合法，等价于「无覆盖」
				warnings.push_back(label + " -> no overrides/bones array found; treated as no overrides");
				RebuildIndex();
				++revision;
				return true;
			}
			if (!list->IsArray())
			{
				lastError = label + " -> overrides must be an array";
				RebuildIndex();
				++revision;
				return false;
			}

			for (size_t i = 0; i < list->Size(); ++i)
			{
				const jsonlite::Value& item = *list->At(i);
				char ctx[64]{};
				std::snprintf(ctx, sizeof(ctx), "%s -> overrides[%zu]", label.c_str(), i);

				if (!item.IsObject())
				{
					warnings.push_back(std::string(ctx) + " is not an object; skipped");
					continue;
				}

				BoneRotationOffset entry;
				bool hasId = false;
				if (const jsonlite::Value* v = item.Find("humanoid_id"))
				{
					entry.humanoidId = v->AsInt(-1);
					hasId = entry.humanoidId >= 0;
				}
				else if (const jsonlite::Value* v2 = item.Find("humanoidId"))
				{
					entry.humanoidId = v2->AsInt(-1);
					hasId = entry.humanoidId >= 0;
				}
				else if (const jsonlite::Value* v3 = item.Find("hid"))
				{
					entry.humanoidId = v3->AsInt(-1);
					hasId = entry.humanoidId >= 0;
				}
				for (const char* nameKey : {"bone", "bone_name", "name"})
				{
					if (const jsonlite::Value* v = item.Find(nameKey))
					{
						if (v->IsString() && !v->stringValue.empty())
						{
							entry.boneName = v->stringValue;
							break;
						}
					}
				}

				if (!hasId && entry.boneName.empty())
				{
					warnings.push_back(std::string(ctx) + " has neither humanoid_id nor bone; skipped");
					continue;
				}

				entry.enabled = item.BoolPath("enabled", true);
				const std::string space = item.StringPath("space", "local");
				entry.spaceWorld = (space == "world" || space == "World" || space == "WORLD");

				bool hasRot = false;
				if (const jsonlite::Value* q = item.Find("quaternion"))
				{
					if (ReadQuaternion(*q, entry.quaternion))
					{
						entry.useQuaternion = true;
						hasRot = true;
					}
					else
					{
						warnings.push_back(std::string(ctx) + " has an invalid quaternion; field ignored");
					}
				}
				if (const jsonlite::Value* e = item.Find("euler_deg"))
				{
					if (ReadVector3(*e, entry.eulerDeg))
					{
						hasRot = true;
					}
					else
					{
						warnings.push_back(
							std::string(ctx) + " has an invalid euler_deg (expected [pitch,yaw,roll]); field ignored");
					}
				}

				if (!hasRot)
				{
					warnings.push_back(
						std::string(ctx) + " has no valid rotation (euler_deg / quaternion); treated as 0 degrees");
				}

				offsets.push_back(std::move(entry));
			}

			// 同键去重：后写的覆盖先写的
			DeduplicateEntries(offsets, warnings, label);

			RebuildIndex();
			++revision;
			return true;
		}

		static void DeduplicateEntries(std::vector<BoneRotationOffset>& list,
		                               std::vector<std::string>& warnings,
		                               const std::string& label)
		{
			std::vector<BoneRotationOffset> kept;
			kept.reserve(list.size());
			for (const auto& e : list)
			{
				bool replaced = false;
				for (auto& k : kept)
				{
					if (SameKey(k, e))
					{
						warnings.push_back(label + " -> duplicate key " + e.Label() + "; last one wins");
						k = e;
						replaced = true;
						break;
					}
				}
				if (!replaced) kept.push_back(e);
			}
			list.swap(kept);
		}
	};
} // namespace GakuMotion
