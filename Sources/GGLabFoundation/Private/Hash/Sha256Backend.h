#pragma once

#include "FoundationPrivateAccess.h"
#include "GGLabFoundation/Hash/Sha256.h"

#include <memory>
#include <span>

namespace gglab::foundation::detail
{
	class Sha256Backend
	{
	public:
		virtual ~Sha256Backend() = default;

		[[nodiscard]] virtual bool IsValid() const noexcept = 0;
		virtual bool AddBytes(std::span<const std::byte> bytes) noexcept = 0;
		[[nodiscard]] virtual Sha256Digest Finish() noexcept = 0;
	};

	[[nodiscard]] std::unique_ptr<Sha256Backend> CreateSha256Backend() noexcept;
	[[nodiscard]] std::unique_ptr<Sha256Backend> CreatePortableSha256Backend() noexcept;
}
