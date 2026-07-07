#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassDebugDrawScene.h"
#include "Graphics/DebugDraw/DebugDrawSystem.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/Shader/ShaderManager.h"

namespace gglab
{
	namespace
	{
		struct DebugDrawPassParameters
		{
			uint32_t ViewIndex = 0;
			uint32_t Flags = 0;
			uint32_t Padding[2]{};
		};
		static_assert(IsPassRootConstantStruct<DebugDrawPassParameters>);

		struct PassData
		{
			RGTextureId m_SceneColor{};
			RGTextureId m_Depth{};
			RGTextureViewId m_Rtv{};
			RGTextureViewId m_Dsv{};
			RHIBufferHandle m_VertexBuffer{};
			uint64_t m_VertexBufferOffset = 0;
			DebugDrawVertexRange m_Range{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};
	}

	void RenderPassDebugDrawScene::AddPass(RenderGraph& rg,
		const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		const auto* debugDraw = services.m_DebugDrawSystem;
		if (!debugDraw || !context.IsRenderSceneReady())
		{
			return;
		}
		const DebugDrawFrameView frame = debugDraw->GetFrameView();
		if (!frame.HasSceneDraws())
		{
			return;
		}

		EnsureInitialized(services);
		const auto* contextPtr = &context;
		rg.AddPass<PassData>(GetRenderGraphPassName(),
			[frame](RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& targets = builder.GetBlackboard()
					.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(RenderViewID::Main);
				builder.WriteInPlace(targets.m_SceneColor, RGTextureAccess::RenderTarget);
				data.m_Depth = builder.Read(targets.m_Depth, RGTextureAccess::DepthStencilRead);
				data.m_SceneColor = targets.m_SceneColor;
				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_SceneColor);

				RHITextureViewDesc dsvDesc{};
				dsvDesc.m_Type = RHITextureViewType::DepthStencil;
				dsvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
				dsvDesc.m_Subresources.m_MipCount = 1;
				dsvDesc.m_Subresources.m_ArraySliceCount = 1;
				dsvDesc.m_Subresources.m_Aspects = RHITextureAspect::Depth | RHITextureAspect::Stencil;
				dsvDesc.m_ReadOnlyDepth = true;
				dsvDesc.m_ReadOnlyStencil = true;
				data.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(data.m_Depth, dsvDesc);

				data.m_VertexBuffer = frame.m_VertexBuffer;
				data.m_VertexBufferOffset = frame.m_VertexBufferOffset;
				data.m_Range = frame.m_Scene;
				data.m_Width = targets.m_Width;
				data.m_Height = targets.m_Height;
			},
			[this, contextPtr, &services](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				auto* renderer = services.m_Renderer;
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				const auto dsv = executeContext.GetViewHandle(data.m_Dsv);
				commandContext->SetPipeline(renderer->GetPipelineCache()->Resolve(
					m_PipelineSlot, m_Recipe, GetInfo()));
				commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1), dsv);
				commandContext->SetViewport({ 0.0f, 0.0f,
					static_cast<float>(data.m_Width), static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0,
					static_cast<int32_t>(data.m_Width), static_cast<int32_t>(data.m_Height) });
				commandContext->SetPrimitiveTopology(RHIPrimitiveTopology::LineList);

				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					renderer->GetSceneConstantBuffer()->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					renderer->GetViewStructuredBuffer()->GetBufferHandle());
				const DebugDrawPassParameters parameters{
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(RenderViewID::Main)),
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);

				const uint32_t bindingSize =
					(data.m_Range.m_FirstVertex + data.m_Range.m_VertexCount) * sizeof(DebugDrawVertex);
				const RHIVertexBufferBinding binding{
					.m_Buffer = data.m_VertexBuffer,
					.m_Offset = data.m_VertexBufferOffset,
					.m_Stride = sizeof(DebugDrawVertex),
					.m_SizeInBytes = bindingSize,
				};
				commandContext->SetVertexBuffers(0, std::span<const RHIVertexBufferBinding>(&binding, 1));
				commandContext->Draw(data.m_Range.m_VertexCount, 1, data.m_Range.m_FirstVertex);
			});
	}

	void RenderPassDebugDrawScene::EnsureInitialized(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}
		ShaderDesc shaderDesc{};
		shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassDebugDraw.hlsl";
		shaderDesc.m_Stage = ShaderStage::Vertex;
		shaderDesc.m_Entry = L"VSMain";
		m_Recipe.m_VSId = services.m_ShaderManager->LoadShader(shaderDesc);
		shaderDesc.m_Stage = ShaderStage::Pixel;
		shaderDesc.m_Entry = L"PSMain";
		m_Recipe.m_PSId = services.m_ShaderManager->LoadShader(shaderDesc);

		m_Recipe.m_BindingLayout = services.m_Renderer->GetCommonBindingLayout();
		m_Recipe.m_InputLayoutId = InputLayoutID::P3C4;
		m_Recipe.m_TopologyType = RHIPrimitiveTopologyType::Line;
		m_Recipe.m_PrimitiveTopology = RHIPrimitiveTopology::LineList;
		m_Recipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R16G16B16A16Float;
		m_Recipe.m_Formats.m_RenderTargetCount = 1;
		m_Recipe.m_Formats.m_DepthStencilFormat = RHIFormat::D24UnormS8Uint;
		m_Recipe.m_RasterizerPreset = RasterizerPreset::TwoSided;
		m_Recipe.m_BlendPreset = BlendPreset::AlphaBlend;
		m_Recipe.m_DepthPreset = DepthPreset::DepthReadOnly;
		m_IsInitialized = true;
	}
}
