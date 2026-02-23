#pragma once
#include "RGResource.h"
#include "GraphicsTypes.h"
#include "TypeUtils.h"

namespace gglab
{
	class RGGpuResourceAllocator;
	class RGExternalResourceRegistry;
	class DX12Texture;

	/*
	* Management runtime generated GPU Textures
	*/
	class RenderResourceRegistry
	{
	public:
		struct CreateInfo
		{
			RGGpuResourceAllocator* m_GpuResourceAllocator = nullptr;
			RGExternalResourceRegistry* m_ExternalResourceRegistry = nullptr;
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
			uint32_t m_BindlessSrvIndex = 0;

			bool m_Allocated = false;
			bool m_Dirty = false;
		};

	public:
		explicit RenderResourceRegistry(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderResourceRegistry);
		~RenderResourceRegistry() = default;

		void MarkDirty(TextureIndex index) noexcept;
		bool IsDirty(TextureIndex index) const noexcept;
		void ClearDirty(TextureIndex index) noexcept;

		DX12Texture* GetTexture(TextureIndex index) noexcept;
		ResourceIndex GetEnternalIndex(TextureIndex index) const noexcept;
		uint32_t GetBindlessSrvIndex(TextureIndex index) const noexcept;

		void ReleaseAll(const DX12FencePoint& fencePoint) noexcept;

	private:
		RGGpuResourceAllocator* m_GpuResourceAllocator = nullptr;
		RGExternalResourceRegistry* m_ExternalResourceRegistry = nullptr;

		std::array<TextureEntry, utils::EnumCount<TextureIndex>()> m_TextureEntries;
	};
}