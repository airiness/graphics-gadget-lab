#pragma once
#include "DevTools/EnumText/EnumText.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/RHI/RHISampler.h"

namespace gglab::devtools
{
	template <> struct EnumTextTraits<RHIFormat>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHIFormat::Unknown, "Unknown"},
			EnumTextEntry{RHIFormat::R8Unorm, "R8Unorm"},
			EnumTextEntry{RHIFormat::R8G8B8A8Typeless, "R8G8B8A8Typeless"},
			EnumTextEntry{RHIFormat::R8G8B8A8Unorm, "R8G8B8A8Unorm"},
			EnumTextEntry{RHIFormat::R8G8B8A8UnormSrgb, "R8G8B8A8UnormSrgb"},
			EnumTextEntry{RHIFormat::R16Float, "R16Float"},
			EnumTextEntry{RHIFormat::R16G16Float, "R16G16Float"},
			EnumTextEntry{RHIFormat::R16G16B16A16Typeless, "R16G16B16A16Typeless"},
			EnumTextEntry{RHIFormat::R16G16B16A16Float, "R16G16B16A16Float"},
			EnumTextEntry{RHIFormat::R32G32Float, "R32G32Float"},
			EnumTextEntry{RHIFormat::R32G32B32Float, "R32G32B32Float"},
			EnumTextEntry{RHIFormat::R32G32B32A32Float, "R32G32B32A32Float"},
			EnumTextEntry{RHIFormat::R32Typeless, "R32Typeless"},
			EnumTextEntry{RHIFormat::R32Float, "R32Float"},
			EnumTextEntry{RHIFormat::R32Uint, "R32Uint"},
			EnumTextEntry{RHIFormat::D24UnormS8Uint, "D24UnormS8Uint"},
			EnumTextEntry{RHIFormat::D32Float, "D32Float"},
		};
	};

	template <> struct EnumTextTraits<RHIBindingType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHIBindingType::Unknown, "Unknown"},
			EnumTextEntry{RHIBindingType::ConstantBuffer, "ConstantBuffer"},
			EnumTextEntry{RHIBindingType::ReadOnlyStorageBuffer, "ReadOnlyStorageBuffer"},
			EnumTextEntry{RHIBindingType::ReadWriteStorageBuffer, "ReadWriteStorageBuffer"},
			EnumTextEntry{RHIBindingType::SampledTexture, "SampledTexture"},
			EnumTextEntry{RHIBindingType::StorageTexture, "StorageTexture"},
			EnumTextEntry{RHIBindingType::Sampler, "Sampler"},
			EnumTextEntry{RHIBindingType::PushConstants, "PushConstants"},
			EnumTextEntry{RHIBindingType::BindlessResourceTable, "BindlessResourceTable"},
			EnumTextEntry{RHIBindingType::BindlessSamplerTable, "BindlessSamplerTable"},
		};
	};

	template <> struct EnumTextTraits<RHIShaderStage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHIShaderStage::None, "None"},
			EnumTextEntry{RHIShaderStage::Vertex, "Vertex"},
			EnumTextEntry{RHIShaderStage::Pixel, "Pixel"},
			EnumTextEntry{RHIShaderStage::Compute, "Compute"},
			EnumTextEntry{RHIShaderStage::AllGraphics, "AllGraphics"},
			EnumTextEntry{RHIShaderStage::All, "All"},
		};
	};

	template <> struct EnumTextTraits<RHIPrimitiveTopology>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHIPrimitiveTopology::Unknown, "Unknown"},
			EnumTextEntry{RHIPrimitiveTopology::PointList, "PointList"},
			EnumTextEntry{RHIPrimitiveTopology::LineList, "LineList"},
			EnumTextEntry{RHIPrimitiveTopology::LineStrip, "LineStrip"},
			EnumTextEntry{RHIPrimitiveTopology::TriangleList, "TriangleList"},
			EnumTextEntry{RHIPrimitiveTopology::TriangleStrip, "TriangleStrip"},
		};
	};

	template <> struct EnumTextTraits<RHIFillMode>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHIFillMode::Solid, "Solid"},
			EnumTextEntry{RHIFillMode::Wireframe, "Wireframe"},
		};
	};

	template <> struct EnumTextTraits<RHICullMode>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHICullMode::None, "None"},
			EnumTextEntry{RHICullMode::Front, "Front"},
			EnumTextEntry{RHICullMode::Back, "Back"},
		};
	};

	template <> struct EnumTextTraits<RHICompareOp>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHICompareOp::Never, "Never"},
			EnumTextEntry{RHICompareOp::Less, "Less"},
			EnumTextEntry{RHICompareOp::Equal, "Equal"},
			EnumTextEntry{RHICompareOp::LessEqual, "LessEqual"},
			EnumTextEntry{RHICompareOp::Greater, "Greater"},
			EnumTextEntry{RHICompareOp::NotEqual, "NotEqual"},
			EnumTextEntry{RHICompareOp::GreaterEqual, "GreaterEqual"},
			EnumTextEntry{RHICompareOp::Always, "Always"},
		};
	};

	template <> struct EnumTextTraits<RHISamplerFilter>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHISamplerFilter::MinMagMipPoint, "Min/Mag/Mip Point"},
			EnumTextEntry{RHISamplerFilter::MinMagPointMipLinear, "Min/Mag Point, Mip Linear"},
			EnumTextEntry{
				RHISamplerFilter::MinPointMagLinearMipPoint, "Min Point, Mag Linear, Mip Point"},
			EnumTextEntry{RHISamplerFilter::MinPointMagMipLinear, "Min Point, Mag/Mip Linear"},
			EnumTextEntry{RHISamplerFilter::MinLinearMagMipPoint, "Min Linear, Mag/Mip Point"},
			EnumTextEntry{
				RHISamplerFilter::MinLinearMagPointMipLinear, "Min Linear, Mag Point, Mip Linear"},
			EnumTextEntry{RHISamplerFilter::MinMagLinearMipPoint, "Min/Mag Linear, Mip Point"},
			EnumTextEntry{RHISamplerFilter::MinMagMipLinear, "Min/Mag/Mip Linear"},
			EnumTextEntry{RHISamplerFilter::Anisotropic, "Anisotropic"},
			EnumTextEntry{RHISamplerFilter::ComparisonMinMagLinearMipPoint, "Comparison Linear"},
			EnumTextEntry{RHISamplerFilter::ComparisonAnisotropic, "Comparison Anisotropic"},
		};
	};

	template <> struct EnumTextTraits<RHITextureAddressMode>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHITextureAddressMode::Wrap, "Wrap"},
			EnumTextEntry{RHITextureAddressMode::Mirror, "Mirror"},
			EnumTextEntry{RHITextureAddressMode::Clamp, "Clamp"},
			EnumTextEntry{RHITextureAddressMode::Border, "Border"},
			EnumTextEntry{RHITextureAddressMode::MirrorOnce, "Mirror Once"},
		};
	};
}
