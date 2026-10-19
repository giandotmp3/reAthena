--!nonstrict
-- Unit rules of the stat functions that no status triggers (generated draft, hand-reviewed).
local status = import("scripts/status/define")
local K = import("scripts/status/consts")
local O = import("scripts/status/ops")

status.rule("hp_fix", { at = 3184, op = "add", when = function(s, e) return e.unit.type == "pc" and e.skill("SU_SPRITEMABLE") > 0 end, v = 1000 })
status.rule("hp_fix", { at = 3186, op = "add", when = function(s, e) return e.unit.type == "pc" and e.skill("SU_POWEROFSEA") > 0 end, v = 1000 })
status.rule("sp_fix", { at = 3348, op = "add", when = function(s, e) return e.unit.type == "pc" and e.skill("SU_SPRITEMABLE") > 0 end, v = 100 })
status.rule("sp_fix", { at = 3350, op = "add", when = function(s, e) return e.unit.type == "pc" and e.skill("SU_POWEROFSEA") > 0 end, v = 100 })
status.rule("speed", { at = 8074, op = "gset", group = "mount", when = function(s, e) return e.sc.FUSION == nil and e.unit.type == "pc" and (O.band(e.unit.option, K.OPTION_RIDING) ~= 0 or O.band(e.unit.option, K.OPTION_DRAGON) ~= 0) end, v = 25 })
status.rule("speed", { at = 8076, op = "gset", group = "mount", when = function(s, e) return e.sc.FUSION == nil and e.unit.type == "pc" and not (O.band(e.unit.option, K.OPTION_RIDING) ~= 0 or O.band(e.unit.option, K.OPTION_DRAGON) ~= 0) and O.band(e.unit.option, K.OPTION_WUGRIDER) ~= 0 end, v = function(s, e) return 15 + (5 * e.skill("RA_WUGRIDER")) end })
status.rule("speed", { at = 8159, op = "gmax", group = "slow", when = function(s, e) return not ((e.unit.type == "pc" and e.sc.HIDING ~= nil) and e.skill("RG_TUNNELDRIVE") > 0) and not ((e.unit.type == "pc" and e.sc.CHASEWALK ~= nil) and e.sc.CHASEWALK.speed_value < 0) and e.unit.type == "pc" and e.unit.bonus_speed_rate + e.unit.bonus_speed_add_rate > 0 end, v = function(s, e) return e.unit.bonus_speed_rate + e.unit.bonus_speed_add_rate end })
status.rule("speed", { at = 8177, op = "gmax", group = "haste", when = function(s, e) return e.unit.type == "pc" and (O.band(e.unit.class_, K.MAPID_SECONDMASK)) == K.MAPID_ASSASSIN and e.skill("TF_MISS") > 0 end, v = function(s, e) return 1 * e.skill("TF_MISS") end })
status.rule("speed", { at = 8221, op = "gmax", group = "haste", when = function(s, e) return e.unit.type == "pc" and e.unit.bonus_speed_rate + e.unit.bonus_speed_add_rate < 0 end, v = function(s, e) return -(e.unit.bonus_speed_rate + e.unit.bonus_speed_add_rate) end })
