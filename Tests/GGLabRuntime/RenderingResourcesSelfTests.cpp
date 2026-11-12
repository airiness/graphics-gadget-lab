#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "Diagnostics/Builders/TransientResourcePoolSnapshotBuilder.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/ArtifactCacheCore.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"
#include "GGLabRuntime/Graphics/RHI/RHIResourceDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHISampler.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"
#include "GGLabRuntime/Graphics/RHI/RHITransferContext.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGCompileDiagnostic.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResourceHandle.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResourceUtils.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraphTypes.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/TransferBatch.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12TextureSupportUtils.h"
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "Graphics/Resource/PersistentTexturePool.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>

namespace gglab::rendering_test
{
	void RunTextureFormatCapabilityTests(SelfTestContext& context) noexcept
	{
		RHITextureDesc desc{};
		desc.m_Format = RHIFormat::R8Unorm;
		desc.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::RenderTarget |
			RHITextureUsage::UnorderedAccess;
		const auto fullSupport1 = static_cast<D3D12_FORMAT_SUPPORT1>(
			D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
			D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
			D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW);
		const auto noTypedUavSupport1 = static_cast<D3D12_FORMAT_SUPPORT1>(fullSupport1 & ~D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW);
		const auto noShaderResourceSupport1 = static_cast<D3D12_FORMAT_SUPPORT1>(fullSupport1 & ~D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE);
		const auto noRenderTargetSupport1 = static_cast<D3D12_FORMAT_SUPPORT1>(fullSupport1 & ~D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
		constexpr auto TypedStoreSupport = static_cast<D3D12_FORMAT_SUPPORT2>(D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);

		RHITextureViewDesc viewDesc{
			.m_Type = RHITextureViewType::UnorderedAccess,
			.m_Dimension = RHITextureViewDimension::Texture2D,
			.m_Format = RHIFormat::R8Unorm,
		};
		context.Check(EvaluateD3D12TextureFormatSupport(desc, fullSupport1) == RHITextureSupportReason::None &&
			EvaluateD3D12TextureViewFormatSupport(desc, viewDesc, fullSupport1, TypedStoreSupport) == RHITextureSupportReason::None,
			"R8Unorm accepts the complete GTAO texture and typed-UAV capability set");
		context.Check(EvaluateD3D12TextureFormatSupport(desc, noTypedUavSupport1) == RHITextureSupportReason::TypedUnorderedAccessUnsupported &&
			EvaluateD3D12TextureViewFormatSupport(
				desc, viewDesc, noTypedUavSupport1, TypedStoreSupport) == RHITextureSupportReason::TypedUnorderedAccessUnsupported &&
			EvaluateD3D12TextureViewFormatSupport(
				desc, viewDesc, fullSupport1, D3D12_FORMAT_SUPPORT2_NONE) == RHITextureSupportReason::TypedUnorderedAccessStoreUnsupported,
			"R8Unorm capability queries distinguish typed-UAV and typed-store fallback causes");

		viewDesc.m_Type = RHITextureViewType::ShaderResource;
		context.Check(EvaluateD3D12TextureViewFormatSupport(
			desc, viewDesc, noShaderResourceSupport1, TypedStoreSupport) == RHITextureSupportReason::ShaderResourceUnsupported,
			"Single-channel SRV capability validation rejects formats without shader access");
		viewDesc.m_Type = RHITextureViewType::RenderTarget;
		context.Check(
			EvaluateD3D12TextureViewFormatSupport(desc, viewDesc, fullSupport1, TypedStoreSupport) == RHITextureSupportReason::None &&
			EvaluateD3D12TextureViewFormatSupport(
				desc, viewDesc, noRenderTargetSupport1, TypedStoreSupport) == RHITextureSupportReason::RenderTargetUnsupported,
			"Single-channel RTV capability validation requires render-target support");

		desc.m_Format = RHIFormat::R16Float;
		viewDesc.m_Format = RHIFormat::R16Float;
		viewDesc.m_Type = RHITextureViewType::UnorderedAccess;
		const bool r16Supported = EvaluateD3D12TextureFormatSupport(desc, fullSupport1) == RHITextureSupportReason::None &&
			EvaluateD3D12TextureViewFormatSupport(desc, viewDesc, fullSupport1, TypedStoreSupport) == RHITextureSupportReason::None;
		desc.m_Format = RHIFormat::R32Float;
		viewDesc.m_Format = RHIFormat::R32Float;
		context.Check(r16Supported &&
			EvaluateD3D12TextureFormatSupport(desc, fullSupport1) == RHITextureSupportReason::None &&
			EvaluateD3D12TextureViewFormatSupport(desc, viewDesc, fullSupport1, TypedStoreSupport) == RHITextureSupportReason::None,
			"R16Float and existing R32Float use the same typed-UAV capability contract");
		context.Check(RHITextureSupportReasonText(RHITextureSupportReason::TypedUnorderedAccessStoreUnsupported) ==
			"typed unordered-access store unsupported",
			"Texture capability failures expose a stable diagnostic reason");
		context.Check(
			RHITextureSupportReasonForUsage(RHITextureUsage::Sampled) == RHITextureSupportReason::ShaderResourceUnsupported &&
			RHITextureSupportReasonForUsage(RHITextureUsage::CopyDest) == RHITextureSupportReason::CopyDestUnsupported &&
			RHITextureSupportReasonForUsage(RHITextureUsage::None) == RHITextureSupportReason::FormatCombinationUnsupported,
			"Texture usage failures map to backend-neutral support reasons");
	}

	void RunCompiledResourceStatusTests(SelfTestContext& context) noexcept
	{
		struct FixturePassData {};
		struct ResourceHandleFixture : RGResourceHandle
		{
			ResourceHandleFixture(Handle handle, Version version) noexcept :
				RGResourceHandle(handle, version)
			{
			}
		};
		RenderGraph graph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		const RHITextureDesc textureDesc{
			.m_Format = RHIFormat::R16Float, .m_Extent = { 8, 8, 1 },
		};
		const RHIBufferDesc bufferDesc{ .m_SizeInBytes = 64, .m_StrideInBytes = 16 };
		RGTextureId earlierTexture;
		RGBufferId earlierBuffer;
		RGTextureId retainedTexture;
		RGBufferId retainedBuffer;
		RGTextureId culledTexture;
		RGBufferId culledBuffer;
		graph.AddPass<FixturePassData>("ResourceStatus.EarlierWriters", RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, FixturePassData&)
			{
				earlierTexture = builder.Write(builder.CreateTexture("Status.RetainedTexture", textureDesc),
					RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				earlierBuffer = builder.Write(builder.CreateBuffer("Status.RetainedBuffer", bufferDesc),
					RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
			});
		graph.AddPass<FixturePassData>("ResourceStatus.RetainedWriters", RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, FixturePassData&)
			{
				retainedTexture = builder.Write(earlierTexture, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				retainedBuffer = builder.Write(earlierBuffer, RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
				builder.SideEffect();
			});
		graph.AddPass<FixturePassData>("ResourceStatus.CulledWriters", RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, FixturePassData&)
			{
				culledTexture = builder.Write(builder.CreateTexture("Status.CulledTexture", textureDesc),
					RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				culledBuffer = builder.Write(builder.CreateBuffer("Status.CulledBuffer", bufferDesc),
					RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
			});
		const RenderGraph& query = graph;
		context.Check(query.GetCompiledResourceStatus(retainedTexture) == RGCompiledResourceStatus::Unavailable &&
			query.GetCompiledResourceStatus(retainedBuffer) == RGCompiledResourceStatus::Unavailable,
			"Resource usage is unavailable before compilation even when retained writes are declared");
		context.Check(graph.Compile(), "Compiled resource status fixture compiles without GPU execution");
		context.Check(query.GetCompiledResourceStatus(retainedTexture) == RGCompiledResourceStatus::Referenced &&
			query.GetCompiledResourceStatus(retainedBuffer) == RGCompiledResourceStatus::Referenced &&
			query.GetCompiledResourceStatus(culledTexture) == RGCompiledResourceStatus::Culled &&
			query.GetCompiledResourceStatus(culledBuffer) == RGCompiledResourceStatus::Culled,
			"Texture and buffer queries distinguish retained accesses from culled declarations");
		RGSnapshot snapshot;
		BuildRenderGraphSnapshot(graph, snapshot);
		context.Check(snapshot.m_Passes.size() == 3 && snapshot.m_Passes[0].m_Culled &&
			!snapshot.m_Passes[1].m_Culled && snapshot.m_Passes[2].m_Culled &&
			earlierTexture.GetVersion() < retainedTexture.GetVersion() &&
			earlierBuffer.GetVersion() < retainedBuffer.GetVersion() &&
			query.GetCompiledResourceStatus(earlierTexture) == RGCompiledResourceStatus::Referenced &&
			query.GetCompiledResourceStatus(earlierBuffer) == RGCompiledResourceStatus::Referenced,
			"Valid logical versions share underlying-resource usage even when their earlier writers are culled");
		const ResourceHandleFixture outOfRange(RGResourceHandle::Handle{ uint16_t{ 65534 } }, 1);
		const ResourceHandleFixture uninitializedVersion(retainedTexture.GetHandle(), RGResourceHandle::UnintializedVersion);
		const ResourceHandleFixture futureVersion(retainedBuffer.GetHandle(),
			static_cast<RGResourceHandle::Version>(retainedBuffer.GetVersion() + 1));
		context.Check(query.GetCompiledResourceStatus(RGTextureId{}) == RGCompiledResourceStatus::Unavailable &&
			query.GetCompiledResourceStatus(RGBufferId{}) == RGCompiledResourceStatus::Unavailable &&
			query.GetCompiledResourceStatus(outOfRange) == RGCompiledResourceStatus::Unavailable &&
			query.GetCompiledResourceStatus(uninitializedVersion) == RGCompiledResourceStatus::Unavailable &&
			query.GetCompiledResourceStatus(futureVersion) == RGCompiledResourceStatus::Unavailable,
			"Resource queries reject invalid slots, uninitialized versions and versions beyond this graph");
		RGTextureId undefinedTexture;
		graph.AddPass<FixturePassData>("ResourceStatus.UndefinedRead", RGPassEncoderType::Compute,
			[&](RenderGraph::RGBuilder& builder, FixturePassData&)
			{
				undefinedTexture = builder.CreateTexture("Status.UndefinedTexture", textureDesc);
				builder.Read(undefinedTexture, RGTextureAccess::Sample, RHIStage::ComputeShader);
				builder.SideEffect();
			});
		context.Check(query.GetCompiledResourceStatus(undefinedTexture) == RGCompiledResourceStatus::Unavailable,
			"Resources absent from the current compiled plan cannot acquire another resource's usage status");
		context.Check(!graph.Compile() && !graph.GetExecutionPlan() &&
			query.GetCompiledResourceStatus(retainedTexture) == RGCompiledResourceStatus::Unavailable &&
			query.GetCompiledResourceStatus(retainedBuffer) == RGCompiledResourceStatus::Unavailable &&
			query.GetCompiledResourceStatus(culledTexture) == RGCompiledResourceStatus::Unavailable &&
			query.GetCompiledResourceStatus(culledBuffer) == RGCompiledResourceStatus::Unavailable,
			"Failed recompilation clears previously compiled usage instead of retaining stale resource activity");
	}

	void RunPersistentTexturePoolContractTests(SelfTestContext& context) noexcept
	{
		static_assert(!std::is_copy_constructible_v<PersistentTextureAllocation>);
		static_assert(!std::is_copy_assignable_v<PersistentTextureAllocation>);
		static_assert(std::is_nothrow_move_constructible_v<PersistentTextureAllocation>);
		static_assert(std::is_nothrow_move_assignable_v<PersistentTextureAllocation>);

		RecordingDevice device;
		device.m_UseControlledFenceCompletion = true;
		PersistentTexturePool pool(&device);

		auto makeCreateInfo = [](RHIFormat format, uint32_t width, uint32_t height) noexcept
		{
			return RHIOwnedTextureCreateInfo{
				.m_Desc = {
					.m_Format = format,
					.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
					.m_Extent = { width, height, 1 },
				},
			};
		};
		auto acquireHistorySet = [&](uint32_t width, uint32_t height)
		{
			const RHIOwnedTextureCreateInfo colorInfo = makeCreateInfo(RHIFormat::R16G16B16A16Float, width, height);
			const RHIOwnedTextureCreateInfo depthInfo = makeCreateInfo(RHIFormat::R32Float, width, height);
			return std::array<PersistentTextureAllocation, 4>{
				pool.AcquireTexture(colorInfo, "TAA.HistoryColor0"),
					pool.AcquireTexture(colorInfo, "TAA.HistoryColor1"),
					pool.AcquireTexture(depthInfo, "TAA.HistoryDepth0"),
					pool.AcquireTexture(depthInfo, "TAA.HistoryDepth1"),
			};
		};
		auto releaseHistorySet = [&](auto& allocations, const RHIFencePoint& fencePoint)
		{
			bool released = true;
			for (PersistentTextureAllocation& allocation : allocations)
			{
				released = pool.ReleaseTexture(std::move(allocation), fencePoint) && released;
			}
			return released;
		};
		auto allPendingAtFence = [](const PersistentTexturePoolDiagnostics& diagnostics, uint64_t fenceValue) noexcept
		{
			return std::ranges::all_of(diagnostics.m_PendingRetirements,
				[fenceValue](const PersistentTexturePendingRetirementDiagnostics& pending)
				{
					return pending.m_Texture.IsValid() &&
						pending.m_FencePoint.m_Value == fenceValue &&
						!pending.m_LogicalName.empty();
				});
		};

		const auto invalidAllocation = pool.AcquireTexture({}, "Invalid");
		auto firstGeneration = acquireHistorySet(1920, 1080);
		const uint64_t fullSizeBytes =
			2 * EstimatePersistentTextureBytes(firstGeneration[0].GetCreateInfo().m_Desc) +
			2 * EstimatePersistentTextureBytes(firstGeneration[2].GetCreateInfo().m_Desc);
		PersistentTextureAllocation movedAllocation = std::move(firstGeneration[0]);
		const bool moveTransferredOwnership = !firstGeneration[0].IsValid() && movedAllocation.IsValid();
		firstGeneration[0] = std::move(movedAllocation);
		const PersistentTexturePoolDiagnostics initialDiagnostics = pool.GetDiagnostics();
		context.Check(!invalidAllocation.IsValid() && moveTransferredOwnership &&
			firstGeneration[0].IsValid() && !movedAllocation.IsValid() &&
			initialDiagnostics.m_ActiveTextureCount == 4 &&
			initialDiagnostics.m_PendingRetirementTextureCount == 0 &&
			initialDiagnostics.m_EstimatedActiveBytes == fullSizeBytes &&
			device.m_CreateTextureCount == 4,
			"Persistent texture allocations are explicit, move-only, and byte-accounted");

		const bool invalidFenceRejected = !pool.ReleaseTexture(std::move(firstGeneration[0]), {});
		const RHIFencePoint firstFence{ RHIFenceHandle{ 1, 1 }, 10 };
		const bool firstReleased = releaseHistorySet(firstGeneration, firstFence);
		const PersistentTexturePoolDiagnostics firstPending = pool.GetDiagnostics();
		pool.Tick();
		const PersistentTexturePoolDiagnostics beforeFirstCompletion = pool.GetDiagnostics();
		context.Check(invalidFenceRejected && firstReleased &&
			firstPending.m_ActiveTextureCount == 0 &&
			firstPending.m_PendingRetirementTextureCount == 4 &&
			firstPending.m_EstimatedPendingRetirementBytes == fullSizeBytes &&
			allPendingAtFence(firstPending, 10) &&
			beforeFirstCompletion.m_PendingRetirementTextureCount == 4 &&
			device.m_DestroyTextureCount == 0 && device.m_RecordTextureUseCount == 4,
			"Persistent texture release is logical immediately and physical only after its fence");

		auto resizedGeneration = acquireHistorySet(1280, 720);
		const uint64_t resizedBytes =
			2 * EstimatePersistentTextureBytes(resizedGeneration[0].GetCreateInfo().m_Desc) +
			2 * EstimatePersistentTextureBytes(resizedGeneration[2].GetCreateInfo().m_Desc);
		const PersistentTexturePoolDiagnostics resizePressure = pool.GetDiagnostics();
		const RHIFencePoint resizeFence{ RHIFenceHandle{ 1, 1 }, 20 };
		const bool resizedReleased = releaseHistorySet(resizedGeneration, resizeFence);
		auto reenabledGeneration = acquireHistorySet(1920, 1080);
		const PersistentTexturePoolDiagnostics togglePressure = pool.GetDiagnostics();
		context.Check(resizePressure.m_ActiveTextureCount == 4 &&
			resizePressure.m_PendingRetirementTextureCount == 4 &&
			resizePressure.m_EstimatedActiveBytes == resizedBytes &&
			resizePressure.m_EstimatedPendingRetirementBytes == fullSizeBytes &&
			resizedReleased && togglePressure.m_ActiveTextureCount == 4 &&
			togglePressure.m_PendingRetirementTextureCount == 8 &&
			togglePressure.m_TotalAcquireCount == 12 && device.m_CreateTextureCount == 12,
			"Resize and toggle pressure create fresh persistent textures while old generations retire");

		device.m_CompletedFenceValue = 10;
		pool.Tick();
		const PersistentTexturePoolDiagnostics partiallyRetired = pool.GetDiagnostics();
		const uint32_t partiallyDestroyedTextureCount = device.m_DestroyTextureCount;
		const RHIFencePoint reenabledFence{ RHIFenceHandle{ 1, 1 }, 30 };
		const bool reenabledReleased = releaseHistorySet(reenabledGeneration, reenabledFence);
		const bool doubleReleaseRejected = !pool.ReleaseTexture(std::move(reenabledGeneration[0]), reenabledFence);
		device.m_CompletedFenceValue = 30;
		pool.Tick();
		const PersistentTexturePoolDiagnostics fullyRetired = pool.GetDiagnostics();
		context.Check(partiallyRetired.m_ActiveTextureCount == 4 &&
			partiallyRetired.m_PendingRetirementTextureCount == 4 &&
			partiallyRetired.m_CompletedRetirementCount == 4 &&
			partiallyDestroyedTextureCount == 4 && reenabledReleased &&
			doubleReleaseRejected && fullyRetired.m_ActiveTextureCount == 0 &&
			fullyRetired.m_PendingRetirementTextureCount == 0 &&
			fullyRetired.m_TotalReleaseCount == 12 &&
			fullyRetired.m_CompletedRetirementCount == 12 &&
			fullyRetired.m_RejectedReleaseCount == 2 &&
			device.m_DestroyTextureCount == 12 &&
			device.m_RecordTextureUseCount == 12 &&
			device.m_LastRecordedFencePoint == reenabledFence,
			"Fence retirement drains only completed generations and rejects duplicate release");
	}

	void RunResourceStateAndPortabilityContractTests(SelfTestContext& context) noexcept
	{
		const RGPersistentTextureImportContract pendingTexture = ResolveRGPersistentTextureImportContract(false);
		const RGPersistentTextureImportContract initializedTexture = ResolveRGPersistentTextureImportContract(true);
		const RHIResourceState sampledState = ToRHIResourceState(RGTextureAccess::Sample, RHIStage::PixelShader);
		const RGPersistentTextureImportContract sampledTexture = ResolveRGPersistentTextureImportContract(true, sampledState);
		context.Check(pendingTexture.m_InitialState == UndefinedRHITextureState() &&
			pendingTexture.m_InitialContentValidity == RGContentValidity::Undefined &&
			initializedTexture.m_InitialState == CommonRHIResourceState() &&
			initializedTexture.m_InitialContentValidity == RGContentValidity::Defined &&
			sampledTexture.m_InitialState == sampledState &&
			sampledTexture.m_InitialContentValidity == RGContentValidity::Defined,
			"Persistent texture imports expose Undefined first use and their explicit terminal state thereafter");

		const RHIOwnedTextureCreateInfo ownedCreateInfo{};
		context.Check(ownedCreateInfo.m_InitialState == UndefinedRHITextureState(),
			"Owned texture create info defaults to Undefined");
		context.Check(IsRHIResourceStateValid(ownedCreateInfo.m_InitialState, RHIResourceStateUsage::TextureInitial),
			"Undefined is a valid owned texture initial state");
		context.Check(IsRHIResourceStateValid(ownedCreateInfo.m_InitialState, RHIResourceStateUsage::TextureBarrierBefore),
			"Undefined is a valid texture barrier source state");
		context.Check(!IsRHIResourceStateValid(ownedCreateInfo.m_InitialState, RHIResourceStateUsage::TextureBarrierAfter),
			"Undefined is rejected as a texture barrier destination state");
		context.Check(!IsRHIResourceStateValid(
			RHIResourceState{ .m_Layout = RHILayout::Unknown },
			RHIResourceStateUsage::TextureInitial),
			"Owned textures default to initial-only Undefined while Unknown remains invalid");
		context.Check(IsRHIResourceStateValid(PresentRHITextureState(), RHIResourceStateUsage::TextureBarrierAfter) &&
			!IsRHIResourceStateValid(PresentRHITextureState(), RHIResourceStateUsage::Buffer),
			"Present is an exact texture-only special state");
		context.Check(ToD3D12BarrierLayout(RHILayout::Undefined) == D3D12_BARRIER_LAYOUT_UNDEFINED &&
			ToD3D12TextureInitialLayout(RHILayout::Undefined) == D3D12_BARRIER_LAYOUT_COMMON &&
			ToD3D12ResourceStates(UndefinedRHITextureState()) == D3D12_RESOURCE_STATE_COMMON,
			"DX12 preserves Undefined for discard barriers while owned textures start in COMMON");

		D3D12_RESOURCE_DESC discardResourceDesc{};
		discardResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		discardResourceDesc.Width = 8;
		discardResourceDesc.Height = 8;
		discardResourceDesc.DepthOrArraySize = 1;
		discardResourceDesc.MipLevels = 1;
		discardResourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		discardResourceDesc.SampleDesc = { 1, 0 };
		const RHIResourceState storageWriteState{
			.m_Stages = RHIStage::ComputeShader,
			.m_Access = RHIAccess::UnorderedAccess,
			.m_Layout = RHILayout::UnorderedAccess,
		};
		const D3D12_TEXTURE_BARRIER discardBarrier = BuildD3D12TextureBarrier({
			.m_Texture = RHITextureHandle{ 1, 1 },
			.m_Before = UndefinedRHITextureState(),
			.m_After = storageWriteState,
			}, reinterpret_cast<ID3D12Resource*>(uintptr_t{ 0x1234 }), discardResourceDesc);
		context.Check(discardBarrier.LayoutBefore == D3D12_BARRIER_LAYOUT_UNDEFINED &&
			discardBarrier.LayoutAfter == D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS &&
			discardBarrier.Flags == D3D12_TEXTURE_BARRIER_FLAG_DISCARD,
			"DX12 first-use barriers discard stale pooled texture layouts and contents");

		const RHITextureDesc graphTextureDesc{
			.m_Format = RHIFormat::R8G8B8A8Unorm,
			.m_Extent = { 8, 8, 1 },
		};
		RenderGraph initializedGraph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		initializedGraph.AddPass<TextureStorageAccessPassData>("InitializeTexture",
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				data.m_Texture = builder.CreateTexture("UndefinedTexture", graphTextureDesc);
				builder.WriteInPlace(data.m_Texture, RGTextureAccess::StorageWrite);
				builder.SideEffect();
			});
		const bool initializedCompiled = initializedGraph.Compile();
		RGSnapshot initializedSnapshot;
		BuildRenderGraphSnapshot(initializedGraph, initializedSnapshot);
		context.Check(initializedCompiled && initializedSnapshot.m_Resources.size() == 1 &&
			initializedSnapshot.m_Resources[0].m_InitialBarrierState == UndefinedRHITextureState(),
			"RenderGraph-created textures begin in Undefined state and Write defines contents");
		const auto* initializedPlan = initializedGraph.GetExecutionPlan();
		const RHIResourceState initializedWriteState = ToRHIResourceState(RGTextureAccess::StorageWrite);
		context.Check(initializedCompiled && initializedPlan &&
			initializedPlan->GetPasses().size() == 1 &&
			initializedPlan->GetPasses()[0].m_PreBarriers.size() == 1 &&
			initializedPlan->GetPasses()[0].m_PreBarriers[0].m_Before == UndefinedRHITextureState() &&
			initializedPlan->GetPasses()[0].m_PreBarriers[0].m_After ==
			initializedWriteState && initializedPlan->GetPasses()[0].m_PostBarriers.empty(),
			"Discardable graph textures keep their terminal layout for the next Undefined acquire");

		// Release erases the live allocation. Snapshot execution evidence must survive it.
		{
			RecordingDevice snapshotDevice;
			TransientResourcePool snapshotPool(&snapshotDevice);
			RecordingComputeCommandContext snapshotCommands;
			RenderGraph snapshotGraph({ .m_Device = &snapshotDevice,
				.m_TransientResourcePool = &snapshotPool });
			RHITextureHandle executedTexture;
			snapshotGraph.AddPass<TextureStorageAccessPassData>("Snapshot.Write", RGPassEncoderType::Compute,
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					data.m_Texture = builder.CreateTexture("Snapshot.Texture", graphTextureDesc);
					builder.WriteInPlace(data.m_Texture, RGTextureAccess::StorageWrite);
					builder.SideEffect();
				},
				[&](RGExecuteContext& execute, TextureStorageAccessPassData& data)
				{
					executedTexture = execute.GetTextureHandle(data.m_Texture);
				});
			context.Check(snapshotGraph.Compile(), "Execution snapshot allocation fixture compiles");
			snapshotGraph.EnableExecutionRecord();
			RGDeferredCommitList executeCommits;
			RGExecuteContext execute(executeCommits, { .m_DirectComputeCommandContext = &snapshotCommands });
			snapshotGraph.Execute(execute);
			executeCommits.Discard();
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(snapshotGraph, snapshot);
			context.Check(snapshot.m_Execution.m_Recorded && snapshot.m_Execution.m_Allocations.size() == 1 &&
				snapshot.m_Execution.m_Allocations[0].m_PoolSlot.IsValid() &&
				snapshot.m_Execution.m_Allocations[0].m_PhysicalHandleIndex == executedTexture.Index() &&
				snapshot.m_Execution.m_Allocations[0].m_PhysicalHandleGeneration == executedTexture.Generation() &&
				!snapshot.m_Resources[0].m_PoolSlot.IsValid() &&
				!snapshot.m_Execution.m_Barriers.empty() &&
				snapshot.m_Execution.m_Barriers[0].m_Barrier.m_PhysicalHandleIndex == executedTexture.Index(),
				"Graph execution records actual pool slots and resolved barriers before allocation release");
			snapshotGraph.Retire({});
			snapshotPool.Tick();
		}

#if defined(NDEBUG)
		// Debug asserts on the violated live-handle invariant. Release must still
		// retain the failed barrier attempt, not hide it or reuse old resolution evidence.
		{
			RecordingDevice barrierDevice;
			TransientResourcePool barrierPool(&barrierDevice);
			RecordingComputeCommandContext commands;
			RenderGraph barrierGraph({ .m_Device = &barrierDevice, .m_TransientResourcePool = &barrierPool });
			RGTextureId imported;
			barrierGraph.AddPass<TextureStorageAccessPassData>("Snapshot.InvalidBarrier", RGPassEncoderType::Compute,
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					imported = builder.ImportTexture("Snapshot.Imported", RHITextureHandle{ 91, 1 },
						graphTextureDesc, RGTextureAccess::Sample, RGContentValidity::Defined);
					builder.WriteInPlace(imported, RGTextureAccess::StorageWrite);
					data.m_Texture = imported;
				});
			context.Check(barrierGraph.Compile(), "Failed barrier resolution fixture compiles");
			barrierGraph.EnableExecutionRecord();
			RGDeferredCommitList executeCommits;
			RGExecuteContext execute(executeCommits, { .m_DirectComputeCommandContext = &commands });
			barrierGraph.Execute(execute);
			executeCommits.Discard();
			auto first = barrierGraph.TakeExecutionRecord();
			context.Check(first && first->m_Barriers.size() == 2 &&
				std::ranges::all_of(first->m_Barriers, [](const auto& b) { return b.m_Barrier.m_HasPhysicalHandle; }),
				"Live imported barriers resolve before the failed-resolution retry");
			auto* resource = static_cast<RGVirtualResource<RGTextureResource>*>(barrierGraph.GetExecutionPlan()->GetResources()[0].m_Resource);
			resource->m_ImportedHandle = {};
			commands.m_TextureBarrierCount = 0;
			barrierGraph.EnableExecutionRecord();
			executeCommits = RGDeferredCommitList{};
			barrierGraph.Execute(execute);
			executeCommits.Discard();
			auto failed = barrierGraph.TakeExecutionRecord();
			context.Check(failed && failed->m_Recorded && failed->m_Barriers.size() == 2 &&
				commands.m_TextureBarrierCount == 0 &&
				std::ranges::none_of(failed->m_Barriers, [](const auto& b) { return b.m_Barrier.m_HasPhysicalHandle; }),
				"Release records unresolved pre/post barriers without stale physical handles or GPU commands");
		}
#endif

		RecordingDevice reuseDevice;
		TransientResourcePool reusePool(&reuseDevice);
		TransientResourcePoolSnapshot emptyPoolSnapshot;
		BuildTransientResourcePoolSnapshot(reusePool, emptyPoolSnapshot);
		context.Check(emptyPoolSnapshot.m_SourceAvailable &&
			emptyPoolSnapshot.m_TextureCounts.m_Total == 0 &&
			emptyPoolSnapshot.m_BufferCounts.m_Total == 0,
			"Transient pool diagnostics distinguish an available empty pool from no source");
		auto discardAllocation = reusePool.AcquireTexture(graphTextureDesc, "Discardable");
		const TransientResourcePoolSlot discardSlot = discardAllocation.m_PoolSlot;
		reusePool.RetireTexture(std::move(discardAllocation), {});
		reusePool.Tick();
		auto commonAllocation = reusePool.AcquireTexture(graphTextureDesc, "PreserveCommon",
			RHIResourceDebugBindingMode::Aliased,
			TransientTextureReuseMode::PreserveCommon);
		const TransientResourcePoolSlot commonSlot = commonAllocation.m_PoolSlot;
		reusePool.RetireTexture(std::move(commonAllocation), {});
		reusePool.Tick();
		auto reusedDiscardAllocation = reusePool.AcquireTexture(graphTextureDesc, "DiscardableAgain");
		context.Check(discardSlot.IsValid() && commonSlot.IsValid() &&
			discardSlot != commonSlot && reusedDiscardAllocation.m_PoolSlot == discardSlot,
			"Transient texture reuse keeps discardable and Common-preserving contracts isolated");

		auto hasUndefinedRead = [](const RenderGraph& graph) noexcept
		{
			return std::ranges::any_of(graph.GetCompileDiagnostics(),
				[](const RGCompileDiagnostic& diagnostic) noexcept
				{
					return diagnostic.m_Code == RGCompileDiagnosticCode::UndefinedContentRead;
				});
		};
		RenderGraph transientReadGraph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		transientReadGraph.AddPass<StorageAccessPassData>("ReadUndefinedBuffer",
			[](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
			{
				data.m_Buffer = builder.CreateBuffer("UndefinedBuffer");
				data.m_Buffer = builder.Read(data.m_Buffer, RGBufferAccess::StructuredRead);
				builder.SideEffect();
			});
		context.Check(!transientReadGraph.Compile() && hasUndefinedRead(transientReadGraph),
			"RenderGraph reports release-visible texture/buffer read-before-write diagnostics");

		{
			RecordingDevice recordingDevice;
			const RHITextureDesc importedDesc{
				.m_Format = RHIFormat::R8G8B8A8Unorm,
				.m_Usage = RHITextureUsage::Sampled,
				.m_Extent = { 8, 8, 1 },
			};
			auto buildImportedRead = [&](RGContentValidity validity)
			{
				auto graph = std::make_unique<RenderGraph>(RenderGraph::CreateInfo{
					.m_Device = &recordingDevice,
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
				});
				graph->AddPass<TextureStorageAccessPassData>("ReadImportedTexture",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
					{
						data.m_Texture = builder.ImportTexture("ImportedTexture",
							RHITextureHandle{ 9, 1 }, importedDesc, UndefinedRHITextureState(), validity);
						data.m_Texture = builder.Read(data.m_Texture, RGTextureAccess::Sample);
						builder.SideEffect();
					});
				return graph;
			};
			auto undefinedImportGraph = buildImportedRead(RGContentValidity::Undefined);
			auto definedImportGraph = buildImportedRead(RGContentValidity::Defined);
			context.Check(!undefinedImportGraph->Compile() && hasUndefinedRead(*undefinedImportGraph) &&
				definedImportGraph->Compile(),
				"Imported state and content validity are explicit, independent contracts");
		}

		RHITextureDesc cubeDesc{
			.m_Format = RHIFormat::R8G8B8A8Unorm,
			.m_Usage = RHITextureUsage::Sampled,
			.m_Extent = { 8, 8, 1 },
			.m_ArraySize = 6,
		};
		const RHITextureViewDesc cubeView{
			.m_Type = RHITextureViewType::ShaderResource,
			.m_Dimension = RHITextureViewDimension::TextureCube,
			.m_Subresources = { .m_ArraySliceCount = 6 },
		};
		context.Check(ValidateRHITextureViewDesc(cubeDesc, cubeView).m_Error == RHITextureValidationError::MissingCubeCompatible,
			"Cube views require an explicit CubeCompatible resource flag");
		cubeDesc.m_CreateFlags = RHITextureCreateFlags::CubeCompatible;
		context.Check(ValidateRHITextureViewDesc(cubeDesc, cubeView).IsValid(),
			"CubeCompatible resources accept complete cube views");

		const RHIPortabilityCapabilities vulkanCapabilities{};
		RHITextureViewDesc minLodView{};
		minLodView.m_ResourceMinLODClamp = 1.0f;
		RHISamplerDesc customBorderSampler{};
		customBorderSampler.m_AddressU = RHITextureAddressMode::Border;
		customBorderSampler.m_BorderColor[0] = 0.5f;
		RHIGraphicsPipelineDesc pipelineDesc{};
		context.Check(
			IsRHIPrimitiveTopologyCompatible(RHIPrimitiveTopologyType::Triangle, RHIPrimitiveTopology::TriangleList) &&
			!IsRHIPrimitiveTopologyCompatible(RHIPrimitiveTopologyType::Line, RHIPrimitiveTopology::TriangleList) &&
			!IsRHIPrimitiveTopologyCompatible(RHIPrimitiveTopologyType::Patch, RHIPrimitiveTopology::TriangleList),
			"Primitive topology compatibility is a backend-neutral pipeline contract");
		const RHIIndexBufferBinding indexBinding{
			.m_Offset = 16,
			.m_SizeInBytes = 24,
			.m_Format = RHIFormat::R32Uint,
		};
		RHIIndexBufferBinding misalignedBinding = indexBinding;
		misalignedBinding.m_Offset = 18;
		RHIIndexBufferBinding misalignedSizeBinding = indexBinding;
		misalignedSizeBinding.m_SizeInBytes = 22;
		RHIIndexBufferBinding unknownFormatBinding = indexBinding;
		unknownFormatBinding.m_Format = RHIFormat::Unknown;
		context.Check(
			IsRHIIndexBufferBindingRangeValid(indexBinding, 64) &&
			!IsRHIIndexBufferBindingRangeValid(misalignedBinding, 64) &&
			!IsRHIIndexBufferBindingRangeValid(misalignedSizeBinding, 64) &&
			!IsRHIIndexBufferBindingRangeValid(unknownFormatBinding, 64) &&
			!IsRHIIndexBufferBindingRangeValid(indexBinding, 32) &&
			IsRHIIndexBufferDrawRangeValid(indexBinding, 6, 0) &&
			IsRHIIndexBufferDrawRangeValid(indexBinding, 2, 4) &&
			!IsRHIIndexBufferDrawRangeValid(indexBinding, 3, 4) &&
			!IsRHIIndexBufferDrawRangeValid(indexBinding, 1, 6) &&
			!IsRHIIndexBufferDrawRangeValid(indexBinding, 1, UINT32_MAX),
			"Index buffer bindings and indexed draws stay within the declared RHI range");
		pipelineDesc.m_VertexInput.m_VertexBufferCount = 1;
		pipelineDesc.m_VertexInput.m_VertexBuffers[0].m_InputRate = RHIVertexInputRate::PerInstance;
		pipelineDesc.m_VertexInput.m_VertexBuffers[0].m_InstanceStepRate = 2;
		context.Check(
			ValidateRHITextureViewPortability(minLodView, vulkanCapabilities).m_Error ==
			RHIPortabilityValidationError::ImageViewMinLodUnsupported &&
			ValidateRHISamplerPortability(customBorderSampler, vulkanCapabilities).m_Error ==
			RHIPortabilityValidationError::CustomBorderColorUnsupported &&
			ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
			RHIPortabilityValidationError::InstanceDivisorUnsupported,
			"Vulkan profile rejects unsupported view, sampler, and instance-divisor semantics");
		pipelineDesc = {};
		pipelineDesc.m_Rasterizer.m_FillMode = RHIFillMode::Wireframe;
		const bool rejectsWireframe =
			ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
			RHIPortabilityValidationError::WireframeUnsupported;
		pipelineDesc = {};
		pipelineDesc.m_Rasterizer.m_DepthClipEnable = false;
		const bool rejectsDepthClamp =
			ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
			RHIPortabilityValidationError::DepthClampUnsupported;
		pipelineDesc = {};
		pipelineDesc.m_Rasterizer.m_DepthBiasClamp = 1.0f;
		const bool rejectsBiasClamp =
			ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
			RHIPortabilityValidationError::DepthBiasClampUnsupported;
		pipelineDesc = {};
		pipelineDesc.m_RenderTargetCount = 2;
		pipelineDesc.m_Blend.m_RenderTargets[1].m_BlendEnable = true;
		const bool rejectsIndependentBlend =
			ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
			RHIPortabilityValidationError::IndependentBlendUnsupported;
		pipelineDesc = {};
		pipelineDesc.m_SampleQuality = 1;
		const bool rejectsSampleQuality =
			ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
			RHIPortabilityValidationError::SampleQualityUnsupported;
		context.Check(rejectsWireframe && rejectsDepthClamp && rejectsBiasClamp &&
			rejectsIndependentBlend && rejectsSampleQuality,
			"Vulkan profile rejects unsupported rasterizer, blend, and sample-quality semantics");

		const RHIResourceState shaderReadState{
			.m_Stages = RHIStage::PixelShader,
			.m_Access = RHIAccess::ShaderResource,
			.m_Layout = RHILayout::ShaderResource,
		};

		{
			RecordingTransferContext transferContext;
			TransferBatch batch(transferContext);

			const RHITextureHandle uploadedTexture{ 3, 1 };
			const RHIBufferHandle uploadedBuffer{ 4, 1 };
			const uint32_t bufferData = 17;
			const bool transferRecorded = batch.UploadTexture(uploadedTexture, {}, UndefinedRHITextureState(), shaderReadState) &&
				batch.UploadBuffer(uploadedBuffer, 0, &bufferData, sizeof(bufferData)) &&
				batch.UploadBuffer(uploadedBuffer, sizeof(bufferData), &bufferData, sizeof(bufferData));
			const RHITransferSubmission submission = batch.Submit(false);
			context.Check(transferRecorded && transferContext.m_TextureBarriers.size() == 2 &&
				transferContext.m_BufferBarriers.size() == 4 &&
				transferContext.m_TextureBarriers[0].m_Before == UndefinedRHITextureState() &&
				transferContext.m_TextureBarriers[1].m_After == shaderReadState &&
				transferContext.m_BufferBarriers.front().m_Before.m_Layout == RHILayout::Common &&
				transferContext.m_BufferBarriers.front().m_After.m_Layout == RHILayout::CopyDest &&
				transferContext.m_BufferBarriers.back().m_After.m_Layout == RHILayout::Common &&
				submission.m_Completion.IsValid() && submission.m_Publications.size() == 2 &&
				submission.m_Publications[0].m_Type == RHIResourceType::Texture &&
				submission.m_Publications[0].m_PublishedState == shaderReadState &&
				submission.m_Publications[1].m_Type == RHIResourceType::Buffer,
				"Transfer submission publishes one terminal state per resource with its completion fence");
		}

		// --- Subresource-granular texture content validity ---
		const RHITextureDesc mipTextureDesc{
			.m_Format = RHIFormat::R8G8B8A8Unorm,
			.m_Extent = { 8, 8, 1 },
			.m_MipLevels = 2,
		};
		const RHISubresourceRange colorMip0{
			.m_MipCount = 1,
			.m_Aspects = RHITextureAspect::Color,
		};
		const RHISubresourceRange colorMip1{
			.m_BaseMip = 1,
			.m_MipCount = 1,
			.m_Aspects = RHITextureAspect::Color,
		};

		{
			RenderGraph mipReadGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			RGTextureId mipTextureId{};
			mipReadGraph.AddPass<TextureStorageAccessPassData>("WriteMip0",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					mipTextureId = builder.CreateTexture("MipValidityTexture", mipTextureDesc);
					builder.WriteInPlace(mipTextureId, RGTextureAccess::RenderTarget, RHIStage::RenderTarget, colorMip0);
					builder.SideEffect();
				});
			mipReadGraph.AddPass<TextureStorageAccessPassData>("ReadMip0",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.Read(mipTextureId, RGTextureAccess::Sample, RHIStage::PixelShader, colorMip0);
					builder.SideEffect();
				});
			context.Check(mipReadGraph.Compile(),
				"Undefined texture written on mip0 accepts reads of mip0");
		}
		{
			RenderGraph mipReadGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			RGTextureId mipTextureId{};
			mipReadGraph.AddPass<TextureStorageAccessPassData>("WriteMip0",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					mipTextureId = builder.CreateTexture("MipValidityTexture", mipTextureDesc);
					builder.WriteInPlace(mipTextureId, RGTextureAccess::RenderTarget, RHIStage::RenderTarget, colorMip0);
					builder.SideEffect();
				});
			mipReadGraph.AddPass<TextureStorageAccessPassData>("ReadMip1",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.Read(mipTextureId, RGTextureAccess::Sample, RHIStage::PixelShader, colorMip1);
					builder.SideEffect();
				});
			context.Check(!mipReadGraph.Compile() && hasUndefinedRead(mipReadGraph),
				"Undefined texture written on mip0 rejects reads of mip1");
		}
		{
			RenderGraph mipReadGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			RGTextureId mipTextureId{};
			mipReadGraph.AddPass<TextureStorageAccessPassData>("WriteMip0",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					mipTextureId = builder.CreateTexture("MipValidityTexture", mipTextureDesc);
					builder.WriteInPlace(mipTextureId, RGTextureAccess::RenderTarget, RHIStage::RenderTarget, colorMip0);
					builder.SideEffect();
				});
			mipReadGraph.AddPass<TextureStorageAccessPassData>("WriteMip1",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.WriteInPlace(mipTextureId, RGTextureAccess::RenderTarget, RHIStage::RenderTarget, colorMip1);
					builder.SideEffect();
				});
			mipReadGraph.AddPass<TextureStorageAccessPassData>("ReadMip0AfterMip1",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.Read(mipTextureId, RGTextureAccess::Sample, RHIStage::PixelShader, colorMip0);
					builder.SideEffect();
				});
			context.Check(mipReadGraph.Compile(),
				"Version inheritance preserves previously defined mip0 after mip1 write");
		}
		{
			RenderGraph mipReadGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			RGTextureId mipTextureId{};
			mipReadGraph.AddPass<TextureStorageAccessPassData>("WriteMip1",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					mipTextureId = builder.CreateTexture("MipValidityTexture", mipTextureDesc);
					builder.WriteInPlace(mipTextureId, RGTextureAccess::RenderTarget, RHIStage::RenderTarget, colorMip1);
					builder.SideEffect();
				});
			mipReadGraph.AddPass<TextureStorageAccessPassData>("ReadMip1",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.Read(mipTextureId, RGTextureAccess::Sample, RHIStage::PixelShader, colorMip1);
					builder.SideEffect();
				});
			context.Check(mipReadGraph.Compile(),
				"Undefined texture written on mip1 accepts reads of mip1");
		}

		// Array slice partial write/read.
		const RHITextureDesc arrayTextureDesc{
			.m_Format = RHIFormat::R8G8B8A8Unorm,
			.m_Extent = { 8, 8, 1 },
			.m_ArraySize = 2,
		};
		const RHISubresourceRange slice0Range{
			.m_ArraySliceCount = 1,
			.m_Aspects = RHITextureAspect::Color,
		};
		const RHISubresourceRange slice1Range{
			.m_BaseArraySlice = 1,
			.m_ArraySliceCount = 1,
			.m_Aspects = RHITextureAspect::Color,
		};
		{
			RenderGraph sliceReadGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			RGTextureId sliceTextureId{};
			sliceReadGraph.AddPass<TextureStorageAccessPassData>("WriteSlice0",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					sliceTextureId = builder.CreateTexture("SliceValidityTexture", arrayTextureDesc);
					builder.WriteInPlace(sliceTextureId, RGTextureAccess::RenderTarget, RHIStage::RenderTarget, slice0Range);
					builder.SideEffect();
				});
			sliceReadGraph.AddPass<TextureStorageAccessPassData>("ReadSlice1",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.Read(sliceTextureId, RGTextureAccess::Sample, RHIStage::PixelShader, slice1Range);
					builder.SideEffect();
				});
			context.Check(!sliceReadGraph.Compile() && hasUndefinedRead(sliceReadGraph),
				"Array slice writes do not define other slices");
		}
		{
			RenderGraph sliceReadGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			RGTextureId sliceTextureId{};
			sliceReadGraph.AddPass<TextureStorageAccessPassData>("WriteSlice1",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					sliceTextureId = builder.CreateTexture("SliceValidityTexture", arrayTextureDesc);
					builder.WriteInPlace(sliceTextureId, RGTextureAccess::RenderTarget, RHIStage::RenderTarget, slice1Range);
					builder.SideEffect();
				});
			sliceReadGraph.AddPass<TextureStorageAccessPassData>("ReadSlice1",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.Read(sliceTextureId, RGTextureAccess::Sample, RHIStage::PixelShader, slice1Range);
					builder.SideEffect();
				});
			context.Check(sliceReadGraph.Compile(), "Array slice writes define their own slice");
		}

		// Depth/stencil aspect granularity.
		const RHITextureDesc depthStencilTextureDesc{
			.m_Format = RHIFormat::D24UnormS8Uint,
			.m_Extent = { 8, 8, 1 },
		};
		const RHISubresourceRange depthAspectRange{ .m_Aspects = RHITextureAspect::Depth };
		const RHISubresourceRange stencilAspectRange{ .m_Aspects = RHITextureAspect::Stencil };
		{
			RenderGraph aspectReadGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			RGTextureId aspectTextureId{};
			aspectReadGraph.AddPass<TextureStorageAccessPassData>("WriteDepthAspect",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					aspectTextureId = builder.CreateTexture("AspectValidityTexture", depthStencilTextureDesc);
					builder.WriteInPlace(aspectTextureId, RGTextureAccess::DepthStencilWrite, RHIStage::DepthStencil, depthAspectRange);
					builder.SideEffect();
				});
			aspectReadGraph.AddPass<TextureStorageAccessPassData>("ReadDepthAspect",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.Read(aspectTextureId, RGTextureAccess::DepthStencilRead, RHIStage::DepthStencil, depthAspectRange);
					builder.SideEffect();
				});
			aspectReadGraph.AddPass<TextureStorageAccessPassData>("ReadStencilAspect",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.Read(aspectTextureId, RGTextureAccess::DepthStencilRead, RHIStage::DepthStencil, stencilAspectRange);
					builder.SideEffect();
				});
			context.Check(!aspectReadGraph.Compile() && hasUndefinedRead(aspectReadGraph),
				"Depth aspect writes do not define the stencil aspect");
		}
		{
			RenderGraph aspectWriteGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			RGTextureId aspectTextureId{};
			aspectWriteGraph.AddPass<TextureStorageAccessPassData>("WriteBothAspects",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					aspectTextureId = builder.CreateTexture("AspectValidityTexture", depthStencilTextureDesc);
					builder.WriteInPlace(aspectTextureId, RGTextureAccess::DepthStencilWrite, RHIStage::DepthStencil, depthAspectRange);
					builder.SideEffect();
				});
			aspectWriteGraph.AddPass<TextureStorageAccessPassData>("WriteStencilAspect",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.WriteInPlace(aspectTextureId, RGTextureAccess::DepthStencilWrite, RHIStage::DepthStencil, stencilAspectRange);
					builder.SideEffect();
				});
			aspectWriteGraph.AddPass<TextureStorageAccessPassData>("ReadStencilAspect",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
				{
					builder.Read(aspectTextureId, RGTextureAccess::DepthStencilRead, RHIStage::DepthStencil, stencilAspectRange);
					builder.SideEffect();
				});
			context.Check(aspectWriteGraph.Compile(),
				"Stencil aspect writes define the stencil aspect");
		}

		// Imported Defined texture with partial reads.
		{
			RecordingDevice recordingDevice;
			RenderGraph importedPartialGraph({
				.m_Device = &recordingDevice,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			importedPartialGraph.AddPass<TextureStorageAccessPassData>("ReadImportedPartial",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					data.m_Texture =
						builder.ImportTexture("PartialImportedTexture", RHITextureHandle{ 10, 1 },
							mipTextureDesc, UndefinedRHITextureState(), RGContentValidity::Defined);
					data.m_Texture = builder.Read(data.m_Texture, RGTextureAccess::Sample, RHIStage::PixelShader, colorMip1);
					builder.SideEffect();
				});
			context.Check(importedPartialGraph.Compile(),
				"Imported Defined textures support partial reads");
		}

		// Read-before-write remains a declaration contract even when the
		// reading pass can be culled.
		{
			RenderGraph cullableReadGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			cullableReadGraph.AddPass<StorageAccessPassData>("CullableUndefinedRead",
				[](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.CreateBuffer("CullableBuffer");
					data.m_Buffer = builder.Read(data.m_Buffer, RGBufferAccess::StructuredRead);
				});
			context.Check(!cullableReadGraph.Compile() && hasUndefinedRead(cullableReadGraph),
				"Cullable passes still report read-before-write declarations");
		}

		// --- Portability fixed-array boundaries ---
		RHIGraphicsPipelineDesc maxCountPipeline{};
		maxCountPipeline.m_VertexInput.m_VertexBufferCount = RHIVertexInputLayoutDesc::MaxVertexBuffers;
		maxCountPipeline.m_RenderTargetCount = RHIGraphicsPipelineDesc::MaxRenderTargets;
		context.Check(ValidateRHIGraphicsPipelinePortability(maxCountPipeline, vulkanCapabilities).IsValid(),
			"Pipeline portability accepts maximum vertex buffer and render target counts");
		RHIGraphicsPipelineDesc vertexOverflowPipeline{};
		vertexOverflowPipeline.m_VertexInput.m_VertexBufferCount =
			RHIVertexInputLayoutDesc::MaxVertexBuffers + 1;
		context.Check(ValidateRHIGraphicsPipelinePortability(
			vertexOverflowPipeline, vulkanCapabilities).m_Error == RHIPortabilityValidationError::VertexBufferCountExceedsLimit,
			"Pipeline portability rejects vertex buffer counts above the input layout limit");
		RHIGraphicsPipelineDesc targetOverflowPipeline{};
		targetOverflowPipeline.m_RenderTargetCount =
			RHIGraphicsPipelineDesc::MaxRenderTargets + 1;
		context.Check(ValidateRHIGraphicsPipelinePortability(
			targetOverflowPipeline, vulkanCapabilities).m_Error == RHIPortabilityValidationError::RenderTargetCountExceedsLimit,
			"Pipeline portability rejects render target counts above the pipeline limit");

		// --- Portability rejection reason text ---
		context.Check(
			GetRHIPortabilityValidationErrorText(RHIPortabilityValidationError::ImageViewMinLodUnsupported) ==
			"image view min LOD clamp is not supported" &&
			!GetRHIPortabilityValidationErrorText(RHIPortabilityValidationError::None).empty() &&
			GetRHIPortabilityValidationErrorText(
				RHIPortabilityValidationError::ImageViewMinLodUnsupported) != GetRHIPortabilityValidationErrorText(
					RHIPortabilityValidationError::None),
			"Portability rejection reasons expose stable, distinct text");

		// --- TransferBatch poisoned state ---
		{
			RecordingTransferContext failingContext;
			TransferBatch failingBatch(failingContext);
			const RHITextureHandle failTexture{ 5, 1 };
			const bool firstUpload = failingBatch.UploadTexture(failTexture, {}, UndefinedRHITextureState(), shaderReadState);
			failingContext.m_FailUploads = true;
			const bool failedUpload = failingBatch.UploadTexture(failTexture, {}, UndefinedRHITextureState(), shaderReadState);
			context.Check(firstUpload && !failedUpload && failingBatch.IsFailed() &&
				failingContext.m_TextureBarriers.size() == 3,
				"UploadTexture failure after recorded barriers poisons the batch");
			const RHITransferSubmission failedSubmission = failingBatch.Submit(false);
			context.Check(failingContext.m_SubmitCallCount == 0 &&
				failingContext.m_AbortCallCount == 1 &&
				!failedSubmission.m_Completion.IsValid() &&
				failedSubmission.m_Publications.empty(),
				"Poisoned batches abort without submitting and return an invalid submission");
		}
		{
			RecordingTransferContext readbackContext;
			TransferBatch readbackBatch(readbackContext);
			const RHITextureReadbackRequest request = readbackBatch.ReadbackTexture(
				RHITextureHandle{ 6, 1 }, RHITextureDesc{
					.m_Format = RHIFormat::R8G8B8A8Unorm,
					.m_Usage = RHITextureUsage::CopySource,
					.m_Extent = { 1, 1, 1 },
				});
			const RHITransferSubmission submission = readbackBatch.Submit(false);
			context.Check(!request.IsValid() && readbackBatch.IsFailed() &&
				readbackContext.m_TextureBarriers.size() == 1 &&
				readbackContext.m_AbortCallCount == 1 &&
				readbackContext.m_SubmitCallCount == 0 &&
				!submission.m_Completion.IsValid(),
				"Readback allocation failure poisons and aborts its partially recorded state transition");
		}
		{
			RecordingTransferContext readbackContext;
			readbackContext.m_FailReadbacks = false;
			TransferBatch readbackBatch(readbackContext);
			const RHITextureHandle readbackTexture{ 6, 1 };
			const RHITextureReadbackRequest request = readbackBatch.ReadbackTexture(
				readbackTexture, RHITextureDesc{
					.m_Format = RHIFormat::R8G8B8A8Unorm,
					.m_Usage = RHITextureUsage::CopySource,
					.m_Extent = { 1, 1, 1 },
				});
			const RHITransferSubmission submission = readbackBatch.Submit(false);
			const RHIResourceState commonState = CommonRHIResourceState();
			context.Check(request.IsValid() && readbackContext.m_TextureBarriers.size() == 2 &&
				readbackContext.m_TextureBarriers[0].m_Before == commonState &&
				readbackContext.m_TextureBarriers[0].m_After.m_Layout == RHILayout::CopySource &&
				readbackContext.m_TextureBarriers[1].m_Before.m_Layout == RHILayout::CopySource &&
				readbackContext.m_TextureBarriers[1].m_After == commonState &&
				submission.m_Completion.IsValid() && submission.m_Publications.size() == 2 &&
				submission.m_Publications[0].m_Texture == readbackTexture &&
				submission.m_Publications[0].m_PublishedState == commonState &&
				submission.m_Publications[1].m_Buffer == request.m_Buffer.Get() &&
				submission.m_Publications[1].m_PublishedState.m_Access == RHIAccess::CopyDest,
				"Texture readback returns its source to Common and publishes both terminal states");
		}
		{
			RecordingTransferContext submitFailureContext;
			TransferBatch submitFailureBatch(submitFailureContext);
			const uint32_t payload = 23;
			const bool uploadRecorded = submitFailureBatch.UploadBuffer(RHIBufferHandle{ 8, 1 }, 0, &payload, sizeof(payload));
			submitFailureContext.m_FailSubmit = true;
			const RHITransferSubmission submission = submitFailureBatch.Submit(false);
			context.Check(uploadRecorded && submitFailureBatch.IsFailed() &&
				submitFailureContext.m_SubmitCallCount == 1 &&
				!submission.m_Completion.IsValid() && submission.m_Publications.empty(),
				"Native submission failure discards the publication manifest");
		}
		{
			RecordingTransferContext moveContext;
			TransferBatch moveSource(moveContext);
			const RHITextureHandle moveTexture{ 7, 1 };
			GGLAB_UNUSED(moveSource.UploadTexture(moveTexture, {}, UndefinedRHITextureState(), shaderReadState));
			moveContext.m_FailUploads = true;
			GGLAB_UNUSED(moveSource.UploadTexture(moveTexture, {}, UndefinedRHITextureState(), shaderReadState));
			TransferBatch moveTarget(std::move(moveSource));
			context.Check(moveTarget.IsFailed(),
				"Move construction preserves the poisoned state");
			const RHITransferSubmission moveSubmission = moveTarget.Submit(false);
			context.Check(moveContext.m_SubmitCallCount == 0 &&
				moveContext.m_AbortCallCount == 1 && !moveSubmission.m_Completion.IsValid(),
				"Moved poisoned batches still abort without submitting");
		}

		// --- TransferBatch overlapping publication validation ---
		const RHIResourceState copyDestState{
			.m_Stages = RHIStage::Copy,
			.m_Access = RHIAccess::CopyDest,
			.m_Layout = RHILayout::CopyDest,
		};
		const RHITextureHandle overlapTexture{ 6, 1 };
		const RHISubresourceRange colorMips{
			.m_MipCount = 2,
			.m_Aspects = RHITextureAspect::Color,
		};
		{
			RecordingTransferContext overlapContext;
			TransferBatch overlapBatch(overlapContext);
			const bool sameRangeAccepted =
				overlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0) &&
				overlapBatch.PublishTexture(overlapTexture, copyDestState, colorMip0) &&
				overlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0);
			const bool disjointAccepted = overlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMip1);
			const bool sameStateOverlapAccepted = overlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMips);
			context.Check(sameRangeAccepted && disjointAccepted && sameStateOverlapAccepted &&
				!overlapBatch.IsFailed(),
				"Exact, disjoint, and same-state overlapping texture publications are accepted");
			const RHITransferSubmission overlapSubmission = overlapBatch.Submit(false);
			context.Check(overlapSubmission.m_Completion.IsValid(),
				"Non-conflicting publications submit normally");
		}
		{
			RecordingTransferContext conflictContext;
			TransferBatch conflictBatch(conflictContext);
			GGLAB_UNUSED(conflictBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
			const bool conflictRejected = !conflictBatch.PublishTexture(overlapTexture, copyDestState, colorMips);
			context.Check(conflictRejected && conflictBatch.IsFailed(),
				"Overlapping publications with conflicting terminal states poison the batch");
		}
		{
			RecordingTransferContext wholeContext;
			TransferBatch wholeBatch(wholeContext);
			GGLAB_UNUSED(wholeBatch.PublishTexture(overlapTexture, shaderReadState, std::nullopt));
			const bool wholeConflictRejected = !wholeBatch.PublishTexture(overlapTexture, copyDestState, colorMip0);
			context.Check(wholeConflictRejected && wholeBatch.IsFailed(),
				"Whole-resource publications overlap every partial range");
		}
		{
			RecordingTransferContext remainingContext;
			TransferBatch remainingBatch(remainingContext);
			const RHISubresourceRange remainingRange{ .m_Aspects = RHITextureAspect::Color };
			GGLAB_UNUSED(remainingBatch.PublishTexture(overlapTexture, shaderReadState, remainingRange));
			const bool remainingConflictRejected = !remainingBatch.PublishTexture(overlapTexture, copyDestState, colorMip0);
			context.Check(remainingConflictRejected && remainingBatch.IsFailed(),
				"Remaining-count ranges overlap explicit partial ranges");
		}

		// --- Exact-range updates must stay consistent with overlapping
		// publications (validate-first, mutate-second) ---
		const RHIResourceState copySourceState{
			.m_Stages = RHIStage::Copy,
			.m_Access = RHIAccess::CopySource,
			.m_Layout = RHILayout::CopySource,
		};
		const RHISubresourceRange colorMips3{
			.m_MipCount = 3,
			.m_Aspects = RHITextureAspect::Color,
		};
		{
			// A narrow exact-range update must be rejected when a broader
			// overlapping publication keeps a different terminal state.
			RecordingTransferContext narrowUpdateContext;
		TransferBatch narrowUpdateBatch(narrowUpdateContext);
		GGLAB_UNUSED(narrowUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
		GGLAB_UNUSED(narrowUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMips));
		const bool narrowUpdateRejected = !narrowUpdateBatch.PublishTexture(overlapTexture, copyDestState, colorMip0);
		context.Check(narrowUpdateRejected && narrowUpdateBatch.IsFailed(),
			"Exact-range updates cannot contradict a broader overlapping publication");
		}
		{
			// A broader exact-range update must be rejected when a narrower
			// overlapping publication keeps a different terminal state.
			RecordingTransferContext broadUpdateContext;
			TransferBatch broadUpdateBatch(broadUpdateContext);
			GGLAB_UNUSED(broadUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMips));
			GGLAB_UNUSED(broadUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
			const bool broadUpdateRejected = !broadUpdateBatch.PublishTexture(overlapTexture, copyDestState, colorMips);
			context.Check(broadUpdateRejected && broadUpdateBatch.IsFailed(),
				"Broader exact-range updates cannot contradict a narrower overlapping publication");
		}
		{
			// A disjoint exact-range update must succeed without poisoning.
			RecordingTransferContext disjointUpdateContext;
			TransferBatch disjointUpdateBatch(disjointUpdateContext);
			GGLAB_UNUSED(disjointUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
			GGLAB_UNUSED(disjointUpdateBatch.PublishTexture(overlapTexture, copyDestState, colorMip1));
			const bool disjointUpdateSucceeded = disjointUpdateBatch.PublishTexture(overlapTexture, copySourceState, colorMip0);
			context.Check(disjointUpdateSucceeded && !disjointUpdateBatch.IsFailed(),
				"Disjoint exact-range updates succeed without poisoning the batch");
		}
		{
			// An exact-range update must succeed when every overlapping
			// publication agrees with the updated terminal state.
			RecordingTransferContext agreeingOverlapContext;
			TransferBatch agreeingOverlapBatch(agreeingOverlapContext);
			GGLAB_UNUSED(agreeingOverlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
			GGLAB_UNUSED(agreeingOverlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMips));
			const bool agreeingOverlapAccepted = agreeingOverlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMips3);
			context.Check(agreeingOverlapAccepted && !agreeingOverlapBatch.IsFailed(),
				"Exact-range updates succeed when all overlapping entries agree on the state");
		}
		{
			// A partial exact-range update must be rejected when a
			// whole-resource publication keeps a different terminal state.
			RecordingTransferContext partialUpdateContext;
			TransferBatch partialUpdateBatch(partialUpdateContext);
			GGLAB_UNUSED(partialUpdateBatch.PublishTexture(overlapTexture, shaderReadState, std::nullopt));
			GGLAB_UNUSED(partialUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
			const bool partialUpdateRejected = !partialUpdateBatch.PublishTexture(overlapTexture, copyDestState, colorMip0);
			context.Check(partialUpdateRejected && partialUpdateBatch.IsFailed(),
				"Partial exact-range updates cannot contradict a whole-resource publication");
		}
		{
			// A whole-resource exact-range update must be rejected when a
			// partial publication keeps a different terminal state.
			RecordingTransferContext wholeUpdateContext;
			TransferBatch wholeUpdateBatch(wholeUpdateContext);
			GGLAB_UNUSED(wholeUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
			GGLAB_UNUSED(wholeUpdateBatch.PublishTexture(overlapTexture, shaderReadState, std::nullopt));
			const bool wholeUpdateRejected = !wholeUpdateBatch.PublishTexture(overlapTexture, copyDestState, std::nullopt);
			context.Check(wholeUpdateRejected && wholeUpdateBatch.IsFailed(),
				"Whole-resource exact-range updates cannot contradict a partial publication");
		}
		{
			// A Remaining-count exact-range update must be rejected when a
			// narrower overlapping publication keeps a different terminal state.
			RecordingTransferContext remainingUpdateContext;
			TransferBatch remainingUpdateBatch(remainingUpdateContext);
			const RHISubresourceRange remainingRange{ .m_Aspects = RHITextureAspect::Color };
			GGLAB_UNUSED(remainingUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
			GGLAB_UNUSED(remainingUpdateBatch.PublishTexture(overlapTexture, shaderReadState, remainingRange));
			const bool remainingUpdateRejected = !remainingUpdateBatch.PublishTexture(overlapTexture, copyDestState, remainingRange);
			context.Check(remainingUpdateRejected && remainingUpdateBatch.IsFailed(),
				"Remaining-count exact-range updates cannot contradict a narrower overlapping publication");
		}
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingResourcesSelfTests(SelfTestContext& context) noexcept
	{
		RunTextureFormatCapabilityTests(context);
		RunPersistentTexturePoolContractTests(context);
		RunResourceStateAndPortabilityContractTests(context);
		RunCompiledResourceStatusTests(context);
	}
}
