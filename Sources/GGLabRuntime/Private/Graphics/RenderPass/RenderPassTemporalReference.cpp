#include "Graphics/RenderPass/RenderPassTemporalReference.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderParameters.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RenderPass/TemporalReferenceGraphResources.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <array>
#include <cstdint>
#include <span>

namespace gglab
{
	namespace
	{
		inline constexpr uint32_t TemporalReferenceThreadGroupSize = 8;

		struct TemporalReferencePassParameters
		{
			uint32_t m_CurrentColorIndex = 0;
			uint32_t m_PreviousSumIndex = 0;
			uint32_t m_NextSumUavIndex = 0;
			uint32_t m_MeanColorUavIndex = 0;
			uint32_t m_SampleIndex = 0;
		};
		static_assert(IsPassConstantBufferStruct<TemporalReferencePassParameters>);
		static_assert(sizeof(TemporalReferencePassParameters) == 20);

		struct TemporalReferencePassData
		{
			RGTextureViewId m_CurrentColorSrv{};
			RGTextureViewId m_PreviousSumSrv{};
			RGTextureViewId m_NextSumUav{};
			RGTextureViewId m_MeanColorUav{};
			uint32_t m_SampleIndex = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};

		RGTextureId AddUnoccludedAmbientOcclusionPass(RenderGraph& rg,
			const RHIExtent3D& extent) noexcept
		{
			struct PassData
			{
				RGTextureViewId m_Rtv{};
			};
			RGTextureId visibility{};
			rg.AddPass<PassData>("TemporalReference.UnoccludedAO",
				[&visibility, extent](RenderGraph::RGBuilder& builder, PassData& data)
				{
					RHITextureDesc desc{};
					desc.m_Format = RHIFormat::R16G16B16A16Float;
					desc.m_Extent = extent;
					desc.m_ClearValue = RHIClearValue{
						.m_Format = desc.m_Format,
						.m_Color = { 1.0f, 0.0f, 0.0f, 1.0f },
					};
					visibility = builder.CreateTexture("TemporalReference.UnoccludedAO", desc);
					builder.WriteInPlace(visibility, RGTextureAccess::RenderTarget);
					data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(visibility);
				},
				[](RGExecuteContext& executeContext, PassData& data)
				{
					auto* commandContext = executeContext.GetGraphicsCommandContext();
					const std::array attachments{ RHIRenderingAttachment{
						.m_View = executeContext.GetViewHandle(data.m_Rtv),
						.m_LoadOp = RHIContentLoadOp::DontCare,
					} };
					commandContext->BeginRendering({ .m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(attachments) });
					commandContext->ClearColorAttachment(0, { 1.0f, 0.0f, 0.0f, 1.0f });
				});
			return visibility;
		}
	}

	TemporalReferenceSourceEligibility RenderPassTemporalReference::GetSourceEligibility(
		TemporalReferenceSignal signal, GTAOFrameStatus gtaoStatus) noexcept
	{
		if (signal == TemporalReferenceSignal::AmbientOcclusion)
		{
			if (gtaoStatus == GTAOFrameStatus::Disabled)
			{
				return TemporalReferenceSourceEligibility::GTAODisabled;
			}
			if (gtaoStatus == GTAOFrameStatus::CoreCapabilityUnavailable)
			{
				return TemporalReferenceSourceEligibility::GTAOCoreCapabilityUnavailable;
			}
		}
		return TemporalReferenceSourceEligibility::Eligible;
	}

	void RenderPassTemporalReference::Prepare(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}
		auto* shaderManager = services.m_ShaderPrograms;
		GGLAB_ASSERT_NOT_NULL(shaderManager);
		m_IsInitialized = true;
		m_PipelineRecipe.m_CSId = shaderManager->LoadProgram(
			shader_programs::TemporalReferenceAccumulateCompute);
		m_PipelineRecipe.m_BindingLayout = services.m_BindingLayout->GetCommonBindingLayout();
		m_IsAvailable = m_PipelineRecipe.m_CSId.IsValid() &&
			m_PipelineRecipe.m_BindingLayout.IsValid();
	}

	void RenderPassTemporalReference::AddPass(RenderGraph& rg, const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* transaction = context.m_TemporalFrameTransaction;
		if (!transaction || !transaction->GetReferenceSample())
		{
			return;
		}
		GGLAB_ASSERT_MSG(m_IsInitialized && m_IsAvailable && transaction->CanAccumulateReference(),
			"Temporal reference frames require a validated accumulation closure.");
		if (!m_IsAvailable || !transaction->CanAccumulateReference())
		{
			return;
		}

		const RenderViewID displayViewId = context.GetDisplayViewId();
		const uint32_t sampleIndex = transaction->GetReferenceSample()->m_Index;
		const TemporalReferenceSignal signal = transaction->GetReferenceSample()->m_Signal;
		const bool ambientOcclusion = signal == TemporalReferenceSignal::AmbientOcclusion;
		auto& targets = rg.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName)
			.GetViewTargets(displayViewId);
		RGTextureId source = targets.m_DisplayColor;
		if (ambientOcclusion)
		{
			const auto& gtao = rg.GetBlackboard().Get<RGGTAOResources>(GTAOResourcesName);
			const bool sourceEligible = GetSourceEligibility(signal, gtao.m_Status) ==
				TemporalReferenceSourceEligibility::Eligible;
			GGLAB_ASSERT_MSG(sourceEligible, "Temporal reference source must be validated.");
			if (!sourceEligible)
			{
				return;
			}
			if (gtao.m_Status == GTAOFrameStatus::NoOpaqueDraws)
			{
				// No depth coverage is a fully visible sample, not missing input. Keep its
				// full extent so accumulation writes every pixel and commits the sum normally.
				source = AddUnoccludedAmbientOcclusionPass(rg,
					{ targets.m_RenderWidth, targets.m_RenderHeight, 1 });
			}
			else
			{
				GGLAB_ASSERT_MSG(gtao.IsComplete(), "Active GTAO must publish complete reference input.");
				if (!gtao.IsComplete())
				{
					return;
				}
				source = gtao.m_FinalAO;
			}
		}
		rg.AddPass<TemporalReferencePassData>(
			GetRenderGraphPassName(), RGPassEncoderType::Compute,
			[transaction, displayViewId, sampleIndex, ambientOcclusion, source](
				RenderGraph::RGBuilder& builder, TemporalReferencePassData& data)
			{
				auto& targets = builder.GetBlackboard()
					.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);
				TemporalReferenceRenderGraphResources sums{};
				const bool imported = transaction->ImportReferenceResources(builder, sums);
				GGLAB_ASSERT_MSG(imported && sums.IsValid(),
					"Temporal reference failed to import its sum pair.");
				if (!imported || !sums.IsValid())
				{
					return;
				}

				// The reference accumulates the complete composed scene, including
				// post-temporal geometry; Temporal AA is inactive, so this is the scene color.
				// An ambient-occlusion reference accumulates the final GTAO visibility instead.
				const RGTextureId currentColor = builder.Read(
					source, RGTextureAccess::Sample,
					RHIStage::ComputeShader);
				data.m_CurrentColorSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(currentColor);
				if (sums.m_PreviousValid)
				{
					sums.m_PreviousSum = builder.Read(
						sums.m_PreviousSum, RGTextureAccess::Sample, RHIStage::ComputeShader);
					data.m_PreviousSumSrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(sums.m_PreviousSum);
				}
				else
				{
					// The shader does not read the previous sum of a first sample; bind a
					// defined current-frame view so the undefined import stays unread.
					data.m_PreviousSumSrv = data.m_CurrentColorSrv;
				}

				const RHITextureDesc& currentDesc = builder.GetTextureDesc(currentColor);
				RHITextureDesc meanDesc{};
				// The mean visibility keeps full precision in a four-channel format that the
				// shader's typed float4 store supports on every backend.
				meanDesc.m_Format = ambientOcclusion ? RHIFormat::R16G16B16A16Float : currentDesc.m_Format;
				meanDesc.m_Extent = currentDesc.m_Extent;
				RGTextureId mean = builder.CreateTexture("TemporalReference.MeanSceneColor", meanDesc);
				builder.WriteInPlace(mean, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				builder.WriteInPlace(
					sums.m_NextSum, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_MeanColorUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(mean);
				data.m_NextSumUav =
					builder.CreateView<RHITextureViewType::UnorderedAccess>(sums.m_NextSum);
				data.m_SampleIndex = sampleIndex;
				data.m_Width = currentDesc.m_Extent.m_Width;
				data.m_Height = currentDesc.m_Extent.m_Height;

				// Post-processing presents the running mean of every accumulated sample; an
				// ambient-occlusion reference publishes it for the gtao-final-ao tap and leaves
				// the GTAO record describing the frame's own visibility.
				if (ambientOcclusion)
				{
					builder.GetBlackboard().GetOrCreate<RGTemporalReferenceResources>(
						TemporalReferenceResourcesName).m_MeanAmbientOcclusion = mean;
				}
				else
				{
					targets.m_DisplayColor = mean;
				}
				const bool exported = transaction->ExportReferenceResources(builder, sums);
				GGLAB_ASSERT_MSG(exported,
					"Temporal reference must fully write and export its next sum.");
			},
			[this, services, &context](RGExecuteContext& executeContext,
				TemporalReferencePassData& data)
			{
				auto* commandContext = executeContext.GetDirectComputeCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const auto currentColor = executeContext.GetViewDescriptor(data.m_CurrentColorSrv);
				const auto previousSum = executeContext.GetViewDescriptor(data.m_PreviousSumSrv);
				const auto nextSum = executeContext.GetViewDescriptor(data.m_NextSumUav);
				const auto meanColor = executeContext.GetViewDescriptor(data.m_MeanColorUav);
				GGLAB_ASSERT_MSG(currentColor.IsValid() && previousSum.IsValid() &&
					nextSum.IsValid() && meanColor.IsValid(),
					"Temporal reference views must be shader visible before dispatch.");

				const TemporalReferencePassParameters parameters{
					.m_CurrentColorIndex = currentColor.m_Index,
					.m_PreviousSumIndex = previousSum.m_Index,
					.m_NextSumUavIndex = nextSum.m_Index,
					.m_MeanColorUavIndex = meanColor.m_Index,
					.m_SampleIndex = data.m_SampleIndex,
				};
				commandContext->SetPipeline(GetOrCreatePipeline(services));
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					{
						.m_Buffer = services.m_FrameBuffers->GetSceneConstantBuffer()->GetBufferHandle(),
						.m_OffsetInBytes = context.m_RenderScene.m_SceneConstantBufferOffset,
						.m_SizeInBytes = sizeof(SceneGPU),
					});
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					services.m_FrameBuffers->GetViewStructuredBuffer()->GetBufferHandle());
				if (!commandContext->SetConstantBufferData(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
				{
					return;
				}
				commandContext->Dispatch(
					(data.m_Width + TemporalReferenceThreadGroupSize - 1) /
					TemporalReferenceThreadGroupSize,
					(data.m_Height + TemporalReferenceThreadGroupSize - 1) /
					TemporalReferenceThreadGroupSize,
					1);
			});
	}

	bool RenderPassTemporalReference::ValidatePipelineClosure(const RenderServices& services) noexcept
	{
		return m_IsAvailable && GetOrCreatePipeline(services).IsValid();
	}

	RHIPipelineHandle RenderPassTemporalReference::GetOrCreatePipeline(
		const RenderServices& services) noexcept
	{
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_PipelineRecipe, GetInfo());
	}
}
