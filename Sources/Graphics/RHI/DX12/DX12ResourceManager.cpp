#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12RHITypeUtils.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Core/Utility/StringUtils.h"
#include "Core/Utility/TypeUtils.h"

#include <algorithm>

namespace gglab
{
	namespace
	{
		template<typename Handle>
		typename Handle::GenerationType NextHandleGeneration(typename Handle::GenerationType generation) noexcept
		{
			++generation;
			if (generation == Handle::InvalidGeneration)
			{
				++generation;
			}

			return generation;
		}
	}

	DX12ResourceManager::~DX12ResourceManager() noexcept = default;

	void DX12ResourceManager::Initialize(DX12Device* device) noexcept
	{
		GGLAB_ASSERT_MSG(device != nullptr, "DX12ResourceManager requires a valid DX12Device.");
		GGLAB_ASSERT_MSG(device->GetMemAllocator() != nullptr, "DX12ResourceManager requires an initialized memory allocator.");

		m_Device = device;
	}

	void DX12ResourceManager::Finalize() noexcept
	{
		ReportLiveResources();

		m_TextureSlots.clear();
		m_FreeTextureSlots.clear();

		m_BufferSlots.clear();
		m_FreeBufferSlots.clear();

		m_Device = nullptr;
	}

	RHITextureHandle DX12ResourceManager::CreateTexture(const RHITextureDesc& desc) noexcept
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "DX12ResourceManager must be initialized before creating textures.");
		GGLAB_ASSERT_MSG(m_Device->GetMemAllocator() != nullptr, "DX12 memory allocator is not initialized.");

		if (desc.m_Format == RHIFormat::Unknown ||
			desc.m_Extent.m_Width == 0 ||
			desc.m_Extent.m_Height == 0 ||
			desc.m_ArraySize == 0 ||
			desc.m_MipLevels == 0 ||
			desc.m_SampleCount == 0)
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::CreateTexture rejected an invalid texture description.");
			return {};
		}

		auto texture = std::make_unique<DX12Texture>();

		DX12Resource::CreateInfo createInfo{};
		createInfo.m_Allocator = m_Device->GetMemAllocator();
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_ResourceDesc = ToD3D12ResourceDesc(desc);
		createInfo.m_InitStates = D3D12_RESOURCE_STATE_COMMON;
		createInfo.m_ClearValue = ToD3D12ClearValue(desc.m_ClearValue);
		texture->Create(createInfo);
		if (!texture->IsValid())
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR("DX12ResourceManager::CreateTexture failed to create the native resource.");
			return {};
		}

		const std::string_view debugName = desc.m_DebugName ? desc.m_DebugName : "";
		if (!debugName.empty())
		{
			const std::wstring wideName = utils::ToWideString(debugName);
			texture->SetDebugName(wideName.c_str());
		}

		const uint32_t slotIndex = AllocateTextureSlot();
		TextureSlot& slot = m_TextureSlots[slotIndex];
		GGLAB_ASSERT_MSG(slot.m_State == ResourceSlotState::Free, "Allocated RHI texture slot is not free.");
		slot.m_Ownership = ResourceOwnership::Owned;
		slot.m_State = ResourceSlotState::Alive;
		slot.m_DebugNameId = debugName.empty() ? StringID{} : StringID(debugName);
		slot.m_RetirementPoints.clear();
		slot.m_Texture = std::move(texture);
		++m_Diagnostics.m_TextureCreateCount;

		return RHITextureHandle(slotIndex, slot.m_Generation);
	}

	RHIBufferHandle DX12ResourceManager::CreateBuffer(const RHIBufferDesc& desc) noexcept
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "DX12ResourceManager must be initialized before creating buffers.");
		GGLAB_ASSERT_MSG(m_Device->GetMemAllocator() != nullptr, "DX12 memory allocator is not initialized.");

		if (desc.m_SizeInBytes == 0)
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::CreateBuffer rejected a zero-sized buffer.");
			return {};
		}

		auto buffer = std::make_unique<DX12Buffer>();

		DX12Resource::CreateInfo createInfo{};
		createInfo.m_Allocator = m_Device->GetMemAllocator();
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_ResourceDesc = ToD3D12ResourceDesc(desc);
		createInfo.m_InitStates = D3D12_RESOURCE_STATE_COMMON;
		buffer->Create(createInfo);
		if (!buffer->IsValid())
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR("DX12ResourceManager::CreateBuffer failed to create the native resource.");
			return {};
		}

		const std::string_view debugName = desc.m_DebugName ? desc.m_DebugName : "";
		if (!debugName.empty())
		{
			const std::wstring wideName = utils::ToWideString(debugName);
			buffer->SetDebugName(wideName.c_str());
		}

		const uint32_t slotIndex = AllocateBufferSlot();
		BufferSlot& slot = m_BufferSlots[slotIndex];
		GGLAB_ASSERT_MSG(slot.m_State == ResourceSlotState::Free, "Allocated RHI buffer slot is not free.");
		slot.m_Ownership = ResourceOwnership::Owned;
		slot.m_State = ResourceSlotState::Alive;
		slot.m_DebugNameId = debugName.empty() ? StringID{} : StringID(debugName);
		slot.m_RetirementPoints.clear();
		slot.m_Buffer = std::move(buffer);
		++m_Diagnostics.m_BufferCreateCount;

		return RHIBufferHandle(slotIndex, slot.m_Generation);
	}

	void DX12ResourceManager::DestroyTexture(RHITextureHandle texture) noexcept
	{
		if (!texture.IsValid() || texture.Index() >= m_TextureSlots.size())
		{
			++m_Diagnostics.m_InvalidDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyTexture received an invalid handle.");
			return;
		}

		TextureSlot& slot = m_TextureSlots[texture.Index()];
		if (slot.m_Generation != texture.Generation())
		{
			if (slot.m_State == ResourceSlotState::PendingRetirement &&
				slot.m_Generation == NextHandleGeneration<RHITextureHandle>(texture.Generation()))
			{
				++m_Diagnostics.m_DoubleDestroyCount;
				GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyTexture detected a double destroy.");
			}
			else
			{
				++m_Diagnostics.m_StaleDestroyCount;
				GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyTexture received a stale handle.");
			}
			return;
		}
		if (slot.m_State != ResourceSlotState::Alive || !slot.m_Texture)
		{
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyTexture received a non-live handle.");
			return;
		}

		slot.m_State = ResourceSlotState::PendingRetirement;
		slot.m_Generation = NextHandleGeneration<RHITextureHandle>(slot.m_Generation);
		slot.m_RetirementPoints = CaptureRetirementPoints();
	}

	void DX12ResourceManager::DestroyBuffer(RHIBufferHandle buffer) noexcept
	{
		if (!buffer.IsValid() || buffer.Index() >= m_BufferSlots.size())
		{
			++m_Diagnostics.m_InvalidDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyBuffer received an invalid handle.");
			return;
		}

		BufferSlot& slot = m_BufferSlots[buffer.Index()];
		if (slot.m_Generation != buffer.Generation())
		{
			if (slot.m_State == ResourceSlotState::PendingRetirement &&
				slot.m_Generation == NextHandleGeneration<RHIBufferHandle>(buffer.Generation()))
			{
				++m_Diagnostics.m_DoubleDestroyCount;
				GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyBuffer detected a double destroy.");
			}
			else
			{
				++m_Diagnostics.m_StaleDestroyCount;
				GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyBuffer received a stale handle.");
			}
			return;
		}
		if (slot.m_State != ResourceSlotState::Alive || !slot.m_Buffer)
		{
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyBuffer received a non-live handle.");
			return;
		}

		slot.m_State = ResourceSlotState::PendingRetirement;
		slot.m_Generation = NextHandleGeneration<RHIBufferHandle>(slot.m_Generation);
		slot.m_RetirementPoints = CaptureRetirementPoints();
	}

	bool DX12ResourceManager::IsAlive(RHITextureHandle texture) const noexcept
	{
		if (!texture.IsValid() || texture.Index() >= m_TextureSlots.size())
		{
			return false;
		}

		const TextureSlot& slot = m_TextureSlots[texture.Index()];
		return slot.m_State == ResourceSlotState::Alive &&
			slot.m_Generation == texture.Generation() &&
			slot.m_Texture != nullptr;
	}

	bool DX12ResourceManager::IsAlive(RHIBufferHandle buffer) const noexcept
	{
		if (!buffer.IsValid() || buffer.Index() >= m_BufferSlots.size())
		{
			return false;
		}

		const BufferSlot& slot = m_BufferSlots[buffer.Index()];
		return slot.m_State == ResourceSlotState::Alive &&
			slot.m_Generation == buffer.Generation() &&
			slot.m_Buffer != nullptr;
	}

	DX12Texture* DX12ResourceManager::ResolveTexture(RHITextureHandle texture) noexcept
	{
		return const_cast<DX12Texture*>(std::as_const(*this).ResolveTexture(texture));
	}

	const DX12Texture* DX12ResourceManager::ResolveTexture(RHITextureHandle texture) const noexcept
	{
		if (!IsAlive(texture))
		{
			return nullptr;
		}

		return m_TextureSlots[texture.Index()].m_Texture.get();
	}

	DX12Buffer* DX12ResourceManager::ResolveBuffer(RHIBufferHandle buffer) noexcept
	{
		return const_cast<DX12Buffer*>(std::as_const(*this).ResolveBuffer(buffer));
	}

	const DX12Buffer* DX12ResourceManager::ResolveBuffer(RHIBufferHandle buffer) const noexcept
	{
		if (!IsAlive(buffer))
		{
			return nullptr;
		}

		return m_BufferSlots[buffer.Index()].m_Buffer.get();
	}

	void DX12ResourceManager::RetireCompletedResources() noexcept
	{
		for (uint32_t index = 0; index < static_cast<uint32_t>(m_TextureSlots.size()); ++index)
		{
			auto& slot = m_TextureSlots[index];
			if (slot.m_State != ResourceSlotState::PendingRetirement)
			{
				continue;
			}

			const bool completed = std::ranges::all_of(
				slot.m_RetirementPoints,
				[](const DX12FencePoint& point)
				{
					return !point.IsValid() || point.IsCompleted();
				});
			if (!completed)
			{
				continue;
			}

			slot.m_Texture.reset();
			slot.m_RetirementPoints.clear();
			slot.m_DebugNameId = {};
			slot.m_State = ResourceSlotState::Free;
			m_FreeTextureSlots.push_back(index);
			++m_Diagnostics.m_TextureRetireCount;
		}

		for (uint32_t index = 0; index < static_cast<uint32_t>(m_BufferSlots.size()); ++index)
		{
			auto& slot = m_BufferSlots[index];
			if (slot.m_State != ResourceSlotState::PendingRetirement)
			{
				continue;
			}

			const bool completed = std::ranges::all_of(
				slot.m_RetirementPoints,
				[](const DX12FencePoint& point)
				{
					return !point.IsValid() || point.IsCompleted();
				});
			if (!completed)
			{
				continue;
			}

			slot.m_Buffer.reset();
			slot.m_RetirementPoints.clear();
			slot.m_DebugNameId = {};
			slot.m_State = ResourceSlotState::Free;
			m_FreeBufferSlots.push_back(index);
			++m_Diagnostics.m_BufferRetireCount;
		}
	}

	uint32_t DX12ResourceManager::AllocateTextureSlot() noexcept
	{
		if (!m_FreeTextureSlots.empty())
		{
			const uint32_t slotIndex = m_FreeTextureSlots.back();
			m_FreeTextureSlots.pop_back();
			return slotIndex;
		}

		const uint32_t slotIndex = static_cast<uint32_t>(m_TextureSlots.size());
		m_TextureSlots.emplace_back();
		return slotIndex;
	}

	uint32_t DX12ResourceManager::AllocateBufferSlot() noexcept
	{
		if (!m_FreeBufferSlots.empty())
		{
			const uint32_t slotIndex = m_FreeBufferSlots.back();
			m_FreeBufferSlots.pop_back();
			return slotIndex;
		}

		const uint32_t slotIndex = static_cast<uint32_t>(m_BufferSlots.size());
		m_BufferSlots.emplace_back();
		return slotIndex;
	}

	std::vector<DX12FencePoint> DX12ResourceManager::CaptureRetirementPoints() const noexcept
	{
		std::vector<DX12FencePoint> points;
		if (!m_Device)
		{
			return points;
		}

		// Phase-one safety model: capture all work submitted before Destroy() on
		// every queue. Submission-time resource tracking can replace this with
		// precise last-use fence points later.
		points.reserve(utils::EnumCount<CommandQueueType>());
		for (const auto queueType : utils::EnumRange<CommandQueueType>())
		{
			auto* queue = m_Device->GetCommandQueue(queueType);
			if (queue)
			{
				points.push_back(queue->Signal());
			}
		}
		return points;
	}

	void DX12ResourceManager::ReportLiveResources() const noexcept
	{
		for (uint32_t index = 0; index < static_cast<uint32_t>(m_TextureSlots.size()); ++index)
		{
			const auto& slot = m_TextureSlots[index];
			if (slot.m_State != ResourceSlotState::Free)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12ResourceManager finalizing texture slot {} ('{}') in state {}.",
					index,
					utils::StringIdToString(slot.m_DebugNameId),
					static_cast<uint32_t>(slot.m_State));
			}
		}
		for (uint32_t index = 0; index < static_cast<uint32_t>(m_BufferSlots.size()); ++index)
		{
			const auto& slot = m_BufferSlots[index];
			if (slot.m_State != ResourceSlotState::Free)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12ResourceManager finalizing buffer slot {} ('{}') in state {}.",
					index,
					utils::StringIdToString(slot.m_DebugNameId),
					static_cast<uint32_t>(slot.m_State));
			}
		}
	}
}
