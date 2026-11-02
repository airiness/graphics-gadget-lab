"""Independent ordered selection oracle and same-frame probe overlay checks."""
import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "Verification"))
import ValidateCaptureBundles as bundles


def require(value, message):
    if not value:
        raise ValueError(message)


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def oracle(summary, classes):
    settings = summary["probeVisualization"]["settings"]
    nx, ny, nz = summary["counts"]
    ids = np.arange(nx * ny * nz, dtype=np.uint32)
    xyz = np.column_stack((ids % nx, ids // (nx * nz), (ids // nx) % nz))
    # Atlas storage is y slices, z rows, x columns; its last singleton channel is optional.
    raw = classes.reshape(ny, nz, nx)[xyz[:, 1], xyz[:, 2], xyz[:, 0]]
    category = np.where(raw <= 2, raw, 3).astype(np.uint32)
    if classes.reshape(-1)[0] & 0x80000000:
        category[:] = 3
    selected = np.ones(len(ids), dtype=bool)
    counts = [len(ids)]
    axis = settings["sliceAxis"]
    if axis:
        selected &= (xyz[:, axis - 1] >= settings["layerMinimum"]) & (xyz[:, axis - 1] <= settings["layerMaximum"])
    counts.append(int(selected.sum()))
    selected &= ((1 << category) & settings["classificationMask"]) != 0
    counts.append(int(selected.sum()))
    centers = np.asarray(summary["minimum"], np.float64) + xyz * summary["spacing"]
    view = summary["receiverView"]
    if settings["maximumDistance"] > 0:
        selected &= np.linalg.norm(centers - np.asarray(view["cameraPosition"]), axis=1) <= settings["maximumDistance"]
    counts.append(int(selected.sum()))
    matrix = np.asarray(view["viewProjection"], np.float64).reshape(4, 4)
    x, y, z, w = matrix.T
    planes = np.asarray((w + x, w - x, w + y, w - y, z, w - z))
    signed = np.column_stack((centers, np.ones(len(ids)))) @ planes.T
    radius = settings["radiusInSpacings"] * summary["spacing"]
    selected &= np.all(signed >= -radius * np.linalg.norm(planes[:, :3], axis=1), axis=1)
    counts.append(int(selected.sum()))
    eligible = ids[selected]
    chosen = eligible[:settings["budget"]]
    counts.extend((len(chosen), len(eligible) - len(chosen)))
    return np.column_stack((chosen, category[chosen])), counts


def validate(root):
    results = []
    for backend in load(root / "backends.json"):
        for fixture in ("f4", "coastal"):
            previous = None
            for case in load(root / "cases.json"):
                directory = root / backend / fixture / case["id"]
                manifest = bundles.validate(next(directory.glob("*.capture-bundle.json")))
                products = {p["source"]: p for p in manifest["products"]}
                graph = load(directory / products["graph"]["metadata"])
                summary = graph["frame"]["readiness"]["giField"]["summary"]
                display = summary["probeVisualization"]
                require(display["settings"]["enabled"] == case["enabled"], "Effective display enable differs from Host control")
                require(not graph["frame"]["developmentTools"], "Probe display unexpectedly depends on DevTools")
                passes = {p["name"]: p["executionOrder"] for p in graph["compile"]["passes"] if not p["culled"]}
                active = case["enabled"] and not case["taa"]
                require(display["available"] == active, "Display availability does not report the actual supported request")
                images = {k: np.asarray(Image.open(directory / next(f for f in products[k]["files"] if f.endswith(".png"))))
                          for k in ("scene", "composited")}
                changed = int(np.any(images["scene"] != images["composited"], axis=-1).sum())
                if not active:
                    require(not any(n.startswith("GI.Probes.") for n in passes), "Disabled/unsupported display still records GPU work")
                    require(not any(r["name"].startswith("GI.Probes.") for r in graph["compile"]["resources"]),
                            "Disabled/unsupported display declares diagnostic resources")
                    require(changed == 0, "Disabled/unsupported display changed the composited image")
                    require(display["selectionLogicalBytes"] == 0 and display["privateDepthLogicalBytes"] == 0,
                            "Disabled display reports active graph memory")
                    if case["taa"]:
                        require(not display["viewSupported"] and summary["receiverView"]["temporalAA"],
                                "Unsupported display silently changed the requested temporal view")
                    results.append(dict(backend=backend, fixture=fixture, case=case["id"], active=False))
                    continue
                require(passes["Capture.Scene"] < passes["GI.Probes.Draw"] < passes["Capture.Composited"],
                        "Probe array contaminates Scene capture or misses Composited capture")
                require(passes["GI.Probes.Select"] < passes["GI.Probes.Draw"] < passes["GI.Field.Publish"],
                        "Probe selection or field export is ordered incorrectly")
                resource = load(directory / products["resource-export"]["metadata"])
                arrays = {t["address"]["resource"]: np.load(directory / t["array"], allow_pickle=False) for t in resource["targets"]}
                stats = arrays["GI.Probes.Counts"].reshape(-1)
                ids = arrays["GI.Probes.RenderedIDs"].reshape(2, -1).T
                require(stats.dtype == np.dtype("<u4") and stats.size == 16 and stats[0] == 0x47505631 and stats[1] == 1,
                        "Invalid bounded display counter ABI")
                require(ids.dtype == np.dtype("<u4") and ids.shape == (case["budget"], 2), "Rendered ID export is not bounded by the requested budget")
                expected, counts = oracle(summary, arrays["GI.Field.Classification"])
                require(np.array_equal(stats[2:9], counts), f"FP64 filter-stage oracle differs: {stats[2:9]} != {counts}")
                require(np.array_equal(ids[:len(expected)], expected), "Ordered rendered IDs differ from independent first-N selection")
                require(np.all(ids[len(expected):, 0] == 0xffffffff) and np.all(ids[len(expected):, 1] == 0),
                        "Unused ID slots contain stale data")
                observation = display["observation"]
                require(int(stats[12]) + (int(stats[13]) << 32) == observation["producingFrame"] and
                        int(stats[14]) + (int(stats[15]) << 32) == observation["fieldUpdate"] and
                        observation["field"] == summary["field"], "GPU counts and frozen field/frame identities differ")
                require(display["selectionLogicalBytes"] == 64 + case["budget"] * 8 and
                        display["privateDepthLogicalBytes"] == 640 * 400 * 4,
                        "Private full-view depth and bounded selection cost are misreported")
                completed = display["completed"]
                require(completed is not None and completed["observation"]["producingFrame"] < observation["producingFrame"],
                        "Async GUI counters lost their own earlier observation identity")
                if case["id"] == "all":
                    previous = expected
                    require(changed > 20 and len(expected) > 0, "The sphere array has no visible composited footprint")
                elif case["id"] == "repeat":
                    require(np.array_equal(expected, previous), "Repeated same-view selection is unstable")
                elif case["id"] == "budget":
                    require(stats[8] > 0 and np.array_equal(expected, previous[:case["budget"]]), "Budget does not truncate the stable sequence")
                elif case["id"] == "empty":
                    require(len(expected) == 0 and changed == 0, "Inverted slice renders an invented selection")
                elif case["id"] == "bounds":
                    require(len(expected) == 0 and changed > 0, "Volume bounds disappeared with an empty ordinary array")
                if fixture == "coastal":
                    require(counts[0] == 12870, "Coastal field no longer exercises the full production probe count")
                results.append(dict(backend=backend, fixture=fixture, case=case["id"], counts=counts, changedPixels=changed))
    irradiance = []
    if (root / "irradiance-cases.json").is_file():
        from ValidateGIProbeIrradiance import validate as validate_irradiance
        irradiance = validate_irradiance(root)
    return dict(ok=True, cases=results, irradiance=irradiance)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    result = validate(args.root)
    (args.root / "validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"Probe visualization passed ({len(result['cases'])} captures)")
