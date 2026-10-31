"""Compare complete receiver-cache controls; no ROI may conceal changed pixels."""
import argparse
import json
from pathlib import Path

import numpy as np


def arrays(directory):
    result = {}
    for path in directory.glob("*.resource-export.json"):
        group = json.loads(path.read_text(encoding="utf-8-sig"))
        for record in group["targets"]:
            value = np.load(path.parent / record["array"], allow_pickle=False)
            if not np.all(np.isfinite(value)):
                raise AssertionError(f"Nonfinite receiver-cache control: {path}")
            result[record["address"]["resource"]] = value
    if not result:
        raise AssertionError(f"No exported arrays: {directory}")
    return result


def equal(left, right, label):
    if left.keys() != right.keys():
        raise AssertionError(f"Changed resource set: {label}")
    for name in left:
        if (left[name].shape != right[name].shape or left[name].dtype != right[name].dtype or
                left[name].tobytes() != right[name].tobytes()):
            raise AssertionError(f"Changed full array: {label}: {name}")


def validate(root, baseline):
    checked = 0
    for backend in ("dx12", "vulkan"):
        if not (root / backend).exists():
            continue
        for directory in sorted((root / backend).glob("*/resolve-0")):
            first = arrays(directory)
            equal(first, arrays(directory.parent / "resolve-1"), str(directory.parent))
            if baseline:
                equal(first, arrays(baseline / directory.relative_to(root)), "before/after " + str(directory))
            if (directory / "history").exists():
                history = arrays(directory / "history")
                equal(history, arrays(directory.parent / "resolve-1/history"),
                      "resolve-only sums/compensation/RNG " + str(directory.parent))
                opaque = history["GI.Receiver.FaceNormal"][..., 3] == 1
                if not np.any(opaque):
                    raise AssertionError(f"Empty primary receiver control: {directory}")
                if "GI.Reference.Irradiance" in first:
                    if (not np.all(first["GI.Reference.Irradiance"][..., 3][opaque] == 1) or
                            not np.all(first["GI.Reference.Position"][..., 3][opaque] == 1)):
                        raise AssertionError(f"Invalid covered primary/path: {directory}")
                    kinds, counts = np.unique(first["GI.Reference.Identity"][..., 3][opaque], return_counts=True)
                    print(f"{backend}/{directory.parent.name}: primary matches " + str(dict(zip(kinds, counts))))
                elif not np.all(np.isin(first["GI.SpecularVisibility.Visibility"][..., 3][opaque], [1, 2])):
                    raise AssertionError(f"Invalid covered visibility receiver: {directory}")
            checked += 1
        for stem in ("diffuse-d1", "diffuse-d3", "visibility"):
            if not (root / backend / stem).exists():
                continue
            equal(arrays(root / backend / stem / "resolve-0"),
                  arrays(root / backend / (stem + "-tiled") / "resolve-0"),
                  backend + " tile/batch " + stem)
            if (root / backend / stem / "resolve-0/history").exists():
                equal(arrays(root / backend / stem / "resolve-0/history"),
                      arrays(root / backend / (stem + "-tiled") / "resolve-0/history"),
                      backend + " tile/batch sums/compensation/RNG " + stem)
    if not checked:
        raise AssertionError("No receiver-cache controls")
    print(f"Receiver-cache full-array checks passed ({checked} controls)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    validate(args.root, args.baseline)
