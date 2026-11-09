# Repository conventions

[简体中文](AGENTS.md)

Commit subjects and bodies use the English Angular format:
`type(scope): imperative English summary`; breaking changes get a
`BREAKING CHANGE:` body footer. One commit does one thing.

Public Actions entry points are new version tag pushes and `workflow_dispatch`
only — no branch push, PR, or scheduled triggers. Tag jobs must check
`github.event.created && !github.event.deleted`.

Manual verification accepts a source ref with `publish` defaulting to false;
publishing requires an existing, matching version tag. Keep all tests plus
manifest, docs and package validation. Only the publish job gets
`contents: write` and must never overwrite existing Release assets.

Validation evidence follows the workspace convention
(`winshun-dev/DEVELOPMENT.md`): curated evidence and machine-readable results
only inside this repo (≤5 MB), raw material stays out; every validation README
must include a "not tested" section.
