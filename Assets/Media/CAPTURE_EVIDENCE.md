# Capture evidence

`CAPTURE.md` is the human reference: images, settings, observations and known
limitations. Its adjacent `capture-evidence.json` preserves investigation data.

Evidence schema version 1 records image SHA-256, capture/input identities,
method and crop, backend/driver/validation facts and quantitative measurements.
Image paths are relative to the evidence file. Input paths marked `runtime`
are relative to the code repository root. Crop rectangles use decoded source
pixels with inclusive x/y and exclusive x+width/y+height bounds.

`null` means unrecorded. `notArchived` and `fingerprintOnly` retain an identity
without requiring a local file. Capture hashes identify historical bytes;
rebuilt binaries and later asset revisions may differ. Recovery-time hashes
are labelled separately from hashes recorded during capture.

`baseline.json` remains authoritative for the frozen rendering baseline.
Evidence references into it use `manifestPath` and a JSON `pointer`.
`ValidateRenderingBaseline.ps1` accepts only baseline schema version 2, which
checks Runtime assets and archived images. Legacy schema version 1 also covered
authoring files and is rejected; authoring validation belongs to its workspace.
