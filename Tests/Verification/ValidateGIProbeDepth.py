"""GPU D32 boundary conformance and independent native scene-occlusion oracle."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

from ValidateGIProbeVisualization import load, oracle, require
from ValidateGIProbeIrradiance import raster, display_response
from ValidateGIFieldSampling import bilinear
import ValidateCaptureBundles as bundles


def arrays(directory):
    resource = load(next(directory.glob("*.resource-export.json")))
    return resource, {t["address"]["resource"]: np.load(directory / t["array"], allow_pickle=False)
                      for t in resource["targets"]}


def cases(reversed_z):
    clear, near = (0, 0x3f800000) if reversed_z else (0x3f800000, 0)
    adjacent_clear, adjacent_near = (1, 0x3f7fffff) if reversed_z else (0x3f7fffff, 1)
    return [
        (.25, .75), (.75, .25), (.5, .5), (.5, 0x3f000001), (.5, 0x3effffff),
        (clear, clear), (clear, adjacent_clear), (near, near), (near, adjacent_near),
        (0xbdcccccd, .5), (0x3f8ccccd, .5), (0x7fc00000, .5), (0x7f800000, .5),
        (0x80000000, 0x7fc00000), (.25, .75, clear), (.5, .5, .5),
        (.25, .75, near), (.25, .75, .5), (0x3effffff, 0x3f000001, .5),
        (.25, .75, 0x7fc00000), (.5, .5), (.25, .75),
        (clear, adjacent_clear), (clear, clear), (near, near),
        (clear, 0x00800000), (0x00800000, 0x00800001),
        (1, 0x7fc00000, 0), (1, 0, 0), (0x007fffff, 0x00800000, 0)]


def bits(value):
    return int(np.float32(value).view(np.uint32)) if isinstance(value, float) else value


def comparable(word):
    # Native D32 conversion is part of the comparison domain, before scene
    # rejection as well as private depth. Cases 22-24 bypass shader admission
    # to independently qualify this conversion on the actual hardware.
    return 0 if word < 0x00800000 or word == 0x80000000 else word


def admitted(word, reversed_z):
    word = comparable(word)
    return 0 < word <= 0x3f800000 if reversed_z else word < 0x3f800000


def conformance(directory):
    bundles.validate(next(directory.glob("*.capture-bundle.json")))
    _, data = arrays(directory)
    results = []
    for reversed_z, convention in enumerate(("Standard", "Reversed")):
        prefix = f"GI.Depth.{convention}."
        inputs = data[prefix + "Inputs"].reshape(16, 30 * 16)
        scene = data[prefix + "Scene"].reshape(16, 30 * 16).view(np.uint32)
        winners = data[prefix + "Winners"].reshape(16, 30 * 16)
        depths = data[prefix + "D32"].reshape(16, 30 * 16).view(np.uint32)
        clear = 0 if reversed_z else 0x3f800000
        for index, case in enumerate(cases(reversed_z)):
            a, b = map(bits, case[:2])
            scene_word = bits(case[2]) if len(case) == 3 else clear
            scene_enabled, admission = len(case) == 3, (index < 22 or index >= 25)
            words = [index, reversed_z, int(scene_enabled), int(admission), a, b,
                     scene_word, 2, int(index in (20, 21)), 1, 0, 0, 0, 0, 0, 0]
            region = np.s_[:, index * 16:(index + 1) * 16]
            require(np.all(inputs[region] == words), f"{convention} case {index}: frozen inputs changed")
            require(np.all(scene[region] == scene_word), f"{convention} case {index}: sampled scene bits changed")
            winner, current = 0, clear
            for probe, word in enumerate((a, b), 1):
                if admission and not admitted(word, reversed_z):
                    continue
                value, scene_value = comparable(word), comparable(scene_word)
                if scene_enabled and (not admitted(word, reversed_z) or scene_value > 0x3f800000 or
                                      not (value >= scene_value if reversed_z else value <= scene_value)):
                    continue
                if (value > current if reversed_z else value < current):
                    winner, current = probe, value
            require(np.all(winners[region] == winner),
                    f"{convention} case {index}: GPU winner {np.unique(winners[region])} != {winner}")
            actual_depth = depths[region].copy()
            actual_depth[actual_depth == 0x80000000] = 0
            require(np.all(actual_depth == current),
                    f"{convention} case {index}: GPU D32 bits {np.unique(actual_depth)} != {current:#x}")
            results.append(dict(convention=convention, case=index, winner=winner, depthBits=current, pixels=256,
                                subnormalConversion="flush-to-zero"))
    return results


def validate(root):
    records, boundaries = [], []
    for backend in load(root / "backends.json"):
        boundaries.append(dict(backend=backend, cases=conformance(root / backend / "coordinate_conformance" / "boundaries")))
        for fixture in ("f4", "coastal"):
            selected_before = None
            for mode in (0, 1):
                for depth_mode in (0, 1):
                    directory = root / backend / "gi_reference" / fixture / f"mode{mode}-depth{depth_mode}"
                    bundles.validate(next(directory.glob("*.capture-bundle.json")))
                    resource, data = arrays(directory)
                    summary = resource["frame"]["readiness"]["giField"]["summary"]
                    display = summary["probeVisualization"]
                    require(display["schemaVersion"] in (4, 5, 6, 7) and display["available"] and display["viewSupported"] and
                            display["depthSubnormalConversion"] == "positive-subnormal-to-zero",
                            "Native display schema/availability is incorrect")
                    require(display["depthMode"] == ("scene-depth" if depth_mode else "xray") and
                            display["settings"]["depthMode"] == display["depthMode"], "Requested depth mode was not applied")
                    require(display["sceneDepthAdditionalLogicalBytes"] == 0 and
                            display["sceneDepthSource"] == ("DisplayView.DepthBuffer" if depth_mode else None),
                            "Scene depth mode allocates a hidden full-view resource or lacks provenance")
                    graph = load(next(directory.glob("*.graph.json")))
                    draw = next(p for p in graph["compile"]["passes"] if p["name"] == "GI.Probes.Draw")
                    scene_reads = [a for a in draw["accesses"] if a["resource"] == "DisplayView.DepthBuffer"]
                    require(len(scene_reads) == depth_mode and all(
                        a["dependencyAccess"] == "Read" and a["access"] == "Sample" and a["stages"] == "PixelShader"
                        for a in scene_reads), "Probe display writes scene depth or declares the wrong pixel-read dependency")
                    resources = [r for r in graph["compile"]["resources"] if r["name"].startswith("GI.Probes.")]
                    expected_resources = {"GI.Probes.Counts", "GI.Probes.RenderedIDs", "GI.Probes.PrivateDepth", "GI.Probes.Counts.Readback"}
                    if display["schemaVersion"] >= 6:
                        expected_resources.add("GI.Probes.CompletedChange.Upload")
                        upload = next(r for r in resources if r["name"] == "GI.Probes.CompletedChange.Upload")
                        require(upload["type"] == "Buffer" and upload["imported"] and upload["logicalBytes"] == 16 and
                                upload["buffer"] == {"sizeBytes": 16, "strideBytes": 16},
                                "Ordinary depth display has an unbounded Change fallback binding")
                    require({r["name"] for r in resources} == expected_resources,
                        "Depth mode introduced an undeclared diagnostic attachment")
                    selected, counts = oracle(summary, data["GI.Field.Classification"])
                    ids = data["GI.Probes.RenderedIDs"].reshape(2, -1).T
                    require(np.array_equal(data["GI.Probes.Counts"].reshape(-1)[2:9], counts) and
                            np.array_equal(ids[:len(selected)], selected), "Depth mode changed deterministic filtering")
                    if selected_before is not None:
                        require(np.array_equal(selected, selected_before), "Depth/mode switches changed the selected IDs")
                    selected_before = selected
                    mask, winners, normals, probe_depth = raster(summary, selected, full=True)
                    scene_depth = data["DisplayView.DepthBuffer"].reshape(mask.shape)
                    # Exclude CPU/GPU interpolation rounding near an occluder. Exact adjacent-float
                    # decisions are tested separately against actual D32 bits in conformance().
                    difference = probe_depth.astype(np.float64) - scene_depth
                    margin = np.maximum(np.abs(scene_depth) * 2e-6, 2e-7)
                    clear = scene_depth == (0 if display["view"]["reversedDepth"] else 1)
                    front = difference > margin if display["view"]["reversedDepth"] else difference < -margin
                    back = difference < -margin if display["view"]["reversedDepth"] else difference > margin
                    visible = mask & (front | clear) if depth_mode else mask
                    occluded = mask & back & ~clear if depth_mode else np.zeros_like(mask)
                    require(visible.sum() > 20, f"{backend}/{fixture}: too few visible interior pixels")
                    if depth_mode:
                        require(occluded.sum() > 20, f"{backend}/{fixture}: fixture does not exercise scene occlusion")
                    image = np.asarray(Image.open(next(directory.glob("*.p1.png"))))
                    scene_image = np.asarray(Image.open(next(directory.glob("*.p0.png"))))
                    require(np.array_equal(image[occluded], scene_image[occluded]), "Occluded probes overwrote scene pixels")
                    if mode:
                        nx, _, nz = summary["counts"]
                        probes = winners[visible]
                        xyz = np.column_stack((probes % nx, probes // (nx * nz), (probes // nx) % nz))
                        response = bilinear(data["GI.Field.Irradiance"], xyz, normals[visible], summary["irradianceInteriorWidth"])[:, :3]
                        expected = display_response(response, display["settings"]["previewEV"])
                    else:
                        light_direction = np.asarray((-1, 2, -3)) / np.sqrt(14)
                        light = .45 + .55 * np.clip(normals[visible] @ light_direction, 0, 1)
                        expected = light[:, None] * np.asarray((.08, .8, .5)) * 255
                    error = np.max(np.abs(image[visible, :3].astype(float) - expected), axis=-1)
                    require(np.quantile(error, .99) <= 3 and np.mean(error <= 2) >= .98,
                            f"{backend}/{fixture}/mode{mode}/depth{depth_mode}: visible sphere colour differs, p99={np.quantile(error,.99):.3f}")
                    records.append(dict(backend=backend, fixture=fixture, mode=mode, depthMode=depth_mode,
                                        visiblePixels=int(visible.sum()), occludedPixels=int(occluded.sum()),
                                        p99ByteError=float(np.quantile(error, .99))))
    return dict(ok=True, boundaries=boundaries, scenes=records)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--root", type=Path)
    group.add_argument("--conformance", type=Path)
    args = parser.parse_args()
    result = dict(ok=True, cases=conformance(args.conformance)) if args.conformance else validate(args.root)
    output = args.conformance if args.conformance else args.root
    (output / "validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(dict(ok=True, output=str(output))))
