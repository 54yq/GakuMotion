// ============================================================================
// GENERATED FILE - DO NOT EDIT BY HAND
//
// Auxiliary bone channel model, fitted against captured ground truth.
//
// source recording : official_fmw_saki.json
// captured frames  : 4515
// generated        : 2026-10-05 11:03:43
//
// How this was produced: for every aux channel the dependency test proved the
// aux angle is a pure function of exactly ONE source bone's local rotation.
// Then each candidate functional form was fitted and validated on an UNSEEN
// CONTIGUOUS TIME BLOCK (not interleaved frames). Entries below state which
// form won and how well it scored.
//
// Regenerate with:
//   python tools/export_aux_model.py <recording.json>
// ============================================================================

#pragma once

#include <cmath>

#include "GkMotionData.hpp"

namespace GakuMotion::aux_model
{
	// 0 = fit(raw local rotation, twist about a fixed local axis) -> exact
	// 1 = fit(polynomial over the rotation vector of the source local rotation)
	enum class AuxModelKind : int
	{
		kTwistExact = 0,
		kPolySurrogate = 1,
	};

	struct AuxChannelModel
	{
		const char* auxBone;
		int sourceHid;
		int auxAxis; // 0=x 1=y 2=z (the aux bone's own local rotation axis)
		AuxModelKind kind;
		int sourceAxis; // twist axis for kTwistExact
		float coefficient; // twist coefficient; a perfect fit needs no offset
		int polyDegree;
		const float* polyCoeffs;
		int polyCount;
		float twistOffset; // always 0 for a calibrated twist fit
		float radiusClampDeg; // 输入旋转向量模长上限：多项式只在标定过的旋转流形内可信，
		// 超出时按方向缩放回边界，避免在未见姿态上自由外插
	};

	namespace detail
	{
		inline const float kLeftArmRollH_Poly5[56] = {
			-9.793012161325e-04f,
			-2.999545659206e-01f,
			-1.094432134243e-01f,
			8.036524153286e-02f,
			-3.012345219990e-08f,
			6.847041036572e-04f,
			9.415841679295e-04f,
			2.615394765895e-04f,
			1.721287568750e-04f,
			-2.518451885978e-04f,
			-7.440408863089e-08f,
			5.586290764800e-06f,
			-3.888984309985e-06f,
			-5.183451711047e-06f,
			-7.303924613369e-06f,
			-8.571659357728e-06f,
			-3.078550230302e-06f,
			-4.284537923324e-08f,
			-2.193343278729e-06f,
			2.550292373428e-06f,
			-3.300756897604e-10f,
			-1.114042850075e-08f,
			-1.945413137735e-08f,
			-3.222079913199e-08f,
			-3.219321672840e-08f,
			2.806082381697e-08f,
			1.363693532163e-09f,
			2.794303918449e-08f,
			4.032384150814e-08f,
			3.531721761671e-08f,
			1.111190209833e-08f,
			1.283898365989e-08f,
			4.242025940839e-09f,
			-7.207499396183e-10f,
			-8.099929999069e-09f,
			5.280683243653e-12f,
			-4.555542494788e-11f,
			5.096350390649e-11f,
			-9.940306806280e-11f,
			2.157968133229e-10f,
			7.593580668156e-11f,
			2.341068270233e-10f,
			8.490256231202e-12f,
			3.198289177640e-10f,
			-1.971578492414e-10f,
			1.273481119957e-11f,
			-7.898010905981e-11f,
			-1.759038703382e-10f,
			-1.209031085569e-10f,
			-1.862834206944e-10f,
			-6.417302539312e-11f,
			-4.191729744655e-12f,
			-1.127471061239e-10f,
			5.241723418223e-11f,
			3.512085019900e-12f,
			2.209794697197e-11f,
		};
		inline const float kLeftArmH_Poly5[56] = {
			-2.616506959681e-03f,
			-7.998789971880e-01f,
			-2.918474520898e-01f,
			2.143079532649e-01f,
			-7.830053475458e-08f,
			1.825896174907e-03f,
			2.510905343184e-03f,
			6.974140760108e-04f,
			4.589687991072e-04f,
			-6.716068442373e-04f,
			-1.983951340633e-07f,
			1.489637537027e-05f,
			-1.037084586783e-05f,
			-1.382311031743e-05f,
			-1.947759402834e-05f,
			-2.285783154832e-05f,
			-8.209485662317e-06f,
			-1.140848164546e-07f,
			-5.848937391841e-06f,
			6.800787518382e-06f,
			-8.797477420417e-10f,
			-2.971181461246e-08f,
			-5.188240289967e-08f,
			-8.591708879946e-08f,
			-8.584348552492e-08f,
			7.483505002624e-08f,
			3.635913408057e-09f,
			7.453204821038e-08f,
			1.075382222779e-07f,
			9.417535388474e-08f,
			2.963458796169e-08f,
			3.424240398249e-08f,
			1.133077917547e-08f,
			-1.912867076794e-09f,
			-2.159541540223e-08f,
			1.408972666493e-11f,
			-1.214622647444e-10f,
			1.358921912732e-10f,
			-2.649860824572e-10f,
			5.755086336308e-10f,
			2.025443367942e-10f,
			6.242872564875e-10f,
			2.267200169554e-11f,
			8.528097009723e-10f,
			-5.257879399241e-10f,
			3.401142836965e-11f,
			-2.107663424963e-10f,
			-4.690957333729e-10f,
			-3.225298147131e-10f,
			-4.967050660826e-10f,
			-1.711406923073e-10f,
			-1.121524969812e-11f,
			-3.007824423703e-10f,
			1.396065133025e-10f,
			9.300172385162e-12f,
			5.889747330143e-11f,
		};
		inline const float kRightArmRollH_Poly5[56] = {
			6.511368436465e-04f,
			-2.999801587787e-01f,
			-1.097667066900e-01f,
			8.006453480976e-02f,
			-5.856731515573e-06f,
			6.878151899968e-04f,
			9.570193741350e-04f,
			2.767216311594e-04f,
			2.093441929978e-04f,
			-2.240541730935e-04f,
			-1.133751580370e-07f,
			5.890308762810e-06f,
			-3.542509414289e-06f,
			-5.243078998851e-06f,
			-7.646155361480e-06f,
			-9.048770526800e-06f,
			-3.199887104484e-06f,
			-7.323300393878e-07f,
			-3.589710141258e-06f,
			1.613671532760e-06f,
			2.397323851651e-10f,
			-6.905123168643e-09f,
			-2.055550403888e-08f,
			-3.464855389537e-08f,
			-4.434250359199e-08f,
			2.061605520522e-08f,
			1.566601975760e-09f,
			2.880112477864e-08f,
			3.955345601183e-08f,
			4.091387670115e-08f,
			7.978161911370e-09f,
			1.662475027614e-08f,
			1.962847252673e-08f,
			1.659488801212e-08f,
			4.751405559738e-09f,
			7.608747756236e-12f,
			-2.883687422648e-11f,
			2.418240458953e-11f,
			-1.002705040909e-10f,
			1.452481772525e-10f,
			1.037960604905e-10f,
			2.344619194244e-10f,
			9.858213989351e-11f,
			4.032451259047e-10f,
			-1.225944454009e-10f,
			-5.738717192335e-13f,
			-2.136593421919e-11f,
			-1.691677986293e-10f,
			-4.117730139811e-11f,
			-2.148322120523e-10f,
			-2.983946180135e-11f,
			-1.199789260457e-11f,
			-1.176796654211e-10f,
			-6.944185830385e-11f,
			-4.577413356214e-11f,
			-4.384398413938e-11f,
		};
		inline const float kRightArmH_Poly5[56] = {
			1.728643868993e-03f,
			-7.999470237892e-01f,
			-2.927104750357e-01f,
			2.135059162955e-01f,
			-1.561485258453e-05f,
			1.834182622746e-03f,
			2.552032861459e-03f,
			7.379136578765e-04f,
			5.582825399633e-04f,
			-5.974385586573e-04f,
			-3.023284986025e-07f,
			1.570716421464e-05f,
			-9.447197209675e-06f,
			-1.398146310905e-05f,
			-2.039001051195e-05f,
			-2.412903502506e-05f,
			-8.533315697649e-06f,
			-1.953259026760e-06f,
			-9.574580720514e-06f,
			4.300760344629e-06f,
			6.402826787340e-10f,
			-1.841503332818e-08f,
			-5.481724959372e-08f,
			-9.238805568343e-08f,
			-1.182482555917e-07f,
			5.498950480675e-08f,
			4.166214374771e-09f,
			7.681966671149e-08f,
			1.054735272775e-07f,
			1.090885835694e-07f,
			2.128096997505e-08f,
			4.433018315334e-08f,
			5.236593468049e-08f,
			4.429237911853e-08f,
			1.271070235263e-08f,
			2.030032498736e-11f,
			-7.687839494775e-11f,
			6.447971171280e-11f,
			-2.673331362723e-10f,
			3.873080565341e-10f,
			2.768181419096e-10f,
			6.251542501388e-10f,
			2.629799929659e-10f,
			1.075228992646e-09f,
			-3.270213164699e-10f,
			-1.458030662524e-12f,
			-5.703730167480e-11f,
			-4.511689012134e-10f,
			-1.098402659552e-10f,
			-5.728471358582e-10f,
			-7.959270526151e-11f,
			-3.199041430542e-11f,
			-3.138452975910e-10f,
			-1.853810982762e-10f,
			-1.223199627606e-10f,
			-1.171473133323e-10f,
		};
	} // namespace detail

	inline const AuxChannelModel* GetChannels() noexcept
	{
		static const AuxChannelModel kChannels[] = {
			{
				"LeftArm_Roll_H", 13, 0, AuxModelKind::kPolySurrogate, 0, 0.0f, 5, detail::kLeftArmRollH_Poly5, 56,
				0.0f, 140.208354f
			},
			{
				"LeftArm_H", 13, 0, AuxModelKind::kPolySurrogate, 0, 0.0f, 5, detail::kLeftArmH_Poly5, 56, 0.0f,
				140.208354f
			},
			{
				"LeftForeArm_Roll_H", 17, 0, AuxModelKind::kTwistExact, 0, 0.366666673f, 0, nullptr, 0, 0.000000559f,
				0.0f
			},
			{"LeftForeArm_H", 15, 1, AuxModelKind::kTwistExact, 1, -0.400000038f, 0, nullptr, 0, 0.000000831f, 0.0f},
			{"LeftHand_H", 17, 0, AuxModelKind::kTwistExact, 0, 0.659999993f, 0, nullptr, 0, -0.000000072f, 0.0f},
			{
				"RightArm_Roll_H", 14, 0, AuxModelKind::kPolySurrogate, 0, 0.0f, 5, detail::kRightArmRollH_Poly5, 56,
				0.0f, 147.504060f
			},
			{
				"RightArm_H", 14, 0, AuxModelKind::kPolySurrogate, 0, 0.0f, 5, detail::kRightArmH_Poly5, 56, 0.0f,
				147.504060f
			},
			{
				"RightForeArm_Roll_H", 18, 0, AuxModelKind::kTwistExact, 0, 0.366666660f, 0, nullptr, 0, 0.000000752f,
				0.0f
			},
			{"RightForeArm_H", 16, 1, AuxModelKind::kTwistExact, 1, -0.400000060f, 0, nullptr, 0, 0.000002331f, 0.0f},
			{"RightHand_H", 18, 0, AuxModelKind::kTwistExact, 0, 0.659999942f, 0, nullptr, 0, 0.000000662f, 0.0f},
		};
		return kChannels;
	}

	inline int GetChannelCount() noexcept { return 10; }

	// Rotation vector of a unit quaternion, components in degrees.
	inline void RotationVectorDeg(const Quaternion& q, float out[3]) noexcept
	{
		const float x = q.x, y = q.y, z = q.z;
		float w = q.w;
		if (w > 1.0f) w = 1.0f;
		if (w < -1.0f) w = -1.0f;
		const float n = std::sqrt(x * x + y * y + z * z);
		if (n < 1e-9f)
		{
			out[0] = out[1] = out[2] = 0.0f;
			return;
		}
		const float k = 114.591559f * std::acos(w) / n; // degrees, matches 2*180/pi
		out[0] = k * x;
		out[1] = k * y;
		out[2] = k * z;
	}

	inline float TwistAngleDeg(const Quaternion& q, int axis) noexcept
	{
		const float comp = (axis == 0) ? q.x : ((axis == 1) ? q.y : q.z);
		return 114.591559f * std::atan2(comp, q.w);
	}

	// Evaluates the polynomial in exactly the same term order the fitter used
	// (itertools.combinations_with_replacement over three rotation-vector axes).
	inline float EvaluatePoly(const float* coeffs, int count, const float v[3]) noexcept
	{
		float total = coeffs[0];
		int used = 1;
		for (int a = 0; a < 3 && used < count; ++a)
		{
			total += coeffs[used++] * v[a];
		}
		for (int a = 0; a < 3 && used < count; ++a)
			for (int b = a; b < 3 && used < count; ++b)
			{
				total += coeffs[used++] * v[a] * v[b];
			}
		for (int a = 0; a < 3 && used < count; ++a)
			for (int b = a; b < 3 && used < count; ++b)
				for (int c = b; c < 3 && used < count; ++c)
				{
					total += coeffs[used++] * v[a] * v[b] * v[c];
				}
		for (int a = 0; a < 3 && used < count; ++a)
			for (int b = a; b < 3 && used < count; ++b)
				for (int c = b; c < 3 && used < count; ++c)
					for (int d = c; d < 3 && used < count; ++d)
					{
						total += coeffs[used++] * v[a] * v[b] * v[c] * v[d];
					}
		for (int a = 0; a < 3 && used < count; ++a)
			for (int b = a; b < 3 && used < count; ++b)
				for (int c = b; c < 3 && used < count; ++c)
					for (int d = c; d < 3 && used < count; ++d)
						for (int e = d; e < 3 && used < count; ++e)
						{
							total += coeffs[used++] * v[a] * v[b] * v[c] * v[d] * v[e];
						}
		for (int a = 0; a < 3 && used < count; ++a)
			for (int b = a; b < 3 && used < count; ++b)
				for (int c = b; c < 3 && used < count; ++c)
					for (int d = c; d < 3 && used < count; ++d)
						for (int e = d; e < 3 && used < count; ++e)
							for (int f = e; f < 3 && used < count; ++f)
							{
								total += coeffs[used++] * v[a] * v[b] * v[c] * v[d] * v[e] * v[f];
							}
		return total;
	}

	inline float EvaluateAuxAngleDeg(const AuxChannelModel& m, const Quaternion& srcLocal) noexcept
	{
		if (m.kind == AuxModelKind::kTwistExact)
		{
			return m.coefficient * TwistAngleDeg(srcLocal, m.sourceAxis) + m.twistOffset;
		}
		float v[3];
		RotationVectorDeg(srcLocal, v);
		if (m.radiusClampDeg > 0.0f)
		{
			const float r = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
			if (r > m.radiusClampDeg)
			{
				const float s = m.radiusClampDeg / r;
				v[0] *= s;
				v[1] *= s;
				v[2] *= s;
			}
		}
		return EvaluatePoly(m.polyCoeffs, m.polyCount, v);
	}
}
