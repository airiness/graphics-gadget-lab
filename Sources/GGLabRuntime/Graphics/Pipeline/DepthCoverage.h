#pragma once
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "Graphics/RenderParameters.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/ScreenSpace/ScreenSpaceTypes.h"

#include <cstdint>
#include <limits>
#include <string>

namespace gglab
{
	struct GraphicsPhysicalPipelineKey;

	enum class DepthCoverageVertexProgram : uint8_t
	{
		RigidMesh,
		SkinnedMesh,
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
		BaseColorMask,
	};

	enum class DepthCoverageProjectionSource : uint8_t
	{
		ViewDataProjection,
		DedicatedJitteredProjection,
	};

	struct DepthCoverageBufferSource
	{
		RHIBufferHandle m_Buffer{};
		uint32_t m_ElementIndex = std::numeric_limits<uint32_t>::max();

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Buffer.IsValid() && m_ElementIndex != std::numeric_limits<uint32_t>::max();
		}

		bool operator==(const DepthCoverageBufferSource&) const noexcept = default;
	};

	struct DepthCoveragePipelineSignature
	{
		ShaderID m_CoverageVertexShader{};
		DepthCoverageVertexProgram m_VertexProgram = DepthCoverageVertexProgram::RigidMesh;
		DepthCoverageDeformationVariant m_Deformation = DepthCoverageDeformationVariant::Rigid;
		InputLayoutID m_InputLayout = InputLayoutID::None;
		DepthCoveragePositionPrecision m_PositionPrecision =
			DepthCoveragePositionPrecision::Float32;
		RHIFormat m_PositionFormat = RHIFormat::Unknown;
		RHIPrimitiveTopologyType m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		RHIPrimitiveTopology m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
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
		DepthCoverageAlphaVariant m_AlphaVariant = DepthCoverageAlphaVariant::Opaque;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_CoverageVertexShader.IsValid() && m_InputLayout != InputLayoutID::None &&
				m_PositionFormat != RHIFormat::Unknown &&
				m_TopologyType != RHIPrimitiveTopologyType::Unknown &&
				m_PrimitiveTopology != RHIPrimitiveTopology::Unknown && m_SampleCount > 0;
		}

		constexpr bool operator==(const DepthCoveragePipelineSignature&) const noexcept = default;
	};

	struct DepthCoverageRasterDomain
	{
		uint64_t m_FrameSerial = 0;
		uint32_t m_ViewBindingId = std::numeric_limits<uint32_t>::max();
		DepthCoverageBufferSource m_CurrentViewSource{};
		DepthCoverageBufferSource m_CurrentJitteredProjectionSource{};
		DepthCoverageProjectionSource m_ProjectionSource =
			DepthCoverageProjectionSource::ViewDataProjection;
		uint32_t m_TargetWidth = 0;
		uint32_t m_TargetHeight = 0;
		RHIViewport m_Viewport{};
		RHIScissorRect m_Scissor{};
		DepthConvention m_DepthConvention = DepthConvention::Standard;

		[[nodiscard]] constexpr bool MatchesTargetExtent(
			uint32_t width, uint32_t height) const noexcept
		{
			return m_TargetWidth == width && m_TargetHeight == height;
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_FrameSerial != 0 && m_ViewBindingId != std::numeric_limits<uint32_t>::max() &&
				m_CurrentViewSource.IsValid() && m_CurrentJitteredProjectionSource.IsValid() &&
				m_TargetWidth > 0 && m_TargetHeight > 0 && m_Viewport.m_X >= 0.0f &&
				m_Viewport.m_Y >= 0.0f && m_Viewport.m_Width > 0.0f &&
				m_Viewport.m_Height > 0.0f &&
				m_Viewport.m_X + m_Viewport.m_Width <= static_cast<float>(m_TargetWidth) &&
				m_Viewport.m_Y + m_Viewport.m_Height <= static_cast<float>(m_TargetHeight) &&
				m_Viewport.m_MinDepth >= 0.0f &&
				m_Viewport.m_MinDepth <= m_Viewport.m_MaxDepth &&
				m_Viewport.m_MaxDepth <= 1.0f && m_Scissor.m_Left >= 0 && m_Scissor.m_Top >= 0 &&
				m_Scissor.m_Right > m_Scissor.m_Left && m_Scissor.m_Bottom > m_Scissor.m_Top &&
				m_Scissor.m_Right <= static_cast<int32_t>(m_TargetWidth) &&
				m_Scissor.m_Bottom <= static_cast<int32_t>(m_TargetHeight);
		}

		bool operator==(const DepthCoverageRasterDomain&) const noexcept = default;
	};

	struct DepthCoverageGeometryBinding
	{
		MeshID m_MeshId{};
		RHIVertexBufferBinding m_VertexBuffer{};
		RHIIndexBufferBinding m_IndexBuffer{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_MeshId.IsValid() && m_VertexBuffer.m_Buffer.IsValid() &&
				m_VertexBuffer.m_Stride > 0 && m_VertexBuffer.m_SizeInBytes > 0 &&
				m_IndexBuffer.m_Buffer.IsValid() && m_IndexBuffer.m_SizeInBytes > 0 &&
				m_IndexBuffer.m_Format != RHIFormat::Unknown;
		}

		bool operator==(const DepthCoverageGeometryBinding&) const noexcept = default;
	};

	struct DepthCoverageIndexedDrawArguments
	{
		uint32_t m_IndexCount = 0;
		uint32_t m_InstanceCount = 1;
		uint32_t m_StartIndexLocation = 0;
		int32_t m_BaseVertexLocation = 0;
		uint32_t m_StartInstanceLocation = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_IndexCount > 0 && m_InstanceCount > 0;
		}

		bool operator==(const DepthCoverageIndexedDrawArguments&) const noexcept = default;
	};

	struct DepthCoverageDrawPacket
	{
		DepthCoverageGeometryBinding m_Geometry{};
		DepthCoverageIndexedDrawArguments m_IndexedDraw{};
		DrawParameters m_DrawParameters{};
		DepthCoverageBufferSource m_CurrentModelSource{};
		DepthCoverageBufferSource m_MaterialAlphaSource{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Geometry.IsValid() && m_IndexedDraw.IsValid() &&
				m_CurrentModelSource.IsValid() && m_MaterialAlphaSource.IsValid();
		}

		bool operator==(const DepthCoverageDrawPacket&) const noexcept = default;
	};

	struct DepthCoverageValidationResult
	{
		bool m_Matches = false;
		std::string m_Mismatch;
	};

	[[nodiscard]] DepthCoveragePipelineSignature BuildDepthCoveragePipelineSignature(
		const GraphicsPhysicalPipelineKey& physicalKey, DepthCoverageVertexProgram vertexProgram,
		DepthCoverageDeformationVariant deformation,
		DepthCoveragePositionPrecision positionPrecision, RHIFormat positionFormat,
		bool doubleSided, DepthCoverageAlphaVariant alphaVariant) noexcept;

	[[nodiscard]] DepthCoverageValidationResult CompareDepthCoveragePipelineSignatures(
		const DepthCoveragePipelineSignature& lhs, const DepthCoveragePipelineSignature& rhs);

	[[nodiscard]] DepthCoverageValidationResult CompareDepthCoverageRasterDomains(
		const DepthCoverageRasterDomain& lhs, const DepthCoverageRasterDomain& rhs);

	[[nodiscard]] bool IsDepthCoverageTargetExtentCompatible(
		const DepthCoverageRasterDomain& rasterDomain, const RHITextureDesc& textureDesc) noexcept;
	[[nodiscard]] bool AreDepthCoverageTargetExtentsCompatible(
		const DepthCoverageRasterDomain& rasterDomain, const RHITextureDesc& colorDesc,
		const RHITextureDesc& depthDesc) noexcept;

	[[nodiscard]] bool IsSameDepthCoverageDrawPacket(
		const DepthCoverageDrawPacket& lhs, const DepthCoverageDrawPacket& rhs) noexcept;

	[[nodiscard]] std::string DescribeDepthCoveragePipelineSignature(
		const DepthCoveragePipelineSignature& signature);
	[[nodiscard]] std::string DescribeDepthCoverageRasterDomain(
		const DepthCoverageRasterDomain& domain);
	[[nodiscard]] std::string DescribeDepthCoverageDrawPacket(
		const DepthCoverageDrawPacket& packet);
}
