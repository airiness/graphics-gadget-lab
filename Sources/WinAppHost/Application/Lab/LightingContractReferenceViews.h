#pragma once

#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Graphics/CameraReferenceView.h"

#include <array>
#include <vector>

namespace gglab
{
	// Blender (X, Y, Z) maps to runtime (X, Z, Y); positions use meters and a 16:9 reference aspect.
	inline const std::array<CameraReferenceView, 4> LightingContractReferenceViews = { {
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
		{
			.m_Id = "CAM_RoughnessSweep",
			.m_Name = "Roughness Sweep",
			.m_Purpose = "Roughness 0, 0.05, 0.1, 0.25, 0.5, 1; lower row dielectric, upper row metallic.",
			.m_Position = { 48.0f, 3.5f, -13.0f },
			.m_Target = { 48.0f, 2.0f, 0.0f },
			.m_VerticalFovDegrees = math::ToDegrees(0.6509917105f),
			.m_FarPlane = 100.0f,
		},
	} };
	inline const std::array<CameraReferenceView, 2> LightingContractMaterialReferenceViews = { {
		{
			.m_Id = "CAM_Clearcoat",
			.m_Name = "Clearcoat",
			.m_Purpose = "Off, smooth, rough and independent coat-normal references.",
			.m_Position = { 67.0f, 3.2f, -10.0f },
			.m_Target = { 67.0f, 0.8f, 0.0f },
			.m_VerticalFovDegrees = math::ToDegrees(0.6509917105f),
			.m_FarPlane = 100.0f,
		},
		{
			.m_Id = "CAM_Anisotropy",
			.m_Name = "Anisotropy",
			.m_Purpose = "Strength off/on, 0/45/90-degree rotations, mirrored UVs and base-normal interaction.",
			.m_Position = { 82.2f, 4.4f, -10.0f },
			.m_Target = { 82.2f, 0.8f, 1.3f },
			.m_VerticalFovDegrees = math::ToDegrees(0.6509917105f),
			.m_FarPlane = 100.0f,
		},
	} };
	inline auto BuildLightingContractReferenceViews(bool physicalSun, bool materialReferences = false)
	{
		std::vector<CameraReferenceView> views{
			LightingContractReferenceViews.begin(), LightingContractReferenceViews.end() };
		if (materialReferences)
		{
			views.insert(views.end(), LightingContractMaterialReferenceViews.begin(),
				LightingContractMaterialReferenceViews.end());
		}
		for (auto& view : views)
		{
			view.m_ManualEV100 = physicalSun ? 15.0f : 0.0f;
			view.m_ProfileVersion = physicalSun ? 2 : 1;
		}
		return views;
	}

}
