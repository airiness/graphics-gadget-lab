#include "Application/Lab/Validation/GIProbeDepthConformance.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/Pipeline/GIProbeVisualization.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gglab
{
	namespace
	{
		const RenderPassInfo SceneInfo{ .m_TypeName = "Lab.GIProbeDepth.Scene",
			.m_DisplayName = "Probe depth comparison inputs", .m_CategoryName = "Debug",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Graphics };
		const RenderPassInfo ProbeInfo{ .m_TypeName = "Lab.GIProbeDepth.Compare",
			.m_DisplayName = "Probe D32 comparison", .m_CategoryName = "Debug",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Graphics };
		constexpr uint32_t Tile = 16;
		struct Parameters
		{
			std::array<uint32_t, 2> m_Candidates{};
			uint32_t m_Scene = 0;
			uint32_t m_Reversed = 0;
			uint32_t m_SceneEnabled = 0;
			uint32_t m_AdmissionEnabled = 1;
			uint32_t m_BaseID = 0;
			uint32_t m_Case = 0;
			uint32_t m_SceneTexture = 0;
			uint32_t m_Batches = 0;
			std::array<uint32_t, 2> m_Padding{};
		};
		static_assert(sizeof(Parameters) == 48 && offsetof(Parameters, m_Case) == 28 &&
			offsetof(Parameters, m_SceneTexture) == 32);
		struct Case
		{
			uint32_t m_A;
			uint32_t m_B;
			uint32_t m_Scene;
			bool m_SceneEnabled = false;
			bool m_Admission = true;
			bool m_Batches = false;
		};
		constexpr size_t CaseCount = 30;
		std::array<Case, CaseCount> Cases(bool reversed) noexcept
		{
			const uint32_t clear = reversed ? 0 : 0x3f800000;
			const uint32_t nearDepth = reversed ? 0x3f800000 : 0;
			// Include raw subnormal conversion as well as adjacent normal D32 values.
			const uint32_t clearAdjacent = reversed ? 1 : 0x3f7fffff;
			const uint32_t nearAdjacent = reversed ? 0x3f7fffff : 1;
			return {{
				{ 0x3e800000, 0x3f400000, clear },
				{ 0x3f400000, 0x3e800000, clear },
				{ 0x3f000000, 0x3f000000, clear },
				{ 0x3f000000, 0x3f000001, clear },
				{ 0x3f000000, 0x3effffff, clear },
				{ clear, clear, clear },
				{ clear, clearAdjacent, clear },
				{ nearDepth, nearDepth, clear },
				{ nearDepth, nearAdjacent, clear },
				{ 0xbdcccccd, 0x3f000000, clear },
				{ 0x3f8ccccd, 0x3f000000, clear },
				{ 0x7fc00000, 0x3f000000, clear },
				{ 0x7f800000, 0x3f000000, clear },
				{ 0x80000000, 0x7fc00000, clear },
				{ 0x3e800000, 0x3f400000, clear, true },
				{ 0x3f000000, 0x3f000000, 0x3f000000, true },
				{ 0x3e800000, 0x3f400000, nearDepth, true },
				{ 0x3e800000, 0x3f400000, 0x3f000000, true },
				{ 0x3effffff, 0x3f000001, 0x3f000000, true },
				{ 0x3e800000, 0x3f400000, 0x7fc00000, true },
				{ 0x3f000000, 0x3f000000, clear, false, true, true },
				{ 0x3e800000, 0x3f400000, clear, false, true, true },
				{ clear, clearAdjacent, clear, false, false },
				{ clear, clear, clear, false, false },
				{ nearDepth, nearDepth, clear, false, false },
				{ clear, 0x00800000, clear },
				{ 0x00800000, 0x00800001, clear },
				{ 1, 0x7fc00000, 0, true },
				{ 1, 0, 0, true },
				{ 0x007fffff, 0x00800000, 0, true },
			}};
		}
	}

	struct GIProbeDepthConformance::State
	{
		ResolvedRenderLayout m_Layout;
		ResolvedBindingRef<SceneGPU> m_Scene;
		GraphicsPipelineSlot m_SceneSlot;
		std::array<GraphicsPipelineSlot, 2> m_ProbeSlots;
		ResolvedRenderPipeline m_ScenePipeline;
		std::array<ResolvedRenderPipeline, 2> m_ProbePipelines;
		bool Prepare(const RenderServices& services) noexcept
		{
			if (!m_Layout.IsValid())
			{
				const auto* common = services.m_BindingLayout->GetCommonLayoutContract();
				if (!common)
				{
					return false;
				}
				m_Layout = ResolvedRenderLayout::Create(
					services.m_Presentation->GetRHIContext()->GetPipelineSystem(), *common, "GIProbeDepth.Conformance");
				m_Scene = ResolvedBindingRef<SceneGPU>::Resolve(m_Layout, scene_bindings::SceneConstants);
			}
			GraphicsPhysicalPipelineKey recipe;
			recipe.m_BindingLayout = m_Layout.GetNativeLayout();
			recipe.m_VSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIProbeDepthConformanceVertex);
			recipe.m_InputLayoutId = InputLayoutID::None;
			recipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			recipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			recipe.m_RasterizerPreset = RasterizerPreset::TwoSided;
			recipe.m_PSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIProbeDepthConformanceScene);
			recipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R32Float;
			recipe.m_Formats.m_RenderTargetFormats[1] = RHIFormat::R32Uint;
			recipe.m_Formats.m_RenderTargetCount = 2;
			recipe.m_DepthPreset = DepthPreset::DepthDisabled;
			m_ScenePipeline = m_Layout.Resolve(*services.m_PipelineResolver, m_SceneSlot, recipe, SceneInfo);
			recipe.m_PSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIProbeDepthConformancePixel);
			recipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R32Uint;
			recipe.m_Formats.m_RenderTargetFormats[1] = RHIFormat::Unknown;
			recipe.m_Formats.m_RenderTargetCount = 1;
			recipe.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
			for (size_t index = 0; index < m_ProbePipelines.size(); ++index)
			{
				recipe.m_DepthPreset = GetGIProbeDepthPreset(index != 0);
				m_ProbePipelines[index] = m_Layout.Resolve(*services.m_PipelineResolver, m_ProbeSlots[index], recipe, ProbeInfo);
			}
			return m_ScenePipeline.IsValid() && m_ProbePipelines[0].IsValid() && m_ProbePipelines[1].IsValid();
		}
	};

	GIProbeDepthConformance::GIProbeDepthConformance() noexcept : m_State(std::make_unique<State>()) {}
	GIProbeDepthConformance::~GIProbeDepthConformance() = default;

	void GIProbeDepthConformance::AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services) noexcept
	{
		const bool ready = m_State->Prepare(services);
		for (uint32_t convention = 0; convention < 2; ++convention)
		{
			const bool reversed = convention != 0;
			const auto cases = Cases(reversed);
			const auto names = reversed ?
				std::array{ "GI.Depth.Reversed.Scene", "GI.Depth.Reversed.Inputs", "GI.Depth.Reversed.Winners", "GI.Depth.Reversed.D32" } :
				std::array{ "GI.Depth.Standard.Scene", "GI.Depth.Standard.Inputs", "GI.Depth.Standard.Winners", "GI.Depth.Standard.D32" };
			RGTextureId sceneTexture;
			struct SceneData
			{
				std::array<RGTextureViewId, 2> m_Outputs;
			};
			graph.AddPass<SceneData>(reversed ? "GI.Depth.Reversed.Inputs" : "GI.Depth.Standard.Inputs",
				[&](RenderGraph::RGBuilder& builder, SceneData& data)
				{
					for (size_t index = 0; index < data.m_Outputs.size(); ++index)
					{
						auto texture = builder.CreateTexture(names[index], {
							.m_Format = index == 0 ? RHIFormat::R32Float : RHIFormat::R32Uint,
							.m_Extent = { uint32_t(CaseCount) * Tile, Tile, 1 } });
						builder.WriteInPlace(texture, RGTextureAccess::RenderTarget);
						if (index == 0)
						{
							sceneTexture = texture;
						}
						data.m_Outputs[index] = builder.CreateView<RHITextureViewType::RenderTarget>(texture);
					}
					builder.SideEffect();
				},
				[this, &frame, cases, convention, ready](RGExecuteContext& execute, SceneData& data)
				{
					auto* commands = execute.GetGraphicsCommandContext();
					if (!ready)
					{
						commands->RejectRecording();
						return;
					}
					ScopedConstantBindings<RHIGraphicsCommandContext> constants(*commands, m_State->m_Layout, m_State->m_Scene);
					if (!constants.SetPipeline(m_State->m_ScenePipeline) ||
						!constants.Bind(m_State->m_Scene, frame.m_FrameConstants.m_SceneSnapshot))
					{
						return;
					}
					const std::array attachments{
						RHIRenderingAttachment{ .m_View = execute.GetViewHandle(data.m_Outputs[0]), .m_LoadOp = RHIContentLoadOp::DontCare },
						RHIRenderingAttachment{ .m_View = execute.GetViewHandle(data.m_Outputs[1]), .m_LoadOp = RHIContentLoadOp::DontCare } };
					commands->BeginRendering({ .m_ColorAttachments = attachments });
					commands->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
					for (uint32_t index = 0; index < cases.size(); ++index)
					{
						commands->SetViewport({ float(index * Tile), 0, float(Tile), float(Tile) });
						commands->SetScissorRect({ int32_t(index * Tile), 0, int32_t((index + 1) * Tile), int32_t(Tile) });
						const auto& c = cases[index];
						const Parameters parameters{ .m_Candidates = { c.m_A, c.m_B }, .m_Scene = c.m_Scene,
							.m_Reversed = convention, .m_SceneEnabled = c.m_SceneEnabled ? 1u : 0u,
							.m_AdmissionEnabled = c.m_Admission ? 1u : 0u, .m_Case = index,
							.m_Batches = c.m_Batches ? 1u : 0u };
						if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
						{
							commands->Draw(6);
						}
					}
					commands->EndRendering();
				});
			struct ProbeData
			{
				RGTextureViewId m_Scene;
				RGTextureViewId m_Winners;
				RGTextureViewId m_Depth;
			};
			graph.AddPass<ProbeData>(reversed ? "GI.Depth.Reversed.Compare" : "GI.Depth.Standard.Compare",
				[=](RenderGraph::RGBuilder& builder, ProbeData& data)
				{
					data.m_Scene = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(sceneTexture, RGTextureAccess::Sample, RHIStage::PixelShader));
					auto winners = builder.CreateTexture(names[2], { .m_Format = RHIFormat::R32Uint,
						.m_Extent = { uint32_t(CaseCount) * Tile, Tile, 1 },
						.m_ClearValue = RHIClearValue{ .m_Format = RHIFormat::R32Uint,
							.m_Color = { 0, 0, 0, 0 } } });
					builder.WriteInPlace(winners, RGTextureAccess::RenderTarget);
					data.m_Winners = builder.CreateView<RHITextureViewType::RenderTarget>(winners);
					auto depth = builder.CreateTexture(names[3], { .m_Format = RHIFormat::D32Float,
						.m_Extent = { uint32_t(CaseCount) * Tile, Tile, 1 },
						.m_ClearValue = RHIClearValue{ .m_Format = RHIFormat::D32Float,
							.m_Depth = reversed ? 0.0f : 1.0f, .m_IsDepthStencil = true } });
					builder.WriteInPlace(depth, RGTextureAccess::DepthStencilWrite);
					data.m_Depth = builder.CreateView<RHITextureViewType::DepthStencil>(depth);
					builder.SideEffect();
				},
				[this, &frame, cases, convention, ready](RGExecuteContext& execute, ProbeData& data)
				{
					auto* commands = execute.GetGraphicsCommandContext();
					if (!ready)
					{
						commands->RejectRecording();
						return;
					}
					ScopedConstantBindings<RHIGraphicsCommandContext> constants(*commands, m_State->m_Layout, m_State->m_Scene);
					if (!constants.SetPipeline(m_State->m_ProbePipelines[convention]) ||
						!constants.Bind(m_State->m_Scene, frame.m_FrameConstants.m_SceneSnapshot))
					{
						return;
					}
					const RHIRenderingAttachment color{ .m_View = execute.GetViewHandle(data.m_Winners), .m_LoadOp = RHIContentLoadOp::DontCare };
					commands->BeginRendering({ .m_ColorAttachments = std::span<const RHIRenderingAttachment>(&color, 1),
						.m_DepthAttachment = RHIRenderingAttachment{ .m_View = execute.GetViewHandle(data.m_Depth), .m_LoadOp = RHIContentLoadOp::DontCare } });
					commands->ClearColorAttachment(0, { 0, 0, 0, 0 });
					commands->ClearDepthAttachment(convention != 0 ? 0.0f : 1.0f);
					commands->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
					for (uint32_t index = 0; index < cases.size(); ++index)
					{
						commands->SetViewport({ float(index * Tile), 0, float(Tile), float(Tile) });
						commands->SetScissorRect({ int32_t(index * Tile), 0, int32_t((index + 1) * Tile), int32_t(Tile) });
						const auto& c = cases[index];
						Parameters parameters{ .m_Candidates = { c.m_A, c.m_B }, .m_Scene = c.m_Scene,
							.m_Reversed = convention, .m_SceneEnabled = c.m_SceneEnabled ? 1u : 0u,
							.m_AdmissionEnabled = c.m_Admission ? 1u : 0u, .m_Case = index,
							.m_Batches = c.m_Batches ? 1u : 0u };
						parameters.m_SceneTexture = execute.GetViewDescriptor(data.m_Scene).m_Index;
						const uint32_t batches = c.m_Batches ? 2u : 1u;
						for (uint32_t batch = 0; batch < batches; ++batch)
						{
							// Carry global identity explicitly; draw-local instance IDs restart per batch.
							parameters.m_BaseID = batch;
							if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
							{
								commands->Draw(6, c.m_Batches ? 1u : 2u);
							}
						}
					}
					commands->EndRendering();
				});
		}
	}
}
