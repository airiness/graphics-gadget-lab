#include "Precompiled.h"
#include "DX12Device.h"
#include "DX12SwapChain.h"
#include "DX12CommandQueue.h"
#include "DX12CommandList.h"
#include "DX12CommandAllocator.h"
#include "DX12Descriptor.h"
#include "DX12Fence.h"
#include "DX12Buffer.h"
#include "DX12ResourceUploader.h"
#include "Application.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12Device::DX12Device() noexcept
	{
		InitializeDXGIFactory();
		InitializeDXGIAdapter();
		InitializeDebugLayer();
		InitializeD3D12Device();
		CheckFeatureSupport();
	}

	DX12Device::~DX12Device() noexcept
	{
	}

	void DX12Device::Initialize() noexcept
	{
		InitializeCommandQueues();
		InitializeDescriptorHeaps();
		InitializeMemAllocator();
		InitializeSwapChain();
		InitializeCommandLists();
		InitializeCommandAllocatorPools();
		InitializeResourceUploader();
	}

	void DX12Device::OnResize(uint32_t width, uint32_t height) noexcept
	{
		// TODO: Window resize Process 
	}

	void DX12Device::Finalize() noexcept
	{
		FinalizeMemAllocator();
	}

	ComPtr<ID3D12CommandQueue> DX12Device::CreateDirectX12CommandQueue(D3D12_COMMAND_LIST_TYPE type, int32_t priority, D3D12_COMMAND_QUEUE_FLAGS flags) const noexcept
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = type;
		desc.Priority = priority;
		desc.Flags = flags;
		desc.NodeMask = 0;

		ComPtr<ID3D12CommandQueue> commandQueue;
		utility::ThrowIfFailed(m_D3D12Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)));

#if defined (BUILD_DEBUG)
		utility::SetDebugName(commandQueue.Get(),
			std::format(L"CommandQueue[{:p}]_{} ", (void*)this, utility::GetCommandListTypeName(type)).c_str());
#endif

		return commandQueue;
	}

	ComPtr<ID3D12GraphicsCommandList7> DX12Device::CreateDirectX12CommandGraphicsList(D3D12_COMMAND_LIST_TYPE type) const noexcept
	{
		ComPtr<ID3D12GraphicsCommandList7> commandList;
		utility::ThrowIfFailed(m_D3D12Device->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE,
			IID_PPV_ARGS(&commandList)));

#if defined (BUILD_DEBUG)
		utility::SetDebugName(commandList.Get(),
			std::format(L"CommandList[{:p}]_{} ", (void*)this, utility::GetCommandListTypeName(type)).c_str());
#endif

		return commandList;
	}

	void DX12Device::InitializeDXGIFactory() noexcept
	{
		UINT createFactoryFlags = 0;

#ifdef BUILD_DEBUG
		createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

		ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue))))
		{
			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
		}
#endif
		ComPtr<IDXGIFactory7> dxgiFactory;
		utility::ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

		m_DxgiFactory = dxgiFactory;
	}

	void DX12Device::InitializeDXGIAdapter() noexcept
	{
		ComPtr<IDXGIAdapter1> dxgiAdapter;
		for (UINT i = 0; m_DxgiFactory->EnumAdapterByGpuPreference(i,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&dxgiAdapter)) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 desc = {};
			dxgiAdapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				continue;
			}

			if (SUCCEEDED(D3D12CreateDevice(dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
			{
				m_DxgiAdapter = dxgiAdapter;
				break;
			}
		}

		GGLAB_ASSERT_MSG(m_DxgiAdapter != nullptr, "Create DxgiAdapter failed.");
	}

	void DX12Device::InitializeDebugLayer() noexcept
	{
#if defined(BUILD_DEBUG)
		// Validate Debug Layer
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// 
			ComPtr<ID3D12Debug1> debugController1;
			if (SUCCEEDED(debugController.As(&debugController1)))
			{
				debugController1->SetEnableGPUBasedValidation(true);
			}
		}

		// Try to load `WinPixGpuCapturer.dll` for Frame Capture.
		if (GetModuleHandle(L"WinPixGpuCapturer.dll") == 0)
		{	
			const auto& dllPath = GetLatestWinPixGpuCapturerPath();
			if (!dllPath.empty())
			{
				LoadLibrary(dllPath.c_str());
			}
		}

#endif
	}

	void DX12Device::InitializeD3D12Device() noexcept
	{
		// Feature check and Create Device
		constexpr D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_12_2,
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0
		};

		HRESULT hr = E_FAIL;
		for (auto level : featureLevels)
		{
			hr = D3D12CreateDevice(m_DxgiAdapter.Get(), level, IID_PPV_ARGS(&m_D3D12Device));
			if (SUCCEEDED(hr))
			{
				m_FeatureLevel = level;
				break;
			}
		}

		utility::ThrowIfFailed(hr);

		// Get IncrementSize
		m_RtvDescriptorSize = m_D3D12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_DsvDescriptorSize = m_D3D12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		m_CbvSrvUavDescriptorSize = m_D3D12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	void DX12Device::InitializeCommandQueues() noexcept
	{
		m_DirectCommandQueue = std::make_unique<DX12CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
		m_ComputeCommandQueue = std::make_unique<DX12CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
		m_CopyCommandQueue = std::make_unique<DX12CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COPY);
	}

	void DX12Device::InitializeSwapChain() noexcept
	{
		auto* app = Application::Get();
		auto width = app->GetWindowWidth();
		auto height = app->GetWindowHeight();

		m_SwapChain = std::make_unique<DX12SwapChain>(this, m_DirectCommandQueue.get(), width, height);
	}

	void DX12Device::InitializeCommandLists() noexcept
	{
		for (int32_t i = 0; i < BufferCount; i++)
		{
			m_GraphicsCommandLists[i] = std::make_unique<DX12CommandList>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
			m_ComputeCommandLists[i] = std::make_unique<DX12CommandList>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
			m_CopyCommandLists[i] = std::make_unique<DX12CommandList>(this, D3D12_COMMAND_LIST_TYPE_COPY);
		}
	}

	void DX12Device::InitializeResourceUploader() noexcept
	{
		m_ResourceUploader = std::make_unique<DX12ResourceUploader>(this);
	}

	void DX12Device::InitializeCommandAllocatorPools() noexcept
	{
		m_GraphicsCommandAllocatorPool = std::make_unique<DX12CommandAllocatorPool>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
		m_ComputeCommandAllocatorPool = std::make_unique<DX12CommandAllocatorPool>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
		m_CopyCommandAllocatorPool = std::make_unique<DX12CommandAllocatorPool>(this, D3D12_COMMAND_LIST_TYPE_COPY);
	}

	void DX12Device::InitializeDescriptorHeaps() noexcept
	{
		m_CbvSrvUavDescriptorHeap = std::make_unique<DX12DescriptorHeap>(this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 1024);
		m_RtvDescriptorHeap = std::make_unique<DX12DescriptorHeap>(this, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 32);
		m_DsvDescriptorHeap = std::make_unique<DX12DescriptorHeap>(this, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 16);
		m_SamplerDescriptorHeap = std::make_unique<DX12DescriptorHeap>(this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 1024);
	}

	void DX12Device::InitializeMemAllocator() noexcept
	{
		using namespace D3D12MA;
		D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
		allocatorDesc.Flags = D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS;
		allocatorDesc.pDevice = m_D3D12Device.Get();
		allocatorDesc.pAdapter = m_DxgiAdapter.Get();
		utility::ThrowIfFailed(D3D12MA::CreateAllocator(&allocatorDesc, &m_MemAllocator));
	}

	void DX12Device::FinalizeMemAllocator() noexcept
	{
		if (m_MemAllocator)
		{
			m_MemAllocator->Release();
			m_MemAllocator = nullptr;
		}
	}

	void DX12Device::CheckFeatureSupport() noexcept
	{
		CD3DX12FeatureSupport featureSupport;
		featureSupport.Init(m_D3D12Device.Get());

		// RatTracing support
		m_DX12FeatureSupport.m_RayTracingSupported = featureSupport.RaytracingTier() != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

		// MeshShader support
		m_DX12FeatureSupport.m_MeshShaderSupported = featureSupport.MeshShaderTier() != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;

		// Enhanced Barrier
		m_DX12FeatureSupport.m_EnhancedBarriers = featureSupport.EnhancedBarriersSupported();

		// Tearing support
		BOOL tearSupport = FALSE;
		if (m_DxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearSupport, sizeof(BOOL)) == S_OK)
		{
			m_DX12FeatureSupport.m_TearingSupported = tearSupport;
		}
	}

	std::wstring DX12Device::GetLatestWinPixGpuCapturerPath() noexcept
	{
		LPWSTR programFilesPath = nullptr;
		SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

		std::filesystem::path pixInstallationPath = programFilesPath;
		pixInstallationPath /= "Microsoft PIX";

		std::wstring newestVersionFound;

		for (auto const& directory_entry : std::filesystem::directory_iterator(pixInstallationPath))
		{
			if (directory_entry.is_directory())
			{
				if (newestVersionFound.empty() || newestVersionFound < directory_entry.path().filename().c_str())
				{
					newestVersionFound = directory_entry.path().filename().c_str();
				}
			}
		}

		if (newestVersionFound.empty())
		{
			return L"";
		}

		return pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll";
	}
}