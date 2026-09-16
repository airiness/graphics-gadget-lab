#pragma once

#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Graphics/CameraReferenceView.h"

#include <array>

namespace gglab
{
	// Blender (X, Y, Z) -> runtime (X, Z, Y), meters. Perspective, vertical FOV.
	// Profile 1 records the greybox composition; it is not Rendering Baseline 1.
	inline const std::array<CameraReferenceView, 3> CoastalAtriumReferenceViews = { {
		{
			.m_Id = "CAM_Courtyard",
			.m_Name = "Courtyard",
			.m_Purpose = "Overall courtyard scale, coastal platform and primary lighting.",
			.m_Position = { 23.0f, 19.0f, -28.0f },
			.m_Target = { -1.0f, 1.8f, -2.0f },
			.m_VerticalFovDegrees = math::ToDegrees(0.6509917105f),
			.m_FarPlane = 150.0f,
		},
		{
			.m_Id = "CAM_ShadowStairs",
			.m_Name = "Shadow Stairs",
			.m_Purpose = "Near railings and stair contacts, mid-distance slat shadows, distant columns.",
			.m_Position = { 5.5f, 3.4f, -14.0f },
			.m_Target = { 0.0f, 2.8f, 1.0f },
			.m_VerticalFovDegrees = math::ToDegrees(0.6939552104f),
			.m_FarPlane = 150.0f,
		},
		{
			.m_Id = "CAM_InteriorExterior",
			.m_Name = "Interior / Exterior",
			.m_Purpose = "Thick doorway occlusion and the transition from corridor to sunlit courtyard.",
			.m_Position = { -12.0f, 4.0f, -2.9f },
			.m_Target = { 2.0f, 2.5f, -5.0f },
			.m_VerticalFovDegrees = math::ToDegrees(0.7984415392f),
			.m_FarPlane = 150.0f,
		},
	} };
}
