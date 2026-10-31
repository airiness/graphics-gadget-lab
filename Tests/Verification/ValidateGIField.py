"""Static field ownership, full-update coverage and native lifecycle evidence."""
import argparse
from pathlib import Path
import numpy as np
from PIL import Image
from ValidateGIProbeUpdate import load, save, require, fields, bordered

NAMES = tuple("GI.Field." + leaf for leaf in ("Rays", "Irradiance", "Distance", "State"))


def metrics(directory):
    status = load(directory / "status.json")
    require(status["lab"]["id"] == "gglab.lab.gi_reference", "Wrong Lab diagnostics")
    return {m["name"]: m["value"] for m in status["lab"]["metrics"]}


def graph_passes(directory):
    graph = load(next(directory.glob("*.graph.json")))
    passes = {p["index"]: p["name"] for p in graph["compile"]["passes"]}
    return [passes[i] for i in graph["execution"]["executedPassIndices"]]


def check_active(directory, fixture):
    receipt = load(directory / "capture.json")
    require(receipt["status"] == "completed" and receipt["publication"] == "Published", "Unpublished field evidence")
    record = load(next(directory.glob("*.resource-export.json")))
    actual = {p["id"]: p["value"] for p in record["frame"]["parameters"]["values"]}
    # Frozen frame records omit unoverridden defaults; the registered probe default is true.
    require(actual["gi_reference.field_enabled"] and actual.get("gi_reference.probe_enabled", True) and
            actual["gi_reference.fixture"] == fixture, "Field frame parameters differ")
    arrays = {}
    for target in record["targets"]:
        value = np.load(directory / target["array"], allow_pickle=False)
        if value.dtype.fields:
            require(value.dtype.names == ("radianceDistance",), "Field ray layout differs")
            value = value["radianceDistance"]
        require(value.dtype == np.dtype("<f4") and np.isfinite(value).all(), "Nonfinite field payload")
        arrays[target["address"]["resource"]] = value
    require(set(arrays) == set(NAMES), "Field exports are incomplete")
    nx, ny, nz = (33, 10, 39) if fixture == 15 else (2, 2, 2)
    probes = nx * ny * nz
    raw, irradiance, distance, state = (arrays[n] for n in NAMES)
    require(raw.shape == (probes * 256, 4) and irradiance.shape == (ny, nz * 8, nx * 8, 4) and
            distance.shape == (ny, nz * 16, nx * 16, 2) and state.shape == (ny, nz, nx, 4), "Field capacity/layout drift")
    require(np.all(state >= 0) and np.all(state <= 1) and np.all(state[..., 3] == 0) and
            np.allclose(state.sum(-1), 1, atol=1e-6), "A full update omitted probes or traced invalid samples")
    require(np.all(irradiance[..., 3] == 1) and np.all(irradiance[..., :3] >= 0) and np.all(distance >= 0),
            "Field initialization/energy is invalid")
    require(np.all(distance[..., 1] + 1e-4 * np.maximum(distance[..., 1], 1) >= distance[..., 0] ** 2),
            "Distance variance became negative")
    for y in range(ny):
        for z in range(nz):
            for x in range(nx):
                for atlas, side in ((irradiance, 8), (distance, 16)):
                    tile = atlas[y, z*side:(z+1)*side, x*side:(x+1)*side]
                    require(np.array_equal(tile, bordered(tile[1:-1, 1:-1])), "Field octahedral seam drift")
    names = graph_passes(directory)
    expected = ("GI.Field.TraceShade", "GI.Field.BlendShared", "GI.Field.Borders")
    require(all(n in names for n in expected), "Missing field update stages")
    require(names.index(expected[0]) < names.index(expected[1]) < names.index(expected[2]), "Field update order differs")
    opaque = next((i for i, n in enumerate(names) if "Forward" in n and "Opaque" in n), None)
    require(opaque is not None and names.index(expected[-1]) < opaque, "Field writes must precede opaque consumers")
    require(not any(n.startswith("GI.ProbeUpdate.") for n in names), "Experimental and field update chains overlap")
    m = metrics(directory)
    progress = fields(m["GI field"])
    require(progress["initialized"] == 1 and progress["invalid"] == 0 and
            progress["submitted"] >= progress["completed"] > 0 and progress["probes"] == probes and
            progress["rays"] == probes*256*16 and progress["field"] == probes*3088+16,
            "Field progress/capacity is invalid")
    allocations = {n: fields(v) for n, v in m.items() if n.startswith("GI field allocation ")}
    require(len(allocations) == 5+progress["statusReadbacks"] and all(v["allocated"] >= v["logical"] > 0 for v in allocations.values()),
            "Missing field allocation facts")
    return progress, allocations


def validate(root, backends):
    summaries = []
    for backend in backends:
        for fixture in (14, 16, 15):
            directory = root / backend / str(fixture)
            active = {}
            for step in ("activate", "steady", "replace", "reactivate"):
                active[step] = check_active(directory / step, fixture)
            before, before_alloc = active["activate"]
            steady, steady_alloc = active["steady"]
            require(before["generation"] == steady["generation"] and steady["submitted"] > before["submitted"] and
                    before_alloc == steady_alloc, "Steady updates reallocated storage or reset their generation")
            require(len({active[s][0]["generation"] for s in ("activate", "replace", "reactivate")}) == 3,
                    "Pipeline replacements inherited an old field identity")
            images = []
            for step in ("off", "release", "reactivate"):
                images.append(np.asarray(Image.open(next((directory / step).glob("*.png"))).convert("RGB")))
            require(all(np.array_equal(images[0], im) for im in images[1:]), "Field-only work changed the production image")
            for step in ("off", "release"):
                require("GI field" not in metrics(directory / step) and
                        not any(n.startswith(("GI.Field.", "GI.ProbeUpdate.")) for n in graph_passes(directory / step)),
                        "Disabled field still owns/executed an update chain")
            summaries.append(dict(rhi=backend, fixture=fixture, generation=before["generation"],
                                  steadySubmitted=steady["submitted"], probes=before["probes"]))
    return dict(status="passed", cases=summaries, productionLookupQualified=False, readinessQualified=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--rhi", default="dx12,vulkan")
    args = parser.parse_args()
    result = validate(args.root, args.rhi.split(","))
    save(args.root / "validation.json", result)
    print("Static field update/lifecycle checks passed")
