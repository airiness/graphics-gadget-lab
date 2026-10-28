#include "Graphics/RHI/DX12/DX12AccelerationStructureManager.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

#include <cstring>

namespace gglab
{
	bool DX12Device::IsAlive(RHIAccelerationStructureHandle handle) const noexcept
	{
		return m_AccelerationStructures && m_AccelerationStructures->IsAlive(handle);
	}

	RHIAllocationFacts DX12Device::QueryAccelerationStructureAllocationFacts(
		RHIAccelerationStructureHandle handle) const noexcept
	{
		return m_AccelerationStructures ? m_AccelerationStructures->QueryAllocationFacts(handle) : RHIAllocationFacts{};
	}

	RHIAllocationFacts DX12AccelerationStructureManager::QueryAllocationFacts(
		RHIAccelerationStructureHandle handle) const noexcept
	{
		const auto resource = m_Store.Resolve(handle);
		return resource ? m_Device.QueryBufferAllocationFacts(resource->m_Storage.Get()) : RHIAllocationFacts{};
	}

	DX12AccelerationStructureManager& DX12Device::GetAccelerationStructureManager() noexcept
	{
		if (!m_AccelerationStructures)
		{
			m_AccelerationStructures = std::make_unique<DX12AccelerationStructureManager>(*this);
		}
		return *m_AccelerationStructures;
	}

	std::optional<RHIASBuildSizes> DX12Device::QueryAccelerationStructureBuildSizes(
		const RHIASBuildInputs& inputs) noexcept
	{
		return GetAccelerationStructureManager().Query(inputs);
	}

	RHIAccelerationStructureHandle DX12Device::CreateAccelerationStructure(
		const RHIAccelerationStructureDesc& desc) noexcept
	{
		return GetAccelerationStructureManager().Create(desc);
	}

	void DX12Device::DestroyAccelerationStructure(RHIAccelerationStructureHandle handle) noexcept
	{
		GetAccelerationStructureManager().Destroy(handle);
	}
	bool DX12AccelerationStructureManager::Describe(const RHIASBuildInputs& inputs,
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& native,
		D3D12_RAYTRACING_GEOMETRY_DESC& geometry) noexcept
	{
		const auto capabilities = m_Device.GetRayQueryCapabilities();
		if (!capabilities.m_Enabled || !ValidateRHIASBuildPreference(inputs.m_Preference).IsValid())
		{
			return false;
		}
		native = {};
		native.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		native.Flags = inputs.m_Preference == RHIASBuildPreference::FastTrace ?
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE :
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
		if (const auto* triangles = std::get_if<RHIASIndexedTriangles>(&inputs.m_Input))
		{
			const auto* vertices = m_Device.GetResourceManager()->ResolveBufferDesc(triangles->m_Vertices.m_Buffer);
			const auto* indices = m_Device.GetResourceManager()->ResolveBufferDesc(triangles->m_Indices.m_Buffer);
			if (!vertices || !indices || !Test(vertices->m_Usage, RHIBufferUsage::AccelerationStructureInput) ||
				!Test(indices->m_Usage, RHIBufferUsage::AccelerationStructureInput) ||
				!ValidateRHIASIndexedTriangles(*triangles, *vertices, *indices, capabilities.m_BuildLimits).IsValid())
			{
				return false;
			}
			geometry = {};
			geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
			geometry.Triangles.VertexCount = triangles->m_VertexCount;
			geometry.Triangles.IndexCount = triangles->m_IndexCount;
			geometry.Triangles.VertexBuffer = {
				m_Device.ResolveBuffer(triangles->m_Vertices.m_Buffer)->GPUVirtualAddress() + triangles->m_Vertices.m_OffsetInBytes,
				triangles->m_VertexStrideInBytes };
			geometry.Triangles.IndexBuffer =
				m_Device.ResolveBuffer(triangles->m_Indices.m_Buffer)->GPUVirtualAddress() + triangles->m_Indices.m_OffsetInBytes;
			native.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
			native.NumDescs = 1;
			native.pGeometryDescs = &geometry;
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
			native.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			native.NumDescs = static_cast<UINT>(instances.size());
		}
		return true;
	}

	std::optional<RHIASBuildSizes> DX12AccelerationStructureManager::Query(const RHIASBuildInputs& inputs) noexcept
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS native{};
		D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
		if (!Describe(inputs, native, geometry))
		{
			return std::nullopt;
		}
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes{};
		m_Device.Get()->GetRaytracingAccelerationStructurePrebuildInfo(&native, &sizes);
		if (sizes.ResultDataMaxSizeInBytes == 0 || sizes.ScratchDataSizeInBytes == 0)
		{
			return std::nullopt;
		}
		return RHIASBuildSizes{ sizes.ResultDataMaxSizeInBytes, sizes.ScratchDataSizeInBytes };
	}

	RHIAccelerationStructureHandle DX12AccelerationStructureManager::Create(
		const RHIAccelerationStructureDesc& desc) noexcept
	{
		if (!m_Device.GetRayQueryCapabilities().m_Enabled || desc.m_SizeInBytes == 0 ||
			desc.m_SizeInBytes % D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT != 0 ||
			(desc.m_Type != RHIAccelerationStructureType::BottomLevel && desc.m_Type != RHIAccelerationStructureType::TopLevel))
		{
			return {};
		}
		auto resource = std::make_shared<RHIAccelerationStructureBackingBase>();
		resource->m_Storage = RHIBufferOwner(&m_Device, m_Device.CreateBuffer({
			.m_SizeInBytes = desc.m_SizeInBytes,
			.m_Usage = RHIBufferUsage::AccelerationStructureStorage,
			.m_DebugName = desc.m_DebugName }));
		if (!resource->m_Storage)
		{
			return {};
		}
		resource->m_Type = desc.m_Type;
		resource->m_SizeInBytes = desc.m_SizeInBytes;
		return m_Store.Insert(std::move(resource));
	}

	void DX12AccelerationStructureManager::Destroy(RHIAccelerationStructureHandle handle) noexcept
	{
		if (!m_Store.Destroy(handle))
		{
			GGLAB_LOG_GRAPHICS_ERROR("DX12 rejected destruction of a stale or invalid AS handle.");
		}
	}

	ID3D12Resource* DX12AccelerationStructureManager::ResolveResource(RHICommandContext& context,
		RHIAccelerationStructureHandle handle) noexcept
	{
		const auto resource = m_Store.Resolve(handle);
		if (!resource || !context.RetainForSubmission(resource))
		{
			context.RejectRecording();
			return nullptr;
		}
		return m_Device.ResolveBuffer(resource->m_Storage.Get())->Get();
	}

	D3D12_GPU_VIRTUAL_ADDRESS DX12AccelerationStructureManager::Bind(RHICommandContext& context,
		RHIAccelerationStructureHandle handle) noexcept
	{
		const auto resource = m_Store.Resolve(handle);
		if (!resource || resource->m_Type != RHIAccelerationStructureType::TopLevel)
		{
			context.RejectRecording();
			return 0;
		}
		auto* native = ResolveResource(context, handle);
		return native ? native->GetGPUVirtualAddress() : 0;
	}

	bool DX12AccelerationStructureManager::Build(RHICommandContext& context,
		ID3D12GraphicsCommandList& commands, RHIAccelerationStructureHandle destination,
		const RHIASBuildInputs& inputs, const RHIASBufferRange& scratch) noexcept
	{
		const auto resource = m_Store.Resolve(destination);
		const auto sizes = Query(inputs);
		const auto* scratchDesc = m_Device.GetResourceManager()->ResolveBufferDesc(scratch.m_Buffer);
		if (!resource || resource->m_Type != inputs.GetType() || !sizes ||
			resource->m_SizeInBytes < sizes->m_StorageSizeInBytes || !scratchDesc ||
			!Test(scratchDesc->m_Usage, RHIBufferUsage::AccelerationStructureScratch) ||
			!ValidateRHIASScratch(scratch, *scratchDesc, *sizes, m_Device.GetRayQueryCapabilities().m_BuildLimits).IsValid())
		{
			context.RejectRecording();
			return false;
		}
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
		D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
		if (!Describe(inputs, build.Inputs, geometry) || !context.RetainForSubmission(resource))
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
			std::vector<D3D12_RAYTRACING_INSTANCE_DESC> native(instances.size());
			static_assert(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) == 64);
			for (size_t index = 0; index < instances.size(); ++index)
			{
				const auto& instance = instances[index];
				const auto bottom = m_Store.Resolve(instance.m_BottomLevel);
				bottoms.push_back(bottom);
				auto& encoded = native[index];
				std::memcpy(encoded.Transform, instance.m_Transform.data(), sizeof(encoded.Transform));
				encoded.InstanceID = instance.m_InstanceIndex;
				encoded.InstanceMask = instance.m_Mask;
				encoded.Flags = Test(instance.m_Flags, RHIASInstanceFlags::TriangleCullDisable) ?
					D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE : D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
				if (Test(instance.m_Flags, RHIASInstanceFlags::TriangleFrontCounterClockwise))
				{
					encoded.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
				}
				encoded.AccelerationStructure = m_Device.ResolveBuffer(bottom->m_Storage.Get())->GPUVirtualAddress();
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
			m_Device.UnmapBuffer(upload->Get(), { 0, native.size() * sizeof(native[0]) });
			if (!context.RetainForSubmission(upload))
			{
				return false;
			}
			build.Inputs.InstanceDescs = m_Device.ResolveBuffer(upload->Get())->GPUVirtualAddress();
			resource->m_BottomLevels = std::move(bottoms);
		}
		build.DestAccelerationStructureData = m_Device.ResolveBuffer(resource->m_Storage.Get())->GPUVirtualAddress();
		build.ScratchAccelerationStructureData = m_Device.ResolveBuffer(scratch.m_Buffer)->GPUVirtualAddress() + scratch.m_OffsetInBytes;
		ComPtr<ID3D12GraphicsCommandList4> rayCommands;
		if (FAILED(commands.QueryInterface(IID_PPV_ARGS(&rayCommands))))
		{
			context.RejectRecording();
			return false;
		}
		context.TrackBufferUse(scratch.m_Buffer);
		rayCommands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
		return true;
	}
}
