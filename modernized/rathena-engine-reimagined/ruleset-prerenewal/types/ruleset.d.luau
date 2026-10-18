-- Domain model types for ruleset.prerenewal (checked by luau-analyze in CI).
export type Manifest = {
	mode: "pre-renewal",
	db_set: "db/pre-re",
	subflags: { cast: boolean, drop: boolean, exp: boolean, lvdmg: boolean, aspd: boolean, stat: boolean, edp: boolean },
	battle_defaults: { [string]: number | boolean },
	legacy_quirks: boolean,
}
export type FieldType = "int" | "double" | "bool" | "string"
export type Contribution = { op: "add" | "mul_pct" | "override", fn: (self: any) -> number }
export type StatusDef = {
	id: string, client_id: number, icon: number?,
	fields: { [string]: FieldType }, contributes: { [string]: Contribution },
	states: { string }, flags: { string }, fail: { string }, end_on_start: { string },
	resist: string?,
}
export type SkillDef = {
	id: string, client_id: number, max_level: number, type: "weapon" | "magic" | "misc",
	target: string, sp: { number }, hooks: { string },
}
export type BonusDelta = { attr: string, op: "add" | "mul_pct" | "override", value: number }
export type ItemDef = {
	id: string, client_id: number, type: string, heal_hp: { number }?, attack: number?,
	defense: number?, weapon_level: number?, slots: number?, equip_bonus: { BonusDelta }?,
}
-- Effects are returned by calculations, applied by native code (BR-RULESET-116).
export type Effect = { kind: string, [string]: any }
export type DamageCtx = { damage: number, div: number, element: string, effects: { Effect }, crit: boolean }
export type RuleCase = {
	rule: string, fn: string, mode: "PRE" | "B", pri: string, title: string,
	example: string?, given: { [string]: any }, expected: { [string]: any }?,
}

-- Weapon pipeline (scripts/damage/weapon.luau). The engine's native ctx implements this; tests use tests/support/fake_ctx.
export type Rng = { rnd: () -> number, value: (lo: number, hi: number) -> number }
export type Lane = {
	get: (self: Lane) -> number, set: (self: Lane, v: number) -> (), add: (self: Lane, v: number) -> (),
	mul_pct: (self: Lane, p: number) -> (), add_pct: (self: Lane, p: number) -> (), clamp_min: (self: Lane, lo: number) -> (),
}
export type Ctx = {
	rng: Rng,
	idiv: (a: number, b: number) -> number,        -- int64, truncates toward zero (C++), never `//`
	mul_div: (v: number, m: number, d: number) -> number,
	acc: (v: number) -> Lane,
}
export type Hand = { atk: number, atk2: number, ele: number, wlv: number }
export type Unit = {
	kind: "pc" | "mob", level: number, str: number, agi: number, vit: number, int: number, dex: number, luk: number,
	batk: number, hit: number, cri: number, flee: number, flee2: number, def: number, def2: number,
	size: number, race: number, element: number, ele_lv: number, boss: boolean, targeted: number,
	rhw: Hand, lhw: Hand?, wtype1: number, wtype2: number, arrow: boolean, job: string?, no_sizefix: boolean?,
	skills: { [string]: number }?, bonus: { [string]: any }?, sc: { [string]: { val1: number?, val2: number?, val3: number?, val4: number? } }?,
}
export type WeaponResult = { damage: number, damage2: number, type: number, div: number, flag: number, dmg_lv: number }
