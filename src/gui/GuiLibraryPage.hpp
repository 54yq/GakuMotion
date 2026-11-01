#pragma once

// =============================================================================
// GuiLibraryPage.hpp — 曲库页面、添加/管理模态弹窗与导入进度
// =============================================================================

#include "GuiOverlayCommon.hpp"
namespace GakuMotion
{
	class GuiOverlay;
	struct GuiContext;
	void DrawLibraryPage(GuiOverlay& overlay, const GuiContext& ctx);
}

#include "../GkGuiOverlay.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#endif

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	// 导入弹窗下拉框里的占位条目。它们是哨兵值而不是磁盘路径，所有"是否选中了
	// 真实文件"的判断都必须把它们排除掉。集中在一处，避免各分支各写一份字符串
	// 比较（漏一处就会把"未指定"当成"显式关闭"）。
	namespace ImportSlot
	{
		inline constexpr const char* kEmpty = "（留空）";
		inline constexpr const char* kClear = "（留空 / 清除）";
		inline constexpr const char* kKeepTrack = "[保持当前已导入轨道]";
		inline constexpr const char* kKeepMotion = "[保持当前动作]";
		// 新增导入时的"自动"：表情沿用动作 VMD 内自带的表情，音乐按同名 WAV /
		// song.wav 自动发现。两者都对应 Python CLI 的默认行为（不传对应开关）。
		inline constexpr const char* kAutoFacial = "（自动：沿用动作 VMD 内表情）";
		inline constexpr const char* kAutoAudio = "（自动：同名 WAV 或 song.wav）";
		// 显式关闭：与"自动"区分开，传 --no-morph / --clear-audio。
		inline constexpr const char* kSkip = "（不导入）";
	}

	inline bool IsSlotPlaceholder(const std::string& path)
	{
		return path.empty() ||
			path == ImportSlot::kEmpty || path == ImportSlot::kClear ||
			path == ImportSlot::kKeepTrack || path == ImportSlot::kKeepMotion ||
			path == ImportSlot::kAutoFacial || path == ImportSlot::kAutoAudio ||
			path == ImportSlot::kSkip;
	}

	// 旋转圆弧加载动画 (Spinner) — 纯公开 ImGui API，无私有头文件依赖
	inline void DrawSpinner(const char* label, float radius, float thickness, const ImVec4& color)
	{
		(void)label;
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const float size = radius * 2.0f;
		ImGui::Dummy(ImVec2(size, size));

		const float time = static_cast<float>(ImGui::GetTime());
		const int num_segments = 30;
		const float start = std::fmod(time * 4.0f, 3.14159265f * 2.0f);
		const float a_min = start;
		const float a_max = start + 3.14159265f * 1.4f;

		const ImVec2 centre(pos.x + radius, pos.y + radius);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PathClear();
		for (int i = 0; i <= num_segments; ++i)
		{
			const float a = a_min + (static_cast<float>(i) / static_cast<float>(num_segments)) * (a_max - a_min);
			drawList->PathLineTo(ImVec2(centre.x + std::cos(a) * radius, centre.y + std::sin(a) * radius));
		}
		drawList->PathStroke(ImGui::GetColorU32(color), 0, thickness);
	}

	// 快速检测已导入目录是否包含表情轨道
	inline bool DetectMotionHasFacial(const std::string& dir)
	{
		// 1. 尝试从 info.json 中读取 "has_facial" 字段
		std::string err;
		jsonlite::Value root;
		if (jsonlite::ParseFile(dir + "/info.json", root, err))
		{
			if (const auto* v = root.Find("has_facial"))
			{
				return v->AsBool(false);
			}
		}

		// 2. 检查是否有 motion.morph_report.json 伴随文件
		std::error_code ec;
		if (std::filesystem::exists(std::filesystem::path(Utf8ToWide(dir)) / "motion.morph_report.json", ec))
		{
			return true;
		}

		// 3. 探查 motion.gkclip 头部（若版本 >= 4 且包含 morph 权重通道）
		const auto clipPath = std::filesystem::path(Utf8ToWide(dir)) / "motion.gkclip";
		if (std::filesystem::exists(clipPath, ec))
		{
			std::ifstream f(clipPath, std::ios::binary);
			if (f.is_open())
			{
				char magic[8]{};
				f.read(magic, 8);
				if (f.gcount() == 8 &&
					(std::memcmp(magic, "GKCLIPCL", 8) == 0 || std::memcmp(magic, "GKCLIP\0\0", 8) == 0))
				{
					uint32_t version = 0, total_frames = 0, bone_count = 0;
					float fps = 0.0f, duration_seconds = 0.0f, root_scale = 0.0f;
					f.read(reinterpret_cast<char*>(&version), 4);
					f.read(reinterpret_cast<char*>(&fps), 4);
					f.read(reinterpret_cast<char*>(&total_frames), 4);
					f.read(reinterpret_cast<char*>(&duration_seconds), 4);
					f.read(reinterpret_cast<char*>(&bone_count), 4);
					f.read(reinterpret_cast<char*>(&root_scale), 4);
					if (f && version >= 4 && bone_count <= 256 && total_frames > 0 && total_frames <= 1000000)
					{
						bool ok = true;
						for (uint32_t b = 0; b < bone_count && ok; ++b)
						{
							uint16_t hid = 0;
							uint8_t nlen = 0, hasPos = 0;
							f.read(reinterpret_cast<char*>(&hid), 2);
							f.read(reinterpret_cast<char*>(&nlen), 1);
							f.read(reinterpret_cast<char*>(&hasPos), 1);
							if (!f || !nlen) { ok = false; break; }
							f.seekg(nlen, std::ios::cur);
						}
						if (ok)
						{
							const uint64_t rotBytes = static_cast<uint64_t>(total_frames) * (sizeof(Vector3) + sizeof(Quaternion) * bone_count);
							f.seekg(rotBytes, std::ios::cur);
							uint8_t hasCam = 0;
							f.read(reinterpret_cast<char*>(&hasCam), 1);
							if (f && hasCam)
							{
								f.seekg(static_cast<uint64_t>(total_frames) * 28ull, std::ios::cur);
							}
							if (f)
							{
								uint32_t morphCount = 0;
								f.read(reinterpret_cast<char*>(&morphCount), 4);
								if (f && morphCount > 0)
								{
									return true;
								}
							}
						}
					}
				}
			}
		}
		return false;
	}

	// 模态弹窗表单状态（严格按文件类型内部拦截，支持缓存探针）
	struct LibraryModalState
	{
		bool isOpen{false};
		bool isEditMode{false}; // false = 添加, true = 管理修改已有
		std::string editTargetKey;

		char titleInput[256]{};
		std::vector<std::string> motionCandidates;
		std::vector<std::string> facialCandidates;
		std::vector<std::string> cameraCandidates;
		std::vector<std::string> audioCandidates;
		std::vector<std::string> vmdCandidates; // 保持兼容

		int motionSelection{0};
		int facialSelection{0};
		int cameraSelection{0};
		int audioSelection{0};
		// 本次挑文件中被忽略的非 WAV 音频数量（导入链只接受 PCM WAV）。
		int unsupportedAudio{0};

		std::unordered_map<std::string, VmdProbeInfo> probeCache;

		const VmdProbeInfo& GetOrProbe(const std::string& path)
		{
			static const VmdProbeInfo emptyInfo{};
			if (IsSlotPlaceholder(path))
			{
				return emptyInfo;
			}
			auto it = probeCache.find(path);
			if (it != probeCache.end()) return it->second;
			VmdProbeInfo info = ProbeCameraOrVmdFile(path);
			probeCache[path] = info;
			return probeCache[path];
		}

		void Reset()
		{
			isOpen = false;
			isEditMode = false;
			editTargetKey.clear();
			titleInput[0] = '\0';
			motionCandidates.clear();
			facialCandidates.clear();
			cameraCandidates.clear();
			audioCandidates.clear();
			vmdCandidates.clear();
			motionSelection = 0;
			facialSelection = 0;
			cameraSelection = 0;
			audioSelection = 0;
			unsupportedAudio = 0;
			probeCache.clear();
		}

		void AppendFiles(const std::vector<std::string>& files)
		{
			auto AddUnique = [](std::vector<std::string>& list, const std::string& item)
			{
				if (std::find(list.begin(), list.end(), item) == list.end())
					list.push_back(item);
			};

			for (const auto& f : files)
			{
				std::filesystem::path p(Utf8ToWide(f));
				std::string ext = p.extension().string();
				for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

				if (ext == ".vmd")
				{
					GetOrProbe(f);
					AddUnique(motionCandidates, f);
					AddUnique(facialCandidates, f);
					AddUnique(cameraCandidates, f);
					AddUnique(vmdCandidates, f);
				}
				else if (ext == ".gkcamera")
				{
					GetOrProbe(f);
					AddUnique(cameraCandidates, f);
				}
				else if (ext == ".wav")
				{
					// 只有 PCM WAV 能被导入；把 mp3/ogg/flac/m4a 列进来只会让用户
					// 选到一个必然被拒绝的文件。
					AddUnique(audioCandidates, f);
				}
			}
		}

		void SelectSingleMotion(const std::string& path)
		{
			GetOrProbe(path);
			auto it = std::find(motionCandidates.begin(), motionCandidates.end(), path);
			if (it == motionCandidates.end())
			{
				motionCandidates.push_back(path);
				motionSelection = static_cast<int>(motionCandidates.size()) - 1;
			}
			else
			{
				motionSelection = static_cast<int>(it - motionCandidates.begin());
			}
		}

		void SelectSingleFacial(const std::string& path)
		{
			GetOrProbe(path);
			auto it = std::find(facialCandidates.begin(), facialCandidates.end(), path);
			if (it == facialCandidates.end())
			{
				facialCandidates.push_back(path);
				facialSelection = static_cast<int>(facialCandidates.size()) - 1;
			}
			else
			{
				facialSelection = static_cast<int>(it - facialCandidates.begin());
			}
		}

		void SelectSingleCamera(const std::string& path)
		{
			GetOrProbe(path);
			auto it = std::find(cameraCandidates.begin(), cameraCandidates.end(), path);
			if (it == cameraCandidates.end())
			{
				cameraCandidates.push_back(path);
				cameraSelection = static_cast<int>(cameraCandidates.size()) - 1;
			}
			else
			{
				cameraSelection = static_cast<int>(it - cameraCandidates.begin());
			}
		}

		void SelectSingleAudio(const std::string& path)
		{
			auto it = std::find(audioCandidates.begin(), audioCandidates.end(), path);
			if (it == audioCandidates.end())
			{
				audioCandidates.push_back(path);
				audioSelection = static_cast<int>(audioCandidates.size()) - 1;
			}
			else
			{
				audioSelection = static_cast<int>(it - audioCandidates.begin());
			}
		}

		void SetupForNew(const std::vector<std::string>& files)
		{
			Reset();
			isOpen = true;
			isEditMode = false;

			const auto classification = ClassifyImportFiles(files);
			std::snprintf(titleInput, sizeof(titleInput), "%s", classification.suggestedTitle.c_str());

			// 新增导入：0 号项默认是"自动"，与 Python CLI 的默认行为一致（表情取
			// 动作 VMD 自带，音乐按同名 WAV / song.wav 发现）。显式关闭另设一个
			// 尾部条目，避免"没选"被翻译成"关掉"。
			motionCandidates.push_back(ImportSlot::kEmpty);
			facialCandidates.push_back(ImportSlot::kAutoFacial);
			cameraCandidates.push_back(ImportSlot::kEmpty);
			audioCandidates.push_back(ImportSlot::kAutoAudio);
			vmdCandidates.push_back(ImportSlot::kEmpty);

			AppendFiles(files);

			facialCandidates.push_back(ImportSlot::kSkip);
			audioCandidates.push_back(ImportSlot::kSkip);

			unsupportedAudio = classification.unsupportedAudioCount;

			auto FindIndex = [](const std::vector<std::string>& list, const std::string& path) -> int
			{
				if (path.empty()) return 0;
				for (size_t i = 1; i < list.size(); ++i)
				{
					if (list[i] == path) return static_cast<int>(i);
				}
				return 0;
			};

			motionSelection = FindIndex(motionCandidates, classification.motionCandidate);
			facialSelection = FindIndex(facialCandidates, classification.facialCandidate);
			cameraSelection = FindIndex(cameraCandidates, classification.cameraCandidate);
			audioSelection = FindIndex(audioCandidates, classification.audioCandidate);
		}

		void SetupForEdit(const LibraryEntry& entry)
		{
			Reset();
			isOpen = true;
			isEditMode = true;
			editTargetKey = entry.id;
			std::snprintf(titleInput, sizeof(titleInput), "%s", entry.title.c_str());

			// 动作槽位：保留当前动作
			motionCandidates.push_back(ImportSlot::kKeepMotion);
			motionSelection = 0;

			// 表情槽位
			facialCandidates.push_back(ImportSlot::kClear);
			if (entry.HasFacial())
			{
				facialCandidates.push_back(ImportSlot::kKeepTrack);
				facialSelection = 1;
			}
			else
			{
				facialSelection = 0;
			}

			// 镜头槽位
			cameraCandidates.push_back(ImportSlot::kClear);
			if (entry.HasCamera())
			{
				cameraCandidates.push_back(ImportSlot::kKeepTrack);
				cameraSelection = 1;
			}
			else
			{
				cameraSelection = 0;
			}

			// 音乐槽位
			audioCandidates.push_back(ImportSlot::kClear);
			if (entry.HasAudio())
			{
				audioCandidates.push_back(ImportSlot::kKeepTrack);
				audioSelection = 1;
			}
			else
			{
				audioSelection = 0;
			}
		}
	};

	inline LibraryModalState g_libraryModal;

	inline void SyncLibraryEntriesFromMotions(GuiOverlay& overlay, const GuiContext& ctx)
	{
		if (!ctx.motions) return;
		if (overlay.librarySyncedRevision == ctx.libraryRevision) return;
		if (overlay.selectedLibraryKey.empty() && overlay.selectedLibraryIndex >= 0 &&
			overlay.selectedLibraryIndex < static_cast<int>(overlay.libraryEntries.size()))
			overlay.selectedLibraryKey = overlay.libraryEntries[overlay.selectedLibraryIndex].id;

		overlay.libraryEntries.clear();
		for (const auto& m : *ctx.motions)
		{
			LibraryEntry e;
			e.id = m.name;
			e.title = m.title.empty() ? m.name : m.title;
			e.durationSeconds = m.durationSeconds;
			e.totalFrames = m.totalFrames;
			e.boneCount = m.boneCount;
			e.hasAudio = m.hasAudio;
			e.hasOverrides = m.hasOverrides;

			e.motionFile = !m.motionFile.empty() ? m.motionFile : (!m.dir.empty() ? m.dir : m.name) + "/motion.gkclip";

			if (m.hasAudio)
				e.audioFile = (!m.dir.empty() ? m.dir : m.name) + "/song.wav";

			e.hasCamera = m.hasCamera;
			e.cameraFile = e.hasCamera ? ((!m.dir.empty() ? m.dir : m.name) + "/camera.gkcamera") : "";

			e.hasFacial = m.hasFacial;
			if (e.hasFacial)
				e.facialFile = e.motionFile;

			overlay.libraryEntries.push_back(e);
		}
		overlay.selectedLibraryIndex = FindMotionKeyIndex(*ctx.motions, overlay.selectedLibraryKey);
		overlay.librarySyncedRevision = ctx.libraryRevision;
	}

	// 添加 / 管理条目模态弹窗
	inline void DrawAddEditModal(GuiOverlay& overlay, const GuiContext& ctx)
	{
		if (!g_libraryModal.isOpen) return;

		ImGui::OpenPopup(g_libraryModal.isEditMode ? "管理曲目##modal" : "添加到曲库##modal");

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(560, 360), ImGuiCond_Appearing);

		if (ImGui::BeginPopupModal(g_libraryModal.isEditMode ? "管理曲目##modal" : "添加到曲库##modal",
		                           &g_libraryModal.isOpen, ImGuiWindowFlags_NoResize))
		{
			auto FormatCandidateLabel = [&](const std::string& path) -> std::string
			{
				if (IsSlotPlaceholder(path))
				{
					return path;
				}
				std::filesystem::path p(Utf8ToWide(path));
				std::string filename = WideToUtf8(p.filename().wstring());
				const VmdProbeInfo& info = g_libraryModal.GetOrProbe(path);
				if (info.valid)
				{
					return filename + "  " + info.BadgeText();
				}
				return filename;
			};

			auto DrawCandidateRow = [&](const char* label, const char* comboId, int& selectedIdx,
			                            const std::vector<std::string>& candidates,
			                            int browseSlot, const wchar_t* filter, const char* browseBtnId)
			{
				ImGui::TextUnformatted(label);
				ImGui::SameLine(115.0f);

				std::string preview = (selectedIdx >= 0 && selectedIdx < static_cast<int>(candidates.size()))
					? FormatCandidateLabel(candidates[selectedIdx])
					: ImportSlot::kEmpty;

				ImGui::SetNextItemWidth(330.0f);
				if (ImGui::BeginCombo(comboId, preview.c_str()))
				{
					for (size_t i = 0; i < candidates.size(); ++i)
					{
						const bool isSelected = (selectedIdx == static_cast<int>(i));
						std::string itemText = FormatCandidateLabel(candidates[i]);
						if (ImGui::Selectable(itemText.c_str(), isSelected))
						{
							selectedIdx = static_cast<int>(i);
						}
						if (isSelected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				ImGui::SameLine();
				ImGui::BeginDisabled(g_asyncFileDialog.IsBusy());
				if (ImGui::Button(browseBtnId, ImVec2(55, 0)))
				{
					g_asyncFileDialog.Open(false, filter, browseSlot);
				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("浏览并指定文件");
			};

			ImGui::Spacing();

			// 顶部文件追加工具栏
			ImGui::BeginDisabled(g_asyncFileDialog.IsBusy());
			if (ImGui::Button("+ 追加文件...", ImVec2(105, 24)))
			{
				g_asyncFileDialog.Open(true, kAllImportFilter, -1);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("从其他文件夹追加动作、表情、镜头或音频文件");

			if (g_asyncFileDialog.IsBusy())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("（正在等待文件选择…）");
			}

			ImGui::Spacing();
			DrawCandidateRow("动作 (.vmd)", "##sel_motion", g_libraryModal.motionSelection,
			                 g_libraryModal.motionCandidates, 0, kMotionFilter, "浏览##b_mot");

			DrawCandidateRow("表情 (.vmd)", "##sel_facial", g_libraryModal.facialSelection,
			                 g_libraryModal.facialCandidates, 1, kFacialFilter, "浏览##b_fac");

			DrawCandidateRow("镜头", "##sel_camera", g_libraryModal.cameraSelection,
			                 g_libraryModal.cameraCandidates, 2, kCameraFilter, "浏览##b_cam");

			DrawCandidateRow("音乐 (音频)", "##sel_audio", g_libraryModal.audioSelection,
			                 g_libraryModal.audioCandidates, 3, kAudioFilter, "浏览##b_aud");

			if (g_libraryModal.unsupportedAudio > 0)
			{
				ImGui::TextColored(GuiTheme::WarningColor(),
					"已忽略 %d 个非 WAV 音频（当前导入链仅支持 PCM WAV）",
					g_libraryModal.unsupportedAudio);
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("演出曲目名称");
			ImGui::SameLine(115.0f);
			ImGui::SetNextItemWidth(330.0f);
			ImGui::InputText("##title_input", g_libraryModal.titleInput, sizeof(g_libraryModal.titleInput));

			// 智能校验与防呆逻辑
			std::string validationError;
			bool canConfirm = true;
			if (ctx.importBusy)
			{
				canConfirm = false;
				validationError = "正在导入，请等待完成后保存";
			}
			else if (g_libraryModal.isEditMode && (!ctx.motions || FindMotionKeyIndex(*ctx.motions, g_libraryModal.editTargetKey) < 0))
			{
				canConfirm = false;
				validationError = "原曲目已不在曲库中；表单已保留，请刷新后重试";
			}

			auto IsDiskFile = [](const std::string& p) -> bool
			{
				return !IsSlotPlaceholder(p);
			};

			std::string pickedMotion = (g_libraryModal.motionSelection >= 0 &&
				g_libraryModal.motionSelection < static_cast<int>(g_libraryModal.motionCandidates.size()))
				? g_libraryModal.motionCandidates[g_libraryModal.motionSelection] : "";

			std::string pickedFacial = (g_libraryModal.facialSelection >= 0 &&
				g_libraryModal.facialSelection < static_cast<int>(g_libraryModal.facialCandidates.size()))
				? g_libraryModal.facialCandidates[g_libraryModal.facialSelection] : "";

			std::string pickedCamera = (g_libraryModal.cameraSelection >= 0 &&
				g_libraryModal.cameraSelection < static_cast<int>(g_libraryModal.cameraCandidates.size()))
				? g_libraryModal.cameraCandidates[g_libraryModal.cameraSelection] : "";

			std::string pickedAudio = (g_libraryModal.audioSelection >= 0 &&
				g_libraryModal.audioSelection < static_cast<int>(g_libraryModal.audioCandidates.size()))
				? g_libraryModal.audioCandidates[g_libraryModal.audioSelection] : "";

			if (!g_libraryModal.isEditMode)
			{
				if (!IsDiskFile(pickedMotion))
				{
					canConfirm = false;
					validationError = "请选择有效的身体动作 (.vmd) 文件";
				}
				else
				{
					const VmdProbeInfo& info = g_libraryModal.GetOrProbe(pickedMotion);
					if (!info.CanUseAsMotion())
					{
						canConfirm = false;
						validationError = "所选动作文件不包含身体骨骼动作关键帧（不能用纯镜头或纯表情文件作为动作）";
					}
				}
			}
			else
			{
				if (IsDiskFile(pickedMotion))
				{
					const VmdProbeInfo& info = g_libraryModal.GetOrProbe(pickedMotion);
					if (!info.CanUseAsMotion())
					{
						canConfirm = false;
						validationError = "所选动作文件不包含身体骨骼动作关键帧";
					}
				}
			}

			if (canConfirm && IsDiskFile(pickedFacial))
			{
				const VmdProbeInfo& info = g_libraryModal.GetOrProbe(pickedFacial);
				if (!info.CanUseAsFacial())
				{
					canConfirm = false;
					validationError = "所选表情文件不包含表情或眼神关键帧";
				}
			}

			if (canConfirm && IsDiskFile(pickedCamera))
			{
				const VmdProbeInfo& info = g_libraryModal.GetOrProbe(pickedCamera);
				if (!info.CanUseAsCamera())
				{
					canConfirm = false;
					validationError = "所选镜头文件不包含镜头关键帧";
				}
			}

			if (canConfirm && IsDiskFile(pickedAudio))
			{
				std::string ext = WideToUtf8(std::filesystem::path(Utf8ToWide(pickedAudio)).extension().wstring());
				for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (ext != ".wav")
				{
					canConfirm = false;
					validationError = "当前音频轨道仅支持 .wav 无损格式";
				}
			}

			if (canConfirm)
			{
				bool titleEmpty = true;
				for (size_t i = 0; i < sizeof(g_libraryModal.titleInput) && g_libraryModal.titleInput[i]; ++i)
				{
					if (!std::isspace(static_cast<unsigned char>(g_libraryModal.titleInput[i])))
					{
						titleEmpty = false;
						break;
					}
				}
				if (titleEmpty)
				{
					canConfirm = false;
					validationError = "请输入有效的演出曲目名称";
				}
			}

			ImGui::Spacing();
			if (!validationError.empty())
			{
				ImGui::TextColored(GuiTheme::WarningColor(), "%s", validationError.c_str());
			}
			else
			{
				ImGui::TextUnformatted("");
			}

			ImGui::Separator();
			ImGui::Spacing();

			// 底部按钮 [取消] [添加 / 保存修改]
			if (ImGui::Button("取消", ImVec2(90, 26)))
			{
				g_libraryModal.isOpen = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);

			const char* confirmBtnText = g_libraryModal.isEditMode ? "保存修改" : "添加";
			ImGui::BeginDisabled(!canConfirm);
			if (ImGui::Button(confirmBtnText, ImVec2(90, 26)))
			{
				MotionImportRequest req{};
				req.title = g_libraryModal.titleInput[0] ? g_libraryModal.titleInput : "未命名演出";
				req.fps = 0.0f;

				if (g_libraryModal.isEditMode)
				{
					req.editKey = g_libraryModal.editTargetKey;

					if (IsDiskFile(pickedMotion))
						req.vmd = pickedMotion;

					if (pickedFacial == ImportSlot::kClear || pickedFacial == ImportSlot::kEmpty ||
						pickedFacial == ImportSlot::kSkip)
						req.disableMorph = true;
					else if (IsDiskFile(pickedFacial))
						req.morphVmd = pickedFacial;

					if (pickedCamera == ImportSlot::kClear || pickedCamera == ImportSlot::kEmpty)
						req.clearCamera = true;
					else if (IsDiskFile(pickedCamera))
						req.cameraVmd = pickedCamera;

					if (pickedAudio == ImportSlot::kClear || pickedAudio == ImportSlot::kEmpty ||
						pickedAudio == ImportSlot::kSkip)
						req.clearAudio = true;
					else if (IsDiskFile(pickedAudio))
						req.audio = pickedAudio;
				}
				else
				{
					// 三态：自动（不传开关，导入器按默认行为处理）／指定文件／
					// 显式关闭（--no-morph、--clear-audio）。
					req.vmd = pickedMotion;

					if (pickedFacial == ImportSlot::kSkip)
						req.disableMorph = true;
					else if (IsDiskFile(pickedFacial))
						req.morphVmd = pickedFacial;

					if (IsDiskFile(pickedCamera))
						req.cameraVmd = pickedCamera;

					if (pickedAudio == ImportSlot::kSkip)
						req.clearAudio = true;
					else if (IsDiskFile(pickedAudio))
						req.audio = pickedAudio;
				}

				if (ctx.fnImportMotion)
				{
					ctx.fnImportMotion(ctx.self, req);
				}

				overlay.isImporting = true;
				overlay.importProgress = 0.0f;
				overlay.importStartTime = ImGui::GetTime();
				overlay.importStatusText = "正在启动导入流程...";

				g_libraryModal.isOpen = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();

			ImGui::EndPopup();
		}
	}

	// 导入进度条与 Spinning 弹窗（与 Core importProgress 实时绑定）
	inline void DrawImportProgressModal(GuiOverlay& overlay, const GuiContext& ctx)
	{
		if (!overlay.isImporting) return;

		ImGui::OpenPopup("导入曲目中##import_progress_modal");
		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		if (ctx.importProgress != nullptr)
		{
			const auto* prog = ctx.importProgress;

			if (prog->finished)
			{
				if (prog->succeeded)
				{
					overlay.isImporting = false;
					ImGui::CloseCurrentPopup();
					return;
				}
				else
				{
					ImGui::SetNextWindowSize(ImVec2(480, 260), ImGuiCond_Appearing);
					if (ImGui::BeginPopupModal("导入曲目中##import_progress_modal", nullptr,
					                           ImGuiWindowFlags_NoResize))
					{
						ImGui::Spacing();
						ImGui::TextColored(GuiTheme::WarningColor(), "导入遇到错误：");
						ImGui::Spacing();

						std::string detail = !prog->errorDetail.empty()
							? prog->errorDetail
							: (!ctx.playlistNotice.empty() ? ctx.playlistNotice : "原生导入遇到未知错误，请查看导入日志");

						ImGui::BeginChild("##import_error_box", ImVec2(-1, 140), true);
						ImGui::TextWrapped("%s", detail.c_str());
						ImGui::EndChild();

						ImGui::Spacing();
						if (ImGui::Button("关闭", ImVec2(90, 26)))
						{
							overlay.isImporting = false;
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}
					return;
				}
			}

			// 进行中
			ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Appearing);
			if (ImGui::BeginPopupModal("导入曲目中##import_progress_modal", nullptr,
			                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
			{
				ImGui::Spacing();
				ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 14.0f);
				DrawSpinner("##import_spinner", 14.0f, 3.0f, GuiTheme::InfoColor());

				ImGui::Spacing();
				std::string st = !prog->statusText.empty()
					? prog->statusText
					: (!prog->stage.empty() ? prog->stage : "正在处理演出数据...");
				ImGui::TextColored(GuiTheme::HighlightColor(), "%s", st.c_str());

				ImGui::Spacing();
				char pctBuf[32];
				std::snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", prog->progress * 100.0f);
				ImGui::ProgressBar(prog->progress, ImVec2(-1, 18.0f), pctBuf);

				ImGui::EndPopup();
			}
		}
		else
		{
			if (!ctx.importBusy)
			{
				overlay.isImporting = false;
				ImGui::CloseCurrentPopup();
				return;
			}

			ImGui::SetNextWindowSize(ImVec2(380, 140), ImGuiCond_Appearing);
			if (ImGui::BeginPopupModal("导入曲目中##import_progress_modal", nullptr,
			                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
			{
				ImGui::Spacing();
				ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 14.0f);
				DrawSpinner("##import_spinner", 14.0f, 3.0f, GuiTheme::InfoColor());

				ImGui::Spacing();
				ImGui::TextColored(GuiTheme::HighlightColor(), "%s",
					overlay.importStatusText.empty() ? "正在导入演出曲目..." : overlay.importStatusText.c_str());

				ImGui::Spacing();
				ImGui::ProgressBar(0.5f, ImVec2(-1, 18.0f), "处理中...");

				ImGui::EndPopup();
			}
		}
	}

	inline void DrawLibraryPage(GuiOverlay& overlay, const GuiContext& ctx)
	{
		SyncLibraryEntriesFromMotions(overlay, ctx);

		// 处理后台异步文件选择器回调结果
		std::vector<std::string> pickedFiles;
		int pickedSlot = -1;
		if (g_asyncFileDialog.Poll(pickedFiles, pickedSlot))
		{
			if (!pickedFiles.empty())
			{
				if (pickedSlot == -1)
				{
					if (g_libraryModal.isOpen)
						g_libraryModal.AppendFiles(pickedFiles);
					else
						g_libraryModal.SetupForNew(pickedFiles);
				}
				else if (pickedSlot == 0)
				{
					g_libraryModal.SelectSingleMotion(pickedFiles.front());
				}
				else if (pickedSlot == 1)
				{
					g_libraryModal.SelectSingleFacial(pickedFiles.front());
				}
				else if (pickedSlot == 2)
				{
					g_libraryModal.SelectSingleCamera(pickedFiles.front());
				}
				else if (pickedSlot == 3)
				{
					g_libraryModal.SelectSingleAudio(pickedFiles.front());
				}
			}
		}

		// ---- 顶部操作栏：空间不足时换行，保持刷新与管理入口可见 ----
		auto ContinueToolbar = [](float nextWidth) {
			const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
			if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth <= right)
				ImGui::SameLine();
		};
		ImGui::BeginDisabled(ctx.libraryRefreshing || ctx.importBusy || !ctx.fnRefreshMotions);
		if (ImGui::Button(ctx.libraryRefreshing ? "正在刷新..." : "刷新曲库", ImVec2(100, 26)))
		{
			if (ctx.fnRefreshMotions && ctx.fnRefreshMotions(ctx.self)) overlay.libraryUiNotice.clear();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("重新扫描外部新增、删除和修改的曲目；不重新加载当前表演");
		ContinueToolbar(140);
		if (ImGui::Button("打开曲库文件夹", ImVec2(140, 26)))
		{
			const auto folder = Utf8ToWide(overlay.motionsRoot);
			const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
			overlay.libraryUiNotice = result <= 32 ? "无法打开曲库文件夹，请检查目录是否存在（错误 " + std::to_string(result) + "）" : "";
		}
		if (ctx.fnReloadMotion)
		{
			ContinueToolbar(160);
			ImGui::BeginDisabled(ctx.loadedMotionKey.empty() || ctx.loadedSourceMissing || ctx.importBusy || ctx.libraryRefreshing);
			if (ImGui::Button("重新加载当前曲目", ImVec2(160, 26))) ctx.fnReloadMotion(ctx.self);
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("载入当前曲目的磁盘更新；保留时间并暂停，点击继续后播放");
		}
		if (!ctx.libraryNotice.empty()) ImGui::TextWrapped("%s", ctx.libraryNotice.c_str());
		if (!ctx.playlistNotice.empty() && ctx.playlistNotice != ctx.libraryNotice)
		{
			const bool failed = ctx.playlistNotice.find("失败") != std::string::npos || ctx.playlistNotice.find("错误") != std::string::npos;
			ImGui::PushStyleColor(ImGuiCol_Text, failed ? GuiTheme::ErrorColor() : GuiTheme::InfoColor());
			ImGui::TextWrapped("%s", ctx.playlistNotice.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::PushStyleColor(ImGuiCol_Text, GuiTheme::WarningColor());
		if (!overlay.libraryUiNotice.empty()) ImGui::TextWrapped("%s", overlay.libraryUiNotice.c_str());
		if (ctx.loadedSourceMissing) ImGui::TextWrapped("当前表演的源文件已移除；已加载内容保留，无法重新加载。");
		else if (ctx.loadedSourceChanged) ImGui::TextWrapped("当前曲目的文件已更新，待重新加载。");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		ImGui::BeginDisabled(g_asyncFileDialog.IsBusy() || ctx.importBusy);
		if (ImGui::Button("+ 添加到曲库", ImVec2(100, 26)))
		{
			g_asyncFileDialog.Open(true, kAllImportFilter, -1);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("选择 .vmd 动作/表情/镜头或音频文件添加到曲库");

		ContinueToolbar(85);

		const bool hasSelection = (overlay.selectedLibraryIndex >= 0 &&
		                           overlay.selectedLibraryIndex < static_cast<int>(overlay.libraryEntries.size()));

		ImGui::BeginDisabled(!hasSelection || ctx.importBusy || ctx.libraryRefreshing);
		if (ImGui::Button("管理选中", ImVec2(85, 26)))
		{
			if (hasSelection)
			{
				g_libraryModal.SetupForEdit(overlay.libraryEntries[overlay.selectedLibraryIndex]);
			}
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("修改选中的曲目配置与文件映射");

		ContinueToolbar(120);
		if (ImGui::Button("从列表移除", ImVec2(120, 26)) && hasSelection)
		{
			const auto& entry = overlay.libraryEntries[overlay.selectedLibraryIndex];
			overlay.PlaylistAction(ctx, GuiPlaylistAction::Remove, entry.id.c_str());
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("隐藏列表条目，保留全部本地文件；可用“恢复已移除曲目”找回");
		ContinueToolbar(120);
		ImGui::BeginDisabled(!ctx.fnPlaylistAction);
		if (ImGui::Button("删除本地文件", ImVec2(120, 26)) && hasSelection)
			overlay.RequestMotionDelete(ctx, overlay.libraryEntries[overlay.selectedLibraryIndex].id.c_str());
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("确认后将整个曲目目录移入可恢复区，文件不会永久删除");
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ContinueToolbar(140);
		ImGui::BeginDisabled(ctx.importBusy || ctx.libraryRefreshing || !ctx.fnPlaylistAction);
		if (ImGui::Button("恢复已移除曲目", ImVec2(140, 26))) overlay.PlaylistAction(ctx, GuiPlaylistAction::Restore);
		ContinueToolbar(140);
		if (ImGui::Button("恢复最近删除", ImVec2(140, 26))) overlay.PlaylistAction(ctx, GuiPlaylistAction::RestoreDeleted);
		ImGui::EndDisabled();
		overlay.DrawMotionDeleteConfirmation(ctx);

		// 搜索独占一行，避免窄窗口把操作按钮挤出内容区域。
		overlay.libraryFilter.Draw("搜索曲目", (std::max)(80.0f, ImGui::GetContentRegionAvail().x - 75.0f));
		if (ImGui::IsItemHovered() && overlay.libraryFilter.InputBuf[0] == '\0')
			ImGui::SetTooltip("输入关键字过滤曲库列表");
		if (overlay.libraryFilter.IsActive())
		{
			ContinueToolbar(55);
			if (ImGui::Button("清空")) overlay.libraryFilter.Clear();
		}
		if (hasSelection)
		{
			const auto& selected = overlay.libraryEntries[overlay.selectedLibraryIndex];
			ImGui::TextWrapped("选中：%s", selected.title.c_str());
			ImGui::BeginDisabled(ctx.importBusy || ctx.libraryRefreshing || !ctx.fnSelectMotion);
			if (ImGui::Button("加载预览")) ctx.fnSelectMotion(ctx.self, overlay.selectedLibraryIndex, false);
			ContinueToolbar(85);
			if (ImGui::Button("播放选中")) ctx.fnSelectMotion(ctx.self, overlay.selectedLibraryIndex, true);
			ImGui::EndDisabled();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// ---- 曲库表格 ----
		// 严格区分三个状态：
		//   Selected: 曲库当前选中条目 (overlay.selectedLibraryIndex)
		//   Loaded:   当前实际加载到 runtime 的稳定曲目 key
		//   Playing:  当前真正正在播放的条目 (ctx.player->state == PlaybackState::Playing)
		const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
		                                   ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
		std::vector<int> visibleIndices;
		for (size_t i = 0; i < overlay.libraryEntries.size(); ++i)
		{
			const auto& entry = overlay.libraryEntries[i];
			if (overlay.libraryFilter.PassFilter(entry.title.c_str()) || overlay.libraryFilter.PassFilter(entry.id.c_str()))
				visibleIndices.push_back(static_cast<int>(i));
		}
		ImGui::TextDisabled("显示 %zu / %zu 个曲目", visibleIndices.size(), overlay.libraryEntries.size());
		if (overlay.libraryEntries.empty()) ImGui::TextWrapped("曲库为空。添加动作，或将动作文件夹放入曲库后点击“刷新曲库”。");
		else if (visibleIndices.empty()) ImGui::TextWrapped("没有匹配的曲目，可清空搜索后查看全部曲目。");

		const float tableHeight = ImGui::GetContentRegionAvail().y - 8.0f;
		if (ImGui::BeginTable("##gkmotion_library_table", 4, tableFlags, ImVec2(0.0f, (std::max)(100.0f, tableHeight))))
		{
			ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 42.0f);
			ImGui::TableSetupColumn("演出曲目", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("包含内容", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("时长", ImGuiTableColumnFlags_WidthFixed, 55.0f);
			ImGui::TableHeadersRow();

			// 使用 ImGuiListClipper 优化大列表虚拟渲染
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(visibleIndices.size()));

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const int idx = visibleIndices[row];
					const auto& entry = overlay.libraryEntries[idx];

					const bool isSelected = (overlay.selectedLibraryIndex == idx);

					bool isLoaded = false;
					bool isPlaying = false;
					if (ctx.player)
					{
						isLoaded = !ctx.loadedMotionKey.empty() ? ctx.loadedMotionKey == entry.id
							: MotionPathEqualsUtf8(ctx.player->loadedMotionFile, entry.motionFile);
						isPlaying = isLoaded && (ctx.player->state == PlaybackState::Playing);
					}

					ImGui::TableNextRow();

					// 列 0：播放状态
					ImGui::TableSetColumnIndex(0);
					if (isPlaying)
						ImGui::TextColored(GuiTheme::SuccessColor(), "[播]");
					else if (isLoaded && ctx.player && ctx.player->state == PlaybackState::Paused)
						ImGui::TextColored(GuiTheme::WarningColor(), "[暂]");
					else if (isLoaded && ctx.player && ctx.player->state == PlaybackState::Preview)
						ImGui::TextColored(GuiTheme::InfoColor(), "[预]");
					else if (isLoaded)
						ImGui::TextDisabled("[载]");
					else
						ImGui::TextUnformatted("");

					// 列 1：演出曲目（支持单击选中，双击播放）
					ImGui::TableSetColumnIndex(1);
					ImGui::PushID(entry.id.c_str());

					const ImGuiSelectableFlags selectFlags = ImGuiSelectableFlags_SpanAllColumns |
					                                         ImGuiSelectableFlags_AllowDoubleClick;

					if (ImGui::Selectable("##row", isSelected, selectFlags))
					{
						overlay.selectedLibraryIndex = idx;
						overlay.selectedLibraryKey = entry.id;

						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						{
							if (ctx.fnSelectMotion && !ctx.importBusy && !ctx.libraryRefreshing)
							{
								ctx.fnSelectMotion(ctx.self, idx, true /* autoplay */);
							}
						}
					}
					ImGui::PopID();

					ImGui::SameLine(0.0f, 0.0f);
					if (isPlaying)
						ImGui::TextColored(GuiTheme::SuccessColor(), "%s", entry.title.c_str());
					else
						ImGui::TextUnformatted(entry.title.c_str());

					// 列 2：包含标签
					ImGui::TableSetColumnIndex(2);
					if (entry.HasMotion())
					{
						ImGui::TextColored(GuiTheme::TagMotionColor(), "[动作]");
						ImGui::SameLine();
					}
					if (entry.HasFacial())
					{
						ImGui::TextColored(GuiTheme::TagFacialColor(), "[表情]");
						ImGui::SameLine();
					}
					if (entry.HasCamera())
					{
						ImGui::TextColored(GuiTheme::TagCameraColor(), "[镜头]");
						ImGui::SameLine();
					}
					if (entry.HasAudio())
					{
						ImGui::TextColored(GuiTheme::TagAudioColor(), "[音乐]");
						ImGui::SameLine();
					}

					// 列 3：时长
					ImGui::TableSetColumnIndex(3);
					char durBuf[16];
					FormatTimeMmSs(entry.durationSeconds, durBuf, sizeof(durBuf));
					ImGui::TextDisabled("%s", durBuf);
				}
			}
			clipper.End();
			ImGui::EndTable();
		}

		// 模态弹窗与导入动画渲染
		DrawAddEditModal(overlay, ctx);
		DrawImportProgressModal(overlay, ctx);
	}
} // namespace GakuMotion
#endif
