#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>

namespace gglab
{
	// Persisted Lab parameter values and verification inputs. Keep these IDs stable;
	// capabilities below are explicit rather than inferred from numeric ordering.
	enum class GIReferenceFixture : int32_t
	{
		IsolatedSphere = 0,
		IsolatedPlane = 1,
		ThickWallRoom = 2,
		ThinWallRoom = 3,
		SunlitColoredRoom = 4,
		SunTransport = 5,
		OffCameraSunBlocker = 6,
		PublishedEnvironment = 7,
		CoupledDiffusePlanes = 8,
		HitMaterial = 9,
		SpecularVisibility = 10,
		CoastalSpecularVisibility = 11,
		CoastalDiffuse = 12,
		SmallSmoothReceiver = 13,
		ProbeFurnace = 14,
		CoastalProbeCost = 15,
		ProbeSlab = 16,
	};

	struct GIReferenceFixtureDescription
	{
		GIReferenceFixture m_Id;
		const char* m_Name;
	};
	inline constexpr std::array GIReferenceFixtures
	{
		GIReferenceFixtureDescription{ GIReferenceFixture::IsolatedSphere, "F1 isolated sphere" },
		GIReferenceFixtureDescription{ GIReferenceFixture::IsolatedPlane, "F1 isolated plane" },
		GIReferenceFixtureDescription{ GIReferenceFixture::ThickWallRoom, "F2 sealed thick-wall room" },
		GIReferenceFixtureDescription{ GIReferenceFixture::ThinWallRoom, "F3 sealed thin-wall room" },
		GIReferenceFixtureDescription{ GIReferenceFixture::SunlitColoredRoom, "F4 sunlit coloured room" },
		GIReferenceFixtureDescription{ GIReferenceFixture::SunTransport, "Contract: sun transport" },
		GIReferenceFixtureDescription{ GIReferenceFixture::OffCameraSunBlocker, "Contract: off-camera sun blocker" },
		GIReferenceFixtureDescription{ GIReferenceFixture::PublishedEnvironment, "Contract: published environment sphere" },
		GIReferenceFixtureDescription{ GIReferenceFixture::CoupledDiffusePlanes, "Contract: coupled diffuse planes" },
		GIReferenceFixtureDescription{ GIReferenceFixture::HitMaterial, "Contract: reduced hit material and snapshots" },
		GIReferenceFixtureDescription{ GIReferenceFixture::SpecularVisibility, "Contract: GGX environment visibility" },
		GIReferenceFixtureDescription{ GIReferenceFixture::CoastalSpecularVisibility, "Coastal: GGX visibility diagnostics" },
		GIReferenceFixtureDescription{ GIReferenceFixture::CoastalDiffuse, "Coastal: diffuse reference" },
		GIReferenceFixtureDescription{ GIReferenceFixture::SmallSmoothReceiver, "Contract: small smooth receiver" },
		GIReferenceFixtureDescription{ GIReferenceFixture::ProbeFurnace, "Contract: probe furnace" },
		GIReferenceFixtureDescription{ GIReferenceFixture::CoastalProbeCost, "Coastal: probe update cost" },
		GIReferenceFixtureDescription{ GIReferenceFixture::ProbeSlab, "Contract: probe slab" },
	};

	constexpr bool IsCoastalGIFixture(GIReferenceFixture fixture) noexcept
	{
		return fixture == GIReferenceFixture::CoastalSpecularVisibility ||
			fixture == GIReferenceFixture::CoastalDiffuse || fixture == GIReferenceFixture::CoastalProbeCost;
	}

	constexpr bool IsGIProbeFixture(GIReferenceFixture fixture) noexcept
	{
		return fixture == GIReferenceFixture::ProbeFurnace ||
			fixture == GIReferenceFixture::CoastalProbeCost || fixture == GIReferenceFixture::ProbeSlab;
	}

	constexpr bool UsesGIPrimaryReceiverMaterials(GIReferenceFixture fixture) noexcept
	{
		return fixture == GIReferenceFixture::SpecularVisibility ||
			fixture == GIReferenceFixture::CoastalSpecularVisibility ||
			fixture == GIReferenceFixture::CoastalDiffuse || fixture == GIReferenceFixture::SmallSmoothReceiver;
	}

	constexpr bool UsesGIStaticCostPath(GIReferenceFixture fixture) noexcept
	{
		return fixture == GIReferenceFixture::CoastalDiffuse ||
			fixture == GIReferenceFixture::SmallSmoothReceiver || IsGIProbeFixture(fixture);
	}

	constexpr bool IsGIContractFixture(GIReferenceFixture fixture) noexcept
	{
		return fixture == GIReferenceFixture::SunTransport ||
			fixture == GIReferenceFixture::OffCameraSunBlocker ||
			fixture == GIReferenceFixture::PublishedEnvironment ||
			fixture == GIReferenceFixture::CoupledDiffusePlanes ||
			fixture == GIReferenceFixture::HitMaterial || UsesGIPrimaryReceiverMaterials(fixture) ||
			IsGIProbeFixture(fixture);
	}

	enum class GIReferenceEnvironment : int32_t
	{
		DirectConstant = 0,
		PublishedConstant = 1,
		PublishedDirectional = 2,
		PhysicalSky = 3,
	};

	// Material case IDs are also consumed by verification scripts and snapshot logs.
	enum class GIHitMaterialCase : uint32_t
	{
		White = 0,
		Tinted = 1,
		BaseColorTexture = 2,
		MetallicTexture = 3,
		SecondaryUV = 4,
		TransformLinearClamp = 5,
		TransformPointClamp = 6,
		TransformLinearWrap = 7,
		Mip64 = 8,
		Mip65 = 9,
		Mip129 = 10,
		ShortMipChain = 11,
		IgnoredSecondaryInputs = 12,
		DoubleSidedBackface = 13,
		SingleSidedBackface = 14,
		TransformReplacement = 15,
		EntityReplacement = 16,
		BlendExcluded = 17,
		OpaqueAfterBlend = 18,
		GeometryReplacement = 19,
		OpaqueAfterGeometryReplacement = 20,
		TextureReplacement = 21,
		MaskUnsupported = 22,
		Count = 23,
	};

	enum class GIMaterialTexture : size_t
	{
		Pattern,
		MetallicRoughness,
		Mip64,
		Mip65,
		Mip129,
		ShortMipChain,
		Replacement,
		Normal,
		Count,
	};

	constexpr std::optional<GIMaterialTexture> GetGIBaseColorTexture(GIHitMaterialCase material) noexcept
	{
		switch (material)
		{
		case GIHitMaterialCase::BaseColorTexture:
		case GIHitMaterialCase::MetallicTexture:
		case GIHitMaterialCase::SecondaryUV:
		case GIHitMaterialCase::TransformLinearClamp:
		case GIHitMaterialCase::TransformPointClamp:
		case GIHitMaterialCase::TransformLinearWrap:
		case GIHitMaterialCase::IgnoredSecondaryInputs:
		case GIHitMaterialCase::GeometryReplacement:
			return GIMaterialTexture::Pattern;
		case GIHitMaterialCase::Mip64:
			return GIMaterialTexture::Mip64;
		case GIHitMaterialCase::Mip65:
			return GIMaterialTexture::Mip65;
		case GIHitMaterialCase::Mip129:
			return GIMaterialTexture::Mip129;
		case GIHitMaterialCase::ShortMipChain:
			return GIMaterialTexture::ShortMipChain;
		case GIHitMaterialCase::TextureReplacement:
			return GIMaterialTexture::Replacement;
		default:
			return std::nullopt;
		}
	}

	enum class GIVisibilityMaterial : uint32_t
	{
		Dielectric = 0,
		Rough = 1,
		Smooth = 2,
		Metallic = 3,
		Clearcoat = 4,
		NormalMaps = 5,
		Anisotropic = 6,
		RotatedAnisotropic = 7,
		UnitIor = 8,
		ZeroIor = 9,
		TexturedClearcoat = 10,
		SmoothNormalField = 11,
		Count = 12,
	};

	enum class GIVisibilityTexture : size_t
	{
		Normal,
		CoatNormal,
		Anisotropy,
		Count,
	};

	enum class GIVisibilityGeometry : int32_t
	{
		Open = 0,
		Closed = 1,
		PartialBlocker = 2,
		OffCameraBlocker = 3,
	};
}
