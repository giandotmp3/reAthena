--!strict
-- Domain model for ruleset.renewal. Pure data shapes; all values are per-call immutable snapshots.
-- Numeric fidelity helpers (int64 div, float32 round) come from script-host (BR-RULESET-115), not here.

export type Stage = string

export type UnitSnapshot = {
  base_lv: number,
  str: number, agi: number, vit: number, int: number, dex: number, luk: number,
  mdef: number, res: number, mres: number, p_atk: number, s_matk: number,
  crate: number, is_player: boolean,
}

export type Effect = { kind: string, args: { [string]: any } }

-- Hooks return deltas plus an ordered effect queue (BR-RULESET-116/117); never mutate the world.
export type StageResult = { damage: number, effects: { Effect } }

export type PackageConfig = {
  mode: "renewal",
  subflags: { cast: boolean, drop: boolean, exp: boolean, lvdmg: boolean, aspd: boolean, stat: boolean, edp: boolean },
  allow_unsupported_subflags: boolean,
}

return {}
