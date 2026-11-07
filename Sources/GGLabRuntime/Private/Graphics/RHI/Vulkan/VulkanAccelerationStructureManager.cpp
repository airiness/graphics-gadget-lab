#include "Graphics/RHI/Vulkan/VulkanAccelerationStructureManager.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

#include <cstring>

namespace gglab
{
	bool VulkanDevice::IsAlive(RHIAccelerationStructureHandle handle) const noexcept
	{
		return m_AccelerationStructures && m_AccelerationStructures->IsAlive(handle);
	}

	RHIAllocationFacts VulkanDevice::QueryAccelerationStructureAllocationFacts(
		RHIAccelerationStructureHandle handle) const noexcept
	{
		return m_AccelerationStructures ? m_AccelerationStructures->QueryAllocationFacts(handle) : RHIAllocationFacts{};
	}

	RHIAllocationFacts VulkanAccelerationStructureManager::QueryAllocationFacts(
		RHIAccelerationStructureHandle handle) const noexcept
	{
		const auto resource = m_Store.Resolve(handle);
		return resource ? m_Device.QueryBufferAllocationFacts(resource->m_Storage.Get()) : RHIAllocationFacts{};
	}

	VulkanAccelerationStructureManager& VulkanDevice::GetAccelerationStructureManager() noexcept
	{
		if (!m_AccelerationStructures)
		{
			m_AccelerationStructures = std::make_unique<VulkanAccelerationStructureManager>(*this);
		}
		return *m_AccelerationStructures;
	}

	std::optional<RHIASBuildSizes> VulkanDevice::QueryAccelerationStructureBuildSizes(
		const RHIASBuildInputs& inputs) noexcept
	{
		if (!RequireOwnerThread("QueryAccelerationStructureBuildSizes"))
		{
			return std::nullopt;
		}
		return GetAccelerationStructureManager().Query(inputs);
	}

	RHIAccelerationStructureHandle VulkanDevice::CreateAccelerationStructure(
		const RHIAccelerationStructureDesc& desc) noexcept
	{
		if (!RequireOwnerThread("CreateAccelerationStructure"))
		{
			return {};
		}
		return GetAccelerationStructureManager().Create(desc);
	}

	void VulkanDevice::DestroyAccelerationStructure(RHIAccelerationStructureHandle handle) noexcept
	{
		if (!RequireOwnerThread("DestroyAccelerationStructure"))
		{
			return;
		}
		GetAccelerationStructureManager().Destroy(handle);
	}
	VulkanAccelerationStructureBacking::~VulkanAccelerationStructureBacking()
	{
		if (m_Handle)
		{
			m_Destroy(m_Device, m_Handle, nullptr);
		}
	}

	VkDeviceAddress VulkanAccelerationStructureManager::BufferAddress(RHIBufferHandle handle) noexcept
	{
		const auto* buffer = m_Device.GetResourceManager().ResolveBuffer(handle);
		if (!buffer)
		{
			return 0;
		}
		const VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, buffer->Get() };
		return vkGetBufferDeviceAddress(m_Device.Get(), &info);
	}

	bool VulkanAccelerationStructureManager::Describe(const RHIASBuildInputs& inputs,
		VkAccelerationStructureBuildGeometryInfoKHR& native,
		VkAccelerationStructureGeometryKHR& geometry, uint32_t& primitiveCount) noexcept
	{
		const auto capabilities = m_Device.GetRayQueryCapabilities();
		if (!capabilities.m_Enabled || !ValidateRHIASBuildPreference(inputs.m_Preference).IsValid())
		{
			return false;
		}
		native = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		native.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		native.flags = inputs.m_Preference == RHIASBuildPreference::FastTrace ?
			VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR :
			VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
		native.geometryCount = 1;
		native.pGeometries = &geometry;
		geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		if (const auto* triangles = std::get_if<RHIASIndexedTriangles>(&inputs.m_Input))
		{
			const auto* vertices = m_Device.GetResourceManager().ResolveBufferDesc(triangles->m_Vertices.m_Buffer);
			const auto* indices = m_Device.GetResourceManager().ResolveBufferDesc(triangles->m_Indices.m_Buffer);
			if (!vertices || !indices || !Test(vertices->m_Usage, RHIBufferUsage::AccelerationStructureInput) ||
				!Test(indices->m_Usage, RHIBufferUsage::AccelerationStructureInput) ||
				!ValidateRHIASIndexedTriangles(*triangles, *vertices, *indices, capabilities.m_BuildLimits).IsValid())
			{
				return false;
			}
			native.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			auto& data = geometry.geometry.triangles;
			data = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
			data.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			data.vertexData.deviceAddress = BufferAddress(triangles->m_Vertices.m_Buffer) + triangles->m_Vertices.m_OffsetInBytes;
			data.vertexStride = triangles->m_VertexStrideInBytes;
			data.maxVertex = triangles->m_VertexCount - 1;
			data.indexType = VK_INDEX_TYPE_UINT32;
			data.indexData.deviceAddress = BufferAddress(triangles->m_Indices.m_Buffer) + triangles->m_Indices.m_OffsetInBytes;
			primitiveCount = triangles->m_IndexCount / 3;
		}
		else
		{
			const auto instances = std::get<std::span<const RHIASInstanceDesc>>(inputs.m_Input);
			if (!ValidateRHIASInstances(instances, capabilities.m_BuildLimits).IsValid())
			{
				return false;
			}
			for (const auto& instance : instances)
			{
				const auto bottom = m_Store.Resolve(instance.m_BottomLevel);
				if (!bottom || bottom->m_Type != RHIAccelerationStructureType::BottomLevel)
				{
					return false;
				}
			}
			native.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
			geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
			geometry.geometry.instances = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
			primitiveCount = static_cast<uint32_t>(instances.size());
		}
		return true;
	}

	std::optional<RHIASBuildSizes> VulkanAccelerationStructureManager::Query(const RHIASBuildInputs& inputs) noexcept
	{
		VkAccelerationStructureBuildGeometryInfoKHR native{};
		VkAccelerationStructureGeometryKHR geometry{};
		uint32_t count = 0;
		if (!Describe(inputs, native, geometry, count))
		{
			return std::nullopt;
		}
		VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		const auto query = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
			vkGetDeviceProcAddr(m_Device.Get(), "vkGetAccelerationStructureBuildSizesKHR"));
		query(m_Device.Get(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &native, &count, &sizes);
		if (sizes.accelerationStructureSize == 0 || sizes.buildScratchSize == 0)
		{
			return std::nullopt;
		}
		return RHIASBuildSizes{ sizes.accelerationStructureSize, sizes.buildScratchSize };
	}

	RHIAccelerationStructureHandle VulkanAccelerationStructureManager::Create(
		const RHIAccelerationStructureDesc& desc) noexcept
	{
		if (!m_Device.GetRayQueryCapabilities().m_Enabled || desc.m_SizeInBytes == 0 ||
			(desc.m_Type != RHIAccelerationStructureType::BottomLevel && desc.m_Type != RHIAccelerationStructureType::TopLevel))
		{
			return {};
		}
		auto resource = std::make_shared<VulkanAccelerationStructureBacking>();
		resource->m_Device = m_Device.Get();
		resource->m_Destroy = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
			vkGetDeviceProcAddr(m_Device.Get(), "vkDestroyAccelerationStructureKHR"));
		resource->m_Storage = RHIBufferOwner(&m_Device, m_Device.CreateBuffer({
			.m_SizeInBytes = desc.m_SizeInBytes,
			.m_Usage = RHIBufferUsage::AccelerationStructureStorage,
			.m_DebugName = desc.m_DebugName }));
		if (!resource->m_Storage)
		{
			return {};
		}
		VkAccelerationStructureCreateInfoKHR info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		info.buffer = m_Device.GetResourceManager().ResolveBuffer(resource->m_Storage.Get())->Get();
		info.size = desc.m_SizeInBytes;
		info.type = desc.m_Type == RHIAccelerationStructureType::BottomLevel ?
			VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		const auto create = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
			vkGetDeviceProcAddr(m_Device.Get(), "vkCreateAccelerationStructureKHR"));
		if (create(m_Device.Get(), &info, nullptr, &resource->m_Handle) != VK_SUCCESS)
		{
			return {};
		}
		const VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
			VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr, resource->m_Handle };
		const auto address = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
			vkGetDeviceProcAddr(m_Device.Get(), "vkGetAccelerationStructureDeviceAddressKHR"));
		resource->m_Address = address(m_Device.Get(), &addressInfo);
		resource->m_Type = desc.m_Type;
		resource->m_SizeInBytes = desc.m_SizeInBytes;
		return resource->m_Address ? m_Store.Insert(std::move(resource)) : RHIAccelerationStructureHandle{};
	}

	void VulkanAccelerationStructureManager::Destroy(RHIAccelerationStructureHandle handle) noexcept
	{
		if (!m_Store.Destroy(handle))
		{
			GGLAB_LOG_GRAPHICS_ERROR("Vulkan rejected destruction of a stale or invalid AS handle.");
		}
	}

	std::shared_ptr<VulkanAccelerationStructureBacking> VulkanAccelerationStructureManager::Resolve(
		RHICommandContext& context, RHIAccelerationStructureHandle handle) noexcept
	{
		auto resource = std::static_pointer_cast<VulkanAccelerationStructureBacking>(m_Store.Resolve(handle));
		if (!resource || !context.RetainForSubmission(resource))
		{
			context.RejectRecording();
			return nullptr;
		}
		return resource;
	}

	VkAccelerationStructureKHR VulkanAccelerationStructureManager::Bind(RHICommandContext& context,
		RHIAccelerationStructureHandle handle) noexcept
	{
		const auto resource = Resolve(context, handle);
		if (!resource || resource->m_Type != RHIAccelerationStructureType::TopLevel)
		{
			context.RejectRecording();
			return VK_NULL_HANDLE;
		}
		return resource->m_Handle;
	}

	bool VulkanAccelerationStructureManager::Build(RHICommandContext& context, VkCommandBuffer commands,
		RHIAccelerationStructureHandle destination, const RHIASBuildInputs& inputs,
		const RHIASBufferRange& scratch) noexcept
	{
		const auto resource = Resolve(context, destination);
		const auto sizes = Query(inputs);
		const auto* scratchDesc = m_Device.GetResourceManager().ResolveBufferDesc(scratch.m_Buffer);
		const auto alignment = m_Device.GetRayQueryCapabilities().m_BuildLimits.m_ScratchAlignmentInBytes;
		if (!commands || !resource || resource->m_Type != inputs.GetType() || !sizes ||
			resource->m_SizeInBytes < sizes->m_StorageSizeInBytes || !scratchDesc ||
			!Test(scratchDesc->m_Usage, RHIBufferUsage::AccelerationStructureScratch) ||
			!ValidateRHIASScratch(scratch, *scratchDesc, *sizes, m_Device.GetRayQueryCapabilities().m_BuildLimits).IsValid() ||
			(BufferAddress(scratch.m_Buffer) + scratch.m_OffsetInBytes) % alignment != 0)
		{
			context.RejectRecording();
			return false;
		}
		VkAccelerationStructureBuildGeometryInfoKHR build{};
		VkAccelerationStructureGeometryKHR geometry{};
		uint32_t count = 0;
		if (!Describe(inputs, build, geometry, count))
		{
			context.RejectRecording();
			return false;
		}
		if (const auto* triangles = std::get_if<RHIASIndexedTriangles>(&inputs.m_Input))
		{
			context.TrackBufferUse(triangles->m_Vertices.m_Buffer);
			context.TrackBufferUse(triangles->m_Indices.m_Buffer);
		}
		else
		{
			const auto instances = std::get<std::span<const RHIASInstanceDesc>>(inputs.m_Input);
			std::vector<std::shared_ptr<RHIAccelerationStructureBackingBase>> bottoms;
			std::vector<VkAccelerationStructureInstanceKHR> native(instances.size());
			static_assert(sizeof(VkAccelerationStructureInstanceKHR) == 64);
			for (size_t index = 0; index < instances.size(); ++index)
			{
				const auto& instance = instances[index];
				const auto bottom = std::static_pointer_cast<VulkanAccelerationStructureBacking>(m_Store.Resolve(instance.m_BottomLevel));
				bottoms.push_back(bottom);
				auto& encoded = native[index];
				std::memcpy(encoded.transform.matrix, instance.m_Transform.data(), sizeof(encoded.transform.matrix));
				encoded.instanceCustomIndex = instance.m_InstanceIndex;
				encoded.mask = instance.m_Mask;
				encoded.flags = Test(instance.m_Flags, RHIASInstanceFlags::TriangleCullDisable) ?
					VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR : 0;
				if (Test(instance.m_Flags, RHIASInstanceFlags::TriangleFrontCounterClockwise))
				{
					encoded.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
				}
				encoded.accelerationStructureReference = bottom->m_Address;
			}
			if (!resource->m_BottomLevels.empty() && resource->m_BottomLevels != bottoms)
			{
				context.RejectRecording();
				return false;
			}
			auto upload = std::make_shared<RHIBufferOwner>(&m_Device, m_Device.CreateBuffer({
				.m_SizeInBytes = native.size() * sizeof(native[0]),
				.m_Usage = RHIBufferUsage::AccelerationStructureInput,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
				.m_DebugName = "AS.InstanceUpload" }));
			void* mapped = *upload ? m_Device.MapBuffer(upload->Get(), {}) : nullptr;
			if (!mapped)
			{
				context.RejectRecording();
				return false;
			}
			std::memcpy(mapped, native.data(), native.size() * sizeof(native[0]));
			if (!m_Device.FlushMappedBufferWrites(upload->Get(), { 0, native.size() * sizeof(native[0]) }) ||
				!context.RetainForSubmission(upload))
			{
				context.RejectRecording();
				return false;
			}
			geometry.geometry.instances.data.deviceAddress = BufferAddress(upload->Get());
			resource->m_BottomLevels = std::move(bottoms);
		}
		build.dstAccelerationStructure = resource->m_Handle;
		build.scratchData.deviceAddress = BufferAddress(scratch.m_Buffer) + scratch.m_OffsetInBytes;
		const VkAccelerationStructureBuildRangeInfoKHR range{ count, 0, 0, 0 };
		const auto* rangePointer = &range;
		const auto encode = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
			vkGetDeviceProcAddr(m_Device.Get(), "vkCmdBuildAccelerationStructuresKHR"));
		context.TrackBufferUse(scratch.m_Buffer);
		encode(commands, 1, &build, &rangePointer);
		return true;
	}
}
