#include "Graphics/RHI/Vulkan/VulkanBootstrap.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"
#include "Core/Log/LogMacros.h"
#include "GGLabFoundation/String/StringUtils.h"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

namespace gglab
{
	namespace
	{
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

		[[nodiscard]] bool MatchesPrefix(
			const VulkanAdapterCapabilitySnapshot& snapshot, std::string_view prefix) noexcept
		{
			if (prefix.empty())
			{
				return false;
			}
			if (utils::StartsWithAsciiIgnoreCase(snapshot.m_Identity.m_DeviceName, prefix))
			{
				return true;
			}
			return utils::StartsWithAsciiIgnoreCase(snapshot.m_Identity.UuidHex(), prefix);
		}

		void LogAdapterSummary(const VulkanAdapterCapabilitySnapshot& snapshot) noexcept
		{
			const auto& identity = snapshot.m_Identity;
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("[{}] {} ({})", identity.m_EnumerationIndex,
				identity.m_DeviceName, ToString(identity.m_DeviceType)));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("    vendor=0x{:04x} device=0x{:04x} {} driver={} ({})",
				identity.m_VendorId, identity.m_DeviceId,
				FormatAdapterVersions(identity.m_ApiVersion, identity.m_DriverVersion),
				identity.m_DriverName, identity.m_DriverInfo));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("    uuid={} driverUuid={}", identity.UuidHex(),
				utils::BytesToHexString(identity.m_DriverUuid)));
		}

		void LogAdapterProfile(const VulkanAdapterCapabilitySnapshot& snapshot) noexcept
		{
			const auto& capabilities = snapshot.m_ProfileCapabilities;
			const auto& availability = snapshot.m_DescriptorCapacityAvailability;
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("    graphicsPresentQueue={} (family {}, queues {})",
				snapshot.m_HasGraphicsPresentQueueFamily ? "yes" : "no",
				snapshot.m_GraphicsPresentQueueFamilyIndex, snapshot.m_GraphicsPresentQueueCount));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
				"    descriptor capacity available: resources={}, samplers={}, combined={}",
				availability.m_ResourceDescriptorCount, availability.m_SamplerDescriptorCount,
				availability.m_CombinedDescriptorCount));
			const std::string_view layoutState =
				!snapshot.m_GlobalDescriptorSetLayoutProbed
				? "not-probed"
				: capabilities.m_GlobalDescriptorSetLayoutSupported ? "supported" : "unsupported";
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("    globalSet1LayoutSupport={} requiredFormatFeatures={}",
				layoutState, capabilities.m_RequiredFormatFeaturesSupported ? "yes" : "no"));
			const auto& portability = snapshot.m_PortabilityCapabilities;
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
				"    conditional features: imageCubeArray={} samplerMirrorClampToEdge={} "
				"fillModeNonSolid={} depthClamp={} depthBiasClamp={} independentBlend={}",
				snapshot.m_ImageCubeArrayAvailable ? "yes" : "no",
				snapshot.m_SamplerMirrorClampToEdgeAvailable ? "yes" : "no",
				portability.m_FillModeNonSolid ? "yes" : "no",
				portability.m_DepthClamp ? "yes" : "no",
				portability.m_DepthBiasClamp ? "yes" : "no",
				portability.m_IndependentBlend ? "yes" : "no"));
			if (!snapshot.m_LayoutProbeError.empty())
			{
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("    layout probe failed: {}",
					snapshot.m_LayoutProbeError));
			}
			for (const VulkanFormatSupportDiagnostic& format : snapshot.m_FormatDiagnostics)
			{
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("    format {} ({}) supported={}",
					format.m_FormatName, format.m_Usage, format.m_Supported ? "yes" : "no"));
			}
		}

		void LogAdapterEvaluation(const VulkanAdapterCapabilitySnapshot& snapshot) noexcept
		{
			if (snapshot.m_ProfileEvaluation.IsAccepted())
			{
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS("    PROFILE: ACCEPTED");
				return;
			}
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS("    PROFILE: REJECTED");
			for (size_t index = 0; index < snapshot.m_ProfileEvaluation.m_RejectionReasonCount;
				++index)
			{
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("      - {}",
					VulkanDeviceProfileRejectionReasonText(
						snapshot.m_ProfileEvaluation.m_RejectionReasons[index])));
			}
		}

		void LogSelectedAdapter(const VulkanAdapterCapabilitySnapshot& snapshot) noexcept
		{
			const auto& identity = snapshot.m_Identity;
			const auto& capabilities = snapshot.m_ProfileCapabilities;
			const auto yesNo = [](bool value) noexcept { return value ? "yes" : "no"; };

			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("Selected adapter: {} ({}) vendor=0x{:04x} device=0x{:04x}",
				identity.m_DeviceName, ToString(identity.m_DeviceType), identity.m_VendorId,
				identity.m_DeviceId));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("  queue family {} ({} queues)",
				snapshot.m_GraphicsPresentQueueFamilyIndex, snapshot.m_GraphicsPresentQueueCount));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("  enabled device extensions: {}, {}",
				VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
				"  features: dynamicRendering={}, synchronization2={}, timelineSemaphore={}, "
				"scalarBlockLayout={}, samplerAnisotropy={}, shaderStorageImageExtendedFormats={}, "
				"mutableDescriptorType={}",
				yesNo(capabilities.m_DynamicRendering), yesNo(capabilities.m_Synchronization2),
				yesNo(capabilities.m_TimelineSemaphore), yesNo(capabilities.m_ScalarBlockLayout),
				yesNo(capabilities.m_SamplerAnisotropy),
				yesNo(capabilities.m_ShaderStorageImageExtendedFormats),
				yesNo(capabilities.m_MutableDescriptorType)));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
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
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
				"  descriptor capacity required={}/{} available={}/{} selected={}/{}",
				GGLabDescriptorCapacityContract.m_ResourceDescriptorCount,
				GGLabDescriptorCapacityContract.m_SamplerDescriptorCount,
				snapshot.m_DescriptorCapacityAvailability.m_ResourceDescriptorCount,
				snapshot.m_DescriptorCapacityAvailability.m_SamplerDescriptorCount,
				GGLabDescriptorCapacityContract.m_ResourceDescriptorCount,
				GGLabDescriptorCapacityContract.m_SamplerDescriptorCount));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("  global set-1 layout support: {}",
				capabilities.m_GlobalDescriptorSetLayoutSupported ? "yes" : "no"));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("  required format features: {}",
				capabilities.m_RequiredFormatFeaturesSupported ? "yes" : "no"));
		}

		[[nodiscard]] VulkanDevice::CreateInfo MakeDeviceCreateInfo(VkInstance instance,
			VkPhysicalDevice physicalDevice,
			const VulkanAdapterCapabilitySnapshot& snapshot) noexcept
		{
			VulkanDevice::CreateInfo deviceCreateInfo{};
			deviceCreateInfo.m_Instance = instance;
			deviceCreateInfo.m_PhysicalDevice = physicalDevice;
			deviceCreateInfo.m_AdapterIdentity = &snapshot.m_Identity;
			deviceCreateInfo.m_ProfileCapabilities = &snapshot.m_ProfileCapabilities;
			deviceCreateInfo.m_PortabilityCapabilities =
				snapshot.m_PortabilityCapabilities;
			deviceCreateInfo.m_ImageCubeArrayAvailable =
				snapshot.m_ImageCubeArrayAvailable;
			deviceCreateInfo.m_SamplerMirrorClampToEdgeAvailable =
				snapshot.m_SamplerMirrorClampToEdgeAvailable;
			deviceCreateInfo.m_GraphicsPresentQueueFamilyIndex =
				snapshot.m_GraphicsPresentQueueFamilyIndex;
			deviceCreateInfo.m_GraphicsPresentQueueCount =
				snapshot.m_GraphicsPresentQueueCount;
			return deviceCreateInfo;
		}

		// Objects created by the qualification run that outlive the report:
		// the final selected device plus the instance and surface it runs on.
		// Destruction order (reverse declaration order) is surface, device,
		// instance.
		struct VulkanBootstrapQualification
		{
			std::unique_ptr<VulkanInstance> m_Instance;
			std::unique_ptr<VulkanDevice> m_Device;
			std::unique_ptr<VulkanSurface> m_Surface;
			VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		};

		// Runs the full qualification (instance, debug messenger, surface,
		// enumeration, layout probes, selection, final device). Returns a
		// process exit code; on success the selected objects are moved into
		// outObjects and the caller owns their lifetime.
		[[nodiscard]] int RunVulkanBootstrapQualification(
			const VulkanBootstrapOptions& options, VulkanBootstrapReport& outReport,
			VulkanBootstrapQualification& outObjects) noexcept
		{
			outReport = {};
			outObjects = {};
			if (options.m_SurfaceFactory == nullptr)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("Vulkan bootstrap requires a surface factory.");
				return 1;
			}

			if (sizeof(void*) != 8)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("Vulkan backend requires Windows x64.");
				return 1;
			}

			VulkanInstance::CreateInfo instanceCreateInfo{};
			instanceCreateInfo.m_RequestValidation = options.m_RequestValidation;
			instanceCreateInfo.m_RequiredInstanceExtensions =
				options.m_SurfaceFactory->RequiredInstanceExtensionNames();
			VulkanInstance::Result instanceResult = VulkanInstance::Create(instanceCreateInfo);
			if (!instanceResult.Succeeded())
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					std::format("Vulkan instance creation failed: {}", instanceResult.m_Error));
				return 1;
			}
			std::unique_ptr<VulkanInstance> instance = std::move(instanceResult.m_Instance);
			outReport.m_HasDebugMessenger = instanceResult.m_HasDebugMessenger;

			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("Vulkan instance created (validation={}).",
				outReport.m_HasDebugMessenger ? "enabled" : "disabled"));

			VulkanSurfaceFactoryBase::Result surfaceResult =
				options.m_SurfaceFactory->Create(instance->Get());
			if (!surfaceResult.Succeeded())
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					std::format("Vulkan surface creation failed: {}", surfaceResult.m_Error));
				return 1;
			}
			std::unique_ptr<VulkanSurface> surface = std::move(surfaceResult.m_Surface);
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS("Vulkan surface created.");

			uint32_t physicalDeviceCount = 0;
			VkResult enumerateResult =
				vkEnumeratePhysicalDevices(instance->Get(), &physicalDeviceCount, nullptr);
			if (enumerateResult != VK_SUCCESS)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
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
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
						"vkEnumeratePhysicalDevices failed with {}.", ToString(enumerateResult)));
					return 1;
				}
			}

			std::vector<VulkanAdapterCapabilitySnapshot>& snapshots = outReport.m_Adapters;
			snapshots.reserve(physicalDevices.size());

			for (uint32_t index = 0; index < physicalDevices.size(); ++index)
			{
				VulkanAdapterCapabilitySnapshot snapshot = QueryVulkanAdapterCapabilitySnapshot(
					instance->Get(), physicalDevices[index], surface->Get(), index);
				snapshot.m_ProfileCapabilities.m_IsWindowsX64 = true;
				snapshot.m_ProfileCapabilities.m_HasVulkanLoader = true;
				snapshot.m_ProfileCapabilities.m_HasWin32SurfaceExtension = true;

				// Preliminary evaluation neutralizes the descriptor-set layout
				// gate because the probe has not run yet; "not probed" must never
				// report as "unsupported". Adapters that fail any other
				// requirement never create a probe device.
				const VulkanDeviceProfileEvaluation preliminary =
					EvaluateVulkanAdapterProfilePreliminary(snapshot);
				if (!preliminary.IsAccepted())
				{
					snapshot.m_ProfileEvaluation = preliminary;
					snapshots.push_back(std::move(snapshot));
					continue;
				}

				// The adapter passes every non-layout requirement: run the layout
				// probe with a local temporary device that is destroyed before the
				// next adapter is examined. A failed probe device leaves the
				// adapter unverified (probed stays false), so it cannot be selected.
				{
					VulkanDevice::Result probeResult = VulkanDevice::Create(
						MakeDeviceCreateInfo(instance->Get(), physicalDevices[index], snapshot));
					if (!probeResult.Succeeded())
					{
						snapshot.m_LayoutProbeError = probeResult.m_Error;
						GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
							"Adapter [{}] temporary layout-probe device creation failed: {}.",
							index, probeResult.m_Error));
						snapshots.push_back(std::move(snapshot));
						continue;
					}

					snapshot.m_GlobalDescriptorSetLayoutProbed = true;
					snapshot.m_ProfileCapabilities.m_GlobalDescriptorSetLayoutSupported =
						ProbeGlobalDescriptorSetLayoutSupport(probeResult.m_Device->Get());
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
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("No adapter satisfies the GGLab Vulkan device profile.");
				return 1;
			case VulkanAdapterSelectionStatus::IndexOutOfRange:
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format("Adapter index {} is out of range ({} adapters enumerated).",
					options.m_SelectionRequest.m_Index, snapshots.size()));
				return 1;
			case VulkanAdapterSelectionStatus::RejectedAdapter:
			{
				const auto& rejected = snapshots[selection.m_SelectedIndex];
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format("Selected adapter [{}] '{}' does not satisfy the profile:",
					rejected.m_Identity.m_EnumerationIndex, rejected.m_Identity.m_DeviceName));
				for (size_t reasonIndex = 0;
					reasonIndex < rejected.m_ProfileEvaluation.m_RejectionReasonCount; ++reasonIndex)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format("  - {}",
						VulkanDeviceProfileRejectionReasonText(
							rejected.m_ProfileEvaluation.m_RejectionReasons[reasonIndex])));
				}
				return 1;
			}
			case VulkanAdapterSelectionStatus::SelectorNoMatch:
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format("Adapter selector '{}' matched no adapter.",
					options.m_SelectionRequest.m_Prefix));
				return 1;
			case VulkanAdapterSelectionStatus::SelectorAmbiguous:
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format("Adapter selector '{}' matched multiple adapters.",
					options.m_SelectionRequest.m_Prefix));
				return 1;
			}

			outReport.m_SelectedAdapterIndex = selection.m_SelectedIndex;
			const auto& selectedSnapshot = snapshots[selection.m_SelectedIndex];
			LogSelectedAdapter(selectedSnapshot);

			// Create the final qualification device for the selected adapter
			// with the same configuration as the temporary probe device. It is
			// moved to the caller instead of being destroyed immediately, so a
			// minimal frame runtime can run on it later.
			{
				VulkanDevice::Result deviceResult = VulkanDevice::Create(
					MakeDeviceCreateInfo(instance->Get(), physicalDevices[selection.m_SelectedIndex],
						selectedSnapshot));
				if (!deviceResult.Succeeded())
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
						"Selected adapter [{}] '{}' device creation failed: {}.",
						selectedSnapshot.m_Identity.m_EnumerationIndex,
						selectedSnapshot.m_Identity.m_DeviceName, deviceResult.m_Error));
					return 1;
				}
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format("Vulkan device created on adapter [{}] '{}'.",
					selectedSnapshot.m_Identity.m_EnumerationIndex,
					selectedSnapshot.m_Identity.m_DeviceName));
				outObjects.m_Device = std::move(deviceResult.m_Device);
			}

			outObjects.m_PhysicalDevice = physicalDevices[selection.m_SelectedIndex];
			outObjects.m_Surface = std::move(surface);
			outObjects.m_Instance = std::move(instance);
			return 0;
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
					// An accepted profile alone is not enough: the layout support
					// must have been verified by an actual probe, otherwise the
					// adapter cannot be selected.
					if (snapshots[index].m_ProfileEvaluation.IsAccepted() &&
						snapshots[index].m_GlobalDescriptorSetLayoutProbed)
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
			if (!snapshots[request.m_Index].m_ProfileEvaluation.IsAccepted() ||
				!snapshots[request.m_Index].m_GlobalDescriptorSetLayoutProbed)
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
			if (!snapshots[matches[0]].m_ProfileEvaluation.IsAccepted() ||
				!snapshots[matches[0]].m_GlobalDescriptorSetLayoutProbed)
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
		VulkanBootstrapQualification objects;
		const int exitCode = RunVulkanBootstrapQualification(options, outReport, objects);
		if (exitCode == 0)
		{
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS("Vulkan bootstrap qualification succeeded; cleaning up.");
		}
		// Destruction order is enforced by member order: surface, device,
		// instance. The frame runtime is never created on this path.
		return exitCode;
	}

	VulkanBootstrapRuntimeResult CreateVulkanBootstrapRuntime(
		const VulkanBootstrapRuntimeCreateInfo& createInfo) noexcept
	{
		VulkanBootstrapRuntimeResult result{};
		if (createInfo.m_Width == 0 || createInfo.m_Height == 0)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error = "CreateVulkanBootstrapRuntime requires a nonzero drawable extent.";
			return result;
		}

		VulkanBootstrapReport report;
		VulkanBootstrapQualification objects;
		const int exitCode =
			RunVulkanBootstrapQualification(createInfo.m_BootstrapOptions, report, objects);
		if (exitCode != 0)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error = "Vulkan bootstrap qualification failed; see the log for rejection reasons.";
			return result;
		}

		result.m_SelectedSnapshot = report.m_Adapters[report.m_SelectedAdapterIndex];
		result.m_HasDebugMessenger = report.m_HasDebugMessenger;
		result.m_PhysicalDevice = objects.m_PhysicalDevice;

		VulkanFrameRuntimeCreateInfo frameInfo{};
		frameInfo.m_Instance = objects.m_Instance.get();
		frameInfo.m_Surface = objects.m_Surface->Get();
		frameInfo.m_PhysicalDevice = objects.m_PhysicalDevice;
		frameInfo.m_Device = objects.m_Device.get();
		frameInfo.m_Snapshot = &result.m_SelectedSnapshot;
		frameInfo.m_FrameSlotCount = createInfo.m_FrameSlotCount;
		frameInfo.m_RequestedFormat = createInfo.m_RequestedFormat;
		frameInfo.m_Vsync = createInfo.m_Vsync;
		frameInfo.m_Width = createInfo.m_Width;
		frameInfo.m_Height = createInfo.m_Height;
		VulkanFrameRuntime::Result frameResult = VulkanFrameRuntime::Create(frameInfo);
		if (!frameResult.Succeeded())
		{
			result.m_Result = frameResult.m_Result;
			result.m_Error = frameResult.m_Error;
			return result;
		}

		result.m_Device = std::move(objects.m_Device);
		result.m_Surface = std::move(objects.m_Surface);
		result.m_Instance = std::move(objects.m_Instance);
		result.m_FrameRuntime = std::move(frameResult.m_Runtime);
		return result;
	}
}
