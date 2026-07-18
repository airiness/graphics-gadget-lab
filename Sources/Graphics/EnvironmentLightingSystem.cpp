#include "Core/Precompiled.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/TextureRegistry.h"
#include "Core/Utility/PathUtils.h"

#include <numbers>

namespace gglab
{
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
		const bool directoryAvailable = std::filesystem::is_directory(rootDirectory, errorCode);
		if (!directoryAvailable)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"EnvironmentLightingSystem: environment directory '{}' is unavailable; using procedural fallback.",
				rootDirectory.string());
		}
		else
		{
			for (std::filesystem::directory_iterator iterator(rootDirectory, errorCode), end;
				iterator != end && !errorCode;
				iterator.increment(errorCode))
			{
				const auto& entry = *iterator;
				if (!entry.is_regular_file(errorCode) || errorCode ||
					!utils::ExtensionEqualsIgnoreCase(entry.path(), ".hdr"))
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
		}

		std::ranges::sort(m_Entries,
			[](const EnvironmentMapEntry& lhs, const EnvironmentMapEntry& rhs) noexcept
			{
				return lhs.m_Path.generic_string() < rhs.m_Path.generic_string();
			});

		if (directoryAvailable && m_Entries.empty())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"EnvironmentLightingSystem: no valid HDR environment was found in '{}'; using procedural fallback.",
				rootDirectory.string());
		}

		if (!SelectDefaultEnvironment())
		{
			RequestRebake();
		}
	}

	bool EnvironmentLightingSystem::SelectDefaultEnvironment() noexcept
	{
		if (GetActiveEnvironment())
		{
			return true;
		}
		for (size_t entryIndex = 0; entryIndex < m_Entries.size(); ++entryIndex)
		{
			if (SelectEnvironment(entryIndex))
			{
				return true;
			}
		}
		return false;
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

		m_ActiveEntryIndex = entryIndex;
		RequestRebake();

		GGLAB_LOG_GRAPHICS_INFO(
			"EnvironmentLightingSystem: selected HDR environment '{}'.",
			m_Entries[entryIndex].m_Path.string());
		return true;
	}

	const EnvironmentMapEntry* EnvironmentLightingSystem::GetActiveEnvironment() const noexcept
	{
		return m_ActiveEntryIndex < m_Entries.size() ? &m_Entries[m_ActiveEntryIndex] : nullptr;
	}

	EnvironmentTextureSource EnvironmentLightingSystem::GetBakeSource() const noexcept
	{
		const auto* activeEnvironment = GetActiveEnvironment();
		if (activeEnvironment)
		{
			return
			{
				.m_Content = activeEnvironment->m_Content,
				.m_Type = EnvironmentTextureSourceType::Equirectangular,
			};
		}
		const TextureID fallbackId =
			ToTextureId(ReservedTextureIDIndex::FallbackEnvironmentCubemap);
		const Texture* fallback = m_TextureRegistry->GetTexture(fallbackId);

		return
		{
			.m_Content = {
				.m_Id = fallbackId,
				.m_Generation = fallback ? fallback->m_ContentGeneration : 0,
			},
			.m_Type = EnvironmentTextureSourceType::Cubemap,
		};
	}

	bool EnvironmentLightingSystem::EnsureActiveEnvironmentTextureLoaded() noexcept
	{
		if (m_ActiveEntryIndex >= m_Entries.size())
		{
			const TextureContentRef content = GetBakeSource().m_Content;
			const Texture* fallback = m_TextureRegistry->GetTexture(content.m_Id);
			return fallback && fallback->m_ContentGeneration == content.m_Generation &&
				fallback->m_State == AssetState::Ready;
		}

		auto& entry = m_Entries[m_ActiveEntryIndex];
		const Texture* texture = m_TextureRegistry->GetTexture(entry.m_Content.m_Id);
		const bool currentContent = texture &&
			texture->m_ContentGeneration == entry.m_Content.m_Generation;
		const bool terminal = currentContent &&
			(texture->m_State == AssetState::Failed || texture->m_State == AssetState::Cancelled);
		if ((!entry.m_Content.IsValid() || !currentContent || terminal) &&
			entry.m_LastLoadAttemptGeneration != m_BakeRequestGeneration)
		{
			entry.m_LastLoadAttemptGeneration = m_BakeRequestGeneration;
			const TextureRegistry::TextureLoadRequest request = m_TextureRegistry->LoadTextureAsync(
				entry.m_Path,
				TextureSemantic::Environment,
				TaskPriority::High);
			entry.m_Content = {
				.m_Id = request.m_TextureId,
				.m_Generation = request.m_Generation,
			};
			texture = m_TextureRegistry->GetTexture(entry.m_Content.m_Id);
		}
		if (!entry.m_Content.IsValid())
		{
			return false;
		}
		if (!texture || texture->m_ContentGeneration != entry.m_Content.m_Generation ||
			texture->m_State != AssetState::Ready)
		{
			return false;
		}

		const auto* textureDesc = m_TextureRegistry->GetTextureDesc(entry.m_Content.m_Id);
		if (!textureDesc || textureDesc->m_Dimension != RHITextureDimension::Texture2D ||
			textureDesc->m_ArraySize != 1 || textureDesc->m_Extent.m_Depth != 1 ||
			static_cast<uint64_t>(textureDesc->m_Extent.m_Height) * 2u != textureDesc->m_Extent.m_Width)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"EnvironmentLightingSystem: HDR environment '{}' must be a 2:1 2D texture (actual: {}x{}, array={}, depth={}).",
				entry.m_Path.string(), textureDesc ? textureDesc->m_Extent.m_Width : 0u,
				textureDesc ? textureDesc->m_Extent.m_Height : 0u,
				textureDesc ? textureDesc->m_ArraySize : 0u,
				textureDesc ? textureDesc->m_Extent.m_Depth : 0u);
			const TextureID invalidTextureId = entry.m_Content.m_Id;
			entry.m_Content = {};
			GGLAB_UNUSED(m_TextureRegistry->RemoveTexture(invalidTextureId));
			return false;
		}
		return true;
	}

	AssetState EnvironmentLightingSystem::GetActiveEnvironmentTextureState() const noexcept
	{
		if (m_ActiveEntryIndex >= m_Entries.size())
		{
			const TextureContentRef content = GetBakeSource().m_Content;
			const Texture* fallback = m_TextureRegistry->GetTexture(content.m_Id);
			return fallback && fallback->m_ContentGeneration == content.m_Generation ?
				fallback->m_State : AssetState::Failed;
		}

		const TextureContentRef content = m_Entries[m_ActiveEntryIndex].m_Content;
		const Texture* texture = m_TextureRegistry->GetTexture(content.m_Id);
		return texture && texture->m_ContentGeneration == content.m_Generation ?
			texture->m_State : AssetState::Failed;
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
		if (m_Settings.m_BakeConfig.m_PrefilteredSpecularSampleCount == clampedSampleCount)
		{
			return;
		}

		m_Settings.m_BakeConfig.m_PrefilteredSpecularSampleCount = clampedSampleCount;
		m_Settings.m_QualityPreset = IBLQualityPreset::Custom;
		RequestRebake();
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
		if (m_Settings.m_BakeConfig.m_PrefilteredSpecularMaxSampleLuminance == clampedLuminance)
		{
			return;
		}

		m_Settings.m_BakeConfig.m_PrefilteredSpecularMaxSampleLuminance = clampedLuminance;
		m_Settings.m_QualityPreset = IBLQualityPreset::Custom;
		RequestRebake();
	}

	void EnvironmentLightingSystem::SetQualityPreset(IBLQualityPreset preset) noexcept
	{
		if (preset >= IBLQualityPreset::Custom || m_Settings.m_QualityPreset == preset)
		{
			return;
		}

		m_Settings.m_QualityPreset = preset;
		m_Settings.m_BakeConfig = GetIBLBakeConfig(preset);
		RequestRebake();
	}

	void EnvironmentLightingSystem::RequestRebake(bool ignoreCache) noexcept
	{
		++m_BakeRequestGeneration;
		if (ignoreCache)
		{
			m_IgnoreCacheGeneration = m_BakeRequestGeneration;
		}
	}
}
