# Distribution policy

## Current source release

The repository may distribute the original Super Link 64 source scaffold under
`AGPL-3.0-or-later`. It includes bridge source, Lua source, documentation,
synthetic tests, dependency pins, and a local integration tool. It must not
include copied dependency source, generated patches, binaries, ROM data, or
extracted assets.

The local integration tool identifies pinned host locations without embedding
upstream implementation hunks. It may modify a user-supplied checkout for local
testing. That permission for original tool source does not approve distribution
of its generated host changes or outputs.

Until `RELEASE_BLOCKERS.md` is completed, do not distribute:

- an SM64CoopDX fork or executable;
- a generated patch, diff, or modified file containing SM64CoopDX source;
- liboot source or binaries containing unresolved decompilation material;
- a native Super Link 64 plug-in or companion binary; or
- a mod package or installer output that contains, fetches, or reconstructs
  excluded game content.

## Future combined distribution

A source release must be the preferred form for modification and must contain
the exact project source, interface definitions, dependency lock, build and
installation scripts, legal notices, contributor records, and change notices
used for that release. It must not depend on a mutable branch to identify its
contents.

If an object-code artifact is offered for download, the corresponding source
must be offered from the same place, without charge or extra access conditions,
and remain tied to that exact artifact. A pointer to an unrelated or moving
upstream branch is insufficient.

## Network and mod distribution

Treat delivery of a mod from a multiplayer host to a client as distribution.
Every transferred program file must carry or be accompanied by the applicable
copyright, warranty, license, and source information. Prefer readable Lua
source. Bytecode requires the exact matching source.

Because ordinary documentation files may not be included in a host's mod-file
transfer, a future mod package must also carry a valid, non-executing Lua-form
copy of its notices and license information. The interactive interface must
provide a prominent License and Source Code route usable by remote users.

Native libraries must never be accepted or executed merely because a remote
host supplied them. Native installation requires a separate, explicit user
action, platform and architecture validation, integrity verification, and a
documented update and removal path.

## Release evidence

Each release decision must retain:

- dependency revisions and cryptographic hashes;
- the licenses and written permissions actually relied upon;
- an archive inventory and forbidden-content scan result;
- source and binary checksums, SBOM, and build provenance;
- test results that do not contain proprietary data; and
- the reviewer and date for legal, security, and packaging approval.
