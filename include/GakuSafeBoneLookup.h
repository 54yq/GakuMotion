#pragma once

// Kept identical in GakuRecorder/include and GakuMotion/include.
// Unity getters can throw managed exceptions during actor initialization or
// teardown. Invoke through IL2CPP so exceptions never unwind into host hooks.
namespace GakuRuntime
{
	struct SafeBoneLookup
	{
		using RuntimeInvoke = void* (*)(const void*, void*, void**, void**);
		using ObjectUnbox = void* (*)(void*);
		RuntimeInvoke invoke{nullptr};
		ObjectUnbox unbox{nullptr};
		const void* actorBone{nullptr};
		const void* animatorBone{nullptr};
		const void* animatorIsHuman{nullptr};
		const void* animatorIsInitialized{nullptr};

		void* ActorBone(void* actor, int boneId) const
		{
			if (!invoke || !actorBone || !actor || boneId < 0 || boneId >= 55) return nullptr;
			void* args[] = {&boneId};
			void* exception = nullptr;
			void* result = invoke(actorBone, actor, args, &exception);
			return exception ? nullptr : result;
		}

		bool ReadFlag(const void* method, void* instance) const
		{
			if (!invoke || !unbox || !method || !instance) return false;
			void* exception = nullptr;
			void* boxed = invoke(method, instance, nullptr, &exception);
			if (exception || !boxed) return false;
			void* value = unbox(boxed);
			return value && *static_cast<const bool*>(value);
		}

		void* AnimatorBone(void* animator, int boneId) const
		{
			if (!animatorBone || boneId < 0 || boneId >= 55 ||
				!ReadFlag(animatorIsInitialized, animator) ||
				!ReadFlag(animatorIsHuman, animator))
				return nullptr;
			void* args[] = {&boneId};
			void* exception = nullptr;
			void* result = invoke(animatorBone, animator, args, &exception);
			return exception ? nullptr : result;
		}
	};
} // namespace GakuRuntime
