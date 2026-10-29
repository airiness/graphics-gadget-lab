#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIAccelerationStructure.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHISampler.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"
#include "GGLabRuntime/Graphics/RHI/RHIResourceDebug.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string_view>

namespace gglab
{
	struct RHIShaderWaveCapabilities
	{
		bool m_Supported = false;
		uint32_t m_MinLaneCount = 0;
		uint32_t m_MaxLaneCount = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Supported && m_MinLaneCount > 0 && m_MaxLaneCount >= m_MinLaneCount;
		}
	};

	class RHIDevice
	{
	public:
		virtual ~RHIDevice() = default;
		[[nodiscard]] virtual bool SupportsPixelStorageAtomics() const noexcept { return false; }
		[[nodiscard]] virtual RHIRayQueryCapabilities GetRayQueryCapabilities() const noexcept
		{
			return {};
		}

		virtual RHIBackendType GetBackendType() const noexcept = 0;
		// Stable across process launches and changes when the adapter or driver
		// compatibility domain changes. It intentionally excludes transient LUIDs.
		virtual std::string_view GetAdapterCompatibilityIdentity() const noexcept = 0;
		virtual RHIShaderWaveCapabilities GetShaderWaveCapabilities() const noexcept = 0;
		virtual RHITextureSupportResult QueryTextureSupport(
			const RHITextureDesc& desc) const noexcept = 0;
		virtual RHITextureSupportResult QueryTextureViewSupport(const RHITextureDesc& textureDesc,
			const RHITextureViewDesc& viewDesc) const noexcept = 0;
		virtual RHITextureHandle CreateTexture(const RHIOwnedTextureCreateInfo& createInfo,
			const RHIResourceDebugIdentityDesc& debugIdentity = {}) noexcept = 0;
		virtual RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc,
			const RHIResourceDebugIdentityDesc& debugIdentity = {}) noexcept = 0;
		virtual RHITextureViewHandle CreateTextureView(
			RHITextureHandle texture, const RHITextureViewDesc& desc) noexcept = 0;
		virtual RHIBufferViewHandle CreateBufferView(
			RHIBufferHandle buffer, const RHIBufferViewDesc& desc) noexcept = 0;
		// Each successful call acquires one sampler ownership reference. Implementations may
		// return the same cached handle for equivalent descriptions; every acquired reference
		// must be balanced by DestroySampler, and the handle remains alive until the final release.
		virtual RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) noexcept = 0;

		virtual void DestroyTexture(RHITextureHandle texture) noexcept = 0;
		virtual void DestroyBuffer(RHIBufferHandle buffer) noexcept = 0;
		[[nodiscard]] virtual std::optional<RHIASBuildSizes> QueryAccelerationStructureBuildSizes(
			const RHIASBuildInputs&) noexcept { return std::nullopt; }
		[[nodiscard]] virtual RHIAccelerationStructureHandle CreateAccelerationStructure(
			const RHIAccelerationStructureDesc&) noexcept { return {}; }
		virtual void DestroyAccelerationStructure(RHIAccelerationStructureHandle) noexcept {}
		virtual void DestroyTextureView(RHITextureViewHandle view) noexcept = 0;
		virtual void DestroyBufferView(RHIBufferViewHandle view) noexcept = 0;
		virtual void DestroySampler(RHISamplerHandle sampler) noexcept = 0;
		virtual void SetTextureDebugBinding(
			RHITextureHandle texture, const RHIResourceDebugBindingDesc& binding) noexcept = 0;
		virtual void SetBufferDebugBinding(
			RHIBufferHandle buffer, const RHIResourceDebugBindingDesc& binding) noexcept = 0;
		virtual std::string_view GetTextureDebugName(RHITextureHandle texture) const noexcept = 0;
		virtual std::string_view GetBufferDebugName(RHIBufferHandle buffer) const noexcept = 0;
		// A CpuToGpu pointer stays valid until UnmapBuffer or buffer destruction. The caller
		// must not rewrite a range that an unfinished submission reads.
		virtual void* MapBuffer(
			RHIBufferHandle buffer, RHIMappedBufferRange readRange) noexcept = 0;
		// Ends a mapping. For CpuToGpu buffers it also publishes the written range.
		virtual void UnmapBuffer(
			RHIBufferHandle buffer, RHIMappedBufferRange writtenRange) noexcept = 0;
		// Makes host writes in the range of a mapped CpuToGpu buffer available to GPU work
		// submitted afterwards; the mapping stays valid. On failure, work that reads the
		// range must not be submitted. Every device states this behavior explicitly.
		[[nodiscard]] virtual bool FlushMappedBufferWrites(
			RHIBufferHandle buffer, RHIMappedBufferRange writtenRange) noexcept = 0;
		virtual uint32_t GetBufferViewAlignment(RHIBufferViewType viewType) const noexcept = 0;
		[[nodiscard]] virtual RHIUploadLimits GetUploadLimits() const noexcept = 0;
		// Callers reject upload storage outside batch-scoped bindings. A backend that cannot
		// check the identity, for example off its owner thread, reports true.
		[[nodiscard]] virtual bool IsUploadStorage(RHIBufferHandle buffer) const noexcept = 0;

		virtual bool IsAlive(RHITextureHandle texture) const noexcept = 0;
		virtual bool IsAlive(RHIBufferHandle buffer) const noexcept = 0;
		// Resource extent and capabilities, not allocator/heap padding or a binding range.
		// Owner-thread query; stale/unsupported handles return nullopt. Debug labels are omitted.
		[[nodiscard]] virtual std::optional<RHIBufferDesc> QueryBufferDescription(
			RHIBufferHandle) const noexcept { return std::nullopt; }
		[[nodiscard]] virtual bool IsAlive(RHIAccelerationStructureHandle) const noexcept { return false; }
		[[nodiscard]] virtual RHIAllocationFacts QueryAccelerationStructureAllocationFacts(
			RHIAccelerationStructureHandle) const noexcept { return {}; }
		virtual bool IsAlive(RHISamplerHandle sampler) const noexcept = 0;
		// Allocator facts of a live resource; callers read them while the handle is valid. A
		// device without allocator queries reports Unsupported rather than estimated values.
		[[nodiscard]] virtual RHIAllocationFacts QueryTextureAllocationFacts(
			RHITextureHandle) const noexcept
		{
			return {};
		}
		[[nodiscard]] virtual RHIAllocationFacts QueryBufferAllocationFacts(
			RHIBufferHandle) const noexcept
		{
			return {};
		}
		[[nodiscard]] virtual RHIAllocatorTotals QueryAllocatorTotals() const noexcept
		{
			return {};
		}
		// Routine release query: true when the GPU no longer uses work up to the point.
		// An invalid point records no use. It must not accept GPU-written contents or
		// verify quiescence; use QueryFencePoint for those decisions.
		[[nodiscard]] bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept
		{
			return !fencePoint.IsValid() || IsFenceSafeToRelease(QueryFencePoint(fencePoint));
		}
		// Checked completion query. An invalid or unknown point is never Completed. Caller
		// misuse fails the query without touching the sticky health state below.
		[[nodiscard]] RHIFenceQueryResult QueryFencePoint(
			const RHIFencePoint& fencePoint) const noexcept
		{
			return fencePoint.IsValid() ?
				ApplyObservedDeviceLoss(QueryFencePointImpl(fencePoint)) :
				RHIFenceQueryResult::Failed;
		}
		// Sticky completion failures poison recording, not the native device. Only checked
		// native results set them: pending queries, routine wait timeouts and caller misuse
		// (invalid, unknown or off-owner points) never do; only a native loss sets the latter.
		[[nodiscard]] bool HasCompletionError() const noexcept
		{
			return m_CompletionFailed.load(std::memory_order_relaxed) || HasObservedDeviceLoss();
		}
		[[nodiscard]] bool HasObservedDeviceLoss() const noexcept
		{
			return m_DeviceLost.load(std::memory_order_relaxed);
		}
		virtual void RecordTextureUse(
			RHITextureHandle texture, const RHIFencePoint& fencePoint) noexcept = 0;
		virtual void RecordBufferUse(
			RHIBufferHandle buffer, const RHIFencePoint& fencePoint) noexcept = 0;

		virtual RHIDescriptorHandle GetTextureViewDescriptor(
			RHITextureViewHandle view) const noexcept = 0;
		virtual RHIDescriptorHandle GetBufferViewDescriptor(
			RHIBufferViewHandle view) const noexcept = 0;
		virtual RHIDescriptorHandle GetSamplerDescriptor(
			RHISamplerHandle sampler) const noexcept = 0;
		// Makes a descriptor reachable by future GPU-visible resource tables. Backends whose
		// descriptors are eagerly visible validate the handle and complete immediately; deferred
		// publication backends advance their descriptor lifecycle here.
		virtual bool PublishTextureViewDescriptor(RHITextureViewHandle view) noexcept
		{
			return GetTextureViewDescriptor(view).IsValid();
		}
		virtual bool PublishSamplerDescriptor(RHISamplerHandle sampler) noexcept
		{
			return GetSamplerDescriptor(sampler).IsValid();
		}

		virtual void RetireCompletedWork() noexcept = 0;

	protected:
		// Receives only valid points. Implementations pass native query results through
		// ObserveFenceQueryResult and fail unknown or off-owner points without observing them.
		[[nodiscard]] virtual RHIFenceQueryResult QueryFencePointImpl(
			const RHIFencePoint& fencePoint) const noexcept = 0;
		// Records a checked native query result in the sticky health state.
		RHIFenceQueryResult ObserveFenceQueryResult(RHIFenceQueryResult result) const noexcept
		{
			if (result == RHIFenceQueryResult::Failed)
			{
				m_CompletionFailed.store(true, std::memory_order_relaxed);
			}
			else if (result == RHIFenceQueryResult::DeviceLost)
			{
				m_DeviceLost.store(true, std::memory_order_relaxed);
			}
			return ApplyObservedDeviceLoss(result);
		}
		// Records a checked native wait or submission-signal result in the sticky health state.
		RHIGpuWaitResult ObserveGpuWaitResult(RHIGpuWaitResult result) const noexcept
		{
			if (result == RHIGpuWaitResult::Failed)
			{
				ObserveFenceQueryResult(RHIFenceQueryResult::Failed);
			}
			else if (result == RHIGpuWaitResult::DeviceLost)
			{
				ObserveFenceQueryResult(RHIFenceQueryResult::DeviceLost);
			}
			return result;
		}

	private:
		[[nodiscard]] RHIFenceQueryResult ApplyObservedDeviceLoss(
			RHIFenceQueryResult result) const noexcept
		{
			// A confirmed loss remains authoritative even if a later counter query succeeds.
			return result != RHIFenceQueryResult::Failed && HasObservedDeviceLoss() ?
				RHIFenceQueryResult::DeviceLost :
				result;
		}

		mutable std::atomic<bool> m_CompletionFailed{ false };
		mutable std::atomic<bool> m_DeviceLost{ false };
	};
}
