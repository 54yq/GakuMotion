#pragma once

#include "GkEnsemble.hpp"
#include "GkMotionPlayer.hpp"
#include "GkSceneActors.hpp"
#include <deque>
#include <memory>
#include <unordered_map>

namespace GakuMotion
{
	// The main player owns audio, transport and camera. Each follower owns a
	// separate clip and controller; it never opens audio or advances a clock.
	class EnsembleRuntime
	{
	public:

		struct Placement { Vector3 position{}; float yawDegrees{0}; };
		struct ArmCalibration { float share{0.0f}; float degrees{45.0f}; bool locked{false}; };
		struct Follower
		{
			uint64_t actorToken{0};
			uint64_t bindingGeneration{0};
			std::string motionKey;
			float offsetSeconds{0};
			bool active{false};
			bool armLocked{false};
			std::unique_ptr<MotionPlayer> player;
			BoneOverrideSet motionOverrides;
			BoneOverrideSet overrides;
		};

		const std::deque<Follower>& Followers() const { return followers_; }
		void EnsureSlots(size_t count) { if (count <= 64 && followers_.size() < count) followers_.resize(count); }
		// The runtime releases secondary-physics ownership before a controller
		// can be restored or destroyed. A failed release retains the follower.
		bool (*fnBeforeRestore)(size_t, MotionPlayer&){nullptr};
		bool (*fnReleaseDetached)(size_t){nullptr};
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
		bool GetPlacement(uint64_t actorToken, Placement& result) const
		{
			const auto found = placements_.find(actorToken);
			if (found == placements_.end()) return false;
			result = found->second;
			return true;
		}
		void RememberPlacement(uint64_t actorToken, const Vector3& position, float yawDegrees)
		{
			if (actorToken && std::isfinite(position.x) && std::isfinite(position.y) &&
			    std::isfinite(position.z) && std::isfinite(yawDegrees))
				placements_[actorToken] = {position, yawDegrees};
		}

		bool GetArmCalibration(uint64_t token, ArmCalibration& result) const
		{
			const auto found = armCalibrations_.find(token);
			if (found == armCalibrations_.end()) return false;
			result = found->second;
			return true;
		}
		void RememberArmCalibration(uint64_t token, const ArmCalibration& value)
		{
			if (token) armCalibrations_[token] = value;
		}
		bool SetSceneArmCalibration(uint64_t token, uint64_t epoch, int operation, float value,
		                            const SceneActorRegistry& registry)
		{
			void* actor = registry.Resolve(token, epoch);
			if (!actor || restoreUnconfirmed_) return false;
			ArmCalibration calibration;
			GetArmCalibration(token, calibration);
			if (operation == 3) calibration.locked = value != 0.0f;
			else if (operation == 0 && std::isfinite(value) && value >= -1.0f && value <= 1.0f) calibration.share = value;
			else if (operation == 1 && !calibration.locked && std::isfinite(value) && value >= -90.0f && value <= 90.0f) calibration.degrees = value;
			else if (operation == 2 && !calibration.locked) { calibration.share = 0.0f; calibration.degrees = 45.0f; }
			else return false;
			armCalibrations_[token] = calibration;
			for (auto& follower : followers_)
				if (follower.player && follower.actorToken == token && follower.player->controller.targetActor == actor)
				{
					follower.player->controller.wristTwistShare = calibration.share;
					follower.player->controller.forearmAxisAdjustmentDeg = calibration.degrees;
					follower.armLocked = calibration.locked;
				}
			return true;
		}

		// A missing or replaced actor is never touched during cleanup. If it was
		// active, retain its snapshot and fail closed until the scene is discarded.
		bool RestoreSlot(size_t index, const SceneActorRegistry& registry)
		{
			if (index >= followers_.size()) return false;
			auto& follower = followers_[index];
			if (follower.player)
			{
				void* actor = registry.Resolve(follower.actorToken, registry.Epoch());
				if (follower.active && (!actor || actor != follower.player->controller.targetActor))
				{ restoreUnconfirmed_ = true; return false; }
				if (fnBeforeRestore && !fnBeforeRestore(index, *follower.player))
				{ restoreUnconfirmed_ = true; return false; }
				if (follower.active && actor && actor == follower.player->controller.targetActor)
				{
					auto result = follower.player->controller.RestoreForCapture();
					if (!result.bodyConfirmed || !result.faceConfirmed || !result.gazeConfirmed)
					{ restoreUnconfirmed_ = true; return false; }
				}
				follower.player->controller.DiscardBindings();
				follower.active = false;
			}
			return true;
		}

		bool Clear(size_t index, const SceneActorRegistry& registry)
		{
			if (!RestoreSlot(index, registry)) return false;
			followers_[index] = {};
			return true;
		}

		bool HasMissingActor(size_t index, const SceneActorRegistry& registry) const
		{
			return index < followers_.size() && followers_[index].player &&
			    !registry.Resolve(followers_[index].actorToken, registry.Epoch());
		}

		bool ClearMissingActor(size_t index, const SceneActorRegistry& registry)
		{
			if (!HasMissingActor(index, registry) || !fnReleaseDetached || !fnReleaseDetached(index))
				return false;
			// The actor is absent from the authoritative roster. Release only
			// the plugin's ownership; never restore through cached Unity pointers.
			followers_[index] = {};
			return true;
		}

		bool RetryRestoreAfterMissingActor(const SceneActorRegistry& registry)
		{
			restoreUnconfirmed_ = false;
			for (size_t i = 0; i < followers_.size(); ++i)
			{
				if (HasMissingActor(i, registry) || !RestoreSlot(i, registry))
					restoreUnconfirmed_ = true;
			}
			return !restoreUnconfirmed_;
		}

		bool ClearAll(const SceneActorRegistry& registry)
		{
			bool ok = true;
			for (size_t i = 0; i < followers_.size(); ++i) ok = Clear(i, registry) && ok;
			return ok;
		}

		// The complete Live census includes inactive actors. Restore surviving
		// instances while the old registry can still validate their identity.
		bool ClearForLiveRosterChange(const SceneActorRegistry& registry,
			const std::vector<SceneActorObservation>& loaded, bool (*releaseDetached)(size_t))
		{
			for (size_t i = 0; i < followers_.size(); ++i)
			{
				auto& follower = followers_[i];
				if (!follower.player) continue;
				const auto* old = registry.Find(follower.player->controller.targetActor);
				if (!old || old->token != follower.actorToken) return false;
				if (ObservedSceneActor(loaded, *old))
				{
					if (!Clear(i, registry)) return false;
				}
				else
				{
					// Release the physics publication before forgetting the binding.
					// The departed controller must not restore its cached Unity objects.
					if (!releaseDetached || !releaseDetached(i)) return false;
					follower = {};
				}
			}
			return true;
		}

		// Use only after scene ownership is lost: Unity objects can be destroyed.
		void Abandon()
		{
			for (auto& follower : followers_) follower = {};
			placements_.clear();
			armCalibrations_.clear();
			restoreUnconfirmed_ = false;
		}

		bool PrepareFollower(Follower& follower, uint64_t actorToken, void* actor, const MotionEntry& motion,
		                     float offset, const MotionPlayer& primary, const BoneOverrideSet& global, bool overridesEnabled)
		{
			if (!actor || actor == primary.controller.targetActor || !std::isfinite(offset) || motion.name.empty()) return false;
			auto next = std::make_unique<MotionPlayer>();
			next->initializeBeforeMotion = primary.initializeBeforeMotion;
			// Copy service callbacks and settings, then discard copied ownership.
			next->controller = primary.controller;
			next->controller.DiscardBindings();
			next->controller.SetTargetActor(actor);
			Placement saved;
			if (GetPlacement(actorToken, saved))
			{
				next->controller.SetInitialPositionOffset(saved.position);
				next->controller.SetInitialYawDegrees(saved.yawDegrees);
			}
			else
			{
				next->controller.SetInitialPositionOffset({});
				next->controller.SetInitialYawDegrees(0);
			}
			next->controller.auxGroundTruth = {};
			const auto foundCalibration = armCalibrations_.find(actorToken);
			const ArmCalibration calibration = foundCalibration == armCalibrations_.end() ? ArmCalibration{} : foundCalibration->second;
			next->controller.wristTwistShare = calibration.share;
			next->controller.forearmAxisAdjustmentDeg = calibration.degrees;
			next->controller.wristDiagnosticPending = false;
			if (!next->LoadMotion(motion.dir)) return false;
			next->timing = motion.timing;
			BoneOverrideSet motionOverrides, effective;
			const auto file = motion.dir + "/bone_overrides.json";
			std::error_code error;
			const bool hasOverrides = std::filesystem::exists(MotionPathFromUtf8(file), error);
			if (error || (hasOverrides && !motionOverrides.LoadFromFile(file))) return false;
			BoneOverrideSet::BuildEffective(global, motionOverrides, overridesEnabled, effective);
			follower.actorToken = actorToken;
			follower.armLocked = calibration.locked;
			follower.motionKey = motion.name;
			follower.offsetSeconds = offset;
			follower.motionOverrides = std::move(motionOverrides);
			follower.overrides = std::move(effective);
			follower.player = std::move(next);
			return true;
		}

		void InstallFollower(size_t index, Follower&& follower)
		{
			auto& installed = followers_[index];
			installed = std::move(follower);
			installed.bindingGeneration = nextBindingGeneration_++;
			if (!nextBindingGeneration_) ++nextBindingGeneration_;
			installed.player->controller.SetBoneOverrides(&installed.overrides);
		}

		bool Set(size_t index, uint64_t actorToken, uint64_t epoch, const MotionEntry& motion,
		         float offset, const SceneActorRegistry& registry, const MotionPlayer& primary,
		         const BoneOverrideSet& global, bool overridesEnabled, const MotionEntry* swapMotion = nullptr)
		{
			if (index >= 64 || restoreUnconfirmed_ || !primary.controller.targetActor ||
			    primary.state == PlaybackState::Playing || primary.state == PlaybackState::Paused) return false;
			size_t occupied = followers_.size();
			for (size_t i = 0; i < followers_.size(); ++i)
				if (i != index && followers_[i].actorToken == actorToken) { occupied = i; break; }
			const bool swap = occupied < followers_.size();
			if (swap && (!swapMotion || index >= followers_.size() || !followers_[index].player ||
			    swapMotion->name != followers_[occupied].motionKey)) return false;

			Follower next, displaced;
			if (!PrepareFollower(next, actorToken, registry.Resolve(actorToken, epoch), motion,
				offset, primary, global, overridesEnabled)) return false;
			if (swap) {
				const auto oldToken = followers_[index].actorToken;
				if (!PrepareFollower(displaced, oldToken, registry.Resolve(oldToken, epoch), *swapMotion,
					followers_[occupied].offsetSeconds, primary, global, overridesEnabled)) return false;
			}
			EnsureSlots(index + 1);
			if (!RestoreSlot(index, registry) || (swap && !RestoreSlot(occupied, registry))) return false;
			InstallFollower(index, std::move(next));
			if (swap) InstallFollower(occupied, std::move(displaced));
			return true;
		}

		bool SetPosition(size_t index, uint64_t actorToken, uint64_t epoch, uint64_t generation,
		                 const Vector3& position, const SceneActorRegistry& registry)
		{
			if (index >= followers_.size() || restoreUnconfirmed_) return false;
			auto& follower = followers_[index];
			if (!follower.player || !generation || follower.bindingGeneration != generation ||
			    follower.actorToken != actorToken || registry.Resolve(actorToken, epoch) != follower.player->controller.targetActor)
				return false;
			if (!follower.player->controller.SetInitialPositionOffset(position)) return false;
			RememberPlacement(actorToken, position, follower.player->controller.initialYawDegrees);
			return true;
		}

		bool MoveToEmptySlot(size_t from, size_t to, uint64_t epoch, const SceneActorRegistry& registry)
		{
			if (from >= followers_.size() || to >= 64 || from == to || restoreUnconfirmed_ ||
			    !followers_[from].player || !registry.Resolve(followers_[from].actorToken, epoch) ||
			    (to < followers_.size() && followers_[to].player)) return false;
			if (!RestoreSlot(from, registry)) return false;
			EnsureSlots(to + 1);
			Follower moved = std::move(followers_[from]);
			followers_[from] = {};
			InstallFollower(to, std::move(moved));
			return true;
		}
		bool SetYaw(size_t index, uint64_t actorToken, uint64_t epoch, uint64_t generation,
		            float yawDegrees, const SceneActorRegistry& registry)
		{
			if (index >= followers_.size() || restoreUnconfirmed_) return false;
			auto& follower = followers_[index];
			if (!follower.player || !generation || follower.bindingGeneration != generation ||
			    follower.actorToken != actorToken || registry.Resolve(actorToken, epoch) != follower.player->controller.targetActor ||
			    !follower.player->controller.SetInitialYawDegrees(yawDegrees)) return false;
			RememberPlacement(actorToken, follower.player->controller.initialPositionOffset,
			                  follower.player->controller.initialYawDegrees);
			return true;
		}

		// The generation and actor identity prevent a queued edit from reaching a replacement binding.
		bool SetArmCalibration(size_t index, uint64_t token, uint64_t epoch, uint64_t generation,
		                       int operation, float value, const SceneActorRegistry& registry)
		{
			if (index >= followers_.size() || restoreUnconfirmed_) return false;
			auto& follower = followers_[index];
			if (!follower.player || !generation || follower.bindingGeneration != generation ||
			    follower.actorToken != token || registry.Resolve(token, epoch) != follower.player->controller.targetActor)
				return false;
			return SetSceneArmCalibration(token, epoch, operation, value, registry);
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
			for (size_t index = 0; index < followers_.size(); ++index)
			{
				auto& follower = followers_[index];
				if (!follower.player || !follower.active) continue;
				void* actor = registry.Resolve(follower.actorToken, registry.Epoch());
				if (!actor || actor != follower.player->controller.targetActor)
				{ restoreUnconfirmed_ = true; ok = false; continue; }
				if (fnBeforeRestore && !fnBeforeRestore(index, *follower.player))
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
				player.controller.rebaseLiveMotionStart = primary.controller.rebaseLiveMotionStart;
				player.controller.auxDriveMode = primary.controller.auxDriveMode;
				player.currentTime = (std::max)(0.0f, primary.currentTime - follower.offsetSeconds);
				player.Tick(0.0f, actor, animator, director, false);
				follower.active = player.controller.bindings.find(actor) != player.controller.bindings.end();
			}
		}

	private:
		std::deque<Follower> followers_{2};
		std::unordered_map<uint64_t, Placement> placements_;
		std::unordered_map<uint64_t, ArmCalibration> armCalibrations_;
		bool restoreUnconfirmed_{false};
		uint64_t nextBindingGeneration_{1};
	};
}
