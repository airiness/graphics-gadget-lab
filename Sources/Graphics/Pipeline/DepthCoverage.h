#pragma once
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHIPipeline.h"

#include <cstdint>
#include <limits>
#include <string>

namespace gglab
{
	struct GraphicsPipelineRecipe;

	enum class DepthCoverageVertexProgram : uint8_t
	{
		RigidMeshV1,
		SkinnedMeshV1,
	};

	enum class DepthCoverageDeformationVariant : uint8_t
	{
		Rigid,
		Skinned,
	};

	enum class DepthCoveragePositionPrecision : uint8_t
	{
		Float32,
		Float16,
	};

	enum class DepthCoverageAlphaVariant : uint8_t
	{
		Opaque,
		BaseColorMaskV1,
	};

	enum class DepthCoverageProjectionSource : uint8_t
	{
		ViewDataProjection,
		DedicatedJitteredProjection,
	};

	struct DepthCoverageSignature
	{
		DepthCoverageVertexProgram m_VertexProgram =
			DepthCoverageVertexProgram::RigidMeshV1;
		DepthCoverageDeformationVariant m_Deformation =
			DepthCoverageDeformationVariant::Rigid;
		InputLayoutID m_InputLayout = InputLayoutID::None;
		DepthCoveragePositionPrecision m_PositionPrecision =
			DepthCoveragePositionPrecision::Float32;
		RHIFormat m_PositionFormat = RHIFormat::Unknown;
		RHIPrimitiveTopologyType m_TopologyType =
			RHIPrimitiveTopologyType::Triangle;
		RHIPrimitiveTopology m_PrimitiveTopology =
			RHIPrimitiveTopology::TriangleList;
		RHIFillMode m_FillMode = RHIFillMode::Solid;
		RHICullMode m_CullMode = RHICullMode::Back;
		bool m_FrontCounterClockwise = false;
		int32_t m_DepthBias = 0;
		float m_DepthBiasClamp = 0.0f;
		float m_SlopeScaledDepthBias = 0.0f;
		bool m_DepthClipEnable = true;
		bool m_DoubleSided = false;
		uint32_t m_SampleCount = 1;
		uint32_t m_SampleQuality = 0;
		uint32_t m_SampleMask = std::numeric_limits<uint32_t>::max();
		bool m_AlphaToCoverageEnable = false;
		DepthCoverageAlphaVariant m_AlphaVariant =
			DepthCoverageAlphaVariant::Opaque;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_InputLayout != InputLayoutID::None &&
				m_PositionFormat != RHIFormat::Unknown &&
				m_SampleCount > 0;
		}

		constexpr bool operator==(
			const DepthCoverageSignature&) const noexcept = default;
	};

	struct DepthCoverageBufferSource
	{
		RHIBufferHandle m_Buffer{};
		uint32_t m_ElementIndex = std::numeric_limits<uint32_t>::max();

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Buffer.IsValid() &&
				m_ElementIndex != std::numeric_limits<uint32_t>::max();
		}

		bool operator==(const DepthCoverageBufferSource&) const noexcept = default;
	};

	struct DepthCoverageBinding
	{
		uint64_t m_FrameSerial = 0;
		uint32_t m_ViewBindingId = std::numeric_limits<uint32_t>::max();
		DepthCoverageBufferSource m_CurrentModelSource{};
		DepthCoverageBufferSource m_CurrentViewSource{};
		DepthCoverageBufferSource m_CurrentJitteredProjectionSource{};
		DepthCoverageProjectionSource m_ProjectionSource =
			DepthCoverageProjectionSource::ViewDataProjection;
		DepthCoverageBufferSource m_MaterialAlphaSource{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_FrameSerial != 0 &&
				m_ViewBindingId != std::numeric_limits<uint32_t>::max() &&
				m_CurrentModelSource.IsValid() &&
				m_CurrentViewSource.IsValid() &&
				m_CurrentJitteredProjectionSource.IsValid() &&
				m_MaterialAlphaSource.IsValid();
		}

		bool operator==(const DepthCoverageBinding&) const noexcept = default;
	};

	struct DepthCoverageComparison
	{
		bool m_SignatureMatches = false;
		bool m_BindingMatches = false;
		std::string m_Mismatch;

		[[nodiscard]] bool IsCompatible() const noexcept
		{
			return m_SignatureMatches && m_BindingMatches;
		}
	};

	[[nodiscard]] DepthCoverageSignature BuildDepthCoverageSignature(
		const GraphicsPipelineRecipe& recipe,
		DepthCoverageVertexProgram vertexProgram,
		DepthCoverageDeformationVariant deformation,
		DepthCoveragePositionPrecision positionPrecision,
		RHIFormat positionFormat,
		bool doubleSided,
		DepthCoverageAlphaVariant alphaVariant) noexcept;

	[[nodiscard]] DepthCoverageComparison CompareDepthCoverageContracts(
		const DepthCoverageSignature& lhsSignature,
		const DepthCoverageBinding& lhsBinding,
		const DepthCoverageSignature& rhsSignature,
		const DepthCoverageBinding& rhsBinding);

	[[nodiscard]] std::string DescribeDepthCoverageSignature(
		const DepthCoverageSignature& signature);
	[[nodiscard]] std::string DescribeDepthCoverageBinding(
		const DepthCoverageBinding& binding);
}
