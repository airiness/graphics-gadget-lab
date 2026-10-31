#include "RayQueryContractSelfTests.h"
#include "AssetTestServices.h"
#include "Graphics/Pipeline/GIReferenceObservation.h"
#include "Graphics/Pipeline/GIComputeDispatch.h"
#include "Graphics/Pipeline/RaySceneShaderBindings.h"
#include "Graphics/Pipeline/GISpecularVisibilityObservation.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/GIProbeUpdate.h"
#include "GGLabRuntime/Graphics/Pipeline/GIField.h"
#include "Graphics/Pipeline/GIFieldState.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/Pipeline/RaySceneSunVisibility.h"
#include "GGLabRuntime/Graphics/Pipeline/RaySceneHitMaterial.h"
#include "Graphics/RenderPass/RenderPassGIReceiver.h"
#include "Graphics/RenderPass/RenderPassGIReference.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "Graphics/RenderPass/GIReceiverGraphResources.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/ShadowGraphResources.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"
#include "GGLabRuntime/Graphics/RHI/RHIAccelerationStructure.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12BindingLayoutUtils.h"
#include "Graphics/RHI/DX12/DX12RayQuerySupport.h"
#include "Graphics/RHI/DX12/DX12PipelineSystem.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/AccelerationStructureStore.h"
#include "Graphics/RayScenePlan.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#include "Graphics/RHI/Vulkan/VulkanRayQuerySupport.h"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace gglab
{
	namespace
	{
		void RunSunVisibilityGraphTests(SelfTestContext& context) noexcept
		{
			class Samplers final : public RenderSamplerAccess
			{
			public:
				SamplerID GetOrCreateSampler(const SamplerKey&) noexcept override { return {}; }
				SamplerID GetPresetSamplerId(SamplerPreset) const noexcept override { return {}; }
				uint32_t GetSamplerIndex(SamplerPreset) const noexcept override { return 0; }
				uint32_t GetSamplerIndex(const SamplerID&) const noexcept override { return 0; }
				uint32_t ResolveSamplerIndex(SamplerID, SamplerPreset) const noexcept override { return 0; }
			} samplers;
			RenderScene scene;
			scene.m_WorldSun.emplace();
			DirectionalShadowFramePlan shadows;
			shadows.m_ShadingEnabled = true;
			const std::array<RenderQueue, 1> queues{};
			RenderFrameContext frame{ .m_RenderScene = scene, .m_RenderQueues = queues,
				.m_DirectionalShadowFramePlan = &shadows };
			const RenderServices services{ .m_Samplers = &samplers };
			asset_test::TestRHIDevice device;
			TransientResourcePool pool(&device);
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			RaySceneGraphResources inputs;
			inputs.m_GeometryCount = 1;
			inputs.m_InstanceCount = 1;
			inputs.m_MaterialCount = 1;
			struct Data {};
			graph.AddPass<Data>("ImportSunInputs", [&](RenderGraph::RGBuilder& builder, Data&)
				{
					builder.GetBlackboard().Create<RGForwardAtmosphereConstants>();
					const RHITextureDesc depthDesc{ .m_Format = RHIFormat::D32Float,
						.m_Usage = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled, .m_Extent = { 8, 8, 1 } };
					auto& depth = builder.GetBlackboard().Create<RGSceneDepthResources>(SceneDepthResourcesName);
					depth.m_Texture = builder.ImportTexture("Depth", device.CreateTexture({ .m_Desc = depthDesc }, {}), depthDesc,
						RGTextureAccess::Sample, RGContentValidity::Defined);
					depth.m_SrvDesc = MakeRHITexture2DViewDesc(RHIFormat::R32Float, 0, 1, RHITextureAspect::Depth);
					depth.m_DsvDesc = MakeRHITexture2DViewDesc(RHIFormat::D32Float, 0, 1, RHITextureAspect::Depth);
					auto& ibl = builder.GetBlackboard().Create<RGIBLResources>(IBLResourcesName);
					const RHITextureDesc lutDesc{ .m_Format = RHIFormat::R16G16Float,
						.m_Usage = RHITextureUsage::Sampled, .m_Extent = { 8, 8, 1 } };
					ibl.m_BrdfLut = builder.ImportTexture("BRDF", device.CreateTexture({ .m_Desc = lutDesc }, {}),
						lutDesc, RGTextureAccess::Sample, RGContentValidity::Defined);
					auto& shadow = builder.GetBlackboard().Create<RGShadowResources>(ShadowResourcesName);
					shadow.m_DirectionalShadowMap = builder.ImportTexture("Shadow", device.CreateTexture({ .m_Desc = depthDesc }, {}), depthDesc,
						RGTextureAccess::Sample, RGContentValidity::Defined);
					inputs.m_TopLevel = builder.Import<RGAccelerationStructureResource>("TLAS", device.CreateAccelerationStructure(
						{ .m_Type = RHIAccelerationStructureType::TopLevel, .m_SizeInBytes = 256 }),
						{ .m_Type = RHIAccelerationStructureType::TopLevel, .m_SizeInBytes = 256 },
						RGAccelerationStructureAccess::QueryRead, RGContentValidity::Defined);
					const std::array buffers{ &inputs.m_Vertices, &inputs.m_Indices, &inputs.m_Geometries, &inputs.m_Instances, &inputs.m_Materials };
					for (uint32_t index = 0; index < buffers.size(); ++index)
					{
						const RHIBufferDesc desc{ .m_SizeInBytes = 256, .m_StrideInBytes = 16, .m_Usage = RHIBufferUsage::Structured };
						*buffers[index] = builder.ImportBuffer(("Pool" + std::to_string(index)).c_str(), device.CreateBuffer(desc, {}),
							{ .m_SizeInBytes = 256, .m_StrideInBytes = 16, .m_Usage = RHIBufferUsage::Structured },
							RGBufferAccess::StructuredRead, RGContentValidity::Defined);
					}
				});
			// Compile-only cancellation exercises production declarations without executing an unprepared shader.
			RaySceneSunVisibility visibility;
			visibility.AddPass(graph, frame, services, inputs);
			RenderPassGIReceiver receivers;
			receivers.AddPass(graph, frame, services);
			RaySceneHitMaterial hitMaterial;
			hitMaterial.AddPass(graph, frame, services, inputs);
			RenderPassGIReference reference;
			reference.AddPass(graph, frame, services, inputs, {});
			graph.AddPass<Data>("ReturnSunInputs", [&](RenderGraph::RGBuilder& builder, Data&)
				{
					builder.Export(inputs.m_Geometries, RGBufferAccess::StructuredRead);
					builder.Export(inputs.m_Instances, RGBufferAccess::StructuredRead);
					const auto& receiverResources = builder.GetBlackboard().Get<RGGIReceiverResources>(
						GIReceiverResourcesName);
					for (const auto texture : receiverResources.m_Textures)
					{
						const auto& clear = builder.GetTextureDesc(texture).m_ClearValue;
						context.Check(clear && clear->m_Format == RHIFormat::R32G32B32A32Float &&
							!clear->m_IsDepthStencil && std::ranges::all_of(clear->m_Color,
								[](float value) { return value == 0.0f; }),
							"Receiver optimized clears match the zero background, including class alpha");
					}
				});
			context.Check(graph.Compile(), "Sun visibility compiles with inferred transient usage and final imported states");
			context.Check(graph.ResolveObservation({ .m_Resource = "RayScene.SunVisibility" }).m_Failure == RGObservationFailure::None,
				"The reachable float sun tap supports T2 observation without a persistent copy allocation");
			for (const auto* name : GIReceiverTextureNames)
			{
				context.Check(graph.ResolveObservation({ .m_Resource = name }).m_Failure == RGObservationFailure::None,
					"Every raster receiver attachment has defined background and supports T2 observation");
			}
			for (const auto* name : { "GI.Reference.Irradiance", "GI.Reference.Diagnostics",
				"GI.Reference.Position", "GI.Reference.Identity", "GI.Reference.LambertRadiance" })
			{
				context.Check(graph.ResolveObservation({ .m_Resource = name }).m_Failure == RGObservationFailure::None,
					"Stateless GI reference exports are reachable without a SceneColor or history dependency");
			}
			context.Check(graph.ResolveObservation({ .m_Resource = "RayScene.HitAlbedo" }).m_Failure == RGObservationFailure::None,
				"Hit material reads the frozen material table and receiver coverage before final table export");
		}

		void RunSunVisibilityPreparationTests(SelfTestContext& context) noexcept
		{
			RenderScene scene;
			DirectionalShadowFramePlan shadows;
			RenderFrameContext frame{ .m_RenderScene = scene, .m_DirectionalShadowFramePlan = &shadows };
			RaySceneSunVisibility visibility;
			context.Check(!visibility.Prepare(frame, {}),
				"Missing WorldSun rejects sun-visibility composition before any GPU service access");
			scene.m_WorldSun.emplace();
			scene.m_WorldSunLightIndex = 0;
			scene.m_DirectionalShadowLightIndex = 1;
			shadows.m_ShadingEnabled = true;
			context.Check(!visibility.Prepare(frame, {}),
				"A CSM for another directional light cannot masquerade as a sun-visibility comparison");
		}

		void RunGISharedContractTests(SelfTestContext& context) noexcept
		{
			// Pin the pre-refactor sequence, not a second copy of the hash implementation.
			uint32_t state = 61051;
			for (const uint32_t expected : { 0xe5dbcb2au, 0x0c7d240au, 0xc42aa907u, 0x21283ec9u })
			{
				const float sample = gi_shader::GIRandom(state);
				context.Check(state == expected && sample > 0 && sample < 1,
					"Shared CPU/HLSL sampling preserves the qualified serial random stream");
			}
			context.Check(gi_shader::GIUniformOpenInterval(0) > 0 &&
				gi_shader::GIUniformOpenInterval(UINT32_MAX) < 1,
				"The open-interval conversion excludes both rounded FP32 endpoints");
			context.Check(GIQueryRowsPerDispatch(1024, 64, 2) == 64 &&
				GIQueryRowsPerDispatch(1024, 0, 512) == 8192 &&
				GIQueryRowsPerDispatch(1u << 23, 4096, 512) == 8,
				"Query splitting handles sampled, resolve-only and oversized-row dispatches");

			// Parameter order is not a count: inherited layouts may contain gaps.
			std::vector<LayoutBindingRequirement> bindings
			{
				{ .m_Key = LayoutBindingKey{ 0x100 },
					.m_Slot = { .m_Type = RHIBindingType::ReadOnlyStorageBuffer,
						.m_Visibility = RHIShaderStage::Compute, .m_Binding = 0, .m_Count = 1 },
					.m_ParameterOrder = 41 },
			};
			ray_scene_bindings::Append(bindings);
			const std::array fragments{ LayoutFragment{ "RaySceneContract", bindings } };
			const auto layout = ComposeLayoutFragments(fragments);
			context.Check(layout.IsSuccess() && bindings[1].m_ParameterOrder == 42 &&
				bindings.back().m_Slot.m_Binding == 10 &&
				bindings[1].m_Slot.m_Type == RHIBindingType::AccelerationStructure,
				"Shared ray bindings append t5-t10 after the actual inherited parameter order");
		}

		void RunGIFieldTests(SelfTestContext& context) noexcept
		{
			GIFieldCreateInfo info;
			context.Check(!ValidateGIFieldCreateInfo(info), "A field cannot prepare without a scene owner");
			info.m_RayScene = std::make_shared<RayScene>();
			context.Check(ValidateGIFieldCreateInfo(info), "The Coastal full-update field profile is valid");
			for (uint32_t count : { 0u, 1u, 129u })
			{
				auto invalid = info;
				invalid.m_Volume.m_Counts[0] = count;
				context.Check(!ValidateGIFieldCreateInfo(invalid), "A field volume needs a bounded three-dimensional cage");
			}
			for (float value : { 0.0f, -1.0f, std::numeric_limits<float>::infinity(),
				std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::max() })
			{
				auto invalid = info;
				invalid.m_Volume.m_Spacing = value;
				context.Check(!ValidateGIFieldCreateInfo(invalid), "Nonfinite, collapsed and overflowing volumes reject before allocation");
			}
			auto invalid = info;
			invalid.m_Volume.m_Minimum[0] = 1e30f;
			context.Check(!ValidateGIFieldCreateInfo(invalid), "Field spacing must remain representable at its origin");
			invalid = info;
			invalid.m_RaysPerProbe = 255;
			context.Check(!ValidateGIFieldCreateInfo(invalid), "Unsupported ray profiles cannot silently change their work budget");
			GIField first(info);
			GIField replacement(info);
			context.Check(first.GetDiagnostics().m_Generation != replacement.GetDiagnostics().m_Generation &&
				!first.GetDiagnostics().m_Initialized, "Replacement fields have distinct identities and no inherited updates");
			RenderScene scene;
			RenderFrameContext frame{ .m_RenderScene = scene };
			context.Check(!first.Prepare(frame, {}).IsReady(), "A field requires the Runtime settlement service");

			const RHIFencePoint fence{ RHIFenceHandle(0, 1), 1 };
			GIFieldFrameState participant;
			auto state = std::make_shared<GIFieldUpdateState>();
			participant.BeginFrame(1);
			context.Check(participant.Track(state, 1) && !participant.Track(state, 1),
				"One field update may participate once in its matching renderer frame");
			state->MarkRecorded(1);
			context.Check(!state->Commit(1), "Recording never publishes a field update");
			participant.Settle({ RenderFrameOutcomeKind::Discarded, 1, fence });
			context.Check(!state->Commit(1) && !state->IsInvalid(),
				"Discard with a cleanup retention fence preserves the prior field without publication");
			participant.BeginFrame(2);
			context.Check(participant.Track(state, 2), "A discarded update retries on a later frame");
			state->MarkRecorded(2);
			participant.Settle({ RenderFrameOutcomeKind::Submitted, 1, fence });
			context.Check(!state->Commit(2), "An old settlement cannot acknowledge a newer field update");
			participant.Settle({ RenderFrameOutcomeKind::Submitted, 2, fence });
			context.Check(state->Commit(2) && !state->Commit(2),
				"Submitted field CPU state commits exactly once, independently of TAA and host feedback");
			participant.Settle({ RenderFrameOutcomeKind::Failed, 2, fence });
			context.Check(!state->IsInvalid(), "Duplicate settlement cannot roll back an accepted field update");
			participant.BeginFrame(3);
			context.Check(participant.Track(state, 3), "The next full update uses a new frame serial");
			participant.Settle({ RenderFrameOutcomeKind::Submitted, 3, fence });
			context.Check(!state->Commit(3), "An unrecorded field has no update to publish");
			for (const auto kind : { RenderFrameOutcomeKind::Failed, RenderFrameOutcomeKind::CompletionUnknown })
			{
				auto failed = std::make_shared<GIFieldUpdateState>();
				participant.BeginFrame(4);
				context.Check(participant.Track(failed, 4), "Failure test tracks its independent field generation");
				failed->MarkRecorded(4);
				participant.Settle({ kind, 4, kind == RenderFrameOutcomeKind::Failed ? fence : RHIFencePoint{} });
				context.Check(failed->IsInvalid() && !failed->Commit(4) && !failed->Begin(5),
					"Failed or unknown in-place writes invalidate the field and prevent further updates");
			}
			auto old = std::make_shared<GIFieldUpdateState>();
			auto next = std::make_shared<GIFieldUpdateState>();
			participant.BeginFrame(5);
			context.Check(participant.Track(old, 5), "The old field owns its recorded frame");
			old->MarkRecorded(5);
			participant.Settle({ RenderFrameOutcomeKind::Submitted, 5, fence });
			context.Check(old->Commit(5) && !next->Commit(5), "A retained old-generation commit never advances its replacement");
			std::weak_ptr<GIFieldUpdateState> weak = old;
			old.reset();
			context.Check(weak.expired(), "Settled participant releases its CPU lease without retaining the session");
		}

		void RunProbeUpdateProfileTests(SelfTestContext& context) noexcept
		{
			GIProbeUpdateProfile profile;
			context.Check(ValidateGIProbeUpdateProfile(profile), "Coastal probe workload accepts its declared full-update capacity");
			profile.m_UpdateDivisor = 4;
			std::vector<uint32_t> visits(12870, 0);
			for (uint32_t update = 0; update < 4; ++update)
			{
				const auto range = GetGIProbeUpdateRange(profile, update);
				context.Check(range.m_Count == 3217 || range.m_Count == 3218,
					"Non-divisible Coastal quarter updates retain their exact work rather than dropping the remainder");
				for (uint32_t probe = range.m_First; probe < range.m_First + range.m_Count; ++probe)
				{
					++visits[probe];
				}
			}
			context.Check(std::ranges::all_of(visits, [](uint32_t count) { return count == 1; }),
				"Four partial updates visit every probe exactly once without overlap");
			profile.m_RefreshPeriod = 5;
			context.Check(GetGIProbeUpdateRange(profile, 0).m_Count == 12870 &&
				GetGIProbeUpdateRange(profile, 5).m_Count == 12870 &&
				GetGIProbeUpdateRange(profile, 6).m_Count < 12870,
				"Declared refreshes use the same capacity while the steady schedule remains partial");
			for (const uint32_t rays : { 64u, 128u, 192u, 256u })
			{
				auto valid = profile;
				valid.m_RaysPerProbe = rays;
				context.Check(ValidateGIProbeUpdateProfile(valid),
					"All four qualified ray counts survive shared workload-limit admission");
			}
			for (const uint32_t rays : { 0u, 63u, 65u, 127u, 193u, 257u, UINT32_MAX })
			{
				auto invalid = profile;
				invalid.m_RaysPerProbe = rays;
				context.Check(!ValidateGIProbeUpdateProfile(invalid) && GetGIProbeUpdateRange(invalid, 0).m_Count == 0,
					"Unsupported ray profiles reject instead of silently truncating work");
			}
			profile.m_Counts = { 128, 32, 128 };
			context.Check(!ValidateGIProbeUpdateProfile(profile), "Oversized workloads reject before GPU allocation or dispatch");
			profile = {};
			profile.m_Spacing = std::numeric_limits<float>::quiet_NaN();
			context.Check(!ValidateGIProbeUpdateProfile(profile), "Non-finite probe geometry cannot reach GPU execution");
		}

		void RunGIReferenceInputTests(SelfTestContext& context) noexcept
		{
			GIReferenceCreateInfo info;
			RaySceneDiagnostics scene{ .m_Status = RaySceneStatus::Prepared,
				.m_BoundsDiagonal = 20.0f, .m_HasCompleteBounds = true };
			context.Check(ValidateGIReferenceInputs(info, scene).IsReady(),
				"Complete all-World bounds admit the reference environment estimator");
			info.m_MaximumDistance = 19.0f;
			context.Check(ValidateGIReferenceInputs(info, scene).m_Reason == "IncompleteScene",
				"A short ray cannot turn truncated geometry into an environment miss");
			info.m_MaximumDistance = 20.0f;
			context.Check(ValidateGIReferenceInputs(info, scene).IsReady(),
				"The complete bounds diagonal is an inclusive ray-distance minimum");
			scene.m_HasCompleteBounds = false;
			context.Check(!ValidateGIReferenceInputs(info, scene).IsReady(),
				"Missing eligible-instance bounds reject sky semantics");
			scene.m_HasCompleteBounds = true;
			scene.m_Status = RaySceneStatus::NotReady;
			context.Check(!ValidateGIReferenceInputs(info, scene).IsReady(),
				"A stale snapshot cannot qualify a partially ready World");
			scene.m_Status = RaySceneStatus::Prepared;
			for (const uint32_t samples : { 0u, 1048577u })
			{
				info.m_SampleCount = samples;
				context.Check(!ValidateGIReferenceInputs(info, scene).IsReady(),
					"Empty and oversized reference observations reject before GPU access");
			}
			info.m_SampleCount = 64;
			info.m_EnvironmentFilter = static_cast<GIReferenceEnvironmentFilter>(99);
			context.Check(!ValidateGIReferenceInputs(info, scene).IsReady(),
				"Unknown environment filters cannot silently change the reference signal");
			info.m_EnvironmentFilter = GIReferenceEnvironmentFilter::MipZero;
			context.Check(ValidateGIReferenceInputs(info, scene).IsReady(),
				"Mip-zero environment comparison is a supported explicit signal");
			info.m_ConstantRadiance[0] = std::numeric_limits<float>::quiet_NaN();
			context.Check(!ValidateGIReferenceInputs(info, scene).IsReady(),
				"Non-finite reference radiance cannot enter the estimator");
			info.m_ConstantRadiance[0] = 1.0f;
			for (const uint32_t depth : { 0u, 257u })
			{
				info.m_MaximumDepth = depth;
				context.Check(!ValidateGIReferenceInputs(info, scene).IsReady(),
					"Invalid fixed depths reject instead of silently clamping transport");
			}
			for (const uint32_t depth : { 1u, 2u, 3u, 4u, 256u })
			{
				info.m_MaximumDepth = depth;
				context.Check(ValidateGIReferenceInputs(info, scene).IsReady(),
					"Fixed depths are valid independently of per-extent work admission");
			}
			info.m_QueryWorkPerSubmission = 0;
			context.Check(!ValidateGIReferenceInputs(info, scene).IsReady(),
				"An empty query-work budget rejects before submission");
		}

		void RunGIReferenceObservationTests(SelfTestContext& context) noexcept
		{
			asset_test::TestRHIDevice device;
			device.SetFencesComplete(false);
			PersistentTexturePool textures(&device);
			TransientResourcePool transients(&device);
			GIReferenceObservation observation(device, textures);
			GIReferenceObservationKey key{ .m_Width = 8, .m_Height = 8, .m_Samples = 9 };
			auto sceneOwner = std::make_shared<RayScene>();
			const std::weak_ptr<RayScene> sceneObserver = sceneOwner;
			key.m_Scene = sceneOwner;
			auto sharedScenes = std::make_shared<std::array<RayScene, 2>>();
			const GIReferenceSceneIdentity firstAlias(std::shared_ptr<RayScene>(sharedScenes, &(*sharedScenes)[0]));
			const GIReferenceSceneIdentity secondAlias(std::shared_ptr<RayScene>(sharedScenes, &(*sharedScenes)[1]));
			context.Check(!(firstAlias == secondAlias),
				"Aliased scene objects sharing an owner still have distinct observation identities");
			auto identicalKey = key;
			context.Check(key == identicalKey, "Reference identity preserves the scene control block");
			sceneOwner.reset();
			context.Check(sceneObserver.expired() && key == identicalKey,
				"Reference keys cannot retain scene texture interests past content retirement");
			key.m_Scene = std::make_shared<RayScene>();
			context.Check(!(key == identicalKey),
				"Expired scene identities cannot alias a new owner at a recycled address");
			const RHIFencePoint fence{ RHIFenceHandle(0, 1), 1 };
			const auto record = [&]() noexcept
				{
					RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &transients });
					struct Data {};
					graph.AddPass<Data>("ObservationBatch", RGPassEncoderType::Compute,
						[&](RenderGraph::RGBuilder& builder, Data&)
						{
							auto surfaces = observation.Import(builder);
							for (auto& surface : surfaces)
							{
								if (observation.GetDiagnostics().m_SubmittedSamples != 0)
								{
									GGLAB_UNUSED(builder.Read(surface.m_Previous,
										RGTextureAccess::Sample, RHIStage::ComputeShader));
								}
								if (observation.GetDiagnostics().m_BatchSamples != 0)
								{
									builder.WriteInPlace(surface.m_Next, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
								}
							}
							observation.Export(builder, surfaces);
							builder.SideEffect();
						});
					context.Check(graph.Compile(), "GI accumulation imports and final states compile");
					observation.MarkRecorded();
				};
			observation.BeginFrame(1);
			context.Check(!observation.Prepare(key, 4, 0), "An empty sample-work budget rejects before allocation");
			context.Check(observation.Prepare(key, 8, 256) && observation.GetDiagnostics().m_BatchSamples == 4,
				"The whole submission is bounded by pixels times samples");
			record();
			observation.Settle({ RenderFrameOutcomeKind::Discarded, 1, {} });
			context.Check(observation.GetDiagnostics().m_SubmittedSamples == 0, "Discarded batches do not advance");
			observation.BeginFrame(2);
			GGLAB_UNUSED(observation.Prepare(key, 4, 256));
			record();
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 2, fence });
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 2, fence });
			context.Check(observation.GetDiagnostics().m_SubmittedSamples == 4 &&
				observation.GetDiagnostics().m_CompletedSamples == 0 && observation.GetDiagnostics().m_CommittedVersion == 1,
				"Submitted advances once and does not claim GPU completion");
			observation.BeginFrame(3);
			GGLAB_UNUSED(observation.Prepare(key, 4, 256));
			context.Check(observation.GetDiagnostics().m_BatchSamples == 0, "Only one unfinished sample batch is allowed");
			observation.Settle({ RenderFrameOutcomeKind::Discarded, 3, {} });
			device.SetFencesComplete(true);
			observation.BeginFrame(4);
			GGLAB_UNUSED(observation.Prepare(key, 8, 512));
			context.Check(observation.GetDiagnostics().m_CompletedSamples == 4 &&
				observation.GetDiagnostics().m_BatchSamples == 5, "Completion permits the exact remaining range with a new partition");
			record();
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 4, fence });
			observation.BeginFrame(5);
			GGLAB_UNUSED(observation.Prepare(key, 4, 256));
			context.Check(observation.GetDiagnostics().m_Status == GIReferenceObservationStatus::Completed &&
				observation.GetDiagnostics().m_CompletedSamples == 9 && observation.GetDiagnostics().m_BatchSamples == 0,
				"A completed observation resolves without counting further samples");
			const auto generation = observation.GetDiagnostics().m_Generation;
			key.m_MaximumDepth = 3;
			context.Check(!observation.Prepare(key, 8, 512, 5),
				"At least one pixel sample must fit diffuse and shadow query work");
			context.Check(observation.Prepare(key, 8, 512, 768) &&
				observation.GetDiagnostics().m_BatchSamples == 2 &&
				observation.GetDiagnostics().m_MaximumDepth == 3 &&
				observation.GetDiagnostics().m_SubmittedSamples == 0 &&
				observation.GetDiagnostics().m_Generation == generation + 1,
				"Depth changes reset sums and query cost bounds the new batch");
			key.m_MaximumDepth = 1;
			++key.m_SceneRevision;
			GGLAB_UNUSED(observation.Prepare(key, 4, 256));
			context.Check(observation.GetDiagnostics().m_Generation == generation + 2 &&
				observation.GetDiagnostics().m_SubmittedSamples == 0, "Snapshot changes cancel and restart the observation");
			++key.m_PositionShaderGeneration;
			GGLAB_UNUSED(observation.Prepare(key, 4, 256));
			context.Check(observation.GetDiagnostics().m_Generation == generation + 3 &&
				observation.GetDiagnostics().m_SubmittedSamples == 0 &&
				observation.GetDiagnostics().m_CommittedVersion == 0,
				"Raster-position shader replacement invalidates the reference and resets every published sample");
			record();
			observation.Settle({ RenderFrameOutcomeKind::Failed, 5, fence });
			context.Check(observation.GetDiagnostics().m_Status == GIReferenceObservationStatus::Failed &&
				observation.GetDiagnostics().m_SubmittedSamples == 0, "Failed execution never contributes, even with a fence");
			observation.Cancel();
			device.SetFencesComplete(false);
			observation.BeginFrame(6);
			GGLAB_UNUSED(observation.Prepare(key, 4, 256));
			record();
			observation.Settle({ RenderFrameOutcomeKind::CompletionUnknown, 6, {} });
			observation.Cancel();
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 12 &&
				!observation.Prepare(key, 4, 256), "Unknown completion retains GPU owners and prevents restart");
			device.SetFencesComplete(true);
			observation.Shutdown();
			textures.Tick();
			observation.BeginFrame(7);
			GGLAB_UNUSED(observation.Prepare(key, 4, 256));
			record();
			device.SetFencesComplete(false);
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 7, fence });
			observation.Cancel();
			context.Check(textures.GetDiagnostics().m_PendingRetirementTextureCount == 12,
				"Cancellation retains the entire sum/RNG pair through its last-use fence");
			textures.Tick();
			context.Check(textures.GetDiagnostics().m_PendingRetirementTextureCount == 12,
				"Pending cancelled resources cannot retire early");
			observation.BeginFrame(8);
			GGLAB_UNUSED(observation.Prepare(key, 4, 256));
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 8, fence });
			context.Check(observation.GetDiagnostics().m_SubmittedSamples == 0,
				"Submission without a recorded/exported batch cannot advance samples");
			device.SetFencesComplete(true);
			observation.Shutdown();
			textures.Tick();
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 0 &&
				textures.GetDiagnostics().m_PendingRetirementTextureCount == 0, "Verified teardown releases every observation surface");
		}

		void RunSpecularVisibilityTests(SelfTestContext& context) noexcept
		{
			GISpecularVisibilityCreateInfo info;
			RaySceneDiagnostics scene{ .m_Status = RaySceneStatus::Prepared,
				.m_BoundsDiagonal = 20, .m_HasCompleteBounds = true };
			context.Check(!ValidateGISpecularVisibilityInputs(info, scene).IsReady(),
				"Reduced hit identity cannot qualify primary specular inputs");
			scene.m_TracksPrimaryMaterials = true;
			context.Check(ValidateGISpecularVisibilityInputs(info, scene).IsReady(),
				"Primary materials and complete scene bounds admit geometric visibility");
			info.m_MaximumDistance = 19;
			context.Check(!ValidateGISpecularVisibilityInputs(info, scene).IsReady(),
				"Visibility cannot truncate off-camera blockers into environment misses");
			info.m_MaximumDistance = 20;
			info.m_QueryWorkPerSubmission = 1;
			context.Check(!ValidateGISpecularVisibilityInputs(info, scene).IsReady(),
				"The work budget accounts for both lobes even when a material disables one");

			asset_test::TestRHIDevice device;
			PersistentTexturePool textures(&device);
			TransientResourcePool transients(&device);
			GISpecularVisibilityObservation observation(device, textures);
			GISpecularVisibilityKey key{ .m_Width = 16, .m_Height = 8, .m_Samples = 2 };
			const RHIFencePoint fence{ RHIFenceHandle(0, 1), 1 };
			const auto record = [&]() noexcept
				{
					RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &transients });
					struct Data {};
					graph.AddPass<Data>("VisibilityTile", RGPassEncoderType::Compute,
						[&](RenderGraph::RGBuilder& builder, Data&)
						{
							auto surfaces = observation.Import(builder);
							for (auto& surface : surfaces)
							{
								if (observation.GetDiagnostics().m_SubmittedSamples != 0)
								{
									GGLAB_UNUSED(builder.Read(surface.m_Previous, RGTextureAccess::Sample, RHIStage::ComputeShader));
								}
								if (observation.GetDiagnostics().m_BatchSamples != 0)
								{
									if (observation.GetTile().m_BeginSweep)
									{
										builder.WriteInPlace(surface.m_Next, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
									}
									else
									{
										builder.ReadWriteInPlace(surface.m_Next, RGTextureAccess::StorageReadWrite, RHIStage::ComputeShader);
									}
								}
							}
							observation.Export(builder, surfaces);
							builder.SideEffect();
						});
					context.Check(graph.Compile(), "Visibility sums have defined graph-owned imports and final states");
					observation.MarkRecorded();
				};
			observation.BeginFrame(1);
			context.Check(observation.Prepare(key, 64, 128) &&
				observation.GetDiagnostics().m_TileCount == 2 && observation.GetDiagnostics().m_BatchSamples == 1,
				"Two-lobe query work tiles without reducing the measurement extent");
			record();
			observation.Settle({ RenderFrameOutcomeKind::Discarded, 1, {} });
			context.Check(observation.GetDiagnostics().m_SubmittedTiles == 0,
				"Discarded visibility work does not consume sample indices or tiles");
			observation.BeginFrame(2);
			GGLAB_UNUSED(observation.Prepare(key, 64, 128));
			record();
			device.SetFencesComplete(false);
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 2, fence });
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 2, fence });
			context.Check(observation.GetDiagnostics().m_SubmittedTiles == 1 &&
				observation.GetDiagnostics().m_CommittedVersion == 0 && observation.GetDiagnostics().m_SubmittedSamples == 0,
				"Partial visibility sweeps settle exactly once without publishing a complete estimate");
			observation.BeginFrame(3);
			GGLAB_UNUSED(observation.Prepare(key, 64, 128));
			context.Check(observation.GetDiagnostics().m_BatchSamples == 0,
				"Pending visibility fences prevent overlapping tile writes");
			observation.Settle({ RenderFrameOutcomeKind::Discarded, 3, {} });
			device.SetFencesComplete(true);
			for (uint64_t serial = 4; serial <= 6; ++serial)
			{
				observation.BeginFrame(serial);
				GGLAB_UNUSED(observation.Prepare(key, 64, 128));
				record();
				observation.Settle({ RenderFrameOutcomeKind::Submitted, serial, fence });
			}
			observation.BeginFrame(7);
			GGLAB_UNUSED(observation.Prepare(key, 64, 128));
			context.Check(observation.GetDiagnostics().m_Status == GISpecularVisibilityStatus::Completed &&
				observation.GetDiagnostics().m_CompletedSamples == 2 && observation.GetDiagnostics().m_CommittedVersion == 2,
				"GPU completion requires every tile and is distinct from external sampling qualification");
			const auto generation = observation.GetDiagnostics().m_Generation;
			++key.m_ReceiverShaderGeneration;
			GGLAB_UNUSED(observation.Prepare(key, 64, 128));
			context.Check(observation.GetDiagnostics().m_Generation == generation + 1 &&
				observation.GetDiagnostics().m_SubmittedSamples == 0,
				"Receiver shader changes invalidate material reconstruction and sums");
			++key.m_PositionShaderGeneration;
			GGLAB_UNUSED(observation.Prepare(key, 64, 128));
			context.Check(observation.GetDiagnostics().m_Generation == generation + 2 &&
				observation.GetDiagnostics().m_SubmittedSamples == 0 &&
				observation.GetDiagnostics().m_CommittedVersion == 0,
				"Raster-position shader replacement invalidates visibility without retaining old samples");
			record();
			observation.Settle({ RenderFrameOutcomeKind::Failed, 7, fence });
			context.Check(observation.GetDiagnostics().m_Status == GISpecularVisibilityStatus::Failed &&
				observation.GetDiagnostics().m_SubmittedTiles == 0, "Failed visibility writes never publish progress");
			observation.Cancel();
			observation.BeginFrame(8);
			GGLAB_UNUSED(observation.Prepare(key, 64, 128));
			record();
			observation.Settle({ RenderFrameOutcomeKind::CompletionUnknown, 8, {} });
			observation.Cancel();
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 8 && !observation.Prepare(key, 64, 128),
				"Unknown completion retains all four visibility surface pairs until checked teardown");
			observation.Shutdown();
			textures.Tick();
			observation.BeginFrame(9);
			GGLAB_UNUSED(observation.Prepare(key, 64, 128));
			record();
			device.SetFencesComplete(false);
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 9, fence });
			observation.BeginFrame(10);
			++key.m_BrdfLutRevision;
			GGLAB_UNUSED(observation.Prepare(key, 64, 128));
			context.Check(observation.GetDiagnostics().m_SubmittedTiles == 0 &&
				textures.GetDiagnostics().m_PendingRetirementTextureCount == 8,
				"LUT replacement resets pending sweeps while retaining old allocations through the fence");
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 10, fence });
			context.Check(observation.GetDiagnostics().m_SubmittedTiles == 0,
				"Unrecorded visibility work cannot advance even on submitted frames");
			observation.BeginFrame(11);
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 11, fence });
			context.Check(observation.GetDiagnostics().m_Status == GISpecularVisibilityStatus::Cancelled,
				"Lab deactivation cancels visibility independently of diffuse reference");
			device.SetFencesComplete(true);
			observation.Shutdown();
			textures.Tick();
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 0 &&
				textures.GetDiagnostics().m_PendingRetirementTextureCount == 0,
				"Visibility teardown releases every allocation after checked completion");
		}

		void RunGIReferenceTilingTests(SelfTestContext& context) noexcept
		{
			asset_test::TestRHIDevice device;
			PersistentTexturePool textures(&device);
			TransientResourcePool transients(&device);
			GIReferenceObservation observation(device, textures);
			GIReferenceObservationKey key{ .m_Width = 16, .m_Height = 8,
				.m_Samples = 2, .m_MaximumDepth = 64 };
			const RHIFencePoint fence{ RHIFenceHandle(0, 1), 1 };
			const auto record = [&]() noexcept
				{
					RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &transients });
					struct Data {};
					graph.AddPass<Data>("ObservationTile", RGPassEncoderType::Compute,
						[&](RenderGraph::RGBuilder& builder, Data&)
						{
							auto surfaces = observation.Import(builder);
							for (auto& surface : surfaces)
							{
								if (observation.GetDiagnostics().m_SubmittedSamples != 0)
								{
									GGLAB_UNUSED(builder.Read(surface.m_Previous, RGTextureAccess::Sample, RHIStage::ComputeShader));
								}
								if (observation.GetDiagnostics().m_BatchSamples != 0)
								{
									if (observation.GetTile().m_BeginSweep)
									{
										builder.WriteInPlace(surface.m_Next,
											RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
									}
									else
									{
										builder.ReadWriteInPlace(surface.m_Next,
											RGTextureAccess::StorageReadWrite, RHIStage::ComputeShader);
									}
								}
							}
							observation.Export(builder, surfaces);
							builder.SideEffect();
						});
					context.Check(graph.Compile(),
						"A pending tile version has defined contents and graph-owned transitions");
					observation.MarkRecorded();
				};
			observation.BeginFrame(1);
			context.Check(observation.Prepare(key, 4, 128, 8192) &&
				observation.GetDiagnostics().m_BatchSamples == 1 &&
				observation.GetDiagnostics().m_TileCount == 2 && observation.GetTile().m_BeginSweep,
				"High depth partitions into bounded tiles without reducing the viewport");
			record();
			observation.Settle({ RenderFrameOutcomeKind::Discarded, 1, {} });
			context.Check(observation.GetDiagnostics().m_SubmittedTiles == 0,
				"Discarded work cannot advance a tile cursor");
			observation.BeginFrame(2);
			GGLAB_UNUSED(observation.Prepare(key, 4, 128, 8192));
			record();
			device.SetFencesComplete(false);
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 2, fence });
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 2, fence });
			context.Check(observation.GetDiagnostics().m_SubmittedTiles == 1 &&
				observation.GetDiagnostics().m_SubmittedSamples == 0 &&
				observation.GetDiagnostics().m_CommittedVersion == 0,
				"A submitted partial sweep advances exactly once without publishing whole-image samples");
			observation.BeginFrame(3);
			GGLAB_UNUSED(observation.Prepare(key, 4, 128, 8192));
			context.Check(observation.GetDiagnostics().m_BatchSamples == 0,
				"The next tile waits for GPU completion");
			record();
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 3, fence });
			device.SetFencesComplete(true);
			observation.BeginFrame(4);
			GGLAB_UNUSED(observation.Prepare(key, 4, 128, 8192));
			context.Check(!observation.GetTile().m_BeginSweep && observation.GetTile().m_EndSweep &&
				observation.GetTile().m_Y == 4 && observation.GetDiagnostics().m_CompletedSamples == 0,
				"The last tile preserves pending data and uses the original sweep sample range");
			record();
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 4, fence });
			context.Check(observation.GetDiagnostics().m_SubmittedSamples == 1 &&
				observation.GetDiagnostics().m_CommittedVersion == 1,
				"Only a complete sweep publishes all six history pairs");
			for (uint64_t serial = 5; serial <= 8; ++serial)
			{
				observation.BeginFrame(serial);
				GGLAB_UNUSED(observation.Prepare(key, 4, 128, 8192, 32));
				context.Check(observation.GetDiagnostics().m_TileCount == 4 &&
					observation.GetDiagnostics().m_BatchSamples == 1,
					"Partition preferences can change at sweep boundaries without changing observation identity");
				record();
				observation.Settle({ RenderFrameOutcomeKind::Submitted, serial, fence });
			}
			observation.BeginFrame(9);
			GGLAB_UNUSED(observation.Prepare(key, 4, 128, 8192));
			context.Check(observation.GetDiagnostics().m_Status == GIReferenceObservationStatus::Completed &&
				observation.GetDiagnostics().m_CompletedSamples == 2 &&
				observation.GetDiagnostics().m_CommittedVersion == 2,
				"Completed samples and readiness count full sweeps rather than tile submissions");
			++key.m_Seed;
			GGLAB_UNUSED(observation.Prepare(key, 4, 128, 8192));
			record();
			observation.Settle({ RenderFrameOutcomeKind::Failed, 9, fence });
			context.Check(observation.GetDiagnostics().m_SubmittedSamples == 0 &&
				observation.GetDiagnostics().m_SubmittedTiles == 0,
				"Failed tile writes never publish pending progress");
			observation.Cancel();
			observation.BeginFrame(10);
			GGLAB_UNUSED(observation.Prepare(key, 4, 128, 8192));
			record();
			observation.Settle({ RenderFrameOutcomeKind::CompletionUnknown, 10, {} });
			observation.Cancel();
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 12 &&
				!observation.Prepare(key, 4, 128, 8192), "Unknown partial sweeps retain every owner through checked teardown");
			observation.Shutdown();
			textures.Tick();
			observation.BeginFrame(11);
			GGLAB_UNUSED(observation.Prepare(key, 4, 128, 8192));
			record();
			device.SetFencesComplete(false);
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 11, fence });
			observation.BeginFrame(12);
			// An inactive Lab never visits the observation during this frame.
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 12, fence });
			context.Check(observation.GetDiagnostics().m_Status == GIReferenceObservationStatus::Cancelled &&
				textures.GetDiagnostics().m_PendingRetirementTextureCount == 12,
				"Lab deactivation retains every partial-sweep owner through its last-use fence");
			device.SetFencesComplete(true);
			observation.Shutdown();
			textures.Tick();
			observation.BeginFrame(13);
			GGLAB_UNUSED(observation.Prepare(key, 4, 128, 8192));
			record();
			device.SetFencesComplete(false);
			observation.Settle({ RenderFrameOutcomeKind::Submitted, 13, fence });
			const auto oldGeneration = observation.GetDiagnostics().m_Generation;
			observation.BeginFrame(14);
			++key.m_SceneRevision;
			GGLAB_UNUSED(observation.Prepare(key, 4, 128, 8192));
			context.Check(observation.GetDiagnostics().m_Generation == oldGeneration + 1 &&
				observation.GetDiagnostics().m_SubmittedSamples == 0 &&
				observation.GetDiagnostics().m_SubmittedTiles == 0 &&
				observation.GetDiagnostics().m_CommittedVersion == 0 && observation.GetTile().m_BeginSweep &&
				textures.GetDiagnostics().m_PendingRetirementTextureCount == 12,
				"A material snapshot revision restarts every partial-sweep cursor and retains old storage through its fence");
			record();
			observation.Settle({ RenderFrameOutcomeKind::Discarded, 14, {} });
			device.SetFencesComplete(true);
			observation.Shutdown();
			textures.Tick();
		}

		void RunGIReceiverPreparationTests(SelfTestContext& context) noexcept
		{
			RenderScene scene;
			std::array<RenderView, 1> views{};
			views[0].m_Width = 8;
			views[0].m_Height = 8;
			views[0].m_DisplayWidth = 16;
			views[0].m_DisplayHeight = 16;
			RenderFrameContext frame{ .m_RenderViews = views, .m_RenderScene = scene };
			RenderPassGIReceiver receivers;
			context.Check(!receivers.Prepare(frame, {}, {}).IsReady(),
				"Upscaled receiver domains fail before pipeline/service access");
			views[0].m_DisplayWidth = 8;
			views[0].m_DisplayHeight = 8;
			frame.m_TemporalFramePlan.m_Consumers[static_cast<size_t>(TemporalConsumer::TemporalAA)].m_Status = TemporalConsumerStatus::Active;
			context.Check(!receivers.Prepare(frame, {}, {}).IsReady(),
				"An active temporal resolve cannot publish GI reference receivers");
			frame.m_TemporalFramePlan = {};
			frame.m_TemporalFramePlan.m_Services = TemporalService::ProjectionJitter;
			context.Check(!receivers.Prepare(frame, {}, {}).IsReady(),
				"Reference-owned projection jitter also rejects GI receiver preparation");
			frame.m_TemporalFramePlan = {};
			views[0].m_JitterPixels.m_X = 0.25f;
			context.Check(!receivers.Prepare(frame, {}, {}).IsReady(),
				"A stale jittered raster view cannot masquerade as unjittered receiver data");
		}

		void RunRayScenePlanTests(SelfTestContext& context) noexcept
		{
			const RHIASBuildLimits limits{ 1024, 1024, 256 };
			RaySceneSource opaque{
				.m_EntityIdentity = 4, .m_Model = ModelID(7), .m_ModelGeneration = 1,
				.m_Geometry = { MeshID(9), 1 },
				.m_Vertices = { RHIBufferHandle{ 0, 1 }, 0, 56, 168 },
				.m_Indices = { RHIBufferHandle{ 1, 1 }, 0, 12, RHIFormat::R32Uint },
				.m_VertexBufferDesc = { .m_SizeInBytes = 168, .m_Usage = RHIBufferUsage::CopySource },
				.m_IndexBufferDesc = { .m_SizeInBytes = 12, .m_Usage = RHIBufferUsage::CopySource },
				.m_VertexCount = 3, .m_IndexCount = 3,
				.m_MaterialKey = RenderMaterialKey::FromAsset(MaterialID(2)),
				.m_Transform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 } };
			auto second = opaque;
			second.m_EntityIdentity = 8;
			second.m_Transform[3] = 200;
			auto blend = opaque;
			blend.m_EntityIdentity = 1;
			blend.m_AlphaMode = AlphaMode::Blend;
			RayScenePlan baseline;
			std::string reason;
			context.Check(PlanRayScene({ second, opaque, blend }, {}, limits, baseline, reason) == RaySceneStatus::Prepared &&
				baseline.m_Geometries.size() == 1 && baseline.m_Instances.size() == 3 &&
				baseline.m_OpaqueCount == 2 && baseline.m_BlendCount == 1 &&
				baseline.m_VertexBytes == 168 && baseline.m_IndexBytes == 12,
				"Ray snapshot deduplicates shared geometry and retains distant instances independently of camera visibility");
			context.Check(baseline.m_Sources[0].m_EntityIdentity == 1 &&
				baseline.m_Instances[0].m_Flags == 0 && baseline.m_Instances[0].m_GeometryIndex == UINT32_MAX &&
				baseline.m_Instances[1].m_Flags == 1,
				"BLEND has a stable excluded table entry and no fake or null BLAS in the TLAS");
			RayScenePlan other;
			context.Check(PlanRayScene({ blend, opaque, second }, {}, limits, other, reason) == RaySceneStatus::Prepared &&
				HasSameRayInstances(baseline, other), "Source enumeration does not change the snapshot mapping");
			second.m_Transform[7] = 3;
			GGLAB_UNUSED(PlanRayScene({ second, opaque, blend }, {}, limits, other, reason));
			context.Check(HasSameRayGeometry(baseline, other) && !HasSameRayTransforms(baseline, other),
				"Rigid transforms dirty only the TLAS while reusing immutable geometry and BLAS");
			second.m_Transform[7] = 0;
			second.m_EntityIdentity += 1u << 20;
			GGLAB_UNUSED(PlanRayScene({ second, opaque, blend }, {}, limits, other, reason));
			context.Check(HasSameRayGeometry(baseline, other) && !HasSameRayInstances(baseline, other),
				"Recycled entity indices retain their generation in the instance identity");
			auto material = opaque;
			material.m_Material.BaseColorFactor.m_R = 0.4f;
			RayScenePlan originalSingle;
			GGLAB_UNUSED(PlanRayScene({ opaque }, {}, limits, originalSingle, reason));
			GGLAB_UNUSED(PlanRayScene({ material }, {}, limits, other, reason));
			context.Check(HasSameRayTransforms(originalSingle, other) && !HasSameRayInstances(originalSingle, other),
				"Material content changes replace snapshot tables without rebuilding the TLAS");
			auto textured = opaque;
			textured.m_TextureContents[0] = { TextureID(14), 1 };
			textured.m_TextureResources[0] = { 8, 1 };
			textured.m_Material.BaseColorBinding.TextureSamplerBinding.TextureIndex = 20;
			RayScenePlan textureBaseline;
			GGLAB_UNUSED(PlanRayScene({ textured }, {}, limits, textureBaseline, reason));
			auto replaced = textured;
			replaced.m_TextureContents[0].m_Generation = 2;
			GGLAB_UNUSED(PlanRayScene({ replaced }, {}, limits, other, reason));
			context.Check(HasSameRayTransforms(textureBaseline, other) &&
				!HasSameRayInstances(textureBaseline, other),
				"Texture content replacement invalidates a snapshot even when GPU material bytes and descriptor index match");
			replaced = textured;
			replaced.m_TextureResources[0] = { 8, 2 };
			GGLAB_UNUSED(PlanRayScene({ replaced }, {}, limits, other, reason));
			context.Check(HasSameRayTransforms(textureBaseline, other) &&
				!HasSameRayInstances(textureBaseline, other),
				"Texture residency slot reuse invalidates tables without rebuilding unchanged BLAS or TLAS");
			replaced = textured;
			replaced.m_TextureContents[1] = { TextureID(15), 4 };
			GGLAB_UNUSED(PlanRayScene({ replaced }, {}, limits, other, reason));
			context.Check(!HasSameRayInstances(textureBaseline, other),
				"Metallic texture content is part of the reduced material identity");
			for (size_t slot = 2; slot < textured.m_TextureContents.size(); ++slot)
			{
				replaced = textured;
				replaced.m_TextureContents[slot] = { TextureID(16), 2 };
				GGLAB_UNUSED(PlanRayScene({ replaced }, {}, limits, other, reason));
				context.Check(HasSameRayTransforms(textureBaseline, other) &&
					!HasSameRayInstances(textureBaseline, other),
					"Every primary normal/coat/anisotropy texture content participates in observation identity");
				replaced = textured;
				replaced.m_TextureResources[slot] = { 8, 2 };
				GGLAB_UNUSED(PlanRayScene({ replaced }, {}, limits, other, reason));
				context.Check(HasSameRayTransforms(textureBaseline, other) &&
					!HasSameRayInstances(textureBaseline, other),
					"Primary texture resource generation changes invalidate identity without rebuilding geometry");
			}
			replaced = textured;
			replaced.m_EntityIdentity += 1;
			replaced.m_TextureContents[0].m_Generation += 1;
			context.Check(PlanRayScene({ textured, replaced }, {}, limits, other, reason) ==
				RaySceneStatus::InvalidSource && other.m_Instances.empty(),
				"Shared material keys cannot alias different texture generations with identical descriptor bytes");
			replaced = textured;
			replaced.m_ModelGeneration += 1;
			GGLAB_UNUSED(PlanRayScene({ replaced }, {}, limits, other, reason));
			context.Check(HasSameRayGeometry(textureBaseline, other) &&
				!HasSameRayTransforms(textureBaseline, other),
				"Reused model slots preserve generation identity independently of mesh cache reuse");
			auto generation = opaque;
			generation.m_Geometry.m_ContentGeneration = 2;
			GGLAB_UNUSED(PlanRayScene({ generation }, {}, limits, other, reason));
			context.Check(!HasSameRayGeometry(originalSingle, other),
				"Mesh content generation invalidates the BLAS key even when its slot and layout match");
			auto mask = opaque;
			mask.m_AlphaMode = AlphaMode::Mask;
			context.Check(PlanRayScene({ mask }, {}, limits, other, reason) == RaySceneStatus::UnsupportedGeometry &&
				other.m_Instances.empty(), "MASK rejects preparation without publishing a partial opaque snapshot");
			auto capacity = RaySceneCapacity{};
			capacity.m_MaxInstances = 1;
			context.Check(PlanRayScene({ opaque, blend }, capacity, limits, other, reason) == RaySceneStatus::CapacityExceeded,
				"Excluded entries still count against the fixed instance table capacity");
			capacity = {};
			capacity.m_MaxVertexBytes = 167;
			context.Check(PlanRayScene({ opaque }, capacity, limits, other, reason) == RaySceneStatus::CapacityExceeded,
				"Ray pool overflow is reported instead of truncating geometry");
			auto invalid = opaque;
			invalid.m_Indices.m_Format = RHIFormat::Unknown;
			context.Check(PlanRayScene({ invalid }, {}, limits, other, reason) == RaySceneStatus::InvalidSource,
				"The ray snapshot rejects a non-R32 index layout");
			invalid = opaque;
			invalid.m_Transform[0] = 0;
			context.Check(PlanRayScene({ invalid }, {}, limits, other, reason) == RaySceneStatus::InvalidSource,
				"Singular instance transforms fail before GPU allocation");
			context.Check(PlanRayScene({ opaque, opaque }, {}, limits, other, reason) == RaySceneStatus::InvalidSource,
				"Duplicate durable instance identities fail preparation");
			auto subrange = opaque;
			subrange.m_Vertices.m_Offset = 1024;
			subrange.m_Indices.m_Offset = 256;
			subrange.m_VertexBufferDesc.m_SizeInBytes = 2048;
			subrange.m_IndexBufferDesc.m_SizeInBytes = 512;
			context.Check(PlanRayScene({ subrange }, {}, limits, other, reason) == RaySceneStatus::Prepared &&
				other.m_Geometries[0].m_SourceVertices.m_Offset == 1024 &&
				other.m_Geometries[0].m_SourceIndices.m_Offset == 256 &&
				other.m_Geometries[0].m_SourceVertexDesc.m_SizeInBytes == 2048,
				"Nonzero raster subranges retain physical offsets and the actual allocation extent");
			auto tooShort = subrange;
			tooShort.m_Vertices.m_SizeInBytes = 124;
			context.Check(PlanRayScene({ tooShort }, {}, limits, other, reason) == RaySceneStatus::InvalidSource,
				"A ray pool copy cannot read trailing Vertex attributes outside its logical binding");
			tooShort = subrange;
			tooShort.m_Indices.m_SizeInBytes = 8;
			context.Check(PlanRayScene({ tooShort }, {}, limits, other, reason) == RaySceneStatus::InvalidSource,
				"Index reads are bounded by the binding as well as the allocation");
			tooShort = subrange;
			tooShort.m_VertexBufferDesc.m_SizeInBytes = 1191;
			context.Check(PlanRayScene({ tooShort }, {}, limits, other, reason) == RaySceneStatus::InvalidSource,
				"A logical binding cannot exceed the real allocation even when its payload fits");
			tooShort = subrange;
			tooShort.m_Indices.m_Offset = std::numeric_limits<uint64_t>::max() - 3;
			tooShort.m_IndexBufferDesc.m_SizeInBytes = std::numeric_limits<uint64_t>::max();
			context.Check(PlanRayScene({ tooShort }, {}, limits, other, reason) == RaySceneStatus::InvalidSource,
				"Physical offset plus logical length cannot wrap around");
			tooShort = subrange;
			tooShort.m_Vertices.m_Offset = 1025;
			context.Check(PlanRayScene({ tooShort }, {}, limits, other, reason) == RaySceneStatus::InvalidSource,
				"Nonzero position offsets still require float alignment");
			auto shared = subrange;
			shared.m_EntityIdentity = 20;
			shared.m_Geometry.m_Mesh = MeshID(10);
			shared.m_Vertices.m_Offset = 1280;
			shared.m_Indices.m_Offset = 300;
			context.Check(PlanRayScene({ subrange, shared }, {}, limits, other, reason) == RaySceneStatus::Prepared &&
				other.m_Geometries.size() == 2, "Distinct meshes may occupy disjoint subranges of one same-role allocation");
			shared.m_Geometry = subrange.m_Geometry;
			context.Check(PlanRayScene({ subrange, shared }, {}, limits, other, reason) == RaySceneStatus::InvalidSource,
				"Equal mesh identities cannot silently merge different physical subranges");
			shared = subrange;
			shared.m_Indices.m_Buffer = shared.m_Vertices.m_Buffer;
			shared.m_IndexBufferDesc = shared.m_VertexBufferDesc;
			context.Check(PlanRayScene({ shared }, {}, limits, other, reason) == RaySceneStatus::UnsupportedGeometry,
				"Mixed vertex/index allocation states remain explicitly unsupported");
		}

		void RunDX12PipelineIdentityTests(SelfTestContext& context) noexcept
		{
			const std::array slots
			{
				RHIBindingSlotDesc{ .m_Type = RHIBindingType::AccelerationStructure,
					.m_Visibility = RHIShaderStage::Compute, .m_Binding = 5 },
			};
			auto renamed = slots;
			renamed[0].m_DebugName = "Another Lab session";
			context.Check(HaveSameRHIBindingSlots(slots, renamed),
				"Repeated logical layouts reuse identity independently of diagnostic names");
			for (uint32_t field = 0; field < 7; ++field)
			{
				auto changed = slots;
				switch (field)
				{
				case 0:
					changed[0].m_Type = RHIBindingType::ReadOnlyStorageBuffer;
					break;
				case 1:
					changed[0].m_Visibility = RHIShaderStage::Pixel;
					break;
				case 2:
					++changed[0].m_Binding;
					break;
				case 3:
					++changed[0].m_Space;
					break;
				case 4:
					++changed[0].m_Count;
					break;
				case 5:
					changed[0].m_SizeInBytes = 16;
					break;
				case 6:
					changed[0].m_BufferBindingMode = RHIBufferBindingMode::Dynamic;
					break;
				}
				context.Check(!HaveSameRHIBindingSlots(slots, changed),
					"Every logical binding field remains significant even when native roots alias");
			}
			context.Check(!HaveSameRHIBindingSlots(slots, {}),
				"A different root-parameter count cannot reuse the logical layout");
			for (const bool compute : { false, true })
			{
				const DX12PipelineIdentity identity{
					.m_BindingLayout = { 0, 1 }, .m_IsCompute = compute };
				auto other = identity;
				other.m_BindingLayout = { 1, 1 };
				context.Check(identity != other,
					"Pipeline identity distinguishes logical layouts even with equal native identities");
				other.m_BindingLayout = { 0, 2 };
				context.Check(identity != other,
					"Pipeline identity retains the binding-layout generation");
				other = identity;
				other.m_IsCompute = !compute;
				context.Check(identity != other,
					"Pipeline identity distinguishes graphics and compute bindings");
			}
			context.Check(Test(RHIStage::All, RHIStage::AccelerationStructureBuild) &&
				ToD3D12BarrierSync(RHIStage::All) == D3D12_BARRIER_SYNC_ALL &&
				ToD3D12BarrierSync(RHIStage::AccelerationStructureBuild) ==
					D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE,
				"All includes AS builds while DX12 retains explicit and universal synchronization scopes");
		}

		constexpr RHIASBuildLimits Limits{
			.m_MaxPrimitiveCount = 1024,
			.m_MaxInstanceCount = 1024,
			.m_ScratchAlignmentInBytes = 256,
		};
		constexpr RHIBufferDesc GeometryBuffer{ .m_SizeInBytes = 4096 };

		[[nodiscard]] RHIASIndexedTriangles Triangle() noexcept
		{
			return {
				.m_Vertices = { RHIBufferHandle{ 0, 1 }, 64, 124 },
				.m_VertexCount = 3,
				.m_VertexStrideInBytes = 56,
				.m_Indices = { RHIBufferHandle{ 1, 1 }, 12, 12 },
				.m_IndexCount = 3,
			};
		}

		void RunCapabilityTests(SelfTestContext& context) noexcept
		{
			context.Check(AlignDX12ASStorageSize(9344) == 9472 && AlignDX12ASStorageSize(9472) == 9472,
				"DX12 rounds native Coastal BLAS prebuild sizes to its storage alignment");
			context.Check(AlignDX12ASStorageSize(0) == 0 &&
				AlignDX12ASStorageSize(std::numeric_limits<uint64_t>::max()) == 0,
				"DX12 storage alignment rejects empty and overflowing sizes");
			const RHIRayQueryCapabilities absent{};
			context.Check(!absent.m_Supported && !absent.m_Enabled &&
				absent.m_UnavailableReason == RHIRayQueryUnavailableReason::BackendUnavailable,
				"An unimplemented device does not claim ray-query support or enablement");
			const auto tier10 = EvaluateDX12RayQuerySupport(
				D3D12_RAYTRACING_TIER_1_0, D3D_SHADER_MODEL_6_6);
			context.Check(!tier10.m_Supported && !tier10.m_Enabled &&
				tier10.m_UnavailableReason == RHIRayQueryUnavailableReason::InlineQueryUnavailable,
				"DXR tier 1.0 is not sufficient for inline ray queries");
			const auto shader64 = EvaluateDX12RayQuerySupport(
				D3D12_RAYTRACING_TIER_1_1, D3D_SHADER_MODEL_6_4);
			context.Check(!shader64.m_Supported &&
				shader64.m_UnavailableReason == RHIRayQueryUnavailableReason::ShaderModelUnavailable,
				"DX12 reports the missing shader-model requirement independently of the RT tier");
			const auto dx12 = EvaluateDX12RayQuerySupport(
				D3D12_RAYTRACING_TIER_1_1, D3D_SHADER_MODEL_6_6);
			context.Check(dx12.m_Supported && !dx12.m_Enabled &&
				dx12.m_UnavailableReason == RHIRayQueryUnavailableReason::None &&
				dx12.m_BuildLimits.m_MaxPrimitiveCount != 0 &&
				dx12.m_BuildLimits.m_MaxInstanceCount != 0 &&
				dx12.m_BuildLimits.m_ScratchAlignmentInBytes == 256,
				"DX12 support exposes build limits without claiming that a device has been created");
#if GGLAB_ENABLE_VULKAN
			const VulkanRayQueryAvailability complete{
				.m_AccelerationStructureExtension = true,
				.m_RayQueryExtension = true,
				.m_DeferredHostOperationsExtension = true,
				.m_AccelerationStructure = true,
				.m_RayQuery = true,
				.m_BufferDeviceAddress = true,
				.m_BuildLimits = Limits,
				.m_MaxGeometryCount = 1,
				.m_MaxDescriptorSetAccelerationStructures = 1,
				.m_MaxPerStageDescriptorAccelerationStructures = 1,
			};
			const auto supported = EvaluateVulkanRayQuerySupport(complete);
			context.Check(supported.m_Supported && !supported.m_Enabled &&
				supported.m_BuildLimits.m_ScratchAlignmentInBytes == Limits.m_ScratchAlignmentInBytes,
				"A complete Vulkan set is supported; only successful device creation may enable it");
			struct MissingFeature
			{
				bool VulkanRayQueryAvailability::* m_Member;
				RHIRayQueryUnavailableReason m_Reason;
			};
			constexpr std::array cases
			{
				MissingFeature{ &VulkanRayQueryAvailability::m_AccelerationStructureExtension, RHIRayQueryUnavailableReason::RequiredExtensionsUnavailable },
				MissingFeature{ &VulkanRayQueryAvailability::m_RayQueryExtension, RHIRayQueryUnavailableReason::RequiredExtensionsUnavailable },
				MissingFeature{ &VulkanRayQueryAvailability::m_DeferredHostOperationsExtension, RHIRayQueryUnavailableReason::RequiredExtensionsUnavailable },
				MissingFeature{ &VulkanRayQueryAvailability::m_AccelerationStructure, RHIRayQueryUnavailableReason::AccelerationStructureUnavailable },
				MissingFeature{ &VulkanRayQueryAvailability::m_RayQuery, RHIRayQueryUnavailableReason::InlineQueryUnavailable },
				MissingFeature{ &VulkanRayQueryAvailability::m_BufferDeviceAddress, RHIRayQueryUnavailableReason::DeviceAddressUnavailable },
			};
			for (const auto& missing : cases)
			{
				auto available = complete;
				available.*missing.m_Member = false;
				const auto result = EvaluateVulkanRayQuerySupport(available);
				context.Check(!result.m_Supported && !result.m_Enabled &&
					result.m_UnavailableReason == missing.m_Reason &&
					result.m_BuildLimits.m_MaxPrimitiveCount == 0,
					"Partial Vulkan feature sets report their missing prerequisite without partial enablement");
			}
			for (uint32_t field = 0; field < 6; ++field)
			{
				auto available = complete;
				switch (field)
				{
				case 0:
					available.m_BuildLimits.m_MaxPrimitiveCount = 0;
					break;
				case 1:
					available.m_BuildLimits.m_MaxInstanceCount = 0;
					break;
				case 2:
					available.m_BuildLimits.m_ScratchAlignmentInBytes = 3;
					break;
				case 3:
					available.m_MaxGeometryCount = 0;
					break;
				case 4:
					available.m_MaxDescriptorSetAccelerationStructures = 0;
					break;
				default:
					available.m_MaxPerStageDescriptorAccelerationStructures = 0;
					break;
				}
				const auto result = EvaluateVulkanRayQuerySupport(available);
				context.Check(!result.m_Supported && !result.m_Enabled &&
					result.m_UnavailableReason == RHIRayQueryUnavailableReason::BuildLimitsUnavailable,
					"Missing build or fixed-descriptor limits cannot produce a supported Vulkan RT set");
			}
#endif
		}

		void RunTriangleTests(SelfTestContext& context) noexcept
		{
			const auto validate = [](const RHIASIndexedTriangles& input) noexcept
				{
					return ValidateRHIASIndexedTriangles(input, GeometryBuffer, GeometryBuffer, Limits);
				};
			auto input = Triangle();
			context.Check(validate(input).IsValid(),
				"Interleaved 56-byte vertices need only the last position, not a trailing vertex stride");
			input.m_Vertices.m_SizeInBytes = 123;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidBufferRange,
				"A position truncated by one byte rejects the BLAS input");
			input = Triangle();
			input.m_Vertices.m_OffsetInBytes = std::numeric_limits<uint64_t>::max() - 8;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidBufferRange,
				"Buffer range addition cannot wrap around to authorize an out-of-bounds position");
			input = Triangle();
			input.m_Vertices.m_Buffer = { 0, 0 };
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidBuffer,
				"An index with an invalid handle generation is not a build input");
			input = Triangle();
			input.m_VertexCount = 0;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidVertexLayout,
				"An empty position stream cannot underflow the last-vertex calculation");
			input = Triangle();
			input.m_VertexStrideInBytes = 54;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidVertexLayout,
				"Position strides must preserve float alignment");
			input = Triangle();
			input.m_VertexFormat = RHIFormat::R32G32Float;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidVertexFormat,
				"The initial triangle contract requires three float position components");
			input = Triangle();
			input.m_IndexFormat = RHIFormat::R32Float;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidIndexFormat,
				"The source pool's R32 unsigned indices cannot be relabeled as floats");
			input = Triangle();
			input.m_IndexCount = 4;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidIndexCount,
				"Incomplete triangle indices fail instead of truncating a primitive");
			input = Triangle();
			input.m_Indices.m_SizeInBytes = 11;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidBufferRange,
				"The complete index stream must lie inside the declared slice");
			input = Triangle();
			input.m_IndexCount = 6;
			input.m_Indices.m_SizeInBytes = 24;
			auto limits = Limits;
			limits.m_MaxPrimitiveCount = 1;
			const auto capacity = ValidateRHIASIndexedTriangles(input, GeometryBuffer, GeometryBuffer, limits);
			context.Check(capacity.m_Error == RHIASValidationError::PrimitiveLimitExceeded,
				"Primitive capacity failure cannot silently drop geometry");
			auto hostBuffer = GeometryBuffer;
			hostBuffer.m_MemoryUsage = RHIMemoryUsage::CpuToGpu;
			const auto memory = ValidateRHIASIndexedTriangles(Triangle(), hostBuffer, GeometryBuffer, Limits);
			context.Check(memory.m_Error == RHIASValidationError::InvalidMemoryUsage,
				"Immutable geometry pools cannot alias CPU-rewritable upload storage");
			limits = Limits;
			limits.m_ScratchAlignmentInBytes = 3;
			const auto alignment = ValidateRHIASIndexedTriangles(Triangle(), GeometryBuffer, GeometryBuffer, limits);
			context.Check(alignment.m_Error == RHIASValidationError::InvalidLimits,
				"Malformed backend limits fail before a native build");
			context.Check(ValidateRHIASBuildPreference(RHIASBuildPreference::FastTrace).IsValid() &&
				ValidateRHIASBuildPreference(RHIASBuildPreference::FastBuild).IsValid() &&
				!ValidateRHIASBuildPreference(static_cast<RHIASBuildPreference>(255)).IsValid(),
				"Build policy accepts the two full-build preferences without inventing an update mode");
		}

		void RunInstanceTests(SelfTestContext& context) noexcept
		{
			std::array instances
			{
				RHIASInstanceDesc{ .m_BottomLevel = { 0, 1 } },
				RHIASInstanceDesc{ .m_BottomLevel = { 1, 1 }, .m_InstanceIndex = 0xffffff, .m_Mask = 0 },
			};
			instances[0].m_Transform = { -2, 0, 0, 12, 0, 3, 0, -4, 0, 0, 4, 8 };
			instances[0].m_Flags = RHIASInstanceFlags::TriangleCullDisable |
				RHIASInstanceFlags::TriangleFrontCounterClockwise;
			context.Check(ValidateRHIASInstances(instances, Limits).IsValid(),
				"Mirrored affine transforms, 24-bit IDs and mask-zero BLEND entries are legal");
			const RHIASBuildInputs bottom{ .m_Input = Triangle() };
			const RHIASBuildInputs top{ .m_Input = std::span<const RHIASInstanceDesc>{ instances } };
			context.Check(bottom.GetType() == RHIAccelerationStructureType::BottomLevel &&
				top.GetType() == RHIAccelerationStructureType::TopLevel,
				"Disjoint build inputs determine BLAS versus TLAS without inconsistent type labels");
			auto invalid = instances;
			invalid[1].m_InstanceIndex = 0x1000000;
			context.Check(ValidateRHIASInstances(invalid, Limits).m_Error ==
				RHIASValidationError::InvalidInstanceIndex,
				"Instance IDs must fit both backends without high-bit truncation");
			invalid = instances;
			invalid[1].m_BottomLevel.Reset();
			context.Check(ValidateRHIASInstances(invalid, Limits).m_Error ==
				RHIASValidationError::InvalidBottomLevel,
				"Even masked entries require a valid logical BLAS reference");
			invalid = instances;
			invalid[1].m_Flags = static_cast<RHIASInstanceFlags>(0x80);
			context.Check(ValidateRHIASInstances(invalid, Limits).m_Error ==
				RHIASValidationError::InvalidInstanceFlags,
				"Unknown native instance bits cannot enter the neutral contract");
			for (const float bad : { 0.0f, std::numeric_limits<float>::infinity(),
				std::numeric_limits<float>::quiet_NaN() })
			{
				invalid = instances;
				invalid[1].m_Transform[0] = bad;
				context.Check(ValidateRHIASInstances(invalid, Limits).m_Error ==
					RHIASValidationError::InvalidTransform,
					"Singular and non-finite instance transforms fail before native encoding");
			}
			auto limits = Limits;
			limits.m_MaxInstanceCount = 1;
			context.Check(ValidateRHIASInstances(instances, limits).m_Error ==
				RHIASValidationError::InstanceLimitExceeded &&
				ValidateRHIASInstances({}, Limits).m_Error == RHIASValidationError::InvalidInstanceCount,
				"Instance overflow and an empty TLAS report failure without truncation");
		}

		void RunScratchTests(SelfTestContext& context) noexcept
		{
			RHIBufferDesc buffer{
				.m_SizeInBytes = 1024,
				.m_Usage = RHIBufferUsage::UnorderedAccess,
			};
			RHIASBufferRange scratch{ RHIBufferHandle{ 2, 1 }, 256, 256 };
			const RHIASBuildSizes sizes{ 1024, 256 };
			context.Check(ValidateRHIASScratch(scratch, buffer, sizes, Limits).IsValid(),
				"GPU-only scratch covers the queried full-build requirement at backend alignment");
			scratch.m_OffsetInBytes = 128;
			context.Check(ValidateRHIASScratch(scratch, buffer, sizes, Limits).m_Error ==
				RHIASValidationError::InvalidScratchAlignment,
				"An otherwise in-bounds scratch slice must satisfy the device's alignment");
			scratch.m_OffsetInBytes = 256;
			scratch.m_SizeInBytes = 255;
			context.Check(ValidateRHIASScratch(scratch, buffer, sizes, Limits).m_Error ==
				RHIASValidationError::InvalidBuildSizes,
				"Insufficient scratch fails without shrinking the native build");
			scratch.m_SizeInBytes = 256;
			buffer.m_Usage = RHIBufferUsage::CopyDest;
			context.Check(ValidateRHIASScratch(scratch, buffer, sizes, Limits).m_Error ==
				RHIASValidationError::InvalidScratchUsage,
				"A copy-only buffer cannot act as build scratch");
			buffer.m_Usage = RHIBufferUsage::UnorderedAccess;
			buffer.m_CreateFlags = RHIBufferCreateFlags::UploadStorage;
			context.Check(ValidateRHIASScratch(scratch, buffer, sizes, Limits).m_Error ==
				RHIASValidationError::InvalidMemoryUsage,
				"Batch upload storage cannot be repurposed as GPU-written scratch");
			for (const auto stages : { RHIStage::AccelerationStructureBuild, RHIStage::All,
				RHIStage::AccelerationStructureBuild | RHIStage::ComputeShader })
			{
				const auto state = ToRHIResourceState(RGBufferAccess::AccelerationStructureScratch, stages);
				context.Check(state.m_Access == RHIAccess::AccelerationStructureScratch &&
					HasUavAccess(state) &&
					ToD3D12BarrierAccess(state.m_Access) == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
					"DX12 scratch keeps ordered storage semantics and native UAV access for every stage scope");
			}
		}

		void RunBindingTests(SelfTestContext& context) noexcept
		{
			const LayoutBindingRequirement tlas{
				.m_Key = LayoutBindingKey{ 0x9000 },
				.m_Slot = {
					.m_Type = RHIBindingType::AccelerationStructure,
					.m_Visibility = RHIShaderStage::Compute,
					.m_Binding = 8,
				},
				.m_ParameterOrder = 20,
			};
			const std::array rayBindings{ tlas };
			const auto forward = scene_bindings::ComposeForwardLayout(
				scene_bindings::GetCommonLayoutContract(), true);
			context.Check(forward.IsSuccess(), "The existing Forward+ contract remains available");
			if (!forward.m_Layout)
			{
				return;
			}
			const std::array fragments
			{
				LayoutFragment{ "Forward", forward.m_Layout->GetBindings() },
				LayoutFragment{ "Ray query", rayBindings },
			};
			const std::array reservations
			{
				LayoutBindingReservation{
					.m_Owner = tlas.m_Key, .m_Type = RHIBindingType::AccelerationStructure, .m_Binding = 8 },
			};
			const auto composed = ComposeLayoutFragments(fragments, reservations);
			context.Check(composed.IsSuccess() && composed.m_Layout->GetParameterCount() ==
				forward.m_Layout->GetParameterCount() + 1,
				"A fixed TLAS adds its own parameter alongside Forward+ and global heaps");
			if (!composed.m_Layout)
			{
				return;
			}
			const auto bindings = composed.m_Layout->GetBindings();
			const auto atmosphere = std::ranges::find(bindings, scene_bindings::AtmosphereConstants,
				&LayoutBindingRequirement::m_Key);
			context.Check(atmosphere != bindings.end() && atmosphere->m_Slot.m_Binding == 5 &&
				atmosphere->m_Slot.m_Type == RHIBindingType::ConstantBuffer,
				"TLAS composition preserves Atmosphere's independent b5 ownership");
			for (uint32_t variant = 0; variant < 5; ++variant)
			{
				auto changed = tlas;
				switch (variant)
				{
				case 0:
					changed.m_Slot.m_Count = 2;
					break;
				case 1:
					changed.m_Slot.m_Space = 1;
					break;
				case 2:
					changed.m_Slot.m_BufferBindingMode = RHIBufferBindingMode::Dynamic;
					break;
				case 3:
					changed.m_Slot.m_SizeInBytes = 64;
					break;
				default:
					changed.m_PayloadTag = LayoutPayloadTag{ 1 };
					break;
				}
				const std::array changedBindings{ changed };
				const std::array invalid{ LayoutFragment{ "Invalid TLAS", changedBindings } };
				constexpr std::array errors
				{
					LayoutCompositionErrorCode::InvalidCount,
					LayoutCompositionErrorCode::InvalidRegisterSpace,
					LayoutCompositionErrorCode::InvalidBufferMode,
					LayoutCompositionErrorCode::InvalidSize,
					LayoutCompositionErrorCode::InvalidPayloadType,
				};
				const auto result = ComposeLayoutFragments(invalid);
				context.Check(!result.m_Layout && result.m_Error.m_Code == errors[variant],
					"TLAS bindings reject arrays, global space, dynamic buffers and constant payload labels");
			}
			auto conflicting = tlas;
			conflicting.m_Key = LayoutBindingKey{ 0x9001 };
			conflicting.m_Slot.m_Type = RHIBindingType::ReadOnlyStorageBuffer;
			const std::array conflictBindings{ tlas, conflicting };
			const std::array conflict{ LayoutFragment{ "Read-only collision", conflictBindings } };
			context.Check(ComposeLayoutFragments(conflict).m_Error.m_Code ==
				LayoutCompositionErrorCode::RegisterOverlap,
				"TLAS and ordinary read-only resources share the HLSL t-register namespace");
			const std::array wrongOwnerBindings{ conflicting };
			const std::array wrongOwner{ LayoutFragment{ "Wrong owner", wrongOwnerBindings } };
			context.Check(ComposeLayoutFragments(wrongOwner, reservations).m_Error.m_Code ==
				LayoutCompositionErrorCode::ReservedBindingConflict,
				"A TLAS reservation cannot be taken by a different semantic resource");
			auto hitData = conflicting;
			hitData.m_Slot.m_Binding = 9;
			hitData.m_Slot.m_SizeInBytes = 32;
			hitData.m_PayloadTag = LayoutPayloadTag{ 90 };
			auto incompatibleHitData = hitData;
			incompatibleHitData.m_PayloadTag = LayoutPayloadTag{ 91 };
			const std::array hitBindings{ hitData, incompatibleHitData };
			const std::array hitFragments{ LayoutFragment{ "Hit data", hitBindings } };
			context.Check(ComposeLayoutFragments(hitFragments).m_Error.m_Code ==
				LayoutCompositionErrorCode::ConflictingBinding,
				"Equal-size hit-data declarations with incompatible payload identities cannot merge");

			const auto desc = composed.m_Layout->BuildRHIDesc("Ray query contract");
			DX12BindingLayoutBuildResult dx12{};
			BuildDX12RootSignatureDesc(desc, dx12);
			context.Check(dx12.IsValid() && std::ranges::any_of(
				std::span(dx12.m_RootParameters.data(), dx12.m_RootParameterCount), [](const auto& parameter)
				{
					return parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV &&
						parameter.Descriptor.ShaderRegister == 8 && parameter.Descriptor.RegisterSpace == 0;
				}),
				"DX12 lowers checked fixed TLAS slots as root SRVs");
			RHIBindingLayoutDesc oversized{};
			oversized.m_SlotCount = RHIBindingLayoutDesc::MaxSlots + 1;
			BuildDX12RootSignatureDesc(oversized, dx12);
			context.Check(!dx12.IsValid() && dx12.m_Error == DX12BindingLayoutError::TooManySlots,
				"Native layout preflight cannot scan beyond the fixed slot array");
#if GGLAB_ENABLE_VULKAN
			const auto vulkan = BuildVulkanBindingLayoutPlan(desc, 16 * 1024);
			context.Check(vulkan.IsValid() && std::ranges::any_of(
				std::span(vulkan.m_Set0Bindings.data(), vulkan.m_Set0BindingCount), [](const auto& binding)
				{
					return binding.m_BindingType == RHIBindingType::AccelerationStructure &&
						binding.m_DescriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
				}), "Vulkan keeps fixed TLAS descriptors distinct from storage-buffer descriptors");
#endif
		}
	}

	void RunRayQueryContractSelfTests(SelfTestContext& context) noexcept
	{
		RunDX12PipelineIdentityTests(context);
		RHIAccelerationStructureStore store;
		auto bottom = std::make_shared<RHIAccelerationStructureBackingBase>();
		std::weak_ptr<RHIAccelerationStructureBackingBase> weakBottom = bottom;
		const auto bottomHandle = store.Insert(bottom);
		auto top = std::make_shared<RHIAccelerationStructureBackingBase>();
		top->m_Type = RHIAccelerationStructureType::TopLevel;
		top->m_BottomLevels.push_back(bottom);
		const auto topHandle = store.Insert(top);
		bottom.reset();
		context.Check(store.Destroy(bottomHandle) && !store.Resolve(bottomHandle) && !weakBottom.expired(),
			"TLAS backing retains its BLAS after logical handle destruction");
		const auto replacement = store.Insert(std::make_shared<RHIAccelerationStructureBackingBase>());
		context.Check(replacement.Index() == bottomHandle.Index() && replacement.Generation() != bottomHandle.Generation() &&
			!store.Resolve(bottomHandle) && !store.Destroy(bottomHandle),
			"AS slot reuse cannot resolve or destroy a replacement through the old generation");
		auto recorded = store.Resolve(topHandle);
		top.reset();
		context.Check(store.Destroy(topHandle) && !weakBottom.expired(),
			"A recording lease retains the TLAS and its transitive native BLAS dependencies");
		recorded.reset();
		context.Check(weakBottom.expired(), "The last recording or submission lease releases retired AS backing");
		static_assert(!std::is_convertible_v<RHIAccelerationStructureHandle, RHIBufferHandle>);
		static_assert(!std::is_convertible_v<RHIBufferHandle, RHIAccelerationStructureHandle>);
		RunCapabilityTests(context);
		RunRayScenePlanTests(context);
		RunSunVisibilityPreparationTests(context);
		RunSunVisibilityGraphTests(context);
		RunGIReceiverPreparationTests(context);
		RunGIReferenceInputTests(context);
		RunGISharedContractTests(context);
		RunProbeUpdateProfileTests(context);
		RunGIFieldTests(context);
		RunGIReferenceObservationTests(context);
		RunSpecularVisibilityTests(context);
		RunGIReferenceTilingTests(context);
		RunTriangleTests(context);
		RunInstanceTests(context);
		RunScratchTests(context);
		RunBindingTests(context);
	}
}
