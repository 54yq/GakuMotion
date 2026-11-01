#pragma once

#include <algorithm>
#include <cmath>

namespace KineTexture
{
	// These layouts and arithmetic expressions preserve the original scalar API.
	struct Vector3
	{
		float x{0.0f}, y{0.0f}, z{0.0f};

		Vector3() = default;
		Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

		Vector3 operator+(const Vector3& rhs) const noexcept
		{
			return Vector3(x + rhs.x, y + rhs.y, z + rhs.z);
		}

		Vector3 operator-(const Vector3& rhs) const noexcept
		{
			return Vector3(x - rhs.x, y - rhs.y, z - rhs.z);
		}

		static Vector3 Lerp(const Vector3& a, const Vector3& b, float t) noexcept
		{
			return Vector3(
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t
			);
		}
	};

	struct Quaternion
	{
		float x{0.0f}, y{0.0f}, z{0.0f}, w{1.0f};

		Quaternion() = default;
		Quaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

		static Quaternion Identity() noexcept
		{
			return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
		}

		static float Dot(const Quaternion& a, const Quaternion& b) noexcept
		{
			return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		}

		Quaternion operator*(const Quaternion& rhs) const noexcept
		{
			return Quaternion(
				w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
				w * rhs.y + y * rhs.w + z * rhs.x - x * rhs.z,
				w * rhs.z + z * rhs.w + x * rhs.y - y * rhs.x,
				w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z
			);
		}

		// Conjugate: callers must supply unit rotations, as in the original API.
		Quaternion Inverse() const noexcept
		{
			return Quaternion(-x, -y, -z, w);
		}

		Vector3 Rotate(const Vector3& v) const noexcept
		{
			float tx = 2.0f * (y * v.z - z * v.y);
			float ty = 2.0f * (z * v.x - x * v.z);
			float tz = 2.0f * (x * v.y - y * v.x);
			return Vector3(
				v.x + w * tx + (y * tz - z * ty),
				v.y + w * ty + (z * tx - x * tz),
				v.z + w * tz + (x * ty - y * tx)
			);
		}

		static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) noexcept
		{
			float dot = Dot(a, b);
			Quaternion b_copy = b;
			if (dot < 0.0f)
			{
				dot = -dot;
				b_copy = Quaternion(-b.x, -b.y, -b.z, -b.w);
			}
			if (dot > 0.9995f)
			{
				float rx = a.x + (b_copy.x - a.x) * t;
				float ry = a.y + (b_copy.y - a.y) * t;
				float rz = a.z + (b_copy.z - a.z) * t;
				float rw = a.w + (b_copy.w - a.w) * t;
				float len = std::sqrt(rx * rx + ry * ry + rz * rz + rw * rw);
				if (len > 1e-6f)
				{
					float inv = 1.0f / len;
					return Quaternion(rx * inv, ry * inv, rz * inv, rw * inv);
				}
				return a;
			}
			float theta_0 = std::acos(std::max(-1.0f, std::min(1.0f, dot)));
			float theta = theta_0 * t;
			float sin_theta = std::sin(theta);
			float sin_theta_0 = std::sin(theta_0);
			float s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
			float s1 = sin_theta / sin_theta_0;
			return Quaternion(
				s0 * a.x + s1 * b_copy.x,
				s0 * a.y + s1 * b_copy.y,
				s0 * a.z + s1 * b_copy.z,
				s0 * a.w + s1 * b_copy.w
			);
		}

		static Quaternion FromEulerDegreesXYZ(float pitchDeg, float yawDeg, float rollDeg) noexcept
		{
			constexpr float deg2rad = 3.14159265358979323846f / 180.0f;
			float p = pitchDeg * deg2rad * 0.5f;
			float y = yawDeg * deg2rad * 0.5f;
			float r = rollDeg * deg2rad * 0.5f;
			float sinP = std::sin(p), cosP = std::cos(p);
			float sinY = std::sin(y), cosY = std::cos(y);
			float sinR = std::sin(r), cosR = std::cos(r);
			return Quaternion(
				sinP * cosY * cosR + cosP * sinY * sinR,
				cosP * sinY * cosR - sinP * cosY * sinR,
				cosP * cosY * sinR + sinP * sinY * cosR,
				cosP * cosY * cosR - sinP * sinY * sinR
			);
		}

		// Zero handling is preserved; ingestion validators reject nonfinite input.
		Quaternion Normalized() const noexcept
		{
			float len2 = x * x + y * y + z * z + w * w;
			if (len2 <= 1e-12f) return Identity();
			float inv = 1.0f / std::sqrt(len2);
			return Quaternion(x * inv, y * inv, z * inv, w * inv);
		}
	};
	static_assert(sizeof(Vector3) == 12);
	static_assert(sizeof(Quaternion) == 16);
}
