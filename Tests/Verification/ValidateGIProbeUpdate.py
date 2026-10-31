"""Independent probe update arithmetic, coverage, memory and benchmark qualification."""
import argparse
import json
from pathlib import Path

import numpy as np

NAMES = tuple("GI.ProbeUpdate." + x for x in ("Rays", "Irradiance", "Distance", "State"))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def directions(rays):
    # Float64 oracle after the declared, bounded FP32 phase construction.
    # Keeping sin/cos in [-pi, pi] avoids backend large-angle range-reduction error.
    index = np.arange(rays, dtype=np.float64)
    z = 1 - 2 * (index + .5) / rays
    turns = index.astype(np.float32) * np.float32(.3819660112501051)
    shifted = turns + np.float32(.5)
    phase = shifted - np.floor(shifted) - np.float32(.5)
    angle = (phase * np.float32(2 * np.pi)).astype(np.float64)
    radius = np.sqrt(1 - z*z)
    return np.stack((radius*np.cos(angle), radius*np.sin(angle), z), axis=-1)


def rotated_directions(rays, update):
    seed = 61051 ^ update
    def random():
        nonlocal seed
        value = (seed + 0x9e3779b9) & 0xffffffff
        value = ((value ^ (value >> 16))*0x7feb352d) & 0xffffffff
        value = ((value ^ (value >> 15))*0x846ca68b) & 0xffffffff
        seed = value ^ (value >> 16)
        return (seed//512 + .5)/8388608
    u, v, w = random(), 2*np.pi*random(), 2*np.pi*random()
    q = np.array([np.sqrt(1-u)*np.sin(v), np.sqrt(1-u)*np.cos(v), np.sqrt(u)*np.sin(w), np.sqrt(u)*np.cos(w)])
    ray = directions(rays)
    return ray + 2*np.cross(q[:3],np.cross(q[:3],ray)+q[3]*ray)


def octahedral(side):
    yy, xx = np.indices((side, side))
    xy = 2 * (np.stack((xx, yy), axis=-1) + .5) / side - 1
    z = 1 - np.abs(xy).sum(-1)
    folded = (1 - np.abs(xy[..., ::-1])) * np.where(xy >= 0, 1, -1)
    xy = np.where((z < 0)[..., None], folded, xy)
    vectors = np.concatenate((xy, z[..., None]), axis=-1)
    return vectors / np.linalg.norm(vectors, axis=-1)[..., None]


def bordered(interior):
    side = interior.shape[0]
    result = np.zeros((side+2, side+2, interior.shape[-1]), dtype=interior.dtype)
    result[1:-1, 1:-1] = interior
    result[1:-1, 0] = interior[::-1, 0]
    result[1:-1, -1] = interior[::-1, -1]
    result[0, 1:-1] = interior[0, ::-1]
    result[-1, 1:-1] = interior[-1, ::-1]
    result[0, 0], result[0, -1] = interior[-1, -1], interior[-1, 0]
    result[-1, 0], result[-1, -1] = interior[0, -1], interior[0, 0]
    return result


def visits(count, divisor, updates, refresh):
    counts = np.zeros(count, dtype=np.uint32)
    for update in range(updates):
        d = 1 if refresh and update % refresh == 0 else divisor
        slot = update % d
        counts[slot*count//d:(slot+1)*count//d] += 1
    return counts


def selected(count, divisor, updates, refresh):
    return visits(count, divisor, updates, refresh) != 0


def payloads(directory):
    receipt = load(directory / "capture.json")
    require(receipt["status"] == "completed" and receipt["publication"] == "Published", "Unpublished probe capture")
    exports = list(directory.glob("*.resource-export.json"))
    require(len(exports) == 1, "Expected one bounded probe payload group")
    record = load(exports[0])
    arrays = {}
    for target in record["targets"]:
        a = np.load(directory / target["array"], allow_pickle=False)
        if a.dtype.fields:
            require(a.dtype.names == ("radianceDistance",), "Probe ray ABI fields differ")
            a = a["radianceDistance"]
        require(a.dtype == np.dtype("<f4") and np.isfinite(a).all(), "Invalid/nonfinite probe value anywhere in the workload")
        arrays[target["address"]["resource"]] = a
    require(set(arrays) == set(NAMES), "Missing or extra workload arrays")
    return arrays, record


def validate_arrays(arrays, case):
    coastal = case["fixture"] == 15
    nx, ny, nz = (33, 10, 39) if coastal else (2, 2, 2)
    probes, rays = nx*ny*nz, case["rays"]
    raw, irradiance, distance, state = (arrays[n] for n in NAMES)
    require(raw.shape == (probes*rays, 4) and irradiance.shape == (ny, nz*8, nx*8, 4) and
            distance.shape == (ny, nz*16, nx*16, 2) and state.shape == (ny, nz, nx, 4), "Probe profile/CPU-GPU layout differs")
    for a in arrays.values():
        require(np.isfinite(a).all(), "Non-finite samples cannot disappear outside an acceptance subset")
    counts = visits(probes, case["divisor"], case["updates"], case.get("refresh", 0))
    visited = counts != 0
    state = state.reshape(probes, 4)
    require(np.all(state >= 0) and np.all(state <= 1) and np.all(state[:, 3] == 0), "Invalid query fraction")
    require(np.allclose(state[visited].sum(-1), 1, atol=1e-6) and np.all(state[~visited] == 0), "Missing/extra partial probe coverage")
    raw = raw.reshape(probes, rays, 4)
    require(np.all(raw[~visited] == 0), "Unselected ray storage was undefined or modified")
    require(np.all(irradiance[..., :3] >= 0) and np.all(distance >= 0), "Negative integrated energy or moment")
    require(np.all(distance[..., 1] + 1e-4*np.maximum(distance[..., 1], 1) >= distance[..., 0]**2), "Negative distance variance")
    ray = directions(rays)
    cosine = np.maximum(octahedral(6).reshape(-1, 3) @ ray.T, 0)
    weight = np.maximum(octahedral(14).reshape(-1, 3) @ ray.T, 0)**50
    coastal_samples = set(np.linspace(0, probes-1, 64).astype(int))
    max_error = 0.0
    for probe in range(probes):
        x, z, y = probe % nx, probe // nx % nz, probe // (nx*nz)
        a = irradiance[y, z*8:(z+1)*8, x*8:(x+1)*8]
        b = distance[y, z*16:(z+1)*16, x*16:(x+1)*16]
        if not visited[probe]:
            require(np.all(a == 0) and np.all(b == 0), "Unselected atlas tile changed")
            continue
        require(np.all(a[..., 3] == 1), "Updated irradiance tile/border is incomplete")
        require(np.array_equal(a, bordered(a[1:-1, 1:-1])) and
                np.array_equal(b, bordered(b[1:-1, 1:-1])), "Octahedral border copies disagree with the seam contract")
        if coastal:
            if probe in coastal_samples:
                # Static inputs/directions give an independent closed-form history sum.
                # Include repeated refreshes, rather than assuming each probe updated once.
                factor = 1 - float(np.float32(.97)) ** int(counts[probe])
                expected_m = factor * (cosine @ raw[probe,:,:3]) / cosine.sum(-1)[:,None]
                clamped = np.minimum(abs(raw[probe,:,3]), 1.5*np.sqrt(3))
                expected_d = factor * (weight @ np.stack((clamped,clamped**2),axis=-1)) / weight.sum(-1)[:,None]
                require(np.allclose(a[1:-1,1:-1,:3].reshape(-1,3), expected_m, rtol=6e-4, atol=1e-5) and
                        np.allclose(b[1:-1,1:-1].reshape(-1,2), expected_d, rtol=6e-4, atol=1e-5),
                        "Independent sampled Coastal integration/hysteresis differs")
                max_error = max(max_error,
                                float(np.max(abs(a[1:-1,1:-1,:3].reshape(-1,3) - expected_m) /
                                             np.maximum(abs(expected_m), 1))),
                                float(np.max(abs(b[1:-1,1:-1].reshape(-1,2) - expected_d) /
                                             np.maximum(abs(expected_d), 1))))
            continue
        if case.get("rotate",False):
            last = 0
            for update in range(case["updates"]):
                d = 1 if case.get("refresh",0) and update % case["refresh"] == 0 else case["divisor"]
                if update % d * probes//d <= probe < (update % d + 1)*probes//d:
                    last = update
            ray = rotated_directions(rays,last)
            cosine = np.maximum(octahedral(6).reshape(-1,3) @ ray.T,0)
            weight = np.maximum(octahedral(14).reshape(-1,3) @ ray.T,0)**50
        if case["fixture"] == 14:
            require(np.allclose(state[probe], [1, 0, 0, 0]), "Furnace acquired hidden scene hits")
            expected_radiance = np.tile([1, .5, 2], (rays, 1))
            expected_distance = np.full(rays, 30000.)
        else:
            require(case["fixture"] == 16 and np.allclose(state[probe], [0, 0, 1, 0]), "Inside slab backfaces became environment misses")
            position = -.25 + .5 * np.array([x, y, z])
            intersections = np.divide(np.where(ray > 0, 1, -1) - position, ray,
                                      out=np.full_like(ray,np.inf), where=abs(ray)>1e-12)
            expected_distance = -.2 * intersections.min(-1)
            expected_radiance = np.zeros((rays, 3))
        require(np.allclose(raw[probe, :, :3], expected_radiance, rtol=1e-5, atol=1e-6) and
                np.allclose(raw[probe, :, 3], expected_distance, rtol=4e-4, atol=1e-5), "Independent slab/furnace ray contract differs")
        expected_m = (cosine @ expected_radiance) / cosine.sum(-1)[:, None]
        measured_m = a[1:-1, 1:-1, :3].reshape(-1, 3)
        error = np.max(np.abs(measured_m - expected_m) / np.maximum(abs(expected_m), 1))
        max_error = max(max_error, float(error))
        require(error < 1e-4, "Independent irradiance ratio/furnace E=pi*L differs")
        clamped = np.minimum(abs(expected_distance), 1.5*np.sqrt(3)*.5)
        expected_moments = (weight @ np.stack((clamped, clamped**2), axis=-1)) / weight.sum(-1)[:, None]
        require(np.allclose(b[1:-1, 1:-1].reshape(-1, 2), expected_moments, rtol=5e-4, atol=1e-5), "Independent distance-moment integration differs")
    return dict(probes=probes, rays=rays, updatedProbes=int(visited.sum()), integrationMaximumScaledError=max_error,
                fieldLogicalBytes=int(irradiance.nbytes+distance.nbytes+state.nbytes), rayCapacityBytes=int(raw.nbytes),
                invalidFraction=0, productionFieldQualified=False)


def fields(value):
    return {k: float(v) for k, v in (part.split("=", 1) for part in value.split(";"))}


def memory(directory, case=None):
    status = load(directory/"status.json")
    require(status.get("lab", {}).get("id") == "gglab.lab.gi_reference", "Missing immutable Lab diagnostics")
    metrics = {m["name"]:m["value"] for m in status["lab"]["metrics"]}
    rows = [(name, fields(value)) for name,value in metrics.items() if " allocation " in name]
    require(fields(metrics["Transient inventory"])["available"] == 1, "Transient inventory unavailable")
    by_ordinal = {}
    for name,row in rows:
        require(row["facts"] == 0 and row["ordinal"] >= 0 and row["allocated"] >= row["logical"] > 0 and row["alignment"] > 0,
                "Unknown or incomplete native reserved-memory facts: "+name)
        previous = by_ordinal.setdefault(row["ordinal"], row["allocated"])
        require(previous == row["allocated"], "Aliased native allocation facts disagree")
    totals = {owner:sum({r["ordinal"]:r["allocated"] for n,r in rows if n.startswith(owner+" ")}.values())
              for owner in ("Probe","RayScene","Scratch")}
    if case:
        p = fields(metrics["Probe progress"])
        require(p["failed"] == 0 and p["initialized"] == 1 and p["submitted"] == p["completed"] == case["updates"],
                "Bounded workload did not settle exactly")
        require(p["probes"] == (12870 if case["fixture"] == 15 else 8) and p["rayCapacity"] == p["probes"]*case["rays"]*16 and
                p["fieldLogical"] == p["probes"]*3088+16, "Logical capacity diagnostic drift")
        require(len([n for n,r in rows if n.startswith("Probe ")]) == 5+p["statusReadbacks"] and totals["RayScene"] > 0 and totals["Scratch"] > 0,
                "Memory ledger omits workload, ray scene or cached build scratch")
        require(int(metrics["Live ray snapshots"]) == 1, "Settled frame retained replaced snapshots")
        require(p["allocationMs"] > 0 and p["shaderLookupMs"] >= 0 and p["pipelineMs"] > 0 and
                p["allocationMs"] + p["shaderLookupMs"] + p["pipelineMs"] <= p["prepareMs"] + 1e-5,
                "Preparation stages do not form a bounded CPU ledger")
    return dict(uniqueReservedBytes=int(sum(by_ordinal.values())), **{k+"ReservedBytes":int(v) for k,v in totals.items()}), metrics


def reference_arrays(arrays):
    require(all(np.isfinite(a).all() for a in arrays.values()), "Invalid cost-control reference output")
    value = arrays["GI.Reference.Irradiance"]
    outcomes = arrays["GI.Reference.Diagnostics"]
    covered = value[..., 3] != 0
    require(covered.any() and np.all(value[covered, 3] == 1), "Invalid cost-control receiver")
    require(np.all(outcomes[covered, 1:] == 0) and
            np.all((outcomes[covered, 0] >= 0) & (outcomes[covered, 0] <= 1)) and
            np.all(outcomes[covered, 0] * 4 == np.floor(outcomes[covered, 0] * 4)) and
            np.any(outcomes[covered, 0] == 1), "Unexpected furnace query outcomes")
    # A smooth sphere's shading-normal samples can be below its triangle face.
    # Such samples contribute zero without changing the four-sample denominator.
    # Check every covered pixel, including that clipped boundary; do not discard it.
    expected = np.pi * 1.00390625 * outcomes[covered, 0, None]
    require(np.allclose(value[covered, :3], expected, rtol=1e-4, atol=1e-5),
            "Independent stateless/bounded furnace normalization differs")
    require(np.all(value[~covered] == 0) and np.all(outcomes[~covered] == 0),
            "Reference background acquired hidden contributions")


def reference_control(directory):
    pairs = []
    for name in ("reference-bounded","reference-stateless"):
        path = directory/name
        receipt = load(path/"capture.json")
        require(receipt["publication"] == "Published" and receipt["status"] == "completed", "Unpublished reference control")
        record = load(next(path.glob("*.resource-export.json")))
        arrays = {t["address"]["resource"]:np.load(path/t["array"],allow_pickle=False) for t in record["targets"]}
        frame = record["frame"]
        actual = {v["id"]:v["value"] for v in frame["parameters"]["values"]}
        require(actual.get("gi_reference.environment", 0) == 1 and
                frame["sceneInputs"]["environment"]["activeIBLGeneration"] > 0,
                "Reference control did not consume its published constant environment")
        reference_arrays(arrays)
        pairs.append(arrays)
    require(all(np.allclose(pairs[0][name],pairs[1][name],rtol=1e-4,atol=1e-5) for name in pairs[0]),
            "Stateless cost control differs from bounded transport")
    save(directory/"reference-cost-validation.json",dict(status="passed",samples=4,signal="incident irradiance"))


def lifecycle(directory):
    records = {}
    for step in ("off","activate","transform","release","reactivate"):
        totals, metrics = memory(directory/("life-"+step))
        records[step] = dict(memory=totals, metrics=metrics)
        if step in ("off","release"):
            require("Probe progress" not in metrics and totals["ProbeReservedBytes"] == totals["RaySceneReservedBytes"] == 0,
                    "Disabled workload retains persistent probe/ray resources")
        else:
            require(fields(metrics["Probe progress"])["completed"] == 1 and int(metrics["TLAS builds"]) > 0, "Activation did not complete")
            require(int(metrics["Live ray snapshots"]) == 1, "Retired ray snapshots did not drain")
            require(int(metrics["Activation GPU frame"]) > 0 and "Activation GPU RayScene.BuildTLAS" in metrics,
                    "The completed activation build profile was not observed")
            phases = fields(metrics["RayScene preparation"])
            require(phases["lastReplacementMs"] > 0 and phases["replacementAllocateMs"] > 0, "Missing CPU replacement phase timing")
            if step != "transform":
                require(fields(metrics["Initialization GPU"])["frame"] > 0, "The completed first initialization profile was not observed")
    before, after = (records[k]["metrics"] for k in ("activate","transform"))
    require(int(after["Geometry revision"]) == int(before["Geometry revision"]) and
            int(after["BLAS builds"]) == int(before["BLAS builds"]) and
            int(after["Snapshot revision"]) == int(before["Snapshot revision"])+1 and
            int(after["TLAS builds"]) == int(before["TLAS builds"])+1, "Transform did not share immutable geometry and replace TLAS")
    overlap = fields(after["Replacement ray memory"])
    require(int(after["Replacement ray snapshots"]) >= 2 and overlap["known"] == 1 and overlap["inactive"] > 0,
            "Replacement overlap was missed")
    require(records["release"]["memory"]["ScratchReservedBytes"] > 0, "Disabled-path cached scratch was omitted")
    save(directory/"lifecycle-validation.json", dict(status="passed", steps=records))
    return records


def inspect(root, case):
    directory = root / case["directory"]
    arrays, record = payloads(directory)
    frame = record["frame"]
    require(frame["content"]["labId"] == "gglab.lab.gi_reference" and frame["backend"] == case["rhi"], "Probe run identity differs")
    actual = {v["id"]: v["value"] for v in frame["parameters"]["values"]}
    defaults = dict(fixture=0, probe_rays=256, probe_divisor=1, probe_updates=1, probe_rotate=True,
                    probe_shared_blend=True, probe_refresh_period=0, probe_enabled=True)
    expected = dict(fixture=case["fixture"], probe_rays=case["rays"], probe_divisor=case["divisor"],
                    probe_updates=case["updates"], probe_rotate=case.get("rotate",False), probe_shared_blend=case["shared"],
                    probe_refresh_period=case.get("refresh", 0), probe_enabled=True)
    require(all(actual.get("gi_reference."+k, defaults[k]) == v for k,v in expected.items()), "Effective probe parameters drifted")
    summary = validate_arrays(arrays, case)
    graph = load(next(directory.glob("*.graph.json")))
    resources = {v["name"]:v for v in graph["compile"]["resources"]}
    for name in NAMES:
        require(name in resources and resources[name]["logicalBytes"] > 0, "Missing logical allocation facts")
    ledger, metrics = memory(directory, case)
    inputs = fields(metrics["Probe inputs"])
    require(inputs["environmentGeneration"] == (frame["sceneInputs"]["environment"]["activeIBLGeneration"]
                                               if case["fixture"] == 15 else 0),
            "Bounded update consumed a different environment generation")
    require(inputs["atmosphereAttenuation"] == (1 if case["fixture"] == 15 else 0),
            "Bounded update omitted or invented atmosphere attenuation")
    summary.update(memory=ledger, preparation=fields(metrics["Probe progress"]), inputs=inputs)
    summary.update(case=case, signal="experimental probe update; no production field/response certificate")
    save(directory/"validation.json", summary)
    return summary, arrays


def self_test():
    visited = selected(12870, 4, 4, 0)
    require(visited.all() and selected(12870, 4, 1, 0).sum() == 3217, "Partial range coverage")
    repeated = visits(12870, 4, 11, 5)
    require(repeated.min() == 5 and repeated.max() == 5,
            "Two subsequent refreshes must preserve exact per-probe history counts")
    axis_intersections = np.divide(np.array([1.,1.,1.]),np.array([0.,0.,1.]),
                                   out=np.full(3,np.inf),where=np.array([False,False,True]))
    require(axis_intersections.min() == 1, "Parallel slab axes cannot become negative infinity")
    ray = directions(256)
    require(np.max(abs(np.linalg.norm(ray, axis=1)-1)) < 1e-12, "Unit directions")
    c = np.maximum(octahedral(6).reshape(-1, 3) @ ray.T, 0)
    require(np.max(abs((c @ np.ones((256,3))) / c.sum(-1)[:,None]-1)) < 1e-12, "Independent constant-radiance ratio")
    case=dict(fixture=14,rays=64,divisor=4,updates=1)
    arrays={NAMES[0]:np.zeros((8*64,4),dtype=np.float32),NAMES[1]:np.zeros((2,16,16,4),dtype=np.float32),
            NAMES[2]:np.zeros((2,32,32,2),dtype=np.float32),NAMES[3]:np.zeros((2,2,2,4),dtype=np.float32)}
    arrays[NAMES[0]][:128]=[1,.5,2,30000]
    for x in (0,1):
        arrays[NAMES[1]][0,:8,x*8:(x+1)*8]=[1,.5,2,1]
        arrays[NAMES[2]][0,:16,x*16:(x+1)*16]=[1.5*np.sqrt(3)*.5,1.6875]
        arrays[NAMES[3]][0,0,x]=[1,0,0,0]
    validate_arrays(arrays,case)
    for name,index in ((NAMES[0],(200,0)),(NAMES[1],(1,0,0,0)),(NAMES[3],(0,0,0,3))):
        changed={k:v.copy() for k,v in arrays.items()}
        changed[name][index]=float("nan")
        try:
            validate_arrays(changed,case)
        except ValueError:
            continue
        raise AssertionError("Invalid data outside selected probes was hidden: "+name)
    fractions = np.array([0, .5, .75, 1], dtype=np.float32)
    values = np.zeros((4, 4), dtype=np.float32)
    values[1:, :3] = (np.pi * 1.00390625 * fractions[1:, None])
    values[1:, 3] = 1
    outcomes = np.zeros_like(values)
    outcomes[:, 0] = fractions
    reference = {"GI.Reference.Irradiance":values, "GI.Reference.Diagnostics":outcomes}
    reference_arrays(reference)
    wrong = {k:v.copy() for k,v in reference.items()}
    wrong["GI.Reference.Irradiance"][1, :3] = np.pi * 1.00390625
    try:
        reference_arrays(wrong)
    except ValueError:
        pass
    else:
        raise AssertionError("Clipped furnace samples must retain their original denominator")
    print("Probe update CPU contracts passed (coverage, directions, furnace ratio, reference normalization and off-range invalidity)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    elif args.root:
        cases=load(args.root/"cases.json")
        results=[]
        pairs={}
        backend_pairs={}
        for case in cases:
            summary,arrays=inspect(args.root,case)
            key=(case["rhi"],case["fixture"],case["rays"],case["divisor"],case["updates"],case.get("refresh",0),case.get("rotate",False))
            if key in pairs:
                for name in NAMES:
                    require(np.allclose(arrays[name],pairs[key][name],rtol=1e-5,atol=1e-5), "Direct/shared result differs")
            else:
                pairs[key]=arrays
            if case["fixture"] != 15:
                cross_key = (case["fixture"],case["rays"],case["divisor"],case["updates"],case.get("refresh",0),case["shared"],case.get("rotate",False))
                if cross_key in backend_pairs:
                    for name in NAMES:
                        require(np.allclose(arrays[name],backend_pairs[cross_key][name],rtol=6e-4,atol=1e-5), "Cross-RHI contract differs")
                else:
                    backend_pairs[cross_key] = arrays
            results.append(summary)
        for path in sorted(args.root.glob("*/*/reference-stateless")):
            reference_control(path.parent)
        life = {}
        for path in sorted(args.root.glob("*/*/life-activate")):
            life[str(path.parent.relative_to(args.root))] = lifecycle(path.parent)
        save(args.root/"validation.json",dict(status="passed",cases=results,lifecycle=life))
        print(json.dumps(dict(status="passed",observations=len(results))))
    else:
        parser.error("Use --self-test or --root DIRECTORY")
