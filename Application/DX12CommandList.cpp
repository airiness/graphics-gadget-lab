#include "Precompiled.h"
#include "DX12CommandList.h"
#include "DX12Device.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12CommandList::DX12CommandList(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept :
		m_DX12Device(dx12Device),
		m_Type(type)
	{
		CreateCommandAllocator(type);
		CreateCommandList(type);
	}

	DX12CommandList::~DX12CommandList() noexcept
	{
	}

	void DX12CommandList::Begin() noexcept
	{
		utility::ThrowIfFailed(m_D3D12CommandAllocator->Reset());
		utility::ThrowIfFailed(m_D3D12GraphicsCommandList->Reset(m_D3D12CommandAllocator.Get(), nullptr));

	}

	void DX12CommandList::End() noexcept
	{
		utility::ThrowIfFailed(m_D3D12GraphicsCommandList->Close());
	}

	void DX12CommandList::Execute(ID3D12CommandQueue* commandQueue) noexcept
	{
		ID3D12CommandList* const commandLists[] = { m_D3D12GraphicsCommandList.Get() };
		commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
	}

	void DX12CommandList::CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type) noexcept
	{
		auto device = m_DX12Device->Get();
		utility::ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&m_D3D12CommandAllocator)));

#if defined (BUILD_DEBUG)
		utility::SetDebugName(m_D3D12CommandAllocator.Get(),
			std::format(L"CommandAllocator[{:p}]_{} ", (void*)this, utility::GetCommandListTypeName(type)).c_str());
#endif


	}
	void DX12CommandList::CreateCommandList(D3D12_COMMAND_LIST_TYPE type) noexcept
	{
		auto device = m_DX12Device->Get();
		utility::ThrowIfFailed(device->CreateCommandList(0, type, m_D3D12CommandAllocator.Get(), nullptr,
			IID_PPV_ARGS(&m_D3D12GraphicsCommandList)));

#if defined (BUILD_DEBUG)
		utility::SetDebugName(m_D3D12GraphicsCommandList.Get(),
			std::format(L"CommandList[{:p}]_{} ", (void*)this, utility::GetCommandListTypeName(type)).c_str());
#endif

		utility::ThrowIfFailed(m_D3D12GraphicsCommandList->Close());
	}

}