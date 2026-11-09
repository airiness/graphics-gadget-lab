"""Picking result/provenance seal shared by capture-bundle audits."""
import math


def require(condition, message):
    if not condition:
        raise ValueError(message)


def observation(result):
    return {key: value for key, value in result.items()
            if key not in ('validity', 'probeId', 'coordinate', 'depth')}


def validate(frame):
    result = frame.get('giProbePicking')
    if result is None:
        return
    require(result['schemaVersion'] == 1 and result['token'] > 0, 'Unsupported picking result')
    expected = observation(result)
    actual = dict(frame['giProbePickingObservation'])
    require(actual.pop('executionDpiScale') == result['mapping']['dpiScale'] and actual == expected,
            'Picking result differs from the frozen product observation')
    summary = frame['readiness']['giField']['summary']
    stamp, view = result['observation'], result['executionView']
    require(stamp['kind'] == 'probe-picking' and stamp['field'] == summary['field'] and
            stamp['producingFrame'] == frame['frame']['serial'] > result['sourceFrame'] and
            stamp['fieldUpdate'] == summary['submittedUpdates'] + 1 and
            stamp['readinessUpdate'] == stamp['fieldUpdate'] - stamp['field']['epochOriginUpdate'],
            'Picking execution stamp differs from the producing field/frame')
    require(view == summary['probeVisualization']['view'] and
            result['settingsRevision'] == summary['probeVisualization']['settingsRevision'],
            'Picking execution view or settings revision differs')
    for key in ('generation', 'sceneRevision'):
        require(result['sourceField'][key] == stamp['field'][key], 'Picking source field changed')
    ignored = {'projection', 'viewProjection', 'jitterPixels', 'temporalFrameIndex', 'jitterPhase'}
    require({k: v for k, v in view.items() if k not in ignored} ==
            {k: v for k, v in result['sourceView'].items() if k not in ignored}, 'Picking logical view changed')
    settings = dict(summary['probeVisualization']['settings'], selection=None)
    require(result['settings'] == settings and result['attachmentExtent'] == [1, 1] and
            result['attachmentLogicalBytes'] == 8 and result['maximumSubmittedVertices'] == settings['budget'] * 768,
            'Picking configuration or bounded workload differs')
    require(all(type(p) is int and 0 <= p < extent for p, extent in zip(result['pixel'], view['displayExtent'])),
            'Picking pixel is outside the source extent')
    validity = result['validity']
    require(validity in ('Hit', 'NoHit', 'MalformedPayload'), 'Unknown picking validity')
    if validity != 'Hit':
        require(all(result[key] is None for key in ('probeId', 'coordinate', 'depth')), 'Non-hit fabricated a probe')
        return
    nx, ny, nz = summary['counts']
    probe = result['probeId']
    require(type(probe) is int and 0 <= probe < nx * ny * nz and
            result['coordinate'] == [probe % nx, probe // (nx * nz), (probe // nx) % nz], 'Picking stable ID differs')
    depth = result['depth']
    require(math.isfinite(depth) and 0 <= depth <= 1 and depth != (0 if view['reversedDepth'] else 1),
            'Picking hit has inadmissible depth')


def validate_pairing(products):
    results = [frame['giProbePicking'] for source, frame in products
               if source == 'resource-export' and frame.get('giProbePicking') is not None]
    observed = any(frame.get('giProbePickingObservation') is not None for _, frame in products)
    if not results:
        require(not observed, 'Picking observation requires its compact resource product')
        return
    expected = observation(results[0])
    expected['executionDpiScale'] = results[0]['mapping']['dpiScale']
    for source, frame in products:
        require(frame.get('giProbePickingObservation') == expected, f'Mismatched paired picking observation: {source}')
