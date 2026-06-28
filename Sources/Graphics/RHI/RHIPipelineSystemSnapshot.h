#pragma once
#include "Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/RenderPass/RenderPassInfo.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gglab
{
	class DX12PipelineSystem;
	class PipelineCache;

	enum class RHIPipelineSnapshotType : uint8_t
	{
		Graphics,
		Compute,
	};

	enum class RHIBindingBackendMapping : uint8_t
	{
		None,
		RootParameter,
		DirectlyIndexed,
	};

	struct RHIBindingSlotSnapshot
	{
		uint32_t m_Slot = 0;
		RHIBindingType m_Type = RHIBindingType::Unknown;
		RHIShaderStage m_Visibility = RHIShaderStage::None;
		uint32_t m_Binding = 0;
		uint32_t m_Space = 0;
		uint32_t m_Count = 1;
		uint32_t m_SizeInBytes = 0;
		std::string m_DebugName;
		RHIBindingBackendMapping m_BackendMapping = RHIBindingBackendMapping::None;
		int32_t m_RootParameter = -1;
		std::string m_BackendBindingType;
	};

	struct RHIBindingLayoutSnapshot
	{
		RHIBindingLayoutHandle m_Handle{};
		bool m_Alive = false;
		std::string m_DebugName;
		std::vector<RHIBindingSlotSnapshot> m_Slots;
		uint32_t m_RootParameterCount = 0;
		uint32_t m_BackendRootSignatureId = 0;
		uint64_t m_BackendRootSignaturePointer = 0;
		bool m_DirectlyIndexedResources = false;
		bool m_DirectlyIndexedSamplers = false;
	};

	struct RHIShaderSnapshot
	{
		RHIShaderHandle m_Handle{};
		ShaderHash128 m_Hash{};
		std::string m_DebugName;
		bool m_Present = false;
	};

	struct RHIVertexAttributeSnapshot
	{
		std::string m_SemanticName;
		uint32_t m_SemanticIndex = 0;
		RHIFormat m_Format = RHIFormat::Unknown;
		uint32_t m_InputSlot = 0;
		uint32_t m_AlignedByteOffset = 0;
	};

	struct RHIPipelineSnapshot
	{
		RHIPipelineHandle m_Handle{};
		RHIPipelineSnapshotType m_Type = RHIPipelineSnapshotType::Graphics;
		bool m_Alive = false;
		RHIBindingLayoutHandle m_BindingLayout{};
		std::vector<RenderPassInfo> m_RenderPasses;

		RHIShaderSnapshot m_VertexShader;
		RHIShaderSnapshot m_PixelShader;
		RHIShaderSnapshot m_DomainShader;
		RHIShaderSnapshot m_HullShader;
		RHIShaderSnapshot m_GeometryShader;
		RHIShaderSnapshot m_ComputeShader;

		std::vector<RHIVertexAttributeSnapshot> m_VertexAttributes;
		std::vector<RHIVertexBufferLayoutDesc> m_VertexBuffers;
		RHIPrimitiveTopologyType m_TopologyType = RHIPrimitiveTopologyType::Unknown;
		RHIPrimitiveTopology m_PrimitiveTopology = RHIPrimitiveTopology::Unknown;
		RHIRasterizerDesc m_Rasterizer{};
		RHIDepthStencilDesc m_DepthStencil{};
		RHIBlendDesc m_Blend{};
		std::vector<RHIFormat> m_RenderTargetFormats;
		RHIFormat m_DepthStencilFormat = RHIFormat::Unknown;
		uint32_t m_SampleCount = 1;
		uint32_t m_SampleQuality = 0;

		uint64_t m_BackendPipelinePointer = 0;
		uint32_t m_BackendRootSignatureId = 0;
		uint64_t m_BackendRootSignaturePointer = 0;
	};

	struct RHIPipelineCacheSnapshot
	{
		uint64_t m_PipelineSystemRevision = 0;
		uint64_t m_BackendCacheRevision = 0;
		uint32_t m_RegisteredGraphicsPipelines = 0;
		uint32_t m_RegisteredComputePipelines = 0;
		uint32_t m_BackendRHIGraphicsPSOs = 0;
		uint32_t m_BackendComputePSOs = 0;
		uint32_t m_BackendLegacyGraphicsPSOs = 0;
		uint32_t m_BackendRootSignatures = 0;
	};

	struct RHIPipelineSystemSnapshot
	{
		std::string m_BackendName;
		RHIPipelineCacheSnapshot m_Cache;
		std::vector<RHIBindingLayoutSnapshot> m_BindingLayouts;
		std::vector<RHIPipelineSnapshot> m_Pipelines;
	};

	void BuildDX12PipelineSystemSnapshot(
		const DX12PipelineSystem& system,
		const PipelineCache* pipelineCache,
		RHIPipelineSystemSnapshot& outSnapshot) noexcept;
}
