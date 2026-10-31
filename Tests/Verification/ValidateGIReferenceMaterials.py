"""Independent float64 reduced-material sampling and finite-order transport oracle.

Fixtures are generated from authored RGBA bytes, never from GPU material tables,
shader RNG, or raster albedo. Material and environment filtering are separate.
"""
import argparse
from functools import lru_cache
import hashlib
import json
import re
from pathlib import Path
import struct

import numpy as np

from ValidateGIReferenceLighting import require, relative_error, wall_form_factor
from ValidateGIReferenceTransport import rectangle, transfer

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'Build/Fixtures/GIMaterials'
FACTOR = np.array([.6, .8, .4])
# The direct comparison also checks a per-pixel analytic conversion/filter
# interval. D3D's 0.5 encoded-sRGB ULP permits deterministic ROI bias; an
# arbitrary near-zero signed-mean requirement would incorrectly reject it.
DIRECT_ABSOLUTE = 2e-3
FILTER_COORDINATE_ERROR = .6/256
# Fixed-format filtering has its own arithmetic precision, separate from the
# coordinate and sRGB conversions. Qualify nearest rounding to one UNORM8 step.
FILTER_ARITHMETIC_ERROR = .5/255
POINT_BOUNDARY = FILTER_COORDINATE_ERROR+1e-5
TRANSPORT_RELATIVE = .01
QUADRATURE_RELATIVE = .002
FILES = ('pattern.dds', 'metal.dds', 'mip64.dds', 'mip65.dds',
         'mip129.dds', 'short256.dds', 'replacement.dds', 'normal.dds')


def authored_image(index, width, height, mip):
    y, x = np.indices((height, width))
    image = np.full((height, width, 4), 255, dtype=np.uint8)
    if index == 0:
        image[..., :3] = np.stack([32 + 48*x, 48 + 40*y, 208 - 24*x - 16*y], -1)
    elif index == 1:
        image[..., :3] = np.stack([11 + x*0, 15 + x*60 + y*3, 48 + y*32 + x*16], -1)
    elif index == 6:
        image[..., :3] = [64, 192, 96]
    elif index == 7:
        image[..., :3] = [220, 128, 220]
    else:
        image[..., :3] = [32 + 20*mip, 90 + 10*mip, 180 - 15*mip]
    return image


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    for index, name in enumerate(FILES):
        w, h, count = ((64, 32, 7) if index == 2 else (65, 32, 7) if index == 3 else
                       (129, 33, 8) if index == 4 else (256, 32, 2) if index == 5 else (4, 4, 1))
        srgb = index not in (1, 7)
        header = [124, 0x2100f, h, w, w*4, 0, count] + [0]*11
        header += [32, 4, 0x30315844, 0, 0, 0, 0, 0] + [0x401008, 0, 0, 0, 0]
        require(len(header) == 31, 'DDS header size')
        data = b'DDS ' + struct.pack('<31I', *header) + struct.pack('<5I', 29 if srgb else 28, 3, 0, 1, 3)
        for level in range(count):
            data += authored_image(index, max(w >> level, 1), max(h >> level, 1), level).tobytes()
        path = directory / name
        # Shared deterministic inputs may be reused by sequential backend runs.
        if path.exists():
            require(path.read_bytes() == data, f'Refuse to replace unrelated fixture {path}')
        else:
            path.write_bytes(data)
        records.append(dict(file=name, sha256=hashlib.sha256(data).hexdigest(), width=w,
                            height=h, authoredLevels=count, srgb=srgb))
    manifest = dict(schemaVersion=1, generator='ValidateGIReferenceMaterials.py',
                    rights='Original synthetic fixture data authored for gglab.', textures=records)
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return manifest


def read_dds(index, directory=None):
    directory = FIXTURES if directory is None else directory
    data = (directory / FILES[index]).read_bytes()
    require(data[:4] == b'DDS ' and data[84:88] == b'DX10', 'Expected authored DX10 DDS')
    h, w, count = struct.unpack_from('<II', data, 12) + (struct.unpack_from('<I', data, 28)[0],)
    fmt = struct.unpack_from('<I', data, 128)[0]
    levels, offset = [], 148
    for level in range(count):
        lw, lh = max(w >> level, 1), max(h >> level, 1)
        size = lw*lh*4
        levels.append(np.frombuffer(data[offset:offset+size], dtype=np.uint8).reshape(lh, lw, 4).copy())
        offset += size
    require(offset == len(data) and fmt in (28, 29), 'DDS extent/format mismatch')
    return levels, fmt == 29


def decode_srgb(value):
    return np.where(value <= .04045, value/12.92, ((value+.055)/1.055)**2.4)


def texture_sample(index, uv, point=False, wrap=False, srgb_offset=0.):
    levels, srgb = read_dds(index)
    # Select by the dimensions present in the authored mip chain, including an
    # intentionally incomplete chain whose last level remains larger than 64.
    selected = next((i for i, image in enumerate(levels) if max(image.shape[:2]) <= 64), len(levels)-1)
    image = levels[selected][..., :3].astype(np.float64)/255
    if srgb:
        converted = decode_srgb(np.clip(image+srgb_offset/255, 0, 1))
        image = np.where((image == 0) | (image == 1), image, converted)
    h, w = image.shape[:2]
    uv = np.asarray(uv)
    def load(x, y):
        if wrap:
            x, y = x % w, y % h
        else:
            x, y = np.clip(x, 0, w-1), np.clip(y, 0, h-1)
        return image[y, x]
    if point:
        return load(np.floor(uv[..., 0]*w).astype(int), np.floor(uv[..., 1]*h).astype(int))
    q = uv*np.array([w, h])-.5
    lower = np.floor(q).astype(int)
    fraction = q-lower
    x, y = lower[..., 0], lower[..., 1]
    a, b = fraction[..., 0, None], fraction[..., 1, None]
    return (1-b)*((1-a)*load(x, y)+a*load(x+1, y)) + b*((1-a)*load(x, y+1)+a*load(x+1, y+1))


def material_albedo(case, uv):
    uv = np.asarray(uv, dtype=np.float64)
    factor = np.ones(3) if case == 0 else FACTOR
    if not (2 <= case <= 12 or case in (19, 21)):
        return np.broadcast_to(factor, uv.shape[:-1]+(3,)).copy()
    sample_uv = uv.copy()
    if case in (4, 19):
        sample_uv = np.stack([.2+.5*uv[..., 1], .1+(.3 if case == 19 else .7)*uv[..., 0]], -1)
    if case in (5, 6, 7):
        scaled = uv*np.array([2., 1.5])
        c, s = np.cos(.5), np.sin(.5)
        sample_uv = scaled @ np.array([[c, s], [-s, c]]) + [-.25, .15]
    index = case-6 if 8 <= case <= 11 else 6 if case == 21 else 0
    base = factor*texture_sample(index, sample_uv, point=case == 6, wrap=case == 7)
    if case == 3:
        metallic = .5*texture_sample(1, uv, point=True)[..., 2]
        base *= 1-metallic[..., None]
    return base


def material_bounds(case, uv):
    expected = material_albedo(case, uv)
    if not (2 <= case <= 12 or case in (19, 21)):
        return expected-2e-6, expected+2e-6
    sample_uv = np.asarray(uv, dtype=np.float64).copy()
    if case in (4, 19):
        sample_uv = np.stack([.2+.5*uv[..., 1], .1+(.3 if case == 19 else .7)*uv[..., 0]], -1)
    if case in (5, 6, 7):
        c, t = np.cos(.5), np.sin(.5)
        sample_uv = (uv*[2., 1.5]) @ np.array([[c, t], [-t, c]]) + [-.25, .15]
    index = case-6 if 8 <= case <= 11 else 6 if case == 21 else 0
    options = dict(point=case == 6, wrap=case == 7)
    lo = texture_sample(index, sample_uv, srgb_offset=-.5, **options)
    hi = texture_sample(index, sample_uv, srgb_offset=.5, **options)
    if case != 6:
        levels, srgb = read_dds(index)
        image = next((im for im in levels if max(im.shape[:2]) <= 64), levels[-1])[..., :3]/255
        if srgb:
            image = decode_srgb(np.clip(image+.5/255, 0, 1))
        du = np.diff(image, axis=1) if case != 7 else np.roll(image, 1, axis=1)-image
        dv = np.diff(image, axis=0) if case != 7 else np.roll(image, 1, axis=0)-image
        slope = np.max(abs(du), axis=(0, 1)) + np.max(abs(dv), axis=(0, 1))
        lo -= FILTER_COORDINATE_ERROR*slope+FILTER_ARITHMETIC_ERROR
        hi += FILTER_COORDINATE_ERROR*slope+FILTER_ARITHMETIC_ERROR
    if case == 3:
        reflected = 1-.5*texture_sample(1, uv, point=True)[..., 2, None]
        lo *= reflected
        hi *= reflected
    return FACTOR*lo-2e-6, FACTOR*hi+2e-6


@lru_cache(maxsize=8)
def wall_quadrature(order):
    # Split at texel centres (linear reconstruction kinks) and cell boundaries
    # (point metallic sampling). This avoids mistaking CPU discretization for noise.
    cuts = np.array([0, .125, .25, .375, .5, .625, .75, .875, 1.])
    points, weights = [], []
    for u0, u1 in zip(cuts[:-1], cuts[1:]):
        for v0, v1 in zip(cuts[:-1], cuts[1:]):
            p, w = rectangle([-2+4*u0, 4*v0, 2], [4*(u1-u0), 0, 0], [0, 4*(v1-v0), 0], order)
            points.append(p)
            weights.append(w)
    return np.concatenate(points), np.concatenate(weights)


def transport_oracle(position, case, depth, order=6):
    p = np.asarray(position)
    factor = wall_form_factor(p[:, 0], p[:, 2])
    if case == 17:
        return np.zeros((len(p), 3)), np.full((len(p), 3), np.pi)
    environment = np.repeat((np.pi-factor)[:, None], 3, 1)
    if case == 14:
        return np.zeros_like(environment), environment
    wall, weights = wall_quadrature(order)
    uv = np.stack([(wall[:, 0]+2)/4, 1-wall[:, 1]/4], -1)
    if case == 13:
        uv[:, 0] = 1-uv[:, 0]
    rho = material_albedo(case, uv)
    sun_direct = 10000/np.sqrt(2)
    field = rho*sun_direct
    if depth == 2:
        floor_factor = wall_form_factor(wall[:, 0], -wall[:, 1], plane_z=0)
        field = np.column_stack([field, rho*(floor_factor*.7*sun_direct/np.pi)[:, None],
                                 rho*(np.pi-floor_factor)[:, None]])
    value = transfer(p, [0, 1, 0], wall, [0, 0, -1], weights, field)
    sun = value[:, :3]
    if depth == 2:
        sun = sun+value[:, 3:6]
        environment = environment+value[:, 6:9]
    return sun, environment


def read_capture(directory):
    files = list(directory.glob('*.resource-export.json'))
    require(len(files) == 1, f'One resource export required: {directory}')
    record = json.loads(files[0].read_text(encoding='utf-8-sig'))
    frame = record['frame']
    require(any(g['name'] == 'gi-reference' and g['state'] == 'ready' for g in frame['readiness']['gates']),
            'Observation must be GPU complete')
    arrays = {}
    for target in record['targets']:
        value = np.load(directory/target['array'], allow_pickle=False)
        require(value.dtype == np.dtype('<f4') and value.shape[0] == 1 and value.shape[-1] == 4 and
                np.isfinite(value).all(), 'Invalid/non-finite material capture')
        arrays[target['address']['resource']] = value[0]
    return frame, arrays


def observation_id(frame):
    detail = next(g['detail'] for g in frame['readiness']['gates'] if g['name'] == 'gi-reference')
    return int(re.search(r'Observation (\d+)', detail)[1])


def log_records(path, marker):
    result = []
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        if marker in line:
            text = line.split(marker, 1)[1].strip().rstrip('.')
            record = {}
            for pair in text.split(', '):
                key, value = pair.split('=', 1)
                record[key] = int(value) if value.isdigit() else value
            result.append(record)
    return result


def audit_snapshots(root, cases):
    result = []
    fixtures = json.loads((root.parent/'fixtures.json').read_text(encoding='utf-8-sig'))['textures']
    for backend, group in sorted({(c['rhi'], c['group']) for c in cases}):
        selected = [c for c in cases if c['rhi'] == backend and c['group'] == group]
        directory = root/backend/group
        records = log_records(directory/'output.log', 'GI material snapshot:')
        textures = log_records(directory/'output.log', 'GI material texture:')
        require(len(records) == len(selected), 'Every material replacement must publish exactly one snapshot')
        identity, previous, previous_observation = None, None, 0
        samplers = {}
        for case, snapshot in zip(selected, records):
            k = case['material_case']
            require(snapshot['case'] == k, 'Wrong ordered snapshot case')
            frame, values = read_capture(root/case['directory'])
            content = frame['control']['hostViewControl']
            stamp = (content['runIdentity'], content['contentIdentity'])
            require(identity is None or stamp == identity, 'Material replacement restarted World/session')
            identity = stamp
            current_observation = observation_id(frame)
            require(current_observation > previous_observation, 'Changed material did not restart its observation')
            previous_observation = current_observation
            if previous is not None:
                old = previous['case']
                changed_geometry = k == 19 or old == 19 or k == 17 or old == 17
                changed_transform = k in (13, 14, 15) and old not in (13, 14) or \
                                    old in (13, 14, 15) and k not in (13, 14) or k == 16
                if changed_geometry:
                    require(snapshot['geometry'] > previous['geometry'] and snapshot['blas'] > previous['blas'],
                            'Changed eligible mesh set did not replace geometry')
                else:
                    require(snapshot['geometry'] == previous['geometry'] and snapshot['blas'] == previous['blas'],
                            'Material/transform changes rebuilt unchanged geometry')
                if changed_geometry or changed_transform:
                    require(snapshot['tlas'] > previous['tlas'], 'Transform/entity/mesh replacement reused stale TLAS')
                else:
                    require(snapshot['tlas'] == previous['tlas'], 'Material-only replacement rebuilt unchanged TLAS')
                if k == 16:
                    require(snapshot['entity'] != previous['entity'], 'Recycled entity lost its generation')
            require(snapshot['blend'] == (1 if k == 17 else 0), 'Wrong opaque/BLEND membership')
            bound = [t for t in textures if t['revision'] == snapshot['revision']]
            for texture in bound:
                slot = texture['slot']
                index = 1 if slot == 1 else k-6 if 8 <= k <= 11 else 6 if k == 21 else 0
                fixture = fixtures[index]
                require(texture['source'] == fixture['sha256'] and len(texture['artifact']) == 64,
                        'Actual sampled texture content is not the authored fixture')
                require(texture['generation'] > 0 and texture['width'] == fixture['width'] and
                        texture['height'] == fixture['height'] and texture['format'] == (3 if fixture['srgb'] else 2),
                        'Actual sampled format/colour space/extent differs')
                expected_mip = {8: 0, 9: 1, 10: 1, 11: 1}.get(k, 0)
                require(texture['mip'] == expected_mip and texture['levels'] >= texture['mip']+1,
                        'Wrong explicit material mip')
            if group == 'wall':
                files = list((root/case['directory']/'tables').glob('*.resource-export.json'))
                require(len(files) == 1, 'Material table provenance missing')
                table_record = json.loads(files[0].read_text(encoding='utf-8-sig'))
                table_frame = table_record['frame']
                require(table_frame['control']['hostViewControl'] == content and
                        observation_id(table_frame) == current_observation, 'Material table inputs differ from image inputs')
                target = table_record['targets'][0]
                table = np.load(files[0].parent/target['array'], allow_pickle=False)
                words = np.frombuffer(table.tobytes(), dtype='<u4').reshape(-1, 132)
                floats = words.view('<f4')
                hit = values['RayScene.HitIdentity']
                mask = values['GI.Reference.Irradiance'][..., 3] == 1
                indices = np.unique(hit[mask, 2]).astype(int)
                require(np.array_equal(indices, [1]) and len(words) == 2, 'Material table slot reuse changed unexpectedly')
                material = words[1]
                require(np.allclose(floats[1, 60:63], np.ones(3) if k == 0 else FACTOR, rtol=0, atol=3e-8) and
                        abs(floats[1, 68]-(.5 if k == 3 else 0)) < 1e-8, 'Actual GPU material factors differ')
                expected_enabled = 2 <= k <= 12 or k in (19, 21)
                require(material[3] == expected_enabled and material[2] == (1 if k in (4, 19) else 0),
                        'Actual GPU binding enablement/UV set differs')
                transform = np.array([[1., 0., 0.], [0., 1., 0.]])
                if k in (5, 6, 7):
                    c, s = np.cos(.5), np.sin(.5)
                    transform = np.array([[c*2, -s*1.5, -.25], [s*2, c*1.5, .15]])
                require(np.allclose(floats[1, [4, 5, 6, 8, 9, 10]].reshape(2, 3), transform, rtol=0, atol=2e-7),
                        'Actual GPU UV transform differs')
                samplers[k] = int(material[1])
                for texture in bound:
                    offset = 0 if texture['slot'] == 0 else 24
                    require(material[offset] == texture['srv'], 'Material table descriptor is not the retained sampled input')
            previous = snapshot
        if all(k in samplers for k in (2, 5, 6, 7)):
            require(samplers[2] == samplers[5] and len({samplers[5], samplers[6], samplers[7]}) == 3,
                    'Linear clamp, point clamp and linear wrap binding identities differ from contract')
        result.append(dict(rhi=backend, group=group, snapshots=len(records), textureInputs=len(textures), textureRecords=textures,
                           sameSession=True, cachePolicy=True, inputDigests=True))
    return result


def audit_transactions(root):
    results = []
    for directory in sorted(root.glob('*/transactions')):
        files = list((directory/'replaced').glob('*.resource-export.json'))
        require(len(files) == 1, 'Partial replacement evidence missing')
        record = json.loads(files[0].read_text(encoding='utf-8-sig'))
        frame = record['frame']
        gate = next(g for g in frame['readiness']['gates'] if g['name'] == 'gi-reference')
        require(gate['state'] == 'pending' and '0/1048576 completed samples, 0 submitted, version 0;' in gate['detail'],
                'Partial replacement published a complete sample')
        cursor = int(re.search(r'tiles (\d+)/64000 submitted', gate['detail'])[1])
        require(0 < cursor < 64000, 'No bounded partial sweep was observed')
        for target in record['targets']:
            array = np.load(files[0].parent/target['array'], allow_pickle=False)[0]
            name = target['address']['resource']
            require(np.isfinite(array).all(), 'Uninitialized/non-finite pending replacement')
            if name == 'GI.Reference.Irradiance':
                require(np.all(array == 0), 'Partial replacement exposed old irradiance')
            elif '.Next' in name:
                require(np.all(array.reshape(64000, -1)[cursor+1:] == 0),
                        f'Unprocessed pending pixels/RNG retained old contents: {name}')
        old = json.loads((directory/'old-status.json').read_text(encoding='utf-8-sig'))
        old_detail = next(g['detail'] for g in old['frame']['gates'] if g['name'] == 'gi-reference')
        old_id = int(re.search(r'Observation (\d+)', old_detail)[1])
        require(observation_id(frame) > old_id, 'Material replacement reused an old observation')
        same = json.loads((directory/'no-change-status.json').read_text(encoding='utf-8-sig'))
        same_detail = next(g['detail'] for g in same['frame']['gates'] if g['name'] == 'gi-reference')
        require(int(re.search(r'Observation (\d+)', same_detail)[1]) == observation_id(frame), 'NoChange restarted observation')
        cancellation = json.loads((directory/'cancellation.json').read_text(encoding='utf-8-sig'))
        require(cancellation['receipt']['status'] == 'cancelled', 'Waiting capture cancellation failed')
        results.append(dict(rhi=directory.parent.name, oldObservation=old_id,
                            newObservation=observation_id(frame), tiles=cursor, unprocessedZero=True))
    return results


def validate(root):
    cases = json.loads((root/'cases.json').read_text(encoding='utf-8-sig'))
    results, retained = [], {}
    for case in cases:
        directory = root/case['directory']
        frame, values = read_capture(directory)
        actual = dict(fixture=0, material_case=0, depth=1, samples=64, seed=61051)
        actual.update({v['id'].split('.', 1)[1]: v['value'] for v in frame['parameters']['values']
                       if v['id'].startswith('gi_reference.')})
        require(actual['fixture'] == 9 and actual['material_case'] == case['material_case'] and
                actual['depth'] == case['depth'] and actual['samples'] == case['samples'] and
                actual['seed'] == 61051, 'Wrong frozen material inputs')
        e = values['GI.Reference.Irradiance']
        require(np.isin(e[..., 3], [0, 1]).all() and
                np.all(e[..., 3][values['GI.Receiver.HitAlbedo'][..., 3] == 1] == 1),
                'Invalid or unmatched receivers cannot be removed from material masks')
        require(np.all(values['GI.Reference.PathDiagnostics'][..., 0] == 0) and
                np.all(values['GI.Reference.Diagnostics'][..., 3] == 0),
                'An invalid material path at any bounce must fail the whole observation')
        position = values['GI.Reference.Position'][..., :3]
        k, depth = case['material_case'], case['depth']
        if case['view'] == 'Contract_MaterialWall':
            mask = (e[..., 3] == 1) & (abs(position[..., 2]-(3 if k == 15 else 2)) < 1e-4) & \
                   (abs(position[..., 0]) < 1.5) & (position[..., 1] > .5) & (position[..., 1] < 3.5)
            require(mask.sum() > 100, 'Missing material wall receivers')
            uv = np.stack([(position[..., 0][mask]+2)/4, 1-position[..., 1][mask]/4], -1)
            if k == 13:
                uv[:, 0] = 1-uv[:, 0]
            if k in (3, 6):
                point_uv = uv
                if k == 6:
                    c, t = np.cos(.5), np.sin(.5)
                    point_uv = (uv*[2., 1.5]) @ np.array([[c, t], [-t, c]]) + [-.25, .15]
                texels = point_uv*4
                keep = np.all(abs(texels-np.round(texels)) > POINT_BOUNDARY, axis=-1)
                rows, columns = np.nonzero(mask)
                mask[rows[~keep], columns[~keep]] = False
                uv = uv[keep]
            expected = material_albedo(k, uv)
            lo, hi = material_bounds(k, uv)
            for name in ('RayScene.HitAlbedo', 'GI.Receiver.HitAlbedo'):
                difference = values[name][mask, :3]-expected
                error = float(abs(difference).max())
                bias = float(abs(difference.mean(0)).max())
                actual_sample = values[name][mask, :3]
                outside = np.maximum(lo-actual_sample, actual_sample-hi).max()
                require(outside <= 0, f'{case}: {name} outside independent sRGB/filter interval by {outside}')
                require(error < DIRECT_ABSOLUTE, f'{case}: {name} CPU error {error}')
                results.append(dict(case=case['directory'], check=name, maximumAbsolute=error, maximumMeanAbsolute=bias, intervalMaximumExcess=float(outside),
                                    pixels=int(mask.sum())))
            if k == 13:
                require((values['RayScene.HitNormal'][mask, 3] == -1).all() and
                        (values['RayScene.HitNormal'][mask, 2] < -.999).all(), 'Double-sided normal flip')
        else:
            mask = (e[..., 3] == 1) & (abs(position[..., 1]) < 1e-5) & \
                   (abs(position[..., 0]) < .8) & (position[..., 2] > -.7) & (position[..., 2] < .4)
            require(mask.sum() > 100, 'Missing neutral floor ROI')
            require((values['GI.Reference.PathDiagnostics'][mask, 0] == 0).all(), 'Invalid material path')
            p = position[mask].astype(float)
            # Integrate the exact same pixel locations; no image-space downsampling.
            fine = transport_oracle(p, k, depth, order=6)
            coarse = transport_oracle(p, k, depth, order=4)
            component_means = []
            for name, expected, low in zip(('SunIrradiance', 'EnvironmentIrradiance'), fine, coarse):
                expected, low = expected.mean(0), low.mean(0)
                quad = relative_error(low, expected)
                require(quad < QUADRATURE_RELATIVE, f'CPU quadrature not resolved: {quad}')
                actual_mean = values['GI.Reference.'+name][mask, :3].mean(0, dtype=float)
                error = relative_error(actual_mean, expected)
                require(error < TRANSPORT_RELATIVE, f'{case}: {name} transport error {error}')
                results.append(dict(case=case['directory'], check=name, relative=error,
                                    cpuQuadratureRelative=quad, expected=expected.tolist(),
                                    actual=actual_mean.tolist(), pixels=int(mask.sum())))
                component_means.append(actual_mean)
            require(np.allclose(e[mask, :3], (values['GI.Reference.SunIrradiance']+
                                            values['GI.Reference.EnvironmentIrradiance'])[mask, :3],
                                rtol=1e-6, atol=1e-5), 'Component sum differs')
            retained[(case['rhi'], depth, k)] = (values, mask, component_means, [v.mean(0) for v in fine])
    for (backend, depth, k), (values, mask, means, expected_high) in retained.items():
        if k == 12:
            baseline = retained[(backend, depth, 2)][0]
            for name in ('Irradiance', 'SunIrradiance', 'EnvironmentIrradiance'):
                require(np.array_equal(values['GI.Reference.'+name][mask], baseline['GI.Reference.'+name][mask]),
                        'Ignored secondary maps/emission changed irradiance')
        if depth == 2 and (backend, 1, k) in retained:
            lower = retained[(backend, 1, k)][2]
            expected_low = retained[(backend, 1, k)][3]
            for component, high, low, actual_high, actual_low in zip(('Sun', 'Environment'),
                    expected_high, expected_low, means, lower):
                increment = high-low
                error = relative_error(actual_high-actual_low, increment)
                require(error < TRANSPORT_RELATIVE, f'{backend} case {k} {component} D2 increment: {error}')
                results.append(dict(case=f'{backend}/increment-{k}', check=component, relative=error))
    cross_rhi = []
    for (backend, depth, k), (values, mask, means, expected) in retained.items():
        peer = retained.get(('vulkan', depth, k))
        if backend != 'dx12' or peer is None:
            continue
        require(np.array_equal(mask, peer[1]), 'Cross-RHI material ROI masks differ')
        for name, actual, reference in zip(('Sun', 'Environment'), means, peer[2]):
            error = relative_error(actual, reference)
            require(error < TRANSPORT_RELATIVE, 'Cross-RHI material transport differs')
            cross_rhi.append(dict(depth=depth, materialCase=k, component=name, relative=error))
    snapshots = audit_snapshots(root, cases)
    transactions = audit_transactions(root)
    result = dict(ok=True, observations=len(cases), checks=results, snapshots=snapshots, transactions=transactions, crossRhi=cross_rhi,
                  scope='Reduced hit materials and D1/D2 transport; existing reference convergence remains separate')
    (root/'materials-validation.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as temporary:
        manifest = generate(Path(temporary))
        require(len(manifest['textures']) == 8, 'Fixture manifest')
        for i in range(8):
            levels, srgb = read_dds(i, Path(temporary))
            require(np.array_equal(levels[0], authored_image(i, levels[0].shape[1], levels[0].shape[0], 0)), 'DDS round trip')
            require(srgb == (i not in (1, 7)), 'Semantic colour encoding')
    require(abs(float(decode_srgb(np.array([128/255]))[0])-.21586050011389926) < 1e-15, 'sRGB value')
    global FIXTURES
    previous = FIXTURES
    try:
        with tempfile.TemporaryDirectory() as temporary:
            FIXTURES = Path(temporary)
            generate(FIXTURES)
            first = decode_srgb(np.array([32, 48, 208])/255)
            second = decode_srgb(np.array([80, 48, 184])/255)
            require(np.allclose(texture_sample(0, [.125, .125]), first, atol=1e-15), 'Exact texel centre')
            require(np.allclose(texture_sample(0, [.25, .125]), (first+second)/2, atol=1e-15),
                    'sRGB decode precedes linear interpolation')
            expected = FACTOR*first*(1-.5*48/255)
            require(np.allclose(material_albedo(3, [.125, .125]), expected, atol=1e-15), 'Metallic blue channel')
            for case, mip in ((8, 0), (9, 1), (10, 1), (11, 1)):
                encoded = np.array([32+20*mip, 90+10*mip, 180-15*mip])/255
                require(np.allclose(material_albedo(case, [.5, .5]), FACTOR*decode_srgb(encoded), atol=1e-15),
                        'Explicit mip boundary and incomplete-chain policy')
            lo, hi = material_bounds(2, np.array([[.125, .125]]))
            ideal = material_albedo(2, np.array([[.125, .125]]))
            require(np.all((ideal >= lo) & (ideal <= hi)), 'Ideal sample lies within the analytic interval')
            require(np.any(.8*ideal < lo), 'The interval rejects a duplicated material factor')
            p = np.array([[0., 0., 0.], [.4, 0., -.5]])
            sun, sky = transport_oracle(p, 1, 1)
            expected_sun = wall_form_factor(p[:, 0], p[:, 2])[:, None]*FACTOR*10000/np.sqrt(2)/np.pi
            require(np.allclose(sun, expected_sun, rtol=1e-10), 'Area quadrature agrees with analytic rectangle')
            high, low = transport_oracle(p, 2, 2, 6), transport_oracle(p, 2, 2, 4)
            require(all(relative_error(a.mean(0), b.mean(0)) < QUADRATURE_RELATIVE for a, b in zip(low, high)),
                    'Independent varying-material quadrature refinement')
    finally:
        FIXTURES = previous
    return dict(ok=True, tests=22)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', type=Path)
    parser.add_argument('--generate', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        result = self_test()
    elif args.generate:
        result = dict(ok=True, manifest=generate(FIXTURES))
    else:
        require(args.directory is not None, 'A run directory is required')
        result = validate(args.directory)
    print(json.dumps(result, indent=2))
