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
			std::string arms = "[";
			for (const auto& arm : ctx.sceneArmCalibrations)
			{
				if (arms.size() > 1) arms += ",";
				arms += "{\"token\":" + JNum(arm.token) + ",\"share\":" + JNum(arm.share) +
					",\"degrees\":" + JNum(arm.degrees) + ",\"locked\":" + JBool(arm.locked) +
					",\"bound\":" + JBool(arm.bound) + ",\"primary\":" + JBool(arm.primary) + "}";
			}
			arms += "]";
			std::string props = "[";
			for (const auto& prop : ctx.actorProps)
			{
				if (props.size() > 1) props += ",";
				props += "{\"key\":" + JString(ActorPropKey(prop)) + ",\"actorToken\":" + JNum(prop.actorToken) +
					",\"actor\":" + JString(prop.actorLabel) +
					",\"name\":" + JString(live_actor_props::DisplayName(prop.propName)) +
					",\"modelId\":" + JNum(prop.modelId) + ",\"modelName\":" + JString(prop.modelName) +
					",\"hidden\":" + JBool(prop.hidden) + ",\"editable\":" + JBool(prop.controllable) +
					",\"active\":" + JBool(prop.active) + ",\"timeline\":" + JBool(prop.timeline) +
					",\"moving\":" + JBool(core_.IsMovingStageProp(prop)) + "}";
			}
			props += "]";
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
				",\"placementReset\":" + JBool(ctx.placementResetExperiment) +
				",\"initializeBones\":" + JBool(core_.player.initializeBeforeMotion) +
				",\"auxMode\":" + JNum(core_.player.controller.auxDriveMode) +
				",\"wristStatus\":" + JNum(core_.player.controller.wristDiagnosticStatus) +
				",\"armNotice\":" + JString(ctx.forearmAxisNotice) + ",\"arms\":" + arms +
				",\"props\":" + props + ",\"propsEditable\":" + JBool(ctx.actorPropsEditable) +
				",\"propsCanScan\":" + JBool(ctx.injectionAllowed || ctx.liveViewingConfirmed) +
				",\"propsScanning\":" + JBool(core_.actorPropScanRequested) +
				",\"propsNotice\":" + JString(ctx.ensembleNotice.starts_with("该时间轴道具") ? ctx.ensembleNotice : "") +
				",\"fixedActorLighting\":" + JBool(core_.fixedActorLighting) +
				",\"fixedActorLightingNotice\":" + JString(core_.fixedActorLightingNotice) +
				",\"followYaw\":" + JBool(core_.camera.settings.followPlacementYaw) +
				",\"liveClock\":" + JBool(ctx.liveActorClockActive) + ",\"liveClockAvailable\":" + JBool(ctx.liveActorClockAvailable) +
				",\"liveDiagnostics\":" + JBool(ctx.liveContinuousArmDiagnostics) +
				",\"liveFps\":" + JNum(ctx.liveFrameMonitor.available ? ctx.liveFrameMonitor.fps : 0) +
				",\"liveStalls\":" + JNum(static_cast<double>(ctx.liveFrameMonitor.stalls)) +
				",\"swingReportLoaded\":" + JBool(ctx.physicsMonitor.checkpointReport.parsed) +
				",\"liveClockNotice\":" + JString(ctx.liveActorClockNotice) +
				",\"position\":" + JVec(core_.player.controller.initialPositionOffset) +
				",\"yaw\":" + JNum(core_.player.controller.initialYawDegrees) +
				",\"primaryPhysics\":" + JString(PrimaryPhysicsLabel(ctx)) +
				",\"formationNotice\":" + JString(ctx.ensembleNotice) + ",\"folders\":" + strings(ctx.folderLibraryRoots) +
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
				else if (action == "live.fixedActorLighting" && ctx.liveTakeoverActive &&
					!visual_handoff::Requested() && BoolParam(params, "value", enable))
				{
					core_.fixedActorLighting = enable;
					core_.fixedActorLightingNotice.clear();
				}
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
				else if (action == "live.record" && ctx.liveViewingConfirmed)
					ctx.fnRecordLiveState(ctx.self);
				else if (action == "live.swingReport") ctx.fnLoadSwingReport(ctx.self);
				else if (action == "live.clock" && BoolParam(params, "value", enable) &&
					(enable ? ctx.liveActorClockAvailable && ctx.livePaused && !ctx.liveTakeoverActive : ctx.liveActorClockActive))
					ctx.fnActorClockTrial(ctx.self, enable);
				else if (action == "live.armDiagnostics" && BoolParam(params, "value", enable))
					ctx.fnSetContinuousArmDiagnostics(ctx.self, enable);
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
			if (action == "stage.propsScan")
			{
				if (!ctx.injectionAllowed && !ctx.liveViewingConfirmed)
					return CmdResult::Err("not_ready", "当前场景不可扫描道具");
				int64_t owner = 0;
				if (ctx.liveViewingConfirmed && (!IntParam(params, "owner", owner) || owner <= 0 ||
					static_cast<uint64_t>(owner) != core_.liveOwnerToken))
					return CmdResult::Err("stale_token", "当前 Live 已变化，请重新扫描");
				ctx.fnScanActorProps(ctx.self);
				return CmdResult::Ok();
			}
			if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
			int64_t epoch = -1;
			if (!IntParam(params, "epoch", epoch) || epoch < 0 || static_cast<uint64_t>(epoch) != ctx.actorRosterEpoch)
				return CmdResult::Err("stale_token", "角色名单已变化，请重新选择");
			if (action == "formation.align") ctx.fnAlignEnsemble(ctx.self);
			else if (action == "formation.auto") ctx.fnAutoAssignEnsemble(ctx.self);
			else if (action == "motion.resetPhysics" || action == "motion.initializeBones" || action == "camera.followYaw")
			{
				bool enable = false;
				if (!BoolParam(params, "value", enable)) return CmdResult::Err("invalid_params", "需要布尔值");
				if (action == "motion.resetPhysics") ctx.fnSetPlacementResetExperiment(ctx.self, enable);
				else if (action == "motion.initializeBones") ctx.fnSetInitializeBeforeMotion(ctx.self, enable);
				else
				{
					auto settings = core_.camera.settings;
					settings.followPlacementYaw = enable;
					core_.QueueCameraSettings(settings);
				}
			}
			else if (action == "motion.auxMode")
			{
				int64_t mode = -1;
				if (!IntParam(params, "value", mode) || mode < 0 || mode >= kAuxModeCount)
					return CmdResult::Err("invalid_params", "辅助骨模式无效");
				ctx.fnSetAuxDriveMode(ctx.self, static_cast<int>(mode));
			}
			else if (action == "motion.wristReport")
			{
				if (core_.player.state == PlaybackState::Stopped)
					return CmdResult::Err("not_ready", "请先载入动作");
				ctx.fnCaptureWristDiagnostic(ctx.self);
			}
			else if (action == "motion.arm")
			{
				int64_t token = -1, op = -1;
				double value = 0;
				if (!IntParam(params, "actorToken", token) || token <= 0 ||
					!core_.sceneActorRegistry.Resolve(static_cast<uint64_t>(token), ctx.actorRosterEpoch) ||
					!IntParam(params, "operation", op) || op < 0 || op > 3 ||
					!NumberParam(params, "value", value) ||
					(op == 0 && std::abs(value) > 1) || (op == 1 && std::abs(value) > 90) ||
					(op == 3 && value != 0 && value != 1))
					return CmdResult::Err("invalid_params", "校正角色或数值无效");
				ctx.fnSetSceneArm(ctx.self, static_cast<uint64_t>(token), ctx.actorRosterEpoch,
					static_cast<int>(op), static_cast<float>(value));
			}
			else if (action == "stage.prop" || action == "stage.propGroup")
			{
				std::string key;
				int64_t token = -1;
				int64_t owner = 0;
				bool hidden = false;
				const bool timelineCurrent = ctx.liveViewingConfirmed && ctx.liveTakeoverActive &&
					IntParam(params, "owner", owner) && owner > 0 && static_cast<uint64_t>(owner) == core_.liveOwnerToken;
				if (!ctx.actorPropsEditable || !BoolParam(params, "value", hidden))
					return CmdResult::Err("stale_token", "道具不可编辑或已变化");
				std::vector<const GuiActorProp*> targets;
				const auto add = [&](const std::string& id, int64_t expectedToken) {
					const auto found = std::find_if(ctx.actorProps.begin(), ctx.actorProps.end(), [&](const auto& prop) {
						return (expectedToken < 0 || prop.actorToken == static_cast<uint64_t>(expectedToken)) &&
							prop.controllable && ActorPropKey(prop) == id &&
							(prop.timeline ? timelineCurrent : core_.sceneActorRegistry.Resolve(prop.actorToken, ctx.actorRosterEpoch) != nullptr);
					});
					if (found == ctx.actorProps.end()) return false;
					targets.push_back(&*found);
					return true;
				};
				if (action == "stage.propGroup") {
					const auto* keys = params.Find("keys");
					if (!keys || !keys->IsArray() || keys->Empty() || keys->Size() > ctx.actorProps.size())
						return CmdResult::Err("invalid_params", "道具列表无效");
					for (const auto& id : keys->arrayItems)
						if (!id.IsString() || !add(id.stringValue, -1))
							return CmdResult::Err("stale_token", "道具组已变化，请重新扫描");
				} else if (!IntParam(params, "actorToken", token) || token < 0 ||
					!StringParam(params, "key", key) || !add(key, token))
					return CmdResult::Err("stale_token", "道具不可编辑或已变化");
				for (const auto* prop : targets)
					ctx.fnSetActorPropHidden(ctx.self, prop->actorToken, ctx.actorRosterEpoch, ActorPropKey(*prop).c_str(), hidden);
			}
			else if (action == "actor.hideModel")
			{
				int64_t token = -1;
				bool hidden = false;
				if (!IntParam(params, "actorToken", token) || token <= 0 ||
					!core_.sceneActorRegistry.Resolve(static_cast<uint64_t>(token), ctx.actorRosterEpoch) ||
					!BoolParam(params, "value", hidden))
					return CmdResult::Err("stale_token", "角色已变化，请重新选择");
				ctx.fnSetActorModelHidden(ctx.self, static_cast<uint64_t>(token), ctx.actorRosterEpoch, hidden);
			}
			else if (action == "formation.savePrimary") ctx.fnSaveInitialPosition(ctx.self);
			else if (action == "formation.place")
			{
				int64_t slot = -2;
				Vector3 position;
				double yaw = 0;
				const bool setPosition = params.Find("position") != nullptr;
				const bool setYaw = params.Find("yaw") != nullptr;
				if (!IntParam(params, "slot", slot) || (!setPosition && !setYaw) ||
					(setYaw && (!NumberParam(params, "yaw", yaw) || std::abs(yaw) > 180)) ||
					(setPosition && (!VecParam(params, "position", position) || std::abs(position.x) > 256 ||
						std::abs(position.y) > 256 || std::abs(position.z) > 256)))
					return CmdResult::Err("invalid_params", "站位或朝向超出范围");
				if (slot == -1)
				{
					int64_t token = -1;
					if (!IntParam(params, "actorToken", token) || token <= 0 ||
					!core_.player.controller.targetActor ||
						core_.sceneActorRegistry.Resolve(static_cast<uint64_t>(token), ctx.actorRosterEpoch) != core_.player.controller.targetActor)
						return CmdResult::Err("stale_token", "主角色已变化");
					if (setPosition) core_.QueueInitialPosition(position);
					if (setYaw) core_.QueueInitialYaw(static_cast<float>(yaw));
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
					if (setPosition) ctx.fnSetEnsemblePosition(ctx.self, static_cast<int>(slot), follower.actorToken, ctx.actorRosterEpoch, follower.bindingGeneration, position);
					if (setYaw) ctx.fnSetEnsembleYaw(ctx.self, static_cast<int>(slot), follower.actorToken, ctx.actorRosterEpoch, follower.bindingGeneration, static_cast<float>(yaw));
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
