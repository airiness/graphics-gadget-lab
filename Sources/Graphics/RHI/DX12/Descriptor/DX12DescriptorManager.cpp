#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorTypes.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	DX12DescriptorManager::DX12DescriptorManager(const CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device)
	{
		GGLAB_ASSERT_NOT_NULL(m_DX12Device);

		// Create Descriptor Heaps
		{
			DX12DescriptorHeap::CreateInfo heapCreateInfo{};
			heapCreateInfo.m_DX12Device = m_DX12Device;

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_CbvSrvUavCount;
			m_Heaps[static_cast<uint8_t>(HeapType::CbvSrvUav)] = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_RtvCount;
			m_Heaps[static_cast<uint8_t>(HeapType::Rtv)] = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_DsvCount;
			m_Heaps[static_cast<uint8_t>(HeapType::Dsv)] = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_SamplerCount;
			m_Heaps[static_cast<uint8_t>(HeapType::Sampler)] = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);
		}

		// Create Descriptor Allocators
		{
			const uint32_t totalSrvCount = createInfo.m_CbvSrvUavCount;
			const uint32_t developGuiSrvCount = createInfo.m_DevelopGuiSrvCount;
			const uint32_t bindlessSrvCount = createInfo.m_BindlessSrvCount;

			GGLAB_ASSERT_MSG(developGuiSrvCount + bindlessSrvCount < totalSrvCount,
				"Srv range overflow.");

			// DevelopGui Srv
			DX12DescriptorAllocatorBase::CreateInfo allocatorCreateInfo{};
			allocatorCreateInfo.m_DescriptorHeap = m_Heaps[static_cast<uint8_t>(HeapType::CbvSrvUav)].get();
			allocatorCreateInfo.m_Range = { 0, developGuiSrvCount };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::DevelopGuiSrv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// Bindless Srv
			allocatorCreateInfo.m_Range = { developGuiSrvCount, bindlessSrvCount };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::BindlessSrv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Srv
			allocatorCreateInfo.m_Range = { developGuiSrvCount + bindlessSrvCount,
				createInfo.m_CbvSrvUavCount - (developGuiSrvCount + bindlessSrvCount) };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::GeneralCbvSrvUav)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Rtv
			allocatorCreateInfo.m_DescriptorHeap = m_Heaps[static_cast<uint8_t>(HeapType::Rtv)].get();
			allocatorCreateInfo.m_Range = { 0, createInfo.m_RtvCount };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::GeneralRtv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Dsv
			allocatorCreateInfo.m_DescriptorHeap = m_Heaps[static_cast<uint8_t>(HeapType::Dsv)].get();
			allocatorCreateInfo.m_Range = { 0, createInfo.m_DsvCount };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::GeneralDsv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Sampler
			allocatorCreateInfo.m_DescriptorHeap = m_Heaps[static_cast<uint8_t>(HeapType::Sampler)].get();
			allocatorCreateInfo.m_Range = { 0, createInfo.m_SamplerCount };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::GeneralSampler)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);
		}
	}

	DX12DescriptorManager::~DX12DescriptorManager() = default;

	void DX12DescriptorManager::Tick() noexcept
	{
		for (auto& allocator : m_FreeListAllocators)
		{
			if (allocator)
			{
				allocator->Tick();
			}
		}
	}

	void DX12DescriptorManager::EndFrame(const DX12FencePoint& fencePoint) noexcept
	{
		for (auto& allocator : m_FreeListAllocators)
		{
			if (allocator)
			{
				allocator->EndFrame(fencePoint);
			}
		}
	}

	DX12DescriptorHeap* DX12DescriptorManager::GetHeap(HeapType heapType) const noexcept
	{
		return m_Heaps[utils::ToIndexChecked(heapType)].get();
	}

	DX12DescriptorFreeListAllocator* DX12DescriptorManager::GetFreeListAllocator(AllocatorType allocatorType) const noexcept
	{
		return m_FreeListAllocators[utils::ToIndexChecked(allocatorType)].get();
	}

	DX12DescriptorID DX12DescriptorManager::AllocateBindlessSrvId() noexcept
	{
		return AllocateDescriptorId(AllocatorType::BindlessSrv);
	}

	DX12DescriptorID DX12DescriptorManager::CreateBindlessSrv(DX12Texture* texture, const TextureSrvCreateInfo& info) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(texture);
		GGLAB_ASSERT_NOT_NULL(texture->Get());

		const auto id = AllocateBindlessSrvId();
		GGLAB_ASSERT(id.IsValid());
		WriteBindlessSrv(id, texture, info);

		return id;
	}

	void DX12DescriptorManager::WriteBindlessSrv(const DX12DescriptorID& descriptorId,
		DX12Texture* texture, const TextureSrvCreateInfo& info) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_DX12Device);
		GGLAB_ASSERT_NOT_NULL(texture);
		GGLAB_ASSERT_NOT_NULL(texture->Get());
		GGLAB_ASSERT(descriptorId.IsValid());

		const auto texDesc = texture->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		// format
		srvDesc.Format = (info.m_Format == DXGI_FORMAT_UNKNOWN) ? texDesc.Format : info.m_Format;
		srvDesc.ViewDimension = info.m_Dimension;

		// mip levels
		const auto totalMips = (texDesc.MipLevels == 0) ? 1u : static_cast<uint32_t>(texDesc.MipLevels);
		const auto mipLevels = (info.m_MipLevels == std::numeric_limits<uint32_t>::max()) ?
			totalMips - info.m_MostDetailedMip :
			std::min(info.m_MipLevels, totalMips - info.m_MostDetailedMip);

		// array
		const uint32_t totalArray = static_cast<uint32_t>(texDesc.DepthOrArraySize);
		const uint32_t arraySize = (info.m_ArraySize == std::numeric_limits<uint32_t>::max()) ?
			totalArray - info.m_FirstArraySlice :
			std::min(info.m_ArraySize, totalArray - info.m_FirstArraySlice);

		switch (info.m_Dimension)
		{
		case D3D12_SRV_DIMENSION_TEXTURE2D:
		{
			srvDesc.Texture2D.MostDetailedMip = info.m_MostDetailedMip;
			srvDesc.Texture2D.MipLevels = mipLevels;
			srvDesc.Texture2D.PlaneSlice = info.m_PlaneSlice;
			srvDesc.Texture2D.ResourceMinLODClamp = info.m_ResourceMinLODClamp;
			break;
		}
		case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
		{
			srvDesc.Texture2DArray.MostDetailedMip = info.m_MostDetailedMip;
			srvDesc.Texture2DArray.MipLevels = mipLevels;
			srvDesc.Texture2DArray.FirstArraySlice = info.m_FirstArraySlice;
			srvDesc.Texture2DArray.ArraySize = arraySize;
			srvDesc.Texture2DArray.PlaneSlice = info.m_PlaneSlice;
			srvDesc.Texture2DArray.ResourceMinLODClamp = info.m_ResourceMinLODClamp;
			break;
		}
		case D3D12_SRV_DIMENSION_TEXTURECUBE:
		{
			srvDesc.TextureCube.MostDetailedMip = info.m_MostDetailedMip;
			srvDesc.TextureCube.MipLevels = mipLevels;
			srvDesc.TextureCube.ResourceMinLODClamp = info.m_ResourceMinLODClamp;
			break;
		}
		case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
		{
			const auto totalCubes = totalArray / CubemapFaceCount;
			const auto firstCube = info.m_FirstArraySlice;
			const auto cubeCount = (info.m_ArraySize == std::numeric_limits<uint32_t>::max()) ?
				(totalCubes - firstCube) :
				std::min(info.m_ArraySize, totalCubes - firstCube);

			srvDesc.TextureCubeArray.MostDetailedMip = info.m_MostDetailedMip;
			srvDesc.TextureCubeArray.MipLevels = mipLevels;
			srvDesc.TextureCubeArray.First2DArrayFace = firstCube * CubemapFaceCount;
			srvDesc.TextureCubeArray.NumCubes = cubeCount;
			srvDesc.TextureCubeArray.ResourceMinLODClamp = info.m_ResourceMinLODClamp;
			break;
		}
		default:
			GGLAB_UNREACHABLE("Unsupported SRV Dimension.");
			break;
		}

		auto descriptorView = BindlessSrvIdToView(descriptorId);
		m_DX12Device->Get()->CreateShaderResourceView(texture->Get(), &srvDesc, descriptorView.m_CpuHandle);
	}

	uint32_t DX12DescriptorManager::BindlessSrvIdToGlobalIndex(const DX12DescriptorID& descriptorId) const noexcept
	{
		return DescriptorIdToGlobalIndex(AllocatorType::BindlessSrv, descriptorId);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE DX12DescriptorManager::GetBindlessSrvGpuHandle(const DX12DescriptorID& descriptorId) const noexcept
	{
		return DescriptorIdToGpuHandle(HeapType::CbvSrvUav, AllocatorType::BindlessSrv, descriptorId);
	}

	void DX12DescriptorManager::RetireBindlessSrvId(const DX12DescriptorID& descriptorId, const DX12FencePoint& fencePoint) noexcept
	{
		RetireDescriptorId(AllocatorType::BindlessSrv, descriptorId, fencePoint);
	}

	DX12DescriptorView DX12DescriptorManager::BindlessSrvIdToView(const DX12DescriptorID& descriptorId) const noexcept
	{
		return DescriptorIdToView(AllocatorType::BindlessSrv, descriptorId);
	}

	DX12DescriptorID DX12DescriptorManager::AllocateBindlessSamplerId() noexcept
	{
		return AllocateDescriptorId(AllocatorType::GeneralSampler);
	}

	DX12DescriptorID DX12DescriptorManager::CreateBindlessSampler(const D3D12_SAMPLER_DESC& samplerDesc) noexcept
	{
		const DX12DescriptorID descriptorId = AllocateBindlessSamplerId();
		GGLAB_ASSERT(descriptorId.IsValid());

		WriteBindlessSampler(descriptorId, samplerDesc);

		return descriptorId;
	}

	void DX12DescriptorManager::WriteBindlessSampler(const DX12DescriptorID& descriptorId, const D3D12_SAMPLER_DESC& samplerDesc) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_DX12Device);
		GGLAB_ASSERT_NOT_NULL(m_DX12Device->Get());
		GGLAB_ASSERT(descriptorId.IsValid());

		const DX12DescriptorView descriptorView = BindlessSamplerIdToView(descriptorId);
		GGLAB_ASSERT(descriptorView.IsValid());

		m_DX12Device->Get()->CreateSampler(
			&samplerDesc,
			descriptorView.m_CpuHandle);
	}

	uint32_t DX12DescriptorManager::BindlessSamplerIdToGlobalIndex(const DX12DescriptorID& descriptorId) const noexcept
	{
		return DescriptorIdToGlobalIndex(AllocatorType::GeneralSampler, descriptorId);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE DX12DescriptorManager::GetBindlessSamplerGpuHandle(const DX12DescriptorID& descriptorId) const noexcept
	{
		return DescriptorIdToGpuHandle(HeapType::Sampler, AllocatorType::GeneralSampler, descriptorId);
	}

	void DX12DescriptorManager::RetireBindlessSamplerId(const DX12DescriptorID& descriptorId, const DX12FencePoint& fencePoint) noexcept
	{
		RetireDescriptorId(AllocatorType::GeneralSampler, descriptorId, fencePoint);
	}

	DX12DescriptorView DX12DescriptorManager::BindlessSamplerIdToView(const DX12DescriptorID& descriptorId) const noexcept
	{
		return DescriptorIdToView(AllocatorType::GeneralSampler, descriptorId);
	}

	DX12DescriptorView DX12DescriptorManager::AllocateDevelopGuiSrvView() noexcept
	{
		auto* allocator = GetFreeListAllocator(AllocatorType::DevelopGuiSrv);
		GGLAB_ASSERT_NOT_NULL(allocator);

		return allocator->AllocateView();
	}

	void DX12DescriptorManager::DeferFreeDevelopGuiSrvInFrame(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) noexcept
	{
		auto* allocator = GetFreeListAllocator(AllocatorType::DevelopGuiSrv);
		GGLAB_ASSERT_NOT_NULL(allocator);

		allocator->DeferFreeFromCpuHandleInFrame(cpuHandle);
	}

	void DX12DescriptorManager::DeferFreeDevelopGuiSrvInFrame(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) noexcept
	{
		auto* allocator = GetFreeListAllocator(AllocatorType::DevelopGuiSrv);
		GGLAB_ASSERT_NOT_NULL(allocator);

		allocator->DeferFreeFromGpuHandleInFrame(gpuHandle);
	}

	DX12DescriptorID DX12DescriptorManager::AllocateDescriptorId(AllocatorType allocatorType) noexcept
	{
		auto* allocator = GetFreeListAllocator(allocatorType);
		GGLAB_ASSERT_NOT_NULL(allocator);

		const auto descriptorId = allocator->AllocateId();

		GGLAB_ASSERT_MSG(descriptorId.IsValid(), "DX12DescriptorManager: AllocateDescriptorId failed.");

		return descriptorId;
	}

	DX12DescriptorView DX12DescriptorManager::DescriptorIdToView(AllocatorType allocatorType, const DX12DescriptorID& descriptorId) const noexcept
	{
		auto* allocator = GetFreeListAllocator(allocatorType);
		GGLAB_ASSERT_NOT_NULL(allocator);

		GGLAB_ASSERT_MSG(descriptorId.IsValid(), "DX12DescriptorManager: invalid descriptor id.");

		GGLAB_ASSERT_MSG(allocator->IsIdAlive(descriptorId),
			"DX12DescriptorManager: descriptor id is stale or belongs to a different allocator.");

		return allocator->ViewAtId(descriptorId);
	}

	uint32_t DX12DescriptorManager::DescriptorIdToGlobalIndex(AllocatorType allocatorType, const DX12DescriptorID& descriptorId) const noexcept
	{
		auto* allocator = GetFreeListAllocator(allocatorType);
		GGLAB_ASSERT_NOT_NULL(allocator);

		GGLAB_ASSERT_MSG(descriptorId.IsValid(), "DX12DescriptorManager: invalid descriptor id.");

		GGLAB_ASSERT_MSG(allocator->IsIdAlive(descriptorId),
			"DX12DescriptorManager: descriptor id is stale or belongs to a different allocator.");

		return descriptorId.m_Index;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE DX12DescriptorManager::DescriptorIdToGpuHandle(HeapType heapType, AllocatorType allocatorType, const DX12DescriptorID& descriptorId) const noexcept
	{
		const uint32_t globalIndex = DescriptorIdToGlobalIndex(allocatorType, descriptorId);

		auto* heap = GetHeap(heapType);
		GGLAB_ASSERT_NOT_NULL(heap);

		return heap->GpuHandleAt(globalIndex);
	}

	void DX12DescriptorManager::RetireDescriptorId(AllocatorType allocatorType, const DX12DescriptorID& descriptorId, const DX12FencePoint& fencePoint) noexcept
	{
		auto* allocator = GetFreeListAllocator(allocatorType);
		GGLAB_ASSERT_NOT_NULL(allocator);

		GGLAB_ASSERT_MSG(descriptorId.IsValid(), "DX12DescriptorManager: invalid descriptor id.");
		GGLAB_ASSERT_MSG(allocator->IsIdAlive(descriptorId),
			"DX12DescriptorManager: descriptor id is stale or belongs to a different allocator.");

		allocator->RetireId(descriptorId, fencePoint);
	}
}
