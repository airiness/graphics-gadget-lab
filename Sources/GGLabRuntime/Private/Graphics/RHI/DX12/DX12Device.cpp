#include "Graphics/RHI/DX12/DX12Device.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Platform/Win/HResult.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "Graphics/RHI/DX12/DX12QueueSystem.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Cache/DX12DescriptorCache.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/RHI/DX12/Utility/DX12PortabilityUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12TextureSupportUtils.h"
#include "Graphics/Utility/DXGIFormatUtils.h"

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#include <windows.h>
#include <shlobj.h>

#if defined(BUILD_DEBUG)
#include <dxgidebug.h>
#include <pix3.h>
#endif

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool SupportsMultisampling(
			ID3D12Device* device, DXGI_FORMAT format, uint16_t sampleCount) noexcept
		{
			D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS multisampleSupport{
				.Format = format,
				.SampleCount = sampleCount,
				.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,
			};
			return SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
				&multisampleSupport, sizeof(multisampleSupport))) &&
				multisampleSupport.NumQualityLevels > 0;
		}
	}

	DX12Device::DX12Device() noexcept = default;

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
		InitializeMemAllocator();
		m_ResourceManager.Initialize(this);

		m_IsInitialized = true;
	}

	void DX12Device::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		RetireCompletedRHIResources();
		SetDescriptorManager(nullptr);
		m_ResourceManager.Finalize();

		m_MemAllocator.Reset();

		m_D3D12Device.Reset();
		m_DxgiAdapter.Reset();
		m_AdapterCompatibilityIdentity.clear();
		m_AdapterName.clear();
		m_DxgiFactory.Reset();

		m_IsInitialized = false;
	}

	RHITextureSupportResult DX12Device::QueryTextureSupport(
		const RHITextureDesc& desc) const noexcept
	{
		const RHITextureValidationResult validation = ValidateRHITextureDesc(desc);
		if (!validation.IsValid())
		{
			return { .m_ValidationError = validation.m_Error };
		}
		if (!m_D3D12Device)
		{
			return { .m_Reason = RHITextureSupportReason::DeviceUnavailable };
		}

		D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{
			.Format = ToDXGIFormat(desc.m_Format),
		};
		if (FAILED(m_D3D12Device->CheckFeatureSupport(
			D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport))))
		{
			return { .m_Reason = RHITextureSupportReason::FormatSupportQueryFailed };
		}

		const RHIFormatInfo& formatInfo = GetRHIFormatInfo(desc.m_Format);
		const RHITextureSupportReason reason =
			EvaluateD3D12TextureFormatSupport(desc, formatSupport.Support1);
		if (reason != RHITextureSupportReason::None)
		{
			return { .m_Reason = reason };
		}

		if (desc.m_SampleCount > 1 && !formatInfo.m_IsTypeless)
		{
			if (!SupportsMultisampling(
				m_D3D12Device.Get(), ToDXGIFormat(desc.m_Format), desc.m_SampleCount))
			{
				return { .m_Reason = RHITextureSupportReason::MultisamplingUnsupported };
			}
		}

		return { .m_Supported = true };
	}

	RHITextureSupportResult DX12Device::QueryTextureViewSupport(
		const RHITextureDesc& textureDesc, const RHITextureViewDesc& viewDesc) const noexcept
	{
		const RHITextureValidationResult validation =
			ValidateRHITextureViewDesc(textureDesc, viewDesc);
		if (!validation.IsValid())
		{
			return { .m_ValidationError = validation.m_Error };
		}
		const RHIPortabilityValidationResult portability =
			ValidateRHITextureViewPortability(viewDesc, DX12PortabilityCapabilities);
		if (!portability.IsValid())
		{
			return { .m_PortabilityError = portability.m_Error };
		}
		const RHITextureSupportResult textureSupport = QueryTextureSupport(textureDesc);
		if (!textureSupport.IsSupported())
		{
			return textureSupport;
		}

		const RHIFormat viewFormat =
			viewDesc.m_Format == RHIFormat::Unknown ? textureDesc.m_Format : viewDesc.m_Format;
		D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{
			.Format = ToDXGIFormat(viewFormat),
		};
		if (FAILED(m_D3D12Device->CheckFeatureSupport(
			D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport))))
		{
			return { .m_Reason = RHITextureSupportReason::FormatSupportQueryFailed };
		}

		const RHITextureSupportReason reason = EvaluateD3D12TextureViewFormatSupport(
			textureDesc, viewDesc, formatSupport.Support1, formatSupport.Support2);
		if (reason != RHITextureSupportReason::None)
		{
			return { .m_Reason = reason };
		}
		if (textureDesc.m_SampleCount > 1 &&
			!SupportsMultisampling(
				m_D3D12Device.Get(), ToDXGIFormat(viewFormat), textureDesc.m_SampleCount))
		{
			return { .m_Reason = RHITextureSupportReason::MultisamplingUnsupported };
		}

		return { .m_Supported = true };
	}

	RHITextureHandle DX12Device::CreateTexture(const RHIOwnedTextureCreateInfo& createInfo,
		const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		return m_ResourceManager.CreateTexture(createInfo, debugIdentity);
	}

	RHIBufferHandle DX12Device::CreateBuffer(
		const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		return m_ResourceManager.CreateBuffer(desc, debugIdentity);
	}

	RHITextureHandle DX12Device::ImportTexture(
		const DX12ResourceManager::ImportedTextureDesc& desc) noexcept
	{
		return m_ResourceManager.ImportTexture(desc);
	}

	RHIBufferHandle DX12Device::ImportBuffer(
		const DX12ResourceManager::ImportedBufferDesc& desc) noexcept
	{
		return m_ResourceManager.ImportBuffer(desc);
	}

	RHITextureViewHandle DX12Device::CreateTextureView(
		RHITextureHandle texture, const RHITextureViewDesc& desc) noexcept
	{
		if (!m_DescriptorCache)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12Device::CreateTextureView called without a DX12DescriptorCache.");
			return {};
		}

		return m_DescriptorCache->GetOrCreateTextureView(texture, desc);
	}

	RHIBufferViewHandle DX12Device::CreateBufferView(
		RHIBufferHandle buffer, const RHIBufferViewDesc& desc) noexcept
	{
		if (!m_DescriptorCache)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12Device::CreateBufferView called without a DX12DescriptorCache.");
			return {};
		}

		return m_DescriptorCache->GetOrCreateBufferView(buffer, desc);
	}

	RHISamplerHandle DX12Device::CreateSampler(const RHISamplerDesc& desc) noexcept
	{
		if (!ValidateRHISamplerPortability(desc, DX12PortabilityCapabilities).IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12Device::CreateSampler rejected a non-portable sampler.");
			return {};
		}
		if (!m_DescriptorCache)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12Device::CreateSampler called without a DX12DescriptorCache.");
			return {};
		}

		return m_DescriptorCache->GetOrCreateSampler(desc);
	}

	void DX12Device::DestroyTexture(RHITextureHandle texture) noexcept
	{
		m_ResourceManager.DestroyTexture(texture);
	}

	void DX12Device::DestroyBuffer(RHIBufferHandle buffer) noexcept
	{
		m_ResourceManager.DestroyBuffer(buffer);
	}

	void DX12Device::DestroyTextureView(RHITextureViewHandle view) noexcept
	{
		if (!m_DescriptorCache)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12Device::DestroyTextureView called without a DX12DescriptorCache.");
			return;
		}

		m_DescriptorCache->DestroyTextureView(view);
	}

	void DX12Device::DestroyBufferView(RHIBufferViewHandle view) noexcept
	{
		if (!m_DescriptorCache)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12Device::DestroyBufferView called without a DX12DescriptorCache.");
			return;
		}

		m_DescriptorCache->DestroyBufferView(view);
	}

	void DX12Device::DestroySampler(RHISamplerHandle sampler) noexcept
	{
		if (!m_DescriptorCache)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12Device::DestroySampler called without a DX12DescriptorCache.");
			return;
		}

		m_DescriptorCache->DestroySampler(sampler);
	}

	void DX12Device::SetTextureDebugBinding(
		RHITextureHandle texture, const RHIResourceDebugBindingDesc& binding) noexcept
	{
		m_ResourceManager.SetTextureDebugBinding(texture, binding);
	}

	void DX12Device::SetBufferDebugBinding(
		RHIBufferHandle buffer, const RHIResourceDebugBindingDesc& binding) noexcept
	{
		m_ResourceManager.SetBufferDebugBinding(buffer, binding);
	}

	std::string_view DX12Device::GetTextureDebugName(RHITextureHandle texture) const noexcept
	{
		return m_ResourceManager.GetTextureDebugName(texture);
	}

	std::string_view DX12Device::GetBufferDebugName(RHIBufferHandle buffer) const noexcept
	{
		return m_ResourceManager.GetBufferDebugName(buffer);
	}

	void* DX12Device::MapBuffer(RHIBufferHandle buffer, RHIMappedBufferRange readRange) noexcept
	{
		auto* nativeBuffer = ResolveBuffer(buffer);
		if (!nativeBuffer)
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12Device::MapBuffer received a non-live buffer handle.");
			return nullptr;
		}
		GGLAB_ASSERT(readRange.m_Begin <= readRange.m_End);
		GGLAB_ASSERT(readRange.m_End <= nativeBuffer->SizeInBytes());
		const D3D12_RANGE dx12ReadRange{
			static_cast<SIZE_T>(readRange.m_Begin),
			static_cast<SIZE_T>(readRange.m_End),
		};
		return nativeBuffer->Map(0, &dx12ReadRange);
	}

	void DX12Device::UnmapBuffer(RHIBufferHandle buffer, RHIMappedBufferRange writtenRange) noexcept
	{
		if (auto* nativeBuffer = ResolveBuffer(buffer))
		{
			GGLAB_ASSERT(writtenRange.m_Begin <= writtenRange.m_End);
			GGLAB_ASSERT(writtenRange.m_End <= nativeBuffer->SizeInBytes());
			const D3D12_RANGE dx12WrittenRange{
				static_cast<SIZE_T>(writtenRange.m_Begin),
				static_cast<SIZE_T>(writtenRange.m_End),
			};
			nativeBuffer->Unmap(0, &dx12WrittenRange);
		}
	}

	uint32_t DX12Device::GetBufferViewAlignment(RHIBufferViewType viewType) const noexcept
	{
		return viewType == RHIBufferViewType::ConstantBuffer
			? D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
			: 1u;
	}

	bool DX12Device::IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept
	{
		return m_QueueSystem ? m_QueueSystem->IsFencePointCompleted(fencePoint)
			: !fencePoint.IsValid();
	}

	void DX12Device::RecordTextureUse(
		RHITextureHandle texture, const DX12FencePoint& fencePoint) noexcept
	{
		RecordTextureUse(texture, fencePoint.ToRHI());
	}

	void DX12Device::RecordTextureUse(
		RHITextureHandle texture, const RHIFencePoint& fencePoint) noexcept
	{
		m_ResourceManager.RecordTextureUse(texture, fencePoint);
	}

	void DX12Device::RecordBufferUse(
		RHIBufferHandle buffer, const DX12FencePoint& fencePoint) noexcept
	{
		RecordBufferUse(buffer, fencePoint.ToRHI());
	}

	void DX12Device::RecordBufferUse(
		RHIBufferHandle buffer, const RHIFencePoint& fencePoint) noexcept
	{
		m_ResourceManager.RecordBufferUse(buffer, fencePoint);
	}

	void DX12Device::SetDescriptorManager(DX12DescriptorManager* descriptorManager) noexcept
	{
		if (m_DescriptorManager == descriptorManager)
		{
			return;
		}

		m_ResourceManager.SetDescriptorCache(nullptr);
		m_DescriptorCache.reset();
		m_DescriptorManager = descriptorManager;
		if (!m_DescriptorManager)
		{
			return;
		}

		DX12DescriptorCache::CreateInfo createInfo{};
		createInfo.m_DX12Device = this;
		createInfo.m_DescriptorManager = m_DescriptorManager;
		m_DescriptorCache = std::make_unique<DX12DescriptorCache>(createInfo);
		m_ResourceManager.SetDescriptorCache(m_DescriptorCache.get());
	}

	bool DX12Device::IsAlive(RHITextureHandle texture) const noexcept
	{
		return m_ResourceManager.IsAlive(texture);
	}

	bool DX12Device::IsAlive(RHIBufferHandle buffer) const noexcept
	{
		return m_ResourceManager.IsAlive(buffer);
	}

	bool DX12Device::IsAlive(RHISamplerHandle sampler) const noexcept
	{
		return m_DescriptorCache && m_DescriptorCache->IsSamplerAlive(sampler);
	}

	RHIDescriptorHandle DX12Device::GetTextureViewDescriptor(
		RHITextureViewHandle view) const noexcept
	{
		if (!m_DescriptorCache)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12Device::GetTextureViewDescriptor called without a DX12DescriptorCache.");
			return {};
		}

		return m_DescriptorCache->ResolveTextureViewDescriptor(view);
	}

	RHIDescriptorHandle DX12Device::GetBufferViewDescriptor(RHIBufferViewHandle view) const noexcept
	{
		if (!m_DescriptorCache)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12Device::GetBufferViewDescriptor called without a DX12DescriptorCache.");
			return {};
		}

		return m_DescriptorCache->ResolveBufferViewDescriptor(view);
	}

	RHIDescriptorHandle DX12Device::GetSamplerDescriptor(RHISamplerHandle sampler) const noexcept
	{
		if (!m_DescriptorCache)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12Device::GetSamplerDescriptor called without a DX12DescriptorCache.");
			return {};
		}

		return m_DescriptorCache->ResolveSamplerDescriptor(sampler);
	}

	DX12DescriptorView DX12Device::ResolveTextureView(RHITextureViewHandle view) const noexcept
	{
		return m_DescriptorCache ? m_DescriptorCache->ResolveTextureView(view)
			: DX12DescriptorView{};
	}

	bool DX12Device::ResolveTextureViewInfo(RHITextureViewHandle view,
		DX12DescriptorView& descriptor, RHITextureViewKey& key) const noexcept
	{
		return m_DescriptorCache && m_DescriptorCache->ResolveTextureViewInfo(view, descriptor, key);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE DX12Device::ResolveShaderVisibleDescriptor(
		RHIDescriptorHeapType heapType, uint32_t descriptorIndex) const noexcept
	{
		if (!m_DescriptorManager)
		{
			return {};
		}

		DX12DescriptorManager::HeapType nativeHeapType{};
		switch (heapType)
		{
		case RHIDescriptorHeapType::CbvSrvUav:
			nativeHeapType = DX12DescriptorManager::HeapType::CbvSrvUav;
			break;
		case RHIDescriptorHeapType::Sampler:
			nativeHeapType = DX12DescriptorManager::HeapType::Sampler;
			break;
		default:
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12Device::ResolveShaderVisibleDescriptor received a non-shader-visible heap type.");
			return {};
		}

		DX12DescriptorHeap* heap = m_DescriptorManager->GetHeap(nativeHeapType);
		if (!heap || descriptorIndex >= heap->DescriptorCount())
		{
			return {};
		}
		return heap->GpuHandleAt(descriptorIndex);
	}

	DX12Texture* DX12Device::ResolveTexture(RHITextureHandle texture) noexcept
	{
		return m_ResourceManager.ResolveTexture(texture);
	}

	const DX12Texture* DX12Device::ResolveTexture(RHITextureHandle texture) const noexcept
	{
		return m_ResourceManager.ResolveTexture(texture);
	}

	DX12Buffer* DX12Device::ResolveBuffer(RHIBufferHandle buffer) noexcept
	{
		return m_ResourceManager.ResolveBuffer(buffer);
	}

	const DX12Buffer* DX12Device::ResolveBuffer(RHIBufferHandle buffer) const noexcept
	{
		return m_ResourceManager.ResolveBuffer(buffer);
	}

	void DX12Device::RetireCompletedWork() noexcept
	{
		RetireCompletedRHIResources();
	}

	void DX12Device::RetireCompletedRHIResources() noexcept
	{
		if (m_DescriptorCache)
		{
			m_DescriptorCache->GarbageCollect();
		}

		m_ResourceManager.RetireCompletedResources();
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

#if defined(BUILD_DEBUG)
		createFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

		ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue))))
		{
			dxgiInfoQueue->SetBreakOnSeverity(
				DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
			dxgiInfoQueue->SetBreakOnSeverity(
				DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
		}
#endif
		ComPtr<IDXGIFactory7> dxgiFactory;
		GGLAB_HR(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

		m_DxgiFactory = dxgiFactory;
	}

	void DX12Device::InitializeDXGIAdapter() noexcept
	{
		ComPtr<IDXGIAdapter1> dxgiAdapter;
		for (UINT i = 0;
			m_DxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(&dxgiAdapter)) != DXGI_ERROR_NOT_FOUND;
			++i)
		{
			DXGI_ADAPTER_DESC1 desc = {};
			dxgiAdapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				continue;
			}

			if (SUCCEEDED(D3D12CreateDevice(
				dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
			{
				m_DxgiAdapter = dxgiAdapter;
				break;
			}
		}

		GGLAB_ASSERT_MSG(m_DxgiAdapter != nullptr, "Create DxgiAdapter failed.");
		if (m_DxgiAdapter)
		{
			DXGI_ADAPTER_DESC1 desc{};
			LARGE_INTEGER driverVersion{};
			GGLAB_HR(m_DxgiAdapter->GetDesc1(&desc));
			m_AdapterName = utils::ToString(desc.Description);
			GGLAB_UNUSED(
				m_DxgiAdapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion));
			m_AdapterCompatibilityIdentity = std::format(
				"dx12:{:08x}:{:08x}:{:08x}:{:08x}:driver-{:016x}", desc.VendorId, desc.DeviceId,
				desc.SubSysId, desc.Revision, static_cast<uint64_t>(driverVersion.QuadPart));
		}
	}

	void DX12Device::InitializeD3D12Device() noexcept
	{
		// Feature check and Create Device
		constexpr D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };

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
			D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
			// ImGui's vendored DX12 backend creates its internal upload buffers through
			// legacy CreateCommittedResource. On an enhanced-barrier device the initial
			// state is intentionally ignored. Project-owned resources use CreateResource3.
			D3D12_MESSAGE_ID denyIds[] = {
				D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED,
			};
			D3D12_INFO_QUEUE_FILTER filter = {};
			//filter.DenyList.NumCategories = _countof(categories);
			//filter.DenyList.pCategoryList = categories;
			filter.DenyList.NumSeverities = _countof(severities);
			filter.DenyList.pSeverityList = severities;
			filter.DenyList.NumIDs = _countof(denyIds);
			filter.DenyList.pIDList = denyIds;
			GGLAB_HR(infoQueue->PushStorageFilter(&filter));
		}
#endif
	}

	void DX12Device::InitializeMemAllocator() noexcept
	{
		using namespace D3D12MA;
		D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
		allocatorDesc.Flags =
			static_cast<D3D12MA::ALLOCATOR_FLAGS>(D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS);
		allocatorDesc.pDevice = m_D3D12Device.Get();
		allocatorDesc.pAdapter = m_DxgiAdapter.Get();
		GGLAB_HR(D3D12MA::CreateAllocator(&allocatorDesc, &m_MemAllocator));
	}

	void DX12Device::CheckFeatureSupport() noexcept
	{
		CD3DX12FeatureSupport featureSupport;
		featureSupport.Init(m_D3D12Device.Get());

		// RatTracing support
		m_FeatureSupport.m_RayTracingSupported =
			featureSupport.RaytracingTier() != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

		// MeshShader support
		m_FeatureSupport.m_MeshShaderSupported =
			featureSupport.MeshShaderTier() != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;

		// Enhanced Barrier
		m_FeatureSupport.m_EnhancedBarriers = featureSupport.EnhancedBarriersSupported();

		D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
		if (SUCCEEDED(m_D3D12Device->CheckFeatureSupport(
			D3D12_FEATURE_D3D12_OPTIONS1, std::addressof(options1), sizeof(options1))))
		{
			m_FeatureSupport.m_ShaderWaveCapabilities = {
				.m_Supported = options1.WaveOps != FALSE,
				.m_MinLaneCount = options1.WaveLaneCountMin,
				.m_MaxLaneCount = options1.WaveLaneCountMax,
			};
		}

		// Tearing support
		BOOL tearSupport = FALSE;
		if (m_DxgiFactory->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearSupport, sizeof(BOOL)) == S_OK)
		{
			m_FeatureSupport.m_TearingSupported = tearSupport;
		}
	}

	std::wstring DX12Device::GetLatestWinPixGpuCapturerPath() noexcept
	{
		LPWSTR programFilesPath = nullptr;
		if (FAILED(SHGetKnownFolderPath(
			FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath)))
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
