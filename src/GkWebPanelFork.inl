		// Translate Web controls to the fork's existing GUI command paths.
		std::string ForkStateLocked()
		{
			const auto ctx = core_.BuildGuiContext();
			std::string followers = "[";
			for (size_t i = 0; i < ctx.ensembleFollowers.size(); ++i)
			{
				const auto& follower = ctx.ensembleFollowers[i];
				if (i) followers += ",";
				followers += "{\"slot\":" + JNum(i) + ",\"actorToken\":" + JNum(follower.actorToken) +
					",\"generation\":" + JNum(follower.bindingGeneration) + ",\"motionKey\":" + JString(follower.motionKey) +
					",\"offsetSeconds\":" + JNum(follower.offsetSeconds) + ",\"position\":" + JVec(follower.position) +
					",\"yaw\":" + JNum(follower.yawDegrees) + ",\"physics\":" + JString(follower.physicsStatus) + "}";
			}
			followers += "]";
			const auto strings = [](const auto& values) {
				std::string json = "[";
				for (const auto& value : values) { if (json.size() > 1) json += ","; json += JString(value); }
				return json + "]";
			};
			return "{\"session\":" + JNum(core_.injectionSession) + ",\"owner\":" + JNum(core_.liveOwnerToken) +
				",\"epoch\":" + JNum(ctx.actorRosterEpoch) + ",\"revision\":" + JNum(core_.libraryRevision) +
				",\"observed\":" + JBool(ctx.liveObserved) + ",\"viewing\":" + JBool(ctx.liveViewingConfirmed) +
				",\"paused\":" + JBool(ctx.livePaused) + ",\"active\":" + JBool(ctx.liveTakeoverActive) +
				",\"available\":" + JBool(ctx.liveTakeoverAvailable) + ",\"aspect\":" + JBool(ctx.liveAspectPreviewRequested) +
				",\"loop\":" + JBool(ctx.liveLoopEnabled) + ",\"loopLead\":" + JNum(ctx.liveLoopLead) +
				",\"liveTime\":" + JNum(ctx.liveTime) + ",\"duration\":" + JNum(ctx.liveDuration) +
				",\"liveNotice\":" + JString(ctx.liveControlNotice) + ",\"followers\":" + followers +
				",\"position\":" + JVec(core_.player.controller.initialPositionOffset) +
				",\"yaw\":" + JNum(core_.player.controller.initialYawDegrees) +
				",\"formationNotice\":" + JString(ctx.ensembleNotice) + ",\"presetNotice\":" + JString(ctx.ensemblePresetNotice) +
				",\"presets\":" + strings(ctx.ensemblePresetNames) + ",\"folders\":" + strings(ctx.folderLibraryRoots) +
				",\"folderNotice\":" + JString(ctx.folderLibraryNotice) + "}";
		}

		CmdResult ForkCommand(const jsonlite::Value& params)
		{
			std::string action;
			if (!StringParam(params, "action", action))
				return CmdResult::Err("invalid_params", "缺少操作类型");
			std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
			int64_t session = -1;
			if (!IntParam(params, "session", session) || session < 0 ||
				static_cast<uint64_t>(session) != core_.injectionSession)
				return CmdResult::Err("stale_revision", "场景已切换，请重新操作");
			const auto ctx = core_.BuildGuiContext();
			if (action.rfind("live.", 0) == 0)
			{
				int64_t owner = -1;
				if (!IntParam(params, "owner", owner) || owner <= 0 ||
					static_cast<uint64_t>(owner) != core_.liveOwnerToken || core_.injectionPolicy.Read().captureDepth != 0)
					return CmdResult::Err("stale_token", "当前 Live 已变化或处于拍照保护期间");
				bool enable = false;
				double number = 0;
				if (action == "live.takeover" && BoolParam(params, "value", enable) &&
					(enable ? ctx.liveTakeoverAvailable && !ctx.liveTakeoverActive : ctx.liveTakeoverActive))
					ctx.fnSetLiveTakeover(ctx.self, enable);
				else if (action == "live.aspect" && BoolParam(params, "value", enable))
					ctx.fnSetLiveAspectPreview(ctx.self, enable);
				else if (action == "live.pause" && ctx.liveViewingConfirmed && !ctx.liveTakeoverActive &&
					BoolParam(params, "value", enable))
					ctx.fnSetLivePaused(ctx.self, enable);
				else if (action == "live.loop" && ctx.liveViewingConfirmed && !ctx.liveTakeoverActive &&
					BoolParam(params, "value", enable))
					ctx.fnSetLiveLoop(ctx.self, enable);
				else if (action == "live.loopLead" && NumberParam(params, "value", number) && number >= 0.5 && number <= 3.0)
					ctx.fnSetLiveLoopLead(ctx.self, static_cast<float>(number));
				else if (action == "live.seek" && ctx.liveViewingConfirmed && !ctx.liveTakeoverActive &&
					NumberParam(params, "value", number) && number >= 0 && number < ctx.liveDuration)
					ctx.fnSeekLive(ctx.self, static_cast<float>(number));
				else return CmdResult::Err("not_ready", "当前 Live 状态不支持此操作");
				return CmdResult::Ok();
			}
			if (action == "folder.add" || action == "folder.remove")
			{
				std::string path;
				if (!StringParam(params, "path", path) || path.empty() || path.size() > 4096 || path.find('\0') != std::string::npos)
					return CmdResult::Err("invalid_params", "请选择有效的目录");
				if (action == "folder.add") ctx.fnAddLibraryFolder(ctx.self, path.c_str());
				else ctx.fnRemoveLibraryFolder(ctx.self, path.c_str());
				return CmdResult::Ok();
			}
			if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
			int64_t epoch = -1;
			if (!IntParam(params, "epoch", epoch) || epoch < 0 || static_cast<uint64_t>(epoch) != ctx.actorRosterEpoch)
				return CmdResult::Err("stale_token", "角色名单已变化，请重新选择");
			if (action == "formation.align") ctx.fnAlignEnsemble(ctx.self);
			else if (action == "formation.auto") ctx.fnAutoAssignEnsemble(ctx.self);
			else if (action == "formation.savePrimary") ctx.fnSaveInitialPosition(ctx.self);
			else if (action == "formation.save" || action == "formation.load")
			{
				std::string name;
				if (!StringParam(params, "name", name) || !ValidPresetName(name) || name.find('\0') != std::string::npos)
					return CmdResult::Err("invalid_params", "请输入有效队形名称");
				if (action == "formation.save") ctx.fnSaveEnsemblePreset(ctx.self, name.c_str());
				else ctx.fnApplyEnsemblePreset(ctx.self, name.c_str());
			}
			else if (action == "formation.place")
			{
				int64_t slot = -2;
				Vector3 position;
				double yaw = 0;
				if (!IntParam(params, "slot", slot) || !VecParam(params, "position", position) ||
					!NumberParam(params, "yaw", yaw) || std::abs(yaw) > 180 ||
					std::abs(position.x) > 256 || std::abs(position.y) > 256 || std::abs(position.z) > 256)
					return CmdResult::Err("invalid_params", "站位或朝向超出范围");
				if (slot == -1)
				{
					int64_t token = -1;
					if (!IntParam(params, "actorToken", token) || token <= 0 ||
						core_.sceneActorRegistry.Resolve(static_cast<uint64_t>(token), ctx.actorRosterEpoch) != core_.player.controller.targetActor)
						return CmdResult::Err("stale_token", "主角色已变化");
					core_.QueueInitialPosition(position);
					core_.QueueInitialYaw(static_cast<float>(yaw));
				}
				else
				{
					int64_t token = -1, generation = -1;
					if (slot < 0 || static_cast<size_t>(slot) >= ctx.ensembleFollowers.size() ||
						!IntParam(params, "actorToken", token) || token <= 0 ||
						!IntParam(params, "generation", generation) || generation <= 0)
						return CmdResult::Err("stale_token", "副角色尚未绑定");
					const auto& follower = ctx.ensembleFollowers[static_cast<size_t>(slot)];
					if (follower.actorToken != static_cast<uint64_t>(token) || follower.bindingGeneration != static_cast<uint64_t>(generation))
						return CmdResult::Err("stale_token", "副角色绑定已变化");
					ctx.fnSetEnsemblePosition(ctx.self, static_cast<int>(slot), follower.actorToken, ctx.actorRosterEpoch, follower.bindingGeneration, position);
					ctx.fnSetEnsembleYaw(ctx.self, static_cast<int>(slot), follower.actorToken, ctx.actorRosterEpoch, follower.bindingGeneration, static_cast<float>(yaw));
				}
			}
			else if (action == "formation.bind" || action == "formation.clear")
			{
				int64_t slot = -1, revision = -1;
				if (!IntParam(params, "slot", slot) || slot < 0 || slot >= 64 ||
					!IntParam(params, "revision", revision) || revision < 0 || static_cast<uint64_t>(revision) != core_.libraryRevision)
					return CmdResult::Err("stale_revision", "角色槽或曲库版本无效");
				if (action == "formation.clear") ctx.fnClearEnsembleFollower(ctx.self, static_cast<int>(slot), ctx.actorRosterEpoch);
				else
				{
					int64_t token = -1;
					std::string key;
					double offset = 0;
					if (!IntParam(params, "actorToken", token) || token <= 0 ||
						!core_.sceneActorRegistry.Resolve(static_cast<uint64_t>(token), ctx.actorRosterEpoch) ||
						!StringParam(params, "motionKey", key) || FindPlaylistIndex(core_.motions, key) < 0 ||
						!NumberParam(params, "offsetSeconds", offset) || std::abs(offset) > 120)
						return CmdResult::Err("invalid_params", "请选择有效的副角色、动作和时间偏移");
					ctx.fnSetEnsembleFollower(ctx.self, static_cast<int>(slot), static_cast<uint64_t>(token), ctx.actorRosterEpoch,
						key.c_str(), static_cast<float>(offset), core_.libraryRevision);
				}
			}
			else return CmdResult::Err("unknown_method", "未知的本地看板操作");
			return CmdResult::Ok();
		}
