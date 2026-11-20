#pragma once
#include "GkIdolController.hpp"
#include <chrono>
#include <cctype>
#include <unordered_set>
#include <sstream>
#include <array>
#include <string_view>

namespace GakuMotion
{
	// Public-Transform-only secondary motion. The caller supplies its selected
	// Actor after MMD's body write, on the Unity thread, and calls Clear on scene
	// destruction/rebind. Naming is deliberately conservative: unknown rigs skip.
	// This is a bounded approximation, without the game's collision/cloth model.
	class FallbackPhysics
	{
	public:
		struct Settings
		{
			float spring{120.f}; // s^-2, attraction toward captured rest
			float damping{18.f}; // s^-1
			float gravity{.6f}; // world downward acceleration, m/s^2
			float maxSwingDegrees{25.f}; // conservative: no collision model
			float maxStepSeconds{1.f / 120.f};
			unsigned maxSubsteps{8};
		};

		struct Stats
		{
			double cpuUs{}, meanCpuUs{}, maxCpuUs{};
			uint64_t frames{}, publishCalls{}, totalCpuNs{}, clamps{}, resets{}, rebinds{}, invalidSamples{};
			size_t boundBones{}, solvedBones{}, skippedCandidates{};
			// Observed before our write: a mismatch means another writer or a
			// changed transform state, not necessarily wind or game physics.
			size_t inputCompared{}, inputChanged{};
			float inputMaxDeltaDegrees{}, inputMeanDeltaDegrees{};
		};

		struct CostumeSample
		{
			std::string_view bone;
			Quaternion rest{}, input{}, parent{}, output{};
			Vector3 anchor{}, acceleration{}, velocity{};
			float length{}, anchorDelta{-1.f}, parentDelta{-1.f}, inputDelta{-1.f}, outputDelta{};
			bool inputKnown{};
		};

		struct CostumeFrame
		{
			enum Peak { Anchor, Parent, External, Acceleration, Speed, Output, PeakCount };
			std::array<CostumeSample, PeakCount> peaks{};
			std::array<float, PeakCount> maxima{-1.f, -1.f, -1.f, -1.f, -1.f, -1.f};
			float spring{}, damping{}, gravity{}, coneDegrees{};
			unsigned garments{}, solved{}, invalid{}, initialResets{}, poseResets{}, angleClamps{}, speedClamps{}, substeps{};
			bool rebound{}, timeReset{};

			void Record(const CostumeSample& sample, float acceleration, float speed)
			{
				const std::array<float, PeakCount> scores{sample.anchorDelta, sample.parentDelta, sample.inputDelta,
					acceleration, speed, sample.outputDelta};
				for (size_t i = 0; i < scores.size(); ++i)
					if (std::isfinite(scores[i]) && scores[i] > maxima[i])
						{ maxima[i] = scores[i]; peaks[i] = sample; }
			}
		};

		// Callback views refer to the active solver's names and are consumed
		// before any rebind or destruction.
		template<class Visit>
		void VisitCostumeRest(Visit&& visit) const
		{
			for (const Bone& bone : bones_)
				if (bone.garment) visit(bone.name, bone.length, bone.axis, bone.rest);
		}

        static bool SampleHeadWorld(const IdolBoneBinding& binding, const IdolController& controller, Quaternion& rotation)
        {
            if (!controller.fnGetWorldRotation) return false;
            for (size_t i=0; i<binding.boneTransforms.size() && i<binding.boneHids.size(); ++i) {
                if (binding.boneHids[i] == 10 && binding.boneTransforms[i])
                    return CaptureRotation(controller.fnGetWorldRotation, binding.boneTransforms[i], rotation);
            }
            return false;
        }

        // Read-only sampling uses a dedicated instance, never the active solver.
        template<class Visit>
        void SampleSecondaryLocal(const IdolBoneBinding& binding, const IdolController& controller, Visit&& visit)
        {
            if (!HasApi(controller) || !binding.actorTransform) return;
            if (!SameBinding(binding)) Bind(binding, controller);
            unsigned hair = 0, garment = 0;
            for (const auto& bone : bones_) {
                auto& count = bone.garment ? garment : hair;
                if (count >= 4) continue;
                Quaternion rotation{};
                if (!CaptureRotation(controller.fnGetLocalRotation, bone.transform, rotation)) continue;
                Quaternion parentWorld{};
                const bool parentValid = bone.parent &&
                    CaptureRotation(controller.fnGetWorldRotation, bone.parent, parentWorld);
                visit(bone.name, rotation, parentValid ? &parentWorld : nullptr);
                ++count;
            }
        }

		FallbackPhysics() = default;

		explicit FallbackPhysics(Settings settings) : settings_(settings)
		{
		}

		const Stats& GetStats() const noexcept { return stats_; }
		// Bounded local-rotation snapshots permit same-rig cross-scene comparison;
		// these samples are the captured base, not proof of a neutral bind pose.
		std::string RestSamples() const
		{
			std::ostringstream out;
			const size_t count = (std::min)(bones_.size(), size_t{24});
			for (size_t i = 0; i < count; ++i)
			{
				const auto& b = bones_[i];
				if (i) out << ';';
				out << b.name << ':' << b.rest.x << ',' << b.rest.y << ',' << b.rest.z << ',' << b.rest.w;
			}
			return out.str();
		}
		void* Actor() const noexcept { return actor_; }
		bool HasCaptureWrites() const noexcept
		{
			for (const auto& b : bones_) if (b.written) return true;
			return false;
		}

		static bool IsGarmentBone(const std::string& name)
		{
			return EndsWith(name, "_S") && CostumeName(name, true);
		}

		// No dereferences: safe after the old Unity objects have been destroyed.
		void Clear() noexcept
		{
			bones_.clear();
			body_.clear();
			actor_ = actorTransform_ = root_ = animator_ = nullptr;
			lastFrame_ = -1;
			stats_.boundBones = stats_.solvedBones = 0;
		}

		struct NativeHandoff
		{
			size_t restored{}, preserved{}, unreadable{};
		};

		NativeHandoff ReleaseToNative(const IdolBoneBinding& binding, const IdolController& controller)
		{
			NativeHandoff result;
			if (SameBinding(binding) && controller.CanDriveActor(binding.actor))
			{
				for (const Bone& b : bones_)
				{
					if (!b.written) continue;
					Quaternion current;
					if (!controller.fnGetLocalRotation ||
						!CaptureRotation(controller.fnGetLocalRotation, b.transform, current))
					{
						++result.unreadable;
						continue;
					}
					// The game can already have published this frame's output
					// before we receive confirmation of native ownership.
					if (1.f - std::fabs(Quaternion::Dot(current, b.lastWritten)) > 1e-6f)
					{
						++result.preserved;
						continue;
					}
					if (!controller.fnSetLocalRotation || !CaptureWrite(controller, b.transform, b.rest))
						++result.unreadable;
					else
						++result.restored;
				}
			}
			Clear();
			return result;
		}

		bool RestoreForCapture(const IdolController& controller)
		{
			if (!HasCaptureWrites()) { Clear(); return true; }
			if (!controller.fnSetLocalRotation || !controller.fnGetLocalRotation) return false;
			bool confirmed = true;
			for (Bone& b : bones_)
			{
				if (!b.written) continue;
				Quaternion observed;
				if (!CaptureWrite(controller, b.transform, b.rest) ||
					!CaptureRotation(controller.fnGetLocalRotation, b.transform, observed) ||
					1.f - std::fabs(Quaternion::Dot(observed, b.rest)) > 1e-5f)
					confirmed = false;
				else b.written = false;
			}
			// Retain any unconfirmed target for a retry; never turn a failed
			// getter/setter into success by dropping the ownership record.
			if (confirmed) Clear();
			return confirmed;
		}

		bool Publish(const IdolBoneBinding& binding, const IdolController& controller,
		             float dt, int frame, CostumeFrame* costume = nullptr)
		{
			if (frame >= 0 && frame == lastFrame_ && SameBinding(binding)) return false;
			const auto start = std::chrono::steady_clock::now();
			++stats_.frames;
			stats_.solvedBones = 0;
			stats_.inputCompared = stats_.inputChanged = 0;
			stats_.inputMaxDeltaDegrees = stats_.inputMeanDeltaDegrees = 0;
			if (!binding.bindPoseCaptured || !binding.actor || !binding.actorTransform ||
				!controller.CanDriveActor(binding.actor) || !HasApi(controller))
			{
				Clear();
				return Finish(start);
			}
			const bool rebound = !SameBinding(binding);
			if (rebound) Bind(binding, controller);
			if (costume) costume->rebound = rebound;
			lastFrame_ = frame;
			if (bones_.empty()) return Finish(start);

			// Never integrate a hitch/seek-sized wall-clock gap into a spring.
			const float step = FiniteSetting(settings_.maxStepSeconds, 1.f / 120.f, 1.f / 240.f, 1.f / 60.f);
			const unsigned maxSteps = std::clamp(settings_.maxSubsteps, 1u, 16u);
			const bool reset = !std::isfinite(dt) || dt <= 0.f || dt > step * maxSteps;
			const unsigned steps = reset ? 1u : (std::min)(maxSteps, static_cast<unsigned>(std::ceil(dt / step)));
			const float h = reset ? 0.f : dt / steps;
			const float stiffness = FiniteSetting(settings_.spring, 120.f, 0.f, 400.f);
			const float damping = FiniteSetting(settings_.damping, 18.f, 0.f, 50.f);
			const float gravity = FiniteSetting(settings_.gravity, .6f, 0.f, 20.f);
			const float cone = FiniteSetting(settings_.maxSwingDegrees, 25.f, 0.f, 80.f) * kRadians;
			if (costume)
			{
				costume->spring = stiffness;
				costume->damping = damping;
				costume->gravity = gravity;
				costume->coneDegrees = cone / kRadians;
				costume->timeReset = reset;
				costume->substeps = reset ? 0u : steps;
			}
			bool resetFrame = reset;
			for (Bone& b : bones_)
			{
				auto* trace = b.garment ? costume : nullptr;
				if (trace) ++trace->garments;
				CostumeSample sample;
				Quaternion observed;
				const bool compare = stats_.inputCompared < 32 && b.written;
				const bool localKnown = (trace || compare) &&
					Rotation(controller.fnGetLocalRotation, b.transform, observed);
				if (compare && localKnown)
				{
					const float delta = RotationAngle(observed, b.lastWritten) / kRadians;
					if (std::isfinite(delta))
					{
						++stats_.inputCompared;
						if (delta > .1f) ++stats_.inputChanged;
						stats_.inputMeanDeltaDegrees += delta;
						stats_.inputMaxDeltaDegrees = (std::max)(stats_.inputMaxDeltaDegrees, delta);
					}
				}
				Vector3 anchor;
				Quaternion parent;
				if (!Position(controller.fnGetWorldPosition, b.transform, anchor) ||
					!Rotation(controller.fnGetWorldRotation, b.parent, parent))
				{
					++stats_.invalidSamples;
					if (trace) ++trace->invalid;
					b.initialized = false;
					continue;
				}
				const Quaternion base = (parent * b.rest).Normalized();
				const Vector3 restDirection = base.Rotate(b.axis);
				const Vector3 restTip = anchor + Scale(restDirection, b.length);
				const bool teleport = b.initialized &&
				(Length(anchor - b.anchor) > (std::max)(.20f, 3.f * b.length) ||
					RotationAngle(base, b.base) > 75.f * kRadians);
				if (trace)
				{
					sample.bone = b.name;
					sample.rest = b.rest;
					sample.input = observed;
					sample.inputKnown = localKnown;
					sample.parent = parent;
					sample.anchor = anchor;
					sample.length = b.length;
					if (b.initialized)
					{
						sample.anchorDelta = Length(anchor - b.anchor);
						sample.parentDelta = RotationAngle(base, b.base) / kRadians;
					}
					if (localKnown && b.written) sample.inputDelta = RotationAngle(observed, b.lastWritten) / kRadians;
					if (!b.initialized) ++trace->initialResets;
					if (teleport) ++trace->poseResets;
				}
				float peakAcceleration = reset || !b.initialized || teleport ? -1.f : 0.f;
				if (!b.initialized || reset || teleport)
				{
					b.tip = restTip;
					b.velocity = {};
					b.initialized = true;
					resetFrame = true;
				}
				else
				{
					for (unsigned n = 0; n < steps; ++n)
					{
						const Vector3 force = Scale(restTip - b.tip, stiffness) + Vector3(0, -gravity, 0);
						if (trace)
						{
							const float magnitude = Length(force);
							if (magnitude > peakAcceleration)
							{
								peakAcceleration = magnitude;
								sample.acceleration = force;
							}
						}
						b.velocity = Scale(b.velocity + Scale(force, h), std::exp(-damping * h));
						const Vector3 oldTip = b.tip;
						Vector3 direction = Unit(b.tip + Scale(b.velocity, h) - anchor, restDirection);
						Quaternion swing = FromTo(restDirection, direction);
						const float angle = RotationAngle(Quaternion::Identity(), swing);
						if (angle > cone && angle > 1e-6f)
						{
							swing = Quaternion::Slerp(Quaternion::Identity(), swing, cone / angle);
							direction = swing.Rotate(restDirection);
							++stats_.clamps;
							if (trace) ++trace->angleClamps;
						}
						b.tip = anchor + Scale(direction, b.length);
						b.velocity = Scale(b.tip - oldTip, 1.f / h);
						// Discard radial constraint velocity; retain tangential inertia.
						b.velocity = b.velocity - Scale(direction, Dot(b.velocity, direction));
						const float speed = Length(b.velocity);
						const float maxSpeed = (std::max)(.4f, 15.f * b.length);
						if (speed > maxSpeed)
						{
							b.velocity = Scale(b.velocity, maxSpeed / speed);
							++stats_.clamps;
							if (trace) ++trace->speedClamps;
						}
					}
				}
				b.anchor = anchor;
				b.base = base;
				const Quaternion world = (FromTo(restDirection, Unit(b.tip - anchor, restDirection)) * base).
					Normalized();
				const Quaternion local = (parent.Inverse() * world).Normalized();
				if (Finite(local) && Write(controller, b.transform, local))
				{
					b.lastWritten = local;
					b.written = true;
					++stats_.solvedBones;
					if (trace)
					{
						++trace->solved;
						sample.output = local;
						sample.outputDelta = RotationAngle(local, b.rest) / kRadians;
						sample.velocity = b.velocity;
						trace->Record(sample, peakAcceleration, Length(b.velocity));
					}
				}
				else
				{
					++stats_.invalidSamples;
					if (trace) ++trace->invalid;
					b.initialized = false;
				}
			}
			if (stats_.inputCompared) stats_.inputMeanDeltaDegrees /= static_cast<float>(stats_.inputCompared);
			if (resetFrame) ++stats_.resets; // frames containing any particle reset
			return Finish(start);
		}

	private:
		static constexpr float kRadians = 3.14159265358979323846f / 180.f;

		struct Bone
		{
			std::string name;
			void* transform{};
			void* parent{};
			Quaternion rest{}, base{}, lastWritten{};
			Vector3 axis{}, tip{}, velocity{}, anchor{};
			float length{};
			bool initialized{}, written{};
			bool garment{};
		};

		Settings settings_{};
		Stats stats_{};
		std::vector<Bone> bones_;
		std::vector<void*> body_;
		void* actor_{};
		void* actorTransform_{};
		void* root_{};
		void* animator_{};
		int lastFrame_{-1};

		static float FiniteSetting(float value, float fallback, float lo, float hi)
		{
			return std::clamp(std::isfinite(value) ? value : fallback, lo, hi);
		}

		static bool Finite(Vector3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		static bool Finite(Quaternion q)
		{
			return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
		}

		static float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		static Vector3 Scale(Vector3 v, float s) { return Vector3(v.x * s, v.y * s, v.z * s); }
		static float Length(Vector3 v) { return std::sqrt(Dot(v, v)); }

		static Vector3 Unit(Vector3 v, Vector3 fallback)
		{
			const float length = Length(v);
			return std::isfinite(length) && length > 1e-7f ? Scale(v, 1.f / length) : fallback;
		}

		static Vector3 Cross(Vector3 a, Vector3 b)
		{
			return Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
		}

		static float RotationAngle(Quaternion a, Quaternion b)
		{
			return 2.f * std::acos(std::clamp(std::fabs(Quaternion::Dot(a, b)), 0.f, 1.f));
		}

		static Quaternion FromTo(Vector3 from, Vector3 to)
		{
			const float dot = std::clamp(Dot(from, to), -1.f, 1.f);
			if (dot > .999999f) return Quaternion::Identity();
			if (dot < -.999999f)
			{
				Vector3 axis = Unit(Cross(from, std::fabs(from.x) < .8f ? Vector3(1, 0, 0) : Vector3(0, 1, 0)),
				                    Vector3(0, 0, 1));
				return Quaternion(axis.x, axis.y, axis.z, 0);
			}
			const Vector3 axis = Cross(from, to);
			return Quaternion(axis.x, axis.y, axis.z, 1.f + dot).Normalized();
		}

		static bool Position(IdolController::GetPositionFn fn, void* object, Vector3& out)
		{
			try
			{
				fn(object, &out.x);
				return Finite(out);
			}
			catch (...) { return false; }
		}

		static bool Rotation(IdolController::GetRotationFn fn, void* object, Quaternion& out)
		{
			try
			{
				fn(object, &out.x);
				const float norm = Quaternion::Dot(out, out);
				if (!Finite(out) || norm < .25f || norm > 4.f) return false;
				out = out.Normalized();
				return true;
			}
			catch (...) { return false; }
		}

		static bool Write(const IdolController& c, void* t, Quaternion q)
		{
			try
			{
				c.fnSetLocalRotation(t, q.x, q.y, q.z, q.w);
				return true;
			}
			catch (...) { return false; }
		}
		static bool CaptureWrite(const IdolController& c, void* t, Quaternion q)
		{
#ifdef _MSC_VER
			__try { return Write(c, t, q); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
			return Write(c, t, q);
#endif
		}
		static bool CaptureRotation(IdolController::GetRotationFn fn, void* object, Quaternion& out)
		{
#ifdef _MSC_VER
			__try { return Rotation(fn, object, out); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
			return Rotation(fn, object, out);
#endif
		}

		static bool HasApi(const IdolController& c)
		{
			return c.fnGetWorldPosition && c.fnGetWorldRotation && c.fnGetLocalRotation &&
				c.fnSetLocalRotation && c.fnGetChildCount && c.fnGetChild && c.fnGetTransformName;
		}

		static bool EndsWith(const std::string& name, const char* suffix)
		{
			const size_t n = std::strlen(suffix);
			return name.size() >= n && name.compare(name.size() - n, n, suffix) == 0;
		}

		static bool CostumeName(std::string name, bool garmentOnly = false)
		{
			for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			for (const char* token : {"skirt", "jacket", "belt", "sleeve", "cloth", "dress", "coat", "ribbon"})
				if (name.find(token) != std::string::npos) return true;
			return !garmentOnly && name.find("hair") != std::string::npos;
		}

		bool SameBinding(const IdolBoneBinding& b) const
		{
			return actor_ == b.actor && actorTransform_ == b.actorTransform && root_ == b.rootTransform &&
				animator_ == b.cachedAnimator && body_ == b.boneTransforms;
		}

		void Bind(const IdolBoneBinding& binding, const IdolController& c)
		{
			Clear();
			++stats_.rebinds;
			stats_.skippedCandidates = 0;
			actor_ = binding.actor;
			actorTransform_ = binding.actorTransform;
			root_ = binding.rootTransform;
			animator_ = binding.cachedAnimator;
			body_ = binding.boneTransforms;
			const std::unordered_set<void*> body(body_.begin(), body_.end());
			struct Entry
			{
				void* t;
				void* parent;
				unsigned depth;
			};
			std::vector<Entry> queue{{actorTransform_, nullptr, 0}};
			std::unordered_set<void*> seen{actorTransform_};
			// Bound both traversal cost and the retained solver state.
			for (size_t i = 0; i < queue.size() && i < 2048; ++i)
			{
				const Entry e = queue[i];
				if (e.depth > 96) continue;
				try
				{
					const int count = c.fnGetChildCount(e.t);
					if (count < 0 || count > 256)
					{
						++stats_.invalidSamples;
						continue;
					}
					std::vector<void*> children;
					for (int j = 0; j < count; ++j)
					{
						void* child = c.fnGetChild(e.t, j);
						if (!child || !seen.insert(child).second) continue;
						children.push_back(child);
						if (queue.size() < 2048) queue.push_back({child, e.t, e.depth + 1});
					}
					const std::string name = c.fnGetTransformName(e.t);
					if (!e.parent || body.count(e.t) || !EndsWith(name, "_S") || !CostumeName(name)) continue;
					void* tip = nullptr;
					bool ambiguous = false;
					for (void* child : children)
					{
						const std::string childName = c.fnGetTransformName(child);
						if (body.count(child) || !CostumeName(childName) ||
							(!EndsWith(childName, "_S") && !EndsWith(childName, "_S_End")))
							continue;
						if (tip)
						{
							ambiguous = true;
							break;
						}
						tip = child;
					}
					Bone b;
					b.name = name;
					b.garment = IsGarmentBone(name);
					b.transform = e.t;
					b.parent = e.parent;
					Quaternion world;
					Vector3 origin, end;
					if (!tip || ambiguous || bones_.size() >= 256 ||
						!Rotation(c.fnGetLocalRotation, e.t, b.rest) || !Rotation(c.fnGetWorldRotation, e.t, world) ||
						!Position(c.fnGetWorldPosition, e.t, origin) || !Position(c.fnGetWorldPosition, tip, end))
					{
						++stats_.skippedCandidates;
						continue;
					}
					b.length = Length(end - origin);
					if (!std::isfinite(b.length) || b.length < .002f || b.length > 1.f)
					{
						++stats_.skippedCandidates;
						continue;
					}
					b.axis = world.Inverse().Rotate(Scale(end - origin, 1.f / b.length));
					bones_.push_back(b);
				}
				catch (...) { ++stats_.invalidSamples; }
			}
			stats_.boundBones = bones_.size();
		}

		bool Finish(std::chrono::steady_clock::time_point start)
		{
			const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - start).count();
			stats_.totalCpuNs += static_cast<uint64_t>((std::max<int64_t>)(0, ns));
			++stats_.publishCalls;
			stats_.cpuUs = static_cast<double>((std::max<int64_t>)(0, ns)) / 1000.0;
			stats_.meanCpuUs = static_cast<double>(stats_.totalCpuNs) / (1000.0 * stats_.publishCalls);
			stats_.maxCpuUs = (std::max)(stats_.maxCpuUs, stats_.cpuUs);
			return stats_.solvedBones != 0;
		}
	};
} // namespace GakuMotion
