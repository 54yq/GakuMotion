#pragma once

static void TestWaistSupportCore()
{
	BeginTest("waist_support_default_on_and_main_thread_control");
	Core core;
	Check(core.player.controller.IsExperimentalWaistSupportEnabled() && core.guiExperimentalWaistSupport.load(), "waist support starts enabled and published");
	core.QueueExperimentalWaistSupport(false);
	Check(core.player.controller.IsExperimentalWaistSupportEnabled(), "queued disable waits for main thread");
	core.ProcessPendingGuiCommands();
	core.QueueExperimentalWaistSupport(true);
	Check(!core.player.controller.IsExperimentalWaistSupportEnabled(), "GUI queues without Unity-side mutation");
	core.ProcessPendingGuiCommands();
	Check(core.player.controller.IsExperimentalWaistSupportEnabled() && core.guiExperimentalWaistSupport.load(),
	      "main-thread consumption enables and publishes waist support");
	core.QueueExperimentalWaistSupport(false);
	core.ProcessPendingGuiCommands();
	Check(!core.player.controller.IsExperimentalWaistSupportEnabled(), "queued disable restores opt-out state");

	const auto directory = std::filesystem::temp_directory_path() / "gaku_waist_config_test";
	const auto path = directory / "control.json";
	RuntimeConfig config;
	config.path = path.string();
	Check(config.experimentalWaistSupport == 1, "missing declaration defaults to enabled");
	WriteTextFile(path, R"({"experimental_waist_support":{"enabled":true}})");
	Check(config.Reload() && config.experimentalWaistSupport == 1, "typed opt-in declaration loads");
	WriteTextFile(path, R"({"experimental_waist_support":{"enabled":"false"}})");
	Check(config.Reload() && config.experimentalWaistSupport == 1,
	      "invalid boolean string cannot silently change state");
	WriteTextFile(path, R"({"command":{"seq":77,"pause":true}})");
	Check(config.Reload() && config.experimentalWaistSupport == 1,
	      "unrelated transport reload preserves session setting");
	WriteTextFile(path, R"({"experimental_waist_support":{"enabled":false}})");
	Check(config.Reload() && config.experimentalWaistSupport == 0, "explicit opt-out declaration loads");
	RuntimeConfig defaults;
	const auto templatePath = directory / "template.json";
	WriteTextFile(templatePath, defaults.DefaultTemplate());
	defaults.path = templatePath.string();
	Check(defaults.Reload() && defaults.experimentalWaistSupport == 1,
	      "generated startup template enables waist support");
	std::filesystem::remove(path);
	std::filesystem::remove(templatePath);
	std::filesystem::remove(directory);
}
