#pragma once

#include "GkWaistSupport.hpp"
#include <limits>

namespace waistSupportTest
{
	using namespace GakuMotion;

	inline bool Near(const Vector3& a, const Vector3& b, float tolerance = 2e-6f)
	{
		return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance &&
			std::fabs(a.z - b.z) <= tolerance;
	}

	inline bool SameRotation(const Quaternion& a, const Quaternion& b)
	{
		const auto left = a.Normalized(), right = b.Normalized();
		const Quaternion difference = (left.Inverse() * right).Normalized();
		// Vector part avoids float acos losing precision near zero.
		const float sine = std::sqrt(difference.x * difference.x + difference.y * difference.y +
			difference.z * difference.z);
		return 2.f * std::atan2(sine, std::fabs(difference.w)) < 1e-5f;
	}

	inline Vector3 Scaled(const Vector3& v, float scale)
	{
		return {v.x * scale, v.y * scale, v.z * scale};
	}

	inline bool Cleared(const waist_support::Result& result)
	{
		return Near(result.localPosition, {0, 0, 0}, 0.f) &&
			result.localRotation.x == 0 && result.localRotation.y == 0 &&
			result.localRotation.z == 0 && result.localRotation.w == 1;
	}
}

static void TestWaistSupport()
{
	using namespace GakuMotion;
	using namespace waistSupportTest;
	BeginTest("waist support: captured geometric helper law with bounded input domain");
	waist_support::Result result;
	Check(waist_support::Solve({1, 2, 3}, Quaternion::Identity(), {1, 1.6f, 3}, result) &&
	      Near(result.localPosition, {0, -.2f, 0}) &&
	      SameRotation(result.localRotation, Quaternion::Identity()),
	      "neutral reference pair returns midpoint below Waist_O with identity rotation");

	const Vector3 waist{1, 2, 3}, thigh{1.3f, 1.6f, 3.2f};
	Check(waist_support::Solve(waist, Quaternion::Identity(), thigh, result) &&
	      Near(result.localPosition, {.15f, -.2f, .1f}),
	      "oblique pair produces an absolute local midpoint, including all translation axes");
	const Vector3 axis = Scaled(thigh - waist, 1.f / std::sqrt(.29f));
	Check(Near(result.localRotation.Rotate({0, -1, 0}), axis) && result.localRotation.y == 0.f &&
	      result.localRotation.w >= 0.f &&
	      Near(waist + result.localPosition, Vector3::Lerp(waist, thigh, .5f)),
	      "helper down axis follows the thigh direction with zero free twist and correct world midpoint");

	const auto originalResult = result;
	const Quaternion localFrame = Quaternion::FromEulerDegreesXYZ(-20, 15, 7).Normalized();
	const Vector3 translatedWaist{-2, 1.7f, 4};
	const Vector3 framedThigh = translatedWaist + localFrame.Rotate(thigh - waist);
	waist_support::Result framed;
	Check(waist_support::Solve(translatedWaist, localFrame, framedThigh, framed) &&
	      Near(framed.localPosition, originalResult.localPosition) &&
	      SameRotation(framed.localRotation, originalResult.localRotation),
	      "inverse world-to-local frame recovers the same pose after reference-frame rotation and translation");
	const Quaternion actorTurn = Quaternion::FromEulerDegreesXYZ(25, 120, -34).Normalized();
	const Vector3 actorShift{-.7f, 2.2f, 1.9f};
	waist_support::Result transformed;
	Check(waist_support::Solve(actorShift + actorTurn.Rotate(translatedWaist), actorTurn * localFrame,
	                           actorShift + actorTurn.Rotate(framedThigh), transformed) &&
	      Near(transformed.localPosition, framed.localPosition) &&
	      SameRotation(transformed.localRotation, framed.localRotation),
	      "subject global orientation and world placement do not change helper local pose");
	const auto negativeFrame = Quaternion(-localFrame.x, -localFrame.y, -localFrame.z, -localFrame.w);
	Check(waist_support::Solve(translatedWaist, negativeFrame, framedThigh, transformed) &&
	      Near(transformed.localPosition, framed.localPosition) &&
	      SameRotation(transformed.localRotation, framed.localRotation),
	      "equivalent quaternion signs do not cause a support flip");

	Check(waist_support::IsUnitScale({1, 1, 1}) &&
	      waist_support::IsUnitScale({1.00005f, .99995f, 1}) &&
	      !waist_support::IsUnitScale({1.001f, 1, 1}) &&
	      !waist_support::IsUnitScale({1, 0, 1}) &&
	      !waist_support::IsUnitScale({-1, 1, 1}),
	      "explicit scale gate accepts rounding and rejects scaling, collapse, or reflection");
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	Check(!waist_support::IsUnitScale({nan, 1, 1}) && !waist_support::IsUnitScale({1, inf, 1}),
	      "nonfinite effective scale cannot enter the bounded runtime candidate");
	bool invalidRejected = true;
	const Quaternion invalidRotations[] = {{0, 0, 0, 0}, {0, 0, 0, 2}, {nan, 0, 0, 1}, {0, inf, 0, 1}};
	for (const auto& rotation : invalidRotations)
	{
		result = originalResult;
		invalidRejected &= !waist_support::Solve(waist, rotation, thigh, result) && Cleared(result);
	}
	Check(invalidRejected, "invalid rotations fail closed and clear any previous valid output");
	Check(!waist_support::Solve({nan, 0, 0}, Quaternion::Identity(), thigh, result) && Cleared(result) &&
	      !waist_support::Solve(waist, Quaternion::Identity(), {0, 0, inf}, result) && Cleared(result),
	      "nonfinite reference positions fail closed before any pose can be applied");
	Check(!waist_support::Solve(waist, Quaternion::Identity(), waist, result) && Cleared(result) &&
	      !waist_support::Solve({0, 0, 0}, Quaternion::Identity(), {0, -.000001f, 0}, result) && Cleared(result),
	      "coincident and sub-ten-micrometre reference pairs cannot define an axis");
	Check(!waist_support::Solve({0, 0, 0}, Quaternion::Identity(), {0, .3f, 0}, result) && Cleared(result) &&
	      !waist_support::Solve({0, 0, 0}, Quaternion::Identity(), {.00001f, .3f, 0}, result) && Cleared(result),
	      "antiparallel down/up direction is rejected without inventing a bend plane");

	// Independent frozen reference Transform captures. These values are copied
	// from the recorder, not generated by Solve. All source takes have
	// rig_sample_phase=recorder_actor_late_update, rig_final_render_state=false.
	// Extraction: build/joint_axis_hips/extract_test_fixtures.py. Full-frame
	// verification and source hashes: build/joint_axis_hips/hip_geometry_truth.json.
	struct Capture
	{
		Vector3 waistWorldPosition;
		Quaternion waistWorldRotation;
		Vector3 thighWorldPosition;
		Vector3 expectedLocalPosition;
		Quaternion expectedLocalRotation;
	};
	const Capture captures[] = {
		// jsna_live_official_campus_mode_A_End.json
		// SHA256 e7d945047d69cc166edb082a9406caa405189f3254d10d6743782a8a763e2ae6
		// Left frame950 t15.878100 / Right frame878 t14.673802.
		{
			{-.152069f, 2.095809f, -.080186f}, {.196320f, -.121060f, -.052251f, .971634f},
			{-.154676f, 1.865407f, -.081103f}, {.015817f, -.105943f, .042416f}, {-.187898f, 0, .070066f, .979686f}
		},
		{
			{.227984f, 2.121185f, -.177773f}, {.153789f, .276656f, .048587f, .947338f},
			{.248824f, 1.876004f, -.177642f}, {-.012975f, -.116265f, .038095f}, {-.156990f, 0, -.053469f, .986152f}
		},
		// jsna_live_alternative_Fighting_My_Way.json
		// SHA256 b93193f081f0349528f54e63738c0e610ddc20b5ada038e2182e3f80b55024af
		// Left frame970 t16.164351 / Right frame1022 t17.026836.
		{
			{.061826f, 1.548563f, .969979f}, {.013870f, .482334f, -.034871f, -.875183f},
			{-.018411f, 1.368909f, 1.007292f}, {-.012306f, -.088743f, .044718f}, {-.229931f, 0, -.063272f, .971148f}
		},
		{
			{-.059149f, 1.486895f, 1.026937f}, {.059412f, -.296824f, .134587f, -.943532f},
			{-.025143f, 1.300354f, 1.114982f}, {.016415f, -.093967f, .042745f}, {-.209835f, 0, .080579f, .974411f}
		},
		// official_costume_rig_1.json (Saki)
		// SHA256 45f9a067980f1491475f4914f309188b7760c8c4f5811eb787d35ec4bd482fef
		// Left frame970 t16.166939 / Right frame2560 t42.948948.
		{
			{.070640f, 1.435523f, .976283f}, {.012712f, .482883f, -.034245f, -.874923f},
			{-.000676f, 1.275340f, 1.009126f}, {-.010866f, -.079087f, .039787f}, {-.229631f, 0, -.062716f, .971255f}
		},
		{
			{-.016020f, 1.516291f, 1.395488f}, {.326414f, .450578f, -.140808f, -.818906f},
			{-.017664f, 1.485280f, 1.545267f}, {.039789f, -.061178f, .022883f}, {-.157695f, 0, .274198f, .948656f}
		}
	};
	bool capturePositions = true, captureRotations = true;
	for (const auto& capture : captures)
	{
		const bool valid = waist_support::Solve(capture.waistWorldPosition, capture.waistWorldRotation,
		                                        capture.thighWorldPosition, result);
		capturePositions &= valid && Near(result.localPosition, capture.expectedLocalPosition);
		captureRotations &= valid && SameRotation(result.localRotation, capture.expectedLocalRotation);
	}
	Check(capturePositions,
	      "six independent reference captures reproduce recorded local position within two micrometres");
	Check(captureRotations,
	      "six independent reference captures reproduce full recorded quaternion within .000573 degrees");
}
