#include "Core/Precompiled.h"

// VMA_IMPLEMENTATION is defined in exactly one translation unit of the
// application. It must be defined before
// any header pulls in vk_mem_alloc.h: the header's include guard would
// otherwise skip the implementation block. The allocator links the static
// Vulkan loader functions through vulkan-1.lib and the version macro pins
// it to the Vulkan 1.3 API profile of this backend.
#define VMA_VULKAN_VERSION 1003000
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "Graphics/RHI/Vulkan/VulkanResourceManager.h"
#include "Graphics/RHI/RHIDescriptorCapacityContract.h"
#include "Graphics/RHI/RHITextureValidation.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanFormat.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <algorithm>
#include <array>
#include <format>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool HasDebugIdentity(const RHIResourceDebugIdentityDesc& identity) noexcept
		{
			return identity.m_Domain != RHIResourceDebugDomain::Unknown ||
				!identity.m_Category.empty() || !identity.m_Label.empty() ||
				!identity.m_Source.empty() || identity.m_StableId.has_value();
		}

		[[nodiscard]] RHIResourceDebugIdentityDesc ResolveDebugIdentity(
			const RHIResourceDebugIdentityDesc& identity, std::string_view legacyName,
			RHIResourceType resourceType) noexcept
		{
			if (HasDebugIdentity(identity))
			{
				return identity;
			}
			return {
				.m_Domain = RHIResourceDebugDomain::Unknown,
				.m_Category = RHIResourceTypeDebugText(resourceType),
				.m_Label = legacyName.empty() ? std::string_view("Unspecified") : legacyName,
			};
		}

		[[nodiscard]] VkImageType ToVkImageType(RHITextureDimension dimension) noexcept
		{
			switch (dimension)
			{
			case RHITextureDimension::Texture1D:
				return VK_IMAGE_TYPE_1D;
			case RHITextureDimension::Texture2D:
				return VK_IMAGE_TYPE_2D;
			case RHITextureDimension::Texture3D:
				return VK_IMAGE_TYPE_3D;
			}
			return VK_IMAGE_TYPE_2D;
		}

		[[nodiscard]] VkSampleCountFlagBits ToVkSampleCount(uint32_t count) noexcept
		{
			switch (count)
			{
			case 2:
				return VK_SAMPLE_COUNT_2_BIT;
			case 4:
				return VK_SAMPLE_COUNT_4_BIT;
			case 8:
				return VK_SAMPLE_COUNT_8_BIT;
			case 16:
				return VK_SAMPLE_COUNT_16_BIT;
			case 32:
				return VK_SAMPLE_COUNT_32_BIT;
			case 64:
				return VK_SAMPLE_COUNT_64_BIT;
			default:
				return VK_SAMPLE_COUNT_1_BIT;
			}
		}

		[[nodiscard]] VkBufferUsageFlags ToVkBufferUsageFlags(RHIBufferUsage usage) noexcept
		{
			VkBufferUsageFlags flags = 0;
			if (Test(usage, RHIBufferUsage::Vertex))
			{
				flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			}
			if (Test(usage, RHIBufferUsage::Index))
			{
				flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			}
			if (Test(usage, RHIBufferUsage::Constant))
			{
				flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			}
			if (Test(usage, RHIBufferUsage::Structured))
			{
				flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
					VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
			}
			if (Test(usage, RHIBufferUsage::UnorderedAccess))
			{
				flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
					VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
			}
			if (Test(usage, RHIBufferUsage::IndirectArgument))
			{
				flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
			}
			if (Test(usage, RHIBufferUsage::CopySource))
			{
				flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			}
			if (Test(usage, RHIBufferUsage::CopyDest))
			{
				flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			}
			return flags;
		}

		[[nodiscard]] VmaMemoryUsage ToVmaMemoryUsage(RHIMemoryUsage usage) noexcept
		{
			switch (usage)
			{
			case RHIMemoryUsage::GpuOnly:
				return VMA_MEMORY_USAGE_GPU_ONLY;
			case RHIMemoryUsage::CpuToGpu:
				return VMA_MEMORY_USAGE_CPU_TO_GPU;
			case RHIMemoryUsage::GpuToCpu:
				return VMA_MEMORY_USAGE_GPU_TO_CPU;
			}
			return VMA_MEMORY_USAGE_GPU_ONLY;
		}

		[[nodiscard]] VkFilter ToVkMagFilter(RHISamplerFilter filter) noexcept
		{
			switch (filter)
			{
			case RHISamplerFilter::MinPointMagLinearMipPoint:
			case RHISamplerFilter::MinPointMagMipLinear:
			case RHISamplerFilter::MinMagLinearMipPoint:
			case RHISamplerFilter::MinMagMipLinear:
			case RHISamplerFilter::Anisotropic:
			case RHISamplerFilter::ComparisonMinMagLinearMipPoint:
			case RHISamplerFilter::ComparisonAnisotropic:
				return VK_FILTER_LINEAR;
			default:
				return VK_FILTER_NEAREST;
			}
		}

		[[nodiscard]] VkFilter ToVkMinFilter(RHISamplerFilter filter) noexcept
		{
			switch (filter)
			{
			case RHISamplerFilter::MinLinearMagMipPoint:
			case RHISamplerFilter::MinLinearMagPointMipLinear:
			case RHISamplerFilter::MinMagLinearMipPoint:
			case RHISamplerFilter::MinMagMipLinear:
			case RHISamplerFilter::Anisotropic:
			case RHISamplerFilter::ComparisonMinMagLinearMipPoint:
			case RHISamplerFilter::ComparisonAnisotropic:
				return VK_FILTER_LINEAR;
			default:
				return VK_FILTER_NEAREST;
			}
		}

		[[nodiscard]] VkSamplerMipmapMode ToVkMipmapMode(RHISamplerFilter filter) noexcept
		{
			switch (filter)
			{
			case RHISamplerFilter::MinMagPointMipLinear:
			case RHISamplerFilter::MinPointMagMipLinear:
			case RHISamplerFilter::MinLinearMagPointMipLinear:
			case RHISamplerFilter::MinMagMipLinear:
			case RHISamplerFilter::Anisotropic:
			case RHISamplerFilter::ComparisonAnisotropic:
				return VK_SAMPLER_MIPMAP_MODE_LINEAR;
			default:
				return VK_SAMPLER_MIPMAP_MODE_NEAREST;
			}
		}

		[[nodiscard]] constexpr bool IsAnisotropicSamplerFilter(
			RHISamplerFilter filter) noexcept
		{
			return filter == RHISamplerFilter::Anisotropic ||
				filter == RHISamplerFilter::ComparisonAnisotropic;
		}

		[[nodiscard]] constexpr bool IsComparisonSamplerFilter(
			RHISamplerFilter filter) noexcept
		{
			return filter == RHISamplerFilter::ComparisonMinMagLinearMipPoint ||
				filter == RHISamplerFilter::ComparisonAnisotropic;
		}

		[[nodiscard]] VkCompareOp ToVkCompareOp(RHICompareOp op) noexcept
		{
			switch (op)
			{
			case RHICompareOp::Never:
				return VK_COMPARE_OP_NEVER;
			case RHICompareOp::Less:
				return VK_COMPARE_OP_LESS;
			case RHICompareOp::Equal:
				return VK_COMPARE_OP_EQUAL;
			case RHICompareOp::LessEqual:
				return VK_COMPARE_OP_LESS_OR_EQUAL;
			case RHICompareOp::Greater:
				return VK_COMPARE_OP_GREATER;
			case RHICompareOp::NotEqual:
				return VK_COMPARE_OP_NOT_EQUAL;
			case RHICompareOp::GreaterEqual:
				return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case RHICompareOp::Always:
				return VK_COMPARE_OP_ALWAYS;
			}
			return VK_COMPARE_OP_NEVER;
		}

		// The sampler contract only supports the three fixed border
		// colors; ValidateRHISamplerPortability rejects everything else
		// before this function runs.
		[[nodiscard]] VkBorderColor ToVkBorderColor(const RHISamplerDesc& desc) noexcept
		{
			const bool transparentBlack = desc.m_BorderColor[0] == 0.0f &&
				desc.m_BorderColor[1] == 0.0f && desc.m_BorderColor[2] == 0.0f &&
				desc.m_BorderColor[3] == 0.0f;
			const bool opaqueWhite = desc.m_BorderColor[0] == 1.0f &&
				desc.m_BorderColor[1] == 1.0f && desc.m_BorderColor[2] == 1.0f &&
				desc.m_BorderColor[3] == 1.0f;
			if (transparentBlack)
			{
				return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			}
			if (opaqueWhite)
			{
				return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			}
			return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		}
	}

	VulkanDescriptorIndexArena::VulkanDescriptorIndexArena(uint32_t capacity) noexcept :
		m_Capacity(capacity), m_Allocated(capacity, 0)
	{
		// The whole [0, capacity) domain is legal; index 0 is a valid
		// descriptor index and exhaustion is reported through std::nullopt.
		m_FreeIndices.reserve(capacity);
		for (uint32_t index = capacity; index > 0; --index)
		{
			m_FreeIndices.push_back(index - 1);
		}
	}

	std::optional<uint32_t> VulkanDescriptorIndexArena::Allocate() noexcept
	{
		if (m_FreeIndices.empty())
		{
			return std::nullopt;
		}
		const uint32_t index = m_FreeIndices.back();
		m_FreeIndices.pop_back();
		GGLAB_ASSERT_MSG(m_Allocated[index] == 0,
			"Vulkan descriptor arena free list contains a live index.");
		m_Allocated[index] = 1;
		++m_LiveCount;
		return index;
	}

	void VulkanDescriptorIndexArena::Release(uint32_t index) noexcept
	{
		if (index >= m_Capacity || m_Allocated[index] == 0)
		{
			return;
		}
		m_Allocated[index] = 0;
		m_FreeIndices.push_back(index);
		--m_LiveCount;
	}

	VulkanResourceManager::~VulkanResourceManager() noexcept = default;

	void VulkanResourceManager::Initialize(VulkanDevice* device) noexcept
	{
		GGLAB_ASSERT_MSG(device != nullptr, "VulkanResourceManager requires a VulkanDevice.");
		GGLAB_ASSERT_MSG(device->GetMemAllocator() != VK_NULL_HANDLE,
			"VulkanResourceManager requires an initialized VMA allocator.");

		m_Device = device;
		m_ResourceDescriptorArena = VulkanDescriptorIndexArena(
			GGLabDescriptorCapacityContract.m_ResourceDescriptorCount);
		m_SamplerDescriptorArena = VulkanDescriptorIndexArena(
			GGLabDescriptorCapacityContract.m_SamplerDescriptorCount);
	}

	void VulkanResourceManager::Finalize() noexcept
	{
		// Keep the leak evidence before draining: every non-free slot is
		// reported while its debug identity is still available, then all
		// surviving native objects are destroyed. The device is expected
		// to be quiesced at this point (the frame runtime is destroyed
		// before the device).
		ReportLiveResources();

		auto drainViewTable = [this](auto& table, auto destroyNative, auto releaseIndex)
			{
				for (uint32_t index = 0; index < table.Size(); ++index)
				{
					auto& slot = table.SlotAt(index);
					if (slot.m_State == RHIHandleSlotState::Free)
					{
						continue;
					}
					destroyNative(slot);
					releaseIndex(slot);
					slot.m_RetirementPoints.clear();
				}
			};

		drainViewTable(m_TextureViews,
			[this](TextureViewSlot& slot) noexcept
			{
				if (slot.m_ImageView != VK_NULL_HANDLE)
				{
					vkDestroyImageView(m_Device->Get(), slot.m_ImageView, nullptr);
					slot.m_ImageView = VK_NULL_HANDLE;
				}
			},
			[this](TextureViewSlot& slot) noexcept
			{
				if (slot.m_DescriptorIndex)
				{
					m_ResourceDescriptorArena.Release(*slot.m_DescriptorIndex);
					slot.m_DescriptorIndex.reset();
				}
			});
		drainViewTable(m_BufferViews,
			[this](BufferViewSlot& slot) noexcept
			{
				if (slot.m_BufferView != VK_NULL_HANDLE)
				{
					vkDestroyBufferView(m_Device->Get(), slot.m_BufferView, nullptr);
					slot.m_BufferView = VK_NULL_HANDLE;
				}
			},
			[this](BufferViewSlot& slot) noexcept
			{
				if (slot.m_DescriptorIndex)
				{
					m_ResourceDescriptorArena.Release(*slot.m_DescriptorIndex);
					slot.m_DescriptorIndex.reset();
				}
			});
		drainViewTable(m_Samplers,
			[this](SamplerSlot& slot) noexcept
			{
				if (slot.m_Sampler != VK_NULL_HANDLE)
				{
					vkDestroySampler(m_Device->Get(), slot.m_Sampler, nullptr);
					slot.m_Sampler = VK_NULL_HANDLE;
				}
			},
			[this](SamplerSlot& slot) noexcept
			{
				if (slot.m_DescriptorIndex)
				{
					m_SamplerDescriptorArena.Release(*slot.m_DescriptorIndex);
					slot.m_DescriptorIndex.reset();
				}
			});

		// Resources release their native handles and VMA allocations
		// through the RAII destructor.
		m_Textures.Clear();
		m_Buffers.Clear();
		m_TextureViews.Clear();
		m_BufferViews.Clear();
		m_Samplers.Clear();
		m_TextureViewCache.clear();
		m_BufferViewCache.clear();
		m_SamplerCache.clear();
		m_TextureResourceViews.clear();
		m_BufferResourceViews.clear();

		m_Device = nullptr;
	}

	RHITextureSupportResult VulkanResourceManager::QueryTextureSupport(
		const RHITextureDesc& desc) const noexcept
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr,
			"VulkanResourceManager must be initialized before querying texture support.");
		return QueryVulkanTextureSupport(m_Device->GetPhysicalDevice(), desc);
	}

	RHITextureHandle VulkanResourceManager::CreateTexture(const RHIOwnedTextureCreateInfo& createInfo,
		const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr,
			"VulkanResourceManager must be initialized before creating textures.");

		const RHITextureDesc& desc = createInfo.m_Desc;
		const RHITextureValidationResult validation = ValidateRHITextureDesc(desc);
		if (!validation.IsValid())
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTexture rejected the texture description: {}.",
				RHITextureValidationErrorText(validation.m_Error));
			return {};
		}
		if (!IsRHIResourceStateValid(
			createInfo.m_InitialState, RHIResourceStateUsage::TextureInitial))
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTexture rejected an invalid initial state.");
			return {};
		}
		if (!IsVulkanFormatSupported(desc.m_Format))
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTexture rejected unsupported RHI format {}.",
				GetRHIFormatInfo(desc.m_Format).m_Name);
			return {};
		}

		// The per-description support query gates creation with the exact
		// contract creation will use: format, tiling, usage, create flags,
		// sample count and size limits.
		const RHITextureSupportResult support = QueryTextureSupport(desc);
		if (!support.IsSupported())
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTexture rejected unsupported texture "
				"description ({}).",
				RHITextureSupportReasonText(support.m_Reason));
			return {};
		}

		const VulkanFormatInfo& formatInfo = GetVulkanFormatInfo(desc.m_Format);
		const VulkanImageCreationContract contract =
			BuildVulkanImageCreationContract(desc);

		VkImageCreateInfo imageCreateInfo{};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		// The format list lives for the whole create call; it is also the
		// only pNext the creation path carries, so the native creation
		// history stored by VulkanTexture never retains a dangling chain.
		std::array<VkFormat, 4> nativeViewFormats{};
		VkImageFormatListCreateInfo formatList{};
		if (contract.m_ViewFormatCount > 0)
		{
			nativeViewFormats = contract.m_ViewFormats;
			formatList.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
			formatList.viewFormatCount = contract.m_ViewFormatCount;
			formatList.pViewFormats = nativeViewFormats.data();
			imageCreateInfo.pNext = &formatList;
		}
		imageCreateInfo.flags = contract.m_CreateFlags;
		imageCreateInfo.imageType = ToVulkanImageType(desc.m_Dimension);
		imageCreateInfo.format = formatInfo.m_ResourceFormat;
		imageCreateInfo.extent = {
			.width = desc.m_Extent.m_Width,
			.height = desc.m_Extent.m_Height,
			.depth = desc.m_Dimension == RHITextureDimension::Texture3D
				? desc.m_Extent.m_Depth
				: 1u,
		};
		imageCreateInfo.mipLevels = desc.m_MipLevels;
		imageCreateInfo.arrayLayers = desc.m_ArraySize;
		imageCreateInfo.samples = ToVulkanSampleCount(desc.m_SampleCount);
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = contract.m_Usage;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo allocationCreateInfo{};
		allocationCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		auto texture = std::make_unique<VulkanTexture>();
		texture->Create(VulkanTexture::CreateInfo{
			.m_Allocator = m_Device->GetMemAllocator(),
			.m_Device = m_Device->Get(),
			.m_CreateInfo = imageCreateInfo,
			.m_AllocationCreateInfo = allocationCreateInfo,
			.m_InitialState = createInfo.m_InitialState,
			});
		if (!texture->IsValid())
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanResourceManager::CreateTexture failed to create the native image.");
			return {};
		}

		const std::string_view legacyName = desc.m_DebugName ? desc.m_DebugName : "";
		const auto resolvedIdentity =
			ResolveDebugIdentity(debugIdentity, legacyName, RHIResourceType::Texture);
		++m_Diagnostics.m_TextureCreateCount;
		RHITextureHandle handle =
			AllocateTextureSlot(std::move(texture), RHIResourceOwnership::Owned, resolvedIdentity);
		if (handle.IsValid())
		{
			TextureSlot& slot = m_Textures.SlotAt(handle.Index());
			slot.m_RHIDesc = desc;
		}
		return handle;
	}

	RHIBufferHandle VulkanResourceManager::CreateBuffer(
		const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr,
			"VulkanResourceManager must be initialized before creating buffers.");

		if (desc.m_SizeInBytes == 0)
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateBuffer rejected a zero-sized buffer.");
			return {};
		}

		VkBufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = desc.m_SizeInBytes;
		bufferCreateInfo.usage = ToVkBufferUsageFlags(desc.m_Usage);
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocationCreateInfo{};
		allocationCreateInfo.usage = ToVmaMemoryUsage(desc.m_MemoryUsage);
		if (desc.m_MemoryUsage == RHIMemoryUsage::CpuToGpu ||
			desc.m_MemoryUsage == RHIMemoryUsage::GpuToCpu)
		{
			// Upload/readback buffers are persistently mapped host-visible
			// allocations; flush/invalidate follow the written/read ranges.
			allocationCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}

		auto buffer = std::make_unique<VulkanBuffer>();
		buffer->Create(VulkanBuffer::CreateInfo{
			.m_Allocator = m_Device->GetMemAllocator(),
			.m_Device = m_Device->Get(),
			.m_CreateInfo = bufferCreateInfo,
			.m_AllocationCreateInfo = allocationCreateInfo,
			.m_InitialState = {
				.m_Stages = RHIStage::None,
				.m_Access = RHIAccess::None,
				.m_Layout = RHILayout::Common,
			},
			.m_MemoryUsage = desc.m_MemoryUsage,
			});
		if (!buffer->IsValid())
		{
			++m_Diagnostics.m_CreateFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanResourceManager::CreateBuffer failed to create the native buffer.");
			return {};
		}

		const std::string_view legacyName = desc.m_DebugName ? desc.m_DebugName : "";
		const auto resolvedIdentity =
			ResolveDebugIdentity(debugIdentity, legacyName, RHIResourceType::Buffer);
		++m_Diagnostics.m_BufferCreateCount;
		RHIBufferHandle handle =
			AllocateBufferSlot(std::move(buffer), RHIResourceOwnership::Owned, resolvedIdentity);
		if (handle.IsValid())
		{
			BufferSlot& slot = m_Buffers.SlotAt(handle.Index());
			slot.m_RHIDesc = desc;
		}
		return handle;
	}

	RHITextureHandle VulkanResourceManager::ImportTexture(const ImportedTextureDesc& desc) noexcept
	{
		if (desc.m_Image == VK_NULL_HANDLE || !IsRHIResourceStateValid(
			desc.m_RHI.m_External.m_InitialState, RHIResourceStateUsage::TextureInitial))
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::ImportTexture rejected its image or initial state.");
			return {};
		}

		auto texture = std::make_unique<VulkanTexture>();
		texture->AdoptExternal(
			m_Device->Get(), desc.m_Image, desc.m_RHI.m_External.m_InitialState);
		if (!texture->IsValid())
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanResourceManager::ImportTexture failed to adopt the native image.");
			return {};
		}

		const char* debugNameText = desc.m_RHI.m_External.m_DebugName
			? desc.m_RHI.m_External.m_DebugName
			: desc.m_RHI.m_Desc.m_DebugName;
		const std::string_view legacyName = debugNameText ? debugNameText : "";
		const auto resolvedIdentity =
			ResolveDebugIdentity(desc.m_DebugIdentity, legacyName, RHIResourceType::Texture);
		++m_Diagnostics.m_TextureImportCount;
		RHITextureHandle handle =
			AllocateTextureSlot(std::move(texture), RHIResourceOwnership::Borrowed, resolvedIdentity);
		if (handle.IsValid())
		{
			// The authoritative RHI description is preserved so view
			// validation and normalization work for imported resources.
			TextureSlot& slot = m_Textures.SlotAt(handle.Index());
			slot.m_RHIDesc = desc.m_RHI.m_Desc;
		}
		return handle;
	}

	RHIBufferHandle VulkanResourceManager::ImportBuffer(const ImportedBufferDesc& desc) noexcept
	{
		if (desc.m_Buffer == VK_NULL_HANDLE || !IsRHIResourceStateValid(
			desc.m_RHI.m_External.m_InitialState, RHIResourceStateUsage::Buffer))
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::ImportBuffer rejected its buffer or initial state.");
			return {};
		}

		auto buffer = std::make_unique<VulkanBuffer>();
		buffer->AdoptExternal(m_Device->Get(), desc.m_Buffer,
			desc.m_RHI.m_Desc.m_SizeInBytes, desc.m_RHI.m_External.m_InitialState);
		if (!buffer->IsValid())
		{
			++m_Diagnostics.m_ImportFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanResourceManager::ImportBuffer failed to adopt the native buffer.");
			return {};
		}

		const char* debugNameText = desc.m_RHI.m_External.m_DebugName
			? desc.m_RHI.m_External.m_DebugName
			: desc.m_RHI.m_Desc.m_DebugName;
		const std::string_view legacyName = debugNameText ? debugNameText : "";
		const auto resolvedIdentity =
			ResolveDebugIdentity(desc.m_DebugIdentity, legacyName, RHIResourceType::Buffer);
		++m_Diagnostics.m_BufferImportCount;
		RHIBufferHandle handle =
			AllocateBufferSlot(std::move(buffer), RHIResourceOwnership::Borrowed, resolvedIdentity);
		if (handle.IsValid())
		{
			// The authoritative RHI description is preserved so later
			// buffer view validation works for imported resources.
			BufferSlot& slot = m_Buffers.SlotAt(handle.Index());
			slot.m_RHIDesc = desc.m_RHI.m_Desc;
		}
		return handle;
	}

	void VulkanResourceManager::DestroyTexture(RHITextureHandle texture) noexcept
	{
		DestroyResource(m_Textures, texture, "VulkanResourceManager::DestroyTexture",
			[this, texture](TextureSlot& slot) noexcept
			{
				// Every view of this texture enters the same retirement
				// gate; the native image view must outlive the image.
				const auto iterator = m_TextureResourceViews.find(texture);
				if (iterator != m_TextureResourceViews.end())
				{
					for (const RHITextureViewHandle view : iterator->second)
					{
						TextureViewSlot* viewSlot = m_TextureViews.Resolve(view);
						if (viewSlot == nullptr)
						{
							continue;
						}
						m_TextureViewCache.erase(viewSlot->m_Key);
						if (m_TextureViews.BeginRetirement(view) == RHIHandleValidationResult::Valid)
						{
							m_TextureViews.SlotAt(view.Index()).m_RetirementPoints =
								slot.m_RetirementPoints;
						}
					}
					m_TextureResourceViews.erase(iterator);
				}
			});
	}

	void VulkanResourceManager::DestroyBuffer(RHIBufferHandle buffer) noexcept
	{
		DestroyResource(m_Buffers, buffer, "VulkanResourceManager::DestroyBuffer",
			[this, buffer](BufferSlot& slot) noexcept
			{
				const auto iterator = m_BufferResourceViews.find(buffer);
				if (iterator != m_BufferResourceViews.end())
				{
					for (const RHIBufferViewHandle view : iterator->second)
					{
						BufferViewSlot* viewSlot = m_BufferViews.Resolve(view);
						if (viewSlot == nullptr)
						{
							continue;
						}
						m_BufferViewCache.erase(viewSlot->m_Key);
						if (m_BufferViews.BeginRetirement(view) == RHIHandleValidationResult::Valid)
						{
							m_BufferViews.SlotAt(view.Index()).m_RetirementPoints =
								slot.m_RetirementPoints;
						}
					}
					m_BufferResourceViews.erase(iterator);
				}
			});
	}

	void VulkanResourceManager::SetTextureDebugBinding(
		RHITextureHandle texture, const RHIResourceDebugBindingDesc& binding) noexcept
	{
		SetResourceDebugBinding(m_Textures, texture, RHIResourceType::Texture, binding,
			"VulkanResourceManager::SetTextureDebugBinding");
	}

	void VulkanResourceManager::SetBufferDebugBinding(
		RHIBufferHandle buffer, const RHIResourceDebugBindingDesc& binding) noexcept
	{
		SetResourceDebugBinding(m_Buffers, buffer, RHIResourceType::Buffer, binding,
			"VulkanResourceManager::SetBufferDebugBinding");
	}

	std::string_view VulkanResourceManager::GetTextureDebugName(
		RHITextureHandle texture) const noexcept
	{
		return GetResourceDebugName(m_Textures, texture);
	}

	std::string_view VulkanResourceManager::GetBufferDebugName(
		RHIBufferHandle buffer) const noexcept
	{
		return GetResourceDebugName(m_Buffers, buffer);
	}

	void VulkanResourceManager::RecordTextureUse(
		RHITextureHandle texture, const RHIFencePoint& fencePoint) noexcept
	{
		RecordResourceUse(
			m_Textures, texture, fencePoint, "VulkanResourceManager::RecordTextureUse", "texture");
	}

	void VulkanResourceManager::RecordBufferUse(
		RHIBufferHandle buffer, const RHIFencePoint& fencePoint) noexcept
	{
		RecordResourceUse(
			m_Buffers, buffer, fencePoint, "VulkanResourceManager::RecordBufferUse", "buffer");
	}

	bool VulkanResourceManager::IsAlive(RHITextureHandle texture) const noexcept
	{
		const TextureSlot* slot = m_Textures.Resolve(texture);
		return slot != nullptr && slot->m_Resource != nullptr;
	}

	bool VulkanResourceManager::IsAlive(RHIBufferHandle buffer) const noexcept
	{
		const BufferSlot* slot = m_Buffers.Resolve(buffer);
		return slot != nullptr && slot->m_Resource != nullptr;
	}

	void* VulkanResourceManager::MapBuffer(
		RHIBufferHandle buffer, RHIMappedBufferRange readRange) noexcept
	{
		VulkanBuffer* nativeBuffer = ResolveBuffer(buffer);
		if (nativeBuffer == nullptr)
		{
			GGLAB_LOG_GRAPHICS_WARN("VulkanResourceManager::MapBuffer received a non-live handle.");
			return nullptr;
		}
		if (nativeBuffer->IsExternal())
		{
			// Borrowed buffers have no allocation metadata: the backend
			// cannot prove host visibility or flush/invalidate ranges, so
			// backend mapping is not supported for imported buffers.
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::MapBuffer rejected an imported buffer.");
			return nullptr;
		}
		return nativeBuffer->Map(readRange);
	}

	void VulkanResourceManager::UnmapBuffer(
		RHIBufferHandle buffer, RHIMappedBufferRange writtenRange) noexcept
	{
		VulkanBuffer* nativeBuffer = ResolveBuffer(buffer);
		if (nativeBuffer == nullptr)
		{
			GGLAB_LOG_GRAPHICS_WARN("VulkanResourceManager::UnmapBuffer received a non-live handle.");
			return;
		}
		nativeBuffer->Unmap(writtenRange);
	}

	VulkanTexture* VulkanResourceManager::ResolveTexture(RHITextureHandle texture) noexcept
	{
		return const_cast<VulkanTexture*>(std::as_const(*this).ResolveTexture(texture));
	}

	const VulkanTexture* VulkanResourceManager::ResolveTexture(
		RHITextureHandle texture) const noexcept
	{
		const TextureSlot* slot = m_Textures.Resolve(texture);
		if (!slot || !slot->m_Resource)
		{
			return nullptr;
		}
		return slot->m_Resource.get();
	}

	VulkanBuffer* VulkanResourceManager::ResolveBuffer(RHIBufferHandle buffer) noexcept
	{
		return const_cast<VulkanBuffer*>(std::as_const(*this).ResolveBuffer(buffer));
	}

	const VulkanBuffer* VulkanResourceManager::ResolveBuffer(RHIBufferHandle buffer) const noexcept
	{
		const BufferSlot* slot = m_Buffers.Resolve(buffer);
		if (!slot || !slot->m_Resource)
		{
			return nullptr;
		}
		return slot->m_Resource.get();
	}

	RHITextureViewHandle VulkanResourceManager::CreateTextureView(
		RHITextureHandle texture, const RHITextureViewDesc& desc) noexcept
	{
		const TextureSlot* slot = m_Textures.Resolve(texture);
		if (!slot || !slot->m_Resource)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTextureView received a non-live texture handle.");
			return {};
		}
		const RHITextureValidationResult validation =
			ValidateRHITextureViewDesc(slot->m_RHIDesc, desc);
		if (!validation.IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTextureView rejected the view description: {}.",
				RHITextureValidationErrorText(validation.m_Error));
			return {};
		}

		// Normalize once: default semantics (Unknown format, All aspects,
		// Remaining ranges) are expanded and the canonical result drives
		// both the cache key and the native view creation, so validation
		// and creation never diverge.
		const std::optional<VulkanNormalizedTextureView> normalized =
			NormalizeVulkanTextureView(slot->m_RHIDesc, desc);
		if (!normalized)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTextureView rejected an unnormalizable view.");
			return {};
		}
		if (!IsVulkanViewFormatCompatible(slot->m_RHIDesc.m_Format, normalized->m_EffectiveFormat))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTextureView rejected view format {} on resource {}.",
				GetRHIFormatInfo(normalized->m_EffectiveFormat).m_Name,
				GetRHIFormatInfo(slot->m_RHIDesc.m_Format).m_Name);
			return {};
		}

		if (desc.m_ResourceMinLODClamp != 0.0f &&
			!m_Device->GetPortabilityCapabilities().m_ImageViewMinLod)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTextureView rejected image-view min LOD without "
				"VK_EXT_image_view_min_lod.");
			return {};
		}
		if (desc.m_Dimension == RHITextureViewDimension::TextureCubeArray &&
			!m_Device->IsImageCubeArrayEnabled())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTextureView rejected a cube-array view without "
				"the imageCubeArray feature.");
			return {};
		}

		// Canonical cache key: the normalized description is the identity
		// of the view, so equivalent defaulted descriptions (Unknown
		// dimension, Unknown format, Remaining ranges) share one entry.
		RHITextureViewDesc canonicalDesc = desc;
		canonicalDesc.m_Format = normalized->m_EffectiveFormat;
		canonicalDesc.m_Dimension = normalized->m_EffectiveDimension;
		canonicalDesc.m_Subresources = normalized->m_Range;
		const RHITextureViewKey key{ texture, canonicalDesc };
		const auto cached = m_TextureViewCache.find(key);
		if (cached != m_TextureViewCache.end())
		{
			if (m_TextureViews.Resolve(cached->second) != nullptr)
			{
				return cached->second;
			}
			m_TextureViewCache.erase(cached);
		}

		// Only shader-visible views consume the bindless resource index
		// arena; attachment views are used through their native image view.
		std::optional<uint32_t> descriptorIndex = std::nullopt;
		if (desc.m_Type == RHITextureViewType::ShaderResource ||
			desc.m_Type == RHITextureViewType::UnorderedAccess)
		{
			descriptorIndex = m_ResourceDescriptorArena.Allocate();
			if (!descriptorIndex)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"VulkanResourceManager::CreateTextureView exhausted the resource "
					"descriptor arena.");
				return {};
			}
		}

		const std::string debugName = FormatRHIResourceDebugName(RHIResourceType::Texture,
			texture.Index(), texture.Generation(), slot->m_DebugIdentity);
		const VkImageView imageView = CreateNativeImageView(*normalized, *slot->m_Resource, debugName);
		if (imageView == VK_NULL_HANDLE)
		{
			if (descriptorIndex)
			{
				m_ResourceDescriptorArena.Release(*descriptorIndex);
			}
			return {};
		}

		const RHITextureViewHandle view = m_TextureViews.Allocate();
		TextureViewSlot& viewSlot = m_TextureViews.SlotAt(view.Index());
		viewSlot.m_Key = key;
		viewSlot.m_ImageView = imageView;
		viewSlot.m_ParentImage = slot->m_Resource->Get();
		viewSlot.m_DescriptorIndex = descriptorIndex;
		m_TextureViewCache.emplace(key, view);
		m_TextureResourceViews[texture].push_back(view);
		return view;
	}

	RHIBufferViewHandle VulkanResourceManager::CreateBufferView(
		RHIBufferHandle buffer, const RHIBufferViewDesc& desc) noexcept
	{
		const BufferSlot* slot = m_Buffers.Resolve(buffer);
		if (!slot || !slot->m_Resource)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateBufferView received a non-live buffer handle.");
			return {};
		}
		const uint64_t bufferSize = slot->m_Resource->GetSizeInBytes();
		if (desc.m_OffsetInBytes > bufferSize ||
			(desc.m_SizeInBytes != 0 && desc.m_SizeInBytes > bufferSize - desc.m_OffsetInBytes))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateBufferView rejected an out-of-range view.");
			return {};
		}
		const uint64_t resolvedSize = desc.m_SizeInBytes == 0
			? bufferSize - desc.m_OffsetInBytes
			: desc.m_SizeInBytes;
		if (resolvedSize == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateBufferView rejected an empty view.");
			return {};
		}

		const RHIBufferViewKey key{ buffer, desc };
		const auto cached = m_BufferViewCache.find(key);
		if (cached != m_BufferViewCache.end())
		{
			if (m_BufferViews.Resolve(cached->second) != nullptr)
			{
				return cached->second;
			}
			m_BufferViewCache.erase(cached);
		}

		// Buffer views never consume the bindless image arena: the binding
		// ABI revision only covers sampled/storage images, and plain
		// buffer descriptors belong to the fixed set-0 layout.

		VkBufferView nativeView = VK_NULL_HANDLE;
		if (desc.m_Format != RHIFormat::Unknown)
		{
			const VulkanFormatInfo& formatInfo = GetVulkanFormatInfo(desc.m_Format);
			const bool shaderResource = desc.m_Type == RHIBufferViewType::ShaderResource;
			const bool unorderedAccess = desc.m_Type == RHIBufferViewType::UnorderedAccess;
			const bool usageMatches =
				(shaderResource && Test(slot->m_RHIDesc.m_Usage, RHIBufferUsage::Structured)) ||
				(unorderedAccess &&
					Test(slot->m_RHIDesc.m_Usage, RHIBufferUsage::UnorderedAccess));
			if (desc.m_StrideInBytes != 0 || !usageMatches || formatInfo.m_IsTypeless ||
				formatInfo.m_IsDepthStencil || formatInfo.m_ResourceFormat == VK_FORMAT_UNDEFINED)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"VulkanResourceManager::CreateBufferView rejected an invalid typed view "
					"contract.");
				return {};
			}

			VkFormatProperties3 formatProperties3{};
			formatProperties3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
			VkFormatProperties2 formatProperties2{};
			formatProperties2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
			formatProperties2.pNext = &formatProperties3;
			vkGetPhysicalDeviceFormatProperties2(m_Device->GetPhysicalDevice(),
				formatInfo.m_ResourceFormat, &formatProperties2);
			const VkFormatFeatureFlags2 requiredFeature = shaderResource
				? VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT
				: VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT;
			if ((formatProperties3.bufferFeatures & requiredFeature) != requiredFeature)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"VulkanResourceManager::CreateBufferView rejected unsupported texel-buffer "
					"format features.");
				return {};
			}

			VkPhysicalDeviceProperties physicalProperties{};
			vkGetPhysicalDeviceProperties(m_Device->GetPhysicalDevice(), &physicalProperties);
			const uint32_t bytesPerElement = GetRHIFormatInfo(desc.m_Format).m_BytesPerBlock;
			if (bytesPerElement == 0 || desc.m_OffsetInBytes %
				physicalProperties.limits.minTexelBufferOffsetAlignment != 0 ||
				resolvedSize % bytesPerElement != 0 ||
				resolvedSize / bytesPerElement > physicalProperties.limits.maxTexelBufferElements)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"VulkanResourceManager::CreateBufferView rejected a texel-buffer range that "
					"violates native alignment or size limits.");
				return {};
			}

			// Texel views carry a native VkBufferView; plain buffer views
			// are described by the descriptor layer directly.
			VkBufferViewCreateInfo viewCreateInfo{};
			viewCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
			viewCreateInfo.buffer = slot->m_Resource->Get();
			viewCreateInfo.format = formatInfo.m_ResourceFormat;
			viewCreateInfo.offset = static_cast<VkDeviceSize>(desc.m_OffsetInBytes);
			viewCreateInfo.range = static_cast<VkDeviceSize>(resolvedSize);
			const VkResult createResult =
				vkCreateBufferView(m_Device->Get(), &viewCreateInfo, nullptr, &nativeView);
			if (createResult != VK_SUCCESS)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"VulkanResourceManager::CreateBufferView failed to create the native view ({}).",
					ToString(createResult));
				return {};
			}
		}

		const RHIBufferViewHandle view = m_BufferViews.Allocate();
		BufferViewSlot& viewSlot = m_BufferViews.SlotAt(view.Index());
		viewSlot.m_Key = key;
		viewSlot.m_BufferView = nativeView;
		viewSlot.m_ParentBuffer = slot->m_Resource->Get();
		m_BufferViewCache.emplace(key, view);
		m_BufferResourceViews[buffer].push_back(view);
		return view;
	}

	RHISamplerHandle VulkanResourceManager::CreateSampler(const RHISamplerDesc& desc) noexcept
	{
		const auto cached = m_SamplerCache.find(desc);
		if (cached != m_SamplerCache.end())
		{
			if (m_Samplers.Resolve(cached->second) != nullptr)
			{
				return cached->second;
			}
			m_SamplerCache.erase(cached);
		}

		if (!ValidateRHISamplerPortability(desc, m_Device->GetPortabilityCapabilities()).IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateSampler rejected a sampler the Vulkan profile "
				"cannot express.");
			return {};
		}
		if ((desc.m_AddressU == RHITextureAddressMode::MirrorOnce ||
			desc.m_AddressV == RHITextureAddressMode::MirrorOnce ||
			desc.m_AddressW == RHITextureAddressMode::MirrorOnce) &&
			!m_Device->IsSamplerMirrorClampToEdgeEnabled())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateSampler rejected mirror-once addressing without "
				"the samplerMirrorClampToEdge feature.");
			return {};
		}

		const std::optional<uint32_t> descriptorIndex = m_SamplerDescriptorArena.Allocate();
		if (!descriptorIndex)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanResourceManager::CreateSampler exhausted the sampler descriptor arena.");
			return {};
		}

		const VkSampler sampler = CreateNativeSampler(desc);
		if (sampler == VK_NULL_HANDLE)
		{
			m_SamplerDescriptorArena.Release(*descriptorIndex);
			return {};
		}

		const RHISamplerHandle handle = m_Samplers.Allocate();
		SamplerSlot& slot = m_Samplers.SlotAt(handle.Index());
		slot.m_Desc = desc;
		slot.m_Sampler = sampler;
		slot.m_DescriptorIndex = descriptorIndex;
		m_SamplerCache.emplace(desc, handle);
		return handle;
	}

	void VulkanResourceManager::DestroyTextureView(RHITextureViewHandle view) noexcept
	{
		DestroyViewHandle(m_TextureViews, view, "VulkanResourceManager::DestroyTextureView",
			[this, view](TextureViewSlot& slot) noexcept
			{
				const RHITextureHandle texture = slot.m_Key.m_Texture;
				m_TextureViewCache.erase(slot.m_Key);
				if (const TextureSlot* textureSlot = m_Textures.Resolve(texture))
				{
					slot.m_RetirementPoints = textureSlot->m_LastUsePoints;
				}
				if (auto iterator = m_TextureResourceViews.find(texture);
					iterator != m_TextureResourceViews.end())
				{
					std::erase(iterator->second, view);
					if (iterator->second.empty())
					{
						m_TextureResourceViews.erase(iterator);
					}
				}
			});
	}

	void VulkanResourceManager::DestroyBufferView(RHIBufferViewHandle view) noexcept
	{
		DestroyViewHandle(m_BufferViews, view, "VulkanResourceManager::DestroyBufferView",
			[this, view](BufferViewSlot& slot) noexcept
			{
				const RHIBufferHandle buffer = slot.m_Key.m_Buffer;
				m_BufferViewCache.erase(slot.m_Key);
				if (const BufferSlot* bufferSlot = m_Buffers.Resolve(buffer))
				{
					slot.m_RetirementPoints = bufferSlot->m_LastUsePoints;
				}
				if (auto iterator = m_BufferResourceViews.find(buffer);
					iterator != m_BufferResourceViews.end())
				{
					std::erase(iterator->second, view);
					if (iterator->second.empty())
					{
						m_BufferResourceViews.erase(iterator);
					}
				}
			});
	}

	void VulkanResourceManager::DestroySampler(RHISamplerHandle sampler) noexcept
	{
		DestroyViewHandle(m_Samplers, sampler, "VulkanResourceManager::DestroySampler",
			[this, sampler](SamplerSlot& slot) noexcept
			{
				m_SamplerCache.erase(slot.m_Desc);
			});
	}

	bool VulkanResourceManager::IsSamplerAlive(RHISamplerHandle sampler) const noexcept
	{
		const SamplerSlot* slot = m_Samplers.Resolve(sampler);
		return slot != nullptr && slot->m_Sampler != VK_NULL_HANDLE;
	}

	RHIDescriptorHandle VulkanResourceManager::GetTextureViewDescriptor(
		RHITextureViewHandle view) const noexcept
	{
		const TextureViewSlot* slot = m_TextureViews.Resolve(view);
		// Attachment views (RenderTarget/DepthStencil) hold no bindless
		// index and report an invalid descriptor.
		if (slot == nullptr || !slot->m_DescriptorIndex)
		{
			return {};
		}
		return RHIDescriptorHandle{
			.m_HeapType = RHIDescriptorHeapType::CbvSrvUav,
			.m_Index = *slot->m_DescriptorIndex,
		};
	}

	RHIDescriptorHandle VulkanResourceManager::GetBufferViewDescriptor(
		RHIBufferViewHandle view) const noexcept
	{
		// Buffer views never consume the bindless image arena; their
		// descriptors belong to the fixed set-0 layout.
		GGLAB_UNUSED(view);
		return {};
	}

	RHIDescriptorHandle VulkanResourceManager::GetSamplerDescriptor(
		RHISamplerHandle sampler) const noexcept
	{
		const SamplerSlot* slot = m_Samplers.Resolve(sampler);
		if (slot == nullptr || !slot->m_DescriptorIndex)
		{
			return {};
		}
		return RHIDescriptorHandle{
			.m_HeapType = RHIDescriptorHeapType::Sampler,
			.m_Index = *slot->m_DescriptorIndex,
		};
	}

	void VulkanResourceManager::RetireCompletedResources() noexcept
	{
		// Views retire before their parent resources: the native image view
		// must be destroyed before the image it references.
		RetireCompletedViewTables();
		RetireCompletedResourceTable(m_Textures, m_Diagnostics.m_TextureRetireCount);
		RetireCompletedResourceTable(m_Buffers, m_Diagnostics.m_BufferRetireCount);
	}

	VkImageView VulkanResourceManager::CreateNativeImageView(
		const VulkanNormalizedTextureView& normalized, VulkanTexture& nativeTexture,
		std::string_view debugName) noexcept
	{
		// The view narrows the parent image's usage to exactly what the
		// view type needs, so the view format only has to carry the
		// corresponding format features.
		VkImageUsageFlags viewUsage = 0;
		switch (normalized.m_Type)
		{
		case RHITextureViewType::RenderTarget:
			viewUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			break;
		case RHITextureViewType::DepthStencil:
			viewUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			break;
		case RHITextureViewType::ShaderResource:
			viewUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
			break;
		case RHITextureViewType::UnorderedAccess:
			viewUsage = VK_IMAGE_USAGE_STORAGE_BIT;
			break;
		}
		VkImageViewUsageCreateInfo usageCreateInfo{};
		usageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
		usageCreateInfo.usage = viewUsage;

		VkImageViewCreateInfo viewCreateInfo{};
		viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.pNext = &usageCreateInfo;
		viewCreateInfo.image = nativeTexture.Get();
		viewCreateInfo.viewType = normalized.m_ViewType;
		// Depth resources always view through the depth image format; the
		// sampled R32Float interpretation stays on the depth aspect.
		viewCreateInfo.format = normalized.m_NativeFormat;
		viewCreateInfo.components = {
			.r = VK_COMPONENT_SWIZZLE_IDENTITY,
			.g = VK_COMPONENT_SWIZZLE_IDENTITY,
			.b = VK_COMPONENT_SWIZZLE_IDENTITY,
			.a = VK_COMPONENT_SWIZZLE_IDENTITY,
		};
		viewCreateInfo.subresourceRange = {
			.aspectMask = normalized.m_AspectMask,
			.baseMipLevel = normalized.m_Range.m_BaseMip,
			.levelCount = normalized.m_Range.m_MipCount,
			.baseArrayLayer = normalized.m_Range.m_BaseArraySlice,
			.layerCount = normalized.m_Range.m_ArraySliceCount,
		};

		VkImageView imageView = VK_NULL_HANDLE;
		const VkResult createResult =
			vkCreateImageView(m_Device->Get(), &viewCreateInfo, nullptr, &imageView);
		if (createResult != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateTextureView failed to create the native image "
				"view ({}).",
				ToString(createResult));
			return VK_NULL_HANDLE;
		}
		SetVulkanObjectDebugName(m_Device->Get(), VK_OBJECT_TYPE_IMAGE_VIEW,
			reinterpret_cast<uint64_t>(imageView), debugName.data());
		return imageView;
	}

	VkSampler VulkanResourceManager::CreateNativeSampler(const RHISamplerDesc& desc) noexcept
	{
		VkSamplerCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		createInfo.magFilter = ToVkMagFilter(desc.m_Filter);
		createInfo.minFilter = ToVkMinFilter(desc.m_Filter);
		createInfo.mipmapMode = ToVkMipmapMode(desc.m_Filter);
		createInfo.addressModeU = ToVulkanSamplerAddressMode(desc.m_AddressU);
		createInfo.addressModeV = ToVulkanSamplerAddressMode(desc.m_AddressV);
		createInfo.addressModeW = ToVulkanSamplerAddressMode(desc.m_AddressW);
		createInfo.mipLodBias = desc.m_MipLODBias;
		const bool anisotropic = IsAnisotropicSamplerFilter(desc.m_Filter);
		createInfo.anisotropyEnable = anisotropic ? VK_TRUE : VK_FALSE;
		createInfo.maxAnisotropy = anisotropic
			? static_cast<float>(std::max(desc.m_MaxAnisotropy, 1u))
			: 1.0f;
		createInfo.compareEnable = IsComparisonSamplerFilter(desc.m_Filter) ? VK_TRUE : VK_FALSE;
		createInfo.compareOp = ToVkCompareOp(desc.m_CompareOp);
		createInfo.minLod = desc.m_MinLOD;
		createInfo.maxLod = desc.m_MaxLOD;
		createInfo.borderColor = ToVkBorderColor(desc);
		createInfo.unnormalizedCoordinates = VK_FALSE;

		VkSampler sampler = VK_NULL_HANDLE;
		const VkResult createResult =
			vkCreateSampler(m_Device->Get(), &createInfo, nullptr, &sampler);
		if (createResult != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanResourceManager::CreateSampler failed to create the native sampler ({}).",
				ToString(createResult));
			return VK_NULL_HANDLE;
		}
		return sampler;
	}

	RHITextureHandle VulkanResourceManager::AllocateTextureSlot(std::unique_ptr<VulkanTexture> texture,
		RHIResourceOwnership ownership, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		GGLAB_ASSERT_MSG(texture != nullptr, "VulkanResourceManager requires a texture wrapper.");
		return AllocateResourceSlot(
			m_Textures, std::move(texture), ownership, RHIResourceType::Texture, debugIdentity);
	}

	RHIBufferHandle VulkanResourceManager::AllocateBufferSlot(std::unique_ptr<VulkanBuffer> buffer,
		RHIResourceOwnership ownership, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		GGLAB_ASSERT_MSG(buffer != nullptr, "VulkanResourceManager requires a buffer wrapper.");
		return AllocateResourceSlot(
			m_Buffers, std::move(buffer), ownership, RHIResourceType::Buffer, debugIdentity);
	}

	template <typename HandleT, typename SlotT, typename ResourceT>
	HandleT VulkanResourceManager::AllocateResourceSlot(RHIHandleTable<HandleT, SlotT>& table,
		std::unique_ptr<ResourceT> resource, RHIResourceOwnership ownership,
		RHIResourceType resourceType, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		const HandleT handle = table.Allocate();
		SlotT& slot = table.SlotAt(handle.Index());
		slot.m_Ownership = ownership;
		slot.m_DebugIdentity.Assign(debugIdentity);
		slot.m_DebugBinding = {};
		slot.m_DebugBindingHistory.clear();
		slot.m_LastUsePoints.clear();
		slot.m_RetirementPoints.clear();
		slot.m_Resource = std::move(resource);
		slot.m_DebugName = FormatRHIResourceDebugName(
			resourceType, handle.Index(), handle.Generation(), slot.m_DebugIdentity);
		slot.m_Resource->SetDebugName(slot.m_DebugName.c_str());
		return handle;
	}

	template <typename HandleT, typename SlotT>
	void VulkanResourceManager::SetResourceDebugBinding(RHIHandleTable<HandleT, SlotT>& table,
		HandleT handle, RHIResourceType resourceType, const RHIResourceDebugBindingDesc& binding,
		const char* functionName) noexcept
	{
		SlotT* slot = table.Resolve(handle);
		if (!slot || !slot->m_Resource)
		{
			GGLAB_LOG_GRAPHICS_WARN("{} received a non-live resource handle.", functionName);
			return;
		}

		if (!slot->m_DebugBinding.IsEmpty())
		{
			slot->m_DebugBindingHistory.push_back(slot->m_DebugBinding);
			constexpr size_t MaxBindingHistory = 8;
			if (slot->m_DebugBindingHistory.size() > MaxBindingHistory)
			{
				slot->m_DebugBindingHistory.erase(slot->m_DebugBindingHistory.begin());
			}
		}
		slot->m_DebugBinding.Assign(binding);

		if (binding.m_Mode == RHIResourceDebugBindingMode::Exclusive)
		{
			slot->m_DebugName = FormatRHIResourceDebugName(resourceType, handle.Index(),
				handle.Generation(), slot->m_DebugIdentity, &slot->m_DebugBinding);
			slot->m_Resource->SetDebugName(slot->m_DebugName.c_str());
		}
	}

	template <typename HandleT, typename SlotT>
	std::string_view VulkanResourceManager::GetResourceDebugName(
		const RHIHandleTable<HandleT, SlotT>& table, HandleT handle) noexcept
	{
		const SlotT* slot = table.Resolve(handle);
		return slot && slot->m_Resource ? std::string_view(slot->m_DebugName) : std::string_view{};
	}

	template <typename HandleT, typename SlotT, typename OnValidT>
	void VulkanResourceManager::DestroyResource(RHIHandleTable<HandleT, SlotT>& table,
		HandleT handle, const char* functionName, OnValidT onValid) noexcept
	{
		const RHIHandleValidationResult result = table.BeginRetirement(handle);
		switch (result)
		{
		case RHIHandleValidationResult::Valid:
		{
			SlotT& slot = table.SlotAt(handle.Index());
			slot.m_RetirementPoints = slot.m_LastUsePoints;
			slot.m_LastUsePoints.clear();
			onValid(slot);
			return;
		}
		case RHIHandleValidationResult::Invalid:
			++m_Diagnostics.m_InvalidDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} received an invalid handle.", functionName);
			return;
		case RHIHandleValidationResult::DoubleDestroy:
			++m_Diagnostics.m_DoubleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} detected a double destroy.", functionName);
			return;
		case RHIHandleValidationResult::Stale:
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} received a stale handle.", functionName);
			return;
		case RHIHandleValidationResult::NonLive:
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} received a non-live handle.", functionName);
			return;
		}
		GGLAB_UNREACHABLE("Unhandled RHI handle validation result.");
	}

	template <typename HandleT, typename SlotT, typename OnValidT>
	void VulkanResourceManager::DestroyViewHandle(RHIHandleTable<HandleT, SlotT>& table,
		HandleT handle, const char* functionName, OnValidT onValid) noexcept
	{
		const RHIHandleValidationResult result = table.BeginRetirement(handle);
		switch (result)
		{
		case RHIHandleValidationResult::Valid:
		{
			// The callback establishes the retirement gate and removes any
			// cache/parent ownership links before the handle can be reused.
			SlotT& slot = table.SlotAt(handle.Index());
			slot.m_RetirementPoints.clear();
			onValid(slot);
			return;
		}
		case RHIHandleValidationResult::Invalid:
			++m_Diagnostics.m_InvalidDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} received an invalid handle.", functionName);
			return;
		case RHIHandleValidationResult::DoubleDestroy:
			++m_Diagnostics.m_DoubleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} detected a double destroy.", functionName);
			return;
		case RHIHandleValidationResult::Stale:
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} received a stale handle.", functionName);
			return;
		case RHIHandleValidationResult::NonLive:
			++m_Diagnostics.m_StaleDestroyCount;
			GGLAB_LOG_GRAPHICS_WARN("{} received a non-live handle.", functionName);
			return;
		}
		GGLAB_UNREACHABLE("Unhandled RHI handle validation result.");
	}

	template <typename HandleT, typename SlotT>
	void VulkanResourceManager::RecordResourceUse(RHIHandleTable<HandleT, SlotT>& table,
		HandleT handle, const RHIFencePoint& fencePoint, const char* functionName,
		const char* resourceKind) noexcept
	{
		if (!fencePoint.IsValid())
		{
			return;
		}

		SlotT* slot = table.Resolve(handle);
		if (!slot || !slot->m_Resource)
		{
			++m_Diagnostics.m_InvalidUseCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"{} received a non-live {} handle.", functionName, resourceKind);
			return;
		}

		RecordLastUsePoint(slot->m_LastUsePoints, fencePoint);
	}

	template <typename HandleT, typename SlotT>
	void VulkanResourceManager::RetireCompletedResourceTable(
		RHIHandleTable<HandleT, SlotT>& table, uint64_t& retireCount) noexcept
	{
		for (uint32_t index = 0; index < table.Size(); ++index)
		{
			auto& slot = table.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::PendingRetirement)
			{
				continue;
			}

			const bool completed = std::ranges::all_of(slot.m_RetirementPoints,
				[this](const RHIFencePoint& point)
				{
					return m_Device && m_Device->IsFencePointCompleted(point);
				});
			if (!completed)
			{
				continue;
			}

			slot.m_Resource.reset();
			slot.m_LastUsePoints.clear();
			slot.m_RetirementPoints.clear();
			slot.m_DebugIdentity = {};
			slot.m_DebugBinding = {};
			slot.m_DebugBindingHistory.clear();
			slot.m_DebugName.clear();
			slot.m_Ownership = RHIResourceOwnership::Owned;
			table.Retire(index);
			++retireCount;
		}
	}

	void VulkanResourceManager::RetireCompletedViewTables() noexcept
	{
		for (uint32_t index = 0; index < m_TextureViews.Size(); ++index)
		{
			TextureViewSlot& slot = m_TextureViews.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::PendingRetirement)
			{
				continue;
			}
			const bool completed = std::ranges::all_of(slot.m_RetirementPoints,
				[this](const RHIFencePoint& point)
				{
					return m_Device && m_Device->IsFencePointCompleted(point);
				});
			if (!completed)
			{
				continue;
			}
			if (slot.m_ImageView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(m_Device->Get(), slot.m_ImageView, nullptr);
			}
			if (slot.m_DescriptorIndex)
			{
				m_ResourceDescriptorArena.Release(*slot.m_DescriptorIndex);
			}
			slot.m_Key = {};
			slot.m_ImageView = VK_NULL_HANDLE;
			slot.m_ParentImage = VK_NULL_HANDLE;
			slot.m_DescriptorIndex.reset();
			slot.m_RetirementPoints.clear();
			m_TextureViews.Retire(index);
		}

		for (uint32_t index = 0; index < m_BufferViews.Size(); ++index)
		{
			BufferViewSlot& slot = m_BufferViews.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::PendingRetirement)
			{
				continue;
			}
			const bool completed = std::ranges::all_of(slot.m_RetirementPoints,
				[this](const RHIFencePoint& point)
				{
					return m_Device && m_Device->IsFencePointCompleted(point);
				});
			if (!completed)
			{
				continue;
			}
			if (slot.m_BufferView != VK_NULL_HANDLE)
			{
				vkDestroyBufferView(m_Device->Get(), slot.m_BufferView, nullptr);
			}
			if (slot.m_DescriptorIndex)
			{
				m_ResourceDescriptorArena.Release(*slot.m_DescriptorIndex);
			}
			slot.m_Key = {};
			slot.m_BufferView = VK_NULL_HANDLE;
			slot.m_ParentBuffer = VK_NULL_HANDLE;
			slot.m_DescriptorIndex.reset();
			slot.m_RetirementPoints.clear();
			m_BufferViews.Retire(index);
		}

		for (uint32_t index = 0; index < m_Samplers.Size(); ++index)
		{
			SamplerSlot& slot = m_Samplers.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::PendingRetirement)
			{
				continue;
			}
			const bool completed = std::ranges::all_of(slot.m_RetirementPoints,
				[this](const RHIFencePoint& point)
				{
					return m_Device && m_Device->IsFencePointCompleted(point);
				});
			if (!completed)
			{
				continue;
			}
			if (slot.m_Sampler != VK_NULL_HANDLE)
			{
				vkDestroySampler(m_Device->Get(), slot.m_Sampler, nullptr);
			}
			if (slot.m_DescriptorIndex)
			{
				m_SamplerDescriptorArena.Release(*slot.m_DescriptorIndex);
			}
			slot.m_Desc = {};
			slot.m_Sampler = VK_NULL_HANDLE;
			slot.m_DescriptorIndex.reset();
			slot.m_RetirementPoints.clear();
			m_Samplers.Retire(index);
		}
	}

	void VulkanResourceManager::RecordLastUsePoint(
		std::vector<RHIFencePoint>& points, const RHIFencePoint& fencePoint) noexcept
	{
		if (!fencePoint.IsValid())
		{
			return;
		}

		for (RHIFencePoint& point : points)
		{
			if (point.m_Fence == fencePoint.m_Fence)
			{
				if (point.m_Value < fencePoint.m_Value)
				{
					point = fencePoint;
				}
				return;
			}
		}

		points.push_back(fencePoint);
	}

	void VulkanResourceManager::ReportLiveResources() const noexcept
	{
		for (uint32_t index = 0; index < m_Textures.Size(); ++index)
		{
			const auto& slot = m_Textures.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::Free)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"VulkanResourceManager finalizing texture slot {} ('{}') in state {}.",
					index, slot.m_DebugName, static_cast<uint32_t>(slot.m_State));
			}
		}
		for (uint32_t index = 0; index < m_Buffers.Size(); ++index)
		{
			const auto& slot = m_Buffers.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::Free)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"VulkanResourceManager finalizing buffer slot {} ('{}') in state {}.",
					index, slot.m_DebugName, static_cast<uint32_t>(slot.m_State));
			}
		}
		for (uint32_t index = 0; index < m_TextureViews.Size(); ++index)
		{
			const auto& slot = m_TextureViews.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::Free)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"VulkanResourceManager finalizing texture view slot {} (texture {}) in "
					"state {}.",
					index, slot.m_Key.m_Texture.Index(),
					static_cast<uint32_t>(slot.m_State));
			}
		}
		for (uint32_t index = 0; index < m_BufferViews.Size(); ++index)
		{
			const auto& slot = m_BufferViews.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::Free)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"VulkanResourceManager finalizing buffer view slot {} (buffer {}) in "
					"state {}.",
					index, slot.m_Key.m_Buffer.Index(),
					static_cast<uint32_t>(slot.m_State));
			}
		}
		for (uint32_t index = 0; index < m_Samplers.Size(); ++index)
		{
			const auto& slot = m_Samplers.SlotAt(index);
			if (slot.m_State != RHIHandleSlotState::Free)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"VulkanResourceManager finalizing sampler slot {} in state {}.",
					index, static_cast<uint32_t>(slot.m_State));
			}
		}
	}
}
