#include "Graphics/RenderPass/RenderPassTemporalAA.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "Graphics/Pipeline/TemporalAACapability.h"
#include "Graphics/Pipeline/TemporalFrameTransaction.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/TemporalAAGraphResources.h"
#include "Graphics/RenderPass/TemporalGeometryGraphResources.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderParameters.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"

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
		inline constexpr uint32_t TemporalAAViewFlagMask =
			TemporalAAHistoryValidBit | TemporalAAHistoryColorPreviewBit |
			TemporalAAHistoryAgePreviewBit;

		struct TemporalAAPassParameters
		{
			uint32_t m_CurrentColorIndex = 0;
			uint32_t m_MotionIndex = 0;
			uint32_t m_CurrentDepthIndex = 0;
			uint32_t m_PreviousColorIndex = 0;
			uint32_t m_PreviousDepthIndex = 0;
			uint32_t m_ResolvedColorUavIndex = 0;
			uint32_t m_NextHistoryColorUavIndex = 0;
			uint32_t m_NextHistoryDepthUavIndex = 0;
			uint32_t m_ReprojectionDiagnosticsUavIndex = 0;
			uint32_t m_LinearClampSamplerIndex = 0;
			uint32_t m_PointClampSamplerIndex = 0;
			uint32_t m_ViewIndexAndHistoryValid = 0;
			uint32_t m_PackedDepthThresholds = 0;
			uint32_t m_PackedMaxHistoryFeedbackAndClampExpansion = 0;
			float m_VelocityWeightScale = 0.0f;
			float m_LuminanceWeightScale = 0.0f;
		};
		static_assert(IsPassRootConstantStruct<TemporalAAPassParameters>);
		static_assert(sizeof(TemporalAAPassParameters) == 64);

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
			TemporalAAPassParameters m_Parameters{};
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

		auto* renderer = services.m_Renderer;
		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(renderer);
		GGLAB_ASSERT_NOT_NULL(shaderManager);
		m_IsInitialized = true;
		m_PipelineRecipe.m_CSId = shaderManager->LoadProgram(
			shader_programs::TemporalAAReprojectionCompute);
		m_PipelineRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
		m_IsAvailable = m_PipelineRecipe.m_CSId.IsValid() &&
			m_PipelineRecipe.m_BindingLayout.IsValid();
	}

	void RenderPassTemporalAA::AddPass(RenderGraph& rg, const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized,
			"Temporal AA must be prepared before graph construction.");
		GGLAB_ASSERT_MSG(context.GetTemporalFramePlan().m_Active,
			"Temporal AA resolve requires one active pre-frame temporal plan.");
		GGLAB_ASSERT_MSG(m_IsAvailable,
			"Temporal resolve requires an available compute artifact and binding layout.");
		if (!m_IsAvailable || !context.GetTemporalFramePlan().m_Active)
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		auto* transaction = context.m_TemporalFrameTransaction;
		GGLAB_ASSERT_NOT_NULL(renderer);
		GGLAB_ASSERT_NOT_NULL(transaction);
		if (!renderer || !transaction)
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
		const auto* resourceRegistry = renderer->GetRenderResourceRegistry();
		const auto* samplerRegistry = renderer->GetSamplerRegistry();
		GGLAB_ASSERT_NOT_NULL(resourceRegistry);
		GGLAB_ASSERT_NOT_NULL(samplerRegistry);
		if (!resourceRegistry || !samplerRegistry)
		{
			return;
		}
		const PostProcessDebugSelection previewSelection =
			resourceRegistry->GetPostProcessPreviewSelection();
		const bool historyColorPreviewRequested =
			resourceRegistry->IsPostProcessPreviewRequested() &&
			UsesTemporalAAHistoryColorPreviewPayload(previewSelection.m_Tap);
		const bool historyAgePreviewRequested =
			resourceRegistry->IsPostProcessPreviewRequested() &&
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
				RHITextureDesc outputDesc{};
				outputDesc.m_Format = TemporalAAResolvedColorFormat;
				outputDesc.m_Extent = currentColorDesc.m_Extent;

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

				const RHITextureDesc& currentColorDesc = builder.GetTextureDesc(currentColor);
				resources.m_Width = currentColorDesc.m_Extent.m_Width;
				resources.m_Height = currentColorDesc.m_Extent.m_Height;
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
				outputDesc.m_Extent = currentColorDesc.m_Extent;
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
							: 0u),
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
				targets.m_SceneColor = resources.m_ResolvedSceneColor;
				const bool exported =
					transaction->ExportHistoryResources(builder, resources.m_History);
				GGLAB_ASSERT_MSG(exported,
					"Temporal AA must fully write and export its next history pair.");
			},
			[this, renderer, &context](RGExecuteContext& executeContext,
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
				parameters.m_NextHistoryDepthUavIndex = nextHistoryDepth.m_Index;
				parameters.m_ReprojectionDiagnosticsUavIndex = diagnostics.m_Index;

				commandContext->SetPipeline(GetOrCreatePipeline(*renderer));
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					renderer->GetSceneConstantBuffer()->GetBufferHandle(),
					context.m_RenderScene.m_SceneConstantBufferOffset);
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					renderer->GetViewStructuredBuffer()->GetBufferHandle());
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
				commandContext->Dispatch(
					(data.m_Width + TemporalAAThreadGroupSize - 1) /
						TemporalAAThreadGroupSize,
					(data.m_Height + TemporalAAThreadGroupSize - 1) /
						TemporalAAThreadGroupSize,
					1);
			});
	}

	bool RenderPassTemporalAA::ValidatePipelineClosure(const Renderer& renderer) noexcept
	{
		return m_IsAvailable && GetOrCreatePipeline(renderer).IsValid();
	}

	RHIPipelineHandle RenderPassTemporalAA::GetOrCreatePipeline(
		const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		const RHIPipelineHandle pipeline =
			pipelineCache->Resolve(m_PipelineSlot, m_PipelineRecipe, GetInfo());
		return pipeline;
	}
}
