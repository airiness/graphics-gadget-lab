#pragma once

#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Graphics/CameraReferenceView.h"

#include <array>

namespace gglab
{
	// Content C0: Blender (X, Y, Z) -> runtime (X, Z, Y), meters, 16:9 reference aspect.
	inline const std::array<CameraReferenceView, 3> LightingContractReferenceViews = { {
		{
			.m_Id = "CAM_ExposureChart",
			.m_Name = "Exposure Chart",
			.m_Purpose = "Linear base-color cards: 0.02, 0.18, 0.50 and 0.90, left to right.",
			.m_Position = { 0.0f, 2.0f, -10.0f },
			.m_Target = { 0.0f, 2.0f, 0.0f },
			.m_VerticalFovDegrees = math::ToDegrees(0.5483349022f),
			.m_FarPlane = 100.0f,
		},
		{
			.m_Id = "CAM_LightingSphere",
			.m_Name = "Lighting Spheres",
			.m_Purpose = "Matte, rough dielectric, smooth dielectric and metallic PBR references.",
			.m_Position = { 16.0f, 3.2f, -9.0f },
			.m_Target = { 16.0f, 0.8f, 0.0f },
			.m_VerticalFovDegrees = math::ToDegrees(0.6509917105f),
			.m_FarPlane = 100.0f,
		},
		{
			.m_Id = "CAM_SunAngles",
			.m_Name = "Sun Angles",
			.m_Purpose = "Horizontal, vertical and 45-degree receivers; toward-light cosine sqrt(0.5), sqrt(0.5), 1.",
			.m_Position = { 32.0f, 6.0f, -7.0f },
			.m_Target = { 32.0f, 1.4f, 0.0f },
			.m_VerticalFovDegrees = math::ToDegrees(0.6128792380f),
			.m_FarPlane = 100.0f,
		},
	} };
}
