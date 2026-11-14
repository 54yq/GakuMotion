#pragma once
#include <cstdint>

template<class Vector>
struct ShadowPoseSnapshot
{
	void* actor{};
	uint64_t serial{};
	int frame{-1};
	Vector min{}, max{};
	bool Matches(void* expectedActor, uint64_t expectedSerial, int currentFrame) const
	{
		return expectedActor && actor == expectedActor && serial == expectedSerial &&
			frame >= 0 && frame == currentFrame;
	}
};
