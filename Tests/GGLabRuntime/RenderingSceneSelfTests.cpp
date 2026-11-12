#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResourceUtils.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraphTypes.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineSceneExtensionBase.h"
#include "GGLabRuntime/Graphics/RenderScene.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

namespace gglab::rendering_test
{
	struct OpaqueSceneExtensionFixturePassData
	{
		RGBufferId m_ScenePhaseBuffer;
	};

	struct OverlayExtensionFixturePassData
	{
	};

	class RecordingOpaqueSceneExtension final : public RenderPipelineSceneExtensionBase
	{
	public:
		RecordingOpaqueSceneExtension(RGBufferId& scenePhaseBuffer, uint32_t& addPassCount, uint32_t& destructionCount) noexcept :
			m_ScenePhaseBuffer(scenePhaseBuffer), m_AddPassCount(addPassCount),
			m_DestructionCount(destructionCount)
		{
		}
		~RecordingOpaqueSceneExtension() override { ++m_DestructionCount; }

		void AddOpaqueScenePasses(RenderGraph& renderGraph, const RenderFrameContext&, const RenderServices&) noexcept override
		{
			++m_AddPassCount;
			renderGraph.AddPass<OpaqueSceneExtensionFixturePassData>(
				"RenderingContract.OpaqueSceneExtension",
				[this](RenderGraph::RGBuilder& builder, OpaqueSceneExtensionFixturePassData& data)
				{
					builder.ReadWriteInPlace(m_ScenePhaseBuffer, RGBufferAccess::StorageReadWrite);
					data.m_ScenePhaseBuffer = m_ScenePhaseBuffer;
				});
		}

	private:
		RGBufferId& m_ScenePhaseBuffer;
		uint32_t& m_AddPassCount;
		uint32_t& m_DestructionCount;
	};

	class RecordingOverlayExtension final : public RenderPipelineOverlayExtensionBase
	{
	public:
		explicit RecordingOverlayExtension(uint32_t& addPassCount) noexcept :
			m_AddPassCount(addPassCount)
		{
		}

		void AddOverlayPasses(RenderGraph& renderGraph, const RenderFrameContext&, const RenderServices&) noexcept override
		{
			++m_AddPassCount;
			renderGraph.AddPass<OverlayExtensionFixturePassData>(
				"RenderingContract.OverlayExtension",
				[](RenderGraph::RGBuilder& builder, OverlayExtensionFixturePassData&)
				{
					builder.SideEffect();
				});
		}

	private:
		uint32_t& m_AddPassCount;
	};

	void RunOpaqueSceneExtensionContractTests(SelfTestContext& context) noexcept
	{
		static_assert(std::is_abstract_v<RenderPipelineSceneExtensionBase>);
		static_assert(std::has_virtual_destructor_v<RenderPipelineSceneExtensionBase>);
		static_assert(!std::is_copy_constructible_v<RenderPipelineForwardPlus::CreateInfo>);
		static_assert(std::is_nothrow_move_constructible_v< RenderPipelineForwardPlus::CreateInfo>);

		uint32_t addPassCount = 0;
		uint32_t destructionCount = 0;
		RGBufferId scenePhaseBuffer;
		auto extension = std::make_unique<RecordingOpaqueSceneExtension>(scenePhaseBuffer, addPassCount, destructionCount);
		RecordingOpaqueSceneExtension* extensionPtr = extension.get();
		context.Check(extensionPtr->GetTemporalParticipation() == SceneExtensionTemporalParticipation::PostTAA,
			"Scene extensions default to post-TAA participation until they declare a temporal contract");

		RenderGraph graph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		graph.AddPass<OpaqueSceneExtensionFixturePassData>(
			"RenderingContract.BuiltInOpaque",
			[&scenePhaseBuffer](RenderGraph::RGBuilder& builder, OpaqueSceneExtensionFixturePassData& data)
			{
				scenePhaseBuffer = builder.CreateBuffer("OpaqueScenePhaseBuffer");
				builder.WriteInPlace(scenePhaseBuffer, RGBufferAccess::StorageWrite);
				data.m_ScenePhaseBuffer = scenePhaseBuffer;
			});
		graph.AddPass<OpaqueSceneExtensionFixturePassData>(
			"RenderingContract.TemporalAA",
			[&scenePhaseBuffer](RenderGraph::RGBuilder& builder, OpaqueSceneExtensionFixturePassData& data)
			{
				builder.ReadWriteInPlace(scenePhaseBuffer, RGBufferAccess::StorageReadWrite);
				data.m_ScenePhaseBuffer = scenePhaseBuffer;
			});
		const RenderScene renderScene{};
		const RenderFrameContext frameContext{ .m_RenderScene = renderScene };
		const RenderServices services{};
		extensionPtr->AddOpaqueScenePasses(graph, frameContext, services);
		for (const char* passName : { "RenderingContract.ForwardTransparent", "RenderingContract.DebugDrawScene", })
		{
			graph.AddPass<OpaqueSceneExtensionFixturePassData>(passName,
				[&scenePhaseBuffer](RenderGraph::RGBuilder& builder, OpaqueSceneExtensionFixturePassData& data)
				{
					builder.ReadWriteInPlace(scenePhaseBuffer, RGBufferAccess::StorageReadWrite);
					data.m_ScenePhaseBuffer = scenePhaseBuffer;
				});
		}
		graph.AddPass<OpaqueSceneExtensionFixturePassData>(
			"RenderingContract.PostProcess",
			[&scenePhaseBuffer](RenderGraph::RGBuilder& builder, OpaqueSceneExtensionFixturePassData& data)
			{
				data.m_ScenePhaseBuffer = builder.Read(scenePhaseBuffer, RGBufferAccess::StorageRead);
				builder.SideEffect();
			});
		const bool compiled = graph.Compile();
		context.Check(compiled && addPassCount == 1,
			"Opaque scene extensions add their RenderGraph passes through the narrow hook");

		if (compiled)
		{
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			context.Check(snapshot.m_Passes.size() == 6 &&
				snapshot.m_Passes[2].m_Name == "RenderingContract.OpaqueSceneExtension" &&
				!snapshot.m_Passes[2].m_Culled,
				"Opaque scene extension passes remain live in the compiled graph");
			context.Check(snapshot.m_Passes.size() == 6 &&
				snapshot.m_Passes[0].m_ExecutionOrder <
				snapshot.m_Passes[1].m_ExecutionOrder &&
				snapshot.m_Passes[1].m_ExecutionOrder <
				snapshot.m_Passes[2].m_ExecutionOrder &&
				snapshot.m_Passes[2].m_ExecutionOrder <
				snapshot.m_Passes[3].m_ExecutionOrder &&
				snapshot.m_Passes[3].m_ExecutionOrder <
				snapshot.m_Passes[4].m_ExecutionOrder &&
				snapshot.m_Passes[4].m_ExecutionOrder <
				snapshot.m_Passes[5].m_ExecutionOrder,
				"Forward scene composition orders opaque, TAA, post-TAA extension, transparent, "
				"scene debug, and post-process consumers");
		}

		auto debugReadback = std::make_shared<ForwardPlusDebugReadback>();
		const long debugReadbackOwnerCount = debugReadback.use_count();
		{
			RenderPipelineForwardPlus pipeline(RenderPipelineForwardPlus::CreateInfo{
				.m_ForwardPlusDebugReadback = debugReadback,
				.m_SceneExtension = std::move(extension),
			});
			context.Check(extension == nullptr && destructionCount == 0 &&
				debugReadback.use_count() > debugReadbackOwnerCount,
				"The Forward+ pipeline owns the optional extension alongside Forward+ diagnostics");
		}
		context.Check(destructionCount == 1 &&
			debugReadback.use_count() == debugReadbackOwnerCount,
			"The Forward+ pipeline retires optional extension and debug ownership with the pipeline");

		RenderPipelineForwardPlus defaultPipeline;
		context.Check(defaultPipeline.GetName() == "ForwardPlus",
			"The Forward+ pipeline remains source-compatible without an optional extension");
	}

	void RunOverlayExtensionContractTests(SelfTestContext& context) noexcept
	{
		static_assert(std::is_abstract_v<RenderPipelineOverlayExtensionBase>);
		static_assert(std::has_virtual_destructor_v<RenderPipelineOverlayExtensionBase>);
		static_assert(std::is_same_v<decltype(RenderServices::m_OverlayExtension), RenderPipelineOverlayExtensionBase*>);

		uint32_t addPassCount = 0;
		RecordingOverlayExtension extension(addPassCount);
		const RenderServices services{ .m_OverlayExtension = &extension };
		const RenderScene renderScene{};
		const RenderFrameContext frameContext{ .m_RenderScene = renderScene };
		RenderGraph graph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		services.m_OverlayExtension->AddOverlayPasses(graph, frameContext, services);

		const bool compiled = graph.Compile();
		context.Check(compiled && addPassCount == 1,
			"Overlay extensions add their RenderGraph passes through the runtime-owned hook");
		if (compiled)
		{
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			context.Check(snapshot.m_Passes.size() == 1 &&
				snapshot.m_Passes.front().m_Name == "RenderingContract.OverlayExtension" &&
				!snapshot.m_Passes.front().m_Culled,
				"Application-owned overlay passes remain live without a concrete DevTools service");
		}
	}

	void RunNapaVoxelRenderGraphContractTests(SelfTestContext& context) noexcept
	{
		struct FixturePassData
		{
			RGTextureId m_Color{};
			RGTextureId m_Depth{};
			RGBufferId m_Vertex{};
			RGBufferId m_Index{};
		};

		RecordingDevice device;
		RenderGraph graph({
			.m_Device = &device,
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RGTextureId sceneColor{};
		RGTextureId sceneDepth{};
		graph.AddPass<FixturePassData>("RenderingContract.BuiltInOpaque",
			[&sceneColor, &sceneDepth](RenderGraph::RGBuilder& builder, FixturePassData& data)
			{
				const RHITextureDesc colorDesc{
					.m_Format = RHIFormat::R16G16B16A16Float,
					.m_Extent = { 1280, 720, 1 },
				};
				const RHITextureDesc depthDesc{
					.m_Format = RHIFormat::D32Float,
					.m_Extent = { 1280, 720, 1 },
				};
				sceneColor = builder.CreateTexture("NapaVoxel.SceneColor", colorDesc);
				sceneDepth = builder.CreateTexture("NapaVoxel.SceneDepth", depthDesc);
				builder.WriteInPlace(sceneColor, RGTextureAccess::RenderTarget);
				builder.WriteInPlace(sceneDepth, RGTextureAccess::DepthStencilWrite);
				data.m_Color = sceneColor;
				data.m_Depth = sceneDepth;
			});
		graph.AddPass<FixturePassData>("RenderingContract.NapaVoxel",
			[&sceneColor, &sceneDepth](RenderGraph::RGBuilder& builder, FixturePassData& data)
			{
				builder.ReadWriteInPlace(sceneColor, RGTextureAccess::RenderTarget);
				builder.ReadWriteInPlace(sceneDepth, RGTextureAccess::DepthStencilWrite);
				data.m_Color = sceneColor;
				data.m_Depth = sceneDepth;
				const RHIBufferDesc vertexDesc{
					.m_SizeInBytes = 3 * 6 * sizeof(float),
					.m_StrideInBytes = 6 * sizeof(float),
					.m_Usage = RHIBufferUsage::Vertex | RHIBufferUsage::CopyDest,
				};
				const RHIBufferDesc indexDesc{
					.m_SizeInBytes = 3 * sizeof(uint32_t),
					.m_StrideInBytes = sizeof(uint32_t),
					.m_Usage = RHIBufferUsage::Index | RHIBufferUsage::CopyDest,
				};
				data.m_Vertex = builder.ImportBuffer("NapaVoxel.Vertex",
					RHIBufferHandle{ 41, 1 }, vertexDesc, RGBufferAccess::None,
					RGContentValidity::Defined);
				data.m_Index = builder.ImportBuffer("NapaVoxel.Index",
					RHIBufferHandle{ 42, 1 }, indexDesc, RGBufferAccess::None,
					RGContentValidity::Defined);
				data.m_Vertex = builder.Read(data.m_Vertex, RGBufferAccess::Vertex, RHIStage::VertexShader);
				data.m_Index = builder.Read(data.m_Index, RGBufferAccess::Index, RHIStage::IndexInput);
				builder.Export(data.m_Vertex, RGBufferAccess::None);
				builder.Export(data.m_Index, RGBufferAccess::None);
			});
		graph.AddPass<FixturePassData>("RenderingContract.ForwardTransparent",
			[&sceneColor, &sceneDepth](RenderGraph::RGBuilder& builder, FixturePassData& data)
			{
				builder.ReadWriteInPlace(sceneColor, RGTextureAccess::RenderTarget);
				data.m_Color = sceneColor;
				data.m_Depth = builder.Read(sceneDepth, RGTextureAccess::DepthStencilRead);
			});
		graph.AddPass<FixturePassData>("RenderingContract.PostProcess",
			[&sceneColor](RenderGraph::RGBuilder& builder, FixturePassData& data)
			{
				data.m_Color = builder.Read(sceneColor, RGTextureAccess::Sample);
				builder.SideEffect();
			});

		const bool compiled = graph.Compile();
		context.Check(compiled, "Napa voxel production RenderGraph dataflow fixture compiles");
		if (!compiled)
		{
			return;
		}

		const RGExecutionPlan* plan = graph.GetExecutionPlan();
		const bool passChainMatches = plan && plan->GetPasses().size() == 4 &&
			plan->GetPasses()[0].m_ExecutionOrder < plan->GetPasses()[1].m_ExecutionOrder &&
			plan->GetPasses()[1].m_ExecutionOrder < plan->GetPasses()[2].m_ExecutionOrder &&
			plan->GetPasses()[2].m_ExecutionOrder < plan->GetPasses()[3].m_ExecutionOrder;
		context.Check(passChainMatches,
			"Napa voxel attachment writes remain between built-in opaque and later consumers");

		bool resourceContractMatches = passChainMatches;
		if (resourceContractMatches)
		{
			const RGCompiledPass& voxelPass = plan->GetPasses()[1];
			const auto hasAccess = [&voxelPass](RGResourceType type, uint64_t access) noexcept
			{
				return std::ranges::any_of(voxelPass.m_Accesses,
					[type, access](const RGCompiledAccess& compiledAccess) noexcept
					{
						return compiledAccess.m_ResourceType == type &&
							compiledAccess.m_AccessValue == access;
					});
			};
			resourceContractMatches =
				hasAccess(RGResourceType::RGTexture, static_cast<uint64_t>(RGTextureAccess::RenderTarget)) &&
				hasAccess(RGResourceType::RGTexture, static_cast<uint64_t>(RGTextureAccess::DepthStencilWrite)) &&
				hasAccess(RGResourceType::RGBuffer, static_cast<uint64_t>(RGBufferAccess::Vertex)) &&
				hasAccess(RGResourceType::RGBuffer, static_cast<uint64_t>(RGBufferAccess::Index));
		}
		context.Check(resourceContractMatches,
			"Napa voxel declares HDR color load/write, depth load/write, and mesh buffer reads");

		bool barrierContractMatches = passChainMatches;
		if (barrierContractMatches)
		{
			const RGCompiledPass& voxelPass = plan->GetPasses()[1];
			const RHIResourceState common = CommonRHIResourceState();
			const RHIResourceState vertex = ToRHIResourceState(RGBufferAccess::Vertex);
			const RHIResourceState index = ToRHIResourceState(RGBufferAccess::Index);
			const auto matchesTransition = [](const RGBarrierIntent& barrier,
				const RHIResourceState& before, const RHIResourceState& after) noexcept
			{
				return barrier.m_Kind == RGBarrierKind::Transition &&
					barrier.m_Before == before && barrier.m_After == after;
			};
			barrierContractMatches = voxelPass.m_PreBarriers.size() == 2 &&
				voxelPass.m_PostBarriers.size() == 2 &&
				std::ranges::any_of(voxelPass.m_PreBarriers,
					[&](const RGBarrierIntent& barrier)
					{
						return matchesTransition(barrier, common, vertex);
					}) &&
				std::ranges::any_of(voxelPass.m_PreBarriers,
					[&](const RGBarrierIntent& barrier)
					{
						return matchesTransition(barrier, common, index);
					}) &&
				std::ranges::any_of(voxelPass.m_PostBarriers,
					[&](const RGBarrierIntent& barrier)
					{
						return matchesTransition(barrier, vertex, common);
					}) &&
				std::ranges::any_of(voxelPass.m_PostBarriers,
					[&](const RGBarrierIntent& barrier)
					{
						return matchesTransition(barrier, index, common);
					});
		}
		context.Check(barrierContractMatches,
			"Napa voxel mesh buffers transition from Common for draw and return to Common");
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingSceneSelfTests(SelfTestContext& context) noexcept
	{
		RunOpaqueSceneExtensionContractTests(context);
		RunOverlayExtensionContractTests(context);
		RunNapaVoxelRenderGraphContractTests(context);
	}
}
