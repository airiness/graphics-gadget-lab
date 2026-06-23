#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12ResourceManagerSnapshot.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Core/Utility/StringUtils.h"

#include <algorithm>

namespace gglab
{
	void BuildDX12ResourceManagerSnapshot(
		const DX12ResourceManager& manager,
		DX12ResourceManagerSnapshot& outSnapshot) noexcept
	{
		outSnapshot = {};

		outSnapshot.m_Diagnostics =
		{
			.m_TextureCreateCount = manager.m_Diagnostics.m_TextureCreateCount,
			.m_BufferCreateCount = manager.m_Diagnostics.m_BufferCreateCount,
			.m_TextureRetireCount = manager.m_Diagnostics.m_TextureRetireCount,
			.m_BufferRetireCount = manager.m_Diagnostics.m_BufferRetireCount,
			.m_CreateFailureCount = manager.m_Diagnostics.m_CreateFailureCount,
			.m_InvalidDestroyCount = manager.m_Diagnostics.m_InvalidDestroyCount,
			.m_StaleDestroyCount = manager.m_Diagnostics.m_StaleDestroyCount,
			.m_DoubleDestroyCount = manager.m_Diagnostics.m_DoubleDestroyCount,
		};

		const auto toSnapshotState = [](DX12ResourceManager::ResourceSlotState state) noexcept
			{
				switch (state)
				{
				case DX12ResourceManager::ResourceSlotState::Free:
					return DX12ResourceSnapshotState::Free;
				case DX12ResourceManager::ResourceSlotState::Alive:
					return DX12ResourceSnapshotState::Alive;
				case DX12ResourceManager::ResourceSlotState::PendingRetirement:
					return DX12ResourceSnapshotState::PendingRetirement;
				}
				GGLAB_UNREACHABLE("Unhandled resource slot state.");
			};

		const auto toSnapshotOwnership = [](DX12ResourceManager::ResourceOwnership ownership) noexcept
			{
				switch (ownership)
				{
				case DX12ResourceManager::ResourceOwnership::Owned:
					return DX12ResourceSnapshotOwnership::Owned;
				case DX12ResourceManager::ResourceOwnership::Borrowed:
					return DX12ResourceSnapshotOwnership::Borrowed;
				}
				GGLAB_UNREACHABLE("Unhandled resource ownership.");
			};

		const auto makeSnapshot = [&](uint32_t index, const auto& slot, const auto* resource)
			{
				DX12ResourceSlotSnapshot result{};
				result.m_Index = index;
				result.m_Generation = slot.m_Generation;
				result.m_State = toSnapshotState(slot.m_State);
				result.m_Ownership = toSnapshotOwnership(slot.m_Ownership);
				result.m_DebugName = utils::StringIdToString(slot.m_DebugNameId);
				result.m_PendingFenceCount = static_cast<uint32_t>(slot.m_RetirementPoints.size());
				result.m_CompletedFenceCount = static_cast<uint32_t>(std::ranges::count_if(
					slot.m_RetirementPoints,
					[](const DX12FencePoint& point)
					{
						return !point.IsValid() || point.IsCompleted();
					}));
				result.m_NativeResourceValid = resource != nullptr && resource->IsValid();
				return result;
			};

		outSnapshot.m_Textures.reserve(manager.m_TextureSlots.size());
		for (uint32_t index = 0; index < static_cast<uint32_t>(manager.m_TextureSlots.size()); ++index)
		{
			const auto& slot = manager.m_TextureSlots[index];
			outSnapshot.m_Textures.push_back(makeSnapshot(index, slot, slot.m_Texture.get()));
		}

		outSnapshot.m_Buffers.reserve(manager.m_BufferSlots.size());
		for (uint32_t index = 0; index < static_cast<uint32_t>(manager.m_BufferSlots.size()); ++index)
		{
			const auto& slot = manager.m_BufferSlots[index];
			outSnapshot.m_Buffers.push_back(makeSnapshot(index, slot, slot.m_Buffer.get()));
		}
	}
}
