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
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

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
		if (settings.m_Mode != GIProbeVisualizationMode::Irradiance && settings.m_Mode != GIProbeVisualizationMode::Change)
		{
			settings.m_Mode = GIProbeVisualizationMode::Classification;
		}
		settings.m_PreviewExposureEV = std::isfinite(settings.m_PreviewExposureEV) ?
			std::clamp(settings.m_PreviewExposureEV, GIProbeVisualizationMinimumExposureEV,
				GIProbeVisualizationMaximumExposureEV) : 0;
		settings.m_ChangeRange = std::isfinite(settings.m_ChangeRange) ?
			std::clamp(settings.m_ChangeRange, 0.001f, 1000000.0f) : 3;
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
		const RenderPassInfo PickInfo{ .m_TypeName = "GI.Probes.Pick",
			.m_DisplayName = "One-pixel probe picking", .m_CategoryName = "Global Illumination",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Graphics };
		constexpr auto StatsFootprint = ComputeRHITextureCopyFootprint(RHIFormat::R32Uint, 16, 1);
		constexpr LayoutBindingKey ChangeInputKey{ 0x710 };
		struct ChangeUpload
		{
			RHIBufferOwner m_Buffer;
			RHIBufferDesc m_Desc;
			std::shared_ptr<const GIFieldChangeObservation> m_Observation;
		};

		std::vector<std::array<uint32_t, 4>> PackChange(const GIFieldChangeObservation& observation)
		{
			const auto& stamp = observation.m_Observation;
			const auto& data = *observation.m_Measurement;
			const auto& readiness = data.m_Readiness;
			const auto bits = [](float value) { return std::bit_cast<uint32_t>(value); };
			std::vector<std::array<uint32_t, 4>> words(8 + data.m_Probes.size() + 2 * data.m_Blocks.size());
			words[0] = { 0x47494331, 1, uint32_t(data.m_Probes.size()), uint32_t(data.m_Blocks.size()) };
			words[1] = { uint32_t(stamp.m_Field.m_Generation), uint32_t(stamp.m_Field.m_Generation >> 32),
				uint32_t(stamp.m_Field.m_RefreshEpoch), uint32_t(stamp.m_Field.m_RefreshEpoch >> 32) };
			words[2] = { uint32_t(stamp.m_ProducingFrame), uint32_t(stamp.m_ProducingFrame >> 32),
				uint32_t(stamp.m_FieldUpdate), uint32_t(stamp.m_FieldUpdate >> 32) };
			words[3] = { uint32_t(stamp.m_ReadinessUpdate), uint32_t(stamp.m_ReadinessUpdate >> 32),
				uint32_t(stamp.m_Field.m_EpochOriginUpdate), uint32_t(stamp.m_Field.m_EpochOriginUpdate >> 32) };
			words[4] = { bits(readiness.m_Threshold), bits(3 * readiness.m_Threshold),
				bits(readiness.m_IrradianceFloor), bits(readiness.m_NoiseSigma) };
			words[5] = { bits(readiness.m_Hysteresis), (data.m_RefreshMode ? 1u : 0u) | (data.m_IncludesDistance ? 2u : 0u),
				data.m_WarmingUpdates, data.m_RequiredQuietUpdates };
			words[6] = { data.m_Counts[0], data.m_Counts[1], data.m_Counts[2], data.m_BlockWidth };
			words[7] = { uint32_t(readiness.m_State), readiness.m_ActiveBlocks, readiness.m_QuietBlocks, readiness.m_ExcludedBlocks };
			for (size_t index = 0; index < data.m_Probes.size(); ++index)
			{
				const auto& probe = data.m_Probes[index];
				words[8 + index] = { bits(probe.m_Change), probe.m_Active ? 1u : 2u, 0, 0 };
			}
			for (size_t index = 0; index < data.m_Blocks.size(); ++index)
			{
				const auto& block = data.m_Blocks[index];
				words[8 + data.m_Probes.size() + 2 * index] = { block.m_ActiveProbes,
					bits(block.m_Percentile95.value_or(0)), bits(block.m_Maximum.value_or(0)), block.m_QuietUpdates };
				words[9 + data.m_Probes.size() + 2 * index] = { block.m_Blockers,
					block.m_WarmingMet ? 1u : 0u, block.m_CurrentQuiet ? 1u : 0u, 0 };
			}
			return words;
		}
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
			std::array<float, 16> m_UnjitteredViewProjection{};
			uint32_t m_SelectedID = 0;
			uint32_t m_HighlightCount = 0;
			std::array<uint32_t, 2> m_Padding{};
			uint32_t m_ChangeWords = 0;
			uint32_t m_ChangeTexture = 0;
			uint32_t m_ChangeNormalized = 0;
			float m_ChangeRange = 3;
			uint32_t m_ChangeBlockCount = 0;
			std::array<uint32_t, 3> m_ChangePadding{};
			std::array<uint32_t, 2> m_PickPixel{};
			std::array<uint32_t, 2> m_PickPadding{};
		};
		static_assert(sizeof(Parameters) == 336 && offsetof(Parameters, m_PickPixel) == 320 &&
			offsetof(Parameters, m_ChangeWords) == 288 &&
			offsetof(Parameters, m_ChangeRange) == 300 && offsetof(Parameters, m_ViewProjection) == 48 &&
			offsetof(Parameters, m_Camera) == 112 && offsetof(Parameters, m_Stats) == 152 &&
			offsetof(Parameters, m_Mode) == 160 && offsetof(Parameters, m_DisplayMode) == 192 &&
			offsetof(Parameters, m_PreviewExposureScale) == 196 &&
			offsetof(Parameters, m_DepthMode) == 200 && offsetof(Parameters, m_SceneDepth) == 204 &&
			offsetof(Parameters, m_UnjitteredViewProjection) == 208 &&
			offsetof(Parameters, m_SelectedID) == 272 && offsetof(Parameters, m_HighlightCount) == 276);
		struct Slot
		{
			RHIFencePoint m_Fence;
			GIObservationStamp m_Observation;
			GIProbeVisualizationViewSnapshot m_View;
			GIProbeVisualizationSettings m_Settings;
			uint32_t m_HighlightCount = 0;
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
		uint32_t m_ChangeInput = 0;
		std::shared_ptr<ChangeUpload> m_ChangeUpload;
		ComputePipelineSlot m_SelectSlot;
		GraphicsPipelineSlot m_DrawSlot;
		GraphicsPipelineSlot m_HighlightSlot;
		GraphicsPipelineSlot m_PickSlot;
		ResolvedRenderPipeline m_Select;
		ResolvedRenderPipeline m_Draw;
		ResolvedRenderPipeline m_Highlight;
		ResolvedRenderPipeline m_Pick;
		std::optional<GIProbePickingRequest> m_Picking;
		uint32_t m_SelectedID = 0;
		GIProbeVisualizationViewSnapshot m_View;
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
					words[9] != slot.m_HighlightCount ||
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
					snapshot.m_CompletedCounts = { words[2], words[3], words[4], words[5], words[6], words[7], words[8], words[9] };
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

	void GIProbeVisualization::SetPicking(std::optional<GIProbePickingRequest> request) noexcept
	{
		m_State->m_Picking = std::move(request);
	}

	void GIProbeVisualization::Prepare(const RenderFrameContext& frame, const RenderServices& services,
		GIProbeVisualizationSettings settings, uint64_t generation,
		const std::array<uint32_t, 3>& counts, std::shared_ptr<const GIFieldChangeObservation> change) noexcept
	{
		auto& state = *m_State;
		state.Poll();
		auto& snapshot = state.m_Progress->m_Snapshot;
		const auto resolvedSettings = ResolveGIProbeVisualizationSettings(settings);
		if (GIProbePickSettings(snapshot.m_Settings) != GIProbePickSettings(resolvedSettings))
		{
			++snapshot.m_SettingsRevision;
		}
		snapshot.m_Settings = resolvedSettings;
		snapshot.m_ChangeObservation = change;
		snapshot.m_ChangeUploadBytes = 0;
		const auto& raster = frame.GetDisplayRenderView();
		const auto& plan = frame.GetTemporalFramePlan();
		const auto& temporal = frame.GetDisplayViewRenderSettings().m_TemporalAA;
		const bool taa = plan.IsConsumerActive(TemporalConsumer::TemporalAA);
		const bool jittered = taa && raster.GetResolution().IsNative() &&
			temporal.m_PostTemporalView == TemporalAAPostTemporalView::Jittered;
		GIProbeVisualizationViewSnapshot next{
			.m_ViewId = static_cast<uint32_t>(raster.m_ViewId),
			.m_RenderExtent = { raster.m_Width, raster.m_Height },
			.m_DisplayExtent = { raster.m_DisplayWidth, raster.m_DisplayHeight },
			.m_CameraPosition = { raster.m_CameraPosition.m_X, raster.m_CameraPosition.m_Y, raster.m_CameraPosition.m_Z },
			.m_View = raster.m_View.ToArray(),
			.m_UnjitteredProjection = raster.m_UnjitteredProj.ToArray(),
			.m_UnjitteredViewProjection = raster.m_UnjitteredViewProj.ToArray(),
			.m_Projection = (jittered ? raster.m_RasterProj : raster.m_UnjitteredProj).ToArray(),
			.m_ViewProjection = (jittered ? raster.m_RasterViewProj : raster.m_UnjitteredViewProj).ToArray(),
			.m_JitterPixels = { raster.m_JitterPixels.m_X, raster.m_JitterPixels.m_Y },
			.m_NearFar = { raster.m_Near, raster.m_Far },
			.m_TemporalFrameIndex = raster.m_TemporalFrameIndex,
			.m_ResolutionPreset = taa ? temporal.m_ResolutionPreset : TemporalAAResolutionPreset::Native,
			.m_ReversedDepth = raster.m_DepthConvention == DepthConvention::Reversed,
			.m_TemporalAA = taa,
			.m_PostTemporalJittered = jittered,
			.m_ProjectionSupported = !taa || plan.GetProjectionJitterOwner() == TemporalConsumer::TemporalAA,
			.m_Valid = raster.m_IsValid };
		state.m_View = AdvanceGIProbeVisualizationView(state.m_View, next);
		const auto& view = state.m_View;
		snapshot.m_View = view;
		snapshot.m_ViewSupported = IsGIProbeVisualizationViewSupported(view);
		snapshot.m_Available = false;
		snapshot.m_PickingAvailable = false;
		snapshot.m_HighlightAvailable = false;
		snapshot.m_HighlightCount = 0;
		snapshot.m_HighlightState = ResolveGIProbeHighlightState(
			snapshot.m_Settings, generation, counts, false, false);
		snapshot.m_PrivateDepthLogicalBytes = 0;
		snapshot.m_SelectionLogicalBytes = 0;
		if (!snapshot.m_Settings.m_Enabled || !snapshot.m_ViewSupported || snapshot.m_ReadbackFailed)
		{
			state.m_ChangeUpload.reset();
			return;
		}
		if (snapshot.m_Settings.m_Mode == GIProbeVisualizationMode::Change && !change)
		{
			state.m_ChangeUpload.reset();
			return;
		}
		const auto uploadObservation = snapshot.m_Settings.m_Mode == GIProbeVisualizationMode::Change ? change : nullptr;
		if (!state.m_ChangeUpload || state.m_ChangeUpload->m_Observation != uploadObservation)
		{
			auto* device = services.m_Presentation->GetDevice();
			auto upload = std::make_shared<ChangeUpload>();
			upload->m_Observation = uploadObservation;
			const auto words = uploadObservation ? PackChange(*uploadObservation) : std::vector<std::array<uint32_t, 4>>(1);
			upload->m_Desc = { .m_SizeInBytes = words.size() * 16, .m_StrideInBytes = 16,
				.m_Usage = RHIBufferUsage::Structured, .m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
				.m_DebugName = "GI.Probes.CompletedChange" };
			upload->m_Buffer = RHIBufferOwner(device, device->CreateBuffer(upload->m_Desc));
			void* mapped = upload->m_Buffer ? device->MapBuffer(upload->m_Buffer.Get(), {}) : nullptr;
			if (!mapped)
			{
				state.m_ChangeUpload.reset();
				return;
			}
			std::memcpy(mapped, words.data(), upload->m_Desc.m_SizeInBytes);
			device->UnmapBuffer(upload->m_Buffer.Get(), { 0, upload->m_Desc.m_SizeInBytes });
			state.m_ChangeUpload = std::move(upload);
		}
		snapshot.m_ChangeUploadBytes = state.m_ChangeUpload->m_Desc.m_SizeInBytes;
		if (!state.m_Layout.IsValid())
		{
			const auto* common = services.m_BindingLayout->GetCommonLayoutContract();
			if (!common)
			{
				return;
			}
			std::vector<LayoutBindingRequirement> bindings(common->GetBindings().begin(), common->GetBindings().end());
			uint32_t order = 0;
			for (const auto& binding : bindings)
			{
				order = std::max(order, binding.m_ParameterOrder + 1);
			}
			bindings.push_back({ .m_Key = ChangeInputKey,
				.m_Slot = { .m_Type = RHIBindingType::ReadOnlyStorageBuffer,
					.m_Visibility = RHIShaderStage::All, .m_Binding = 11, .m_Count = 1 }, .m_ParameterOrder = order });
			const std::array fragments{ LayoutFragment{ "GIProbeVisualization", bindings } };
			const auto composed = ComposeLayoutFragments(fragments);
			if (!composed.IsSuccess())
			{
				return;
			}
			state.m_ChangeInput = *composed.m_Layout->FindParameterIndex(ChangeInputKey);
			state.m_Layout = ResolvedRenderLayout::Create(
				services.m_Presentation->GetRHIContext()->GetPipelineSystem(), *composed.m_Layout, "GI.Probes");
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
		recipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R32Uint;
		recipe.m_PSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIProbePickingPixel);
		state.m_Pick = state.m_Layout.Resolve(*services.m_PipelineResolver, state.m_PickSlot, recipe, PickInfo);
		snapshot.m_PickingAvailable = state.m_Pick.IsValid();
		recipe.m_Formats.m_RenderTargetFormats[0] = services.m_Presentation->GetSwapChain()->GetFormat();
		recipe.m_PSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIProbeVisualizationPixel);
		recipe.m_DepthPreset = DepthPreset::DepthDisabled;
		state.m_Highlight = state.m_Layout.Resolve(*services.m_PipelineResolver,
			state.m_HighlightSlot, recipe, DrawInfo);
		snapshot.m_HighlightAvailable = state.m_Highlight.IsValid();
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
			uint64_t allocatedBytes = 0;
			bool allocationKnown = true;
			std::vector<uint64_t> allocations;
			for (const auto& buffer : state.m_Resources->m_Readbacks)
			{
				const auto facts = device->QueryBufferAllocationFacts(buffer.Get());
				allocationKnown &= facts.m_Status == RHIAllocationFactsStatus::Known;
				if (facts.m_Status == RHIAllocationFactsStatus::Known &&
					std::ranges::find(allocations, facts.m_NativeAllocationId) == allocations.end())
				{
					allocations.push_back(facts.m_NativeAllocationId);
					allocatedBytes += facts.m_AllocatedBytes;
				}
			}
			if (allocationKnown)
			{
				snapshot.m_ReadbackAllocatedBytes = allocatedBytes;
			}
		}
		snapshot.m_ReadbackBufferBytes = state.m_Resources->m_Readbacks.size() * StatsFootprint.m_SizeInBytes;
		snapshot.m_PrivateDepthLogicalBytes = uint64_t(view.m_DisplayExtent[0]) * view.m_DisplayExtent[1] * 4;
		snapshot.m_SelectionLogicalBytes = 64 + uint64_t(snapshot.m_Settings.m_Budget) * 8;
		snapshot.m_Available = !snapshot.m_ReadbackFailed && frame.m_FrameSlotIndex < state.m_Progress->m_Slots.size();
		snapshot.m_HighlightState = ResolveGIProbeHighlightState(snapshot.m_Settings, generation, counts,
			snapshot.m_Available, snapshot.m_HighlightAvailable);
		if (snapshot.m_HighlightState == GIProbeHighlightState::Ready)
		{
			const auto coordinate = snapshot.m_Settings.m_Selection->m_Coordinate;
			state.m_SelectedID = coordinate[0] + counts[0] * (coordinate[2] + counts[2] * coordinate[1]);
			snapshot.m_HighlightCount = 1;
		}
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
			.m_DepthMode = static_cast<uint32_t>(settings.m_DepthMode),
			.m_UnjitteredViewProjection = view.m_UnjitteredViewProjection,
			.m_SelectedID = state.m_SelectedID, .m_HighlightCount = snapshot.m_HighlightCount,
			.m_ChangeWords = uint32_t(state.m_ChangeUpload->m_Desc.m_SizeInBytes / 4),
			.m_ChangeNormalized = settings.m_ChangeNormalized ? 1u : 0u, .m_ChangeRange = settings.m_ChangeRange,
			.m_ChangeBlockCount = settings.m_Mode == GIProbeVisualizationMode::Change && settings.m_ChangeBlocks ?
				uint32_t(snapshot.m_ChangeObservation->m_Measurement->m_Blocks.size()) : 0u };
		const auto upload = state.m_ChangeUpload;
		RGBufferId changeBuffer;
		RGTextureId stats;
		RGTextureId ids;
		// Each immutable upload is retained by every submission that reads it. A later
		// completion allocates a new buffer rather than overwriting in-flight storage.
		struct ChangeData
		{
			RGBufferId m_Input;
			RGTextureViewId m_Output;
		};
		graph.AddPass<ChangeData>("GI.Probes.CompletedChange", RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, ChangeData& data)
			{
				changeBuffer = builder.ImportBuffer("GI.Probes.CompletedChange.Upload", upload->m_Buffer.Get(),
					upload->m_Desc, RGBufferAccess::StructuredRead, RGContentValidity::Defined);
				data.m_Input = builder.Read(changeBuffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
				if (settings.m_Mode == GIProbeVisualizationMode::Change)
				{
					auto output = builder.CreateTexture("GI.Probes.Change", { .m_Format = RHIFormat::R32Uint,
						.m_Extent = { 256, (parameters.m_ChangeWords + 255) / 256, 1 } });
					builder.WriteInPlace(output, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_Output = builder.CreateView<RHITextureViewType::UnorderedAccess>(output);
					// Keep the named completed-observation product available to late capture
					// extraction even when the heatmap reads the immutable upload directly.
					builder.SideEffect();
				}
			},
			[this, &frame, upload, parameters](RGExecuteContext& execute, ChangeData& data)
			{
				if (!data.m_Output.IsValid())
				{
					return;
				}
				auto& state = *m_State;
				auto* commands = execute.GetDirectComputeCommandContext();
				ScopedConstantBindings<RHIComputeCommandContext> constants(*commands, state.m_Layout, state.m_Scene);
				if (!commands->RetainForSubmission(upload) || !constants.SetPipeline(state.m_Select) ||
					!constants.Bind(state.m_Scene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					return;
				}
				commands->SetReadOnlyBuffer(state.m_ChangeInput, execute.GetBufferHandle(data.m_Input));
				auto values = parameters;
				values.m_Mode = 4;
				values.m_ChangeTexture = execute.GetViewDescriptor(data.m_Output).m_Index;
				if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), values))
				{
					commands->Dispatch(((parameters.m_ChangeWords + 255) / 256) * 4, 1, 1);
				}
			});
		struct SelectData
		{
			RGBufferId m_Change;
			RGTextureViewId m_Classification;
			RGTextureViewId m_Stats;
			RGTextureViewId m_IDs;
		};
		graph.AddPass<SelectData>(SelectInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, SelectData& data)
			{
				data.m_Change = builder.Read(changeBuffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
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
			[this, &frame, parameters, upload](RGExecuteContext& execute, SelectData& data)
			{
				auto& state = *m_State;
				auto* commands = execute.GetDirectComputeCommandContext();
				ScopedConstantBindings<RHIComputeCommandContext> constants(*commands, state.m_Layout, state.m_Scene);
				if (!commands->RetainForSubmission(upload) || !constants.SetPipeline(state.m_Select) || !constants.Bind(state.m_Scene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					return;
				}
				auto values = parameters;
				commands->SetReadOnlyBuffer(state.m_ChangeInput, execute.GetBufferHandle(data.m_Change));
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
			RGBufferId m_Change;
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
				data.m_Change = builder.Read(changeBuffer, RGBufferAccess::StructuredRead, RHIStage::VertexShader | RHIStage::PixelShader);
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
			[this, &frame, parameters, settings, upload](RGExecuteContext& execute, DrawData& data)
			{
				auto& state = *m_State;
				auto* commands = execute.GetGraphicsCommandContext();
				ScopedConstantBindings<RHIGraphicsCommandContext> constants(*commands, state.m_Layout, state.m_Scene);
				if (!commands->RetainForSubmission(upload) || !constants.SetPipeline(state.m_Draw) || !constants.Bind(state.m_Scene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					return;
				}
				const RHIRenderingAttachment color{ .m_View = execute.GetViewHandle(data.m_Color) };
				commands->SetReadOnlyBuffer(state.m_ChangeInput, execute.GetBufferHandle(data.m_Change));
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
				if (parameters.m_HighlightCount != 0 && constants.SetPipeline(state.m_Highlight) &&
					constants.Bind(state.m_Scene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					values.m_Mode = 2;
					commands->SetReadOnlyBuffer(state.m_ChangeInput, execute.GetBufferHandle(data.m_Change));
					if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), values))
					{
						commands->Draw(16 * 8 * 6, 1, 0);
					}
				}
				if (parameters.m_ChangeBlockCount != 0 && constants.SetPipeline(state.m_Highlight) &&
					constants.Bind(state.m_Scene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					commands->SetReadOnlyBuffer(state.m_ChangeInput, execute.GetBufferHandle(data.m_Change));
					values.m_Mode = 3;
					if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), values))
					{
						commands->Draw(12 * 6, parameters.m_ChangeBlockCount, 0);
					}
				}
			});
		const auto slot = frame.m_FrameSlotIndex;
		const GIFieldSummary pickingSummary{ .m_Field = observation.m_Field, .m_Visualization = snapshot };
		if (state.m_Picking && state.m_Picking->m_SourceFrame < observation.m_ProducingFrame &&
			CheckGIProbePickingBinding(*state.m_Picking, pickingSummary, state.m_Picking->m_Mapping) ==
				GIProbePickingBinding::Matching)
		{
			const auto request = *state.m_Picking;
			struct PickData
			{
				RGBufferId m_Change;
				RGTextureViewId m_Stats;
				RGTextureViewId m_IDs;
				RGTextureViewId m_SceneDepth;
				RGTextureViewId m_Color;
				RGTextureViewId m_Depth;
			};
			graph.AddPass<PickData>(PickInfo.m_TypeName.c_str(),
				[&](RenderGraph::RGBuilder& builder, PickData& data)
				{
					data.m_Change = builder.Read(changeBuffer, RGBufferAccess::StructuredRead, RHIStage::VertexShader);
					data.m_Stats = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(stats, RGTextureAccess::Sample, RHIStage::VertexShader));
					data.m_IDs = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(ids, RGTextureAccess::Sample, RHIStage::VertexShader));
					if (settings.m_DepthMode == GIProbeDepthMode::SceneDepth)
					{
						const auto& depth = builder.GetBlackboard().Get<RGDisplayDepthResources>(DisplayDepthResourcesName);
						data.m_SceneDepth = builder.CreateView<RHITextureViewType::ShaderResource>(
							builder.Read(depth.m_Texture, RGTextureAccess::Sample, RHIStage::PixelShader), depth.m_SrvDesc);
					}
					auto color = builder.CreateTexture("GI.Probes.PickID", {
						.m_Format = RHIFormat::R32Uint, .m_Extent = { 1, 1, 1 },
						.m_ClearValue = RHIClearValue{ .m_Format = RHIFormat::R32Uint,
							.m_Color = { 0, 0, 0, 0 } } });
					auto depth = builder.CreateTexture("GI.Probes.PickDepth", {
						.m_Format = RHIFormat::D32Float, .m_Extent = { 1, 1, 1 },
						.m_ClearValue = RHIClearValue{ .m_Format = RHIFormat::D32Float,
							.m_Depth = view.m_ReversedDepth ? 0.0f : 1.0f, .m_IsDepthStencil = true } });
					builder.WriteInPlace(color, RGTextureAccess::RenderTarget);
					builder.WriteInPlace(depth, RGTextureAccess::DepthStencilWrite);
					data.m_Color = builder.CreateView<RHITextureViewType::RenderTarget>(color);
					data.m_Depth = builder.CreateView<RHITextureViewType::DepthStencil>(depth);
					builder.SideEffect();
				},
				[this, &frame, upload, parameters, request](RGExecuteContext& execute, PickData& data)
				{
					auto& state = *m_State;
					auto* commands = execute.GetGraphicsCommandContext();
					ScopedConstantBindings<RHIGraphicsCommandContext> constants(*commands, state.m_Layout, state.m_Scene);
					if (!commands->RetainForSubmission(upload) || !constants.SetPipeline(state.m_Pick) ||
						!constants.Bind(state.m_Scene, frame.m_FrameConstants.m_SceneSnapshot))
					{
						return;
					}
					commands->SetReadOnlyBuffer(state.m_ChangeInput, execute.GetBufferHandle(data.m_Change));
					const RHIRenderingAttachment color{ .m_View = execute.GetViewHandle(data.m_Color), .m_LoadOp = RHIContentLoadOp::DontCare };
					commands->BeginRendering({ .m_ColorAttachments = std::span(&color, 1),
						.m_DepthAttachment = RHIRenderingAttachment{ .m_View = execute.GetViewHandle(data.m_Depth),
							.m_LoadOp = RHIContentLoadOp::DontCare } });
					// Zero is the no-hit encoding; stable probe IDs are encoded as ID + 1.
					commands->ClearColorAttachment(0, { 0, 0, 0, 0 });
					commands->ClearDepthAttachment(parameters.m_ReversedDepth ? 0.0f : 1.0f);
					commands->SetViewport({ 0, 0, 1, 1 });
					commands->SetScissorRect({ 0, 0, 1, 1 });
					commands->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
					auto values = parameters;
					values.m_Mode = 5;
					values.m_PickPixel = request.m_Pixel;
					values.m_Stats = execute.GetViewDescriptor(data.m_Stats).m_Index;
					values.m_IDs = execute.GetViewDescriptor(data.m_IDs).m_Index;
					if (data.m_SceneDepth.IsValid())
					{
						values.m_SceneDepth = execute.GetViewDescriptor(data.m_SceneDepth).m_Index;
					}
					if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), values))
					{
						commands->Draw(16 * 8 * 6, request.m_Settings.m_Budget, 0);
					}
				});
		}
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
				execute.DeferCommit([progress, slot, observation, settings, view, parameters](const RGCommitContext& commit) noexcept
				{
					progress->m_Slots[slot] = { commit.m_Fence, observation, view, settings, parameters.m_HighlightCount };
				});
			});
	}
}
