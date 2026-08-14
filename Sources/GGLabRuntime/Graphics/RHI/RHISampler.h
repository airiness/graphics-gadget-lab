#pragma once
#include "Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <limits>
#include <tuple>

namespace gglab
{
	enum class RHISamplerFilter : uint8_t
	{
		MinMagMipPoint,
		MinMagPointMipLinear,
		MinPointMagLinearMipPoint,
		MinPointMagMipLinear,
		MinLinearMagMipPoint,
		MinLinearMagPointMipLinear,
		MinMagLinearMipPoint,
		MinMagMipLinear,
		Anisotropic,
		ComparisonMinMagLinearMipPoint,
		ComparisonAnisotropic,
	};

	enum class RHITextureAddressMode : uint8_t
	{
		Wrap,
		Mirror,
		Clamp,
		Border,
		MirrorOnce,
	};

	struct RHISamplerDesc
	{
		RHISamplerFilter m_Filter = RHISamplerFilter::MinMagMipLinear;

		RHITextureAddressMode m_AddressU = RHITextureAddressMode::Clamp;
		RHITextureAddressMode m_AddressV = RHITextureAddressMode::Clamp;
		RHITextureAddressMode m_AddressW = RHITextureAddressMode::Clamp;

		float m_MipLODBias = 0.0f;
		uint32_t m_MaxAnisotropy = 1;

		RHICompareOp m_CompareOp = RHICompareOp::Never;

		float m_BorderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		float m_MinLOD = 0.0f;
		float m_MaxLOD = std::numeric_limits<float>::max();

		constexpr bool operator==(const RHISamplerDesc&) const noexcept = default;

		constexpr auto AsTuple() const noexcept
		{
			return std::tuple{ m_Filter, m_AddressU, m_AddressV, m_AddressW, m_MipLODBias,
				m_MaxAnisotropy, m_CompareOp, m_BorderColor[0], m_BorderColor[1], m_BorderColor[2],
				m_BorderColor[3], m_MinLOD, m_MaxLOD };
		}
	};

	[[nodiscard]] constexpr inline RHIPortabilityValidationResult ValidateRHISamplerPortability(
		const RHISamplerDesc& desc, const RHIPortabilityCapabilities& capabilities) noexcept
	{
		const bool usesBorder = desc.m_AddressU == RHITextureAddressMode::Border ||
			desc.m_AddressV == RHITextureAddressMode::Border ||
			desc.m_AddressW == RHITextureAddressMode::Border;
		if (!usesBorder || capabilities.m_CustomBorderColor)
		{
			return {};
		}

		const bool transparentBlack = desc.m_BorderColor[0] == 0.0f &&
			desc.m_BorderColor[1] == 0.0f && desc.m_BorderColor[2] == 0.0f &&
			desc.m_BorderColor[3] == 0.0f;
		const bool opaqueBlack = desc.m_BorderColor[0] == 0.0f &&
			desc.m_BorderColor[1] == 0.0f && desc.m_BorderColor[2] == 0.0f &&
			desc.m_BorderColor[3] == 1.0f;
		const bool opaqueWhite = desc.m_BorderColor[0] == 1.0f &&
			desc.m_BorderColor[1] == 1.0f && desc.m_BorderColor[2] == 1.0f &&
			desc.m_BorderColor[3] == 1.0f;
		return transparentBlack || opaqueBlack || opaqueWhite
			? RHIPortabilityValidationResult{}
			: RHIPortabilityValidationResult{
				.m_Error = RHIPortabilityValidationError::CustomBorderColorUnsupported };
	}
}
