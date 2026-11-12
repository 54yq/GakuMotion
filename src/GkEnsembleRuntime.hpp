#pragma once

#include "GkEnsemble.hpp"
#include "GkMotionPlayer.hpp"
#include "GkSceneActors.hpp"
#include <array>
#include <memory>

namespace GakuMotion
{
	// The main player owns audio, transport and camera. Each follower owns a
	// separate clip and controller; it never opens audio or advances a clock.
	class EnsembleRuntime
	{
	public:
		static constexpr size_t kFollowerCount = EnsembleAssignments::kMaxParts - 1;
		struct Follower
		{
			uint64_t actorToken{0};
			std::string motionKey;
			float offsetSeconds{0};
			bool active{false};
			std::unique_ptr<MotionPlayer> player;
			BoneOverrideSet motionOverrides;
			BoneOverrideSet overrides;
		};

		const std::array<Follower, kFollowerCount>& Followers() const { return followers_; }
		bool RestoreUnconfirmed() const { return restoreUnconfirmed_; }
		bool HasFollowers() const
		{
			for (const auto& follower : followers_) if (follower.player) return true;
			return false;
		}
		bool HasActiveFollowers() const
		{
			for (const auto& follower : followers_) if (follower.active) return true;
			return false;
		}

		// A missing or replaced actor is never touched during cleanup. If it was
		// active, retain its snapshot and fail closed until the scene is discarded.
		bool Clear(size_t index, const SceneActorRegistry& registry)
		{
			if (index >= kFollowerCount) return false;
			auto& follower = followers_[index];
			if (follower.player)
			{
				void* actor = registry.Resolve(follower.actorToken, registry.Epoch());
				if (follower.active && (!actor || actor != follower.player->controller.targetActor))
				{ restoreUnconfirmed_ = true; return false; }
				if (follower.active && actor && actor == follower.player->controller.targetActor)
				{
					auto result = follower.player->controller.RestoreForCapture();
					if (!result.bodyConfirmed || !result.faceConfirmed || !result.gazeConfirmed)
					{ restoreUnconfirmed_ = true; return false; }
				}
				follower.player->controller.DiscardBindings();
			}
			follower = {};
			assignments_.Clear(index + 1);
			return true;
		}

		bool ClearAll(const SceneActorRegistry& registry)
		{
			bool ok = true;
			for (size_t i = 0; i < kFollowerCount; ++i) ok = Clear(i, registry) && ok;
			return ok;
		}

		// Use only after scene ownership is lost: Unity objects can be destroyed.
		void Abandon()
		{
			for (auto& follower : followers_) follower = {};
			assignments_.Reset();
			restoreUnconfirmed_ = false;
		}

		bool Set(size_t index, uint64_t actorToken, uint64_t epoch, const MotionEntry& motion,
		         float offset, const SceneActorRegistry& registry, const MotionPlayer& primary,
		         const BoneOverrideSet& global, bool overridesEnabled)
		{
			if (index >= kFollowerCount || !primary.controller.targetActor || !std::isfinite(offset) || motion.name.empty() ||
			    primary.state == PlaybackState::Playing || primary.state == PlaybackState::Paused) return false;
			void* actor = registry.Resolve(actorToken, epoch);
			if (!actor || actor == primary.controller.targetActor) return false;
			for (size_t i = 0; i < kFollowerCount; ++i)
				if (i != index && followers_[i].actorToken == actorToken) return false;
			auto nextAssignments = assignments_;
			if (!nextAssignments.Set(index + 1, actorToken, epoch, motion.name, offset)) return false;

			auto next = std::make_unique<MotionPlayer>();
			// Copy service callbacks and settings, then discard copied ownership.
			next->controller = primary.controller;
			next->controller.DiscardBindings();
			next->controller.SetTargetActor(actor);
			next->controller.initialPositionOffset = {};
			next->controller.auxGroundTruth = {};
			if (!next->LoadMotion(motion.dir)) return false;
			next->timing = motion.timing;
			BoneOverrideSet motionOverrides, effective;
			const auto file = motion.dir + "/bone_overrides.json";
			std::error_code error;
			const bool hasOverrides = std::filesystem::exists(MotionPathFromUtf8(file), error);
			if (error || (hasOverrides && !motionOverrides.LoadFromFile(file))) return false;
			BoneOverrideSet::BuildEffective(global, motionOverrides, overridesEnabled, effective);
			if (!Clear(index, registry)) return false;
			auto& follower = followers_[index];
			follower.actorToken = actorToken;
			follower.motionKey = motion.name;
			follower.offsetSeconds = offset;
			follower.motionOverrides = std::move(motionOverrides);
			follower.overrides = std::move(effective);
			follower.player = std::move(next);
			follower.player->controller.SetBoneOverrides(&follower.overrides);
			assignments_ = std::move(nextAssignments);
			return true;
		}

		void RebuildOverrides(const BoneOverrideSet& global, bool enabled)
		{
			for (auto& follower : followers_)
				if (follower.player)
					BoneOverrideSet::BuildEffective(global, follower.motionOverrides, enabled, follower.overrides);
		}

		// Returns false when restoration could not be verified. For capture callers
		// this must prevent the host from proceeding with a native capture.
		bool RestoreLive(const SceneActorRegistry& registry)
		{
			bool ok = true;
			for (auto& follower : followers_)
			{
				if (!follower.player || !follower.active) continue;
				void* actor = registry.Resolve(follower.actorToken, registry.Epoch());
				if (!actor || actor != follower.player->controller.targetActor)
				{ restoreUnconfirmed_ = true; ok = false; continue; }
				const auto result = follower.player->controller.RestoreForCapture();
				const bool restored = result.bodyConfirmed && result.faceConfirmed && result.gazeConfirmed;
				if (restored) follower.active = false;
				else restoreUnconfirmed_ = true;
				ok &= restored;
			}
			return ok;
		}

		void Tick(void* actor, void* animator, void* director, const MotionPlayer& primary,
		          const SceneActorRegistry& registry)
		{
			if (!actor || primary.state == PlaybackState::Stopped) return;
			for (auto& follower : followers_)
			{
				if (!follower.player || registry.Resolve(follower.actorToken, registry.Epoch()) != actor) continue;
				auto& player = *follower.player;
				const auto& ctrl = player.controller;
				if (!animator || !ctrl.fnGetWorldPosition || !ctrl.fnSetWorldPosition || !ctrl.fnGetWorldRotation ||
				    !ctrl.fnSetWorldRotation || !ctrl.fnGetAnimatorController || !ctrl.fnSetAnimatorController ||
				    !ctrl.fnGetAnimatorEnabled || !ctrl.fnSetAnimatorEnabled)
				{
					if (follower.active) restoreUnconfirmed_ = true;
					continue;
				}
				player.state = primary.state;
				player.controller.facialWeights = primary.controller.facialWeights;
				player.controller.auxDriveMode = primary.controller.auxDriveMode;
				player.currentTime = (std::max)(0.0f, primary.currentTime - follower.offsetSeconds);
				player.Tick(0.0f, actor, animator, director, false);
				follower.active = player.controller.bindings.find(actor) != player.controller.bindings.end();
			}
		}

	private:
		std::array<Follower, kFollowerCount> followers_{};
		EnsembleAssignments assignments_;
		bool restoreUnconfirmed_{false};
	};
}
