#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassDebugDrawOverlay.h"
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
		constexpr uint32_t DebugDrawFlagScreenSpace = 1u << 0;
		constexpr uint32_t DebugDrawFlagOutputSRGB = 1u << 1;

		struct DebugDrawPassParameters
		{
			uint32_t ViewIndex = 0;
			uint32_t Flags = 0;
			uint32_t Padding[2]{};
		};
		static_assert(IsPassRootConstantStruct<DebugDrawPassParameters>);

		struct PassData
		{
			RGTextureId m_BackBuffer{};
			RGTextureViewId m_Rtv{};
			RHIBufferHandle m_VertexBuffer{};
			uint64_t m_VertexBufferOffset = 0;
			DebugDrawVertexRange m_WorldRange{};
			DebugDrawVertexRange m_ScreenRange{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};
	}

	void RenderPassDebugDrawOverlay::AddPass(RenderGraph& rg,
		const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		const auto* debugDraw = services.m_DebugDrawSystem;
		if (!debugDraw || !context.IsRenderSceneReady())
		{
			return;
		}
		const DebugDrawFrameView frame = debugDraw->GetFrameView();
		if (!frame.HasOverlayDraws())
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
				builder.WriteInPlace(targets.m_BackBuffer, RGTextureAccess::RenderTarget);
				data.m_BackBuffer = targets.m_BackBuffer;
				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BackBuffer);
				data.m_VertexBuffer = frame.m_VertexBuffer;
				data.m_VertexBufferOffset = frame.m_VertexBufferOffset;
				data.m_WorldRange = frame.m_OverlayWorld;
				data.m_ScreenRange = frame.m_OverlayScreen;
				data.m_Width = targets.m_Width;
				data.m_Height = targets.m_Height;
			},
			[this, contextPtr, &services](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				auto* renderer = services.m_Renderer;
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				commandContext->SetPipeline(renderer->GetPipelineCache()->Resolve(
					m_PipelineSlot, m_Recipe, GetInfo()));
				commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1));
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

				const uint32_t lastVertex = std::max(
					data.m_WorldRange.m_FirstVertex + data.m_WorldRange.m_VertexCount,
					data.m_ScreenRange.m_FirstVertex + data.m_ScreenRange.m_VertexCount);
				const RHIVertexBufferBinding binding{
					.m_Buffer = data.m_VertexBuffer,
					.m_Offset = data.m_VertexBufferOffset,
					.m_Stride = sizeof(DebugDrawVertex),
					.m_SizeInBytes = lastVertex * sizeof(DebugDrawVertex),
				};
				commandContext->SetVertexBuffers(0, std::span<const RHIVertexBufferBinding>(&binding, 1));

				auto drawRange = [commandContext](const DebugDrawVertexRange& range, uint32_t flags) noexcept
					{
						if (range.IsEmpty())
						{
							return;
						}
						const DebugDrawPassParameters parameters{
							.ViewIndex = static_cast<uint32_t>(utils::ToIndex(RenderViewID::Main)),
							.Flags = flags,
						};
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
						commandContext->Draw(range.m_VertexCount, 1, range.m_FirstVertex);
					};
				drawRange(data.m_WorldRange, DebugDrawFlagOutputSRGB);
				drawRange(data.m_ScreenRange, DebugDrawFlagScreenSpace | DebugDrawFlagOutputSRGB);
			});
	}

	void RenderPassDebugDrawOverlay::EnsureInitialized(const RenderServices& services) noexcept
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
		m_Recipe.m_Formats.m_RenderTargetFormats[0] = services.m_Renderer->GetSwapChain()->GetFormat();
		m_Recipe.m_Formats.m_RenderTargetCount = 1;
		m_Recipe.m_RasterizerPreset = RasterizerPreset::TwoSided;
		m_Recipe.m_BlendPreset = BlendPreset::AlphaBlend;
		m_Recipe.m_DepthPreset = DepthPreset::DepthDisabled;
		m_IsInitialized = true;
	}
}
