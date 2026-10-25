#include "RenderServicesViewSelfTests.h"
#include "GGLabRuntime/Graphics/RenderServicesView.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <latch>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace gglab
{
	namespace
	{
		RenderServices CompleteBindings() noexcept
		{
			// Shape-only fixture. Tests never dispatch through these pointer sentinels.
			return {
				.m_PipelineResolver = reinterpret_cast<RenderPipelineResolver*>(uintptr_t{ 1 }),
				.m_ShaderPrograms = reinterpret_cast<RenderShaderProgramAccess*>(uintptr_t{ 1 }),
				.m_Samplers = reinterpret_cast<RenderSamplerAccess*>(uintptr_t{ 1 }),
				.m_Resources = reinterpret_cast<RenderResourceRegistryAccess*>(uintptr_t{ 1 }),
				.m_TextureAssets = reinterpret_cast<RenderTextureAssetAccess*>(uintptr_t{ 1 }),
				.m_FrameBuffers = reinterpret_cast<RenderFrameBufferAccess*>(uintptr_t{ 1 }),
				.m_Environment = reinterpret_cast<RenderEnvironmentAccess*>(uintptr_t{ 1 }),
				.m_Presentation = reinterpret_cast<RenderPresentationAccess*>(uintptr_t{ 1 }),
				.m_BindingLayout = reinterpret_cast<RenderBindingLayoutAccess*>(uintptr_t{ 1 }),
				.m_Temporal = reinterpret_cast<RenderTemporalAccess*>(uintptr_t{ 1 }),
				.m_AssetUpload = reinterpret_cast<AssetUploadScheduling*>(uintptr_t{ 1 }),
				.m_OverlayExtension = reinterpret_cast<RenderPipelineOverlayExtensionBase*>(uintptr_t{ 1 }),
			};
		}
	}

	int RunRenderServicesViewChild(std::string_view scenario) noexcept
	{
		if (scenario != "revoke" && scenario != "destroy" && scenario != "lifecycle" &&
			scenario != "wrong-thread")
		{
			return 2;
		}
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		auto owner = std::make_unique<RenderServicesPublication>(CompleteBindings());
		if (scenario == "wrong-thread")
		{
			std::thread worker([&]() { owner->Revoke(); });
			worker.join();
			return 91;
		}
		owner->GetView().TryUse([&](const RenderServices&) noexcept
			{
				if (scenario == "revoke")
				{
					owner->Revoke();
				}
				else if (scenario == "destroy")
				{
					owner.reset();
				}
				else
				{
					owner->CheckLifecycleAccess();
				}
			});
		return 92;
	}

	void RunRenderServicesViewSelfTests(SelfTestContext& context) noexcept
	{
		uint32_t calls = 0;
		const auto count = [&](const RenderServices&) noexcept { ++calls; };
		RenderServicesView absent;
		context.Check(!absent.IsValid() && !absent.TryUse(count) && calls == 0,
			"A default service view rejects without invoking its callback");
		RenderServicesPublication incomplete({});
		context.Check(!incomplete.GetView().IsValid() && !incomplete.GetView().TryUse(count),
			"Incomplete stable bindings cannot publish a usable service view");
		RenderServicesView retained;
		{
			RenderServicesPublication owner(CompleteBindings());
			retained = owner.GetView();
			const auto copy = retained;
			bool injectionsAbsent = false;
			context.Check(retained.IsValid() && copy.TryUse([&](const RenderServices& services) noexcept
				{
					injectionsAbsent = !services.m_TextureAssets && !services.m_OverlayExtension;
					retained.TryUse(count);
				}) && injectionsAbsent && calls == 1,
				"Copied views permit nested synchronous queries without retaining frame injections");
			owner.Revoke();
			owner.Revoke();
			context.Check(!retained.IsValid() && !copy.TryUse(count) && calls == 1,
				"Revocation invalidates every saved view and is idempotent");
		}
		context.Check(!retained.IsValid() && !retained.TryUse(count),
			"A saved view remains safely invalid after its publication owner is destroyed");
		{
			std::optional<RenderServicesPublication> owner;
			owner.emplace(CompleteBindings());
			const auto old = owner->GetView();
			const auto* address = &*owner;
			owner.reset();
			owner.emplace(CompleteBindings());
			context.Check(address == &*owner && !old.IsValid() && owner->GetView().IsValid(),
				"Reusing the publication owner's address cannot revive an old view");
		}
		{
			struct TemporalOwner final : RenderTemporalAccess
			{
				void InvalidateTemporalHistoryAfterResolveProgramChange() noexcept override {}
				TemporalHistoryManagerDiagnostics GetTemporalHistoryDiagnostics() const override { return {}; }
			};
			auto resourceOwner = std::make_shared<TemporalOwner>();
			const std::weak_ptr<TemporalOwner> weak = resourceOwner;
			auto bindings = CompleteBindings();
			bindings.m_Temporal = resourceOwner.get();
			RenderServicesPublication owner(bindings);
			const auto view = owner.GetView();
			const bool unowned = resourceOwner.use_count() == 1 && view.IsValid();
			owner.Revoke();
			resourceOwner.reset();
			context.Check(unowned && weak.expired() && !view.IsValid(),
				"Saved service views retain no resource owner after publication revocation");
		}
		{
			auto owner = std::make_unique<RenderServicesPublication>(CompleteBindings());
			const auto view = owner->GetView();
			std::latch started(1);
			std::atomic_uint32_t wrongThreadCallbacks = 0;
			std::atomic_bool rejected = true;
			std::thread worker([view, &started, &wrongThreadCallbacks, &rejected]() noexcept
				{
					for (uint32_t iteration = 0; iteration < 32; ++iteration)
					{
						if (view.IsValid() || view.TryUse([&](const RenderServices&) noexcept
							{
								++wrongThreadCallbacks;
							}))
						{
							rejected = false;
						}
						if (iteration == 0)
						{
							started.count_down();
						}
					}
				});
			started.wait();
			owner->Revoke();
			owner.reset();
			worker.join();
			context.Check(rejected && wrongThreadCallbacks == 0 && !view.IsValid(),
				"A transferred view rejects off-thread use while its owner revokes and releases metadata");
		}
		for (const auto scenario : { "revoke", "destroy", "lifecycle", "wrong-thread" })
		{
			const auto child = RunSelfTestChild("--render-services-child", scenario);
			const std::string_view reason = std::string_view(scenario) == "wrong-thread" ?
				"lifecycle operation requires the owner thread" :
				"lifecycle operation during a service borrow";
			context.Check(child.m_Completed && child.m_ExitCode != 0 &&
				child.m_Output.find(reason) != std::string::npos,
				"Service lifecycle misuse fails before teardown in a bounded child");
		}
	}
}
