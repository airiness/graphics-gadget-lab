#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/DepthCoverage.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlus.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlusTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/GIProbeVisualization.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAO.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelinePresets.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TypedConstantBinding.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResourceUtils.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraphTypes.h"
#include "GGLabRuntime/Graphics/RenderPipeline/DepthCoverageFramePlan.h"
#include "GGLabRuntime/Graphics/RenderPipeline/ForwardPlusFrameValidation.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include "GGLabRuntime/Graphics/RenderScene.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/ScreenSpace/ScreenSpaceTypes.h"
#include "GGLabRuntime/Graphics/Shader/ShaderProgramCatalog.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/RHIUploadPool.h"
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "Graphics/RenderPass/ForwardBindingLayout.h"
#include "Graphics/RenderPass/ForwardPBRShaderSet.h"
#include "Graphics/RenderPass/RenderPassDepthPrepass.h"
#include "Graphics/RenderPass/RenderPassForwardOpaque.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

namespace gglab::rendering_test
{
	void RunProjectionConventionTests(SelfTestContext& context) noexcept
	{
		constexpr float NearZ = 0.25f;
		constexpr float FarZ = 250.0f;
		constexpr float FovRadians = math::ToRadians(67.0f);
		constexpr float Aspect = 16.0f / 9.0f;

		const Matrix standardProjection = math::CreatePerspectiveFieldOfViewLH(FovRadians, Aspect, NearZ, FarZ);
		const Matrix reversedProjection = math::CreatePerspectiveFieldOfViewLHReversedZ(FovRadians, Aspect, NearZ, FarZ);

		const float standardNear = ProjectPosition(Vector3(0.0f, 0.0f, NearZ), standardProjection).m_RawDepth;
		const float standardFar = ProjectPosition(Vector3(0.0f, 0.0f, FarZ), standardProjection).m_RawDepth;
		const float reversedNear = ProjectPosition(Vector3(0.0f, 0.0f, NearZ), reversedProjection).m_RawDepth;
		const float reversedFar = ProjectPosition(Vector3(0.0f, 0.0f, FarZ), reversedProjection).m_RawDepth;

		context.Check(NearlyEqual(standardNear, 0.0f) && NearlyEqual(standardFar, 1.0f),
			"Standard-Z projection maps near to zero and far to one");
		context.Check(NearlyEqual(reversedNear, 1.0f) && NearlyEqual(reversedFar, 0.0f),
			"Reversed-Z projection maps near to one and far to zero");
		context.Check(
			screen_space::GetDepthBackgroundValue(DepthConvention::Standard) == 1.0f &&
			screen_space::GetDepthBackgroundValue(DepthConvention::Reversed) == 0.0f &&
			screen_space::IsDepthBackground(1.0f, DepthConvention::Standard) &&
			screen_space::IsDepthBackground(0.0f, DepthConvention::Reversed),
			"Depth background values are convention-aware");
		context.Check(
			!screen_space::IsDepthBackground(1.0f - 5.0e-7f, DepthConvention::Standard) &&
			!screen_space::IsDepthBackground(5.0e-7f, DepthConvention::Reversed),
			"Non-clear depth values near the far plane remain geometry");

		constexpr float PrecisionNearZ = 0.05f;
		constexpr float PrecisionFarZ = 5000.0f;
		constexpr float FarGeometryZ = PrecisionFarZ * 0.95f;
		const Matrix precisionStandardProjection = math::CreatePerspectiveFieldOfViewLH(FovRadians, Aspect, PrecisionNearZ, PrecisionFarZ);
		const Matrix precisionReversedProjection =
			math::CreatePerspectiveFieldOfViewLHReversedZ(FovRadians, Aspect, PrecisionNearZ, PrecisionFarZ);
		const float precisionStandardDepth = ProjectPosition(Vector3(0.0f, 0.0f, FarGeometryZ), precisionStandardProjection).m_RawDepth;
		const float precisionReversedDepth = ProjectPosition(Vector3(0.0f, 0.0f, FarGeometryZ), precisionReversedProjection).m_RawDepth;
		context.Check(precisionStandardDepth < 1.0f &&
			1.0f - precisionStandardDepth < 1.0e-6f &&
			!screen_space::IsDepthBackground(precisionStandardDepth, DepthConvention::Standard) &&
			precisionReversedDepth > 0.0f && precisionReversedDepth < 1.0e-6f &&
			!screen_space::IsDepthBackground(precisionReversedDepth, DepthConvention::Reversed),
			"Geometry at 95 percent of the far range is not classified as background");
		context.Check(
			screen_space::IsDepthNearer(0.25f, 0.75f, DepthConvention::Standard) &&
			screen_space::IsDepthFarther(0.75f, 0.25f, DepthConvention::Standard) &&
			screen_space::IsDepthNearer(0.75f, 0.25f, DepthConvention::Reversed) &&
			screen_space::IsDepthFarther(0.25f, 0.75f, DepthConvention::Reversed),
			"Depth ordering is convention-aware");
	}

	void RunPositionReconstructionTests(SelfTestContext& context) noexcept
	{
		constexpr float NearZ = 0.1f;
		constexpr float FarZ = 500.0f;
		const Matrix standardProjection = math::CreatePerspectiveFieldOfViewLH(math::ToRadians(60.0f), 16.0f / 9.0f, NearZ, FarZ);
		const Matrix reversedProjection = math::CreatePerspectiveFieldOfViewLHReversedZ(math::ToRadians(60.0f), 16.0f / 9.0f, NearZ, FarZ);
		const Vector3 positionVS(0.75f, -0.35f, 17.0f);

		const ProjectedPosition standardSample = ProjectPosition(positionVS, standardProjection);
		const ProjectedPosition reversedSample = ProjectPosition(positionVS, reversedProjection);
		const Vector3 reconstructedStandard = screen_space::ReconstructViewPosition(
			standardSample.m_UV, standardSample.m_RawDepth, math::Inverse(standardProjection));
		const Vector3 reconstructedReversed = screen_space::ReconstructViewPosition(
			reversedSample.m_UV, reversedSample.m_RawDepth, math::Inverse(reversedProjection));
		const Vector4 standardDepthParams = screen_space::MakeDepthReconstructionParams(standardProjection);
		const Vector4 reversedDepthParams = screen_space::MakeDepthReconstructionParams(reversedProjection);

		context.Check(NearlyEqual(reconstructedStandard, positionVS),
			"Standard-Z reconstructs a mid-depth view position");
		context.Check(NearlyEqual(reconstructedReversed, positionVS),
			"Reversed-Z reconstructs a mid-depth view position");
		context.Check(
			NearlyEqual(screen_space::RawDepthToPositiveViewZ(reversedSample.m_UV, reversedSample.m_RawDepth, math::Inverse(reversedProjection)),
				positionVS.m_Z, PositionTolerance),
			"Raw depth reconstructs positive left-handed view Z");
		context.Check(
			NearlyEqual(screen_space::RawDepthToPositiveViewZ(standardSample.m_UV, standardSample.m_RawDepth, math::Inverse(standardProjection)),
				positionVS.m_Z, PositionTolerance),
			"Standard-Z raw depth reconstructs positive left-handed view Z");
		context.Check(NearlyEqual(screen_space::RawDepthToPositiveViewZ(
			standardSample.m_RawDepth, standardDepthParams, DepthConvention::Standard),
			positionVS.m_Z, PositionTolerance) &&
			NearlyEqual(screen_space::RawDepthToPositiveViewZ(reversedSample.m_RawDepth, reversedDepthParams, DepthConvention::Reversed),
				positionVS.m_Z, PositionTolerance),
			"Compact depth reconstruction matches Standard-Z and Reversed-Z projection contracts");

		const Matrix view = math::CreateLookAtLH(Vector3(3.0f, 2.0f, -4.0f), Vector3(0.0f, 1.0f, 5.0f), Vector3::UnitY);
		const Matrix viewProjection = view * reversedProjection;
		const Vector3 positionWS(1.25f, 0.5f, 8.0f);
		const ProjectedPosition worldSample = ProjectPosition(positionWS, viewProjection);
		const Vector3 reconstructedWorld = screen_space::ReconstructWorldPosition(
			worldSample.m_UV, worldSample.m_RawDepth, math::Inverse(viewProjection));
		context.Check(NearlyEqual(reconstructedWorld, positionWS),
			"Reversed-Z raw depth reconstructs world position");

		const Matrix standardViewProjection = view * standardProjection;
		const ProjectedPosition standardWorldSample = ProjectPosition(positionWS, standardViewProjection);
		const Vector3 reconstructedStandardWorld =
			screen_space::ReconstructWorldPosition(standardWorldSample.m_UV,
				standardWorldSample.m_RawDepth, math::Inverse(standardViewProjection));
		context.Check(NearlyEqual(reconstructedStandardWorld, positionWS),
			"Standard-Z raw depth reconstructs world position");

		const Matrix degenerateInverseTransform{};
		context.Check(NearlyEqual(screen_space::ReconstructPositionFromRawDepth(Vector2(0.5f, 0.5f), 0.5f, degenerateInverseTransform),
			Vector3::Zero),
			"Position reconstruction returns zero when homogeneous W is degenerate");
	}

	void RunScreenCoordinateTests(SelfTestContext& context) noexcept
	{
		const Vector2 pixelCenter = screen_space::PixelCenterToUV(0, 0, 4, 2);
		const Vector2 ndc = screen_space::UVToNDC(pixelCenter);
		context.Check(NearlyEqual(pixelCenter, Vector2(0.125f, 0.25f)),
			"Pixel centers map to top-left texture UV coordinates");
		context.Check(NearlyEqual(ndc, Vector2(-0.75f, 0.5f)) &&
			NearlyEqual(screen_space::NDCToUV(ndc), pixelCenter),
			"Top-left UV and D3D NDC coordinates round-trip");
	}

	void RunRenderViewConventionTests(SelfTestContext& context) noexcept
	{
		Camera camera(Camera::CreateInfo{});
		const ResolvedViewRenderSettings settings{};
		const ResolvedTemporalFramePlan temporalFramePlan{
			.m_DisplayViewId = RenderViewID::Main,
		};
		const RenderView mainView = RenderViewBuilder{}.Build<RenderViewID::Main>(
			RenderViewBuildInfo<RenderViewID::Main>{
			.m_Camera = camera,
				.m_RenderSettings = settings,
				.m_TemporalFramePlan = temporalFramePlan,
				.m_Resolution = ResolveNativeViewResolution({ 1280, 720 }),
		});
		const RenderView shadowView =
			RenderViewBuilder{}.Build<RenderViewID::DirectionalShadow>(
				RenderViewBuildInfo<RenderViewID::DirectionalShadow>{
			.m_MainView = mainView,
		});
		const ViewResolution nativeResolution = ResolveNativeViewResolution({ 1280, 720 });
		context.Check(nativeResolution.IsNative() &&
			nativeResolution.m_Render == ViewExtent{ 1280, 720 } &&
			mainView.GetResolution() == nativeResolution &&
			mainView.m_Width == 1280 && mainView.m_DisplayHeight == 720 &&
			shadowView.m_DisplayWidth == 0,
			"A camera view carries its render and display extents, which native rendering "
			"keeps equal; shadow views have no display extent");

		const float mainNear = ProjectPosition(Vector3(0.0f, 0.0f, mainView.m_Near), mainView.m_RasterProj).m_RawDepth;
		const float mainFar = ProjectPosition(Vector3(0.0f, 0.0f, mainView.m_Far), mainView.m_RasterProj).m_RawDepth;
		context.Check(mainView.m_DepthConvention == DepthConvention::Reversed &&
			NearlyEqual(mainNear, 1.0f) && NearlyEqual(mainFar, 0.0f),
			"Main view records and projects with its Reversed-Z contract");
		context.Check(shadowView.m_DepthConvention == DepthConvention::Standard,
			"Directional shadow view records its Standard-Z contract");
	}

	void RunSampleableDepthFormatTests(SelfTestContext& context) noexcept
	{
		const RHITextureDesc depthDesc{
			.m_Format = RHIFormat::R32Typeless,
			.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::DepthStencil,
			.m_Extent = { 1280, 720, 1 },
			.m_ClearValue =
				RHIClearValue{
					.m_Format = RHIFormat::D32Float,
					.m_Depth = 0.0f,
					.m_IsDepthStencil = true,
				},
		};
		const RHITextureViewDesc dsvDesc{
			.m_Type = RHITextureViewType::DepthStencil,
			.m_Dimension = RHITextureViewDimension::Texture2D,
			.m_Format = RHIFormat::D32Float,
			.m_Subresources =
				{
					.m_MipCount = 1,
					.m_ArraySliceCount = 1,
					.m_Aspects = RHITextureAspect::Depth,
				},
		};
		const RHITextureViewDesc srvDesc{
			.m_Type = RHITextureViewType::ShaderResource,
			.m_Dimension = RHITextureViewDimension::Texture2D,
			.m_Format = RHIFormat::R32Float,
			.m_Subresources =
				{
					.m_MipCount = 1,
					.m_ArraySliceCount = 1,
					.m_Aspects = RHITextureAspect::Depth,
				},
		};
		context.Check(ValidateRHITextureDesc(depthDesc).IsValid() &&
			ValidateRHITextureViewDesc(depthDesc, dsvDesc).IsValid() &&
			ValidateRHITextureViewDesc(depthDesc, srvDesc).IsValid(),
			"R32 typeless main depth accepts typed D32 DSV and R32 SRV views");

		RHITextureDesc typelessClearDesc = depthDesc;
		typelessClearDesc.m_ClearValue->m_Format = RHIFormat::R32Typeless;
		context.Check(!ValidateRHITextureDesc(typelessClearDesc).IsValid(),
			"Optimized clear values reject typeless formats");

		GraphicsPhysicalPipelineKey reversedWriteRecipe{};
		reversedWriteRecipe.m_DepthPreset = DepthPreset::ReversedZWrite;
		const auto reversedWrite = BuildRHIGraphicsPipelineDesc(reversedWriteRecipe).m_DepthStencil;
		GraphicsPhysicalPipelineKey reversedReadRecipe{};
		reversedReadRecipe.m_DepthPreset = DepthPreset::ReversedZReadOnly;
		const auto reversedRead = BuildRHIGraphicsPipelineDesc(reversedReadRecipe).m_DepthStencil;
		GraphicsPhysicalPipelineKey reversedEqualRecipe{};
		reversedEqualRecipe.m_DepthPreset = DepthPreset::ReversedZEqualReadOnly;
		const auto reversedEqual = BuildRHIGraphicsPipelineDesc(reversedEqualRecipe).m_DepthStencil;
		GraphicsPhysicalPipelineKey probeDepthRecipe{};
		probeDepthRecipe.m_DepthPreset = GetGIProbeDepthPreset(true);
		const auto probeDepth = BuildRHIGraphicsPipelineDesc(probeDepthRecipe).m_DepthStencil;
		context.Check(probeDepth.m_DepthTestEnable && probeDepth.m_DepthWriteEnable &&
			probeDepth.m_DepthCompareOp == RHICompareOp::Greater,
			"Probe depth keeps the earliest stable ID at equal D32 depth without changing production coverage");
		GraphicsPhysicalPipelineKey standardWriteRecipe{};
		standardWriteRecipe.m_DepthPreset = GetGIProbeDepthPreset(false);
		const auto standardWrite = BuildRHIGraphicsPipelineDesc(standardWriteRecipe).m_DepthStencil;
		context.Check(reversedWrite.m_DepthTestEnable && reversedWrite.m_DepthWriteEnable &&
			reversedWrite.m_DepthCompareOp == RHICompareOp::GreaterEqual &&
			reversedRead.m_DepthTestEnable && !reversedRead.m_DepthWriteEnable &&
			reversedRead.m_DepthCompareOp == RHICompareOp::GreaterEqual &&
			reversedEqual.m_DepthTestEnable &&
			!reversedEqual.m_DepthWriteEnable &&
			reversedEqual.m_DepthCompareOp == RHICompareOp::Equal &&
			standardWrite.m_DepthTestEnable && standardWrite.m_DepthWriteEnable &&
			standardWrite.m_DepthCompareOp == RHICompareOp::Less,
			"Depth presets encode explicit Reversed-Z and Standard-Z compare contracts");

		struct SampleableDepthPassData
		{
			RGTextureViewId m_View{};
		};
		RecordingDevice recordingDevice;
		RecordingGraphicsCommandContext recordingGraphicsContext;
		constexpr RHITextureHandle graphDepthHandle{ 41, 7 };
		RenderGraph graph({
			.m_Device = &recordingDevice,
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RGTextureId graphDepth;
		RHITextureDesc graphDepthDesc = depthDesc;
		graphDepthDesc.m_Usage = RHITextureUsage::None;
		graph.AddPass<SampleableDepthPassData>("SampleableDepth.Write",
			[&graphDepth, graphDepthDesc, graphDepthHandle, dsvDesc](RenderGraph::RGBuilder& builder, SampleableDepthPassData& data)
			{
				graphDepth = builder.ImportTexture("DisplayView.DepthBuffer", graphDepthHandle,
					graphDepthDesc, CommonRHIResourceState(), RGContentValidity::Undefined);
				builder.WriteInPlace(graphDepth, RGTextureAccess::DepthStencilWrite);
				data.m_View = builder.CreateView<RHITextureViewType::DepthStencil>(graphDepth, dsvDesc);
			});
		graph.AddPass<SampleableDepthPassData>("SampleableDepth.Sample",
			[&graphDepth, srvDesc](RenderGraph::RGBuilder& builder, SampleableDepthPassData& data)
			{
				graphDepth = builder.Read(graphDepth, RGTextureAccess::Sample, RHIStage::PixelShader);
				data.m_View = builder.CreateView<RHITextureViewType::ShaderResource>(graphDepth, srvDesc);
				builder.SideEffect();
			});

		const bool graphCompiled = graph.Compile();
		context.Check(graphCompiled, "RenderGraph sampleable-depth DSV-to-SRV fixture compiles");
		if (graphCompiled)
		{
			const RHIResourceState commonState = CommonRHIResourceState();
			const RHIResourceState depthWriteState = ToRHIResourceState(RGTextureAccess::DepthStencilWrite);
			const RHIResourceState pixelSampleState = ToRHIResourceState(RGTextureAccess::Sample, RHIStage::PixelShader);
			const auto* executionPlan = graph.GetExecutionPlan();
			const bool hasExactPlannerContract =
				executionPlan && executionPlan->GetResources().size() == 1 &&
				executionPlan->GetPasses().size() == 2 &&
				executionPlan->GetPasses()[0].m_PreBarriers.size() == 1 &&
				executionPlan->GetPasses()[0].m_PostBarriers.empty() &&
				executionPlan->GetPasses()[1].m_PreBarriers.size() == 1 &&
				executionPlan->GetPasses()[1].m_PostBarriers.size() == 1;
			bool plannerBarrierFieldsMatch = hasExactPlannerContract;
			if (hasExactPlannerContract)
			{
				const auto& writeBarrier = executionPlan->GetPasses()[0].m_PreBarriers[0];
				const auto& sampleBarrier = executionPlan->GetPasses()[1].m_PreBarriers[0];
				const auto& finalBarrier = executionPlan->GetPasses()[1].m_PostBarriers[0];
				const auto isFullTextureTransition = [](const RGBarrierIntent& barrier) noexcept
				{
					return barrier.m_Resource.Value() == 0 &&
						barrier.m_Kind == RGBarrierKind::Transition &&
						!barrier.m_Subresources.has_value();
				};
				plannerBarrierFieldsMatch =
					isFullTextureTransition(writeBarrier) &&
					writeBarrier.m_Reason == RGBarrierReason::AccessTransition &&
					writeBarrier.m_Before == commonState &&
					writeBarrier.m_After == depthWriteState &&
					isFullTextureTransition(sampleBarrier) &&
					sampleBarrier.m_Reason == RGBarrierReason::AccessTransition &&
					sampleBarrier.m_Before == depthWriteState &&
					sampleBarrier.m_After == pixelSampleState &&
					isFullTextureTransition(finalBarrier) &&
					finalBarrier.m_Reason == RGBarrierReason::FinalStateTransition &&
					finalBarrier.m_Before == pixelSampleState &&
					finalBarrier.m_After == commonState;
			}
			context.Check(plannerBarrierFieldsMatch,
				"RenderGraph plans the exact Common-to-DSW-to-pixel-SRV-to-Common depth contract");

			const bool dependencyMatches =
				executionPlan &&
				std::ranges::any_of(executionPlan->GetDependencyEdges(),
					[](const RGPassDependencyEdge& edge) noexcept
					{
						return edge.m_From.Value() == 0 && edge.m_To.Value() == 1 &&
							edge.m_Reason == RGDependencyReason::WriterToReader;
					});
			context.Check(dependencyMatches,
				"Sampleable depth preserves its writer-to-reader dependency");

			bool compiledViewsMatch = executionPlan && executionPlan->GetTextureViews().size() == 2;
			if (compiledViewsMatch)
			{
				const auto& compiledDsv = executionPlan->GetTextureViews()[0].m_Desc;
				const auto& compiledSrv = executionPlan->GetTextureViews()[1].m_Desc;
				compiledViewsMatch =
					compiledDsv.m_Type == RHITextureViewType::DepthStencil &&
					compiledDsv.m_Dimension == RHITextureViewDimension::Texture2D &&
					compiledDsv.m_Format == RHIFormat::D32Float &&
					compiledDsv.m_Subresources == dsvDesc.m_Subresources &&
					compiledSrv.m_Type == RHITextureViewType::ShaderResource &&
					compiledSrv.m_Dimension == RHITextureViewDimension::Texture2D &&
					compiledSrv.m_Format == RHIFormat::R32Float &&
					compiledSrv.m_Subresources == srvDesc.m_Subresources;
			}
			context.Check(compiledViewsMatch,
				"Sampleable depth compiles canonical D32 DSV and R32 SRV views of one resource");

			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			const auto expectedUsage = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled;
			context.Check(
				snapshot.m_Resources.size() == 1 &&
				snapshot.m_Resources[0].m_UsageBits == static_cast<uint64_t>(expectedUsage) &&
				snapshot.m_Resources[0].m_TextureFormat == RHIFormat::R32Typeless &&
				snapshot.m_Passes[0].m_Accesses[0].m_AccessValue == static_cast<uint64_t>(RGTextureAccess::DepthStencilWrite) &&
				snapshot.m_Passes[1].m_Accesses[0].m_AccessValue == static_cast<uint64_t>(RGTextureAccess::Sample) &&
				snapshot.m_Passes[0].m_PreBarriers.size() == 1 &&
				snapshot.m_Passes[1].m_PreBarriers.size() == 1 &&
				snapshot.m_Passes[1].m_PostBarriers.size() == 1,
				"RenderGraph snapshot publishes the inferred sampleable-depth contract");

			RGDeferredCommitList executeContextCommits;
			RGExecuteContext executeContext(executeContextCommits, {
				.m_GraphicsCommandContext = &recordingGraphicsContext,
			});
			graph.Execute(executeContext);
			executeContextCommits.Discard();
			const auto isFullResourceBarrier = [](const RHITextureBarrier& barrier) noexcept
			{
				return !barrier.m_Subresources.has_value();
			};
			const bool loweredBarriersMatch =
				recordingGraphicsContext.m_TextureBarriers.size() == 3 &&
				recordingGraphicsContext.m_TextureBarriers[0].m_Texture == graphDepthHandle &&
				isFullResourceBarrier(recordingGraphicsContext.m_TextureBarriers[0]) &&
				recordingGraphicsContext.m_TextureBarriers[0].m_Before == commonState &&
				recordingGraphicsContext.m_TextureBarriers[0].m_After == depthWriteState &&
				recordingGraphicsContext.m_TextureBarriers[1].m_Texture == graphDepthHandle &&
				isFullResourceBarrier(recordingGraphicsContext.m_TextureBarriers[1]) &&
				recordingGraphicsContext.m_TextureBarriers[1].m_Before == depthWriteState &&
				recordingGraphicsContext.m_TextureBarriers[1].m_After == pixelSampleState &&
				recordingGraphicsContext.m_TextureBarriers[2].m_Texture == graphDepthHandle &&
				isFullResourceBarrier(recordingGraphicsContext.m_TextureBarriers[2]) &&
				recordingGraphicsContext.m_TextureBarriers[2].m_Before == pixelSampleState &&
				recordingGraphicsContext.m_TextureBarriers[2].m_After == commonState;
			context.Check(loweredBarriersMatch,
				"RenderGraph executor lowers exactly three resource-specific depth barriers");

			D3D12_RESOURCE_DESC nativeResourceDesc{};
			nativeResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			nativeResourceDesc.Width = 1280;
			nativeResourceDesc.Height = 720;
			nativeResourceDesc.DepthOrArraySize = 1;
			nativeResourceDesc.MipLevels = 1;
			nativeResourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			nativeResourceDesc.SampleDesc = { 1, 0 };
			auto* nativeResource = reinterpret_cast<ID3D12Resource*>(uintptr_t{ 0x1234 });
			bool nativeBarrierMatches = false;
			if (recordingGraphicsContext.m_TextureBarriers.size() == 3)
			{
				const D3D12_TEXTURE_BARRIER nativeBarrier =
					BuildD3D12TextureBarrier(recordingGraphicsContext.m_TextureBarriers[1], nativeResource, nativeResourceDesc);
				nativeBarrierMatches =
					nativeBarrier.SyncBefore == D3D12_BARRIER_SYNC_DEPTH_STENCIL &&
					nativeBarrier.SyncAfter == D3D12_BARRIER_SYNC_PIXEL_SHADING &&
					nativeBarrier.AccessBefore == D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE &&
					nativeBarrier.AccessAfter == D3D12_BARRIER_ACCESS_SHADER_RESOURCE &&
					nativeBarrier.LayoutBefore == D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE &&
					nativeBarrier.LayoutAfter == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE &&
					nativeBarrier.pResource == nativeResource &&
					nativeBarrier.Subresources.IndexOrFirstMipLevel == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
					nativeBarrier.Flags == D3D12_TEXTURE_BARRIER_FLAG_NONE;
			}
			context.Check(nativeBarrierMatches,
				"DX12 lowers the depth transition to the exact Enhanced Barrier contract");
		}

		struct DepthAccessChainPassData
		{
		};
		RenderGraph accessChainGraph({
			.m_Device = &recordingDevice,
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RGTextureId accessChainDepth;
		accessChainGraph.AddPass<DepthAccessChainPassData>("DepthAccessChain.Prepass",
			[&accessChainDepth, graphDepthDesc, graphDepthHandle](RenderGraph::RGBuilder& builder, DepthAccessChainPassData&)
			{
				accessChainDepth = builder.ImportTexture("DepthAccessChain.Depth",
					graphDepthHandle, graphDepthDesc, RGTextureAccess::None,
					RGContentValidity::Undefined);
				builder.WriteInPlace(accessChainDepth, RGTextureAccess::DepthStencilWrite);
			});
		accessChainGraph.AddPass<DepthAccessChainPassData>("DepthAccessChain.Sample",
			[&accessChainDepth](RenderGraph::RGBuilder& builder, DepthAccessChainPassData&)
			{
				accessChainDepth = builder.Read(accessChainDepth, RGTextureAccess::Sample, RHIStage::ComputeShader);
				builder.SideEffect();
			});
		accessChainGraph.AddPass<DepthAccessChainPassData>("DepthAccessChain.Forward",
			[&accessChainDepth](RenderGraph::RGBuilder& builder, DepthAccessChainPassData&)
			{
				accessChainDepth = builder.Read(accessChainDepth, RGTextureAccess::DepthStencilRead);
				builder.SideEffect();
			});

		const bool accessChainCompiled = accessChainGraph.Compile();
		bool accessChainMatches = accessChainCompiled;
		if (accessChainCompiled)
		{
			const auto* plan = accessChainGraph.GetExecutionPlan();
			const RHIResourceState writeState = ToRHIResourceState(RGTextureAccess::DepthStencilWrite);
			const RHIResourceState sampleState = ToRHIResourceState(RGTextureAccess::Sample, RHIStage::ComputeShader);
			const RHIResourceState readState = ToRHIResourceState(RGTextureAccess::DepthStencilRead);
			accessChainMatches = plan && plan->GetPasses().size() == 3 &&
				plan->GetPasses()[0].m_PreBarriers.size() == 1 &&
				plan->GetPasses()[1].m_PreBarriers.size() == 1 &&
				plan->GetPasses()[2].m_PreBarriers.size() == 1 &&
				plan->GetPasses()[2].m_PostBarriers.size() == 1;
			if (accessChainMatches)
			{
				const auto& sampleBarrier = plan->GetPasses()[1].m_PreBarriers.front();
				const auto& readBarrier = plan->GetPasses()[2].m_PreBarriers.front();
				accessChainMatches = sampleBarrier.m_Before == writeState &&
					sampleBarrier.m_After == sampleState &&
					readBarrier.m_Before == sampleState &&
					readBarrier.m_After == readState;
			}
		}
		context.Check(accessChainMatches,
			"RenderGraph preserves the depth-write to sample to read-only-depth access chain");
	}

	void RunForwardPlusReadbackFenceTests(SelfTestContext& context) noexcept
	{
		const RHIFencePoint fence{ RHIFenceHandle{ 19, 3 }, 42 };
		const auto grid = MakeForwardPlusTileGrid(32, 16);
		for (const auto completion : { RHIFenceQueryResult::Completed, RHIFenceQueryResult::Pending,
			RHIFenceQueryResult::Failed, RHIFenceQueryResult::DeviceLost })
		{
			for (const bool nativeQuery : { false, true })
			{
				RecordingDevice device;
				device.m_PassConstantsEnabled = true;
				device.m_FenceQueryResult = completion;
				device.m_ObserveFenceQueryResult = nativeQuery;
				auto readback = std::make_shared<ForwardPlusDebugReadback>();
				readback->Initialize(device, 1);
				readback->PrepareGridBuffer(device, 0, grid);
				context.Check(readback->ConsumeCompletedSlot(0).IsReady() &&
					device.m_PassConstantMapCount == 0 && device.m_FenceQueryCount == 0,
					"An unscheduled Forward+ slot never queries or maps unsubmitted data");
				const uint64_t generation = readback->GetCurrentGeneration();
				readback->MarkScheduled(0, 7, fence, generation, grid, 0, 0);
				readback->MarkHdrDiffScheduled(0, 7, fence, generation, 32, 16);
				readback->MarkGridScheduled(0, 7, fence, generation, grid);
				const auto validation = readback->ConsumeCompletedSlot(0);
				const bool completed = completion == RHIFenceQueryResult::Completed;
				const bool lost = completion == RHIFenceQueryResult::DeviceLost;
				const auto expected = completed ?
					RenderFrameValidationStatus::Ready :
					lost ? RenderFrameValidationStatus::DeviceLost : RenderFrameValidationStatus::ContractFailure;
				ForwardPlusFrameValidationInputs inputs{
					.m_ReadbackValidation = validation,
					.m_PresentationAvailable = true,
					.m_DisplayExtentMatchesPresentation = true,
					.m_RenderSceneReady = true,
					.m_DepthCoverageValid = true,
				};
				context.Check(ClassifyForwardPlusFrame(inputs).m_Status == expected &&
					device.m_PassConstantMapCount == (completed ? 3u : 0u),
					"Only Completed accepts tile, HDR and grid bytes after the frame-slot wait");
				if (completed)
				{
					context.Check(readback->GetLatest().m_FrameSerial == 7 &&
						readback->GetLatestHdrDiff().m_FrameSerial == 7 && readback->GetLatestGrid(),
						"All three completed readback decoders publish the submitted frame");
					continue;
				}
				context.Check(validation.m_Detail.find("fence=(19:3), value=42") != std::string::npos &&
					validation.m_Detail.find(lost ? "DeviceLost" :
						completion == RHIFenceQueryResult::Pending ? "Pending" :
						nativeQuery ? "native completion failure" : "rejected query") != std::string::npos,
					"Fatal diagnostics retain the original queried fence and native-versus-rejected result");
				inputs.m_DisplayExtentMatchesPresentation = false;
				context.Check(ClassifyForwardPlusFrame(inputs).m_Status == expected,
					"Readback failures and confirmed loss take precedence over resize skips");
				const auto allocations = device.m_PassConstantStorage.size();
				const auto destructions = device.m_AtmosphereDestroyedBufferCount;
				readback->InvalidateResults();
				readback->PrepareGridBuffer(device, 0, MakeForwardPlusTileGrid(64, 64));
				readback->Initialize(device, 2);
				readback->MarkScheduled(0, 99, fence, generation, grid, 0, 0);
				readback->MarkHdrDiffScheduled(0, 99, fence, generation, 64, 64);
				readback->MarkGridScheduled(0, 99, fence, generation, grid);
				context.Check(readback->ConsumeCompletedSlot(0).m_Detail == validation.m_Detail &&
					readback->GetScheduledCount() == 1 && device.m_PassConstantMapCount == 0 &&
					device.m_PassConstantStorage.size() == allocations &&
					device.m_AtmosphereDestroyedBufferCount == destructions,
					"A fatal slot retains its original evidence and cannot map, resize or reschedule");
				// Empty services deliberately make any presentation access or preparation invalid.
				RenderPipelineForwardPlus pipeline({ .m_ForwardPlusDebugReadback = readback });
				const RenderScene scene;
				const RenderFrameContext frame{ .m_RenderScene = scene };
				context.Check(pipeline.ValidateRenderFrame(frame, {}).m_Detail == validation.m_Detail,
					"Production validation checks the readback before presentation, resize or pipeline preparation");
			}
		}
		// A later unresolved fence must prevent mapping an earlier completed readback.
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			device.m_UseControlledFenceCompletion = true;
			device.m_CompletedFenceValue = fence.m_Value;
			ForwardPlusDebugReadback readback;
			readback.Initialize(device, 1);
			readback.PrepareGridBuffer(device, 0, grid);
			const auto generation = readback.GetCurrentGeneration();
			readback.MarkScheduled(0, 7, fence, generation, grid, 0, 0);
			readback.MarkHdrDiffScheduled(0, 7,
				{ fence.m_Fence, fence.m_Value + 1 }, generation, 32, 16);
			readback.MarkGridScheduled(0, 7, fence, generation, grid);
			const auto validation = readback.ConsumeCompletedSlot(0);
			context.Check(validation.m_Status == RenderFrameValidationStatus::ContractFailure &&
				validation.m_Detail.find("readback=hdr-diff") != std::string::npos &&
				device.m_PassConstantMapCount == 0 && device.m_FenceQueryCount == 3,
				"All adopted fences are checked before mapping any completed tile, HDR or grid bytes");
		}
		// Generation filtering cannot erase unresolved fences, and completed stale bytes never map.
		for (const bool completed : { false, true })
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			device.m_FenceQueryResult = completed ?
				RHIFenceQueryResult::Completed :
				RHIFenceQueryResult::Pending;
			ForwardPlusDebugReadback readback;
			readback.Initialize(device, 1);
			readback.MarkScheduled(0, 7, fence, readback.GetCurrentGeneration(), grid, 0, 0);
			readback.InvalidateResults();
			context.Check(readback.ConsumeCompletedSlot(0).IsReady() == completed &&
				device.m_PassConstantMapCount == 0 && !readback.GetLatest().m_IsValid,
				"Generation filtering follows completion and never maps stale or unresolved bytes");
		}
	}

	void RunForwardPlusContractTests(SelfTestContext& context) noexcept
	{
		const ForwardPlusFrameValidationInputs ready{
			.m_PresentationAvailable = true,
			.m_DisplayExtentMatchesPresentation = true,
			.m_RenderSceneReady = true,
			.m_HasOpaqueDraws = true,
			.m_GlobalLightCount = ForwardPlusGlobalLightCapacity,
			.m_DepthCoverageValid = true,
		};
		const auto classify = [](ForwardPlusFrameValidationInputs inputs)
		{
			return ClassifyForwardPlusFrame(inputs).m_Status;
		};
		ForwardPlusFrameValidationInputs readyTemporal = ready;
		readyTemporal.m_TemporalActive = true;
		readyTemporal.m_TemporalResolveClosureValid = true;
		ForwardPlusFrameValidationInputs missingPresentation = ready;
		missingPresentation.m_PresentationAvailable = false;
		ForwardPlusFrameValidationInputs resizing = ready;
		resizing.m_DisplayExtentMatchesPresentation = false;
		resizing.m_DepthCoverageValid = false;
		ForwardPlusFrameValidationInputs sceneUnavailable = ready;
		sceneUnavailable.m_RenderSceneReady = false;
		ForwardPlusFrameValidationInputs nativeDiagnostics = ready;
		nativeDiagnostics.m_MaterialDiagnostics = true;
		ForwardPlusFrameValidationInputs upscaledDiagnostics = nativeDiagnostics;
		upscaledDiagnostics.m_NativeResolution = false;
		ForwardPlusFrameValidationInputs upscaledWithoutDiagnostics = upscaledDiagnostics;
		upscaledWithoutDiagnostics.m_MaterialDiagnostics = false;
		context.Check(classify(nativeDiagnostics) == RenderFrameValidationStatus::Ready &&
			classify(upscaledWithoutDiagnostics) == RenderFrameValidationStatus::Ready &&
			classify(upscaledDiagnostics) == RenderFrameValidationStatus::ContractFailure,
			"Frame validation rejects diagnostic MRTs at an upscaled extent before graph construction");
		ForwardPlusFrameValidationInputs closureLost = readyTemporal;
		closureLost.m_TemporalResolveClosureValid = false;
		ForwardPlusFrameValidationInputs closureUnusedWithoutTemporal = closureLost;
		closureUnusedWithoutTemporal.m_TemporalActive = false;
		ForwardPlusFrameValidationInputs gtaoReady = ready;
		gtaoReady.m_GTAOEnabledAndSupported = true;
		gtaoReady.m_GTAOPipelineAvailable = true;
		ForwardPlusFrameValidationInputs gtaoRecipesMissing = gtaoReady;
		gtaoRecipesMissing.m_GTAOPipelineAvailable = false;
		ForwardPlusFrameValidationInputs gtaoUnsupported = gtaoRecipesMissing;
		gtaoUnsupported.m_GTAOEnabledAndSupported = false;
		ForwardPlusFrameValidationInputs lightOverflow = ready;
		lightOverflow.m_GlobalLightCount = ForwardPlusGlobalLightCapacity + 1;
		const RenderFrameValidationResult lightFailure = ClassifyForwardPlusFrame(lightOverflow);
		// Transparent-only frames record no Forward+ opaque shading or GTAO.
		ForwardPlusFrameValidationInputs transparentOnly = lightOverflow;
		transparentOnly.m_HasOpaqueDraws = false;
		transparentOnly.m_GTAOEnabledAndSupported = true;
		transparentOnly.m_GTAOPipelineAvailable = false;
		ForwardPlusFrameValidationInputs coverageMismatch = ready;
		coverageMismatch.m_DepthCoverageValid = false;
		coverageMismatch.m_DepthCoverageDiagnostic = "Coverage variant 1 mismatch";
		const RenderFrameValidationResult coverageFailure = ClassifyForwardPlusFrame(coverageMismatch);
		context.Check(classify(ready) == RenderFrameValidationStatus::Ready &&
			classify(readyTemporal) == RenderFrameValidationStatus::Ready &&
			classify(closureUnusedWithoutTemporal) == RenderFrameValidationStatus::Ready &&
			classify(resizing) == RenderFrameValidationStatus::Skipped &&
			classify(missingPresentation) == RenderFrameValidationStatus::ContractFailure &&
			classify(sceneUnavailable) == RenderFrameValidationStatus::ContractFailure &&
			classify(closureLost) == RenderFrameValidationStatus::ContractFailure &&
			classify(gtaoReady) == RenderFrameValidationStatus::Ready &&
			classify(gtaoUnsupported) == RenderFrameValidationStatus::Ready &&
			classify(gtaoRecipesMissing) == RenderFrameValidationStatus::ContractFailure &&
			classify(transparentOnly) == RenderFrameValidationStatus::Ready &&
			lightFailure.m_Status == RenderFrameValidationStatus::ContractFailure &&
			lightFailure.m_Detail == std::format("requested {}, limit {}", ForwardPlusGlobalLightCapacity + 1, ForwardPlusGlobalLightCapacity) &&
			coverageFailure.m_Status == RenderFrameValidationStatus::ContractFailure &&
			coverageFailure.m_Detail == "Coverage variant 1 mismatch" &&
			!coverageFailure.m_Reason.empty(),
			"Frame validation skips only resize mismatches, limits only opaque work by Forward+ capacity and GTAO readiness, and fails with evidence instead of selecting another lighting path, topology or silently dropping enabled GTAO");

		ForwardPlusFrameValidationInputs gtaoHistoryReady = gtaoReady;
		gtaoHistoryReady.m_GTAOHistoryRequired = true;
		gtaoHistoryReady.m_GTAOTemporalPipelineAvailable = true;
		gtaoHistoryReady.m_GTAOHistoryAvailable = true;
		ForwardPlusFrameValidationInputs gtaoHistoryRecipeMissing = gtaoHistoryReady;
		gtaoHistoryRecipeMissing.m_GTAOTemporalPipelineAvailable = false;
		ForwardPlusFrameValidationInputs gtaoHistoryUnallocated = gtaoHistoryReady;
		gtaoHistoryUnallocated.m_GTAOHistoryAvailable = false;
		// Under the Temporal AA resolve, temporal GTAO keeps no history of its own.
		ForwardPlusFrameValidationInputs gtaoHistoryNotRequired = gtaoHistoryRecipeMissing;
		gtaoHistoryNotRequired.m_GTAOHistoryAvailable = false;
		gtaoHistoryNotRequired.m_GTAOHistoryRequired = false;
		ForwardPlusFrameValidationInputs gtaoHistoryWithoutOpaqueDraws = gtaoHistoryUnallocated;
		gtaoHistoryWithoutOpaqueDraws.m_HasOpaqueDraws = false;
		const RenderFrameValidationResult gtaoRecipeFailure = ClassifyForwardPlusFrame(gtaoHistoryRecipeMissing);
		const RenderFrameValidationResult gtaoAllocationFailure = ClassifyForwardPlusFrame(gtaoHistoryUnallocated);
		context.Check(classify(gtaoHistoryReady) == RenderFrameValidationStatus::Ready &&
			gtaoRecipeFailure.m_Status == RenderFrameValidationStatus::ContractFailure &&
			gtaoAllocationFailure.m_Status == RenderFrameValidationStatus::ContractFailure &&
			gtaoRecipeFailure.m_Reason != gtaoAllocationFailure.m_Reason &&
			classify(gtaoHistoryNotRequired) == RenderFrameValidationStatus::Ready &&
			classify(gtaoHistoryWithoutOpaqueDraws) == RenderFrameValidationStatus::Ready,
			"A recorded GTAO whose temporal plan keeps its own history fails without the accumulation recipe or an allocated history instead of falling back to spatial-only visibility");

		// Production composition must not depend on Lab-owned validation programs.
		const auto productionDemand = shader_programs::GetForwardPBRMaterialDiagnosticsShaderProgramDemand();
		const std::array validationPrograms
		{
			shader_programs::ForwardPBRForwardPlusValidationPixel,
			shader_programs::ForwardPBRForwardPlusValidationGTAOPixel,
			shader_programs::ForwardPBRForwardPlusValidationMaterialDiagnosticsPixel,
			shader_programs::ForwardPBRForwardPlusValidationGTAOMaterialDiagnosticsPixel,
		};
		context.Check(std::ranges::none_of(validationPrograms, [&](const ShaderProgramRef& program)
			{
				return std::ranges::find(productionDemand, program) != productionDemand.end();
			}),
			"Production Forward shader demand excludes HDR-diff validation programs");

		// Startup preload is the production readiness closure: a missing production
		// artifact must fail the preload, not the first frame that needs it.
		const auto initialDemand = shader_programs::GetRendererInitialShaderProgramDemand();
		const auto inInitialDemand = [&initialDemand](const ShaderProgramRef& program) noexcept
		{
			return std::ranges::find(initialDemand, program) != initialDemand.end();
		};
		const std::array productionPrograms
		{
			shader_programs::ForwardPBRAllLightsPixel,
			shader_programs::ForwardPBRForwardPlusPixel,
			shader_programs::ForwardPBRForwardPlusGTAOPixel,
			shader_programs::ForwardPlusCullCompute,
			shader_programs::GTAOEvaluateCompute,
			shader_programs::GTAOTemporalCompute,
			shader_programs::GTAODenoiseXCompute,
			shader_programs::GTAODenoiseYCompute,
			shader_programs::GTAOUpsampleCompute,
			shader_programs::AtmosphereLutCompute,
		};
		const std::array labOwnedPrograms
		{
			shader_programs::ForwardPBRForwardPlusValidationPixel,
			shader_programs::ForwardPBRForwardPlusValidationGTAOPixel,
			shader_programs::ForwardPlusCullDiagnosticsCompute,
			shader_programs::ForwardPlusValidationTilesCompute,
			shader_programs::ForwardPlusValidationFrameCompute,
			shader_programs::GTAOEvaluateDiagnosticsCompute,
			shader_programs::GTAOTemporalDiagnosticsCompute,
		};
		context.Check(std::ranges::all_of(productionPrograms, inInitialDemand) &&
			std::ranges::none_of(labOwnedPrograms, inInitialDemand),
			"Renderer startup demand preloads production Forward+, GTAO and atmosphere programs and excludes Lab-owned validation and diagnostics programs");

		ForwardPBRShaderSet shaderSet{};
		shaderSet.m_CoverageVertexShader = ShaderID{ 1 };
		shaderSet.m_AllLightsShadingPixelShader = ShaderID{ 2 };
		shaderSet.m_ForwardPlus.m_Shading = ShaderID{ 3 };
		shaderSet.m_ForwardPlus.m_GTAOContribution = ShaderID{ 5 };
		shaderSet.m_AlphaTestPixelShader = ShaderID{ 6 };
		shaderSet.m_VelocityOpaquePixelShader = ShaderID{ 7 };
		shaderSet.m_VelocityAlphaTestPixelShader = ShaderID{ 8 };
		shaderSet.m_AllLightsMaterialDiagnosticsPixelShader = ShaderID{ 10 };
		shaderSet.m_ForwardPlus.m_MaterialDiagnostics = ShaderID{ 11 };
		shaderSet.m_ForwardPlus.m_GTAOContributionMaterialDiagnostics = ShaderID{ 12 };
		const bool productionValid = shaderSet.IsValid() && shaderSet.AreMaterialDiagnosticsValid();
		shaderSet.m_IncludesHdrDiffValidation = true;
		const bool validationRequiresPrograms = !shaderSet.IsValid() && !shaderSet.AreMaterialDiagnosticsValid();
		shaderSet.m_ForwardPlusValidation = {
			.m_Shading = ShaderID{ 20 },
			.m_GTAOContribution = ShaderID{ 21 },
			.m_MaterialDiagnostics = ShaderID{ 22 },
			.m_GTAOContributionMaterialDiagnostics = ShaderID{ 23 },
		};
		context.Check(productionValid && validationRequiresPrograms &&
			shaderSet.IsValid() && shaderSet.AreMaterialDiagnosticsValid(),
			"Forward shader validity requires HDR-diff programs only for the Lab-composed validation recipe");

		const ForwardPlusHdrDiffReadback withinTolerance{
			.m_MaxAbsoluteError = ForwardPlusHdrDiffAbsoluteTolerance,
			.m_MaxRelativeLuminanceError = ForwardPlusHdrDiffRelativeLuminanceTolerance,
			.m_ComparedPixelCount = 1,
			.m_IsValid = true,
		};
		ForwardPlusHdrDiffReadback outsideTolerance = withinTolerance;
		outsideTolerance.m_MaxAbsoluteError = std::nextafter(ForwardPlusHdrDiffAbsoluteTolerance, std::numeric_limits<float>::infinity());
		context.Check(IsForwardPlusHdrDiffWithinTolerance(withinTolerance) &&
			!IsForwardPlusHdrDiffWithinTolerance(outsideTolerance),
			"Forward+ HDR diff acceptance uses explicit inclusive absolute and relative luminance tolerances");
		context.Check(IsForwardPlusReadbackGenerationCurrent(9, 9) &&
			!IsForwardPlusReadbackGenerationCurrent(8, 9) &&
			!IsForwardPlusReadbackGenerationCurrent(0, 9),
			"Forward+ diagnostics reject stale and uninitialized readback generations");
		context.Check(ShouldPublishForwardPlusReadback(9, 103, 9, 9, 102) &&
			!ShouldPublishForwardPlusReadback(9, 101, 9, 9, 102) &&
			!ShouldPublishForwardPlusReadback(8, 104, 9, 9, 102),
			"Forward+ diagnostics publish only current-generation non-regressing frames");
		context.Check(IsForwardPlusGlobalLightCountSupported(0) &&
			IsForwardPlusGlobalLightCountSupported(ForwardPlusGlobalLightCapacity) &&
			!IsForwardPlusGlobalLightCountSupported(ForwardPlusGlobalLightCapacity + 1),
			"Forward+ uses a bounded global-light loop whose capacity frame validation enforces");
		std::array<uint32_t, 4> unsortedGlobalLightIndices{ 31, 4, 19, 7 };
		SortForwardPlusGlobalLightIndices(unsortedGlobalLightIndices);
		context.Check(unsortedGlobalLightIndices == std::array<uint32_t, 4>{ 4, 7, 19, 31 },
			"Forward+ establishes a stable ascending global-light order");
		const ForwardPlusTileGrid metricsGrid = MakeForwardPlusTileGrid(32, 16);
		const std::array<ForwardPlusTileHeader, 2> metricHeaders
		{
			ForwardPlusTileHeader{ .m_Offset = 0, .m_CountAndFlags = 0 },
			ForwardPlusTileHeader{ .m_Offset = 64, .m_CountAndFlags = 3 },
		};
		const std::array<ForwardPlusTileDepthRange, 2> metricDepthRanges
		{
			ForwardPlusTileDepthRange{},
			ForwardPlusTileDepthRange{ .m_MinViewZ = 2.0f, .m_MaxViewZ = 7.0f },
		};
		const ForwardPlusGridMetrics gridMetrics = BuildForwardPlusGridMetrics(metricsGrid, metricHeaders, metricDepthRanges);
		context.Check(gridMetrics.m_IsValid &&
			gridMetrics.m_NonEmptyLightListTileCount == 1 &&
			gridMetrics.m_EmptyLightListTileCount == 1 &&
			gridMetrics.m_TotalLightReferences == 3 &&
			gridMetrics.m_AverageLightsPerTile == 1.5 &&
			gridMetrics.m_MaxLightsPerTile == 3 &&
			gridMetrics.m_OverflowTileCount == 0 && gridMetrics.m_MinViewZ == 2.0f &&
			gridMetrics.m_MaxViewZ == 7.0f,
			"Forward+ full-grid diagnostics aggregate an exact deterministic fixture");

		const ForwardPlusTileGrid grid1080 = MakeForwardPlusTileGrid(1920, 1080);
		const ForwardPlusTileGrid grid4K = MakeForwardPlusTileGrid(3840, 2160);
		context.Check(grid1080.IsValid() && grid1080.m_TileCountX == 120 &&
			grid1080.m_TileCountY == 68 && grid1080.m_TileCount == 8160 &&
			grid4K.IsValid() && grid4K.m_TileCountX == 240 &&
			grid4K.m_TileCountY == 135 && grid4K.m_TileCount == 32400 &&
			!MakeForwardPlusTileGrid(0, 1080).IsValid(),
			"Forward+ tile grids use exact 16x16 ceil-division");

		const ForwardPlusTileHeader header{
			.m_Offset = GetForwardPlusTileOffset(7),
			.m_CountAndFlags = 0xabcd0005u,
		};
		context.Check(GetForwardPlusTileOffset(0) == 0 && GetForwardPlusTileOffset(7) == 448 &&
			header.m_Offset == 448 && header.GetCount() == 5,
			"Forward+ headers preserve fixed-stride address and masked count semantics");

		std::array<uint8_t, ForwardPlusTileLightCapacity> emptyHits{};
		const auto emptyWave32 = BuildStableForwardPlusLightList(emptyHits, 32);
		const auto emptyWave64 = BuildStableForwardPlusLightList(emptyHits, 64);

		auto oneHit = emptyHits;
		oneHit[37] = 1;
		const auto oneWave32 = BuildStableForwardPlusLightList(oneHit, 32);
		const auto oneWave64 = BuildStableForwardPlusLightList(oneHit, 64);

		std::array<uint8_t, ForwardPlusTileLightCapacity> allHits{};
		allHits.fill(1);
		const auto allWave32 = BuildStableForwardPlusLightList(allHits, 32);
		const auto allWave64 = BuildStableForwardPlusLightList(allHits, 64);

		auto patternedHits = emptyHits;
		for (uint32_t lightIndex = 0; lightIndex < ForwardPlusTileLightCapacity; ++lightIndex)
		{
			patternedHits[lightIndex] = (lightIndex % 3u) == 1u || (lightIndex % 11u) == 0u;
		}
		const auto patternedWave32 = BuildStableForwardPlusLightList(patternedHits, 32);
		const auto patternedWave64 = BuildStableForwardPlusLightList(patternedHits, 64);
		context.Check(emptyWave32.empty() && emptyWave32 == emptyWave64 &&
			oneWave32 == std::vector<uint32_t>{37} && oneWave32 == oneWave64 &&
			allWave32.size() == ForwardPlusTileLightCapacity &&
			allWave32 == allWave64 && allWave32.front() == 0 &&
			allWave32.back() == 63 && patternedWave32 == patternedWave64 &&
			std::ranges::is_sorted(patternedWave32),
			"Forward+ stable compaction preserves global light order for 0, 1, and 64 lights across Wave32 and Wave64 partitions");

		PersistentStructuredBufferTable<uint64_t, uint32_t> fullCapacityTable(2, 1);
		fullCapacityTable.BeginUpdate();
		GGLAB_UNUSED(fullCapacityTable.Upsert(1, 10));
		fullCapacityTable.EndUpdate();
		fullCapacityTable.BeginUpdate();
		const uint32_t firstReplacementSlot = fullCapacityTable.Upsert(2, 20);
		const uint32_t secondReplacementSlot = fullCapacityTable.Upsert(3, 30);
		fullCapacityTable.EndUpdate();
		bool oldKeyRemains = false;
		bool firstKeyPresent = false;
		bool secondKeyPresent = false;
		for (uint32_t slot = 0; slot < 2; ++slot)
		{
			const auto& key = fullCapacityTable.GetKey(slot);
			oldKeyRemains |= key && *key == 1;
			firstKeyPresent |= key && *key == 2;
			secondKeyPresent |= key && *key == 3;
		}
		context.Check(firstReplacementSlot != decltype(fullCapacityTable)::InvalidSlot &&
			secondReplacementSlot != decltype(fullCapacityTable)::InvalidSlot &&
			firstReplacementSlot != secondReplacementSlot &&
			fullCapacityTable.GetLiveCount() == 2 && !oldKeyRemains &&
			firstKeyPresent && secondKeyPresent,
			"Persistent GPU tables can replace a full prior key set without transient capacity overflow");
	}

	void RunDepthCoverageContractTests(SelfTestContext& context) noexcept
	{
		const auto makeVariantBits = [](RenderBucket bucket, bool doubleSided) noexcept
		{
			uint64_t bits = static_cast<uint64_t>(bucket)
				<< RenderQueueBuilder::VariantBit::BucketShift;
			if (doubleSided)
			{
				bits |= RenderQueueBuilder::VariantBit::DoubleSided;
			}
			return bits;
		};

		GraphicsPhysicalPipelineKey basePhysicalKey{};
		basePhysicalKey.m_VSId = ShaderID{ 17 };
		basePhysicalKey.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
		basePhysicalKey.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		basePhysicalKey.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		basePhysicalKey.m_Formats.m_SampleCount = 1;
		basePhysicalKey.m_Formats.m_SampleQuality = 0;
		basePhysicalKey.m_RasterizerPreset = RasterizerPreset::Default;
		basePhysicalKey.m_DepthPreset = DepthPreset::ReversedZWrite;
		basePhysicalKey.m_BlendPreset = BlendPreset::Default;

		const auto opaque =
			RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(basePhysicalKey, makeVariantBits(RenderBucket::Opaque, false));
		const auto opaqueRepeat =
			RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(basePhysicalKey, makeVariantBits(RenderBucket::Opaque, false));
		const auto alphaTest =
			RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(
				basePhysicalKey, makeVariantBits(RenderBucket::AlphaTest, false));
		const auto doubleSided =
			RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(basePhysicalKey, makeVariantBits(RenderBucket::Opaque, true));
		const auto transparent =
			RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(
				basePhysicalKey, makeVariantBits(RenderBucket::Transparent, false));
		GraphicsPhysicalPipelineKey prepassPhysicalKey = basePhysicalKey;
		prepassPhysicalKey.m_Formats.m_RenderTargetCount = 0;
		prepassPhysicalKey.m_BlendPreset = BlendPreset::ColorWriteDisable;
		const auto prepassOpaque =
			RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
				prepassPhysicalKey, makeVariantBits(RenderBucket::Opaque, false));
		const auto prepassAlpha =
			RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
				prepassPhysicalKey, makeVariantBits(RenderBucket::AlphaTest, false));
		context.Check(
			(opaque && opaqueRepeat && alphaTest && doubleSided && prepassOpaque && prepassAlpha) &&
			(*opaque == *opaqueRepeat && *opaque == *prepassOpaque &&
				*alphaTest == *prepassAlpha) &&
			!transparent,
			"Prepass and Forward variants generate matching stable coverage signatures");
		if (!opaque || !alphaTest || !doubleSided || !prepassOpaque || !prepassAlpha)
		{
			return;
		}

		const uint64_t opaqueVariant = makeVariantBits(RenderBucket::Opaque, false);
		const GraphicsLogicalPipelineMetadata forwardMetadata =
			RenderPassForwardOpaque::BuildLogicalPipelineMetadataForVariant(basePhysicalKey, opaqueVariant);
		const GraphicsLogicalPipelineMetadata forwardMetadataRepeat =
			RenderPassForwardOpaque::BuildLogicalPipelineMetadataForVariant(basePhysicalKey, opaqueVariant);
		const GraphicsLogicalPipelineMetadata prepassMetadata =
			RenderPassDepthPrepass::BuildLogicalPipelineMetadataForVariant(prepassPhysicalKey, opaqueVariant);
		context.Check(
			forwardMetadata == forwardMetadataRepeat && forwardMetadata == prepassMetadata,
			"Logical coverage metadata is deterministic before any physical pipeline is resolved");

		DepthCoveragePipelineSignature alphaNormalized = *alphaTest;
		alphaNormalized.m_AlphaVariant = DepthCoverageAlphaVariant::Opaque;
		DepthCoveragePipelineSignature doubleSidedNormalized = *doubleSided;
		doubleSidedNormalized.m_CullMode = opaque->m_CullMode;
		doubleSidedNormalized.m_DoubleSided = false;
		context.Check(alphaNormalized == *opaque && doubleSidedNormalized == *opaque,
			"Coverage variants differ only in their declared pipeline fields");

		const auto differsAfter = [&opaque](auto mutate) noexcept
		{
			DepthCoveragePipelineSignature changed = *opaque;
			mutate(changed);
			return changed != *opaque;
		};
		const bool pipelineFieldsParticipate =
			differsAfter([](auto& value) { value.m_CoverageVertexShader = ShaderID{ 18 }; }) &&
			differsAfter([](auto& value)
				{
					value.m_VertexProgram = DepthCoverageVertexProgram::SkinnedMesh;
				}) &&
			differsAfter([](auto& value)
				{
					value.m_Deformation = DepthCoverageDeformationVariant::Skinned;
				}) &&
			differsAfter([](auto& value) { value.m_InputLayout = InputLayoutID::P3C4; }) &&
			differsAfter([](auto& value)
				{
					value.m_PositionPrecision = DepthCoveragePositionPrecision::Float16;
				}) &&
			differsAfter(
				[](auto& value) { value.m_PositionFormat = RHIFormat::R32G32B32A32Float; }) &&
			differsAfter(
				[](auto& value) { value.m_TopologyType = RHIPrimitiveTopologyType::Line; }) &&
			differsAfter([](auto& value)
				{
					value.m_PrimitiveTopology = RHIPrimitiveTopology::LineList;
				}) &&
			differsAfter([](auto& value) { value.m_FillMode = RHIFillMode::Wireframe; }) &&
			differsAfter([](auto& value) { value.m_CullMode = RHICullMode::Front; }) &&
			differsAfter([](auto& value) { value.m_FrontCounterClockwise = true; }) &&
			differsAfter([](auto& value) { value.m_DepthClipEnable = false; }) &&
			differsAfter([](auto& value) { value.m_DoubleSided = true; }) &&
			differsAfter([](auto& value) { ++value.m_DepthBias; }) &&
			differsAfter([](auto& value) { value.m_DepthBiasClamp = 1.0f; }) &&
			differsAfter([](auto& value) { value.m_SlopeScaledDepthBias = 1.0f; }) &&
			differsAfter([](auto& value) { value.m_SampleCount = 4; }) &&
			differsAfter([](auto& value) { value.m_SampleQuality = 1; }) &&
			differsAfter([](auto& value) { value.m_SampleMask = 0x7FFFFFFFu; }) &&
			differsAfter([](auto& value) { value.m_AlphaToCoverageEnable = true; }) &&
			differsAfter([](auto& value)
				{
					value.m_AlphaVariant = DepthCoverageAlphaVariant::BaseColorMask;
				});
		context.Check(pipelineFieldsParticipate,
			"Every coverage pipeline field participates in signature identity");

		GraphicsPhysicalPipelineKey nonCoveragePhysicalKey = basePhysicalKey;
		nonCoveragePhysicalKey.m_DepthPreset = DepthPreset::ReversedZReadOnly;
		nonCoveragePhysicalKey.m_BlendPreset = BlendPreset::AlphaBlend;
		nonCoveragePhysicalKey.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
		const auto nonCoverageSignature =
			RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(
				nonCoveragePhysicalKey, makeVariantBits(RenderBucket::Opaque, false));
		context.Check(nonCoverageSignature && *nonCoverageSignature == *opaque,
			"Depth, blend, and render-target state do not change coverage identity");

		const GraphicsLogicalPipelineMetadata opaqueMetadata{
			.m_DepthCoveragePipelineSignature = *opaque,
		};
		const GraphicsLogicalPipelineMetadata alphaMetadata{
			.m_DepthCoveragePipelineSignature = *alphaTest,
		};
		const GraphicsPhysicalPipelineKey opaquePhysicalKey = basePhysicalKey;
		const GraphicsPhysicalPipelineKey alphaPhysicalKey = basePhysicalKey;
		context.Check(opaquePhysicalKey == alphaPhysicalKey && opaqueMetadata != alphaMetadata,
			"Logical coverage metadata changes without changing the physical pipeline key");

		const DepthCoverageRasterDomain rasterDomain{
			.m_FrameSerial = 23,
			.m_ViewBindingId = 1,
			.m_CurrentViewSource =
				{
					.m_Buffer = RHIBufferHandle{ 4, 5 },
					.m_OffsetInBytes = 512,
					.m_ElementIndex = 29,
				},
			.m_CurrentJitteredProjectionSource =
				{
					.m_Buffer = RHIBufferHandle{ 4, 5 },
					.m_OffsetInBytes = 512,
					.m_ElementIndex = 29,
				},
			.m_ProjectionSource = DepthCoverageProjectionSource::ViewDataProjection,
			.m_TargetWidth = 1280,
			.m_TargetHeight = 720,
			.m_Viewport =
				{
					.m_Width = 1280.0f,
					.m_Height = 720.0f,
				},
			.m_Scissor =
				{
					.m_Right = 1280,
					.m_Bottom = 720,
				},
			.m_DepthConvention = DepthConvention::Reversed,
		};
		const auto rasterDomainDiffersAfter = [&rasterDomain](auto mutate) noexcept
		{
			DepthCoverageRasterDomain changed = rasterDomain;
			mutate(changed);
			return changed != rasterDomain;
		};
		DepthCoverageRasterDomain differentSlice = rasterDomain;
		differentSlice.m_CurrentViewSource.m_OffsetInBytes += 256;
		differentSlice.m_CurrentJitteredProjectionSource.m_OffsetInBytes += 256;
		const auto sliceMismatch = CompareDepthCoverageRasterDomains(rasterDomain, differentSlice);
		context.Check(!sliceMismatch.m_Matches &&
			sliceMismatch.m_Mismatch.find("CurrentViewSource") != std::string::npos &&
			DescribeDepthCoverageRasterDomain(rasterDomain) != DescribeDepthCoverageRasterDomain(differentSlice),
			"Coverage distinguishes two slices on the same page with the same relative view index");
		context.Check(
			rasterDomain.IsValid() &&
			CompareDepthCoverageRasterDomains(rasterDomain, rasterDomain).m_Matches &&
			rasterDomainDiffersAfter([](auto& value) { ++value.m_FrameSerial; }) &&
			rasterDomainDiffersAfter([](auto& value) { ++value.m_ViewBindingId; }) &&
			rasterDomainDiffersAfter([](auto& value)
				{
					value.m_CurrentViewSource.m_Buffer = RHIBufferHandle{ 4, 6 };
				}) &&
			rasterDomainDiffersAfter([](auto& value)
				{
					++value.m_CurrentJitteredProjectionSource.m_ElementIndex;
				}) &&
			rasterDomainDiffersAfter(
				[](auto& value)
				{
					value.m_ProjectionSource = DepthCoverageProjectionSource::DedicatedJitteredProjection;
				}) &&
			rasterDomainDiffersAfter([](auto& value) { ++value.m_TargetWidth; }) &&
			rasterDomainDiffersAfter([](auto& value) { ++value.m_TargetHeight; }) &&
			rasterDomainDiffersAfter([](auto& value) { value.m_Viewport.m_X = 1.0f; }) &&
			rasterDomainDiffersAfter([](auto& value) { value.m_Viewport.m_Y = 1.0f; }) &&
			rasterDomainDiffersAfter(
				[](auto& value) { value.m_Viewport.m_Width = 1920.0f; }) &&
			rasterDomainDiffersAfter(
				[](auto& value) { value.m_Viewport.m_Height = 1080.0f; }) &&
			rasterDomainDiffersAfter(
				[](auto& value) { value.m_Viewport.m_MinDepth = 0.25f; }) &&
			rasterDomainDiffersAfter(
				[](auto& value) { value.m_Viewport.m_MaxDepth = 0.75f; }) &&
			rasterDomainDiffersAfter([](auto& value) { value.m_Scissor.m_Left = 1; }) &&
			rasterDomainDiffersAfter([](auto& value) { value.m_Scissor.m_Top = 1; }) &&
			rasterDomainDiffersAfter([](auto& value) { value.m_Scissor.m_Right = 1920; }) &&
			rasterDomainDiffersAfter(
				[](auto& value) { value.m_Scissor.m_Bottom = 1080; }) &&
			rasterDomainDiffersAfter(
				[](auto& value) { value.m_DepthConvention = DepthConvention::Standard; }),
			"Raster-domain identity covers the complete view and raster state");

		DepthCoverageRasterDomain undersizedDomain = rasterDomain;
		undersizedDomain.m_TargetWidth = 1279;
		DepthCoverageRasterDomain oversizedViewportDomain = rasterDomain;
		oversizedViewportDomain.m_Viewport.m_Width = 1281.0f;
		const RHITextureDesc coverageColorDesc{
			.m_Format = RHIFormat::R16G16B16A16Float,
			.m_Extent = { 1280, 720, 1 },
		};
		const RHITextureDesc coverageDepthDesc{
			.m_Format = RHIFormat::R32Typeless,
			.m_Extent = { 1280, 720, 1 },
		};
		RHITextureDesc mismatchedCoverageDepthDesc = coverageDepthDesc;
		mismatchedCoverageDepthDesc.m_Extent.m_Height = 719;
		context.Check(rasterDomain.MatchesTargetExtent(1280, 720) &&
			!rasterDomain.MatchesTargetExtent(1281, 720) &&
			!undersizedDomain.IsValid() && !oversizedViewportDomain.IsValid() &&
			AreDepthCoverageTargetExtentsCompatible(rasterDomain, coverageColorDesc, coverageDepthDesc) &&
			!AreDepthCoverageTargetExtentsCompatible(rasterDomain, coverageColorDesc, mismatchedCoverageDepthDesc),
			"Raster domains and target descriptors reject color-depth extent mismatches");
		std::array<RHITextureDesc, forward_shading::MaterialDiagnosticTargetCount> diagnostics
		{
		coverageColorDesc, coverageColorDesc, coverageColorDesc };
		bool diagnosticExtentsChecked = forward_shading::AreSceneAttachmentExtentsCompatible(
			rasterDomain, coverageColorDesc, coverageDepthDesc, diagnostics) &&
			forward_shading::AreSceneAttachmentExtentsCompatible(rasterDomain, coverageColorDesc, coverageDepthDesc, {}) &&
			!forward_shading::AreSceneAttachmentExtentsCompatible(rasterDomain, coverageColorDesc, mismatchedCoverageDepthDesc, diagnostics);
		for (auto& diagnostic : diagnostics)
		{
			diagnostic.m_Extent = { 853, 480, 1 };
			diagnosticExtentsChecked &= !forward_shading::AreSceneAttachmentExtentsCompatible(
				rasterDomain, coverageColorDesc, coverageDepthDesc, diagnostics);
			diagnostic = coverageColorDesc;
		}
		context.Check(diagnosticExtentsChecked,
			"Forward attachment validation checks every diagnostic MRT as well as color and depth");

		DepthCoverageRasterDomain changedRasterDomain = rasterDomain;
		++changedRasterDomain.m_FrameSerial;
		changedRasterDomain.m_TargetWidth = 1920;
		changedRasterDomain.m_Viewport.m_Width = 1920.0f;
		const auto pipelineComparison = CompareDepthCoveragePipelineSignatures(*opaque, *alphaTest);
		const auto rasterComparison = CompareDepthCoverageRasterDomains(rasterDomain, changedRasterDomain);
		context.Check(
			!pipelineComparison.m_Matches &&
			pipelineComparison.m_Mismatch.find("AlphaVariant") != std::string::npos &&
			!rasterComparison.m_Matches &&
			rasterComparison.m_Mismatch.find("FrameSerial") != std::string::npos &&
			rasterComparison.m_Mismatch.find("Viewport") != std::string::npos &&
			DescribeDepthCoveragePipelineSignature(*opaque).find("InputLayout") != std::string::npos &&
			DescribeDepthCoverageRasterDomain(rasterDomain).find("Scissor") != std::string::npos,
			"Coverage diagnostics identify pipeline and raster-domain mismatches");

		const auto invalidPipelineComparison = CompareDepthCoveragePipelineSignatures(
			DepthCoveragePipelineSignature{}, DepthCoveragePipelineSignature{});
		DepthCoverageRasterDomain invalidRhsDomain = rasterDomain;
		invalidRhsDomain.m_FrameSerial = 0;
		const auto invalidRasterComparison = CompareDepthCoverageRasterDomains(rasterDomain, invalidRhsDomain);
		context.Check(!invalidPipelineComparison.m_Matches &&
			invalidPipelineComparison.m_Mismatch.find("Pipeline.LhsInvalid") != std::string::npos &&
			!invalidRasterComparison.m_Matches &&
			invalidRasterComparison.m_Mismatch.find("RasterDomain.RhsInvalid") != std::string::npos,
			"Coverage comparison fails closed for invalid contract domains");

		const DepthCoverageDrawPacket packet{
			.m_Geometry =
				{
					.m_MeshId = ProceduralCubeMeshID,
					.m_VertexBuffer =
						{
							.m_Buffer = RHIBufferHandle{ 8, 9 },
							.m_Offset = 64,
							.m_Stride = 48,
							.m_SizeInBytes = 480,
						},
					.m_IndexBuffer =
						{
							.m_Buffer = RHIBufferHandle{ 10, 11 },
							.m_Offset = 32,
							.m_SizeInBytes = 72,
							.m_Format = RHIFormat::R32Uint,
						},
				},
			.m_IndexedDraw =
				{
					.m_IndexCount = 18,
					.m_InstanceCount = 2,
					.m_StartIndexLocation = 3,
					.m_BaseVertexLocation = -2,
					.m_StartInstanceLocation = 4,
				},
			.m_DrawParameters =
				{
					.ObjectOffset = 17,
				},
			.m_CurrentModelSource =
				{
					.m_Buffer = RHIBufferHandle{ 2, 3 },
					.m_ElementIndex = 17,
				},
			.m_MaterialAlphaSource =
				{
					.m_Buffer = RHIBufferHandle{ 6, 7 },
					.m_ElementIndex = 31,
				},
		};
		const auto packetDiffersAfter = [&packet](auto mutate) noexcept
		{
			DepthCoverageDrawPacket changed = packet;
			mutate(changed);
			return changed != packet;
		};
		RenderQueue queue{
			.m_DrawItems =
				{
					DrawItem{
						.m_CoverageDrawPacket = packet,
						.m_Bucket = RenderBucket::Opaque,
						.m_VariantBits = opaqueVariant,
					},
				},
			.m_ViewId = RenderViewID::Main,
			.m_CoverageRasterDomain = rasterDomain,
		};
		queue.m_BucketDrawRanges[utils::ToIndex(RenderBucket::Opaque)] = {
			.m_Start = 0,
			.m_Count = 1,
		};
		queue.m_BucketDrawRanges[utils::ToIndex(RenderBucket::AlphaTest)] = {
			.m_Start = 1,
		};
		queue.m_BucketDrawRanges[utils::ToIndex(RenderBucket::Transparent)] = {
			.m_Start = 1,
		};
		// Exercise the production bucket helper for both owners, including a
		// same-layout material pipeline switch and a missing required snapshot.
		for (const auto bucket : { RenderBucket::Opaque, RenderBucket::Transparent })
		{
			for (const bool missingShadow : { false, true })
			{
				RecordingDevice recordingDevice;
				recordingDevice.m_PassConstantsEnabled = true;
				RHIUploadPool recordingPool(recordingDevice);
				RHIUploadWriter recordingWriter;
				auto recordingBatch = recordingPool.BeginBatch();
				recordingBatch->Attach(recordingWriter);
				RenderFrameConstantBindings frameConstants;
				frameConstants.m_SceneSnapshot = TypedConstantSnapshot<SceneGPU>::Upload(
					recordingWriter.GetFrameUploads(), SceneGPU{});
				const auto shadow = TypedConstantSnapshot<DirectionalShadowGPU>::Upload(
					recordingWriter.GetFrameUploads(), DirectionalShadowGPU{});
				if (!missingShadow)
				{
					frameConstants.m_DirectionalShadowSnapshot = shadow;
				}
				const auto atmosphere = TypedConstantSnapshot<AtmosphereGPU>::Upload(
					recordingWriter.GetFrameUploads(), MakeNeutralAtmosphere());
				BindingTestPipelineSystem bindingPipelines;
				const auto bindingLayout = ForwardBindingLayout::Create(
					bindingPipelines, scene_bindings::GetCommonLayoutContract(), false);
				auto recordingQueue = queue;
				recordingQueue.m_DrawItems.resize(2, queue.m_DrawItems.front());
				recordingQueue.m_DrawItems[0].m_VariantBits = 0;
				recordingQueue.m_DrawItems[1].m_VariantBits = 1;
				recordingQueue.m_BucketDrawRanges = {};
				recordingQueue.m_BucketDrawRanges[utils::ToIndex(bucket)] = { 0, 2 };
				RecordingGraphicsCommandContext recording;
				recording.BeginRecordingScope(recordingWriter);
				{
					auto bindings = bindingLayout.BeginRecording(recording);
					const std::array buckets{ bucket };
					forward_shading::DrawBuckets(recording, recordingQueue, buckets, &recordingQueue,
						bindingLayout, frameConstants, atmosphere, bindings, [&bindingLayout](uint64_t variant) noexcept
						{
							return ResolveBindingTestPipeline(bindingLayout, static_cast<uint32_t>(variant) + 1u);
						});
					context.Check(missingShadow ? recording.m_CommandCount == 0 && recordingBatch->HasFailed() :
						recording.m_CommandCount == 2 && recording.m_ConstantBindings.size() == 6 &&
						!recordingBatch->HasFailed(),
						std::format("Production Forward bucket {} rejects missing Shadow or rebinds three inputs across material pipelines (missing={})",
							static_cast<uint32_t>(bucket), missingShadow));
				}
				recording.EndRecordingScope();
				recordingBatch->Detach(recordingWriter);
				recordingBatch->Cancel();
				context.Check(recordingPool.GetStatistics().m_LastBatch.m_UploadCount == 3,
					"Opaque and transparent bucket recording reuse all three producer uploads");
			}
		}
		const auto& prepassPacket = queue.m_DrawItems.front().m_CoverageDrawPacket;
		const auto& forwardPacket = queue.m_DrawItems.front().m_CoverageDrawPacket;
		const DepthCoverageDrawPacket copiedPacket = packet;
		context.Check(
			packet.IsValid() && IsSameDepthCoverageDrawPacket(prepassPacket, forwardPacket) &&
			!IsSameDepthCoverageDrawPacket(packet, copiedPacket) &&
			packetDiffersAfter(
				[](auto& value) { value.m_Geometry.m_MeshId = ProceduralSphereMeshID; }) &&
			packetDiffersAfter([](auto& value)
				{
					value.m_Geometry.m_VertexBuffer.m_Buffer = RHIBufferHandle{ 8, 10 };
				}) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_Geometry.m_VertexBuffer.m_Offset; }) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_Geometry.m_VertexBuffer.m_Stride; }) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_Geometry.m_VertexBuffer.m_SizeInBytes; }) &&
			packetDiffersAfter([](auto& value)
				{
					value.m_Geometry.m_IndexBuffer.m_Buffer = RHIBufferHandle{ 10, 12 };
				}) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_Geometry.m_IndexBuffer.m_Offset; }) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_Geometry.m_IndexBuffer.m_SizeInBytes; }) &&
			packetDiffersAfter([](auto& value)
				{
					value.m_Geometry.m_IndexBuffer.m_Format = RHIFormat::R32Float;
				}) &&
			packetDiffersAfter([](auto& value) { ++value.m_IndexedDraw.m_IndexCount; }) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_IndexedDraw.m_InstanceCount; }) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_IndexedDraw.m_StartIndexLocation; }) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_IndexedDraw.m_BaseVertexLocation; }) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_IndexedDraw.m_StartInstanceLocation; }) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_DrawParameters.ObjectOffset; }) &&
			packetDiffersAfter([](auto& value)
				{
					value.m_CurrentModelSource.m_Buffer = RHIBufferHandle{ 2, 4 };
				}) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_CurrentModelSource.m_ElementIndex; }) &&
			packetDiffersAfter([](auto& value)
				{
					value.m_MaterialAlphaSource.m_Buffer = RHIBufferHandle{ 6, 8 };
				}) &&
			packetDiffersAfter(
				[](auto& value) { ++value.m_MaterialAlphaSource.m_ElementIndex; }) &&
			DescribeDepthCoverageDrawPacket(packet).find("DrawIndexed") != std::string::npos &&
			DescribeDepthCoverageDrawPacket(packet).find("MaterialAlpha") != std::string::npos,
			"RenderQueue owns one complete coverage draw packet shared by both passes");

		DepthCoverageFramePlanBuildInfo framePlanBuildInfo{
			.m_RenderQueue = std::addressof(queue),
			.m_ExpectedViewId = RenderViewID::Main,
			.m_TargetWidth = 1280,
			.m_TargetHeight = 720,
			.m_DepthConvention = DepthConvention::Reversed,
		};
		framePlanBuildInfo.m_PrepassPipelineSignatures[opaqueVariant] = *prepassOpaque;
		framePlanBuildInfo.m_ForwardPipelineSignatures[opaqueVariant] = *opaque;
		const DepthCoverageFramePlan equalPlan = BuildDepthCoverageFramePlan(framePlanBuildInfo);

		DepthCoverageFramePlanBuildInfo mismatchedShaderBuildInfo = framePlanBuildInfo;
		mismatchedShaderBuildInfo.m_ForwardPipelineSignatures[opaqueVariant]
			->m_CoverageVertexShader = ShaderID{ 18 };
		const DepthCoverageFramePlan mismatchedPlan = BuildDepthCoverageFramePlan(mismatchedShaderBuildInfo);

		DepthCoverageFramePlanBuildInfo missingSignatureBuildInfo = framePlanBuildInfo;
		missingSignatureBuildInfo.m_ForwardPipelineSignatures[opaqueVariant].reset();
		const DepthCoverageFramePlan missingSignaturePlan = BuildDepthCoverageFramePlan(missingSignatureBuildInfo);

		RenderQueue invalidPacketQueue = queue;
		invalidPacketQueue.m_DrawItems.front().m_CoverageDrawPacket = {};
		DepthCoverageFramePlanBuildInfo invalidPacketBuildInfo = framePlanBuildInfo;
		invalidPacketBuildInfo.m_RenderQueue = std::addressof(invalidPacketQueue);
		const DepthCoverageFramePlan rejectedPlan = BuildDepthCoverageFramePlan(invalidPacketBuildInfo);

		DepthCoverageFramePlanBuildInfo standardDepthBuildInfo = framePlanBuildInfo;
		standardDepthBuildInfo.m_DepthConvention = DepthConvention::Standard;
		const DepthCoverageFramePlan standardDepthPlan = BuildDepthCoverageFramePlan(standardDepthBuildInfo);

		RenderQueue emptyQueue{
			.m_ViewId = RenderViewID::Main,
			.m_CoverageRasterDomain = rasterDomain,
		};
		DepthCoverageFramePlanBuildInfo emptyQueueBuildInfo = framePlanBuildInfo;
		emptyQueueBuildInfo.m_RenderQueue = std::addressof(emptyQueue);
		const DepthCoverageFramePlan emptyQueuePlan = BuildDepthCoverageFramePlan(emptyQueueBuildInfo);
		context.Check(
			equalPlan.IsValid() && equalPlan.m_Diagnostic.empty() &&
			equalPlan.AddsForwardOpaquePass() && !equalPlan.AddsForwardTransparentPass() &&
			!mismatchedPlan.IsValid() && !mismatchedPlan.AddsForwardOpaquePass() &&
			mismatchedPlan.m_Diagnostic.find("CoverageVertexShader") != std::string::npos &&
			!missingSignaturePlan.IsValid() &&
			missingSignaturePlan.m_Diagnostic.find("pipeline signatures") != std::string::npos &&
			!rejectedPlan.IsValid() && !rejectedPlan.m_Diagnostic.empty() &&
			!standardDepthPlan.IsValid() &&
			standardDepthPlan.m_Diagnostic.find("Reversed-Z") != std::string::npos &&
			emptyQueuePlan.IsValid() &&
			!emptyQueuePlan.AddsForwardOpaquePass() &&
			!emptyQueuePlan.AddsForwardTransparentPass(),
			"Frame-level coverage planning validates the single EQUAL topology, reports the first violation and omits empty Forward buckets");
	}

	// CPU reference rules for the GTAO shaders tested below.
	struct GTAOSurfaceCandidate
	{
		float m_RawDepth = 0.0f;
		float m_ViewZ = 0.0f;
	};

	struct GTAOSurfaceSelection
	{
		uint32_t m_SelectedIndex = 0;
		float m_RawDepth = 0.0f;
		float m_ViewZ = 0.0f;
		bool m_IsValid = false;
	};

	struct GTAONormalAxisNeighborAvailability
	{
		bool m_HasNegativeNeighbor = false;
		bool m_HasPositiveNeighbor = false;
	};

	GTAOSurfaceSelection SelectGTAOHalfResolutionSurface(
		const std::array<GTAOSurfaceCandidate, 4>& candidates,
		DepthConvention convention) noexcept
	{
		GTAOSurfaceSelection selection{};
		for (uint32_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex)
		{
			const auto& candidate = candidates[candidateIndex];
			if (screen_space::IsDepthBackground(candidate.m_RawDepth, convention) ||
				!std::isfinite(candidate.m_ViewZ) || candidate.m_ViewZ <= 0.0f)
			{
				continue;
			}

			if (!selection.m_IsValid || screen_space::IsDepthNearer(candidate.m_RawDepth, selection.m_RawDepth, convention))
			{
				selection = {
					.m_SelectedIndex = candidateIndex,
					.m_RawDepth = candidate.m_RawDepth,
					.m_ViewZ = candidate.m_ViewZ,
					.m_IsValid = true,
				};
			}
		}
		return selection;
	}

	float GTAOInterleavedGradientNoise(uint32_t pixelX, uint32_t pixelY) noexcept
	{
		const auto fract = [](float value) noexcept { return value - std::floor(value); };
		return fract(52.9829189f * fract(0.06711056f * static_cast<float>(pixelX) + 0.00583715f * static_cast<float>(pixelY)));
	}

	[[nodiscard]] constexpr float ResolveGTAODiffuseIBLVisibility(float materialAO, float gtao) noexcept
	{
		return std::clamp(materialAO * gtao, 0.0f, 1.0f);
	}

	[[nodiscard]] constexpr float ResolveGTAOSpecularIBLVisibility(float materialAO) noexcept
	{
		return std::clamp(materialAO, 0.0f, 1.0f);
	}

	[[nodiscard]] constexpr GTAONormalAxisNeighborAvailability
		GetGTAONormalAxisNeighborAvailability(uint32_t centerCoordinate, uint32_t extent) noexcept
	{
		return {
			.m_HasNegativeNeighbor = centerCoordinate > 0 && centerCoordinate < extent,
			.m_HasPositiveNeighbor = centerCoordinate < extent && centerCoordinate + 1 < extent,
		};
	}

	void RunScreenSpaceAndDepthContractTests(SelfTestContext& context) noexcept
	{
		RunProjectionConventionTests(context);
		RunPositionReconstructionTests(context);
		RunScreenCoordinateTests(context);
		RunRenderViewConventionTests(context);
		RunSampleableDepthFormatTests(context);
		RunDepthCoverageContractTests(context);
		RunForwardPlusReadbackFenceTests(context);
	RunForwardPlusContractTests(context);

		context.Check(MakeGTAOHalfResolutionExtent(1920, 1080) == GTAOExtent{ 960, 540 } &&
			MakeGTAOHalfResolutionExtent(1919, 1079) == GTAOExtent{ 960, 540 } &&
			!MakeGTAOHalfResolutionExtent(0, 0).IsValid(),
			"GTAO half-resolution extents use deterministic per-axis ceiling division");

		const std::array<GTAOSurfaceCandidate, 4> reversedCandidates =
		{
			GTAOSurfaceCandidate{ .m_RawDepth = 0.0f, .m_ViewZ = 0.0f },
			GTAOSurfaceCandidate{ .m_RawDepth = 0.4f, .m_ViewZ = 3.0f },
			GTAOSurfaceCandidate{ .m_RawDepth = 0.7f, .m_ViewZ = 2.0f },
			GTAOSurfaceCandidate{ .m_RawDepth = 0.7f, .m_ViewZ = 2.0f },
		};
		const GTAOSurfaceSelection reversedSelection = SelectGTAOHalfResolutionSurface(reversedCandidates, DepthConvention::Reversed);
		const std::array<GTAOSurfaceCandidate, 4> standardTieCandidates =
		{
			GTAOSurfaceCandidate{ .m_RawDepth = 0.25f, .m_ViewZ = 2.0f },
			GTAOSurfaceCandidate{ .m_RawDepth = 0.25f, .m_ViewZ = 2.0f },
			GTAOSurfaceCandidate{ .m_RawDepth = 0.5f, .m_ViewZ = 4.0f },
			GTAOSurfaceCandidate{ .m_RawDepth = 1.0f, .m_ViewZ = 0.0f },
		};
		const GTAOSurfaceSelection standardSelection = SelectGTAOHalfResolutionSurface(standardTieCandidates, DepthConvention::Standard);
		const std::array<GTAOSurfaceCandidate, 4> backgroundCandidates{};
		const GTAOSurfaceSelection backgroundSelection = SelectGTAOHalfResolutionSurface(backgroundCandidates, DepthConvention::Reversed);
		context.Check(reversedSelection.m_IsValid &&
			reversedSelection.m_SelectedIndex == 2 && reversedSelection.m_RawDepth == 0.7f &&
			standardSelection.m_IsValid && standardSelection.m_SelectedIndex == 0 &&
			!backgroundSelection.m_IsValid && backgroundSelection.m_RawDepth == 0.0f &&
			backgroundSelection.m_ViewZ == 0.0f,
			"GTAO surface selection rejects background, chooses nearest depth, and preserves TL-to-BR ties");

		const float noise = GTAOInterleavedGradientNoise(37, 19);
		context.Check(noise >= 0.0f && noise < 1.0f &&
			noise == GTAOInterleavedGradientNoise(37, 19),
			"GTAO interleaved-gradient noise is finite, normalized, and deterministic");

		const auto leftBoundaryNeighbors = GetGTAONormalAxisNeighborAvailability(0, 8);
		const auto interiorNeighbors = GetGTAONormalAxisNeighborAvailability(3, 8);
		const auto rightBoundaryNeighbors = GetGTAONormalAxisNeighborAvailability(7, 8);
		const auto singlePixelNeighbors = GetGTAONormalAxisNeighborAvailability(0, 1);
		context.Check(!leftBoundaryNeighbors.m_HasNegativeNeighbor &&
			leftBoundaryNeighbors.m_HasPositiveNeighbor &&
			interiorNeighbors.m_HasNegativeNeighbor &&
			interiorNeighbors.m_HasPositiveNeighbor &&
			rightBoundaryNeighbors.m_HasNegativeNeighbor &&
			!rightBoundaryNeighbors.m_HasPositiveNeighbor &&
			!singlePixelNeighbors.m_HasNegativeNeighbor &&
			!singlePixelNeighbors.m_HasPositiveNeighbor,
			"GTAO normal reconstruction never substitutes the center pixel for a missing edge neighbor");

		const RHITextureSupportResult supportedRequirement{ .m_Supported = true };
		const RHITextureSupportResult unsupportedTypedStore{
			.m_Reason = RHITextureSupportReason::TypedUnorderedAccessStoreUnsupported,
			.m_Supported = false,
		};
		const RHITextureSupportResult unsupportedShaderResource{
			.m_Reason = RHITextureSupportReason::ShaderResourceUnsupported,
			.m_Supported = false,
		};
		const GTAOSurfaceFormatSupport supportedFormat{
			.m_ShaderResource = supportedRequirement,
			.m_TypedUavStore = supportedRequirement,
		};
		const GTAOSurfaceFormatSupport unsupportedR8Store{
			.m_ShaderResource = supportedRequirement,
			.m_TypedUavStore = unsupportedTypedStore,
		};
		const GTAOSurfaceFormatSupport unsupportedR8Srv{
			.m_ShaderResource = unsupportedShaderResource,
			.m_TypedUavStore = supportedRequirement,
		};
		const GTAOFinalAOFormatResolution preferredFinalAO = ResolveGTAOFinalAOFormat(supportedFormat, supportedFormat);
		const GTAOFinalAOFormatResolution storeFallbackFinalAO = ResolveGTAOFinalAOFormat(unsupportedR8Store, supportedFormat);
		const GTAOFinalAOFormatResolution srvFallbackFinalAO = ResolveGTAOFinalAOFormat(unsupportedR8Srv, supportedFormat);
		const GTAOFinalAOFormatResolution unavailableFinalAO = ResolveGTAOFinalAOFormat(unsupportedR8Store, unsupportedR8Srv);
		context.Check(preferredFinalAO.m_Format == RHIFormat::R8Unorm &&
			!preferredFinalAO.UsesFallback() &&
			storeFallbackFinalAO.m_Format == RHIFormat::R16Float &&
			storeFallbackFinalAO.UsesFallback() &&
			storeFallbackFinalAO.m_PreferredR8Unorm.m_TypedUavStore.m_Reason == RHITextureSupportReason::TypedUnorderedAccessStoreUnsupported &&
			srvFallbackFinalAO.m_Format == RHIFormat::R16Float &&
			srvFallbackFinalAO.m_PreferredR8Unorm.m_ShaderResource.m_Reason == RHITextureSupportReason::ShaderResourceUnsupported &&
			!unavailableFinalAO.IsAvailable(),
			"GTAO FinalAO requires both SRV and typed UAV store support and preserves failures");

		context.Check(ResolveGTAODiffuseIBLVisibility(0.5f, 0.25f) == 0.125f &&
			ResolveGTAOSpecularIBLVisibility(0.5f) == 0.5f &&
			ResolveGTAODiffuseIBLVisibility(0.5f, 1.0f) == 0.5f,
			"GTAO modulates material-occluded diffuse IBL without changing specular IBL visibility");

		context.Check(
			ResolveGTAOFrameStatus(false, false, false) == GTAOFrameStatus::Disabled &&
			ResolveGTAOFrameStatus(true, false, false) == GTAOFrameStatus::CoreCapabilityUnavailable &&
			ResolveGTAOFrameStatus(true, true, false) == GTAOFrameStatus::NoOpaqueDraws &&
			ResolveGTAOFrameStatus(true, true, true) == GTAOFrameStatus::Active,
			"GTAO frame status preserves deterministic disabled, unsupported, idle, and active causes");

		ViewRenderProfile gtaoProfile{};
		context.Check(gtaoProfile.m_Lighting.m_GTAO.m_Enabled,
			"GTAO is enabled by the active-view authoring default");
		gtaoProfile.m_Lighting.m_GTAO.m_Radius = -1.0f;
		gtaoProfile.m_Lighting.m_GTAO.m_FalloffStart = 99.0f;
		gtaoProfile.m_Lighting.m_GTAO.m_FalloffEnd = -99.0f;
		gtaoProfile.m_Lighting.m_GTAO.m_Power = 99.0f;
		gtaoProfile.m_Lighting.m_GTAO.m_DirectionCount = 0;
		gtaoProfile.m_Lighting.m_GTAO.m_StepCount = 99;
		gtaoProfile.m_Lighting.m_GTAO.m_DenoiseRadius = 99;
		const Camera gtaoCamera(Camera::CreateInfo{});
		const GTAOSettings resolvedGTAO = ResolveViewRenderSettings(gtaoProfile, gtaoCamera).m_Lighting.m_GTAO;
		context.Check(resolvedGTAO.m_Radius == 0.01f &&
			resolvedGTAO.m_FalloffStart == resolvedGTAO.m_Radius &&
			resolvedGTAO.m_FalloffEnd == resolvedGTAO.m_Radius &&
			resolvedGTAO.m_Power == 8.0f &&
			resolvedGTAO.m_DirectionCount == 1 &&
			resolvedGTAO.m_StepCount == GTAOMaxStepCount &&
			resolvedGTAO.m_DenoiseRadius == GTAOMaxDenoiseRadius,
			"GTAO authoring inputs resolve to bounded deterministic spatial settings");
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingDepthSelfTests(SelfTestContext& context) noexcept
	{
		RunScreenSpaceAndDepthContractTests(context);
	}
}
