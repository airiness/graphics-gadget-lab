#pragma once
#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/PostProcess/PostProcessColor.h"
#include "Graphics/PostProcess/PostProcessDebug.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHITypes.h"
#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"

#include <array>
#include <string>
#include <vector>

namespace gglab
{
	struct PostProcessTextureDiagnostics
	{
		uint64_t m_LogicalBytes = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_Format = RHIFormat::Unknown;
		PostProcessColorState m_ColorState = PostProcessColorState::SceneLinearRec709;
		float m_PreExposure = 1.0f;
		bool m_Available = false;
	};

	struct PostProcessGpuPassDiagnostics
	{
		std::string m_Name;
		double m_Milliseconds = 0.0;
		uint32_t m_CallCount = 0;
	};

	struct PostProcessPreviewDiagnostics
	{
		PostProcessDebugSelection m_Selected{};
		PostProcessDebugSelection m_Published{};
		RHIDescriptorHandle m_SrvDescriptor{};
		uint64_t m_UpdateCount = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_Format = RHIFormat::Unknown;
		float m_ExposureEV = 0.0f;
		bool m_Requested = false;
		bool m_HasPublished = false;
	};

	struct SceneDepthDiagnostics
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_ResourceFormat = RHIFormat::Unknown;
		RHIFormat m_DsvFormat = RHIFormat::Unknown;
		RHIFormat m_SrvFormat = RHIFormat::Unknown;
		float m_ClearDepth = 0.0f;
		DepthConvention m_Convention = DepthConvention::Standard;
		bool m_HasTypedClear = false;
		bool m_Available = false;
	};

	struct PostProcessDiagnosticsSnapshot
	{
		PostProcessTextureDiagnostics m_SceneColor{};
		SceneDepthDiagnostics m_SceneDepth{};
		PostProcessTextureDiagnostics m_BloomPrefilter{};
		std::array<PostProcessTextureDiagnostics, MaxBloomPyramidLevels> m_BloomPyramid{};
		uint32_t m_BloomLevelCount = 0;
		uint64_t m_BloomLogicalBytes = 0;
		PostProcessTextureDiagnostics m_BloomResult{};
		PostProcessPreviewDiagnostics m_Preview{};

		std::vector<PostProcessGpuPassDiagnostics> m_GpuPasses;
		uint64_t m_GpuFrameIndex = 0;
		double m_PostProcessGpuMilliseconds = 0.0;
		double m_BloomGpuMilliseconds = 0.0;
		bool m_GpuProfilerEnabled = false;
		bool m_GpuTimingAvailable = false;
	};

	template <> struct SnapshotTraits<PostProcessDiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.PostProcessDiagnosticsSnapshot");
	};
}
