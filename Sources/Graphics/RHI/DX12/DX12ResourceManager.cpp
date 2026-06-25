#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Cache/DX12ViewCache.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12ResourceDescUtils.h"
#include "Core/Utility/StringUtils.h"

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
		m_ViewCache = nullptr;
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
		DestroyResource(
			m_Textures,
			texture,
			"DX12ResourceManager::DestroyTexture",
			[this, texture](TextureSlot& slot) noexcept
			{
				if (m_ViewCache)
				{
					m_ViewCache->RetireTextureViews(texture, slot.m_RetirementPoints);
				}
			});
	}

	void DX12ResourceManager::DestroyBuffer(RHIBufferHandle buffer) noexcept
	{
		DestroyResource(
			m_Buffers,
			buffer,
			"DX12ResourceManager::DestroyBuffer",
			[this, buffer](BufferSlot& slot) noexcept
			{
				if (m_ViewCache)
				{
					m_ViewCache->RetireBufferViews(buffer, slot.m_RetirementPoints);
				}
			});
	}

	void DX12ResourceManager::RecordTextureUse(RHITextureHandle texture, const RHIFencePoint& fencePoint) noexcept
	{
		RecordResourceUse(
			m_Textures,
			texture,
			fencePoint,
			"DX12ResourceManager::RecordTextureUse",
			"texture");
	}

	void DX12ResourceManager::RecordBufferUse(RHIBufferHandle buffer, const RHIFencePoint& fencePoint) noexcept
	{
		RecordResourceUse(
			m_Buffers,
			buffer,
			fencePoint,
			"DX12ResourceManager::RecordBufferUse",
			"buffer");
	}

	bool DX12ResourceManager::IsAlive(RHITextureHandle texture) const noexcept
	{
		const TextureSlot* slot = m_Textures.Resolve(texture);
		return slot != nullptr && slot->m_Resource != nullptr;
	}

	bool DX12ResourceManager::IsAlive(RHIBufferHandle buffer) const noexcept
	{
		const BufferSlot* slot = m_Buffers.Resolve(buffer);
		return slot != nullptr && slot->m_Resource != nullptr;
	}

	DX12Texture* DX12ResourceManager::ResolveTexture(RHITextureHandle texture) noexcept
	{
		return const_cast<DX12Texture*>(std::as_const(*this).ResolveTexture(texture));
	}

	const DX12Texture* DX12ResourceManager::ResolveTexture(RHITextureHandle texture) const noexcept
	{
		const TextureSlot* slot = m_Textures.Resolve(texture);
		if (!slot || !slot->m_Resource)
		{
			return nullptr;
		}

		return slot->m_Resource.get();
	}

	DX12Buffer* DX12ResourceManager::ResolveBuffer(RHIBufferHandle buffer) noexcept
	{
		return const_cast<DX12Buffer*>(std::as_const(*this).ResolveBuffer(buffer));
	}

	const DX12Buffer* DX12ResourceManager::ResolveBuffer(RHIBufferHandle buffer) const noexcept
	{
		const BufferSlot* slot = m_Buffers.Resolve(buffer);
		if (!slot || !slot->m_Resource)
		{
			return nullptr;
		}

		return slot->m_Resource.get();
	}

	void DX12ResourceManager::RetireCompletedResources() noexcept
	{
		RetireCompletedResourceTable(m_Textures, m_Diagnostics.m_TextureRetireCount);
		RetireCompletedResourceTable(m_Buffers, m_Diagnostics.m_BufferRetireCount);
	}

	RHITextureHandle DX12ResourceManager::AllocateTextureSlot(
		std::unique_ptr<DX12Texture> texture,
		RHIResourceOwnership ownership,
		std::string_view debugName) noexcept
	{
		GGLAB_ASSERT_MSG(texture != nullptr, "DX12ResourceManager requires a texture wrapper.");
		return AllocateResourceSlot(m_Textures, std::move(texture), ownership, debugName);
	}

	RHIBufferHandle DX12ResourceManager::AllocateBufferSlot(
		std::unique_ptr<DX12Buffer> buffer,
		RHIResourceOwnership ownership,
		std::string_view debugName) noexcept
	{
		GGLAB_ASSERT_MSG(buffer != nullptr, "DX12ResourceManager requires a buffer wrapper.");
		return AllocateResourceSlot(m_Buffers, std::move(buffer), ownership, debugName);
	}

	template<typename HandleT, typename SlotT, typename ResourceT>
	HandleT DX12ResourceManager::AllocateResourceSlot(
		RHIHandleTable<HandleT, SlotT>& table,
		std::unique_ptr<ResourceT> resource,
		RHIResourceOwnership ownership,
		std::string_view debugName) noexcept
	{
		const HandleT handle = table.Allocate();
		SlotT& slot = table.SlotAt(handle.Index());
		slot.m_Ownership = ownership;
		slot.m_DebugNameId = debugName.empty() ? StringID{} : StringID(debugName);
		slot.m_LastUsePoints.clear();
		slot.m_RetirementPoints.clear();
		slot.m_Resource = std::move(resource);
		return handle;
	}

	template<typename HandleT, typename SlotT, typename OnValidT>
	void DX12ResourceManager::DestroyResource(
		RHIHandleTable<HandleT, SlotT>& table,
		HandleT handle,
		const char* functionName,
		OnValidT onValid) noexcept
	{
		const RHIHandleValidationResult result = table.BeginRetirement(handle);
		switch (result)
		{
		case RHIHandleValidationResult::Valid:
		{
			SlotT& slot = table.SlotAt(handle.Index());
			slot.m_RetirementPoints = slot.m_LastUsePoints;
			slot.m_LastUsePoints.clear();
			onValid(slot);
			return;
		}
		case RHIHandleValidationResult::Invalid:
			++m_Diagnostics.m_InvalidDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} received an invalid handle.", functionName);
			return;
		case RHIHandleValidationResult::DoubleDestroy:
			++m_Diagnostics.m_DoubleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} detected a double destroy.", functionName);
			return;
		case RHIHandleValidationResult::Stale:
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} received a stale handle.", functionName);
			return;
		case RHIHandleValidationResult::NonLive:
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} received a non-live handle.", functionName);
			return;
		}
		GGLAB_UNREACHABLE("Unhandled RHI handle validation result.");
	}

	template<typename HandleT, typename SlotT>
	void DX12ResourceManager::RecordResourceUse(
		RHIHandleTable<HandleT, SlotT>& table,
		HandleT handle,
		const RHIFencePoint& fencePoint,
		const char* functionName,
		const char* resourceKind) noexcept
	{
		if (!fencePoint.IsValid())
		{
			return;
		}

		SlotT* slot = table.Resolve(handle);
		if (!slot || !slot->m_Resource)
		{
			++m_Diagnostics.m_InvalidUseCount;
			GGLAB_LOG_GRAPHICS_WARN("{} received a non-live {} handle.", functionName, resourceKind);
			return;
		}

		RecordLastUsePoint(slot->m_LastUsePoints, fencePoint);
	}

	template<typename HandleT, typename SlotT>
	void DX12ResourceManager::RetireCompletedResourceTable(
		RHIHandleTable<HandleT, SlotT>& table,
		uint64_t& retireCount) noexcept
	{
		for (uint32_t index = 0; index < table.Size(); ++index)
		{
			auto& slot = table.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::PendingRetirement)
			{
				continue;
			}

			const bool completed = std::ranges::all_of(
				slot.m_RetirementPoints,
				[this](const RHIFencePoint& point)
				{
					return m_Device && m_Device->IsFencePointCompleted(point);
				});
			if (!completed)
			{
				continue;
			}

			slot.m_Resource.reset();
			slot.m_LastUsePoints.clear();
			slot.m_RetirementPoints.clear();
			slot.m_DebugNameId = {};
			slot.m_Ownership = RHIResourceOwnership::Owned;
			table.Retire(index);
			++retireCount;
		}
	}

	void DX12ResourceManager::RecordLastUsePoint(
		std::vector<RHIFencePoint>& points,
		const RHIFencePoint& fencePoint) noexcept
	{
		if (!fencePoint.IsValid())
		{
			return;
		}

		for (RHIFencePoint& point : points)
		{
			if (point.m_Fence == fencePoint.m_Fence)
			{
				if (point.m_Value < fencePoint.m_Value)
				{
					point = fencePoint;
				}
				return;
			}
		}

		points.push_back(fencePoint);
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
