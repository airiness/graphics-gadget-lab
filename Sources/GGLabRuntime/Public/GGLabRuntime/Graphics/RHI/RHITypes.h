#pragma once
#include "GGLabFoundation/Base/EnumFlags.h"

#include <cstdint>
#include <string_view>

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

	enum class RHITextureAspect : uint8_t
	{
		None = 0,
		Color = 1u << 0,
		Depth = 1u << 1,
		Stencil = 1u << 2,
		DepthStencil = Depth | Stencil,
		All = Color | Depth | Stencil,
	};
	GGLAB_ENUM_FLAGS(RHITextureAspect);

	enum class RHIFormat : uint16_t
	{
		Unknown,

		R8G8B8A8Typeless,
		R8G8B8A8Unorm,
		R8G8B8A8UnormSrgb,

		R16G16Float,
		R16G16B16A16Typeless,
		R16G16B16A16Float,

		R32G32Float,
		R32G32B32Float,
		R32G32B32A32Float,
		R32Typeless,
		R32Float,
		R32Uint,

		D24UnormS8Uint,
		D32Float,

		R8Unorm,
		R16Float,
		B8G8R8A8Unorm,
		B8G8R8A8UnormSrgb,

		// RHIFormat values are serialized by texture artifacts. Append new formats above Count.
		Count,
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
		IndexInput = 1ull << 1,
		VertexShader = 1ull << 2,
		PixelShader = 1ull << 3,
		ComputeShader = 1ull << 4,
		RenderTarget = 1ull << 5,
		DepthStencil = 1ull << 6,
		Copy = 1ull << 7,
		Resolve = 1ull << 8,
		Present = 1ull << 9,
		AllGraphics =
		DrawIndirect | IndexInput | VertexShader | PixelShader | RenderTarget | DepthStencil,
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
		// Texture initial/barrier-before only. Existing enum values are serialized by diagnostics.
		Undefined,
	};

	struct RHIResourceState
	{
		RHIStage m_Stages = RHIStage::All;
		RHIAccess m_Access = RHIAccess::Common;
		RHILayout m_Layout = RHILayout::Common;

		bool operator==(const RHIResourceState&) const noexcept = default;
	};

	[[nodiscard]] constexpr inline RHIResourceState UndefinedRHITextureState() noexcept
	{
		return {
			.m_Stages = RHIStage::None,
			.m_Access = RHIAccess::None,
			.m_Layout = RHILayout::Undefined,
		};
	}

	[[nodiscard]] constexpr inline RHIResourceState PresentRHITextureState() noexcept
	{
		return {
			.m_Stages = RHIStage::Present,
			.m_Access = RHIAccess::Present,
			.m_Layout = RHILayout::Present,
		};
	}

	enum class RHIResourceStateUsage : uint8_t
	{
		TextureInitial,
		TextureBarrierBefore,
		TextureBarrierAfter,
		Buffer,
	};

	[[nodiscard]] constexpr inline bool IsRHIResourceStateValid(
		const RHIResourceState& state, RHIResourceStateUsage usage) noexcept
	{
		if (state.m_Layout == RHILayout::Unknown)
		{
			return false;
		}
		if (state.m_Layout == RHILayout::Undefined)
		{
			return usage != RHIResourceStateUsage::TextureBarrierAfter &&
				usage != RHIResourceStateUsage::Buffer && state.m_Stages == RHIStage::None &&
				state.m_Access == RHIAccess::None;
		}
		if (state.m_Layout == RHILayout::Present || state.m_Stages == RHIStage::Present ||
			Test(state.m_Access, RHIAccess::Present))
		{
			return usage != RHIResourceStateUsage::Buffer && state == PresentRHITextureState();
		}
		return state.m_Stages != RHIStage::None && state.m_Access != RHIAccess::None;
	}

	struct RHIPortabilityCapabilities
	{
		bool m_ImageViewMinLod = false;
		bool m_CustomBorderColor = false;
		bool m_VertexAttributeDivisor = false;
		bool m_FillModeNonSolid = false;
		bool m_DepthClamp = false;
		bool m_DepthBiasClamp = false;
		bool m_IndependentBlend = false;
		bool m_SampleQuality = false;
	};

	enum class RHIPortabilityValidationError : uint8_t
	{
		None,
		ImageViewMinLodUnsupported,
		CustomBorderColorUnsupported,
		InvalidVertexInputRate,
		InstanceDivisorUnsupported,
		WireframeUnsupported,
		DepthClampUnsupported,
		DepthBiasClampUnsupported,
		IndependentBlendUnsupported,
		SampleQualityUnsupported,
		VertexBufferCountExceedsLimit,
		RenderTargetCountExceedsLimit,
	};

	struct RHIPortabilityValidationResult
	{
		RHIPortabilityValidationError m_Error = RHIPortabilityValidationError::None;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Error == RHIPortabilityValidationError::None;
		}
	};

	[[nodiscard]] constexpr inline std::string_view GetRHIPortabilityValidationErrorText(
		RHIPortabilityValidationError error) noexcept
	{
		switch (error)
		{
		case RHIPortabilityValidationError::None:
			return "no portability validation error";
		case RHIPortabilityValidationError::ImageViewMinLodUnsupported:
			return "image view min LOD clamp is not supported";
		case RHIPortabilityValidationError::CustomBorderColorUnsupported:
			return "custom sampler border color is not supported";
		case RHIPortabilityValidationError::InvalidVertexInputRate:
			return "per-vertex input layouts must use instance step rate 0";
		case RHIPortabilityValidationError::InstanceDivisorUnsupported:
			return "instance step rate greater than 1 is not supported";
		case RHIPortabilityValidationError::WireframeUnsupported:
			return "rasterizer wireframe fill is not supported";
		case RHIPortabilityValidationError::DepthClampUnsupported:
			return "rasterizer depth clamp is not supported";
		case RHIPortabilityValidationError::DepthBiasClampUnsupported:
			return "rasterizer depth bias clamp is not supported";
		case RHIPortabilityValidationError::IndependentBlendUnsupported:
			return "independent render target blend states are not supported";
		case RHIPortabilityValidationError::SampleQualityUnsupported:
			return "non-zero sample quality is not supported";
		case RHIPortabilityValidationError::VertexBufferCountExceedsLimit:
			return "vertex buffer count exceeds the input layout limit";
		case RHIPortabilityValidationError::RenderTargetCountExceedsLimit:
			return "render target count exceeds the pipeline limit";
		}
		return "unknown portability validation error";
	}

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
