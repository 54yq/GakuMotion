#pragma once

#include <atomic>
#include <string>

namespace GakuMotion
{
	enum class GazeMode
	{
		Auto, // Defer to VRify if active, else use Desktop LookAt
		ForceOff, // Strictly follow MMD dance head motion
		DesktopOn // Enable Desktop Camera LookAt
	};

	class LookAtManager
	{
	public:
		std::atomic<GazeMode> gazeMode{GazeMode::Auto};
		std::atomic<bool> desktopLookAtEnabled{true};
		std::atomic<float> eyeWeight{0.7f};
		std::atomic<float> headWeight{0.4f};
		std::atomic<float> bodyWeight{0.0f};

		// Callback to query VRify state dynamically
		using QueryVRifyLiveGazeFn = bool (*)();
		QueryVRifyLiveGazeFn fnQueryVRifyGaze{nullptr};

		// Callbacks to interact with CampusActorLookAtController
		using SetLookAtTargetFn = void (*)(void* lookAtController, void* cameraTransform);
		using SetLookAtWeightsFn = void (*)(void* lookAtController, float eye, float head, float body);
		using SetLookAtEnabledFn = void (*)(void* lookAtController, bool enabled);

		SetLookAtTargetFn fnSetTarget{nullptr};
		SetLookAtWeightsFn fnSetWeights{nullptr};
		SetLookAtEnabledFn fnSetEnabled{nullptr};

		bool IsVRifyLiveGazeActive() const noexcept
		{
			if (fnQueryVRifyGaze)
			{
				return fnQueryVRifyGaze();
			}
			return false;
		}

		void Update(void* lookAtController, void* mainCameraTransform)
		{
			if (!lookAtController) return;

			// 1. Check if VRify LiveGaze is active
			if (gazeMode.load() == GazeMode::Auto && IsVRifyLiveGazeActive())
			{
				// VRify handles gaze completely, do not interfere
				return;
			}

			// 2. Force Off: Disable LookAt to preserve full VMD head motion
			if (gazeMode.load() == GazeMode::ForceOff || !desktopLookAtEnabled.load())
			{
				if (fnSetEnabled) fnSetEnabled(lookAtController, false);
				return;
			}

			// 3. Desktop LookAt: Aim at main camera
			if (mainCameraTransform && fnSetTarget && fnSetWeights && fnSetEnabled)
			{
				fnSetEnabled(lookAtController, true);
				fnSetTarget(lookAtController, mainCameraTransform);
				fnSetWeights(lookAtController, eyeWeight.load(), headWeight.load(), bodyWeight.load());
			}
		}
	};
} // namespace GakuMotion
