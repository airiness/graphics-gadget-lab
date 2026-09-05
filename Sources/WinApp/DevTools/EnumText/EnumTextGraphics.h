#pragma once
#include "DevTools/EnumText/EnumText.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerTypes.h"

namespace gglab::devtools
{
	template <> struct EnumTextTraits<ModelType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ModelType::Invalid, "Invalid"},
			EnumTextEntry{ModelType::GlTF, "glTF"},
			EnumTextEntry{ModelType::Procedural, "Procedural"},
		};
	};

	template <> struct EnumTextTraits<AssetState>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{AssetState::Unloaded, "Unloaded"},
			EnumTextEntry{AssetState::Queued, "Queued"},
			EnumTextEntry{AssetState::LoadingCpu, "Loading CPU"},
			EnumTextEntry{AssetState::CpuReady, "CPU Ready"},
			EnumTextEntry{AssetState::Publishing, "Publishing"},
			EnumTextEntry{AssetState::UploadQueued, "Upload Queued"},
			EnumTextEntry{AssetState::GpuProcessing, "GPU Processing"},
			EnumTextEntry{AssetState::Ready, "Ready"},
			EnumTextEntry{AssetState::Failed, "Failed"},
			EnumTextEntry{AssetState::Cancelled, "Cancelled"},
		};
	};

	template <> struct EnumTextTraits<AssetContentState>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{AssetContentState::Unloaded, "Unloaded"},
			EnumTextEntry{AssetContentState::Loading, "Loading"},
			EnumTextEntry{AssetContentState::Ready, "Ready"},
			EnumTextEntry{AssetContentState::Failed, "Failed"},
			EnumTextEntry{AssetContentState::Cancelled, "Cancelled"},
		};
	};

	template <> struct EnumTextTraits<AssetResidencyState>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{AssetResidencyState::NonResident, "Non-resident"},
			EnumTextEntry{AssetResidencyState::Queued, "Queued"},
			EnumTextEntry{AssetResidencyState::Uploading, "Uploading"},
			EnumTextEntry{AssetResidencyState::Resident, "Resident"},
			EnumTextEntry{AssetResidencyState::Evicting, "Evicting"},
		};
	};

	template <> struct EnumTextTraits<AssetResidencyPolicy>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{AssetResidencyPolicy::Cacheable, "Cacheable"},
			EnumTextEntry{AssetResidencyPolicy::Pinned, "Pinned"},
		};
	};

	template <> struct EnumTextTraits<AssetUploadStatus>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{AssetUploadStatus::Pending, "Pending"},
			EnumTextEntry{AssetUploadStatus::Succeeded, "Succeeded"},
			EnumTextEntry{AssetUploadStatus::Failed, "Failed"},
		};
	};

	template <> struct EnumTextTraits<LightType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{LightType::Directional, "Directional"},
			EnumTextEntry{LightType::Spot, "Spot"},
			EnumTextEntry{LightType::Point, "Point"},
		};
	};

	template <> struct EnumTextTraits<TextureSemantic>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{TextureSemantic::BaseColor, "Base Color"},
			EnumTextEntry{TextureSemantic::Emissive, "Emissive"},
			EnumTextEntry{TextureSemantic::Normal, "Normal"},
			EnumTextEntry{TextureSemantic::MetallicRoughness, "Metallic Roughness"},
			EnumTextEntry{TextureSemantic::Occlusion, "Occlusion"},
			EnumTextEntry{TextureSemantic::UVTest, "UV Test"},
			EnumTextEntry{TextureSemantic::Environment, "Environment"},
			EnumTextEntry{TextureSemantic::GenericColor, "Generic Color"},
			EnumTextEntry{TextureSemantic::GenericData, "Generic Data"},
			EnumTextEntry{TextureSemantic::Unknown, "Unknown"},
		};
	};

	template <> struct EnumTextTraits<SamplerPreset>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{SamplerPreset::PointClamp, "Point Clamp"},
			EnumTextEntry{SamplerPreset::PointWrap, "Point Wrap"},
			EnumTextEntry{SamplerPreset::LinearClamp, "Linear Clamp"},
			EnumTextEntry{SamplerPreset::LinearWrap, "Linear Wrap"},
			EnumTextEntry{SamplerPreset::LinearWrapUClampV, "Linear Wrap U / Clamp V"},
			EnumTextEntry{SamplerPreset::AnisotropicClamp, "Anisotropic Clamp"},
			EnumTextEntry{SamplerPreset::AnisotropicWrap, "Anisotropic Wrap"},
			EnumTextEntry{SamplerPreset::ShadowCmpLinearClamp, "Shadow Compare Linear Clamp"},
		};
	};

	template <> struct EnumTextTraits<RenderViewID>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RenderViewID::Main, "Main"},
			EnumTextEntry{RenderViewID::DirectionalShadow, "DirectionalShadow"},
			EnumTextEntry{RenderViewID::DebugCamera0, "DebugCamera0"},
			EnumTextEntry{RenderViewID::DebugCamera1, "DebugCamera1"},
			EnumTextEntry{RenderViewID::DebugCamera2, "DebugCamera2"},
			EnumTextEntry{RenderViewID::Unknown, "Unknown"},
		};
	};

	template <> struct EnumTextTraits<RenderViewVisibilityMode>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RenderViewVisibilityMode::Self, "Self"},
			EnumTextEntry{RenderViewVisibilityMode::MainCamera, "Main Camera"},
			EnumTextEntry{
				RenderViewVisibilityMode::IntersectionWithMainCamera, "Intersection With Main"},
			EnumTextEntry{RenderViewVisibilityMode::None, "None"},
		};
	};

	template <> struct EnumTextTraits<RenderBucket>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RenderBucket::Opaque, "Opaque"},
			EnumTextEntry{RenderBucket::AlphaTest, "Alpha Test"},
			EnumTextEntry{RenderBucket::Transparent, "Transparent"},
		};
	};

	template <> struct EnumTextTraits<RenderResourceRegistry::IBLPreviewLayout>
	{
		using IBLPreviewLayout = RenderResourceRegistry::IBLPreviewLayout;

		static constexpr std::array Entries = {
			EnumTextEntry{IBLPreviewLayout::Grid2x3, "2x3 Grid"},
			EnumTextEntry{IBLPreviewLayout::Cross, "Cross"},
		};
	};
}
