#include "Application/Lab/Sessions/CoastalRaySceneLabSession.h"
#include "Application/Lab/Validation/CoastalRaySceneOracle.h"
#include "Application/Content/RuntimeAssetBundle.h"
#include "Application/Demo/CoastalSceneCameraPaths.h"
#include "Application/Demo/CoastalSceneLightingProfiles.h"
#include "Application/Demo/CoastalSceneReferenceViews.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetLoadProgress.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/RaySceneSunVisibility.h"
#include "GGLabRuntime/Graphics/Pipeline/RaySceneHitMaterial.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Scene/Components.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		const LabParameterId EnabledId("coastal_ray_scene.enabled");
		const LabParameterId ExerciseUpdatesId("coastal_ray_scene.exercise_updates");
		constexpr uint32_t ProbeWidth = CoastalProbeWidth;
		using HitResult = CoastalProbeHit;
		constexpr uint64_t ResultBytes = CoastalProbeRayCount * sizeof(HitResult);
		constexpr uint64_t ReadbackBytes = ResultBytes + CoastalOracleGeometryBytes + CoastalOracleInstanceBytes;
		struct Parameters
		{
			uint32_t m_Width = ProbeWidth;
			uint32_t m_InstanceCount = 0;
			uint32_t m_GeometryCount = 0;
			float m_OriginOffsetX = 0;
		};
		static_assert(sizeof(Parameters) == 16 && offsetof(Parameters, m_OriginOffsetX) == 12);
		const RenderPassInfo ProbeInfo{ .m_TypeName = "Lab.CoastalRayScene.Probe",
			.m_DisplayName = "Coastal ray-scene membership probe", .m_CategoryName = "Ray Scene",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute };
	}

	struct CoastalRaySceneLabState
	{
		struct Readback
		{
			RHIBufferOwner m_Buffer;
			RHIFencePoint m_Fence;
		};
		RayScene m_Scene;
		World* m_World = nullptr;
		AssetManager* m_Assets = nullptr;
		RHIDevice* m_Device = nullptr;
		RHIBufferOwner m_Results;
		std::vector<Readback> m_Readbacks;
		uint64_t m_VerifiedFrames = 0;
		uint32_t m_HitCount = 0;
		bool m_Enabled = true;
		bool m_ExerciseUpdates = false;
		bool m_UpdatesVerified = false;
		uint32_t m_UpdateStep = 0;
		bool m_Failed = false;
		float m_ProbeOffsetX = 0;
		std::string m_FailureReason;

		bool Initialize(RHIContext& context) noexcept
		{
			m_Device = &context.GetDevice();
			m_Results = RHIBufferOwner(m_Device, m_Device->CreateBuffer({ .m_SizeInBytes = ResultBytes,
				.m_StrideInBytes = sizeof(HitResult), .m_Usage = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySource }));
			m_Readbacks.resize(context.GetFrameSlotCount());
			for (auto& slot : m_Readbacks)
			{
				slot.m_Buffer = RHIBufferOwner(m_Device, m_Device->CreateBuffer({ .m_SizeInBytes = ReadbackBytes,
					.m_Usage = RHIBufferUsage::CopyDest, .m_MemoryUsage = RHIMemoryUsage::GpuToCpu }));
				if (!slot.m_Buffer)
				{
					return false;
				}
			}
			return bool(m_Results);
		}

		void CheckReadback(uint32_t index) noexcept
		{
			auto& slot = m_Readbacks[index];
			if (!slot.m_Fence.IsValid())
			{
				return;
			}
			if (m_Device->QueryFencePoint(slot.m_Fence) != RHIFenceQueryResult::Completed)
			{
				m_Failed = true;
				return;
			}
			const auto* hits = static_cast<const HitResult*>(m_Device->MapBuffer(slot.m_Buffer.Get(), { 0, ReadbackBytes }));
			if (!hits)
			{
				m_Failed = true;
				return;
			}
			m_HitCount = 0;
			for (uint32_t ray = 0; ray < CoastalProbeRayCount; ++ray)
			{
				const auto& hit = hits[ray];
				if (hit.m_Errors != 0 || hit.m_Hit > 1 || (hit.m_Hit != 0 &&
					(!std::isfinite(hit.m_Distance) || hit.m_Distance < 0.001f || hit.m_Distance > 100.0f ||
						!std::isfinite(hit.m_Barycentrics[0]) || !std::isfinite(hit.m_Barycentrics[1]) ||
						hit.m_Barycentrics[0] < -1e-5f || hit.m_Barycentrics[1] < -1e-5f ||
						hit.m_Barycentrics[0] + hit.m_Barycentrics[1] > 1.00001f)))
				{
					m_Failed = true;
				}
				m_HitCount += hit.m_Hit;
			}
			const auto* bytes = reinterpret_cast<const std::byte*>(hits);
			m_Failed = m_Failed ||
				!ValidateCoastalRaySceneHits({ hits, CoastalProbeRayCount }, m_FailureReason) ||
				!ValidateCoastalRaySceneTables({ bytes + ResultBytes, CoastalOracleGeometryBytes },
					{ bytes + ResultBytes + CoastalOracleGeometryBytes, CoastalOracleInstanceBytes }, m_FailureReason);
			m_Device->UnmapBuffer(slot.m_Buffer.Get(), {});
			slot.m_Fence = {};
			m_Failed = m_Failed || m_HitCount == 0;
			if (!m_Failed)
			{
				if (m_VerifiedFrames == 0)
				{
					const auto& scene = m_Scene.GetDiagnostics();
					GGLAB_LOG_GRAPHICS_INFO("Coastal RayScene verified: geometries={}, instances={}, opaque={}, excludedBlend={}, materials={}, vertexBytes={}, indexBytes={}, probeHits={}/1024, fixedHits={}, fixedMisses=2, tableDigests=matched.",
						scene.m_GeometryCount, scene.m_InstanceCount, scene.m_OpaqueInstanceCount,
						scene.m_ExcludedBlendCount, scene.m_MaterialCount, scene.m_VertexBytes, scene.m_IndexBytes,
						m_HitCount, CoastalProbeReferences.size());
				}
				++m_VerifiedFrames;
			}
		}
	};

	namespace
	{
		class CoastalRayScenePipeline final : public RenderPipelineBase
		{
		public:
			explicit CoastalRayScenePipeline(std::shared_ptr<CoastalRaySceneLabState> state) noexcept :
				m_State(std::move(state)),
				m_Raster(CreateRenderPipelineForwardPlus({ .m_EnableGIReceiver = m_State->m_Enabled })) {}
			std::string_view GetName() const noexcept override { return "Coastal ray scene"; }
			ResolvedTemporalFramePlan ResolveTemporalFramePlan(TemporalFramePlanResolveInfo info) const noexcept override
			{
				return m_Raster->ResolveTemporalFramePlan(info);
			}
			MaterialDiagnosticPrewarmProgress PrewarmMaterialDiagnostics(const RenderServices& services,
				std::span<const uint64_t> variants) noexcept override
			{
				return m_Raster->PrewarmMaterialDiagnostics(services, variants);
			}
			RenderFrameValidationResult ValidateRenderFrame(const RenderFrameContext& frame,
				const RenderServices& services) noexcept override
			{
				const auto raster = m_Raster->ValidateRenderFrame(frame, services);
				if (!raster.IsReady() || !m_State->m_Enabled)
				{
					return raster;
				}
				auto& context = *services.m_Presentation->GetRHIContext();
				const auto status = m_State->m_Scene.Prepare(*m_State->m_World, *m_State->m_Assets,
					context.GetDevice(), *services.m_Samplers);
				if (status == RaySceneStatus::NotReady || status == RaySceneStatus::Unavailable)
				{
					return RenderFrameValidationResult::Skip("Coastal ray scene is not available");
				}
				if (status != RaySceneStatus::Prepared)
				{
					return RenderFrameValidationResult::ContractFailure("Coastal ray-scene preparation failed",
						m_State->m_Scene.GetDiagnostics().m_Reason);
				}
				if (!ValidateCoastalRaySceneLayout(m_State->m_Scene.GetDiagnostics(), m_State->m_FailureReason))
				{
					return RenderFrameValidationResult::ContractFailure("Coastal ray-scene reference layout changed",
						m_State->m_FailureReason);
				}
				if (!m_Initialized)
				{
					m_Initialized = true;
					m_State->m_Failed = !m_State->Initialize(context) || !InitializeLayout(context, services);
				}
				if (!m_SunVisibility.Prepare(frame, services))
				{
					return RenderFrameValidationResult::ContractFailure("Coastal sun-visibility preparation failed");
				}
				if (!m_HitMaterial.Prepare(services))
				{
					return RenderFrameValidationResult::ContractFailure("Coastal hit-material preparation failed");
				}
				if (!m_State->m_Failed)
				{
					m_State->CheckReadback(frame.m_FrameSlotIndex);
				}
				return m_State->m_Failed ? RenderFrameValidationResult::ContractFailure(
					"Coastal ray-scene GPU membership verification failed", m_State->m_FailureReason) :
					RenderFrameValidationResult::Ready();
			}

			void BuildRenderGraph(RenderGraph& graph, const RenderFrameContext& frame,
				const RenderServices& services) noexcept override
			{
				RaySceneGraphResources rayScene;
				if (m_State->m_Enabled)
				{
					const auto scene = m_State->m_Scene.AddBuildPasses(graph);
					rayScene = scene;
					const auto counts = m_State->m_Scene.GetDiagnostics();
					struct ProbeData { RGBufferId m_Results; RGBufferId m_Readback; };
					const auto slot = frame.m_FrameSlotIndex;
					const auto probeOffset = m_State->m_ProbeOffsetX;
					auto data = std::make_shared<ProbeData>();
					graph.AddPass<ProbeData>(ProbeInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
						[=, this](RenderGraph::RGBuilder& builder, ProbeData& pass)
						{
							for (const auto id : scene.m_BottomLevels)
							{
								GGLAB_UNUSED(builder.Read(id, RGAccelerationStructureAccess::QueryRead));
							}
							GGLAB_UNUSED(builder.Read(scene.m_TopLevel, RGAccelerationStructureAccess::QueryRead));
							GGLAB_UNUSED(builder.Read(scene.m_Geometries, RGBufferAccess::StructuredRead));
							GGLAB_UNUSED(builder.Read(scene.m_Instances, RGBufferAccess::StructuredRead));
							pass.m_Results = builder.ImportBuffer("CoastalRayScene.Hits", m_State->m_Results.Get(),
								{ .m_SizeInBytes = ResultBytes, .m_StrideInBytes = sizeof(HitResult),
									.m_Usage = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySource },
								RGBufferAccess::CopySource, RGContentValidity::Undefined);
							builder.WriteInPlace(pass.m_Results, RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
							pass.m_Readback = builder.ImportBuffer("CoastalRayScene.Readback", m_State->m_Readbacks[slot].m_Buffer.Get(),
								{ .m_SizeInBytes = ReadbackBytes, .m_Usage = RHIBufferUsage::CopyDest, .m_MemoryUsage = RHIMemoryUsage::GpuToCpu },
								RGBufferAccess::CopyDest, RGContentValidity::Undefined);
							*data = pass;
						},
						[=, this, &services](RGExecuteContext& context, ProbeData& pass)
						{
							auto* commands = context.GetDirectComputeCommandContext();
							if (!commands->RetainForSubmission(scene.m_Lifetime))
							{
								return;
							}
							const auto pipeline = m_Layout.Resolve(*services.m_PipelineResolver, m_Slot, m_Recipe, ProbeInfo);
							commands->SetPipeline(pipeline.GetHandle(m_Layout, true));
							if (!commands->SetAccelerationStructure(0, context.GetAccelerationStructureHandle(scene.m_TopLevel)))
							{
								return;
							}
							commands->SetReadOnlyBuffer(1, context.GetBufferHandle(scene.m_Geometries));
							commands->SetReadOnlyBuffer(2, context.GetBufferHandle(scene.m_Instances));
							commands->SetReadWriteBuffer(3, context.GetBufferHandle(pass.m_Results));
							commands->SetInlineConstants(4, Parameters{ ProbeWidth, counts.m_InstanceCount, counts.m_GeometryCount, probeOffset });
							commands->Dispatch(ProbeWidth / 8, ProbeWidth / 8, 1);
						});
					struct EmptyData {};
					graph.AddPass<EmptyData>("Lab.CoastalRayScene.Readback", RGPassEncoderType::Copy,
						[=](RenderGraph::RGBuilder& builder, EmptyData&)
						{
							GGLAB_UNUSED(builder.Read(data->m_Results, RGBufferAccess::CopySource));
							GGLAB_UNUSED(builder.Read(scene.m_Geometries, RGBufferAccess::CopySource));
							GGLAB_UNUSED(builder.Read(scene.m_Instances, RGBufferAccess::CopySource));
							builder.WriteInPlace(data->m_Readback, RGBufferAccess::CopyDest);
							builder.SideEffect();
						},
						[this, slot, scene](RGExecuteContext& context, EmptyData&)
						{
							context.GetCopyCommandContext()->CopyBuffer(m_State->m_Readbacks[slot].m_Buffer.Get(), 0,
								m_State->m_Results.Get(), 0, ResultBytes);
							context.GetCopyCommandContext()->CopyBuffer(m_State->m_Readbacks[slot].m_Buffer.Get(), ResultBytes,
								context.GetBufferHandle(scene.m_Geometries), 0, CoastalOracleGeometryBytes);
							context.GetCopyCommandContext()->CopyBuffer(m_State->m_Readbacks[slot].m_Buffer.Get(),
								ResultBytes + CoastalOracleGeometryBytes, context.GetBufferHandle(scene.m_Instances), 0, CoastalOracleInstanceBytes);
							auto* receipt = &m_State->m_Readbacks[slot].m_Fence;
							context.DeferCommit([receipt](const RGCommitContext& commit) noexcept
								{
									*receipt = commit.m_Fence;
								});
						});
				}
				m_Raster->BuildRenderGraph(graph, frame, services);
				if (m_State->m_Enabled)
				{
					m_SunVisibility.AddPass(graph, frame, services, rayScene);
					m_HitMaterial.AddPass(graph, frame, services, rayScene);
					struct FinishData {};
					// Export seals these imports; every ray consumer must be declared first.
					graph.AddPass<FinishData>("Lab.CoastalRayScene.RestoreTables",
						[rayScene](RenderGraph::RGBuilder& builder, FinishData&)
						{
							builder.Export(rayScene.m_Geometries, RGBufferAccess::StructuredRead);
							builder.Export(rayScene.m_Instances, RGBufferAccess::StructuredRead);
						});
				}
			}

		private:
			bool InitializeLayout(RHIContext& context, const RenderServices& services) noexcept
			{
				std::array<LayoutBindingRequirement, 5> bindings;
				const std::array types{ RHIBindingType::AccelerationStructure, RHIBindingType::ReadOnlyStorageBuffer,
					RHIBindingType::ReadOnlyStorageBuffer, RHIBindingType::ReadWriteStorageBuffer, RHIBindingType::InlineConstants };
				for (uint32_t index = 0; index < bindings.size(); ++index)
				{
					bindings[index] = { .m_Key = LayoutBindingKey{ index + 1 },
						.m_Slot = { .m_Type = types[index], .m_Visibility = RHIShaderStage::Compute,
							.m_Binding = index < 3 ? index : 0, .m_Count = 1,
							.m_SizeInBytes = index == 4 ? uint32_t(sizeof(Parameters)) : 0 }, .m_ParameterOrder = index };
				}
				const std::array fragments{ LayoutFragment{ "CoastalRayScene", bindings } };
				const auto contract = ComposeLayoutFragments(fragments);
				if (!contract.IsSuccess())
				{
					return false;
				}
				m_Layout = ResolvedRenderLayout::Create(context.GetPipelineSystem(), *contract.m_Layout, "CoastalRayScene.Probe");
				m_Recipe.m_CSId = services.m_ShaderPrograms->LoadProgram(shader_programs::RaySceneProbeCompute);
				m_Recipe.m_BindingLayout = m_Layout.GetNativeLayout();
				return m_Layout.IsValid();
			}
			std::shared_ptr<CoastalRaySceneLabState> m_State;
			std::unique_ptr<RenderPipelineBase> m_Raster;
			ResolvedRenderLayout m_Layout;
			ComputePipelineRecipe m_Recipe;
			ComputePipelineSlot m_Slot;
			RaySceneSunVisibility m_SunVisibility;
			RaySceneHitMaterial m_HitMaterial;
			bool m_Initialized = false;
		};
	}

	CoastalRaySceneLabSession::CoastalRaySceneLabSession(const LabSessionCreateInfo& info) noexcept :
		LabSessionBase(GetDescriptor(), info, nullptr), m_State(std::make_shared<CoastalRaySceneLabState>())
	{
		m_State->m_World = &m_World;
		m_State->m_Assets = m_Services.m_AssetManager;
		SetRenderPipeline(std::make_unique<CoastalRayScenePipeline>(m_State));
		auto& profile = GetMutableViewRenderProfile();
		profile.m_TemporalAA.m_Enabled = false;
		profile.m_Lighting.m_GTAO.m_Enabled = true;
		profile.m_PostProcess.m_Bloom.m_Enabled = false;
		profile.m_EnableScenePreExposure = true;
		GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = EnabledId, .m_Name = "Ray Scene Enabled",
			.m_Group = "Ray Scene", .m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::RestartSession, .m_DefaultValue = true }));
		GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = ExerciseUpdatesId, .m_Name = "Exercise Transform and Entity Updates",
			.m_Group = "Ray Scene", .m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::RestartSession, .m_DefaultValue = false }));
		const bool views = GetCameraRig().SetReferenceViews({ CoastalSceneReferenceViews.begin(), CoastalSceneReferenceViews.end() });
		const bool paths = GetCameraRig().SetCameraPaths(MakeCoastalSceneCameraPaths());
		const bool restored = GetCameraRig().RestoreReferenceView("Retreat_Overview");
		GGLAB_ASSERT(views && paths && restored);
	}

	void CoastalRaySceneLabSession::OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept
	{
		LabSessionBase::OnParametersRestoredForPrepare(impact);
		m_State->m_Enabled = GetParameters().Get<bool>(EnabledId, true);
		m_State->m_ExerciseUpdates = GetParameters().Get<bool>(ExerciseUpdatesId, false);
		SetRenderPipeline(std::make_unique<CoastalRayScenePipeline>(m_State));
	}
	void CoastalRaySceneLabSession::BeginPrepare() noexcept
	{
		m_CaptureAssetBundle.reset();
		std::string failure;
		const auto bundle = LoadRuntimeAssetBundle(m_Services.m_AssetRoot / "Models/GGLabCoastalRetreat/bundle.json", failure);
		if (!bundle)
		{
			m_Progress = { .m_Status = LoadingStatus::Failed, .m_Stage = "Installed asset validation failed", .m_Detail = failure };
			return;
		}
		if (bundle->m_Id != CoastalOracleBundleId || bundle->m_Revision != CoastalOracleBundleRevision)
		{
			m_Progress = { .m_Status = LoadingStatus::Failed, .m_Stage = "Coastal ray-scene reference asset changed",
				.m_Detail = "Review the frozen probe oracle before using another asset bundle revision." };
			return;
		}
		m_Model = GetAssetOwnerScope().LoadModelAsync(bundle->m_EntryPoint).m_ModelId;
		m_CaptureAssetBundle = FrameCaptureAssetBundle{ bundle->m_Id, bundle->m_Revision };
		m_Progress = { .m_Status = m_Model.IsValid() ? LoadingStatus::Preparing : LoadingStatus::Failed,
			.m_Fraction = 0.0f, .m_Stage = "Loading Coastal Ready assets" };
	}
	void CoastalRaySceneLabSession::TickPrepare() noexcept
	{
		if (!m_Progress.IsPreparing())
		{
			return;
		}
		const auto* model = m_Services.m_AssetManager->GetModel(m_Model);
		if (!model)
		{
			m_Progress = { .m_Status = LoadingStatus::Failed, .m_Stage = "Coastal model unavailable" };
			return;
		}
		LoadingProgressBuilder progress;
		progress.AddAssetStep(1.0f, GetAssetLoadProgress(model->m_State, AssetLoadKind::Model, model->m_LoadProgress));
		m_Progress = progress.Build();
	}
	void CoastalRaySceneLabSession::CommitPrepare() noexcept
	{
		auto& registry = m_World.GetRegistry();
		const auto model = registry.create();
		registry.emplace<components::TransformComponent>(model);
		registry.emplace<components::ModelComponent>(model, m_Model);
		const auto light = registry.create();
		components::TransformComponent transform;
		auto direction = CoastalSceneLightingProfiles.front().m_Inputs.m_SunDirection;
		direction.Normalize();
		transform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
		registry.emplace<components::TransformComponent>(light, transform);
		components::LightComponent sun;
		sun.m_Intensity = 3.0f;
		sun.m_DirectionalShadowSettings.emplace();
		sun.m_WorldSun = WorldSunSettings{};
		registry.emplace<components::LightComponent>(light, sun);
		m_World.m_Atmosphere = AtmosphereSettings{};
		m_World.MarkLightingAuthoringChanged();
	}
	void CoastalRaySceneLabSession::CancelPrepare() noexcept { ResetAssetInterests(); }
	void CoastalRaySceneLabSession::OnEnter() noexcept
	{
		m_PreviousEnvironment = m_Services.m_EnvironmentLighting->GetEnvironmentLightingSettings();
		m_Entered = true;
		auto* control = m_Services.m_EnvironmentLightingControl;
		control->SetIntensity(1.0f);
		control->SetRotationRadians(0.0f);
		control->SetSkyboxEnabled(true);
		control->SetBackgroundMode(EnvironmentBackgroundMode::PhysicalSky);
	}
	void CoastalRaySceneLabSession::OnExit() noexcept
	{
		const auto& scene = m_State->m_Scene.GetDiagnostics();
		GGLAB_LOG_GRAPHICS_INFO("Coastal RayScene summary: snapshots={}, geometryRevisions={}, blasBuilds={}, tlasBuilds={}, copiedBytes={}, verifiedFrames={}, updateChecks={}.",
			scene.m_Revision, scene.m_GeometryRevision, scene.m_BottomLevelBuildCount,
			scene.m_TopLevelBuildCount, scene.m_GeometryCopyBytes, m_State->m_VerifiedFrames, m_State->m_UpdatesVerified);
		if (m_Entered)
		{
			auto* control = m_Services.m_EnvironmentLightingControl;
			control->SetIntensity(m_PreviousEnvironment.m_Intensity);
			control->SetRotationRadians(m_PreviousEnvironment.m_RotationRadians);
			control->SetSkyboxEnabled(m_PreviousEnvironment.m_EnableSkybox);
			control->SetBackgroundMode(m_PreviousEnvironment.m_BackgroundMode);
			m_Entered = false;
		}
		ResetAssetInterests();
	}
	void CoastalRaySceneLabSession::Update(float deltaTime) noexcept
	{
		UpdateCamera(deltaTime);
		if (!m_State->m_Enabled || !m_State->m_ExerciseUpdates || m_State->m_UpdatesVerified ||
			m_State->m_VerifiedFrames < uint64_t(m_State->m_UpdateStep + 1) * 8)
		{
			return;
		}
		if (m_State->m_UpdateStep == 3)
		{
			const auto& scene = m_State->m_Scene.GetDiagnostics();
			m_State->m_UpdatesVerified = scene.m_GeometryRevision == 1 &&
				scene.m_BottomLevelBuildCount == scene.m_GeometryCount && scene.m_TopLevelBuildCount == 4;
			m_State->m_Failed = !m_State->m_UpdatesVerified;
			GGLAB_LOG_GRAPHICS_INFO("Coastal RayScene update checks: passed={}, blasBuilds={}, tlasBuilds={}.",
				m_State->m_UpdatesVerified, scene.m_BottomLevelBuildCount, scene.m_TopLevelBuildCount);
			return;
		}
		auto& registry = m_World.GetRegistry();
		for (const auto entity : registry.view<components::ModelComponent>())
		{
			if (m_State->m_UpdateStep == 2)
			{
				// The recycled entity slot must produce a new instance identity, sharing BLAS.
				registry.destroy(entity);
				const auto replacement = registry.create();
				registry.emplace<components::TransformComponent>(replacement);
				registry.emplace<components::ModelComponent>(replacement, m_Model);
			}
			else
			{
				registry.get<components::TransformComponent>(entity).m_Position.m_X =
					m_State->m_UpdateStep == 0 ? 0.5f : 0.0f;
				m_State->m_ProbeOffsetX = m_State->m_UpdateStep == 0 ? 0.5f : 0.0f;
			}
			++m_State->m_UpdateStep;
			break;
		}
	}
	std::span<const WorldLightingProfile> CoastalRaySceneLabSession::GetLightingProfiles() const noexcept
	{
		return CoastalSceneLightingProfiles;
	}
	void CoastalRaySceneLabSession::BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		const auto& scene = m_State->m_Scene.GetDiagnostics();
		diagnostics.m_Title = "Coastal ray scene";
		for (const auto& [name, value] : std::array<std::pair<const char*, uint64_t>, 10>{ {
			{ "Snapshot revision", scene.m_Revision }, { "Geometry revision", scene.m_GeometryRevision },
			{ "Geometries", scene.m_GeometryCount }, { "Instance entries", scene.m_InstanceCount },
			{ "Opaque TLAS instances", scene.m_OpaqueInstanceCount }, { "Excluded BLEND entries", scene.m_ExcludedBlendCount },
			{ "BLAS builds", scene.m_BottomLevelBuildCount }, { "TLAS builds", scene.m_TopLevelBuildCount },
			{ "Geometry copy bytes", scene.m_GeometryCopyBytes }, { "Verified probe frames", m_State->m_VerifiedFrames } } })
		{
			diagnostics.m_Metrics.push_back({ name, std::to_string(value) });
		}
		diagnostics.m_Checks.push_back({ "GPU membership", m_State->m_Failed ? LabDiagnosticCheckStatus::Failed :
			m_State->m_VerifiedFrames ? LabDiagnosticCheckStatus::Passed : LabDiagnosticCheckStatus::Pending,
			m_State->m_Enabled ? scene.m_Reason : "Ray scene is disabled; no ray resources or passes are created." });
	}
	LabId CoastalRaySceneLabSession::GetId() noexcept { return LabId("gglab.lab.coastal_ray_scene"); }
	LabDescriptor CoastalRaySceneLabSession::GetDescriptor() noexcept
	{
		return { .m_Id = GetId(), .m_DisplayName = "Coastal Ray Scene", .m_Category = "Rendering",
			.m_Description = "Builds a complete Ready-asset ray snapshot and verifies GPU membership independently of raster visibility.",
			.m_Kind = LabKind::Pipeline };
	}
	std::unique_ptr<LabSessionBase> CoastalRaySceneLabSession::Create(const LabSessionCreateInfo& info) noexcept
	{
		return std::make_unique<CoastalRaySceneLabSession>(info);
	}
}
