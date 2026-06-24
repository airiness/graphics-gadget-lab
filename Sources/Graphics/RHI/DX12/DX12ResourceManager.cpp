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

		m_Textures.Clear();
		m_Buffers.Clear();

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

		++m_Diagnostics.m_TextureCreateCount;
		return AllocateTextureSlot(std::move(texture), RHIResourceOwnership::Owned, debugName);
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

		++m_Diagnostics.m_BufferCreateCount;
		return AllocateBufferSlot(std::move(buffer), RHIResourceOwnership::Owned, debugName);
	}

	RHITextureHandle DX12ResourceManager::ImportTexture(const ImportedTextureDesc& desc) noexcept
	{
		if (!desc.m_Resource)
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::ImportTexture rejected a null native resource.");
			return {};
		}

		auto texture = std::make_unique<DX12Texture>();
		texture->AdoptExternal(
			desc.m_Resource,
			ToD3D12ResourceStates(desc.m_RHI.m_External.m_InitialState));
		if (!texture->IsValid())
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR("DX12ResourceManager::ImportTexture failed to adopt the native resource.");
			return {};
		}

		const char* debugNameText = desc.m_RHI.m_External.m_DebugName ?
			desc.m_RHI.m_External.m_DebugName :
			desc.m_RHI.m_Desc.m_DebugName;
		const std::string_view debugName = debugNameText ? debugNameText : "";
		if (!debugName.empty())
		{
			const std::wstring wideName = utils::ToWideString(debugName);
			texture->SetDebugName(wideName.c_str());
		}

		++m_Diagnostics.m_TextureImportCount;
		return AllocateTextureSlot(std::move(texture), RHIResourceOwnership::Borrowed, debugName);
	}

	RHIBufferHandle DX12ResourceManager::ImportBuffer(const ImportedBufferDesc& desc) noexcept
	{
		if (!desc.m_Resource)
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::ImportBuffer rejected a null native resource.");
			return {};
		}

		auto buffer = std::make_unique<DX12Buffer>();
		buffer->AdoptExternal(
			desc.m_Resource,
			ToD3D12ResourceStates(desc.m_RHI.m_External.m_InitialState));
		if (!buffer->IsValid())
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR("DX12ResourceManager::ImportBuffer failed to adopt the native resource.");
			return {};
		}

		const char* debugNameText = desc.m_RHI.m_External.m_DebugName ?
			desc.m_RHI.m_External.m_DebugName :
			desc.m_RHI.m_Desc.m_DebugName;
		const std::string_view debugName = debugNameText ? debugNameText : "";
		if (!debugName.empty())
		{
			const std::wstring wideName = utils::ToWideString(debugName);
			buffer->SetDebugName(wideName.c_str());
		}

		++m_Diagnostics.m_BufferImportCount;
		return AllocateBufferSlot(std::move(buffer), RHIResourceOwnership::Borrowed, debugName);
	}

	void DX12ResourceManager::DestroyTexture(RHITextureHandle texture) noexcept
	{
		const RHIHandleValidationResult result = m_Textures.BeginRetirement(texture);
		switch (result)
		{
		case RHIHandleValidationResult::Valid:
		{
			TextureSlot& slot = m_Textures.SlotAt(texture.Index());
			slot.m_RetirementPoints = CaptureRetirementPoints();
			return;
		}
		case RHIHandleValidationResult::Invalid:
			++m_Diagnostics.m_InvalidDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyTexture received an invalid handle.");
			return;
		case RHIHandleValidationResult::DoubleDestroy:
			++m_Diagnostics.m_DoubleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyTexture detected a double destroy.");
			return;
		case RHIHandleValidationResult::Stale:
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyTexture received a stale handle.");
			return;
		case RHIHandleValidationResult::NonLive:
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyTexture received a non-live handle.");
			return;
		}
		GGLAB_UNREACHABLE("Unhandled RHI handle validation result.");
	}

	void DX12ResourceManager::DestroyBuffer(RHIBufferHandle buffer) noexcept
	{
		const RHIHandleValidationResult result = m_Buffers.BeginRetirement(buffer);
		switch (result)
		{
		case RHIHandleValidationResult::Valid:
		{
			BufferSlot& slot = m_Buffers.SlotAt(buffer.Index());
			slot.m_RetirementPoints = CaptureRetirementPoints();
			return;
		}
		case RHIHandleValidationResult::Invalid:
			++m_Diagnostics.m_InvalidDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyBuffer received an invalid handle.");
			return;
		case RHIHandleValidationResult::DoubleDestroy:
			++m_Diagnostics.m_DoubleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyBuffer detected a double destroy.");
			return;
		case RHIHandleValidationResult::Stale:
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyBuffer received a stale handle.");
			return;
		case RHIHandleValidationResult::NonLive:
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("DX12ResourceManager::DestroyBuffer received a non-live handle.");
			return;
		}
		GGLAB_UNREACHABLE("Unhandled RHI handle validation result.");
	}

	bool DX12ResourceManager::IsAlive(RHITextureHandle texture) const noexcept
	{
		const TextureSlot* slot = m_Textures.Resolve(texture);
		return slot != nullptr && slot->m_Texture != nullptr;
	}

	bool DX12ResourceManager::IsAlive(RHIBufferHandle buffer) const noexcept
	{
		const BufferSlot* slot = m_Buffers.Resolve(buffer);
		return slot != nullptr && slot->m_Buffer != nullptr;
	}

	DX12Texture* DX12ResourceManager::ResolveTexture(RHITextureHandle texture) noexcept
	{
		return const_cast<DX12Texture*>(std::as_const(*this).ResolveTexture(texture));
	}

	const DX12Texture* DX12ResourceManager::ResolveTexture(RHITextureHandle texture) const noexcept
	{
		const TextureSlot* slot = m_Textures.Resolve(texture);
		if (!slot || !slot->m_Texture)
		{
			return nullptr;
		}

		return slot->m_Texture.get();
	}

	DX12Buffer* DX12ResourceManager::ResolveBuffer(RHIBufferHandle buffer) noexcept
	{
		return const_cast<DX12Buffer*>(std::as_const(*this).ResolveBuffer(buffer));
	}

	const DX12Buffer* DX12ResourceManager::ResolveBuffer(RHIBufferHandle buffer) const noexcept
	{
		const BufferSlot* slot = m_Buffers.Resolve(buffer);
		if (!slot || !slot->m_Buffer)
		{
			return nullptr;
		}

		return slot->m_Buffer.get();
	}

	void DX12ResourceManager::RetireCompletedResources() noexcept
	{
		for (uint32_t index = 0; index < m_Textures.Size(); ++index)
		{
			auto& slot = m_Textures.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::PendingRetirement)
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
			slot.m_Ownership = RHIResourceOwnership::Owned;
			m_Textures.Retire(index);
			++m_Diagnostics.m_TextureRetireCount;
		}

		for (uint32_t index = 0; index < m_Buffers.Size(); ++index)
		{
			auto& slot = m_Buffers.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::PendingRetirement)
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
			slot.m_Ownership = RHIResourceOwnership::Owned;
			m_Buffers.Retire(index);
			++m_Diagnostics.m_BufferRetireCount;
		}
	}

	RHITextureHandle DX12ResourceManager::AllocateTextureSlot(
		std::unique_ptr<DX12Texture> texture,
		RHIResourceOwnership ownership,
		std::string_view debugName) noexcept
	{
		GGLAB_ASSERT_MSG(texture != nullptr, "DX12ResourceManager requires a texture wrapper.");

		const RHITextureHandle handle = m_Textures.Allocate();
		TextureSlot& slot = m_Textures.SlotAt(handle.Index());
		slot.m_Ownership = ownership;
		slot.m_DebugNameId = debugName.empty() ? StringID{} : StringID(debugName);
		slot.m_RetirementPoints.clear();
		slot.m_Texture = std::move(texture);
		return handle;
	}

	RHIBufferHandle DX12ResourceManager::AllocateBufferSlot(
		std::unique_ptr<DX12Buffer> buffer,
		RHIResourceOwnership ownership,
		std::string_view debugName) noexcept
	{
		GGLAB_ASSERT_MSG(buffer != nullptr, "DX12ResourceManager requires a buffer wrapper.");

		const RHIBufferHandle handle = m_Buffers.Allocate();
		BufferSlot& slot = m_Buffers.SlotAt(handle.Index());
		slot.m_Ownership = ownership;
		slot.m_DebugNameId = debugName.empty() ? StringID{} : StringID(debugName);
		slot.m_RetirementPoints.clear();
		slot.m_Buffer = std::move(buffer);
		return handle;
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
		for (uint32_t index = 0; index < m_Textures.Size(); ++index)
		{
			const auto& slot = m_Textures.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::Free)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12ResourceManager finalizing texture slot {} ('{}') in state {}.",
					index,
					utils::StringIdToString(slot.m_DebugNameId),
					static_cast<uint32_t>(slot.m_State));
			}
		}
		for (uint32_t index = 0; index < m_Buffers.Size(); ++index)
		{
			const auto& slot = m_Buffers.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::Free)
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
