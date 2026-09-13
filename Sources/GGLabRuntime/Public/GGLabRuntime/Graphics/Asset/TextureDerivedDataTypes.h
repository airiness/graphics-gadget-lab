#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Asset/AssetContentFingerprint.h"
#include "GGLabRuntime/Graphics/Asset/TextureArtifact.h"

#include <cstdint>
#include <memory>
#include <string>

namespace gglab
{
	struct TextureDerivedDataCoordinatorCore;
	struct TextureDerivedDataRequestState;

	enum class ArtifactRequestDisposition : uint8_t
	{
		Hit,
		Waiting,
		BuildRequired,
	};

	struct TextureDerivedDataArtifact
	{
		TextureArtifactHandle m_Artifact;
		AssetContentFingerprint m_ContentFingerprint{};
		bool m_DerivedDataCacheHit = false;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Artifact && m_Artifact->IsValid() && m_ContentFingerprint.IsValid();
		}
	};

	class TextureArtifactWaiterHandle final
	{
	public:
		TextureArtifactWaiterHandle() noexcept = default;
		TextureArtifactWaiterHandle(TextureArtifactWaiterHandle&& other) noexcept;
		TextureArtifactWaiterHandle& operator=(TextureArtifactWaiterHandle&& other) noexcept;
		GGLAB_DELETE_COPYABLE(TextureArtifactWaiterHandle);
		~TextureArtifactWaiterHandle();

		[[nodiscard]] bool IsValid() const noexcept;
		bool Cancel() noexcept;

	private:
		friend class TextureDerivedDataSystem;
		TextureArtifactWaiterHandle(std::shared_ptr<TextureDerivedDataCoordinatorCore> core,
			std::shared_ptr<TextureDerivedDataRequestState> state) noexcept;
		void Release(bool cancelled) noexcept;

		std::shared_ptr<TextureDerivedDataCoordinatorCore> m_Core;
		std::shared_ptr<TextureDerivedDataRequestState> m_State;
	};

	class TextureArtifactBuildClaim final
	{
	public:
		TextureArtifactBuildClaim() noexcept = default;
		TextureArtifactBuildClaim(TextureArtifactBuildClaim&& other) noexcept;
		TextureArtifactBuildClaim& operator=(TextureArtifactBuildClaim&& other) noexcept;
		GGLAB_DELETE_COPYABLE(TextureArtifactBuildClaim);
		~TextureArtifactBuildClaim();

		[[nodiscard]] bool IsValid() const noexcept;

	private:
		friend class TextureDerivedDataSystem;
		friend bool FinishTextureDerivedDataBuild(TextureArtifactBuildClaim& claim,
			TextureDerivedDataArtifact artifact, std::string error) noexcept;
		TextureArtifactBuildClaim(std::shared_ptr<TextureDerivedDataCoordinatorCore> core,
			std::shared_ptr<TextureDerivedDataRequestState> state, uint64_t serial) noexcept;
		void Abandon() noexcept;

		std::shared_ptr<TextureDerivedDataCoordinatorCore> m_Core;
		std::shared_ptr<TextureDerivedDataRequestState> m_State;
		uint64_t m_Serial = 0;
	};

	struct TextureDerivedDataRequestResult
	{
		ArtifactRequestDisposition m_Disposition = ArtifactRequestDisposition::Waiting;
		TextureDerivedDataArtifact m_Artifact;
		TextureArtifactWaiterHandle m_Waiter;
		TextureArtifactBuildClaim m_BuildClaim;
	};

	enum class ArtifactWaitDisposition : uint8_t
	{
		Succeeded,
		Failed,
		Cancelled,
	};

	struct TextureArtifactWaitResult
	{
		ArtifactWaitDisposition m_Disposition = ArtifactWaitDisposition::Failed;
		TextureDerivedDataArtifact m_Artifact;
		std::string m_Error;
	};
}
