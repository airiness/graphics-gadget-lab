#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Cache/DX12DescriptorCache.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12ResourceDescUtils.h"
#include "Graphics/RHI/RHITextureValidation.h"
#include "Core/Utility/StringUtils.h"

#include <algorithm>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool HasDebugIdentity(const RHIResourceDebugIdentityDesc& identity) noexcept
		{
			return identity.m_Domain != RHIResourceDebugDomain::Unknown ||
				!identity.m_Category.empty() || !identity.m_Label.empty() ||
				!identity.m_Source.empty() || identity.m_StableId.has_value();
		}

		[[nodiscard]] RHIResourceDebugIdentityDesc ResolveDebugIdentity(
			const RHIResourceDebugIdentityDesc& identity, std::string_view legacyName,
			RHIResourceType resourceType) noexcept
		{
			if (HasDebugIdentity(identity))
			{
				return identity;
			}
			return {
				.m_Domain = RHIResourceDebugDomain::Unknown,
				.m_Category = RHIResourceTypeDebugText(resourceType),
				.m_Label = legacyName.empty() ? std::string_view("Unspecified") : legacyName,
			};
		}
	}

	DX12ResourceManager::~DX12ResourceManager() noexcept = default;

	void DX12ResourceManager::Initialize(DX12Device* device) noexcept
	{
		GGLAB_ASSERT_MSG(device != nullptr, "DX12ResourceManager requires a valid DX12Device.");
		GGLAB_ASSERT_MSG(device->GetMemAllocator() != nullptr,
			"DX12ResourceManager requires an initialized memory allocator.");

		m_Device = device;
	}

	void DX12ResourceManager::Finalize() noexcept
	{
		ReportLiveResources();

		m_Textures.Clear();
		m_Buffers.Clear();

		m_Device = nullptr;
		m_DescriptorCache = nullptr;
	}

	RHITextureHandle DX12ResourceManager::CreateTexture(
		const RHITextureDesc& desc, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr,
			"DX12ResourceManager must be initialized before creating textures.");
		GGLAB_ASSERT_MSG(
			m_Device->GetMemAllocator() != nullptr, "DX12 memory allocator is not initialized.");

		const RHITextureValidationResult validation = ValidateRHITextureDesc(desc);
		if (!validation.IsValid())
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12ResourceManager::CreateTexture rejected the texture description: {}.",
				RHITextureValidationErrorText(validation.m_Error));
			return {};
		}

		auto texture = std::make_unique<DX12Texture>();

		DX12Resource::CreateInfo createInfo{};
		createInfo.m_Allocator = m_Device->GetMemAllocator();
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_ResourceDesc = ToD3D12ResourceDesc(desc);
		createInfo.m_EnhancedInitialLayout = D3D12_BARRIER_LAYOUT_COMMON;
		createInfo.m_ClearValue = ToD3D12ClearValue(desc.m_ClearValue);
		texture->Create(createInfo);
		if (!texture->IsValid())
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12ResourceManager::CreateTexture failed to create the native resource.");
			return {};
		}

		const std::string_view legacyName = desc.m_DebugName ? desc.m_DebugName : "";
		const auto resolvedIdentity =
			ResolveDebugIdentity(debugIdentity, legacyName, RHIResourceType::Texture);
		m_Diagnostics.m_UnnamedResourceCreateCount +=
			!HasDebugIdentity(debugIdentity) && legacyName.empty() ? 1u : 0u;
		++m_Diagnostics.m_TextureCreateCount;
		return AllocateTextureSlot(
			std::move(texture), RHIResourceOwnership::Owned, resolvedIdentity);
	}

	RHIBufferHandle DX12ResourceManager::CreateBuffer(
		const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr,
			"DX12ResourceManager must be initialized before creating buffers.");
		GGLAB_ASSERT_MSG(
			m_Device->GetMemAllocator() != nullptr, "DX12 memory allocator is not initialized.");

		if (desc.m_SizeInBytes == 0)
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12ResourceManager::CreateBuffer rejected a zero-sized buffer.");
			return {};
		}

		auto buffer = std::make_unique<DX12Buffer>();

		DX12Resource::CreateInfo createInfo{};
		createInfo.m_Allocator = m_Device->GetMemAllocator();
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_ResourceDesc = ToD3D12ResourceDesc(desc);
		// This backend records enhanced barriers. Buffers do not have a texture
		// layout and must be created with D3D12_BARRIER_LAYOUT_UNDEFINED instead
		// of a legacy initial resource state.
		createInfo.m_EnhancedInitialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
		switch (desc.m_MemoryUsage)
		{
		case RHIMemoryUsage::GpuOnly:
			createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
			break;
		case RHIMemoryUsage::CpuToGpu:
			createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
			createInfo.m_ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			break;
		case RHIMemoryUsage::GpuToCpu:
			createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
			createInfo.m_ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			break;
		}
		buffer->Create(createInfo);
		if (!buffer->IsValid())
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12ResourceManager::CreateBuffer failed to create the native resource.");
			return {};
		}

		const std::string_view legacyName = desc.m_DebugName ? desc.m_DebugName : "";
		const auto resolvedIdentity =
			ResolveDebugIdentity(debugIdentity, legacyName, RHIResourceType::Buffer);
		m_Diagnostics.m_UnnamedResourceCreateCount +=
			!HasDebugIdentity(debugIdentity) && legacyName.empty() ? 1u : 0u;
		++m_Diagnostics.m_BufferCreateCount;
		return AllocateBufferSlot(std::move(buffer), RHIResourceOwnership::Owned, resolvedIdentity);
	}

	RHITextureHandle DX12ResourceManager::ImportTexture(const ImportedTextureDesc& desc) noexcept
	{
		if (!desc.m_Resource)
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12ResourceManager::ImportTexture rejected a null native resource.");
			return {};
		}

		auto texture = std::make_unique<DX12Texture>();
		texture->AdoptExternal(
			desc.m_Resource, ToD3D12ResourceStates(desc.m_RHI.m_External.m_InitialState));
		if (!texture->IsValid())
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12ResourceManager::ImportTexture failed to adopt the native resource.");
			return {};
		}

		const char* debugNameText = desc.m_RHI.m_External.m_DebugName
			? desc.m_RHI.m_External.m_DebugName
			: desc.m_RHI.m_Desc.m_DebugName;
		const std::string_view legacyName = debugNameText ? debugNameText : "";
		const auto resolvedIdentity =
			ResolveDebugIdentity(desc.m_DebugIdentity, legacyName, RHIResourceType::Texture);
		m_Diagnostics.m_UnnamedResourceCreateCount +=
			!HasDebugIdentity(desc.m_DebugIdentity) && legacyName.empty() ? 1u : 0u;
		++m_Diagnostics.m_TextureImportCount;
		return AllocateTextureSlot(
			std::move(texture), RHIResourceOwnership::Borrowed, resolvedIdentity);
	}

	RHIBufferHandle DX12ResourceManager::ImportBuffer(const ImportedBufferDesc& desc) noexcept
	{
		if (!desc.m_Resource)
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12ResourceManager::ImportBuffer rejected a null native resource.");
			return {};
		}

		auto buffer = std::make_unique<DX12Buffer>();
		buffer->AdoptExternal(
			desc.m_Resource, ToD3D12ResourceStates(desc.m_RHI.m_External.m_InitialState));
		if (!buffer->IsValid())
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12ResourceManager::ImportBuffer failed to adopt the native resource.");
			return {};
		}

		const char* debugNameText = desc.m_RHI.m_External.m_DebugName
			? desc.m_RHI.m_External.m_DebugName
			: desc.m_RHI.m_Desc.m_DebugName;
		const std::string_view legacyName = debugNameText ? debugNameText : "";
		const auto resolvedIdentity =
			ResolveDebugIdentity(desc.m_DebugIdentity, legacyName, RHIResourceType::Buffer);
		m_Diagnostics.m_UnnamedResourceCreateCount +=
			!HasDebugIdentity(desc.m_DebugIdentity) && legacyName.empty() ? 1u : 0u;
		++m_Diagnostics.m_BufferImportCount;
		return AllocateBufferSlot(
			std::move(buffer), RHIResourceOwnership::Borrowed, resolvedIdentity);
	}

	void DX12ResourceManager::DestroyTexture(RHITextureHandle texture) noexcept
	{
		DestroyResource(m_Textures, texture, "DX12ResourceManager::DestroyTexture",
			[this, texture](TextureSlot& slot) noexcept
			{
				if (m_DescriptorCache)
				{
					m_DescriptorCache->RetireTextureViews(texture, slot.m_RetirementPoints);
				}
			});
	}

	void DX12ResourceManager::DestroyBuffer(RHIBufferHandle buffer) noexcept
	{
		DestroyResource(m_Buffers, buffer, "DX12ResourceManager::DestroyBuffer",
			[this, buffer](BufferSlot& slot) noexcept
			{
				if (m_DescriptorCache)
				{
					m_DescriptorCache->RetireBufferViews(buffer, slot.m_RetirementPoints);
				}
			});
	}

	void DX12ResourceManager::SetTextureDebugBinding(
		RHITextureHandle texture, const RHIResourceDebugBindingDesc& binding) noexcept
	{
		SetResourceDebugBinding(m_Textures, texture, RHIResourceType::Texture, binding,
			"DX12ResourceManager::SetTextureDebugBinding");
	}

	void DX12ResourceManager::SetBufferDebugBinding(
		RHIBufferHandle buffer, const RHIResourceDebugBindingDesc& binding) noexcept
	{
		SetResourceDebugBinding(m_Buffers, buffer, RHIResourceType::Buffer, binding,
			"DX12ResourceManager::SetBufferDebugBinding");
	}

	std::string_view DX12ResourceManager::GetTextureDebugName(
		RHITextureHandle texture) const noexcept
	{
		return GetResourceDebugName(m_Textures, texture);
	}

	std::string_view DX12ResourceManager::GetBufferDebugName(RHIBufferHandle buffer) const noexcept
	{
		return GetResourceDebugName(m_Buffers, buffer);
	}

	void DX12ResourceManager::RecordTextureUse(
		RHITextureHandle texture, const RHIFencePoint& fencePoint) noexcept
	{
		RecordResourceUse(
			m_Textures, texture, fencePoint, "DX12ResourceManager::RecordTextureUse", "texture");
	}

	void DX12ResourceManager::RecordBufferUse(
		RHIBufferHandle buffer, const RHIFencePoint& fencePoint) noexcept
	{
		RecordResourceUse(
			m_Buffers, buffer, fencePoint, "DX12ResourceManager::RecordBufferUse", "buffer");
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

	RHITextureHandle DX12ResourceManager::AllocateTextureSlot(std::unique_ptr<DX12Texture> texture,
		RHIResourceOwnership ownership, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		GGLAB_ASSERT_MSG(texture != nullptr, "DX12ResourceManager requires a texture wrapper.");
		return AllocateResourceSlot(
			m_Textures, std::move(texture), ownership, RHIResourceType::Texture, debugIdentity);
	}

	RHIBufferHandle DX12ResourceManager::AllocateBufferSlot(std::unique_ptr<DX12Buffer> buffer,
		RHIResourceOwnership ownership, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		GGLAB_ASSERT_MSG(buffer != nullptr, "DX12ResourceManager requires a buffer wrapper.");
		return AllocateResourceSlot(
			m_Buffers, std::move(buffer), ownership, RHIResourceType::Buffer, debugIdentity);
	}

	template <typename HandleT, typename SlotT, typename ResourceT>
	HandleT DX12ResourceManager::AllocateResourceSlot(RHIHandleTable<HandleT, SlotT>& table,
		std::unique_ptr<ResourceT> resource, RHIResourceOwnership ownership,
		RHIResourceType resourceType, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		const HandleT handle = table.Allocate();
		SlotT& slot = table.SlotAt(handle.Index());
		slot.m_Ownership = ownership;
		slot.m_DebugIdentity.Assign(debugIdentity);
		slot.m_DebugBinding = {};
		slot.m_DebugBindingHistory.clear();
		slot.m_LastUsePoints.clear();
		slot.m_RetirementPoints.clear();
		slot.m_Resource = std::move(resource);
		slot.m_DebugName = FormatRHIResourceDebugName(
			resourceType, handle.Index(), handle.Generation(), slot.m_DebugIdentity);
		const std::wstring wideName = utils::ToWideString(slot.m_DebugName);
		slot.m_Resource->SetDebugName(wideName.c_str());
		return handle;
	}

	template <typename HandleT, typename SlotT>
	void DX12ResourceManager::SetResourceDebugBinding(RHIHandleTable<HandleT, SlotT>& table,
		HandleT handle, RHIResourceType resourceType, const RHIResourceDebugBindingDesc& binding,
		const char* functionName) noexcept
	{
		SlotT* slot = table.Resolve(handle);
		if (!slot || !slot->m_Resource)
		{
			GGLAB_LOG_GRAPHICS_WARN("{} received a non-live resource handle.", functionName);
			return;
		}

		if (!slot->m_DebugBinding.IsEmpty())
		{
			slot->m_DebugBindingHistory.push_back(slot->m_DebugBinding);
			constexpr size_t MaxBindingHistory = 8;
			if (slot->m_DebugBindingHistory.size() > MaxBindingHistory)
			{
				slot->m_DebugBindingHistory.erase(slot->m_DebugBindingHistory.begin());
			}
		}
		slot->m_DebugBinding.Assign(binding);

		if (binding.m_Mode == RHIResourceDebugBindingMode::Exclusive)
		{
			slot->m_DebugName = FormatRHIResourceDebugName(resourceType, handle.Index(),
				handle.Generation(), slot->m_DebugIdentity, &slot->m_DebugBinding);
			const std::wstring wideName = utils::ToWideString(slot->m_DebugName);
			slot->m_Resource->SetDebugName(wideName.c_str());
		}
	}

	template <typename HandleT, typename SlotT>
	std::string_view DX12ResourceManager::GetResourceDebugName(
		const RHIHandleTable<HandleT, SlotT>& table, HandleT handle) noexcept
	{
		const SlotT* slot = table.Resolve(handle);
		return slot && slot->m_Resource ? std::string_view(slot->m_DebugName) : std::string_view{};
	}

	template <typename HandleT, typename SlotT, typename OnValidT>
	void DX12ResourceManager::DestroyResource(RHIHandleTable<HandleT, SlotT>& table, HandleT handle,
		const char* functionName, OnValidT onValid) noexcept
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

	template <typename HandleT, typename SlotT>
	void DX12ResourceManager::RecordResourceUse(RHIHandleTable<HandleT, SlotT>& table,
		HandleT handle, const RHIFencePoint& fencePoint, const char* functionName,
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
			GGLAB_LOG_GRAPHICS_WARN(
				"{} received a non-live {} handle.", functionName, resourceKind);
			return;
		}

		RecordLastUsePoint(slot->m_LastUsePoints, fencePoint);
	}

	template <typename HandleT, typename SlotT>
	void DX12ResourceManager::RetireCompletedResourceTable(
		RHIHandleTable<HandleT, SlotT>& table, uint64_t& retireCount) noexcept
	{
		for (uint32_t index = 0; index < table.Size(); ++index)
		{
			auto& slot = table.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::PendingRetirement)
			{
				continue;
			}

			const bool completed =
				std::ranges::all_of(slot.m_RetirementPoints, [this](const RHIFencePoint& point)
					{ return m_Device && m_Device->IsFencePointCompleted(point); });
			if (!completed)
			{
				continue;
			}

			slot.m_Resource.reset();
			slot.m_LastUsePoints.clear();
			slot.m_RetirementPoints.clear();
			slot.m_DebugIdentity = {};
			slot.m_DebugBinding = {};
			slot.m_DebugBindingHistory.clear();
			slot.m_DebugName.clear();
			slot.m_Ownership = RHIResourceOwnership::Owned;
			table.Retire(index);
			++retireCount;
		}
	}

	void DX12ResourceManager::RecordLastUsePoint(
		std::vector<RHIFencePoint>& points, const RHIFencePoint& fencePoint) noexcept
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
					"DX12ResourceManager finalizing texture slot {} ('{}') in state {}.", index,
					slot.m_DebugName, static_cast<uint32_t>(slot.m_State));
			}
		}
		for (uint32_t index = 0; index < m_Buffers.Size(); ++index)
		{
			const auto& slot = m_Buffers.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::Free)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12ResourceManager finalizing buffer slot {} ('{}') in state {}.", index,
					slot.m_DebugName, static_cast<uint32_t>(slot.m_State));
			}
		}
	}
}
