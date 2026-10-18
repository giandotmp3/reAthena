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
