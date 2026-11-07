"""Receiver inspection provenance and independent staged-weight checks."""
import math


def require(condition, message):
    if not condition:
        raise ValueError(message)


def near(a, b, message, relative=5e-5, absolute=2e-5):
    require(math.isfinite(a) and math.isfinite(b) and math.isclose(a, b, rel_tol=relative, abs_tol=absolute), message)


def validate_pairing(products):
    inspections = [frame["giReceiverInspection"] for source, frame in products
                   if source == "resource-export" and frame.get("giReceiverInspection") is not None]
    observed = any(frame.get("giReceiverObservation") is not None or
                   frame.get("giReceiverInspection") is not None for _, frame in products)
    if not inspections:
        require(not observed, "Receiver observation requires an inspection resource product")
        return
    result = inspections[0]
    expected = dict(stamp=result["observation"], view=result["view"])
    for source, frame in products:
        require(frame.get("giReceiverObservation") == expected,
                f"Paired {source} product has a missing or mismatched receiver observation")


def validate(frame, schema):
    result = frame.get("giReceiverInspection")
    if result is None:
        return
    schema.validate(result)
    stamp, view, request = result["observation"], result["view"], result["request"]
    require(frame["giReceiverObservation"] == dict(stamp=stamp, view=view), "Receiver and product stamps differ")
    summary = frame["readiness"]["giField"]["summary"]
    require(stamp["field"] == summary["field"] and view == summary["receiverView"], "Frozen field/view differs")
    require(stamp["producingFrame"] == frame["frame"]["serial"], "Receiver producing frame differs")
    require(stamp["fieldUpdate"] == summary["submittedUpdates"] + 1 and
            stamp["readinessUpdate"] == stamp["fieldUpdate"] - stamp["field"]["epochOriginUpdate"], "Invalid update mapping")
    require(request["expectedGeneration"] == stamp["field"]["generation"] and
            request["expectedRefreshEpoch"] in (None, stamp["field"]["refreshEpoch"]) and
            request["expectedViewRevision"] == view["revision"] and request["expectedViewId"] == view["viewId"],
            "Required observation identity did not hold")
    require(view["renderExtent"] == view["displayExtent"] and
            all(0 <= p < n for p, n in zip(request["pixel"], view["displayExtent"])), "Unsupported view or pixel")
    pixels = math.prod(view["renderExtent"])
    require(view["receiverAttachmentLogicalBytes"] == pixels * 9 * 16 and
            view["lookupDiagnosticLogicalBytes"] == pixels * 2 * 16, "Receiver prerequisite cost is incomplete")
    if result["validity"] in ("InvalidNumerics", "MalformedPayload"):
        return
    if result["validity"] == "Background":
        require(result["position"] is None and not result["corners"], "Background acquired a probe cage")
        return
    for a, b in zip(result["irradianceE"], result["ordinaryIrradianceE"]):
        near(a, b, "Diagnostic irradiance disagrees with ordinary production query")
    near(result["totalWeight"], result["ordinaryWeight"], "Diagnostic weight disagrees with ordinary query")
    if not result["cageEvaluated"]:
        require(not result["corners"] and result["biasedPosition"] is None, "Early return acquired evaluated corners")
        return
    profile = result["fieldProfile"]
    position, biased, normal = result["position"], result["biasedPosition"], result["shadingNormal"]
    require(profile["counts"] == summary["counts"] and profile["irradianceInteriorWidth"] == summary["irradianceInteriorWidth"],
            "Query profile differs from the producing field")
    near(profile["spacing"], summary["spacing"], "Query spacing differs from the producing field")
    to_camera = [c - p for c, p in zip(view["cameraPosition"], position)]
    camera_distance = math.sqrt(sum(v * v for v in to_camera))
    for actual, expected in zip(result["viewDirection"], to_camera):
        near(actual, expected / camera_distance, "Query view direction differs from the producing camera")
    homogeneous = position + [1]
    matrix = view["viewProjection"]
    clip = [sum(homogeneous[i] * matrix[i * 4 + j] for i in range(4)) for j in range(4)]
    projected = [(clip[0] / clip[3] * .5 + .5) * view["displayExtent"][0],
                 (.5 - clip[1] / clip[3] * .5) * view["displayExtent"][1]]
    require(all(abs(p - (index + .5)) < .05 for p, index in zip(projected, request["pixel"])),
            "Receiver position does not project to the bound pixel center")
    require(profile["counts"] == summary["counts"] and profile["irradianceInteriorWidth"] == summary["irradianceInteriorWidth"],
            "Query profile differs from the producing field")
    near(profile["spacing"], summary["spacing"], "Query spacing differs from the producing field")
    to_camera = [c - p for c, p in zip(view["cameraPosition"], position)]
    camera_distance = math.sqrt(sum(v * v for v in to_camera))
    for actual, expected in zip(result["viewDirection"], to_camera):
        near(actual, expected / camera_distance, "Query view direction differs from the producing camera")
    homogeneous = position + [1]
    matrix = view["viewProjection"]
    clip = [sum(homogeneous[i] * matrix[i * 4 + j] for i in range(4)) for j in range(4)]
    projected = [(clip[0] / clip[3] * .5 + .5) * view["displayExtent"][0],
                 (.5 - clip[1] / clip[3] * .5) * view["displayExtent"][1]]
    require(all(abs(p - (index + .5)) < .05 for p, index in zip(projected, request["pixel"])),
            "Receiver position does not project to the bound pixel center")
    for axis in range(3):
        near(biased[axis], position[axis] + profile["spacing"] *
             (profile["normalBias"] * result["vertexNormal"][axis] + profile["viewBias"] * result["viewDirection"][axis]),
             "Bias disagrees with the producing field profile")
    require(len({tuple(c["coordinate"]) for c in result["corners"]}) == 8, "Cage lost unique corners")
    for corner in result["corners"]:
        require(all(c < n for c, n in zip(corner["coordinate"], profile["counts"])), "Corner is outside its field")
        for p, origin, coordinate in zip(corner["worldPosition"], summary["minimum"], corner["coordinate"]):
            near(p, origin + profile["spacing"] * coordinate, "Probe position differs from the grid")
        require(all(c < n for c, n in zip(corner["coordinate"], profile["counts"])), "Corner is outside its field")
        for p, origin, coordinate in zip(corner["worldPosition"], summary["minimum"], corner["coordinate"]):
            near(p, origin + profile["spacing"] * coordinate, "Probe position differs from the grid")
        near(corner["trilinearWeight"], math.prod(corner["trilinearFactors"]), "Trilinear weight differs")
        if not corner["samplingEvaluated"]:
            require(corner["reason"] == "NotActive" and corner["classification"] != 1 and
                    corner["atlasM"] is None and corner["visibility"] is None and corner["rawWeight"] == 0,
                    "Excluded corner fabricated sampling or weight")
            continue
        require(corner["classification"] == 1, "An inactive corner sampled an atlas")
        to_probe = [p - r for p, r in zip(corner["worldPosition"], position)]
        length = math.sqrt(sum(v * v for v in to_probe))
        direction = [v / max(length, 1e-10) for v in to_probe]
        backface = (sum(a * b for a, b in zip(direction, normal)) + 1) * .5
        near(corner["backfaceFactor"], backface * backface + .2, "Backface factor differs")
        near(corner["preVisibilityWeight"], corner["trilinearWeight"] * corner["backfaceFactor"], "Initial weight differs")
        distance = math.dist(corner["worldPosition"], biased)
        near(corner["receiverDistance"], distance, "Visibility distance differs")
        variance = max(corner["variance"], 1e-4 * profile["spacing"] ** 2) if distance > corner["meanDistance"] else 0
        visibility = (variance / (variance + (distance - corner["meanDistance"]) ** 2)) ** 3 if variance else 1
        near(corner["usedVariance"], variance, "Variance floor differs")
        near(corner["visibility"], visibility, "Cubed visibility differs", relative=2e-4)
        before = corner["preVisibilityWeight"] * corner["visibility"]
        near(corner["preCrushWeight"], before, "Visibility-weighted value differs")
        near(corner["rawWeight"], before ** 3 / (.2 ** 2) if before < .2 else before, "Weight crushing differs")
    weight = result["totalWeight"]
    near(sum(c["rawWeight"] for c in result["corners"]), weight, "Total corner weight differs")
    if weight == 0:
        require(result["normalizationReason"] is not None and
                all(c["normalizedWeight"] is None and c["contributionE"] is None for c in result["corners"]),
                "Zero total weight acquired normalized contributions")
        return
    near(sum(c["normalizedWeight"] for c in result["corners"]), 1, "Normalized weights do not sum to one")
    for corner in result["corners"]:
        near(corner["normalizedWeight"], corner["rawWeight"] / weight, "Normalized corner weight differs")
        if corner["atlasM"] is not None:
            for e, m in zip(corner["contributionE"], corner["atlasM"]):
                near(e, math.pi * corner["normalizedWeight"] * m, "Corner scene-referred E differs")
    for axis in range(3):
        near(sum(c["contributionE"][axis] for c in result["corners"]), result["irradianceE"][axis], "Contributions disagree with E")
