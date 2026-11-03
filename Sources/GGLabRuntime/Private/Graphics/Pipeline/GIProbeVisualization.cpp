#include "Graphics/Pipeline/GIProbeVisualization.h"
#include "Graphics/RenderPass/GIFieldGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace gglab
{
	GIProbeVisualizationSettings ResolveGIProbeVisualizationSettings(
		GIProbeVisualizationSettings settings) noexcept
	{
		settings.m_Radius = std::isfinite(settings.m_Radius) ?
			std::clamp(settings.m_Radius, 0.01f, 0.5f) : 0.12f;
		settings.m_Budget = std::clamp(settings.m_Budget, 1u, GIProbeVisualizationMaximumBudget);
		settings.m_SliceAxis = std::min(settings.m_SliceAxis, 3u);
		settings.m_LayerMinimum = std::min(settings.m_LayerMinimum, 65535u);
		settings.m_LayerMaximum = std::min(settings.m_LayerMaximum, 65535u);
		settings.m_ClassificationMask &= 15;
		settings.m_MaximumDistance = std::isfinite(settings.m_MaximumDistance) ?
			std::clamp(settings.m_MaximumDistance, 0.0f, 1000000.0f) : 0;
		if (settings.m_Mode != GIProbeVisualizationMode::Irradiance)
		{
			settings.m_Mode = GIProbeVisualizationMode::Classification;
		}
		settings.m_PreviewExposureEV = std::isfinite(settings.m_PreviewExposureEV) ?
			std::clamp(settings.m_PreviewExposureEV, GIProbeVisualizationMinimumExposureEV,
				GIProbeVisualizationMaximumExposureEV) : 0;
		if (settings.m_DepthMode != GIProbeDepthMode::SceneDepth)
		{
			settings.m_DepthMode = GIProbeDepthMode::XRay;
		}
		return settings;
	}

	namespace
	{
		const RenderPassInfo SelectInfo{ .m_TypeName = "GI.Probes.Select",
			.m_DisplayName = "Stable probe display selection", .m_CategoryName = "Global Illumination",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute };
		const RenderPassInfo DrawInfo{ .m_TypeName = "GI.Probes.Draw",
			.m_DisplayName = "GI probe spheres", .m_CategoryName = "Global Illumination",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Graphics };
		constexpr auto StatsFootprint = ComputeRHITextureCopyFootprint(RHIFormat::R32Uint, 16, 1);
		struct Parameters
		{
			GIFieldSamplingParameters m_Field;
			std::array<float, 16> m_ViewProjection{};
			std::array<float, 3> m_Camera{};
			float m_Radius = 0;
			uint32_t m_SliceAxis = 0;
			uint32_t m_LayerMinimum = 0;
			uint32_t m_LayerMaximum = 0;
			uint32_t m_Budget = 0;
			float m_MaximumDistance = 0;
			uint32_t m_ClassificationMask = 0;
			uint32_t m_Stats = 0;
			uint32_t m_IDs = 0;
			uint32_t m_Mode = 0;
			uint32_t m_ReversedDepth = 0;
			std::array<uint32_t, 2> m_Extent{};
			std::array<uint32_t, 4> m_Stamp{};
			uint32_t m_DisplayMode = 0;
			float m_PreviewExposureScale = 1;
			uint32_t m_DepthMode = 0;
			uint32_t m_SceneDepth = 0;
		};
		static_assert(sizeof(Parameters) == 208 && offsetof(Parameters, m_ViewProjection) == 48 &&
			offsetof(Parameters, m_Camera) == 112 && offsetof(Parameters, m_Stats) == 152 &&
			offsetof(Parameters, m_Mode) == 160 && offsetof(Parameters, m_DisplayMode) == 192 &&
			offsetof(Parameters, m_PreviewExposureScale) == 196 &&
			offsetof(Parameters, m_DepthMode) == 200 && offsetof(Parameters, m_SceneDepth) == 204);
		struct Slot
		{
			RHIFencePoint m_Fence;
			GIObservationStamp m_Observation;
			GIReceiverViewSnapshot m_View;
			GIProbeVisualizationSettings m_Settings;
		};
		// Deferred commits retain CPU values only. Native buffers are retained by
		// the recorded submission, including failures after work was submitted.
		struct Progress
		{
			std::vector<Slot> m_Slots;
			GIProbeVisualizationSnapshot m_Snapshot;
		};
		struct Resources
		{
			RHIDevice* m_Device = nullptr;
			std::vector<RHIBufferOwner> m_Readbacks;
		};
	}

	struct GIProbeVisualization::State
	{
		ResolvedRenderLayout m_Layout;
		ResolvedBindingRef<SceneGPU> m_Scene;
		ComputePipelineSlot m_SelectSlot;
		GraphicsPipelineSlot m_DrawSlot;
		ResolvedRenderPipeline m_Select;
		ResolvedRenderPipeline m_Draw;
		GIReceiverViewSnapshot m_View;
		std::shared_ptr<Progress> m_Progress = std::make_shared<Progress>();
		std::shared_ptr<Resources> m_Resources = std::make_shared<Resources>();
		void Poll() noexcept
		{
			auto& snapshot = m_Progress->m_Snapshot;
			for (size_t index = 0; index < m_Progress->m_Slots.size(); ++index)
			{
				auto& slot = m_Progress->m_Slots[index];
				if (!slot.m_Fence.IsValid())
				{
					continue;
				}
				const auto completion = m_Resources->m_Device->QueryFencePoint(slot.m_Fence);
				if (completion == RHIFenceQueryResult::Pending)
				{
					continue;
				}
				if (completion != RHIFenceQueryResult::Completed)
				{
					snapshot.m_ReadbackFailed = true;
					continue;
				}
				const void* mapped = m_Resources->m_Device->MapBuffer(
					m_Resources->m_Readbacks[index].Get(), { 0, 64 });
				if (!mapped)
				{
					snapshot.m_ReadbackFailed = true;
					continue;
				}
				std::array<uint32_t, 16> words{};
				std::memcpy(words.data(), mapped, sizeof(words));
				m_Resources->m_Device->UnmapBuffer(m_Resources->m_Readbacks[index].Get(), {});
				if (words[0] != GIProbeVisualizationMagic || words[1] != GIProbeVisualizationFilterVersion ||
					words[7] > slot.m_Settings.m_Budget || words[6] < words[7] || words[8] != words[6] - words[7] ||
					(uint64_t(words[12]) | (uint64_t(words[13]) << 32)) != slot.m_Observation.m_ProducingFrame ||
					(uint64_t(words[14]) | (uint64_t(words[15]) << 32)) != slot.m_Observation.m_FieldUpdate)
				{
					snapshot.m_ReadbackFailed = true;
				}
				else if (!snapshot.m_CompletedObservation ||
					slot.m_Observation.m_ProducingFrame > snapshot.m_CompletedObservation->m_ProducingFrame)
				{
					snapshot.m_CompletedObservation = slot.m_Observation;
					snapshot.m_CompletedView = slot.m_View;
					snapshot.m_CompletedSettings = slot.m_Settings;
					snapshot.m_CompletedCounts = { words[2], words[3], words[4], words[5], words[6], words[7], words[8] };
				}
				slot.m_Fence = {};
			}
		}
	};

	GIProbeVisualization::GIProbeVisualization() noexcept : m_State(std::make_unique<State>()) {}
	GIProbeVisualization::~GIProbeVisualization() = default;
	GIProbeVisualizationSnapshot GIProbeVisualization::GetSnapshot() const noexcept
	{
		return m_State->m_Progress->m_Snapshot;
	}

	void GIProbeVisualization::Prepare(const RenderFrameContext& frame, const RenderServices& services,
		GIProbeVisualizationSettings settings, const GIReceiverViewSnapshot& view) noexcept
	{
		auto& state = *m_State;
		state.Poll();
		auto& snapshot = state.m_Progress->m_Snapshot;
		snapshot.m_Settings = ResolveGIProbeVisualizationSettings(settings);
		state.m_View = view;
		snapshot.m_ViewSupported = IsGIProbeVisualizationViewSupported(view);
		snapshot.m_Available = false;
		snapshot.m_PrivateDepthLogicalBytes = 0;
		snapshot.m_SelectionLogicalBytes = 0;
		if (!snapshot.m_Settings.m_Enabled || !snapshot.m_ViewSupported || snapshot.m_ReadbackFailed)
		{
			return;
		}
		if (!state.m_Layout.IsValid())
		{
			const auto* common = services.m_BindingLayout->GetCommonLayoutContract();
			if (!common)
			{
				return;
			}
			state.m_Layout = ResolvedRenderLayout::Create(
				services.m_Presentation->GetRHIContext()->GetPipelineSystem(), *common, "GI.Probes");
			state.m_Scene = ResolvedBindingRef<SceneGPU>::Resolve(state.m_Layout, scene_bindings::SceneConstants);
		}
		state.m_Select = state.m_Layout.Resolve(*services.m_PipelineResolver, state.m_SelectSlot,
			ComputePipelineRecipe{ .m_BindingLayout = state.m_Layout.GetNativeLayout(),
				.m_CSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIProbeVisualizationCompute) }, SelectInfo);
		GraphicsPhysicalPipelineKey recipe;
		recipe.m_BindingLayout = state.m_Layout.GetNativeLayout();
		recipe.m_InputLayoutId = InputLayoutID::None;
		recipe.m_VSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIProbeVisualizationVertex);
		recipe.m_PSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIProbeVisualizationPixel);
		recipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		recipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		recipe.m_Formats.m_RenderTargetFormats[0] = services.m_Presentation->GetSwapChain()->GetFormat();
		recipe.m_Formats.m_RenderTargetCount = 1;
		recipe.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
		recipe.m_RasterizerPreset = RasterizerPreset::TwoSided;
		recipe.m_DepthPreset = GetGIProbeDepthPreset(view.m_ReversedDepth);
		state.m_Draw = state.m_Layout.Resolve(*services.m_PipelineResolver, state.m_DrawSlot, recipe, DrawInfo);
		if (!state.m_Select.IsValid() || !state.m_Draw.IsValid())
		{
			return;
		}
		if (state.m_Resources->m_Readbacks.empty())
		{
			auto* device = services.m_Presentation->GetDevice();
			state.m_Resources->m_Device = device;
			const uint32_t slots = services.m_Presentation->GetRHIContext()->GetFrameSlotCount();
			for (uint32_t index = 0; index < slots; ++index)
			{
				RHIBufferOwner buffer(device, device->CreateBuffer({
					.m_SizeInBytes = StatsFootprint.m_SizeInBytes, .m_Usage = RHIBufferUsage::CopyDest,
					.m_MemoryUsage = RHIMemoryUsage::GpuToCpu, .m_DebugName = "GI.Probes.Counts.Readback" }));
				if (!buffer)
				{
					snapshot.m_ReadbackFailed = true;
					return;
				}
				state.m_Resources->m_Readbacks.push_back(std::move(buffer));
			}
			state.m_Progress->m_Slots.resize(slots);
		}
		snapshot.m_ReadbackBufferBytes = state.m_Resources->m_Readbacks.size() * StatsFootprint.m_SizeInBytes;
		snapshot.m_PrivateDepthLogicalBytes = uint64_t(view.m_DisplayExtent[0]) * view.m_DisplayExtent[1] * 4;
		snapshot.m_SelectionLogicalBytes = 64 + uint64_t(snapshot.m_Settings.m_Budget) * 8;
		snapshot.m_Available = !snapshot.m_ReadbackFailed && frame.m_FrameSlotIndex < state.m_Progress->m_Slots.size();
	}

	void GIProbeVisualization::AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
		const GIObservationStamp& observation) noexcept
	{
		auto& state = *m_State;
		const auto& snapshot = state.m_Progress->m_Snapshot;
		const auto* field = graph.GetBlackboard().TryGet<RGGIFieldResources>();
		if (!field || !snapshot.m_Available || !snapshot.m_Settings.m_Enabled)
		{
			return;
		}
		const auto settings = snapshot.m_Settings;
		const auto view = state.m_View;
		const Parameters parameters{ .m_Field = field->m_Parameters, .m_ViewProjection = view.m_ViewProjection,
			.m_Camera = view.m_CameraPosition, .m_Radius = settings.m_Radius * field->m_Parameters.m_Spacing,
			.m_SliceAxis = settings.m_SliceAxis, .m_LayerMinimum = settings.m_LayerMinimum,
			.m_LayerMaximum = settings.m_LayerMaximum, .m_Budget = settings.m_Budget,
			.m_MaximumDistance = settings.m_MaximumDistance, .m_ClassificationMask = settings.m_ClassificationMask,
			.m_ReversedDepth = view.m_ReversedDepth ? 1u : 0u, .m_Extent = view.m_DisplayExtent,
			.m_Stamp = { uint32_t(observation.m_ProducingFrame), uint32_t(observation.m_ProducingFrame >> 32),
				uint32_t(observation.m_FieldUpdate), uint32_t(observation.m_FieldUpdate >> 32) },
			.m_DisplayMode = static_cast<uint32_t>(settings.m_Mode),
			.m_PreviewExposureScale = std::exp2(settings.m_PreviewExposureEV),
			.m_DepthMode = static_cast<uint32_t>(settings.m_DepthMode) };
		RGTextureId stats;
		RGTextureId ids;
		struct SelectData
		{
			RGTextureViewId m_Classification;
			RGTextureViewId m_Stats;
			RGTextureViewId m_IDs;
		};
		graph.AddPass<SelectData>(SelectInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, SelectData& data)
			{
				data.m_Classification = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(field->m_Textures[2], RGTextureAccess::Sample, RHIStage::ComputeShader),
					MakeRHITexture2DArrayViewDesc(RHIFormat::R32Uint, 0, 0, field->m_Parameters.m_Counts[1]));
				stats = builder.CreateTexture("GI.Probes.Counts", {
					.m_Format = RHIFormat::R32Uint, .m_Extent = { 16, 1, 1 } });
				ids = builder.CreateTexture("GI.Probes.RenderedIDs", {
					.m_Format = RHIFormat::R32Uint, .m_Extent = { settings.m_Budget, 2, 1 } });
				builder.WriteInPlace(stats, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				builder.WriteInPlace(ids, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Stats = builder.CreateView<RHITextureViewType::UnorderedAccess>(stats);
				data.m_IDs = builder.CreateView<RHITextureViewType::UnorderedAccess>(ids);
			},
			[this, &frame, parameters](RGExecuteContext& execute, SelectData& data)
			{
				auto& state = *m_State;
				auto* commands = execute.GetDirectComputeCommandContext();
				ScopedConstantBindings<RHIComputeCommandContext> constants(*commands, state.m_Layout, state.m_Scene);
				if (!constants.SetPipeline(state.m_Select) || !constants.Bind(state.m_Scene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					return;
				}
				auto values = parameters;
				values.m_Field.m_Classification = execute.GetViewDescriptor(data.m_Classification).m_Index;
				values.m_Stats = execute.GetViewDescriptor(data.m_Stats).m_Index;
				values.m_IDs = execute.GetViewDescriptor(data.m_IDs).m_Index;
				if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), values))
				{
					commands->Dispatch(1, 1, 1);
				}
			});
		struct DrawData
		{
			RGTextureViewId m_Irradiance;
			RGTextureViewId m_SceneDepth;
			RGTextureViewId m_Stats;
			RGTextureViewId m_IDs;
			RGTextureViewId m_Color;
			RGTextureViewId m_Depth;
		};
		graph.AddPass<DrawData>(DrawInfo.m_TypeName.c_str(),
			[&](RenderGraph::RGBuilder& builder, DrawData& data)
			{
				data.m_Stats = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(stats, RGTextureAccess::Sample, RHIStage::VertexShader));
				data.m_IDs = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(ids, RGTextureAccess::Sample, RHIStage::VertexShader));
				if (settings.m_Mode == GIProbeVisualizationMode::Irradiance)
				{
					data.m_Irradiance = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(field->m_Textures[0], RGTextureAccess::Sample, RHIStage::PixelShader),
						MakeRHITexture2DArrayViewDesc(RHIFormat::R32G32B32A32Float, 0, 0, field->m_Parameters.m_Counts[1]));
				}
				if (settings.m_DepthMode == GIProbeDepthMode::SceneDepth)
				{
					const auto& depth = builder.GetBlackboard().Get<RGDisplayDepthResources>(DisplayDepthResourcesName);
					data.m_SceneDepth = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(depth.m_Texture, RGTextureAccess::Sample, RHIStage::PixelShader), depth.m_SrvDesc);
				}
				auto& targets = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(frame.GetDisplayViewId());
				builder.ReadWriteInPlace(targets.m_BackBuffer, RGTextureAccess::RenderTarget);
				data.m_Color = builder.CreateView<RHITextureViewType::RenderTarget>(targets.m_BackBuffer);
				auto depth = builder.CreateTexture("GI.Probes.PrivateDepth", {
					.m_Format = RHIFormat::D32Float, .m_Extent = { view.m_DisplayExtent[0], view.m_DisplayExtent[1], 1 },
					.m_ClearValue = RHIClearValue{ .m_Format = RHIFormat::D32Float,
						.m_Depth = view.m_ReversedDepth ? 0.0f : 1.0f, .m_IsDepthStencil = true } });
				builder.WriteInPlace(depth, RGTextureAccess::DepthStencilWrite);
				data.m_Depth = builder.CreateView<RHITextureViewType::DepthStencil>(depth);
			},
			[this, &frame, parameters, settings](RGExecuteContext& execute, DrawData& data)
			{
				auto& state = *m_State;
				auto* commands = execute.GetGraphicsCommandContext();
				ScopedConstantBindings<RHIGraphicsCommandContext> constants(*commands, state.m_Layout, state.m_Scene);
				if (!constants.SetPipeline(state.m_Draw) || !constants.Bind(state.m_Scene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					return;
				}
				const RHIRenderingAttachment color{ .m_View = execute.GetViewHandle(data.m_Color) };
				commands->BeginRendering({ .m_ColorAttachments = std::span<const RHIRenderingAttachment>(&color, 1),
					.m_DepthAttachment = RHIRenderingAttachment{ .m_View = execute.GetViewHandle(data.m_Depth),
						.m_LoadOp = RHIContentLoadOp::DontCare } });
				commands->ClearDepthAttachment(parameters.m_ReversedDepth != 0 ? 0.0f : 1.0f);
				commands->SetViewport({ 0, 0, float(parameters.m_Extent[0]), float(parameters.m_Extent[1]) });
				commands->SetScissorRect({ 0, 0, int32_t(parameters.m_Extent[0]), int32_t(parameters.m_Extent[1]) });
				commands->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
				auto values = parameters;
				if (data.m_Irradiance.IsValid())
				{
					values.m_Field.m_Irradiance = execute.GetViewDescriptor(data.m_Irradiance).m_Index;
				}
				if (data.m_SceneDepth.IsValid())
				{
					values.m_SceneDepth = execute.GetViewDescriptor(data.m_SceneDepth).m_Index;
				}
				values.m_Stats = execute.GetViewDescriptor(data.m_Stats).m_Index;
				values.m_IDs = execute.GetViewDescriptor(data.m_IDs).m_Index;
				if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), values))
				{
					commands->Draw(16 * 8 * 6, settings.m_Budget, 0);
				}
				if (settings.m_Bounds)
				{
					values.m_Mode = 1;
					if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), values))
					{
						commands->Draw(12 * 6, 1, 0);
					}
				}
			});
		const auto slot = frame.m_FrameSlotIndex;
		const auto progress = state.m_Progress;
		const auto resources = state.m_Resources;
		// A pending slot is never overwritten, including when its last completion
		// became unknown. Drawing remains GPU-only; counts explicitly retain their stamp.
		if (progress->m_Slots[slot].m_Fence.IsValid())
		{
			return;
		}
		struct ReadbackData
		{
			RGTextureId m_Source;
			RGBufferId m_Destination;
		};
		graph.AddPass<ReadbackData>("GI.Probes.ReadCounts",
			[=](RenderGraph::RGBuilder& builder, ReadbackData& data)
			{
				data.m_Source = builder.Read(stats, RGTextureAccess::CopySource, RHIStage::Copy);
				data.m_Destination = builder.ImportBuffer("GI.Probes.Counts.Readback", resources->m_Readbacks[slot].Get(),
					{ .m_SizeInBytes = StatsFootprint.m_SizeInBytes, .m_Usage = RHIBufferUsage::CopyDest,
						.m_MemoryUsage = RHIMemoryUsage::GpuToCpu }, RGBufferAccess::CopyDest, RGContentValidity::Undefined);
				builder.WriteInPlace(data.m_Destination, RGBufferAccess::CopyDest, RHIStage::Copy);
				builder.SideEffect();
			},
			[=](RGExecuteContext& execute, ReadbackData& data)
			{
				auto* commands = execute.GetGraphicsCommandContext();
				if (!commands->RetainForSubmission(resources))
				{
					return;
				}
				commands->CopyTextureToBuffer({ .m_Source = execute.GetTextureHandle(data.m_Source),
					.m_Destination = execute.GetBufferHandle(data.m_Destination), .m_Footprint = StatsFootprint });
				execute.DeferCommit([progress, slot, observation, settings, view](const RGCommitContext& commit) noexcept
				{
					progress->m_Slots[slot] = { commit.m_Fence, observation, view, settings };
				});
			});
	}
}
