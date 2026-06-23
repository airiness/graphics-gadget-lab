#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandAllocator.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"
#include "Core/HResult.h"

namespace gglab
{
	DX12Device::~DX12Device()
	{
		if (m_IsInitialized)
		{
			Finalize();
		}
	}

	void DX12Device::Initialize(const CreateInfo& createInfo) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}

#if defined(BUILD_DEBUG)
		if (createInfo.m_TryLoadWinPix)
		{
			InitializeWinPIX();
		}
#endif

		InitializeDXGIFactory();
		InitializeDXGIAdapter();
#if defined(BUILD_DEBUG)
		InitializeDebugLayer();
#endif
		InitializeD3D12Device();
		InitializeInfoQueue();
		CheckFeatureSupport();
		InitializeCommandQueues();
		InitializeMemAllocator();
		InitializeCommandLists();
		InitializeCommandAllocatorPools();

		m_IsInitialized = true;
	}

	void DX12Device::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		FlushGPU();

		for (const auto queueType : utils::EnumRange<CommandQueueType>())
		{
			auto& commandAllocatorPool = m_CommandAllocatorPools[utils::ToIndex(queueType)];
			commandAllocatorPool.reset();

			auto& commandQueue = m_CommandQueues[utils::ToIndex(queueType)];
			commandQueue.reset();
		}

		m_GraphicsCommandLists = {};
		m_ComputeCommandLists = {};

		m_MemAllocator.Reset();

		m_D3D12Device.Reset();
		m_DxgiAdapter.Reset();
		m_DxgiFactory.Reset();

		m_IsInitialized = false;
	}

	void DX12Device::FlushGPU() noexcept
	{
		for (const auto queueType : utils::EnumRange<CommandQueueType>())
		{
			auto& commandQueue = m_CommandQueues[utils::ToIndex(queueType)];
			commandQueue->FlushCommandQueue();
		}
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
		if (GetModuleHandle(L"WinPixGpuCapturer.dll") != 0)
		{
			return;
		}

		const auto dllPath = GetLatestWinPixGpuCapturerPath();
		if (!dllPath.empty())
		{
			LoadLibrary(dllPath.c_str());
		}

		PIXSetHUDOptions(PIX_HUD_SHOW_ON_NO_WINDOWS);
#endif
	}

	void DX12Device::InitializeDXGIFactory() noexcept
	{
		UINT createFactoryFlags = 0;

#if defined (BUILD_DEBUG)
		createFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

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
		DX12CommandQueue::CreateInfo createInfo{};
		createInfo.m_DX12Device = this;

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		m_CommandQueues[utils::ToIndex(CommandQueueType::Graphics)] = std::make_unique<DX12CommandQueue>(createInfo);
		m_CommandQueues[utils::ToIndex(CommandQueueType::Transfer)] = std::make_unique<DX12CommandQueue>(createInfo);	//d3dx12 upload resource uses direct type

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		m_CommandQueues[utils::ToIndex(CommandQueueType::Compute)] = std::make_unique<DX12CommandQueue>(createInfo);

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COPY;
		m_CommandQueues[utils::ToIndex(CommandQueueType::Copy)] = std::make_unique<DX12CommandQueue>(createInfo);
	}

	void DX12Device::InitializeCommandLists() noexcept
	{
		DX12CommandList::CreateInfo createInfo{};
		createInfo.m_DX12Device = this;

		for (int32_t i = 0; i < BufferCount; i++)
		{
			createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			m_GraphicsCommandLists[i] =
				std::make_unique<DX12CommandList>(createInfo);

			createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			m_ComputeCommandLists[i] =
				std::make_unique<DX12CommandList>(createInfo);
		}
	}

	void DX12Device::InitializeCommandAllocatorPools() noexcept
	{
		DX12CommandAllocator::CreateInfo createInfo{};
		createInfo.m_DX12Device = this;
		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		m_CommandAllocatorPools[utils::ToIndex(CommandQueueType::Graphics)] = std::make_unique<DX12CommandAllocatorPool>(createInfo);
		m_CommandAllocatorPools[utils::ToIndex(CommandQueueType::Transfer)] = std::make_unique<DX12CommandAllocatorPool>(createInfo);

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		m_CommandAllocatorPools[utils::ToIndex(CommandQueueType::Compute)] = std::make_unique<DX12CommandAllocatorPool>(createInfo);

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COPY;
		m_CommandAllocatorPools[utils::ToIndex(CommandQueueType::Copy)] = std::make_unique<DX12CommandAllocatorPool>(createInfo);
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

	void DX12Device::CheckFeatureSupport() noexcept
	{
		CD3DX12FeatureSupport featureSupport;
		featureSupport.Init(m_D3D12Device.Get());

		// RatTracing support
		m_FeatureSupport.m_RayTracingSupported = featureSupport.RaytracingTier() != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

		// MeshShader support
		m_FeatureSupport.m_MeshShaderSupported = featureSupport.MeshShaderTier() != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;

		// Enhanced Barrier
		m_FeatureSupport.m_EnhancedBarriers = featureSupport.EnhancedBarriersSupported();

		// Tearing support
		BOOL tearSupport = FALSE;
		if (m_DxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearSupport, sizeof(BOOL)) == S_OK)
		{
			m_FeatureSupport.m_TearingSupported = tearSupport;
		}
	}

	std::wstring DX12Device::GetLatestWinPixGpuCapturerPath() noexcept
	{
		LPWSTR programFilesPath = nullptr;
		if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath)))
		{
			return L"";
		}

		std::filesystem::path pixInstallationPath = programFilesPath;
		CoTaskMemFree(programFilesPath);

		pixInstallationPath /= "Microsoft PIX";

		if (!std::filesystem::exists(pixInstallationPath))
		{
			return L"";
		}

		std::wstring newestVersionFound;
		for (auto const& directoryEntry : std::filesystem::directory_iterator(pixInstallationPath))
		{
			if (directoryEntry.is_directory())
			{
				const auto name = directoryEntry.path().filename().wstring();
				if (newestVersionFound.empty() || newestVersionFound < name)
				{
					newestVersionFound = name;
				}
			}
		}

		if (newestVersionFound.empty())
		{
			return L"";
		}

		return (pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll");
	}
}