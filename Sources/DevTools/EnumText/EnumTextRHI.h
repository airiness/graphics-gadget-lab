#pragma once
#include "DevTools/EnumText/EnumText.h"
#include "Graphics/RHI/RHIBindingLayout.h"
#include "Graphics/RHI/RHIPipeline.h"

namespace gglab::devtools
{
	template<>
	struct EnumTextTraits<RHIBindingType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHIBindingType::Unknown, "Unknown" },
			EnumTextEntry{ RHIBindingType::ConstantBuffer, "ConstantBuffer" },
			EnumTextEntry{ RHIBindingType::ReadOnlyStorageBuffer, "ReadOnlyStorageBuffer" },
			EnumTextEntry{ RHIBindingType::ReadWriteStorageBuffer, "ReadWriteStorageBuffer" },
			EnumTextEntry{ RHIBindingType::SampledTexture, "SampledTexture" },
			EnumTextEntry{ RHIBindingType::StorageTexture, "StorageTexture" },
			EnumTextEntry{ RHIBindingType::Sampler, "Sampler" },
			EnumTextEntry{ RHIBindingType::PushConstants, "PushConstants" },
			EnumTextEntry{ RHIBindingType::BindlessSampledTextureTable, "BindlessTextureTable" },
			EnumTextEntry{ RHIBindingType::BindlessSamplerTable, "BindlessSamplerTable" },
		};
	};

	template<>
	struct EnumTextTraits<RHIShaderStage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHIShaderStage::None, "None" },
			EnumTextEntry{ RHIShaderStage::Vertex, "Vertex" },
			EnumTextEntry{ RHIShaderStage::Pixel, "Pixel" },
			EnumTextEntry{ RHIShaderStage::Compute, "Compute" },
			EnumTextEntry{ RHIShaderStage::AllGraphics, "AllGraphics" },
			EnumTextEntry{ RHIShaderStage::All, "All" },
		};
	};

	template<>
	struct EnumTextTraits<RHIPrimitiveTopology>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHIPrimitiveTopology::Unknown, "Unknown" },
			EnumTextEntry{ RHIPrimitiveTopology::PointList, "PointList" },
			EnumTextEntry{ RHIPrimitiveTopology::LineList, "LineList" },
			EnumTextEntry{ RHIPrimitiveTopology::LineStrip, "LineStrip" },
			EnumTextEntry{ RHIPrimitiveTopology::TriangleList, "TriangleList" },
			EnumTextEntry{ RHIPrimitiveTopology::TriangleStrip, "TriangleStrip" },
		};
	};

	template<>
	struct EnumTextTraits<RHIFillMode>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHIFillMode::Solid, "Solid" },
			EnumTextEntry{ RHIFillMode::Wireframe, "Wireframe" },
		};
	};

	template<>
	struct EnumTextTraits<RHICullMode>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHICullMode::None, "None" },
			EnumTextEntry{ RHICullMode::Front, "Front" },
			EnumTextEntry{ RHICullMode::Back, "Back" },
		};
	};

	template<>
	struct EnumTextTraits<RHICompareOp>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHICompareOp::Never, "Never" },
			EnumTextEntry{ RHICompareOp::Less, "Less" },
			EnumTextEntry{ RHICompareOp::Equal, "Equal" },
			EnumTextEntry{ RHICompareOp::LessEqual, "LessEqual" },
			EnumTextEntry{ RHICompareOp::Greater, "Greater" },
			EnumTextEntry{ RHICompareOp::NotEqual, "NotEqual" },
			EnumTextEntry{ RHICompareOp::GreaterEqual, "GreaterEqual" },
			EnumTextEntry{ RHICompareOp::Always, "Always" },
		};
	};
}
