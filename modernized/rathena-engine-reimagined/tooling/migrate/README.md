# rathena-migrate (Python reference migrator)

Reads the legacy YAML databases (`db/<x>.yml` stub -> `db/pre-re` or `db/re` -> `db/import`), merges them **exactly like the
legacy loader** and writes Luau data modules plus a pre-renewal vs renewal diff report. It is verified field by field
against the real legacy map-server (see "Verification").

Nothing here touches the legacy sources or the ruleset packages. Generated output goes to `out/` (git-ignored); the caller wires
it into `ruleset-prerenewal`, `ruleset-renewal` and `rathena.base` later. Python 3 + PyYAML only, no network.

## Usage

```sh
cd modernized/rathena-engine-reimagined/tooling/migrate
python3 -I rathena_migrate.py migrate  --mode both            # out/prerenewal, out/renewal, out/diff_pre_vs_re.md  (~80 s)
python3 -I rathena_migrate.py migrate  --mode prerenewal      # ~14 s
python3 -I rathena_migrate.py validate --mode renewal --only item_db,mob_db --strict   # load + report only, JSON on stdout
python3 -I rathena_migrate.py diff                            # only out/diff_pre_vs_re.md
```

Options: `--root` (repo root holding `db/`, default this repo), `--out`, `--import-dir` (replaces the `db/import/` chain link,
used for overlays/tests), `--no-raw` (skip the not-yet-ported tables), `--no-renewal-aspd` (renewal build without
`RENEWAL_ASPD`: `job_aspd` comes from `db/pre-re`), `--generator` (follow `Generator: true` imports), `--strict`.
Exit codes: 0 ok (the shipped data loads with 0 rejected entries in both modes; the only data warning is the duplicate `EP172ALPHA` key of mob 20641), 1 `--strict` and entries were rejected, 2 a file was unreadable / malformed / has a wrong `Header.Type` or
`Header.Version` / a non-overlay file is missing. A missing `db/import/*.yml` is not an error (the overlay is optional, as in
the legacy server). Rejected entries are reported, not fatal, because the legacy loader skips them too.

### Output layout (`out/`)

```
out/prerenewal/ and out/renewal/
  item_db_usable.luau item_db_equip.luau item_db_etc.luau   # split by the YAML file that created the item
  mob_db.luau skill_db.luau status.luau
  job_stats.luau job_aspd.luau job_exp.luau job_basepoints.luau   # the one merged JOB_STATS database, four views
  size_fix.luau attr_fix.luau refine.luau statpoint.luau
  raw/<db>.luau                                            # quests, pets, item groups, combos, ... (TODO, see below)
  manifest.json                                            # counts, validation summary, sha256 of every file (no timestamps)
out/diff_pre_vs_re.md
out/verification_report.md  out/verification/*.json        # written by tools/verify_all.py
```

Every module is `return { ["ruleset.prerenewal:item/red_potion"] = { id = ..., client_id = 501, ... }, ... }`. Ids are
`<ruleset.prerenewal|ruleset.renewal>:<kind>/<slug>` (slug of AegisName for items and mobs, of `Name` for skills, of the `SC_` name
for statuses, of the `JOB_` name for jobs); the legacy number is kept as `client_id`. A slug collision (e.g. `Knife` and `Knife_`)
gets `_<legacy id>` appended and is listed in `manifest.json` (`id_slug_collisions`: 461 in pre-re = 409 items + 52 mobs, 847 in renewal = 785 + 62). Cross references
(drops, ItemCost, status lists, `alias_of`) are namespaced ids. Enum fields are the legacy constant names without prefix
(`type = "HEALING"`), bit sets are name lists plus the exact legacy mask (`locations` + `equip_mask`, `modes` + `mode_mask`, `jobs` +
`job_mask`); integers above 2^53 become decimal strings. Per-level skill arrays are trimmed to `max_level` and omitted when all
zero. Script strings (`Script`, `EquipScript`, `UnEquipScript`, status `Script`, raw tables' `Script`/`Condition`) are carried
verbatim with `script_lang = "rathena"` on the entry; translation belongs to the legacy-npc unit.

Entry totals (rows in the merged legacy state; items include the loader's synthetic dummy item 499):

| | items | mobs | skills | statuses | jobs |
|---|---:|---:|---:|---:|---:|
| pre-renewal | 6,170 (6,169 + dummy) | 1,004 | 1,228 | 699 | 76 |
| renewal | 29,357 (29,356 + dummy) | 2,675 | 1,635 | 1,018 | 175 |

These equal the counts the real server holds after loading (dump hook) and the numbers in `AI_NATIVE_SPEC_ENGINE.md` 4.4.

## What the merge reproduces

`rmig/base.py` mirrors `YamlDatabase::load()` (`src/common/database.cpp`): header `Type`/`Version`/minimum-version check (a file that
fails it is skipped with its imports), `Header.Clear: true` clears the whole DB at that point, the file's `Body` entries in order,
then `Footer.Imports` in order, each recursively. `Mode` is compared with the compiled mode (`Renewal` build + `RENEWAL_ASPD` off: only
`job_aspd.yml` imports use `Prerenewal`), `Generator` imports are skipped unless enabled. The DB classes (`db_items.py`, `db_mobs.py`,
`db_skills.py`, `db_misc.py`) are line-by-line ports of each `parseBodyNode` and `loadingFinished`, including the quirks:

* an existing id is **patched per field**; fields absent from the node keep their value; a field that fails to parse aborts the
  entry **after** the earlier fields were applied (the patch stays); only a new id needs its mandatory nodes and gets defaults;
* scalar text is converted like rapidyaml/`asUInt16`/`asBool` (`Yes` is not a boolean, overflow is an error, `0x10` is 16);
  mappings keep duplicate keys because the legacy loops visit them (mob 20641 has `EP172ALPHA` twice -> `race2 = [29, 29]`);
* lists: mob `Drops`/`MvpDrops` use `Index` (overwrite, `Index == size` appends, `Index > size` ignored, `>= MAX` skipped, no Index
  appends, full list skips, an overwrite resets the drop's other fields, unknown item skips only that drop); `RaceGroups`, skill
  `Requires.Status`/`Equipment`/`Flags`, status `Fail`/`EndOn*` are `key: true/false` add/remove sets (status lists allow duplicates);
  skill level arrays accept a scalar (fills all 13) or `Level` lists (set listed levels, then linear extrapolation or last-value fill
  from the highest listed level, recomputed on every patch); job/refine/statpoint lists are keyed by `Level`;
* bit sets patch the existing mask (`Classes`, `Locations`, `Modes`, `Flags`, `CalcFlags`, `States`) but item `Jobs` resets the set
  first; mob `Ai` replaces the whole mode, `Modes` then patch bits (order matters);
* `loadingFinished`: item price derivation (only if the **last** node touching the item gave exactly one of Buy/Sell), sell-price exploit
  guard, `Delayconsume` -> `Usable`, weapon/armor level defaults, shield `View`, dummy item; mob `Class` mode bits, `adelay >= amotion`,
  renewal ATK rewrite (80 %/120 %); status `Script` forces `SCB_ALL`, the unconditional/conditional `SCS` rule (the legacy code resets the
  *conditional* flag); job HP/SP/AP tables and cumulative stat bonus; statpoint extrapolation;
* legacy bugs kept verbatim: skill `CopyFlags: Plagiarism: false` does `option &= FLAG` (clears every other bit), item `SubType` fallback
  overwritten by the unvalidated constant, status condition rule above.

Enum values are the real constants of the legacy build: `tools/extract_constants.py` builds `data/legacy_constants.json` (3,613 /
3,862 script constants and 96 compile-time limits per mode) from the dump hook, so nothing is guessed. Battle-config values the
loaders read (`base_exp_rate`, `monster_hp_rate`, ...) default to the shipped `conf/battle` values (the dump hook prints them);
`mob_drop_ratio_adjust` (item_rate_*, `drop_rateincrease`) is not modelled, it is the identity at the shipped defaults.

## Verification against the real loader

`legacy_dump/` adds a small hook to a **copy** of the tree (the repo is never modified): `legacy-dbdump-hook.patch` appends
`dbdump_hook.inc` to `src/map/script.cpp` and calls it in `do_init()` after `run_oninit()`; it prints canonical JSON of what the
loader actually holds (`item_db`, `mob_db`, `skill_db`, `status_db`, `size_fix_db`, `elemental_attribute_db`, `refine_db`,
`statpoint_db`, `job_db`) plus the script constants, limits and battle values.

```sh
RA_PRERENEWAL=1 legacy_dump/build_dump.sh $B/pre     # copy + patch + cmake + build, ~10 min (comments out #define RENEWAL in the copy)
legacy_dump/build_dump.sh $B/re
python3 -I tools/extract_constants.py --prerenewal <dump dir> --renewal <dump dir>   # only when the legacy constants change
python3 -I tools/verify_all.py --pre-build $B/pre --re-build $B/re                   # 6 dumps (3 s each) + compare, ~3 min
```

`legacy_dump/run_dump.sh <build> <out> [overlay]` runs the server headless (`--run-once`) with the overlay copied over the **copy's**
`db/import`; `tools/compare_legacy.py` compares every dumped field of every entry with the migrator. Three scenarios per mode:
`base` (shipped data), `patch` (`tests/fixtures/overlay_patch`: patch an existing item/skill/status/job/refine/statpoint field,
partial failure, duplicate AegisName, new items/mob/skill, mob `Index` patching with every boundary case, list extrapolation, rejected
entries), `clear` (`tests/fixtures/overlay_clear`: `Header.Clear` on mob_db, size_fix, attr_fix, status, statpoint).

Result (`out/verification_report.md`, full numbers there): **all 9 DBs, both modes, all three scenarios: every entry identical**, no
entry only on one side, 0 unexplained differences.

| DB (base scenario) | pre-re entries | pre-re identical | re entries | re identical | fields compared (pre-re / re) |
|---|---:|---:|---:|---:|---:|
| item_db | 6,170 | 6,170 | 29,357 | 29,357 | 333,180 / 1,614,635 |
| mob_db | 1,004 | 1,004 | 2,675 | 2,675 | 42,168 / 115,025 |
| skill_db | 1,228 | 1,228 | 1,635 | 1,635 | 69,996 / 93,195 |
| status | 699 | 699 | 1,018 | 1,018 | 11,883 / 17,306 |
| size_fix | 24 | 24 | 24 | 24 | 96 / 96 |
| attr_fix | 4 levels | 4 | 4 | 4 | 8 / 8 |
| statpoint | 275 | 275 | 275 | 275 | 825 / 825 |
| job_stats | 76 | 76 | 175 | 175 | 1,444 / 3,325 |
| refine | 70 refine-levels | 70 | 160 | 160 | 630 / 1,440 |

Discrepancy classes (every one is explained, none is a migrator bug in the final state):

| class | where | detail |
|---|---|---|
| loader quirk | `skill_db.unit_layout` (11 pre-re / 12 re skills) | `skill_init_unit_layout()` replaces the YAML `Layout: -1` ("special layout") by a hard-coded layout-table index; the migrator keeps `-1` and the predicate accepts exactly `-1 -> n >= 0` |
| field not stored by the YAML (derived) | `job_stats.max_param` | defaults come from `battle.conf` `max_*_parameter` per class family in `JobDatabase::loadingFinished`; only explicit `MaxStats` are migrated and compared |
| field not stored by the loader | item/status `Script`, `EquipScript`, `UnEquipScript` | the loader keeps compiled code, so only presence is comparable. A script made only of comments/whitespace/braces compiles to nothing (15 pre-re items, e.g. 12313): the comparison models that. The text itself is checked by the Luau round-trip test |
| loader quirk | mob `Name`/`AegisName`/`JapaneseName`/`Title` | `std::string::resize(24)` pads with NULs; compared after stripping them |
| loader quirk (derived) | renewal mob `atk`/`atk2` | `status_calc_misc` rewrites them to 80 %/120 % of `Attack`; the migrator applies the same rewrite and keeps the raw value as `attack` |

Bugs found in the migrator by this comparison and fixed (so the comparison is meaningful): missing `IT_ETC` default for items without
`Type`, constants table filtered case-sensitively (`Job_SuperNovice`), duplicate map keys dropped, sub-flag `RENEWAL_CAST` wrongly tied to
the mode, family tables of `calc_basehp/sp` by name instead of id, uint32 wrap of negative base SP.

**Not verified / not compared** (honest list): script text equality against the loader (not stored); `skill_db` fields filled by other
files (`skill_nocast_db.txt`, `skill_damage_db.txt`, `abra_db`, improvised-song rates); mob `skill` (mob_skill_db.txt), `vd` (mob_avail);
`mob_drop_ratio_adjust` with non-default `battle.conf`; any `battle.conf` other than the shipped one; `--no-renewal-aspd` and
`--generator` against the real server (unit-tested only); `db/import` templates other than the fixtures; the CSV-style databases
(`mob_skill_db.txt` 5,494 / 14,514 rows pre-re / re, `produce_db.txt`, `skill_nocast_db.txt`, `skill_damage_db.txt`,
`skill_changematerial_db.txt`, `item_noequip.txt`, `job_noenter_map.txt`, `status_disabled.txt`) are **not migrated at all yet**
(`sv_readdb` path, no YAML merge).

## pre-renewal vs renewal diff (`out/diff_pre_vs_re.md`)

Per DB: entries in both / only in re / only in pre-re, identical vs changed common entries, per-field change counts, and a verdict
column ("shared (rathena.base candidate)", "mostly shared", "ruleset-specific") that feeds the `rathena.base` decision. Loader-only
representation differences are normalised away (mode-specific `matk` column, `ITEMJ_ALL` 7 vs 127, renewal ATK rewrite), so a
difference listed there is a data difference. Summary (common entries identical): item_db 40 %, mob_db 0 %, skill_db 36 %, status 66 %,
size_fix 92 % (2 weapons differ), statpoint 38 %, refine/attr_fix/job_stats 0 %. Verdict column: `achievement_level_db` shared,
`abra_db` mostly shared; `mercenary_db`, `guild_skill_tree`, `instance_db` are 100 % identical on the common keys but renewal adds
20 / 4 / 74 entries (base + renewal overlay); `size_fix` and `quest_db` (86.5 % identical) are overlay candidates; everything
else (items, mobs, skills, statuses, jobs, refine, attr_fix, statpoint, exp tables) stays per ruleset.

## Tests

```sh
python3 -I -m unittest discover -s tests          # ~2 min; RMIG_FULL=1 adds the renewal full-database + Luau tests (~6 min)
```

`tests/`: `test_yamlio` (scalar typing, accessors), `test_chain` (Mode, order, nested imports, `Clear`, header/version failures, syntax
errors, Generator, the `job_aspd` rule, overlay dir), `test_items`, `test_mobs`, `test_skills_status` (every merge rule above on tiny
fixtures), `test_golden` (the migrator against snapshots of the real legacy loader in `fixtures/legacy_golden.json`, incl. both
overlays), `test_cli` (entry counts vs independent regex counts of the YAML, namespaced ids, **byte-identical output across runs with
a different `PYTHONHASHSEED`**, re-parse of the generated Luau with the Luau CLI and entry counts, verbatim script round trip,
malformed input -> exit 2 / `--strict` -> exit 1, nothing written next to the YAML), `test_misc` (Luau serializer round trip, diff report).
The Luau tests skip when the CLI is absent (`RMIG_LUAU` overrides the path).

## Q-D: do the other databases have custom merge rules? (answered by reading every `parseBodyNode`)

Yes, but all of them are **per-field patching of an existing key**; they differ in the key, the mandatory nodes on create and the
list handling. Ported and verified: item_db, mob_db, skill_db, status, job_stats (+aspd/exp/basepoints, same class), size_fix, attr_fix,
refine, statpoint. The rest below is read, not ported (emitted as raw tables merged by key with a shallow per-field update, an
**approximation** that is wrong where noted):

| DB | key | mandatory on create | list handling |
|---|---|---|---|
| quest_db | `Id` | `Title` | `Targets` and `Drops` are **appended** on every patch (no replace, no Index); `mobs_allowed` add/remove |
| pet_db | `Mob` (AegisName) | `EggItem`, `Fullness`, `CaptureRate` | evolution map keyed by target mob, requirements keyed by item |
| item_group_db | `Group` (`IG_`) | `SubGroup`+`List` per subgroup, `Item` per new entry | `SubGroups[].Clear: <id>` removes a subgroup; `List[].Index` keyed, `Clear: true` removes an entry; entry fields patched |
| item_combos | the item list itself | `Combos` | `Clear: true` deletes the combo; `Script` replaces |
| achievement_db | `Id` | `Name` | `Targets` keyed by id (`Clear` erases), `Dependent` add/remove |
| achievement_level, exp_guild, exp_homun | `Level` | `Level` + value | none |
| castle_db | `Id` | `Map`, `Name`, `Npc` | none |
| instance_db | `Id` | `Name`, `Enter` | `Enter`/`Maps` blocks patched per field |
| mercenary_db, elemental_db, homunculus_db | `Id`/`Class` | AegisName+Name (elemental also Size, Element, ElementLevel) | none (`Skills` keyed) |
| skill_tree | `Job` | none | `Inherit` add/remove by bool; `Tree` keyed by skill name, `Requires` keyed by skill (level 0 erases) |
| guild_skill_tree | `Id` | `MaxLevel` | `Required` keyed |
| mob_summon | `Group` | `Default` | `Summon` keyed by mob |
| abra_db, spellbook, magicmushroom, create_arrow | `Skill`/`Source` | per DB | none |
| stylist, barter, item_package, enchantgrade | `Index` lists | `Look`+`Options` / `Groups` / `Type`+`Levels` | Index patching like mob drops |
| item_randomopt_group | `Id` | `Group` | group name must stay unique (used by mob drops, ported minimally) |

`Header.Clear` is generic (every DB). `Index` appears in mob drops (ported), item_group, item_package, stylist, barter, enchantgrade and
mob_chat; nowhere else.

## Q-F: status `val1..val4` (not done here)

The status DB itself never names `val1..val4`. It gives per status: `Icon`, `DurationLookup` (skill for default duration), `States`,
`CalcFlags`, `Opt1/2/3`, `Options`, `Flags`, `MinRate`, `MinDuration`, `Fail`/`EndOnStart`/`EndReturn`/`EndOnEnd` lists and an optional
`Script`. Counts (pre-re / re): 699 / 1,018 statuses; 67 / 168 carry a `Script`; scripts read the values only through
`getstatus(SC_X, n)` with n = 1..4 = `val1..val4`: 64 / 153 statuses read `val1`, 10 / 33 read `val2`, 5 / 7 `val3`, 5 / 5 `val4`
(e.g. the four `ARMOR_ELEMENT_*` read all four). The meaning of each slot exists only in `status.cpp` (`status_change_start` per type)
and in the skill code, so the per-status field naming (BR-RULESET-051 / Q5) needs a table derived from those functions; this
migrator only carries the script text and the `legacy_sc` number.

## Open points / what remains

* CSV-style databases (list above) and a real port of the raw tables (quest_db first: Targets append rule) with the same dump-and-compare
  verification; `item_randomopt_db`, `mob_avail`, `mob_chat_db`, `map_drops`, `item_cash`, `laphine_*`, `reputation*`, `stylist` are not read at all.
* Script translation (legacy-npc unit); `script_lang = "rathena"` marks every carrier.
* Wiring `out/` into the packages (`ruleset.*` own `<mod>:kind/slug`; shared content per the diff report), the id_map reservation
  for `client_id`s, and the slug collision policy (currently `_<legacy id>` suffix).
* Speed: the pure-Python YAML composer dominates (renewal 50 s). A C++ port in `tooling/` can reuse `tests/fixtures/legacy_golden.json`,
  `data/legacy_constants.json` and the dump hook as its acceptance oracle.
