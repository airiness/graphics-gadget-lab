"""Qualify quiet cost measurements and aggregate sample-joined median/p95 stage times."""
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/"Scripts/Verification"))
import ValidateGpuTiming
from ValidateGIProbeUpdate import load, save, require


def statistics(values):
    ordered = np.sort(values)
    return dict(samples=len(values), medianMs=float(ordered[int(np.ceil(.5*len(values)))-1]),
                p95Ms=float(ordered[int(np.ceil(.95*len(values)))-1]), meanMs=float(np.mean(values)))


def inspect(root, case):
    directory = root/case["directory"]
    status = load(directory/"status.json")
    env = status["environment"]
    require(env["build"]["configuration"] == "Release" and not env["build"]["pixIntegration"] and
            env["validation"]["effective"] == "off" and env["process"]["host"] == "hidden" and
            status["session"]["width"] == 1920 and status["session"]["height"] == 1080, "Cost context is not quiet native-size Release")
    sequence = load(directory/"sequence.json")["sequence"]
    require(sequence["state"] == "completed" and sequence["timingPublication"]["outcome"] == "Published" and
            sequence["parameterPublication"]["outcome"] == "Published", "Unpublished cost range")
    manifest_path = Path(sequence["timingPublication"]["manifest"])
    manifest = load(manifest_path)
    timing_product = next(p for p in manifest["products"] if p["schema"] == "gglab.gpu-timing")
    document = load(manifest_path.parent/timing_product["metadata"])
    ValidateGpuTiming.validate(document)
    require(document["sequence"]["frameCount"] == 360 and document["stablePassSet"] and document["watch"] is None,
            "Incomplete or structurally unstable timing range")
    count = document["coverage"]["recordedSamples"]
    for sample in document["coverage"]["samples"]:
        c = sample["conditions"]
        require(c["gpuProfiling"] and not any(c[k] for k in ("developmentTools","graph","resourceExport","nonFiniteScan","productionCounters")),
                "An observation tool contaminated a timed sample")
    passes = [s for s in document["scopes"] if s["kind"] == "pass"]
    names = {s["name"] for s in passes}
    require(not any(n.startswith("RayScene.Build") or n == "GI.ProbeUpdate.Initialize" for n in names), "Activation leaked into steady timing")
    groups = {"traceShade":[], "blend":[], "borders":[], "reference":[], "receiver":[], "probeTotal":[]}
    expected_trace = "GI.ProbeUpdate.TraversalOnly" if case["kind"] == "traversal" else "GI.ProbeUpdate.TraceShade"
    for scope in passes:
        name = scope["name"]
        for group, match in (("traceShade",name == expected_trace), ("blend",name in ("GI.ProbeUpdate.BlendDirect","GI.ProbeUpdate.BlendShared")),
                             ("borders",name == "GI.ProbeUpdate.Borders"), ("reference",name == "GI.Reference.PathTrace"),
                             ("receiver",name.startswith("GI.Receiver")), ("probeTotal",name.startswith("GI.ProbeUpdate.") and name != "GI.ProbeUpdate.Publish")):
            if match:
                values = np.zeros(count)
                require(len(scope["sampleIndices"]) == count, "Missing stage samples")
                values[np.array(scope["sampleIndices"],dtype=int)] = scope["milliseconds"]
                groups[group].append(values)
    if case["kind"] in ("probe","traversal"):
        require(expected_trace in names and ("GI.ProbeUpdate.BlendShared" if case["shared"] else "GI.ProbeUpdate.BlendDirect") in names and
                "GI.ProbeUpdate.Borders" in names, "Declared probe profile did not execute")
    elif case["kind"] == "off":
        require(not any(n.startswith(("GI.ProbeUpdate.","RayScene.","GI.Receiver","GI.Reference.")) for n in names), "GI-off executes experimental resources/work")
    else:
        require("GI.Reference.PathTrace" in names and not any(n.startswith("GI.ProbeUpdate.") for n in names), "Reference control did not execute")
    metrics = {key:statistics(np.sum(value,axis=0)) for key,value in groups.items() if value}
    metrics["frame"] = statistics(document["frame"]["milliseconds"])
    # Frame parameters are independently schema-audited by the runner. Check the profile across every uncaptured sample.
    parameters_product = next(p for p in manifest["products"] if p["schema"] == "gglab.parameter-frames")
    parameters = load(manifest_path.parent/parameters_product["metadata"])
    expected = {"fixture":12 if case["kind"] == "reference" else 15, "probe_allocation_diagnostics":False,
                "probe_rays":case["rays"], "probe_divisor":case["divisor"], "probe_updates":0,
                "probe_rotate":False, "probe_shared_blend":case["shared"], "probe_traversal_only":case["kind"] == "traversal",
                "probe_enabled":case["kind"] != "off", "accumulate":False, "samples":1, "batch_samples":1, "depth":1}
    defaults = {"probe_allocation_diagnostics":True,"probe_rays":256,"probe_divisor":1,"probe_updates":1,
                "probe_rotate":True,"probe_shared_blend":True,"probe_traversal_only":False,"probe_enabled":True,"fixture":0,"accumulate":True,"samples":64,"batch_samples":64,"depth":1}
    require(len(parameters["frames"]) == count, "Missing uncaptured-frame provenance")
    for frame in parameters["frames"]:
        actual = {v["id"]:v["value"] for v in frame["parameters"]["values"]}
        require(all(actual.get("gi_reference."+k,defaults[k]) == value for k,value in expected.items()), "Timed profile drifted")
    return dict(case=case, metrics=metrics, environment=env, timing=str(manifest_path.parent/timing_product["metadata"]),
                fieldQualification=False, traversalDifferenceIsolatedShading=False)


def main(root):
    results = [inspect(root,case) for case in load(root/"cases.json")]
    save(root/"validation.json",dict(status="passed",cases=results, performanceAcceptance=False))
    lines = ["# GI update cost evidence", "", "Quiet Release / 1920x1080 / 360 samples per case. Stage sums join raw sample indices before statistics.",
             "No production field or performance acceptance is inferred. Traversal controls are separate workloads, not isolated shade timers.", "",
             "| RHI | Workload | Rays | Divisor | Shared | Repeat | Trace median/p95 ms | Blend median/p95 ms | Borders median/p95 ms | Probe total median/p95 ms | Frame median/p95 ms |",
             "| --- | --- | ---: | ---: | --- | ---: | --- | --- | --- | --- | --- |"]
    notes = []
    for result in results:
        c,m = result["case"],result["metrics"]
        def cell(key):
            return f"{m[key]['medianMs']:.3f}/{m[key]['p95Ms']:.3f}" if key in m else "-"
        lines.append(f"| {c['rhi']} | {c['kind']} | {c['rays']} | {c['divisor']} | {c['shared']} | {c['repeat']} | {cell('traceShade')} | {cell('blend')} | {cell('borders')} | {cell('probeTotal')} | {cell('frame')} |")
        if c["kind"] == "reference":
            notes.append(f"{c['rhi']} reference control (1 sample/pixel, D=1): PathTrace median/p95 "
                         f"{cell('reference')} ms; receiver passes {cell('receiver')} ms. "
                         "The probe-ray count column is inactive for this workload.")
    lines.extend(["", *notes])
    (root/"report.md").write_text("\n".join(lines)+"\n",encoding="utf-8")
    print(json.dumps(dict(status="passed",observations=len(results))))


if __name__ == "__main__":
    main(Path(sys.argv[1]))
