#include "RayQueryContractSelfTests.h"
#include "AssetTestServices.h"
#include "Graphics/Pipeline/GIReferenceObservation.h"
#include "Graphics/Pipeline/GIComputeDispatch.h"
#include "Graphics/Pipeline/GIObservationSceneIdentity.h"
#include "Graphics/Pipeline/GIObservationHistoryViews.h"
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "Graphics/Pipeline/RaySceneQuery.h"
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
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
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
#include <cstring>
#include <memory>
#include <span>
#include <string>
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
			context.Check(GIQueryRowsPerDispatch(1024, 0, 512, gi_shader::GIReceiverMaximumQueries) == 96 &&
				GIQueryRowsPerDispatch(1024, 64, 2, gi_shader::GIReceiverMaximumQueries) == 32,
				"Dispatch splitting includes both cold matching and sampling query work");

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

		void RunGIQueryRecordingTests(SelfTestContext& context) noexcept
		{
			// Logical recording only: no native dispatches or snapshots are submitted.
			class DispatchCommands final : public RHIComputeCommandContext
			{
			public:
				RHICommandContextHandle GetHandle() const noexcept override { return {}; }
				RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
				void TrackTextureUse(RHITextureHandle) noexcept override {}
				void TrackBufferUse(RHIBufferHandle) noexcept override {}
				void TextureBarrier(std::span<const RHITextureBarrier>) noexcept override {}
				void BufferBarrier(std::span<const RHIBufferBarrier>) noexcept override {}
				void FlushBarriers() noexcept override {}
				void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override {}
				void CopyTextureToBuffer(const RHITextureToBufferCopy&) noexcept override {}
				void SetPipeline(RHIPipelineHandle) noexcept override {}
				void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
				bool SetConstantBuffer(uint32_t, const RHIConstantBufferBinding&) noexcept override { return false; }
				void SetInlineConstantWords(uint32_t, std::span<const uint32_t>, uint32_t) noexcept override {}
				void SetReadOnlyBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
				void SetReadWriteBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
				void Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept override
				{
					m_Dispatches.push_back({ m_Row, x, y, z });
					if (m_Dispatches.size() - 1 == m_FailedDispatch)
					{
						RejectRecording();
					}
				}
				uint32_t m_Row = 0;
				uint32_t m_FailedDispatch = UINT32_MAX;
				std::vector<std::array<uint32_t, 4>> m_Dispatches;
			};
			struct Case
			{
				GIQueryDispatchInfo m_Info;
				std::vector<std::array<uint32_t, 4>> m_Dispatches;
			};
			const std::array cases
			{
				Case{ { 19, 11, 7, 2, 0 }, { { 0, 3, 2, 1 } } },
				Case{ { 1024, 131, 64, 2, 0 },
					{ { 0, 128, 8, 1 }, { 64, 128, 8, 1 }, { 128, 128, 1, 1 } } },
				Case{ { 1024, 201, 0, 0, 83 },
					{ { 0, 128, 12, 1 }, { 96, 128, 12, 1 }, { 192, 128, 2, 1 } } },
				Case{ { 1024, 65, 64, 2, 83 },
					{ { 0, 128, 4, 1 }, { 32, 128, 4, 1 }, { 64, 128, 1, 1 } } },
				Case{ { 1024, 16385, 0, 512, 0 },
					{ { 0, 128, 1024, 1 }, { 8192, 128, 1024, 1 }, { 16384, 128, 1, 1 } } },
				Case{ { 16384, 19, 4096, 512, 0 },
					{ { 0, 2048, 1, 1 }, { 8, 2048, 1, 1 }, { 16, 2048, 1, 1 } } },
			};
			for (const auto& test : cases)
			{
				DispatchCommands commands;
				uint32_t uploads = 0;
				bool ordered = true;
				const bool recorded = RecordGIQueryDispatches(commands, test.m_Info,
					[&](uint32_t row) noexcept
					{
						ordered &= uploads == commands.m_Dispatches.size();
						++uploads;
						commands.m_Row = row;
						return true;
					});
				context.Check(recorded && ordered && uploads == test.m_Dispatches.size() &&
					commands.m_Dispatches == test.m_Dispatches,
					"Full-viewport query recording preserves row offsets, work caps, group rounding and upload order");
			}
			for (const auto& info : { GIQueryDispatchInfo{ .m_Height = 1 }, GIQueryDispatchInfo{ .m_Width = 1 } })
			{
				DispatchCommands commands;
				bool uploaded = false;
				const bool recorded = RecordGIQueryDispatches(commands, info,
					[&](uint32_t) noexcept
					{
						uploaded = true;
						return true;
					});
				context.Check(!recorded && !uploaded && commands.HasRecordingError() && commands.m_Dispatches.empty(),
					"Empty query extents reject before constants, division or dispatch");
			}
			enum class Failure
			{
				BeforeRecording,
				Constants,
				ConstantsRecordingError,
				FirstDispatch,
				LastDispatch,
			};
			struct FailureCase
			{
				Failure m_Failure;
				uint32_t m_Uploads;
				uint32_t m_Dispatches;
			};
			constexpr std::array failures
			{
				FailureCase{ Failure::BeforeRecording, 0, 0 },
				FailureCase{ Failure::Constants, 2, 1 },
				FailureCase{ Failure::ConstantsRecordingError, 2, 1 },
				FailureCase{ Failure::FirstDispatch, 1, 1 },
				FailureCase{ Failure::LastDispatch, 3, 3 },
			};
			for (const auto& test : failures)
			{
				DispatchCommands commands;
				commands.m_FailedDispatch = test.m_Failure == Failure::FirstDispatch ?
					0u :
					test.m_Failure == Failure::LastDispatch ? 2u : UINT32_MAX;
				if (test.m_Failure == Failure::BeforeRecording)
				{
					commands.RejectRecording();
				}
				uint32_t uploads = 0;
				const bool recorded = RecordGIQueryDispatches(commands, cases[1].m_Info,
					[&](uint32_t row) noexcept
					{
						++uploads;
						commands.m_Row = row;
						if (test.m_Failure == Failure::ConstantsRecordingError && row == 64)
						{
							commands.RejectRecording();
						}
						return test.m_Failure != Failure::Constants || row != 64;
					});
				context.Check(!recorded && uploads == test.m_Uploads && commands.m_Dispatches.size() == test.m_Dispatches,
					"Constant failures and recording errors, including the final dispatch, never report complete recording");
			}

			RGDeferredCommitList emptyCommits;
			const RGExecuteContext emptyExecute(emptyCommits, {});
			context.Check(GIReceiverCache::ResolveDescriptors(emptyExecute, {}) == std::array<uint32_t, 4>{},
				"Unimported receiver caches preserve zero descriptors without querying an execution device");
			asset_test::TestRHIDevice device;
			TransientResourcePool pool(&device);
			for (const bool writing : { false, true })
			{
				RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
				graph.AddPass<GIReceiverCache::Views>("CacheDescriptors", RGPassEncoderType::Compute,
					[&](RenderGraph::RGBuilder& builder, GIReceiverCache::Views& views)
					{
						for (size_t index = 0; index < views.size(); ++index)
						{
							const RHITextureDesc desc{ .m_Format = index < 2 ? RHIFormat::R32G32B32A32Float : RHIFormat::R32Uint,
								.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess, .m_Extent = { 8, 8, 1 } };
							auto texture = builder.ImportTexture(("Cache.Descriptor." + std::to_string(index)).c_str(),
								device.CreateTexture({ .m_Desc = desc }, {}), desc, RGTextureAccess::None, RGContentValidity::Defined);
							if (writing)
							{
								builder.WriteInPlace(texture, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
								views[index] = builder.CreateView<RHITextureViewType::UnorderedAccess>(texture);
							}
							else
							{
								views[index] = builder.CreateView<RHITextureViewType::ShaderResource>(
									builder.Read(texture, RGTextureAccess::Sample, RHIStage::ComputeShader));
							}
						}
						builder.SideEffect();
					},
					[&](RGExecuteContext& execute, GIReceiverCache::Views& views)
					{
						// The fake device gives each descriptor lookup a fresh consecutive index.
						const auto first = execute.GetViewDescriptor(views[0]).m_Index;
						context.Check(GIReceiverCache::ResolveDescriptors(execute, views) ==
							std::array<uint32_t, 4>{ first + 1, first + 2, first + 3, first + 4 },
							"Receiver descriptors resolve all SRV/UAV slots in the existing order");
						auto partial = views;
						partial[1].Reset();
						partial[3].Reset();
						context.Check(GIReceiverCache::ResolveDescriptors(execute, partial) ==
							std::array<uint32_t, 4>{ first + 5, 0, first + 6, 0 },
							"Missing receiver views keep zero slots rather than compacting or resolving invalid ids");
						context.Check(GIReceiverCache::ResolveDescriptors(execute, {}) == std::array<uint32_t, 4>{} &&
							execute.GetViewDescriptor(views[0]).m_Index == first + 7,
							"Empty receiver mappings do not publish or consume descriptors");
					});
				context.Check(graph.Compile(), "Receiver descriptor resolution accepts both read and preparation views");
				if (graph.GetExecutionPlan())
				{
					DispatchCommands commands;
					RGDeferredCommitList commits;
					RGExecuteContext execute(commits, { .m_DirectComputeCommandContext = &commands });
					graph.Execute(execute);
					graph.Retire({});
				}
			}
		}

		void RunRaySceneQueryTests(SelfTestContext& context) noexcept
		{
			class QueryCommands final : public RHIComputeCommandContext
			{
			public:
				RHICommandContextHandle GetHandle() const noexcept override { return {}; }
				RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
				void TrackTextureUse(RHITextureHandle) noexcept override {}
				void TrackBufferUse(RHIBufferHandle) noexcept override {}
				void TrackAccelerationStructureUse(RHIAccelerationStructureHandle) noexcept override {}
				void TextureBarrier(std::span<const RHITextureBarrier>) noexcept override {}
				void BufferBarrier(std::span<const RHIBufferBarrier>) noexcept override {}
				void AccelerationStructureBarrier(RHIAccelerationStructureHandle,
					RHIResourceState, RHIResourceState) noexcept override {}
				void FlushBarriers() noexcept override {}
				void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override {}
				void CopyTextureToBuffer(const RHITextureToBufferCopy&) noexcept override {}
				void SetPipeline(RHIPipelineHandle) noexcept override {}
				void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
				bool SetConstantBuffer(uint32_t, const RHIConstantBufferBinding&) noexcept override { return false; }
				void SetInlineConstantWords(uint32_t, std::span<const uint32_t>, uint32_t) noexcept override {}
				bool SetAccelerationStructure(uint32_t parameter,
					RHIAccelerationStructureHandle handle) noexcept override
				{
					m_TopLevelParameter = parameter;
					m_TopLevel = handle;
					return m_AcceptTopLevel;
				}
				void SetReadOnlyBuffer(uint32_t parameter, RHIBufferHandle buffer, uint64_t) noexcept override
				{
					m_Parameters.push_back(parameter);
					m_Buffers.push_back(buffer);
					if (parameter == m_FailedParameter)
					{
						RejectRecording();
					}
				}
				void SetReadWriteBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
				void Dispatch(uint32_t, uint32_t, uint32_t) noexcept override {}
				bool m_AcceptTopLevel = true;
				uint32_t m_FailedParameter = UINT32_MAX;
				uint32_t m_TopLevelParameter = UINT32_MAX;
				RHIAccelerationStructureHandle m_TopLevel;
				std::vector<uint32_t> m_Parameters;
				std::vector<RHIBufferHandle> m_Buffers;
			};

			for (const auto inputs : { ray_scene_bindings::QueryInputs::GeometryOnly,
				ray_scene_bindings::QueryInputs::Materials })
			{
				const uint32_t count = inputs == ray_scene_bindings::QueryInputs::GeometryOnly ?
					ray_scene_bindings::GeometryOnlyCount : ray_scene_bindings::Count;
				std::vector<LayoutBindingRequirement> bindings;
				ray_scene_bindings::Append(bindings, count);
				// Scramble native parameter order independently of semantic slots and t5-t10.
				constexpr std::array orders{ 30u, 3u, 18u, 1u, 9u, 5u };
				for (uint32_t slot = 0; slot < count; ++slot)
				{
					bindings[slot].m_ParameterOrder = orders[slot];
				}
				const std::array fragments{ LayoutFragment{ "Query", bindings } };
				const auto layout = ComposeLayoutFragments(fragments);
				context.Check(layout.IsSuccess(), "Query layouts allow non-semantic native parameter order");
				if (!layout.IsSuccess())
				{
					continue;
				}
				std::array<uint32_t, ray_scene_bindings::Count> parameters{};
				auto selected = std::span(parameters).first(count);
				context.Check(ray_scene_bindings::ResolveParameters(*layout.m_Layout, selected) &&
					parameters[ray_scene_bindings::TopLevel] != ray_scene_bindings::TopLevel,
					"Query parameters resolve semantic keys rather than assuming native slot indices");
				std::array<uint32_t, ray_scene_bindings::Count> untouched;
				untouched.fill(UINT32_MAX);
				if (count == ray_scene_bindings::GeometryOnlyCount)
				{
					context.Check(!ray_scene_bindings::ResolveParameters(*layout.m_Layout, untouched) &&
						std::ranges::all_of(untouched, [](uint32_t value) { return value == UINT32_MAX; }),
						"A missing material binding rejects the complete mapping without partial publication");
				}
				context.Check(!ray_scene_bindings::ResolveParameters(*layout.m_Layout,
					std::span(untouched).first(1)) && untouched[0] == UINT32_MAX,
					"Incomplete query input sets cannot publish a parameter mapping");

				asset_test::TestRHIDevice device;
				TransientResourcePool pool(&device);
				RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
				RaySceneGraphResources scene;
				RHIAccelerationStructureHandle topLevel;
				std::vector<RHIBufferHandle> expectedBuffers;
				struct Empty {};
				graph.AddPass<Empty>("ImportQuery", [&](RenderGraph::RGBuilder& builder, Empty&)
					{
						const RHIAccelerationStructureDesc desc{
							.m_Type = RHIAccelerationStructureType::TopLevel, .m_SizeInBytes = 256 };
						topLevel = device.CreateAccelerationStructure(desc);
						scene.m_TopLevel = builder.Import<RGAccelerationStructureResource>("Query.TLAS",
							topLevel, desc, RGAccelerationStructureAccess::QueryRead, RGContentValidity::Defined);
						for (uint32_t index = 0; index < 2; ++index)
						{
							const RHIAccelerationStructureDesc bottomDesc{ .m_SizeInBytes = 256 };
							scene.m_BottomLevels.push_back(builder.Import<RGAccelerationStructureResource>(
								("Query.BLAS." + std::to_string(index)).c_str(),
								device.CreateAccelerationStructure(bottomDesc), bottomDesc,
								RGAccelerationStructureAccess::QueryRead, RGContentValidity::Undefined));
						}
						const std::array ids
						{
							&scene.m_Vertices, &scene.m_Indices, &scene.m_Geometries,
							&scene.m_Instances, &scene.m_Materials,
						};
						for (uint32_t slot = ray_scene_bindings::Vertices; slot < count; ++slot)
						{
							const RHIBufferDesc bufferDesc{ .m_SizeInBytes = 256, .m_StrideInBytes = 16,
								.m_Usage = RHIBufferUsage::Structured };
							const auto handle = device.CreateBuffer(bufferDesc, {});
							expectedBuffers.push_back(handle);
							*ids[slot - ray_scene_bindings::Vertices] = builder.ImportBuffer(
								("Query.Buffer." + std::to_string(slot)).c_str(), handle, bufferDesc,
								RGBufferAccess::StructuredRead, RGContentValidity::Defined);
						}
					});
				uint32_t built = 0;
				for (uint32_t index = 0; index < scene.m_BottomLevels.size(); ++index)
				{
					graph.AddPass<Empty>(("BuildQueryBLAS." + std::to_string(index)).c_str(),
						RGPassEncoderType::Compute,
						[&, index](RenderGraph::RGBuilder& builder, Empty&)
						{
							builder.WriteInPlace(scene.m_BottomLevels[index], RGAccelerationStructureAccess::BuildWrite);
						},
						[&](RGExecuteContext&, Empty&) { ++built; });
				}
				bool bound = false;
				uint32_t parameterCount = count;
				graph.AddPass<Empty>("Query", RGPassEncoderType::Compute,
					[&](RenderGraph::RGBuilder& builder, Empty&)
					{
						ray_scene_bindings::DeclareQueryReads(builder, scene, inputs);
						builder.SideEffect();
					},
					[&](RGExecuteContext& execute, Empty&)
					{
						// Logical recording fixture only: no native query or scene snapshot is submitted.
						bound = ray_scene_bindings::BindQueryResources(*execute.GetDirectComputeCommandContext(),
							execute, scene, std::span(parameters).first(parameterCount));
					});
				context.Check(graph.Compile(), "Both query input sets compile with all BLAS producer dependencies");
				if (!graph.GetExecutionPlan())
				{
					continue;
				}
				for (uint32_t failure = 0; failure < 4; ++failure)
				{
					QueryCommands commands;
					commands.m_AcceptTopLevel = failure != 1;
					commands.m_FailedParameter = failure == 2 ? parameters[ray_scene_bindings::Indices] : UINT32_MAX;
					parameterCount = failure == 3 ? 1 : count;
					RGDeferredCommitList commits;
					RGExecuteContext execute(commits, { .m_DirectComputeCommandContext = &commands });
					built = 0;
					graph.Execute(execute);
					context.Check(built == scene.m_BottomLevels.size(),
						"Every traversed BLAS producer remains live before a query");
					if (failure == 0)
					{
						context.Check(bound && commands.m_TopLevel == topLevel &&
							commands.m_TopLevelParameter == parameters[ray_scene_bindings::TopLevel] &&
							commands.m_Buffers == expectedBuffers &&
							commands.m_Parameters == std::vector<uint32_t>(selected.begin() + 1, selected.end()),
							"Query binding preserves native parameters, pool order and the optional material table");
					}
					else
					{
						const size_t expected = failure == 2 ? 2 : 0;
						context.Check(!bound && commands.m_Buffers.size() == expected &&
							(failure == 1 || commands.HasRecordingError()),
							"Invalid input counts, rejected TLAS and buffer recording errors stop query setup");
					}
					graph.Retire({});
				}
			}
		}

		void RunGIReceiverObservationTests(SelfTestContext& context) noexcept
		{
			GIReceiverViewSnapshot first{ .m_RenderExtent = { 320, 200 }, .m_DisplayExtent = { 320, 200 },
				.m_ReceiversAvailable = true, .m_Supported = true };
			first = AdvanceGIReceiverView({}, first);
			context.Check(first.m_Revision == 1 && AdvanceGIReceiverView(first, first).m_Revision == 1,
				"A logical receiver view stays bound across ordinary frame advancement");
			auto moved = first;
			moved.m_CameraPosition[0] = 1;
			moved = AdvanceGIReceiverView(first, moved);
			context.Check(moved.m_Revision == 2, "Camera movement invalidates earlier pixel bindings");
			auto resized = moved;
			resized.m_RenderExtent[0] = 640;
			resized.m_DisplayExtent[0] = 640;
			resized = AdvanceGIReceiverView(moved, resized);
			context.Check(resized.m_Revision == 3, "Receiver extent changes advance the logical view revision");
			GIFieldSummary summary{ .m_Field = { .m_Generation = 7, .m_RefreshEpoch = 2, .m_EpochOriginUpdate = 80 },
				.m_Counts = { 2, 2, 2 }, .m_ReceiverView = first };
			GIReceiverInspectionRequest request{ .m_Pixel = { 160, 100 }, .m_ExpectedGeneration = 7,
				.m_ExpectedViewRevision = 1, .m_ExpectedRefreshEpoch = 2 };
			context.Check(MatchesGIReceiverInspection(request, summary), "A receiver request binds its field and logical view");
			summary.m_ReceiverView = moved;
			context.Check(!MatchesGIReceiverInspection(request, summary), "A newer camera cannot satisfy an old pixel request");
			summary.m_ReceiverView = first;
			request.m_Pixel[0] = 320;
			context.Check(!MatchesGIReceiverInspection(request, summary), "Out-of-bounds pixels reject without clamping");
			request.m_Pixel[0] = 160;
			std::vector<uint8_t> bytes(GIReceiverInspectionBytes);
			std::array<uint32_t, 4> header{ GIReceiverInspectionMagic,
				uint32_t(GIReceiverInspectionValidity::NoValidSupport), 1, 0 };
			std::memcpy(bytes.data(), header.data(), 16);
			const std::array<uint32_t, 4> profile{ 2, 2, 2, 6 };
			std::memcpy(bytes.data() + 8 * 16, profile.data(), 16);
			std::array<GIReceiverCorner, 8> corners{};
			for (uint32_t index = 0; index < corners.size(); ++index)
			{
				corners[index].m_Coordinate = { index & 1, (index >> 1) & 1, (index >> 2) & 1 };
				corners[index].m_Classification = 2;
				corners[index].m_Reason = GIReceiverCornerReason::NotActive;
			}
			std::memcpy(bytes.data() + 9 * 16, corners.data(), sizeof(corners));
			const GIObservationStamp stamp{ .m_Field = summary.m_Field,
				.m_Kind = GIObservationKind::ReceiverContribution, .m_ProducingFrame = 913,
				.m_FieldUpdate = 85, .m_ReadinessUpdate = 5 };
			const auto result = DecodeGIReceiverInspection(request, stamp, first, bytes);
			context.Check(result->m_Validity == GIReceiverInspectionValidity::NoValidSupport && result->m_CageEvaluated &&
				result->m_Corners[7].m_Coordinate == std::array<uint32_t, 3>{ 1, 1, 1 } &&
				result->m_Corners[7].m_NormalizationAvailable == 0,
				"A bounded receiver payload preserves every corner and normalization availability");
			context.Check(DecodeGIReceiverInspection(request, stamp, moved, bytes)->m_Validity ==
				GIReceiverInspectionValidity::MalformedPayload, "Payload decoding checks the bound logical view");
			const float nan = std::numeric_limits<float>::quiet_NaN();
			std::memcpy(bytes.data() + 9 * 16 + offsetof(GIReceiverCorner, m_AtlasM), &nan, sizeof(nan));
			context.Check(DecodeGIReceiverInspection(request, stamp, first, bytes)->m_Validity ==
				GIReceiverInspectionValidity::InvalidNumerics && std::isfinite(result->m_Corners[0].m_AtlasM[0]),
				"Invalid numerical evidence is explicit and retained CPU observations survive readback reuse");
			context.Check(DecodeGIReceiverInspection(request, stamp, first, std::span(bytes).first(16))->m_Validity ==
				GIReceiverInspectionValidity::MalformedPayload, "A truncated GPU record cannot publish a valid receiver");
		}

		void RunGIFieldTests(SelfTestContext& context) noexcept
		{
			GIFieldReadinessEstimator estimator;
			const std::array<uint32_t, 3> readinessCounts{ 5, 5, 5 };
			GIFieldReadinessSettings readinessSettings;
			std::vector<GIFieldProbeChange> changes(125);
			const auto observe = [&](uint32_t update, float change, bool active = true)
				{
					for (auto& probe : changes)
					{
						probe.m_Active = active ? 1.0f : 0.0f;
						probe.m_Updates = update;
						probe.m_Change = change;
					}
					return estimator.Observe(update, 0.001f, changes);
				};
			estimator.Reset(readinessCounts, readinessSettings);
			context.Check(!estimator.GetMeasurement(), "A reset has no completed change measurement");
			context.Check(estimator.GetSnapshot().m_State == GIFieldReadinessState::Initializing,
				"A replacement field starts without a readiness certificate");
			for (uint32_t update = 1; update <= 62; ++update)
			{
				context.Check(observe(update, 0), "Finite black-field measurements advance in order");
				if (update == 1)
				{
					const auto warming = estimator.GetMeasurement();
					context.Check(warming->m_Blocks[0].m_CurrentQuiet && !warming->m_Blocks[0].m_WarmingMet &&
						warming->m_Blocks[0].m_QuietUpdates == 0 && warming->m_Blocks[0].m_Blockers == 9,
						"Low measured change remains distinct from warming and the full quiet hold");
				}
				if (update == 50)
				{
					const auto partialHold = estimator.GetMeasurement();
					context.Check(partialHold->m_Blocks[0].m_CurrentQuiet &&
						partialHold->m_Blocks[0].m_QuietUpdates == 17 && partialHold->m_Blocks[0].m_Blockers == 8,
						"A currently quiet block explicitly reports its incomplete 17/30 hold");
				}
			}
			context.Check(estimator.GetSnapshot().m_State != GIFieldReadinessState::Ready,
				"Warm-up and thirty quiet updates cannot be replaced by elapsed submissions");
			context.Check(observe(63, 0) && estimator.GetSnapshot().m_State == GIFieldReadinessState::Ready &&
				estimator.GetSnapshot().m_QuietBlocks == 8 && estimator.GetSnapshot().m_ReadyUpdate == 63,
				"The exact-black field becomes Ready with a positive scene-referred denominator");
			const auto retained = estimator.GetMeasurement();
			context.Check(retained && retained->m_Probes.size() == 125 && retained->m_Blocks.size() == 8 &&
				retained->m_Blocks[0].m_ActiveProbes == 64 && retained->m_Blocks[7].m_ActiveProbes == 1 &&
				retained->m_Blocks[0].m_CurrentQuiet && retained->m_Blocks[0].m_QuietUpdates == 30 &&
				retained->m_IncludesDistance &&
				retained->m_Blocks[0].m_Blockers == 0 && retained->m_Readiness.m_CompletedUpdates == 63,
				"Completed blocks retain estimator decisions, edge-block counts and the full quiet hold");
			context.Check(!observe(63, 0) && estimator.GetSnapshot().m_ReadyUpdate == 63,
				"Repeated polls cannot advance quiet-update counts");
			context.Check(observe(64, 0.2f) && estimator.GetSnapshot().m_State == GIFieldReadinessState::Settling,
				"Observed changes revoke Ready without changing shading");
			context.Check(retained->m_Probes[0].m_Change == 0 && retained->m_Blocks[0].m_Blockers == 0 &&
				estimator.GetMeasurement()->m_Blocks[0].m_Blockers == 14,
				"New changes publish new immutable values with percentile, maximum and quiet-hold blockers");
			for (uint32_t update = 65; update <= 240; ++update)
			{
				context.Check(observe(update, 0.2f), "Nonquiet measurements remain valid diagnostic data");
			}
			context.Check(estimator.GetSnapshot().m_State == GIFieldReadinessState::Unconverged &&
				!estimator.GetSnapshot().m_Refreshing,
				"The timeout ends refresh and preserves an explicit Unconverged verdict");
			for (uint32_t update = 241; update <= 270; ++update)
			{
				context.Check(observe(update, 0), "Unconverged fields continue to be measured");
			}
			context.Check(estimator.GetSnapshot().m_State == GIFieldReadinessState::Ready,
				"A timed-out field can later establish thirty quiet completed updates");
			estimator.Reset(readinessCounts, readinessSettings);
			for (uint32_t update = 1; update <= 4; ++update)
			{
				context.Check(observe(update, 0, false), "Excluded blocks preserve finite observation records");
			}
			context.Check(estimator.GetSnapshot().m_ExcludedBlocks == 8 &&
				estimator.GetSnapshot().m_State == GIFieldReadinessState::Ready,
				"Blocks without Active probes are reported and do not block readiness");
			context.Check(!estimator.GetMeasurement()->m_Blocks[0].m_Percentile95 &&
				!estimator.GetMeasurement()->m_Blocks[0].m_Maximum &&
				estimator.GetMeasurement()->m_Blocks[0].m_Blockers == 0,
				"Excluded blocks have unavailable statistics and never acquire readiness blockers");
			estimator.Reset(readinessCounts, readinessSettings);
			context.Check(!observe(2, 0) && estimator.GetSnapshot().m_Failed,
				"Missing completed measurements cannot silently accumulate quiet time");
			estimator.Reset(readinessCounts, readinessSettings);
			changes[0].m_Change = std::numeric_limits<float>::quiet_NaN();
			for (auto& probe : changes)
			{
				probe.m_Updates = 1;
			}
			context.Check(!estimator.Observe(1, 0.001f, changes) && estimator.GetSnapshot().m_Failed,
				"Nonfinite readiness records are contract failures, including excluded probes");
			estimator.Reset(readinessCounts, readinessSettings);
			context.Check(!estimator.Observe(1, 0, changes),
				"An all-black field never admits a zero irradiance floor");

			context.Check(estimator.GetSnapshot().m_NoiseSigma == 0,
				"Measured noise never silently changes this owner's threshold profile");
			GIFieldReadinessSettings noisySettings{ .m_NoiseSigma = 0.1f, .m_SteadyNoiseSigma = 0.001f };
			estimator.Reset(readinessCounts, noisySettings);
			for (uint32_t update = 1; update <= 63; ++update)
			{
				context.Check(observe(update, 0.2f), "Explicitly measured refresh noise is an immutable input");
			}
			context.Check(estimator.GetSnapshot().m_State == GIFieldReadinessState::Ready &&
				estimator.GetSnapshot().m_RefreshNoiseSamples == 30 &&
				std::abs(estimator.GetSnapshot().m_RefreshNoiseEstimate - 0.2f) < 1.0e-6f,
				"The reported per-probe RMS matches a constant frozen-input change stream");
			for (auto& probe : changes)
			{
				probe.m_Updates = 64;
			}
			context.Check(estimator.Observe(64, 0.001f, changes, 0.97f, false) &&
				estimator.GetSnapshot().m_Threshold == 0.01f &&
				estimator.GetSnapshot().m_State == GIFieldReadinessState::Settling,
				"Changing alpha selects its own noise input and restarts the quiet hold");
			context.Check(!estimator.GetMeasurement()->m_RefreshMode &&
				estimator.GetMeasurement()->m_Blocks[0].m_QuietUpdates == 0 &&
				estimator.GetMeasurement()->m_Readiness.m_Hysteresis == 0.97f,
				"Change explanations retain the measurement phase and its adopted hysteresis");

			GIFieldReadinessEstimator outliers;
			std::vector<GIFieldProbeChange> blockProbes(64);
			outliers.Reset({ 4, 4, 4 }, readinessSettings);
			for (uint32_t update = 1; update <= 34; ++update)
			{
				for (auto& probe : blockProbes)
				{
					probe = { .m_Change = 0, .m_Active = 1, .m_Updates = update };
				}
				blockProbes.back().m_Change = 0.04f;
				context.Check(outliers.Observe(update, 0.001f, blockProbes), "A finite outlier remains an admitted observation");
			}
			const auto singleOutlier = outliers.GetMeasurement();
			context.Check(singleOutlier->m_Blocks[0].m_Percentile95 == 0 &&
				singleOutlier->m_Blocks[0].m_Maximum == 0.04f && singleOutlier->m_Blocks[0].m_Blockers == 12,
				"One high outlier blocks maximum independently of the p95 condition");
			for (auto& probe : blockProbes)
			{
				probe.m_Updates = 35;
			}
			for (size_t index = 60; index < 64; ++index)
			{
				blockProbes[index].m_Change = 0.02f;
			}
			context.Check(outliers.Observe(35, 0.001f, blockProbes) &&
				outliers.GetMeasurement()->m_Blocks[0].m_Blockers == 10,
				"The p95 blocker remains distinct when the maximum is below three tau");
			const auto beforeReset = outliers.GetMeasurement();
			outliers.Reset({ 4, 4, 4 }, readinessSettings, true);
			context.Check(!outliers.GetMeasurement() && beforeReset->m_Blocks[0].m_Blockers == 10,
				"Refresh clears current change data while retained CPU observations keep their values");
			for (auto& probe : blockProbes)
			{
				probe.m_Updates = 1;
			}
			context.Check(outliers.Observe(1, 0.001f, blockProbes) &&
				!outliers.GetMeasurement()->m_IncludesDistance && outliers.GetMeasurement()->m_WarmingUpdates == 4,
				"Lighting-refresh explanations retain irradiance-only measurements and their own warming span");

			estimator.Reset(readinessCounts, noisySettings);
			for (auto& probe : changes)
			{
				probe.m_Updates = 1;
			}
			context.Check(estimator.Observe(1, 0.001f, changes, 0.5f, false) &&
				estimator.GetSnapshot().m_NoiseSigma == noisySettings.m_SteadyNoiseSigma,
				"Equal refresh and steady alpha still select the explicitly declared phase noise");
			estimator.Reset(readinessCounts, readinessSettings, true);
			context.Check(estimator.GetSnapshot().m_State == GIFieldReadinessState::Settling &&
				estimator.GetSnapshot().m_CompletedUpdates == 0 && estimator.GetSnapshot().m_ReadyUpdate == 0,
				"A published lighting refresh keeps field history but starts a new Settling epoch");
			GIUpdateStatus status;
			context.Check(IsGIUpdateStatusValid(status, 8, 256, 1),
				"Clean GPU status has no stale error coordinates");
			status.m_Probe = 0;
			context.Check(!IsGIUpdateStatusValid(status, 8, 256, 1),
				"A partially initialized clean status cannot publish a field");
			status = { GIUpdateError::InvalidRay, 7, 2, 255 };
			context.Check(IsGIUpdateStatusValid(status, 8, 256, 3) &&
				!IsGIUpdateStatusValid(status, 8, 256, 2) &&
				!IsGIUpdateStatusValid(status, 7, 256, 3) &&
				!IsGIUpdateStatusValid(status, 8, 255, 3),
				"Error records must belong to this owner's completed readback and ray capacity");
			for (const auto error : { GIUpdateError::InvalidHistory, GIUpdateError::InvalidBlend })
			{
				status = { error, 0, 0, gi_shader::GIProbeInteriorTexels - 1 };
				context.Check(IsGIUpdateStatusValid(status, 8, 64, 1),
					"Atlas errors address both irradiance and distance interiors independently of ray count");
				++status.m_Element;
				context.Check(!IsGIUpdateStatusValid(status, 8, 256, 1),
					"An out-of-range atlas error is a corrupt readback");
			}
			for (uint32_t width : { 14u, 30u })
			{
				status = { GIUpdateError::InvalidHistory, 0, 0,
					width * width + gi_shader::GIProbeDistanceInteriorTexels - 1 };
				context.Check(IsGIUpdateStatusValid(status, 8, 64, 1, width) &&
					!IsGIUpdateStatusValid(status, 8, 64, 1),
					"Numerical verdicts use this owner's angular profile, not the default atlas size");
				++status.m_Element;
				context.Check(!IsGIUpdateStatusValid(status, 8, 64, 1, width),
					"The first texel beyond a higher-resolution atlas is still a corrupt readback");
			}
			status = { static_cast<GIUpdateError>(UINT32_MAX), 0, 0, 0 };
			context.Check(!IsGIUpdateStatusValid(status, 8, 256, 1),
				"Unknown GPU status codes never become clean completion");

			GIFieldCreateInfo info;
			context.Check(!ValidateGIFieldCreateInfo(info), "A field cannot prepare without a scene owner");
			info.m_RayScene = std::make_shared<RayScene>();
			context.Check(ValidateGIFieldCreateInfo(info), "The Coastal full-update field profile is valid");
			for (uint32_t width : { 6u, 14u, 30u })
			{
				auto angular = info;
				angular.m_IrradianceInteriorWidth = width;
				context.Check(ValidateGIFieldCreateInfo(angular),
					"Explicit power-of-two bordered irradiance profiles are admitted");
			}
			for (uint32_t width : { 0u, 5u, 7u, 8u, 13u, 15u, 16u, 31u, UINT32_MAX })
			{
				auto angular = info;
				angular.m_IrradianceInteriorWidth = width;
				context.Check(!ValidateGIFieldCreateInfo(angular),
					"Unsupported angular profiles reject before dispatch or allocation");
			}
			for (float bias : { 0.0f, 0.1f, 0.3f, 1.0f })
			{
				auto biased = info;
				biased.m_ViewBias = bias;
				context.Check(ValidateGIFieldCreateInfo(biased),
					"Bounded viewer offsets are explicit static-field profile controls");
			}
			for (float bias : { -0.1f, 1.1f, std::numeric_limits<float>::infinity(),
				std::numeric_limits<float>::quiet_NaN() })
			{
				auto biased = info;
				biased.m_ViewBias = bias;
				context.Check(!ValidateGIFieldCreateInfo(biased),
					"Nonfinite or unbounded viewer offsets reject before allocation");
			}
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
			GIProbeVisualizationSettings display{
				.m_Radius = std::numeric_limits<float>::quiet_NaN(), .m_Budget = UINT32_MAX,
				.m_SliceAxis = 7, .m_LayerMinimum = 5, .m_LayerMaximum = 3,
				.m_ClassificationMask = UINT32_MAX, .m_MaximumDistance = -1 };
			display = ResolveGIProbeVisualizationSettings(display);
			context.Check(!display.m_Enabled && display.m_Radius == 0.12f &&
				display.m_Budget == GIProbeVisualizationMaximumBudget && display.m_SliceAxis == 3 &&
				display.m_ClassificationMask == 15 && display.m_MaximumDistance == 0,
				"Probe display defaults off and resolves finite, bounded diagnostic settings");
			context.Check(display.m_LayerMinimum > display.m_LayerMaximum,
				"An inverted inclusive slice remains empty instead of silently exchanging its endpoints");
			display.m_DepthMode = static_cast<GIProbeDepthMode>(255);
			display.m_Mode = static_cast<GIProbeVisualizationMode>(255);
			display.m_PreviewExposureEV = std::numeric_limits<float>::infinity();
			display = ResolveGIProbeVisualizationSettings(display);
			context.Check(display.m_Mode == GIProbeVisualizationMode::Classification && display.m_PreviewExposureEV == 0 &&
				display.m_DepthMode == GIProbeDepthMode::XRay,
				"Unknown modes and nonfinite sphere exposure resolve without contaminating scene settings");
			for (float ev : { -100.0f, 100.0f })
			{
				display.m_Mode = GIProbeVisualizationMode::Irradiance;
				display.m_PreviewExposureEV = ev;
				display = ResolveGIProbeVisualizationSettings(display);
				context.Check(display.m_Mode == GIProbeVisualizationMode::Irradiance &&
					display.m_PreviewExposureEV == (ev < 0 ? -20.0f : 8.0f),
					"Irradiance sphere exposure uses the independent GI preview range");
			}
			display.m_Selection = GIProbeSelection{ { 2, 3, 4 }, uint64_t(UINT32_MAX) + 10 };
			display.m_Enabled = true;
			display.m_ClassificationMask = 0;
			display.m_LayerMinimum = 10;
			display.m_LayerMaximum = 0;
			display.m_MaximumDistance = 0.01f;
			const auto generation = display.m_Selection->m_ExpectedGeneration;
			context.Check(ResolveGIProbeHighlightState(display, generation, { 5, 5, 5 }, true, true) ==
				GIProbeHighlightState::Ready && MatchesGIProbeSelection(*display.m_Selection, generation, { 5, 5, 5 }),
				"A 64-bit field-bound highlight ignores ordinary class, slice, distance and budget filters");
			context.Check(ResolveGIProbeHighlightState(display, generation + 1, { 5, 5, 5 }, true, true) ==
				GIProbeHighlightState::FieldMismatch &&
				ResolveGIProbeHighlightState(display, generation, { 2, 5, 5 }, true, true) == GIProbeHighlightState::OutOfRange,
				"Field replacement and invalid coordinates cannot silently rebind a selected highlight");
			context.Check(ResolveGIProbeHighlightState(display, generation, { 5, 5, 5 }, true, false) ==
				GIProbeHighlightState::Unavailable,
				"An unavailable highlight pipeline does not claim a submitted diagnostic probe");
			display.m_Enabled = false;
			context.Check(ResolveGIProbeHighlightState(display, generation, { 5, 5, 5 }, true, true) ==
				GIProbeHighlightState::Disabled, "Disabling the viewer suppresses its independent highlight");
			display.m_Selection.reset();
			context.Check(ResolveGIProbeHighlightState(display, generation, { 5, 5, 5 }, true, true) ==
				GIProbeHighlightState::None, "Cleared selection contains no latent default coordinate");
			GIProbeVisualizationViewSnapshot displayView{ .m_Revision = 1,
				.m_RenderExtent = { 640, 400 }, .m_DisplayExtent = { 640, 400 },
				.m_ProjectionSupported = true, .m_Valid = true };
			context.Check(IsGIProbeVisualizationViewSupported(displayView),
				"Classification display does not require receiver diagnostic attachments");
			const auto logical = displayView;
			displayView.m_TemporalAA = true;
			displayView = AdvanceGIProbeVisualizationView(logical, displayView);
			context.Check(IsGIProbeVisualizationViewSupported(displayView) && displayView.m_Revision == 2,
				"Temporal enable changes logical view while supporting the display");
			auto nextPhase = displayView;
			nextPhase.m_TemporalFrameIndex = 5;
			nextPhase.m_JitterPixels = { 0.25f, -0.125f };
			nextPhase.m_Projection[8] = 0.25f;
			nextPhase.m_ViewProjection[8] = 0.25f;
			nextPhase = AdvanceGIProbeVisualizationView(displayView, nextPhase);
			context.Check(nextPhase.m_Revision == displayView.m_Revision &&
				nextPhase.m_TemporalFrameIndex == 5 && nextPhase.m_JitterPixels[0] == 0.25f,
				"Jitter phase retains logical revision and its own execution matrices and jitter");
			nextPhase.m_UnjitteredProjection[0] = 2;
			nextPhase = AdvanceGIProbeVisualizationView(displayView, nextPhase);
			context.Check(nextPhase.m_Revision == 3, "A camera projection change advances visualization revision");
			displayView.m_ResolutionPreset = TemporalAAResolutionPreset::Quality;
			displayView.m_RenderExtent = { 427, 267 };
			context.Check(IsGIProbeVisualizationViewSupported(displayView) &&
				GetGIProbeVisualizationDepthSourceName(displayView) == "DisplayView.DisplayDepth",
				"Quality uses the existing display-domain temporal depth");
			displayView.m_ProjectionSupported = false;
			context.Check(!IsGIProbeVisualizationViewSupported(displayView),
				"An unsupported jitter owner does not silently reinterpret temporal depth");
			displayView.m_ProjectionSupported = true;
			displayView.m_TemporalAA = false;
			context.Check(!IsGIProbeVisualizationViewSupported(displayView), "Upscaling requires its temporal resolve");
			GIFieldSummary countsSummary;
			countsSummary.m_Field.m_Generation = 4;
			countsSummary.m_ReceiverView.m_Revision = 99;
			auto& countsDisplay = countsSummary.m_Visualization;
			countsDisplay.m_View = logical;
			countsDisplay.m_View.m_Revision = 7;
			countsDisplay.m_View.m_TemporalAA = true;
			countsDisplay.m_CompletedView = countsDisplay.m_View;
			countsDisplay.m_CompletedSettings = countsDisplay.m_Settings;
			countsDisplay.m_CompletedObservation = GIObservationStamp{ .m_Field = countsSummary.m_Field };
			countsDisplay.m_View.m_TemporalFrameIndex = 5;
			countsDisplay.m_View.m_JitterPixels = { 0.25f, -0.125f };
			context.Check(MatchesGIProbeVisualizationCounts(countsSummary),
				"Completed visualization counts match across jitter phases independently of the receiver clock");
			countsDisplay.m_HighlightCount = 1;
			context.Check(!MatchesGIProbeVisualizationCounts(countsSummary),
				"A highlight availability change makes previous completed counts historical");
			countsDisplay.m_CompletedCounts.m_Highlights = 1;
			context.Check(MatchesGIProbeVisualizationCounts(countsSummary),
				"Completed highlight counts match only the current diagnostic submission count");
			++countsDisplay.m_View.m_Revision;
			context.Check(!MatchesGIProbeVisualizationCounts(countsSummary),
				"A new logical visualization view makes older completed counts historical");
			--countsDisplay.m_View.m_Revision;
			++countsSummary.m_Field.m_Generation;
			context.Check(!MatchesGIProbeVisualizationCounts(countsSummary),
				"Completed visualization counts retain their original field generation");
			GIField first(info);
			GIField replacement(info);
			const auto summary = first.GetCaptureState().m_Summary;
			for (int query = 0; query < 100; ++query)
			{
				context.Check(first.GetCaptureState().m_Summary == summary &&
					!summary->m_CompletedMeasurement && summary->m_SubmittedUpdates == 0,
					"Read-only field summary queries retain the published CPU observation");
			}
			GIProbeInspectionRequest request{ .m_Coordinate = { 1, 2, 3 },
				.m_ExpectedGeneration = summary->m_Field.m_Generation, .m_ExpectedRefreshEpoch = 1 };
			context.Check(MatchesGIProbeInspection(request, *summary) &&
				!MatchesGIProbeInspection(request, *replacement.GetCaptureState().m_Summary),
				"A probe coordinate is scoped to its expected field generation");
			request.m_ExpectedRefreshEpoch = 2;
			context.Check(!MatchesGIProbeInspection(request, *summary), "An explicitly stale epoch cannot be inspected");
			request.m_ExpectedRefreshEpoch.reset();
			request.m_Coordinate[0] = summary->m_Counts[0];
			context.Check(!MatchesGIProbeInspection(request, *summary), "The first coordinate outside a volume rejects");
			request.m_Coordinate[0] = 1;
			for (uint32_t interior : { 6u, 14u, 30u })
			{
				const uint32_t width = interior + 2;
				std::vector<uint8_t> bytes((1 + width * width + 16 * 16) * 16);
				const std::array<uint32_t, 4> header{ GIProbeInspectionMagic, 1, 0, interior };
				std::memcpy(bytes.data(), header.data(), sizeof(header));
				const std::array<float, 4> irradiance{ 3.1415927f, 2, 1, 0 };
				std::memcpy(bytes.data() + 16, irradiance.data(), sizeof(irradiance));
				GIObservationStamp stamp{ .m_Field = { .m_Generation = request.m_ExpectedGeneration,
					.m_RefreshEpoch = 2, .m_EpochOriginUpdate = 81 },
					.m_Kind = GIObservationKind::ProbeAtlas, .m_ProducingFrame = 913,
					.m_FieldUpdate = 85, .m_ReadinessUpdate = 4 };
				const auto decoded = DecodeGIProbeInspection(request, stamp, { 2, 3, 4 }, bytes);
				context.Check(decoded->m_Validity == GIProbeInspectionValidity::Valid &&
					decoded->m_Irradiance.size() == width * width && decoded->m_Distance.size() == 256 &&
					decoded->m_Irradiance[0] == irradiance && decoded->m_Observation.m_ProducingFrame == 913 &&
					decoded->m_Observation.m_FieldUpdate - decoded->m_Observation.m_Field.m_EpochOriginUpdate == 4,
					"Bounded inspection preserves decoded E, tile layout and distinct frame/update/epoch identities");
				auto staleRequest = request;
				staleRequest.m_ExpectedRefreshEpoch = 1;
				context.Check(DecodeGIProbeInspection(staleRequest, stamp, {}, bytes)->m_Validity ==
					GIProbeInspectionValidity::MalformedPayload,
					"A retained old-epoch request cannot relabel a new epoch's completed payload");
				auto badStamp = stamp;
				badStamp.m_ReadinessUpdate = 85;
				context.Check(DecodeGIProbeInspection(request, badStamp, {}, bytes)->m_Validity ==
					GIProbeInspectionValidity::MalformedPayload,
					"Field-lifetime updates cannot be substituted for epoch-relative readiness updates");
				bytes[0] = 0;
				context.Check(DecodeGIProbeInspection(request, stamp, {}, bytes)->m_Validity ==
					GIProbeInspectionValidity::MalformedPayload, "Malformed readback never becomes a valid probe");
				std::memcpy(bytes.data(), header.data(), sizeof(header));
				const float invalidValue = std::numeric_limits<float>::quiet_NaN();
				std::memcpy(bytes.data() + 16, &invalidValue, sizeof(invalidValue));
				context.Check(DecodeGIProbeInspection(request, stamp, {}, bytes)->m_Validity ==
					GIProbeInspectionValidity::InvalidNumerics && decoded->m_Irradiance[0] == irradiance,
					"Non-finite evidence is typed invalid; retained CPU results cannot be overwritten by reuse");
			}
			context.Check(first.GetDiagnostics().m_Generation != replacement.GetDiagnostics().m_Generation &&
				!first.GetDiagnostics().m_Initialized, "Replacement fields have distinct identities and no inherited updates");
			RenderScene scene;
			RenderFrameContext frame{ .m_RenderScene = scene };
			context.Check(!first.Prepare(frame, {}).IsReady(), "A field requires the Runtime settlement service");
			std::array<RenderView, 1> displayViews;
			displayViews[0].m_IsValid = true;
			displayViews[0].m_Width = displayViews[0].m_DisplayWidth = 16;
			displayViews[0].m_Height = displayViews[0].m_DisplayHeight = 16;
			ResolvedViewRenderSettings displaySettings;
			displaySettings.m_GIProbes.m_Enabled = true;
			frame.m_RenderViews = displayViews;
			frame.m_ViewRenderSettings = std::span(&displaySettings, 1);
			context.Check(!first.Prepare(frame, {}).IsReady(),
				"A rejected field never prepares viewer GPU resources even when a valid display requests them");

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
			info.m_QueryWorkPerSubmission = GIObservationLimits::MaximumQueryWorkPerSubmission;
			info.m_ReceiverQueryWorkPerSubmission = gi_shader::GIReceiverMaximumQueries - 1;
			context.Check(!ValidateGIReferenceInputs(info, scene).IsReady(),
				"Primary receiver preparation has independent admission before allocation");
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
			const GIObservationSceneIdentity firstAlias(std::shared_ptr<RayScene>(sharedScenes, &(*sharedScenes)[0]));
			const GIObservationSceneIdentity secondAlias(std::shared_ptr<RayScene>(sharedScenes, &(*sharedScenes)[1]));
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
							GGLAB_UNUSED(observation.GetReceiverCache().Import(builder));
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
			key.m_TextureLodBias = -1.0f;
			GGLAB_UNUSED(observation.Prepare(key, 4, 256));
			context.Check(observation.GetDiagnostics().m_Generation == generation + 4 &&
				observation.GetDiagnostics().m_ReceiverPreparation.m_SubmittedPixels == 0,
				"Receiver raster footprint changes reset the matching cache with the estimator");
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
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 16 &&
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
			context.Check(textures.GetDiagnostics().m_PendingRetirementTextureCount == 16,
				"Cancellation retains the entire sum/RNG pair through its last-use fence");
			textures.Tick();
			context.Check(textures.GetDiagnostics().m_PendingRetirementTextureCount == 16,
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
			info.m_QueryWorkPerSubmission = GIObservationLimits::VisibilityQueriesPerSample;
			info.m_ReceiverQueryWorkPerSubmission = gi_shader::GIReceiverMaximumQueries - 1;
			context.Check(!ValidateGISpecularVisibilityInputs(info, scene).IsReady(),
				"Visibility sampling cannot conceal an insufficient primary matching budget");

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
							GGLAB_UNUSED(observation.GetReceiverCache().Import(builder));
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
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 12 && !observation.Prepare(key, 64, 128),
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
				textures.GetDiagnostics().m_PendingRetirementTextureCount == 12,
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
							GGLAB_UNUSED(observation.GetReceiverCache().Import(builder));
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
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 16 &&
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
				textures.GetDiagnostics().m_PendingRetirementTextureCount == 16,
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
				textures.GetDiagnostics().m_PendingRetirementTextureCount == 16,
				"A material snapshot revision restarts every partial-sweep cursor and retains old storage through its fence");
			record();
			observation.Settle({ RenderFrameOutcomeKind::Discarded, 14, {} });
			device.SetFencesComplete(true);
			observation.Shutdown();
			textures.Tick();
		}

		template <size_t SurfaceCount>
		void RunGIObservationHistoryViewTests(SelfTestContext& context) noexcept
		{
			struct Case
			{
				GIObservationHistoryAccess m_Access;
				bool m_PendingDefined = false;
				bool m_Compiles = true;
			};
			constexpr std::array cases
			{
				Case{ { false, false, false }, false }, // Preparation only.
				Case{ { false, true, true }, false },   // First tile of the first sweep.
				Case{ { false, true, false }, true },   // Later tile before any published estimate.
				Case{ { true, true, true }, false },    // First tile of a new sweep.
				Case{ { true, true, false }, true },    // Later tile with a published estimate.
				Case{ { false, false, false }, true },  // Pending first sweep, no sampling.
				Case{ { true, false, false }, false },  // Completed read-only resolve.
				Case{ { true, false, false }, true },   // Pending later sweep, read-only resolve.
				Case{ { false, false, true }, false },  // Preparation does not initialize pending contents.
				Case{ { false, true, false }, false, false }, // Later tiles require defined pending data.
			};
			asset_test::TestRHIDevice device;
			TransientResourcePool pool(&device);
			for (const auto& test : cases)
			{
				RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
				std::array<typename TemporalHistoryTextures<SurfaceCount>::RenderGraphSurface, SurfaceCount> surfaces;
				std::array<RHIFormat, SurfaceCount> formats{};
				GIObservationHistoryViews<SurfaceCount> views;
				bool versionsMatch = true;
				bool fullWritesMatch = true;
				struct Empty {};
				graph.AddPass<Empty>("ObservationHistoryViews", RGPassEncoderType::Compute,
					[&](RenderGraph::RGBuilder& builder, Empty&)
					{
						for (size_t index = 0; index < surfaces.size(); ++index)
						{
							formats[index] = SurfaceCount == GIReferenceObservation::SurfaceCount &&
								index == static_cast<size_t>(GIReferenceObservation::SurfaceSlot::RandomState) ?
									RHIFormat::R32Uint :
									RHIFormat::R32G32B32A32Float;
							const RHITextureDesc desc{ .m_Format = formats[index],
								.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
								.m_Extent = { 8, 8, 1 } };
							surfaces[index].m_Previous = builder.ImportTexture(
								("History.Previous." + std::to_string(index)).c_str(),
								device.CreateTexture({ .m_Desc = desc }, {}), desc, RGTextureAccess::None,
								test.m_Access.m_PreviousValid ? RGContentValidity::Defined : RGContentValidity::Undefined);
							surfaces[index].m_Next = builder.ImportTexture(
								("History.Next." + std::to_string(index)).c_str(),
								device.CreateTexture({ .m_Desc = desc }, {}), desc, RGTextureAccess::None,
								test.m_PendingDefined ? RGContentValidity::Defined : RGContentValidity::Undefined);
						}
						// The first Write can reuse an unread import's version; ReadWrite creates a new one.
						const auto imported = surfaces;
						views = DeclareGIObservationHistoryViews(builder, surfaces, test.m_Access);
						for (size_t index = 0; index < surfaces.size(); ++index)
						{
							const auto nextVersion = imported[index].m_Next.GetVersion() +
								(test.m_Access.m_Writing && !test.m_Access.m_BeginSweep ? 1 : 0);
							versionsMatch &= surfaces[index].m_Previous == imported[index].m_Previous &&
								surfaces[index].m_Next.GetHandle() == imported[index].m_Next.GetHandle() &&
								surfaces[index].m_Next.GetVersion() == nextVersion;
							fullWritesMatch &= TemporalHistoryTextures<SurfaceCount>::IsFullyWritten(builder,
								surfaces[index]) == test.m_Access.m_Writing;
							if (test.m_Access.m_PreviousValid)
							{
								builder.Export(surfaces[index].m_Previous, RGTextureAccess::None);
							}
							if (test.m_Access.m_Writing || test.m_PendingDefined)
							{
								builder.Export(surfaces[index].m_Next, RGTextureAccess::None);
							}
						}
						builder.SideEffect();
					});
				context.Check(versionsMatch && fullWritesMatch,
					"History recipes update only written next versions and satisfy the owner's full-write gate");
				const bool compiled = graph.Compile();
				context.Check(compiled == test.m_Compiles,
					"History preparation, sampling and resolve compile; undefined continuation remains rejected");
				if (!compiled || !test.m_Compiles)
				{
					continue;
				}
				const auto& plan = *graph.GetExecutionPlan();
				const auto& pass = plan.GetPasses().front();
				const size_t expected = SurfaceCount *
					(static_cast<size_t>(test.m_Access.m_PreviousValid) +
						static_cast<size_t>(test.m_Access.m_Writing));
				bool accessesMatch = pass.m_Accesses.size() == expected;
				for (const auto& access : pass.m_Accesses)
				{
					const bool previous = access.m_Resource.Value() % 2 == 0;
					const auto dependency = previous ?
						RGDependencyAccess::Read :
						test.m_Access.m_BeginSweep ?
							RGDependencyAccess::Write :
							RGDependencyAccess::ReadWrite;
					const auto usage = previous ?
						RGTextureAccess::Sample :
						test.m_Access.m_BeginSweep ?
							RGTextureAccess::StorageWrite :
							RGTextureAccess::StorageReadWrite;
					accessesMatch &= access.m_DependencyAccess == dependency &&
						access.m_AccessValue == static_cast<uint64_t>(usage) &&
						access.m_Stages == RHIStage::ComputeShader &&
						(previous ? test.m_Access.m_PreviousValid : test.m_Access.m_Writing);
					const auto& resource = plan.GetResources()[access.m_Resource.Value()];
					accessesMatch &= resource.m_FinalState == CommonRHIResourceState();
				}
				context.Check(accessesMatch,
					"History accesses preserve sampled reads, first-tile Write, later ReadWrite and final common states");
				const auto matchesView = [&](RGTextureViewId id, bool present, RGTextureId resource,
					RHITextureViewType type, RHIFormat format) noexcept
					{
						if (!present)
						{
							return !id.IsValid();
						}
						if (!id.IsValid() || id.Value() >= plan.GetTextureViews().size())
						{
							return false;
						}
						const auto& view = plan.GetTextureViews()[id.Value()];
						// Inherit each surface's format, including the reference's integer RNG surface.
						return view.m_Resource.Value() == resource.GetHandle().Value() &&
							view.m_Desc.m_Type == type && view.m_Desc.m_Format == RHIFormat::Unknown &&
							graph.GetTextureDesc(resource).m_Format == format;
					};
				bool viewsMatch = plan.GetTextureViews().size() == expected;
				for (size_t index = 0; index < surfaces.size(); ++index)
				{
					viewsMatch &= matchesView(views.m_Previous[index], test.m_Access.m_PreviousValid,
						surfaces[index].m_Previous, RHITextureViewType::ShaderResource, formats[index]);
					viewsMatch &= matchesView(views.m_Next[index], test.m_Access.m_Writing,
						surfaces[index].m_Next, RHITextureViewType::UnorderedAccess, formats[index]);
				}
				context.Check(viewsMatch,
					"History views preserve every surface's resource, format and SRV/UAV role without unused views");
			}
		}

		void RunGITiledObservationTests(SelfTestContext& context) noexcept
		{
			asset_test::TestRHIDevice device;
			PersistentTexturePool textures(&device);
			TransientResourcePool transients(&device);
			const RHIFencePoint fence{ RHIFenceHandle(0, 1), 1 };
			const auto record = [&](auto& observation, bool markRecorded = true) noexcept
				{
					RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &transients });
					struct Data {};
					graph.AddPass<Data>("TiledObservationContract", RGPassEncoderType::Compute,
						[&](RenderGraph::RGBuilder& builder, Data&)
						{
							GGLAB_UNUSED(observation.GetReceiverCache().Import(builder));
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
					context.Check(graph.Compile(), "Tiled observation exports retain defined history states");
					if (markRecorded)
					{
						observation.MarkRecorded();
					}
				};
			const auto checkObservation = [&](auto& observation, auto key, auto prepare,
				uint32_t textureCount) noexcept
				{
					using Status = decltype(observation.GetDiagnostics().m_Status);
					observation.BeginFrame(1);
					context.Check(prepare(key, 47, 0) && observation.GetTile().m_Width == 19 &&
						observation.GetTile().m_Height == 2 && observation.GetDiagnostics().m_TileCount == 6,
						"Normalized non-divisible query work preserves full-width tiles and edge rows");
					record(observation, false);
					observation.Settle({ RenderFrameOutcomeKind::Submitted, 1, fence });
					context.Check(observation.GetDiagnostics().m_SubmittedTiles == 0 &&
						observation.GetDiagnostics().m_ReceiverPreparation.m_SubmittedPixels == 0,
						"Exports alone cannot publish history or receiver preparation");
					observation.BeginFrame(2);
					GGLAB_UNUSED(prepare(key, 47, 0));
					observation.MarkRecorded();
					observation.Settle({ RenderFrameOutcomeKind::Submitted, 2, fence });
					context.Check(observation.GetDiagnostics().m_SubmittedTiles == 0 &&
						observation.GetDiagnostics().m_ReceiverPreparation.m_SubmittedPixels == 0,
						"Recording alone cannot publish unexported history or receiver preparation");
					observation.BeginFrame(3);
					GGLAB_UNUSED(prepare(key, 47, 0));
					record(observation);
					observation.Settle({ RenderFrameOutcomeKind::Submitted, 3, fence });
					observation.BeginFrame(4);
					const auto generation = observation.GetDiagnostics().m_Generation;
					context.Check(!prepare(key, 5, 1) && observation.GetDiagnostics().m_BatchSamples == 0 &&
						observation.GetDiagnostics().m_SubmittedTiles == 1 &&
						observation.GetDiagnostics().m_Generation == generation,
						"A reduced execution budget rejects an active tile without resetting its sweep");
					context.Check(prepare(key, 47, 1) && observation.GetTile().m_X == 0 &&
						observation.GetTile().m_Y == 2 && observation.GetTile().m_Width == 19 &&
						observation.GetTile().m_Height == 2 && observation.GetDiagnostics().m_TileCount == 6,
						"Restoring work resumes the same tile; new partition preferences wait");
					record(observation);
					observation.Settle({ RenderFrameOutcomeKind::Submitted, 4, fence });
					for (uint64_t serial = 5; serial <= 8; ++serial)
					{
						observation.BeginFrame(serial);
						GGLAB_UNUSED(prepare(key, 47, 1));
						record(observation);
						observation.Settle({ RenderFrameOutcomeKind::Submitted, serial, fence });
					}
					observation.BeginFrame(9);
					context.Check(prepare(key, 47, 1) && observation.GetDiagnostics().m_CompletedSamples == 1 &&
						observation.GetDiagnostics().m_CommittedVersion == 1 &&
						observation.GetDiagnostics().m_Generation == generation &&
						observation.GetDiagnostics().m_TileCount == 209 &&
						observation.GetTile().m_Width == 1 && observation.GetTile().m_Height == 1,
						"Only a full sweep publishes history and adopts new partition preferences");
					observation.Cancel();
					textures.Tick();
					key.m_Samples = 1;
					observation.BeginFrame(10);
					GGLAB_UNUSED(prepare(key, 1000, 0));
					record(observation);
					observation.Settle({ RenderFrameOutcomeKind::Submitted, 10, fence });
					observation.BeginFrame(11);
					context.Check(prepare(key, 1000, 0) &&
						observation.GetDiagnostics().m_Status == Status::Completed &&
						observation.GetDiagnostics().m_BatchSamples == 0,
						"Completed status projects without scheduling further sample work");
					record(observation);
					device.SetFencesComplete(false);
					observation.Settle({ RenderFrameOutcomeKind::Failed, 11, { fence.m_Fence, 2 } });
					context.Check(observation.GetDiagnostics().m_Status == Status::Failed &&
						observation.GetDiagnostics().m_CommittedVersion == 1,
						"A failed read-only resolve cannot publish a new history version");
					observation.Cancel();
					textures.Tick();
					context.Check(textures.GetDiagnostics().m_PendingRetirementTextureCount == textureCount,
						"Failed read-only resolves retain every history and receiver surface through last use");
					device.SetFencesComplete(true);
					observation.Shutdown();
					textures.Tick();
				};
			GIReferenceObservation reference(device, textures);
			checkObservation(reference, GIReferenceObservationKey{ .m_Width = 19, .m_Height = 11,
				.m_Samples = 3, .m_MaximumDepth = 3 },
				[&](const auto& key, uint32_t work, uint32_t tile)
				{
					return reference.Prepare(key, 3, work, work * 6 + 5, tile);
				}, 16);
			GISpecularVisibilityObservation visibility(device, textures);
			checkObservation(visibility, GISpecularVisibilityKey{ .m_Width = 19, .m_Height = 11, .m_Samples = 3 },
				[&](const auto& key, uint32_t work, uint32_t tile)
				{
					return visibility.Prepare(key, 3, work * 2 + 1, tile);
				}, 12);

			reference.BeginFrame(12);
			visibility.BeginFrame(12);
			GGLAB_UNUSED(reference.Prepare({ .m_Width = 8, .m_Height = 8, .m_Samples = 1 },
				1, 64, 128, 0, gi_shader::GIReceiverMaximumQueries));
			GGLAB_UNUSED(visibility.Prepare({ .m_Width = 8, .m_Height = 8, .m_Samples = 1 }, 1, 128));
			record(reference);
			record(visibility);
			device.SetFencesComplete(false);
			reference.Settle({ RenderFrameOutcomeKind::CompletionUnknown, 12, {} });
			visibility.Settle({ RenderFrameOutcomeKind::Submitted, 12, fence });
			reference.Cancel();
			device.SetFencesComplete(true);
			visibility.BeginFrame(13);
			GGLAB_UNUSED(visibility.Prepare({ .m_Width = 8, .m_Height = 8, .m_Samples = 1 }, 1, 128));
			context.Check(reference.GetDiagnostics().m_Status == GIReferenceObservationStatus::Failed &&
				reference.GetDiagnostics().m_ReceiverPreparation.m_SubmittedPixels == 0 &&
				visibility.GetDiagnostics().m_Status == GISpecularVisibilityStatus::Completed &&
				visibility.GetDiagnostics().m_CompletedSamples == 1 &&
				visibility.GetDiagnostics().m_ReceiverPreparation.m_CompletedPixels == 64,
				"Shared execution code keeps poisoning, receiver preparation and completion observation-local");
			reference.Shutdown();
			visibility.Shutdown();
			textures.Tick();
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 0 &&
				textures.GetDiagnostics().m_PendingRetirementTextureCount == 0,
				"Independent checked teardown releases every shared-core instance");
		}

		void RunGIReceiverCacheTests(SelfTestContext& context) noexcept
		{
			asset_test::TestRHIDevice device;
			device.SetFencesComplete(false);
			PersistentTexturePool textures(&device);
			TransientResourcePool transients(&device);
			GIReceiverCache cache(device, textures, { "Cache.PositionError", "Cache.FaceMatch",
				"Cache.Instance", "Cache.Primitive" });
			constexpr uint32_t queries = gi_shader::GIReceiverMaximumQueries;
			const RHIFencePoint fence{ RHIFenceHandle(0, 1), 1 };
			const auto record = [&]() noexcept
				{
					RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &transients });
					struct Data {};
					graph.AddPass<Data>("PrepareOrReadCache", RGPassEncoderType::Compute,
						[&](RenderGraph::RGBuilder& builder, Data&)
						{
							const auto views = cache.Import(builder);
							context.Check(std::ranges::all_of(views, [](auto view) { return view.IsValid(); }),
								"Receiver preparation and cache reads declare all four surfaces");
							builder.SideEffect();
						});
					context.Check(graph.Compile(), "Partial receiver preparation preserves graph-defined contents");
					cache.MarkRecorded();
				};
			cache.BeginFrame(1);
			context.Check(!cache.Prepare(8, 4, queries - 1) && textures.GetDiagnostics().m_ActiveTextureCount == 0,
				"Even one receiver must fit every possible matching attempt before allocation");
			context.Check(cache.Prepare(8, 4, queries * 13 + queries - 1) &&
				cache.GetProgress().m_BatchPixels == 13 && !cache.CanSample() &&
				cache.GetProgress().m_BatchQueryWork == queries * 13 && cache.GetProgress().m_CacheBytes == 32 * 40,
				"Cold matching has a separate conservative query cap and 40-byte-per-pixel storage");
			record();
			cache.Settle({ RenderFrameOutcomeKind::Discarded, 1, {} });
			context.Check(cache.GetProgress().m_SubmittedPixels == 0,
				"Discarded preparation cannot validate or consume any receiver pixels");
			cache.BeginFrame(2);
			GGLAB_UNUSED(cache.Prepare(8, 4, queries * 13));
			record();
			cache.Settle({ RenderFrameOutcomeKind::Submitted, 2, fence });
			cache.Settle({ RenderFrameOutcomeKind::Submitted, 2, fence });
			context.Check(cache.GetProgress().m_SubmittedPixels == 13 && cache.GetProgress().m_CompletedPixels == 0,
				"Receiver preparation settles once and distinguishes submission from completion");
			cache.BeginFrame(3);
			GGLAB_UNUSED(cache.Prepare(8, 4, queries * 13));
			context.Check(cache.GetProgress().m_BatchPixels == 0 && !cache.CanSample(),
				"A pending preparation fence prevents overlapping writes and premature sampling");
			cache.Settle({ RenderFrameOutcomeKind::Discarded, 3, {} });
			device.SetFencesComplete(true);
			cache.BeginFrame(4);
			GGLAB_UNUSED(cache.Prepare(8, 4, queries * 13));
			context.Check(cache.GetBeginPixel() == 13 && cache.GetProgress().m_CompletedPixels == 13,
				"Completed preparation resumes at the exact next pixel");
			record();
			cache.Settle({ RenderFrameOutcomeKind::Submitted, 4, fence });
			cache.BeginFrame(5);
			GGLAB_UNUSED(cache.Prepare(8, 4, queries * 13));
			context.Check(cache.GetBeginPixel() == 26 && cache.GetProgress().m_BatchPixels == 6,
				"The final receiver batch includes the non-divisible image remainder");
			record();
			device.SetFencesComplete(false);
			cache.Settle({ RenderFrameOutcomeKind::Submitted, 5, fence });
			cache.BeginFrame(6);
			GGLAB_UNUSED(cache.Prepare(8, 4, queries));
			context.Check(cache.GetProgress().m_Ready && !cache.CanSample() &&
				cache.GetProgress().m_BatchQueryWork == 0 && cache.GetProgress().m_CompletedPixels == 26,
				"Submitted complete data may resolve through queue ordering but sampling waits for its fence");
			record();
			cache.Settle({ RenderFrameOutcomeKind::Submitted, 6, fence });
			device.SetFencesComplete(true);
			cache.BeginFrame(7);
			GGLAB_UNUSED(cache.Prepare(8, 4, queries * 32));
			context.Check(cache.CanSample() && cache.GetProgress().m_CompletedPixels == 32 &&
				cache.GetProgress().m_BatchQueryWork == 0 && textures.GetDiagnostics().m_TotalAcquireCount == 4,
				"Changing execution budgets cannot invalidate a ready cache or repeat its expensive queries");
			record();
			device.SetFencesComplete(false);
			cache.Settle({ RenderFrameOutcomeKind::Failed, 7, fence });
			cache.Release();
			context.Check(textures.GetDiagnostics().m_PendingRetirementTextureCount == 4,
				"A failed cached read still retains all resources through its real last-use fence");
			cache.BeginFrame(8);
			GGLAB_UNUSED(cache.Prepare(8, 4, queries * 32));
			record();
			cache.Settle({ RenderFrameOutcomeKind::CompletionUnknown, 8, {} });
			cache.Release();
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 4 && !cache.Prepare(8, 4, queries * 32),
				"Unknown completion retains receiver storage and forbids ordinary reuse");
			device.SetFencesComplete(true);
			cache.Shutdown();
			textures.Tick();

			const auto checkObservation = [&]<typename Observation, typename Key>
				(Observation& observation, Key key, auto prepare) noexcept
				{
					const auto recordObservation = [&]() noexcept
						{
							RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &transients });
							struct Data {};
							graph.AddPass<Data>("ColdObservation", RGPassEncoderType::Compute,
								[&](RenderGraph::RGBuilder& builder, Data&)
								{
									GGLAB_UNUSED(observation.GetReceiverCache().Import(builder));
									auto surfaces = observation.Import(builder);
									for (auto& surface : surfaces)
									{
										if (observation.GetDiagnostics().m_BatchSamples != 0)
										{
											builder.WriteInPlace(surface.m_Next, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
										}
									}
									observation.Export(builder, surfaces);
									builder.SideEffect();
								});
							context.Check(graph.Compile(), "Cold observations declare matching without undefined sum reads");
							observation.MarkRecorded();
						};
					for (uint64_t serial = 1; serial <= 2; ++serial)
					{
						observation.BeginFrame(serial);
						context.Check(prepare(key) && observation.GetDiagnostics().m_BatchSamples == 0,
							"Multi-frame receiver preparation cannot consume sample or tile indices");
						recordObservation();
						observation.Settle({ RenderFrameOutcomeKind::Submitted, serial, fence });
					}
					observation.BeginFrame(3);
					context.Check(prepare(key) && observation.GetDiagnostics().m_BatchSamples != 0 &&
						observation.GetDiagnostics().m_ReceiverPreparation.m_BatchQueryWork == 0 &&
						observation.GetDiagnostics().m_SubmittedSamples == 0 && observation.GetTile().m_BeginSweep,
						"The original sample sequence starts only after complete receiver preparation");
					recordObservation();
					observation.Settle({ RenderFrameOutcomeKind::Submitted, 3, fence });
					observation.BeginFrame(4);
					++key.m_PositionShaderGeneration;
					context.Check(prepare(key) && observation.GetDiagnostics().m_BatchSamples == 0 &&
						observation.GetDiagnostics().m_ReceiverPreparation.m_SubmittedPixels == 0 &&
						observation.GetDiagnostics().m_CommittedVersion == 0,
						"Matching input replacement invalidates the cache and every accumulated sample together");
					observation.Settle({ RenderFrameOutcomeKind::Discarded, 4, {} });
					observation.Shutdown();
					textures.Tick();
				};
			GIReferenceObservation reference(device, textures);
			checkObservation(reference, GIReferenceObservationKey{ .m_Width = 16, .m_Height = 8, .m_Samples = 2 },
				[&](const auto& key) { return reference.Prepare(key, 1, 128, 256, 0, queries * 64); });
			GISpecularVisibilityObservation visibility(device, textures);
			checkObservation(visibility, GISpecularVisibilityKey{ .m_Width = 16, .m_Height = 8, .m_Samples = 2 },
				[&](const auto& key) { return visibility.Prepare(key, 1, 256, 0, queries * 64); });
			context.Check(textures.GetDiagnostics().m_ActiveTextureCount == 0 &&
				textures.GetDiagnostics().m_PendingRetirementTextureCount == 0,
				"Receiver preparation teardown releases every cache and observation allocation");
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
		RunGIQueryRecordingTests(context);
		RunRaySceneQueryTests(context);
		RunProbeUpdateProfileTests(context);
		RunGIFieldTests(context);
		RunGIReceiverObservationTests(context);
		RunGIReferenceObservationTests(context);
		RunSpecularVisibilityTests(context);
		RunGIReferenceTilingTests(context);
		RunGIObservationHistoryViewTests<GIReferenceObservation::SurfaceCount>(context);
		RunGIObservationHistoryViewTests<GISpecularVisibilityObservation::SurfaceCount>(context);
		RunGITiledObservationTests(context);
		RunGIReceiverCacheTests(context);
		RunTriangleTests(context);
		RunInstanceTests(context);
		RunScratchTests(context);
		RunBindingTests(context);
	}
}
