# AppRuntime lifecycle tests

`GGLabAppRuntimeTests.exe` runs the `gpu-fault-lifecycle` suite alongside the
existing lifecycle and capture coordinator suites. It needs no window, driver,
shader compiler or GPU. Synthetic shader artifacts are consumed only by the
simulated backend; they are not executable shader validation evidence.

The fault fixture composes the production `GGLabAppRuntime`, `Renderer`,
RenderGraph, IBL scheduler, frame capture service and asset upload scheduler
through the normal RHI factory. Its simulated backend uses the real neutral
`RHIUploadPool` and `ResolveFrameSubmission` implementations. Only this test
target receives the private neutral `Graphics/RHI` include directory; it has
no general Runtime Private, WinApp or Shader Toolchain access. Its test sources
remain owned and compiled exclusively by `GGLabAppRuntimeTests`.

The frame-outcome matrix exercises successful submission, fenced and unfenced
`EndFrame` failures, RHI-internal recording aborts, and Renderer aborts before
recording, after recording, and after an upload-order rejection. The latter
paths drive the production `RenderHost` directly using the initialized
AppRuntime services; fenced and unfenced aborts preserve their distinct
retention behavior. A separate `AppRuntime::Tick` scenario holds eight completed
capture payloads, triggers real export backpressure after graph construction,
and verifies that the next frame submits once capacity is released.

These scenarios check graph retirement fences, the last submitted fence,
capture retry versus failure, fatal host behavior and temporal settlement.
The fixture's synthetic history producer imports, declares writes to and
exports real temporal history resources without executing a shader. This
checks history ownership and publication, not history pixel values. The pure
outcome derivation table, including abort without an RHI frame, belongs to
`GGLabRuntimeTests --suite rendering-contracts`. Concrete participant API order
is reviewed against the documented Renderer settlement order; no general
Runtime Private include access or production test hook is added here.

After production IBL has queued a pending readback, a frame submission is
accepted without a completion signal and idle verification fails. The suite
checks upload quarantine, retained graph/capture/IBL owners, terminal runtime
state, refusal of another frame, queued upload cancellation after quiescence,
unread failed captures, and idempotent finalization. Verified idle and confirmed
device loss are separate teardown outcomes.
An abort whose retirement signal cannot be verified also exercises Renderer
poisoning, unread failed capture retention and checked teardown.

Timeout and failed-idle exhaustion execute in child processes because production
AppRuntime deliberately calls `_Exit` without destroying GPU-visible owners.
The child writes flushed lifecycle events; the parent checks the exit status,
bounded waits, and absence of destruction, readback, new submissions or waits
on an unsignaled point. A watchdog terminates only its own child if it hangs.
`--gpu-fault-child <timeout|failed> <receipt>` is an internal test-runner entry.

These are deterministic cross-layer software lifecycle regressions. They do not
inject a fault into a native driver or establish GPU/device-loss qualification.
