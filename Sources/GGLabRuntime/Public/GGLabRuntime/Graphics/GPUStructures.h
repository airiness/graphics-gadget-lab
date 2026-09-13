#pragma once
#include "GGLabRuntime/Core/Math/Color.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabFoundation/Base/TypeUtils.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

// struct member name without m_ for GPU using

namespace gglab
{
#define GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE (16)
#define GGLAB_GPU_STRUCTURE_ALIGNAS alignas(GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE)

	struct LightGPU
	{
		Vector4 Position;
		Vector4 Direction;
		Vector4 Color;
		float Intensity;
		float Range;
		float SpotAngle;
		uint32_t LightType;
	};

	struct TextureSamplerBindingGPU
	{
		uint32_t TextureIndex;
		uint32_t SamplerIndex;
	};

	struct IBLResourceGPU
	{
		TextureSamplerBindingGPU EnvironmentBinding;
		TextureSamplerBindingGPU IrradianceBinding;
		TextureSamplerBindingGPU PrefilteredSpecularBinding;
		TextureSamplerBindingGPU BrdfLutBinding;

		uint32_t PrefilteredSpecularMipLevels;
		float EnvironmentIntensity;
		float EnvironmentRotationRadians;
		uint32_t Padding;
	};

	struct SceneGPU
	{
		uint32_t ObjectBaseIndex;
		uint32_t ObjectCount;
		uint32_t MaterialBaseIndex;
		uint32_t MaterialCount;
		uint32_t ViewBaseIndex;
		uint32_t ViewCount;
		uint32_t LightBaseIndex;
		uint32_t LightCount;
		uint32_t DirectionalShadowLightIndex;
		uint32_t Padding[3];

		IBLResourceGPU IBLResource;
	};
	static_assert(sizeof(SceneGPU) == 96);

	struct ObjectGPU
	{
		Matrix ModelMat;
		Matrix PreviousModelMat;
		Matrix NormalMat;
		uint32_t MaterialIndex;
		uint32_t ViewIndex;
		uint32_t Padding[2];
	};
	static constexpr uint32_t MaxObjectCapacity = 1024;

	struct MaterialTextureBindingGPU
	{
		TextureSamplerBindingGPU TextureSamplerBinding;
		uint32_t TexCoordIndex;
		uint32_t Padding;
	};

	struct MaterialGPU
	{
		MaterialTextureBindingGPU BaseColorBinding;
		MaterialTextureBindingGPU EmissiveBinding;
		MaterialTextureBindingGPU MetallicRoughnessBinding;
		MaterialTextureBindingGPU NormalBinding;
		MaterialTextureBindingGPU OcclusionBinding;

		Color BaseColorFactor;
		Color EmissiveColorFactor;

		float MetallicFactor;
		float RoughnessFactor;
		float NormalScale;
		float OcclusionStrength;

		int32_t AlphaMode; // 0: OPAQUE, 1: MASK, 2: BLEND
		float AlphaCutoff;
		uint32_t Flags; // bit 0: doubleSided
		uint32_t DebugView;
	};
	static_assert(sizeof(MaterialGPU) == 144);
	static constexpr uint32_t MaxMaterialCapacity = 256;
	static constexpr uint32_t MaxLightCapacity = 64;

	struct ViewGPU
	{
		Matrix ViewMat;
		Matrix ProjMat;
		Matrix InvViewMat;
		Matrix InvProjMat;
		Matrix PreviousViewMat;
		Matrix PreviousRasterViewProj;
		Vector4 CameraPos;
		Vector4 PreviousDepthReconstructionParams;
		float Near;
		float Far;
		float FovRadians;
		float Aspect;
		Vector2 CurrentJitterUV;
		Vector2 PreviousJitterUV;
		float ExposureMultiplier;
		uint32_t Width;
		uint32_t Height;
		uint32_t DepthConvention;
		uint32_t PreviousDepthConvention;
		uint32_t Padding[3];
	};
	struct GPUAbiMember
	{
		std::string_view m_Name;
		size_t m_Offset = 0;
	};
	inline constexpr std::array ObjectGPUAbiMembers = {
		GPUAbiMember{ "ModelMat", offsetof(ObjectGPU, ModelMat) },
		GPUAbiMember{ "PreviousModelMat", offsetof(ObjectGPU, PreviousModelMat) },
		GPUAbiMember{ "NormalMat", offsetof(ObjectGPU, NormalMat) },
		GPUAbiMember{ "MaterialIndex", offsetof(ObjectGPU, MaterialIndex) },
		GPUAbiMember{ "ViewIndex", offsetof(ObjectGPU, ViewIndex) },
		GPUAbiMember{ "Padding", offsetof(ObjectGPU, Padding) },
	};
	inline constexpr size_t ObjectGPUAbiStride = sizeof(ObjectGPU);
	static_assert(std::is_standard_layout_v<ObjectGPU>);
	static_assert(offsetof(ObjectGPU, ModelMat) == 0);
	static_assert(offsetof(ObjectGPU, PreviousModelMat) == 64);
	static_assert(offsetof(ObjectGPU, NormalMat) == 128);
	static_assert(offsetof(ObjectGPU, MaterialIndex) == 192);
	static_assert(offsetof(ObjectGPU, ViewIndex) == 196);
	static_assert(offsetof(ObjectGPU, Padding) == 200);
	static_assert(sizeof(ObjectGPU) == 208);
	inline constexpr std::array ViewGPUAbiMembers = {
		GPUAbiMember{ "ViewMat", offsetof(ViewGPU, ViewMat) },
		GPUAbiMember{ "ProjMat", offsetof(ViewGPU, ProjMat) },
		GPUAbiMember{ "InvViewMat", offsetof(ViewGPU, InvViewMat) },
		GPUAbiMember{ "InvProjMat", offsetof(ViewGPU, InvProjMat) },
		GPUAbiMember{ "PreviousViewMat", offsetof(ViewGPU, PreviousViewMat) },
		GPUAbiMember{ "PreviousRasterViewProj", offsetof(ViewGPU, PreviousRasterViewProj) },
		GPUAbiMember{ "CameraPos", offsetof(ViewGPU, CameraPos) },
		GPUAbiMember{ "PreviousDepthReconstructionParams",
			offsetof(ViewGPU, PreviousDepthReconstructionParams) },
		GPUAbiMember{ "Near", offsetof(ViewGPU, Near) },
		GPUAbiMember{ "Far", offsetof(ViewGPU, Far) },
		GPUAbiMember{ "FovRadians", offsetof(ViewGPU, FovRadians) },
		GPUAbiMember{ "Aspect", offsetof(ViewGPU, Aspect) },
		GPUAbiMember{ "CurrentJitterUV", offsetof(ViewGPU, CurrentJitterUV) },
		GPUAbiMember{ "PreviousJitterUV", offsetof(ViewGPU, PreviousJitterUV) },
		GPUAbiMember{ "ExposureMultiplier", offsetof(ViewGPU, ExposureMultiplier) },
		GPUAbiMember{ "Width", offsetof(ViewGPU, Width) },
		GPUAbiMember{ "Height", offsetof(ViewGPU, Height) },
		GPUAbiMember{ "DepthConvention", offsetof(ViewGPU, DepthConvention) },
		GPUAbiMember{ "PreviousDepthConvention", offsetof(ViewGPU, PreviousDepthConvention) },
		GPUAbiMember{ "Padding", offsetof(ViewGPU, Padding) },
	};
	inline constexpr size_t ViewGPUAbiStride = sizeof(ViewGPU);
	static_assert(std::is_standard_layout_v<ViewGPU>);
	static_assert(offsetof(ViewGPU, ViewMat) == 0);
	static_assert(offsetof(ViewGPU, ProjMat) == 64);
	static_assert(offsetof(ViewGPU, InvViewMat) == 128);
	static_assert(offsetof(ViewGPU, InvProjMat) == 192);
	static_assert(offsetof(ViewGPU, PreviousViewMat) == 256);
	static_assert(offsetof(ViewGPU, PreviousRasterViewProj) == 320);
	static_assert(offsetof(ViewGPU, CameraPos) == 384);
	static_assert(offsetof(ViewGPU, PreviousDepthReconstructionParams) == 400);
	static_assert(offsetof(ViewGPU, Near) == 416);
	static_assert(offsetof(ViewGPU, Far) == 420);
	static_assert(offsetof(ViewGPU, FovRadians) == 424);
	static_assert(offsetof(ViewGPU, Aspect) == 428);
	static_assert(offsetof(ViewGPU, CurrentJitterUV) == 432);
	static_assert(offsetof(ViewGPU, PreviousJitterUV) == 440);
	static_assert(offsetof(ViewGPU, ExposureMultiplier) == 448);
	static_assert(offsetof(ViewGPU, Width) == 452);
	static_assert(offsetof(ViewGPU, Height) == 456);
	static_assert(offsetof(ViewGPU, DepthConvention) == 460);
	static_assert(offsetof(ViewGPU, PreviousDepthConvention) == 464);
	static_assert(offsetof(ViewGPU, Padding) == 468);
	static_assert(sizeof(ViewGPU) == 480);
	static constexpr uint32_t MaxViewCapacity =
		static_cast<uint32_t>(utils::ToIndex(RenderViewID::Count)) * 8;
}
