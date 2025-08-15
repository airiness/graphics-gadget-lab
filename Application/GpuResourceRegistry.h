#pragma once
#include "RGResource.h"
#include "DX12Texture.h"
#include "DX12Buffer.h"

namespace graphicsGadgetLab
{
	class DX12Device;
	class GpuResourceRegistry
	{
	public:
		explicit GpuResourceRegistry(DX12Device* device) noexcept;
		~GpuResourceRegistry() noexcept = default;

		template<typename ResourceDesc>
		int32_t AcquireResource(const ResourceDesc& desc) noexcept = delete;

		template<>
		int32_t AcquireResource<RGTextureDesc>(const RGTextureDesc& desc) noexcept;

		template<>
		int32_t AcquireResource<RGBufferDesc>(const RGBufferDesc& desc) noexcept;

		template<typename ResourceDesc>
		void ReleaseResource(int32_t index) noexcept = delete;

		template<>
		void ReleaseResource<RGTextureDesc>(int32_t index) noexcept;

		template<>
		void ReleaseResource<RGBufferDesc>(int32_t index) noexcept;

		void ReleaseAll() noexcept;

		DX12Texture* GetTexture(int32_t index) const noexcept;
		DX12Buffer* GetBuffer(int32_t index) const noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;

		std::vector<std::unique_ptr<DX12Texture>> m_Textures;
		std::vector<std::unique_ptr<DX12Buffer>> m_Buffers;
	};

	template<>
	inline int32_t GpuResourceRegistry::AcquireResource(const RGTextureDesc& desc) noexcept
	{
		auto index = static_cast<int32_t>(m_Textures.size());
		m_Textures.emplace_back(std::make_unique<DX12Texture>(
			m_DX12Device,
			D3D12_HEAP_TYPE_DEFAULT,
			CD3DX12_RESOURCE_DESC::Tex2D(
				desc.m_Format,
				desc.m_Width,
				desc.m_Height,
				desc.m_ArraySize,
				desc.m_MipLevels,
				desc.m_SampleCount),
			ToD3D12ResourceStates<RGTextureUsage>(desc.m_Usage)));

		return index;
	}

	template<>
	inline int32_t GpuResourceRegistry::AcquireResource(const RGBufferDesc& desc) noexcept
	{
		auto index = static_cast<int32_t>(m_Buffers.size());
		m_Buffers.emplace_back(std::make_unique<DX12Buffer>(
			m_DX12Device,
			D3D12_HEAP_TYPE_DEFAULT,
			CD3DX12_RESOURCE_DESC::Buffer(desc.m_Size),
			ToD3D12ResourceStates<RGBufferUsage>(desc.m_Usage)));

		return index;
	}

	template<>
	inline void GpuResourceRegistry::ReleaseResource<RGTextureDesc>(int32_t index) noexcept
	{
	}

	template<>
	inline void GpuResourceRegistry::ReleaseResource<RGBufferDesc>(int32_t index) noexcept
	{
	}
}