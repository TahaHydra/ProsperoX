# Phase 6 Bendy container investigation

Follow-up: [tessellation configuration checkpoint](phase6-tessellation-configuration.md)
now passes ring/offchip setup and qualified condition destruction. Bendy reads
initial level assets and stops at qualified POSIX `unlink` in IL2CPP. No
menu/gameplay. The loader/runtime history below remains preserved evidence.

2026-09-13. Current real-title integration target: Bendy and the Dark Revival,
PPSA27624, local `param.json` version `01.000.003`, master `01.00`.
Input: `C:\Dev\ps5\Bendy\PPSA27624-app0\eboot.bin`, 34,277,858 bytes,
SHA-256 `149EEA474A0C79EC6B8E79F9F352F37A8180B1FA7ED294B09937E6D119D9C548`.

The user extracted the normal v01.003 package, not intentionally a backport,
but cannot verify the executable's original dumping or transformation history.
Treat it as a possibly repacked/decrypted scene executable of unknown provenance.
Neither canonical retail layout nor definite corruption has been established.

## Actual runs

Two bounded 90-second attempts naturally exited **321 in under one second**,
after selecting the real RX 7800 XT, before guest execution. Vulkan and
synchronization validation were enabled. No guest frames, menu, audio, or input
route was reached. The environment also reports a stale Epic overlay JSON path;
that loader message is separate from the executable rejection. There were no
VUID or synchronization-hazard reports in these short attempts.

Evidence directories under `_Build/evidence`:

* `phase6-bendy-baseline-20260913`: exact input/build hashes, title metadata,
  arguments, exit, logs, `container-layout.json` and `repack-hypothesis.json`.
* `phase6-bendy-diagnostic-20260913`: rerun with the expanded rejection diagnostic.

The first rejected program header is index 12, type `0x6fffff01`, logical offset
34,260,552, declared payload 6,986 bytes. It has no SELF payload owner.
Declared SELF end is 34,270,880; physical bytes after it total 6,978.
Header 13 is an ownerless NOTE, 72 bytes, with zero flags/address/memory size.
All six block-bearing SELF entries have matching stored/decompressed/owner sizes
and lie within the file. That observation does not authenticate their contents.

## A known writer pattern explains the discrepancy

The open-source [make_fself.py implementation](https://github.com/EchoStretch/kstuff/blob/elfldr-compatability/ps5-kstuff/porting_tool/make_fself.py)
selects LOAD/RELRO/DYNLIBDATA/COMMENT for entries, retains VERSION separately,
and omits standalone NOTE data. `_prepare` rounds the final entry end up to 16
bytes for `file_size`, while `save` appends VERSION immediately after the final
entry's bytes without seeking to that aligned end (lines 619–692 inspected).

For this input, the last payload ends at 34,270,872: eight bytes before the
aligned declared end. Adding the declared 6,986-byte VERSION gives exactly the
physical file length, 34,277,858. Extended-info program type is 1, matching the
writer's fake type. This is a strong structural match to a known FSELF writer
pattern, **not proof that this writer produced this file or that every payload
is valid**. No commercial bytes were used to create a fixture or copied into
ProsperoX. No external implementation was copied.

The independent [ftpsrv reader](https://github.com/ps5-payload-dev/ftpsrv/blob/master/self.c)
instead locates VERSION at the declared SELF end and can ignore failure reading
it; it also skips headers without entries. Those permissive choices are not a
sufficient substitute for ProsperoX's executable validation contract.

## Implemented repack contract and runtime checkpoint

The subsequent user-authorized implementation adds the separate
[strict repack profile](self-repack-contract.md), retaining normal SELF handling.
Synthetic inputs were tested before accepting the commercial containers.
`phase6_self_repack` passes **38 accepted / 1,315 rejected** generated cases,
including every byte truncation, all 16 final-entry alignment remainders, both
recognized magic values, ownership/overlap/overflow errors, runtime NOTE
rejection, VERSION subrange reads, and reader-state reuse. Unknown/padded
trailer alternatives are rejected. Full ELF export refuses before overwriting
an existing file because omitted metadata cannot be reconstructed.

The adjacent libraries demonstrate why metadata headers must be distinguished
from runtime mappings: libc VERSION is 26 file bytes / 32 memory bytes with
16-byte alignment and a preceding omitted NOTE; the web API library retains
32 memory bytes for a 39-byte VERSION. Neither VERSION is mapped. Its retained
memory-size field is bounded/aligned metadata, never an allocation/read length.
The exact physical payload contract remains unchanged for these variants.

The executable and both adjacent modules now pass loading and relocation.
Guest execution starts at `0x900000070`, dynamically loads PS5Util and
Il2cppUserAssemblies, starts their modules successfully, initializes memory,
creates worker threads, and reads scripting metadata. The following generic
runtime defects were exposed and addressed with synthetic regressions:

* TLS patch discovery stopped at undecodable bytes at executable offset
  `0x5c71`, missing a supported prefixed `mov rax, fs:[0]` at `0x81eae4`.
  Windows now waits for loaded, file-backed unwind metadata and resumes
  bounded decoding at its function starts. It does not resynchronize by
  searching raw bytes. The fixture executes TLS after a gap and preserves
  TLS-looking immediate/gap bytes and guest red-zone values. Malformed unwind
  tables cannot publish partial anchors. Other hosts retain the prior path.
* Exact POSIX open/close/lseek/fstat registrations were missing. They now use
  POSIX wrappers, preserving `-1` plus guest errno and the distinct kernel ABI.
  Negative descriptors in POSIX lseek/fstat now report EBADF, following
  [lseek](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lseek.html)
  and [fstat](https://pubs.opengroup.org/onlinepubs/009696699/functions/fstat.html).
* Existing Phase 2 address wait/wake functions now have the explicit
  `libkernel_sync_on_address` identity requested by guests. Existing pthread
  scheduling queries/updates also have their `libkernel` registrations, with
  direct pthread error returns. No wildcard resolver fallback was added.
* Fatal diagnostics retain captured guest registers and bounded frame-pointer
  walks, making native guest faults distinguishable from a host-only unwind.

Latest runtime evidence: `_Build/evidence/phase6-bendy-setsched-20260913`.
The 90-second-bounded run **naturally exits 86 after 2.95 seconds** at
`23LRUSvYu1M[Agc_v1][Agc_v1.1][Func]`, eboot relocation 524, patch address
`0x901dc9d60`. No menu, game frame, or gameplay is claimed. Windows Vulkan and
synchronization validation are enabled on the RX 7800 XT; no VUID or
synchronization-hazard reports occurred in this attempt.

The next engineering boundary is the qualified AGC ABI. ProsperoX currently
registers this NID under `Graphics5`; its `AgcInit` returns success even for
unsupported versions. SharpEmu independently registers it under `libSceAgc`,
but warns that its human-readable name is provisional. Audit the public
Agc/AgcDriver identities and initialization/version contract, add synthetic
error/state tests, then bind the supported behavior explicitly. Do not alias
the entire Graphics5 table blindly or conceal unsupported initialization.

Final build succeeds. Focused Phase 1/2 plus repack/POSIX tests passed **14/14**
before host memory pressure. The final full regression attempt, with real RX
7800 XT validation, reports **54/77 passed** in 43.45 seconds, including **all
nine Phase 6 tests**. Twenty-two tests failed before their assertions because
Windows could not commit the 13,824 MiB guest direct-memory backing; the
remaining compute test reached its known unsupported attachment-feedback-loop
dynamic-state capability. No VUID/SYNC-HAZARD reports were found. Evidence:
`_Build/evidence/phase6-repack-final-regression-20260913` (`LastTest.log`, JUnit,
CTest output and environment result). The full suite is **not green**.

At investigation time Windows reported 11,216,244,736 bytes of available commit
(10.45 GiB), less than the 13.5 GiB emulator backing alone. Free at least about
4 GiB of commit headroom and repeat the full suite; this is unrelated to SSD
space for game files. No paging-file settings or other applications were
changed. Phase 6 commercial route/session criteria remain unmet.
