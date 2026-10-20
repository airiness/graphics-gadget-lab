# Native formatter automation

Use this reference for command-line Visual Studio COM automation or native
formatter failures. Keep the formatting scope and isolated-copy requirements
from `SKILL.md`.

## Execution context and instance

Discover full Visual Studio IDE installations with `vswhere` or Setup
Configuration and select the owner's intended toolchain. Check for
`Common7/IDE/devenv.exe`; Build Tools alone cannot provide DTE formatting.
Avoid an unversioned ProgID or a hard-coded VS major version when installations
coexist.

Check the actual Windows identity with `whoami`, the process architecture and
the .NET thread's apartment state. Run DTE calls on the same STA thread; a
Windows PowerShell host can use `powershell -NoProfile -STA -File <script>`.
Environment variables such as `USERPROFILE` do not establish the process identity.

Codex's Windows sandbox can use a separate account and private desktop while
profile environment variables still point to the owner's directories. Check
access to the selected IDE's profile under `LOCALAPPDATA/Microsoft/VisualStudio`.
A working MSBuild installation or an already-open user IDE does not establish
that DTE can initialize in this context. If isolation prevents startup, use the
normal user context only through the permitted escalation mechanism, respecting
existing authorization. Otherwise use the skill's manual fallback. Do not
disable the sandbox or change profile ACLs, DCOM permissions, registration or
shared IDE preferences to make formatting work.

## Launch, readiness and calls

Prefer launching the selected `devenv.exe` explicitly as a hidden, task-owned
process. Record its PID and start time, then obtain its DTE from the Running
Object Table (ROT) using the exact version and PID suffix:
`VisualStudio.DTE.<major>.0:<pid>`. A leading `!` may precede this suffix.
Do not bind to an arbitrary running IDE. See Microsoft's
[instance launch example](https://learn.microsoft.com/en-us/visualstudio/extensibility/launch-visual-studio-dte?view=vs-2022).

Bound the wait for ROT registration and stop if the owned process exits.
Obtaining DTE does not prove the shell or language service is ready. Use bounded
retries for `RPC_E_CALL_REJECTED` (`0x80010001`) and
`RPC_E_SERVERCALL_RETRYLATER` (`0x8001010A`), inspecting the underlying COM
exception when PowerShell wraps it. For example, retry every 250 ms with a
30-second deadline per operation. Apply this to properties, document opening,
formatting, saving, closing and quitting; do not use one fixed startup sleep
or retry every exception. A COM message filter is another option; see
[RetryRejectedCall](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-imessagefilter-retryrejectedcall).

Format isolated copies with their effective EditorConfig settings. Confirm the
intended document and language service before issuing the format command.
Successful C++ formatting does not verify HLSL language-service availability.

## Diagnose and clean up

Record the first failing stage, HRESULT, execution identity and selected IDE.
Distinguish these failures before deciding the formatter is unavailable:

| Failure | Interpretation and next check |
| --- | --- |
| `0x80080005` / `CO_E_SERVER_EXEC_FAILURE` during activation | The COM server did not initialize successfully. Check matching CLSID and account in System log DistributedCOM events, especially event 10010 for registration timeout, and profile access. This alone does not prove installation corruption. |
| `0x80010001` / `0x8001010A` after obtaining DTE | A call was rejected or the IDE was busy; allow bounded readiness retries and investigate persistent modal or initialization state. |
| `0x80040154` / `REGDB_E_CLASSNOTREG` | Check the chosen version, registration view and whether a full IDE is installed. |
| `0x80070005` / `E_ACCESSDENIED` | Check caller identity, profile access and execution-context permissions. |

Preserve the primary failure. Catch and report cleanup errors separately so a
rejected `Quit()` cannot replace the original error. Release COM references even
when quitting fails; releasing references does not itself confirm process exit.
Quit only the task-owned IDE. If termination is necessary, verify the recorded
PID and start time; never terminate all `devenv` processes or close an existing
user session. If ownership cannot be established, report that cleanup limitation.

Once bounded recovery is exhausted, complete scoped manual repairs and other
applicable checks. Report native formatting only for files that actually
completed it, and distinguish activation, formatting and cleanup failures.
