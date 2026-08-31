#include "Graphics/RenderPass/RenderPassDebugDraw.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

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
			RGTextureId m_Color{};
			RGTextureId m_Depth{};
			RGTextureViewId m_Rtv{};
			RGTextureViewId m_Dsv{};
			RHIBufferHandle m_VertexBuffer{};
			uint64_t m_VertexBufferOffset = 0;
			DebugDrawBatchRanges m_World{};
			DebugDrawBatchRanges m_Screen{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};

		[[nodiscard]] uint32_t RangeEnd(const DebugDrawVertexRange& range) noexcept
		{
			return range.m_FirstVertex + range.m_VertexCount;
		}
	}

	RenderPassDebugDraw::RenderPassDebugDraw(DebugDrawPassMode mode) noexcept :
		RenderPassBase(MakeInfo(mode)), m_Mode(mode)
	{
	}

	RenderPassInfo RenderPassDebugDraw::MakeInfo(DebugDrawPassMode mode) noexcept
	{
		const bool scene = mode == DebugDrawPassMode::Scene;
		return {
			.m_TypeName = scene ? "Debug.DebugDrawScene" : "Debug.DebugDrawOverlay",
			.m_DisplayName = scene ? "Debug Draw Scene" : "Debug Draw Overlay",
			.m_CategoryName = "Debug",
			.m_Description = scene ? "Draws depth-tested debug geometry into scene color."
								   : "Draws always-visible world and screen-space debug geometry.",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Graphics,
		};
	}

	void RenderPassDebugDraw::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		if (!context.IsRenderSceneReady())
		{
			return;
		}
		const DebugDrawFrameView frame = context.m_DebugDrawFrame;
		const bool scene = m_Mode == DebugDrawPassMode::Scene;
		if ((scene && !frame.HasSceneDraws()) || (!scene && !frame.HasOverlayDraws()))
		{
			return;
		}

		EnsureInitialized(services);
		const auto* contextPtr = &context;
		const RenderViewID displayViewId = context.GetDisplayViewId();
		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[frame, scene, displayViewId](RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& targets = builder.GetBlackboard()
					.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);
				if (scene)
				{
					auto& sceneDepth =
						builder.GetBlackboard().Get<RGSceneDepthResources>(SceneDepthResourcesName);
					builder.ReadWriteInPlace(targets.m_SceneColor, RGTextureAccess::RenderTarget);
					data.m_Color = targets.m_SceneColor;
					data.m_Depth =
						builder.Read(sceneDepth.m_Texture, RGTextureAccess::DepthStencilRead);
					RHITextureViewDesc dsvDesc = sceneDepth.m_DsvDesc;
					dsvDesc.m_ReadOnlyDepth = true;
					data.m_Dsv =
						builder.CreateView<RHITextureViewType::DepthStencil>(data.m_Depth, dsvDesc);
					data.m_World = frame.m_Scene;
				}
				else
				{
					builder.ReadWriteInPlace(targets.m_BackBuffer, RGTextureAccess::RenderTarget);
					data.m_Color = targets.m_BackBuffer;
					data.m_World = frame.m_OverlayWorld;
					data.m_Screen = frame.m_OverlayScreen;
				}
				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_Color);
				data.m_VertexBuffer = frame.m_VertexBuffer;
				data.m_VertexBufferOffset = frame.m_VertexBufferOffset;
				data.m_Width = targets.m_Width;
				data.m_Height = targets.m_Height;
			},
			[this, contextPtr, &services, scene, displayViewId](
				RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				auto* renderer = services.m_Renderer;
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				const auto dsv =
					scene ? executeContext.GetViewHandle(data.m_Dsv) : RHITextureViewHandle{};
				const RHIRenderingAttachment colorAttachment{ .m_View = rtv };
				commandContext->BeginRendering({
					.m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&colorAttachment, 1),
					.m_DepthAttachment = dsv.IsValid()
						? std::optional<RHIRenderingAttachment>(
							RHIRenderingAttachment{ .m_View = dsv })
						: std::nullopt,
				});
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
					static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
					static_cast<int32_t>(data.m_Height) });

				const uint32_t lastVertex = std::max({
					RangeEnd(data.m_World.m_Lines),
					RangeEnd(data.m_World.m_Triangles),
					RangeEnd(data.m_Screen.m_Lines),
					RangeEnd(data.m_Screen.m_Triangles),
					});
				const RHIVertexBufferBinding binding{
					.m_Buffer = data.m_VertexBuffer,
					.m_Offset = data.m_VertexBufferOffset,
					.m_Stride = sizeof(DebugDrawVertex),
					.m_SizeInBytes = lastVertex * sizeof(DebugDrawVertex),
				};
				commandContext->SetVertexBuffers(
					0, std::span<const RHIVertexBufferBinding>(&binding, 1));

				auto draw =
					[this, contextPtr, commandContext, renderer, displayViewId](
						const DebugDrawVertexRange& range, bool triangles, uint32_t flags) noexcept
					{
						if (range.IsEmpty())
						{
							return;
						}
						commandContext->SetPipeline(GetPipeline(*renderer, triangles));
						commandContext->SetConstantBuffer(
							static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
							renderer->GetSceneConstantBuffer()->GetBufferHandle(),
							contextPtr->m_RenderScene.m_SceneConstantBufferOffset);
						commandContext->SetReadOnlyBuffer(
							static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
							renderer->GetViewStructuredBuffer()->GetBufferHandle());
						commandContext->SetPrimitiveTopology(triangles
							? RHIPrimitiveTopology::TriangleList
							: RHIPrimitiveTopology::LineList);
						const DebugDrawPassParameters parameters{
							.ViewIndex = static_cast<uint32_t>(utils::ToIndex(displayViewId)),
							.Flags = flags,
						};
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
						commandContext->Draw(range.m_VertexCount, 1, range.m_FirstVertex);
					};
				const uint32_t worldFlags = scene ? 0u : DebugDrawFlagOutputSRGB;
				draw(data.m_World.m_Lines, false, worldFlags);
				draw(data.m_World.m_Triangles, true, worldFlags);
				const uint32_t screenFlags = DebugDrawFlagScreenSpace | DebugDrawFlagOutputSRGB;
				draw(data.m_Screen.m_Lines, false, screenFlags);
				draw(data.m_Screen.m_Triangles, true, screenFlags);
			});
	}

	void RenderPassDebugDraw::EnsureInitialized(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}
		const ShaderID vs =
			services.m_ShaderManager->LoadProgram(shader_programs::DebugDrawVertex);
		const ShaderID ps =
			services.m_ShaderManager->LoadProgram(shader_programs::DebugDrawPixel);

		for (uint32_t index = 0; index < m_Recipes.size(); ++index)
		{
			auto& recipe = m_Recipes[index];
			const bool triangles = index != 0;
			recipe.m_BindingLayout = services.m_Renderer->GetCommonBindingLayout();
			recipe.m_InputLayoutId = InputLayoutID::P3C4;
			recipe.m_VSId = vs;
			recipe.m_PSId = ps;
			recipe.m_TopologyType =
				triangles ? RHIPrimitiveTopologyType::Triangle : RHIPrimitiveTopologyType::Line;
			recipe.m_PrimitiveTopology =
				triangles ? RHIPrimitiveTopology::TriangleList : RHIPrimitiveTopology::LineList;
			recipe.m_Formats.m_RenderTargetFormats[0] =
				m_Mode == DebugDrawPassMode::Scene
				? RHIFormat::R16G16B16A16Float
				: services.m_Renderer->GetSwapChain()->GetFormat();
			recipe.m_Formats.m_RenderTargetCount = 1;
			recipe.m_Formats.m_DepthStencilFormat =
				m_Mode == DebugDrawPassMode::Scene ? RHIFormat::D32Float : RHIFormat::Unknown;
			recipe.m_RasterizerPreset = RasterizerPreset::TwoSided;
			recipe.m_BlendPreset = BlendPreset::AlphaBlend;
			recipe.m_DepthPreset = m_Mode == DebugDrawPassMode::Scene
				? DepthPreset::ReversedZReadOnly
				: DepthPreset::DepthDisabled;
		}
		m_IsInitialized = true;
	}

	RHIPipelineHandle RenderPassDebugDraw::GetPipeline(
		const Renderer& renderer, bool triangles) noexcept
	{
		const size_t index = triangles ? 1 : 0;
		return renderer.GetPipelineCache()->Resolve(
			m_PipelineSlots[index], m_Recipes[index], GetInfo());
	}
}
