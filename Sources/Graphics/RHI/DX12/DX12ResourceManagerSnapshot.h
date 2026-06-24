#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gglab
{
	class DX12ResourceManager;

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
	};

	struct DX12ResourceSlotSnapshot
	{
		uint32_t m_Index = 0;
		uint32_t m_Generation = 0;
		DX12ResourceSnapshotState m_State = DX12ResourceSnapshotState::Free;
		DX12ResourceSnapshotOwnership m_Ownership = DX12ResourceSnapshotOwnership::Owned;
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

	void BuildDX12ResourceManagerSnapshot(
		const DX12ResourceManager& manager,
		DX12ResourceManagerSnapshot& outSnapshot) noexcept;
}
