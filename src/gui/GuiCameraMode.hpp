#pragma once

#include "../GkCamera.hpp"

namespace GakuMotion
{
	// Both camera entry points use this user-facing selection. Hold remains a
	// legacy runtime value and is represented as VMD until settings migrate.
	inline constexpr const char* kCameraControlModes[] = {"游戏", "VMD", "跟随", "注视"};
	inline constexpr const char* kCameraControlQuickModes[] = {"镜头: 游戏", "镜头: VMD", "镜头: 跟随", "镜头: 注视"};

	inline int CameraControlModeIndex(const CameraSettings& settings) noexcept
	{
		if (!settings.enabled) return 0;
		if (settings.mode != CameraMode::Follow) return 1;
		return settings.followBehavior == FollowBehavior::LookAt ? 3 : 2;
	}

	inline bool SetCameraControlMode(CameraSettings& settings, int selection) noexcept
	{
		if (selection < 0 || selection > 3) return false;
		settings.enabled = selection != 0;
		// Returning to the game's camera releases ownership and retains GM
		// parameters for the next explicit selection.
		if (selection == 0) return true;
		settings.mode = selection == 1 ? CameraMode::Track : CameraMode::Follow;
		settings.followActor = selection != 1;
		if (selection == 2) settings.followBehavior = FollowBehavior::FollowMove;
		if (selection == 3) settings.followBehavior = FollowBehavior::LookAt;
		return true;
	}
}
