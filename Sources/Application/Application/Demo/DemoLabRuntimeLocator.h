#pragma once
#include "Lab/LabInterfaces.h"

namespace gglab
{
	class DemoManager;
	class DemoLabHost;

	class DemoLabRuntimeLocator final : public LabRuntimeLocatorBase
	{
	public:
		DemoLabRuntimeLocator(DemoManager* demoManager, uint32_t labHostIndex) noexcept;

		LabRuntime* GetLabRuntimeIfCreated() noexcept override;
		const LabRuntime* GetLabRuntimeIfCreated() const noexcept override;

	private:
		DemoLabHost* GetLabHost() const noexcept;

		DemoManager* m_DemoManager = nullptr;
		uint32_t m_LabHostIndex = 0;
	};
}
