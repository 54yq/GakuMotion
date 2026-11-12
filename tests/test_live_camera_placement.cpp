#include "GkCamera.hpp"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace GakuMotion;
int main()
{
	int failures = 0;
	auto check = [&](bool ok, const char* message) { if (!ok) { ++failures; std::printf("FAIL: %s\n", message); } };
	auto near = [](Vector3 a, Vector3 b) {
		return std::fabs(a.x-b.x) < 1e-4f && std::fabs(a.y-b.y) < 1e-4f && std::fabs(a.z-b.z) < 1e-4f;
	};
	CameraFrame frame;
	frame.position = {2, 1, 3};
	frame.rotation = Quaternion::FromEulerDegreesXYZ(0, 90, 0);
	Vector3 offset;
	check(CameraFrontPlacement(frame, {-4, 1, 3}, {1, 0, 0}, 4, offset) && near(offset, {11, 0, 0}),
	      "places a character behind a rotated camera in front");
	Vector3 again;
	check(CameraFrontPlacement(frame, {6, 1, 3}, offset, 4, again) && near(again, offset),
	      "repeating after pose update does not accumulate");
	frame.rotation = {0, 0, 0, 0};
	check(!CameraFrontPlacement(frame, {0, 0, 0}, {0, 0, 0}, 4, again), "zero quaternion rejected");
	frame.rotation = Quaternion::Identity();
	frame.position.x = std::numeric_limits<float>::quiet_NaN();
	check(!CameraFrontPlacement(frame, {0, 0, 0}, {0, 0, 0}, 4, again), "nonfinite camera rejected");
	std::printf("Live camera placement: %d failures\n", failures);
	return failures ? 1 : 0;
}
