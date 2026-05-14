#pragma once
#include "GraphicsTypes.h"
#include "FNV1a.h"
#include "TypeUtils.h"

namespace gglab
{
	class DX12DescriptorManager;

	enum class SamplerPreset : uint8_t
	{
		PointClamp,
		PointWrap,

		LinearClamp,
		LinearWrap,

		AnisotropicClamp,
		AnisotropicWrap,

		ShadowCmpLinearClamp,

		Count
	};

	struct SamplerKey
	{
		D3D12_FILTER m_Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

		D3D12_TEXTURE_ADDRESS_MODE m_AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		D3D12_TEXTURE_ADDRESS_MODE m_AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		D3D12_TEXTURE_ADDRESS_MODE m_AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

		float m_MipLODBias = 0.0f;
		uint32_t m_MaxAnisotropy = 1;

		D3D12_COMPARISON_FUNC m_ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		float m_BorderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		float m_MinLOD = 0.0f;
		float m_MaxLOD = D3D12_FLOAT32_MAX;

		constexpr bool operator==(const SamplerKey&) const noexcept = default;

		constexpr auto AsTuple() const noexcept
		{
			return std::tuple{
				m_Filter,
				m_AddressU,
				m_AddressV,
				m_AddressW,
				m_MipLODBias,
				m_MaxAnisotropy,
				m_ComparisonFunc,
				m_BorderColor[0],
				m_BorderColor[1],
				m_BorderColor[2],
				m_BorderColor[3],
				m_MinLOD,
				m_MaxLOD
			};
		}
	};
	using SamplerKeyHash = KeyHash<SamplerKey>;

	class SamplerRegistry
	{
	public:
		struct CreateInfo
		{
			DX12DescriptorManager* m_DescriptorManager = nullptr;
		};

	private:
		struct SamplerEntry
		{
			SamplerID m_SamplerId{};
			SamplerKey m_Key{};
			DX12DescriptorID m_DescriptorId{};
		};

	public:
		explicit SamplerRegistry(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(SamplerRegistry);
		~SamplerRegistry() = default;

		void InitializePresetSamplers() noexcept;

		SamplerID GetOrCreateSampler(const SamplerKey& key) noexcept;
		SamplerID GetOrCreateSampler(const D3D12_SAMPLER_DESC& samplerDesc) noexcept;
		SamplerID GetPresetSamplerId(SamplerPreset preset) const noexcept;

		uint32_t GetSamplerIndex(SamplerPreset preset) const noexcept;
		uint32_t GetSamplerIndex(const SamplerID& samplerId) const noexcept;

		D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerGpuHandle(SamplerPreset preset) const noexcept;
		D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerGpuHandle(const SamplerID& samplerId) const noexcept;

		const SamplerKey& GetSamplerKey(const SamplerID& samplerId) const noexcept;

		uint32_t ResolveSamplerIndex(const SamplerID& samplerId, SamplerPreset fallbackPreset) const noexcept;

	public:
		static SamplerKey MakeSamplerKey(const D3D12_SAMPLER_DESC& samplerDesc) noexcept;
		static D3D12_SAMPLER_DESC MakeSamplerDesc(const SamplerKey& key) noexcept;

	private:
		const SamplerEntry& GetEntry(const SamplerID& samplerId) const noexcept;
		SamplerEntry& GetEntry(const SamplerID& samplerId) noexcept;

	private:
		static D3D12_SAMPLER_DESC MakeBaseSamplerDesc() noexcept;
		static D3D12_SAMPLER_DESC MakePresetSamplerDesc(SamplerPreset preset) noexcept;

	private:
		DX12DescriptorManager* m_DescriptorManager = nullptr;

		SamplerIDCounter m_SamplerIdCounter{};

		std::unordered_map<SamplerKey, SamplerID, SamplerKeyHash> m_SamplerMap;
		std::unordered_map<SamplerID, SamplerEntry> m_SamplerEntries;

		std::array<SamplerID, utils::EnumCount<SamplerPreset>()> m_PresetSamplers{};
		bool m_PresetSamplersInitialized = false;
	};
}