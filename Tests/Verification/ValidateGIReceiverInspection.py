"""GPU receiver/ordinary-query agreement and paired marker image checks."""
import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "Verification"))
import ValidateCaptureBundles as bundles


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def validate(root):
    results = []
    for backend in load(root / "backends.json"):
        for case in load(root / "cases.json"):
            directory = root / backend / case["id"]
            bundle_path = next(directory.glob("*.capture-bundle.json"))
            manifest = bundles.validate(bundle_path)
            resource = load(next(directory.glob("*.resource-export.json")))
            result = resource["frame"]["giReceiverInspection"]
            require(result["validity"] not in ("MalformedPayload", "InvalidNumerics"), "Receiver query returned invalid evidence")
            arrays = {t["address"]["resource"]: np.load(directory / t["array"], allow_pickle=False) for t in resource["targets"]}
            arrays = {name: value[0] if value.ndim == 4 and value.shape[0] == 1 else value
                      for name, value in arrays.items()}
            raw = arrays["GI.Field.ReceiverInspection"].reshape(-1)
            require(raw.dtype == np.dtype("<u4") and raw.nbytes == 1168 and raw[0] == 0x47524931,
                    "Native receiver observation lost its declared bounded format")
            values = raw.view(np.float32).reshape(-1, 4)
            x, y = result["request"]["pixel"]
            position = arrays["GI.Receiver.Position"][y, x]
            if result["validity"] != "Background":
                require(np.allclose(position[:3], result["position"], rtol=1e-6, atol=1e-6), "Receiver position differs from paired raster")
                require(np.allclose(arrays["GI.Receiver.VertexNormal"][y, x, :3], result["vertexNormal"], atol=1e-6) and
                        np.allclose(arrays["GI.Receiver.ShadingNormal"][y, x, :3], result["shadingNormal"], atol=1e-6),
                        "Receiver normals differ from the sampled material")
                query = arrays["GI.Field.ReceiverQuery"][y, x]
                irradiance = arrays["GI.Field.ReceiverIrradiance"][y, x, :3]
                require(np.allclose(irradiance, result["irradianceE"], rtol=5e-5, atol=2e-5) and
                        np.isclose(query[0], result["totalWeight"], rtol=5e-5, atol=2e-5) and
                        np.isclose(query[1], result["coverage"], atol=1e-6),
                        "Inspection disagrees with the independently compiled ordinary full-view query")
                require(np.array_equal(values[6, :3], np.asarray(result["irradianceE"], np.float32)),
                        "CPU publication changed the completed GPU irradiance")
            if case["id"] == "background":
                require(result["validity"] == "Background" and not result["corners"], "Background acquired a successful probe query")
            elif case["id"] == "unsupported-cage":
                require(result["validity"] == "NoValidSupport" and len(result["corners"]) == 8 and
                        all(c["reason"] == "NotActive" for c in result["corners"]), "Inactive cage lost rejected corner reasons")
            elif case["id"] == "outside":
                require(result["validity"] == "OutsideCoverage" and not result["corners"], "Outside coverage acquired cage contributions")
            else:
                require(result["validity"] == "Valid" and len(result["corners"]) == 8, "Expected a contributing receiver")
            images = {}
            for product in manifest["products"]:
                if product["source"] in ("scene", "composited"):
                    metadata = load(directory / product["metadata"])
                    require(metadata["giReceiverObservation"] == resource["frame"]["giReceiverObservation"],
                            "Marker image and query did not use the same field/view observation")
                    images[product["source"]] = np.asarray(Image.open(directory / metadata["image"]["file"]).convert("RGB"))
            changed = int(np.any(images["scene"] != images["composited"], axis=2).sum())
            graph = load(next(directory.glob("*.resource-export.graph.json")))
            passes = [graph["compile"]["passes"][i]["name"] for i in graph["execution"]["executedPassIndices"]]
            marker = "GI.Field.ReceiverMarkers"
            if case["markers"]:
                require(marker in passes and passes.index("Capture.Scene") < passes.index(marker) < passes.index("Capture.Composited") and
                        passes.index("GI.Field.InspectReceiver") < passes.index(marker), "GPU marker ordering differs from the capture contract")
                if case["id"] != "background":
                    require(changed > 10, "Receiver markers did not reach the composited image")
            else:
                require(marker not in passes and changed == 0, "Disabled markers changed the production capture")
            require(result["fieldProfile"]["irradianceInteriorWidth"] == (30 if case["id"].startswith("sunroom") else 6),
                    "Receiver inspection did not retain its producing angular profile")
            results.append(dict(backend=backend, case=case["id"], validity=result["validity"], markerPixels=changed,
                                rawBytes=raw.nbytes, corners=len(result["corners"])))
    return dict(ok=True, cases=results)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    result = validate(args.root)
    (args.root / "validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("Receiver contribution, ordinary-query and paired-marker checks passed")
