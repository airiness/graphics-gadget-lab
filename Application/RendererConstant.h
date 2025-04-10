#pragma once
namespace graphicsGadgetLab
{
	class DX12RootSifnature;
	class DX12PipelinseState;

	enum class CommonRSRootParamIndex : uint32_t
	{
		ConstantBufferIndex,
		TextureDescriptorTable,
		RootParamCount
	};

	enum class RootSignatureIndex : uint32_t
	{
		CommonRootSignature = 0,
		RootSignatureCount
	};
	using RootSignatureArray = std::array<std::unique_ptr<DX12RootSignature>, static_cast<size_t>(RootSignatureIndex::RootSignatureCount)>;

	enum class PSOIndex : uint32_t
	{
		NormalTexturePSO = 0,
		PSOCount
	};
	using PSOArray = std::array<std::unique_ptr<DX12PipelinseState>, static_cast<size_t>(PSOIndex::PSOCount)>;
}