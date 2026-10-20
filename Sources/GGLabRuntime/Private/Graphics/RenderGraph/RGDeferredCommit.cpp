#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

#include <utility>

namespace gglab
{
	RGDeferredCommitList::~RGDeferredCommitList() noexcept
	{
		Discard();
	}

	void RGDeferredCommitList::Fail(const char* reason) noexcept
	{
		m_Failed = true;
		GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("RenderGraph deferred commit contract failure: {}", reason);
	}

	void RGDeferredCommitList::Enqueue(RGDeferredCommit commit) noexcept
	{
		if (m_State != State::Open)
		{
			Fail("enqueue requires an open recording list");
			return;
		}
		if (!commit.m_Callable)
		{
			Fail("cannot enqueue a moved-from commit");
			return;
		}
		m_Commits.push_back(std::move(commit));
	}

	void RGDeferredCommitList::Seal() noexcept
	{
		if (m_State == State::Open)
		{
			m_State = State::Sealed;
		}
	}

	void RGDeferredCommitList::CommitAll(const RGCommitContext& context) noexcept
	{
		if (m_State == State::Settling)
		{
			Fail("cannot reenter commit settlement");
			return;
		}
		if (!context.m_Fence.IsValid())
		{
			Fail("commit settlement requires a valid submission fence");
			Discard();
			return;
		}
		if (m_State == State::Settled)
		{
			return;
		}
		if (m_Failed)
		{
			Discard();
			return;
		}

		m_State = State::Settling;
		auto commits = std::move(m_Commits);
		for (auto& commit : commits)
		{
			auto callable = std::move(commit.m_Callable);
			callable->Invoke(context);
		}
		m_State = State::Settled;
	}

	void RGDeferredCommitList::Discard() noexcept
	{
		if (m_State == State::Settling)
		{
			Fail("cannot discard commits during settlement");
			return;
		}
		// Close before destructing captures so reentrant enqueue is rejected as well.
		m_State = State::Settled;
		m_Commits.clear();
	}
}
