"""Independent CPU oracles and declared ROI gates for D=1 GI lighting contracts."""
import argparse
import json
import hashlib
from pathlib import Path
import struct
import zlib

import numpy as np

LUMA = np.array([0.2126, 0.7152, 0.0722])
ABSOLUTE = 1e-6
RELATIVE = 0.01
DEFAULTS = dict(fixture=0, depth=1, samples=64, batch_samples=64, seed=61051, albedo=0, ev100=10,
                environment=0, filter=0, intensity=1, yaw=0, sun_lux=10000,
                secondary_albedo=0, atmosphere=False)
# F6 reuses frozen F4. Its D=1 off-screen signal is the patch-mean sun component: the
# reference must resolve it (seed-to-seed difference at most one third, as for gated
# metrics), and it must exceed the 10% energy gate so that omitting it would fail.
F6_VIEWS = ("F6_PoseWest", "F6_PoseEast")
F6_EXCLUDED_WALL = {"F6_PoseWest": 2, "F6_PoseEast": 3}
F6_OPENING_INSTANCES = (5, 6, 7)
F6_SIGNAL_SAMPLING_FRACTION = 1/3
F6_SIGNAL_ENERGY_GATE = 0.10


def require(condition, message):
    if not condition:
        raise ValueError(message)


def relative_error(actual, expected):
    actual, expected = np.asarray(actual), np.asarray(expected)
    rgb = np.abs(actual - expected) / np.maximum(np.abs(expected), ABSOLUTE)
    luminance = abs(float((actual - expected) @ LUMA)) / max(abs(float(expected @ LUMA)), ABSOLUTE)
    return max(float(np.max(rgb)), luminance)


def wall_form_factor(x, z, height=4.0, plane_z=2.0, left=-2.0, right=2.0):
    """Integral of cos(receiver) cos(wall) / r^2 over a vertical rectangle.
    Integrate height analytically, then horizontal position using arctangents.
    This is independent of the GPU hemisphere estimator and ray query.
    """
    d = plane_z - z
    a, b = left - x, right - x
    q = np.sqrt(d*d + height*height)
    return 0.5 * ((np.arctan(b/d) - np.arctan(a/d)) -
                  d/q * (np.arctan(b/q) - np.arctan(a/q)))


def atmosphere_transmittance(settings, mu, altitude=2.0, order=192):
    """Independent float64 Gauss-Legendre optical-depth integration, in metres."""
    if settings is None:
        return np.ones(3)
    bottom = settings["planetRadiusMeters"]
    radius = bottom + altitude
    top = bottom + settings["heightMeters"]
    distance = -radius*mu + np.sqrt((radius*mu)**2 + top*top - radius*radius)
    nodes, weights = np.polynomial.legendre.leggauss(order)
    t = (nodes + 1) * distance/2
    h = np.sqrt(radius*radius + t*t + 2*radius*mu*t) - bottom
    rayleigh = np.asarray(settings["rayleighScattering"])[None, :] *         np.exp(-h/settings["rayleighScaleHeightMeters"])[:, None]
    mie = settings["mieExtinction"] * np.exp(-h/settings["mieScaleHeightMeters"])
    ozone = np.maximum(0, 1 - np.abs(h-settings["absorptionCenterMeters"]) /
                       settings["absorptionHalfWidthMeters"])
    extinction = rayleigh + mie[:, None] + ozone[:, None]*np.asarray(settings["absorption"])
    return np.exp(-(extinction * (weights*distance/2)[:, None]).sum(0))


def back_sky_factor(position, order=20):
    """Projected opening integral; both front-wall planes constrain ray slopes."""
    nodes, weights = np.polynomial.legendre.leggauss(order)
    result = np.empty(len(position))
    for start in range(0, len(position), 256):
        p = position[start:start+256]
        x, y = p[:, 0], p[:, 1]
        u0 = np.maximum((-1-x)/4, (-1-x)/4.2)
        u1 = np.minimum((1-x)/4, (1-x)/4.2)
        v0 = np.maximum(-y/4, -y/4.2)
        v1 = np.minimum((3-y)/4, (3-y)/4.2)
        u = (u0+u1)[:, None]/2 + (u1-u0)[:, None]/2*nodes
        v = (v0+v1)[:, None]/2 + (v1-v0)[:, None]/2*nodes
        integrand = 1/(1+u[:, :, None]**2+v[:, None, :]**2)**2
        result[start:start+len(p)] = np.einsum("ijk,j,k->i", integrand, weights, weights) *             (u1-u0)*(v1-v0)/4
    return result


def inverse_square_primitive(q, a2):
    a = np.sqrt(a2)
    return q/(2*a2*(q*q+a2)) + np.arctan(q/a)/(2*a2*a)


def back_sun_factor(position, order=128):
    """Projected sunlit floor, including the strip inside the opening thickness."""
    nodes, weights = np.polynomial.legendre.leggauss(order)
    result = np.zeros(len(position))
    for z0, z1 in ((-0.8, 2.0), (2.0, 2.2)):
        z = z0+(nodes+1)*(z1-z0)/2
        left = -1+0.2*(2.2-z)
        right = 1+0.2*(2-z)
        # For z>2 only the outer opening lies ahead of the secondary sun ray.
        if z0 >= 2:
            right = 1+0.2*(2.2-z)
        for start in range(0, len(position), 512):
            p = position[start:start+512]
            y, x = p[:, 1, None], p[:, 0, None]
            d = z[None, :]+2
            lo, hi = np.broadcast_to(left, (len(p), order)), np.broadcast_to(right, (len(p), order))
            if z0 >= 2:
                lo = np.maximum(lo, x+(-1-x)*d/4)
                hi = np.minimum(hi, x+(1-x)*d/4)
            a2 = d*d+y*y
            integral = inverse_square_primitive(hi-x, a2)-inverse_square_primitive(lo-x, a2)
            result[start:start+len(p)] += (integral*y*d*weights*(z1-z0)/2).sum(1)
    return result


def jamb_sun_factor(position, floor, order=64):
    """The right jamb's inward face x=1 is neutral and directly sunlit.
    Account for its sun shadow and the receiver's view through the inner opening.
    """
    nodes, weights = np.polynomial.legendre.leggauss(order)
    z = 2+(nodes+1)*0.1
    result = np.zeros(len(position))
    for start in range(0, len(position), 512):
        p = position[start:start+512]
        d = np.maximum(1-p[:, 0, None], 0)
        h = 3-(2.2-z[None, :])
        if floor:
            a2 = d*d+(z[None, :]-p[:, 2, None])**2
            integral = d/2*(1/a2-1/(a2+h*h))
        else:
            depth = z[None, :]+2
            h = np.minimum(h, 3+(3-p[:, 1, None])*(z[None, :]-2)/4)
            a2 = d*d+depth*depth
            y = p[:, 1, None]
            integral = d*depth*(inverse_square_primitive(h-y, a2)-inverse_square_primitive(-y, a2))
        result[start:start+len(p)] = (integral*weights*0.1).sum(1)
    return result


def f4_oracle(position, floor):
    if floor:
        x, z = position[:, 0], position[:, 2]
        d = 2-z
        outer = d+0.2
        left = np.maximum(-1, x+(-1-x)*outer/d)
        right = np.minimum(1, x+(1-x)*outer/d)
        sky = wall_form_factor(x, z, 3, 2.2, left, right)*0.1
        sun = np.zeros(len(x))
    else:
        sky = back_sky_factor(position)*0.1
        sun = back_sun_factor(position)*(0.8*10000/np.sqrt(2.04)/np.pi)
    sun += jamb_sun_factor(position, floor)*(0.8*10000*0.2/np.sqrt(2.04)/np.pi)
    return np.repeat(sun[:, None], 3, axis=1), np.repeat(sky[:, None], 3, axis=1)


def masks(case, values, camera):
    p = values["GI.Reference.Position"][..., :3]
    identity = values["GI.Reference.Identity"]
    covered = values["GI.Reference.Position"][..., 3] == 1
    x, y, z = (p[..., i] for i in range(3))
    if case["fixture"] in (5, 6, 8):
        return {"floor": covered & (identity[..., 0] == 0) & np.isin(identity[..., 1], [0, 1]) &
                (np.abs(x) <= 1) & (np.abs(z) <= 1)}
    if case["fixture"] == 7:
        v = np.asarray(camera["position"])-p
        v /= np.maximum(np.linalg.norm(v, axis=-1)[..., None], 1e-12)
        n = values["GI.Receiver.ShadingNormal"][..., :3]
        return {"sphere": covered & (identity[..., 0] == 0) & (np.sum(v*n, axis=-1) >= 0.5)}
    if case["view"] == "F4_Floor":
        mask = covered & (identity[..., 0] == 0) & np.isin(identity[..., 1], [4, 5]) &             (np.abs(x) <= 1.5) & (z >= -1.5) & (z <= 0.5)
        return {"floor": mask, "red-adjacent": mask & (x <= -0.5), "green-adjacent": mask & (x >= 0.5)}
    if case["view"] == "F4_Back":
        return {"back": covered & (identity[..., 0] == 4) & np.isin(identity[..., 1], [2, 3]) &
                (np.abs(x) <= 1.5) & (y >= 0.5) & (y <= 3.5)}
    if case["view"] in ("F4_West", "F4_East"):
        instance, triangles = (2, [10, 11]) if case["view"] == "F4_West" else (3, [8, 9])
        return {"wall": covered & (identity[..., 0] == instance) & np.isin(identity[..., 1], triangles) &
                (np.abs(z) <= 1.5) & (y >= 0.5) & (y <= 3.5)}
    if case["view"] in F6_VIEWS:
        return {"patch": covered & (identity[..., 0] == 0) & np.isin(identity[..., 1], [4, 5]) &
                (np.abs(x) <= 0.4) & (z >= -1.1) & (z <= -0.5)}
    return {"overview": covered}


def f6_signal_prerequisite(base, independent):
    """Evaluate one F6 pose from two independent-seed D=1 patch observations."""
    def luminance(observation, signal):
        return float(np.asarray(observation[signal], dtype=float) @ LUMA)
    sun = [luminance(base, "sun"), luminance(independent, "sun")]
    total = [luminance(base, "mean"), luminance(independent, "mean")]
    environment = [luminance(base, "environment"), luminance(independent, "environment")]
    signal, energy = float(np.mean(sun)), float(np.mean(total))
    difference = abs(sun[0]-sun[1])
    resolved = signal > 0 and difference <= F6_SIGNAL_SAMPLING_FRACTION*signal
    sensitive = energy > 0 and signal > F6_SIGNAL_ENERGY_GATE*energy
    mean = (np.asarray(base["mean"], dtype=float)+np.asarray(independent["mean"], dtype=float))/2
    chromaticity = mean[:2]/mean.sum() if mean.sum() > 0 else np.zeros(2)
    return dict(sunLuminance=signal, seedDifference=difference, totalLuminance=energy,
                environmentLuminance=float(np.mean(environment)),
                sunFraction=signal/energy if energy > 0 else 0.0,
                chromaticityOffset=(chromaticity-1/3).tolist(),
                samplingResolved=bool(resolved), exceedsEnergyGate=bool(sensitive),
                status="SignalPrerequisiteMet" if resolved and sensitive else "SignalPrerequisiteNotMet")


def overlay(path, normal, mask):
    image = np.clip((normal*0.5+0.5)*0.45, 0, 1)
    image[mask] = image[mask]*0.35 + np.array([0.0, 0.6, 0.1])
    pixels = np.uint8(image*255)
    h, w = pixels.shape[:2]
    def chunk(kind, data):
        return struct.pack(">I", len(data))+kind+data+struct.pack(">I", zlib.crc32(kind+data))
    png = bytes.fromhex("89504e470d0a1a0a")+chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(bytes([0])+row.tobytes() for row in pixels)))+chunk(b"IEND", b"")
    path.write_bytes(png)


def inspect(root, case):
    directory = root/case["directory"]
    records = list(directory.glob("*.resource-export.json"))
    require(len(records) == 1, f"Expected one export: {directory}")
    record = json.loads(records[0].read_text(encoding="utf-8-sig"))
    frame = record["frame"]
    require(frame["readiness"]["ready"], "Unready lighting observation")
    gates = [g for g in frame["readiness"]["gates"] if g["name"] == "gi-reference"]
    require(len(gates) == 1 and gates[0]["state"] == "ready", "Reference observation is not GPU-complete")
    require(frame["content"]["labId"] == "gglab.lab.gi_reference" and
            frame["camera"]["referenceView"] == case["view"], "Wrong content/view")
    require(frame["temporal"]["status"] == "disabled" and frame["temporal"]["jitterPixels"] == [0, 0] and
            frame["temporal"]["renderExtent"] == frame["temporal"]["displayExtent"], "Non-Native/temporal receiver")
    inputs = frame["sceneInputs"]
    require(not frame["temporal"]["gtao"]["enabled"] and not inputs["postProcess"]["bloomEnabled"],
            "GTAO/bloom active")
    parameters = DEFAULTS.copy()
    parameters.update({p["id"].split(".", 1)[1]: p["value"] for p in frame["parameters"]["values"]
                       if p["id"].startswith("gi_reference.")})
    require(all(parameters[key] == case[key] for key in DEFAULTS if key in case), "Parameter provenance mismatch")
    require(inputs["exposure"]["effectiveEV100"] == case["ev100"], "Wrong actual exposure")
    sun = inputs["sun"]
    expected_sun = case["fixture"] in (4, 5, 6) or case.get("environment") == 3
    require((sun is not None) == expected_sun, "Wrong published WorldSun")
    if expected_sun:
        direction = np.array([0.2, -1, -1] if case["fixture"] == 4 else [0, -1, 1], dtype=float)
        direction /= np.linalg.norm(direction)
        require(np.max(np.abs(np.asarray(sun["photonDirection"])-direction)) < 1e-6, "Photon direction differs")
        require(sun["toaIlluminanceLux"] == (10000 if case["fixture"] == 4 else case["sun_lux"]),
                "Published solar units/intensity differ")
    environment = inputs["environment"]
    require(environment["activeIBLGeneration"] > 0, "Missing published World Lighting generation")
    published = case.get("environment", 0) != 0
    require(environment["intensity"] == (case["intensity"] if published else 0), "Environment intensity cleared or stale")
    require(abs(environment["rotationRadians"]-np.deg2rad(case["yaw"] if published else 0)) < 1e-6,
            "Wrong published environment yaw")
    require((inputs["atmosphere"] is not None) == (case.get("atmosphere", False) or case.get("environment") == 3),
            "Wrong atmosphere attenuation arm")
    if case.get("environment") == 3:
        require(environment["backgroundMode"] == 2, "Physical Sky must use the published sky environment")
    values = {}
    for target in record["targets"]:
        array = np.load(directory/target["array"], allow_pickle=False)
        require(array.dtype == np.dtype("<f4") and array.ndim == 4 and array.shape[0] == 1 and
                array.shape[-1] == 4 and np.isfinite(array).all(), "Invalid/non-finite lighting export")
        values[target["address"]["resource"]] = array[0]
    e = values["GI.Reference.Irradiance"]
    covered = values["GI.Receiver.HitAlbedo"][..., 3] == 1
    require(covered.any() and np.all(e[..., 3][covered] == 1) and np.all(e[..., 3][~covered] == 0),
            "Invalid receivers cannot be removed from statistics")
    require(np.all(e[..., :3] >= 0), "Negative irradiance")
    diag = values["GI.Reference.Diagnostics"]
    require(np.all(diag >= 0) and np.all(diag.sum(-1) <= 1.00001) and np.all(diag[..., 3] == 0),
            "Invalid secondary samples or denominator")
    sunlight = values["GI.Reference.SunIrradiance"]
    skylight = values["GI.Reference.EnvironmentIrradiance"]
    require(np.allclose(e[..., :3], sunlight[..., :3]+skylight[..., :3], rtol=1e-6, atol=ABSOLUTE),
            "Sun/environment components do not sum to E")
    require(np.all(sunlight[..., 3][covered] == (1 if inputs["atmosphere"] else 0)), "Wrong bound attenuation path")
    mip = (0 if case["filter"] else max(0, np.log2(environment["iblConfig"]["environmentSize"]/32))) if published else -1
    require(np.all(skylight[..., 3][covered] == mip), "Actual cubemap filter selection differs")
    if case["fixture"] == 6:
        require(not np.any(values["GI.Reference.Identity"][..., 0][covered] == 2), "Sun blocker is visible to camera")
    if case["fixture"] == 4:
        # All sunlit secondary surfaces are neutral; neither coloured wall is directly sunlit.
        require(np.max(np.ptp(e[..., :3][covered], axis=-1)) < 1e-4, "Unexpected D=1 coloured energy in frozen F4")
        identity = values["GI.Reference.Identity"][..., 0]
        for instance, albedo in ((2, [0.8, 0.05, 0.05]), (3, [0.05, 0.8, 0.05])):
            selected = covered & (identity == instance)
            if selected.any():
                require(np.max(np.abs(values["GI.Receiver.HitAlbedo"][selected, :3]-albedo)) < 1e-6,
                        "Frozen F4 side-wall material differs")
    lighting = frame["parameters"]["lighting"]
    require(lighting["active"]["generation"] == lighting["requested"]["generation"], "Unpublished World Lighting inputs")
    require(lighting["active"]["generation"] == environment["activeIBLGeneration"],
            "Frame scene inputs and parameter lighting generations differ")
    result = dict(worldLightingGeneration=lighting["active"]["generation"],
                  primaryLookupFallbacks=int(np.count_nonzero(values["GI.Reference.Identity"][..., 3] == 2)), environment=environment,
                  sun=sun, atmosphere=inputs["atmosphere"], parameters=parameters, actualMip=float(mip),
                  irradianceSha256=hashlib.sha256(e.tobytes()).hexdigest(),
                  depth=1, depthConverged=False, comparisonStatus="ReferenceUnconverged", masks={})
    if case["view"] in F6_VIEWS:
        # Certify the off-screen claim from primary receiver identities: the
        # front jambs/lintel lie behind both poses, and each pose excludes one wall.
        identity = values["GI.Reference.Identity"][..., 0]
        visible = {str(i): int(np.count_nonzero(covered & (identity == i))) for i in range(8)}
        require(all(visible[str(i)] == 0 for i in F6_OPENING_INSTANCES), "F6 pose sees the sunlit opening")
        require(visible[str(F6_EXCLUDED_WALL[case["view"]])] == 0, "F6 pose sees its excluded coloured wall")
        result["visibleInstancePixels"] = visible
    for name, mask in masks(case, values, frame["camera"]).items():
        require(mask.sum() >= 32, f"Empty required mask: {case['directory']}/{name}")
        overlay(directory/f"mask-{name}.png", values["GI.Receiver.ShadingNormal"][..., :3], mask)
        position = values["GI.Reference.Position"][mask, :3].astype(np.float64)
        mean = e[mask, :3].mean(0, dtype=np.float64)
        sun_mean = sunlight[mask, :3].mean(0, dtype=np.float64)
        sky_mean = skylight[mask, :3].mean(0, dtype=np.float64)
        expected = None
        if case["fixture"] in (5, 6):
            albedo = np.array([0.8, 0.05, 0.05] if case["secondary_albedo"] else [0.8]*3)
            factor = wall_form_factor(position[:, 0], position[:, 2]).mean()
            attenuation = atmosphere_transmittance(inputs["atmosphere"], 1/np.sqrt(2))
            expected = factor*albedo*case["sun_lux"]/np.sqrt(2)/np.pi*attenuation if case["fixture"] == 5 else np.zeros(3)
            require(np.all(sky_mean == 0), "Sun-only contract contains environment energy")
            require(np.max(np.abs(values["GI.Receiver.HitAlbedo"][mask, :3] -
                    ([0.2, 0.5, 0.8] if case["albedo"] else [1, 1, 1]))) < 1e-6, "Wrong receiver material")
        elif case["fixture"] == 7 and case["environment"] != 3:
            normal = values["GI.Receiver.ShadingNormal"][mask, :3].astype(float)
            yaw = np.deg2rad(case["yaw"])
            rotated = normal.copy()
            rotated[:, 0] = np.cos(yaw)*normal[:, 0]-np.sin(yaw)*normal[:, 2]
            rotated[:, 2] = np.sin(yaw)*normal[:, 0]+np.cos(yaw)*normal[:, 2]
            if case["environment"] == 2:
                expected = np.pi*(1+rotated/3).mean(0)*case["intensity"]
            else:
                expected = np.full(3, np.pi*1.00390625*(case["intensity"] if published else 1))
        elif case["fixture"] == 4 and case["view"] in ("F4_Floor", "F4_Back") + F6_VIEWS:
            oracle_sun, oracle_sky = f4_oracle(position, case["view"] != "F4_Back")
            require(relative_error(sun_mean, oracle_sun.mean(0)) < RELATIVE, f"F4 sun oracle: {name}")
            require(relative_error(sky_mean, oracle_sky.mean(0)) < RELATIVE, f"F4 sky oracle: {name}")
            expected = (oracle_sun+oracle_sky).mean(0)
            require(np.max(np.abs(values["GI.Receiver.HitAlbedo"][mask, :3]-0.8)) < 1e-6,
                    "F4 measurement receiver must remain neutral")
        error = relative_error(mean, expected) if expected is not None else None
        require(error is None or error < RELATIVE, f"CPU oracle failed {case['directory']}/{name}: {error}")
        result["masks"][name] = dict(pixels=int(mask.sum()), maskSha256=hashlib.sha256(mask.tobytes()).hexdigest(),
                                    irradianceSha256=hashlib.sha256(e[mask].tobytes()).hexdigest(),
                                    mean=mean.tolist(), sun=sun_mean.tolist(),
                                    environment=sky_mean.tolist(), oracle=None if expected is None else expected.tolist(),
                                    oracleError=error)
    graph = json.loads(next(directory.glob("*.graph.json")).read_text(encoding="utf-8-sig"))
    passes = {p["name"]: p for p in graph["compile"]["passes"] if not p["culled"]}
    require("GI.Reference.PathTrace" in passes and "GI.Receiver" in passes, "Missing GI graph work")
    require(passes["GI.Receiver"]["executionOrder"] < passes["GI.Reference.PathTrace"]["executionOrder"],
            "Reference precedes receivers")
    require(not any(p.startswith("TAA.") for p in passes), "Temporal work is active")
    return result


def validate(root, cases):
    results = {case["directory"]: inspect(root, case) for case in cases}
    convergence, comparisons, component_convergence, component_comparisons = {}, {}, {}, {}
    for case in cases:
        key = case["directory"]
        if case.get("variant") != "base":
            continue
        for variant in ("independent", "double"):
            other = key.rsplit("-", 1)[0]+"-"+variant
            require(other in results, f"Missing convergence observation: {other}")
            for mask, value in results[key]["masks"].items():
                right = results[other]["masks"][mask]
                require(value["maskSha256"] == right["maskSha256"], "Convergence masks differ")
                error = relative_error(right["mean"], value["mean"])
                require(error < RELATIVE, f"ReferenceUnconverged: {key}/{mask}/{variant}: {error}")
                convergence[f"{key}/{mask}/{variant}"] = error
                for signal in ("sun", "environment"):
                    change = relative_error(right[signal], value[signal])
                    require(change < RELATIVE,
                            f"ReferenceUnconverged component: {key}/{mask}/{variant}/{signal}: {change}")
                    component_convergence[f"{key}/{mask}/{variant}/{signal}"] = change
        results[key]["samplingStatus"] = "SamplingConverged"
    for case in cases:
        key = case["directory"]
        if not key.startswith("dx12/"):
            continue
        other = key.replace("dx12/", "vulkan/", 1)
        if other not in results:
            continue
        for name, left in results[key]["masks"].items():
            right = results[other]["masks"][name]
            require(left["maskSha256"] == right["maskSha256"], "Cross-RHI mask membership differs")
            error = relative_error(right["mean"], left["mean"])
            require(error < RELATIVE, f"Cross-RHI ROI error: {key}/{name}: {error}")
            comparisons[f"{key}/{name}"] = error
            for signal in ("sun", "environment"):
                change = relative_error(right[signal], left[signal])
                require(change < RELATIVE, f"Cross-RHI component error: {key}/{name}/{signal}: {change}")
                component_comparisons[f"{key}/{name}/{signal}"] = change
    # Complete arrays must preserve the exact primary RNG and addition order.
    for case in cases:
        if "/sun-batch-" in case["directory"]:
            key = case["directory"]
            baseline = key.split("/", 1)[0] + "/sun-base"
            require(results[key]["irradianceSha256"] == results[baseline]["irradianceSha256"],
                    f"Submission partition changed the D=1 estimator: {key}")
    # Same-seed changes isolate each physical factor; primary receiver albedo cannot change E.
    contract_families = ("sun-", "constant-", "gradient-", "physical-")
    for backend in sorted({c["directory"].split("/")[0] for c in cases}):
        if not any(key.startswith(f"{backend}/{family}") for key in results for family in contract_families):
            continue
        def roi(name):
            return np.asarray(results[f"{backend}/{name}"]["masks"]["floor"]["mean"])
        for variant, scale in (("intensity", [2, 2, 2]), ("red", [1, 0.0625, 0.0625]), ("receiver", [1, 1, 1])):
            error = relative_error(roi(f"sun-{variant}"), roi("sun-base")*scale)
            require(error < 1e-5, f"Sun/secondary/receiver factor applied incorrectly: {variant}")
        require(np.all(roi("sun-blocked") == 0), "Off-camera blocker did not remove sun transport")
        require(results[f"{backend}/constant-direct"]["irradianceSha256"] ==
                results[f"{backend}/constant-cube"]["irradianceSha256"],
                "Published constant cubemap differs from direct constant input")
        # The floor is the primary receiver under test. Other visible surfaces can
        # receive a bounce from that floor and legitimately change with its material.
        require(results[f"{backend}/sun-base"]["masks"]["floor"]["irradianceSha256"] ==
                results[f"{backend}/sun-receiver"]["masks"]["floor"]["irradianceSha256"],
                "Reference E depends on the primary receiver albedo")
        for prefix in ("gradient", "physical"):
            base = results[f"{backend}/{prefix}-base"]["masks"]["sphere"]["environment"]
            mip0 = results[f"{backend}/{prefix}-mip0"]["masks"]["sphere"]["environment"]
            results[f"{backend}/{prefix}-mip0"]["filterDifferenceFromFace32"] = relative_error(mip0, base)
            # Same pixel/sample sequence: report spatial changes as well as the
            # ROI mean, which could hide positive/negative filtering differences.
            def filtered_image(name):
                directory = root/f"{backend}/{name}"
                record = json.loads(next(directory.glob("*.resource-export.json")).read_text(encoding="utf-8-sig"))
                values = {t["address"]["resource"]: np.load(directory/t["array"], allow_pickle=False)[0]
                          for t in record["targets"]}
                mask = masks({"fixture": 7}, values, record["frame"]["camera"])["sphere"]
                return values["GI.Reference.EnvironmentIrradiance"][mask, :3].astype(float)
            filtered = filtered_image(f"{prefix}-base")
            unfiltered = filtered_image(f"{prefix}-mip0")
            delta = filtered-unfiltered
            dy = delta@LUMA
            results[f"{backend}/{prefix}-mip0"]["filterBias"] = dict(
                meanAbsoluteRGB=np.abs(delta).mean(0).tolist(),
                rmseRGB=np.sqrt((delta*delta).mean(0)).tolist(),
                maxAbsoluteRGB=np.abs(delta).max(0).tolist(),
                luminanceNRMSE=float(np.sqrt(np.mean(dy*dy))/max(float((unfiltered@LUMA).mean()), ABSOLUTE)),
                p95AbsoluteLuminance=float(np.percentile(np.abs(dy), 95)),
                maxAbsoluteLuminance=float(np.abs(dy).max()))
        base = results[f"{backend}/gradient-base"]["masks"]["sphere"]["environment"]
        doubled = results[f"{backend}/gradient-intensity"]["masks"]["sphere"]["environment"]
        require(relative_error(doubled, np.asarray(base)*2) < 1e-5, "Environment intensity applied incorrectly")
    f6 = {}
    for case in cases:
        if case["view"] in F6_VIEWS and case.get("variant") == "base":
            family = case["directory"].rsplit("-", 1)[0]
            f6[family] = f6_signal_prerequisite(results[case["directory"]]["masks"]["patch"],
                                                results[family+"-independent"]["masks"]["patch"])
    for backend in sorted({key.split("/")[0] for key in f6}):
        west, east = (results[f"{backend}/f6-{pose}-base"]["masks"]["patch"] for pose in ("posewest", "poseeast"))
        # One world-space patch in two projections; reported only, not a field claim.
        f6[f"{backend}/cross-pose"] = dict(referenceMeanDifference=relative_error(east["mean"], west["mean"]))
    signal = None
    if f6:
        statuses = {value["status"] for value in f6.values() if "status" in value}
        signal = dict(observations=f6, status="SignalPrerequisiteMet" if statuses == {"SignalPrerequisiteMet"}
                      else "SignalPrerequisiteNotMet",
                      rules=dict(signal="patch-mean D=1 sun-component luminance",
                                 samplingFraction=F6_SIGNAL_SAMPLING_FRACTION,
                                 energyGate=F6_SIGNAL_ENERGY_GATE,
                                 seeds=[61051, 61052], notMet="neither a pass nor a failure"))
    report = dict(observations=results, sampling=convergence, crossRhi=comparisons,
                  componentSampling=component_convergence, crossRhiComponents=component_comparisons,
                  f6Signal=signal,
                  rules=dict(relative=RELATIVE, absolute=ABSOLUTE, signal="linear unexposed RGB E and luminance",
                             crossRhi="ROI means; complex paths need not be bit-identical",
                             depth="D=1 only; no DDGI comparison qualification"))
    (root/"lighting-results.json").write_text(json.dumps(report, indent=2)+chr(10))
    return report


def self_test():
    nodes, weights = np.polynomial.legendre.leggauss(64)
    x = nodes*2
    y = (nodes+1)*2
    for px, pz in ((0, 0), (-0.8, 0.6), (0.9, -0.9)):
        d = 2-pz
        numerical = np.sum((y[:, None]*d/((x[None, :]-px)**2+y[:, None]**2+d*d)**2) *
                           (weights[:, None]*weights[None, :]*4))
        require(abs(numerical-wall_form_factor(px, pz)) < 1e-12, "Rectangle oracle disagrees with quadrature")
    positions = np.array([[0, 1.6, -2], [1.4, 3.4, -2], [-1.4, 0.6, -2]])
    # Piecewise opening limits have kinks. Resolve their form-factor integral to
    # 1e-7 absolute, far below the 1% transport gate, rather than claiming roundoff.
    require(np.max(np.abs(back_sun_factor(positions, 64)-back_sun_factor(positions, 128))) < 1e-7,
            "F4 sun quadrature is unresolved")
    require(np.max(np.abs(back_sky_factor(positions, 16)-back_sky_factor(positions, 32))) < 1e-10,
            "F4 sky quadrature is unresolved")
    require(relative_error([0, 0, 0], [0, 0, 0]) == 0, "Black ROI normalization")
    try:
        require(relative_error([1.02]*3, [1]*3) < RELATIVE, "Deliberate energy error")
        raise AssertionError("Energy error was accepted")
    except ValueError:
        pass
    # An entering beam starts with |x|<=1, y<=3 and dx/d(-y)=0.2.
    require(1+3*0.2 < 2, "Frozen F4 beam must land before reaching a coloured wall")
    def patch(sun, sky):
        return dict(sun=[sun]*3, environment=[sky]*3, mean=[sun+sky]*3)
    require(f6_signal_prerequisite(patch(1.78, 0.016), patch(1.70, 0.016))["status"] ==
            "SignalPrerequisiteMet", "Resolved F6 signal rejected")
    require(f6_signal_prerequisite(patch(1e-4, 0.016), patch(1e-4, 0.016))["status"] ==
            "SignalPrerequisiteNotMet", "Near-zero F6 signal accepted")
    require(f6_signal_prerequisite(patch(1.0, 0.016), patch(0.5, 0.016))["status"] ==
            "SignalPrerequisiteNotMet", "Unresolved F6 signal accepted")
    floor = np.array([[[0, 0, 0, 1], [0.41, 0, -0.8, 1], [0, 0, -1.11, 1], [0.4, 0, -0.5, 1]]], dtype=float)
    selected = masks({"fixture": 4, "view": "F6_PoseWest"}, {
        "GI.Reference.Position": floor, "GI.Reference.Identity": np.array([[[0, 4, 1, 3]]*4], dtype=float)}, {})
    require(selected["patch"].tolist() == [[False, False, False, True]], "F6 patch bounds differ")
    return dict(status="passed", checks=12)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    print(json.dumps(self_test()))
