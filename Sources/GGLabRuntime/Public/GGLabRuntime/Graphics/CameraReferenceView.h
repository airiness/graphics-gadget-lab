#pragma once

#include "GGLabRuntime/Core/Math/Vector.h"

#include <cstdint>
#include <string>

namespace gglab
{
	// Scene-authored poses for the main perspective camera, in runtime coordinates.
	// Reference aspect documents composition; restoration keeps the current viewport aspect.
	struct CameraReferenceView
	{
		std::string m_Id;
		std::string m_Name;
		std::string m_Purpose;
		uint32_t m_ProfileVersion = 1;
		Vector3 m_Position = Vector3::Zero;
		Vector3 m_Target = Vector3::Forward;
		float m_VerticalFovDegrees = 60.0f;
		float m_NearPlane = 0.1f;
		float m_FarPlane = 1000.0f;
		float m_ExposureCompensationEV = 0.0f;
		float m_ReferenceAspect = 16.0f / 9.0f;
	};
}
