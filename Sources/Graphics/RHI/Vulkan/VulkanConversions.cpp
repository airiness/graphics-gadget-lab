#include "Graphics/RHI/Vulkan/VulkanConversions.h"

namespace gglab
{
	VkPrimitiveTopology ToVulkanPrimitiveTopology(RHIPrimitiveTopology topology) noexcept
	{
		switch (topology)
		{
		case RHIPrimitiveTopology::PointList:
			return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case RHIPrimitiveTopology::LineList:
			return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case RHIPrimitiveTopology::LineStrip:
			return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		case RHIPrimitiveTopology::TriangleList:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case RHIPrimitiveTopology::TriangleStrip:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		default:
			return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
		}
	}

	VkPolygonMode ToVulkanPolygonMode(RHIFillMode mode) noexcept
	{
		return mode == RHIFillMode::Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	}

	VkCullModeFlags ToVulkanCullMode(RHICullMode mode) noexcept
	{
		switch (mode)
		{
		case RHICullMode::Front:
			return VK_CULL_MODE_FRONT_BIT;
		case RHICullMode::Back:
			return VK_CULL_MODE_BACK_BIT;
		default:
			return VK_CULL_MODE_NONE;
		}
	}

	VkFrontFace ToVulkanFrontFace(bool frontCounterClockwise) noexcept
	{
		return frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
	}

	VkCompareOp ToVulkanCompareOp(RHICompareOp op) noexcept
	{
		switch (op)
		{
		case RHICompareOp::Never:
			return VK_COMPARE_OP_NEVER;
		case RHICompareOp::Less:
			return VK_COMPARE_OP_LESS;
		case RHICompareOp::Equal:
			return VK_COMPARE_OP_EQUAL;
		case RHICompareOp::LessEqual:
			return VK_COMPARE_OP_LESS_OR_EQUAL;
		case RHICompareOp::Greater:
			return VK_COMPARE_OP_GREATER;
		case RHICompareOp::NotEqual:
			return VK_COMPARE_OP_NOT_EQUAL;
		case RHICompareOp::GreaterEqual:
			return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case RHICompareOp::Always:
		default:
			return VK_COMPARE_OP_ALWAYS;
		}
	}

	VkBlendFactor ToVulkanBlendFactor(RHIBlendFactor factor) noexcept
	{
		switch (factor)
		{
		case RHIBlendFactor::Zero:
			return VK_BLEND_FACTOR_ZERO;
		case RHIBlendFactor::One:
			return VK_BLEND_FACTOR_ONE;
		case RHIBlendFactor::SrcColor:
			return VK_BLEND_FACTOR_SRC_COLOR;
		case RHIBlendFactor::OneMinusSrcColor:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case RHIBlendFactor::SrcAlpha:
			return VK_BLEND_FACTOR_SRC_ALPHA;
		case RHIBlendFactor::OneMinusSrcAlpha:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case RHIBlendFactor::DstAlpha:
			return VK_BLEND_FACTOR_DST_ALPHA;
		case RHIBlendFactor::OneMinusDstAlpha:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case RHIBlendFactor::DstColor:
			return VK_BLEND_FACTOR_DST_COLOR;
		case RHIBlendFactor::OneMinusDstColor:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		}
		return VK_BLEND_FACTOR_ONE;
	}

	VkBlendOp ToVulkanBlendOp(RHIBlendOp op) noexcept
	{
		switch (op)
		{
		case RHIBlendOp::Subtract:
			return VK_BLEND_OP_SUBTRACT;
		case RHIBlendOp::ReverseSubtract:
			return VK_BLEND_OP_REVERSE_SUBTRACT;
		case RHIBlendOp::Min:
			return VK_BLEND_OP_MIN;
		case RHIBlendOp::Max:
			return VK_BLEND_OP_MAX;
		case RHIBlendOp::Add:
		default:
			return VK_BLEND_OP_ADD;
		}
	}

	VkColorComponentFlags ToVulkanColorWriteMask(RHIColorWriteMask mask) noexcept
	{
		VkColorComponentFlags result = 0;
		if (Test(mask, RHIColorWriteMask::Red))
		{
			result |= VK_COLOR_COMPONENT_R_BIT;
		}
		if (Test(mask, RHIColorWriteMask::Green))
		{
			result |= VK_COLOR_COMPONENT_G_BIT;
		}
		if (Test(mask, RHIColorWriteMask::Blue))
		{
			result |= VK_COLOR_COMPONENT_B_BIT;
		}
		if (Test(mask, RHIColorWriteMask::Alpha))
		{
			result |= VK_COLOR_COMPONENT_A_BIT;
		}
		return result;
	}

	VkShaderStageFlags ToVulkanShaderStages(RHIShaderStage stages) noexcept
	{
		VkShaderStageFlags result = 0;
		if (Test(stages, RHIShaderStage::Vertex))
		{
			result |= VK_SHADER_STAGE_VERTEX_BIT;
		}
		if (Test(stages, RHIShaderStage::Pixel))
		{
			result |= VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		if (Test(stages, RHIShaderStage::Compute))
		{
			result |= VK_SHADER_STAGE_COMPUTE_BIT;
		}
		return result;
	}

	VulkanShaderRegisterClass ToVulkanShaderRegisterClass(RHIBindingType type) noexcept
	{
		switch (type)
		{
		case RHIBindingType::ConstantBuffer:
		case RHIBindingType::PushConstants:
			return VulkanShaderRegisterClass::ConstantBuffer;
		case RHIBindingType::ReadOnlyStorageBuffer:
		case RHIBindingType::SampledTexture:
			return VulkanShaderRegisterClass::ShaderResource;
		case RHIBindingType::ReadWriteStorageBuffer:
		case RHIBindingType::StorageTexture:
			return VulkanShaderRegisterClass::UnorderedAccess;
		case RHIBindingType::Sampler:
			return VulkanShaderRegisterClass::Sampler;
		default:
			return VulkanShaderRegisterClass::ConstantBuffer;
		}
	}

	VkDescriptorType ToVulkanDescriptorType(RHIBindingType type) noexcept
	{
		switch (type)
		{
		case RHIBindingType::ConstantBuffer:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case RHIBindingType::ReadOnlyStorageBuffer:
		case RHIBindingType::ReadWriteStorageBuffer:
			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case RHIBindingType::SampledTexture:
			return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case RHIBindingType::StorageTexture:
			return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		case RHIBindingType::Sampler:
			return VK_DESCRIPTOR_TYPE_SAMPLER;
		case RHIBindingType::PushConstants:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		default:
			return VK_DESCRIPTOR_TYPE_MAX_ENUM;
		}
	}
}
