#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

#include <functional>
#include <memory>
#include <type_traits>

namespace gglab
{
	struct RenderServicesPublicationState;

	// Copyable, non-owning publication capability. Queries and synchronous borrows
	// run on the publication's owner thread. A callback may copy owned results out,
	// but must not retain service pointers or invoke host/application lifecycle work.
	// Example: view.TryUse([&](const RenderServices& services) noexcept {
	//     diagnostics = services.m_Temporal->GetTemporalHistoryDiagnostics();
	// });
	class RenderServicesView
	{
	public:
		RenderServicesView() noexcept = default;
		[[nodiscard]] bool IsValid() const noexcept;

		template <typename Callback>
			requires std::is_nothrow_invocable_v<Callback&, const RenderServices&>
		bool TryUse(Callback&& callback) const noexcept
		{
			auto invoke = [&callback](const RenderServices& services) noexcept
				{
					std::invoke(callback, services);
				};
			return TryUseImpl(&invoke, [](void* context, const RenderServices& services) noexcept
				{
					(*static_cast<decltype(invoke)*>(context))(services);
				});
		}

	private:
		friend class RenderServicesPublication;
		explicit RenderServicesView(
			const std::shared_ptr<RenderServicesPublicationState>& state) noexcept : m_State(state) {}
		bool TryUseImpl(void* context,
			void (*invoke)(void*, const RenderServices&) noexcept) const noexcept;

		std::weak_ptr<RenderServicesPublicationState> m_State;
	};

	// Runtime composition owns publication metadata, never the bound services.
	// Revocation is permanent; create a new owner for a new host composition.
	class RenderServicesPublication
	{
	public:
		explicit RenderServicesPublication(const RenderServices& bindings) noexcept;
		~RenderServicesPublication() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderServicesPublication);

		[[nodiscard]] RenderServicesView GetView() const noexcept;
		void CheckLifecycleAccess() const noexcept;
		void Revoke() noexcept;

	private:
		std::shared_ptr<RenderServicesPublicationState> m_State;
	};
}
