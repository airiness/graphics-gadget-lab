"""Independent field change/floor oracle; observed readiness is separate from quality."""
import argparse
import json
from pathlib import Path

import numpy as np


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def records(path):
    value = np.load(path, allow_pickle=False)
    if value.dtype.names:
        require(value.dtype.names == ("record",), "Readiness layout is not the declared float4 ABI")
        value = value["record"]
    require(value.dtype == np.dtype("<f4") and value.ndim == 2 and value.shape[1] == 4,
            "Readiness buffer lost its float32 record layout")
    return value


def interiors(atlas, counts, channels):
    nx, ny, nz = counts
    tile = atlas.shape[2] // nx
    require(atlas.shape == (ny, nz*tile, nx*tile, channels), "Invalid probe atlas shape")
    return atlas.reshape(ny, nz, tile, nx, tile, channels).transpose(0, 1, 3, 2, 4, 5)[
        :, :, :, 1:-1, 1:-1, :].reshape(nx*ny*nz, -1, channels).astype(np.float64)


def validate_case(directory, case):
    diagnostic = directory / "diagnostic"
    record = load(next(diagnostic.glob("*.resource-export.json")))
    targets = record["targets"]
    require([t["address"]["resource"] for t in targets[:3]] ==
            ["GI.Field.Readiness", "GI.Field.Readiness", "GI.Field.Classification"],
            "Missing paired pre/post readiness records")
    before = records(diagnostic / targets[0]["array"])
    after = records(diagnostic / targets[1]["array"])
    counts = case["counts"]
    total = int(np.prod(counts))
    require(before.shape == after.shape == (1+2*total, 4), "Readiness record count differs from profile")
    classes = np.load(diagnostic / targets[2]["array"], allow_pickle=False).reshape(-1)
    require(classes.size == total and np.isin(classes, [1, 2]).all(), "Unclassified/invalid probes reached readiness")
    active = classes == 1
    previous = before[2::2]
    current = after[2::2]
    require(np.isfinite(before[0]).all() and np.isfinite(after[0]).all() and
            np.isfinite(previous[:, :3]).all() and np.isfinite(current[:, :3]).all() and
            np.isfinite(after[1::2]).all(), "Readiness measurements contain nonfinite values")
    median = np.sort(previous[active, 0])[active.sum()//2] if active.any() else 0.
    floor = max(.001, .001*float(median))
    require(np.isclose(after[0, 0], floor, rtol=2e-6, atol=1e-8) and
            np.isclose(after[0, 1], median, rtol=2e-6, atol=1e-8) and
            after[0, 2] == active.sum() and after[0, 3] == 4,
            "GPU floor differs from independent Active-probe upper median")
    require(np.array_equal(before[0], after[0]), "Blend changed its immutable readiness floor")
    updates = np.ascontiguousarray(current[:, 3]).view(np.uint32)
    require((updates == updates[0]).all() and updates[0] > 0, "Mixed readiness epochs/updates")
    elapsed = int(updates[0])
    ring = (elapsed-1) % 4
    require(np.allclose(current[:, 1], after[1::2].astype(float).mean(axis=1), rtol=2e-6, atol=1e-8),
            "Four-update average differs from exported changes")
    require(np.array_equal(current[:, 2], active.astype(np.float32)), "Inactive probes entered the readiness population")
    if elapsed > 1:
        old_updates = np.ascontiguousarray(previous[:, 3]).view(np.uint32)
        require((old_updates == elapsed-1).all(), "Readiness skipped an update")
        keep = [i for i in range(4) if i != ring]
        require(np.array_equal(before[1::2, keep], after[1::2, keep]), "Unrelated change history was overwritten")
    error = None
    if case["id"] != "sunroom":
        old = interiors(np.load(diagnostic / targets[3]["array"]), counts, 4)[..., :3]
        new = interiors(np.load(diagnostic / targets[4]["array"]), counts, 4)[..., :3]
        expected = (np.abs(new-old)*np.pi / np.maximum(old*np.pi, floor)).max(axis=(1, 2))
        old_distance = interiors(np.load(diagnostic / targets[5]["array"]), counts, 2)[..., 0]
        new_distance = interiors(np.load(diagnostic / targets[6]["array"]), counts, 2)[..., 0]
        spacing = .5 if case["id"] in ("excluded", "plane") else 1.
        expected = np.maximum(expected, (np.abs(new_distance-old_distance)/spacing).max(axis=1))
        actual = after[1::2, ring].astype(float)
        error = float(np.max(np.abs(actual-expected)/np.maximum(1., expected)))
        require(error < 2e-4, f"GPU per-probe changes differ from FP64 atlas deltas: {error}")
        if case["id"] == "black":
            require(active.any() and np.count_nonzero(new) == 0 and floor == .001,
                    "F10 must contain Active probes and preserve exact zero field energy")
    frozen = record["frame"]["readiness"]["giField"]
    require(frozen["schemaVersion"] == 1 and frozen["enabled"] and not frozen["failed"] and
            frozen["generation"] > 0 and frozen["completedUpdates"] <= elapsed and
            frozen["irradianceFloor"] >= .001 and frozen["threshold"] >= .009999,
            "Capture lacks a valid frozen readiness identity")
    baseline = load(directory / "noise-baseline.json")["frame"]["giField"]
    require((baseline["refreshNoiseSamples"] >= 30 or (baseline["activeBlocks"] == 0 and baseline["excludedBlocks"] > 0)) and
            baseline["steadyNoiseSamples"] == 64,
            "Noise profile lacks bounded same-alpha measurements")
    baseline_gate = load(directory / "baseline-gated/receipt.json")
    require(baseline_gate["status"] in ("completed", "failed"), "Missing baseline gate result")
    if baseline_gate["status"] == "failed":
        require("Unconverged" in json.dumps(baseline_gate), "Baseline gate hid its failure reason")
    values = {item["id"]: item["value"] for item in record["frame"]["parameters"]["values"]}
    require(values.get("gi_reference.seed", 61051) == 61052,
            "Production observation reused the calibration seed")
    require(np.isclose(values.get("gi_reference.field_refresh_noise", 0), baseline["refreshNoiseEstimate"], rtol=1e-6) and
            np.isclose(values.get("gi_reference.field_steady_noise", 0), baseline["steadyNoiseEstimate"], rtol=1e-6),
            "Frozen noise inputs differ from the explicitly adopted measurements")
    require(np.isclose(frozen["noiseSigma"], baseline["steadyNoiseEstimate"], rtol=1e-6) and
            np.isclose(frozen["threshold"], max(.01, 3*baseline["steadyNoiseEstimate"]), rtol=1e-6),
            "Ready observation did not use its steady-alpha noise threshold")
    adopted = load(directory / "calibrated-effect.json")
    require(adopted["batch"]["succeeded"], "Measured noise profile was not consumed")
    if case["id"] == "black":
        coordinates = np.indices((counts[1], counts[2], counts[0])).reshape(3, -1).T
        positions = coordinates[:, [2, 0, 1]] + np.asarray([-4, -2, -4])
        interior = (np.abs(positions[:, [0, 2]]) < 2).all(axis=1) & (positions[:, 1] > 0) & (positions[:, 1] < 4)
        require(np.any(active & interior), "F10 classified every interior probe Inactive")
    gated = load(directory / "gated/receipt.json")
    if gated["status"] == "completed":
        require(gated["publication"] == "Published", "Ready capture did not publish")
        metadata = gated["products"][0]["metadata"]
        readiness = metadata["readiness"]["giField"]
        require(readiness["state"] == "Ready" and not readiness["refreshing"] and
                readiness["completedUpdates"] >= readiness["readyUpdate"]+120,
                "Capture published before Ready plus 120 completed updates")
    else:
        require(gated["status"] == "failed" and "Unconverged" in json.dumps(gated),
                "An after-ready failure did not preserve its Unconverged reason")
    return {"case": case["id"], "state": frozen["state"], "completedUpdates": frozen["completedUpdates"],
            "maximumChangeOracleError": error, "afterReadyPublished": gated["status"] == "completed",
            "readyWithin120": bool(gated["status"] == "completed" and readiness["readyUpdate"] <= 120),
            "baselineAfterReadyPublished": baseline_gate["status"] == "completed",
            "refreshNoiseEstimate": baseline["refreshNoiseEstimate"],
            "steadyNoiseEstimate": baseline["steadyNoiseEstimate"],
            "noiseCalibrationQualified": False, "referenceConvergenceQualified": False,
            "temporalStabilityQualified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    results = []
    for backend in load(args.root / "backends.json"):
        for case in load(args.root / "cases.json"):
            result = validate_case(args.root / backend / case["id"], case)
            results.append({"backend": backend, **result})
    output = {"status": "passed", "observations": results,
              "allReadinessGatesPassed": all(r["readyWithin120"] for r in results)}
    (args.root / "validation.json").write_text(json.dumps(output, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(output))


if __name__ == "__main__":
    main()
