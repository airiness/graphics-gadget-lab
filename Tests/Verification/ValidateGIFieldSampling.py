"""Independent FP64 lookup oracle and scoped static-field quality measurements."""
import argparse
import itertools
import json
from pathlib import Path

import numpy as np

from ValidateGIReferenceLighting import f4_oracle, masks as reference_masks


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def bilinear(atlas, probes, directions, interior):
    d = directions / np.abs(directions).sum(axis=1)[:, None]
    uv = d[:, :2].copy()
    folded = d[:, 2] < 0
    uv[folded] = (1 - np.abs(uv[folded, ::-1])) * np.where(uv[folded] >= 0, 1, -1)
    texels = probes[:, [0, 2]] * (interior + 2) + 1 + (uv * .5 + .5) * interior - .5
    lower = np.floor(texels).astype(int)
    f = texels - lower
    result = np.zeros((len(probes), atlas.shape[-1]), dtype=np.float64)
    for x, y in itertools.product((0, 1), repeat=2):
        weight = (f[:, 0] if x else 1-f[:, 0]) * (f[:, 1] if y else 1-f[:, 1])
        result += weight[:, None] * atlas[probes[:, 1], lower[:, 1]+y, lower[:, 0]+x]
    return result


def oracle(position, normal, vertex, camera, minimum, spacing, counts, irradiance, distance, classes,
           view_bias=.3):
    interior = irradiance.shape[2]//counts[0]-2
    require(interior in (6, 14, 30) and irradiance.shape ==
            (counts[1], counts[2]*(interior+2), counts[0]*(interior+2), 4),
            "Invalid irradiance atlas layout")
    grid = (position - minimum) / spacing
    edge = np.minimum(grid, counts-1-grid).min(axis=1).clip(0, 1)
    coverage = edge*edge*(3-2*edge)
    view = camera-position
    view /= np.linalg.norm(view, axis=1)[:, None]
    biased = position + spacing*(.1*vertex+view_bias*view)
    cage = np.minimum(np.maximum((biased-minimum)/spacing, 0), counts-1)
    lower = np.minimum(np.floor(cage).astype(int), counts-2)
    f = (cage-lower).clip(0, 1)
    energy = np.zeros((len(position), 3))
    total = np.zeros(len(position))
    for offset in itertools.product((0, 1), repeat=3):
        offset = np.asarray(offset)
        probe = lower+offset
        origin = minimum+spacing*probe
        toward = origin-position
        toward /= np.maximum(np.linalg.norm(toward, axis=1), 1e-10)[:, None]
        weight = np.where(offset, f, 1-f).prod(axis=1) * (((toward*normal).sum(axis=1)+1)*.5)**2
        # Backface wrapping includes a 0.2 floor before visibility and crushing.
        weight += .2*np.where(offset, f, 1-f).prod(axis=1)
        vector = biased-origin
        length = np.linalg.norm(vector, axis=1)
        direction = np.where((length > 1e-10)[:, None], vector/np.maximum(length, 1e-10)[:, None], normal)
        moments = bilinear(distance, probe, direction, 14)
        variance = np.maximum(moments[:, 1]-moments[:, 0]**2, 1e-4*spacing**2)
        delta = np.maximum(length-moments[:, 0], 0)
        weight *= (variance/(variance+delta**2))**3
        weight = np.where(weight < .2, weight**3/.2**2, weight)
        weight *= (classes[probe[:, 1], probe[:, 2], probe[:, 0], 0] == 1) & (coverage > 0)
        total += weight
        energy += weight[:, None]*bilinear(irradiance, probe, normal, interior)[:, :3]
    energy *= np.pi/np.where(total > 0, total, 1)[:, None]
    support = np.where(coverage == 0, 4, np.where(total == 0, 3, np.where(total < .05, 2, 1)))
    return energy, np.column_stack((total, coverage, support, np.ones(len(total))))


def volume(fixture):
    if fixture in (0, 1):
        return np.array([-3., -3., -3.]), .5, np.array([13, 13, 13])
    if fixture in (2, 3):
        return np.array([-4., -2., -4.]), 1., np.array([9, 9, 9])
    if fixture == 4:
        return np.array([-3.125, -1.3125, -3.0625]), .375, np.array([18, 18, 18])
    if fixture in (17, 18):
        return np.array([-1., -1., -1.]), .5, np.array([5, 5, 5])
    if fixture == 15:
        return np.array([-17.25, -1.5, -23.25]), 1., np.array([33, 10, 39])
    return np.array([-3., -1., -3.]), 1., np.array([7, 7, 7])


def inspect(directory, case):
    record = load(next(directory.glob("*.resource-export.json")))
    frame = record["frame"]
    actual_parameters = dict(fixture=0, albedo=0, ev100=10, field_enabled=False,
                             field_sampling=True, probe_enabled=True)
    actual_parameters.update({p["id"].removeprefix("gi_reference."): p["value"]
                              for p in frame["parameters"]["values"] if p["id"].startswith("gi_reference.")})
    require(all(actual_parameters[k] == case[k] for k in ("fixture", "albedo", "ev100")) and
            all(actual_parameters[k] for k in ("field_enabled", "field_sampling", "probe_enabled")),
            "Lookup frame did not consume its declared field/fixture parameters")
    require(frame["sceneInputs"]["exposure"]["effectiveEV100"] == case["ev100"], "Field exposure differs")
    require(frame["camera"]["referenceView"] == case["view"], "Wrong receiver view")
    require(frame["temporal"]["status"] == "disabled" and frame["temporal"]["jitterPixels"] == [0, 0],
            "Field diagnostic receivers must be unjittered")
    values = {t["address"]["resource"]: np.load(directory/t["array"], allow_pickle=False) for t in record["targets"]}
    require(len(values) == 8 and all(np.isfinite(v).all() for v in values.values()), "Incomplete/nonfinite lookup group")
    classes = values["GI.Field.Classification"]
    require(classes.dtype == np.dtype("<u4") and np.isin(classes, [1, 2]).all(), "Unclassified probes reached a query")
    minimum, spacing, counts = volume(case["fixture"])
    require(classes.shape == (counts[1], counts[2], counts[0], 1), "Classification grid differs")
    tile = 32 if case["fixture"] == 4 else 8
    require(values["GI.Field.Irradiance"].shape == (counts[1], counts[2]*tile, counts[0]*tile, 4),
            "Fixture irradiance angular profile differs")
    position = values["GI.Receiver.Position"][0]
    normal = values["GI.Receiver.ShadingNormal"][0, ..., :3]
    vertex = values["GI.Receiver.VertexNormal"][0, ..., :3]
    energy = values["GI.Field.ReceiverIrradiance"][0]
    query = values["GI.Field.ReceiverQuery"][0]
    covered = position[..., 3] == 1
    require(covered.any() and np.array_equal(energy[..., 3], covered.astype(float)) and
            np.array_equal(query[..., 3], covered.astype(float)), "Field diagnostics dropped opaque receivers")
    require((energy[~covered] == 0).all() and (query[~covered] == 0).all(), "Undefined diagnostic background")
    coords = np.flatnonzero(covered)
    coords = coords[::max(1, len(coords)//4096)]
    camera = np.asarray(frame["camera"]["position"], dtype=np.float64)
    expected, expected_query = oracle(position.reshape(-1, 4)[coords, :3].astype(float),
        normal.reshape(-1, 3)[coords].astype(float), vertex.reshape(-1, 3)[coords].astype(float),
        camera, minimum, spacing, counts, values["GI.Field.Irradiance"], values["GI.Field.Distance"], classes,
        view_bias=.15 if case["fixture"] == 4 else .3)
    actual = energy.reshape(-1, 4)[coords, :3]
    actual_query = query.reshape(-1, 4)[coords]
    scaled = float(np.max(np.abs(actual-expected)/np.maximum(1., np.abs(expected))))
    require(scaled < 2e-3, f"GPU lookup differs from FP64 integration: {scaled}")
    require(np.max(np.abs(actual_query[:, :2]-expected_query[:, :2])) < 1e-4 and
            np.array_equal(actual_query[:, 2:], expected_query[:, 2:]), "GPU support/coverage differs from oracle")
    require((energy[..., :3] >= 0).all(), "Negative field energy")
    no_support = covered & np.isin(query[..., 2], [3, 4])
    require((energy[no_support, :3] == 0).all(), "Unsupported field query substituted sky")
    graph = load(next(directory.glob("*.graph.json")))
    names = {p["index"]: p["name"] for p in graph["compile"]["passes"]}
    executed = [names[i] for i in graph["execution"]["executedPassIndices"]]
    require("GI.Field.Classify" not in executed, "A static field reclassified unchanged geometry")
    opaque = next(i for i, n in enumerate(executed) if "Forward" in n and "Opaque" in n)
    require(executed.index("GI.Field.Borders") < opaque < executed.index("GI.Field.Publish") and
            executed.index("GI.Field.Lookup") < executed.index("GI.Field.Publish"), "Field export closed access before its consumers")
    mask = covered.copy()
    if case["fixture"] == 0:
        to_camera = camera-position[..., :3]
        to_camera /= np.maximum(np.linalg.norm(to_camera, axis=-1), 1e-12)[..., None]
        mask &= (normal*to_camera).sum(-1) >= .5
    elif case["fixture"] == 1:
        mask &= (np.abs(position[..., 0]) <= 1) & (np.abs(position[..., 2]) <= 1)
    if case["fixture"] == 17:
        require(np.any(covered & (query[..., 1] == 0)) and
                np.any(covered & (query[..., 1] > 0) & (query[..., 1] < 1)), "Coverage fixture did not exercise the edge")
    if case["fixture"] == 18:
        require(np.all(classes == 2) and np.all(query[covered, 2] == 3) and
                np.all(energy[covered, :3] == 0), "Inactive cages did not return zero NoValidSupport")
    require(mask.any(), "Empty field measurement mask")
    mean = energy[mask, :3].mean(axis=0)
    quality = None
    if case["fixture"] in (0, 1):
        error = float(np.max(np.abs(mean/np.pi-1)))
        quality = dict(gate="F1 area-mean irradiance within 1% of pi L", error=error, passed=error <= .01)
    elif case["fixture"] == 2:
        leak = float(np.max(mean/np.pi))
        quality = dict(gate="F2 interior mean / constant lit irradiance <= 1%", error=leak, passed=leak <= .01)
    elif case["fixture"] == 4:
        reference_directory = directory.parent/"reference"
        reference_record = load(next(reference_directory.glob("*.resource-export.json")))
        reference_values = {t["address"]["resource"]: np.load(reference_directory/t["array"], allow_pickle=False)[0]
                            for t in reference_record["targets"]}
        reference_position = reference_values["GI.Receiver.Position"]
        require(np.array_equal(position[..., 3], reference_position[..., 3]) and
                np.max(np.abs(position[covered, :3]-reference_position[covered, :3])) < 1e-4,
                "Matched reference receivers changed")
        ref_energy = reference_values["GI.Reference.Irradiance"]
        luminance = np.array([.2126, .7152, .0722])
        rois = {}
        for name, roi in reference_masks(case, reference_values, reference_record["frame"]["camera"]).items():
            require(roi.any() and np.all(covered[roi]) and np.all(ref_energy[roi, 3] == 1),
                    f"Invalid or empty F4 comparison ROI: {name}")
            field_mean = energy[roi, :3].astype(float).mean(0)
            ref_mean = ref_energy[roi, :3].astype(float).mean(0)
            sun, sky = f4_oracle(position[roi, :3].astype(float), case["view"] == "F4_Floor")
            analytic_mean = (sun+sky).mean(0)
            bias = float((field_mean-ref_mean)@luminance/max(ref_mean@luminance, 1e-6))
            analytic_bias = float((field_mean-analytic_mean)@luminance/max(analytic_mean@luminance, 1e-6))
            chromaticity_error = float(np.max(np.abs(field_mean[:2]/max(field_mean.sum(), 1e-6)-
                                                     ref_mean[:2]/max(ref_mean.sum(), 1e-6))))
            rois[name] = dict(pixels=int(roi.sum()), fieldMean=field_mean.tolist(),
                             referenceMean=ref_mean.tolist(), analyticMean=analytic_mean.tolist(),
                             relativeBias=bias, analyticRelativeBias=analytic_bias,
                             chromaticityError=chromaticity_error,
                             passed=abs(bias) <= .1 and chromaticity_error <= .02)
        quality = dict(gate="F4 one-bounce per-ROI energy within 10% and chromaticity within 0.02",
                       error=max(abs(row["relativeBias"]) for row in rois.values()),
                       passed=all(row["passed"] for row in rois.values()), rois=rois,
                       referenceDepth=1, referenceSamples=1024, referenceConvergenceQualified=False)
    return dict(id=case["id"], fixture=case["fixture"], checkedPixels=len(coords), scaledError=scaled,
                meanIrradiance=mean.tolist(), active=int((classes == 1).sum()), inactive=int((classes == 2).sum()),
                supports={str(s): int((query[covered, 2] == s).sum()) for s in (1, 2, 3, 4)}, quality=quality)


def inspect_ao(directory):
    records, values = {}, {}
    for arm in ('gtao-off', 'gtao-on'):
        folder = directory/arm
        record = load(next(folder.glob('*.resource-export.json')))
        records[arm] = record
        values[arm] = {t['address']['resource']: np.load(folder/t['array'], allow_pickle=False)
                       for t in record['targets']}
        require(all(np.isfinite(a).all() for a in values[arm].values()), 'Nonfinite A0 comparison')
        require(record['frame']['temporal']['gtao']['enabled'] == (arm == 'gtao-on'), 'Wrong A0 GTAO arm')
    off, on = values['gtao-off'], values['gtao-on']
    p = on['GI.Receiver.Position'][0]
    require(np.array_equal(p, off['GI.Receiver.Position'][0]), 'A0 receivers changed between arms')
    coverage = on['GI.Field.ReceiverQuery'][0, ..., 1]
    require(np.array_equal(coverage, off['GI.Field.ReceiverQuery'][0, ..., 1]), 'A0 coverage changed')
    ao = on['GTAO.FinalAO'][0, ..., 0].astype(float)
    if on['GTAO.FinalAO'].dtype == np.dtype('uint8'):
        ao /= 255.
    edge = (p[..., 3] == 1) & (coverage > 0) & (coverage < 1) & (ao < .98)
    require(edge.sum() >= 32, 'A0 fixture did not exercise occluded volume-edge receivers')
    factor = on['GI.Receiver.DiffuseWeight'][0, ..., :3].astype(float) * \
        on['GI.Receiver.BaseColor'][0, ..., :3].astype(float) / np.pi
    cube = on['IBL.IrradianceCubemap'][..., :3].astype(float)
    ibl = cube.mean(axis=(0, 1, 2))
    require(np.max(np.abs(cube-ibl)) < .01, 'A0 control requires its constant published environment')
    colours = {arm: values[arm]['DisplayView.SceneColor'][0, ..., :3].astype(float) /
               records[arm]['frame']['sceneInputs']['exposure']['preExposure'] for arm in values}
    # Correct the actual field's stochastic change between submitted frames;
    # specular IBL cancels and material AO/clearcoat are fixture constants 1/0.
    delta_field = off['GI.Field.ReceiverIrradiance'][0, ..., :3].astype(float) - \
        on['GI.Field.ReceiverIrradiance'][0, ..., :3].astype(float)
    observed = colours['gtao-off']-colours['gtao-on']-factor*coverage[..., None]*delta_field
    expected = factor*((1-coverage)*(1-ao))[..., None]*ibl
    error = float(np.max(np.abs(observed[edge]-expected[edge])))
    require(error < .006, f'GTAO attenuated the GI share or changed another lighting term: {error}')
    return dict(edgePixels=int(edge.sum()), maximumRadianceError=error)


def validate(root):
    # Only GPU capture comparisons need Pillow; the CPU self-test does not.
    from PIL import Image

    cases = load(root/"cases.json")
    report = []
    ao_report = []
    for backend in load(root/"backends.json"):
        backend_cases = [dict(rhi=backend, **inspect(root/backend/case["id"]/"lookup", case)) for case in cases]
        report.extend(backend_cases)
        ao_report.append(dict(rhi=backend, **inspect_ao(root/backend/'a0')))
        plane_means = [np.asarray(c["meanIrradiance"]) for c in backend_cases if c["fixture"] == 1]
        require(all(np.max(np.abs(mean/plane_means[0]-1)) < .01 for mean in plane_means[1:]),
                "Exposure or primary albedo changed scene-referred field irradiance")
        for control in ("plane", "coastal"):
            if control not in {case["id"] for case in cases}:
                continue
            images = {arm: np.asarray(Image.open(next((root/backend/control/arm).glob("*.png"))).convert("RGB"))
                      for arm in ("on", "update-only", "off")}
            require(np.array_equal(images["update-only"], images["off"]), "Update-only field changed production shading")
            require(not np.array_equal(images["on"], images["off"]), "Field composition never affected opaque shading")
    # Quality measurements retain failed gates independently of the lookup contract.
    return dict(status="passed", lookupContractsPassed=True, quality=report, aoComparisons=ao_report,
                staticFieldQualityQualified=False, readinessQualified=False)


def self_test():
    counts = np.array([3, 3, 3])
    atlas = np.ones((3, 24, 24, 4))
    moments = np.empty((3, 48, 48, 2))
    moments[:] = [10, 100]
    classes = np.ones((3, 3, 3, 1), dtype=np.uint32)
    points = np.array([[1., 1., 1.], [.5, 1., 1.], [-1., 1., 1.]])
    normals = np.tile([0., 1., 0.], (3, 1))
    args = (points, normals, normals, np.array([1., 3., -3.]), np.zeros(3), 1., counts, atlas, moments, classes)
    for width in (6, 14, 30):
        resized = np.ones((3, 3*(width+2), 3*(width+2), 4))
        energy, query = oracle(*args[:7], resized, *args[8:])
        require(np.allclose(energy[:2], np.pi) and np.array_equal(energy[2], np.zeros(3)),
                f"Constant radiance units/coverage at angular width {width}")
    require(np.allclose(query[:, 1], [1, .5, 0]) and query[2, 2] == 4, "Coverage uses an unbiassed world surface")
    classes[:] = 2
    energy, query = oracle(*args)
    require(np.all(energy == 0) and np.array_equal(query[:, 2], [3, 3, 4]), "Inactive cages cannot normalize or return sky")
    print("Field lookup oracle constant, coverage and unsupported-cage checks passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        result = validate(args.root)
        (args.root/"validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print("Field lookup, support, export-order and composition controls passed")
