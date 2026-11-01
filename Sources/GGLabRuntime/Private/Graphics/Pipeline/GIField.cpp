#include "GGLabRuntime/Graphics/Pipeline/GIField.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "Graphics/Pipeline/GIFieldState.h"
#include "Graphics/Pipeline/GIProbeUpdateWorkload.h"
#include "Graphics/RenderPass/GIFieldGraphResources.h"
#include "Graphics/RenderPass/GIReceiverGraphResources.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
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
			!std::isfinite(info.m_ViewBias) || info.m_ViewBias < 0 || info.m_ViewBias > 1)
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
		State(GIFieldCreateInfo info) noexcept :
			m_Info(std::move(info)), m_Workload(m_Info.m_RayScene, UpdateProfile(m_Info), true) {}
	};

	GIField::GIField(GIFieldCreateInfo info) noexcept :
		m_State(std::make_unique<State>(std::move(info))) {}
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
			.m_Initialized = update.m_Initialized,
			.m_Invalid = state.m_Update->IsInvalid() || update.m_Failed,
			.m_Reason = state.m_Update->IsInvalid() ? "GI field submission failed or completion is unknown" : update.m_Reason,
		};
		for (const auto& allocation : update.m_Allocations)
		{
			result.m_Allocations.push_back({ allocation.m_LogicalBytes, allocation.m_Allocation });
		}
		return result;
	}

	RenderFrameValidationResult GIField::Prepare(const RenderFrameContext& frame,
		const RenderServices& services) noexcept
	{
		auto& state = *m_State;
		state.m_UpdatePrepared = false;
		if (!ValidateGIFieldCreateInfo(state.m_Info) || !services.m_GIFieldFrameState)
		{
			return RenderFrameValidationResult::ContractFailure("Invalid GI field descriptor or settlement service");
		}
		if (state.m_Update->IsInvalid())
		{
			return RenderFrameValidationResult::ContractFailure("GI field invalidated by failed or unknown submission");
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
		if (!state.m_LookupPipeline.IsValid())
		{
			return RenderFrameValidationResult::ContractFailure("Field lookup pipeline unavailable");
		}
		const auto revision = state.m_Info.m_RayScene->GetDiagnostics().m_Revision;
		const auto lighting = frame.m_RenderScene.m_WorldLightingGeneration;
		const uint64_t environment = state.m_Info.m_ConstantEnvironment ?
			0 : services.m_Environment->GetBakingStatus().m_ActiveGeneration;
		const auto shaders = state.m_Workload.GetShaderGenerations();
		if (state.m_Sealed && (revision != state.m_SceneRevision || lighting != state.m_LightingGeneration ||
			environment != state.m_EnvironmentGeneration || shaders != state.m_ShaderGenerations))
		{
			return RenderFrameValidationResult::ContractFailure(
				"Static GI field inputs changed; replace the field through the safe pipeline path");
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
			m_State->m_Info.m_EnableReceiverSampling, m_State->m_Info.m_ViewBias);
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

	void GIField::ExportResources(RenderGraph& graph) noexcept
	{
		m_State->m_Workload.ExportFieldResources(graph);
	}
}
