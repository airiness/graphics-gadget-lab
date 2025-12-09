#include "Precompiled.h"
#include "DX12Device.h"
#include "DX12SwapChain.h"
#include "DX12CommandQueue.h"
#include "DX12CommandList.h"
#include "DX12CommandAllocator.h"
#include "DX12Fence.h"
#include "DX12Buffer.h"
#include "DX12ResourceUploader.h"
#include "Application.h"
#include "HResult.h"

namespace gglab
{
	DX12Device::DX12Device() noexcept
	{
		InitializeWinPIX();
		InitializeDXGIFactory();
		InitializeDXGIAdapter();
		InitializeDebugLayer();
		InitializeD3D12Device();
		InitializeInfoQueue();
		CheckFeatureSupport();
	}

	DX12Device::~DX12Device() noexcept
	{
	}

	void DX12Device::Initialize() noexcept
	{
		InitializeCommandQueues();
		InitializeDescriptorAllocators();
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

	void DX12Device::FlushGPU() noexcept
	{
		m_DirectCommandQueue->FlushCommandQueue();
		m_ComputeCommandQueue->FlushCommandQueue();
		m_CopyCommandQueue->FlushCommandQueue();
	}

	ComPtr<ID3D12CommandQueue> DX12Device::CreateDirectX12CommandQueue(D3D12_COMMAND_LIST_TYPE type, int32_t priority, D3D12_COMMAND_QUEUE_FLAGS flags) const noexcept
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = type;
		desc.Priority = priority;
		desc.Flags = flags;
		desc.NodeMask = 0;

		ComPtr<ID3D12CommandQueue> commandQueue;
		GGLAB_HR_DX(m_D3D12Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)), m_D3D12Device.Get());

		return commandQueue;
	}

	ComPtr<ID3D12GraphicsCommandList7> DX12Device::CreateDirectX12CommandGraphicsList(D3D12_COMMAND_LIST_TYPE type) const noexcept
	{
		ComPtr<ID3D12GraphicsCommandList7> commandList;
		GGLAB_HR_DX(m_D3D12Device->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE,IID_PPV_ARGS(&commandList)),
			m_D3D12Device.Get());

		return commandList;
	}

	void DX12Device::InitializeDXGIFactory() noexcept
	{
		UINT createFactoryFlags = 0;

#if defined (BUILD_DEBUG)
		createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

		ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue))))
		{
			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
		}
#endif
		ComPtr<IDXGIFactory7> dxgiFactory;
		GGLAB_HR(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

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
		ComPtr<ID3D12Debug1> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			debugController->SetEnableGPUBasedValidation(true);
			
			ComPtr<ID3D12Debug5> debugController5;
			if (SUCCEEDED(debugController.As(&debugController5)))
			{
				debugController5->SetEnableAutoName(true);
			}
		}
#endif
	}

	void DX12Device::InitializeWinPIX() noexcept
	{
#if defined(BUILD_DEBUG)
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

		HRESULT result = E_FAIL;
		for (auto level : featureLevels)
		{
			result = D3D12CreateDevice(m_DxgiAdapter.Get(), level, IID_PPV_ARGS(&m_D3D12Device));
			if (SUCCEEDED(result))
			{
				m_FeatureLevel = level;
				break;
			}
		}

		GGLAB_HR(result);
	}

	void DX12Device::InitializeInfoQueue() noexcept
	{
#if defined(BUILD_DEBUG)
		ComPtr<ID3D12InfoQueue> infoQueue;
		if (SUCCEEDED(m_D3D12Device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
		{		
			// Break on DXGI_ERROR_DEVICE_REMOVED and DXGI_ERROR_DEVICE_RESET
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
			// Suppress whole categories of messages
			//D3D12_MESSAGE_CATEGORY categories[] = {};
			// Suppress messages based on their severity level
			D3D12_MESSAGE_SEVERITY severities[] =
			{
				D3D12_MESSAGE_SEVERITY_INFO
			};
			// Suppress individual messages by their ID
			D3D12_MESSAGE_ID denyIds[] =
			{
				D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE, // I'm really not sure why this is an error.  I often clear with different color than the resource was created with.
				D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                       // This warning occurs when mapping a buffer with NULL range (which is valid).
				D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                     // This warning occurs when unmapping a buffer with NULL range (which is valid).
				D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_WRONGSWAPCHAINBUFFERREFERENCE, // This warning occurs when a command list that was recorded with one swapchain is executed with another swapchain (which is valid).
			};
			D3D12_INFO_QUEUE_FILTER filter = {};
			//filter.DenyList.NumCategories = _countof(categories);
			//filter.DenyList.pCategoryList = categories;
			filter.DenyList.NumSeverities = _countof(severities);
			filter.DenyList.pSeverityList = severities;
			//filter.DenyList.NumIDs = _countof(denyIds);
			//filter.DenyList.pIDList = denyIds;
			GGLAB_HR(infoQueue->PushStorageFilter(&filter));
		}

#endif
	}

	void DX12Device::InitializeCommandQueues() noexcept
	{
		m_DirectCommandQueue = std::make_unique<DX12CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
		m_ComputeCommandQueue = std::make_unique<DX12CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
		m_CopyCommandQueue = std::make_unique<DX12CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COPY);
		m_UploadCommandQueue = std::make_unique<DX12CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);	//d3dx12 upload resource uses direct type
	}

	void DX12Device::InitializeSwapChain() noexcept
	{
		auto* app = Application::GetInstance();
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
		}
		m_CopyCommandList = std::make_unique<DX12CommandList>(this, D3D12_COMMAND_LIST_TYPE_COPY);
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
		m_UploadCommandAllocatorPool = std::make_unique<DX12CommandAllocatorPool>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	}

	void DX12Device::InitializeDescriptorAllocators() noexcept
	{
		m_CbvSrvUavDescriptorAllocator = std::make_unique<DX12DescriptorFreeListAllocator>(this,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 1024);

		m_RtvDescriptorAllocator = std::make_unique<DX12DescriptorFreeListAllocator>(this,
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 32);

		m_DsvDescriptorAllocator = std::make_unique<DX12DescriptorFreeListAllocator>(this,
			D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 16);

		m_SamplerDescriptorAllocator = std::make_unique<DX12DescriptorFreeListAllocator>(this,
			D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 256);
	}

	void DX12Device::InitializeMemAllocator() noexcept
	{
		using namespace D3D12MA;
		D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
		allocatorDesc.Flags = D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS;
		allocatorDesc.pDevice = m_D3D12Device.Get();
		allocatorDesc.pAdapter = m_DxgiAdapter.Get();
		GGLAB_HR(D3D12MA::CreateAllocator(&allocatorDesc, &m_MemAllocator));
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