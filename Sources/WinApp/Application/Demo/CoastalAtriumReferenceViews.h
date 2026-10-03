#pragma once

#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Graphics/CameraReferenceView.h"

#include <array>

namespace gglab
{
	// Blender (X, Y, Z) -> runtime (X, Z, Y), meters. Perspective, vertical FOV.
	// Profile 1 records the greybox composition; it is not Rendering Baseline 1.
	inline const std::array<CameraReferenceView, 8> CoastalAtriumReferenceViews = { {
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
		{
			.m_Id = "CAM_SkyHorizon",
			.m_Name = "Sky / Horizon",
			.m_Purpose = "Sky and horizon above the coastal platform, with architecture and shoreline in the foreground.",
			.m_Position = { 20.0f, 5.5f, -26.0f },
			.m_Target = { 0.0f, 3.0f, 0.0f },
			.m_VerticalFovDegrees = math::ToDegrees(0.7984415392f),
			.m_FarPlane = 150.0f,
		},
		// Supplemental surface views use a 16:9 frame and 36 mm horizontal sensor.
		{
			.m_Id = "CAM_PavingSurface",
			.m_Name = "Paving Surface",
			.m_Purpose = "Stone block variation, mortar joints and shallow surface relief.",
			.m_Position = { 1.8f, 3.65f, -4.5f },
			.m_Target = { -0.2f, 2.4f, -1.5f },
			.m_VerticalFovDegrees = math::ToDegrees(0.4426288847f),
			.m_FarPlane = 150.0f,
		},
		{
			.m_Id = "CAM_RailingDetail",
			.m_Name = "Railing Detail",
			.m_Purpose = "Satin structure metal, shallow finishing marks and railing contact surfaces.",
			.m_Position = { 5.1f, 2.15f, -12.1f },
			.m_Target = { 3.58f, 1.55f, -10.325f },
			.m_VerticalFovDegrees = math::ToDegrees(0.5631968436f),
			.m_FarPlane = 150.0f,
		},
		{
			.m_Id = "CAM_UpholsterySurface",
			.m_Name = "Upholstery Surface",
			.m_Purpose = "Neutral woven cushions alongside the coated shell and brushed frame.",
			.m_Position = { 7.5f, 3.95f, -3.35f },
			.m_Target = { 6.35f, 3.1f, -1.1f },
			.m_VerticalFovDegrees = math::ToDegrees(0.3641052332f),
			.m_FarPlane = 150.0f,
		},
		{
			.m_Id = "CAM_UpholsteryWeave",
			.m_Name = "Upholstery Weave",
			.m_Purpose = "Close inspection of the cushion's approximately 2 mm warp/weft pattern.",
			.m_Position = { 5.85f, 3.5f, -1.85f },
			.m_Target = { 5.75f, 3.065f, -1.22f },
			.m_VerticalFovDegrees = math::ToDegrees(0.2371180160f),
			.m_FarPlane = 150.0f,
		},
	} };
}
