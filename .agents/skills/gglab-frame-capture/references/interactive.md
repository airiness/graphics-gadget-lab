# Interactive capture

Use this workflow when the owner requests a visible launch or is already using
the app. In an ordinary/F5 launch, open DevTools **Application > Frame Capture**;
no session connection is required. `--no-devtools` removes the panel.

- **Capture (F9)** saves the selected Scene/Composited PNG and metadata. When
  ImGui owns keyboard input, use the button or focus the rendered view first.
- **Capture All Views** queues authored views in order. Temporary pressure
  waits before the next view; it does not limit the total number of views.
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
  explicit failures. Editing a target later does not alter a queued request.
- **Current Frame** gates/pending counts explain waits. **History** retains the
  latest 16 results, including actual frame serials and paths/failures. Click a
  filename to copy its full path; resource results list arrays and paired graph.
- **Open folder** opens a successful result's actual directory only on an
  explicit click. Editing Output Directory afterward does not redirect it.
  Shell failure does not change capture completion; hidden launches never
  open folders automatically.

Apply the same image/metadata/log and numerical checks as hidden runs. Interactive
graphs can include DevTools passes; compare `frame.developmentTools` before
attributing structural differences to the renderer. Exports add observation or
file work; disable them for ordinary performance measurements.
