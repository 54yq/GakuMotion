#include "GkShadowPoseSnapshot.hpp"
#include <array>
#include <cassert>
#include <cstdio>
struct Vec { float x{}, y{}, z{}; };
int main()
{
	int actors[3]{};
	std::array<ShadowPoseSnapshot<Vec>, 3> poses{};
	for (int i = 0; i < 3; ++i) poses[i] = {&actors[i], 7, 120, {float(i),0,0}, {float(i)+1,1,1}};
	for (int i = 0; i < 3; ++i) {
		assert(poses[i].Matches(&actors[i], 7, 120));
		assert(!poses[i].Matches(&actors[(i+1)%3], 7, 120));
		assert(!poses[i].Matches(&actors[i], 8, 120));
		assert(!poses[i].Matches(&actors[i], 7, 121));
		assert(!poses[i].Matches(nullptr, 7, 120));
	}
	poses[1] = {};
	assert(!poses[1].Matches(&actors[1], 7, 120));
	assert(poses[0].Matches(&actors[0], 7, 120) && poses[2].Matches(&actors[2], 7, 120));
	std::puts("Shadow pose snapshots: actor isolation, stale serial/frame and reset passed");
}
