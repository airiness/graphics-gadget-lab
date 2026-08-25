#include "Graphics/EnvironmentAssetController.h"
#include "Graphics/Asset/AssetPaths.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Log/LogMacros.h"
#include "GGLabFoundation/IO/PathUtils.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gglab
{
	EnvironmentAssetController::EnvironmentAssetController(const CreateInfo& createInfo) noexcept :
		m_AssetManager(createInfo.m_AssetManager),
		m_EnvironmentLighting(createInfo.m_EnvironmentLighting),
		m_AssetRoot(createInfo.m_AssetRoot)
	{
		GGLAB_ASSERT_NOT_NULL(m_AssetManager);
		GGLAB_ASSERT_NOT_NULL(m_EnvironmentLighting);
		GGLAB_ASSERT_MSG(!m_AssetRoot.empty() && m_AssetRoot.is_absolute(),
			"EnvironmentAssetController requires an absolute asset root.");
	}

	EnvironmentAssetController::~EnvironmentAssetController()
	{
		Reset();
	}

	void EnvironmentAssetController::Initialize(const std::filesystem::path& rootDirectory) noexcept
	{
		Reset();
		m_Entries.clear();
		const std::filesystem::path resolvedRoot = ResolveAssetPath(m_AssetRoot, rootDirectory);

		std::error_code errorCode;
		const bool directoryAvailable = std::filesystem::is_directory(resolvedRoot, errorCode);
		if (!directoryAvailable)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"EnvironmentAssetController: environment directory '{}' is unavailable; using procedural fallback.",
				resolvedRoot.string());
			return;
		}

		for (std::filesystem::directory_iterator iterator(resolvedRoot, errorCode), end;
			iterator != end && !errorCode; iterator.increment(errorCode))
		{
			const auto& entry = *iterator;
			if (!entry.is_regular_file(errorCode) || errorCode ||
				!utils::ExtensionEqualsAsciiIgnoreCase(entry.path(), ".hdr"))
			{
				continue;
			}
			m_Entries.push_back({
				.m_Path = entry.path(),
				.m_DisplayName = entry.path().stem().string(),
				});
		}

		if (errorCode)
		{
			GGLAB_LOG_GRAPHICS_WARN("EnvironmentAssetController: failed while scanning '{}': {}.",
				resolvedRoot.string(), errorCode.message());
		}
		std::ranges::sort(m_Entries,
			[](const EnvironmentMapEntry& lhs, const EnvironmentMapEntry& rhs) noexcept
			{ return lhs.m_Path.generic_string() < rhs.m_Path.generic_string(); });

		if (m_Entries.empty())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"EnvironmentAssetController: no valid HDR environment was found in '{}'; using procedural fallback.",
				resolvedRoot.string());
			return;
		}
		GGLAB_UNUSED(SelectDefaultEnvironment());
	}

	void EnvironmentAssetController::Reset() noexcept
	{
		if (!m_AssetManager || !m_EnvironmentLighting)
		{
			return;
		}

		CommitFallback();
		AssetOwnerScope oldActive = std::move(m_ActiveOwner);
		AssetOwnerScope oldPending = std::move(m_PendingOwner);
		m_ActiveEntryIndex = InvalidEntryIndex;
		m_PendingSelection = {};
		oldPending.Reset();
		oldActive.Reset();
	}

	void EnvironmentAssetController::Tick() noexcept
	{
		if (!m_PendingSelection.IsValid())
		{
			return;
		}
		if (m_PendingSelection.m_Serial != m_SelectionSerial)
		{
			RejectPending(EnvironmentAssetEntryState::Unrequested, "stale selection serial");
			return;
		}

		const std::optional<AssetState> state =
			m_AssetManager->GetTextureState(m_PendingSelection.m_Content);
		if (!state)
		{
			RejectPending(EnvironmentAssetEntryState::Failed, "stale texture content");
			return;
		}
		if (*state == AssetState::Failed || *state == AssetState::Cancelled)
		{
			RejectPending(EnvironmentAssetEntryState::Failed, "texture load failed");
			return;
		}
		if (*state != AssetState::Ready)
		{
			return;
		}

		const auto resource =
			m_AssetManager->GetResidentTextureResource(m_PendingSelection.m_Content);
		if (!resource)
		{
			RejectPending(EnvironmentAssetEntryState::Failed, "resident resource unavailable");
			return;
		}
		if (!ValidateEnvironmentShape(resource->m_Desc))
		{
			RejectPending(
				EnvironmentAssetEntryState::InvalidShape, "texture is not a 2:1 2D environment");
			return;
		}
		CommitPending();
	}

	bool EnvironmentAssetController::SelectDefaultEnvironment() noexcept
	{
		if (m_ActiveEntryIndex < m_Entries.size())
		{
			return true;
		}
		return !m_Entries.empty() && SelectEnvironment(0);
	}

	bool EnvironmentAssetController::SelectEnvironment(size_t entryIndex) noexcept
	{
		if (entryIndex >= m_Entries.size())
		{
			return false;
		}
		if (m_ActiveEntryIndex == entryIndex && !m_PendingSelection.IsValid())
		{
			return true;
		}
		if (m_PendingSelection.m_EntryIndex == entryIndex)
		{
			return true;
		}

		++m_SelectionSerial;
		GGLAB_ASSERT_MSG(m_SelectionSerial != 0, "Environment selection serial overflowed.");
		if (m_SelectionSerial == 0)
		{
			return false;
		}

		// A new selection command supersedes the previous candidate even when the
		// replacement fails before an asynchronous load can be submitted.
		AssetOwnerScope supersededOwner = std::move(m_PendingOwner);
		if (m_PendingSelection.m_EntryIndex < m_Entries.size())
		{
			auto& superseded = m_Entries[m_PendingSelection.m_EntryIndex];
			if (superseded.m_State == EnvironmentAssetEntryState::Loading)
			{
				superseded.m_State = EnvironmentAssetEntryState::Unrequested;
			}
		}
		m_PendingSelection = {};
		supersededOwner.Reset();
		auto& entry = m_Entries[entryIndex];
		entry.m_LastSelectionSerial = m_SelectionSerial;

		AssetOwnerScope pendingOwner = m_AssetManager->CreateOwnerScope();
		const AssetManager::TextureLoadRequest request = pendingOwner.LoadTextureAsync(
			entry.m_Path, TextureSemantic::Environment, TaskPriority::High);
		if (!request.IsValid())
		{
			entry.m_State = EnvironmentAssetEntryState::Failed;
			return false;
		}

		m_PendingOwner = std::move(pendingOwner);
		m_PendingSelection = {
			.m_EntryIndex = entryIndex,
			.m_Content =
				{
					.m_Id = request.m_TextureId,
					.m_Generation = request.m_Generation,
				},
			.m_Serial = m_SelectionSerial,
		};
		entry.m_Content = m_PendingSelection.m_Content;
		entry.m_State = EnvironmentAssetEntryState::Loading;
		GGLAB_LOG_GRAPHICS_INFO(
			"EnvironmentAssetController: loading environment candidate '{}' (selection {}).",
			entry.m_Path.string(), m_SelectionSerial);
		return true;
	}

	bool EnvironmentAssetController::SelectEnvironmentFile(
		const std::filesystem::path& path, std::string_view displayName) noexcept
	{
		const std::filesystem::path resolvedPath = ResolveAssetPath(m_AssetRoot, path);
		if (resolvedPath.empty())
		{
			return false;
		}
		const auto existing =
			std::ranges::find(m_Entries, resolvedPath, &EnvironmentMapEntry::m_Path);
		if (existing != m_Entries.end())
		{
			return SelectEnvironment(static_cast<size_t>(existing - m_Entries.begin()));
		}
		m_Entries.push_back({
			.m_Path = resolvedPath,
			.m_DisplayName = displayName.empty()
				? resolvedPath.stem().string()
				: std::string(displayName),
			});
		return SelectEnvironment(m_Entries.size() - 1);
	}

	const EnvironmentMapEntry* EnvironmentAssetController::GetActiveEnvironment() const noexcept
	{
		return m_ActiveEntryIndex < m_Entries.size() ? &m_Entries[m_ActiveEntryIndex] : nullptr;
	}

	size_t EnvironmentAssetController::GetPendingEnvironmentIndex() const noexcept
	{
		return m_PendingSelection.IsValid() ? m_PendingSelection.m_EntryIndex : InvalidEntryIndex;
	}

	void EnvironmentAssetController::CommitFallback() noexcept
	{
		const TextureID fallbackId =
			ToTextureId(ReservedTextureIDIndex::FallbackEnvironmentCubemap);
		const TextureContentRef content = m_AssetManager->GetTextureContentRef(fallbackId);
		const auto contentFingerprint = m_AssetManager->GetTextureContentFingerprint(content);
		GGLAB_ASSERT_MSG(m_AssetManager->GetResidentTextureResource(content).has_value(),
			"Environment fallback cubemap must be resident before controller reset.");
		if (!content.IsValid() || !contentFingerprint)
		{
			return;
		}
		m_EnvironmentLighting->CommitEnvironmentSource({
			.m_Content = content,
			.m_Type = EnvironmentTextureSourceType::Cubemap,
			.m_ContentFingerprint = *contentFingerprint,
			});
	}

	void EnvironmentAssetController::CommitPending() noexcept
	{
		GGLAB_ASSERT(m_PendingSelection.IsValid());
		const size_t entryIndex = m_PendingSelection.m_EntryIndex;
		const TextureContentRef content = m_PendingSelection.m_Content;
		const auto contentFingerprint = m_AssetManager->GetTextureContentFingerprint(content);
		GGLAB_ASSERT_MSG(contentFingerprint.has_value(),
			"A ready environment texture must have decoded-content provenance.");
		if (!contentFingerprint)
		{
			RejectPending(
				EnvironmentAssetEntryState::Failed, "decoded-content fingerprint unavailable");
			return;
		}
		AssetOwnerScope oldActive = std::move(m_ActiveOwner);

		// The candidate lease becomes active before the visible source changes.
		m_ActiveOwner = std::move(m_PendingOwner);
		m_ActiveEntryIndex = entryIndex;
		m_PendingSelection = {};
		auto& entry = m_Entries[entryIndex];
		entry.m_State = EnvironmentAssetEntryState::Ready;
		m_EnvironmentLighting->CommitEnvironmentSource({
			.m_Content = content,
			.m_Type = EnvironmentTextureSourceType::Equirectangular,
			.m_ContentFingerprint = *contentFingerprint,
			});

		// Release the previous source only after the new source and lease are committed.
		oldActive.Reset();
		GGLAB_LOG_GRAPHICS_INFO(
			"EnvironmentAssetController: committed HDR environment '{}'.", entry.m_Path.string());
	}

	void EnvironmentAssetController::RejectPending(
		EnvironmentAssetEntryState state, std::string_view reason) noexcept
	{
		if (m_PendingSelection.m_EntryIndex < m_Entries.size())
		{
			auto& entry = m_Entries[m_PendingSelection.m_EntryIndex];
			entry.m_State = state;
			GGLAB_LOG_GRAPHICS_WARN(
				"EnvironmentAssetController: rejected environment candidate '{}': {}.",
				entry.m_Path.string(), reason);
		}
		m_PendingSelection = {};
		m_PendingOwner = {};
	}

	bool EnvironmentAssetController::ValidateEnvironmentShape(
		const RHITextureDesc& desc) const noexcept
	{
		return desc.m_Dimension == RHITextureDimension::Texture2D && desc.m_ArraySize == 1 &&
			desc.m_Extent.m_Depth == 1 &&
			static_cast<uint64_t>(desc.m_Extent.m_Height) * 2u == desc.m_Extent.m_Width;
	}
}
