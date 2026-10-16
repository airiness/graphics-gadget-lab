# Graph and GPU resource exports

Use `$session = 'Scripts/GGLabSession.ps1'` with a running session `atrium`.
Commands share capture view/timing/settling/output options and `-NoWait`.
Require `status: completed` and use returned paths.

## Discover a graph

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session graph -Session atrium -Summary -Label discovery
powershell -NoProfile -ExecutionPolicy Bypass -File $session graph -Session atrium -Label full-graph
```

Summary lists executed passes in order, with versioned reads/writes and reusable
resource addresses. Full exports additionally contain compile declarations,
culling, dependencies, descriptors, barriers, acquire/release records and actual
execution allocations. Pass `occurrence` is 1-based declaration order among equal
names, including culled declarations; execution order cannot renumber it.

Full `compile` and `execution` are separate evidence. Physical handles are
session-local; a transient pool slot establishes pool reuse, not native heap
aliasing. Null handle resolution remains explicit. `submission` identifies the
completed frame fence. Build/shader identities and per-pass GPU timing remain
explicitly unavailable where the export has no authoritative evidence.

Graph requests alone add no GPU observation nodes/queries, but do add CPU/file
work. Active copies/scans/counters appear under observation records. Schemas v1,
v2, v3 and v4 cover graph-only, resource copies, boundary scans and production
counters respectively. Do not interpret an observation flag as completed
numerical coverage.

## Address a resource

Discover current names/versions with `graph -Summary` before constructing targets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session export -Session atrium `
    -Targets '[{"resource":"TAA.ResolvedSceneColor"},{"resource":"DisplayView.DepthBuffer"}]' `
    -IncludeGraph -View CAM_Courtyard -Label values
```

Use `-TargetsFile` for a UTF-8 file containing the same JSON array, especially
when passing through a shell that would change JSON quoting.

| Address field | Meaning |
| --- | --- |
| `resource` | Required RenderGraph resource name. |
| `at` | Omitted/`"final"`: last declared version; `{"before":"Pass"}`: version read by that pass; `{"after":"Pass"}`: version written by it. |
| `occurrence` | Optional 1-based occurrence of the named pass, including culled declarations. |
| `mip` | Texture mip, default 0. |
| `slices: [first, count]` | Texture slice/face range; omitted means all slices. |
| `aspect` | Optional `color`, `depth` or `stencil`; depth defaults to depth, stencil requires explicit selection. |

Copies preserve original pass order/culling, but can extend resource lifetimes
and change allocation reuse. They never revive a culled producer or define an
unwritten mip/slice. Imported allocations must already support CopySource.
Failures such as `Culled`, `UndefinedContent`, `UnsupportedFormat`,
`UnsupportedResource` and `TooLarge` are explicit; do not retarget silently.

## Interpret the result

A resource group publishes NPY arrays and a `.resource-export.json` sidecar;
`-IncludeGraph` pairs the exact same submitted frame. Discover groups through
completed sidecars, which publish last. The response has `exports`, `metadata`,
optional `graph` and `frameSerial`.

- NPY v1 values are little-endian, C order. Textures are
  `[slices, height, width, channels]`; cubes use the existing face order.
  Supported dimensions include 2D/arrays/cubes/mips and buffers; MSAA/3D fail.
- Unorm/sRGB bytes and RGBA/BGRA order are retained. Float16/Float32/Uint32 retain
  their bits. D32 is Float32; D24 depth becomes Float32 in [0,1], stencil Uint8.
  Typeless data uses the unique addressed typed view or flagged raw words.
- Registered buffers use structured dtypes with field offsets/stride; otherwise
  expect `[elements, wordsPerElement]` Uint32. Compare producer-valid records,
  excluding inactive capacity. Padding is removed from exported textures.
- Read sidecar address/version/writer, format, shape/dtype/layout and semantics.
  Scene color can be pre-exposed; depth carries its convention; motion vectors
  are current UV minus previous UV, positive Y down. Null semantics are unknown.
- The shared budget is 64 requests, 8 payload slots and 256 MiB including GPU
  readback and CPU payload data. Each image/array occupies a slot. Groups never
  split across frames. Include all paired payloads when sizing a request.

## Exact sequence-frame exports

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_StaticRailings -CaptureFrames 4,30 `
    -Exports '[{"frame":4,"targets":[{"resource":"DisplayView.SceneColor"}],"graph":true}]' `
    -Label observed
```

`-ExportsFile` accepts the same JSON array. The frame's image, arrays and optional
graph share a render serial. References export the final sample. Duplicate groups
for one frame or frames outside the path fail. Resource observation changes GPU
work/lifetimes; keep it off for performance measurements.

Validate schema, version/writer consistency, NPY data and paired frame identities
with Python `jsonschema` and `numpy` available:

```powershell
python Scripts/ValidateResourceExports.py Build/Sessions/atrium/Captures
python Scripts/ValidateRenderGraphExports.py Build/Sessions/atrium/Captures
```

Validate both backend runs when claiming shared correctness. Authoritative schemas
remain [ResourceExport](../../../../Schemas/ResourceExport.schema.json) and
[RenderGraph](../../../../Schemas/RenderGraph.schema.json); a JSON parse alone
does not validate their contracts.
