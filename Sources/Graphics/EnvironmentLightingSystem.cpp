#include "Core/Precompiled.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/TextureRegistry.h"

#include <cctype>
#include <numbers>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool IsHdrFile(const std::filesystem::path& path) noexcept
		{
			std::string extension = path.extension().string();
			std::ranges::transform(extension, extension.begin(),
				[](unsigned char value) noexcept
				{
					return static_cast<char>(std::tolower(value));
				});
			return extension == ".hdr";
		}
	}

	EnvironmentLightingSystem::EnvironmentLightingSystem(const CreateInfo& createInfo) noexcept :
		m_TextureRegistry(createInfo.m_TextureRegistry),
		m_RenderResourceRegistry(createInfo.m_RenderResourceRegistry)
	{
		GGLAB_ASSERT_NOT_NULL(m_TextureRegistry);
		GGLAB_ASSERT_NOT_NULL(m_RenderResourceRegistry);
	}

	void EnvironmentLightingSystem::Initialize(const std::filesystem::path& rootDirectory) noexcept
	{
		m_Entries.clear();
		m_ActiveEntryIndex = InvalidEntryIndex;

		std::error_code errorCode;
		if (!std::filesystem::is_directory(rootDirectory, errorCode))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"EnvironmentLightingSystem: environment directory '{}' is unavailable; using procedural fallback.",
				rootDirectory.string());
			return;
		}

		for (std::filesystem::directory_iterator iterator(rootDirectory, errorCode), end;
			iterator != end && !errorCode;
			iterator.increment(errorCode))
		{
			const auto& entry = *iterator;
			if (!entry.is_regular_file(errorCode) || errorCode || !IsHdrFile(entry.path()))
			{
				continue;
			}

			m_Entries.push_back(
				{
					.m_Path = entry.path(),
					.m_DisplayName = entry.path().stem().string(),
				});
		}

		if (errorCode)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"EnvironmentLightingSystem: failed while scanning '{}': {}.",
				rootDirectory.string(),
				errorCode.message());
		}

		std::ranges::sort(m_Entries,
			[](const EnvironmentMapEntry& lhs, const EnvironmentMapEntry& rhs) noexcept
			{
				return lhs.m_Path.generic_string() < rhs.m_Path.generic_string();
			});

		for (size_t entryIndex = 0; entryIndex < m_Entries.size(); ++entryIndex)
		{
			if (SelectEnvironment(entryIndex))
			{
				return;
			}
		}

		GGLAB_LOG_GRAPHICS_WARN(
			"EnvironmentLightingSystem: no valid HDR environment was found in '{}'; using procedural fallback.",
			rootDirectory.string());
	}

	bool EnvironmentLightingSystem::SelectEnvironment(size_t entryIndex) noexcept
	{
		if (entryIndex >= m_Entries.size())
		{
			return false;
		}
		if (m_ActiveEntryIndex == entryIndex)
		{
			return true;
		}

		auto& entry = m_Entries[entryIndex];
		if (!entry.m_TextureId.IsValid())
		{
			entry.m_LoadAttempted = true;
			entry.m_TextureId = m_TextureRegistry->LoadTexture(
				entry.m_Path,
				TextureSemantic::Environment);
		}
		if (!entry.m_TextureId.IsValid())
		{
			return false;
		}

		const auto* textureDesc = m_TextureRegistry->GetTextureDesc(entry.m_TextureId);
		if (!textureDesc ||
			textureDesc->m_Dimension != RHITextureDimension::Texture2D ||
			textureDesc->m_ArraySize != 1 ||
			textureDesc->m_Extent.m_Depth != 1 ||
			static_cast<uint64_t>(textureDesc->m_Extent.m_Height) * 2u != textureDesc->m_Extent.m_Width)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"EnvironmentLightingSystem: HDR environment '{}' must be a 2:1 2D texture (actual: {}x{}, array={}, depth={}).",
				entry.m_Path.string(),
				textureDesc ? textureDesc->m_Extent.m_Width : 0u,
				textureDesc ? textureDesc->m_Extent.m_Height : 0u,
				textureDesc ? textureDesc->m_ArraySize : 0u,
				textureDesc ? textureDesc->m_Extent.m_Depth : 0u);
			return false;
		}

		m_ActiveEntryIndex = entryIndex;
		m_RenderResourceRegistry->MarkDirty(
			RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);

		GGLAB_LOG_GRAPHICS_INFO(
			"EnvironmentLightingSystem: selected HDR environment '{}' ({}x{}).",
			entry.m_Path.string(),
			textureDesc->m_Extent.m_Width,
			textureDesc->m_Extent.m_Height);
		return true;
	}

	const EnvironmentMapEntry* EnvironmentLightingSystem::GetActiveEnvironment() const noexcept
	{
		return m_ActiveEntryIndex < m_Entries.size() ? &m_Entries[m_ActiveEntryIndex] : nullptr;
	}

	TextureID EnvironmentLightingSystem::GetActiveTextureId() const noexcept
	{
		const auto* activeEnvironment = GetActiveEnvironment();
		return activeEnvironment ? activeEnvironment->m_TextureId : InvalidTextureID;
	}

	void EnvironmentLightingSystem::SetIntensity(float intensity) noexcept
	{
		if (std::isfinite(intensity))
		{
			const float clampedIntensity = std::max(intensity, 0.0f);
			if (m_Settings.m_Intensity != clampedIntensity)
			{
				m_Settings.m_Intensity = clampedIntensity;
				m_RenderResourceRegistry->MarkAllIBLPreviewsDirty();
			}
		}
	}

	void EnvironmentLightingSystem::SetRotationRadians(float rotationRadians) noexcept
	{
		if (std::isfinite(rotationRadians))
		{
			constexpr float FullRotation = 2.0f * std::numbers::pi_v<float>;
			const float wrappedRotation = std::remainder(rotationRadians, FullRotation);
			if (m_Settings.m_RotationRadians != wrappedRotation)
			{
				m_Settings.m_RotationRadians = wrappedRotation;
				m_RenderResourceRegistry->MarkAllIBLPreviewsDirty();
			}
		}
	}

	void EnvironmentLightingSystem::SetPrefilteredSpecularSampleCount(uint32_t sampleCount) noexcept
	{
		constexpr uint32_t MinSampleCount = 1;
		constexpr uint32_t MaxSampleCount = 4096;
		const uint32_t clampedSampleCount = std::clamp(sampleCount, MinSampleCount, MaxSampleCount);
		if (m_Settings.m_PrefilteredSpecularSampleCount == clampedSampleCount)
		{
			return;
		}

		m_Settings.m_PrefilteredSpecularSampleCount = clampedSampleCount;
		m_RenderResourceRegistry->MarkDirty(
			RenderResourceRegistry::TextureIndex::IBL_PrefilteredSpecularCubemap);
	}

	void EnvironmentLightingSystem::SetPrefilteredSpecularMaxSampleLuminance(float maxSampleLuminance) noexcept
	{
		if (!std::isfinite(maxSampleLuminance))
		{
			return;
		}

		constexpr float MinLuminance = 1.0f;
		constexpr float MaxLuminance = 65000.0f;
		const float clampedLuminance = std::clamp(maxSampleLuminance, MinLuminance, MaxLuminance);
		if (m_Settings.m_PrefilteredSpecularMaxSampleLuminance == clampedLuminance)
		{
			return;
		}

		m_Settings.m_PrefilteredSpecularMaxSampleLuminance = clampedLuminance;
		m_RenderResourceRegistry->MarkDirty(
			RenderResourceRegistry::TextureIndex::IBL_PrefilteredSpecularCubemap);
	}
}
