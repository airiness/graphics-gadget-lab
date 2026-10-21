#pragma once
#include "Graphics/Capture/CapturePayloadBudget.h"

#include <memory>
#include <utility>

namespace gglab
{
	// The aliased public value carries private accounting without exposing Runtime
	// implementation types. Destroy data before returning credit on the last consumer.
	template <class Product>
	struct CaptureProductStorage
	{
		CapturePayloadBudget::Lease m_Lease;
		Product m_Product;
	};

	template <class Product>
	[[nodiscard]] std::shared_ptr<const Product> MakeCaptureProduct(
		Product product, CapturePayloadBudget::Lease lease)
	{
		auto storage = std::make_shared<CaptureProductStorage<Product>>(
			std::move(lease), std::move(product));
		return { storage, &storage->m_Product };
	}
}
