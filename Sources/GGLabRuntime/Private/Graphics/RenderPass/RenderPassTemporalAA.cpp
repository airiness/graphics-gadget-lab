#include "Graphics/RenderPass/RenderPassTemporalAA.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessColorState.h"
#include "Graphics/Pipeline/TemporalAACapability.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/TemporalAAGraphResources.h"
#include "Graphics/RenderPass/TemporalGeometryGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderParameters.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "Graphics/SamplerRegistry.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <cstdint>
#include <span>

namespace gglab
{
	namespace
	{
		inline constexpr uint32_t TemporalAAThreadGroupSize = 8;
		inline constexpr uint32_t TemporalAAHistoryValidBit = 0x80000000u;
		inline constexpr uint32_t TemporalAAHistoryColorPreviewBit = 0x40000000u;
		inline constexpr uint32_t TemporalAAHistoryAgePreviewBit = 0x20000000u;
		inline constexpr uint32_t TemporalAAHistoryCatmullRomBit = 0x10000000u;
		inline constexpr uint32_t TemporalAACurrentGaussianBit = 0x08000000u;
		inline constexpr uint32_t TemporalAAClosestDepthMotionBit = 0x04000000u;
		inline constexpr uint32_t TemporalAADisplayDepthBit = 0x02000000u;
		inline constexpr uint32_t TemporalAAViewFlagMask =
			TemporalAAHistoryValidBit | TemporalAAHistoryColorPreviewBit |
			TemporalAAHistoryAgePreviewBit | TemporalAAHistoryCatmullRomBit |
			TemporalAACurrentGaussianBit | TemporalAAClosestDepthMotionBit |
			TemporalAADisplayDepthBit;

		struct TemporalAAPassParameters
		{
			uint32_t m_CurrentColorIndex = 0;
			uint32_t m_MotionIndex = 0;
			uint32_t m_CurrentDepthIndex = 0;
			uint32_t m_PreviousColorIndex = 0;
			uint32_t m_PreviousDepthIndex = 0;
			uint32_t m_ResolvedColorUavIndex = 0;
			uint32_t m_NextHistoryColorUavIndex = 0;
			uint32_t m_ReprojectionDiagnosticsUavIndex = 0;
			uint32_t m_LinearClampSamplerIndex = 0;
			uint32_t m_PointClampSamplerIndex = 0;
			uint32_t m_ViewIndexAndHistoryValid = 0;
			uint32_t m_PackedDepthThresholds = 0;
			uint32_t m_PackedMaxHistoryFeedbackAndClampExpansion = 0;
			float m_VelocityWeightScale = 0.0f;
			float m_LuminanceWeightScale = 0.0f;
			uint32_t m_DisplayDepthUavIndex = 0;
		};
		static_assert(IsPassRootConstantStruct<TemporalAAPassParameters>);
		static_assert(sizeof(TemporalAAPassParameters) == 64);

		struct TemporalAADepthHistoryPassParameters
		{
			uint32_t m_CurrentDepthIndex = 0;
			uint32_t m_NextHistoryDepthUavIndex = 0;
			uint32_t m_Padding[2]{};
		};
		static_assert(IsPassRootConstantStruct<TemporalAADepthHistoryPassParameters>);

		struct TemporalAAPassData
		{
			RGTextureViewId m_CurrentColorSrv{};
			RGTextureViewId m_MotionSrv{};
			RGTextureViewId m_CurrentDepthSrv{};
			RGTextureViewId m_PreviousColorSrv{};
			RGTextureViewId m_PreviousDepthSrv{};
			RGTextureViewId m_ResolvedColorUav{};
			RGTextureViewId m_NextHistoryColorUav{};
			RGTextureViewId m_NextHistoryDepthUav{};
			RGTextureViewId m_ReprojectionDiagnosticsUav{};
			RGTextureViewId m_DisplayDepthUav{};
			TemporalAAPassParameters m_Parameters{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			// Render extent of the depth that the next depth history copies.
			uint32_t m_DepthWidth = 0;
			uint32_t m_DepthHeight = 0;
		};

		struct TemporalAADisplayDepthPassParameters
		{
			uint32_t m_SourceDepthIndex = 0;
			uint32_t m_Padding[3]{};
		};
		static_assert(IsPassRootConstantStruct<TemporalAADisplayDepthPassParameters>);

		struct TemporalAADisplayDepthPassData
		{
			RGTextureViewId m_SourceSrv{};
			RGTextureViewId m_Dsv{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};

		struct TemporalAAResolvedColorInitializePassData
		{
			RGTextureViewId m_ResolvedColorRtv{};
		};
	}

	void RenderPassTemporalAA::Prepare(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}

		auto* shaderManager = services.m_ShaderPrograms;
		GGLAB_ASSERT_NOT_NULL(shaderManager);
		m_IsInitialized = true;
		m_PipelineRecipe.m_CSId = shaderManager->LoadProgram(
			shader_programs::TemporalAAReprojectionCompute);
		m_PipelineRecipe.m_BindingLayout = services.m_BindingLayout->GetCommonBindingLayout();
		m_DepthHistoryPipelineRecipe.m_CSId = shaderManager->LoadProgram(
			shader_programs::TemporalAADepthHistoryCompute);
		m_DepthHistoryPipelineRecipe.m_BindingLayout = m_PipelineRecipe.m_BindingLayout;

		// Converts the resolved display depth into the D32 post-temporal display depth.
		m_DisplayDepthPipelineKey.m_BindingLayout = m_PipelineRecipe.m_BindingLayout;
		m_DisplayDepthPipelineKey.m_InputLayoutId = InputLayoutID::None;
		m_DisplayDepthPipelineKey.m_VSId =
			shaderManager->LoadProgram(shader_programs::TemporalAADisplayDepthVertex);
		m_DisplayDepthPipelineKey.m_PSId =
			shaderManager->LoadProgram(shader_programs::TemporalAADisplayDepthPixel);
		m_DisplayDepthPipelineKey.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		m_DisplayDepthPipelineKey.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		m_DisplayDepthPipelineKey.m_Formats.m_RenderTargetCount = 0;
		m_DisplayDepthPipelineKey.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
		m_DisplayDepthPipelineKey.m_Formats.m_SampleCount = 1;
		m_DisplayDepthPipelineKey.m_Formats.m_SampleQuality = 0;
		m_DisplayDepthPipelineKey.m_RasterizerPreset = RasterizerPreset::Default;
		m_DisplayDepthPipelineKey.m_BlendPreset = BlendPreset::Default;
		m_DisplayDepthPipelineKey.m_DepthPreset = DepthPreset::AlwaysZWrite;

		m_IsAvailable = m_PipelineRecipe.m_CSId.IsValid() &&
			m_DepthHistoryPipelineRecipe.m_CSId.IsValid() &&
			m_DisplayDepthPipelineKey.m_VSId.IsValid() &&
			m_DisplayDepthPipelineKey.m_PSId.IsValid() &&
			m_PipelineRecipe.m_BindingLayout.IsValid();
	}

	void RenderPassTemporalAA::AddPass(RenderGraph& rg, const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized,
			"Temporal AA must be prepared before graph construction.");
		const bool consumerActive =
			context.GetTemporalFramePlan().IsConsumerActive(TemporalConsumer::TemporalAA);
		GGLAB_ASSERT_MSG(consumerActive,
			"Temporal AA resolve requires an active Temporal AA consumer in the frame plan.");
		GGLAB_ASSERT_MSG(m_IsAvailable,
			"Temporal resolve requires an available compute artifact and binding layout.");
		if (!m_IsAvailable || !consumerActive)
		{
			return;
		}

		auto* transaction = context.m_TemporalFrameTransaction;
		GGLAB_ASSERT_NOT_NULL(transaction);
		if (!transaction)
		{
			return;
		}
		GGLAB_ASSERT_MSG(IsTemporalColorCompatible(transaction->GetColorAbi(),
			PostProcessColorState::SceneLinearRec709, transaction->GetScenePreExposure()),
			"Temporal AA cannot read scene color outside its active color ABI.");
		if (!IsTemporalColorCompatible(transaction->GetColorAbi(),
			PostProcessColorState::SceneLinearRec709, transaction->GetScenePreExposure()))
		{
			return;
		}
		const uint32_t viewIndex =
			static_cast<uint32_t>(utils::ToIndex(context.GetDisplayViewId()));
		GGLAB_ASSERT_MSG((viewIndex & TemporalAAViewFlagMask) == 0,
			"Temporal AA view indices must fit below the packed view flag bits.");
		const RenderViewID displayViewId = context.GetDisplayViewId();
		const TemporalAASettings temporalAASettings =
			context.GetDisplayViewRenderSettings().m_TemporalAA;
		const bool previousHistoryCompatible =
			transaction->HasCompatiblePreviousHistory();
		const auto* resourceRegistry = services.m_Resources;
		const auto* samplerRegistry = services.m_Samplers;
		GGLAB_ASSERT_NOT_NULL(resourceRegistry);
		GGLAB_ASSERT_NOT_NULL(samplerRegistry);
		if (!resourceRegistry || !samplerRegistry)
		{
			return;
		}
		const PostProcessDebugSelection previewSelection =
			resourceRegistry->GetPostProcessPreviewSelection(PostProcessPreviewChannel::TemporalAA);
		const bool historyColorPreviewRequested =
			resourceRegistry->IsPostProcessPreviewRequested(PostProcessPreviewChannel::TemporalAA) &&
			UsesTemporalAAHistoryColorPreviewPayload(previewSelection.m_Tap);
		const bool historyAgePreviewRequested =
			resourceRegistry->IsPostProcessPreviewRequested(PostProcessPreviewChannel::TemporalAA) &&
			UsesTemporalAAHistoryAgePreviewPayload(previewSelection.m_Tap);

		rg.AddPass<TemporalAAResolvedColorInitializePassData>(
			"PostProcess.TemporalAA.InitializeResolvedSceneColor",
			[displayViewId](RenderGraph::RGBuilder& builder,
				TemporalAAResolvedColorInitializePassData& data)
			{
				// The compute resolve fully overwrites this texture, so its logical write does
				// not depend on this pass. Keep the physical D3D12 initialization operation
				// alive explicitly for CREATE_NOT_ZEROED RTV/UAV allocations.
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				const auto& targets = blackboard.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);
				const RHITextureDesc& currentColorDesc =
					builder.GetTextureDesc(targets.m_SceneColor);
				// The resolve reads render-domain inputs and writes display-domain outputs.
				GGLAB_ASSERT_MSG(currentColorDesc.m_Extent.m_Width == targets.m_RenderWidth &&
					currentColorDesc.m_Extent.m_Height == targets.m_RenderHeight,
					"Temporal AA reads scene color at the render extent.");
				RHITextureDesc outputDesc{};
				outputDesc.m_Format = TemporalAAResolvedColorFormat;
				outputDesc.m_Extent = { targets.m_DisplayWidth, targets.m_DisplayHeight, 1u };

				auto& resources =
					blackboard.GetOrCreate<RGTemporalAAResources>(TemporalAAResourcesName);
				resources.m_ResolvedSceneColor =
					builder.CreateTexture("TAA.ResolvedSceneColor", outputDesc);
				builder.WriteInPlace(
					resources.m_ResolvedSceneColor, RGTextureAccess::RenderTarget);
				data.m_ResolvedColorRtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(
						resources.m_ResolvedSceneColor);
			},
			[](RGExecuteContext& executeContext,
				TemporalAAResolvedColorInitializePassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const RHITextureViewHandle resolvedColorRtv =
					executeContext.GetViewHandle(data.m_ResolvedColorRtv);
				GGLAB_ASSERT_MSG(resolvedColorRtv.IsValid(),
					"Temporal AA resolved color must have a live initialization RTV.");
				const RHIRenderingAttachment colorAttachment{
					.m_View = resolvedColorRtv,
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				commandContext->BeginRendering({ .m_ColorAttachments =
					std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
				commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
			});

		rg.AddPass<TemporalAAPassData>(
			GetRenderGraphPassName(), RGPassEncoderType::Compute,
			[transaction, displayViewId, viewIndex, temporalAASettings,
			previousHistoryCompatible, historyColorPreviewRequested,
			historyAgePreviewRequested,
			linearClampSamplerIndex = samplerRegistry->GetSamplerIndex(SamplerPreset::LinearClamp),
			pointClampSamplerIndex = samplerRegistry->GetSamplerIndex(SamplerPreset::PointClamp)](
				RenderGraph::RGBuilder& builder, TemporalAAPassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				auto& targets = blackboard.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);
				const auto& sceneDepth =
					blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto& temporalGeometry = blackboard.Get<RGTemporalGeometryResources>(
					TemporalGeometryResourcesName);
				GGLAB_ASSERT_MSG(temporalGeometry.IsValid(),
					"Temporal AA requires display motion vectors.");

				auto& resources =
					blackboard.GetOrCreate<RGTemporalAAResources>(TemporalAAResourcesName);
				const bool imported =
					transaction->ImportHistoryResources(builder, resources.m_History);
				GGLAB_ASSERT_MSG(imported && resources.m_History.IsValid(),
					"Temporal AA failed to import its frame-owned history set.");
				if (!imported || !resources.m_History.IsValid())
				{
					return;
				}

				const RGTextureId currentColor = builder.Read(
					targets.m_SceneColor, RGTextureAccess::Sample, RHIStage::ComputeShader);
				const RGTextureId motion = builder.Read(temporalGeometry.m_MotionVectors,
					RGTextureAccess::Sample, RHIStage::ComputeShader);
				const RGTextureId currentDepth = builder.Read(sceneDepth.m_Texture,
					RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_CurrentColorSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(currentColor);
				data.m_MotionSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					motion, temporalGeometry.m_MotionSrvDesc);
				data.m_CurrentDepthSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					currentDepth, sceneDepth.m_SrvDesc);
				const RHITextureDesc& currentDepthDesc = builder.GetTextureDesc(currentDepth);
				data.m_DepthWidth = currentDepthDesc.m_Extent.m_Width;
				data.m_DepthHeight = currentDepthDesc.m_Extent.m_Height;

				GGLAB_ASSERT_MSG(!previousHistoryCompatible ||
					resources.m_History.m_PreviousValid,
					"Compatible temporal history requires defined imported history resources.");
				if (previousHistoryCompatible)
				{
					resources.m_History.m_PreviousColor = builder.Read(
						resources.m_History.m_PreviousColor, RGTextureAccess::Sample,
						RHIStage::ComputeShader);
					resources.m_History.m_PreviousDepth = builder.Read(
						resources.m_History.m_PreviousDepth, RGTextureAccess::Sample,
						RHIStage::ComputeShader);
					data.m_PreviousColorSrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(
							resources.m_History.m_PreviousColor);
					data.m_PreviousDepthSrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(
							resources.m_History.m_PreviousDepth);
				}
				else
				{
					// Bind defined current-frame fallbacks. The shader does not sample them while
					// previous-history-valid is false, and the undefined previous imports stay unread.
					data.m_PreviousColorSrv = data.m_CurrentColorSrv;
					data.m_PreviousDepthSrv = data.m_CurrentDepthSrv;
				}

				resources.m_Width = targets.m_DisplayWidth;
				resources.m_Height = targets.m_DisplayHeight;
				data.m_Width = resources.m_Width;
				data.m_Height = resources.m_Height;
				GGLAB_ASSERT_MSG(resources.m_ResolvedSceneColor.IsValid(),
					"Temporal AA resolved color must be initialized before compute resolve.");
				if (!resources.m_ResolvedSceneColor.IsValid())
				{
					return;
				}
				RHITextureDesc outputDesc{};
				outputDesc.m_Format = TemporalAAResolvedColorFormat;
				outputDesc.m_Extent = { targets.m_DisplayWidth, targets.m_DisplayHeight, 1u };
				resources.m_ReprojectionDiagnostics =
					builder.CreateTexture("TAA.ReprojectionDiagnostics", outputDesc);

				builder.WriteInPlace(resources.m_ResolvedSceneColor,
					RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				builder.WriteInPlace(resources.m_ReprojectionDiagnostics,
					RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				builder.WriteInPlace(resources.m_History.m_NextColor,
					RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				builder.WriteInPlace(resources.m_History.m_NextDepth,
					RGTextureAccess::StorageWrite, RHIStage::ComputeShader);

				data.m_ResolvedColorUav =
					builder.CreateView<RHITextureViewType::UnorderedAccess>(
						resources.m_ResolvedSceneColor);
				data.m_ReprojectionDiagnosticsUav =
					builder.CreateView<RHITextureViewType::UnorderedAccess>(
						resources.m_ReprojectionDiagnostics);
				data.m_NextHistoryColorUav =
					builder.CreateView<RHITextureViewType::UnorderedAccess>(
						resources.m_History.m_NextColor);
				data.m_NextHistoryDepthUav =
					builder.CreateView<RHITextureViewType::UnorderedAccess>(
						resources.m_History.m_NextDepth);

				// Below native resolution post-temporal composition needs a display-extent
				// depth; at native it tests against the scene depth itself.
				const bool resolveDisplayDepth = targets.m_RenderWidth != targets.m_DisplayWidth ||
					targets.m_RenderHeight != targets.m_DisplayHeight;
				if (resolveDisplayDepth)
				{
					RHITextureDesc displayDepthDesc{};
					displayDepthDesc.m_Format = RHIFormat::R32Float;
					displayDepthDesc.m_Extent = { targets.m_DisplayWidth, targets.m_DisplayHeight, 1u };
					resources.m_DisplayDepthSource =
						builder.CreateTexture("TAA.DisplayDepthSource", displayDepthDesc);
					builder.WriteInPlace(resources.m_DisplayDepthSource,
						RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_DisplayDepthUav =
						builder.CreateView<RHITextureViewType::UnorderedAccess>(
							resources.m_DisplayDepthSource);
				}

				data.m_Parameters = {
					.m_LinearClampSamplerIndex = linearClampSamplerIndex,
					.m_PointClampSamplerIndex = pointClampSamplerIndex,
					.m_ViewIndexAndHistoryValid = viewIndex |
						(previousHistoryCompatible ? TemporalAAHistoryValidBit : 0u) |
						(historyColorPreviewRequested
							? TemporalAAHistoryColorPreviewBit
							: 0u) |
						(historyAgePreviewRequested
							? TemporalAAHistoryAgePreviewBit
							: 0u) |
						(temporalAASettings.m_HistoryFilter ==
							TemporalAAHistoryFilter::CatmullRomClamped
							? TemporalAAHistoryCatmullRomBit
							: 0u) |
						(temporalAASettings.m_CurrentFilter == TemporalAACurrentFilter::Gaussian
							? TemporalAACurrentGaussianBit
							: 0u) |
						(temporalAASettings.m_MotionSelection ==
							TemporalAAMotionSelection::ClosestDepth
							? TemporalAAClosestDepthMotionBit
							: 0u) |
						(resolveDisplayDepth ? TemporalAADisplayDepthBit : 0u),
					.m_PackedDepthThresholds = PackTemporalAAUnitRangePair(
						temporalAASettings.m_DepthAbsoluteThreshold,
						temporalAASettings.m_DepthRelativeThreshold),
					.m_PackedMaxHistoryFeedbackAndClampExpansion =
						PackTemporalAAMaxHistoryFeedbackAndClampExpansion(
						temporalAASettings.m_MaxHistoryFeedback,
						temporalAASettings.m_NeighborhoodClampExpansion),
					.m_VelocityWeightScale = temporalAASettings.m_VelocityWeightScale,
					.m_LuminanceWeightScale = temporalAASettings.m_LuminanceWeightScale,
				};
				// Still update history, but diagnostic MRTs describe this frame's raw
				// radiance and coverage, not the temporally reconstructed image. Material
				// diagnostics render at native resolution; a frame that discovers them at a
				// smaller render extent shows the resolved image instead.
				if (!targets.m_MaterialDiagnosticColor.IsValid() || resolveDisplayDepth)
				{
					targets.m_DisplayColor = resources.m_ResolvedSceneColor;
				}
				const bool exported =
					transaction->ExportHistoryResources(builder, resources.m_History);
				GGLAB_ASSERT_MSG(exported,
					"Temporal AA must fully write and export its next history pair.");
			},
			[this, services, &context](RGExecuteContext& executeContext,
				TemporalAAPassData& data)
			{
				auto* commandContext = executeContext.GetDirectComputeCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				auto parameters = data.m_Parameters;
				const auto currentColor =
					executeContext.GetViewDescriptor(data.m_CurrentColorSrv);
				const auto motion = executeContext.GetViewDescriptor(data.m_MotionSrv);
				const auto currentDepth =
					executeContext.GetViewDescriptor(data.m_CurrentDepthSrv);
				const auto previousColor =
					executeContext.GetViewDescriptor(data.m_PreviousColorSrv);
				const auto previousDepth =
					executeContext.GetViewDescriptor(data.m_PreviousDepthSrv);
				const auto resolvedColor =
					executeContext.GetViewDescriptor(data.m_ResolvedColorUav);
				const auto nextHistoryColor =
					executeContext.GetViewDescriptor(data.m_NextHistoryColorUav);
				const auto nextHistoryDepth =
					executeContext.GetViewDescriptor(data.m_NextHistoryDepthUav);
				const auto diagnostics =
					executeContext.GetViewDescriptor(data.m_ReprojectionDiagnosticsUav);
				GGLAB_ASSERT_MSG(currentColor.IsValid() && motion.IsValid() &&
					currentDepth.IsValid() && previousColor.IsValid() &&
					previousDepth.IsValid() && resolvedColor.IsValid() &&
					nextHistoryColor.IsValid() && nextHistoryDepth.IsValid() &&
					diagnostics.IsValid(),
					"Temporal AA views must be shader visible before dispatch.");

				parameters.m_CurrentColorIndex = currentColor.m_Index;
				parameters.m_MotionIndex = motion.m_Index;
				parameters.m_CurrentDepthIndex = currentDepth.m_Index;
				parameters.m_PreviousColorIndex = previousColor.m_Index;
				parameters.m_PreviousDepthIndex = previousDepth.m_Index;
				parameters.m_ResolvedColorUavIndex = resolvedColor.m_Index;
				parameters.m_NextHistoryColorUavIndex = nextHistoryColor.m_Index;
				parameters.m_ReprojectionDiagnosticsUavIndex = diagnostics.m_Index;
				if (data.m_DisplayDepthUav.IsValid())
				{
					const auto displayDepth =
						executeContext.GetViewDescriptor(data.m_DisplayDepthUav);
					GGLAB_ASSERT_MSG(displayDepth.IsValid(),
						"The resolved display depth must be shader visible before dispatch.");
					parameters.m_DisplayDepthUavIndex = displayDepth.m_Index;
				}

				// The next depth history copies the render-domain scene depth; the resolve
				// neither reads nor writes it, so the dispatches need no barrier between them.
				const TemporalAADepthHistoryPassParameters depthHistoryParameters{
					.m_CurrentDepthIndex = currentDepth.m_Index,
					.m_NextHistoryDepthUavIndex = nextHistoryDepth.m_Index,
				};
				commandContext->SetPipeline(GetOrCreateDepthHistoryPipeline(services));
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					services.m_FrameBuffers->GetSceneConstantBuffer()->GetBufferHandle(),
					context.m_RenderScene.m_SceneConstantBufferOffset);
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					services.m_FrameBuffers->GetViewStructuredBuffer()->GetBufferHandle());
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					depthHistoryParameters);
				commandContext->Dispatch(
					(data.m_DepthWidth + TemporalAAThreadGroupSize - 1) /
						TemporalAAThreadGroupSize,
					(data.m_DepthHeight + TemporalAAThreadGroupSize - 1) /
						TemporalAAThreadGroupSize,
					1);

				commandContext->SetPipeline(GetOrCreatePipeline(services));
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
				commandContext->Dispatch(
					(data.m_Width + TemporalAAThreadGroupSize - 1) /
						TemporalAAThreadGroupSize,
					(data.m_Height + TemporalAAThreadGroupSize - 1) /
						TemporalAAThreadGroupSize,
					1);
			});

		AddDisplayDepthPass(rg, services, displayViewId);
	}

	void RenderPassTemporalAA::AddDisplayDepthPass(RenderGraph& rg,
		const RenderServices& services, RenderViewID displayViewId) noexcept
	{
		auto& blackboard = rg.GetBlackboard();
		const auto* resources = blackboard.TryGet<RGTemporalAAResources>(TemporalAAResourcesName);
		if (!resources || !resources->m_DisplayDepthSource.IsValid())
		{
			return;
		}
		rg.AddPass<TemporalAADisplayDepthPassData>(
			"PostProcess.TemporalAA.DisplayDepth",
			[displayViewId](RenderGraph::RGBuilder& builder, TemporalAADisplayDepthPassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				const auto& resources =
					blackboard.Get<RGTemporalAAResources>(TemporalAAResourcesName);
				const auto& targets = blackboard.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);
				auto& displayDepth =
					blackboard.Get<RGDisplayDepthResources>(DisplayDepthResourcesName);
				const RHITextureDesc& depthDesc = builder.GetTextureDesc(displayDepth.m_Texture);
				GGLAB_ASSERT_MSG(depthDesc.m_Extent.m_Width == targets.m_DisplayWidth &&
					depthDesc.m_Extent.m_Height == targets.m_DisplayHeight,
					"The resolved display depth fills the display-extent depth target.");

				const RGTextureId source = builder.Read(resources.m_DisplayDepthSource,
					RGTextureAccess::Sample, RHIStage::PixelShader);
				data.m_SourceSrv = builder.CreateView<RHITextureViewType::ShaderResource>(source);
				builder.WriteInPlace(displayDepth.m_Texture, RGTextureAccess::DepthStencilWrite);
				data.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(
					displayDepth.m_Texture, displayDepth.m_DsvDesc);
				data.m_Width = targets.m_DisplayWidth;
				data.m_Height = targets.m_DisplayHeight;
			},
			[this, services](RGExecuteContext& executeContext, TemporalAADisplayDepthPassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const auto source = executeContext.GetViewDescriptor(data.m_SourceSrv);
				const RHITextureViewHandle dsv = executeContext.GetViewHandle(data.m_Dsv);
				GGLAB_ASSERT_MSG(source.IsValid() && dsv.IsValid(),
					"The display depth conversion needs its source and depth views.");
				commandContext->BeginRendering({
					.m_DepthAttachment = RHIRenderingAttachment{
						.m_View = dsv,
						.m_LoadOp = RHIContentLoadOp::DontCare,
					},
				});
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
					static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
					static_cast<int32_t>(data.m_Height) });
				commandContext->SetPipeline(GetOrCreateDisplayDepthPipeline(services));
				const TemporalAADisplayDepthPassParameters parameters{
					.m_SourceDepthIndex = source.m_Index,
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
				commandContext->DrawFullscreenTriangle();
			});
	}

	bool RenderPassTemporalAA::ValidatePipelineClosure(const RenderServices& services) noexcept
	{
		return m_IsAvailable && GetOrCreatePipeline(services).IsValid() &&
			GetOrCreateDepthHistoryPipeline(services).IsValid() &&
			GetOrCreateDisplayDepthPipeline(services).IsValid();
	}

	RHIPipelineHandle RenderPassTemporalAA::GetOrCreatePipeline(
		const RenderServices& services) noexcept
	{
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		const RHIPipelineHandle pipeline =
			pipelineCache->Resolve(m_PipelineSlot, m_PipelineRecipe, GetInfo());
		return pipeline;
	}

	RHIPipelineHandle RenderPassTemporalAA::GetOrCreateDepthHistoryPipeline(
		const RenderServices& services) noexcept
	{
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(
			m_DepthHistoryPipelineSlot, m_DepthHistoryPipelineRecipe, GetInfo());
	}

	RHIPipelineHandle RenderPassTemporalAA::GetOrCreateDisplayDepthPipeline(
		const RenderServices& services) noexcept
	{
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(
			m_DisplayDepthPipelineSlot, m_DisplayDepthPipelineKey, GetInfo());
	}
}
