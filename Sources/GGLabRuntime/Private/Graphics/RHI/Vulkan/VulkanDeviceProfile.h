#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIDescriptorCapacityContract.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gglab
{
	struct VulkanApiVersion
	{
		uint32_t m_Major = 0;
		uint32_t m_Minor = 0;
	};

	[[nodiscard]] constexpr bool IsVulkanApiVersionAtLeast(
		VulkanApiVersion available, VulkanApiVersion required) noexcept
	{
		return available.m_Major > required.m_Major ||
			(available.m_Major == required.m_Major && available.m_Minor >= required.m_Minor);
	}

	struct VulkanDeviceProfile
	{
		std::string_view m_Name;
		VulkanApiVersion m_MinimumApiVersion;
		RHIDescriptorCapacityContract m_DescriptorCapacity;
	};

	inline constexpr VulkanDeviceProfile GGLabVulkanDeviceProfile{
		.m_Name = "GGLab Vulkan Device Profile",
		.m_MinimumApiVersion = { 1, 3 },
		.m_DescriptorCapacity = GGLabDescriptorCapacityContract,
	};

	enum class VulkanDeviceProfileRejectionReason : uint8_t
	{
		None,
		ApiVersionTooLow,
		SwapchainExtensionUnavailable,
		GraphicsPresentQueueUnavailable,
		DynamicRenderingUnavailable,
		Synchronization2Unavailable,
		ShaderDemoteToHelperInvocationUnavailable,
		TimelineSemaphoreUnavailable,
		ScalarBlockLayoutUnavailable,
		SamplerAnisotropyUnavailable,
		ShaderStorageImageExtendedFormatsUnavailable,
		RuntimeDescriptorArrayUnavailable,
		DescriptorBindingPartiallyBoundUnavailable,
		DescriptorBindingUpdateUnusedWhilePendingUnavailable,
		DescriptorBindingSampledImageUpdateAfterBindUnavailable,
		DescriptorBindingStorageImageUpdateAfterBindUnavailable,
		ShaderSampledImageArrayNonUniformIndexingUnavailable,
		ShaderStorageImageArrayNonUniformIndexingUnavailable,
		MutableDescriptorTypeExtensionUnavailable,
		MutableDescriptorTypeUnavailable,
		DescriptorSetSampledImageLimitInsufficient,
		PerStageSampledImageLimitInsufficient,
		DescriptorSetStorageImageLimitInsufficient,
		PerStageStorageImageLimitInsufficient,
		DescriptorSetSamplerLimitInsufficient,
		PerStageSamplerLimitInsufficient,
		PerStageUpdateAfterBindResourceLimitInsufficient,
		UpdateAfterBindPoolLimitInsufficient,
		GlobalDescriptorSetLayoutUnsupported,
		RequiredFormatFeaturesUnavailable,
		Count,
	};

	struct VulkanDescriptorCapacityLimits
	{
		uint32_t m_MaxDescriptorSetUpdateAfterBindSampledImages = 0;
		uint32_t m_MaxPerStageDescriptorUpdateAfterBindSampledImages = 0;
		uint32_t m_MaxDescriptorSetUpdateAfterBindStorageImages = 0;
		uint32_t m_MaxPerStageDescriptorUpdateAfterBindStorageImages = 0;
		uint32_t m_MaxDescriptorSetUpdateAfterBindSamplers = 0;
		uint32_t m_MaxPerStageDescriptorUpdateAfterBindSamplers = 0;
		uint32_t m_MaxPerStageUpdateAfterBindResources = 0;
		uint32_t m_MaxUpdateAfterBindDescriptorsInAllPools = 0;
	};

	struct VulkanDescriptorCapacityAvailability
	{
		uint32_t m_ResourceDescriptorCount = 0;
		uint32_t m_SamplerDescriptorCount = 0;
		uint32_t m_CombinedDescriptorCount = 0;
	};

	[[nodiscard]] constexpr VulkanDescriptorCapacityAvailability
		CalculateVulkanDescriptorCapacityAvailability(
			const VulkanDescriptorCapacityLimits& limits) noexcept
	{
		return {
			.m_ResourceDescriptorCount = std::min({
				limits.m_MaxDescriptorSetUpdateAfterBindSampledImages,
				limits.m_MaxPerStageDescriptorUpdateAfterBindSampledImages,
				limits.m_MaxDescriptorSetUpdateAfterBindStorageImages,
				limits.m_MaxPerStageDescriptorUpdateAfterBindStorageImages,
			}),
			.m_SamplerDescriptorCount = std::min(
				limits.m_MaxDescriptorSetUpdateAfterBindSamplers,
				limits.m_MaxPerStageDescriptorUpdateAfterBindSamplers),
			.m_CombinedDescriptorCount = std::min(
				limits.m_MaxPerStageUpdateAfterBindResources,
				limits.m_MaxUpdateAfterBindDescriptorsInAllPools),
		};
	}

	struct VulkanDeviceProfileCapabilities
	{
		VulkanApiVersion m_ApiVersion{};
		bool m_HasSwapchainExtension = false;
		bool m_HasGraphicsPresentQueue = false;

		bool m_DynamicRendering = false;
		bool m_Synchronization2 = false;
		bool m_ShaderDemoteToHelperInvocation = false;
		bool m_TimelineSemaphore = false;
		bool m_ScalarBlockLayout = false;
		bool m_SamplerAnisotropy = false;
		bool m_ShaderStorageImageExtendedFormats = false;
		bool m_RuntimeDescriptorArray = false;
		bool m_DescriptorBindingPartiallyBound = false;
		bool m_DescriptorBindingUpdateUnusedWhilePending = false;
		bool m_DescriptorBindingSampledImageUpdateAfterBind = false;
		bool m_DescriptorBindingStorageImageUpdateAfterBind = false;
		bool m_ShaderSampledImageArrayNonUniformIndexing = false;
		bool m_ShaderStorageImageArrayNonUniformIndexing = false;
		bool m_HasMutableDescriptorTypeExtension = false;
		bool m_MutableDescriptorType = false;

		VulkanDescriptorCapacityLimits m_DescriptorCapacityLimits{};
		bool m_GlobalDescriptorSetLayoutSupported = false;
		bool m_RequiredFormatFeaturesSupported = false;

		// Queried capabilities that are deliberately outside binding ABI revision 1.
		bool m_DescriptorBindingVariableDescriptorCount = false;
		bool m_DescriptorBindingUniformBufferUpdateAfterBind = false;
		bool m_DescriptorBindingStorageBufferUpdateAfterBind = false;
		bool m_ShaderUniformBufferArrayNonUniformIndexing = false;
		bool m_ShaderStorageBufferArrayNonUniformIndexing = false;
	};

	struct VulkanDeviceProfileEvaluation
	{
		static constexpr size_t MaxRejectionReasons =
			static_cast<size_t>(VulkanDeviceProfileRejectionReason::Count);

		std::array<VulkanDeviceProfileRejectionReason, MaxRejectionReasons> m_RejectionReasons{};
		size_t m_RejectionReasonCount = 0;

		[[nodiscard]] constexpr bool IsAccepted() const noexcept
		{
			return m_RejectionReasonCount == 0;
		}

		[[nodiscard]] constexpr bool HasReason(
			VulkanDeviceProfileRejectionReason reason) const noexcept
		{
			for (size_t i = 0; i < m_RejectionReasonCount; ++i)
			{
				if (m_RejectionReasons[i] == reason)
				{
					return true;
				}
			}
			return false;
		}

		constexpr void Reject(VulkanDeviceProfileRejectionReason reason) noexcept
		{
			m_RejectionReasons[m_RejectionReasonCount++] = reason;
		}
	};

	[[nodiscard]] constexpr VulkanDeviceProfileEvaluation EvaluateVulkanDeviceProfile(
		const VulkanDeviceProfileCapabilities& capabilities) noexcept
	{
		VulkanDeviceProfileEvaluation evaluation{};
		const auto require = [&evaluation](
			bool supported, VulkanDeviceProfileRejectionReason reason) constexpr noexcept
			{
				if (!supported)
				{
					evaluation.Reject(reason);
				}
			};

		require(IsVulkanApiVersionAtLeast(
			capabilities.m_ApiVersion, GGLabVulkanDeviceProfile.m_MinimumApiVersion),
			VulkanDeviceProfileRejectionReason::ApiVersionTooLow);
		require(capabilities.m_HasSwapchainExtension,
			VulkanDeviceProfileRejectionReason::SwapchainExtensionUnavailable);
		require(capabilities.m_HasGraphicsPresentQueue,
			VulkanDeviceProfileRejectionReason::GraphicsPresentQueueUnavailable);
		require(capabilities.m_DynamicRendering,
			VulkanDeviceProfileRejectionReason::DynamicRenderingUnavailable);
		require(capabilities.m_Synchronization2,
			VulkanDeviceProfileRejectionReason::Synchronization2Unavailable);
		require(capabilities.m_ShaderDemoteToHelperInvocation,
			VulkanDeviceProfileRejectionReason::ShaderDemoteToHelperInvocationUnavailable);
		require(capabilities.m_TimelineSemaphore,
			VulkanDeviceProfileRejectionReason::TimelineSemaphoreUnavailable);
		require(capabilities.m_ScalarBlockLayout,
			VulkanDeviceProfileRejectionReason::ScalarBlockLayoutUnavailable);
		require(capabilities.m_SamplerAnisotropy,
			VulkanDeviceProfileRejectionReason::SamplerAnisotropyUnavailable);
		require(capabilities.m_ShaderStorageImageExtendedFormats,
			VulkanDeviceProfileRejectionReason::ShaderStorageImageExtendedFormatsUnavailable);
		require(capabilities.m_RuntimeDescriptorArray,
			VulkanDeviceProfileRejectionReason::RuntimeDescriptorArrayUnavailable);
		require(capabilities.m_DescriptorBindingPartiallyBound,
			VulkanDeviceProfileRejectionReason::DescriptorBindingPartiallyBoundUnavailable);
		require(capabilities.m_DescriptorBindingUpdateUnusedWhilePending,
			VulkanDeviceProfileRejectionReason::DescriptorBindingUpdateUnusedWhilePendingUnavailable);
		require(capabilities.m_DescriptorBindingSampledImageUpdateAfterBind,
			VulkanDeviceProfileRejectionReason::DescriptorBindingSampledImageUpdateAfterBindUnavailable);
		require(capabilities.m_DescriptorBindingStorageImageUpdateAfterBind,
			VulkanDeviceProfileRejectionReason::DescriptorBindingStorageImageUpdateAfterBindUnavailable);
		require(capabilities.m_ShaderSampledImageArrayNonUniformIndexing,
			VulkanDeviceProfileRejectionReason::ShaderSampledImageArrayNonUniformIndexingUnavailable);
		require(capabilities.m_ShaderStorageImageArrayNonUniformIndexing,
			VulkanDeviceProfileRejectionReason::ShaderStorageImageArrayNonUniformIndexingUnavailable);
		require(capabilities.m_HasMutableDescriptorTypeExtension,
			VulkanDeviceProfileRejectionReason::MutableDescriptorTypeExtensionUnavailable);
		require(capabilities.m_MutableDescriptorType,
			VulkanDeviceProfileRejectionReason::MutableDescriptorTypeUnavailable);
		const auto& limits = capabilities.m_DescriptorCapacityLimits;
		const uint32_t requiredResources =
			GGLabVulkanDeviceProfile.m_DescriptorCapacity.m_ResourceDescriptorCount;
		const uint32_t requiredSamplers =
			GGLabVulkanDeviceProfile.m_DescriptorCapacity.m_SamplerDescriptorCount;
		require(limits.m_MaxDescriptorSetUpdateAfterBindSampledImages >= requiredResources,
			VulkanDeviceProfileRejectionReason::DescriptorSetSampledImageLimitInsufficient);
		require(limits.m_MaxPerStageDescriptorUpdateAfterBindSampledImages >= requiredResources,
			VulkanDeviceProfileRejectionReason::PerStageSampledImageLimitInsufficient);
		require(limits.m_MaxDescriptorSetUpdateAfterBindStorageImages >= requiredResources,
			VulkanDeviceProfileRejectionReason::DescriptorSetStorageImageLimitInsufficient);
		require(limits.m_MaxPerStageDescriptorUpdateAfterBindStorageImages >= requiredResources,
			VulkanDeviceProfileRejectionReason::PerStageStorageImageLimitInsufficient);
		require(limits.m_MaxDescriptorSetUpdateAfterBindSamplers >= requiredSamplers,
			VulkanDeviceProfileRejectionReason::DescriptorSetSamplerLimitInsufficient);
		require(limits.m_MaxPerStageDescriptorUpdateAfterBindSamplers >= requiredSamplers,
			VulkanDeviceProfileRejectionReason::PerStageSamplerLimitInsufficient);
		const uint64_t requiredCombined =
			static_cast<uint64_t>(requiredResources) + requiredSamplers;
		require(limits.m_MaxPerStageUpdateAfterBindResources >= requiredCombined,
			VulkanDeviceProfileRejectionReason::PerStageUpdateAfterBindResourceLimitInsufficient);
		require(limits.m_MaxUpdateAfterBindDescriptorsInAllPools >= requiredCombined,
			VulkanDeviceProfileRejectionReason::UpdateAfterBindPoolLimitInsufficient);
		require(capabilities.m_GlobalDescriptorSetLayoutSupported,
			VulkanDeviceProfileRejectionReason::GlobalDescriptorSetLayoutUnsupported);
		require(capabilities.m_RequiredFormatFeaturesSupported,
			VulkanDeviceProfileRejectionReason::RequiredFormatFeaturesUnavailable);

		return evaluation;
	}

	[[nodiscard]] constexpr std::string_view VulkanDeviceProfileRejectionReasonText(
		VulkanDeviceProfileRejectionReason reason) noexcept
	{
		switch (reason)
		{
		case VulkanDeviceProfileRejectionReason::None:
			return "none";
		case VulkanDeviceProfileRejectionReason::ApiVersionTooLow:
			return "Vulkan API version 1.3 is unavailable";
		case VulkanDeviceProfileRejectionReason::SwapchainExtensionUnavailable:
			return "VK_KHR_swapchain is unavailable";
		case VulkanDeviceProfileRejectionReason::GraphicsPresentQueueUnavailable:
			return "no queue family supports both graphics and present";
		case VulkanDeviceProfileRejectionReason::DynamicRenderingUnavailable:
			return "dynamicRendering is unavailable";
		case VulkanDeviceProfileRejectionReason::Synchronization2Unavailable:
			return "synchronization2 is unavailable";
		case VulkanDeviceProfileRejectionReason::ShaderDemoteToHelperInvocationUnavailable:
			return "shaderDemoteToHelperInvocation is unavailable";
		case VulkanDeviceProfileRejectionReason::TimelineSemaphoreUnavailable:
			return "timelineSemaphore is unavailable";
		case VulkanDeviceProfileRejectionReason::ScalarBlockLayoutUnavailable:
			return "scalarBlockLayout is unavailable";
		case VulkanDeviceProfileRejectionReason::SamplerAnisotropyUnavailable:
			return "samplerAnisotropy is unavailable";
		case VulkanDeviceProfileRejectionReason::ShaderStorageImageExtendedFormatsUnavailable:
			return "shaderStorageImageExtendedFormats is unavailable";
		case VulkanDeviceProfileRejectionReason::RuntimeDescriptorArrayUnavailable:
			return "runtimeDescriptorArray is unavailable";
		case VulkanDeviceProfileRejectionReason::DescriptorBindingPartiallyBoundUnavailable:
			return "descriptorBindingPartiallyBound is unavailable";
		case VulkanDeviceProfileRejectionReason::DescriptorBindingUpdateUnusedWhilePendingUnavailable:
			return "descriptorBindingUpdateUnusedWhilePending is unavailable";
		case VulkanDeviceProfileRejectionReason::DescriptorBindingSampledImageUpdateAfterBindUnavailable:
			return "descriptorBindingSampledImageUpdateAfterBind is unavailable";
		case VulkanDeviceProfileRejectionReason::DescriptorBindingStorageImageUpdateAfterBindUnavailable:
			return "descriptorBindingStorageImageUpdateAfterBind is unavailable";
		case VulkanDeviceProfileRejectionReason::ShaderSampledImageArrayNonUniformIndexingUnavailable:
			return "shaderSampledImageArrayNonUniformIndexing is unavailable";
		case VulkanDeviceProfileRejectionReason::ShaderStorageImageArrayNonUniformIndexingUnavailable:
			return "shaderStorageImageArrayNonUniformIndexing is unavailable";
		case VulkanDeviceProfileRejectionReason::MutableDescriptorTypeExtensionUnavailable:
			return "VK_EXT_mutable_descriptor_type is unavailable";
		case VulkanDeviceProfileRejectionReason::MutableDescriptorTypeUnavailable:
			return "mutableDescriptorType is unavailable";
		case VulkanDeviceProfileRejectionReason::DescriptorSetSampledImageLimitInsufficient:
			return "maxDescriptorSetUpdateAfterBindSampledImages is below 65,536";
		case VulkanDeviceProfileRejectionReason::PerStageSampledImageLimitInsufficient:
			return "maxPerStageDescriptorUpdateAfterBindSampledImages is below 65,536";
		case VulkanDeviceProfileRejectionReason::DescriptorSetStorageImageLimitInsufficient:
			return "maxDescriptorSetUpdateAfterBindStorageImages is below 65,536";
		case VulkanDeviceProfileRejectionReason::PerStageStorageImageLimitInsufficient:
			return "maxPerStageDescriptorUpdateAfterBindStorageImages is below 65,536";
		case VulkanDeviceProfileRejectionReason::DescriptorSetSamplerLimitInsufficient:
			return "maxDescriptorSetUpdateAfterBindSamplers is below 2,048";
		case VulkanDeviceProfileRejectionReason::PerStageSamplerLimitInsufficient:
			return "maxPerStageDescriptorUpdateAfterBindSamplers is below 2,048";
		case VulkanDeviceProfileRejectionReason::PerStageUpdateAfterBindResourceLimitInsufficient:
			return "maxPerStageUpdateAfterBindResources is below the required combined capacity";
		case VulkanDeviceProfileRejectionReason::UpdateAfterBindPoolLimitInsufficient:
			return "maxUpdateAfterBindDescriptorsInAllPools is below the required combined capacity";
		case VulkanDeviceProfileRejectionReason::GlobalDescriptorSetLayoutUnsupported:
			return "the required Vulkan global descriptor-set layout is unsupported";
		case VulkanDeviceProfileRejectionReason::RequiredFormatFeaturesUnavailable:
			return "required rendering format features are unavailable";
		case VulkanDeviceProfileRejectionReason::Count:
			break;
		}
		return "unknown device-profile rejection reason";
	}
}
