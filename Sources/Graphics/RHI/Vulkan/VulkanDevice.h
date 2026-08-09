#pragma once
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITypes.h"
#include "Graphics/RHI/Vulkan/VulkanAdapter.h"
#include "Graphics/RHI/Vulkan/VulkanResourceManager.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <memory>
#include <string>

namespace gglab
{
	class VulkanTimelineFence;

	// Reduces hardware-available portability capabilities to the set that
	// is deliberately enabled on the logical device: capabilities the
	// backend implements and adopts are enabled when available, everything
	// else is forced off so the device state never claims support that
	// lacks a native lowering.
	[[nodiscard]] constexpr RHIPortabilityCapabilities ApplyVulkanPortabilityPolicy(
		const RHIPortabilityCapabilities& available) noexcept
	{
		RHIPortabilityCapabilities enabled{};
		enabled.m_FillModeNonSolid = available.m_FillModeNonSolid;
		enabled.m_DepthClamp = available.m_DepthClamp;
		enabled.m_DepthBiasClamp = available.m_DepthBiasClamp;
		enabled.m_IndependentBlend = available.m_IndependentBlend;
		// Custom border colors, image-view min LOD and instance divisors
		// are not lowered by the backend: they stay disabled even when the
		// hardware reports them.
		enabled.m_CustomBorderColor = false;
		enabled.m_ImageViewMinLod = false;
		enabled.m_VertexAttributeDivisor = false;
		enabled.m_SampleQuality = false;
		return enabled;
	}

	// Owns the VkDevice created for a profile-accepted adapter, the VMA
	// allocator and the resource subsystem. Only the graphics/present queue
	// is created; frame objects are produced by the frame runtime, which
	// borrows the device and registers its graphics timeline here so
	// resource retirement can resolve RHIFencePoints.
	class VulkanDevice
	{
	public:
		struct CreateInfo
		{
			VkInstance m_Instance = VK_NULL_HANDLE;
			VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
			// Feature availability captured by the capability snapshot; only
			// profile-required features are enabled on the device.
			const VulkanDeviceProfileCapabilities* m_ProfileCapabilities = nullptr;
			// Hardware-available conditional portability capabilities
			// (diagnostics and policy input); they never gate device
			// creation. The device reduces them through
			// ApplyVulkanPortabilityPolicy.
			RHIPortabilityCapabilities m_PortabilityCapabilities{};
			// Hardware availability of cube-array image views; the device
			// enables the feature when available.
			bool m_ImageCubeArrayAvailable = false;
			uint32_t m_GraphicsPresentQueueFamilyIndex = 0;
			uint32_t m_GraphicsPresentQueueCount = 1;
		};

		struct Result
		{
			std::unique_ptr<VulkanDevice> m_Device;
			std::string m_Error;
			VkResult m_Result = VK_SUCCESS;

			[[nodiscard]] bool Succeeded() const noexcept { return m_Device != nullptr; }
		};

	public:
		VulkanDevice() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanDevice);
		~VulkanDevice();

		[[nodiscard]] static Result Create(const CreateInfo& createInfo) noexcept;

		[[nodiscard]] VkDevice Get() const noexcept { return m_Device; }
		[[nodiscard]] VkInstance GetInstance() const noexcept { return m_Instance; }
		[[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const noexcept
		{
			return m_PhysicalDevice;
		}
		[[nodiscard]] VkQueue GetGraphicsQueue() const noexcept { return m_GraphicsQueue; }
		[[nodiscard]] uint32_t GetGraphicsQueueFamilyIndex() const noexcept
		{
			return m_QueueFamilyIndex;
		}

		[[nodiscard]] VmaAllocator GetMemAllocator() const noexcept { return m_MemAllocator; }
		[[nodiscard]] VulkanResourceManager& GetResourceManager() noexcept
		{
			return m_ResourceManager;
		}
		[[nodiscard]] const VulkanResourceManager& GetResourceManager() const noexcept
		{
			return m_ResourceManager;
		}
		// The exposed portability capabilities: only capabilities with a
		// complete native lowering are reported to the RHI. The set stays
		// empty until a consuming feature (pipeline or sampler) lands.
		[[nodiscard]] const RHIPortabilityCapabilities& GetPortabilityCapabilities() const noexcept
		{
			return m_PortabilityCapabilities;
		}
		// The capabilities actually enabled on the logical device (policy
		// applied); used for diagnostics and future pipeline creation.
		[[nodiscard]] const RHIPortabilityCapabilities& GetEnabledPortabilityCapabilities() const noexcept
		{
			return m_EnabledPortabilityCapabilities;
		}
		[[nodiscard]] bool IsImageCubeArrayEnabled() const noexcept { return m_ImageCubeArrayEnabled; }

		// Registers the frame runtime's graphics timeline as the completion
		// source for RHIFencePoint resolution. Borrowed: the frame runtime
		// owns the timeline, registers it here and must unregister it again
		// before the device goes away.
		void SetGraphicsTimeline(VulkanTimelineFence* timeline) noexcept
		{
			m_GraphicsTimeline = timeline;
		}
		[[nodiscard]] VulkanTimelineFence* GetGraphicsTimeline() const noexcept
		{
			return m_GraphicsTimeline;
		}
		[[nodiscard]] bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept;

	private:
		void Destroy() noexcept;

		VkInstance m_Instance = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_QueueFamilyIndex = 0;
		RHIPortabilityCapabilities m_PortabilityCapabilities{};
		RHIPortabilityCapabilities m_EnabledPortabilityCapabilities{};
		bool m_ImageCubeArrayEnabled = false;
		VulkanTimelineFence* m_GraphicsTimeline = nullptr;

		VmaAllocator m_MemAllocator = VK_NULL_HANDLE;
		VulkanResourceManager m_ResourceManager;
	};
}
