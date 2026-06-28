#include "Core/Precompiled.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/ShaderCompiler.h"

namespace gglab
{
	ShaderManager::ShaderManager() noexcept
	{
		m_Compiler = std::make_unique<ShaderCompiler>();

		ShaderDesc defaultDesc{};
		defaultDesc.m_Flags |= IsDebuggerPresent() ? ShaderCompileFlags::Debug : ShaderCompileFlags::None;
		defaultDesc.m_IncludeDirs = { L"Assets/Shaders" };
		defaultDesc.m_Defines = {};
		m_Compiler->SetDefaultShaderConfig(defaultDesc);
	}

	void ShaderManager::SetDefaultShaderConfig(const ShaderDesc& defaultDesc) noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_Compiler->SetDefaultShaderConfig(defaultDesc);
	}

	ShaderID ShaderManager::LoadShader(const ShaderDesc& desc) noexcept
	{
		ShaderDesc norm = m_Compiler->NormalizeShaderDesc(desc);

		const auto keyHash = ShaderCompiler::ComputeRecipeHash(norm);
		ShaderKey key{ .m_KeyHash = keyHash };

		// return if exist.
		{
			std::shared_lock lock(m_Mutex);
			if (auto it = m_KeyIdMap.find(key); it != m_KeyIdMap.end())
			{
				return it->second;
			}
		}

		// create shader if not exist
		if (!std::filesystem::exists(norm.m_SourcePath))
		{
			GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::LoadShader: File not found: {}", norm.m_SourcePath.string());
			return ShaderID();
		}

		std::unique_ptr<Shader> shader = std::make_unique<Shader>(desc);
		if (!RefreshShaderInternal(*shader, norm))
		{
			GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::LoadShader: Shader compile failed: {}", norm.m_SourcePath.string());
			return ShaderID();
		}

		{
			std::unique_lock lock(m_Mutex);
			if (auto it = m_KeyIdMap.find(key); it != m_KeyIdMap.end())
			{
				return it->second;
			}

			ShaderID id{ static_cast<uint32_t>(m_Shaders.size()) };
			m_Shaders.push_back(std::move(shader));
			m_KeyIdMap.emplace(key, id);
			return id;
		}
	}

	void ShaderManager::Preload(const std::vector<ShaderDesc>& descList) noexcept
	{
		for (const auto& desc : descList)
		{
			LoadShader(desc);
		}
	}

	int32_t ShaderManager::RefreshChanged() noexcept
	{
		std::unique_lock lock(m_Mutex);
		int32_t count = 0;
		for (auto& shader : m_Shaders)
		{
			if (shader)
			{
				if (RefreshShaderInternal(*shader))
				{
					++count;
				}
				else
				{
					GGLAB_LOG_GRAPHICS_ERROR("RefreshChanged: recompile failed: {}", shader->GetDesc().m_SourcePath.string());
				}
			}
		}
		if (count > 0)
		{
			m_Revision.fetch_add(1, std::memory_order_relaxed);
		}
		return count;
	}

	bool ShaderManager::RefreshShader(ShaderID shaderId) noexcept
	{
		std::unique_lock lock(m_Mutex);
		if (!shaderId.IsValid() || shaderId.Value() >= m_Shaders.size() || !m_Shaders[shaderId.Value()])
		{
			return false;
		}
		const bool changed = RefreshShaderInternal(*m_Shaders[shaderId.Value()]);
		if (changed)
		{
			m_Revision.fetch_add(1, std::memory_order_relaxed);
		}
		return changed;
	}

	ShaderBytecode ShaderManager::GetBytecode(ShaderID shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (shaderId.IsValid() && shaderId.Value() < m_Shaders.size() && m_Shaders[shaderId.Value()])
		{
			return m_Shaders[shaderId.Value()]->GetBytecode();
		}
		GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::GetBytecode: Invalid shader ID {}", shaderId.Value());
		return {};
	}

	ShaderHash128 ShaderManager::GetHash(ShaderID shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (shaderId.IsValid() && shaderId.Value() < m_Shaders.size() && m_Shaders[shaderId.Value()])
		{
			return m_Shaders[shaderId.Value()]->GetCompileArtifact().m_Hash;
		}
		return {};
	}

	uint64_t ShaderManager::GetGeneration(ShaderID shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (shaderId.IsValid() && shaderId.Value() < m_Shaders.size() && m_Shaders[shaderId.Value()])
		{
			return m_Shaders[shaderId.Value()]->GetGeneration();
		}
		return 0;
	}

	bool ShaderManager::RefreshShaderInternal(Shader& shader) noexcept
	{
		ShaderDesc norm = m_Compiler->NormalizeShaderDesc(shader.GetDesc());
		return RefreshShaderInternal(shader, norm);
	}

	bool ShaderManager::RefreshShaderInternal(Shader& shader, const ShaderDesc& normalizedDesc) noexcept
	{
		ShaderCompileArtifact artifact = m_Compiler->CompileOrLoadArtifact(normalizedDesc);

		const auto changed = (shader.GetGeneration() == 0) ||
			(artifact.m_Hash != shader.GetCompileArtifact().m_Hash);

		std::error_code errorCode;
		artifact.m_SourceTimeStamp = std::filesystem::exists(normalizedDesc.m_SourcePath, errorCode) ?
			std::filesystem::last_write_time(normalizedDesc.m_SourcePath, errorCode) :
			std::filesystem::file_time_type{};

		shader.SetCompileArtifact(std::move(artifact), changed);
		return changed;
	}
}
