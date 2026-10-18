# ruleset-renewal

Luau package `ruleset.renewal` (architecture sections 6.3, 6.4, 10, 12 M5): the renewal
architecture proof. It must be a data diff plus a few stage overrides over the shared
mechanism; if it needs engine or `rathena.base` changes, that is a design bug.

- `mod.toml`, `package.toml`: manifest and BR-RULESET-118 config (two bundles; sub-flags are unsupported advanced overrides, off by default).
- `data/`: pipeline order (019, 044), tables, hook surface (117).
- `scripts/`: domain types, formula stubs (pending), `register` entry.
- `conformance/vectors/`: vectors keyed by BR-RULESET ID (119); hand-derived, to be replaced with legacy golden traces.
- `tests/`: `python3 -m unittest discover -s tests -v` (Python 3.11+, stdlib only).

Pending behavior tests are expectedFailure tagged by rule ID; an unexpected pass fails the run.
Open: RES rule wording (025) and float32 crit (034) need golden-trace confirmation.
