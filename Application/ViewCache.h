#pragma once
#include "DX12Descriptor.h"
#include "FNV1a.h"

namespace gglab
{
	class DX12Device;
	class DX12Texture;
	class DX12DescriptorFreeListAllocator;
	class ViewCache
	{
	public:
		enum class Type : uint32_t
		{
			RTV,
			DSV,
			// TODO: UAV, SRV, CBV

			Count,
		};

		using DescriptorsAllocatorArray = std::array<DX12DescriptorFreeListAllocator*, static_cast<uint32_t>(Type::Count)>;

		struct ViewKey
		{
			Type m_Type = Type::RTV;
			uint32_t m_ResouceIndex = 0;
			DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
			uint16_t m_MipSlice = 1;
			uint16_t m_ArraySlice = 1;
			uint8_t m_PlaneSlice = 1;
			uint8_t m_Dimension = 0;
			uint8_t m_Flags = 0;

			bool operator==(const ViewKey& other) const noexcept = default;

			auto AsTuple() const noexcept
			{
				return std::tie(m_Type, m_ResouceIndex, m_MipSlice, m_ArraySlice, m_PlaneSlice, m_Dimension, m_Flags);
			}

			static ViewKey CreateRTVKey(uint32_t resourceIndex,
				DX12Texture* texture,
				const D3D12_RENDER_TARGET_VIEW_DESC& inDesc,
				D3D12_RENDER_TARGET_VIEW_DESC& outDesc) noexcept;

			static ViewKey CreateDSVKey(uint32_t resourceIndex,
				DX12Texture* texture,
				const D3D12_DEPTH_STENCIL_VIEW_DESC& inDesc,
				D3D12_DEPTH_STENCIL_VIEW_DESC& outDesc);
		};
		using ViewKeyHash = KeyHash<ViewKey>;
		

	public:
		explicit ViewCache(DX12Device*, const DescriptorsAllocatorArray& descritproAllocators) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(ViewCache);
		~ViewCache();

		const DX12Descriptor& GetRenderTargetView(uint32_t resourceIndex, DX12Texture* texture, 
			std::optional<D3D12_RENDER_TARGET_VIEW_DESC> desc) noexcept;

		const DX12Descriptor& GetDepthStenciView(uint32_t resourceIndex, DX12Texture* texture,
			std::optional<D3D12_DEPTH_STENCIL_VIEW_DESC> desc) noexcept;

		void RetireResourceAllViews(uint32_t resourceIndex, const DX12FencePoint& fencePoint) noexcept;

		void GarbageCollect() noexcept;

		void RetireResourceAllViewsImmediately(uint32_t resourceIndex) noexcept;


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
}