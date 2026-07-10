#pragma once

// Environment textures are baked in their source orientation. Runtime yaw is
// applied by transforming world-space directions back into environment space.
float3 WorldToEnvironmentDirection(float3 directionWS, float rotationRadians)
{
	float sineRotation;
	float cosineRotation;
	sincos(-rotationRadians, sineRotation, cosineRotation);

	return normalize(float3(
		cosineRotation * directionWS.x + sineRotation * directionWS.z,
		directionWS.y,
		-sineRotation * directionWS.x + cosineRotation * directionWS.z));
}
