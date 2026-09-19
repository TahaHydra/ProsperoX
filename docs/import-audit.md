# Static compatibility import auditing

ProsperoX can inspect a game's statically referenced PS5 imports without starting guest code.
The audit path loads SELF/ELF metadata and bundled modules through the normal loader, but it does not relocate imports, start modules, call the guest entry point, initialize Audio/Controller, or initialize Vulkan/Graphics.

## Blocking imports

The status counts answer "what did the auditor find". They do not answer "will
the title run", and the difference has cost a debugging session:

* `MissingHle` — the NID is not implemented under any identity.
* `AliasCandidate` — the NID *is* implemented, under a different
  library/module qualification than the one the title asked for.

`AliasCandidate` reads like an advisory, and for a weak import it is one. For a
**strong** import it is a guaranteed runtime termination: the loader patches the
relocation to a stub and the first call exits with `UNRESOLVED_STRONG_IMPORT`.
Bendy's `dbOlWdppb4o[Agc_v1][Agc_v1.1]` was exactly this — counted under
`aliases`, not under `missing`, and fatal.

So the report carries a separate count:

```
  blocking=N (strong imports that will terminate the title)
```

`blocking` is every non-weak `AliasCandidate`, `MissingHle` or `Malformed`
import. Those are printed first, under `[BLOCKING <status>]`, and each printed
import states its binding. In JSON, each import carries `"blocking": true|false`
and each game carries `"blocking_imports": N`.

**`blocking` must be 0 before a title is expected to run.** Any other count can
be non-zero without meaning anything is wrong.

The loader reports the same thing independently, at link time rather than on
first call, so it is visible before the title starts:

```
BLOCKING_IMPORT symbol=dbOlWdppb4o[Agc_v1][Agc_v1.1][Func] program=... relocation=530:
  this NID is implemented under a different identity
  (dbOlWdppb4o[Graphics5_v1][Graphics5_v1.1][Func]). Resolving it means adding a
  reviewed qualified entry for the identity the title asked for, never a blanket
  library alias
```

### Resolving a blocking AliasCandidate

Add a reviewed entry for the identity the title asked for — for AGC that is the
`AgcQualified` block in `src/libs/libAgcDriver.cpp`, which registers the same
function pointer under the `Agc` identity. Review means reading the
implementation and establishing that it behaves identically however it is
qualified.

Do not add a blanket library alias. One existed for `Agc -> Graphics5` and it
exposed all 156 Graphics5 exports under the `Agc` identity, including entries
whose behaviour there had never been looked at; `tests/Phase6AgcTests.inc`
fails if it comes back.

## One game

```powershell
.\_Build\phase0-windows\kyty_emulator.exe `
  --audit-game "E:\GOY\PPSA26344" `
  --audit-json ".\_Build\ghost-imports.json"
```

## A whole collection

```powershell
.\_Build\phase0-windows\kyty_emulator.exe `
  --audit-library "E:\Games" `
  --audit-json ".\_Build\library-imports.json"
```

A game root is a directory containing `eboot.bin`. Library mode recursively discovers such roots and audits each game with an isolated runtime-linker instance before producing a globally deduplicated backlog.

## Result classes

- `ExactHle`: the complete requested NID/type/library/module/version identity exists in ProsperoX.
- `CompatibleHle`: an already reviewed bounded compatibility rule resolves the request.
- `GuestModule`: a bundled module from the same game exports the exact requested identity.
- `AliasCandidate`: ProsperoX has the same NID and symbol type under another qualification. This is evidence for ABI review only; the auditor never creates aliases automatically.
- `MissingHle`: a strong static import has no matching HLE implementation, approved compatibility rule, or bundled guest-module export.
- `WeakUnresolved`: an unresolved ELF weak import; it remains weak rather than being converted into a fabricated implementation.
- `Malformed`: the binary or its dynamic relocation metadata could not be interpreted safely.

The JSON format is schema version 1 and is deterministic so reports can be diffed or consumed by CI/dashboard tooling.

Static auditing cannot predict imports requested later through runtime-only mechanisms such as `sceKernelDlsym` or paths selected only by guest control flow. Those require a separate runtime trace; static results should not be treated as a guarantee that a title will boot or render.
