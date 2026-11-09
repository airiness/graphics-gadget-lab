"""Validate a bounded probe observation against its frozen capture frame."""
import math


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(frame, schema):
    result = frame.get("giProbeInspection")
    if result is None:
        return
    schema.validate(result)
    stamp = result["observation"]
    identity = stamp["field"]
    summary = frame["readiness"]["giField"]["summary"]
    request = result["request"]
    require(stamp == frame["giProbeObservation"], "Probe and product observation stamps differ")
    require(stamp["producingFrame"] == frame["frame"]["serial"], "Probe producing frame differs from capture")
    require(identity == summary["field"], "Probe was relabelled with another field identity")
    require(request["expectedGeneration"] == identity["generation"], "Expected generation did not hold")
    require(request["expectedRefreshEpoch"] in (None, identity["refreshEpoch"]), "Expected epoch did not hold")
    require(stamp["fieldUpdate"] == summary["submittedUpdates"] + 1 and
            stamp["readinessUpdate"] == stamp["fieldUpdate"] - identity["epochOriginUpdate"],
            "Field-lifetime/epoch update mapping differs")
    for axis, coordinate in enumerate(request["coordinate"]):
        require(coordinate < summary["counts"][axis], "Probe coordinate is outside its volume")
        require(math.isclose(result["worldPosition"][axis], summary["minimum"][axis] +
                             summary["spacing"] * coordinate, abs_tol=1e-5, rel_tol=1e-6),
                "World position disagrees with the volume")
    width = result["irradianceTileWidth"]
    if result["validity"] == "MalformedPayload":
        require(not result["irradiance"] and not result["distance"], "Malformed payload contains decoded tiles")
        return
    require(len(result["irradiance"]) == width * width and len(result["distance"]) == 256,
            "Inspection tiles are incomplete")
    require((1 + width * width + 256) * 16 <= 20496, "Inspection exceeds its native GPU bound")
    if result["validity"] in ("Valid", "Inactive"):
        require(all(v is not None and math.isfinite(v) for tile in (result["irradiance"], result["distance"])
                    for texel in tile for v in texel), "Non-finite evidence was reported as valid")
        require(result["classification"] == (1 if result["validity"] == "Valid" else 2),
                "Validity does not match probe classification")
