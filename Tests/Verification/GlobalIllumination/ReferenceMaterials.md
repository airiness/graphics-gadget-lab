# Reduced GI material and snapshot qualification

`gi-reference-materials` checks the frozen reduced secondary material contract;
it does not qualify specular visibility, DDGI, cost, or the completion of G2.
F1-F10 definitions and the reference estimator are unchanged.

Run from the code repository root with the owner toolchain's existing build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite gi-reference-materials -Configuration Develop -Rhi dx12,vulkan -Python <python-with-numpy-and-jsonschema>
```

The runner generates original deterministic DDS data under
`Build/Fixtures/GIMaterials`, records SHA-256 identities, and supplies the absolute
`GGLAB_GI_MATERIAL_FIXTURE_ROOT` environment variable to its hidden child sessions.
Fixture 9 requires those generated inputs. It is independent of the installed
content repository. No third-party images are used. `--self-test` and `--generate`
are available on `ValidateGIReferenceMaterials.py`. The optional `-Smoke` run is
a diagnostic subset and does not satisfy the complete material gate. Sequential groups
reuse only content-addressed ShaderCache/ShaderArtifacts copies from their previous
group; scene, Lab and lighting state remain isolated. An optional
`-ShaderCacheDirectory <directory>` supplies initial `<directory>/<rhi>/ShaderCache`
and `ShaderArtifacts` seeds. Runtime validates their input/artifact identities as
usual; compilation and native-validation timeouts are unchanged.

## Material contract and numerical rules

The expected albedo is `factor * linear(baseTexture) * (1 - metallicFactor * MR.B)`.
A disabled binding contributes one. sRGB decoding occurs before interpolation;
metallic/roughness data remains linear. UV1 differs from UV0 on the procedural
wall, and affine scale/rotation/offset precedes clamp/wrap sampling.

Material filtering chooses the first available mip whose longest dimension is at
most 64 texels, or the last available mip of an incomplete chain. The 64x32,
65x32, 129x33 and two-level 256x32 fixtures have independently authored per-mip
colours. Their selected levels are 0, 1, 1 and 1 respectively; the last case
explicitly reports a 128x16 selected level. This policy is independent of the
environment's Face32 filter.

Direct GPU ray and raster albedo are each checked against a float64 CPU sampler.
The worst absolute pixel error must be below 0.002. Every pixel must also
fall within an independently computed sRGB/filter interval: 0.5 encoded 8-bit sRGB
ULP, 0.6/256 texel coordinate error propagated through the authored texture's
Lipschitz bounds, a separate 0.5/255 linear-filter arithmetic allowance, and
0.000002 shader arithmetic allowance. This is a declared numerical
qualification profile; it is not a Vulkan conformance or minimum-precision claim.
The encoded-side sRGB bound and fixed-point coordinate conversion follow
[Microsoft's functional specification, sections 3.2.3.7, 3.2.4.1, 7.18.5 and 7.18.16.2](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
[Vulkan sampling](https://docs.vulkan.org/spec/latest/chapters/textures.html) defines
filter weight precision through subTexelPrecisionBits. Ideal-value pixel errors
and signed ROI bias remain reported; deterministic conversion bias is allowed
inside the analytic interval. Point-sampler boundaries exclude 0.6/256+0.00001
texels, so coordinate snapping cannot silently select another texel.

D=1/D=2 floor transport uses an independent area quadrature, split at texel
reconstruction and point-sampling boundaries. It integrates the same receiver
pixel positions, separately for Sun and environment. Fine/coarse CPU refinement
must differ by less than 0.2%; component means and each D=2 increment must agree
with the GPU within 1%, using `max(abs(reference), 1e-6)` per RGB/luminance channel.
The floor's primary albedo is excluded from E; its 0.7 material participates when
a continuation path returns to it. Normal/roughness/coat/emission inputs on the
secondary wall must leave the neutral floor's E and components exactly unchanged.
Single-sided backfaces occlude sky and contribute zero; double-sided backfaces
flip normals; BLEND is excluded. MASK remains UnsupportedGeometry, with headless
classification coverage and no alpha-tested reference claim.

## Snapshot and provenance evidence

Each direct/transport group changes material cases through an Immediate parameter
on the existing World, RayScene and Lab session. Consumption reads a frozen scene
material identity and frame serial, never live parameter storage. Captures retain
source and compiler/executable hashes, actual texture source/artifact digests,
content generation, resource/SRV identities, sampled format, dimensions, mip,
seed/samples, masks, and frozen camera/lighting inputs. A separate bounded export
retains the actual GPU material table, including sampler and UV transform bytes.

The audit checks material-only BLAS/TLAS reuse, transform/entity replacement,
entity generation, eligible geometry replacement, opaque/BLEND transitions and
reuse of material table slot 1. Different numerical depths use separate groups;
all replacements inside a group must retain the same content/session identity.
The public asset interface currently has no live in-place texture content replace
operation. Equal-SRV content generation and resource-slot generation replacement
are covered by the headless planner tests, rather than fabricated GPU reloads.
Sampler registry entries are immutable for the registry lifetime.

A separate 320x200 run replaces a material during a bounded, incomplete spatial
sweep. A waiting capture is cancelled. The new observation must start at version
zero; its unresolved irradiance and every unprocessed sum/compensation/RNG texel
must be exactly zero. Repeating the same material value must be NoChange.
Submitted/Discarded/Failed/CompletionUnknown and retirement remain covered by the
Runtime/AppRuntime fault suites; this test does not induce native device loss.

A RayScene snapshot retains its reduced-material texture interests through build
and query submission leases. Observation identity uses a weak control block so a
Renderer observation cannot keep those asset interests alive after content retires
and AssetManager starts shutdown. Existing model content can be retained by an
AssetOwnerScope; the procedural fixture retains both prepared model dependency
closures until session retirement.

`materials-validation.json`, ordered snapshot/texture log records, GPU table
exports, pending-state exports and the per-backend native logs are the reviewable
evidence. Runtime Completed still means calculation complete, and does not award
an automatic sampling/depth convergence certificate to these additional fixtures.
