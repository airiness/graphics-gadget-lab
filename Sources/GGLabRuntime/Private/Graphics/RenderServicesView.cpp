#include "GGLabRuntime/Graphics/RenderServicesView.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace gglab
{
	struct RenderServicesPublicationState
	{
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		RenderServices m_Bindings;
		size_t m_ActiveBorrows = 0;
		bool m_Live = true;
	};

	namespace
	{
		[[noreturn]] void FailPublicationContract(const char* reason) noexcept
		{
			std::fprintf(stderr, "RenderServices publication contract failure: %s\n", reason);
			std::fflush(stderr);
			std::abort();
		}

		bool IsOwnerThread(const RenderServicesPublicationState& state) noexcept
		{
			// This check precedes every read of mutable publication state.
			if (state.m_OwnerThread != std::this_thread::get_id())
			{
				std::fputs("RenderServicesView rejected a non-owner thread.\n", stderr);
				return false;
			}
			return true;
		}
	}

	bool RenderServicesView::IsValid() const noexcept
	{
		const auto state = m_State.lock();
		return state && IsOwnerThread(*state) && state->m_Live &&
			state->m_Bindings.HasRequiredServices();
	}

	bool RenderServicesView::TryUseImpl(void* context,
		void (*invoke)(void*, const RenderServices&) noexcept) const noexcept
	{
		const auto state = m_State.lock();
		if (!state || !IsOwnerThread(*state) || !state->m_Live ||
			!state->m_Bindings.HasRequiredServices())
		{
			return false;
		}
		++state->m_ActiveBorrows;
		invoke(context, state->m_Bindings);
		--state->m_ActiveBorrows;
		return true;
	}

	RenderServicesPublication::RenderServicesPublication(const RenderServices& bindings) noexcept :
		m_State(std::make_shared<RenderServicesPublicationState>())
	{
		m_State->m_Bindings = bindings;
		m_State->m_Bindings.m_TextureAssets = nullptr;
		m_State->m_Bindings.m_OverlayExtension = nullptr;
	}

	RenderServicesPublication::~RenderServicesPublication() noexcept
	{
		Revoke();
	}

	RenderServicesView RenderServicesPublication::GetView() const noexcept
	{
		return RenderServicesView(m_State);
	}

	void RenderServicesPublication::CheckLifecycleAccess() const noexcept
	{
		if (!IsOwnerThread(*m_State))
		{
			FailPublicationContract("lifecycle operation requires the owner thread");
		}
		if (m_State->m_ActiveBorrows != 0)
		{
			FailPublicationContract("lifecycle operation during a service borrow");
		}
	}

	void RenderServicesPublication::Revoke() noexcept
	{
		CheckLifecycleAccess();
		m_State->m_Live = false;
		m_State->m_Bindings = {};
	}
}
