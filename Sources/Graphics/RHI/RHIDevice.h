#pragma once
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHISampler.h"
#include "Graphics/RHI/RHITexture.h"

#include <memory>

namespace gglab
{
	class RHITransferContext;

	class RHIDevice
	{
	public:
		virtual ~RHIDevice() = default;

		virtual RHIBackendType GetBackendType() const noexcept = 0;
		virtual std::unique_ptr<RHITransferContext> CreateTransferContext() noexcept = 0;
		virtual void WaitForFence(
			RHIQueueType waitingQueue,
			const RHIFencePoint& fencePoint) noexcept = 0;

		virtual RHITextureHandle CreateTexture(const RHITextureDesc& desc) noexcept = 0;
		virtual RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) noexcept = 0;
		virtual RHITextureViewHandle CreateTextureView(RHITextureHandle texture,
			const RHITextureViewDesc& desc) noexcept = 0;
		virtual RHIBufferViewHandle CreateBufferView(RHIBufferHandle buffer,
			const RHIBufferViewDesc& desc) noexcept = 0;
		virtual RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) noexcept = 0;

		virtual void DestroyTexture(RHITextureHandle texture) noexcept = 0;
		virtual void DestroyBuffer(RHIBufferHandle buffer) noexcept = 0;
		virtual void DestroyTextureView(RHITextureViewHandle view) noexcept = 0;
		virtual void DestroyBufferView(RHIBufferViewHandle view) noexcept = 0;
		virtual void DestroySampler(RHISamplerHandle sampler) noexcept = 0;
		virtual void* MapBuffer(RHIBufferHandle buffer) noexcept = 0;
		virtual void UnmapBuffer(RHIBufferHandle buffer) noexcept = 0;
		virtual uint32_t GetBufferViewAlignment(RHIBufferViewType viewType) const noexcept = 0;

		virtual bool IsAlive(RHITextureHandle texture) const noexcept = 0;
		virtual bool IsAlive(RHIBufferHandle buffer) const noexcept = 0;
		virtual bool IsAlive(RHISamplerHandle sampler) const noexcept = 0;
		virtual bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept = 0;

		virtual RHIDescriptorHandle GetTextureViewDescriptor(RHITextureViewHandle view) const noexcept = 0;
		virtual RHIDescriptorHandle GetBufferViewDescriptor(RHIBufferViewHandle view) const noexcept = 0;
		virtual RHIDescriptorHandle GetSamplerDescriptor(RHISamplerHandle sampler) const noexcept = 0;

		virtual void RetireCompletedWork() noexcept = 0;
	};
}
