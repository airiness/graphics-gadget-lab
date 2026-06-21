#pragma once
#include "Core/StringId.h"
#include "Graphics/DX12/Cache/PSOKey.h"
#include "Graphics/DX12/Cache/PipelineDesc.h"
#include "Graphics/ShaderManager.h"

namespace gglab
{
	class DX12RootSignatureCache;

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

	struct CachedGraphics
	{
		GraphicsPSOKey m_Key{};
		GraphicsPipelineDesc m_Desc{};
	};

	struct CachedCompute
	{
		ComputePSOKey m_Key{};
		ComputePipelineDesc m_Desc{};
	};

	class RenderPassRecipeRegistry
	{
	public:
		struct CreateInfo
		{
			ShaderManager* m_ShaderManager = nullptr;
			DX12RootSignatureCache* m_RootSignatureCache = nullptr;
		};

		explicit RenderPassRecipeRegistry(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderPassRecipeRegistry);
		~RenderPassRecipeRegistry() = default;

		const CachedGraphics& GetOrCreateGraphics(
			std::string_view passId,
			const GraphicsPipelineRecipe& recipe) noexcept;
		const CachedCompute& GetOrCreateCompute(
			std::string_view passId,
			const ComputePipelineRecipe& recipe) noexcept;

		void Clear() noexcept;
		bool EraseGraphics(std::string_view passId) noexcept;
		bool EraseCompute(std::string_view passId) noexcept;

		size_t InvalidateByShader(const std::vector<ShaderID>& shaderIds) noexcept;

	private:
		struct GraphicsShaderGenerations
		{
			uint64_t m_VS = 0;
			uint64_t m_PS = 0;
			uint64_t m_DS = 0;
			uint64_t m_HS = 0;
			uint64_t m_GS = 0;

			constexpr bool operator==(const GraphicsShaderGenerations&) const noexcept = default;
		};

		struct GraphicsEntry
		{
			CachedGraphics m_Cached;
			GraphicsPipelineRecipe m_Recipe{};
			GraphicsShaderGenerations m_ShaderGenerations{};
		};

		struct ComputeEntry
		{
			CachedCompute m_Cached;
			ComputePipelineRecipe m_Recipe{};
			uint64_t m_ShaderGeneration = 0;
		};

		GraphicsShaderGenerations GetShaderGenerations(
			const GraphicsPipelineRecipe& recipe) const noexcept;
		GraphicsPipelineDesc BuildGraphicsDesc(
			const GraphicsPipelineRecipe& recipe) const noexcept;
		ComputePipelineDesc BuildComputeDesc(
			const ComputePipelineRecipe& recipe) const noexcept;

		ShaderManager* m_ShaderManager = nullptr;
		DX12RootSignatureCache* m_RootSignatureCache = nullptr;
		std::unordered_map<StringID, GraphicsEntry> m_GraphicsMap;
		std::unordered_map<StringID, ComputeEntry> m_ComputeMap;

		mutable std::shared_mutex m_Mutex;
	};
}
