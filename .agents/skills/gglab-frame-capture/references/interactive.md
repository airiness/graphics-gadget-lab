# Interactive capture

Use this workflow when the owner requests a visible launch or is already using
the app. In an ordinary/F5 launch, open DevTools **Application > Frame Capture**;
no session connection is required. `--no-devtools` removes the panel.

For a developer-owned F5 session, set the debugger arguments to
`--session dev --session-lifetime developer`. Connect with
`GGLabSession.ps1 attach -Session dev`; retain its `runIdentity` for later
commands. Attachment does not own the process. Developer lifetime has no idle
exit; ordinary session launches retain automation idle expiry. Minimized sessions
answer control requests, cancellation and stop without rendering frames.

- **Capture (F9)** submits the selected Scene/Composited image as one job with
  a final bundle manifest, PNG and metadata. Busy/rejected admission has no job
  ID or history record; the panel, notification and log report the reason.
  Manual actions are not retried automatically. When
  ImGui owns keyboard input, use the button or focus the rendered view first.
- **Capture All Views** snapshots authored views/settings and processes them in
  order, owning one accepted job at a time. Busy retries without changing the
  view. Consuming the terminal receipt allows the next view; rejection/failure
  stops the batch. The total authored view count is not limited by receipt
  capacity. **Cancel views** stops new admission and reports the whole current
  job's cancellation verdict. TooLate preserves the eventual publication.
  An unresolved batch cannot be replaced.
- **Screenshot**, **Render Graph** and **Resources** share Timing, Settle Frames,
  Reference View, Output Directory, Label and Note. Timing defaults to **Next
  frame**, meaning the next rendered frame. Select **After ready** for settled
  content. Source affects only screenshots; F9 retains its selected source.
- **Export Graph** exports full compile/execution records; **Summary only**
  exports executed pass order and versioned reads/writes for discovery.
- **Resources** lists supported candidates from the latest diagnostics snapshot.
  **+** adds a target; choose Final/Before pass/After pass, occurrence, texture
  mip/slices and aspect. Add the resource more than once for different versions.
  **X** removes one target. Buffer exports use the full buffer.
- **Export Resources** writes NPY arrays plus a sidecar; **Include graph** pairs
  the same frame. Discovery support does not guarantee current contents,
  liveness or capacity; the actual frame resolves addresses again and reports
  explicit failures. Editing a target later does not alter an admitted plan. Resource + graph is
  one required bundle with per-product status and a final manifest.
- **Current Frame** gates/pending counts explain waits. **History** retains the
  latest 16 consumed job metadata/file records, including actual frame serials,
  independent status/publication, per-product paths/failures and the manifest.
  Click a filename to copy its full path. History owns no raw payload or reserved
  receipt; eviction removes display data only, leaving artifacts on disk.
  Pending IDs are bounded by shared job/receipt admission, while the authored
  view list is separate business input. These are record bounds, not byte bounds.
- **Open folder** requires Completed + Published and opens the retained manifest's
  actual directory only on an explicit click. Editing Output Directory afterward does not redirect it.
  Shell failure does not change capture completion; hidden launches never
  open folders automatically.

Apply the same image/metadata/log and numerical checks as hidden runs. Interactive
graphs can include DevTools passes; compare `frame.developmentTools` before
attributing structural differences to the renderer. Exports add observation or
file work; disable them for ordinary performance measurements.
