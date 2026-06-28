#pragma once
#include "Graphics/RHI/RHIBindingLayout.h"
#include "Graphics/RHI/RHIHandles.h"
#include "Graphics/RHI/RHITypes.h"

#include <array>
#include <cstdint>
#include <limits>

namespace gglab
{
	enum class RHIPrimitiveTopologyType : uint8_t
	{
		Unknown,
		Point,
		Line,
		Triangle,
		Patch,
	};

	enum class RHIPrimitiveTopology : uint8_t
	{
		Unknown,
		PointList,
		LineList,
		LineStrip,
		TriangleList,
		TriangleStrip,
	};

	enum class RHIVertexInputRate : uint8_t
	{
		PerVertex,
		PerInstance,
	};

	struct RHIVertexAttributeDesc
	{
		const char* m_SemanticName = nullptr;
		uint32_t m_SemanticIndex = 0;
		RHIFormat m_Format = RHIFormat::Unknown;
		uint32_t m_InputSlot = 0;
		uint32_t m_AlignedByteOffset = 0;
	};

	struct RHIVertexBufferLayoutDesc
	{
		uint32_t m_InputSlot = 0;
		uint32_t m_StrideInBytes = 0;
		RHIVertexInputRate m_InputRate = RHIVertexInputRate::PerVertex;
		uint32_t m_InstanceStepRate = 0;
	};

	struct RHIVertexInputLayoutDesc
	{
		static constexpr uint32_t MaxAttributes = 16;
		static constexpr uint32_t MaxVertexBuffers = 8;

		std::array<RHIVertexAttributeDesc, MaxAttributes> m_Attributes{};
		uint32_t m_AttributeCount = 0;
		std::array<RHIVertexBufferLayoutDesc, MaxVertexBuffers> m_VertexBuffers{};
		uint32_t m_VertexBufferCount = 0;
	};

	enum class RHIFillMode : uint8_t
	{
		Solid,
		Wireframe,
	};

	enum class RHICullMode : uint8_t
	{
		None,
		Front,
		Back,
	};

	struct RHIRasterizerDesc
	{
		RHIFillMode m_FillMode = RHIFillMode::Solid;
		RHICullMode m_CullMode = RHICullMode::Back;
		bool m_FrontCounterClockwise = false;
		int32_t m_DepthBias = 0;
		float m_DepthBiasClamp = 0.0f;
		float m_SlopeScaledDepthBias = 0.0f;
		bool m_DepthClipEnable = true;
	};

	struct RHIDepthStencilDesc
	{
		bool m_DepthTestEnable = true;
		bool m_DepthWriteEnable = true;
		RHICompareOp m_DepthCompareOp = RHICompareOp::Less;
		bool m_StencilEnable = false;
	};

	enum class RHIBlendFactor : uint8_t
	{
		Zero,
		One,
		SrcColor,
		OneMinusSrcColor,
		SrcAlpha,
		OneMinusSrcAlpha,
		DstAlpha,
		OneMinusDstAlpha,
		DstColor,
		OneMinusDstColor,
	};

	enum class RHIBlendOp : uint8_t
	{
		Add,
		Subtract,
		ReverseSubtract,
		Min,
		Max,
	};

	enum class RHIColorWriteMask : uint8_t
	{
		None = 0,
		Red = 1u << 0,
		Green = 1u << 1,
		Blue = 1u << 2,
		Alpha = 1u << 3,
		All = Red | Green | Blue | Alpha,
	};
	GGLAB_ENUM_FLAGS(RHIColorWriteMask);

	struct RHIRenderTargetBlendDesc
	{
		bool m_BlendEnable = false;
		RHIBlendFactor m_SrcColor = RHIBlendFactor::One;
		RHIBlendFactor m_DstColor = RHIBlendFactor::Zero;
		RHIBlendOp m_ColorOp = RHIBlendOp::Add;
		RHIBlendFactor m_SrcAlpha = RHIBlendFactor::One;
		RHIBlendFactor m_DstAlpha = RHIBlendFactor::Zero;
		RHIBlendOp m_AlphaOp = RHIBlendOp::Add;
		RHIColorWriteMask m_WriteMask = RHIColorWriteMask::All;
	};

	struct RHIBlendDesc
	{
		static constexpr uint32_t MaxRenderTargets = 8;

		bool m_AlphaToCoverageEnable = false;
		std::array<RHIRenderTargetBlendDesc, MaxRenderTargets> m_RenderTargets{};
	};

	struct RHIGraphicsPipelineDesc
	{
		static constexpr uint32_t MaxRenderTargets = RHIBlendDesc::MaxRenderTargets;

		RHIBindingLayoutHandle m_BindingLayout{};
		RHIShaderHandle m_VertexShader{};
		RHIShaderHandle m_PixelShader{};
		RHIShaderHandle m_DomainShader{};
		RHIShaderHandle m_HullShader{};
		RHIShaderHandle m_GeometryShader{};

		RHIVertexInputLayoutDesc m_VertexInput{};
		RHIPrimitiveTopologyType m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		RHIPrimitiveTopology m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		RHIRasterizerDesc m_Rasterizer{};
		RHIDepthStencilDesc m_DepthStencil{};
		RHIBlendDesc m_Blend{};

		std::array<RHIFormat, MaxRenderTargets> m_RenderTargetFormats{};
		uint32_t m_RenderTargetCount = 0;
		RHIFormat m_DepthStencilFormat = RHIFormat::Unknown;
		uint32_t m_SampleCount = 1;
		uint32_t m_SampleQuality = 0;
		uint32_t m_SampleMask = std::numeric_limits<uint32_t>::max();
		const char* m_DebugName = nullptr;
	};

	struct RHIComputePipelineDesc
	{
		RHIBindingLayoutHandle m_BindingLayout{};
		RHIShaderHandle m_ComputeShader{};
		const char* m_DebugName = nullptr;
	};
}
