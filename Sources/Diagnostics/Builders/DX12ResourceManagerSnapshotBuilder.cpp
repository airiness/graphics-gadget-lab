#include "Diagnostics/Builders/DX12ResourceManagerSnapshotBuilder.h"
#include "Diagnostics/Snapshots/DX12ResourceManagerSnapshot.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12Texture.h"

#include <algorithm>

namespace gglab
{
	void BuildDX12ResourceManagerSnapshot(
		const DX12ResourceManager& manager, DX12ResourceManagerSnapshot& outSnapshot) noexcept
	{
		outSnapshot = {};

		outSnapshot.m_Diagnostics = {
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
			.m_UnnamedResourceCreateCount = manager.m_Diagnostics.m_UnnamedResourceCreateCount,
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
				result.m_DebugDomain = slot.m_DebugIdentity.m_Domain;
				result.m_DebugCategory = slot.m_DebugIdentity.m_Category;
				result.m_DebugLabel = slot.m_DebugIdentity.m_Label;
				result.m_DebugSource = slot.m_DebugIdentity.m_Source;
				result.m_HasDebugStableId = slot.m_DebugIdentity.m_StableId.has_value();
				result.m_DebugStableId = slot.m_DebugIdentity.m_StableId.value_or(0);
				result.m_DebugOwner = slot.m_DebugBinding.m_Owner;
				result.m_HasDebugBindingSerial = slot.m_DebugBinding.m_Serial.has_value();
				result.m_DebugBindingSerial = slot.m_DebugBinding.m_Serial.value_or(0);
				result.m_DebugBindingMode = slot.m_DebugBinding.m_Mode;
				result.m_DebugBindingHistory.reserve(slot.m_DebugBindingHistory.size());
				for (const auto& binding : slot.m_DebugBindingHistory)
				{
					result.m_DebugBindingHistory.push_back({
						.m_Owner = binding.m_Owner,
						.m_Serial = binding.m_Serial.value_or(0),
						.m_HasSerial = binding.m_Serial.has_value(),
						.m_Mode = binding.m_Mode,
						});
				}
				result.m_DebugName = slot.m_DebugName;
				result.m_LastUseFenceCount = static_cast<uint32_t>(slot.m_LastUsePoints.size());
				result.m_CompletedLastUseFenceCount = static_cast<uint32_t>(std::ranges::count_if(
					slot.m_LastUsePoints, [&manager](const RHIFencePoint& point)
					{ return manager.m_Device && manager.m_Device->IsFencePointCompleted(point); }));
				result.m_PendingFenceCount = static_cast<uint32_t>(slot.m_RetirementPoints.size());
				result.m_CompletedFenceCount = static_cast<uint32_t>(std::ranges::count_if(
					slot.m_RetirementPoints, [&manager](const RHIFencePoint& point)
					{ return manager.m_Device && manager.m_Device->IsFencePointCompleted(point); }));
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
