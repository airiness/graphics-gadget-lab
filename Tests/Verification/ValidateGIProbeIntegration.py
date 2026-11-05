"""Conditional FP64 integration checks using an opt-in GPU arithmetic witness.

Reconstructed directions, observed FP32 directions, observed cosine weights and
Blend are separate comparisons. Historical replay is not an original-binary GPU
rerun, an independent transport reference, or a waiver of its retained failure.
"""
import argparse
import json
import unittest
from pathlib import Path

import numpy as np

from AnalyzeGIFieldTransient import receipt, save, trace_error
from ValidateGIFieldBaseline import parameters
from ValidateGIFieldFeedback import hash32, quaternion, rotate
from ValidateGIFieldLightStep import field_update, load, read_group, require
from ValidateGIProbeUpdate import directions, octahedral

MAGIC, VERSION, HEADER_ROWS, BLOCKS = 0x47494d31, 1, 4, 5


def decode(raw):
    require(raw.dtype == np.dtype('<u4') and raw.ndim == 4 and raw.shape[0] == raw.shape[3] == 1,
            'Integration witness must be a single-layer R32Uint texture')
    width = raw.shape[2]
    require(width % 4 == 0, 'Incomplete bit-preserved records')
    u = raw.reshape(raw.shape[1], width//4, 4)
    require(tuple(u[0, 0, :2]) == (MAGIC, VERSION), 'Unknown integration witness ABI')
    update, probe = map(int, u[0, 0, 2:])
    rays, side, replay, epoch = map(int, u[0, 3])
    require(rays in (64, 128, 192, 256) and side in (6, 14, 30) and width == 4*rays and
            update > epoch and replay > 0, 'Invalid integration witness header')
    texels = side*side
    rows = (texels+rays-1)//rays
    require(u.shape == (HEADER_ROWS+BLOCKS*rows+texels, rays, 4), 'Incomplete witness extent')
    f = u.view('<f4').astype(float)
    require(np.isfinite(f[1:]).all(), 'Non-finite integration witness')
    values = [f[HEADER_ROWS+i*rows:HEADER_ROWS+(i+1)*rows].reshape(-1, 4)[:texels]
              for i in range(BLOCKS)]
    normal, integrated, previous, blended, numerator = values
    weight = f[HEADER_ROWS+BLOCKS*rows:]
    h = float(f[0, 4, 0])
    require(np.isfinite(f[0, 1:3]).all() and (numerator[:, 3] > 0).all() and
            0 <= h < 1 and (weight[..., (1, 3)] >= 0).all() and
            np.array_equal(weight[..., 1], np.maximum(weight[..., 0], 0)) and
            np.array_equal(weight[..., 3], np.maximum(weight[..., 2], 0)), 'Invalid blend or cosine clamp')
    require(np.allclose(np.linalg.norm(normal[:, :3], axis=1), 1, atol=2e-6, rtol=0) and
            np.allclose(np.linalg.norm(f[1, :, :3], axis=1), 1, atol=2e-6, rtol=0) and
            np.allclose(np.linalg.norm(f[3, :, :3], axis=1), 1, atol=2e-6, rtol=0), 'Invalid direction witness')
    require((integrated[:, 3] == 1).all() and (blended[:, 3] == 1).all() and
            (integrated[:, :3] >= 0).all() and (f[2, :, :3] >= 0).all(), 'Invalid integration values')
    return dict(update=update, probe=probe, rays=rays, side=side, replay=replay, epoch=epoch,
                rotation=f[0, 1], replayRotation=f[0, 2], hysteresis=h,
                counts=u[0, 5, :3].astype(int), rotate=bool(u[0, 5, 3]), flags=int(u[0, 6, 0]),
                direction=f[1, :, :3], ray=f[2], replayDirection=f[3, :, :3], normal=normal[:, :3],
                integrated=integrated[:, :3], previous=previous[:, :3], blended=blended[:, :3],
                numerator=numerator[:, :3], denominator=numerator[:, 3],
                signedWeight=weight[..., 0], weight=weight[..., 1], replaySignedWeight=weight[..., 2], replayWeight=weight[..., 3])


def integrate(weight, rays):
    return (weight @ rays[:, :3])/np.maximum(weight.sum(axis=1)[:, None], 1e-20)


def reconstructed(w, seed, update, rotation=None):
    base = directions(w['rays'])
    index = update-1
    if w['flags'] & 1:
        base = rotate(base, quaternion(hash32(w['probe'] ^ (hash32(index) if w['rotate'] else 0))))
    if w['rotate']:
        base = rotate(base, quaternion(seed ^ index) if rotation is None else rotation)
    return base


def arithmetic(w):
    expected_weight = np.maximum(w['normal'] @ w['direction'].T, 0)
    by_weight = integrate(w['weight'], w['ray'])
    by_direction = integrate(expected_weight, w['ray'])
    # Three multiplies and two additions (or fewer roundings with contraction).
    # This is a forward rounding bound for the observed FP32 dot operands, not
    # a fitted irradiance tolerance or a bound on sin/cos/direction construction.
    operations, unit = 5, 2.**-24
    gamma = operations*unit/(1-operations*unit)
    bound = gamma*(abs(w['normal']) @ abs(w['direction']).T)+operations*np.finfo(np.float32).tiny
    dot_error = abs(w['signedWeight']-w['normal'] @ w['direction'].T)
    checks = dict(observedWeightIntegration=trace_error(w['integrated'], by_weight),
                  observedDirectionIntegration=trace_error(w['integrated'], by_direction),
                  observedBlend=trace_error(w['blended'], w['hysteresis']*w['previous']+
                                           (1-w['hysteresis'])*w['integrated']),
                  numeratorDivision=trace_error(w['integrated'], w['numerator']/w['denominator'][:, None]))
    return dict(checks=checks, dotForwardBoundPassed=bool((dot_error <= bound).all()),
                maximumDotAbsoluteError=float(dot_error.max()),
                maximumDotBoundFraction=float((dot_error/np.maximum(bound, 1e-30)).max()),
                contractsPassed=all(c['arithmeticPassed'] for c in checks.values()) and bool((dot_error <= bound).all()))


def readiness_updates(value, count):
    require(value.dtype.names == ('record',), 'Readiness buffer lost its float4 ABI')
    value = value['record']
    require(value.dtype == np.dtype('<f4') and value.shape == (1+2*count, 4), 'Incomplete readiness buffer')
    return np.ascontiguousarray(value[2::2, 3]).view('<u4')


def targets(path):
    group = read_group(path)
    result, inputs = {}, [receipt(path)]
    for target in group['targets']:
        address = target['address']
        name = address['resource'].removeprefix('GI.Field.')
        if isinstance(address['at'], dict):
            require(name == 'Irradiance' and address['at'] == dict(before='GI.Field.BlendShared'), 'Unexpected pre-blend address')
            name = 'Previous'
        else:
            require(address['at'] == 'final', 'Non-final integration observation')
        require(name not in result and address['mip'] == 0, 'Duplicate/mipped integration target')
        file = path.parent/target['array']
        inputs.append(receipt(file))
        result[name] = np.load(file, allow_pickle=False)
    return group, result, inputs


def compare_reconstruction(w, seed, rays, previous, actual, update, replay=False, hysteresis=None):
    normals = octahedral(w['side']).reshape(-1, 3)
    rotation = w['replayRotation'] if replay else w['rotation']
    observed_direction = w['replayDirection'] if replay else w['direction']
    observed_weight = w['replayWeight'] if replay else w['weight']
    ideal = reconstructed(w, seed, update)
    uploaded_rotation = reconstructed(w, seed, update, rotation)
    h = w['hysteresis'] if hysteresis is None else hysteresis
    comparisons = {}
    # Counterfactual comparisons, not an additive decomposition or a fitted correction.
    for name, weight in (
            ('reconstructed', np.maximum(normals @ ideal.T, 0)),
            ('uploadedCommonRotation', np.maximum(normals @ uploaded_rotation.T, 0)),
            ('observedNormal', np.maximum(w['normal'] @ uploaded_rotation.T, 0)),
            ('observedDirection', np.maximum(w['normal'] @ observed_direction.T, 0)),
            ('observedWeight', observed_weight)):
        comparisons[name] = trace_error(actual, h*previous+(1-h)*integrate(weight, rays))
    return dict(comparisons=comparisons,
                maximumDirectionAbsoluteDifference=float(abs(ideal-observed_direction).max()),
                maximumNormalAbsoluteDifference=float(abs(normals-w['normal']).max()),
                uploadedRotationDifference=(rotation-quaternion(seed ^ (update-1))).tolist())


def validate_case(root, case, historical_point=None, historical_root=None):
    directory = root/case['directory']
    capture = load(directory/'receipt.json')
    require(capture['status'] == 'completed' and capture['publication'] == 'Published', 'Witness was not published')
    paths = list(directory.glob('*.resource-export.json'))
    require(len(paths) == 1, 'One same-frame integration export is required')
    group, arrays, inputs = targets(paths[0])
    inputs.append(receipt(directory/'receipt.json'))
    require(capture['frameSerial'] == group['frame']['frame']['serial'], 'Publication/frame mismatch')
    require(set(arrays) == {'Previous', 'Irradiance', 'Rays', 'Readiness', 'ProbeInspection', 'IntegrationInspection'},
            'Incomplete paired integration witness')
    w = decode(arrays['IntegrationInspection'])
    frame = group['frame']
    field = frame['readiness']['giField']
    summary = field['summary']
    probe = frame['giProbeInspection']
    coordinate = probe['request']['coordinate']
    require(probe['schemaVersion'] == 2 and probe['validity'] == 'Valid' and
            probe['request']['integrationReplayUpdate'] == case['replayUpdate'] and
            probe['observation']['field'] == summary['field'] and
            probe['observation']['fieldUpdate'] == w['update'] and
            probe['observation']['producingFrame'] == frame['frame']['serial'] and
            w['epoch'] == summary['field']['epochOriginUpdate'] and
            w['replay'] == (case['replayUpdate'] or w['update']), 'Stale/mixed integration witness')
    require(frame['temporal']['renderExtent'] == frame['temporal']['displayExtent'] == [320, 200] and
            frame['temporal']['status'] == 'disabled', 'Integration recipe requires Native/TAA-off')
    require(np.array_equal(w['counts'], summary['counts']) and w['side'] == summary['irradianceInteriorWidth'] and
            w['probe'] == coordinate[0]+w['counts'][0]*(coordinate[2]+w['counts'][2]*coordinate[1]) and
            parameters(frame)['seed'] == case['seed'], 'Different sampling recipe/probe')
    updates = readiness_updates(arrays['Readiness'], int(w['counts'].prod()))
    require(field_update(field, updates) == w['update'], 'GPU update witnesses disagree')
    all_rays = np.ascontiguousarray(arrays['Rays']).view('<f4').reshape(int(w['counts'].prod()), w['rays'], 4)
    require(np.array_equal(w['ray'], all_rays[w['probe']]), 'Integration witness did not consume paired Trace output')
    side = w['side']+2
    x, _, z = coordinate
    region = (0, slice(z*side+1, z*side+1+w['side']), slice(x*side+1, x*side+1+w['side']))
    for name, value in (('Previous', w['previous']), ('Irradiance', w['blended'])):
        require(np.array_equal(value, arrays[name][region][..., :3].reshape(-1, 3)), 'Atlas/witness versions disagree')
    result = dict(case=case, update=w['update'], replayUpdate=w['replay'], **arithmetic(w),
                  live=compare_reconstruction(w, case['seed'], w['ray'], w['previous'], w['blended'], w['update']),
                  currentReplayDirectionBitwiseEqual=None if w['replay'] != w['update'] else
                      bool(np.array_equal(w['direction'], w['replayDirection'])))
    if historical_point and case['replayUpdate']:
        point = load(historical_point)
        require(w['probe'] in point['selected'] and point['case']['traceSeed'] == case['seed'] and
                point['update'] == w['replay'] and not point['case']['feedback'], 'Historical replay recipe differs')
        old_paths = list((historical_root/point['case']['directory']/'samples').glob(f"light-step-f{point['frame']:04d}.*.resource-export.json"))
        require(len(old_paths) == 1, 'Missing/ambiguous retained historical frame')
        old_group, old, old_inputs = targets(old_paths[0])
        inputs += [receipt(historical_point)]+old_inputs
        old_field = old_group['frame']['readiness']['giField']
        require(old_field['refreshing'] and old_field['hysteresis'] == .5 and
                parameters(old_group['frame'])['seed'] == case['seed'],
                'Unproven historical refresh coefficient or sampling seed')
        old_updates = readiness_updates(old['Readiness'], int(w['counts'].prod()))
        require(field_update(old_field, old_updates) == w['replay'] and
                old_field['summary']['counts'] == summary['counts'] and
                old_field['summary']['irradianceInteriorWidth'] == w['side'], 'Historical field layout/update differs')
        y = coordinate[1]-point['case']['atlasSlices'][0]
        old_region = (y, region[1], region[2])
        previous, actual = [old[k][old_region][..., :3].reshape(-1, 3).astype(float) for k in ('Previous', 'Irradiance')]
        rays = np.ascontiguousarray(old['Rays']).view('<f4').reshape(-1, w['rays'], 4)[w['probe']]
        historical = compare_reconstruction(w, case['seed'], rays, previous, actual, w['replay'], replay=True, hysteresis=.5)
        worst = historical['comparisons']['reconstructed']['worst']
        texel, channel = worst['texel'], worst['channel']
        normal = octahedral(w['side']).reshape(-1, 3)[texel]
        ideal_direction = reconstructed(w, case['seed'], w['replay'])
        ideal_weight = np.maximum(ideal_direction @ normal, 0)
        contribution = ideal_weight*rays[:, channel]
        ray = int(contribution.argmax())
        observed_expected = .5*(previous+integrate(w['replayWeight'], rays))
        historical['sensitiveSite'] = dict(texel=texel, channel=channel, ray=ray,
            positiveContributors=int((contribution > 0).sum()), rayRadiance=float(rays[ray, channel]),
            reconstructedCosine=float(ideal_weight[ray]), observedReplayCosine=float(w['replayWeight'][texel, ray]),
            observedReplaySignedCosine=float(w['replaySignedWeight'][texel, ray]),
            fp64DotOfObservedReplayInputs=float(w['normal'][texel] @ w['replayDirection'][ray]),
            reconstructedNormal=normal.tolist(), observedNormal=w['normal'][texel].tolist(),
            reconstructedDirection=ideal_direction[ray].tolist(), observedReplayDirection=w['replayDirection'][ray].tolist(),
            originalStored=float(actual[texel, channel]), originalReconstructedExpected=worst['expected'],
            replayWeightExpected=float(observed_expected[texel, channel]))
        result['historicalReplay'] = dict(originalBinaryRerun=False, originalBackend=point['case']['backend'],
            frame=point['frame'], update=point['update'], **historical)
    return result, inputs


def validate(root, output, point=None, historical_root=None):
    require(not output.exists(), 'Output must be fresh; never overwrite retained analysis')
    output.mkdir(parents=True)
    results, inputs = [], [receipt(root/'integration-cases.json')]
    for case in load(root/'integration-cases.json'):
        result, used = validate_case(root, case, point, historical_root)
        results.append(result)
        inputs += used
    require(results, 'No integration witnesses')
    require(all(receipt(r['path']) == r for r in inputs), 'Integration input changed during analysis')
    save(output/'input-receipts.json', inputs)
    result = dict(contractsPassed=all(r['contractsPassed'] for r in results), cases=results, numpyVersion=np.__version__,
                  independentReferenceQualified=False, responseQualityQualified=False,
                  originalFailuresPreserved=True, analysisOnly=True)
    save(output/'result.json', result)
    return result


class Tests(unittest.TestCase):
    @staticmethod
    def witness():
        rays, side = 64, 6
        rows = HEADER_ROWS+BLOCKS+side*side
        raw = np.zeros((1, rows, 4*rays, 1), '<u4')
        u = raw.reshape(rows, rays, 4)
        f = u.view('<f4')
        u[0, 0] = [MAGIC, VERSION, 5, 0]
        u[0, 3] = [rays, side, 5, 0]
        f[0, 4, 0] = .5
        normal = octahedral(side).reshape(-1, 3).astype('<f4')
        direction = directions(rays).astype('<f4')
        f[1, :, :3] = f[3, :, :3] = direction
        f[2] = 1
        f[4, :side*side, :3] = normal
        weights = np.maximum(normal @ direction.T, 0)
        f[HEADER_ROWS+BLOCKS:, :, 0] = normal @ direction.T
        f[HEADER_ROWS+BLOCKS:, :, 1] = weights
        f[HEADER_ROWS+BLOCKS:, :, 2] = normal @ direction.T
        f[HEADER_ROWS+BLOCKS:, :, 3] = weights
        f[5, :side*side] = 1
        f[7, :side*side, :3] = .5
        f[7, :side*side, 3] = 1
        f[8, :side*side] = weights.sum(axis=1)[:, None]
        return raw

    def test_decode_and_conditional_arithmetic(self):
        self.assertTrue(arithmetic(decode(self.witness()))['contractsPassed'])

    def test_fabricated_raw_integral_fails(self):
        w = decode(self.witness())
        w['integrated'] *= 1.01
        self.assertFalse(arithmetic(w)['contractsPassed'])

    def test_changed_extent_rejects(self):
        with self.assertRaises(ValueError):
            decode(self.witness()[:, :-1])

    def test_changed_clamp_rejects(self):
        raw = self.witness()
        raw.reshape(raw.shape[1], 64, 4)[9, 0, 1] = np.float32(-1).view('<u4')
        with self.assertRaises(ValueError):
            decode(raw)

    def test_versioned_value_schema(self):
        import jsonschema
        schema = load(Path(__file__).resolve().parents[2]/'Schemas/GIProbeInspection.schema.json')
        field = dict(generation=1, sceneRevision=0, lightingGeneration=0, environmentGeneration=0,
                     refreshEpoch=1, epochOriginUpdate=0)
        value = dict(schemaVersion=1, request=dict(coordinate=[0, 0, 0], expectedGeneration=1, expectedRefreshEpoch=None),
                     observation=dict(field=field, kind='probe-atlas', producingFrame=1, fieldUpdate=1, readinessUpdate=1),
                     worldPosition=[0, 0, 0], positionUnits='world-units', classification=0, validity='MalformedPayload',
                     tileLayout='row-major-bordered-octahedral', irradianceUnits='scene-referred-irradiance-E',
                     distanceUnits=['world-units', 'world-units-squared'], irradianceTileWidth=0, distanceTileWidth=16,
                     irradiance=[], distance=[])
        jsonschema.validate(value, schema)
        value['request']['integrationReplayUpdate'] = 563
        with self.assertRaises(jsonschema.ValidationError):
            jsonschema.validate(value, schema)
        value['schemaVersion'] = 2
        jsonschema.validate(value, schema)
        del value['request']['integrationReplayUpdate']
        with self.assertRaises(jsonschema.ValidationError):
            jsonschema.validate(value, schema)

    def test_constant_integration(self):
        rays = np.ones((256, 4))
        self.assertTrue(np.array_equal(integrate(np.ones((6, 256)), rays), np.ones((6, 3))))

    def test_near_black_failure_is_retained(self):
        result = trace_error(np.array([[.00021792040206491947]]), np.array([[.00021046580890512754]]))
        self.assertFalse(result['arithmeticPassed'])
        self.assertEqual(result['budget'], .005)
        self.assertEqual(result['normalizationFloor'], .001)

    def test_grazing_input_conditioning_is_not_a_blend_failure(self):
        ideal = np.full((1, 256), (63.92876681745136-1.6278664627957957e-5)/255)
        ideal[0, 77] = 1.6278664627957957e-5
        observed = ideal.copy()
        observed[0, 77] += 5.832163182448999e-7
        rays = np.zeros((256, 4))
        rays[77, 0] = 1634.2579345703125
        previous = np.array([[4.788179467141163e-6, 0, 0]])
        actual = (.5*(previous+integrate(observed, rays))).astype(np.float32)
        self.assertFalse(trace_error(actual, .5*(previous+integrate(ideal, rays)))['arithmeticPassed'])
        self.assertTrue(trace_error(actual, .5*(previous+integrate(observed, rays)))['arithmeticPassed'])

    def test_empty_weight(self):
        self.assertTrue(np.array_equal(integrate(np.zeros((2, 64)), np.ones((64, 4))), np.zeros((2, 3))))

    def test_wrong_abi_and_type(self):
        for array in (np.zeros((1, 1, 256, 1), '<u4'), np.zeros((1, 1, 256, 1), '<f4')):
            with self.assertRaises(ValueError):
                decode(array)

    def test_observed_weights_do_not_hide_bad_blend(self):
        self.assertFalse(trace_error(np.ones((6, 3))*1.01, np.ones((6, 3)))['arithmeticPassed'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--historical-point', type=Path)
    parser.add_argument('--historical-root', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests)).wasSuccessful() else 1
    require(args.root is not None and bool(args.historical_point) == bool(args.historical_root), 'Root and paired historical arguments required')
    result = validate(args.root, args.output or args.root/'integration-analysis', args.historical_point, args.historical_root)
    print(json.dumps(dict(contractsPassed=result['contractsPassed'], cases=len(result['cases']))))
    return 0 if result['contractsPassed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
