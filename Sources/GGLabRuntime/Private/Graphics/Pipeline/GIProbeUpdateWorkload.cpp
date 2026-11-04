#include "Graphics/Pipeline/GIProbeUpdateWorkload.h"
#include "Graphics/Pipeline/GIFieldState.h"
#include "Graphics/RenderPass/GIFieldGraphResources.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneConstantBindings.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "Graphics/Pipeline/RaySceneQuery.h"
#include "Graphics/GIShaderConstants.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		enum class ProbeStage : size_t
		{
			Initialize,
			TraceShade,
			BlendDirect,
			BlendShared,
			Borders,
			Classify,
			ReadinessFloor,
			Count,
		};
		enum class ProbeSurface : size_t
		{
			Irradiance,
			Distance,
			State,
			Classification,
		};
		constexpr std::array FieldSurfaces{ "GI.Field.Irradiance", "GI.Field.Distance", "GI.Field.State", "GI.Field.Classification" };
		constexpr std::array FieldPasses{ "GI.Field.Initialize", "GI.Field.TraceShade",
			"GI.Field.BlendDirect", "GI.Field.BlendShared", "GI.Field.Borders", "GI.Field.Classify", "GI.Field.ReadinessFloor" };
		struct SurfaceInfo
		{
			const char* m_Name;
			uint32_t m_TileWidth;
			RHIFormat m_Format;
			uint32_t m_TexelBytes;
		};
		constexpr std::array Surfaces
		{
			SurfaceInfo{ "GI.ProbeUpdate.Irradiance", gi_shader::GIProbeIrradianceTileWidth, RHIFormat::R32G32B32A32Float, 4 * sizeof(float) },
			SurfaceInfo{ "GI.ProbeUpdate.Distance", gi_shader::GIProbeDistanceTileWidth, RHIFormat::R32G32Float, 2 * sizeof(float) },
			SurfaceInfo{ "GI.ProbeUpdate.State", 1, RHIFormat::R32G32B32A32Float, 4 * sizeof(float) },
			SurfaceInfo{ "GI.Field.Classification", 1, RHIFormat::R32Uint, sizeof(uint32_t) },
		};
		uint32_t SurfaceTileWidth(const GIProbeUpdateProfile& profile, size_t index) noexcept
		{
			return index == static_cast<size_t>(ProbeSurface::Irradiance) ?
				profile.m_IrradianceInteriorWidth + 2 * gi_shader::GIProbeBorderWidth :
				Surfaces[index].m_TileWidth;
		}
		uint64_t LogicalSurfaceBytes(uint32_t probes, const SurfaceInfo& surface, uint32_t width) noexcept
		{
			return uint64_t(probes) * width * width * surface.m_TexelBytes;
		}

		constexpr LayoutBindingKey RayInputKey{ 0x906 };
		constexpr LayoutBindingKey RayOutputKey{ 0x907 };
		constexpr LayoutBindingKey StatusOutputKey{ 0x908 };
		constexpr LayoutBindingKey ReadinessOutputKey{ 0x909 };
		constexpr LayoutBindingKey FeedbackOutputKey{ 0x90a };
		constexpr uint32_t StatusOutputRegister = 2; // u2.
		constexpr uint32_t RayInputRegister = 11; // t11; ray-scene records occupy t5-t10.
		constexpr uint32_t RayOutputRegister = 1; // u1.
		static_assert(GIProbeUpdateLimits::MaximumRaysPerProbe == gi_shader::GIProbeMaximumRays);
		static_assert(GIProbeUpdateLimits::DefaultIrradianceInteriorWidth == gi_shader::GIProbeIrradianceInteriorWidth);
		static_assert(sizeof(GIUpdateStatus) == gi_shader::GIUpdateStatusBytes);
		static_assert(uint32_t(GIUpdateError::InvalidRay) == gi_shader::GIUpdateErrorInvalidRay &&
			uint32_t(GIUpdateError::InvalidHistory) == gi_shader::GIUpdateErrorInvalidHistory &&
			uint32_t(GIUpdateError::InvalidBlend) == gi_shader::GIUpdateErrorInvalidBlend);
		// Inline program identities own strings. Store constant addresses instead
		// of copying them during cross-translation-unit dynamic initialization.
		constexpr std::array ProgramRefs
		{
			&shader_programs::GIProbeInitializeCompute, &shader_programs::GIProbeTraceCompute,
			&shader_programs::GIProbeBlendDirectCompute, &shader_programs::GIProbeBlendSharedCompute,
			&shader_programs::GIProbeBordersCompute, &shader_programs::GIProbeClassifyCompute,
			&shader_programs::GIFieldReadinessFloorCompute
		};
		const std::array PassInfos
		{
			RenderPassInfo{ .m_TypeName = "GI.ProbeUpdate.Initialize", .m_DisplayName = "Initialize probe workload",
				.m_CategoryName = "Global Illumination", .m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute },
			RenderPassInfo{ .m_TypeName = "GI.ProbeUpdate.TraceShade", .m_DisplayName = "Probe traversal and reduced hit lighting",
				.m_CategoryName = "Global Illumination", .m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute },
			RenderPassInfo{ .m_TypeName = "GI.ProbeUpdate.BlendDirect", .m_DisplayName = "Direct ray atlas integration",
				.m_CategoryName = "Global Illumination", .m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute },
			RenderPassInfo{ .m_TypeName = "GI.ProbeUpdate.BlendShared", .m_DisplayName = "Shared ray atlas integration",
				.m_CategoryName = "Global Illumination", .m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute },
			RenderPassInfo{ .m_TypeName = "GI.ProbeUpdate.Borders", .m_DisplayName = "Probe octahedral borders",
				.m_CategoryName = "Global Illumination", .m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute },
			RenderPassInfo{ .m_TypeName = "GI.Field.Classify", .m_DisplayName = "Classify static probes",
				.m_CategoryName = "Global Illumination", .m_Category = RenderPassCategory::Lighting, .m_Type = RenderPassType::Compute },
			RenderPassInfo{ .m_TypeName = "GI.Field.ReadinessFloor", .m_DisplayName = "Field readiness irradiance floor",
				.m_CategoryName = "Global Illumination", .m_Category = RenderPassCategory::Lighting, .m_Type = RenderPassType::Compute },
		};
		static_assert(ProgramRefs.size() == static_cast<size_t>(ProbeStage::Count));
		static_assert(ProgramRefs.size() == GIProbeUpdateWorkload::ProgramCount);
		static_assert(PassInfos.size() == ProgramRefs.size());
		struct Parameters
		{
			std::array<uint32_t, 3> m_GridSize{};
			uint32_t m_RaysPerProbe = 0;
			uint32_t m_FirstProbe = 0;
			uint32_t m_UpdateProbeCount = 0;
			uint32_t m_ProbeCount = 0;
			uint32_t m_UpdateIndex = 0;
			std::array<float, 3> m_GridMinimum{};
			float m_ProbeSpacing = 0;
			std::array<float, 3> m_ConstantRadiance{};
			float m_MaximumDistance = 0;
			std::array<uint32_t, 3> m_SceneCounts{};
			uint32_t m_ConstantEnvironment = 0;
			std::array<uint32_t, Surfaces.size()> m_Targets{ UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
			uint32_t m_Transmittance = UINT32_MAX;
			uint32_t m_AtmosphereSampler = 0;
			uint32_t m_FieldFlags = 0;
			uint32_t m_TraverseScene = 0;
			std::array<float, 4> m_Rotation{};
			float m_Hysteresis = 0;
			uint32_t m_RotateDirections = 0;
			uint32_t m_TraversalOnly = 0;
			uint32_t m_IrradianceInteriorWidth = 0;
			uint32_t m_ReadinessEnabled = 0;
			uint32_t m_ReadinessEpoch = 0;
			float m_DistanceHysteresis = 0;
			uint32_t m_ReadinessDistance = 0;
			float m_FeedbackViewBias = 0;
			std::array<uint32_t, 3> m_FeedbackPadding{};
		};
		static_assert(sizeof(Parameters) == 176 && offsetof(Parameters, m_Targets) == 80 &&
			offsetof(Parameters, m_Rotation) == 112 && offsetof(Parameters, m_Hysteresis) == 128 &&
			offsetof(Parameters, m_IrradianceInteriorWidth) == 140 &&
			offsetof(Parameters, m_ReadinessEnabled) == 144 && offsetof(Parameters, m_ReadinessDistance) == 156 &&
			offsetof(Parameters, m_FieldFlags) == 104 && offsetof(Parameters, m_FeedbackViewBias) == 160);

		struct FeedbackRecord
		{
			std::array<uint32_t, 4> m_Support{};
			// Bootstrap, incompatible, consumed age (0 or 1), one-based update.
			std::array<uint32_t, 4> m_Observation{};
		};
		static_assert(sizeof(FeedbackRecord) == 32 && offsetof(FeedbackRecord, m_Observation) == 16);

		// Borrowed handles only: submissions and the workload retain the actual buffers.
		struct StatusReadback
		{
			RHIBufferHandle m_Buffer;
			RHIFencePoint m_Fence;
			uint64_t m_Update = 0;
			GIObservationStamp m_Observation;
			float m_Hysteresis = 0;
			bool m_Refreshing = false;
		};
		struct Progress
		{
			RHIDevice* m_Device = nullptr;
			std::vector<StatusReadback> m_Readbacks;
			GIProbeUpdateDiagnostics m_Diagnostics;
			// Submitted storage must survive until its numerical verdict is read after completion.
			bool m_StorageInitialized = false;
			GIFieldReadinessEstimator m_Readiness;
			GIFieldReadinessSettings m_ReadinessSettings;
			GIFieldIntegrationHistory m_IrradianceHistory;
			GIFieldIntegrationHistory m_DistanceHistory;
			GIFieldFeedbackSnapshot m_Feedback;
			uint64_t m_ReadinessEpoch = 0;
			uint64_t m_RefreshEpoch = 1;
			std::optional<GIObservationStamp> m_CompletedObservation;
			std::shared_ptr<const GIFieldChangeObservation> m_ChangeObservation;
			uint64_t m_ReadbackBytes = sizeof(GIUpdateStatus);
			uint64_t m_FeedbackOffset = 0;
			bool m_RefreshDistance = true;
			bool m_CompletionFailed = false;
			uint32_t m_RaysPerProbe = 0;
			uint32_t m_IrradianceInteriorWidth = 0;
			void Poll() noexcept
			{
				for (;;)
				{
					StatusReadback* next = nullptr;
					for (auto& candidate : m_Readbacks)
					{
						if (candidate.m_Fence.IsValid() && (!next || candidate.m_Update < next->m_Update))
						{
							next = &candidate;
						}
					}
					if (!next)
					{
						break;
					}
					auto& slot = *next;
					const auto query = m_Device->QueryFencePoint(slot.m_Fence);
					if (query == RHIFenceQueryResult::Pending)
					{
						break;
					}
					if (query != RHIFenceQueryResult::Completed)
					{
						m_CompletionFailed = true;
						m_Diagnostics.m_Failed = true;
						m_Diagnostics.m_Reason = "Probe workload completion is unknown or failed";
						return;
					}
					const void* mapped = m_Device->MapBuffer(slot.m_Buffer, { 0, m_ReadbackBytes });
					if (!mapped)
					{
						m_CompletionFailed = true;
						m_Diagnostics.m_Failed = true;
						m_Diagnostics.m_Reason = "Probe update status readback mapping failed";
						return;
					}
					GIUpdateStatus status;
					std::memcpy(&status, mapped, sizeof(status));
					// Validate the complete status before publishing any measurement from it.
					if (!IsGIUpdateStatusValid(status, m_Diagnostics.m_ProbeCount, m_RaysPerProbe,
						slot.m_Update, m_IrradianceInteriorWidth))
					{
						m_Device->UnmapBuffer(slot.m_Buffer, {});
						m_CompletionFailed = true;
						m_Diagnostics.m_Failed = true;
						m_Diagnostics.m_Reason = "Probe update status readback contains an invalid record";
						return;
					}

					if (status.m_Error == GIUpdateError::None && m_ReadinessSettings.m_Enabled &&
						slot.m_Update > m_ReadinessEpoch &&
						slot.m_Observation.m_Field.m_RefreshEpoch == m_RefreshEpoch &&
						slot.m_Observation.m_Field.m_EpochOriginUpdate == m_ReadinessEpoch)
					{
						const auto* bytes = static_cast<const std::byte*>(mapped) + sizeof(GIUpdateStatus);
						std::array<float, 4> header;
						std::memcpy(header.data(), bytes, sizeof(header));
						std::vector<GIFieldProbeChange> probes(m_Diagnostics.m_ProbeCount);
						std::memcpy(probes.data(), bytes + sizeof(header), probes.size() * sizeof(GIFieldProbeChange));
						if (!m_Readiness.Observe(slot.m_Update - m_ReadinessEpoch, header[0], probes, slot.m_Hysteresis, slot.m_Refreshing))
						{
							m_CompletionFailed = true;
							m_Diagnostics.m_Failed = true;
							m_Diagnostics.m_Reason = "Invalid or out-of-order field readiness measurement";
						}
						else
						{
							m_CompletedObservation = slot.m_Observation;
							m_ChangeObservation = std::make_shared<GIFieldChangeObservation>(
								GIFieldChangeObservation{ slot.m_Observation, m_Readiness.GetMeasurement() });
						}
					}
					if (status.m_Error == GIUpdateError::None && m_Feedback.m_Enabled)
					{
						GIFieldFeedbackSnapshot feedback{ .m_Enabled = true,
							.m_CompletedUpdate = slot.m_Update, .m_SourceUpdate = slot.m_Update - 1 };
						const auto* bytes = static_cast<const std::byte*>(mapped) + m_FeedbackOffset;
						for (uint32_t probe = 0; probe < m_Diagnostics.m_ProbeCount; ++probe)
						{
							FeedbackRecord record;
							std::memcpy(&record, bytes + probe * sizeof(record), sizeof(record));
							uint64_t hits = 0;
							for (const auto count : record.m_Support)
							{
								hits += count;
							}
							hits += uint64_t(record.m_Observation[0]) + record.m_Observation[1];
							const bool consumed = record.m_Support[0] != 0 || record.m_Support[1] != 0;
							if (record.m_Observation[3] != slot.m_Update || hits > m_RaysPerProbe ||
								record.m_Observation[1] != 0 ||
								record.m_Observation[2] != (consumed ? 1u : 0u) ||
								(slot.m_Update == 1 ? hits != record.m_Observation[0] : record.m_Observation[0] != 0))
							{
								m_CompletionFailed = true;
								m_Diagnostics.m_Failed = true;
								m_Diagnostics.m_Reason = "Invalid or mismatched field feedback measurement";
								break;
							}
							feedback.m_Valid += record.m_Support[0];
							feedback.m_LowConfidence += record.m_Support[1];
							feedback.m_NoValidSupport += record.m_Support[2];
							feedback.m_OutsideCoverage += record.m_Support[3];
							feedback.m_Initializing += record.m_Observation[0];
							feedback.m_Incompatible += record.m_Observation[1];
						}
						if (!m_CompletionFailed)
						{
							m_Feedback = feedback;
						}
					}
					m_Device->UnmapBuffer(slot.m_Buffer, {});
					if (status.m_Error != GIUpdateError::None)
					{
						if (m_Diagnostics.m_UpdateStatus.m_Error == GIUpdateError::None)
						{
							m_Diagnostics.m_UpdateStatus = status;
						}
						m_Diagnostics.m_Failed = true;
						m_Diagnostics.m_Reason = "Probe update contains invalid numerical data; replace the owner";
					}
					else
					{
						m_Diagnostics.m_CompletedUpdates = std::max(m_Diagnostics.m_CompletedUpdates, slot.m_Update);
					}
					m_Diagnostics.m_Initialized = m_Diagnostics.m_CompletedUpdates != 0 && !m_Diagnostics.m_Failed;
					slot.m_Fence.Reset();
				}
			}
		};
		struct Resources
		{
			RHIDevice* m_Device = nullptr;
			RHIBufferOwner m_Rays;
			RHIBufferOwner m_Status;
			RHIBufferOwner m_Readiness;
			RHIBufferOwner m_Feedback;
			RHIBufferDesc m_FeedbackDesc;
			RHIBufferDesc m_ReadinessDesc;
			std::vector<RHIBufferOwner> m_Readbacks;
			RHIBufferDesc m_StatusDesc;
			RHIBufferDesc m_RayDesc;
			std::array<RHITextureOwner, Surfaces.size()> m_Textures;
			std::array<RHITextureDesc, Surfaces.size()> m_Descriptions;
		};
		std::array<float, 4> Rotation(uint32_t seed) noexcept
		{
			const auto random = [&seed]() noexcept
				{
					return gi_shader::GIRandom(seed);
				};
			const float u = random();
			const float v = 2 * std::numbers::pi_v<float> * random();
			const float w = 2 * std::numbers::pi_v<float> * random();
			return { std::sqrt(1 - u) * std::sin(v), std::sqrt(1 - u) * std::cos(v),
				std::sqrt(u) * std::sin(w), std::sqrt(u) * std::cos(w) };
		}
	}

	struct GIProbeUpdateWorkload::State
	{
		std::shared_ptr<RayScene> m_Scene;
		bool m_Field = false;
		GIProbeUpdateProfile m_Profile;
		std::shared_ptr<Resources> m_Resources;
		std::shared_ptr<Progress> m_Progress = std::make_shared<Progress>();
		ResolvedRenderLayout m_Layout;
		ResolvedRenderLayout m_DataLayout;
		ResolvedBindingRef<SceneGPU> m_DataScene;
		ResolvedBindingRef<AtmosphereGPU> m_DataAtmosphere;
		uint32_t m_DataRayInput = 0;
		uint32_t m_DataRayOutput = 0;
		ResolvedBindingRef<SceneGPU> m_SceneBinding;
		ResolvedBindingRef<AtmosphereGPU> m_Atmosphere;
		std::array<uint32_t, ray_scene_bindings::Count> m_Bindings{};
		uint32_t m_RayOutput = 0;
		uint32_t m_StatusOutput = 0;
		uint32_t m_DataStatusOutput = 0;
		uint32_t m_ReadinessOutput = 0;
		uint32_t m_DataReadinessOutput = 0;
		uint32_t m_FeedbackOutput = 0;
		uint32_t m_DataFeedbackOutput = 0;
		std::array<ComputePipelineSlot, ProgramRefs.size()> m_Slots;
		std::array<ResolvedRenderPipeline, ProgramRefs.size()> m_Pipelines;
		std::array<uint64_t, ProgramRefs.size()> m_Generations{};
	};

	GIProbeUpdateWorkload::GIProbeUpdateWorkload(std::shared_ptr<RayScene> scene,
		GIProbeUpdateProfile profile, bool field, GIFieldReadinessSettings readiness, bool feedback) noexcept :
		m_State(std::make_unique<State>())
	{
		m_State->m_Scene = std::move(scene);
		m_State->m_Profile = profile;
		m_State->m_Field = field;
		readiness.m_Enabled = field && readiness.m_Enabled;
		m_State->m_Progress->m_ReadinessSettings = readiness;
		m_State->m_Progress->m_Feedback.m_Enabled = field && feedback;
		m_State->m_Progress->m_Readiness.Reset(
			ValidateGIProbeUpdateProfile(profile) ? profile.m_Counts : std::array<uint32_t, 3>{}, readiness);
	}
	GIProbeUpdateWorkload::~GIProbeUpdateWorkload() = default;
	std::shared_ptr<RayScene> GIProbeUpdateWorkload::GetRayScene() const noexcept { return m_State->m_Scene; }

	GIProbeUpdateDiagnostics GIProbeUpdateWorkload::GetDiagnostics(bool includeAllocations) const noexcept
	{
		if (!m_State->m_Field)
		{
			m_State->m_Progress->Poll();
		}
		auto result = m_State->m_Progress->m_Diagnostics;
		if (const auto resources = m_State->m_Resources; includeAllocations && resources)
		{
			result.m_Allocations.push_back({ result.m_RayCapacityBytes,
				resources->m_Device->QueryBufferAllocationFacts(resources->m_Rays.Get()) });
			for (size_t index = 0; index < Surfaces.size(); ++index)
			{
				if (index == static_cast<size_t>(ProbeSurface::Classification) && !m_State->m_Field)
				{
					continue;
				}
				const auto& surface = Surfaces[index];
				const auto logicalBytes = LogicalSurfaceBytes(result.m_ProbeCount, surface,
					SurfaceTileWidth(m_State->m_Profile, index));
				result.m_Allocations.push_back({ logicalBytes,
					resources->m_Device->QueryTextureAllocationFacts(resources->m_Textures[index].Get()) });
			}
			result.m_Allocations.push_back({ sizeof(GIUpdateStatus),
				resources->m_Device->QueryBufferAllocationFacts(resources->m_Status.Get()) });
			if (resources->m_Readiness)
			{
				result.m_Allocations.push_back({ resources->m_ReadinessDesc.m_SizeInBytes,
					resources->m_Device->QueryBufferAllocationFacts(resources->m_Readiness.Get()) });
			}
			if (resources->m_Feedback)
			{
				result.m_Allocations.push_back({ resources->m_FeedbackDesc.m_SizeInBytes,
					resources->m_Device->QueryBufferAllocationFacts(resources->m_Feedback.Get()) });
			}
			for (const auto& readback : resources->m_Readbacks)
			{
				result.m_Allocations.push_back({ m_State->m_Progress->m_ReadbackBytes,
					resources->m_Device->QueryBufferAllocationFacts(readback.Get()) });
			}
		}
		return result;
	}

	GIFieldReadinessSnapshot GIProbeUpdateWorkload::GetReadiness() const noexcept
	{
		auto snapshot = m_State->m_Progress->m_Readiness.GetSnapshot();
		snapshot.m_Failed = snapshot.m_Failed || m_State->m_Progress->m_Diagnostics.m_Failed;
		if (!snapshot.m_Enabled)
		{
			snapshot.m_Hysteresis = m_State->m_Profile.m_Hysteresis;
		}
		return snapshot;
	}

	GIFieldFeedbackSnapshot GIProbeUpdateWorkload::GetFeedback() const noexcept
	{
		return m_State->m_Progress->m_Feedback;
	}

	std::shared_ptr<const GIFieldChangeObservation> GIProbeUpdateWorkload::GetChangeObservation() const noexcept
	{
		const auto& progress = *m_State->m_Progress;
		return progress.m_Diagnostics.m_Failed || progress.m_CompletionFailed ? nullptr : progress.m_ChangeObservation;
	}

	std::optional<GIObservationStamp> GIProbeUpdateWorkload::GetCompletedObservation() const noexcept
	{
		return m_State->m_Progress->m_CompletedObservation;
	}

	uint64_t GIProbeUpdateWorkload::GetRefreshEpoch() const noexcept
	{
		return m_State->m_Progress->m_RefreshEpoch;
	}

	uint64_t GIProbeUpdateWorkload::GetEpochOriginUpdate() const noexcept
	{
		return m_State->m_Progress->m_ReadinessEpoch;
	}

	void GIProbeUpdateWorkload::RefreshLighting() noexcept
	{
		auto& progress = *m_State->m_Progress;
		progress.m_ReadinessEpoch = progress.m_Diagnostics.m_SubmittedUpdates;
		++progress.m_RefreshEpoch;
		progress.m_CompletedObservation.reset();
		progress.m_ChangeObservation.reset();
		progress.m_RefreshDistance = false;
		progress.m_Readiness.Reset(m_State->m_Profile.m_Counts, progress.m_ReadinessSettings, true);
	}

	std::array<uint64_t, GIProbeUpdateWorkload::ProgramCount> GIProbeUpdateWorkload::GetShaderGenerations() const noexcept
	{
		return m_State->m_Generations;
	}

	RenderFrameValidationResult GIProbeUpdateWorkload::Prepare(const RenderFrameContext& frame,
		const RenderServices& services) noexcept
	{
		const auto prepareBegin = std::chrono::steady_clock::now();
		auto& state = *m_State;
		const bool firstPreparation = !state.m_Resources;
		const auto& profile = state.m_Profile;
		if (!state.m_Scene || !ValidateGIProbeUpdateProfile(profile))
		{
			return RenderFrameValidationResult::ContractFailure("Invalid probe update workload profile");
		}
		const auto& scene = state.m_Scene->GetDiagnostics();
		if (scene.m_Status != RaySceneStatus::Prepared ||
			(profile.m_TraverseScene && (!scene.m_HasCompleteBounds || profile.m_MaximumDistance < scene.m_BoundsDiagonal)))
		{
			return RenderFrameValidationResult::ContractFailure("Probe workload requires a complete ray scene and ray interval");
		}
		state.m_Progress->Poll();
		if (state.m_Progress->m_Diagnostics.m_SubmittedUpdates >= UINT32_MAX)
		{
			return RenderFrameValidationResult::ContractFailure("Probe update index exhausted; replace the owner");
		}
		if (state.m_Progress->m_CompletionFailed)
		{
			return RenderFrameValidationResult::ContractFailure(state.m_Progress->m_Diagnostics.m_Reason);
		}
		if (!state.m_Layout.IsValid())
		{
			auto& device = *services.m_Presentation->GetDevice();
			const auto* common = services.m_BindingLayout->GetCommonLayoutContract();
			const auto atmosphere = common ? scene_bindings::ComposeAtmosphereLayout(*common) : LayoutCompositionResult{};
			if (!atmosphere.IsSuccess())
			{
				return RenderFrameValidationResult::ContractFailure("Probe workload atmosphere layout unavailable");
			}
			std::vector<LayoutBindingRequirement> bindings(atmosphere.m_Layout->GetBindings().begin(),
				atmosphere.m_Layout->GetBindings().end());
			ray_scene_bindings::Append(bindings);
			const LayoutBindingRequirement rayInput{ .m_Key = RayInputKey,
				.m_Slot = { .m_Type = RHIBindingType::ReadOnlyStorageBuffer,
					.m_Visibility = RHIShaderStage::Compute, .m_Binding = RayInputRegister, .m_Count = 1 },
				.m_ParameterOrder = ray_scene_bindings::NextParameterOrder(bindings) };
			bindings.push_back(rayInput);
			const LayoutBindingRequirement rayOutput{ .m_Key = RayOutputKey,
				.m_Slot = { .m_Type = RHIBindingType::ReadWriteStorageBuffer,
					.m_Visibility = RHIShaderStage::Compute, .m_Binding = RayOutputRegister, .m_Count = 1 },
				.m_ParameterOrder = ray_scene_bindings::NextParameterOrder(bindings) };
			bindings.push_back(rayOutput);
			const LayoutBindingRequirement statusOutput{ .m_Key = StatusOutputKey,
				.m_Slot = { .m_Type = RHIBindingType::ReadWriteStorageBuffer,
					.m_Visibility = RHIShaderStage::Compute, .m_Binding = StatusOutputRegister, .m_Count = 1 },
				.m_ParameterOrder = ray_scene_bindings::NextParameterOrder(bindings) };
			bindings.push_back(statusOutput);
			const LayoutBindingRequirement readinessOutput{ .m_Key = ReadinessOutputKey,
				.m_Slot = { .m_Type = RHIBindingType::ReadWriteStorageBuffer,
					.m_Visibility = RHIShaderStage::Compute, .m_Binding = 3, .m_Count = 1 },
				.m_ParameterOrder = ray_scene_bindings::NextParameterOrder(bindings) };
			bindings.push_back(readinessOutput);
			const LayoutBindingRequirement feedbackOutput{ .m_Key = FeedbackOutputKey,
				.m_Slot = { .m_Type = RHIBindingType::ReadWriteStorageBuffer,
					.m_Visibility = RHIShaderStage::Compute, .m_Binding = 4, .m_Count = 1 },
				.m_ParameterOrder = ray_scene_bindings::NextParameterOrder(bindings) };
			bindings.push_back(feedbackOutput);
			const std::array fragments{ LayoutFragment{ "GIProbeUpdate", bindings } };
			const auto composed = ComposeLayoutFragments(fragments);
			if (!composed.IsSuccess())
			{
				return RenderFrameValidationResult::ContractFailure("Probe workload layout composition failed");
			}
			state.m_Layout = ResolvedRenderLayout::Create(services.m_Presentation->GetRHIContext()->GetPipelineSystem(),
				*composed.m_Layout, "GI.ProbeUpdate");
			if (!ray_scene_bindings::ResolveParameters(*composed.m_Layout, state.m_Bindings))
			{
				return RenderFrameValidationResult::ContractFailure("Probe workload binding unavailable");
			}
			const auto outputParameter = composed.m_Layout->FindParameterIndex(RayOutputKey);
			const auto statusParameter = composed.m_Layout->FindParameterIndex(StatusOutputKey);
			if (!outputParameter || !statusParameter)
			{
				return RenderFrameValidationResult::ContractFailure("Probe ray output binding unavailable");
			}
			state.m_RayOutput = *outputParameter;
			state.m_StatusOutput = *statusParameter;
			state.m_ReadinessOutput = *composed.m_Layout->FindParameterIndex(ReadinessOutputKey);
			state.m_FeedbackOutput = *composed.m_Layout->FindParameterIndex(FeedbackOutputKey);
			// Non-tracing kernels must not inherit a TLAS reservation: the encoders
			// require every declared AS at dispatch, even when a shader does not use it.
			std::vector<LayoutBindingRequirement> dataBindings(atmosphere.m_Layout->GetBindings().begin(),
				atmosphere.m_Layout->GetBindings().end());
			dataBindings.push_back(rayInput);
			dataBindings.push_back(rayOutput);
			dataBindings.push_back(statusOutput);
			dataBindings.push_back(readinessOutput);
			dataBindings.push_back(feedbackOutput);
			const std::array dataFragments{ LayoutFragment{ "GIProbeUpdateData", dataBindings } };
			const auto dataLayout = ComposeLayoutFragments(dataFragments);
			if (!dataLayout.IsSuccess())
			{
				return RenderFrameValidationResult::ContractFailure("Probe data layout composition failed");
			}
			state.m_DataLayout = ResolvedRenderLayout::Create(services.m_Presentation->GetRHIContext()->GetPipelineSystem(),
				*dataLayout.m_Layout, "GI.ProbeUpdate.Data");
			const auto dataInput = dataLayout.m_Layout->FindParameterIndex(RayInputKey);
			const auto dataOutput = dataLayout.m_Layout->FindParameterIndex(RayOutputKey);
			const auto dataStatus = dataLayout.m_Layout->FindParameterIndex(StatusOutputKey);
			if (!dataInput || !dataOutput || !dataStatus)
			{
				return RenderFrameValidationResult::ContractFailure("Probe data binding unavailable");
			}
			state.m_DataRayInput = *dataInput;
			state.m_DataRayOutput = *dataOutput;
			state.m_DataStatusOutput = *dataStatus;
			state.m_DataReadinessOutput = *dataLayout.m_Layout->FindParameterIndex(ReadinessOutputKey);
			state.m_DataFeedbackOutput = *dataLayout.m_Layout->FindParameterIndex(FeedbackOutputKey);
			state.m_DataScene = ResolvedBindingRef<SceneGPU>::Resolve(state.m_DataLayout, scene_bindings::SceneConstants);
			state.m_DataAtmosphere = ResolvedBindingRef<AtmosphereGPU>::Resolve(state.m_DataLayout, scene_bindings::AtmosphereConstants);
			state.m_SceneBinding = ResolvedBindingRef<SceneGPU>::Resolve(state.m_Layout, scene_bindings::SceneConstants);
			state.m_Atmosphere = ResolvedBindingRef<AtmosphereGPU>::Resolve(state.m_Layout, scene_bindings::AtmosphereConstants);
			auto resources = std::make_shared<Resources>();
			resources->m_Device = &device;
			const uint32_t probes = profile.m_Counts[0] * profile.m_Counts[1] * profile.m_Counts[2];
			resources->m_RayDesc = { .m_SizeInBytes = uint64_t(probes) * profile.m_RaysPerProbe * gi_shader::GIProbeRayStride,
				.m_StrideInBytes = gi_shader::GIProbeRayStride,
				.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess |
					RHIBufferUsage::CopySource,
				.m_DebugName = state.m_Field ? "GI.Field.Rays" : "GI.ProbeUpdate.Rays" };
			resources->m_Rays = RHIBufferOwner(&device, device.CreateBuffer(resources->m_RayDesc));
			if (!resources->m_Rays)
			{
				return RenderFrameValidationResult::ContractFailure("Probe ray buffer allocation failed");
			}
			auto& diagnostics = state.m_Progress->m_Diagnostics;
			diagnostics.m_ProbeCount = probes;
			diagnostics.m_RayCapacityBytes = resources->m_RayDesc.m_SizeInBytes;
			for (size_t index = 0; index < Surfaces.size(); ++index)
			{
				if (index == static_cast<size_t>(ProbeSurface::Classification) && !state.m_Field)
				{
					continue;
				}
				const auto& surface = Surfaces[index];
				const uint32_t width = SurfaceTileWidth(profile, index);
				auto& desc = resources->m_Descriptions[index];
				desc = { .m_Format = surface.m_Format,
					.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess | RHITextureUsage::CopySource,
					.m_Extent = { profile.m_Counts[0] * width, profile.m_Counts[2] * width, 1 },
					.m_ArraySize = static_cast<uint16_t>(profile.m_Counts[1]), .m_DebugName = state.m_Field ? FieldSurfaces[index] : surface.m_Name };
				if (!device.QueryTextureSupport(desc).IsSupported())
				{
					return RenderFrameValidationResult::ContractFailure("Probe atlas format or extent unsupported");
				}
				resources->m_Textures[index] = RHITextureOwner(&device, device.CreateTexture({ .m_Desc = desc }));
				if (!resources->m_Textures[index])
				{
					return RenderFrameValidationResult::ContractFailure("Probe atlas allocation failed");
				}
				diagnostics.m_FieldLogicalBytes += LogicalSurfaceBytes(probes, surface, width);
			}
			resources->m_StatusDesc = { .m_SizeInBytes = sizeof(GIUpdateStatus),
				.m_StrideInBytes = sizeof(GIUpdateStatus),
				.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySource,
				.m_DebugName = state.m_Field ? "GI.Field.UpdateStatus" : "GI.ProbeUpdate.UpdateStatus" };
			resources->m_Status = RHIBufferOwner(&device, device.CreateBuffer(resources->m_StatusDesc));
			const uint32_t slots = services.m_Presentation->GetRHIContext()->GetFrameSlotCount();
			if (!resources->m_Status || slots == 0)
			{
				return RenderFrameValidationResult::ContractFailure("Probe update status allocation failed");
			}
			if (state.m_Progress->m_ReadinessSettings.m_Enabled)
			{
				resources->m_ReadinessDesc = { .m_SizeInBytes = 16 + uint64_t(probes) * sizeof(GIFieldProbeChange),
					.m_StrideInBytes = 16,
					.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySource,
					.m_DebugName = "GI.Field.Readiness" };
				resources->m_Readiness = RHIBufferOwner(&device, device.CreateBuffer(resources->m_ReadinessDesc));
				if (!resources->m_Readiness)
				{
					return RenderFrameValidationResult::ContractFailure("Field readiness buffer allocation failed");
				}
				state.m_Progress->m_ReadbackBytes += resources->m_ReadinessDesc.m_SizeInBytes;
				diagnostics.m_FieldLogicalBytes += resources->m_ReadinessDesc.m_SizeInBytes;
			}
			if (state.m_Progress->m_Feedback.m_Enabled)
			{
				resources->m_FeedbackDesc = { .m_SizeInBytes = uint64_t(probes) * sizeof(FeedbackRecord),
					.m_StrideInBytes = 16, .m_Usage = RHIBufferUsage::Structured |
						RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySource,
					.m_DebugName = "GI.Field.Feedback" };
				resources->m_Feedback = RHIBufferOwner(&device, device.CreateBuffer(resources->m_FeedbackDesc));
				if (!resources->m_Feedback)
				{
					return RenderFrameValidationResult::ContractFailure("Field feedback diagnostics allocation failed");
				}
				state.m_Progress->m_FeedbackOffset = state.m_Progress->m_ReadbackBytes;
				state.m_Progress->m_ReadbackBytes += resources->m_FeedbackDesc.m_SizeInBytes;
				diagnostics.m_FieldLogicalBytes += resources->m_FeedbackDesc.m_SizeInBytes;
			}
			state.m_Progress->m_Readbacks.resize(slots);
			for (uint32_t index = 0; index < slots; ++index)
			{
				RHIBufferOwner readback(&device, device.CreateBuffer({
					.m_SizeInBytes = state.m_Progress->m_ReadbackBytes, .m_Usage = RHIBufferUsage::CopyDest,
					.m_MemoryUsage = RHIMemoryUsage::GpuToCpu, .m_DebugName = "GI.UpdateStatus.Readback" }));
				if (!readback)
				{
					return RenderFrameValidationResult::ContractFailure("Probe update status readback allocation failed");
				}
				state.m_Progress->m_Readbacks[index].m_Buffer = readback.Get();
				resources->m_Readbacks.push_back(std::move(readback));
			}
			diagnostics.m_StatusReadbackSlots = slots;
			state.m_Progress->m_RaysPerProbe = profile.m_RaysPerProbe;
			state.m_Progress->m_IrradianceInteriorWidth = profile.m_IrradianceInteriorWidth;
			diagnostics.m_FieldLogicalBytes += sizeof(GIUpdateStatus);
			state.m_Progress->m_Device = &device;
			state.m_Resources = std::move(resources);
		}
		if (firstPreparation)
		{
			state.m_Progress->m_Diagnostics.m_AllocationMilliseconds =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prepareBegin).count();
		}
		for (uint32_t index = 0; index < ProgramRefs.size(); ++index)
		{
			if ((index == static_cast<size_t>(ProbeStage::Classify) && !state.m_Field) ||
				(index == static_cast<size_t>(ProbeStage::ReadinessFloor) && !state.m_Progress->m_ReadinessSettings.m_Enabled))
			{
				continue;
			}
			const auto lookupBegin = std::chrono::steady_clock::now();
			const auto& programRef = *ProgramRefs[index];
			if (!programRef.IsValid())
			{
				return RenderFrameValidationResult::ContractFailure("Probe shader identity is invalid",
					PassInfos[index].m_TypeName);
			}
			const auto program = services.m_ShaderPrograms->LoadProgram(programRef);
			const auto generation = services.m_ShaderPrograms->GetGeneration(program);
			if (!program.IsValid() || generation == 0)
			{
				return RenderFrameValidationResult::ContractFailure("Probe shader artifact is unavailable",
					programRef.m_ProgramId + ":" + programRef.m_VariantId);
			}
			if (state.m_Generations[index] != 0 && state.m_Generations[index] != generation)
			{
				return RenderFrameValidationResult::ContractFailure("Probe update programs changed; replace the update owner");
			}
			state.m_Generations[index] = generation;
			const auto pipelineBegin = std::chrono::steady_clock::now();
			if (firstPreparation)
			{
				state.m_Progress->m_Diagnostics.m_ShaderLookupMilliseconds +=
					std::chrono::duration<double, std::milli>(pipelineBegin - lookupBegin).count();
			}
			const auto& layout = index == static_cast<size_t>(ProbeStage::TraceShade) ||
				index == static_cast<size_t>(ProbeStage::Classify) ?
				state.m_Layout : state.m_DataLayout;
			const ComputePipelineRecipe recipe{ .m_BindingLayout = layout.GetNativeLayout(), .m_CSId = program };
			state.m_Pipelines[index] = layout.Resolve(*services.m_PipelineResolver, state.m_Slots[index], recipe, PassInfos[index]);
			if (firstPreparation)
			{
				state.m_Progress->m_Diagnostics.m_PipelineMilliseconds +=
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pipelineBegin).count();
			}
			if (!state.m_Pipelines[index].IsValid())
			{
				return RenderFrameValidationResult::ContractFailure("Probe workload pipeline unavailable");
			}
		}
		if (firstPreparation)
		{
			state.m_Progress->m_Diagnostics.m_PreparationMilliseconds =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prepareBegin).count();
		}
		if (frame.m_FrameSlotIndex >= state.m_Progress->m_Readbacks.size() ||
			state.m_Progress->m_Readbacks[frame.m_FrameSlotIndex].m_Fence.IsValid())
		{
			return RenderFrameValidationResult::ContractFailure("Probe status readback slot is still in flight");
		}
		return RenderFrameValidationResult::Ready();
	}

	void GIProbeUpdateWorkload::AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services, const RaySceneGraphResources& scene,
		std::shared_ptr<GIFieldUpdateState> fieldUpdate, bool compose, float viewBias,
		GIFieldIdentity identity) noexcept
	{
		auto& state = *m_State;
		if (!state.m_Profile.m_ConstantEnvironment)
		{
			const auto& bake = services.m_Environment->GetBakingStatus();
			if (!CanUpdateGIPublishedEnvironment(bake.m_HasActiveBake, bake.m_ActiveGeneration,
				services.m_Environment->GetBakeRequestGeneration(),
				frame.m_RenderScene.m_WorldLightingGeneration, state.m_Progress->m_StorageInitialized))
			{
				// Initial work waits for publication; existing work must follow the
				// built active generation, not pause for a newer staging bake.
				return;
			}
		}
		const auto resources = state.m_Resources;
		const auto progress = state.m_Progress;
		const auto profile = state.m_Profile;
		const bool feedback = progress->m_Feedback.m_Enabled;
		const bool initialized = progress->m_StorageInitialized;
		const auto readiness = GetReadiness();
		const float irradianceDecay = readiness.m_Enabled && readiness.m_Refreshing ?
			progress->m_ReadinessSettings.m_RefreshHysteresis : profile.m_Hysteresis;
		const bool refreshDistance = readiness.m_Refreshing && progress->m_RefreshDistance;
		const float distanceDecay = readiness.m_Enabled && refreshDistance ?
			progress->m_ReadinessSettings.m_RefreshHysteresis : profile.m_Hysteresis;
		const auto irradianceHistory = progress->m_IrradianceHistory.Next(irradianceDecay, readiness.m_Refreshing);
		const auto distanceHistory = progress->m_DistanceHistory.Next(distanceDecay, refreshDistance);
		const float updateHysteresis = readiness.m_Enabled ? irradianceHistory.m_Hysteresis : irradianceDecay;
		const float distanceHysteresis = readiness.m_Enabled ? distanceHistory.m_Hysteresis : distanceDecay;
		const uint32_t update = static_cast<uint32_t>(progress->m_Diagnostics.m_SubmittedUpdates);
		const uint32_t total = progress->m_Diagnostics.m_ProbeCount;
		const bool record = !progress->m_Diagnostics.m_Failed &&
			(profile.m_MaximumUpdates == 0 || update < profile.m_MaximumUpdates);
		const auto range = GetGIProbeUpdateRange(profile, update);
		const uint32_t begin = range.m_First;
		const uint32_t count = range.m_Count;
		struct Imported
		{
			RGBufferId m_Rays;
			RGBufferId m_Status;
			RGBufferId m_Readiness;
			RGBufferId m_Feedback;
			std::array<RGTextureId, Surfaces.size()> m_Textures;
		};
		auto imported = std::make_shared<Imported>();
		struct Empty {};
		graph.AddPass<Empty>(state.m_Field ? "GI.Field.Import" : "GI.ProbeUpdate.Import",
			[resources, imported, initialized, field = state.m_Field](RenderGraph::RGBuilder& builder, Empty&)
			{
				imported->m_Rays = builder.ImportBuffer(field ? "GI.Field.Rays" : "GI.ProbeUpdate.Rays", resources->m_Rays.Get(),
					resources->m_RayDesc, initialized ? RGBufferAccess::StructuredRead : RGBufferAccess::None,
					initialized ? RGContentValidity::Defined : RGContentValidity::Undefined);
				imported->m_Status = builder.ImportBuffer(field ? "GI.Field.UpdateStatus" : "GI.ProbeUpdate.UpdateStatus",
					resources->m_Status.Get(), resources->m_StatusDesc,
					initialized ? RGBufferAccess::StructuredRead : RGBufferAccess::None,
					initialized ? RGContentValidity::Defined : RGContentValidity::Undefined);
				if (resources->m_Readiness)
				{
					imported->m_Readiness = builder.ImportBuffer("GI.Field.Readiness", resources->m_Readiness.Get(),
						resources->m_ReadinessDesc, initialized ? RGBufferAccess::StructuredRead : RGBufferAccess::None,
						initialized ? RGContentValidity::Defined : RGContentValidity::Undefined);
				}
				if (resources->m_Feedback)
				{
					imported->m_Feedback = builder.ImportBuffer("GI.Field.Feedback", resources->m_Feedback.Get(),
						resources->m_FeedbackDesc, initialized ? RGBufferAccess::StructuredRead : RGBufferAccess::None,
						initialized ? RGContentValidity::Defined : RGContentValidity::Undefined);
				}
				for (size_t index = 0; index < imported->m_Textures.size(); ++index)
				{
					if (!resources->m_Textures[index])
					{
						continue;
					}
					imported->m_Textures[index] = builder.ImportTexture(field ? FieldSurfaces[index] : Surfaces[index].m_Name, resources->m_Textures[index].Get(),
						resources->m_Descriptions[index],
						initialized ? ToRHIResourceState(RGTextureAccess::Sample) : UndefinedRHITextureState(),
						initialized ? RGContentValidity::Defined : RGContentValidity::Undefined);
				}
			});
		if (record)
		{
			const std::array stages{ ProbeStage::Initialize, ProbeStage::Classify, ProbeStage::ReadinessFloor, ProbeStage::TraceShade,
				profile.m_SharedBlend ? ProbeStage::BlendShared : ProbeStage::BlendDirect, ProbeStage::Borders };
			for (const auto stage : stages)
			{
				const auto stageIndex = static_cast<size_t>(stage);
				const bool tracing = stage == ProbeStage::TraceShade;
				const bool measuring = stage == ProbeStage::ReadinessFloor;
				const bool classifying = stage == ProbeStage::Classify;
				const bool querying = tracing || classifying;
				const bool blending = stage == ProbeStage::BlendDirect || stage == ProbeStage::BlendShared;
				if (stage == ProbeStage::Initialize && initialized)
				{
					continue;
				}
				if (measuring && !readiness.m_Enabled)
				{
					continue;
				}
				if (classifying && (initialized || !state.m_Field))
				{
					continue;
				}
				struct PassData
				{
					std::array<RGTextureViewId, Surfaces.size()> m_Views;
					RGTextureViewId m_Transmittance;
					TypedConstantSnapshot<AtmosphereGPU> m_Atmosphere;
					Parameters m_Parameters;
				};
				const char* name = tracing && profile.m_TraversalOnly ?
					"GI.ProbeUpdate.TraversalOnly" :
					state.m_Field ? FieldPasses[stageIndex] : PassInfos[stageIndex].m_TypeName.c_str();
				graph.AddPass<PassData>(name, RGPassEncoderType::Compute,
					[=, &frame](RenderGraph::RGBuilder& builder, PassData& data)
					{
						if (stage == ProbeStage::Initialize)
						{
							builder.WriteInPlace(imported->m_Status, RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
						}
						else
						{
							builder.ReadWriteInPlace(imported->m_Status, RGBufferAccess::StorageReadWrite, RHIStage::ComputeShader);
						}
						if (imported->m_Readiness.IsValid() &&
							(stage == ProbeStage::Initialize || measuring || blending))
						{
							if (stage == ProbeStage::Initialize)
							{
								builder.WriteInPlace(imported->m_Readiness, RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
							}
							else
							{
								builder.ReadWriteInPlace(imported->m_Readiness, RGBufferAccess::StorageReadWrite, RHIStage::ComputeShader);
							}
						}
						if (imported->m_Feedback.IsValid() && (stage == ProbeStage::Initialize || tracing))
						{
							builder.WriteInPlace(imported->m_Feedback, RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
						}
						for (size_t index = 0; index < Surfaces.size(); ++index)
						{
							if (!imported->m_Textures[index].IsValid())
							{
								continue;
							}
							const bool probeState = index == static_cast<size_t>(ProbeSurface::State);
							const bool classification = index == static_cast<size_t>(ProbeSurface::Classification);
							if (tracing && !probeState)
							{
								if (feedback)
								{
									data.m_Views[index] = builder.CreateView<RHITextureViewType::ShaderResource>(
										builder.Read(imported->m_Textures[index], RGTextureAccess::Sample, RHIStage::ComputeShader));
								}
								continue;
							}
							if (stage == ProbeStage::Initialize)
							{
								builder.WriteInPlace(imported->m_Textures[index], RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
							}
							else if ((classification && (querying || blending || measuring)) ||
								(!classifying && !measuring && !classification && tracing == probeState))
							{
								builder.ReadWriteInPlace(imported->m_Textures[index], RGTextureAccess::StorageReadWrite, RHIStage::ComputeShader);
							}
							else
							{
								continue;
							}
							auto desc = MakeRHITexture2DArrayViewDesc(resources->m_Descriptions[index].m_Format,
								0, 0, profile.m_Counts[1]);
							desc.m_Type = RHITextureViewType::UnorderedAccess;
							data.m_Views[index] = builder.CreateView<RHITextureViewType::UnorderedAccess>(imported->m_Textures[index], desc);
						}
						if (stage == ProbeStage::Initialize)
						{
							builder.WriteInPlace(imported->m_Rays, RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
						}
						else if (tracing)
						{
							builder.ReadWriteInPlace(imported->m_Rays, RGBufferAccess::StorageReadWrite, RHIStage::ComputeShader);
						}
						else if (blending)
						{
							GGLAB_UNUSED(builder.Read(imported->m_Rays, RGBufferAccess::StructuredRead, RHIStage::ComputeShader));
						}
						if (querying)
						{
							ray_scene_bindings::DeclareQueryReads(builder, scene);
							if (!profile.m_ConstantEnvironment)
							{
								const auto& ibl = builder.GetBlackboard().Get<RGIBLResources>(IBLResourcesName);
								GGLAB_UNUSED(builder.Read(ibl.m_EnvironmentCubemap, RGTextureAccess::Sample, RHIStage::ComputeShader));
							}
							if (const auto* atmosphere = builder.GetBlackboard().TryGet<RGAtmosphereResources>(AtmosphereResourcesName);
								atmosphere && frame.m_RenderScene.m_WorldSun)
							{
								data.m_Transmittance = builder.CreateView<RHITextureViewType::ShaderResource>(
									builder.Read(atmosphere->m_Luts[0], RGTextureAccess::Sample, RHIStage::ComputeShader));
							}
						}
						data.m_Atmosphere = builder.GetBlackboard().Get<RGForwardAtmosphereConstants>().m_Snapshot;
						data.m_Parameters = {
							.m_GridSize = profile.m_Counts, .m_RaysPerProbe = profile.m_RaysPerProbe,
							.m_FirstProbe = begin, .m_UpdateProbeCount = count,
							.m_ProbeCount = total, .m_UpdateIndex = update,
							.m_GridMinimum = profile.m_Minimum, .m_ProbeSpacing = profile.m_Spacing,
							.m_ConstantRadiance = profile.m_ConstantRadiance, .m_MaximumDistance = profile.m_MaximumDistance,
							.m_SceneCounts = { scene.m_InstanceCount, scene.m_GeometryCount, scene.m_MaterialCount },
							.m_ConstantEnvironment = profile.m_ConstantEnvironment ? 1u : 0u,
							.m_FieldFlags = (state.m_Field ? 1u : 0u) | (feedback ? 2u : 0u),
							.m_TraverseScene = profile.m_TraverseScene ? 1u : 0u,
							.m_Rotation = Rotation(profile.m_Seed ^ (profile.m_RotateDirections ? update : 0u)),
							.m_Hysteresis = updateHysteresis, .m_RotateDirections = profile.m_RotateDirections ? 1u : 0u,
							.m_TraversalOnly = profile.m_TraversalOnly ? 1u : 0u,
							.m_IrradianceInteriorWidth = profile.m_IrradianceInteriorWidth,
							.m_ReadinessEnabled = readiness.m_Enabled ? 1u : 0u,
							.m_ReadinessEpoch = static_cast<uint32_t>(progress->m_ReadinessEpoch),
							.m_DistanceHysteresis = distanceHysteresis,
							.m_ReadinessDistance = progress->m_RefreshDistance ? 1u : 0u,
							.m_FeedbackViewBias = viewBias };
						builder.SideEffect();
					},
					[=, this, &frame, &services](RGExecuteContext& execute, PassData& data)
					{
						auto& current = *m_State;
						auto* commands = execute.GetDirectComputeCommandContext();
						const auto& layout = querying ? current.m_Layout : current.m_DataLayout;
						const auto& sceneBinding = querying ? current.m_SceneBinding : current.m_DataScene;
						const auto& atmosphereBinding = querying ? current.m_Atmosphere : current.m_DataAtmosphere;
						ScopedConstantBindings<RHIComputeCommandContext> constants(*commands,
							layout, sceneBinding, atmosphereBinding);
						if (!commands->RetainForSubmission(resources) || !commands->RetainForSubmission(scene.m_Lifetime) ||
							!constants.SetPipeline(current.m_Pipelines[stageIndex]) ||
							!constants.Bind(sceneBinding, frame.m_FrameConstants.m_SceneSnapshot) ||
							!constants.Bind(atmosphereBinding, data.m_Atmosphere))
						{
							return;
						}
						commands->SetReadWriteBuffer(querying ? current.m_StatusOutput : current.m_DataStatusOutput,
							resources->m_Status.Get());
						commands->SetReadWriteBuffer(querying ? current.m_ReadinessOutput : current.m_DataReadinessOutput,
							resources->m_Readiness ? resources->m_Readiness.Get() : resources->m_Status.Get());
						commands->SetReadWriteBuffer(querying ? current.m_FeedbackOutput : current.m_DataFeedbackOutput,
							resources->m_Feedback ? resources->m_Feedback.Get() : resources->m_Status.Get());
						if (querying)
						{
							// The composed layout cannot inherit a prior pass/frame's light table.
							// Sun hit lighting must read this frame slot alongside its Scene constants.
							commands->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::LightSB),
								services.m_FrameBuffers->GetLightStructuredBuffer()->GetBufferHandle(frame.m_FrameSlotIndex));
							if (!ray_scene_bindings::BindQueryResources(*commands, execute, scene, current.m_Bindings))
							{
								return;
							}
						}
						if (stage == ProbeStage::Initialize || tracing)
						{
							commands->SetReadWriteBuffer(tracing ? current.m_RayOutput : current.m_DataRayOutput,
								resources->m_Rays.Get());
						}
						else if (blending)
						{
							commands->SetReadOnlyBuffer(current.m_DataRayInput, resources->m_Rays.Get());
						}
						auto parameters = data.m_Parameters;
						for (size_t index = 0; index < Surfaces.size(); ++index)
						{
							if (data.m_Views[index].IsValid())
							{
								parameters.m_Targets[index] = execute.GetViewDescriptor(data.m_Views[index]).m_Index;
							}
						}
						if (data.m_Transmittance.IsValid())
						{
							parameters.m_Transmittance = execute.GetViewDescriptor(data.m_Transmittance).m_Index;
						}
						parameters.m_AtmosphereSampler = services.m_Samplers->GetSamplerIndex(SamplerPreset::LinearClamp);
						if (!commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
						{
							return;
						}
						if (stage == ProbeStage::Initialize)
						{
							const uint32_t width = std::max(SurfaceTileWidth(profile, 0),
								gi_shader::GIProbeDistanceTileWidth);
							commands->Dispatch(
								profile.m_Counts[0] * width / gi_shader::GIQueryGroupWidth,
								profile.m_Counts[2] * width / gi_shader::GIQueryGroupHeight,
								profile.m_Counts[1]);
						}
						else
						{
							commands->Dispatch(measuring ? 1u : classifying ? total : count, 1, 1);
						}
						if (tracing)
						{
							const auto lighting = frame.m_RenderScene.m_WorldLightingGeneration;
							const uint64_t environment = profile.m_ConstantEnvironment ?
								0 : services.m_Environment->GetBakingStatus().m_ActiveGeneration;
							execute.DeferCommit([progress, lighting, environment, attenuation = data.m_Transmittance.IsValid()]
								(const RGCommitContext&) noexcept
								{
									progress->m_Diagnostics.m_WorldLightingGeneration = lighting;
									progress->m_Diagnostics.m_EnvironmentGeneration = environment;
									progress->m_Diagnostics.m_AtmosphereAttenuation = attenuation;
								});
						}
						if (stage == ProbeStage::Borders)
						{
							if (fieldUpdate)
							{
								fieldUpdate->MarkRecorded(frame.m_FrameSerial);
							}
							execute.DeferCommit([progress, update, fieldUpdate, irradianceHistory, distanceHistory,
								readinessEnabled = readiness.m_Enabled, serial = frame.m_FrameSerial]
								(const RGCommitContext&) noexcept
								{
									if (fieldUpdate)
									{
										const bool accepted = fieldUpdate->Commit(serial);
										GGLAB_ASSERT_MSG(accepted, "GI field CPU publication requires matching Submitted settlement.");
										if (!accepted)
										{
											return;
										}
									}
									if (readinessEnabled)
									{
										progress->m_IrradianceHistory = irradianceHistory;
										progress->m_DistanceHistory = distanceHistory;
									}
									progress->m_StorageInitialized = true;
									progress->m_Diagnostics.m_SubmittedUpdates = uint64_t(update) + 1;
								});
						}
					});
			}
			struct ReadbackData
			{
				RGBufferId m_Source;
				RGBufferId m_Destination;
				RGBufferId m_Readiness;
				RGBufferId m_Feedback;
			};
			const auto slot = frame.m_FrameSlotIndex;
			graph.AddPass<ReadbackData>(state.m_Field ? "GI.Field.ReadStatus" : "GI.ProbeUpdate.ReadStatus",
				[=](RenderGraph::RGBuilder& builder, ReadbackData& data)
				{
					data.m_Source = builder.Read(imported->m_Status, RGBufferAccess::CopySource, RHIStage::Copy);
					if (imported->m_Readiness.IsValid())
					{
						data.m_Readiness = builder.Read(imported->m_Readiness, RGBufferAccess::CopySource, RHIStage::Copy);
					}
					if (imported->m_Feedback.IsValid())
					{
						data.m_Feedback = builder.Read(imported->m_Feedback, RGBufferAccess::CopySource, RHIStage::Copy);
					}
					data.m_Destination = builder.ImportBuffer("GI.UpdateStatus.Readback", resources->m_Readbacks[slot].Get(),
						{ .m_SizeInBytes = progress->m_ReadbackBytes, .m_Usage = RHIBufferUsage::CopyDest,
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
					commands->CopyBuffer(execute.GetBufferHandle(data.m_Destination), 0,
						execute.GetBufferHandle(data.m_Source), 0, sizeof(GIUpdateStatus));
					if (data.m_Readiness.IsValid())
					{
						commands->CopyBuffer(execute.GetBufferHandle(data.m_Destination), sizeof(GIUpdateStatus),
							execute.GetBufferHandle(data.m_Readiness), 0, resources->m_ReadinessDesc.m_SizeInBytes);
					}
					if (data.m_Feedback.IsValid())
					{
						commands->CopyBuffer(execute.GetBufferHandle(data.m_Destination), progress->m_FeedbackOffset,
							execute.GetBufferHandle(data.m_Feedback), 0, resources->m_FeedbackDesc.m_SizeInBytes);
					}
					const GIObservationStamp observation{ .m_Field = identity, .m_ProducingFrame = frame.m_FrameSerial,
						.m_FieldUpdate = uint64_t(update) + 1,
						.m_ReadinessUpdate = uint64_t(update) + 1 - identity.m_EpochOriginUpdate };
					execute.DeferCommit(
						[progress, slot, update, updateHysteresis, refreshing = readiness.m_Refreshing, observation]
						(const RGCommitContext& commit) noexcept
						{
							progress->m_Readbacks[slot].m_Fence = commit.m_Fence;
							progress->m_Readbacks[slot].m_Update = uint64_t(update) + 1;
							progress->m_Readbacks[slot].m_Observation = observation;
							progress->m_Readbacks[slot].m_Hysteresis = updateHysteresis;
							progress->m_Readbacks[slot].m_Refreshing = refreshing;
						});
				});
		}
		if (imported->m_Readiness.IsValid())
		{
			graph.AddPass<Empty>("GI.Field.PublishReadiness",
				[imported](RenderGraph::RGBuilder& builder, Empty&)
				{
					builder.Export(imported->m_Readiness, RGBufferAccess::StructuredRead);
					builder.SideEffect();
				});
		}
		if (imported->m_Feedback.IsValid())
		{
			graph.AddPass<Empty>("GI.Field.PublishFeedback",
				[imported](RenderGraph::RGBuilder& builder, Empty&)
				{
					builder.Export(imported->m_Feedback, RGBufferAccess::StructuredRead);
					builder.SideEffect();
				});
		}
		graph.AddPass<Empty>(state.m_Field ? "GI.Field.QueryResources" : "GI.ProbeUpdate.Publish",
			[imported, profile, compose, viewBias, field = state.m_Field, &services](RenderGraph::RGBuilder& builder, Empty&)
			{
				// The next frame imports these persistent resources in the exported states.
				if (!field)
				{
					builder.Export(imported->m_Rays, RGBufferAccess::StructuredRead);
					builder.Export(imported->m_Status, RGBufferAccess::StructuredRead);
					for (const auto texture : imported->m_Textures)
					{
						if (texture.IsValid())
						{
							builder.Export(texture, RGTextureAccess::Sample);
						}
					}
				}
				if (field)
				{
					builder.GetBlackboard().Create<RGGIFieldResources>() = {
						.m_Parameters = { .m_Counts = profile.m_Counts,
							.m_IrradianceInteriorWidth = profile.m_IrradianceInteriorWidth,
							.m_Minimum = profile.m_Minimum, .m_Spacing = profile.m_Spacing,
							.m_ViewBias = viewBias },
						.m_Textures = { imported->m_Textures[0], imported->m_Textures[1], imported->m_Textures[3] },
						.m_State = imported->m_Textures[2], .m_Rays = imported->m_Rays,
						.m_Status = imported->m_Status,
						.m_Compose = compose };
				}
				builder.SideEffect();
			},
			[resources](RGExecuteContext& execute, Empty&)
			{
				GGLAB_UNUSED(execute.GetGraphicsCommandContext()->RetainForSubmission(resources));
			});
	}

	void GIProbeUpdateWorkload::ExportFieldResources(RenderGraph& graph) noexcept
	{
		const auto* field = graph.GetBlackboard().TryGet<RGGIFieldResources>();
		if (!field)
		{
			return;
		}
		struct Empty {};
		graph.AddPass<Empty>("GI.Field.Publish",
			[field](RenderGraph::RGBuilder& builder, Empty&)
			{
				// Export closes access for this graph. Every receiver must declare its
				// reads first; these final states govern the following frame's import.
				for (const auto texture : field->m_Textures)
				{
					builder.Export(texture, RGTextureAccess::Sample);
				}
				builder.Export(field->m_State, RGTextureAccess::Sample);
				builder.Export(field->m_Rays, RGBufferAccess::StructuredRead);
				builder.Export(field->m_Status, RGBufferAccess::StructuredRead);
				builder.SideEffect();
			},
			[resources = m_State->m_Resources](RGExecuteContext& execute, Empty&)
			{
				GGLAB_UNUSED(execute.GetGraphicsCommandContext()->RetainForSubmission(resources));
			});
	}
}
