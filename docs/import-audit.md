# Static compatibility import auditing

ProsperoX can inspect a game's statically referenced PS5 imports without starting guest code.
The audit path loads SELF/ELF metadata and bundled modules through the normal loader, but it does not relocate imports, start modules, call the guest entry point, initialize Audio/Controller, or initialize Vulkan/Graphics.

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
