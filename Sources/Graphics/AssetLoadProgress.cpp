#include "Core/Precompiled.h"
#include "Graphics/AssetLoadProgress.h"

namespace gglab
{
	namespace
	{
		std::string_view GetCpuProcessingStage(AssetLoadKind kind) noexcept
		{
			switch (kind)
			{
			case AssetLoadKind::Model: return "Parsing model and decoding textures";
			case AssetLoadKind::Texture: return "Decoding texture and preparing mip data";
			case AssetLoadKind::Mesh: return "Building mesh data";
			case AssetLoadKind::Generic:
			default:
				return "Processing asset on CPU";
			}
		}

		std::string_view GetGpuProcessingStage(AssetLoadKind kind) noexcept
		{
			switch (kind)
			{
			case AssetLoadKind::Model: return "Uploading model resources and generating mips";
			case AssetLoadKind::Texture: return "Uploading texture and generating mips";
			case AssetLoadKind::Mesh: return "Uploading mesh buffers";
			case AssetLoadKind::Generic:
			default:
				return "Processing asset on GPU";
			}
		}
	}

	AssetLoadProgress GetAssetLoadProgress(
		AssetState state,
		AssetLoadKind kind) noexcept
	{
		switch (state)
		{
		case AssetState::Unloaded:
			return { state, 0.0f, "Waiting for asset request" };
		case AssetState::Queued:
			return { state, 0.05f, "Queued for CPU processing" };
		case AssetState::LoadingCpu:
			return { state, 0.25f, GetCpuProcessingStage(kind) };
		case AssetState::CpuReady:
			return { state, 0.55f, "CPU processing complete" };
		case AssetState::UploadQueued:
			return { state, 0.65f, "Queued for GPU upload" };
		case AssetState::GpuProcessing:
			return { state, 0.85f, GetGpuProcessingStage(kind) };
		case AssetState::Ready:
			return { state, 1.0f, "Asset ready" };
		case AssetState::Failed:
			return { state, 0.0f, "Asset loading failed" };
		case AssetState::Cancelled:
			return { state, 0.0f, "Asset loading cancelled" };
		}
		return { state, 0.0f, "Unknown asset state" };
	}
}
