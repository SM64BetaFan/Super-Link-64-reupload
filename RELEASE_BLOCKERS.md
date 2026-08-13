# Combined release blockers

## Status

This repository may publish the original Super Link 64 source scaffold under
`AGPL-3.0-or-later`. It contains no dependency source, generated host patch,
native binary, combined executable, ROM, or extracted game asset.

**A runnable or combined release is blocked.** Do not publish a modified host,
generated patch or diff, linked native library, combined executable, container
image, or preassembled mod package until every critical item below is closed
with recorded evidence.

This checklist is a project control, not a legal opinion. Obtain qualified
legal review before distributing a combined work.

## Critical licensing blockers

- [ ] Obtain a repository-wide SM64CoopDX license or written authorization from
      the relevant rights holders that expressly permits copying, modification,
      patching, linking, and distribution in source and object form under terms
      compatible with `AGPL-3.0-or-later`.
- [ ] Resolve redistribution rights for every zeldaret/oot-derived file compiled
      into liboot. A grant covering only liboot-authored integration code is not
      sufficient.
- [ ] Complete a combined-work review for the exact architecture. Static versus
      dynamic linking is not accepted here as a way to avoid AGPL obligations.
- [ ] Confirm that every contributor has the right to submit their work under
      `AGPL-3.0-or-later`, and record the copyright holders.
- [ ] Add the complete, unmodified GNU Affero General Public License text to the
      eventual release and verify that no notice suggests it covers unlicensed
      third-party material.

## Host and distribution clearance

- [ ] Pin an approved SM64CoopDX release and commit; do not track a moving branch.
- [ ] Preserve upstream credits and all dependency licenses and notices.
- [ ] Obtain written approval before submitting to a service whose terms require
      rights broader than the uploader can grant for third-party material.
- [ ] Resolve the official CoopDX mod site's prohibition on ROM discussion before
      posting a ROM-dependent project there, or use a different distribution
      channel.
- [ ] Review the project name, logos, screenshots, store text, and non-affiliation
      language for trademark and copyright risk.

## AGPL release requirements

- [ ] License every original program component under `AGPL-3.0-or-later` with
      clear per-file or repository-level notices.
- [ ] Publish the preferred form for modification, including exact bridge code,
      Lua source, interface definitions, dependency pins, and build/install
      scripts corresponding to each binary.
- [ ] Provide equivalent, no-charge access to the exact corresponding source at
      the same place as each downloadable object-code artifact.
- [ ] Add a prominent in-product License and Source Code route for interactive
      and networked use.
- [ ] Ensure multiplayer recipients receive the applicable copyright, warranty,
      license, and source notices. Do not ship Lua bytecode without its matching
      source.
- [ ] Mark modified covered components prominently with the fact and date of
      modification.

## ROM and asset controls

- [ ] Keep all ROMs outside the repository, build tree, cache, CI workspace, test
      fixtures, and release staging directory.
- [ ] Reject ROM extensions, fragments, extracted assets, audio captures,
      geometry dumps, generated asset caches, save data, and asset-bearing patch
      formats during packaging.
- [ ] Confirm runtime code never uploads or sends ROM paths, ROM bytes, extracted
      asset bytes, or user filesystem information to peers or telemetry.
- [ ] Use only non-reversible identity metadata for local compatibility checks.
- [ ] Audit every image, model, sound, font, and promotional file for documented
      authorship and redistribution permission.

## Technical and supply-chain gates

- [ ] Do not rely on CoopDX development-only unsafe Lua libraries or automatic
      loading of native code received from a multiplayer host.
- [ ] Require explicit local installation of native components and validate the
      liboot API and library version before use.
- [ ] Define a versioned trust boundary for any native plug-in or companion
      protocol and complete a security review of all peer-controlled input.
- [ ] Produce platform-specific packages without bundling SM64CoopDX itself
      unless its distribution rights have been cleared.
- [ ] Generate checksums, a dependency lock, an SBOM, and signed provenance for
      release artifacts.
- [ ] Verify archives against an allowlist and inspect their contents before
      publication.

## Current source-only boundary

The source tree may contain original Super Link 64 code, synthetic tests,
interface descriptions, dependency identifiers, and local integration tooling.
The installer locates hook points by symbol and structure; it does not carry
upstream source hunks. It may edit a user-supplied checkout for local testing,
but the resulting host diff and executable stay outside this repository and
must not be redistributed under the current policy.
