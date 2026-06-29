#pragma once
#include "Core/EnumFlags.h"

#include <cstdint>

namespace gglab
{
	enum class RHIBackendType : uint8_t
	{
		Unknown,
		DX12,
		Vulkan,
	};

	enum class RHIQueueType : uint8_t
	{
		Graphics,
		Compute,
		Copy,
		Transfer,
	};

	enum class RHIResourceType : uint8_t
	{
		Unknown,
		Texture,
		Buffer,
	};

	enum class RHIFormat : uint16_t
	{
		Unknown,

		R8G8B8A8Typeless,
		R8G8B8A8Unorm,
		R8G8B8A8UnormSrgb,
		R16G16Float,
		R16G16B16A16Float,

		R32G32Float,
		R32G32B32Float,
		R32G32B32A32Float,
		R32Typeless,
		R32Float,
		R32Uint,
		D24UnormS8Uint,
		D32Float,
	};

	enum class RHIShaderStage : uint32_t
	{
		None = 0,
		Vertex = 1u << 0,
		Pixel = 1u << 1,
		Compute = 1u << 2,
		AllGraphics = Vertex | Pixel,
		All = Vertex | Pixel | Compute,
	};
	GGLAB_ENUM_FLAGS(RHIShaderStage);

	enum class RHIStage : uint64_t
	{
		None = 0,
		DrawIndirect = 1ull << 0,
		VertexInput = 1ull << 1,
		VertexShader = 1ull << 2,
		PixelShader = 1ull << 3,
		ComputeShader = 1ull << 4,
		RenderTarget = 1ull << 5,
		DepthStencil = 1ull << 6,
		Copy = 1ull << 7,
		Resolve = 1ull << 8,
		Present = 1ull << 9,
		AllGraphics = DrawIndirect | VertexInput | VertexShader | PixelShader | RenderTarget | DepthStencil,
		AllShaders = VertexShader | PixelShader | ComputeShader,
		All = AllGraphics | ComputeShader | Copy | Resolve | Present,
	};
	GGLAB_ENUM_FLAGS(RHIStage);

	enum class RHIAccess : uint64_t
	{
		None = 0,
		Common = 1ull << 0,
		ShaderResource = 1ull << 1,
		RenderTarget = 1ull << 2,
		DepthStencilRead = 1ull << 3,
		DepthStencilWrite = 1ull << 4,
		UnorderedAccess = 1ull << 5,
		CopySource = 1ull << 6,
		CopyDest = 1ull << 7,
		VertexBuffer = 1ull << 8,
		IndexBuffer = 1ull << 9,
		ConstantBuffer = 1ull << 10,
		IndirectArgument = 1ull << 11,
		Present = 1ull << 12,
	};
	GGLAB_ENUM_FLAGS(RHIAccess);

	enum class RHILayout : uint8_t
	{
		Unknown,
		Common,
		ShaderResource,
		RenderTarget,
		DepthStencilRead,
		DepthStencilWrite,
		UnorderedAccess,
		CopySource,
		CopyDest,
		Present,
	};

	struct RHIResourceState
	{
		RHIStage m_Stages = RHIStage::All;
		RHIAccess m_Access = RHIAccess::Common;
		RHILayout m_Layout = RHILayout::Common;

		bool operator==(const RHIResourceState&) const noexcept = default;
	};

	enum class RHICompareOp : uint8_t
	{
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always,
	};

	struct RHIClearValue
	{
		RHIFormat m_Format = RHIFormat::Unknown;
		float m_Color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float m_Depth = 1.0f;
		uint8_t m_Stencil = 0;
		bool m_IsDepthStencil = false;
	};

	struct RHIExtent3D
	{
		uint32_t m_Width = 1;
		uint32_t m_Height = 1;
		uint32_t m_Depth = 1;
	};
}
