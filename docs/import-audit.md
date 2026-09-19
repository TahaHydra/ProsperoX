# Static compatibility import auditing

ProsperoX can inspect a game's statically referenced PS5 imports without starting guest code.
The audit path loads SELF/ELF metadata and bundled modules through the normal loader, but it does not relocate imports, start modules, call the guest entry point, initialize Audio/Controller, or initialize Vulkan/Graphics.

## Reading the audit

The status counts say what the auditor found. Two derived counts say what to do
about it, and they are deliberately separate:

```
  resolvable=N    (implemented under another identity -- drive to 0)
  unimplemented=N (no implementation; fatal only if called)
```

* **`resolvable`** — strong imports the emulator already implements, under a
  library/module qualification other than the one the title asked for (status
  `AliasCandidate`). The loader does not resolve these, so each is stubbed and
  terminates the title on first call, while the code that would have answered it
  sits in the binary. Every one is a reviewed qualified registration away from
  working, with no behaviour to decide. **This should be 0.**

* **`unimplemented`** — strong imports with no implementation anywhere (status
  `MissingHle`). Also stubbed, but a stub is only fatal when the guest calls it,
  and titles import far more than they call. Bendy runs with dozens. This is a
  work list, not a verdict.

Do not read either as "will the title run". A title can have
`unimplemented=57` and run to gameplay, then terminate on the fifty-eighth.
What static analysis can say for certain is that `resolvable > 0` is emulator
work left undone.

Resolvable imports print first under `[RESOLVABLE AliasCandidate]`; each printed
import states its binding and which bucket it is in. In JSON, each import
carries `"resolvable"` and `"unimplemented"`, and each game carries
`"resolvable_imports"` and `"unimplemented_imports"`.

The loader reports the same thing independently, at link time rather than on
first call, so it is visible before the title starts:

```
BLOCKING_IMPORT symbol=dbOlWdppb4o[Agc_v1][Agc_v1.1][Func] program=... relocation=530:
  this NID is implemented under a different identity
  (dbOlWdppb4o[Graphics5_v1][Graphics5_v1.1][Func]). Resolving it means adding a
  reviewed qualified entry for the identity the title asked for, never a blanket
  library alias
```

### Resolving a resolvable import

Add a reviewed entry for the identity the title asked for — for AGC that is the
`AgcQualified` block in `src/libs/libAgcDriver.cpp`, which registers the same
function pointer under the `Agc` identity. Review means reading the
implementation and establishing that it behaves identically however it is
qualified.

Do not add a blanket library alias. One existed for `Agc -> Graphics5` and it
exposed all 156 Graphics5 exports under the `Agc` identity, including entries
whose behaviour there had never been looked at; `tests/Phase6AgcTests.inc`
fails if it comes back. The reviewed list currently holds 80 of those 156, and
76 remain unreachable under `Agc` — a new Graphics5 export is not exposed by
default, which is the whole difference between a list and an alias.

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
