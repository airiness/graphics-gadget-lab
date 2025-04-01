#include "Precompiled.h"
#include "DX12Device.h"
#include "Application.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12Device::DX12Device() noexcept
	{
	}

	DX12Device::~DX12Device() noexcept
	{
	}

	void DX12Device::Initialize() noexcept
	{
		InitializeDXGIFactory();
		InitializeDXGIAdapter();
	}

	void DX12Device::Update() noexcept
	{
	}

	void DX12Device::Finalize() noexcept
	{
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

		// Disable Alt+Enter
		auto* application = Application::Get();
		auto hWnd = application->GetHwnd();
		utility::ThrowIfFailed(dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES));

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

		ASSERT_MSG(m_DxgiAdapter != nullptr, "Create DxgiAdapter failed.");
	}

	void DX12Device::InitializeD3D12Device() noexcept
	{
		// Validate Debug Layer
#if defined(BUILD_DEBUG)
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
#endif

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

		// Check feature support
		CheckFeatureSupport();
	}

	void DX12Device::CheckFeatureSupport() noexcept
	{
		// RatTracing support
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
		if (SUCCEEDED(m_D3D12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
		{
			m_RayTracingSupported = (options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED);
		}

		// MeshShader support
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
		if (SUCCEEDED(m_D3D12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
		{
			m_MeshShaderSupported = (options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED);
		}

		// VRS, Sampler feedback?
	}
}