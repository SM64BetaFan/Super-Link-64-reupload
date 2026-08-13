# ROM and asset policy

## Release boundary

Super Link 64 releases must contain no Nintendo ROM, ROM fragment, extracted or
converted game asset, or output that substantially reproduces proprietary game
content. This applies whether the content is compressed, encoded, renamed,
embedded in source, stored in a generated cache, or carried inside another
archive.

Excluded material includes:

- `.z64`, `.v64`, `.n64`, `.rom`, WAD, save, and memory-image files;
- ROM-derived patches or payloads that contain copyrighted game bytes;
- extracted models, textures, animations, maps, scripts, fonts, and audio;
- decoded texture caches, display-list or geometry dumps, frame captures used
  as fixtures, PCM recordings, and asset-bearing fidelity traces; and
- private filesystem paths, crash dumps, logs, or telemetry containing user or
  ROM data.

## Local runtime use

Any supported runtime input must be selected locally by the user. The program
must validate compatibility locally, retain no copy beyond what is necessary
for the active session, and never send the input, derived asset bytes, or local
path to a host, peer, update service, analytics service, or crash reporter.

The project must not download a ROM, search broad user directories for one,
provide circumvention material, or weaken dependency checks in order to accept
an unsupported image.

## Permitted metadata and original assets

Non-reversible hashes and basic header identifiers may be recorded solely for
local compatibility validation. They must not be presented as proof of a
user's ownership or as a compatibility claim beyond recorded testing.

Wholly original assets may be included only with documented authorship, an
explicit redistribution license, editable source when applicable, and a
`THIRD_PARTY.yml` entry when the copyright is not held by the project.

Packaging must use an allowlist and fail closed if an unclassified binary or
asset file is found.
