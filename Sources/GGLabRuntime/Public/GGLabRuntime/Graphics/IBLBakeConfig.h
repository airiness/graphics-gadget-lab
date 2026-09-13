#pragma once

#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <cstdint>
#include <string_view>

namespace gglab
{
	enum class IBLQualityPreset : uint8_t
	{
		Low,
		Medium,
		High,
		Offline,
		Custom,

		Count
	};

	struct IBLBakeConfig
	{
		uint32_t m_EnvironmentCubemapSize = 512;
		RHIFormat m_EnvironmentCubemapFormat = RHIFormat::R16G16B16A16Float;

		uint32_t m_IrradianceCubemapSize = 32;
		RHIFormat m_IrradianceCubemapFormat = RHIFormat::R16G16B16A16Float;
		uint32_t m_IrradianceSampleCount = 256;

		uint32_t m_PrefilteredSpecularCubemapSize = 128;
		uint32_t m_PrefilteredSpecularMipLevels = 5;
		RHIFormat m_PrefilteredSpecularCubemapFormat = RHIFormat::R16G16B16A16Float;
		uint32_t m_PrefilteredSpecularSampleCount = 512;
		float m_PrefilteredSpecularMaxSampleLuminance = 1000.0f;

		uint32_t m_BrdfLutSize = 256;
		RHIFormat m_BrdfLutFormat = RHIFormat::R16G16Float;

		constexpr bool operator==(const IBLBakeConfig&) const noexcept = default;
	};

	[[nodiscard]] constexpr IBLBakeConfig GetIBLBakeConfig(IBLQualityPreset preset) noexcept
	{
		switch (preset)
		{
		case IBLQualityPreset::Low:
			return {
				.m_EnvironmentCubemapSize = 256,
				.m_IrradianceCubemapSize = 16,
				.m_IrradianceSampleCount = 64,
				.m_PrefilteredSpecularCubemapSize = 64,
				.m_PrefilteredSpecularMipLevels = 4,
				.m_PrefilteredSpecularSampleCount = 128,
				.m_BrdfLutSize = 128,
			};
		case IBLQualityPreset::Medium:
			return {};
		case IBLQualityPreset::High:
			return {
				.m_EnvironmentCubemapSize = 1024,
				.m_IrradianceCubemapSize = 64,
				.m_IrradianceSampleCount = 1024,
				.m_PrefilteredSpecularCubemapSize = 256,
				.m_PrefilteredSpecularMipLevels = 7,
				.m_PrefilteredSpecularSampleCount = 1024,
				.m_BrdfLutSize = 256,
			};
		case IBLQualityPreset::Offline:
			return {
				.m_EnvironmentCubemapSize = 2048,
				.m_EnvironmentCubemapFormat = RHIFormat::R32G32B32A32Float,
				.m_IrradianceCubemapSize = 128,
				.m_IrradianceCubemapFormat = RHIFormat::R32G32B32A32Float,
				.m_IrradianceSampleCount = 4096,
				.m_PrefilteredSpecularCubemapSize = 512,
				.m_PrefilteredSpecularMipLevels = 9,
				.m_PrefilteredSpecularCubemapFormat = RHIFormat::R32G32B32A32Float,
				.m_PrefilteredSpecularSampleCount = 4096,
				.m_PrefilteredSpecularMaxSampleLuminance = 65000.0f,
				.m_BrdfLutSize = 512,
				.m_BrdfLutFormat = RHIFormat::R32G32Float,
			};
		case IBLQualityPreset::Custom:
		case IBLQualityPreset::Count:
			return {};
		}
		return {};
	}

	[[nodiscard]] constexpr std::string_view GetIBLQualityPresetName(
		IBLQualityPreset preset) noexcept
	{
		switch (preset)
		{
		case IBLQualityPreset::Low:
			return "Low";
		case IBLQualityPreset::Medium:
			return "Medium";
		case IBLQualityPreset::High:
			return "High";
		case IBLQualityPreset::Offline:
			return "Offline";
		case IBLQualityPreset::Custom:
			return "Custom";
		case IBLQualityPreset::Count:
			break;
		}
		return "Unknown";
	}
}
