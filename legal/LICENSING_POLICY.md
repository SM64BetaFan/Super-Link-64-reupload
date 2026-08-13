# Licensing policy

## Original work

Original program code and documentation created for Super Link 64 are intended
to be distributed under `AGPL-3.0-or-later`, unless a file is clearly identified
as non-program material under another compatible license.

Contributors must submit only work they have the right to license on those
terms. Contributions must retain authorship and modification history. No
project notice may remove or narrow rights already granted by the GNU Affero
General Public License.

## Third-party work

Third-party work keeps its own copyright and license. A Super Link 64 header,
directory, archive, or build process does not relicense it. Every third-party
component requires all of the following before inclusion:

1. an identified source and immutable revision;
2. an identified copyright holder or provenance trail;
3. an explicit license or written grant covering the intended use;
4. a compatibility review against `AGPL-3.0-or-later`;
5. preserved notices and any required source or attribution; and
6. an entry in `THIRD_PARTY.yml`.

`NOASSERTION`, missing license text, unclear authorship, or an informal claim of
being open source is treated as no release permission.

## Linking and patches

Project policy assumes that a native bridge linked to liboot is part of an
AGPL-covered combined work, whether the link is static or dynamic. A different
conclusion requires written legal review for the exact implementation.

An original adapter that contains no copied host code may be licensed under the
AGPL. That does not grant permission to distribute the host, a modified host,
or a resulting combined executable. Patch files containing upstream text or
adaptations may not be published until the upstream modification and
redistribution rights are documented.

Dependency fetching, submodules, build-time patching, and asking users to
assemble components locally are not accepted as substitutes for redistribution
permission when the project itself conveys the affected material.
