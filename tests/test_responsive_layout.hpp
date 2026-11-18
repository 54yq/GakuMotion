// Included by test_gui_ux.cpp after Harness. These inspect actual ImGui windows,
// including the host's scroll range, rather than relying on hidden scrollbars.
ImGuiWindow* FindResponsiveWindow(const char* fragment)
{
	for (ImGuiWindow* window : GImGui->Windows)
		if (window->Active && std::string_view(window->Name).find(fragment) != std::string_view::npos) return window;
	return nullptr;
}

void TestResponsiveLibraryAndStageLayout(const std::filesystem::path& root)
{
	auto& io = ImGui::GetIO();
	const auto originalDisplay = io.DisplaySize;
	ImFont* originalFont = io.FontDefault;
	wchar_t windowsDirectory[MAX_PATH]{};
	const UINT windowsLength = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
	const auto cjkPath = std::filesystem::path(windowsDirectory) / L"Fonts" / L"msyh.ttc";
	if (windowsLength && windowsLength < MAX_PATH && std::filesystem::exists(cjkPath))
	{
		io.FontDefault = io.Fonts->AddFontFromFileTTF(cjkPath.string().c_str(), 18.0f, nullptr,
			io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	}
	const ImGuiStyle originalStyle = ImGui::GetStyle();
	io.DisplaySize = ImVec2(1920, 1080);
	for (const float scale : {1.0f, 1.5f})
	{
		ImGui::GetStyle() = originalStyle;
		ImGui::GetStyle().ScaleAllSizes(scale);
		Harness app(root);
		app.fontScale = scale;
		app.windowWidth = 1500.0f;
		app.windowHeight = 940.0f;
		app.Settle();
		auto* host = ImGui::FindWindowByName("UX fixture");
		auto* transport = app.TransportWindow();
		Check(host && host->ScrollMax.y <= .5f && host->ScrollMax.x <= .5f && transport &&
			transport->Pos.y + transport->Size.y <= host->InnerClipRect.Max.y + .5f,
			"library shell fits the real host at normal and scaled style spacing without outer scrolling");

		app.Click("管理选中");
		app.Settle();
		auto* modal = FindResponsiveWindow("管理曲目##modal");
		auto* body = FindResponsiveWindow("/##library_modal_body_");
		ImRect cancel, save;
		const bool buttons = app.Find("取消", cancel) && app.Find("保存修改", save);
		Check(modal && body && modal->ScrollMax.y <= .5f && body->ScrollMax.y <= .5f && buttons &&
			cancel.Min.y >= modal->InnerClipRect.Min.y - .5f && save.Max.y <= modal->InnerClipRect.Max.y + .5f,
			"font-sized import form fits without scrolling and keeps both footer actions visible");
		auto* timingTable = FindResponsiveWindow("/##library_file_timing_");
		Check(timingTable && timingTable->ScrollMax.y <= .5f,
			"all four file timing rows fit inside the import table without vertical scrolling");
		app.Click("取消");

		app.core.gui.activePage = GuiPage::Stage;
		app.contextPatch = [](GuiContext& context) {
			context.stageSettings.customNodeControl = true;
			context.stageCandidates.resize(100);
			for (size_t i = 0; i < context.stageCandidates.size(); ++i)
				context.stageCandidates[i].name = "Synthetic scene node " + std::to_string(i);
		};
		app.Settle();
		if (auto* page = FindResponsiveWindow("/##gkmotion_main_content_")) page->StateStorage.Clear();
		app.Settle();
		app.Click("高级场景编辑");
		app.Settle();
		auto* tree = FindResponsiveWindow("/##custom_node_tree_");
		auto* page = FindResponsiveWindow("/##gkmotion_main_content_");
		const float shortTreeHeight = tree ? tree->Size.y : 0.0f;
		Check(tree && page && shortTreeHeight > 0.0f && tree->ScrollMax.y > 0.0f && page->ScrollMax.y <= .5f,
			"scene nodes scroll inside their tree while the enclosing stage page fits");
		app.windowHeight += 100.0f;
		app.Settle();
		tree = FindResponsiveWindow("/##custom_node_tree_");
		page = FindResponsiveWindow("/##gkmotion_main_content_");
		host = ImGui::FindWindowByName("UX fixture");
		Check(tree && page && host && std::fabs(tree->Size.y - shortTreeHeight - 100.0f) <= 1.0f &&
			page->ScrollMax.y <= .5f && host->ScrollMax.y <= .5f,
			"scene node tree stretches by the host height change without adding outer scrollbars");
	}
	ImGui::GetStyle() = originalStyle;
	{
		Harness app(root);
		io.DisplaySize = ImVec2(760, 280);
		app.windowWidth = 720.0f;
		app.windowHeight = 270.0f;
		app.Settle();
		g_libraryModal.SetupForEdit(app.core.gui.libraryEntries.front());
		app.Settle();
		auto* modal = FindResponsiveWindow("管理曲目##modal");
		auto* body = FindResponsiveWindow("/##library_modal_body_");
		ImRect cancel, save;
		const bool buttons = app.Find("取消", cancel) && app.Find("保存修改", save);
		Check(modal && body && modal->Size.y <= 248.5f && modal->ScrollMax.y <= .5f && body->ScrollMax.y > 0.0f &&
			buttons && cancel.Min.y >= modal->InnerClipRect.Min.y - .5f && save.Max.y <= modal->InnerClipRect.Max.y + .5f,
			"short viewport scrolls the form body while retaining the complete action row");
		app.Click("取消");
	}
	io.DisplaySize = originalDisplay;
	io.FontDefault = originalFont;
	io.FontGlobalScale = 1.0f;
}
