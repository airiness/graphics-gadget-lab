#pragma once
#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/RHI/RHIResourceDebug.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gglab
{
	enum class DX12ResourceSnapshotOwnership : uint8_t
	{
		Owned,
		Borrowed,
	};

	enum class DX12ResourceSnapshotState : uint8_t
	{
		Free,
		Alive,
		PendingRetirement,
	};

	struct DX12ResourceDebugBindingSnapshot
	{
		std::string m_Owner;
		uint64_t m_Serial = 0;
		bool m_HasSerial = false;
		RHIResourceDebugBindingMode m_Mode = RHIResourceDebugBindingMode::Exclusive;
	};

	struct DX12ResourceManagerDiagnosticsSnapshot
	{
		uint64_t m_TextureCreateCount = 0;
		uint64_t m_BufferCreateCount = 0;
		uint64_t m_TextureImportCount = 0;
		uint64_t m_BufferImportCount = 0;
		uint64_t m_TextureRetireCount = 0;
		uint64_t m_BufferRetireCount = 0;
		uint64_t m_CreateFailureCount = 0;
		uint64_t m_ImportFailureCount = 0;
		uint64_t m_InvalidUseCount = 0;
		uint64_t m_InvalidDestroyCount = 0;
		uint64_t m_StaleDestroyCount = 0;
		uint64_t m_DoubleDestroyCount = 0;
		uint64_t m_UnnamedResourceCreateCount = 0;
	};

	struct DX12ResourceSlotSnapshot
	{
		uint32_t m_Index = 0;
		uint32_t m_Generation = 0;
		DX12ResourceSnapshotState m_State = DX12ResourceSnapshotState::Free;
		DX12ResourceSnapshotOwnership m_Ownership = DX12ResourceSnapshotOwnership::Owned;
		RHIResourceDebugDomain m_DebugDomain = RHIResourceDebugDomain::Unknown;
		std::string m_DebugCategory;
		std::string m_DebugLabel;
		std::string m_DebugSource;
		uint64_t m_DebugStableId = 0;
		bool m_HasDebugStableId = false;
		std::string m_DebugOwner;
		uint64_t m_DebugBindingSerial = 0;
		bool m_HasDebugBindingSerial = false;
		RHIResourceDebugBindingMode m_DebugBindingMode = RHIResourceDebugBindingMode::Exclusive;
		std::vector<DX12ResourceDebugBindingSnapshot> m_DebugBindingHistory;
		std::string m_DebugName;
		uint32_t m_LastUseFenceCount = 0;
		uint32_t m_CompletedLastUseFenceCount = 0;
		uint32_t m_PendingFenceCount = 0;
		uint32_t m_CompletedFenceCount = 0;
		bool m_NativeResourceValid = false;
	};

	struct DX12ResourceManagerSnapshot
	{
		DX12ResourceManagerDiagnosticsSnapshot m_Diagnostics;
		std::vector<DX12ResourceSlotSnapshot> m_Textures;
		std::vector<DX12ResourceSlotSnapshot> m_Buffers;
	};

	template <> struct SnapshotTraits<DX12ResourceManagerSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.DX12ResourceManagerSnapshot");
	};
}
