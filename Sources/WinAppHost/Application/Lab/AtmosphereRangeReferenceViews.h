#pragma once

#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Graphics/CameraReferenceView.h"

#include <array>
#include <cmath>
#include <format>

namespace gglab
{
	// Blender (X, Y, Z) -> runtime (X, Z, Y), meters, reference aspect 16:9.
	inline const std::array<CameraReferenceView, 7> AtmosphereRangeReferenceViews = []
		{
			std::array<CameraReferenceView, 7> views{};
			views[0] = {
				.m_Id = "CAM_AerialRange",
				.m_Name = "Aerial Range Overview",
				.m_Purpose = "Identical 6 m targets at center-ray distances 25, 50, 100, 250, 500, 1000 m, left to right.",
				.m_Position = { 0.0f, 20.0f, 0.0f },
				.m_Target = { 0.0f, 20.0f, 1.0f },
				.m_VerticalFovDegrees = math::ToDegrees(0.9272952180f),
				.m_FarPlane = 2000.0f,
				.m_ManualEV100 = 15.0f,
			};
			constexpr std::array distances{ 25, 50, 100, 250, 500, 1000 };
			constexpr std::array slopes{ -0.65f, -0.39f, -0.13f, 0.13f, 0.39f, 0.65f };
			for (size_t index = 0; index < distances.size(); ++index)
			{
				views[index + 1] = {
					.m_Id = std::format("CAM_Range_{:05}m", distances[index]),
					.m_Name = std::format("Range {} m", distances[index]),
					.m_Purpose = "Same observer and lighting; enlarged target framing for interior linear samples.",
					.m_Position = { 0.0f, 20.0f, 0.0f },
					.m_Target = { slopes[index], 20.2f, 1.0f },
					.m_VerticalFovDegrees = math::ToDegrees(2.0f * std::atan(10.0f / distances[index])),
					.m_FarPlane = 2000.0f,
					.m_ManualEV100 = 15.0f,
				};
			}
			return views;
		}();
}
