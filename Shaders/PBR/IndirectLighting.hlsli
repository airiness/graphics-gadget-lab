#pragma once
#include <PBR/MaterialShading.hlsli>
#include <PBR/AnisotropicIBL.hlsli>

struct MaterialIBLSamples
{
	float3 Irradiance;
	float3 BaseSpecular;
	float3 ClearcoatSpecular;
};

struct MaterialIBLResponse
{
	float3 Diffuse;
	float3 Specular;
};

float3 GetMaterialIBLReflection(PreparedMaterialShading material)
{
	// One bent reflection retains the existing anisotropic approximation and
	// avoids duplicating sharp environment landmarks with offset texture taps.
	return material.Anisotropy.Strength > 0.0
		? GetAnisotropicIBLReflection(material.Base.NormalWS, material.ViewDirectionWS,
			material.Anisotropy.BitangentWS, material.Anisotropy.AlphaT, material.Anisotropy.AlphaB)
		: reflect(-material.ViewDirectionWS, material.Base.NormalWS);
}

MaterialIBLResponse EvaluateBaseMaterialIBL(PreparedMaterialShading material,
	MaterialIBLSamples environment)
{
	MaterialIBLResponse response;
	response.Diffuse = environment.Irradiance * material.DiffuseWeight * Fd_Lambert(material.BaseColor);
	response.Specular = environment.BaseSpecular * material.SpecularDirectionalAlbedo;
	return response;
}

void ApplyClearcoatIBL(inout MaterialIBLResponse response, ClearcoatShadingState coat,
	float3 clearcoatEnvironment)
{
	// The caller shares the active-coat gate with the environment fetch. Keep
	// both base crossings and the additive coat response in this one layer helper.
	const float baseTransmission = 1.0 - coat.Factor * coat.DirectionalAlbedo;
	response.Diffuse *= baseTransmission * baseTransmission;
	response.Specular *= baseTransmission * baseTransmission;
	response.Specular += clearcoatEnvironment * (coat.Factor * coat.DirectionalAlbedo);
}
