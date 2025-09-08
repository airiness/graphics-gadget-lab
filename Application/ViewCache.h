#pragma once
#include "DX12Descriptor.h"
#include "FNV1a.h"

namespace gglab
{
	class DX12Device;
	class DX12Texture;
	class DX12DescriptorFreeListAllocator;
	class DX12FencePoint;
	class ViewCache
	{
	public:
		using ResourceIndex = uint32_t;
		enum class Type : uint32_t
		{
			RTV,
			DSV,
			SRV,
			UVA,
			Count,
		};

		using DescriptorsAllocatorArray = std::array<DX12DescriptorFreeListAllocator*, static_cast<uint32_t>(Type::Count)>;

		struct ViewKey
		{
			Type m_Type = Type::RTV;
			DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
			uint32_t m_ResouceIndex = 0;
			uint32_t m_ComponentMapping = 0;
			uint16_t m_MipSlice = 0;
			uint16_t m_MipLevels = 0;
			uint16_t m_ArraySlice = 0;
			uint8_t m_PlaneSlice = 0;
			uint8_t m_Dimension = 0;
			uint8_t m_Flags = 0;

			bool operator==(const ViewKey& other) const noexcept = default;

			auto AsTuple() const noexcept
			{
				return std::tie(m_Type, m_Format, m_ResouceIndex, m_ComponentMapping, m_MipSlice, m_MipLevels, m_ArraySlice, m_PlaneSlice, m_Dimension, m_Flags);
			}
		};
		using ViewKeyHash = KeyHash<ViewKey>;
		
	public:
		explicit ViewCache(DX12Device* dx12Device, const DescriptorsAllocatorArray& descriptorAllocators) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(ViewCache);
		~ViewCache();

		const DX12Descriptor& GetRTV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_RENDER_TARGET_VIEW_DESC> desc) noexcept;

		const DX12Descriptor& GetDSV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_DEPTH_STENCIL_VIEW_DESC> desc) noexcept;

		const DX12Descriptor& GetSRV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC> desc) noexcept;

		const DX12Descriptor& GetUAV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_UNORDERED_ACCESS_VIEW_DESC> desc) noexcept;

		void RetireResourceAllViews(ResourceIndex resourceIndex, const DX12FencePoint& fencePoint) noexcept;

		void GarbageCollect() noexcept;

		void FreeAllImmediately(ResourceIndex resourceIndex) noexcept;

	private:
		DX12DescriptorFreeListAllocator* GetDescriptorAllocator(Type type) const noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		DescriptorsAllocatorArray m_DescriptorAllocators;

		std::unordered_map<ViewKey, DX12Descriptor, ViewKeyHash> m_Cache;
		std::unordered_map<uint32_t, std::vector<ViewKey>> m_ResourceViews;

		struct Pending
		{
			uint32_t m_ResourceIndex = 0;
			DX12FencePoint m_FencePoint;
			std::vector<DX12Descriptor> m_Descriptors;
		};

		std::deque<Pending> m_Pendings;

		std::mutex m_Mutex;
	};

	template<ViewCache::Type T> struct ViewTraits;
}