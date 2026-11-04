#include "GGLabRuntime/Graphics/Pipeline/GIField.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "Graphics/Pipeline/GIFieldState.h"
#include "Graphics/Pipeline/GIProbeVisualization.h"
#include "Graphics/Pipeline/GIProbeUpdateWorkload.h"
#include "Graphics/RenderPass/GIFieldGraphResources.h"
#include "Graphics/RenderPass/GIReceiverGraphResources.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace gglab
{
	namespace
	{
		std::atomic<uint64_t> NextGeneration{ 1 };
		const RenderPassInfo LookupPassInfo{ .m_TypeName = "GI.Field.Lookup",
			.m_DisplayName = "Field receiver query diagnostics", .m_CategoryName = "Global Illumination",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute };
		const RenderPassInfo InspectionPassInfo{ .m_TypeName = "GI.Field.InspectProbe",
			.m_DisplayName = "Inspect one field probe", .m_CategoryName = "Global Illumination",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute };
		struct InspectionParameters
		{
			GIFieldSamplingParameters m_Field;
			std::array<uint32_t, 3> m_Coordinate{};
			uint32_t m_Output = 0;
		};
		static_assert(sizeof(InspectionParameters) == 64 && offsetof(InspectionParameters, m_Output) == 60);
		const RenderPassInfo ReceiverInspectionPassInfo{ .m_TypeName = "GI.Field.InspectReceiver",
			.m_DisplayName = "Inspect one receiver contribution", .m_CategoryName = "Global Illumination",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute };
		const RenderPassInfo ReceiverMarkersPassInfo{ .m_TypeName = "GI.Field.ReceiverMarkers",
			.m_DisplayName = "Receiver contribution X-ray markers", .m_CategoryName = "Global Illumination",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Graphics };
		struct ReceiverInspectionParameters
		{
			GIFieldSamplingParameters m_Field;
			uint32_t m_Position = 0;
			uint32_t m_VertexNormal = 0;
			uint32_t m_ShadingNormal = 0;
			uint32_t m_Output = 0;
			std::array<uint32_t, 2> m_Pixel{};
			std::array<uint32_t, 2> m_Padding{};
			std::array<float, 3> m_Camera{};
			uint32_t m_CameraPadding = 0;
		};
		static_assert(sizeof(ReceiverInspectionParameters) == 96 &&
			offsetof(ReceiverInspectionParameters, m_Pixel) == 64 && offsetof(ReceiverInspectionParameters, m_Camera) == 80);
		struct ReceiverMarkerParameters
		{
			std::array<float, 16> m_ViewProjection{};
			uint32_t m_Records = 0;
			std::array<uint32_t, 2> m_Extent{};
			uint32_t m_Padding = 0;
		};
		static_assert(sizeof(ReceiverMarkerParameters) == 80 && offsetof(ReceiverMarkerParameters, m_Records) == 64);
		struct ReceiverInspectionResources
		{
			RGTextureId m_Records;
		};
		struct LookupParameters
		{
			GIFieldSamplingParameters m_Field;
			uint32_t m_Position = 0;
			uint32_t m_VertexNormal = 0;
			uint32_t m_ShadingNormal = 0;
			uint32_t m_Irradiance = 0;
			uint32_t m_Query = 0;
			std::array<uint32_t, 2> m_Extent{};
			uint32_t m_Padding = 0;
			std::array<float, 3> m_Camera{};
			uint32_t m_CameraPadding = 0;
		};
		static_assert(sizeof(LookupParameters) == 96 && offsetof(LookupParameters, m_Camera) == 80);

		GIProbeUpdateProfile UpdateProfile(const GIFieldCreateInfo& info) noexcept
		{
			GIProbeUpdateProfile result;
			result.m_Counts = info.m_Volume.m_Counts;
			result.m_Minimum = info.m_Volume.m_Minimum;
			result.m_Spacing = info.m_Volume.m_Spacing;
			result.m_IrradianceInteriorWidth = info.m_IrradianceInteriorWidth;
			result.m_RaysPerProbe = info.m_RaysPerProbe;
			result.m_Seed = info.m_Seed;
			result.m_MaximumDistance = info.m_MaximumDistance;
			result.m_Hysteresis = info.m_Hysteresis;
			result.m_MaximumUpdates = 0;
			result.m_ConstantEnvironment = info.m_ConstantEnvironment;
			result.m_ConstantRadiance = info.m_ConstantRadiance;
			return result;
		}
	}

	bool ValidateGIFieldCreateInfo(const GIFieldCreateInfo& info) noexcept
	{
		if (!info.m_RayScene || !ValidateGIProbeUpdateProfile(UpdateProfile(info)) ||
			!std::isfinite(info.m_ViewBias) || info.m_ViewBias < 0 || info.m_ViewBias > 1 ||
			!std::isfinite(info.m_Readiness.m_NoiseSigma) || info.m_Readiness.m_NoiseSigma < 0 ||
			!std::isfinite(3 * info.m_Readiness.m_NoiseSigma) ||
			!std::isfinite(3 * info.m_Readiness.m_SteadyNoiseSigma) || info.m_Readiness.m_SteadyNoiseSigma < 0 ||
			!std::isfinite(info.m_Readiness.m_RefreshHysteresis) ||
			info.m_Readiness.m_RefreshHysteresis < 0 || info.m_Readiness.m_RefreshHysteresis >= 1)
		{
			return false;
		}
		for (size_t axis = 0; axis < 3; ++axis)
		{
			const float extent = float(info.m_Volume.m_Counts[axis] - 1) * info.m_Volume.m_Spacing;
			if (info.m_Volume.m_Counts[axis] < 2 || !std::isfinite(extent) ||
				!std::isfinite(info.m_Volume.m_Minimum[axis] + extent) ||
				info.m_Volume.m_Minimum[axis] + info.m_Volume.m_Spacing <= info.m_Volume.m_Minimum[axis])
			{
				return false;
			}
		}
		return true;
	}

	struct GIField::State
	{
		GIFieldCreateInfo m_Info;
		GIProbeVisualization m_Visualization;
		GIProbeUpdateWorkload m_Workload;
		std::shared_ptr<GIFieldUpdateState> m_Update = std::make_shared<GIFieldUpdateState>();
		uint64_t m_Generation = NextGeneration.fetch_add(1, std::memory_order_relaxed);
		uint64_t m_SceneRevision = 0;
		uint64_t m_LightingGeneration = 0;
		uint64_t m_EnvironmentGeneration = 0;
		std::array<uint64_t, GIProbeUpdateWorkload::ProgramCount> m_ShaderGenerations{};
		bool m_Sealed = false;
		bool m_UpdatePrepared = false;
		ResolvedRenderLayout m_LookupLayout;
		ResolvedBindingRef<SceneGPU> m_LookupScene;
		ComputePipelineSlot m_LookupSlot;
		ResolvedRenderPipeline m_LookupPipeline;
		uint64_t m_LookupGeneration = 0;
		std::shared_ptr<const GIFieldSummary> m_Summary;
		std::optional<GIProbeInspectionRequest> m_Inspection;
		ComputePipelineSlot m_InspectionSlot;
		ResolvedRenderPipeline m_InspectionPipeline;
		GIReceiverViewSnapshot m_ReceiverView;
		std::optional<GIReceiverInspectionRequest> m_ReceiverInspection;
		ComputePipelineSlot m_ReceiverInspectionSlot;
		ResolvedRenderPipeline m_ReceiverInspectionPipeline;
		GraphicsPipelineSlot m_ReceiverMarkerSlot;
		ResolvedRenderPipeline m_ReceiverMarkerPipeline;
		State(GIFieldCreateInfo info) noexcept :
			m_Info(std::move(info)), m_Workload(m_Info.m_RayScene, UpdateProfile(m_Info), true,
				m_Info.m_Readiness, m_Info.m_EnableFeedback) {}
	};

	GIField::GIField(GIFieldCreateInfo info) noexcept :
		m_State(std::make_unique<State>(std::move(info)))
	{
		auto summary = std::make_shared<GIFieldSummary>();
		summary->m_Field.m_Generation = m_State->m_Generation;
		summary->m_Field.m_RefreshEpoch = 1;
		summary->m_Counts = m_State->m_Info.m_Volume.m_Counts;
		summary->m_Minimum = m_State->m_Info.m_Volume.m_Minimum;
		summary->m_Spacing = m_State->m_Info.m_Volume.m_Spacing;
		summary->m_IrradianceInteriorWidth = m_State->m_Info.m_IrradianceInteriorWidth;
		summary->m_Readiness = m_State->m_Workload.GetReadiness();
		m_State->m_Summary = std::move(summary);
	}
	GIField::~GIField() = default;
	std::shared_ptr<RayScene> GIField::GetRayScene() const noexcept { return m_State->m_Info.m_RayScene; }

	GIFieldDiagnostics GIField::GetDiagnostics(bool allocations) const noexcept
	{
		const auto& state = *m_State;
		const auto update = state.m_Workload.GetDiagnostics(allocations);
		GIFieldDiagnostics result{
			.m_Generation = state.m_Generation,
			.m_SubmittedUpdates = update.m_SubmittedUpdates,
			.m_CompletedUpdates = update.m_CompletedUpdates,
			.m_SceneRevision = state.m_SceneRevision,
			.m_LightingGeneration = state.m_LightingGeneration,
			.m_EnvironmentGeneration = state.m_EnvironmentGeneration,
			.m_FieldLogicalBytes = update.m_FieldLogicalBytes,
			.m_RayCapacityBytes = update.m_RayCapacityBytes,
			.m_ProbeCount = update.m_ProbeCount,
			.m_StatusReadbackSlots = update.m_StatusReadbackSlots,
			.m_UpdateStatus = update.m_UpdateStatus,
			.m_Readiness = state.m_Workload.GetReadiness(),
			.m_Feedback = state.m_Workload.GetFeedback(),
			.m_Initialized = update.m_Initialized,
			.m_Invalid = state.m_Update->IsInvalid() || update.m_Failed,
			.m_Reason = state.m_Update->IsInvalid() ? "GI field submission failed or completion is unknown" : update.m_Reason,
		};
		result.m_Readiness.m_Failed = result.m_Readiness.m_Failed || result.m_Invalid;
		for (const auto& allocation : update.m_Allocations)
		{
			result.m_Allocations.push_back({ allocation.m_LogicalBytes, allocation.m_Allocation });
		}
		return result;
	}

	GIFieldCaptureState GIField::GetCaptureState() const noexcept
	{
		const auto diagnostics = GetDiagnostics();
		return { .m_Generation = diagnostics.m_Generation,
			.m_SceneRevision = diagnostics.m_SceneRevision,
			.m_LightingGeneration = diagnostics.m_LightingGeneration,
			.m_EnvironmentGeneration = diagnostics.m_EnvironmentGeneration,
			.m_Readiness = diagnostics.m_Readiness, .m_Summary = m_State->m_Summary };
	}

	RenderFrameValidationResult GIField::Prepare(const RenderFrameContext& frame,
		const RenderServices& services, bool receiversAvailable) noexcept
	{
		auto& state = *m_State;
		state.m_UpdatePrepared = false;
		// Publish at the owning frame boundary, including failure exits. Readers never
		// pump GPU completions or attach new identities to an earlier measurement.
		struct PublishSummary
		{
			State& m_State;
			const RenderFrameContext& m_Frame;
			const RenderServices& m_Services;
			uint64_t m_FrameSerial = 0;
			~PublishSummary()
			{
				auto& state = m_State;
				auto summary = std::make_shared<GIFieldSummary>();
				const auto diagnostics = state.m_Workload.GetDiagnostics();
				summary->m_Field = { state.m_Generation, state.m_SceneRevision,
					state.m_LightingGeneration, state.m_EnvironmentGeneration,
					state.m_Workload.GetRefreshEpoch(), state.m_Workload.GetEpochOriginUpdate() };
				summary->m_Counts = state.m_Info.m_Volume.m_Counts;
				summary->m_Minimum = state.m_Info.m_Volume.m_Minimum;
				summary->m_Spacing = state.m_Info.m_Volume.m_Spacing;
				summary->m_IrradianceInteriorWidth = state.m_Info.m_IrradianceInteriorWidth;
				summary->m_SubmittedUpdates = diagnostics.m_SubmittedUpdates;
				summary->m_Readiness = state.m_Workload.GetReadiness();
				summary->m_Invalid = diagnostics.m_Failed || state.m_Update->IsInvalid();
				auto change = state.m_Workload.GetChangeObservation();
				if (change && change->m_Observation.m_Field == summary->m_Field && !summary->m_Invalid)
				{
					summary->m_ChangeObservation = std::move(change);
				}
				const auto displayIndex = static_cast<size_t>(m_Frame.GetDisplayViewId());
				if (state.m_UpdatePrepared && displayIndex < m_Frame.m_RenderViews.size() &&
					displayIndex < m_Frame.m_ViewRenderSettings.size())
				{
					state.m_Visualization.Prepare(m_Frame, m_Services,
						m_Frame.GetDisplayViewRenderSettings().m_GIProbes, state.m_Generation,
						state.m_Info.m_Volume.m_Counts, summary->m_ChangeObservation);
				}
				summary->m_Initialized = diagnostics.m_Initialized;
				summary->m_InspectionAvailable = state.m_UpdatePrepared;
				summary->m_ReceiverView = state.m_ReceiverView;
				summary->m_Visualization = state.m_Visualization.GetSnapshot();
				summary->m_Visualization.m_Available &= state.m_UpdatePrepared;
				if (!summary->m_Visualization.m_Available)
				{
					summary->m_Visualization.m_HighlightCount = 0;
					if (summary->m_Visualization.m_HighlightState == GIProbeHighlightState::Ready)
					{
						summary->m_Visualization.m_HighlightState = GIProbeHighlightState::Unavailable;
					}
				}
				if (summary->m_Visualization.m_Available)
				{
					const uint64_t update = summary->m_SubmittedUpdates + (summary->m_Invalid ? 0 : 1);
					summary->m_Visualization.m_Observation = GIObservationStamp{
						.m_Field = summary->m_Field, .m_Kind = GIObservationKind::ProbeVisualization,
						.m_ProducingFrame = m_FrameSerial, .m_FieldUpdate = update,
						.m_ReadinessUpdate = update - summary->m_Field.m_EpochOriginUpdate };
				}
				summary->m_ReceiverInspectionAvailable = state.m_UpdatePrepared &&
					state.m_ReceiverView.m_Supported && state.m_ReceiverInspectionPipeline.IsValid();
				summary->m_ReceiverMarkersAvailable = summary->m_ReceiverInspectionAvailable &&
					state.m_ReceiverMarkerPipeline.IsValid();
				const auto completed = state.m_Workload.GetCompletedObservation();
				if (completed && completed->m_Field == summary->m_Field && !summary->m_Invalid)
				{
					summary->m_CompletedMeasurement = completed;
				}
				state.m_Summary = std::move(summary);
			}
		} publication{ state, frame, services, frame.m_FrameSerial };
		if (!ValidateGIFieldCreateInfo(state.m_Info) || !services.m_GIFieldFrameState)
		{
			return RenderFrameValidationResult::ContractFailure("Invalid GI field descriptor or settlement service");
		}
		if (state.m_Update->IsInvalid())
		{
			return RenderFrameValidationResult::ContractFailure("GI field invalidated by failed or unknown submission");
		}
		if (static_cast<size_t>(frame.GetDisplayViewId()) < frame.m_RenderViews.size())
		{
			const auto& view = frame.GetDisplayRenderView();
			const uint64_t pixels = uint64_t(view.m_Width) * view.m_Height;
			GIReceiverViewSnapshot next{
				.m_ViewId = static_cast<uint32_t>(view.m_ViewId),
				.m_RenderExtent = { view.m_Width, view.m_Height },
				.m_DisplayExtent = { view.m_DisplayWidth, view.m_DisplayHeight },
				.m_CameraPosition = { view.m_CameraPosition.m_X, view.m_CameraPosition.m_Y, view.m_CameraPosition.m_Z },
				.m_View = view.m_View.ToArray(), .m_Projection = view.m_UnjitteredProj.ToArray(),
				.m_ViewProjection = view.m_UnjitteredViewProj.ToArray(),
				.m_JitterPixels = { view.m_JitterPixels.m_X, view.m_JitterPixels.m_Y },
				.m_NearFar = { view.m_Near, view.m_Far },
				.m_ReceiverAttachmentBytes = receiversAvailable ? pixels * (GIReceiverTextureNames.size() + 1) * 16 : 0,
				.m_LookupDiagnosticBytes = receiversAvailable ? pixels * 2 * 16 : 0,
				.m_ReversedDepth = view.m_DepthConvention == DepthConvention::Reversed,
				.m_ReceiversAvailable = receiversAvailable,
				.m_TemporalAA = frame.GetTemporalFramePlan().IsConsumerActive(TemporalConsumer::TemporalAA) };
			next.m_Supported = receiversAvailable && view.m_IsValid && view.GetResolution().IsNative() &&
				!next.m_TemporalAA && next.m_JitterPixels == std::array<float, 2>{};
			state.m_ReceiverView = AdvanceGIReceiverView(state.m_ReceiverView, next);
		}
		if (!state.m_Info.m_ConstantEnvironment)
		{
			const auto bake = services.m_Environment->GetBakingStatus();
			if (!bake.m_HasActiveBake || bake.m_ActiveGeneration != services.m_Environment->GetBakeRequestGeneration())
			{
				// IBL baking itself needs this frame to render. Skip only field work,
				// otherwise waiting for publication prevents the producer from advancing.
				return RenderFrameValidationResult::Ready();
			}
		}
		const auto result = state.m_Workload.Prepare(frame, services);
		if (!result.IsReady())
		{
			return result;
		}
		if (state.m_Info.m_EnableReceiverSampling && state.m_Workload.GetDiagnostics().m_Failed)
		{
			// Update-only inspection retains raw error evidence. Production shading
			// cannot resume from a numerically invalid in-place field generation.
			return RenderFrameValidationResult::ContractFailure(
				"GI field numerical failure; replace the field through the safe pipeline path");
		}
		if (!state.m_LookupLayout.IsValid())
		{
			const auto* common = services.m_BindingLayout->GetCommonLayoutContract();
			if (!common)
			{
				return RenderFrameValidationResult::ContractFailure("Field lookup common layout unavailable");
			}
			state.m_LookupLayout = ResolvedRenderLayout::Create(
				services.m_Presentation->GetRHIContext()->GetPipelineSystem(), *common, "GI.Field.Lookup");
			state.m_LookupScene = ResolvedBindingRef<SceneGPU>::Resolve(
				state.m_LookupLayout, scene_bindings::SceneConstants);
		}
		const auto lookup = services.m_ShaderPrograms->LoadProgram(shader_programs::GIFieldLookupCompute);
		const auto lookupGeneration = services.m_ShaderPrograms->GetGeneration(lookup);
		if (state.m_LookupGeneration != 0 && state.m_LookupGeneration != lookupGeneration)
		{
			return RenderFrameValidationResult::ContractFailure("Field lookup changed; replace the static field");
		}
		state.m_LookupGeneration = lookupGeneration;
		state.m_LookupPipeline = state.m_LookupLayout.Resolve(*services.m_PipelineResolver,
			state.m_LookupSlot, ComputePipelineRecipe{
				.m_BindingLayout = state.m_LookupLayout.GetNativeLayout(), .m_CSId = lookup }, LookupPassInfo);
		const auto inspection = services.m_ShaderPrograms->LoadProgram(shader_programs::GIProbeInspectionCompute);
		state.m_InspectionPipeline = state.m_LookupLayout.Resolve(*services.m_PipelineResolver,
			state.m_InspectionSlot, ComputePipelineRecipe{
				.m_BindingLayout = state.m_LookupLayout.GetNativeLayout(), .m_CSId = inspection }, InspectionPassInfo);
		state.m_ReceiverInspectionPipeline = state.m_LookupLayout.Resolve(*services.m_PipelineResolver,
			state.m_ReceiverInspectionSlot, ComputePipelineRecipe{
				.m_BindingLayout = state.m_LookupLayout.GetNativeLayout(),
				.m_CSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIReceiverInspectionCompute) },
			ReceiverInspectionPassInfo);
		GraphicsPhysicalPipelineKey markers;
		markers.m_BindingLayout = state.m_LookupLayout.GetNativeLayout();
		markers.m_InputLayoutId = InputLayoutID::None;
		markers.m_VSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIReceiverMarkersVertex);
		markers.m_PSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIReceiverMarkersPixel);
		markers.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		markers.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		markers.m_Formats.m_RenderTargetFormats[0] = services.m_Presentation->GetSwapChain()->GetFormat();
		markers.m_Formats.m_RenderTargetCount = 1;
		markers.m_RasterizerPreset = RasterizerPreset::TwoSided;
		markers.m_BlendPreset = BlendPreset::AlphaBlend;
		markers.m_DepthPreset = DepthPreset::DepthDisabled;
		state.m_ReceiverMarkerPipeline = state.m_LookupLayout.Resolve(*services.m_PipelineResolver,
			state.m_ReceiverMarkerSlot, markers, ReceiverMarkersPassInfo);
		if (!state.m_LookupPipeline.IsValid())
		{
			return RenderFrameValidationResult::ContractFailure("Field lookup pipeline unavailable");
		}
		const auto revision = state.m_Info.m_RayScene->GetDiagnostics().m_Revision;
		const auto lighting = frame.m_RenderScene.m_WorldLightingGeneration;
		const uint64_t environment = state.m_Info.m_ConstantEnvironment ?
			0 : services.m_Environment->GetBakingStatus().m_ActiveGeneration;
		const auto shaders = state.m_Workload.GetShaderGenerations();
		if (state.m_Sealed && (revision != state.m_SceneRevision || shaders != state.m_ShaderGenerations ||
			(!state.m_Info.m_Readiness.m_Enabled && (lighting != state.m_LightingGeneration ||
				environment != state.m_EnvironmentGeneration))))
		{
			return RenderFrameValidationResult::ContractFailure(
				"Static GI field inputs changed; replace the field through the safe pipeline path");
		}
		if (state.m_Sealed && (lighting != state.m_LightingGeneration || environment != state.m_EnvironmentGeneration))
		{
			state.m_Workload.RefreshLighting();
		}
		state.m_SceneRevision = revision;
		state.m_LightingGeneration = lighting;
		state.m_EnvironmentGeneration = environment;
		state.m_ShaderGenerations = shaders;
		state.m_Sealed = true;
		state.m_UpdatePrepared = true;
		return RenderFrameValidationResult::Ready();
	}

	void GIField::AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services, const RaySceneGraphResources& scene) noexcept
	{
		if (!m_State->m_UpdatePrepared)
		{
			return;
		}
		const bool tracked = services.m_GIFieldFrameState->Track(m_State->m_Update, frame.m_FrameSerial);
		GGLAB_ASSERT_MSG(tracked, "A GI field must participate in exactly one matching renderer frame.");
		m_State->m_Workload.AddPasses(graph, frame, services, scene, m_State->m_Update,
			m_State->m_Info.m_EnableReceiverSampling, m_State->m_Info.m_ViewBias, m_State->m_Summary->m_Field);
	}

	void GIField::AddReceiverDiagnostics(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services) noexcept
	{
		const auto* field = graph.GetBlackboard().TryGet<RGGIFieldResources>();
		const auto* receiver = graph.GetBlackboard().TryGet<RGGIReceiverResources>(GIReceiverResourcesName);
		if (!field || !receiver)
		{
			return;
		}
		struct PassData
		{
			GIFieldSamplingViews m_Field;
			std::array<RGTextureViewId, 3> m_Receivers;
			std::array<RGTextureViewId, 2> m_Outputs;
			LookupParameters m_Parameters;
		};
		const auto& view = frame.GetDisplayRenderView();
		graph.AddPass<PassData>(LookupPassInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
			[=](RenderGraph::RGBuilder& builder, PassData& data)
			{
				data.m_Field.Declare(builder, *field, RHIStage::ComputeShader);
				const std::array inputs{ receiver->m_Position,
					receiver->Get(GIReceiverSurface::VertexNormal), receiver->Get(GIReceiverSurface::ShadingNormal) };
				for (size_t index = 0; index < inputs.size(); ++index)
				{
					data.m_Receivers[index] = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(inputs[index], RGTextureAccess::Sample, RHIStage::ComputeShader));
				}
				const std::array names{ "GI.Field.ReceiverIrradiance", "GI.Field.ReceiverQuery" };
				for (size_t index = 0; index < names.size(); ++index)
				{
					auto output = builder.CreateTexture(names[index], {
						.m_Format = RHIFormat::R32G32B32A32Float,
						.m_Extent = { view.m_Width, view.m_Height, 1 } });
					builder.WriteInPlace(output, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_Outputs[index] = builder.CreateView<RHITextureViewType::UnorderedAccess>(output);
				}
				// Diagnostic targets are graph-owned, not persistent external resources.
				builder.SideEffect();
				data.m_Parameters.m_Extent = { view.m_Width, view.m_Height };
				data.m_Parameters.m_Camera = {
					view.m_CameraPosition.m_X, view.m_CameraPosition.m_Y, view.m_CameraPosition.m_Z };
			},
			[this, &frame](RGExecuteContext& execute, PassData& data)
			{
				auto& state = *m_State;
				auto* commands = execute.GetDirectComputeCommandContext();
				ScopedConstantBindings<RHIComputeCommandContext> constants(*commands,
					state.m_LookupLayout, state.m_LookupScene);
				if (!constants.SetPipeline(state.m_LookupPipeline) ||
					!constants.Bind(state.m_LookupScene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					return;
				}
				auto parameters = data.m_Parameters;
				parameters.m_Field = data.m_Field.Resolve(execute);
				parameters.m_Position = execute.GetViewDescriptor(data.m_Receivers[0]).m_Index;
				parameters.m_VertexNormal = execute.GetViewDescriptor(data.m_Receivers[1]).m_Index;
				parameters.m_ShadingNormal = execute.GetViewDescriptor(data.m_Receivers[2]).m_Index;
				parameters.m_Irradiance = execute.GetViewDescriptor(data.m_Outputs[0]).m_Index;
				parameters.m_Query = execute.GetViewDescriptor(data.m_Outputs[1]).m_Index;
				if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
				{
					commands->Dispatch((parameters.m_Extent[0] + 7) / 8, (parameters.m_Extent[1] + 7) / 8, 1);
				}
			});
		GGLAB_UNUSED(services);
	}

	void GIField::SetProbeInspection(std::optional<GIProbeInspectionRequest> request) noexcept
	{
		m_State->m_Inspection = std::move(request);
	}

	void GIField::AddProbeInspection(RenderGraph& graph, const RenderFrameContext& frame) noexcept
	{
		auto& state = *m_State;
		const auto* field = graph.GetBlackboard().TryGet<RGGIFieldResources>();
		if (!field || !state.m_UpdatePrepared || !state.m_Inspection ||
			!MatchesGIProbeInspection(*state.m_Inspection, *state.m_Summary) ||
			!state.m_InspectionPipeline.IsValid())
		{
			return;
		}
		const auto request = *state.m_Inspection;
		const uint32_t width = state.m_Info.m_IrradianceInteriorWidth + 2;
		const uint32_t texels = 1 + width * width + 16 * 16;
		struct PassData
		{
			GIFieldSamplingViews m_Field;
			RGTextureViewId m_Output;
		};
		graph.AddPass<PassData>(InspectionPassInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
			[=](RenderGraph::RGBuilder& builder, PassData& data)
			{
				data.m_Field.Declare(builder, *field, RHIStage::ComputeShader);
				auto output = builder.CreateTexture("GI.Field.ProbeInspection", {
					.m_Format = RHIFormat::R32Uint, .m_Extent = { texels * 4, 1, 1 } });
				builder.WriteInPlace(output, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Output = builder.CreateView<RHITextureViewType::UnorderedAccess>(output);
				builder.SideEffect();
			},
			[this, &frame, request, texels](RGExecuteContext& execute, PassData& data)
			{
				auto& state = *m_State;
				auto* commands = execute.GetDirectComputeCommandContext();
				ScopedConstantBindings<RHIComputeCommandContext> constants(*commands,
					state.m_LookupLayout, state.m_LookupScene);
				if (!constants.SetPipeline(state.m_InspectionPipeline) ||
					!constants.Bind(state.m_LookupScene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					return;
				}
				const InspectionParameters parameters{ .m_Field = data.m_Field.Resolve(execute),
					.m_Coordinate = request.m_Coordinate,
					.m_Output = execute.GetViewDescriptor(data.m_Output).m_Index };
				if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
				{
					commands->Dispatch((texels - 1 + 63) / 64, 1, 1);
				}
			});
	}

	void GIField::SetReceiverInspection(std::optional<GIReceiverInspectionRequest> request) noexcept
	{
		m_State->m_ReceiverInspection = std::move(request);
	}

	void GIField::AddReceiverInspection(RenderGraph& graph, const RenderFrameContext& frame) noexcept
	{
		auto& state = *m_State;
		const auto* field = graph.GetBlackboard().TryGet<RGGIFieldResources>();
		const auto* receiver = graph.GetBlackboard().TryGet<RGGIReceiverResources>(GIReceiverResourcesName);
		if (!field || !receiver || !state.m_UpdatePrepared || !state.m_ReceiverInspection ||
			!state.m_ReceiverView.m_Supported || !MatchesGIReceiverInspection(*state.m_ReceiverInspection, *state.m_Summary) ||
			!state.m_Summary->m_ReceiverInspectionAvailable ||
			(state.m_ReceiverInspection->m_Markers && !state.m_Summary->m_ReceiverMarkersAvailable))
		{
			return;
		}
		const auto request = *state.m_ReceiverInspection;
		struct PassData
		{
			GIFieldSamplingViews m_Field;
			std::array<RGTextureViewId, 3> m_Receivers;
			RGTextureViewId m_Output;
		};
		graph.AddPass<PassData>(ReceiverInspectionPassInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
			[=](RenderGraph::RGBuilder& builder, PassData& data)
			{
				data.m_Field.Declare(builder, *field, RHIStage::ComputeShader);
				const std::array inputs{ receiver->m_Position,
					receiver->Get(GIReceiverSurface::VertexNormal), receiver->Get(GIReceiverSurface::ShadingNormal) };
				for (size_t index = 0; index < inputs.size(); ++index)
				{
					data.m_Receivers[index] = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(inputs[index], RGTextureAccess::Sample, RHIStage::ComputeShader));
				}
				auto records = builder.CreateTexture("GI.Field.ReceiverInspection", {
					.m_Format = RHIFormat::R32Uint, .m_Extent = { GIReceiverInspectionBytes / 4, 1, 1 } });
				builder.WriteInPlace(records, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Output = builder.CreateView<RHITextureViewType::UnorderedAccess>(records);
				builder.GetBlackboard().Create<ReceiverInspectionResources>() = { records };
				builder.SideEffect();
			},
			[this, &frame, request](RGExecuteContext& execute, PassData& data)
			{
				auto& state = *m_State;
				auto* commands = execute.GetDirectComputeCommandContext();
				ScopedConstantBindings<RHIComputeCommandContext> constants(*commands,
					state.m_LookupLayout, state.m_LookupScene);
				if (!constants.SetPipeline(state.m_ReceiverInspectionPipeline) ||
					!constants.Bind(state.m_LookupScene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					return;
				}
				const ReceiverInspectionParameters parameters{ .m_Field = data.m_Field.Resolve(execute),
					.m_Position = execute.GetViewDescriptor(data.m_Receivers[0]).m_Index,
					.m_VertexNormal = execute.GetViewDescriptor(data.m_Receivers[1]).m_Index,
					.m_ShadingNormal = execute.GetViewDescriptor(data.m_Receivers[2]).m_Index,
					.m_Output = execute.GetViewDescriptor(data.m_Output).m_Index,
					.m_Pixel = request.m_Pixel, .m_Camera = state.m_ReceiverView.m_CameraPosition };
				if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
				{
					commands->Dispatch(1, 1, 1);
				}
			});
	}

	void GIField::AddReceiverMarkers(RenderGraph& graph, const RenderFrameContext& frame) noexcept
	{
		const auto& state = *m_State;
		const auto* inspection = graph.GetBlackboard().TryGet<ReceiverInspectionResources>();
		if (!inspection || !state.m_ReceiverInspection || !state.m_ReceiverInspection->m_Markers ||
			!state.m_ReceiverMarkerPipeline.IsValid())
		{
			return;
		}
		struct PassData
		{
			RGTextureViewId m_Records;
			RGTextureViewId m_Color;
		};
		const auto viewId = frame.GetDisplayViewId();
		graph.AddPass<PassData>(ReceiverMarkersPassInfo.m_TypeName.c_str(),
			[=](RenderGraph::RGBuilder& builder, PassData& data)
			{
				data.m_Records = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(inspection->m_Records, RGTextureAccess::Sample, RHIStage::VertexShader));
				auto& targets = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(viewId);
				builder.ReadWriteInPlace(targets.m_BackBuffer, RGTextureAccess::RenderTarget);
				data.m_Color = builder.CreateView<RHITextureViewType::RenderTarget>(targets.m_BackBuffer);
			},
			[this, &frame](RGExecuteContext& execute, PassData& data)
			{
				auto& state = *m_State;
				auto* commands = execute.GetGraphicsCommandContext();
				ScopedConstantBindings<RHIGraphicsCommandContext> constants(*commands,
					state.m_LookupLayout, state.m_LookupScene);
				if (!constants.SetPipeline(state.m_ReceiverMarkerPipeline) ||
					!constants.Bind(state.m_LookupScene, frame.m_FrameConstants.m_SceneSnapshot))
				{
					return;
				}
				const auto extent = state.m_ReceiverView.m_DisplayExtent;
				const RHIRenderingAttachment color{ .m_View = execute.GetViewHandle(data.m_Color) };
				commands->BeginRendering({ .m_ColorAttachments = std::span<const RHIRenderingAttachment>(&color, 1) });
				commands->SetViewport({ 0, 0, float(extent[0]), float(extent[1]) });
				commands->SetScissorRect({ 0, 0, int32_t(extent[0]), int32_t(extent[1]) });
				commands->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
				const ReceiverMarkerParameters parameters{ .m_ViewProjection = state.m_ReceiverView.m_ViewProjection,
					.m_Records = execute.GetViewDescriptor(data.m_Records).m_Index, .m_Extent = extent };
				if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
				{
					commands->Draw(19 * 6, 1, 0);
				}
			});
	}

	void GIField::AddProbeVisualization(RenderGraph& graph, const RenderFrameContext& frame) noexcept
	{
		if (!m_State->m_UpdatePrepared)
		{
			return;
		}
		const auto& summary = *m_State->m_Summary;
		const uint64_t update = summary.m_SubmittedUpdates + (summary.m_Invalid ? 0 : 1);
		m_State->m_Visualization.AddPasses(graph, frame, {
			.m_Field = summary.m_Field, .m_Kind = GIObservationKind::ProbeVisualization,
			.m_ProducingFrame = frame.m_FrameSerial, .m_FieldUpdate = update,
			.m_ReadinessUpdate = update - summary.m_Field.m_EpochOriginUpdate });
	}

	void GIField::SetProbePicking(std::optional<GIProbePickingRequest> request) noexcept
	{
		m_State->m_Visualization.SetPicking(std::move(request));
	}

	void GIField::ExportResources(RenderGraph& graph) noexcept
	{
		m_State->m_Workload.ExportFieldResources(graph);
	}
}
