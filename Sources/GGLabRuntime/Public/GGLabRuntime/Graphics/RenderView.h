#pragma once
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Core/StringId.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/ScreenSpace/ScreenSpaceTypes.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"

#include <cstdint>

namespace gglab
{
	class Camera;

	template <RenderViewID ViewId> struct RenderViewBuildInfo;

	template <RenderViewID ViewId> struct RenderViewBuildTraits;

	struct RenderView
	{
		Matrix m_View = Matrix::Identity;
		Matrix m_InvView = Matrix::Identity;

		Matrix m_UnjitteredProj = Matrix::Identity;
		Matrix m_UnjitteredViewProj = Matrix::Identity;
		Matrix m_InvUnjitteredProj = Matrix::Identity;
		Matrix m_InvUnjitteredViewProj = Matrix::Identity;

		Matrix m_RasterProj = Matrix::Identity;
		Matrix m_RasterViewProj = Matrix::Identity;
		Matrix m_InvRasterProj = Matrix::Identity;
		Matrix m_InvRasterViewProj = Matrix::Identity;

		Matrix m_PreviousView = Matrix::Identity;
		Matrix m_PreviousRasterViewProj = Matrix::Identity;
		Vector4 m_DepthReconstructionParams = Vector4::Zero;
		Vector4 m_PreviousDepthReconstructionParams = Vector4::Zero;
		Vector2 m_JitterPixels = Vector2::Zero;
		Vector2 m_JitterUV = Vector2::Zero;
		Vector2 m_PreviousJitterUV = Vector2::Zero;

		Vector3 m_CameraPosition = Vector3::Zero;
		float m_Near = 0.1f;
		float m_Far = 10000.0f;
		float m_FovRadians = 0.0f;
		float m_Aspect = 1.0f;
		float m_ExposureCompensationEV = 0.0f;
		float m_ExposureMultiplier = 1.0f;
		float m_ScenePreExposure = 1.0f;
		float m_PreviousScenePreExposure = 1.0f;

		// Render (raster) extent: the view's geometry is rasterized and shaded at it.
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		// Display extent of a camera view: its temporal output and post-temporal
		// composition use it. Zero for views that are never displayed, such as shadow
		// views.
		uint32_t m_DisplayWidth = 0;
		uint32_t m_DisplayHeight = 0;
		uint64_t m_TemporalResetIdentity = 0;
		uint64_t m_TemporalSessionIdentity = 0;
		// Display-view frames since the temporal history was reset; zero for every other
		// view. A replayed camera-path sequence repeats it exactly.
		uint32_t m_TemporalFrameIndex = 0;
		// Added to the LOD of material texture samples: the active temporal view's LOD
		// offset, or a reference's own bias; zero for every other view.
		float m_TextureLodBias = 0.0f;

		DepthConvention m_DepthConvention = DepthConvention::Standard;
		DepthConvention m_PreviousDepthConvention = DepthConvention::Standard;
		RenderViewID m_ViewId = RenderViewID::Unknown;
		StringID m_Name{};
		bool m_HasPreviousTemporalState = false;
		bool m_IsValid = false;

		[[nodiscard]] ViewResolution GetResolution() const noexcept
		{
			return ViewResolution{
				.m_Render = { m_Width, m_Height },
				.m_Display = { m_DisplayWidth, m_DisplayHeight },
			};
		}
	};

	struct DirectionalShadowProjectionInfo
	{
		DirectionalShadowFitMode m_FitMode = DirectionalShadowFitMode::Tight;
		float m_SphereRadius = 0.0f;
		Vector2 m_Extent = Vector2::Zero;
		Vector2 m_WorldUnitsPerTexel = Vector2::Zero;
		// XY in a light basis anchored at the world origin, before and after snapping.
		Vector2 m_UnsnappedCenterLS = Vector2::Zero;
		Vector2 m_CenterLS = Vector2::Zero;
		bool m_TexelSnappingApplied = false;
	};

	struct DirectionalShadowViewBuildResult
	{
		RenderView m_View{};
		DirectionalShadowProjectionInfo m_Projection{};
	};

	class RenderViewBuilder
	{
	public:
		RenderView BuildDebugCameraView(RenderViewID viewId, const Camera& camera,
			const ResolvedViewRenderSettings& renderSettings,
				const ResolvedTemporalFramePlan& temporalFramePlan, ViewResolution resolution,
			StringID name) const noexcept;

		template <RenderViewID ViewId>
		RenderView Build(const RenderViewBuildInfo<ViewId>& info) const noexcept
		{
			return RenderViewBuildTraits<ViewId>::Build(info);
		}
	};

	template <> struct RenderViewBuildInfo<RenderViewID::Main>
	{
		const Camera& m_Camera;
		const ResolvedViewRenderSettings& m_RenderSettings;
		const ResolvedTemporalFramePlan& m_TemporalFramePlan;
		ViewResolution m_Resolution{};
		StringID m_Name = StringID("MainView");
	};

	template <> struct RenderViewBuildInfo<RenderViewID::DirectionalShadow>
	{
		const RenderView& m_MainView;
		Vector3 m_LightDirection = -Vector3::UnitY;
		uint32_t m_ShadowMapSize = DefaultDirectionalShadowMapSize;
		float m_MaxShadowDistance = DefaultDirectionalShadowMaxDistance;
		float m_CasterExtrusionDistance = DefaultDirectionalShadowCasterExtrusionDistance;
		float m_OrthoPadding = DefaultDirectionalShadowOrthoPadding;
		float m_DepthPadding = DefaultDirectionalShadowDepthPadding;
		float m_FilterSupportTexels = 0.0f;
		DirectionalShadowFitMode m_FitMode = DirectionalShadowFitMode::StableSphere;
		bool m_EnableTexelSnapping = true;
		StringID m_Name = StringID("DirectionalShadowView");
	};

	template <> struct RenderViewBuildTraits<RenderViewID::Main>
	{
		static RenderView Build(const RenderViewBuildInfo<RenderViewID::Main>& info) noexcept;
	};

	template <> struct RenderViewBuildTraits<RenderViewID::DirectionalShadow>
	{
		static RenderView Build(
			const RenderViewBuildInfo<RenderViewID::DirectionalShadow>& info) noexcept;
	};
	[[nodiscard]] DirectionalShadowViewBuildResult BuildDirectionalShadowView(
		const RenderViewBuildInfo<RenderViewID::DirectionalShadow>& info) noexcept;
}
