#pragma once
#include "StringId.h"
#include "PSOKey.h"
#include "PipelineDesc.h"
#include "ShaderManager.h"

namespace gglab
{
	struct FormatStamp
	{
		uint32_t m_RtvCount = 0;
		DXGI_FORMAT m_Rtv[8]{};
		DXGI_FORMAT m_Dsv = DXGI_FORMAT_UNKNOWN;
		uint32_t m_SampleCount = 1;
		uint32_t m_SampleQuality = 0;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_RtvCount, m_Rtv[0], m_Rtv[1], m_Rtv[2], m_Rtv[3],
				m_Rtv[4], m_Rtv[5], m_Rtv[6], m_Rtv[7],
				m_Dsv, m_SampleCount, m_SampleQuality);
		}

		bool operator==(const FormatStamp& rhs) const noexcept
		{
			return AsTuple() == rhs.AsTuple();
		}
	};

	struct GraphicsKeyInputs
	{
		RootSignatureId m_RootSignatureId{};

		ShaderId m_VSId{};
		ShaderId m_PSId{};
		ShaderId m_DSId{};
		ShaderId m_HSId{};
		ShaderId m_GSId{};

		uint64_t m_VSGen = 0;
		uint64_t m_PSGen = 0;
		uint64_t m_DSGen = 0;
		uint64_t m_HSGen = 0;
		uint64_t m_GSGen = 0;

		FormatStamp m_Formats{};
		D3D12_PRIMITIVE_TOPOLOGY_TYPE m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		PackedRasterizer m_Rasterizer{};
		PackedDepth m_Depth{};
		PackedBlend m_Blend{};

		uint32_t m_SampleMask = std::numeric_limits<uint32_t>::max();
		uint64_t m_VariantBits = 0;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_RootSignatureId.Value(),
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
				m_Rasterizer.m_Bits,
				m_Depth.m_Bits,
				m_Blend.m_Bits,
				m_SampleMask,
				m_VariantBits);
		}

		bool operator==(const GraphicsKeyInputs& rhs) const noexcept
		{
			return AsTuple() == rhs.AsTuple();
		}
	};

	struct ComputeKeyInputs
	{
		RootSignatureId m_RootSignatureId{};
		ShaderId m_CSId{};
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

		size_t InvalidateByShader(const std::vector<ShaderId>& shaderIds) noexcept;

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
		std::unordered_map<StringId, GraphicsEntry> m_GraphicsMap;
		std::unordered_map<StringId, ComputeEntry> m_ComputeMap;

		mutable std::shared_mutex m_Mutex;
	};
}