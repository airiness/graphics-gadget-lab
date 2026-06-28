#pragma once
#include "Graphics/RenderContexts.h"
#include "Graphics/RenderPass/RenderPassInfo.h"

#include <string>
#include <string_view>
#include <utility>

namespace gglab
{
	class RenderGraph;
	class RenderPassBase
	{
	public:
		virtual ~RenderPassBase() = default;
		const RenderPassInfo& GetInfo() const noexcept { return m_Info; }
		virtual void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) = 0;

	protected:
		explicit RenderPassBase(RenderPassInfo info) noexcept :
			m_Info(std::move(info))
		{}

		const char* GetRenderGraphPassName() const noexcept
		{
			return m_Info.m_TypeName.c_str();
		}

		std::string MakeRenderGraphPassName(std::string_view suffix) const
		{
			if (suffix.empty())
			{
				return m_Info.m_TypeName;
			}
			std::string result = m_Info.m_TypeName;
			result += '.';
			result += suffix;
			return result;
		}

	private:
		RenderPassInfo m_Info;
	};
}
