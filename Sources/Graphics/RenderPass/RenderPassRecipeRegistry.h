#pragma once
#include "Core/StringId.h"
#include "Graphics/DX12/Cache/PSOKey.h"
#include "Graphics/DX12/Cache/PipelineDesc.h"
#include "Graphics/ShaderManager.h"

namespace gglab
{
	struct GraphicsKeyInputs
	{
		RootSignatureID m_RootSignatureId{};
		InputLayoutID m_InputLayoutId{};

		ShaderID m_VSId{};
		ShaderID m_PSId{};
		ShaderID m_DSId{};
		ShaderID m_HSId{};
		ShaderID m_GSId{};

		uint64_t m_VSGen = 0;
		uint64_t m_PSGen = 0;
		uint64_t m_DSGen = 0;
		uint64_t m_HSGen = 0;
		uint64_t m_GSGen = 0;

		PipelineFormats m_Formats{};

		D3D12_PRIMITIVE_TOPOLOGY_TYPE m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		uint32_t m_SampleMask = std::numeric_limits<uint32_t>::max();

		RasterizerPreset m_RasterizerPreset = RasterizerPreset::Default;
		DepthPreset m_DepthPreset = DepthPreset::Default;
		BlendPreset m_BlendPreset = BlendPreset::Default;

		uint64_t m_VariantBits = 0;

		constexpr bool operator==(const GraphicsKeyInputs&) const noexcept = default;
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
				m_VSGen,
				m_PSGen,
				m_DSGen,
				m_HSGen,
				m_GSGen,
				m_Formats.AsTuple(),
				m_Topology,		
				m_SampleMask,
				m_RasterizerPreset,
				m_DepthPreset,
				m_BlendPreset,
				m_VariantBits);
		}
	};

	struct ComputeKeyInputs
	{
		RootSignatureID m_RootSignatureId{};
		ShaderID m_CSId{};
		uint64_t m_CSGen = 0;
		uint64_t m_VariantBits = 0;

		constexpr bool operator==(const ComputeKeyInputs&) const noexcept = default;
		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_RootSignatureId.Value(),
				m_CSId.Value(),
				m_CSGen,
				m_VariantBits);
		}
	};

	using GraphicsBuilder = std::function<void(GraphicsPipelineDesc& out, const GraphicsKeyInputs& input, ShaderManager* shaderManager)>;
	using ComputeBuilder = std::function<void(ComputePipelineDesc& out, const ComputeKeyInputs& input, ShaderManager* shaderManager)>;

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
		explicit RenderPassRecipeRegistry(ShaderManager* shaderManager) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderPassRecipeRegistry);
		~RenderPassRecipeRegistry() = default;

		const CachedGraphics& GetOrCreateGraphics(std::string_view passId, const GraphicsKeyInputs& input, const GraphicsBuilder& builder) noexcept;
		const CachedCompute& GetOrCreateCompute(std::string_view passId, const ComputeKeyInputs& input, const ComputeBuilder& builder) noexcept;

		void Clear() noexcept;
		bool EraseGraphics(std::string_view passId) noexcept;
		bool EraseCompute(std::string_view passId) noexcept;

		size_t InvalidateByShader(const std::vector<ShaderID>& shaderIds) noexcept;

	private:
		struct GraphicsEntry
		{
			size_t m_Hash = 0;
			CachedGraphics m_Cached;
			GraphicsKeyInputs m_Inputs{};
		};

		struct ComputeEntry
		{
			size_t m_Hash = 0;
			CachedCompute m_Cached;
			ComputeKeyInputs m_Inputs{};
		};

		ShaderManager* m_ShaderManager = nullptr;
		std::unordered_map<StringID, GraphicsEntry> m_GraphicsMap;
		std::unordered_map<StringID, ComputeEntry> m_ComputeMap;

		mutable std::shared_mutex m_Mutex;
	};
}