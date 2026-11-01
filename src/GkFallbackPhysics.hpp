#pragma once
#include "GkIdolController.hpp"
#include <chrono>
#include <cctype>
#include <unordered_set>

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
		};

		FallbackPhysics() = default;

		explicit FallbackPhysics(Settings settings) : settings_(settings)
		{
		}

		const Stats& GetStats() const noexcept { return stats_; }
		void* Actor() const noexcept { return actor_; }
		bool HasCaptureWrites() const noexcept
		{
			for (const auto& b : bones_) if (b.written) return true;
			return false;
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

		// Optional live-object cleanup before switching modes/Stop. Never use it
		// after scene destruction; Clear is the lifecycle escape hatch then.
		void Restore(const IdolController& controller)
		{
			if (controller.fnSetLocalRotation)
			{
				for (const Bone& b : bones_) Write(controller, b.transform, b.rest);
			}
			Clear();
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
		             float dt, int frame)
		{
			if (frame >= 0 && frame == lastFrame_ && SameBinding(binding)) return false;
			const auto start = std::chrono::steady_clock::now();
			++stats_.frames;
			stats_.solvedBones = 0;
			if (!binding.bindPoseCaptured || !binding.actor || !binding.actorTransform ||
				!controller.CanDriveActor(binding.actor) || !HasApi(controller))
			{
				Clear();
				return Finish(start);
			}
			if (!SameBinding(binding)) Bind(binding, controller);
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
			bool resetFrame = reset;
			for (Bone& b : bones_)
			{
				Vector3 anchor;
				Quaternion parent;
				if (!Position(controller.fnGetWorldPosition, b.transform, anchor) ||
					!Rotation(controller.fnGetWorldRotation, b.parent, parent))
				{
					++stats_.invalidSamples;
					b.initialized = false;
					continue;
				}
				const Quaternion base = (parent * b.rest).Normalized();
				const Vector3 restDirection = base.Rotate(b.axis);
				const Vector3 restTip = anchor + Scale(restDirection, b.length);
				const bool teleport = b.initialized &&
				(Length(anchor - b.anchor) > (std::max)(.20f, 3.f * b.length) ||
					RotationAngle(base, b.base) > 75.f * kRadians);
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
					b.written = true;
					++stats_.solvedBones;
				}
				else
				{
					++stats_.invalidSamples;
					b.initialized = false;
				}
			}
			if (resetFrame) ++stats_.resets; // frames containing any particle reset
			return Finish(start);
		}

	private:
		static constexpr float kRadians = 3.14159265358979323846f / 180.f;

		struct Bone
		{
			void* transform{};
			void* parent{};
			Quaternion rest{}, base{};
			Vector3 axis{}, tip{}, velocity{}, anchor{};
			float length{};
			bool initialized{}, written{};
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

		static bool CostumeName(std::string name)
		{
			for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			for (const char* token : {"hair", "skirt", "jacket", "belt", "sleeve", "cloth", "dress", "coat", "ribbon"})
				if (name.find(token) != std::string::npos) return true;
			return false;
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
