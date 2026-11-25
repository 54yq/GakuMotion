		// Local QA requests are evaluated on the main actor tick.
		void PollLocalMeniControl()
		{
			const auto now = std::chrono::steady_clock::now();
			if (now < localMeniPollAt) return;
			localMeniPollAt = now + std::chrono::milliseconds(150);
			const auto root = MotionPathFromUtf8(configRoot);
			const auto requestPath = root / "meni_mcp.request";
			std::ifstream request(requestPath);
			if (!request) return;

			std::array<char, 128> buffer{};
			request.getline(buffer.data(), buffer.size());
			if (!request) return;
			request.close();
			std::istringstream fields(buffer.data());
			std::string id, action, trailing;
			if (!(fields >> id >> action) || fields >> trailing || id.size() != 32 ||
				id.find_first_not_of("0123456789abcdef") != std::string::npos ||
				id == localMeniLastRequest) return;

			const char* error = nullptr;
			if (action == "stage-on")
			{
				if (!InjectionAllowed()) error = "scene_not_ready";
				else
				{
					externalStageEnabled.store(true);
					externalStageFollow.store(true);
				}
			}
			else if (action == "locate-start")
			{
				if (!InjectionAllowed() || externalStageStatus.load() != 1 || !player.meniSourceMotion)
					error = "stage_or_motion_not_ready";
				else
				{
					externalStageLocateResult.store(0);
					externalStageLocate.store(true);
				}
			}
			else if (action == "reload-axis")
			{
				std::ifstream file(root / "meni_stage_props.txt");
				auto parsed = stage_prop_timeline::Read(file);
				if (!parsed || (file >> std::ws).peek() != std::char_traits<char>::eof())
					error = "invalid_axis";
				else
				{
					stagePropCues = std::move(*parsed);
					stagePropPreview.reset();
					sourcePropTrialGeneration.reset();
					stagePropSamples = {};
				}
			}
			else if (action == "contact-shadows-on" || action == "contact-shadows-off")
			{
				if (!InjectionAllowed() || externalStageStatus.load() != 1)
					error = "stage_not_ready";
				else
					externalStageContactShadows.store(action == "contact-shadows-on");
			}
			else if (action == "contact-trace-on" || action == "contact-trace-off")
			{
				if (!InjectionAllowed() || externalStageStatus.load() != 1)
					error = "stage_not_ready";
				else
					externalStageContactTrace.store(action == "contact-trace-on");
			}
			else if (action != "state") error = "unknown_action";

			const auto responsePath = root / ("meni_mcp." + id + ".json");
			auto temp = responsePath;
			temp += ".tmp";
			std::ofstream response(temp, std::ios::trunc);
			response.imbue(std::locale::classic());
			response << "{\"ok\":" << (error ? "false" : "true");
			if (error) response << ",\"error\":\"" << error << '"';
			response << ",\"stageEnabled\":" << (externalStageEnabled.load() ? "true" : "false")
				<< ",\"stageStatus\":" << externalStageStatus.load()
				<< ",\"locateResult\":" << externalStageLocateResult.load()
				<< ",\"stageFollow\":" << (externalStageFollow.load() ? "true" : "false")
				<< ",\"contactShadows\":" << (externalStageContactShadows.load() ? "true" : "false")
				<< ",\"stageVisibilityFirst\":" << (externalStageVisibilityFirst.load() ? "true" : "false")
				<< ",\"stageVisibilitySecond\":" << (externalStageVisibilitySecond.load() ? "true" : "false")
				<< ",\"contactTrace\":" << (externalStageContactTrace.load() ? "true" : "false")
				<< ",\"stageTime\":" << externalStageTime.load()
				<< ",\"sourceFrame\":" << stage_prop_timeline::FrameAtTime(player.AppliedMotionSourceTime())
				<< ",\"axisCueCount\":" << stagePropCues.size()
				<< ",\"manualProp\":" << externalStageHeldProp.load()
				<< ",\"previewAxis\":" << (stagePropPreview ? "true" : "false")
				<< ",\"sourceTrial\":" << (SourcePropTrialActive() ? "true" : "false")
				<< ",\"stageVisibilityActors\":[";
			bool firstActor = true;
			for (const auto& follower : ensemble.Followers())
			{
				int place = 0;
				if (follower.motionKey == "cutt_son1156_Place07") place = 7;
				if (follower.motionKey == "cutt_son1156_Place08") place = 8;
				if (follower.motionKey == "cutt_son1156_Place10") place = 10;
				if (!follower.player || !place) continue;
				if (!firstActor) response << ',';
				firstActor = false;
				response << "{\"token\":" << follower.actorToken << ",\"place\":" << place
					<< ",\"hiddenByStage\":" << (stageHiddenActorModels.contains(follower.actorToken) ? "true" : "false") << '}';
			}
			response << "],\"props\":[";
			const auto vec = [&](const Vector3& value)
			{
				response << '[' << value.x << ',' << value.y << ',' << value.z << ']';
			};
			for (size_t index = 0; index < stagePropSamples.size(); ++index)
			{
				if (index) response << ',';
				if (const auto& sample = stagePropSamples[index])
				{
					response << "{\"frame\":" << sample->frame << ",\"position\":";
					vec(sample->position);
					response << ",\"rotation\":";
					vec(sample->rotation);
					response << '}';
				}
				else response << "null";
			}
			response << "]}\n";
			response.close();
			std::error_code systemError;
			if (response) std::filesystem::rename(temp, responsePath, systemError);
			if (response && !systemError) localMeniLastRequest = std::move(id);
		}
