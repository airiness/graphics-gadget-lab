#include "Graphics/RenderPass/RenderPassGIReceiver.h"
#include "Graphics/RenderPass/GIReceiverGraphResources.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <span>

namespace gglab
{
	namespace
	{
		const RenderPassInfo PassInfo{ .m_TypeName = "GI.Receiver", .m_DisplayName = "GI receiver data",
			.m_CategoryName = "Global Illumination", .m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Graphics };
		struct Parameters
		{
			uint32_t m_View = 0;
			std::array<uint32_t, 3> m_Padding{};
		};
		static_assert(sizeof(Parameters) == 16);
	}

	RenderFrameValidationResult RenderPassGIReceiver::Prepare(const RenderFrameContext& frame,
		const RenderServices& services, ShaderID coverageVertex) noexcept
	{
		if (!frame.GetDisplayRenderView().GetResolution().IsNative() ||
			frame.GetTemporalFramePlan().IsConsumerActive(TemporalConsumer::TemporalAA) ||
			frame.GetTemporalFramePlan().HasService(TemporalService::ProjectionJitter) ||
			frame.GetDisplayRenderView().m_JitterPixels.m_X != 0.0f ||
			frame.GetDisplayRenderView().m_JitterPixels.m_Y != 0.0f)
		{
			return RenderFrameValidationResult::ContractFailure("GI receivers require Native, unjittered raster with TAA off");
		}
		if (!m_Layout.IsValid())
		{
			const RHITextureDesc desc{ .m_Format = RHIFormat::R32G32B32A32Float,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::RenderTarget | RHITextureUsage::CopySource };
			if (!services.m_Presentation->GetDevice()->QueryTextureSupport(desc).IsSupported())
			{
				return RenderFrameValidationResult::ContractFailure("GI receiver float attachments are unavailable");
			}
			m_Layout = ResolvedRenderLayout::Create(services.m_Presentation->GetRHIContext()->GetPipelineSystem(),
				*services.m_BindingLayout->GetCommonLayoutContract(), "GI.Receiver");
			m_Scene = ResolvedBindingRef<SceneGPU>::Resolve(m_Layout, scene_bindings::SceneConstants);
			m_Recipe.m_BindingLayout = m_Layout.GetNativeLayout();
			m_Recipe.m_VSId = coverageVertex;
			m_Recipe.m_PSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIReceiverPixel);
			m_Recipe.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
			m_Recipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			m_Recipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			m_Recipe.m_DepthPreset = DepthPreset::ReversedZEqualReadOnly;
			m_Recipe.m_BlendPreset = BlendPreset::Default;
			m_Recipe.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
			m_Recipe.m_Formats.m_RenderTargetCount = static_cast<uint32_t>(GIReceiverTextureNames.size());
			for (auto& format : m_Recipe.m_Formats.m_RenderTargetFormats)
			{
				format = RHIFormat::R32G32B32A32Float;
			}
		}
		for (size_t side = 0; side < m_Pipelines.size(); ++side)
		{
			m_Recipe.m_RasterizerPreset = side == 0 ? RasterizerPreset::Default : RasterizerPreset::TwoSided;
			m_Pipelines[side] = m_Layout.Resolve(*services.m_PipelineResolver, m_Slots[side], m_Recipe, PassInfo);
			if (!m_Pipelines[side].IsValid())
			{
				return RenderFrameValidationResult::ContractFailure("GI receiver pipeline is unavailable");
			}
		}
		return RenderFrameValidationResult::Ready();
	}

	void RenderPassGIReceiver::AddPass(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services) noexcept
	{
		struct PassData
		{
			std::array<RGTextureViewId, GIReceiverTextureNames.size()> m_Targets{};
			RGTextureViewId m_Depth;
		};
		graph.AddPass<PassData>(PassInfo.m_TypeName.c_str(),
			[](RenderGraph::RGBuilder& builder, PassData& data)
			{
				const auto& depth = builder.GetBlackboard().Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto extent = builder.GetTextureDesc(depth.m_Texture).m_Extent;
				const auto& ibl = builder.GetBlackboard().Get<RGIBLResources>(IBLResourcesName);
				GGLAB_UNUSED(builder.Read(ibl.m_BrdfLut, RGTextureAccess::Sample, RHIStage::PixelShader));
				auto& receivers = builder.GetBlackboard().GetOrCreate<RGGIReceiverResources>(GIReceiverResourcesName);
				for (size_t index = 0; index < data.m_Targets.size(); ++index)
				{
					auto& texture = receivers.m_Textures[index];
					// Background class is zero; the optimized clear must match the encoded clear.
					texture = builder.CreateTexture(GIReceiverTextureNames[index],
						{ .m_Format = RHIFormat::R32G32B32A32Float, .m_Extent = extent,
							.m_ClearValue = RHIClearValue{ .m_Format = RHIFormat::R32G32B32A32Float,
								.m_Color = { 0.0f, 0.0f, 0.0f, 0.0f } } });
					builder.WriteInPlace(texture, RGTextureAccess::RenderTarget);
					data.m_Targets[index] = builder.CreateView<RHITextureViewType::RenderTarget>(texture);
				}
				const auto read = builder.Read(depth.m_Texture, RGTextureAccess::DepthStencilRead);
				auto desc = depth.m_DsvDesc;
				desc.m_ReadOnlyDepth = true;
				data.m_Depth = builder.CreateView<RHITextureViewType::DepthStencil>(read, desc);
				builder.SideEffect();
			},
			[this, &frame, services](RGExecuteContext& execute, PassData& data)
			{
				auto* commands = execute.GetGraphicsCommandContext();
				std::array<RHIRenderingAttachment, GIReceiverTextureNames.size()> targets{};
				for (size_t index = 0; index < targets.size(); ++index)
				{
					targets[index] = { .m_View = execute.GetViewHandle(data.m_Targets[index]),
						.m_LoadOp = RHIContentLoadOp::DontCare };
				}
				commands->BeginRendering({ .m_ColorAttachments = targets,
					.m_DepthAttachment = RHIRenderingAttachment{ .m_View = execute.GetViewHandle(data.m_Depth),
						.m_LoadOp = RHIContentLoadOp::Load } });
				for (uint32_t index = 0; index < targets.size(); ++index)
				{
					commands->ClearColorAttachment(index, { 0.0f, 0.0f, 0.0f, 0.0f });
				}
				const auto& queue = frame.GetRenderQueue(frame.GetDisplayViewId());
				const auto& domain = queue.m_CoverageRasterDomain;
				commands->SetViewport(domain.m_Viewport);
				commands->SetScissorRect(domain.m_Scissor);
				commands->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
				ScopedConstantBindings<RHIGraphicsCommandContext> constants(*commands, m_Layout, m_Scene);
				for (const auto bucket : { RenderBucket::Opaque, RenderBucket::AlphaTest })
				{
					const auto& range = queue.m_BucketDrawRanges[utils::ToIndex(bucket)];
					for (uint32_t offset = 0; offset < range.m_Count; ++offset)
					{
						const auto& draw = queue.m_DrawItems[range.m_Start + offset];
						const auto& packet = draw.m_CoverageDrawPacket;
						const size_t side = RenderQueueBuilder::DecodeVariantDoubleSided(draw.m_VariantBits) ? 1u : 0u;
						if (!constants.SetPipeline(m_Pipelines[side]) ||
							!constants.Bind(m_Scene, frame.m_FrameConstants.m_SceneSnapshot) ||
							!commands->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB), frame.m_FrameConstants.m_Views) ||
							!commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB),
								Parameters{ .m_View = domain.m_ViewBindingId }))
						{
							return;
						}
						commands->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB),
							services.m_FrameBuffers->GetObjectStructuredBuffer()->GetBufferHandle(frame.m_FrameSlotIndex));
						commands->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
							services.m_FrameBuffers->GetMaterialStructuredBuffer()->GetBufferHandle(frame.m_FrameSlotIndex));
						commands->SetVertexBuffers(0, std::span<const RHIVertexBufferBinding>(&packet.m_Geometry.m_VertexBuffer, 1));
						commands->SetIndexBuffer(packet.m_Geometry.m_IndexBuffer);
						commands->SetInlineConstants(static_cast<uint32_t>(CommonRSRootParamIndex::DrawConstants), packet.m_DrawParameters);
						const auto& indexed = packet.m_IndexedDraw;
						commands->DrawIndexed(indexed.m_IndexCount, indexed.m_InstanceCount,
							indexed.m_StartIndexLocation, indexed.m_BaseVertexLocation, indexed.m_StartInstanceLocation);
					}
				}
			});
	}
}
