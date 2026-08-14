#pragma once

#include <cstdint>
#include <string>

namespace gglab
{
	enum class RenderPassCategory : uint8_t
	{
		Unknown,
		Geometry,
		Lighting,
		Shadow,
		IBL,
		PostProcess,
		Debug,
		UI,
	};

	enum class RenderPassType : uint8_t
	{
		Graphics,
		Compute,
		Transfer,
		Mixed,
	};

	struct RenderPassInfo
	{
		std::string m_TypeName;
		std::string m_DisplayName;
		std::string m_CategoryName;
		std::string m_Description;

		RenderPassCategory m_Category = RenderPassCategory::Unknown;
		RenderPassType m_Type = RenderPassType::Graphics;

		bool m_ShowInDevelopGui = true;
		bool m_EnableGpuMarker = true;
		bool m_EnableProfiling = true;
	};
}
