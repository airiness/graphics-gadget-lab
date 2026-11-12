#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"
#include "GGLabRuntime/Graphics/RHI/RHIAccelerationStructure.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGObjectArena.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGObservation.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResourceUtils.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraphTypes.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Pipeline/TemporalAACapability.h"
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <vector>

namespace gglab::rendering_test
{
	struct DualStorageAccessPassData
	{
		RGBufferId m_FirstBuffer;
		RGBufferId m_SecondBuffer;
	};

	struct GTAODataflowPassData
	{
		std::array<RGTextureViewId, 3> m_Views{};
	};

	struct BarrierBatchingPassData
	{
		RGTextureId m_Texture;
		RGBufferId m_Buffer;
	};

	void RunAccelerationStructureGraphTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		TransientResourcePool pool(&device);
		RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
		struct Data
		{
		};
		RGAccelerationStructureId bottom;
		RGAccelerationStructureId top;
		RGAccelerationStructureId unused;
		RGBufferId scratch;
		graph.AddPass<Data>("AS.Setup", [&](RenderGraph::RGBuilder& builder, Data&)
			{
				bottom = builder.Import<RGAccelerationStructureResource>("AS.Bottom", { 1, 1 },
					{ .m_SizeInBytes = 4096 }, RGAccelerationStructureAccess::QueryRead, RGContentValidity::Undefined);
				top = builder.Import<RGAccelerationStructureResource>("AS.Top", { 2, 1 },
					{ .m_Type = RHIAccelerationStructureType::TopLevel, .m_SizeInBytes = 4096 },
					RGAccelerationStructureAccess::QueryRead, RGContentValidity::Undefined);
				unused = builder.Import<RGAccelerationStructureResource>("AS.Unused", { 3, 1 },
					{ .m_SizeInBytes = 4096 }, RGAccelerationStructureAccess::QueryRead, RGContentValidity::Undefined);
				scratch = builder.CreateBuffer("AS.Scratch", { .m_SizeInBytes = 1024 });
			});
		graph.AddPass<Data>("AS.FirstBuild", [&](RenderGraph::RGBuilder& builder, Data&)
			{
				builder.SideEffect();
				builder.WriteInPlace(bottom, RGAccelerationStructureAccess::BuildWrite);
				builder.WriteInPlace(scratch, RGBufferAccess::AccelerationStructureScratch);
			});
		graph.AddPass<Data>("AS.FinalBuild", [&](RenderGraph::RGBuilder& builder, Data&)
			{
				builder.WriteInPlace(bottom, RGAccelerationStructureAccess::BuildWrite);
				builder.WriteInPlace(scratch, RGBufferAccess::AccelerationStructureScratch, RHIStage::All);
			});
		graph.AddPass<Data>("AS.TopBuild", [&](RenderGraph::RGBuilder& builder, Data&)
			{
				GGLAB_UNUSED(builder.Read(bottom, RGAccelerationStructureAccess::BuildRead));
				builder.WriteInPlace(top, RGAccelerationStructureAccess::BuildWrite);
				builder.WriteInPlace(scratch, RGBufferAccess::AccelerationStructureScratch,
					RHIStage::AccelerationStructureBuild | RHIStage::ComputeShader);
			});
		graph.AddPass<Data>("AS.Query", [&](RenderGraph::RGBuilder& builder, Data&)
			{
				builder.SideEffect();
				GGLAB_UNUSED(builder.Read(bottom, RGAccelerationStructureAccess::QueryRead));
				GGLAB_UNUSED(builder.Read(top, RGAccelerationStructureAccess::QueryRead));
			});
		graph.AddPass<Data>("AS.UnusedBuild", [&](RenderGraph::RGBuilder& builder, Data&)
				{ builder.WriteInPlace(unused, RGAccelerationStructureAccess::BuildWrite); });
		graph.AddPass<Data>("AS.Unrelated", [](RenderGraph::RGBuilder&, Data&) {});
		const bool compiled = graph.Compile();
		context.Check(compiled, "Imported AS resources compile through normal versioned dependency analysis");
		if (compiled)
		{
			const auto& passes = graph.GetExecutionPlan()->GetPasses();
			context.Check(!passes[1].m_Culled && !passes[2].m_Culled && !passes[3].m_Culled &&
				!passes[4].m_Culled && !passes[5].m_Culled && passes[6].m_Culled,
				"Imported AS writes retain their external side effects while an unrelated pass is culled");
			context.Check(std::ranges::any_of(passes[2].m_PreBarriers, [](const auto& barrier)
				{
					return barrier.m_Kind == RGBarrierKind::AccelerationStructure;
				}), "Consecutive live AS writers receive an explicit ordered AS memory barrier");
			for (const uint32_t passIndex : { 2u, 3u })
			{
				context.Check(std::ranges::any_of(passes[passIndex].m_PreBarriers, [](const auto& barrier)
					{
						return barrier.m_Kind == RGBarrierKind::Uav &&
							barrier.m_Before.m_Access == RHIAccess::AccelerationStructureScratch &&
							barrier.m_After.m_Access == RHIAccess::AccelerationStructureScratch;
					}), "Scratch writes keep their explicit access and ordered hazard across stage changes");
			}
			context.Check(std::ranges::any_of(passes[3].m_PreBarriers, [](const auto& barrier)
				{
					return barrier.m_Before.m_Access == RHIAccess::AccelerationStructureWrite &&
						barrier.m_After.m_Access == RHIAccess::AccelerationStructureRead;
				}), "TLAS construction declares and synchronizes its BLAS build reads");
			context.Check(std::ranges::any_of(passes[4].m_PreBarriers, [](const auto& barrier)
				{
					return barrier.m_After.m_Stages == RHIStage::ComputeShader;
				}), "The graph carries AS availability to the compute ray-query stage");
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			const auto serialized = SerializeRenderGraphSnapshot(snapshot, false);
			context.Check(serialized.find("AccelerationStructure") != std::string::npos &&
				serialized.find("BuildRead") != std::string::npos && serialized.find("QueryRead") != std::string::npos,
				"Graph exports preserve typed AS resources and access semantics");
			context.Check(graph.ResolveObservation({ .m_Resource = "AS.Top" }, RGObservationKind::ResourceExport).m_Failure ==
				RGObservationFailure::UnsupportedResource,
				"Opaque AS storage cannot be reinterpreted by ordinary buffer resource exports");
		}
		RenderGraph undefined({ .m_Device = &device, .m_TransientResourcePool = &pool });
		undefined.AddPass<Data>("AS.UnbuiltRead", [&](RenderGraph::RGBuilder& builder, Data&)
			{
				builder.SideEffect();
				const auto unbuilt = builder.Import<RGAccelerationStructureResource>("AS.Unbuilt", { 4, 1 },
					{ .m_SizeInBytes = 4096 }, RGAccelerationStructureAccess::QueryRead, RGContentValidity::Undefined);
				GGLAB_UNUSED(builder.Read(unbuilt));
			});
		context.Check(!undefined.Compile(), "A declared AS query cannot consume an unbuilt imported structure");
		context.Check(!IsRGAccessCompatible(RGAccelerationStructureAccess::BuildRead,
			RGDependencyAccess::Read, RGOrderingRequirement::Unordered),
			"AS accesses cannot opt out of dependency ordering");
	}

	void RunPartialTextureProducerTests(SelfTestContext& context) noexcept
	{
		// Both array layers and mip levels must inherit only their own live producer.
		for (const bool useMips : { false, true })
		{
			for (const bool sampleWhole : { false, true })
			{
				RenderGraph graph({ .m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
					.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }) });
				RGTextureId texture{};
				struct Data {};
				const RHISubresourceRange first{ .m_MipCount = 1, .m_ArraySliceCount = 1 };
				const RHISubresourceRange second{ .m_BaseMip = useMips ? 1u : 0u, .m_MipCount = 1,
					.m_BaseArraySlice = useMips ? 0u : 1u, .m_ArraySliceCount = 1 };
				graph.AddPass<Data>("Initialize", [&](RenderGraph::RGBuilder& builder, Data&)
					{
						texture = builder.CreateTexture("PartialTexture", { .m_Format = RHIFormat::R32Float,
							.m_Extent = { 16, 16, 1 }, .m_ArraySize = static_cast<uint16_t>(useMips ? 1 : 2),
							.m_MipLevels = static_cast<uint16_t>(useMips ? 2 : 1) });
						builder.WriteInPlace(texture, RGTextureAccess::RenderTarget);
					});
				graph.AddPass<Data>("OverwrittenFirst", [&](RenderGraph::RGBuilder& builder, Data&)
					{
						builder.WriteInPlace(texture, RGTextureAccess::RenderTarget, first);
					});
				graph.AddPass<Data>("FinalFirst", [&](RenderGraph::RGBuilder& builder, Data&)
					{
						builder.WriteInPlace(texture, RGTextureAccess::RenderTarget, first);
					});
				graph.AddPass<Data>("Sample", [&](RenderGraph::RGBuilder& builder, Data&)
					{
						builder.Read(texture, RGTextureAccess::Sample, sampleWhole ? RHISubresourceRange{} : second);
						builder.SideEffect();
					});
				const bool compiled = graph.Compile();
				RGSnapshot snapshot;
				BuildRenderGraphSnapshot(graph, snapshot);
				context.Check(compiled && !snapshot.m_Passes[0].m_Culled && snapshot.m_Passes[1].m_Culled &&
					snapshot.m_Passes[2].m_Culled == !sampleWhole && snapshot.m_Resources[0].m_LastUserPassIndex == 3 &&
					snapshot.m_Passes[3].m_DestroyResources.size() == 1,
					"Partial texture reads retain inherited producers, cull overwritten/unused writes, and release after the final consumer");
			}
		}
	}

	[[nodiscard]] bool HasDependencyEdge(const RGSnapshot& snapshot, uint32_t fromPass, uint32_t toPass, RGDependencyReason reason) noexcept
	{
		return std::ranges::any_of(snapshot.m_DependencyEdges,
			[=](const RGSnapshotDependencyEdge& edge)
			{
				return edge.m_FromPassIndex == static_cast<int32_t>(fromPass) &&
					edge.m_ToPassIndex == static_cast<int32_t>(toPass) &&
					edge.m_Reason == reason;
			});
	}

	void RunGTAORenderGraphDataflowTests(SelfTestContext& context) noexcept
	{
		RenderGraph graph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		const RHITextureDesc halfAODesc{
			.m_Format = RHIFormat::R16Float,
			.m_Extent = { 640, 360, 1 },
		};
		RHITextureDesc halfDepthDesc = halfAODesc;
		halfDepthDesc.m_Format = RHIFormat::R32Float;
		const RHITextureDesc finalAODesc{
			.m_Format = RHIFormat::R8Unorm,
			.m_Extent = { 1280, 720, 1 },
		};

		RGTextureId rawAO;
		RGTextureId halfDepth;
		RGTextureId denoiseX;
		RGTextureId denoiseY;
		RGTextureId finalAO;
		graph.AddPass<GTAODataflowPassData>("Lighting.GTAO", RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, GTAODataflowPassData& data)
			{
				rawAO = builder.CreateTexture("GTAO.RawAO", halfAODesc);
				halfDepth = builder.CreateTexture("GTAO.HalfDepthViewZ", halfDepthDesc);
				builder.WriteInPlace(rawAO, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				builder.WriteInPlace(halfDepth, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Views[0] = builder.CreateView<RHITextureViewType::UnorderedAccess>(rawAO);
				data.m_Views[1] = builder.CreateView<RHITextureViewType::UnorderedAccess>(halfDepth);
			});
		graph.AddPass<GTAODataflowPassData>(
			"Lighting.GTAO.DenoiseX", RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, GTAODataflowPassData& data)
			{
				const RGTextureId rawAOSrv = builder.Read(rawAO, RGTextureAccess::Sample, RHIStage::ComputeShader);
				const RGTextureId halfDepthSrv = builder.Read(halfDepth, RGTextureAccess::Sample, RHIStage::ComputeShader);
				denoiseX = builder.CreateTexture("GTAO.DenoiseX", halfAODesc);
				builder.WriteInPlace(denoiseX, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Views[0] = builder.CreateView<RHITextureViewType::ShaderResource>(rawAOSrv);
				data.m_Views[1] = builder.CreateView<RHITextureViewType::ShaderResource>(halfDepthSrv);
				data.m_Views[2] = builder.CreateView<RHITextureViewType::UnorderedAccess>(denoiseX);
			});
		graph.AddPass<GTAODataflowPassData>(
			"Lighting.GTAO.DenoiseY", RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, GTAODataflowPassData& data)
			{
				const RGTextureId denoiseXSrv = builder.Read(denoiseX, RGTextureAccess::Sample, RHIStage::ComputeShader);
				const RGTextureId halfDepthSrv = builder.Read(halfDepth, RGTextureAccess::Sample, RHIStage::ComputeShader);
				denoiseY = builder.CreateTexture("GTAO.DenoiseY", halfAODesc);
				builder.WriteInPlace(denoiseY, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Views[0] = builder.CreateView<RHITextureViewType::ShaderResource>(denoiseXSrv);
				data.m_Views[1] = builder.CreateView<RHITextureViewType::ShaderResource>(halfDepthSrv);
				data.m_Views[2] = builder.CreateView<RHITextureViewType::UnorderedAccess>(denoiseY);
			});
		graph.AddPass<GTAODataflowPassData>(
			"Lighting.GTAO.Upsample", RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, GTAODataflowPassData& data)
			{
				const RGTextureId denoiseYSrv = builder.Read(denoiseY, RGTextureAccess::Sample, RHIStage::ComputeShader);
				const RGTextureId halfDepthSrv = builder.Read(halfDepth, RGTextureAccess::Sample, RHIStage::ComputeShader);
				finalAO = builder.CreateTexture("GTAO.FinalAO", finalAODesc);
				builder.WriteInPlace(finalAO, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Views[0] = builder.CreateView<RHITextureViewType::ShaderResource>(denoiseYSrv);
				data.m_Views[1] = builder.CreateView<RHITextureViewType::ShaderResource>(halfDepthSrv);
				data.m_Views[2] = builder.CreateView<RHITextureViewType::UnorderedAccess>(finalAO);
			});
		graph.AddPass<GTAODataflowPassData>("Geometry.ForwardPBR.Opaque",
			[&](RenderGraph::RGBuilder& builder, GTAODataflowPassData& data)
			{
				const RGTextureId finalAOSrv = builder.Read(finalAO, RGTextureAccess::Sample, RHIStage::PixelShader);
				data.m_Views[0] = builder.CreateView<RHITextureViewType::ShaderResource>(finalAOSrv);
				builder.SideEffect();
			});

		const bool compiled = graph.Compile();
		context.Check(compiled, "GTAO production RenderGraph dataflow fixture compiles");
		if (!compiled)
		{
			return;
		}

		RGSnapshot snapshot;
		BuildRenderGraphSnapshot(graph, snapshot);
		const bool allPassesLive = snapshot.m_Passes.size() == 5 &&
			std::ranges::none_of(snapshot.m_Passes,
				[](const RGSnapshotPassInfo& pass) noexcept { return pass.m_Culled; });
		context.Check(allPassesLive &&
			HasDependencyEdge(snapshot, 0, 1, RGDependencyReason::WriterToReader) &&
			HasDependencyEdge(snapshot, 1, 2, RGDependencyReason::WriterToReader) &&
			HasDependencyEdge(snapshot, 2, 3, RGDependencyReason::WriterToReader) &&
			HasDependencyEdge(snapshot, 3, 4, RGDependencyReason::WriterToReader),
			"GTAO evaluate, denoise, upsample, and opaque consumption remain one live chain");

		const auto& opaqueBarriers = snapshot.m_Passes[4].m_PreBarriers;
		context.Check(opaqueBarriers.size() == 1 &&
			opaqueBarriers[0].m_ResourceName == "GTAO.FinalAO" &&
			opaqueBarriers[0].m_Kind == RGBarrierKind::Transition &&
			opaqueBarriers[0].m_Reason == RGBarrierReason::AccessTransition &&
			opaqueBarriers[0].m_Before.m_Stages == RHIStage::ComputeShader &&
			opaqueBarriers[0].m_Before.m_Access == RHIAccess::UnorderedAccess &&
			opaqueBarriers[0].m_After.m_Stages == RHIStage::PixelShader &&
			opaqueBarriers[0].m_After.m_Access == RHIAccess::ShaderResource,
			"GTAO FinalAO transitions exactly from compute UAV-write to opaque pixel SRV-read");
	}

	void RunRenderGraphAccessAndBarrierContractTests(SelfTestContext& context) noexcept
	{
		struct TextureCompatibilityRow
		{
			RGTextureAccess m_Access;
			std::array<bool, 3> m_DependencyAccesses;
		};
		constexpr std::array TextureCompatibility =
		{
			TextureCompatibilityRow{ RGTextureAccess::None,{ false, false, false } },
			TextureCompatibilityRow{ RGTextureAccess::Sample,{ true, false, false } },
			TextureCompatibilityRow{ RGTextureAccess::RenderTarget,{ false, true, true } },
			TextureCompatibilityRow{ RGTextureAccess::DepthStencilWrite,{ false, true, true } },
			TextureCompatibilityRow{ RGTextureAccess::DepthStencilRead,{ true, false, false } },
			TextureCompatibilityRow{ RGTextureAccess::StorageRead,{ true, false, false } },
			TextureCompatibilityRow{ RGTextureAccess::StorageWrite,{ false, true, false } },
			TextureCompatibilityRow{ RGTextureAccess::StorageReadWrite,{ false, false, true } },
			TextureCompatibilityRow{ RGTextureAccess::CopySource,{ true, false, false } },
			TextureCompatibilityRow{ RGTextureAccess::CopyDest,{ false, true, false } },
			TextureCompatibilityRow{ RGTextureAccess::Present,{ false, false, false } },
		};
		struct BufferCompatibilityRow
		{
			RGBufferAccess m_Access;
			std::array<bool, 3> m_DependencyAccesses;
		};
		constexpr std::array BufferCompatibility =
		{
			BufferCompatibilityRow{ RGBufferAccess::None,{ false, false, false } },
			BufferCompatibilityRow{ RGBufferAccess::Vertex,{ true, false, false } },
			BufferCompatibilityRow{ RGBufferAccess::Index,{ true, false, false } },
			BufferCompatibilityRow{ RGBufferAccess::Constant,{ true, false, false } },
			BufferCompatibilityRow{ RGBufferAccess::StructuredRead,{ true, false, false } },
			BufferCompatibilityRow{ RGBufferAccess::StorageRead,{ true, false, false } },
			BufferCompatibilityRow{ RGBufferAccess::StorageWrite,{ false, true, false } },
			BufferCompatibilityRow{ RGBufferAccess::StorageReadWrite,{ false, false, true } },
			BufferCompatibilityRow{ RGBufferAccess::CopySource,{ true, false, false } },
			BufferCompatibilityRow{ RGBufferAccess::CopyDest,{ false, true, false } },
			BufferCompatibilityRow{ RGBufferAccess::IndirectArgument,{ true, false, false } },
		};
		constexpr std::array DependencyAccesses =
		{
			RGDependencyAccess::Read,
			RGDependencyAccess::Write,
			RGDependencyAccess::ReadWrite,
		};
		static_assert(
			TextureCompatibility.size() == static_cast<size_t>(RGTextureAccess::Present) + 1);
		static_assert(BufferCompatibility.size() == static_cast<size_t>(RGBufferAccess::IndirectArgument) + 1);

		bool completeCompatibilityMatrixMatches = true;
		for (const auto& row : TextureCompatibility)
		{
			for (size_t accessIndex = 0; accessIndex < DependencyAccesses.size(); ++accessIndex)
			{
				const auto dependencyAccess = DependencyAccesses[accessIndex];
				completeCompatibilityMatrixMatches &=
					IsRGAccessCompatible(row.m_Access, dependencyAccess, RGOrderingRequirement::Ordered) == row.m_DependencyAccesses[accessIndex];
				completeCompatibilityMatrixMatches &=
					IsRGAccessCompatible(row.m_Access, dependencyAccess, RGOrderingRequirement::Unordered) ==
					(row.m_DependencyAccesses[accessIndex] && IsStorageAccess(row.m_Access));
			}
		}
		for (const auto& row : BufferCompatibility)
		{
			for (size_t accessIndex = 0; accessIndex < DependencyAccesses.size(); ++accessIndex)
			{
				const auto dependencyAccess = DependencyAccesses[accessIndex];
				completeCompatibilityMatrixMatches &=
					IsRGAccessCompatible(row.m_Access, dependencyAccess, RGOrderingRequirement::Ordered) == row.m_DependencyAccesses[accessIndex];
				completeCompatibilityMatrixMatches &=
					IsRGAccessCompatible(row.m_Access, dependencyAccess, RGOrderingRequirement::Unordered) ==
					(row.m_DependencyAccesses[accessIndex] && IsStorageAccess(row.m_Access));
			}
		}
		context.Check(completeCompatibilityMatrixMatches,
			"RenderGraph validates the complete texture and buffer access compatibility matrix");

		bool orderedUavMatrixMatches = true;
		for (const auto beforeAccess : DependencyAccesses)
		{
			for (const auto afterAccess : DependencyAccesses)
			{
				const bool expected = beforeAccess != RGDependencyAccess::Read ||
					afterAccess != RGDependencyAccess::Read;
				orderedUavMatrixMatches &=
					NeedsOrderedUavBarrier(beforeAccess, RGOrderingRequirement::Ordered, afterAccess, RGOrderingRequirement::Ordered) == expected;
				orderedUavMatrixMatches &=
					!NeedsOrderedUavBarrier(beforeAccess, RGOrderingRequirement::Unordered, afterAccess, RGOrderingRequirement::Ordered);
				orderedUavMatrixMatches &=
					!NeedsOrderedUavBarrier(beforeAccess, RGOrderingRequirement::Ordered, afterAccess, RGOrderingRequirement::Unordered);
			}
		}
		context.Check(orderedUavMatrixMatches,
			"Ordered UAV hazards cover the complete read, write, and read-write matrix");
		const RHIResourceState pixelReadState = ToRHIResourceState(RGBufferAccess::StructuredRead, RHIStage::PixelShader);
		const RHIResourceState computeReadState = ToRHIResourceState(RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
		context.Check(!NeedsRHIResourceTransition(pixelReadState, computeReadState),
			"Stage-only changes do not alter persistent resource state");

		const RHIResourceState textureStorageState = ToRHIResourceState(RGTextureAccess::StorageRead);
		const RHIResourceState bufferStorageState = ToRHIResourceState(RGBufferAccess::StorageRead);
		context.Check(
			textureStorageState == ToRHIResourceState(RGTextureAccess::StorageWrite) &&
			textureStorageState == ToRHIResourceState(RGTextureAccess::StorageReadWrite) &&
			bufferStorageState == ToRHIResourceState(RGBufferAccess::StorageWrite) &&
			bufferStorageState == ToRHIResourceState(RGBufferAccess::StorageReadWrite) &&
			ToRHIUsage(RGTextureAccess::StorageRead) == RHITextureUsage::UnorderedAccess &&
			ToRHIUsage(RGTextureAccess::StorageWrite) == RHITextureUsage::UnorderedAccess &&
			ToRHIUsage(RGTextureAccess::StorageReadWrite) == RHITextureUsage::UnorderedAccess &&
			ToRHIUsage(RGBufferAccess::StorageRead) == RHIBufferUsage::UnorderedAccess &&
			ToRHIUsage(RGBufferAccess::StorageWrite) == RHIBufferUsage::UnorderedAccess &&
			ToRHIUsage(RGBufferAccess::StorageReadWrite) == RHIBufferUsage::UnorderedAccess &&
			HasUavAccess(textureStorageState) && HasUavAccess(bufferStorageState),
			"Storage read, write, and read-write accesses map to UAV resource states");
		context.Check(
			IsRGAccessCompatible(RGTextureAccess::StorageRead, RGDependencyAccess::Read, RGOrderingRequirement::Ordered) &&
			IsRGAccessCompatible(RGTextureAccess::StorageWrite, RGDependencyAccess::Write, RGOrderingRequirement::Ordered) &&
			IsRGAccessCompatible(RGBufferAccess::StorageReadWrite, RGDependencyAccess::ReadWrite, RGOrderingRequirement::Unordered) &&
			!IsRGAccessCompatible(RGTextureAccess::StorageRead, RGDependencyAccess::Write, RGOrderingRequirement::Ordered) &&
			!IsRGAccessCompatible(RGBufferAccess::StructuredRead, RGDependencyAccess::Read, RGOrderingRequirement::Unordered),
			"Storage access and ordering declarations reject incompatible semantics");

		RenderGraph graph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RGBufferId storageBuffer;
		graph.AddPass<StorageAccessPassData>("InitialStorageWrite",
			[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				storageBuffer = builder.CreateBuffer("StorageBuffer");
				builder.WriteInPlace(storageBuffer, RGBufferAccess::StorageWrite);
				data.m_Buffer = storageBuffer;
			});
		graph.AddPass<StorageAccessPassData>("StorageRead",
			[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				data.m_Buffer = builder.Read(storageBuffer, RGBufferAccess::StorageRead, std::nullopt, RGOrderingRequirement::Unordered);
				builder.SideEffect();
			});
		graph.AddPass<StorageAccessPassData>("SecondStorageWrite",
			[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				builder.WriteInPlace(storageBuffer, RGBufferAccess::StorageWrite);
				data.m_Buffer = storageBuffer;
			});
		graph.AddPass<StorageAccessPassData>("StorageReadWrite",
			[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				builder.ReadWriteInPlace(storageBuffer, RGBufferAccess::StorageReadWrite);
				data.m_Buffer = storageBuffer;
			});
		graph.AddPass<StorageAccessPassData>("FinalStorageRead",
			[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				data.m_Buffer = builder.Read(storageBuffer, RGBufferAccess::StorageRead);
				builder.SideEffect();
			});
		graph.AddPass<StorageAccessPassData>("UnusedStorageWrite",
			[](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				data.m_Buffer = builder.CreateBuffer("UnusedStorageBuffer");
				builder.WriteInPlace(data.m_Buffer, RGBufferAccess::StorageWrite);
			});

		const bool compiled = graph.Compile();
		context.Check(compiled, "Storage access RenderGraph fixture compiles");
		if (!compiled)
		{
			return;
		}

		RGSnapshot snapshot;
		BuildRenderGraphSnapshot(graph, snapshot);
		const auto& initialWrite = snapshot.m_Passes[0];
		const auto& storageRead = snapshot.m_Passes[1];
		const auto& secondWrite = snapshot.m_Passes[2];
		const auto& storageReadWrite = snapshot.m_Passes[3];
		const auto& finalRead = snapshot.m_Passes[4];
		const auto& unusedWrite = snapshot.m_Passes[5];

		context.Check(
			initialWrite.m_Accesses[0].m_DependencyAccess == RGDependencyAccess::Write &&
			initialWrite.m_Accesses[0].m_AccessValue == static_cast<uint64_t>(RGBufferAccess::StorageWrite) &&
			storageRead.m_Accesses[0].m_DependencyAccess == RGDependencyAccess::Read &&
			storageRead.m_Accesses[0].m_AccessValue == static_cast<uint64_t>(RGBufferAccess::StorageRead) &&
			storageReadWrite.m_Accesses[0].m_DependencyAccess == RGDependencyAccess::ReadWrite &&
			storageReadWrite.m_Accesses[0].m_AccessValue == static_cast<uint64_t>(RGBufferAccess::StorageReadWrite),
			"Storage access semantics remain distinct in RenderGraph snapshots");
		context.Check(
			storageRead.m_Accesses[0].m_Ordering == RGOrderingRequirement::Unordered &&
			initialWrite.m_Accesses[0].m_Ordering == RGOrderingRequirement::Ordered,
			"RenderGraph snapshots preserve explicit and default ordering requirements");
		context.Check(
			HasDependencyEdge(snapshot, 0, 1, RGDependencyReason::WriterToReader) &&
			HasDependencyEdge(snapshot, 1, 2, RGDependencyReason::PreviousReaderToWriter),
			"RenderGraph creates writer-to-reader and reader-to-writer edges");
		context.Check(
			HasDependencyEdge(snapshot, 0, 2, RGDependencyReason::PreviousWriterToWriter),
			"RenderGraph creates writer-to-writer edges");
		context.Check(HasDependencyEdge(snapshot, 2, 3, RGDependencyReason::WriterToReader) &&
			HasDependencyEdge(snapshot, 3, 4, RGDependencyReason::WriterToReader),
			"Read-write storage access participates in incoming and outgoing dependencies");
		context.Check(!initialWrite.m_PreBarriers.empty() &&
			initialWrite.m_PreBarriers[0].m_Kind == RGBarrierKind::Transition &&
			storageRead.m_PreBarriers.empty() &&
			secondWrite.m_PreBarriers.size() == 1 &&
			secondWrite.m_PreBarriers[0].m_Kind == RGBarrierKind::Uav &&
			storageReadWrite.m_PreBarriers.size() == 1 &&
			storageReadWrite.m_PreBarriers[0].m_Kind == RGBarrierKind::Uav &&
			finalRead.m_PreBarriers.size() == 1 &&
			finalRead.m_PreBarriers[0].m_Kind == RGBarrierKind::Uav,
			"Storage barriers distinguish transitions, ordered hazards, and explicit unordered access");
		context.Check(unusedWrite.m_Culled && unusedWrite.m_ExecutionOrder < 0 &&
			unusedWrite.m_PreBarriers.empty() &&
			unusedWrite.m_PostBarriers.empty(),
			"Culled storage writers leave no execution or barrier work");

		RenderGraph transitionGraph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RGBufferId transitionBuffer;
		transitionGraph.AddPass<StorageAccessPassData>("TransitionStorageWrite",
			[&transitionBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				transitionBuffer = builder.CreateBuffer("TransitionBuffer");
				builder.WriteInPlace(transitionBuffer, RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Buffer = transitionBuffer;
			});
		transitionGraph.AddPass<StorageAccessPassData>("TransitionStructuredRead",
			[&transitionBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				data.m_Buffer = builder.Read(transitionBuffer, RGBufferAccess::StructuredRead, RHIStage::PixelShader);
				builder.SideEffect();
			});
		const bool transitionCompiled = transitionGraph.Compile();
		context.Check(transitionCompiled, "Compute UAV-write to pixel SRV-read fixture compiles");
		if (transitionCompiled)
		{
			RGSnapshot transitionSnapshot;
			BuildRenderGraphSnapshot(transitionGraph, transitionSnapshot);
			const auto& barriers = transitionSnapshot.m_Passes[1].m_PreBarriers;
			context.Check(barriers.size() == 1 &&
				barriers[0].m_ResourceName == "TransitionBuffer" &&
				barriers[0].m_ResourceType == RGResourceType::RGBuffer &&
				barriers[0].m_Kind == RGBarrierKind::Transition &&
				barriers[0].m_Reason == RGBarrierReason::AccessTransition &&
				barriers[0].m_Before.m_Stages == RHIStage::ComputeShader &&
				barriers[0].m_Before.m_Access == RHIAccess::UnorderedAccess &&
				barriers[0].m_Before.m_Layout == RHILayout::Common &&
				barriers[0].m_After.m_Stages == RHIStage::PixelShader &&
				barriers[0].m_After.m_Access == RHIAccess::ShaderResource &&
				barriers[0].m_After.m_Layout == RHILayout::Common,
				"Forward+ cull outputs transition exactly from compute UAV-write to pixel SRV-read");
		}

		RenderGraph computeTransitionGraph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RGBufferId computeTransitionBuffer;
		computeTransitionGraph.AddPass<StorageAccessPassData>("ComputeTransitionStorageWrite",
			[&computeTransitionBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				computeTransitionBuffer = builder.CreateBuffer("ComputeTransitionBuffer");
				builder.WriteInPlace(computeTransitionBuffer, RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Buffer = computeTransitionBuffer;
			});
		computeTransitionGraph.AddPass<StorageAccessPassData>("ComputeTransitionStructuredRead",
			[&computeTransitionBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				data.m_Buffer = builder.Read(computeTransitionBuffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
				builder.SideEffect();
			});
		const bool computeTransitionCompiled = computeTransitionGraph.Compile();
		context.Check(
			computeTransitionCompiled, "Compute UAV-write to compute SRV-read fixture compiles");
		if (computeTransitionCompiled)
		{
			RGSnapshot transitionSnapshot;
			BuildRenderGraphSnapshot(computeTransitionGraph, transitionSnapshot);
			const auto& barriers = transitionSnapshot.m_Passes[1].m_PreBarriers;
			context.Check(barriers.size() == 1 &&
				barriers[0].m_ResourceName == "ComputeTransitionBuffer" &&
				barriers[0].m_ResourceType == RGResourceType::RGBuffer &&
				barriers[0].m_Kind == RGBarrierKind::Transition &&
				barriers[0].m_Reason == RGBarrierReason::AccessTransition &&
				barriers[0].m_Before.m_Stages == RHIStage::ComputeShader &&
				barriers[0].m_Before.m_Access == RHIAccess::UnorderedAccess &&
				barriers[0].m_Before.m_Layout == RHILayout::Common &&
				barriers[0].m_After.m_Stages == RHIStage::ComputeShader &&
				barriers[0].m_After.m_Access == RHIAccess::ShaderResource &&
				barriers[0].m_After.m_Layout == RHILayout::Common,
				"HDR diff metrics transition exactly from compute UAV-write to compute SRV-read");
		}

		RenderGraph resourceSpecificGraph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RGBufferId firstBuffer;
		RGBufferId secondBuffer;
		resourceSpecificGraph.AddPass<DualStorageAccessPassData>("WriteTwoStorageBuffers",
			[&](RenderGraph::RGBuilder& builder, DualStorageAccessPassData& data)
			{
				firstBuffer = builder.CreateBuffer("FirstStorageBuffer");
				secondBuffer = builder.CreateBuffer("SecondStorageBuffer");
				builder.WriteInPlace(firstBuffer, RGBufferAccess::StorageWrite);
				builder.WriteInPlace(secondBuffer, RGBufferAccess::StorageWrite);
				data.m_FirstBuffer = firstBuffer;
				data.m_SecondBuffer = secondBuffer;
			});
		resourceSpecificGraph.AddPass<DualStorageAccessPassData>("ReadWriteTwoStorageBuffers",
			[&](RenderGraph::RGBuilder& builder, DualStorageAccessPassData& data)
			{
				builder.ReadWriteInPlace(firstBuffer, RGBufferAccess::StorageReadWrite);
				builder.ReadWriteInPlace(secondBuffer, RGBufferAccess::StorageReadWrite);
				data.m_FirstBuffer = firstBuffer;
				data.m_SecondBuffer = secondBuffer;
				builder.SideEffect();
			});
		const bool resourceSpecificCompiled = resourceSpecificGraph.Compile();
		context.Check(
			resourceSpecificCompiled, "Resource-specific UAV barrier fixture compiles");
		if (resourceSpecificCompiled)
		{
			RGSnapshot resourceSpecificSnapshot;
			BuildRenderGraphSnapshot(resourceSpecificGraph, resourceSpecificSnapshot);
			const auto& barriers = resourceSpecificSnapshot.m_Passes[1].m_PreBarriers;
			context.Check(
				barriers.size() == 2 && barriers[0].m_Kind == RGBarrierKind::Uav &&
				barriers[1].m_Kind == RGBarrierKind::Uav &&
				barriers[0].m_VirtualResourceIndex != barriers[1].m_VirtualResourceIndex,
				"Each ordered UAV hazard retains its own resource identity for physical resolution");
		}

		RenderGraph multiSubresourceGraph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RGTextureId mipTexture;
		const RHITextureDesc mipTextureDesc = {
			.m_Format = RHIFormat::R8G8B8A8Unorm,
			.m_Extent = { .m_Width = 4, .m_Height = 4, .m_Depth = 1 },
			.m_MipLevels = 3,
		};
		const RHISubresourceRange firstMip = {
			.m_BaseMip = 0,
			.m_MipCount = 1,
			.m_BaseArraySlice = 0,
			.m_ArraySliceCount = 1,
			.m_Aspects = RHITextureAspect::Color,
		};
		const RHISubresourceRange firstTwoMips = {
			.m_BaseMip = 0,
			.m_MipCount = 2,
			.m_BaseArraySlice = 0,
			.m_ArraySliceCount = 1,
			.m_Aspects = RHITextureAspect::Color,
		};
		const RHISubresourceRange secondMip = {
			.m_BaseMip = 1,
			.m_MipCount = 1,
			.m_BaseArraySlice = 0,
			.m_ArraySliceCount = 1,
			.m_Aspects = RHITextureAspect::Color,
		};
		const RHISubresourceRange thirdMip = {
			.m_BaseMip = 2,
			.m_MipCount = 1,
			.m_BaseArraySlice = 0,
			.m_ArraySliceCount = 1,
			.m_Aspects = RHITextureAspect::Color,
		};
		multiSubresourceGraph.AddPass<TextureStorageAccessPassData>("WriteFirstMip",
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				mipTexture = builder.CreateTexture("MipTexture", mipTextureDesc);
				// Every subresource later consumed in this fixture must be
				// defined first: mip0 (UAV), mip1 (copy destination) and
				// mip2 (UAV, later sampled and copied).
				builder.WriteInPlace(mipTexture, RGTextureAccess::StorageWrite, firstMip);
				builder.WriteInPlace(mipTexture, RGTextureAccess::CopyDest, secondMip);
				builder.WriteInPlace(mipTexture, RGTextureAccess::StorageWrite, thirdMip);
				data.m_Texture = mipTexture;
			});
		multiSubresourceGraph.AddPass<TextureStorageAccessPassData>("SampleThirdMip",
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				data.m_Texture = builder.Read(mipTexture, RGTextureAccess::Sample, RHIStage::PixelShader, thirdMip);
				builder.SideEffect();
			});
		multiSubresourceGraph.AddPass<TextureStorageAccessPassData>("ReadWriteFirstTwoMips",
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				builder.ReadWriteInPlace(mipTexture, RGTextureAccess::StorageReadWrite, firstTwoMips);
				data.m_Texture = mipTexture;
				builder.SideEffect();
			});
		multiSubresourceGraph.AddPass<TextureStorageAccessPassData>("CopyToThirdMip",
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				builder.WriteInPlace(mipTexture, RGTextureAccess::CopyDest, thirdMip);
				data.m_Texture = mipTexture;
				builder.SideEffect();
			});
		const bool multiSubresourceCompiled = multiSubresourceGraph.Compile();
		context.Check(
			multiSubresourceCompiled, "Multi-subresource texture UAV barrier fixture compiles");
		if (multiSubresourceCompiled)
		{
			RGSnapshot multiSubresourceSnapshot;
			BuildRenderGraphSnapshot(multiSubresourceGraph, multiSubresourceSnapshot);
			const auto& barriers = multiSubresourceSnapshot.m_Passes[2].m_PreBarriers;
			const auto transition = std::ranges::find(barriers, RGBarrierKind::Transition, &RGSnapshotBarrierInfo::m_Kind);
			const auto uav = std::ranges::find(barriers, RGBarrierKind::Uav, &RGSnapshotBarrierInfo::m_Kind);
			context.Check(barriers.size() == 2 && transition != barriers.end() &&
				transition->m_Subresources &&
				transition->m_Subresources->m_BaseMip == 1 &&
				transition->m_Subresources->m_MipCount == 1 &&
				uav != barriers.end() && uav->m_Subresources == firstMip &&
				uav->m_Before.m_Layout == RHILayout::UnorderedAccess &&
				uav->m_After.m_Layout == RHILayout::UnorderedAccess,
				"Texture transitions and Enhanced UAV ordering retain exact subresource state");
			const auto& copyBarriers = multiSubresourceSnapshot.m_Passes[3].m_PreBarriers;
			context.Check(copyBarriers.size() == 1 &&
				copyBarriers[0].m_Kind == RGBarrierKind::Transition &&
				copyBarriers[0].m_Subresources == thirdMip &&
				Test(copyBarriers[0].m_Before.m_Stages, RHIStage::PixelShader),
				"A texture UAV barrier preserves non-UAV synchronization scopes on untouched subresources");
		}

		RenderGraph stageScopeGraph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RGBufferId stageScopeBuffer;
		stageScopeGraph.AddPass<StorageAccessPassData>("InitializeStageScopeBuffer",
			[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				stageScopeBuffer = builder.CreateBuffer("StageScopeBuffer");
				builder.WriteInPlace(stageScopeBuffer, RGBufferAccess::CopyDest);
				data.m_Buffer = stageScopeBuffer;
			});
		stageScopeGraph.AddPass<StorageAccessPassData>("PixelRead",
			[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				data.m_Buffer = builder.Read(stageScopeBuffer, RGBufferAccess::StructuredRead, RHIStage::PixelShader);
				builder.SideEffect();
			});
		stageScopeGraph.AddPass<StorageAccessPassData>("ComputeRead",
			[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				data.m_Buffer = builder.Read(stageScopeBuffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
				builder.SideEffect();
			});
		stageScopeGraph.AddPass<StorageAccessPassData>("CopyAfterReads",
			[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				builder.WriteInPlace(stageScopeBuffer, RGBufferAccess::CopyDest);
				data.m_Buffer = stageScopeBuffer;
				builder.SideEffect();
			});
		const bool stageScopeCompiled = stageScopeGraph.Compile();
		context.Check(stageScopeCompiled, "Barrier synchronization-scope fixture compiles");
		if (stageScopeCompiled)
		{
			RGSnapshot stageScopeSnapshot;
			BuildRenderGraphSnapshot(stageScopeGraph, stageScopeSnapshot);
			const auto& computeRead = stageScopeSnapshot.m_Passes[2];
			const auto& copyAfterReads = stageScopeSnapshot.m_Passes[3];
			context.Check(computeRead.m_PreBarriers.empty() &&
				copyAfterReads.m_PreBarriers.size() == 1 &&
				Test(copyAfterReads.m_PreBarriers[0].m_Before.m_Stages, RHIStage::PixelShader) &&
				Test(copyAfterReads.m_PreBarriers[0].m_Before.m_Stages, RHIStage::ComputeShader),
				"Read-only stage scopes accumulate until a persistent state transition");
		}

		RenderGraph temporalResolvedInitializationGraph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool =
				reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RGTextureId temporalResolvedColor;
		temporalResolvedInitializationGraph.AddPass<TextureStorageAccessPassData>(
			"TAA.InitializeResolvedColor",
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				builder.SideEffect();
				RHITextureDesc desc{};
				desc.m_Format = TemporalAAResolvedColorFormat;
				desc.m_Extent = { 1920, 1080, 1 };
				temporalResolvedColor = builder.CreateTexture("TAA.ResolvedColorInitialization", desc);
				builder.WriteInPlace(temporalResolvedColor, RGTextureAccess::RenderTarget);
				data.m_Texture = temporalResolvedColor;
			});
		temporalResolvedInitializationGraph.AddPass<TextureStorageAccessPassData>(
			"TAA.ComputeResolve", RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				// Production TAA stays live through its exported history writes.
				builder.SideEffect();
				builder.WriteInPlace(temporalResolvedColor, RGTextureAccess::StorageWrite);
				data.m_Texture = temporalResolvedColor;
			});
		temporalResolvedInitializationGraph.AddPass<TextureStorageAccessPassData>(
			"Forward.TransparentLoad",
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				builder.ReadWriteInPlace(temporalResolvedColor, RGTextureAccess::RenderTarget);
				data.m_Texture = temporalResolvedColor;
				builder.SideEffect();
			});
		const bool temporalResolvedInitializationCompiled = temporalResolvedInitializationGraph.Compile();
		context.Check(temporalResolvedInitializationCompiled,
			"TAA resolved-color initialization fixture compiles");
		if (temporalResolvedInitializationCompiled)
		{
			RGSnapshot temporalResolvedSnapshot;
			BuildRenderGraphSnapshot(temporalResolvedInitializationGraph, temporalResolvedSnapshot);
			const auto resource = std::ranges::find(temporalResolvedSnapshot.m_Resources,
				"TAA.ResolvedColorInitialization", &RGSnapshotResourceInfo::m_Name);
			const auto isTransition = [](const RGSnapshotPassInfo& pass, RHILayout before, RHILayout after) noexcept
			{
				return std::ranges::any_of(pass.m_PreBarriers,
					[before, after](const RGSnapshotBarrierInfo& barrier) noexcept
					{
						return barrier.m_ResourceName ==
							"TAA.ResolvedColorInitialization" &&
							barrier.m_Kind == RGBarrierKind::Transition &&
							barrier.m_Before.m_Layout == before &&
							barrier.m_After.m_Layout == after;
					});
			};
			context.Check(resource != temporalResolvedSnapshot.m_Resources.end() &&
				Test(static_cast<RHITextureUsage>(resource->m_UsageBits), RHITextureUsage::RenderTarget) &&
				Test(static_cast<RHITextureUsage>(resource->m_UsageBits), RHITextureUsage::UnorderedAccess),
				"TAA resolved color preserves its mixed RTV/UAV usage contract");
			context.Check(temporalResolvedSnapshot.m_Passes.size() == 3 &&
				isTransition(temporalResolvedSnapshot.m_Passes[0], RHILayout::Undefined, RHILayout::RenderTarget),
				"TAA resolved color initializes through a first-use render-target transition");
			context.Check(temporalResolvedSnapshot.m_Passes.size() == 3 &&
				isTransition(temporalResolvedSnapshot.m_Passes[1], RHILayout::RenderTarget, RHILayout::UnorderedAccess),
				"TAA resolve transitions initialized color from RTV to UAV");
			context.Check(temporalResolvedSnapshot.m_Passes.size() == 3 &&
				isTransition(temporalResolvedSnapshot.m_Passes[2], RHILayout::UnorderedAccess, RHILayout::RenderTarget),
				"Transparent load transitions resolved color back from UAV to RTV");
		}

		RecordingGraphicsCommandContext graphicsContext;
		RecordingComputeCommandContext directComputeContext;
		uint32_t graphicsExecutions = 0;
		uint32_t computeExecutions = 0;
		uint32_t copyExecutions = 0;
		uint32_t culledComputeExecutions = 0;
		bool graphicsContextSelected = false;
		bool directComputeContextSelected = false;
		bool copyContextSelected = false;
		bool copyResourcesResolved = false;
		bool asyncComputeRemainsUnavailable = false;
		RecordingDevice encoderDevice;
		RenderGraph encoderGraph({
			.m_Device = &encoderDevice,
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		encoderGraph.AddTrivialSideEffectPass("GraphicsEncoder",
			[&](RGExecuteContext& executeContext)
			{
				++graphicsExecutions;
				auto* selectedContext = executeContext.GetGraphicsCommandContext();
				graphicsContextSelected = selectedContext == &graphicsContext;
				const RHIRenderingAttachment colorAttachment{
					.m_View = RHITextureViewHandle{ 31, 1 },
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				selectedContext->BeginRendering({ .m_ColorAttachments = std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
			});
		encoderGraph.AddTrivialSideEffectPass("ComputeEncoder", RGPassEncoderType::Compute,
			[&](RGExecuteContext& executeContext)
			{
				++computeExecutions;
				directComputeContextSelected = executeContext.GetDirectComputeCommandContext() == &directComputeContext;
				asyncComputeRemainsUnavailable = executeContext.GetAsyncComputeCommandContext() == nullptr;
			});
		encoderGraph.AddPass<DualStorageAccessPassData>(
			"CopyEncoder", RGPassEncoderType::Copy,
			[](RenderGraph::RGBuilder& builder, DualStorageAccessPassData& data)
			{
				const RHIBufferDesc copyBufferDesc{
					.m_SizeInBytes = 64,
					.m_Usage = RHIBufferUsage::CopySource | RHIBufferUsage::CopyDest,
				};
				data.m_FirstBuffer = builder.ImportBuffer("CopySource", RHIBufferHandle{ 21, 1 },
					copyBufferDesc, RGBufferAccess::CopySource, RGContentValidity::Defined);
				data.m_SecondBuffer = builder.ImportBuffer("CopyDestination",
					RHIBufferHandle{ 22, 1 }, copyBufferDesc, RGBufferAccess::CopyDest,
					RGContentValidity::Undefined);
				data.m_FirstBuffer = builder.Read(data.m_FirstBuffer, RGBufferAccess::CopySource, RHIStage::Copy);
				builder.WriteInPlace(data.m_SecondBuffer, RGBufferAccess::CopyDest, RHIStage::Copy);
				builder.SideEffect();
			},
			[&](RGExecuteContext& executeContext, DualStorageAccessPassData& data)
			{
				++copyExecutions;
				auto* copyContext = executeContext.GetCopyCommandContext();
				copyContextSelected = copyContext == &graphicsContext;
				const RHIBufferHandle source = executeContext.GetBufferHandle(data.m_FirstBuffer);
				const RHIBufferHandle destination = executeContext.GetBufferHandle(data.m_SecondBuffer);
				copyResourcesResolved =
					source == RHIBufferHandle{ 21, 1 } && destination == RHIBufferHandle{ 22, 1 };
				if (copyContext)
				{
					copyContext->CopyBuffer(destination, 4, source, 8, 16);
				}
			});
		encoderGraph.AddPass<StorageAccessPassData>(
			"CulledComputeEncoder", RGPassEncoderType::Compute,
			[](RenderGraph::RGBuilder&, StorageAccessPassData&) {},
			[&](RGExecuteContext&, StorageAccessPassData&) { ++culledComputeExecutions; });
		const bool encoderGraphCompiled = encoderGraph.Compile();
		context.Check(encoderGraphCompiled, "RenderGraph pass encoder fixture compiles");
		if (encoderGraphCompiled)
		{
			RGSnapshot encoderSnapshot;
			BuildRenderGraphSnapshot(encoderGraph, encoderSnapshot);
			context.Check(
				encoderSnapshot.m_Passes.size() == 4 &&
				encoderSnapshot.m_Passes[0].m_EncoderType == RGPassEncoderType::Graphics &&
				encoderSnapshot.m_Passes[1].m_EncoderType == RGPassEncoderType::Compute &&
				encoderSnapshot.m_Passes[2].m_EncoderType == RGPassEncoderType::Copy &&
				encoderSnapshot.m_Passes[3].m_EncoderType == RGPassEncoderType::Compute &&
				encoderSnapshot.m_Passes[3].m_Culled,
				"RenderGraph snapshots preserve encoder metadata for live and culled passes");

			RGDeferredCommitList executeContextCommits;
			RGExecuteContext executeContext(executeContextCommits, {
				.m_GraphicsCommandContext = &graphicsContext,
				.m_DirectComputeCommandContext = &directComputeContext,
				.m_AsyncComputeCommandContext = nullptr,
			});
			encoderGraph.Execute(executeContext);
			executeContextCommits.Discard();
			context.Check(graphicsExecutions == 1 && computeExecutions == 1 &&
				copyExecutions == 1 && culledComputeExecutions == 0 &&
				graphicsContextSelected && directComputeContextSelected &&
				copyContextSelected && copyResourcesResolved &&
				graphicsContext.m_CopyBufferCount == 1 &&
				graphicsContext.m_BeginRenderingCount == 1 &&
				graphicsContext.m_EndRenderingCount == 1 &&
				!graphicsContext.IsRendering() &&
				asyncComputeRemainsUnavailable,
				"RenderGraph closes graphics rendering before the next direct-list encoder");
			context.Check(graphicsContext.m_BeginProfileCount == 2 &&
				graphicsContext.m_EndProfileCount == 2 &&
				directComputeContext.m_BeginProfileCount == 1 &&
				directComputeContext.m_EndProfileCount == 1,
				"RenderGraph profiles Graphics, Compute, and Copy on their selected command contexts");
			context.Check(graphicsContext.m_Profile.m_Samples.size() == 2 &&
				directComputeContext.m_Profile.m_Samples.size() == 1 &&
				directComputeContext.m_Profile.m_Samples[0].m_Kind == GpuProfileScopeKind::RenderGraphPass &&
				directComputeContext.m_Profile.m_Samples[0].m_Name == encoderSnapshot.m_Passes[1].m_Name &&
				directComputeContext.m_Profile.m_Samples[0].m_Occurrence == encoderSnapshot.m_Passes[1].m_Occurrence,
				"Compute encoder routing preserves the graph's profiling identity");
		}

		RecordingGraphicsCommandContext batchingGraphicsContext;
		RecordingComputeCommandContext batchingComputeContext;
		RecordingDevice batchingDevice;
		RenderGraph barrierBatchingGraph({
			.m_Device = &batchingDevice,
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		barrierBatchingGraph.AddPass<BarrierBatchingPassData>(
			"BatchTextureAndBufferBarriers", RGPassEncoderType::Compute,
			[](RenderGraph::RGBuilder& builder, BarrierBatchingPassData& data)
			{
				const RHITextureDesc textureDesc = {
					.m_Format = RHIFormat::R8G8B8A8Unorm,
					.m_Extent = { .m_Width = 4, .m_Height = 4, .m_Depth = 1 },
				};
				const RHIBufferDesc bufferDesc = {
					.m_SizeInBytes = 64,
					.m_StrideInBytes = 16,
				};
				data.m_Texture = builder.ImportTexture("BatchingTexture",
					RHITextureHandle{ 1, 1 }, textureDesc, RGTextureAccess::CopyDest,
					RGContentValidity::Defined);
				data.m_Buffer = builder.ImportBuffer("BatchingBuffer", RHIBufferHandle{ 1, 1 },
					bufferDesc, RGBufferAccess::CopyDest, RGContentValidity::Defined);
				data.m_Texture = builder.Read(data.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_Buffer = builder.Read(data.m_Buffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
				builder.Export(data.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
				builder.Export(data.m_Buffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
				builder.SideEffect();
			},
			[](RGExecuteContext&, BarrierBatchingPassData&) {});
		const bool barrierBatchingCompiled = barrierBatchingGraph.Compile();
		context.Check(barrierBatchingCompiled, "RenderGraph barrier batching fixture compiles");
		if (barrierBatchingCompiled)
		{
			RGDeferredCommitList batchingExecuteContextCommits;
			RGExecuteContext batchingExecuteContext(batchingExecuteContextCommits, {
				.m_GraphicsCommandContext = &batchingGraphicsContext,
				.m_DirectComputeCommandContext = &batchingComputeContext,
				.m_AsyncComputeCommandContext = nullptr,
			});
			barrierBatchingGraph.Execute(batchingExecuteContext);
			batchingExecuteContextCommits.Discard();
			context.Check(batchingComputeContext.m_TextureBarrierCount == 1 &&
				batchingComputeContext.m_BufferBarrierCount == 1 &&
				batchingComputeContext.m_FlushBarrierCount == 1,
				"RenderGraph batches texture and buffer barriers into one explicit flush");
		}
	}

	void RunGraphObjectLifetimeTests(SelfTestContext& context) noexcept
	{
		struct Data
		{
			Data() = default;
			GGLAB_DELETE_COPYABLE_MOVABLE(Data);
			~Data() noexcept
			{
				if (m_Destroyed)
				{
					++*m_Destroyed;
				}
			}
			Data* m_Self = this;
			uint32_t* m_Destroyed = nullptr;
			uint32_t m_Index = 0;
			std::string m_Text = "Stable graph pass payload";
			std::unique_ptr<uint32_t> m_Owner = std::make_unique<uint32_t>(42);
			std::array<std::byte, 20u << 10> m_Padding{};
		};
		// No compile, failed compile, recorded/discarded and recorded/submitted scopes.
		for (uint32_t mode = 0; mode < 4; ++mode)
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			TransientResourcePool pool(&device);
			RecordingGraphicsCommandContext graphics;
			RecordingComputeCommandContext commands;
			uint32_t destroyed = 0;
			uint32_t executed = 0;
			uint32_t committed = 0;
			bool stable = true;
			{
				RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool,
					.m_ObjectArena = { .m_RegularBlockPayloadBytes = 512 } });
				// These borrowers/commits must settle before the graph's payloads end.
				RGDeferredCommitList commits;
				RGExecuteContext execution(commits, { &graphics, &commands, nullptr });
				RGBufferId buffer;
				std::vector<Data*> addresses;
				for (uint32_t index = 0; index < 64; ++index)
				{
					auto* pass = graph.AddPass<Data>("Arena.Live", RGPassEncoderType::Compute,
						[&](RenderGraph::RGBuilder& builder, Data& data)
						{
							data.m_Index = index;
							data.m_Destroyed = &destroyed;
							if (index == 0)
							{
								buffer = builder.CreateBuffer("Arena.Buffer", { .m_SizeInBytes = 64 });
								if (mode == 1)
								{
									builder.Read(buffer, RGBufferAccess::StorageRead);
								}
								else
								{
									builder.WriteInPlace(buffer, RGBufferAccess::StorageWrite);
								}
							}
							else
							{
								builder.ReadWriteInPlace(buffer, RGBufferAccess::StorageReadWrite);
							}
							builder.SideEffect();
						},
						[&, owner = std::make_unique<uint32_t>(17)](RGExecuteContext& execute, Data& data)
						{
							stable = stable && data.m_Self == &data && data.m_Index == executed &&
								data.m_Text == "Stable graph pass payload" && *data.m_Owner == 42 && *owner == 17;
							++executed;
							execute.DeferCommit([&committed](const RGCommitContext&) noexcept { ++committed; });
						});
					addresses.push_back(&pass->GetData());
				}
				graph.AddPass<Data>("Arena.Culled", RGPassEncoderType::Compute,
					[&](RenderGraph::RGBuilder& builder, Data& data)
					{
						data.m_Destroyed = &destroyed;
						GGLAB_UNUSED(builder.Write(builder.CreateBuffer("Arena.Dead", { .m_SizeInBytes = 64 }),
							RGBufferAccess::StorageWrite));
					},
					[&](RGExecuteContext&, Data&) { stable = false; });
				auto& blackboard = graph.GetBlackboard().Create<std::string>();
				blackboard = "Stable blackboard payload";
				const auto* blackboardAddress = &blackboard;
				context.Check(graph.GetObjectArenaStatistics().m_ObjectBytes > (1u << 20),
					"Real graph fixture exceeds the previous arena growth threshold");
				if (mode != 0)
				{
					const bool compiled = graph.Compile();
					context.Check(compiled == (mode != 1), "Arena lifetime fixture preserves compile success/failure");
					if (compiled)
					{
						const auto originalPasses = graph.GetExecutionPlan()->GetPasses();
						const auto* originalResource = graph.GetExecutionPlan()->GetResources()[0].m_Resource;
						for (uint32_t index = 0; index < 128; ++index)
						{
							const std::string name = "Arena.Late." + std::to_string(index);
							graph.GetBlackboard().Create<std::array<std::byte, 1024>>(name.c_str());
						}
						const auto target = graph.ResolveObservation({ .m_Resource = "Arena.Buffer" });
						const auto dead = graph.ResolveObservation({ .m_Resource = "Arena.Dead" });
						const auto readback = device.CreateBuffer({ .m_SizeInBytes = 64,
							.m_Usage = RHIBufferUsage::CopyDest, .m_MemoryUsage = RHIMemoryUsage::GpuToCpu }, {});
						const std::array copies{ RGObservationCopy{ .m_Target = target,
							.m_Readback = readback, .m_ReadbackBytes = 64 } };
						const bool attached = graph.AttachObservationCopies(copies);
						bool originalPlanStable = attached && dead.m_Failure == RGObservationFailure::Culled &&
							graph.GetExecutionPlan()->GetResources()[0].m_Resource == originalResource;
						for (size_t index = 0; index < originalPasses.size(); ++index)
						{
							const auto& before = originalPasses[index];
							const auto& after = graph.GetExecutionPlan()->GetPasses()[index];
							originalPlanStable = originalPlanStable && before.m_Executor == after.m_Executor &&
								before.m_Culled == after.m_Culled && before.m_ExecutionOrder == after.m_ExecutionOrder &&
								before.m_PreBarriers.size() == after.m_PreBarriers.size();
							for (size_t barrier = 0; barrier < before.m_PreBarriers.size(); ++barrier)
							{
								if (barrier >= after.m_PreBarriers.size())
								{
									break;
								}
								const auto& original = before.m_PreBarriers[barrier];
								const auto& retained = after.m_PreBarriers[barrier];
								originalPlanStable = originalPlanStable && original.m_Resource == retained.m_Resource &&
									original.m_Kind == retained.m_Kind && original.m_Reason == retained.m_Reason &&
									original.m_Before == retained.m_Before && original.m_After == retained.m_After &&
									original.m_Subresources == retained.m_Subresources;
							}
						}
						context.Check(originalPlanStable,
							"Late allocations and observation attachment preserve original compiled borrows and liveness");
						graph.Execute(execution);
						commits.Seal();
						if (mode == 3)
						{
							const RHIFencePoint fence{ RHIFenceHandle{ 1, 1 }, 1 };
							commits.CommitAll({ .m_FrameSerial = 1, .m_Fence = fence });
							graph.Retire(fence);
						}
						else
						{
							commits.Discard();
							graph.Retire({});
						}
						context.Check(executed == 64 && committed == (mode == 3 ? 64u : 0u) && destroyed == 0,
							"Execution, culling, transient release and commit/discard settlement keep CPU payloads alive");
						device.DestroyBuffer(readback);
					}
				}
				for (uint32_t index = 0; index < addresses.size(); ++index)
				{
					stable = stable && addresses[index]->m_Self == addresses[index] &&
						addresses[index]->m_Index == index;
				}
				context.Check(stable && &graph.GetBlackboard().Get<std::string>() == blackboardAddress &&
					blackboard == "Stable blackboard payload" && destroyed == 0,
					"Pass and blackboard addresses survive graph processing until graph destruction");
			}
			context.Check(destroyed == 65, "Early exit and settlement destroy live and culled pass data exactly once");
			pool.Tick();
		}
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingGraphSelfTests(SelfTestContext& context) noexcept
	{
		RunGraphObjectLifetimeTests(context);
		RunAccelerationStructureGraphTests(context);
		RunPartialTextureProducerTests(context);
		RunGTAORenderGraphDataflowTests(context);
		RunRenderGraphAccessAndBarrierContractTests(context);
	}

	void RunRenderGraphObjectStorageBenchmark() noexcept
	{
		struct Data
		{
			std::array<std::byte, 128> m_Bytes{};
			std::string m_Text = "Graph-owned non-trivial benchmark payload";
			std::vector<uint32_t> m_Values{ 1, 2, 3, 4 };
		};
		RecordingDevice device;
		TransientResourcePool pool(&device);
		for (const uint32_t passCount : { 32u, 2048u })
		{
			const uint32_t iterations = passCount == 32 ? 1000 : 100;
			double buildMicroseconds = 0;
			double destroyMicroseconds = 0;
			RGObjectArena::Statistics statistics;
			for (uint32_t iteration = 0; iteration < iterations; ++iteration)
			{
				const auto begin = std::chrono::steady_clock::now();
				auto graph = std::make_unique<RenderGraph>(RenderGraph::CreateInfo{
					.m_Device = &device, .m_TransientResourcePool = &pool });
				graph->GetBlackboard().Create<std::string>() = "benchmark";
				for (uint32_t index = 0; index < passCount; ++index)
				{
					auto execute = [](RGExecuteContext&, Data&) noexcept {};
					graph->AddPass<Data>("ArenaBenchmark.Pass", RGPassEncoderType::Compute,
						[](RenderGraph::RGBuilder& builder, Data&) { builder.SideEffect(); },
						decltype(execute){});
				}
				const auto built = std::chrono::steady_clock::now();
				statistics = graph->GetObjectArenaStatistics();
				graph.reset();
				const auto destroyed = std::chrono::steady_clock::now();
				buildMicroseconds += std::chrono::duration<double, std::micro>(built - begin).count();
				destroyMicroseconds += std::chrono::duration<double, std::micro>(destroyed - built).count();
			}
			std::printf("rg-object-storage passes=%u iterations=%u build-us=%.3f destroy-us=%.3f "
				"object-bytes=%zu record-bytes=%zu payload-used=%zu payload-reserved=%zu "
				"block-overhead=%zu regular-blocks=%zu dedicated-blocks=%zu\n",
				passCount, iterations, buildMicroseconds / iterations, destroyMicroseconds / iterations,
				statistics.m_ObjectBytes, statistics.m_DestructorRecordBytes, statistics.m_UsedPayloadBytes,
				statistics.m_ReservedPayloadBytes, statistics.m_BlockOverheadBytes,
				statistics.m_RegularBlockCount, statistics.m_DedicatedBlockCount);
		}
	}
}
