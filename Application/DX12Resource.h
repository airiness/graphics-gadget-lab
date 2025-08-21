#pragma once

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12Resource
	{
	public:
		struct CreateInfo
		{
			D3D12MA::Allocator* m_Allocator = nullptr;
			D3D12MA::ALLOCATION_DESC m_AllocDesc = {};
			CD3DX12_RESOURCE_DESC m_ResourceDesc = {};
			D3D12_RESOURCE_STATES m_InitStates = D3D12_RESOURCE_STATE_COMMON;
			std::optional<D3D12_CLEAR_VALUE> m_ClearValue = std::nullopt;
		};

		struct AliasingInfo
		{
			D3D12MA::Allocator* m_Allocator = nullptr;
			D3D12MA::Allocation* m_Allocation = nullptr;
			uint64_t m_LocalOffset = 0;
			CD3DX12_RESOURCE_DESC m_ResourceDesc = {};
			D3D12_RESOURCE_STATES m_InitStates = D3D12_RESOURCE_STATE_COMMON;
			std::optional<D3D12_CLEAR_VALUE> m_ClearValue = std::nullopt;
		};

	public:
		DX12Resource() = default;
		explicit DX12Resource(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12Resource);
		virtual ~DX12Resource() noexcept = default;

		ID3D12Resource* Get() const noexcept;

		bool TransitionNeeded(D3D12_RESOURCE_STATES newStates) const noexcept;
		void AdoptExternal(ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES initStates) noexcept;
		void RebindAliasing(const AliasingInfo& aliasingInfo) noexcept;
		void Reset() noexcept;
		D3D12_RESOURCE_BARRIER MakeTransition(D3D12_RESOURCE_STATES newState, uint32_t subResource) const noexcept;
		void SetDebugName(const wchar_t* name) noexcept;

	public:
		static D3D12_RESOURCE_BARRIER MakeAliasingBarrier(const DX12Resource* before, const DX12Resource* after) noexcept;

	protected:
		D3D12MA::Allocator* m_Allocator = nullptr;
		CD3DX12_RESOURCE_DESC m_ResourceDesc = {};
		D3D12_RESOURCE_STATES m_ResourceState = D3D12_RESOURCE_STATE_COMMON;
		std::optional<D3D12_CLEAR_VALUE> m_ClearValue = std::nullopt;
		ComPtr<D3D12MA::Allocation> m_Allocation;
		ComPtr<ID3D12Resource> m_Resource;
	};
}

