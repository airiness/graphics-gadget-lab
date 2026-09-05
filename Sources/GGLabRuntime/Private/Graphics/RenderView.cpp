#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Graphics/Camera.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace gglab
{
	namespace
	{
		RenderView BuildPerspectiveCameraView(RenderViewID viewId, const Camera& camera,
			const ResolvedViewRenderSettings& renderSettings,
			const ResolvedTemporalFramePlan& temporalFramePlan, uint32_t width, uint32_t height,
			StringID name) noexcept
		{
			RenderView view{};
			view.m_Name = name;
			view.m_ViewId = viewId;
			view.m_IsValid = true;
			view.m_DepthConvention = DepthConvention::Reversed;

			view.m_View = camera.GetViewMatrix();
			view.m_InvView = math::Inverse(view.m_View);
			view.m_UnjitteredProj = math::CreatePerspectiveFieldOfViewLHReversedZ(math::ToRadians(camera.GetFov()),
				camera.GetAspect(), camera.GetNear(), camera.GetFar());
			view.m_UnjitteredViewProj = view.m_View * view.m_UnjitteredProj;
			view.m_InvUnjitteredProj = math::Inverse(view.m_UnjitteredProj);
			view.m_InvUnjitteredViewProj = math::Inverse(view.m_UnjitteredViewProj);
			view.m_RasterProj = view.m_UnjitteredProj;
			view.m_RasterViewProj = view.m_UnjitteredViewProj;
			view.m_InvRasterProj = view.m_InvUnjitteredProj;
			view.m_InvRasterViewProj = view.m_InvUnjitteredViewProj;
			view.m_DepthReconstructionParams =
				screen_space::MakeDepthReconstructionParams(view.m_RasterProj);
			view.m_PreviousView = view.m_View;
			view.m_PreviousRasterViewProj = view.m_RasterViewProj;
			view.m_PreviousDepthReconstructionParams = view.m_DepthReconstructionParams;
			view.m_PreviousDepthConvention = view.m_DepthConvention;

			if (viewId == temporalFramePlan.m_DisplayViewId)
			{
				view.m_TemporalResetIdentity = temporalFramePlan.m_ResetIdentity;
				view.m_TemporalSessionIdentity = temporalFramePlan.m_SessionIdentity;
			}

			view.m_CameraPosition = camera.GetPosition();
			view.m_Near = camera.GetNear();
			view.m_Far = camera.GetFar();
			view.m_FovRadians = math::ToRadians(camera.GetFov());
			view.m_Aspect = camera.GetAspect();
			view.m_ExposureCompensationEV = renderSettings.m_Exposure.m_CompensationEV;
			view.m_ExposureMultiplier = renderSettings.m_Exposure.m_ExposureScale;

			view.m_Width = width;
			view.m_Height = height;

			return view;
		}
	}

	RenderView RenderViewBuilder::BuildDebugCameraView(RenderViewID viewId, const Camera& camera,
		const ResolvedViewRenderSettings& renderSettings,
		const ResolvedTemporalFramePlan& temporalFramePlan, uint32_t width, uint32_t height,
		StringID name) const noexcept
	{
		GGLAB_ASSERT(IsDebugCameraRenderViewID(viewId));
		return BuildPerspectiveCameraView(
			viewId, camera, renderSettings, temporalFramePlan, width, height, name);
	}

	RenderView RenderViewBuildTraits<RenderViewID::Main>::Build(
		const RenderViewBuildInfo<RenderViewID::Main>& info) noexcept
	{
		return BuildPerspectiveCameraView(RenderViewID::Main, info.m_Camera, info.m_RenderSettings,
			info.m_TemporalFramePlan, info.m_Width, info.m_Height, info.m_Name);
	}

	RenderView RenderViewBuildTraits<RenderViewID::DirectionalShadow>::Build(
		const RenderViewBuildInfo<RenderViewID::DirectionalShadow>& info) noexcept
	{
		Vector3 lightDir = info.m_LightDirection;
		if (lightDir.LengthSquared() <= 1.0e-8f)
		{
			lightDir = -Vector3::UnitY;
		}
		lightDir.Normalize();

		Vector3 cameraForward =
			math::TransformDirection(Vector3::Forward, info.m_MainView.m_InvView);
		Vector3 cameraRight = math::TransformDirection(Vector3::Right, info.m_MainView.m_InvView);
		Vector3 cameraUp = math::TransformDirection(Vector3::Up, info.m_MainView.m_InvView);
		if (cameraForward.LengthSquared() <= 1.0e-8f)
		{
			cameraForward = Vector3::Forward;
		}
		if (cameraRight.LengthSquared() <= 1.0e-8f)
		{
			cameraRight = Vector3::UnitX;
		}
		if (cameraUp.LengthSquared() <= 1.0e-8f)
		{
			cameraUp = Vector3::UnitY;
		}
		cameraForward.Normalize();
		cameraRight.Normalize();
		cameraUp.Normalize();

		const float nearZ = std::max(info.m_MainView.m_Near, 0.001f);
		const float farZ =
			std::max(nearZ + 0.001f, std::min(info.m_MainView.m_Far, info.m_MaxShadowDistance));
		const float tanHalfFov = std::tan(info.m_MainView.m_FovRadians * 0.5f);
		const float nearHalfHeight = tanHalfFov * nearZ;
		const float nearHalfWidth = nearHalfHeight * std::max(info.m_MainView.m_Aspect, 0.001f);
		const float farHalfHeight = tanHalfFov * farZ;
		const float farHalfWidth = farHalfHeight * std::max(info.m_MainView.m_Aspect, 0.001f);

		const Vector3 nearCenter = info.m_MainView.m_CameraPosition + cameraForward * nearZ;
		const Vector3 farCenter = info.m_MainView.m_CameraPosition + cameraForward * farZ;

		std::array<Vector3, 8> frustumCorners = {
			nearCenter - cameraRight * nearHalfWidth + cameraUp * nearHalfHeight,
			nearCenter + cameraRight * nearHalfWidth + cameraUp * nearHalfHeight,
			nearCenter - cameraRight * nearHalfWidth - cameraUp * nearHalfHeight,
			nearCenter + cameraRight * nearHalfWidth - cameraUp * nearHalfHeight,
			farCenter - cameraRight * farHalfWidth + cameraUp * farHalfHeight,
			farCenter + cameraRight * farHalfWidth + cameraUp * farHalfHeight,
			farCenter - cameraRight * farHalfWidth - cameraUp * farHalfHeight,
			farCenter + cameraRight * farHalfWidth - cameraUp * farHalfHeight,
		};

		Vector3 frustumCenter = Vector3::Zero;
		for (const Vector3& corner : frustumCorners)
		{
			frustumCenter += corner;
		}
		frustumCenter /= static_cast<float>(frustumCorners.size());

		Vector3 lightUp = Vector3::UnitY;
		if (std::abs(lightDir.Dot(lightUp)) > 0.95f)
		{
			lightUp = Vector3::UnitZ;
		}

		const Vector3 lightEyeForBounds = frustumCenter - lightDir;
		const Matrix lightViewForBounds =
			math::CreateLookAtLH(lightEyeForBounds, frustumCenter, lightUp);

		Vector3 minLS(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max());
		Vector3 maxLS(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
			std::numeric_limits<float>::lowest());

		const auto includeLightSpacePoint = [&minLS, &maxLS, &lightViewForBounds](
			const Vector3& pointWS) noexcept
			{
				const Vector3 pointLS = math::TransformPoint(pointWS, lightViewForBounds);
				minLS = math::Min(minLS, pointLS);
				maxLS = math::Max(maxLS, pointLS);
			};

		const float casterExtrusionDistance = std::max(info.m_CasterExtrusionDistance, 0.0f);
		for (const Vector3& corner : frustumCorners)
		{
			includeLightSpacePoint(corner);
			includeLightSpacePoint(corner - lightDir * casterExtrusionDistance);
		}

		const float orthoPadding = std::max(info.m_OrthoPadding, 0.0f);
		minLS.m_X -= orthoPadding;
		minLS.m_Y -= orthoPadding;
		maxLS.m_X += orthoPadding;
		maxLS.m_Y += orthoPadding;

		const float depthPadding = std::max(info.m_DepthPadding, 0.0f);
		minLS.m_Z -= depthPadding;
		maxLS.m_Z += depthPadding;

		constexpr float ShadowNear = 0.1f;
		const float shadowFar = std::max((maxLS.m_Z - minLS.m_Z) + ShadowNear, ShadowNear + 1.0f);
		// In the LH light view, points in front of the light have increasing z.
		// Place the eye before the minimum required near plane while preserving the fitted depth range.
		const float lightEyeOffset = minLS.m_Z - ShadowNear;
		const Vector3 lightEye = lightEyeForBounds + lightDir * lightEyeOffset;
		const Vector3 lightTarget = lightEye + lightDir;

		RenderView view{};
		view.m_Name = info.m_Name;
		view.m_ViewId = RenderViewID::DirectionalShadow;
		view.m_IsValid = true;
		view.m_DepthConvention = DepthConvention::Standard;
		view.m_View = math::CreateLookAtLH(lightEye, lightTarget, lightUp);
		view.m_InvView = math::Inverse(view.m_View);
		view.m_UnjitteredProj = math::CreateOrthographicOffCenterLH(
			minLS.m_X, maxLS.m_X, minLS.m_Y, maxLS.m_Y, ShadowNear, shadowFar);
		view.m_UnjitteredViewProj = view.m_View * view.m_UnjitteredProj;
		view.m_InvUnjitteredProj = math::Inverse(view.m_UnjitteredProj);
		view.m_InvUnjitteredViewProj = math::Inverse(view.m_UnjitteredViewProj);
		view.m_RasterProj = view.m_UnjitteredProj;
		view.m_RasterViewProj = view.m_UnjitteredViewProj;
		view.m_InvRasterProj = view.m_InvUnjitteredProj;
		view.m_InvRasterViewProj = view.m_InvUnjitteredViewProj;
		view.m_DepthReconstructionParams =
			screen_space::MakeDepthReconstructionParams(view.m_RasterProj);
		view.m_PreviousView = view.m_View;
		view.m_PreviousRasterViewProj = view.m_RasterViewProj;
		view.m_PreviousDepthReconstructionParams = view.m_DepthReconstructionParams;
		view.m_PreviousDepthConvention = view.m_DepthConvention;
		view.m_CameraPosition = lightEye;
		view.m_Near = ShadowNear;
		view.m_Far = shadowFar;
		view.m_FovRadians = 0.0f;
		view.m_Aspect = 1.0f;
		view.m_ExposureCompensationEV = 0.0f;
		view.m_ExposureMultiplier = 1.0f;
		view.m_Width = info.m_ShadowMapSize;
		view.m_Height = info.m_ShadowMapSize;

		return view;
	}
}
