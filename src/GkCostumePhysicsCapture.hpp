#pragma once
#include "GkFallbackPhysics.hpp"
#include "GkJsonLite.hpp"
#include <filesystem>
#include <fstream>
#include <locale>

namespace GakuMotion
{
	// Main-thread, bounded recorder for solver and actor pose diagnostics.
	class CostumePhysicsCapture
	{
	public:
		using Clock = std::chrono::steady_clock;
		static constexpr unsigned kFrameLimit = 8192;

		bool Open(const std::filesystem::path& path, const std::string& motion, uint64_t session)
		{
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			if (error || std::filesystem::exists(path, error) || error) return false;
			out_.clear();
			out_.imbue(std::locale::classic());
			out_.open(path, std::ios::binary);
			if (!out_) return false;
			state_ = {};
            poseSampler_.Clear();
			frames_ = 0;
			session_ = session;
			started_ = Clock::now();
			nextFlush_ = started_ + std::chrono::seconds(1);
			out_ << "{\"event\":\"start\",\"schema\":1,\"motion\":";
			Name(motion);
			out_ << ",\"session\":" << session << "}\n";
			return true;
		}

		bool Active() const { return out_.is_open() && out_.good() && frames_ < kFrameLimit; }
		bool Opened() const { return out_.is_open(); }
		bool Expired(Clock::time_point now) const { return now - started_ >= std::chrono::seconds(15); }
		uint64_t Session() const { return session_; }
		unsigned Frames() const { return frames_; }

		bool Close(const char* reason)
		{
			out_ << "{\"event\":\"end\",\"reason\":\"" << reason << "\",\"frames\":" << frames_ << "}\n";
			out_.close();
            poseSampler_.Clear();
			return out_.good();
		}

		void Record(unsigned slot, const void* actor, int frame, float time, float dt, int transport, int seeks,
		            bool scrubbing, const FallbackPhysics& solver, const FallbackPhysics::CostumeFrame& sample)
		{
			if (!Active() || slot == 0 || slot > state_.size()) return;
			const auto now = Clock::now();
			auto& role = state_[slot - 1];
			const auto actorId = reinterpret_cast<uintptr_t>(actor);
			if (role.actor != actorId || sample.rebound || (role.scrubbing && !scrubbing))
				role.pendingRest = true;
			if (role.pendingRest && sample.garments && (!scrubbing || role.actor != actorId))
			{
				out_ << "{\"event\":\"rest\"";
				Identity(slot, actorId, frame, time);
				out_ << ",\"bones\":[";
				bool first = true;
				solver.VisitCostumeRest([&](const std::string& bone, float length, Vector3 axis, Quaternion rest) {
					if (!first) out_ << ',';
					first = false;
					out_ << "{\"bone\":";
					Name(bone);
					out_ << ",\"length_m\":" << length << ",\"axis\":";
					Vector(axis);
					out_ << ",\"rest_local\":";
					Rotation(rest);
					out_ << '}';
				});
				out_ << "]}\n";
				role.pendingRest = false;
			}
			role.actor = actorId;
			role.scrubbing = scrubbing;

			out_ << "{\"event\":\"frame\"";
			Identity(slot, actorId, frame, time);
			out_ << ",\"dt_s\":";
			Number(dt);
			out_ << ",\"wall_s\":" << std::chrono::duration<float>(now - started_).count()
			     << ",\"transport\":" << transport << ",\"seeks\":" << seeks << ",\"scrubbing\":" << scrubbing
			     << ",\"rebound\":" << sample.rebound << ",\"time_reset\":" << sample.timeReset
			     << ",\"initial_resets\":" << sample.initialResets << ",\"pose_resets\":" << sample.poseResets
			     << ",\"angle_clamps\":" << sample.angleClamps << ",\"speed_clamps\":" << sample.speedClamps
			     << ",\"substeps\":" << sample.substeps << ",\"garment_bones\":" << sample.garments
			     << ",\"solved\":" << sample.solved << ",\"invalid\":" << sample.invalid
			     << ",\"spring_s_inv2\":" << sample.spring << ",\"damping_s_inv\":" << sample.damping
			     << ",\"gravity_m_s2\":" << sample.gravity << ",\"cone_deg\":" << sample.coneDegrees;
			static constexpr const char* labels[]{"anchor_delta_m", "base_delta_deg", "prewrite_delta_deg",
				"spring_gravity_accel_m_s2", "tip_speed_m_s", "output_from_rest_deg"};
			out_ << ",\"peaks\":{";
			for (size_t i = 0; i < sample.peaks.size(); ++i)
			{
				if (i) out_ << ',';
				out_ << '"' << labels[i] << "\":";
				if (sample.maxima[i] < 0.f) { out_ << "null"; continue; }
				WritePeak(sample.maxima[i], sample.peaks[i]);
			}
			out_ << "}}\n";
			++frames_;
			if (now >= nextFlush_)
			{
				out_.flush();
				nextFlush_ = now + std::chrono::seconds(1);
			}
		}

        void RecordActorPose(const char* stage, const IdolBoneBinding& binding, const IdolController& controller,
                             int frame, float time, int requested, int actual, int sourceFrame, float nativeDt)
        {
            if (!Active()) return;
            out_ << "{\"event\":\"actor_pose\",\"stage\":";
            Name(stage);
            Identity(1, reinterpret_cast<uintptr_t>(binding.actor), frame, time);
            out_ << ",\"requested_backend\":" << requested << ",\"actual_backend\":" << actual
                 << ",\"source_frame\":" << sourceFrame << ",\"native_dt_s\":";
            if (sourceFrame >= 0) Number(nativeDt); else out_ << "null";
            out_ << ",\"bones\":[";
            bool first = true;
            poseSampler_.SampleSecondaryLocal(binding, controller, [&](const std::string& name, Quaternion rotation) {
                if (!first) out_ << ',';
                first = false;
                out_ << "{\"bone\":";
                Name(name);
                out_ << ",\"local\":";
                Rotation(rotation);
                out_ << '}';
            });
            out_ << "]}\n";
            ++frames_;
            const auto now = Clock::now();
            if (now >= nextFlush_) {
                out_.flush();
                nextFlush_ = now + std::chrono::seconds(1);
            }
        }

	private:
        FallbackPhysics poseSampler_;
		struct Role
		{
			uintptr_t actor{};
			bool pendingRest{}, scrubbing{};
		};
		std::array<Role, 16> state_{};
		std::ofstream out_;
		Clock::time_point started_{}, nextFlush_{};
		uint64_t session_{};
		unsigned frames_{};

		void Name(const std::string& name) { out_ << '"' << jsonlite::EscapeString(name) << '"'; }
		void Number(float value) { if (std::isfinite(value)) out_ << value; else out_ << "null"; }
		void Vector(Vector3 v)
		{
			out_ << '['; Number(v.x); out_ << ','; Number(v.y); out_ << ','; Number(v.z); out_ << ']';
		}
		void Rotation(Quaternion q)
		{
			out_ << '['; Number(q.x); out_ << ','; Number(q.y); out_ << ','; Number(q.z);
			out_ << ','; Number(q.w); out_ << ']';
		}
		void Identity(unsigned slot, uintptr_t actor, int frame, float time)
		{
			out_ << ",\"slot\":" << slot << ",\"actor\":" << actor
			     << ",\"unity_frame\":" << frame << ",\"motion_s\":";
			Number(time);
		}
		void WritePeak(float value, const FallbackPhysics::CostumeSample& sample)
		{
			out_ << "{\"value\":" << value << ",\"bone\":";
			Name(std::string(sample.bone));
			out_ << ",\"anchor_m\":"; Vector(sample.anchor);
			out_ << ",\"parent_world\":"; Rotation(sample.parent);
			out_ << ",\"input_local\":";
			if (sample.inputKnown) Rotation(sample.input); else out_ << "null";
			out_ << ",\"rest_local\":"; Rotation(sample.rest);
			out_ << ",\"output_local\":"; Rotation(sample.output);
			out_ << ",\"accel_m_s2\":"; Vector(sample.acceleration);
			out_ << ",\"velocity_m_s\":"; Vector(sample.velocity);
			out_ << '}';
		}
	};
}
