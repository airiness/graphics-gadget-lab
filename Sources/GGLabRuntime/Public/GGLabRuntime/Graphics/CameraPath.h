#pragma once

#include "GGLabRuntime/Core/Math/Vector.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	enum class CameraPathInterpolation : uint8_t
	{
		// Constant velocity between keys.
		Linear,
		// Uniform Catmull-Rom through the keys of one shot; shot ends repeat their key.
		CatmullRom,
	};

	// One authored main-camera pose at an integer sequence frame, in runtime coordinates.
	struct CameraPathKey
	{
		uint32_t m_Frame = 0;
		Vector3 m_Position = Vector3::Zero;
		Vector3 m_Target = Vector3::Forward;
		float m_VerticalFovDegrees = 60.0f;
		// Starts a new shot: the pose at this key is a camera cut that resets temporal
		// history. Frame 0 always starts a shot. Interpolation never crosses a cut;
		// frames between the last key of a shot and the next cut hold that last pose.
		bool m_Cut = false;
	};

	// Scene-authored camera motion for deterministic temporal sequences. A pose is a
	// pure function of the integer sequence frame, independent of wall-clock and
	// simulation time. The version identifies the motion: change it whenever a key,
	// the interpolation or a projection/exposure field changes, so recorded evidence
	// remains comparable by (id, version).
	struct CameraPath
	{
		std::string m_Id;
		std::string m_Name;
		std::string m_Purpose;
		uint32_t m_Version = 1;
		CameraPathInterpolation m_Interpolation = CameraPathInterpolation::Linear;
		float m_NearPlane = 0.1f;
		float m_FarPlane = 1000.0f;
		float m_ManualEV100 = 0.0f;
		float m_ExposureCompensationEV = 0.0f;
		// First key at frame 0; frames strictly increase.
		std::vector<CameraPathKey> m_Keys;
	};

	struct CameraPathPose
	{
		Vector3 m_Position = Vector3::Zero;
		Vector3 m_Target = Vector3::Forward;
		float m_VerticalFovDegrees = 60.0f;
		// True on frame 0 and on the frame of every cut key.
		bool m_Cut = false;
	};

	// Requires an id, name and non-zero version; a first key at frame 0 and strictly
	// increasing frames; finite poses whose view direction stays within the camera's
	// pitch limit; and FOV, near/far and exposure values that the camera accepts
	// without clamping.
	[[nodiscard]] bool IsCameraPathValid(const CameraPath& path) noexcept;
	// Last key frame + 1; zero for an invalid path.
	[[nodiscard]] uint32_t GetCameraPathFrameCount(const CameraPath& path) noexcept;
	// Empty for an invalid path or a frame at or beyond the frame count.
	[[nodiscard]] std::optional<CameraPathPose> EvaluateCameraPath(
		const CameraPath& path, uint32_t frame) noexcept;
}
