#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/DirectionalShadowFramePlan.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraphTypes.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderPass/ShadowGraphResources.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/ScreenSpace/ScreenSpaceTypes.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/RenderPass/RenderPassDirectionalShadowMap.h"
#include "Graphics/RenderSceneBuilder.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace gglab::rendering_test
{
	void RunDirectionalShadowCascadeFrameTests(SelfTestContext& context) noexcept
	{
		RenderFrameBuildResult frame{};
		frame.m_RenderViews.resize(utils::ToIndex(RenderViewID::Count));
		Camera camera(Camera::CreateInfo{});
		const ResolvedViewRenderSettings settings{};
		const ResolvedTemporalFramePlan plan{};
		const RenderView mainView = RenderViewBuilder{}.Build<RenderViewID::Main>({
			.m_Camera = camera,
			.m_RenderSettings = settings,
			.m_TemporalFramePlan = plan,
			.m_Resolution = ResolveNativeViewResolution({ 1280, 720 }),
		});
		frame.m_RenderViews[utils::ToIndex(RenderViewID::Main)] = mainView;
		frame.m_RenderViews[utils::ToIndex(RenderViewID::DebugCamera2)] = mainView;
		frame.m_RenderViews[utils::ToIndex(RenderViewID::DebugCamera2)].m_Width = 640;
		const RenderView legacyShadow = RenderViewBuilder{}.Build<RenderViewID::DirectionalShadow>({
			.m_MainView = mainView,
			.m_FitMode = DirectionalShadowFitMode::Tight,
		});
		auto& cascades = frame.m_DirectionalShadowFramePlan;
		cascades.m_Cascades.push_back({ .m_View = legacyShadow });
		const auto singleViewData = RenderSceneBuilder::BuildViewData(frame.m_RenderViews, cascades);
		context.Check(!cascades.HasViewRange(), "View flattening returns bindings without mutating the frame plan");
		cascades.m_ViewBaseOffset = singleViewData.m_ShadowViewBaseOffset;
		const auto& shadowGpu = singleViewData.m_Views[cascades.GetViewIndex(0)];
		context.Check(cascades.GetViewIndex(0) == frame.m_RenderViews.size() &&
			cascades.GetViewIndex(0) != utils::ToIndex(RenderViewID::DirectionalShadow) &&
			shadowGpu.ViewMat.ToArray() == legacyShadow.m_View.ToArray() &&
			shadowGpu.ProjMat.ToArray() == legacyShadow.m_RasterProj.ToArray() &&
			shadowGpu.Width == legacyShadow.m_Width &&
			shadowGpu.DepthConvention == static_cast<uint32_t>(DepthConvention::Standard),
			"Single-cascade upload preserves the legacy shadow projection at a non-enum offset");

		for (uint32_t index = 1; index < 4; ++index)
		{
			RenderView receiver = mainView;
			receiver.m_CameraPosition.m_X = static_cast<float>(index) * 10.0f;
			cascades.m_Cascades.push_back({
				.m_View = RenderViewBuilder{}.Build<RenderViewID::DirectionalShadow>({
					.m_MainView = receiver,
				}),
			});
		}
		const auto multipleViewData = RenderSceneBuilder::BuildViewData(frame.m_RenderViews, cascades);
		bool contiguousViews = multipleViewData.m_Views.size() == frame.m_RenderViews.size() + 4;
		bool distinctCascades = true;
		for (uint32_t index = 0; index < 4; ++index)
		{
			const auto& uploadedView = multipleViewData.m_Views[cascades.GetViewIndex(index)];
			contiguousViews &= cascades.GetViewIndex(index) == frame.m_RenderViews.size() + index &&
				uploadedView.ViewMat.ToArray() == cascades.m_Cascades[index].m_View.m_View.ToArray();
			for (uint32_t other = index + 1; other < 4; ++other)
			{
				distinctCascades &= uploadedView.ViewMat.ToArray() != cascades.m_Cascades[other].m_View.m_View.ToArray();
			}
		}
		context.Check(contiguousViews && distinctCascades &&
			multipleViewData.m_Views[utils::ToIndex(RenderViewID::Main)].Width == 1280 &&
			multipleViewData.m_Views[utils::ToIndex(RenderViewID::DebugCamera2)].Width == 640,
			"Multiple shadow views flatten contiguously without overwriting camera slots or aliasing cascades");

		cascades.m_Cascades[2].m_RenderQueue.m_DrawItems.resize(3);
		RenderFrameBuildResult moved = std::move(frame);
		frame = {};
		const auto movedContext = moved.MakeRenderFrameContext();
		context.Check(&movedContext.GetDirectionalShadowFramePlan() == &moved.m_DirectionalShadowFramePlan &&
			movedContext.GetDirectionalShadowFramePlan().GetViewIndex(3) == multipleViewData.m_Views.size() - 1 &&
			movedContext.GetDirectionalShadowFramePlan().m_Cascades[2].m_RenderQueue.m_DrawItems.size() == 3 &&
			movedContext.GetDirectionalShadowFramePlan().m_Cascades[0].m_RenderQueue.m_DrawItems.empty(),
			"Frame moves retain independent cascade queues and bind contexts to the new owner");

		DirectionalShadowFramePlan empty{};
		const auto cameraOnly = RenderSceneBuilder::BuildViewData(moved.m_RenderViews, empty);
		const auto noViews = RenderSceneBuilder::BuildViewData({}, empty);
		context.Check(cameraOnly.m_Views.size() == moved.m_RenderViews.size() && noViews.m_Views.empty(),
			"Empty cascade sets do not invent an uploaded shadow view");

		RenderView jitteredView = mainView;
		jitteredView.m_JitterPixels = Vector2(0.25f, -0.125f);
		jitteredView.m_JitterUV = temporal::JitterPixelsToUV(jitteredView.m_JitterPixels, jitteredView.m_Width, jitteredView.m_Height);
		Matrix clipJitter = Matrix::Identity;
		clipJitter.m_41 = 0.001f;
		clipJitter.m_42 = -0.002f;
		jitteredView.m_RasterProj = jitteredView.m_UnjitteredProj * clipJitter;
		jitteredView.m_RasterViewProj = jitteredView.m_View * jitteredView.m_RasterProj;
		const RenderView postTemporalView = BuildUnjitteredPostTemporalView(jitteredView);
		const auto withPostTemporal = RenderSceneBuilder::BuildViewData(
			moved.m_RenderViews, moved.m_DirectionalShadowFramePlan, &postTemporalView);
		const auto withoutPostTemporal = RenderSceneBuilder::BuildViewData(moved.m_RenderViews, moved.m_DirectionalShadowFramePlan);
		const ViewGPU& postTemporalGpu =
			withPostTemporal.m_Views[withPostTemporal.m_PostTemporalViewOffset];
		context.Check(withPostTemporal.m_PostTemporalViewOffset ==
			moved.m_RenderViews.size() + moved.m_DirectionalShadowFramePlan.m_Cascades.size() &&
			withPostTemporal.m_Views.size() == withPostTemporal.m_PostTemporalViewOffset + 1 &&
			withPostTemporal.m_ShadowViewBaseOffset == withoutPostTemporal.m_ShadowViewBaseOffset &&
			withoutPostTemporal.m_PostTemporalViewOffset == RenderSceneBuilder::UnassignedViewOffset &&
			postTemporalGpu.ProjMat.ToArray() == jitteredView.m_UnjitteredProj.ToArray() &&
			postTemporalGpu.ProjMat.ToArray() != jitteredView.m_RasterProj.ToArray() &&
			postTemporalGpu.ViewMat.ToArray() == jitteredView.m_View.ToArray() &&
			postTemporalGpu.CurrentJitterUV.m_X == 0.0f &&
			postTemporalGpu.CurrentJitterUV.m_Y == 0.0f &&
			postTemporalGpu.Width == jitteredView.m_DisplayWidth &&
			postTemporalGpu.Height == jitteredView.m_DisplayHeight,
			"An unjittered post-temporal view uploads after the shadow views with the "
			"unjittered projection, no jitter and the display extent");
	}

	void RunDirectionalShadowGraphTests(SelfTestContext& context) noexcept
	{
		class ShadowShaderAccess final : public RenderShaderProgramAccess
		{
		public:
			ShaderID LoadProgram(const ShaderProgramRef&) noexcept override { return ShaderID::Invalid(); }
			uint64_t GetGeneration(ShaderID) const noexcept override { return 0; }
		} shaders;
		class ShadowBindingAccess final : public RenderBindingLayoutAccess
		{
		public:
			RHIBindingLayoutHandle GetCommonBindingLayout() const noexcept override { return {}; }
			RHIBindingLayoutDesc GetCommonBindingLayoutDesc() const noexcept override { return {}; }
			const ComposedLayout* GetCommonLayoutContract() const noexcept override
			{
				return &scene_bindings::GetCommonLayoutContract();
			}
		} bindings;
		const RenderServices services{ .m_ShaderPrograms = &shaders, .m_BindingLayout = &bindings };
		for (uint32_t count : { 1u, 4u })
		{
			RenderFrameBuildResult frame{};
			frame.m_DirectionalShadowFramePlan.m_Cascades.resize(count);
			frame.m_DirectionalShadowFramePlan.m_ViewBaseOffset = 7;
			const auto frameContext = frame.MakeRenderFrameContext();
			RenderGraph graph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			struct GraphPassData {};
			graph.AddPass<GraphPassData>("ShadowTest.Setup",
				[count](RenderGraph::RGBuilder& builder, GraphPassData&)
				{
					auto& resources = builder.GetBlackboard().GetOrCreate<RGShadowResources>(ShadowResourcesName);
					resources.m_CascadeCount = count;
					resources.m_DirectionalShadowMap = builder.CreateTexture("ShadowTest.Array", {
						.m_Format = RHIFormat::R32Typeless,
						.m_Extent = { 1024, 1024, 1 },
						.m_ArraySize = static_cast<uint16_t>(count),
					});
				});
			RenderPassDirectionalShadowMap shadowPass;
			shadowPass.AddPass(graph, frameContext, services);
			graph.AddPass<GraphPassData>("ShadowTest.Sample",
				[](RenderGraph::RGBuilder& builder, GraphPassData&)
				{
					builder.Read(builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName).m_DirectionalShadowMap,
						RGTextureAccess::Sample);
					builder.SideEffect();
				});
			const bool compiled = graph.Compile();
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			bool allWritersLive = compiled && snapshot.m_Passes.size() == count + 2;
			for (uint32_t index = 1; allWritersLive && index <= count; ++index)
			{
				allWritersLive &= !snapshot.m_Passes[index].m_Culled &&
					snapshot.m_Passes[index].m_ExecutionOrder < snapshot.m_Passes.back().m_ExecutionOrder &&
					snapshot.m_Passes[index].m_Accesses.size() == 1 &&
					snapshot.m_Passes[index].m_Accesses[0].m_DependencyAccess == RGDependencyAccess::Write;
			}
			context.Check(allWritersLive,
				"Actual cascade passes retain every sampled layer without synthetic GPU reads");
		}
	}

	void RunDirectionalShadowSplitTests(SelfTestContext& context) noexcept
	{
		Camera camera(Camera::CreateInfo{});
		const ResolvedViewRenderSettings settings{};
		const ResolvedTemporalFramePlan plan{};
		RenderView mainView = RenderViewBuilder{}.Build<RenderViewID::Main>({
			.m_Camera = camera, .m_RenderSettings = settings, .m_TemporalFramePlan = plan,
			.m_Resolution = ResolveNativeViewResolution({ 1280, 720 }),
		});
		mainView.m_Near = 1.0f;
		mainView.m_Far = 100.0f;
		DirectionalShadowSettings shadowSettings{};
		const auto defaultCascades = BuildDirectionalShadowFramePlan(mainView, -Vector3::UnitY, shadowSettings);
		context.Check(defaultCascades.m_Cascades.size() == MaxDirectionalShadowCascades &&
			defaultCascades.m_Cascades.back().m_SplitFar == 80.0f,
			"Default directional shadows cover 80 meters when the camera far plane permits it");
		shadowSettings.m_MaxShadowDistance = 81.0f;
		for (float lambda : { 0.0f, 0.65f, 1.0f })
		{
			shadowSettings.m_SplitLambda = lambda;
			auto cascades = BuildDirectionalShadowFramePlan(mainView, -Vector3::UnitY, shadowSettings);
			bool valid = cascades.m_Cascades.size() == 4;
			for (uint32_t index = 0; index < 4; ++index)
			{
				const auto& cascade = cascades.m_Cascades[index];
				const float uniformSplit = 1.0f + 20.0f * static_cast<float>(index + 1);
				const float logarithmicSplit = std::pow(3.0f, static_cast<float>(index + 1));
				valid &= NearlyEqual(cascade.m_SplitFar, uniformSplit * (1.0f - lambda) + logarithmicSplit * lambda) &&
					cascade.m_SplitFar > cascade.m_SplitNear &&
					cascade.m_SplitNear == (index ? cascades.m_Cascades[index - 1].m_SplitFar : 1.0f);
				// Every receiver corner must remain inside its independently fitted light volume.
				for (float depth : { cascade.m_SplitNear, cascade.m_SplitFar })
				{
					const float halfHeight = std::tan(mainView.m_FovRadians * 0.5f) * depth;
					for (float x : { -1.0f, 1.0f })
					{
						for (float y : { -1.0f, 1.0f })
						{
							const Vector3 world = math::TransformPoint(
								Vector3(x * halfHeight * mainView.m_Aspect, y * halfHeight, depth), mainView.m_InvView);
							const Vector4 clip = math::Transform(Vector4(world, 1.0f), cascade.m_View.m_UnjitteredViewProj);
							valid &= std::abs(clip.m_X) <= clip.m_W + 0.001f &&
								std::abs(clip.m_Y) <= clip.m_W + 0.001f &&
								clip.m_Z >= -0.001f && clip.m_Z <= clip.m_W + 0.001f;
						}
					}
				}
			}
			context.Check(valid, "Practical splits cover contiguous main-view depths and fit all receiver corners");
			cascades.m_ViewBaseOffset = 7;
			const auto gpu = BuildDirectionalShadowGPU(cascades, cascades.m_ViewBaseOffset);
			context.Check(gpu.CascadeCount == 4 && gpu.ViewBaseIndex == 7 &&
				gpu.MainViewIndex == static_cast<uint32_t>(RenderViewID::Main) &&
				gpu.NearDepth == 1.0f && gpu.SplitFar[3] == 81.0f,
				"Shadow metadata uses the uploaded cascade range and positive main-camera depths");
		}
		shadowSettings.m_CascadeCount = 1;
		shadowSettings.m_MaxShadowDistance = 150.0f;
		shadowSettings.m_FitMode = DirectionalShadowFitMode::Tight;
		const auto single = BuildDirectionalShadowFramePlan(mainView, -Vector3::UnitY, shadowSettings);
		const auto reference = RenderViewBuilder{}.Build<RenderViewID::DirectionalShadow>({
			.m_MainView = mainView, .m_MaxShadowDistance = 150.0f,
			.m_FilterSupportTexels = 2.0f,
			.m_FitMode = DirectionalShadowFitMode::Tight,
		});
		context.Check(single.m_Cascades.size() == 1 && single.m_Cascades[0].m_SplitFar == 100.0f &&
			single.m_Cascades[0].m_View.m_UnjitteredViewProj.ToArray() == reference.m_UnjitteredViewProj.ToArray(),
			"Single-cascade mode uses the filter guard and clamps shadow distance to camera far");
		context.Check(BuildDirectionalShadowGPU({}, DirectionalShadowFramePlan::UnassignedViewBaseOffset).CascadeCount == 0,
			"Missing shadow views cannot enable a GPU cascade lookup");
	}

	void RunDirectionalShadowPlanningTests(SelfTestContext& context) noexcept
	{
		Camera camera({ .m_Near = 0.1f, .m_Far = 100.0f, .m_Fov = 60.0f, .m_Width = 1280, .m_Height = 720 });
		const ResolvedViewRenderSettings viewSettings{};
		const ResolvedTemporalFramePlan temporal{};
		const auto mainView = RenderViewBuilder{}.Build<RenderViewID::Main>({
			.m_Camera = camera, .m_RenderSettings = viewSettings, .m_TemporalFramePlan = temporal,
			.m_Resolution = ResolveNativeViewResolution({ 1280, 720 }) });
		DirectionalShadowSettings settings{};
		settings.m_Enable = false;
		const auto disabled = BuildDirectionalShadowFramePlan(mainView, -Vector3::UnitY, settings);
		const auto upload = RenderSceneBuilder::BuildViewData({}, disabled);
		RenderGraph emptyGraph({ .m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }) });
		RenderFrameBuildResult frame{};
		frame.m_DirectionalShadowFramePlan = disabled;
		RenderPassDirectionalShadowMap shadowPass;
		shadowPass.AddPass(emptyGraph, frame.MakeRenderFrameContext(), {});
		RGSnapshot emptySnapshot;
		const bool emptyCompiled = emptyGraph.Compile();
		BuildRenderGraphSnapshot(emptyGraph, emptySnapshot);
		context.Check(!disabled.m_ShadingEnabled && disabled.m_Cascades.empty() && upload.m_Views.empty() &&
			BuildDirectionalShadowGPU(disabled, upload.m_ShadowViewBaseOffset).CascadeCount == 0 &&
			emptyCompiled && emptySnapshot.m_Passes.empty() && emptySnapshot.m_Resources.empty(),
			"Disabled shadows without preview demand build no views, queues, GPU cascades or shadow passes");
		const auto preview = BuildDirectionalShadowFramePlan(mainView, -Vector3::UnitY, settings, true);
		context.Check(!preview.m_ShadingEnabled && preview.m_PreviewRequested && preview.m_Cascades.size() == 4,
			"Explicit preview demand builds cascades without enabling shadowed lighting");
		settings.m_Enable = true;
		settings.m_OrthoPadding = 0.0f;
		settings.m_ShadowMapSize = 256;
		for (const auto fit : { DirectionalShadowFitMode::Tight, DirectionalShadowFitMode::StableSphere })
		{
			settings.m_FitMode = fit;
			const auto plan = BuildDirectionalShadowFramePlan(mainView, Vector3(-1.0f, -0.7f, 0.2f), settings);
			bool covered = true;
			for (uint32_t index = 0; index < plan.m_Cascades.size(); ++index)
			{
				const auto& cascade = plan.m_Cascades[index];
				const float receiverNear = index == 0 ? cascade.m_SplitNear : plan.m_Cascades[index - 1].m_BlendStart;
				for (const float depth : { receiverNear, cascade.m_SplitFar })
				{
					const float halfHeight = std::tan(mainView.m_FovRadians * 0.5f) * depth;
					for (float x : { -1.0f, 1.0f })
					{
						for (float y : { -1.0f, 1.0f })
						{
							const Vector3 world = math::TransformPoint(Vector3(x * halfHeight * mainView.m_Aspect,
								y * halfHeight, depth), mainView.m_InvView);
							const Vector4 clip = math::Transform(Vector4(world, 1.0f), cascade.m_View.m_UnjitteredViewProj);
							const float limit = 1.0f - 4.0f / static_cast<float>(settings.m_ShadowMapSize);
							covered &= std::abs(clip.m_X) <= limit + 0.0001f &&
								std::abs(clip.m_Y) <= limit + 0.0001f;
						}
					}
				}
			}
			const auto gpu = BuildDirectionalShadowGPU(plan, 7);
			const float farDepth = plan.m_Cascades.back().m_SplitFar;
			context.Check(covered && gpu.BlendStart[0] < gpu.SplitFar[0] &&
				gpu.DistanceFadeStart < farDepth && NearlyEqual((farDepth - gpu.DistanceFadeStart) * gpu.DistanceFadeInvRange, 1.0f),
				"Cascade overlap includes receiver corners plus the full PCF guard, and the far boundary fades to fully lit");
		}
	}

	void RunDirectionalShadowStabilityTests(SelfTestContext& context) noexcept
	{
		const auto makeView = [](float yaw, float pitch) noexcept
		{
			Camera camera({ .m_Position = Vector3(5.5f, 3.4f, -14.0f),
				.m_Near = 0.1f, .m_Far = 150.0f, .m_Fov = 50.0f, .m_Width = 1920, .m_Height = 1080 });
			camera.SetYawPitch(yaw, pitch);
			camera.Update();
			const ResolvedViewRenderSettings settings{};
			const ResolvedTemporalFramePlan plan{};
			return RenderViewBuilder{}.Build<RenderViewID::Main>({ .m_Camera = camera,
				.m_RenderSettings = settings, .m_TemporalFramePlan = plan, .m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }) });
		};
		const auto translateView = [](RenderView view, const Vector3& offset) noexcept
		{
			view.m_CameraPosition += offset;
			view.m_InvView.Translation(view.m_CameraPosition);
			view.m_View = math::Inverse(view.m_InvView);
			view.m_UnjitteredViewProj = view.m_View * view.m_UnjitteredProj;
			view.m_InvUnjitteredViewProj = math::Inverse(view.m_UnjitteredViewProj);
			view.m_RasterViewProj = view.m_View * view.m_RasterProj;
			view.m_InvRasterViewProj = math::Inverse(view.m_RasterViewProj);
			return view;
		};
		const auto fixedPointTexels = [](const RenderView& view) noexcept
		{
			const Vector4 clip = math::Transform(Vector4(1.0f, 0.5f, 2.0f, 1.0f), view.m_UnjitteredViewProj);
			return Vector2(clip.m_X, clip.m_Y) * (0.5f * static_cast<float>(view.m_Width) / clip.m_W);
		};
		const RenderView mainView = makeView(0.3f, -0.1f);
		const Vector3 lightDirection = Vector3(-1.0f, -0.85f, 0.35f).Normalized();
		const Matrix lightToWorld = math::Transpose(math::CreateLookAtLH(Vector3::Zero, lightDirection, Vector3::UnitY));
		const Vector3 lightRight = math::TransformDirection(Vector3::UnitX, lightToWorld);
		const Vector3 lightUp = math::TransformDirection(Vector3::UnitY, lightToWorld);
		DirectionalShadowSettings settings{};
		settings.m_OrthoPadding = 0.0f;
		const auto baseline = BuildDirectionalShadowFramePlan(mainView, lightDirection, settings);
		bool stableRotation = true;
		for (float yaw : { -1.1f, 0.0f, 1.3f })
		{
			for (float pitch : { -0.4f, 0.0f, 0.5f })
			{
				const auto rotated = BuildDirectionalShadowFramePlan(makeView(yaw, pitch), lightDirection, settings);
				for (uint32_t index = 0; index < MaxDirectionalShadowCascades; ++index)
				{
					const auto& first = baseline.m_Cascades[index];
					const auto& second = rotated.m_Cascades[index];
					const Vector2 delta = fixedPointTexels(second.m_View) - fixedPointTexels(first.m_View);
					stableRotation &= first.m_Projection.m_SphereRadius == second.m_Projection.m_SphereRadius &&
						first.m_Projection.m_Extent.m_X == second.m_Projection.m_Extent.m_X &&
						first.m_Projection.m_Extent.m_Y == second.m_Projection.m_Extent.m_Y &&
						std::abs(delta.m_X - std::round(delta.m_X)) < 0.003f &&
						std::abs(delta.m_Y - std::round(delta.m_Y)) < 0.003f;
				}
			}
		}
		context.Check(stableRotation,
			"Camera rotation preserves each sphere footprint and moves fixed world points by integral shadow texels");

		for (uint32_t index = 0; index < MaxDirectionalShadowCascades; ++index)
		{
			const auto& projection = baseline.m_Cascades[index].m_Projection;
			const Vector2 centerOffset = projection.m_CenterLS - projection.m_UnsnappedCenterLS;
			const RenderView centeredView = translateView(mainView, lightRight * centerOffset.m_X + lightUp * centerOffset.m_Y);
			const auto centered = BuildDirectionalShadowFramePlan(centeredView, lightDirection, settings).m_Cascades[index];
			const float texelSize = centered.m_Projection.m_WorldUnitsPerTexel.m_X;
			const auto moved = BuildDirectionalShadowFramePlan(translateView(centeredView,
				(lightRight + lightUp) * (0.2f * texelSize)), lightDirection, settings).m_Cascades[index];
			const Vector2 smallDelta = fixedPointTexels(moved.m_View) - fixedPointTexels(centered.m_View);
			const auto stepped = BuildDirectionalShadowFramePlan(translateView(centeredView,
				(lightRight - lightUp) * (1.2f * texelSize)), lightDirection, settings).m_Cascades[index];
			const Vector2 stepDelta = fixedPointTexels(stepped.m_View) - fixedPointTexels(centered.m_View);
			context.Check(smallDelta.Length() < 0.003f &&
				std::abs(stepDelta.m_X + 1.0f) < 0.003f && std::abs(stepDelta.m_Y - 1.0f) < 0.003f,
				"Sub-texel XY translation holds the shadow grid and crossing its boundary advances exactly one texel");
			const auto depthMoved = BuildDirectionalShadowFramePlan(translateView(centeredView,
				lightDirection * (0.23f * texelSize)), lightDirection, settings).m_Cascades[index];
			context.Check((fixedPointTexels(depthMoved.m_View) - fixedPointTexels(centered.m_View)).Length() < 0.003f &&
				std::abs(depthMoved.m_View.m_View.m_43 - centered.m_View.m_View.m_43 + 0.23f * texelSize) < 0.0002f,
				"Light-space Z remains continuously fitted while the XY grid stays fixed");
		}

		bool containsCasters = true;
		for (uint32_t resolution : { 2u, 1024u, 2048u })
		{
			settings.m_ShadowMapSize = resolution;
			for (const Vector3 direction : { lightDirection, -Vector3::UnitY })
			{
				for (float yaw : { -0.7f, 0.6f })
				{
					const RenderView receiver = makeView(yaw, 0.4f);
					const auto cascades = BuildDirectionalShadowFramePlan(receiver, direction, settings);
					for (const auto& cascade : cascades.m_Cascades)
					{
						for (float depth : { cascade.m_SplitNear, cascade.m_SplitFar })
						{
							const float halfHeight = std::tan(receiver.m_FovRadians * 0.5f) * depth;
							for (float x : { -1.0f, 1.0f })
							{
								for (float y : { -1.0f, 1.0f })
								{
									const Vector3 corner = math::TransformPoint(Vector3(x * halfHeight * receiver.m_Aspect,
										y * halfHeight, depth), receiver.m_InvView);
									for (float extrusion : { 0.0f, settings.m_CasterExtrusionDistance })
									{
										const Vector4 clip = math::Transform(Vector4(corner - direction * extrusion, 1.0f),
											cascade.m_View.m_UnjitteredViewProj);
										containsCasters &= std::abs(clip.m_X) <= clip.m_W + 0.0001f &&
											std::abs(clip.m_Y) <= clip.m_W + 0.0001f &&
											clip.m_Z >= -0.0001f && clip.m_Z <= clip.m_W + 0.0001f;
									}
								}
							}
						}
					}
				}
			}
		}
		context.Check(containsCasters,
			"Snapped spheres with zero ortho padding cover every receiver and extruded caster corner, including vertical light and low resolution");
		settings.m_ShadowMapSize = DefaultDirectionalShadowMapSize;
		RenderView jittered = mainView;
		jittered.m_JitterPixels = Vector2(0.25f, -0.3f);
		jittered.m_RasterProj.m_31 += 0.01f;
		jittered.m_RasterProj.m_32 -= 0.02f;
		const auto jitteredCascades = BuildDirectionalShadowFramePlan(jittered, lightDirection, settings);
		bool ignoresJitter = true;
		for (uint32_t index = 0; index < MaxDirectionalShadowCascades; ++index)
		{
			ignoresJitter &= jitteredCascades.m_Cascades[index].m_View.m_UnjitteredViewProj.ToArray() ==
				baseline.m_Cascades[index].m_View.m_UnjitteredViewProj.ToArray();
		}
		context.Check(ignoresJitter, "Temporal raster jitter cannot alter a cascade projection");
		settings.m_EnableTexelSnapping = false;
		const auto unsnapped = BuildDirectionalShadowFramePlan(mainView, lightDirection, settings);
		context.Check(!unsnapped.m_Cascades[0].m_Projection.m_TexelSnappingApplied &&
			unsnapped.m_Cascades[0].m_Projection.m_Extent.m_X == baseline.m_Cascades[0].m_Projection.m_Extent.m_X,
			"Snapping A/B preserves the stable footprint");
		settings.m_EnableTexelSnapping = true;
		settings.m_ShadowMapSize = 1;
		const auto oneTexel = BuildDirectionalShadowFramePlan(mainView, Vector3::Zero, settings);
		context.Check(oneTexel.m_Cascades[0].m_View.m_IsValid &&
			!oneTexel.m_Cascades[0].m_Projection.m_TexelSnappingApplied &&
			std::isfinite(oneTexel.m_Cascades[0].m_Projection.m_Extent.m_X),
			"One-texel maps retain a finite unsnapped fit and zero light direction uses its fallback");
	}

	void RunDirectionalShadowBiasTests(SelfTestContext& context) noexcept
	{
		Camera camera({ .m_Position = Vector3(5.5f, 3.4f, -14.0f),
			.m_Near = 0.1f, .m_Far = 150.0f, .m_Fov = 50.0f, .m_Width = 1920, .m_Height = 1080 });
		const ResolvedViewRenderSettings viewSettings{};
		const ResolvedTemporalFramePlan plan{};
		const RenderView mainView = RenderViewBuilder{}.Build<RenderViewID::Main>({ .m_Camera = camera,
			.m_RenderSettings = viewSettings, .m_TemporalFramePlan = plan, .m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }) });
		const Vector3 lightDirection = Vector3(-1.0f, -0.85f, 0.35f).Normalized();
		DirectionalShadowSettings settings{};
		settings.m_ReceiverSlopeBiasTexels = 1.5f;
		settings.m_MaxShadowDistance = 80.0f;
		auto baseline = BuildDirectionalShadowFramePlan(mainView, lightDirection, settings);
		bool worldProjectionMatches = true;
		for (const auto& cascade : baseline.m_Cascades)
		{
			const auto& bias = cascade.m_Bias;
			const Vector3 receiver = mainView.m_CameraPosition + Vector3(0.0f, 0.0f, cascade.m_SplitFar);
			const Vector4 receiverClip = math::Transform(Vector4(receiver, 1.0f), cascade.m_View.m_UnjitteredViewProj);
			for (float slope : { 0.0f, 1.0f, bias.m_ReceiverMaxSlope })
			{
				const float offsetWorld = bias.m_ReceiverConstantWorld + bias.m_ReceiverSlopeWorld * slope;
				const Vector4 offsetClip = math::Transform(Vector4(receiver - lightDirection * offsetWorld, 1.0f),
					cascade.m_View.m_UnjitteredViewProj);
				const float depthDelta = bias.m_ReceiverDepthBias + bias.m_ReceiverSlopeDepthBias * slope;
				worldProjectionMatches &= depthDelta > 0.0f &&
					std::abs(receiverClip.m_Z - offsetClip.m_Z - depthDelta) < 0.000001f;
			}
		}
		context.Check(worldProjectionMatches,
			"Cascade receiver bias equals a world-space offset toward the light under standard Z");
		context.Check(baseline.m_Cascades.back().m_Bias.m_ReceiverConstantWorld >
			baseline.m_Cascades.front().m_Bias.m_ReceiverConstantWorld,
			"Far cascades resolve larger world offsets from their larger texel footprints");

		settings.m_DepthPadding += 100.0f;
		settings.m_CasterExtrusionDistance += 100.0f;
		const auto expandedDepth = BuildDirectionalShadowFramePlan(mainView, lightDirection, settings);
		bool depthInvariant = true;
		for (uint32_t index = 0; index < MaxDirectionalShadowCascades; ++index)
		{
			const auto& before = baseline.m_Cascades[index].m_Bias;
			const auto& after = expandedDepth.m_Cascades[index].m_Bias;
			depthInvariant &= before.m_ReceiverConstantWorld == after.m_ReceiverConstantWorld &&
				before.m_ReceiverSlopeWorld == after.m_ReceiverSlopeWorld &&
				after.m_ReceiverDepthBias < before.m_ReceiverDepthBias &&
				after.m_ReceiverSlopeDepthBias < before.m_ReceiverSlopeDepthBias &&
				std::abs(after.m_ReceiverDepthBias * after.m_DepthSpan - before.m_ReceiverConstantWorld) < 0.000001f;
		}
		context.Check(depthInvariant,
			"Larger caster extrusion and depth padding preserve world bias while reducing its normalized depth delta");

		settings.m_ShadowMapSize = 1024;
		settings.m_FitMode = DirectionalShadowFitMode::Tight;
		const auto lowResolution = BuildDirectionalShadowFramePlan(mainView, lightDirection, settings);
		settings.m_ShadowMapSize = 2048;
		const auto highResolution = BuildDirectionalShadowFramePlan(mainView, lightDirection, settings);
		bool resolutionScales = true;
		for (uint32_t index = 0; index < MaxDirectionalShadowCascades; ++index)
		{
			const auto& low = lowResolution.m_Cascades[index];
			const auto& high = highResolution.m_Cascades[index];
			resolutionScales &= std::abs(low.m_Bias.m_ReceiverDepthBias / high.m_Bias.m_ReceiverDepthBias - 2.0f) < 0.01f &&
				std::abs(low.m_Bias.m_ReceiverSlopeWorld / high.m_Bias.m_ReceiverSlopeWorld - 2.0f) < 0.01f &&
				low.m_Bias.m_WorldUnitsPerTexel == std::max(low.m_Projection.m_WorldUnitsPerTexel.m_X, low.m_Projection.m_WorldUnitsPerTexel.m_Y);
		}
		context.Check(resolutionScales,
			"Doubling resolution approximately halves residual bias including the filter guard, using the larger axis footprint");

		baseline.m_ViewBaseOffset = 7;
		const auto gpu = BuildDirectionalShadowGPU(baseline, baseline.m_ViewBaseOffset);
		bool uploadMatches = gpu.CascadeCount == MaxDirectionalShadowCascades;
		for (uint32_t index = 0; index < MaxDirectionalShadowCascades; ++index)
		{
			const auto& bias = baseline.m_Cascades[index].m_Bias;
			uploadMatches &= gpu.ReceiverDepthBias[index] == bias.m_ReceiverDepthBias &&
				gpu.ReceiverSlopeDepthBias[index] == bias.m_ReceiverSlopeDepthBias &&
				gpu.ReceiverMaxSlope[index] == bias.m_ReceiverMaxSlope;
		}
		context.Check(uploadMatches, "Every uploaded cascade receives its own resolved constant, slope and clamp");

		settings.m_CascadeCount = 1;
		auto single = BuildDirectionalShadowFramePlan(mainView, lightDirection, settings);
		single.m_ViewBaseOffset = 7;
		const auto singleGpu = BuildDirectionalShadowGPU(single, single.m_ViewBaseOffset);
		context.Check(singleGpu.CascadeCount == 1 &&
			singleGpu.ReceiverDepthBias[0] == single.m_Cascades[0].m_Bias.m_ReceiverDepthBias &&
			singleGpu.ReceiverDepthBias[1] == 0.0f && singleGpu.ReceiverSlopeDepthBias[2] == 0.0f &&
			singleGpu.ReceiverMaxSlope[3] == 0.0f,
			"Single-cascade receiver bias uploads only its active slot and leaves unused slots zero");
		settings.m_CascadeCount = MaxDirectionalShadowCascades;
		settings.m_ReceiverBiasTexels = -1.0f;
		settings.m_ReceiverSlopeBiasTexels = -1.0f;
		settings.m_ReceiverMaxSlope = 100.0f;
		const auto bounded = BuildDirectionalShadowFramePlan(mainView, lightDirection, settings);
		context.Check(bounded.m_Cascades[0].m_Bias.m_ReceiverDepthBias == 0.0f &&
			bounded.m_Cascades[0].m_Bias.m_ReceiverSlopeDepthBias == 0.0f &&
			bounded.m_Cascades[0].m_Bias.m_ReceiverMaxSlope == 16.0f,
			"Receiver bias bounds grazing slope and rejects negative authored offsets");
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingShadowsSelfTests(SelfTestContext& context) noexcept
	{
		RunDirectionalShadowCascadeFrameTests(context);
		RunDirectionalShadowSplitTests(context);
		RunDirectionalShadowPlanningTests(context);
		RunDirectionalShadowStabilityTests(context);
		RunDirectionalShadowBiasTests(context);
		RunDirectionalShadowGraphTests(context);
	}
}
