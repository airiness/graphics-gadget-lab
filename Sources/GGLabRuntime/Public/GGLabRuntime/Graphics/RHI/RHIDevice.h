#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHISampler.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"
#include "GGLabRuntime/Graphics/RHI/RHIResourceDebug.h"

#include <memory>
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
		virtual uint32_t GetMaxConstantBufferRange() const noexcept { return 64 * 1024; }

		virtual bool IsAlive(RHITextureHandle texture) const noexcept = 0;
		virtual bool IsAlive(RHIBufferHandle buffer) const noexcept = 0;
		virtual bool IsAlive(RHISamplerHandle sampler) const noexcept = 0;
		// Routine release query: true when the GPU no longer uses work up to the point.
		// An invalid point records no use. It must not accept GPU-written contents or
		// verify quiescence; use QueryFencePoint for those decisions.
		virtual bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept = 0;
		// Checked completion query. An invalid or unknown point is never Completed.
		[[nodiscard]] virtual RHIFenceQueryResult QueryFencePoint(
			const RHIFencePoint& fencePoint) const noexcept = 0;
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
	};
}
