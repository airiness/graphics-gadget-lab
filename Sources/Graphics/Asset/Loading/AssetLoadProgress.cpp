#include "Core/Precompiled.h"
#include "Graphics/Asset/Loading/AssetLoadProgress.h"

namespace gglab
{
	namespace
	{
		std::string_view GetCpuProcessingStage(AssetLoadKind kind) noexcept
		{
			switch (kind)
			{
			case AssetLoadKind::Model:
				return "Parsing model and decoding textures";
			case AssetLoadKind::Texture:
				return "Decoding texture and preparing mip data";
			case AssetLoadKind::Mesh:
				return "Building mesh data";
			case AssetLoadKind::Generic:
			default:
				return "Processing asset on CPU";
			}
		}

		std::string_view GetGpuProcessingStage(AssetLoadKind kind) noexcept
		{
			switch (kind)
			{
			case AssetLoadKind::Model:
				return "Uploading model GPU resources";
			case AssetLoadKind::Texture:
				return "Uploading texture GPU resources";
			case AssetLoadKind::Mesh:
				return "Uploading mesh buffers";
			case AssetLoadKind::Generic:
			default:
				return "Processing asset on GPU";
			}
		}
	}

	AssetLoadProgress GetAssetLoadProgress(
		AssetState state, AssetLoadKind kind, const ProgressChannelPtr& progress) noexcept
	{
		if (progress)
		{
			const ProgressSnapshot snapshot = progress->GetSnapshot();
			if (snapshot.HasProgress())
			{
				return {
					.m_State = state,
					.m_Fraction = state == AssetState::Ready ? 1.0f : snapshot.m_Fraction,
					.m_Stage = snapshot.m_Stage,
					.m_Detail = snapshot.m_Detail,
				};
			}
		}

		switch (state)
		{
		case AssetState::Unloaded:
			return { state, 0.0f, "Waiting for asset request", {} };
		case AssetState::Queued:
			return { state, 0.05f, "Queued for CPU processing", {} };
		case AssetState::LoadingCpu:
			return { state, 0.25f, std::string(GetCpuProcessingStage(kind)), {} };
		case AssetState::CpuReady:
			return { state, 0.55f, "CPU processing complete", {} };
		case AssetState::Publishing:
			return { state, 0.62f, "Publishing asset resources", {} };
		case AssetState::UploadQueued:
			return { state, 0.65f, "Queued for GPU upload", {} };
		case AssetState::GpuProcessing:
			return { state, 0.85f, std::string(GetGpuProcessingStage(kind)), {} };
		case AssetState::Ready:
			return { state, 1.0f, "Asset ready", {} };
		case AssetState::Evicting:
			return { state, 1.0f, "Retiring GPU residency", {} };
		case AssetState::Failed:
			return { state, 0.0f, "Asset loading failed", {} };
		case AssetState::Cancelled:
			return { state, 0.0f, "Asset loading cancelled", {} };
		}
		return { state, 0.0f, "Unknown asset state", {} };
	}
}
