#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Cache/DX12DescriptorCache.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"
#include "Graphics/RHI/DX12/Utility/DX12SamplerUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12ViewDescUtils.h"

namespace gglab
{
	namespace
	{
		RHIDescriptorHeapType ToRHIDescriptorHeapType(D3D12_DESCRIPTOR_HEAP_TYPE heapType) noexcept
		{
			switch (heapType)
			{
			case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
				return RHIDescriptorHeapType::CbvSrvUav;
			case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
				return RHIDescriptorHeapType::Sampler;
			case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
				return RHIDescriptorHeapType::RenderTarget;
			case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
				return RHIDescriptorHeapType::DepthStencil;
			default:
				GGLAB_UNREACHABLE("Unhandled D3D12 descriptor heap type.");
			}
		}

		RHIDescriptorHandle ToRHIDescriptorHandle(const DX12DescriptorHandle& descriptor) noexcept
		{
			if (!descriptor.IsValid())
			{
				return {};
			}

			return {
				.m_HeapType = ToRHIDescriptorHeapType(descriptor.HeapType()),
				.m_Index = descriptor.Index(),
			};
		}
	}

	DX12DescriptorCache::DX12DescriptorCache(const CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device), m_DescriptorManager(createInfo.m_DescriptorManager)
	{
		GGLAB_ASSERT(m_DX12Device);
		GGLAB_ASSERT(m_DescriptorManager);
	}

	DX12DescriptorCache::~DX12DescriptorCache()
	{
		GarbageCollect();

		for (auto& slot : m_RHITextureViews.Slots())
		{
			if (slot.m_Descriptor.IsValid())
			{
				slot.m_Descriptor.Free();
			}
		}
		m_RHITextureViews.Clear();
		m_RHITextureViewCache.clear();
		m_RHITextureResourceViews.clear();

		for (auto& slot : m_RHIBufferViews.Slots())
		{
			if (slot.m_Descriptor.IsValid())
			{
				slot.m_Descriptor.Free();
			}
		}
		m_RHIBufferViews.Clear();
		m_RHIBufferViewCache.clear();
		m_RHIBufferResourceViews.clear();

		for (auto& slot : m_RHISamplers.Slots())
		{
			if (slot.m_Descriptor.IsValid())
			{
				slot.m_Descriptor.Free();
			}
		}
		m_RHISamplers.Clear();
		m_RHISamplerCache.clear();
	}

	RHITextureViewHandle DX12DescriptorCache::GetOrCreateTextureView(
		RHITextureHandle texture, const RHITextureViewDesc& desc) noexcept
	{
		RHITextureViewKey key{ .m_Texture = texture, .m_Desc = desc };

		{
			std::shared_lock lock(m_Mutex);
			if (auto iterator = m_RHITextureViewCache.find(key);
				iterator != m_RHITextureViewCache.end())
			{
				if (m_RHITextureViews.Resolve(iterator->second))
				{
					return iterator->second;
				}
			}
		}

		DX12Texture* nativeTexture = m_DX12Device->ResolveTexture(texture);
		if (!nativeTexture)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12DescriptorCache::GetOrCreateTextureView received a non-live texture handle.");
			return {};
		}

		DX12DescriptorHandle descriptor = CreateTextureDescriptor(key, nativeTexture);
		if (!descriptor.IsValid())
		{
			return {};
		}

		std::unique_lock lock(m_Mutex);
		if (auto iterator = m_RHITextureViewCache.find(key);
			iterator != m_RHITextureViewCache.end())
		{
			if (m_RHITextureViews.Resolve(iterator->second))
			{
				descriptor.Free();
				return iterator->second;
			}
		}

		const RHITextureViewHandle view = m_RHITextureViews.Allocate();
		TextureViewSlot& slot = m_RHITextureViews.SlotAt(view.Index());
		slot.m_Key = key;
		slot.m_RetirementPoints.clear();
		slot.m_Descriptor = std::move(descriptor);
		m_RHITextureViewCache[key] = view;
		m_RHITextureResourceViews[texture].push_back(view);
		return view;
	}

	RHISamplerHandle DX12DescriptorCache::GetOrCreateSampler(const RHISamplerDesc& desc) noexcept
	{
		{
			std::shared_lock lock(m_Mutex);
			if (auto iterator = m_RHISamplerCache.find(desc); iterator != m_RHISamplerCache.end())
			{
				if (m_RHISamplers.Resolve(iterator->second))
				{
					return iterator->second;
				}
			}
		}

		DX12DescriptorHandle descriptor = CreateSamplerDescriptor(desc);
		if (!descriptor.IsValid())
		{
			return {};
		}

		std::unique_lock lock(m_Mutex);
		if (auto iterator = m_RHISamplerCache.find(desc); iterator != m_RHISamplerCache.end())
		{
			if (m_RHISamplers.Resolve(iterator->second))
			{
				descriptor.Free();
				return iterator->second;
			}
		}

		const RHISamplerHandle sampler = m_RHISamplers.Allocate();
		SamplerSlot& slot = m_RHISamplers.SlotAt(sampler.Index());
		slot.m_Key = desc;
		slot.m_RetirementPoints.clear();
		slot.m_Descriptor = std::move(descriptor);
		m_RHISamplerCache[desc] = sampler;
		return sampler;
	}

	RHIBufferViewHandle DX12DescriptorCache::GetOrCreateBufferView(
		RHIBufferHandle buffer, const RHIBufferViewDesc& desc) noexcept
	{
		RHIBufferViewKey key{ .m_Buffer = buffer, .m_Desc = desc };

		{
			std::shared_lock lock(m_Mutex);
			if (auto iterator = m_RHIBufferViewCache.find(key);
				iterator != m_RHIBufferViewCache.end())
			{
				if (m_RHIBufferViews.Resolve(iterator->second))
				{
					return iterator->second;
				}
			}
		}

		DX12Buffer* nativeBuffer = m_DX12Device->ResolveBuffer(buffer);
		if (!nativeBuffer)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12DescriptorCache::GetOrCreateBufferView received a non-live buffer handle.");
			return {};
		}

		DX12DescriptorHandle descriptor = CreateBufferDescriptor(key, nativeBuffer);
		if (!descriptor.IsValid())
		{
			return {};
		}

		std::unique_lock lock(m_Mutex);
		if (auto iterator = m_RHIBufferViewCache.find(key); iterator != m_RHIBufferViewCache.end())
		{
			if (m_RHIBufferViews.Resolve(iterator->second))
			{
				descriptor.Free();
				return iterator->second;
			}
		}

		const RHIBufferViewHandle view = m_RHIBufferViews.Allocate();
		BufferViewSlot& slot = m_RHIBufferViews.SlotAt(view.Index());
		slot.m_Key = key;
		slot.m_RetirementPoints.clear();
		slot.m_Descriptor = std::move(descriptor);
		m_RHIBufferViewCache[key] = view;
		m_RHIBufferResourceViews[buffer].push_back(view);
		return view;
	}

	DX12DescriptorView DX12DescriptorCache::ResolveTextureView(
		RHITextureViewHandle view) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		const TextureViewSlot* slot = m_RHITextureViews.Resolve(view);
		if (!slot || !slot->m_Descriptor.IsValid())
		{
			return {};
		}

		return slot->m_Descriptor.ToDescriptorView();
	}

	DX12DescriptorView DX12DescriptorCache::ResolveBufferView(
		RHIBufferViewHandle view) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		const BufferViewSlot* slot = m_RHIBufferViews.Resolve(view);
		if (!slot || !slot->m_Descriptor.IsValid())
		{
			return {};
		}

		return slot->m_Descriptor.ToDescriptorView();
	}

	RHIDescriptorHandle DX12DescriptorCache::ResolveTextureViewDescriptor(
		RHITextureViewHandle view) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		const TextureViewSlot* slot = m_RHITextureViews.Resolve(view);
		if (!slot)
		{
			return {};
		}

		return ToRHIDescriptorHandle(slot->m_Descriptor);
	}

	RHIDescriptorHandle DX12DescriptorCache::ResolveBufferViewDescriptor(
		RHIBufferViewHandle view) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		const BufferViewSlot* slot = m_RHIBufferViews.Resolve(view);
		if (!slot)
		{
			return {};
		}

		return ToRHIDescriptorHandle(slot->m_Descriptor);
	}

	RHIDescriptorHandle DX12DescriptorCache::ResolveSamplerDescriptor(
		RHISamplerHandle sampler) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		const SamplerSlot* slot = m_RHISamplers.Resolve(sampler);
		if (!slot || !slot->m_Descriptor.IsValid())
		{
			return {};
		}

		return ToRHIDescriptorHandle(slot->m_Descriptor);
	}

	void DX12DescriptorCache::DestroyTextureView(RHITextureViewHandle view) noexcept
	{
		std::unique_lock lock(m_Mutex);
		DestroyTextureViewLocked(view);
	}

	void DX12DescriptorCache::DestroyBufferView(RHIBufferViewHandle view) noexcept
	{
		std::unique_lock lock(m_Mutex);
		DestroyBufferViewLocked(view);
	}

	void DX12DescriptorCache::DestroySampler(RHISamplerHandle sampler) noexcept
	{
		std::unique_lock lock(m_Mutex);
		DestroySamplerLocked(sampler);
	}

	bool DX12DescriptorCache::IsSamplerAlive(RHISamplerHandle sampler) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		return m_RHISamplers.Resolve(sampler) != nullptr;
	}

	void DX12DescriptorCache::RetireTextureViews(
		RHITextureHandle texture, std::span<const RHIFencePoint> fencePoints) noexcept
	{
		std::unique_lock lock(m_Mutex);

		auto iterator = m_RHITextureResourceViews.find(texture);
		if (iterator == m_RHITextureResourceViews.end())
		{
			return;
		}

		std::vector<RHITextureViewHandle> views = std::move(iterator->second);
		m_RHITextureResourceViews.erase(iterator);

		for (const RHITextureViewHandle view : views)
		{
			TextureViewSlot* slot = m_RHITextureViews.Resolve(view);
			if (!slot)
			{
				continue;
			}

			m_RHITextureViewCache.erase(slot->m_Key);
			if (m_RHITextureViews.BeginRetirement(view) == RHIHandleValidationResult::Valid)
			{
				TextureViewSlot& retiredSlot = m_RHITextureViews.SlotAt(view.Index());
				retiredSlot.m_RetirementPoints.assign(fencePoints.begin(), fencePoints.end());
			}
		}
	}

	void DX12DescriptorCache::RetireBufferViews(
		RHIBufferHandle buffer, std::span<const RHIFencePoint> fencePoints) noexcept
	{
		std::unique_lock lock(m_Mutex);

		auto iterator = m_RHIBufferResourceViews.find(buffer);
		if (iterator == m_RHIBufferResourceViews.end())
		{
			return;
		}

		std::vector<RHIBufferViewHandle> views = std::move(iterator->second);
		m_RHIBufferResourceViews.erase(iterator);

		for (const RHIBufferViewHandle view : views)
		{
			BufferViewSlot* slot = m_RHIBufferViews.Resolve(view);
			if (!slot)
			{
				continue;
			}

			m_RHIBufferViewCache.erase(slot->m_Key);
			if (m_RHIBufferViews.BeginRetirement(view) == RHIHandleValidationResult::Valid)
			{
				BufferViewSlot& retiredSlot = m_RHIBufferViews.SlotAt(view.Index());
				retiredSlot.m_RetirementPoints.assign(fencePoints.begin(), fencePoints.end());
			}
		}
	}

	void DX12DescriptorCache::GarbageCollect() noexcept
	{
		std::unique_lock lock(m_Mutex);
		for (uint32_t index = 0; index < m_RHITextureViews.Size(); ++index)
		{
			TextureViewSlot& slot = m_RHITextureViews.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::PendingRetirement)
			{
				continue;
			}

			const bool completed =
				std::ranges::all_of(slot.m_RetirementPoints, [this](const RHIFencePoint& point)
					{ return m_DX12Device && m_DX12Device->IsFencePointCompleted(point); });
			if (!completed)
			{
				continue;
			}

			if (slot.m_Descriptor.IsValid())
			{
				slot.m_Descriptor.Free();
			}
			slot.m_Key = {};
			slot.m_RetirementPoints.clear();
			m_RHITextureViews.Retire(index);
		}

		for (uint32_t index = 0; index < m_RHIBufferViews.Size(); ++index)
		{
			BufferViewSlot& slot = m_RHIBufferViews.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::PendingRetirement)
			{
				continue;
			}

			const bool completed =
				std::ranges::all_of(slot.m_RetirementPoints, [this](const RHIFencePoint& point)
					{ return m_DX12Device && m_DX12Device->IsFencePointCompleted(point); });
			if (!completed)
			{
				continue;
			}

			if (slot.m_Descriptor.IsValid())
			{
				slot.m_Descriptor.Free();
			}
			slot.m_Key = {};
			slot.m_RetirementPoints.clear();
			m_RHIBufferViews.Retire(index);
		}
	}

	DX12DescriptorHandle DX12DescriptorCache::AllocateHandle(
		DX12DescriptorManager::AllocatorType allocatorType) const noexcept
	{
		auto* allocator = m_DescriptorManager->GetFreeListAllocator(allocatorType);
		GGLAB_ASSERT_NOT_NULL(allocator);
		return allocator->AllocateHandle();
	}

	DX12DescriptorHandle DX12DescriptorCache::CreateTextureDescriptor(
		const RHITextureViewKey& key, DX12Texture* texture) const noexcept
	{
		GGLAB_ASSERT(texture != nullptr);

		const D3D12_RESOURCE_DESC resourceDesc = texture->GetDesc();
		switch (key.m_Desc.m_Type)
		{
		case RHITextureViewType::RenderTarget:
		{
			DX12DescriptorHandle descriptor =
				AllocateHandle(DX12DescriptorManager::AllocatorType::GeneralRtv);
			const D3D12_RENDER_TARGET_VIEW_DESC desc =
				BuildD3D12RenderTargetViewDesc(key.m_Desc, resourceDesc);
			m_DX12Device->Get()->CreateRenderTargetView(
				texture->Get(), &desc, descriptor.CpuHandleAt());
			return descriptor;
		}
		case RHITextureViewType::DepthStencil:
		{
			DX12DescriptorHandle descriptor =
				AllocateHandle(DX12DescriptorManager::AllocatorType::GeneralDsv);
			const D3D12_DEPTH_STENCIL_VIEW_DESC desc =
				BuildD3D12DepthStencilViewDesc(key.m_Desc, resourceDesc);
			m_DX12Device->Get()->CreateDepthStencilView(
				texture->Get(), &desc, descriptor.CpuHandleAt());
			return descriptor;
		}
		case RHITextureViewType::ShaderResource:
		{
			DX12DescriptorHandle descriptor =
				AllocateHandle(DX12DescriptorManager::AllocatorType::GeneralCbvSrvUav);
			const D3D12_SHADER_RESOURCE_VIEW_DESC desc =
				BuildD3D12ShaderResourceViewDesc(key.m_Desc, resourceDesc);
			m_DX12Device->Get()->CreateShaderResourceView(
				texture->Get(), &desc, descriptor.CpuHandleAt());
			return descriptor;
		}
		case RHITextureViewType::UnorderedAccess:
		{
			DX12DescriptorHandle descriptor =
				AllocateHandle(DX12DescriptorManager::AllocatorType::GeneralCbvSrvUav);
			const D3D12_UNORDERED_ACCESS_VIEW_DESC desc =
				BuildD3D12UnorderedAccessViewDesc(key.m_Desc, resourceDesc);
			m_DX12Device->Get()->CreateUnorderedAccessView(
				texture->Get(), nullptr, &desc, descriptor.CpuHandleAt());
			return descriptor;
		}
		}

		GGLAB_UNREACHABLE("Unhandled RHITextureViewType.");
	}

	DX12DescriptorHandle DX12DescriptorCache::CreateBufferDescriptor(
		const RHIBufferViewKey& key, DX12Buffer* buffer) const noexcept
	{
		GGLAB_ASSERT(buffer != nullptr);

		DX12DescriptorHandle descriptor =
			AllocateHandle(DX12DescriptorManager::AllocatorType::GeneralCbvSrvUav);
		switch (key.m_Desc.m_Type)
		{
		case RHIBufferViewType::ConstantBuffer:
		{
			const D3D12_CONSTANT_BUFFER_VIEW_DESC desc = BuildD3D12ConstantBufferViewDesc(
				key.m_Desc, buffer->GPUVirtualAddress(), buffer->SizeInBytes());
			m_DX12Device->Get()->CreateConstantBufferView(&desc, descriptor.CpuHandleAt());
			return descriptor;
		}
		case RHIBufferViewType::ShaderResource:
		{
			const D3D12_SHADER_RESOURCE_VIEW_DESC desc =
				BuildD3D12ShaderResourceViewDesc(key.m_Desc, buffer->SizeInBytes());
			m_DX12Device->Get()->CreateShaderResourceView(
				buffer->Get(), &desc, descriptor.CpuHandleAt());
			return descriptor;
		}
		case RHIBufferViewType::UnorderedAccess:
		{
			const D3D12_UNORDERED_ACCESS_VIEW_DESC desc =
				BuildD3D12UnorderedAccessViewDesc(key.m_Desc, buffer->SizeInBytes());
			m_DX12Device->Get()->CreateUnorderedAccessView(
				buffer->Get(), nullptr, &desc, descriptor.CpuHandleAt());
			return descriptor;
		}
		}

		GGLAB_UNREACHABLE("Unhandled RHIBufferViewType.");
	}

	DX12DescriptorHandle DX12DescriptorCache::CreateSamplerDescriptor(
		const RHISamplerDesc& desc) const noexcept
	{
		DX12DescriptorHandle descriptor =
			m_DescriptorManager
			->GetFreeListAllocator(DX12DescriptorManager::AllocatorType::GeneralSampler)
			->AllocateHandle();
		if (!descriptor.IsValid())
		{
			return {};
		}

		const D3D12_SAMPLER_DESC nativeDesc = ToD3D12SamplerDesc(desc);
		m_DX12Device->Get()->CreateSampler(&nativeDesc, descriptor.CpuHandleAt());
		return descriptor;
	}

	void DX12DescriptorCache::DestroyTextureViewLocked(RHITextureViewHandle view) noexcept
	{
		TextureViewSlot* slot = m_RHITextureViews.Resolve(view);
		if (!slot)
		{
			return;
		}

		const RHITextureHandle texture = slot->m_Key.m_Texture;
		m_RHITextureViewCache.erase(slot->m_Key);
		if (auto iterator = m_RHITextureResourceViews.find(texture);
			iterator != m_RHITextureResourceViews.end())
		{
			std::erase(iterator->second, view);
			if (iterator->second.empty())
			{
				m_RHITextureResourceViews.erase(iterator);
			}
		}

		if (m_RHITextureViews.BeginRetirement(view) == RHIHandleValidationResult::Valid)
		{
			TextureViewSlot& retiredSlot = m_RHITextureViews.SlotAt(view.Index());
			retiredSlot.m_RetirementPoints.clear();
		}
	}

	void DX12DescriptorCache::DestroyBufferViewLocked(RHIBufferViewHandle view) noexcept
	{
		BufferViewSlot* slot = m_RHIBufferViews.Resolve(view);
		if (!slot)
		{
			return;
		}

		const RHIBufferHandle buffer = slot->m_Key.m_Buffer;
		m_RHIBufferViewCache.erase(slot->m_Key);
		if (auto iterator = m_RHIBufferResourceViews.find(buffer);
			iterator != m_RHIBufferResourceViews.end())
		{
			std::erase(iterator->second, view);
			if (iterator->second.empty())
			{
				m_RHIBufferResourceViews.erase(iterator);
			}
		}

		if (m_RHIBufferViews.BeginRetirement(view) == RHIHandleValidationResult::Valid)
		{
			BufferViewSlot& retiredSlot = m_RHIBufferViews.SlotAt(view.Index());
			retiredSlot.m_RetirementPoints.clear();
		}
	}

	void DX12DescriptorCache::DestroySamplerLocked(RHISamplerHandle sampler) noexcept
	{
		SamplerSlot* slot = m_RHISamplers.Resolve(sampler);
		if (!slot)
		{
			return;
		}

		m_RHISamplerCache.erase(slot->m_Key);
		if (m_RHISamplers.BeginRetirement(sampler) == RHIHandleValidationResult::Valid)
		{
			SamplerSlot& retiredSlot = m_RHISamplers.SlotAt(sampler.Index());
			if (retiredSlot.m_Descriptor.IsValid())
			{
				retiredSlot.m_Descriptor.Free();
			}
			retiredSlot.m_Key = {};
			retiredSlot.m_RetirementPoints.clear();
			m_RHISamplers.Retire(sampler.Index());
		}
	}

}
