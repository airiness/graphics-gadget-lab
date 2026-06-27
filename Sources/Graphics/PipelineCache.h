#pragma once
#include "Graphics/RHI/DX12/Cache/PipelineDesc.h"

namespace gglab
{
	class DX12PipelineState;
	class DX12Device;
	class DX12PSOCache;
	class DX12RootSignatureCache;
	class ShaderManager;

	struct GraphicsPipelineRecipe
	{
		RootSignatureID m_RootSignatureId{};
		InputLayoutID m_InputLayoutId{};

		ShaderID m_VSId{};
		ShaderID m_PSId{};
		ShaderID m_DSId{};
		ShaderID m_HSId{};
		ShaderID m_GSId{};

		PipelineFormats m_Formats{};

		D3D12_PRIMITIVE_TOPOLOGY_TYPE m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		uint32_t m_SampleMask = std::numeric_limits<uint32_t>::max();

		RasterizerPreset m_RasterizerPreset = RasterizerPreset::Default;
		DepthPreset m_DepthPreset = DepthPreset::Default;
		BlendPreset m_BlendPreset = BlendPreset::Default;

		int32_t m_DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		float m_DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		float m_SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;

		constexpr bool operator==(const GraphicsPipelineRecipe&) const noexcept = default;
		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_RootSignatureId.Value(),
				m_InputLayoutId,
				m_VSId.Value(),
				m_PSId.Value(),
				m_DSId.Value(),
				m_HSId.Value(),
				m_GSId.Value(),
				m_Formats.AsTuple(),
				m_Topology,
				m_SampleMask,
				m_RasterizerPreset,
				m_DepthPreset,
				m_BlendPreset,
				m_DepthBias,
				m_DepthBiasClamp,
				m_SlopeScaledDepthBias);
		}
	};

	struct ComputePipelineRecipe
	{
		RootSignatureID m_RootSignatureId{};
		ShaderID m_CSId{};

		constexpr bool operator==(const ComputePipelineRecipe&) const noexcept = default;
		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_RootSignatureId.Value(),
				m_CSId.Value());
		}
	};

	struct GraphicsPipelineSlot
	{
	public:
		void Reset() noexcept { *this = {}; }

	private:
		friend class PipelineCache;

		GraphicsPipelineRecipe m_Recipe{};
		uint64_t m_ShaderRevision = 0;
		DX12PipelineState* m_PipelineState = nullptr;
		uint64_t m_PipelineCacheId = 0;
		uint64_t m_PSOCacheRevision = 0;
	};

	struct ComputePipelineSlot
	{
	public:
		void Reset() noexcept { *this = {}; }

	private:
		friend class PipelineCache;

		ComputePipelineRecipe m_Recipe{};
		uint64_t m_ShaderRevision = 0;
		DX12PipelineState* m_PipelineState = nullptr;
		uint64_t m_PipelineCacheId = 0;
		uint64_t m_PSOCacheRevision = 0;
	};

	class PipelineCache
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_Device = nullptr;
			ShaderManager* m_ShaderManager = nullptr;
			DX12RootSignatureCache* m_RootSignatureCache = nullptr;
			DX12PSOCache* m_PSOCache = nullptr;
		};

		explicit PipelineCache(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(PipelineCache);
		~PipelineCache() = default;

		RHIPipelineHandle Resolve(
			GraphicsPipelineSlot& slot,
			const GraphicsPipelineRecipe& recipe) noexcept;
		DX12PipelineState* Resolve(
			ComputePipelineSlot& slot,
			const ComputePipelineRecipe& recipe) noexcept;

	private:
		ComputePipelineDesc BuildComputeDesc(
			const ComputePipelineRecipe& recipe) const noexcept;

		ShaderManager* m_ShaderManager = nullptr;
		DX12Device* m_Device = nullptr;
		DX12RootSignatureCache* m_RootSignatureCache = nullptr;
		DX12PSOCache* m_PSOCache = nullptr;
		uint64_t m_CacheId = 0;
	};
}
