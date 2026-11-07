"""Audit probe viewer correctness separately from quiet Release CPU/GPU overhead."""
import argparse
import copy
import json
import math
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "Verification"))
import ValidateCaptureBundles as bundles
import ValidateGpuTiming as timing
import ValidateParameterFrames as parameters
from ValidateGIProbeVisualization import oracle


def require(value, message):
    if not value:
        raise ValueError(message)


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def stats(values):
    values = np.asarray(values, np.float64)
    require(values.size > 0 and np.isfinite(values).all(), "Missing or invalid timing samples")
    ordered = np.sort(values)
    return dict(samples=len(values), medianMs=float(ordered[math.ceil(.5 * len(values)) - 1]),
                p95Ms=float(ordered[math.ceil(.95 * len(values)) - 1]), meanMs=float(values.mean()))


def read_capture(directory):
    receipt = load(directory / "receipt.json")
    require(receipt["status"] == "completed" and receipt["publication"] == "Published", "Unpublished viewer evidence")
    manifest = bundles.validate(Path(receipt["manifest"]))
    base = Path(receipt["manifest"]).parent
    products = {p["source"]: p for p in manifest["products"]}
    graph = load(base / products["graph"]["metadata"])
    return base, products, graph, manifest


def allocation_facts(graph):
    resources = graph["compile"]["resources"]
    result = []
    for allocation in graph["execution"]["allocations"]:
        resource = resources[allocation["resourceIndex"]]
        if resource["name"].startswith(("GI.Probes.", "GI.Receiver", "GI.Field.Receiver", "GI.Field.Lookup")):
            result.append(dict(name=resource["name"], logicalBytes=resource["logicalBytes"], **allocation))
    return result


def inspect_counts(display):
    completed = display["completed"]
    require(completed is not None and completed["settings"] == display["settings"], "Counts belong to older viewer settings")
    require(completed["observation"]["field"] == display["observation"]["field"], "Counts belong to a replaced field")
    c = completed["counts"]
    values = [c[k] for k in ("enumerated", "afterSlice", "afterClassification", "afterDistance", "afterFrustum", "rendered")]
    require(all(a >= b >= 0 for a, b in zip(values, values[1:])) and
            c["rendered"] == min(c["afterFrustum"], display["settings"]["budget"]) and
            c["budgetDropped"] == c["afterFrustum"] - c["rendered"], "Inconsistent filter/budget counts")
    require(c["highlights"] == display["highlightCount"], "Highlight count is mixed with ordinary survivors")
    return c


def validate_warmup(proof, requested, record, run, frames):
    require(proof.get("schema") == "gglab.probe-viewer-warmup" and proof.get("schemaVersion") == 1 and
            proof.get("method") == "submitted-settle-counter", "Missing submitted-frame warmup evidence")
    require(type(requested) is int and requested > 0 and proof["requestedFrames"] == requested,
            "Warmup request differs from measurement settings")
    samples = proof["samples"]
    require(len(samples) >= 2 and samples[0] == proof["start"] and samples[-1] == proof["end"],
            "Warmup endpoints do not match its observations")
    binding_keys = ("runIdentity", "contentId", "parameterRevision", "controlState", "field",
                    "viewRevision", "visualization", "extent", "backend")
    start, end = samples[0], samples[-1]
    binding = {k: start[k] for k in binding_keys}
    case = record["case"]
    viewer = start["visualization"]
    require(start["runIdentity"] == run and start["contentId"] == "gglab.lab.gi_reference" and
            start["backend"] == record["rhi"] and start["extent"] == [1280, 720], "Warmup session differs")
    require(viewer["enabled"] == (case["kind"] != "off") and viewer["budget"] == case["budget"] and
            viewer["mode"] == ("irradiance" if case["mode"] else "classification") and
            viewer["depthMode"] == ("scene-depth" if case["depth"] else "xray") and
            (viewer["selection"] is not None) == case["selected"], "Warmup viewer arm differs")
    for i, sample in enumerate(samples):
        require(sample["ready"] is True and sample["outcome"] == "submitted" and
                {k: sample[k] for k in binding_keys} == binding, "Warmup readiness or binding changed")
        require(all(type(sample[k]) is int and 0 <= sample[k] <= limit for k, limit in
                    (("frameSerial", 2**64 - 1), ("settledFrames", 2**32 - 1))), "Invalid warmup counters")
        if i:
            serial_delta = sample["frameSerial"] - samples[i-1]["frameSerial"]
            submitted_delta = sample["settledFrames"] - samples[i-1]["settledFrames"]
            require(0 <= submitted_delta <= serial_delta, "Warmup counter reset or exceeds submitted serials")
    submitted = end["settledFrames"] - start["settledFrames"]
    require(submitted >= requested and proof["submittedFrames"] == submitted, "Insufficient actual warmup frames")
    require(frames[0]["parameters"]["frameSerial"] > end["frameSerial"] and
            all(f["parameters"]["revision"] == start["parameterRevision"] and
                f["parameters"]["contentId"] == start["contentId"] for f in frames),
            "Timed range precedes warmup or changes its parameter binding")
    return dict(requestedFrames=requested, submittedFrames=submitted, startFrameSerial=start["frameSerial"],
                endFrameSerial=end["frameSerial"], observations=len(samples))


def inspect_cost(root, record, settings):
    path = root / record["directory"]
    case = record["case"]
    status = load(path / "status.json")
    env = status["environment"]
    require(env["build"]["configuration"] == "Release" and not env["build"]["pixIntegration"] and
            env["validation"]["effective"] == "off" and env["process"]["host"] == "hidden" and
            env["device"]["backend"] == record["rhi"] and
            (status["session"]["width"], status["session"]["height"]) == (1280, 720),
            "Viewer timing requires quiet hidden Release without PIX")
    sequence = load(path / "sequence.json")["sequence"]
    require(sequence["state"] == "completed" and sequence["timingPublication"]["outcome"] == "Published" and
            sequence["parameterPublication"]["outcome"] == "Published", "Missing complete timing/parameter range")
    manifest_path = Path(sequence["timingPublication"]["manifest"])
    manifest = bundles.validate(manifest_path)
    products = {p["source"]: p for p in manifest["products"]}
    require(set(products) == {"gpu-timing", "parameter-frames"}, "Timed range contains image/export products")
    document = timing.validate(load(manifest_path.parent / products["gpu-timing"]["metadata"]))
    frames = parameters.validate(load(manifest_path.parent / products["parameter-frames"]["metadata"]))["frames"]
    require(manifest["environment"]["process"]["runIdentity"] == env["process"]["runIdentity"],
            "Timed range belongs to another session")
    warmup = validate_warmup(load(path / "warmup" / "submitted-frames.json"), settings["warmupFrames"],
                             record, env["process"]["runIdentity"], frames)
    require(document["schemaVersion"] == 2 and document["sequence"]["frameCount"] == 360 and
            document["stablePassSet"] and document["watch"] is None, "Incomplete or unstable viewer timing range")
    expected = {"gi_reference.fixture": record["fixtureValue"], "gi_reference.field_enabled": True,
                "gi_reference.field_sampling": True, "gi_reference.field_readiness": False,
                "gi_reference.receiver_diagnostics": False, "gi_reference.probe_allocation_diagnostics": False,
                "gi_reference.probe_rays": 256, "gi_reference.seed": 1,
                "view.gi_probes.enabled": case["kind"] != "off", "view.gi_probes.budget": case["budget"],
                "view.gi_probes.mode": case["mode"], "view.gi_probes.depth_mode": case["depth"],
                "view.taa.enabled": False}
    # Frozen frames omit default-valued items. The owning session catalog, captured
    # before warmup, supplies those defaults without duplicating Lab/Host policy here.
    catalog = load(root / record["rhi"] / record["fixture"] / "parameters.json")
    defaults = {v["id"]: v["default"] for v in catalog["parameters"]}
    require(set(expected) <= set(defaults), "Parameter discovery omitted a measured input")
    for frame in frames:
        actual = {v["id"]: v["value"] for v in frame["parameters"]["values"]}
        require(all(actual.get(k, defaults.get(k)) == v for k, v in expected.items()), "Timed viewer/GI inputs drifted")
    samples = document["coverage"]["samples"]
    keep = np.array([s["sequenceFrame"] >= settings["discardFrames"] for s in samples])
    for sample in samples:
        c = sample["conditions"]
        require(c["gpuProfiling"] and not any(c[k] for k in
                ("developmentTools", "graph", "resourceExport", "nonFiniteScan", "productionCounters")),
                "Diagnostic observation contaminated a timed sample")
        require(sample["cpuBuildRecordMilliseconds"] is not None, "CPU measurement is unavailable")
    viewer = np.zeros(len(samples))
    names = set()
    for scope in document["scopes"]:
        if scope["kind"] == "pass":
            names.add(scope["name"])
            if scope["name"].startswith("GI.Probes."):
                viewer[np.asarray(scope["sampleIndices"], int)] += scope["milliseconds"]
    enabled = case["kind"] != "off"
    require(("GI.Probes.Draw" in names) == enabled and ("GI.Probes.Select" in names) == enabled,
            "Timed range did not execute its declared viewer arm")
    require(not any(n.startswith(("GI.Receiver", "GI.Reference.", "RayScene.Build")) or
                    n == "GI.ProbeUpdate.Initialize" for n in names), "Prerequisite/activation work leaked into the measurement")
    _, _, graph, _ = read_capture(path / "allocation")
    summary = graph["frame"]["readiness"]["giField"]["summary"]
    display = summary["probeVisualization"]
    require(display["schemaVersion"] in (5, 6, 7) and display["settings"]["enabled"] == enabled, "Allocation arm differs")
    if record["fixture"] == "coastal":
        require(math.prod(summary["counts"]) == 12870, "Coastal volume was reduced")
    counts = inspect_counts(display) if enabled else None
    if enabled:
        require(display["privateDepthLogicalBytes"] == 1280 * 720 * 4 and
                display["selectionLogicalBytes"] == 64 + case["budget"] * 8 and
                display["readbackAllocatedBytes"] is not None and
                display["readbackAllocatedBytes"] >= display["readbackBufferBytes"], "Incomplete viewer memory accounting")
        require(display["highlightCount"] == int(case["selected"]), "Separate selected marker did not execute")
    values = dict(cpuBuildRecord=np.asarray([s["cpuBuildRecordMilliseconds"] for s in samples])[keep],
                  gpuFrame=np.asarray(document["frame"]["milliseconds"])[keep], gpuViewer=viewer[keep])
    return dict(record=record, metrics={k: stats(v) for k, v in values.items()}, counts=counts,
                allocation=allocation_facts(graph), graphTotals=graph["execution"]["totals"],
                readbackLogicalBytes=display["readbackBufferBytes"], readbackAllocatedBytes=display["readbackAllocatedBytes"],
                environment=env, warmup=warmup), values


def validate_matrix(settings, records):
    expected = {(b, f, r, name) for b in settings["backends"] for f in ("f4", "coastal")
                for r in range(settings["repeats"]) for name in
                ["off-before", "off-after"] + [f"{k}-b{n}-d{d}" for n in settings["budgets"]
                                               for d in (0, 1) for k in ("classification", "irradiance", "selected")]}
    actual = [(r["rhi"], r["fixture"], r["repeat"], r["case"]["id"]) for r in records]
    require(settings["repeats"] >= 2 and len(actual) == len(expected) and set(actual) == expected,
            "The requested viewer matrix/repetitions are incomplete or duplicated")


def validate_cost(root):
    settings, records = load(root / "settings.json"), load(root / "cases.json")
    validate_matrix(settings, records)
    results, groups = [], {}
    for record in records:
        result, values = inspect_cost(root, record, settings)
        results.append(result)
        c = record["case"]
        key = (record["rhi"], record["fixture"], c["kind"], c["budget"] if c["kind"] != "off" else 0, c["depth"] if c["kind"] != "off" else 0)
        groups.setdefault(key, []).append(values)
    aggregate = []
    for key, repetitions in groups.items():
        backend, fixture, kind, budget, depth = key
        baseline = groups[(backend, fixture, "off", 0, 0)]
        metrics = {k: stats(np.concatenate([v[k] for v in repetitions])) for k in repetitions[0]}
        delta = {k: metrics[k]["medianMs"] - stats(np.concatenate([v[k] for v in baseline]))["medianMs"]
                 for k in ("cpuBuildRecord", "gpuFrame")}
        aggregate.append(dict(rhi=backend, fixture=fixture, kind=kind, budget=budget, depth=depth, metrics=metrics, deltaMedianMs=delta))
    save(root / "validation.json", dict(ok=True, performanceAcceptance=False, settings=settings, cases=results, aggregate=aggregate))
    submitted = [r["warmup"]["submittedFrames"] for r in results]
    lines = ["# Probe viewer overhead", "", "Quiet Release, 1280x720, Native/TAA off, validation/DevTools/PIX off.",
             f"Each arm: {settings['repeats']} repetitions, at least {settings['warmupFrames']} verified submitted warmup frames (observed {min(submitted)}-{max(submitted)}), 360 raw frames; first {settings['discardFrames']} sequence frames excluded explicitly.",
             "Warmup counts use the submitted Ready settle-counter delta after the arm/allocation capture; warmup/submitted-frames.json retains observations and endpoint serials. RPC elapsed frames and next-frame receipts are not warmup proof.",
             "CPU = owner-thread elapsed build/record after BeginFrame and before EndFrame; excludes frame-slot waits, submission/Present, content Update and readback tails.",
             "GPU viewer = sample-joined GI.Probes passes, including count copies, private depth clear and the selected marker. Delta = difference of pooled medians against bracketing viewer-off runs.",
             "Graph/allocation evidence is outside timing. Native resource allocation is not residency; allocator totals include retained pools and unrelated resources.",
             "B receiver prerequisites/markers and E picking are separate workloads; these C measurements grant no GI performance or convergence acceptance.", "",
             "| RHI | Fixture | Mode | Budget | Depth | CPU median/p95 ms | CPU delta ms | GPU frame median/p95 ms | GPU delta ms | Viewer GPU median/p95 ms |",
             "| --- | --- | --- | ---: | --- | --- | ---: | --- | ---: | --- |"]
    for a in aggregate:
        def cell(k):
            m = a["metrics"][k]
            return f"{m['medianMs']:.3f}/{m['p95Ms']:.3f}"
        lines.append(f"| {a['rhi']} | {a['fixture']} | {a['kind']} | {a['budget']} | {'scene' if a['depth'] else 'xray'} | {cell('cpuBuildRecord')} | {a['deltaMedianMs']['cpuBuildRecord']:.3f} | {cell('gpuFrame')} | {a['deltaMedianMs']['gpuFrame']:.3f} | {cell('gpuViewer')} |")
    lines += ["", "Per-arm logical/native allocations, pool totals and enumerated/filter/drawn/budget-drop/highlight counts are retained in validation.json.",
              "The ordinary draw submits budget instances; the GPU rejects unused instances. Drawn probe count and submitted instance capacity are different quantities."]
    lines += ["", "| RHI / fixture / arm | Enumerated / slice / class / distance / frustum | Drawn / dropped / highlights | Private depth logical/native B | Selection logical B | Retained readback logical/native B |",
              "| --- | --- | --- | --- | ---: | --- |"]
    for result in results:
        r, c = result["record"], result["counts"]
        if r["repeat"] or c is None:
            continue
        depth = next(a for a in result["allocation"] if a["name"] == "GI.Probes.PrivateDepth")
        stages = '/'.join(str(c[k]) for k in ("enumerated", "afterSlice", "afterClassification", "afterDistance", "afterFrustum"))
        lines.append(f"| {r['rhi']} / {r['fixture']} / {r['case']['id']} | {stages} | {c['rendered']}/{c['budgetDropped']}/{c['highlights']} | {depth['logicalBytes']}/{depth['allocatedBytes']} | {64 + r['case']['budget'] * 8} | {result['readbackLogicalBytes']}/{result['readbackAllocatedBytes']} |")
    (root / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Viewer timing qualified ({len(records)} complete ranges); no performance pass/fail threshold applied")


def validate_images(root):
    results = []
    depth_coverage = {}
    for record in load(root / "cases.json"):
        base, products, graph, manifest = read_capture(root / record["directory"])
        summary = graph["frame"]["readiness"]["giField"]["summary"]
        d = summary["probeVisualization"]
        requested = record["case"]
        require(d["settings"]["enabled"] == (requested["kind"] != "off") and
                d["settings"]["budget"] == requested["budget"] and
                d["settings"]["mode"] == ("irradiance" if requested["mode"] else "classification") and
                d["settings"]["depthMode"] == ("scene-depth" if requested["depth"] else "xray"),
                "Image evidence does not match the requested viewer arm")
        if record.get("defaultBudget"):
            require(d["settings"]["budget"] == 4096, "Inherited viewer budget is not the measured usability default")
            require(not any(v["id"] == "view.gi_probes.budget" for v in graph["frame"]["parameters"]["values"]),
                    "Default-budget verification accidentally used an explicit budget override")
        require(manifest["environment"]["build"]["configuration"] == "Develop" and
                manifest["environment"]["validation"]["effective"] == "standard", "Correctness evidence requires Develop validation")
        images = {s: np.asarray(Image.open(base / next(f for f in products[s]["files"] if f.endswith('.png'))))
                  for s in ("scene", "composited")}
        changed = int(np.any(images["scene"] != images["composited"], axis=-1).sum())
        active = d["settings"]["enabled"]
        if record["fixture"] == "coastal":
            require(math.prod(summary["counts"]) == 12870, "Usability check reduced the Coastal volume")
        passes = {p["name"]: p["executionOrder"] for p in graph["compile"]["passes"] if not p["culled"]}
        if active:
            require(passes["Capture.Scene"] < passes["GI.Probes.Draw"] < passes["Capture.Composited"], "Viewer contaminated a scene tap")
            resource = load(base / products["resource-export"]["metadata"])
            arrays = {t["address"]["resource"]: np.load(base / t["array"], allow_pickle=False) for t in resource["targets"]}
            expected, counts = oracle(summary, arrays["GI.Field.Classification"])
            raw = arrays["GI.Probes.Counts"].reshape(-1)
            ids = arrays["GI.Probes.RenderedIDs"].reshape(2, -1).T
            require(np.array_equal(raw[2:9], counts) and np.array_equal(ids[:len(expected)], expected), "Full-volume filter/budget oracle differs")
            require(np.all(ids[len(expected):, 0] == 0xffffffff) and np.all(ids[len(expected):, 1] == 0),
                    "Unused ordinary instance slots contain stale probe IDs")
            require(int(raw[9]) == d["highlightCount"], "Selected highlight changed ordinary count accounting")
            if d["depthMode"] == "xray":
                require(changed > 0, "X-ray viewer has no image footprint")
            elif record["case"]["kind"] in ("classification", "irradiance"):
                # First-N can retain only below-floor probes. Scene occlusion is valid;
                # require positive output independently at the largest tested capacity.
                key = record["rhi"], record["fixture"], record["case"]["kind"]
                depth_coverage.setdefault(key, []).append((d["settings"]["budget"], changed))
            counts = inspect_counts(d)
        else:
            require(changed == 0 and not any(n.startswith("GI.Probes.") for n in passes), "Disabled viewer still draws")
            counts = None
        results.append(dict(record=record, changedPixels=changed, counts=counts, allocation=allocation_facts(graph)))
    for key, coverage in depth_coverage.items():
        require(max(coverage)[1] > 0, f"Full-budget Scene Depth lost its positive visibility control: {key}")
    receivers = []
    for record in load(root / "receiver-cases.json"):
        _, _, graph, _ = read_capture(root / record["directory"])
        passes = {p["name"] for p in graph["compile"]["passes"] if not p["culled"]}
        require("GI.Field.InspectReceiver" in passes and "GI.Field.ReceiverMarkers" in passes,
                "Receiver prerequisite/marker capture did not execute B")
        receivers.append(dict(record=record, allocation=allocation_facts(graph), totals=graph["execution"]["totals"]))
    save(root / "validation.json", dict(ok=True, cases=results, receiverPrerequisites=receivers, timingQualified=False))
    print(f"Viewer correctness passed ({len(results)} image/count/allocation cases)")


def warmup_self_test():
    case = dict(kind="classification", budget=4096, mode=0, depth=0, selected=False)
    record = dict(rhi="dx12", case=case)
    start = dict(runIdentity="fixture-run", contentId="gglab.lab.gi_reference", parameterRevision=3,
                 controlState={}, field=dict(generation=1), viewRevision=1, extent=[1280, 720], backend="dx12",
                 visualization=dict(enabled=True, budget=4096, mode="classification", depthMode="xray", selection=None),
                 frameSerial=1000, settledFrames=10, ready=True, outcome="submitted")
    end = dict(start, frameSerial=1120, settledFrames=130)
    proof = dict(schema="gglab.probe-viewer-warmup", schemaVersion=1, method="submitted-settle-counter",
                 requestedFrames=120, submittedFrames=120, start=start, end=end, samples=[start, end])
    frames = [dict(parameters=dict(frameSerial=1121, revision=3, contentId=start["contentId"]))]
    def check(value, timed=frames):
        return validate_warmup(value, 120, record, "fixture-run", timed)
    require(check(proof)["submittedFrames"] == 120, "Valid warmup rejected")
    malformed = [dict(status="completed", publication="Published",
                      timing=dict(mode="next-frame", settleFrames=0, settledFrames=0))]
    for key, value in (("settledFrames", 129), ("settledFrames", 9), ("frameSerial", 1119),
                       ("parameterRevision", 4), ("ready", False), ("outcome", "aborted"),
                       ("runIdentity", "replacement"), ("viewRevision", 2), ("field", dict(generation=2))):
        bad = copy.deepcopy(proof)
        bad["end"][key] = value
        malformed.append(bad)
    for key, value in (("submittedFrames", 121), ("requestedFrames", 60), ("schemaVersion", 2)):
        bad = copy.deepcopy(proof)
        bad[key] = value
        malformed.append(bad)
    bad = copy.deepcopy(proof)
    bad["start"]["visualization"]["budget"] = 256
    malformed.append(bad)
    bad = copy.deepcopy(proof)
    bad["start"]["runIdentity"] = bad["end"]["runIdentity"] = "another-run"
    malformed.append(bad)
    for bad in malformed:
        try:
            check(bad)
        except (ValueError, KeyError):
            continue
        raise AssertionError("Invalid warmup accepted")
    for key, value in (("frameSerial", 1120), ("revision", 4), ("contentId", "replacement")):
        timed = copy.deepcopy(frames)
        timed[0]["parameters"][key] = value
        try:
            check(proof, timed)
        except ValueError:
            continue
        raise AssertionError("Unbound timed range accepted")
    overshoot = copy.deepcopy(proof)
    overshoot["end"].update(frameSerial=1125, settledFrames=135)
    overshoot["submittedFrames"] = 125
    timed = copy.deepcopy(frames)
    timed[0]["parameters"]["frameSerial"] = 1126
    require(check(overshoot, timed)["submittedFrames"] == 125, "Verified minimum overshoot rejected")
    return len(malformed) + 5


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--cost", type=Path)
    group.add_argument("--images", type=Path)
    group.add_argument("--self-test", action="store_true")
    parser.add_argument("--case-index", type=int)
    args = parser.parse_args()
    if args.case_index is not None and args.cost is None:
        parser.error("--case-index requires --cost")
    if args.self_test:
        settings = dict(backends=["dx12"], budgets=[256], repeats=2)
        names = ["off-before", "off-after"] + [f"{k}-b256-d{d}" for d in (0, 1)
                                                  for k in ("classification", "irradiance", "selected")]
        records = [dict(rhi="dx12", fixture=f, repeat=r, case=dict(id=n))
                   for f in ("f4", "coastal") for r in range(2) for n in names]
        validate_matrix(settings, records)
        for malformed in (records[:-1], records[:-1] + [records[0]], records + [records[0]]):
            try:
                validate_matrix(settings, malformed)
            except ValueError:
                continue
            raise AssertionError("Incomplete/duplicated matrix was accepted")
        require(stats([1, 9, 2, 3])["medianMs"] == 2, "Nearest-rank statistics changed")
        checks = warmup_self_test()
        print(f"Viewer evidence self-test passed (matrix/statistics and {checks} warmup checks)")
    elif args.cost:
        if args.case_index is None:
            validate_cost(args.cost)
        else:
            records = load(args.cost / "cases.json")
            require(0 <= args.case_index < len(records), "Unknown preflight case")
            result, _ = inspect_cost(args.cost, records[args.case_index], load(args.cost / "settings.json"))
            save(args.cost / records[args.case_index]["directory"] / "case-audit.json", result)
            print("Viewer timing preflight passed")
    else:
        validate_images(args.images)


if __name__ == "__main__":
    main()
