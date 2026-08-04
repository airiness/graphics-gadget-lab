#pragma once
#include "Graphics/RHI/RHIDescriptorCapacityContract.h"

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

	inline constexpr VulkanDeviceProfile GGLabVulkanV1DeviceProfile{
		.m_Name = "GGLab Vulkan v1 Device Profile",
		.m_MinimumApiVersion = { 1, 3 },
		.m_DescriptorCapacity = GGLabDescriptorCapacityContract,
	};

	enum class VulkanDeviceProfileRejectionReason : uint8_t
	{
		None,
		UnsupportedPlatform,
		VulkanLoaderUnavailable,
		ApiVersionTooLow,
		Win32SurfaceExtensionUnavailable,
		SwapchainExtensionUnavailable,
		GraphicsPresentQueueUnavailable,
		DynamicRenderingUnavailable,
		Synchronization2Unavailable,
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
		ResourceDescriptorCapacityInsufficient,
		SamplerDescriptorCapacityInsufficient,
		GlobalDescriptorSetLayoutUnsupported,
		RequiredFormatFeaturesUnavailable,
		Count,
	};

	struct VulkanDeviceProfileCapabilities
	{
		bool m_IsWindowsX64 = false;
		bool m_HasVulkanLoader = false;
		VulkanApiVersion m_ApiVersion{};
		bool m_HasWin32SurfaceExtension = false;
		bool m_HasSwapchainExtension = false;
		bool m_HasGraphicsPresentQueue = false;

		bool m_DynamicRendering = false;
		bool m_Synchronization2 = false;
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

		uint32_t m_ResourceDescriptorCapacity = 0;
		uint32_t m_SamplerDescriptorCapacity = 0;
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

	[[nodiscard]] constexpr VulkanDeviceProfileEvaluation EvaluateVulkanV1DeviceProfile(
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

		require(capabilities.m_IsWindowsX64,
			VulkanDeviceProfileRejectionReason::UnsupportedPlatform);
		require(capabilities.m_HasVulkanLoader,
			VulkanDeviceProfileRejectionReason::VulkanLoaderUnavailable);
		require(IsVulkanApiVersionAtLeast(
			capabilities.m_ApiVersion, GGLabVulkanV1DeviceProfile.m_MinimumApiVersion),
			VulkanDeviceProfileRejectionReason::ApiVersionTooLow);
		require(capabilities.m_HasWin32SurfaceExtension,
			VulkanDeviceProfileRejectionReason::Win32SurfaceExtensionUnavailable);
		require(capabilities.m_HasSwapchainExtension,
			VulkanDeviceProfileRejectionReason::SwapchainExtensionUnavailable);
		require(capabilities.m_HasGraphicsPresentQueue,
			VulkanDeviceProfileRejectionReason::GraphicsPresentQueueUnavailable);
		require(capabilities.m_DynamicRendering,
			VulkanDeviceProfileRejectionReason::DynamicRenderingUnavailable);
		require(capabilities.m_Synchronization2,
			VulkanDeviceProfileRejectionReason::Synchronization2Unavailable);
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
		require(capabilities.m_ResourceDescriptorCapacity >=
			GGLabVulkanV1DeviceProfile.m_DescriptorCapacity.m_ResourceDescriptorCount,
			VulkanDeviceProfileRejectionReason::ResourceDescriptorCapacityInsufficient);
		require(capabilities.m_SamplerDescriptorCapacity >=
			GGLabVulkanV1DeviceProfile.m_DescriptorCapacity.m_SamplerDescriptorCount,
			VulkanDeviceProfileRejectionReason::SamplerDescriptorCapacityInsufficient);
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
		case VulkanDeviceProfileRejectionReason::UnsupportedPlatform:
			return "Vulkan v1 is limited to Windows x64";
		case VulkanDeviceProfileRejectionReason::VulkanLoaderUnavailable:
			return "Vulkan loader is unavailable";
		case VulkanDeviceProfileRejectionReason::ApiVersionTooLow:
			return "Vulkan API version 1.3 is unavailable";
		case VulkanDeviceProfileRejectionReason::Win32SurfaceExtensionUnavailable:
			return "VK_KHR_win32_surface is unavailable";
		case VulkanDeviceProfileRejectionReason::SwapchainExtensionUnavailable:
			return "VK_KHR_swapchain is unavailable";
		case VulkanDeviceProfileRejectionReason::GraphicsPresentQueueUnavailable:
			return "no queue family supports both graphics and present";
		case VulkanDeviceProfileRejectionReason::DynamicRenderingUnavailable:
			return "dynamicRendering is unavailable";
		case VulkanDeviceProfileRejectionReason::Synchronization2Unavailable:
			return "synchronization2 is unavailable";
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
		case VulkanDeviceProfileRejectionReason::ResourceDescriptorCapacityInsufficient:
			return "resource descriptor capacity is below 65,536";
		case VulkanDeviceProfileRejectionReason::SamplerDescriptorCapacityInsufficient:
			return "sampler descriptor capacity is below 2,048";
		case VulkanDeviceProfileRejectionReason::GlobalDescriptorSetLayoutUnsupported:
			return "the Vulkan v1 global descriptor-set layout is unsupported";
		case VulkanDeviceProfileRejectionReason::RequiredFormatFeaturesUnavailable:
			return "required rendering format features are unavailable";
		case VulkanDeviceProfileRejectionReason::Count:
			break;
		}
		return "unknown device-profile rejection reason";
	}
}
