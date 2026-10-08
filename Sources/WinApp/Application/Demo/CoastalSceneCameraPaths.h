#pragma once

#include "Application/Demo/CoastalSceneReferenceViews.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Graphics/CameraPath.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace gglab
{
	// Deterministic temporal evaluation sequences for the coastal retreat, derived
	// from its retained reference views. Runtime coordinates in meters; poses are
	// indexed by sequence frame only. Change a path's version whenever its motion,
	// projection or exposure changes. View poses are copied rather than looked up,
	// so editing a reference view cannot silently change a versioned path.
	[[nodiscard]] inline std::vector<CameraPath> MakeCoastalSceneCameraPaths()
	{
		constexpr float nearPlane = 0.1f;
		constexpr float farPlane = 6000.0f;
		const auto makePath = [&](const char* id, const char* name, const char* purpose,
			CameraPathInterpolation interpolation, float nearZ,
			std::vector<CameraPathKey> keys)
			{
				return CameraPath{
					.m_Id = id,
					.m_Name = name,
					.m_Purpose = purpose,
					.m_Version = 1,
					.m_Interpolation = interpolation,
					.m_NearPlane = nearZ,
					.m_FarPlane = farPlane,
					.m_ManualEV100 = CoastalSceneManualEV100,
					.m_Keys = std::move(keys),
				};
			};
		// Rotates `point` about the vertical axis through `pivot`.
		const auto rotateAboutY = [](const Vector3& point, const Vector3& pivot, float radians)
			{
				const Vector3 offset = point - pivot;
				const float c = std::cos(radians);
				const float s = std::sin(radians);
				return pivot + Vector3(offset.m_X * c + offset.m_Z * s, offset.m_Y,
					-offset.m_X * s + offset.m_Z * c);
			};

		// Shadow Stairs: near railings, mid-distance slats, distant columns.
		const Vector3 stairsPosition{ 5.5f, 3.4f, -14.0f };
		const Vector3 stairsTarget{ 0.0f, 2.8f, 1.0f };
		const float stairsFov = math::ToDegrees(0.6939552104f);
		// Interior / Exterior: thick doorway between corridor and sunlit courtyard.
		const Vector3 doorwayPosition{ -12.0f, 4.0f, -2.9f };
		const Vector3 doorwayTarget{ 2.0f, 2.5f, -5.0f };
		const float doorwayFov = math::ToDegrees(0.7984415392f);
		// Retreat Lounge: cushions, notched deck and table props.
		const Vector3 loungePosition{ 8.15f, 3.65f, -5.7f };
		const Vector3 loungeTarget{ 6.35f, 3.25f, -1.2f };
		const float loungeFov = math::ToDegrees(0.6057697296f);
		// Sky / Horizon: sky above the platform with architecture and shoreline.
		const Vector3 horizonPosition{ 20.0f, 5.5f, -26.0f };
		const Vector3 horizonTarget{ 0.0f, 3.0f, 0.0f };
		const float horizonFov = math::ToDegrees(0.7984415392f);
		// Courtyard: overall scale and primary lighting.
		const Vector3 courtyardPosition{ 23.0f, 19.0f, -28.0f };
		const Vector3 courtyardTarget{ -1.0f, 1.8f, -2.0f };
		const float courtyardFov = math::ToDegrees(0.6509917105f);

		// Glass Terrace: from inside the sea terrace through the alpha-blended guard glass
		// toward the sun glint, so transparent panes cover sea highlights, guard posts, the
		// dock and the lifebuoy.
		const Vector3 glassPosition{ 2.5f, 3.1f, -8.7f };
		const Vector3 glassTarget{ 9.0f, 2.1f, -18.0f };
		const float glassFov = math::ToDegrees(0.7984415392f);
		const Vector3 glassPanOffset{ 1.5f, 0.0f, 0.0f };

		// Camera right for the Shadow Stairs view (left-handed, Y up): normalize(up x forward).
		const Vector3 stairsForward = stairsTarget - stairsPosition;
		const float stairsRightLength = std::sqrt(
			stairsForward.m_Z * stairsForward.m_Z + stairsForward.m_X * stairsForward.m_X);
		const Vector3 stairsRight(stairsForward.m_Z / stairsRightLength, 0.0f,
			-stairsForward.m_X / stairsRightLength);
		const Vector3 panOffset = stairsRight * 1.2f;

		const Vector3 doorwayForward = doorwayTarget - doorwayPosition;
		const Vector3 dollyOffset =
			doorwayForward * (4.0f / std::sqrt(doorwayForward.m_X * doorwayForward.m_X +
				doorwayForward.m_Y * doorwayForward.m_Y + doorwayForward.m_Z * doorwayForward.m_Z));

		std::vector<CameraPathKey> orbitKeys;
		for (uint32_t step = 0; step <= 6; ++step)
		{
			orbitKeys.push_back({
				.m_Frame = step * 40,
				.m_Position = rotateAboutY(
					loungePosition, loungeTarget, math::ToRadians(5.0f * static_cast<float>(step))),
				.m_Target = loungeTarget,
				.m_VerticalFovDegrees = loungeFov,
				});
		}
		std::vector<CameraPathKey> horizonKeys;
		for (uint32_t step = 0; step <= 4; ++step)
		{
			horizonKeys.push_back({
				.m_Frame = step * 45,
				.m_Position = horizonPosition,
				.m_Target = rotateAboutY(horizonTarget, horizonPosition,
					math::ToRadians(5.0f * static_cast<float>(step))),
				.m_VerticalFovDegrees = horizonFov,
				});
		}

		return {
			makePath("SEQ_StaticRailings", "Static Railings",
				"Static convergence over near railings, stair slats and distant columns.",
				CameraPathInterpolation::Linear, nearPlane, {
					{ .m_Frame = 0, .m_Position = stairsPosition, .m_Target = stairsTarget,
						.m_VerticalFovDegrees = stairsFov },
					{ .m_Frame = 95, .m_Position = stairsPosition, .m_Target = stairsTarget,
						.m_VerticalFovDegrees = stairsFov },
				}),
			makePath("SEQ_LateralPanRailings", "Lateral Pan Railings",
				"Slow 1.2 m sideways truck across railings and repeated slats.",
				CameraPathInterpolation::Linear, nearPlane, {
					{ .m_Frame = 0, .m_Position = stairsPosition, .m_Target = stairsTarget,
						.m_VerticalFovDegrees = stairsFov },
					{ .m_Frame = 179, .m_Position = stairsPosition + panOffset,
						.m_Target = stairsTarget + panOffset, .m_VerticalFovDegrees = stairsFov },
				}),
			makePath("SEQ_DollyDoorway", "Dolly Doorway",
				"4 m forward dolly from the corridor toward the courtyard, revealing disoccluded surfaces.",
				CameraPathInterpolation::Linear, nearPlane, {
					{ .m_Frame = 0, .m_Position = doorwayPosition, .m_Target = doorwayTarget,
						.m_VerticalFovDegrees = doorwayFov },
					{ .m_Frame = 179, .m_Position = doorwayPosition + dollyOffset,
						.m_Target = doorwayTarget + dollyOffset, .m_VerticalFovDegrees = doorwayFov },
				}),
			makePath("SEQ_OrbitLounge", "Orbit Lounge",
				"30 degree orbit around the lounge for changing specular response and mixed motion.",
				CameraPathInterpolation::CatmullRom, 0.05f, std::move(orbitKeys)),
			makePath("SEQ_HorizonPan", "Horizon Pan",
				"20 degree pan across sky, horizon and distant geometry from a fixed position.",
				CameraPathInterpolation::CatmullRom, nearPlane, std::move(horizonKeys)),
			makePath("SEQ_StaticGlassTerrace", "Static Glass Terrace",
				"Static view through the terrace guard glass toward the sea: post-temporal "
				"transparent shimmer over sea highlights and guard posts.",
				CameraPathInterpolation::Linear, nearPlane, {
					{ .m_Frame = 0, .m_Position = glassPosition, .m_Target = glassTarget,
						.m_VerticalFovDegrees = glassFov },
					{ .m_Frame = 95, .m_Position = glassPosition, .m_Target = glassTarget,
						.m_VerticalFovDegrees = glassFov },
				}),
			makePath("SEQ_PanGlassTerrace", "Pan Glass Terrace",
				"1.5 m sideways truck along the terrace guard glass: transparent panes and "
				"frames over a moving opaque and sea background.",
				CameraPathInterpolation::Linear, nearPlane, {
					{ .m_Frame = 0, .m_Position = glassPosition, .m_Target = glassTarget,
						.m_VerticalFovDegrees = glassFov },
					{ .m_Frame = 179, .m_Position = glassPosition + glassPanOffset,
						.m_Target = glassTarget + glassPanOffset, .m_VerticalFovDegrees = glassFov },
				}),
			makePath("SEQ_CutCourtyardToStairs", "Cut Courtyard To Stairs",
				"Static courtyard shot, then a camera cut to the stairs for reset and first-frame recovery.",
				CameraPathInterpolation::Linear, nearPlane, {
					{ .m_Frame = 0, .m_Position = courtyardPosition, .m_Target = courtyardTarget,
						.m_VerticalFovDegrees = courtyardFov },
					{ .m_Frame = 59, .m_Position = courtyardPosition, .m_Target = courtyardTarget,
						.m_VerticalFovDegrees = courtyardFov },
					{ .m_Frame = 60, .m_Position = stairsPosition, .m_Target = stairsTarget,
						.m_VerticalFovDegrees = stairsFov, .m_Cut = true },
					{ .m_Frame = 119, .m_Position = stairsPosition, .m_Target = stairsTarget,
						.m_VerticalFovDegrees = stairsFov },
				}),
		};
	}
}
