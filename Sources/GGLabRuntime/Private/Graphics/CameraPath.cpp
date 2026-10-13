#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Graphics/Camera.h"

#include <cmath>
#include <cstddef>

namespace gglab
{
	namespace
	{
		// Matches the camera's pitch clamp, so a valid key restores without clamping.
		constexpr float MaxPathPitchRadians = math::ToRadians(85.0f);

		[[nodiscard]] bool IsKeyValid(const CameraPathKey& key) noexcept
		{
			Vector3 forward;
			return math::IsFinite(key.m_Position) && math::IsFinite(key.m_Target) &&
				math::TryNormalize(key.m_Target - key.m_Position, forward) &&
				std::abs(forward.m_Y) <= std::sin(MaxPathPitchRadians) &&
				math::IsFinite(key.m_VerticalFovDegrees) &&
				Camera::ClampFov(key.m_VerticalFovDegrees) == key.m_VerticalFovDegrees;
		}

		template <typename T>
		[[nodiscard]] T Interpolate(CameraPathInterpolation interpolation, const T& p0,
			const T& p1, const T& p2, const T& p3, float t) noexcept
		{
			if (interpolation == CameraPathInterpolation::Linear)
			{
				return p1 + (p2 - p1) * t;
			}
			const float t2 = t * t;
			const float t3 = t2 * t;
			return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
				(p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
		}
	}

	bool IsCameraPathValid(const CameraPath& path) noexcept
	{
		if (path.m_Id.empty() || path.m_Name.empty() || path.m_Version == 0 ||
			path.m_Keys.empty() || path.m_Keys.front().m_Frame != 0 ||
			!math::IsFinite(path.m_NearPlane) || !math::IsFinite(path.m_FarPlane) ||
			Camera::ClampNear(path.m_NearPlane) != path.m_NearPlane ||
			Camera::ClampFar(path.m_NearPlane, path.m_FarPlane) != path.m_FarPlane ||
			!math::IsFinite(path.m_ManualEV100) ||
			Camera::ClampManualEV100(path.m_ManualEV100) != path.m_ManualEV100 ||
			!math::IsFinite(path.m_ExposureCompensationEV) ||
			Camera::ClampExposureCompensationEV(path.m_ExposureCompensationEV) !=
				path.m_ExposureCompensationEV)
		{
			return false;
		}
		for (size_t index = 0; index < path.m_Keys.size(); ++index)
		{
			if (!IsKeyValid(path.m_Keys[index]) ||
				(index > 0 && path.m_Keys[index].m_Frame <= path.m_Keys[index - 1].m_Frame))
			{
				return false;
			}
		}
		return true;
	}

	uint32_t GetCameraPathFrameCount(const CameraPath& path) noexcept
	{
		return IsCameraPathValid(path) ? path.m_Keys.back().m_Frame + 1 : 0;
	}

	std::optional<CameraPathPose> EvaluateCameraPath(
		const CameraPath& path, uint32_t frame) noexcept
	{
		if (frame >= GetCameraPathFrameCount(path))
		{
			return std::nullopt;
		}

		const std::vector<CameraPathKey>& keys = path.m_Keys;
		size_t current = 0;
		while (current + 1 < keys.size() && keys[current + 1].m_Frame <= frame)
		{
			++current;
		}
		size_t shotBegin = current;
		while (shotBegin > 0 && !keys[shotBegin].m_Cut)
		{
			--shotBegin;
		}
		size_t shotEnd = current;
		while (shotEnd + 1 < keys.size() && !keys[shotEnd + 1].m_Cut)
		{
			++shotEnd;
		}

		const CameraPathKey& key1 = keys[current];
		const bool cut = frame == 0 || (key1.m_Cut && key1.m_Frame == frame);
		if (current == shotEnd)
		{
			return CameraPathPose{
				.m_Position = key1.m_Position,
				.m_Target = key1.m_Target,
				.m_VerticalFovDegrees = key1.m_VerticalFovDegrees,
				.m_Cut = cut,
			};
		}

		const CameraPathKey& key0 = current > shotBegin ? keys[current - 1] : key1;
		const CameraPathKey& key2 = keys[current + 1];
		const CameraPathKey& key3 = current + 2 <= shotEnd ? keys[current + 2] : key2;
		const float t = static_cast<float>(frame - key1.m_Frame) /
			static_cast<float>(key2.m_Frame - key1.m_Frame);
		return CameraPathPose{
			.m_Position = Interpolate(path.m_Interpolation, key0.m_Position, key1.m_Position,
				key2.m_Position, key3.m_Position, t),
			.m_Target = Interpolate(path.m_Interpolation, key0.m_Target, key1.m_Target,
				key2.m_Target, key3.m_Target, t),
			.m_VerticalFovDegrees = Interpolate(path.m_Interpolation,
				key0.m_VerticalFovDegrees, key1.m_VerticalFovDegrees,
				key2.m_VerticalFovDegrees, key3.m_VerticalFovDegrees, t),
			.m_Cut = cut,
		};
	}
}
