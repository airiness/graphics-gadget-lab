#include "Core/Precompiled.h"
#include "Diagnostics/Builders/DX12ResourceManagerSnapshotBuilder.h"
#include "Diagnostics/Snapshots/DX12ResourceManagerSnapshot.h"
#include "Graphics/RHI/DX12/DX12Device.h"
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
			.m_TextureImportCount = manager.m_Diagnostics.m_TextureImportCount,
			.m_BufferImportCount = manager.m_Diagnostics.m_BufferImportCount,
			.m_TextureRetireCount = manager.m_Diagnostics.m_TextureRetireCount,
			.m_BufferRetireCount = manager.m_Diagnostics.m_BufferRetireCount,
			.m_CreateFailureCount = manager.m_Diagnostics.m_CreateFailureCount,
			.m_ImportFailureCount = manager.m_Diagnostics.m_ImportFailureCount,
			.m_InvalidUseCount = manager.m_Diagnostics.m_InvalidUseCount,
			.m_InvalidDestroyCount = manager.m_Diagnostics.m_InvalidDestroyCount,
			.m_StaleDestroyCount = manager.m_Diagnostics.m_StaleDestroyCount,
			.m_DoubleDestroyCount = manager.m_Diagnostics.m_DoubleDestroyCount,
		};

		const auto toSnapshotState = [](RHIHandleSlotState state) noexcept
			{
				switch (state)
				{
				case RHIHandleSlotState::Free:
					return DX12ResourceSnapshotState::Free;
				case RHIHandleSlotState::Alive:
					return DX12ResourceSnapshotState::Alive;
				case RHIHandleSlotState::PendingRetirement:
					return DX12ResourceSnapshotState::PendingRetirement;
				}
				GGLAB_UNREACHABLE("Unhandled resource slot state.");
			};

		const auto toSnapshotOwnership = [](RHIResourceOwnership ownership) noexcept
			{
				switch (ownership)
				{
				case RHIResourceOwnership::Owned:
					return DX12ResourceSnapshotOwnership::Owned;
				case RHIResourceOwnership::Borrowed:
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
				result.m_LastUseFenceCount = static_cast<uint32_t>(slot.m_LastUsePoints.size());
				result.m_CompletedLastUseFenceCount = static_cast<uint32_t>(std::ranges::count_if(
					slot.m_LastUsePoints,
					[&manager](const RHIFencePoint& point)
					{
						return manager.m_Device && manager.m_Device->IsFencePointCompleted(point);
					}));
				result.m_PendingFenceCount = static_cast<uint32_t>(slot.m_RetirementPoints.size());
				result.m_CompletedFenceCount = static_cast<uint32_t>(std::ranges::count_if(
					slot.m_RetirementPoints,
					[&manager](const RHIFencePoint& point)
					{
						return manager.m_Device && manager.m_Device->IsFencePointCompleted(point);
					}));
				result.m_NativeResourceValid = resource != nullptr && resource->IsValid();
				return result;
			};

		outSnapshot.m_Textures.reserve(manager.m_Textures.Slots().size());
		for (uint32_t index = 0; index < manager.m_Textures.Size(); ++index)
		{
			const auto& slot = manager.m_Textures.SlotAt(index);
			outSnapshot.m_Textures.push_back(makeSnapshot(index, slot, slot.m_Resource.get()));
		}

		outSnapshot.m_Buffers.reserve(manager.m_Buffers.Slots().size());
		for (uint32_t index = 0; index < manager.m_Buffers.Size(); ++index)
		{
			const auto& slot = manager.m_Buffers.SlotAt(index);
			outSnapshot.m_Buffers.push_back(makeSnapshot(index, slot, slot.m_Resource.get()));
		}
	}
}
