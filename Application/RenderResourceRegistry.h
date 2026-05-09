#pragma once
#include "RGResource.h"
#include "GraphicsTypes.h"
#include "GPUStructures.h"
#include "TypeUtils.h"

namespace gglab
{
	class RGGpuResourceAllocator;
	class RGExternalResourceRegistry;
	class DX12Texture;
	class DX12DescriptorManager;

	/*
	* Management runtime generated GPU Textures
	*/
	class RenderResourceRegistry
	{
	public:
		struct CreateInfo
		{
			RGGpuResourceAllocator* m_RGGpuResAllocator = nullptr;
			RGExternalResourceRegistry* m_ExternalResourceRegistry = nullptr;
			DX12DescriptorManager* m_DescriptorManager = nullptr;
		};

		enum class TextureIndex : uint8_t
		{
			IBL_BrdfLut,

			Count
		};

	private:
		struct TextureEntry
		{
			RGTextureDesc m_RgTexDesc{};
			ResourceIndex m_InternalIndex{};
			ResourceIndex m_ExternalIndex{};
			DX12DescriptorID m_SrvId{};

			bool m_Allocated = false;
			bool m_Dirty = false;
		};

	public:
		explicit RenderResourceRegistry(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderResourceRegistry);
		~RenderResourceRegistry() = default;

		void EnsureIblResources(uint32_t brdfLutSize = 256,
			DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_R16G16_FLOAT,
			const DX12FencePoint* retireFenceOpt = nullptr) noexcept;

		void MarkDirty(TextureIndex index) noexcept;
		bool IsDirty(TextureIndex index) const noexcept;
		void ClearDirty(TextureIndex index) noexcept;

		DX12Texture* GetTexture(TextureIndex index) noexcept;
		ResourceIndex GetExternalIndex(TextureIndex index) const noexcept;
		DX12DescriptorID GetBindlessSrvId(TextureIndex index) const noexcept;
		uint32_t GetBindlessSrvIndex(TextureIndex index) const noexcept;
		D3D12_GPU_DESCRIPTOR_HANDLE GetBindlessSrvGpuHandle(TextureIndex index) const noexcept;

		void FillIBLBindlessGPU(IBLResourceGPU& out) const noexcept;

		void ReleaseAll(const DX12FencePoint& fencePoint) noexcept;

	private:
		void EnsureTexture(TextureIndex index,
			const RGTextureDesc& desc,
			const DX12FencePoint* retireFenceOpt = nullptr) noexcept;

		void DestroyTexture(TextureIndex index, const DX12FencePoint& fencePoint) noexcept;

	private:
		RGGpuResourceAllocator* m_RGGpuResAllocator = nullptr;
		RGExternalResourceRegistry* m_ExternalResourceRegistry = nullptr;
		DX12DescriptorManager* m_DescriptorManager = nullptr;

		std::array<TextureEntry, utils::EnumCount<TextureIndex>()> m_TextureEntries;
	};
}