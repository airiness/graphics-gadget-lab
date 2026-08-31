#pragma once
#include "GGLabRuntime/Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <cstdint>
#include <array>
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

	enum class IBLBakeStage : uint8_t
	{
		Idle,
		LoadingCache,
		Environment,
		EnvironmentMipChain,
		Irradiance,
		PrefilteredSpecular,
		BrdfLut,
		WaitingForGpu,
		SavingCache,
		Ready,
		Failed,
	};

	enum class IBLArtifactStage : uint8_t
	{
		Environment,
		Irradiance,
		PrefilteredSpecular,
		BrdfLut,

		Count,
	};

	enum class IBLArtifactResolution : uint8_t
	{
		Miss,
		CpuCache,
		LocalDdc,
		GpuBuild,
	};

	struct IBLStageArtifactStatus
	{
		DerivedDataKey m_DerivedDataKey{};
		ArtifactContentDigest m_ContentDigest{};
		IBLArtifactResolution m_Resolution = IBLArtifactResolution::Miss;
	};

	struct IBLBakeStatus
	{
		uint64_t m_RequestedGeneration = 0;
		uint64_t m_BakingGeneration = 0;
		uint64_t m_ActiveGeneration = 0;
		IBLBakeStage m_Stage = IBLBakeStage::Idle;
		float m_Progress = 0.0f;
		double m_GpuMilliseconds = 0.0;
		bool m_GpuTimingAvailable = false;
		std::array<IBLStageArtifactStatus, static_cast<size_t>(IBLArtifactStage::Count)>
			m_Artifacts{};
		bool m_CacheHit = false;
		bool m_PartialCacheHit = false;
		bool m_CpuCacheHit = false;
		bool m_DerivedDataCacheHit = false;
		uint32_t m_CacheHitStageCount = 0;
		uint32_t m_GpuBuildStageCount = 0;
		bool m_CacheWritePending = false;
		bool m_HasActiveBake = false;
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

	[[nodiscard]] constexpr std::string_view GetIBLBakeStageName(IBLBakeStage stage) noexcept
	{
		switch (stage)
		{
		case IBLBakeStage::Idle:
			return "Idle";
		case IBLBakeStage::LoadingCache:
			return "Loading cache";
		case IBLBakeStage::Environment:
			return "Environment";
		case IBLBakeStage::EnvironmentMipChain:
			return "Environment mip chain";
		case IBLBakeStage::Irradiance:
			return "Irradiance";
		case IBLBakeStage::PrefilteredSpecular:
			return "Prefiltered specular";
		case IBLBakeStage::BrdfLut:
			return "BRDF LUT";
		case IBLBakeStage::WaitingForGpu:
			return "Waiting for GPU";
		case IBLBakeStage::SavingCache:
			return "Saving cache";
		case IBLBakeStage::Ready:
			return "Ready";
		case IBLBakeStage::Failed:
			return "Failed";
		}
		return "Unknown";
	}

	[[nodiscard]] constexpr std::string_view GetIBLArtifactStageName(
		IBLArtifactStage stage) noexcept
	{
		switch (stage)
		{
		case IBLArtifactStage::Environment:
			return "Environment";
		case IBLArtifactStage::Irradiance:
			return "Irradiance";
		case IBLArtifactStage::PrefilteredSpecular:
			return "Prefiltered specular";
		case IBLArtifactStage::BrdfLut:
			return "BRDF LUT";
		case IBLArtifactStage::Count:
			break;
		}
		return "Unknown";
	}

	[[nodiscard]] constexpr std::string_view GetIBLArtifactResolutionName(
		IBLArtifactResolution resolution) noexcept
	{
		switch (resolution)
		{
		case IBLArtifactResolution::Miss:
			return "Miss";
		case IBLArtifactResolution::CpuCache:
			return "CPU cache";
		case IBLArtifactResolution::LocalDdc:
			return "Local DDC";
		case IBLArtifactResolution::GpuBuild:
			return "GPU build";
		}
		return "Unknown";
	}
}
