# Repacked SELF contract

ProsperoX recognizes a separate, bounded PS5 FSELF container profile. It is a
structural description, not authentication, proof of provenance or a decryption
facility. No title IDs, filenames, hashes or fixed eight-byte correction enter
the decision. Normal SELF acceptance rules remain the default.

The fallback is eligible only when normal SELF payload ownership is insufficient.
It requires a PS5 ELF (ABI version 2), either already recognized SELF magic
(`0x1d3d154f` or `0xeef51454`), the version-0/flags-0x22 fake header identity,
extended-info program type 1,
bounded/aligned header and metadata regions, and an even sequence of digest/data
entry pairs. Entries must use the plain signed 16 KiB block profile, name unique
LOAD/RELRO/DYNLIBDATA/COMMENT owners, have exact stored/uncompressed/owner sizes,
and follow the declared physical layout with 16-byte alignment. Owner file
ranges cannot overlap. Every ordinary program-header payload must be wholly
contained in exactly one owner; partial or ambiguous mappings are rejected.
Existing runtime size, dynamic-table, relocation and TLS validation still runs.

Exactly one ownerless VERSION is allowed: no flags or virtual/physical address,
bounded nonzero size (at most 64 MiB), alignment zero or a power of two up to 16,
and a memory-size field at most 64 MiB, either equal to the payload size or a
multiple of its alignment. VERSION is never a guest mapping: the retained memory
size does not determine how many bytes are read, allocated, or zero-filled. The
writer preserves ELF headers; this field can differ from the emitted file size.
Its logical interval cannot overlap another header. Its physical start must be
uniquely provable by exact EOF at the final entry end (the published unpadded
writer layout). No alternative padded placement is inferred. The declared SELF
end must equal the final entry end rounded up to 16. Extra bytes, short trailers
and competing mappings are errors. The resolved offset is retained for reads;
there is no size-only fallback for arbitrary repack reads.

An ownerless NOTE may be omitted only with zero flags, zero virtual/physical
address, zero memory size, normal NOTE alignment and a logical interval after
all owned payloads that overlaps no other payload. NOTE may precede or follow
VERSION logically. NOTE is not mapped or read as runtime
data. Omission never fabricates zero bytes. Other missing segment types fail.
Debug dumps identify omissions; full ELF export is refused for this profile
rather than manufacturing missing metadata or section tables.

Behavioral source: the open-source
[make_fself.py writer](https://github.com/EchoStretch/kstuff/blob/elfldr-compatability/ps5-kstuff/porting_tool/make_fself.py),
especially entry selection and final VERSION emission. Implementation and
synthetic bytes are original; no upstream implementation or commercial payload
is copied. The generated fixture matrix is the acceptance specification.

The distinction between supplementary metadata and loadable process segments
also follows the [ELF program-header model](https://gabi.xinuos.com/elf/07-pheader.html).
This does not assert a canonical Sony VERSION format or authenticate a dump.
Normal accepted SELF fixtures remain on their existing path; the repack profile
is not a global replacement for SELF validation.
