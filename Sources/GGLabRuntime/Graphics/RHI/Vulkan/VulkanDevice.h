#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/Vulkan/VulkanAdapter.h"
#include "Graphics/RHI/Vulkan/VulkanDescriptorManager.h"
#include "Graphics/RHI/Vulkan/VulkanResourceManager.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <memory>
#include <string>
#include <thread>

namespace gglab
{
	class VulkanTimelineFence;

	struct VulkanQueueSelection final
	{
		uint32_t m_RequestedQueueCount = 0;
		uint32_t m_GraphicsQueueIndex = 0;
		uint32_t m_TransferQueueIndex = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_RequestedQueueCount > 0;
		}
		[[nodiscard]] constexpr bool HasSeparateTransferQueue() const noexcept
		{
			return IsValid() && m_TransferQueueIndex != m_GraphicsQueueIndex;
		}
	};

	[[nodiscard]] constexpr VulkanQueueSelection SelectVulkanQueues(
		uint32_t availableQueueCount) noexcept
	{
		if (availableQueueCount == 0)
		{
			return {};
		}
		return {
			.m_RequestedQueueCount = availableQueueCount >= 2 ? 2u : 1u,
			.m_GraphicsQueueIndex = 0,
			.m_TransferQueueIndex = availableQueueCount >= 2 ? 1u : 0u,
		};
	}

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
	// allocator and the resource subsystem. The graphics/present queue is
	// always created; a second queue from the same family is used for transfer
	// when available. Frame objects are produced by the frame runtime, which
	// borrows the device and registers its graphics timeline here so
	// resource retirement can resolve RHIFencePoints.
	class VulkanDevice final : public RHIDevice
	{
	public:
		struct CreateInfo
		{
			VkInstance m_Instance = VK_NULL_HANDLE;
			VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
			const VulkanAdapterIdentity* m_AdapterIdentity = nullptr;
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
			// Hardware availability of the mirror-clamp-to-edge sampler
			// address mode; the device enables the feature when available.
			bool m_SamplerMirrorClampToEdgeAvailable = false;
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
		[[nodiscard]] VkQueue GetTransferQueue() const noexcept { return m_TransferQueue; }
		[[nodiscard]] uint32_t GetGraphicsQueueFamilyIndex() const noexcept
		{
			return m_QueueFamilyIndex;
		}
		[[nodiscard]] uint32_t GetTransferQueueFamilyIndex() const noexcept
		{
			return m_QueueFamilyIndex;
		}
		[[nodiscard]] uint32_t GetGraphicsQueueIndex() const noexcept
		{
			return m_QueueSelection.m_GraphicsQueueIndex;
		}
		[[nodiscard]] uint32_t GetTransferQueueIndex() const noexcept
		{
			return m_QueueSelection.m_TransferQueueIndex;
		}
		[[nodiscard]] bool HasSeparateTransferQueue() const noexcept
		{
			return m_QueueSelection.HasSeparateTransferQueue();
		}
		[[nodiscard]] const VkPhysicalDeviceLimits& GetPhysicalDeviceLimits() const noexcept
		{
			return m_PhysicalDeviceLimits;
		}

		[[nodiscard]] VmaAllocator GetMemAllocator() const noexcept { return m_MemAllocator; }
		[[nodiscard]] VulkanDescriptorManager& GetDescriptorManager() noexcept
		{
			return m_DescriptorManager;
		}
		[[nodiscard]] const VulkanDescriptorManager& GetDescriptorManager() const noexcept
		{
			return m_DescriptorManager;
		}
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
		[[nodiscard]] bool IsSamplerMirrorClampToEdgeEnabled() const noexcept
		{
			return m_SamplerMirrorClampToEdgeEnabled;
		}

		RHIBackendType GetBackendType() const noexcept override { return RHIBackendType::Vulkan; }
		std::string_view GetAdapterCompatibilityIdentity() const noexcept override
		{
			return m_AdapterCompatibilityIdentity;
		}
		RHIShaderWaveCapabilities GetShaderWaveCapabilities() const noexcept override
		{
			return m_ShaderWaveCapabilities;
		}
		RHITextureSupportResult QueryTextureSupport(
			const RHITextureDesc& desc) const noexcept override;
		RHITextureSupportResult QueryTextureViewSupport(const RHITextureDesc& textureDesc,
			const RHITextureViewDesc& viewDesc) const noexcept override;
		RHITextureHandle CreateTexture(const RHIOwnedTextureCreateInfo& createInfo,
			const RHIResourceDebugIdentityDesc& debugIdentity = {}) noexcept override;
		RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc,
			const RHIResourceDebugIdentityDesc& debugIdentity = {}) noexcept override;
		RHITextureViewHandle CreateTextureView(
			RHITextureHandle texture, const RHITextureViewDesc& desc) noexcept override;
		RHIBufferViewHandle CreateBufferView(
			RHIBufferHandle buffer, const RHIBufferViewDesc& desc) noexcept override;
		RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) noexcept override;
		void DestroyTexture(RHITextureHandle texture) noexcept override;
		void DestroyBuffer(RHIBufferHandle buffer) noexcept override;
		void DestroyTextureView(RHITextureViewHandle view) noexcept override;
		void DestroyBufferView(RHIBufferViewHandle view) noexcept override;
		void DestroySampler(RHISamplerHandle sampler) noexcept override;
		void SetTextureDebugBinding(RHITextureHandle texture,
			const RHIResourceDebugBindingDesc& binding) noexcept override;
		void SetBufferDebugBinding(RHIBufferHandle buffer,
			const RHIResourceDebugBindingDesc& binding) noexcept override;
		std::string_view GetTextureDebugName(RHITextureHandle texture) const noexcept override;
		std::string_view GetBufferDebugName(RHIBufferHandle buffer) const noexcept override;
		void* MapBuffer(RHIBufferHandle buffer, RHIMappedBufferRange readRange) noexcept override;
		void UnmapBuffer(
			RHIBufferHandle buffer, RHIMappedBufferRange writtenRange) noexcept override;
		uint32_t GetBufferViewAlignment(RHIBufferViewType viewType) const noexcept override;
		bool IsAlive(RHITextureHandle texture) const noexcept override;
		bool IsAlive(RHIBufferHandle buffer) const noexcept override;
		bool IsAlive(RHISamplerHandle sampler) const noexcept override;
		RHIDescriptorHandle GetTextureViewDescriptor(
			RHITextureViewHandle view) const noexcept override;
		RHIDescriptorHandle GetBufferViewDescriptor(
			RHIBufferViewHandle view) const noexcept override;
		RHIDescriptorHandle GetSamplerDescriptor(
			RHISamplerHandle sampler) const noexcept override;
		bool PublishTextureViewDescriptor(RHITextureViewHandle view) noexcept override;
		bool PublishSamplerDescriptor(RHISamplerHandle sampler) noexcept override;
		void RecordTextureUse(
			RHITextureHandle texture, const RHIFencePoint& fencePoint) noexcept override;
		void RecordBufferUse(
			RHIBufferHandle buffer, const RHIFencePoint& fencePoint) noexcept override;
		void RetireCompletedWork() noexcept override;

		[[nodiscard]] bool IsOwnerThread() const noexcept
		{
			return std::this_thread::get_id() == m_OwnerThreadId;
		}
		[[nodiscard]] bool RequireOwnerThread(std::string_view operation) const noexcept;

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
		[[nodiscard]] VulkanTimelineFence* GetTransferTimeline() const noexcept
		{
			return m_TransferTimeline.get();
		}
		[[nodiscard]] bool IsFencePointCompleted(
			const RHIFencePoint& fencePoint) const noexcept override;

	private:
		void Destroy() noexcept;

		VkInstance m_Instance = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue m_TransferQueue = VK_NULL_HANDLE;
		uint32_t m_QueueFamilyIndex = 0;
		VulkanQueueSelection m_QueueSelection{};
		RHIPortabilityCapabilities m_PortabilityCapabilities{};
		RHIPortabilityCapabilities m_EnabledPortabilityCapabilities{};
		bool m_ImageCubeArrayEnabled = false;
		bool m_SamplerMirrorClampToEdgeEnabled = false;
		VulkanTimelineFence* m_GraphicsTimeline = nullptr;
		std::unique_ptr<VulkanTimelineFence> m_TransferTimeline;
		std::thread::id m_OwnerThreadId{};
		std::string m_AdapterCompatibilityIdentity;
		RHIShaderWaveCapabilities m_ShaderWaveCapabilities{};
		VkPhysicalDeviceLimits m_PhysicalDeviceLimits{};

		VmaAllocator m_MemAllocator = VK_NULL_HANDLE;
		VulkanDescriptorManager m_DescriptorManager;
		VulkanResourceManager m_ResourceManager;
	};
}
