#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanBootstrap.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"
#include "Core/Log/Logger.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace gglab
{
	namespace
	{
		void LogBootstrapInfo(const std::string& message) noexcept
		{
			if (auto& logger = Logger::GetLogger(Logger::LoggerType::Application))
			{
				logger->info("{}", message);
			}
		}

		void LogBootstrapError(const std::string& message) noexcept
		{
			if (auto& logger = Logger::GetLogger(Logger::LoggerType::Application))
			{
				logger->error("{}", message);
			}
		}

		constexpr uint32_t DefaultDiscreteRank = 0;
		constexpr uint32_t DefaultIntegratedRank = 1;
		constexpr uint32_t DefaultOtherRank = 2;
		constexpr uint32_t DefaultCpuRank = 3;

		[[nodiscard]] uint32_t DefaultAdapterRank(VkPhysicalDeviceType type) noexcept
		{
			switch (type)
			{
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
				return DefaultDiscreteRank;
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
				return DefaultIntegratedRank;
			case VK_PHYSICAL_DEVICE_TYPE_CPU:
				return DefaultCpuRank;
			default:
				return DefaultOtherRank;
			}
		}

		[[nodiscard]] std::string LowerAscii(std::string_view text) noexcept
		{
			std::string lower(text);
			std::ranges::transform(lower, lower.begin(),
				[](char value) noexcept
				{
					return value >= 'A' && value <= 'Z'
						? static_cast<char>(value - 'A' + 'a')
						: value;
				});
			return lower;
		}

		[[nodiscard]] bool MatchesPrefix(
			const VulkanAdapterCapabilitySnapshot& snapshot, std::string_view prefix) noexcept
		{
			if (prefix.empty())
			{
				return false;
			}
			const std::string lowerPrefix = LowerAscii(prefix);
			const std::string lowerName = LowerAscii(snapshot.m_Identity.m_DeviceName);
			if (lowerName.starts_with(lowerPrefix))
			{
				return true;
			}
			const std::string uuidHex = snapshot.m_Identity.UuidHex();
			return uuidHex.starts_with(lowerPrefix);
		}

		[[nodiscard]] std::string UuidHex(const std::array<uint8_t, VK_UUID_SIZE>& uuid) noexcept
		{
			std::string text;
			text.reserve(VK_UUID_SIZE * 2);
			for (const uint8_t byte : uuid)
			{
				text += std::format("{:02x}", byte);
			}
			return text;
		}

		void LogAdapterSummary(const VulkanAdapterCapabilitySnapshot& snapshot) noexcept
		{
			const auto& identity = snapshot.m_Identity;
			LogBootstrapInfo(std::format("[{}] {} ({})", identity.m_EnumerationIndex,
				identity.m_DeviceName, ToString(identity.m_DeviceType)));
			LogBootstrapInfo(std::format("    vendor=0x{:04x} device=0x{:04x} {} driver={} ({})",
				identity.m_VendorId, identity.m_DeviceId,
				FormatAdapterVersions(identity.m_ApiVersion, identity.m_DriverVersion),
				identity.m_DriverName, identity.m_DriverInfo));
			LogBootstrapInfo(std::format("    uuid={} driverUuid={}", identity.UuidHex(),
				UuidHex(identity.m_DriverUuid)));
		}

		void LogAdapterProfile(const VulkanAdapterCapabilitySnapshot& snapshot) noexcept
		{
			const auto& capabilities = snapshot.m_ProfileCapabilities;
			const auto& availability = snapshot.m_DescriptorCapacityAvailability;
			LogBootstrapInfo(std::format("    graphicsPresentQueue={} (family {}, queues {})",
				snapshot.m_HasGraphicsPresentQueueFamily ? "yes" : "no",
				snapshot.m_GraphicsPresentQueueFamilyIndex, snapshot.m_GraphicsPresentQueueCount));
			LogBootstrapInfo(std::format(
				"    descriptor capacity available: resources={}, samplers={}, combined={}",
				availability.m_ResourceDescriptorCount, availability.m_SamplerDescriptorCount,
				availability.m_CombinedDescriptorCount));
			LogBootstrapInfo(std::format("    globalSet1LayoutSupported={} requiredFormatFeatures={}",
				capabilities.m_GlobalDescriptorSetLayoutSupported ? "yes" : "no",
				capabilities.m_RequiredFormatFeaturesSupported ? "yes" : "no"));
			for (const VulkanFormatSupportDiagnostic& format : snapshot.m_FormatDiagnostics)
			{
				LogBootstrapInfo(std::format("    format {} ({}) supported={}",
					format.m_FormatName, format.m_Usage, format.m_Supported ? "yes" : "no"));
			}
		}

		void LogAdapterEvaluation(const VulkanAdapterCapabilitySnapshot& snapshot) noexcept
		{
			if (snapshot.m_ProfileEvaluation.IsAccepted())
			{
				LogBootstrapInfo("    PROFILE: ACCEPTED");
				return;
			}
			LogBootstrapInfo("    PROFILE: REJECTED");
			for (size_t index = 0; index < snapshot.m_ProfileEvaluation.m_RejectionReasonCount;
				++index)
			{
				LogBootstrapInfo(std::format("      - {}",
					VulkanDeviceProfileRejectionReasonText(
						snapshot.m_ProfileEvaluation.m_RejectionReasons[index])));
			}
		}

		void LogSelectedAdapter(const VulkanAdapterCapabilitySnapshot& snapshot) noexcept
		{
			const auto& identity = snapshot.m_Identity;
			const auto& capabilities = snapshot.m_ProfileCapabilities;
			const auto yesNo = [](bool value) noexcept { return value ? "yes" : "no"; };

			LogBootstrapInfo(std::format("Selected adapter: {} ({}) vendor=0x{:04x} device=0x{:04x}",
				identity.m_DeviceName, ToString(identity.m_DeviceType), identity.m_VendorId,
				identity.m_DeviceId));
			LogBootstrapInfo(std::format("  queue family {} ({} queues)",
				snapshot.m_GraphicsPresentQueueFamilyIndex, snapshot.m_GraphicsPresentQueueCount));
			LogBootstrapInfo(std::format("  enabled device extensions: {}, {}",
				VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME));
			LogBootstrapInfo(std::format(
				"  features: dynamicRendering={}, synchronization2={}, timelineSemaphore={}, "
				"scalarBlockLayout={}, samplerAnisotropy={}, shaderStorageImageExtendedFormats={}, "
				"mutableDescriptorType={}",
				yesNo(capabilities.m_DynamicRendering), yesNo(capabilities.m_Synchronization2),
				yesNo(capabilities.m_TimelineSemaphore), yesNo(capabilities.m_ScalarBlockLayout),
				yesNo(capabilities.m_SamplerAnisotropy),
				yesNo(capabilities.m_ShaderStorageImageExtendedFormats),
				yesNo(capabilities.m_MutableDescriptorType)));
			LogBootstrapInfo(std::format(
				"  descriptor indexing: runtimeDescriptorArray={}, partiallyBound={}, "
				"updateUnusedWhilePending={}, sampledImageUAB={}, storageImageUAB={}, "
				"sampledNonUniform={}, storageNonUniform={}",
				yesNo(capabilities.m_RuntimeDescriptorArray),
				yesNo(capabilities.m_DescriptorBindingPartiallyBound),
				yesNo(capabilities.m_DescriptorBindingUpdateUnusedWhilePending),
				yesNo(capabilities.m_DescriptorBindingSampledImageUpdateAfterBind),
				yesNo(capabilities.m_DescriptorBindingStorageImageUpdateAfterBind),
				yesNo(capabilities.m_ShaderSampledImageArrayNonUniformIndexing),
				yesNo(capabilities.m_ShaderStorageImageArrayNonUniformIndexing)));
			LogBootstrapInfo(std::format(
				"  descriptor capacity required={}/{} available={}/{} selected={}/{}",
				GGLabDescriptorCapacityContract.m_ResourceDescriptorCount,
				GGLabDescriptorCapacityContract.m_SamplerDescriptorCount,
				snapshot.m_DescriptorCapacityAvailability.m_ResourceDescriptorCount,
				snapshot.m_DescriptorCapacityAvailability.m_SamplerDescriptorCount,
				GGLabDescriptorCapacityContract.m_ResourceDescriptorCount,
				GGLabDescriptorCapacityContract.m_SamplerDescriptorCount));
			LogBootstrapInfo(std::format("  global set-1 layout support: {}",
				capabilities.m_GlobalDescriptorSetLayoutSupported ? "yes" : "no"));
			LogBootstrapInfo(std::format("  required format features: {}",
				capabilities.m_RequiredFormatFeaturesSupported ? "yes" : "no"));
		}

		[[nodiscard]] bool OnlyMissingLayoutSupport(
			const VulkanDeviceProfileEvaluation& evaluation) noexcept
		{
			for (size_t index = 0; index < evaluation.m_RejectionReasonCount; ++index)
			{
				if (evaluation.m_RejectionReasons[index] !=
					VulkanDeviceProfileRejectionReason::GlobalDescriptorSetLayoutUnsupported)
				{
					return false;
				}
			}
			return true;
		}
	}

	VulkanAdapterSelectionResult SelectVulkanAdapter(
		const std::vector<VulkanAdapterCapabilitySnapshot>& snapshots,
		const VulkanAdapterSelectionRequest& request) noexcept
	{
		VulkanAdapterSelectionResult result{};

		const auto acceptedIndices = [&snapshots]()
		{
			std::vector<uint32_t> indices;
			for (uint32_t index = 0; index < snapshots.size(); ++index)
			{
				if (snapshots[index].m_ProfileEvaluation.IsAccepted())
				{
					indices.push_back(index);
				}
			}
			return indices;
		}();

		switch (request.m_Kind)
		{
		case VulkanAdapterSelectionKind::Default:
		{
			if (acceptedIndices.empty())
			{
				result.m_Status = VulkanAdapterSelectionStatus::NoAcceptedAdapter;
				return result;
			}
			// Deterministic ranking: discrete first, then integrated, then
			// other, then CPU; stable within one rank by enumeration index.
			const uint32_t selected = *std::ranges::min_element(acceptedIndices,
				[&snapshots](uint32_t lhs, uint32_t rhs)
				{
					const uint32_t lhsRank =
						DefaultAdapterRank(snapshots[lhs].m_Identity.m_DeviceType);
					const uint32_t rhsRank =
						DefaultAdapterRank(snapshots[rhs].m_Identity.m_DeviceType);
					if (lhsRank != rhsRank)
					{
						return lhsRank < rhsRank;
					}
					return lhs < rhs;
				});
			result.m_Status = VulkanAdapterSelectionStatus::Selected;
			result.m_SelectedIndex = selected;
			return result;
		}
		case VulkanAdapterSelectionKind::Index:
		{
			if (request.m_Index >= snapshots.size())
			{
				result.m_Status = VulkanAdapterSelectionStatus::IndexOutOfRange;
				return result;
			}
			if (!snapshots[request.m_Index].m_ProfileEvaluation.IsAccepted())
			{
				result.m_Status = VulkanAdapterSelectionStatus::RejectedAdapter;
				result.m_SelectedIndex = request.m_Index;
				return result;
			}
			result.m_Status = VulkanAdapterSelectionStatus::Selected;
			result.m_SelectedIndex = request.m_Index;
			return result;
		}
		case VulkanAdapterSelectionKind::Prefix:
		{
			std::vector<uint32_t> matches;
			for (uint32_t index = 0; index < snapshots.size(); ++index)
			{
				if (MatchesPrefix(snapshots[index], request.m_Prefix))
				{
					matches.push_back(index);
				}
			}
			if (matches.empty())
			{
				result.m_Status = VulkanAdapterSelectionStatus::SelectorNoMatch;
				return result;
			}
			if (matches.size() > 1)
			{
				result.m_Status = VulkanAdapterSelectionStatus::SelectorAmbiguous;
				return result;
			}
			if (!snapshots[matches[0]].m_ProfileEvaluation.IsAccepted())
			{
				result.m_Status = VulkanAdapterSelectionStatus::RejectedAdapter;
				result.m_SelectedIndex = matches[0];
				return result;
			}
			result.m_Status = VulkanAdapterSelectionStatus::Selected;
			result.m_SelectedIndex = matches[0];
			return result;
		}
		}
		result.m_Status = VulkanAdapterSelectionStatus::NoAcceptedAdapter;
		return result;
	}

	int RunVulkanBootstrap(
		const VulkanBootstrapOptions& options, VulkanBootstrapReport& outReport) noexcept
	{
		outReport = {};

		if (sizeof(void*) != 8)
		{
			LogBootstrapError("Vulkan backend requires Windows x64.");
			return 1;
		}

		VulkanInstance::CreateInfo instanceCreateInfo{};
		instanceCreateInfo.m_RequestValidation = options.m_RequestValidation;
		instanceCreateInfo.m_IsDebugBuild = options.m_IsDebugBuild;
		VulkanInstance::Result instanceResult = VulkanInstance::Create(instanceCreateInfo);
		if (!instanceResult.Succeeded())
		{
			LogBootstrapError(
				std::format("Vulkan instance creation failed: {}", instanceResult.m_Error));
			return 1;
		}
		std::unique_ptr<VulkanInstance> instance = std::move(instanceResult.m_Instance);
		outReport.m_HasDebugMessenger = instanceResult.m_HasDebugMessenger;

		LogBootstrapInfo(std::format("Vulkan instance created (validation={}).",
			outReport.m_HasDebugMessenger ? "enabled" : "disabled"));

		VulkanWin32Surface::Result surfaceResult =
			VulkanWin32Surface::Create(instance->Get(), options.m_HInstance, options.m_Hwnd);
		if (!surfaceResult.Succeeded())
		{
			LogBootstrapError(
				std::format("Vulkan surface creation failed: {}", surfaceResult.m_Error));
			return 1;
		}
		std::unique_ptr<VulkanWin32Surface> surface = std::move(surfaceResult.m_Surface);
		LogBootstrapInfo("Vulkan Win32 surface created.");

		uint32_t physicalDeviceCount = 0;
		VkResult enumerateResult =
			vkEnumeratePhysicalDevices(instance->Get(), &physicalDeviceCount, nullptr);
		if (enumerateResult != VK_SUCCESS)
		{
			LogBootstrapError(std::format(
				"vkEnumeratePhysicalDevices failed with {}.", ToString(enumerateResult)));
			return 1;
		}
		std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
		if (physicalDeviceCount > 0)
		{
			enumerateResult = vkEnumeratePhysicalDevices(
				instance->Get(), &physicalDeviceCount, physicalDevices.data());
			if (enumerateResult != VK_SUCCESS)
			{
				LogBootstrapError(std::format(
					"vkEnumeratePhysicalDevices failed with {}.", ToString(enumerateResult)));
				return 1;
			}
		}

		std::vector<VulkanAdapterCapabilitySnapshot>& snapshots = outReport.m_Adapters;
		snapshots.reserve(physicalDevices.size());
		std::vector<std::unique_ptr<VulkanDevice>> probeDevices;
		probeDevices.reserve(physicalDevices.size());

		for (uint32_t index = 0; index < physicalDevices.size(); ++index)
		{
			VulkanAdapterCapabilitySnapshot snapshot = QueryVulkanAdapterCapabilitySnapshot(
				instance->Get(), physicalDevices[index], surface->Get(), index);
			snapshot.m_ProfileCapabilities.m_IsWindowsX64 = true;
			snapshot.m_ProfileCapabilities.m_HasVulkanLoader = true;
			snapshot.m_ProfileCapabilities.m_HasWin32SurfaceExtension = true;

			EvaluateVulkanAdapterProfile(snapshot);
			if (OnlyMissingLayoutSupport(snapshot.m_ProfileEvaluation))
			{
				VulkanDevice::CreateInfo deviceCreateInfo{};
				deviceCreateInfo.m_PhysicalDevice = physicalDevices[index];
				deviceCreateInfo.m_ProfileCapabilities = &snapshot.m_ProfileCapabilities;
				deviceCreateInfo.m_GraphicsPresentQueueFamilyIndex =
					snapshot.m_GraphicsPresentQueueFamilyIndex;
				deviceCreateInfo.m_GraphicsPresentQueueCount =
					snapshot.m_GraphicsPresentQueueCount;
				VulkanDevice::Result deviceResult = VulkanDevice::Create(deviceCreateInfo);
				if (deviceResult.Succeeded())
				{
					snapshot.m_ProfileCapabilities.m_GlobalDescriptorSetLayoutSupported =
						ProbeGlobalDescriptorSetLayoutSupport(deviceResult.m_Device->Get());
					probeDevices.push_back(std::move(deviceResult.m_Device));
				}
				else
				{
					LogBootstrapError(std::format(
						"Adapter [{}] device creation for layout probe failed: {}.",
						index, deviceResult.m_Error));
				}
				EvaluateVulkanAdapterProfile(snapshot);
			}
			snapshots.push_back(std::move(snapshot));
		}

		for (const auto& snapshot : snapshots)
		{
			LogAdapterSummary(snapshot);
			LogAdapterProfile(snapshot);
			LogAdapterEvaluation(snapshot);
		}

		const VulkanAdapterSelectionResult selection =
			SelectVulkanAdapter(snapshots, options.m_SelectionRequest);
		switch (selection.m_Status)
		{
		case VulkanAdapterSelectionStatus::Selected:
			break;
		case VulkanAdapterSelectionStatus::NoAcceptedAdapter:
			LogBootstrapError("No adapter satisfies the GGLab Vulkan device profile.");
			return 1;
		case VulkanAdapterSelectionStatus::IndexOutOfRange:
			LogBootstrapError(std::format("Adapter index {} is out of range ({} adapters enumerated).",
				options.m_SelectionRequest.m_Index, snapshots.size()));
			return 1;
		case VulkanAdapterSelectionStatus::RejectedAdapter:
		{
			const auto& rejected = snapshots[selection.m_SelectedIndex];
			LogBootstrapError(std::format("Selected adapter [{}] '{}' does not satisfy the profile:",
				rejected.m_Identity.m_EnumerationIndex, rejected.m_Identity.m_DeviceName));
			for (size_t reasonIndex = 0;
				reasonIndex < rejected.m_ProfileEvaluation.m_RejectionReasonCount; ++reasonIndex)
			{
				LogBootstrapError(std::format("  - {}",
					VulkanDeviceProfileRejectionReasonText(
						rejected.m_ProfileEvaluation.m_RejectionReasons[reasonIndex])));
			}
			return 1;
		}
		case VulkanAdapterSelectionStatus::SelectorNoMatch:
			LogBootstrapError(std::format("Adapter selector '{}' matched no adapter.",
				options.m_SelectionRequest.m_Prefix));
			return 1;
		case VulkanAdapterSelectionStatus::SelectorAmbiguous:
			LogBootstrapError(std::format("Adapter selector '{}' matched multiple adapters.",
				options.m_SelectionRequest.m_Prefix));
			return 1;
		}

		outReport.m_SelectedAdapterIndex = selection.m_SelectedIndex;
		const auto& selectedSnapshot = snapshots[selection.m_SelectedIndex];
		LogSelectedAdapter(selectedSnapshot);

		// Destroy the probe devices of adapters that were not selected; the
		// selected adapter keeps its probe device as the qualification device.
		for (uint32_t index = 0; index < probeDevices.size(); ++index)
		{
			if (index != selection.m_SelectedIndex)
			{
				probeDevices[index].reset();
			}
		}
		if (selection.m_SelectedIndex < probeDevices.size() &&
			probeDevices[selection.m_SelectedIndex])
		{
			LogBootstrapInfo(std::format("Vulkan device created on adapter [{}] '{}'.",
				selectedSnapshot.m_Identity.m_EnumerationIndex,
				selectedSnapshot.m_Identity.m_DeviceName));
		}
		else
		{
			LogBootstrapError(std::format(
				"Selected adapter [{}] has no logical device; profile acceptance is inconsistent.",
				selection.m_SelectedIndex));
			return 1;
		}

		LogBootstrapInfo("Vulkan bootstrap qualification succeeded; cleaning up.");
		probeDevices.clear();
		surface.reset();
		instance.reset();
		return 0;
	}
}
