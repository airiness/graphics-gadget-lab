#pragma once
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/EnvironmentLightingSystem.h"

#include <limits>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	enum class EnvironmentAssetEntryState : uint8_t
	{
		Unrequested,
		Loading,
		Ready,
		Failed,
		InvalidShape,
	};

	struct EnvironmentMapEntry
	{
		std::filesystem::path m_Path;
		std::string m_DisplayName;
		TextureContentRef m_Content{};
		uint64_t m_LastSelectionSerial = 0;
		EnvironmentAssetEntryState m_State = EnvironmentAssetEntryState::Unrequested;
	};

	class EnvironmentAssetController
	{
	public:
		static constexpr size_t InvalidEntryIndex = std::numeric_limits<size_t>::max();

		struct CreateInfo
		{
			AssetManager* m_AssetManager = nullptr;
			EnvironmentLightingSystem* m_EnvironmentLighting = nullptr;
		};

		explicit EnvironmentAssetController(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(EnvironmentAssetController);
		~EnvironmentAssetController();

		void Initialize(const std::filesystem::path& rootDirectory) noexcept;
		void Reset() noexcept;
		void Tick() noexcept;

		[[nodiscard]] bool SelectDefaultEnvironment() noexcept;
		[[nodiscard]] bool SelectEnvironment(size_t entryIndex) noexcept;
		[[nodiscard]] bool SelectEnvironmentFile(
			const std::filesystem::path& path,
			std::string_view displayName = {}) noexcept;

		[[nodiscard]] std::span<const EnvironmentMapEntry> GetEntries() const noexcept
		{
			return m_Entries;
		}
		[[nodiscard]] const EnvironmentMapEntry* GetActiveEnvironment() const noexcept;
		[[nodiscard]] size_t GetActiveEnvironmentIndex() const noexcept
		{
			return m_ActiveEntryIndex;
		}
		[[nodiscard]] size_t GetPendingEnvironmentIndex() const noexcept;
		[[nodiscard]] uint64_t GetSelectionSerial() const noexcept { return m_SelectionSerial; }
		[[nodiscard]] bool IsEntryTextureReady(size_t entryIndex) const noexcept;

	private:
		struct PendingSelection
		{
			size_t m_EntryIndex = InvalidEntryIndex;
			TextureContentRef m_Content{};
			uint64_t m_Serial = 0;

			[[nodiscard]] bool IsValid() const noexcept
			{
				return m_EntryIndex != InvalidEntryIndex && m_Content.IsValid() && m_Serial != 0;
			}
		};

		void CommitFallback() noexcept;
		void CommitPending() noexcept;
		void RejectPending(EnvironmentAssetEntryState state, std::string_view reason) noexcept;
		[[nodiscard]] bool ValidateEnvironmentShape(const RHITextureDesc& desc) const noexcept;

		AssetManager* m_AssetManager = nullptr;
		EnvironmentLightingSystem* m_EnvironmentLighting = nullptr;
		std::vector<EnvironmentMapEntry> m_Entries;
		AssetOwnerScope m_ActiveOwner;
		AssetOwnerScope m_PendingOwner;
		PendingSelection m_PendingSelection{};
		size_t m_ActiveEntryIndex = InvalidEntryIndex;
		uint64_t m_SelectionSerial = 0;
	};
}
