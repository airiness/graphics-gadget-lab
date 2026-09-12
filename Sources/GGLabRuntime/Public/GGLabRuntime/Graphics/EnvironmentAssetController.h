#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Asset/TextureAssetViews.h"
#include "GGLabRuntime/Graphics/EnvironmentSelectionControlBase.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	class AssetManager;
	class AssetOwnerScope;
	class EnvironmentLightingSystem;
	struct RHITextureDesc;

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

	class EnvironmentAssetController : public EnvironmentSelectionControlBase
	{
	public:
		static constexpr size_t InvalidEntryIndex = std::numeric_limits<size_t>::max();

		struct CreateInfo
		{
			AssetManager* m_AssetManager = nullptr;
			EnvironmentLightingSystem* m_EnvironmentLighting = nullptr;
			std::filesystem::path m_AssetRoot;
		};

		explicit EnvironmentAssetController(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(EnvironmentAssetController);
		~EnvironmentAssetController() override;

		void Initialize(const std::filesystem::path& rootDirectory) noexcept;
		void Reset() noexcept;
		void Tick() noexcept;

		[[nodiscard]] bool SelectDefaultEnvironment() noexcept;
		[[nodiscard]] bool SelectEnvironment(size_t entryIndex) noexcept override;
		[[nodiscard]] bool SelectEnvironmentFile(
			const std::filesystem::path& path, std::string_view displayName = {}) noexcept;

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
		std::filesystem::path m_AssetRoot;
		std::vector<EnvironmentMapEntry> m_Entries;
		// Owner leases stay opaque so the Public header does not expose the asset
		// subsystem's ownership implementation.
		std::unique_ptr<AssetOwnerScope> m_ActiveOwner;
		std::unique_ptr<AssetOwnerScope> m_PendingOwner;
		PendingSelection m_PendingSelection{};
		size_t m_ActiveEntryIndex = InvalidEntryIndex;
		uint64_t m_SelectionSerial = 0;
	};
}
