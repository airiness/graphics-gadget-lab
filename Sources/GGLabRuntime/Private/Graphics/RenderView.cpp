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
			view.m_ScenePreExposure = renderSettings.m_Exposure.m_PreExposure;

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
		return BuildDirectionalShadowView(info).m_View;
	}

	DirectionalShadowViewBuildResult BuildDirectionalShadowView(
		const RenderViewBuildInfo<RenderViewID::DirectionalShadow>& info) noexcept
	{
		DirectionalShadowViewBuildResult result{};
		auto& projection = result.m_Projection;
		projection.m_FitMode = info.m_FitMode;
		const bool stable = info.m_FitMode == DirectionalShadowFitMode::StableSphere;
		const uint32_t resolution = std::max(info.m_ShadowMapSize, 1u);
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

		// Build the stable light orientation at the world origin. Reconstructing it
		// from eye and eye + direction introduces camera-position-dependent rotation error.
		const Matrix lightRotation = math::CreateLookAtLH(Vector3::Zero, lightDir, lightUp);
		const Vector3 lightEyeForBounds = stable ? Vector3::Zero : frustumCenter - lightDir;
		Matrix lightViewForBounds = lightRotation;
		lightViewForBounds.Translation(-math::TransformPoint(lightEyeForBounds, lightRotation));

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

		const float filterSupport = std::clamp(info.m_FilterSupportTexels, 0.0f,
			std::max(0.0f, (static_cast<float>(resolution) - 2.0f) * 0.5f));
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
		Vector3 lightEye = lightEyeForBounds + lightDir * lightEyeOffset;
		Matrix lightView{};
		if (stable)
		{
			// The midpoint sphere encloses both frustum planes. Its radius uses only
			// projection parameters, so translation, rotation and TAA jitter cannot resize XY.
			const float halfDepth = (farZ - nearZ) * 0.5f;
			projection.m_SphereRadius = std::sqrt(halfDepth * halfDepth +
				farHalfWidth * farHalfWidth + farHalfHeight * farHalfHeight);
			float halfExtent = std::max(projection.m_SphereRadius + orthoPadding, 0.001f);
			if (resolution > 1)
			{
				// Reserve filter support plus half a final texel for snapping at each edge.
				// Keep this guard with snapping off as well, so A/B does not change the footprint.
				halfExtent *= static_cast<float>(resolution) /
					(static_cast<float>(resolution - 1) - 2.0f * filterSupport);
			}
			projection.m_Extent = Vector2(2.0f * halfExtent);
			projection.m_WorldUnitsPerTexel = projection.m_Extent / static_cast<float>(resolution);
			const Vector3 sphereCenter = info.m_MainView.m_CameraPosition +
				cameraForward * ((nearZ + farZ) * 0.5f);
			const Vector3 centerLS = math::TransformPoint(sphereCenter, lightRotation);
			projection.m_UnsnappedCenterLS = Vector2(centerLS.m_X, centerLS.m_Y);
			projection.m_CenterLS = projection.m_UnsnappedCenterLS;
			// A one-texel map has no finite guard for half-texel snapping; keep its valid unsnapped fit.
			projection.m_TexelSnappingApplied = info.m_EnableTexelSnapping && resolution > 1;
			if (projection.m_TexelSnappingApplied)
			{
				const float texelSize = projection.m_WorldUnitsPerTexel.m_X;
				projection.m_CenterLS.m_X = std::round(centerLS.m_X / texelSize) * texelSize;
				projection.m_CenterLS.m_Y = std::round(centerLS.m_Y / texelSize) * texelSize;
			}
			const Vector3 eyeLS(projection.m_CenterLS.m_X, projection.m_CenterLS.m_Y, lightEyeOffset);
			lightEye = math::TransformPoint(eyeLS, math::Transpose(lightRotation));
			lightView = lightRotation;
			lightView.Translation(-eyeLS);
			minLS.m_X = minLS.m_Y = -halfExtent;
			maxLS.m_X = maxLS.m_Y = halfExtent;
		}
		else
		{
			// Expand around the fitted center so the filter never samples outside the receiver coverage.
			const float guardScale = static_cast<float>(resolution) /
				(static_cast<float>(resolution) - 2.0f * filterSupport);
			const Vector2 guard((maxLS.m_X - minLS.m_X) * (guardScale - 1.0f) * 0.5f,
				(maxLS.m_Y - minLS.m_Y) * (guardScale - 1.0f) * 0.5f);
			minLS.m_X -= guard.m_X;
			minLS.m_Y -= guard.m_Y;
			maxLS.m_X += guard.m_X;
			maxLS.m_Y += guard.m_Y;
			// Reuse the fitted orientation: reconstructing eye + direction after the
			// large Z shift can rotate a tight fit enough to consume its filter guard.
			lightView = lightRotation;
			lightView.Translation(-math::TransformPoint(lightEye, lightRotation));
			projection.m_Extent = Vector2(maxLS.m_X - minLS.m_X, maxLS.m_Y - minLS.m_Y);
			projection.m_WorldUnitsPerTexel = projection.m_Extent / static_cast<float>(resolution);
			const Vector3 centerLS = math::TransformPoint(lightEye, lightRotation);
			projection.m_UnsnappedCenterLS = Vector2(centerLS.m_X + (minLS.m_X + maxLS.m_X) * 0.5f,
				centerLS.m_Y + (minLS.m_Y + maxLS.m_Y) * 0.5f);
			projection.m_CenterLS = projection.m_UnsnappedCenterLS;
		}

		RenderView& view = result.m_View;
		view.m_Name = info.m_Name;
		view.m_ViewId = RenderViewID::DirectionalShadow;
		view.m_IsValid = true;
		view.m_DepthConvention = DepthConvention::Standard;
		view.m_View = lightView;
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
		view.m_Width = resolution;
		view.m_Height = resolution;

		return result;
	}
}
