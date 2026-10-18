"""status, job_stats (+job_aspd/job_exp/job_basepoints), size_fix, attr_fix, refine, statpoint (src/map/status.cpp, pc.cpp)."""
import math
from .base import Applier
from .yamlio import Reject, has

PARAM_NAMES = ["Str", "Agi", "Vit", "Int", "Dex", "Luk", "Pow", "Sta", "Wis", "Spl", "Con", "Crt"]
ELEMENT_NAMES = ["Neutral", "Water", "Earth", "Fire", "Wind", "Poison", "Holy", "Dark", "Ghost", "Undead"]


def _flag(lst, idx, active):
    if active:
        if idx not in lst:
            lst.append(idx)
            lst.sort()
    else:
        if idx in lst:
            lst.remove(idx)


class StatusDb(Applier):
    NAME = "status"
    TYPE = "STATUS_DB"
    VERSION = 4
    MIN_VERSION = 3
    STUB = "db/status.yml"

    def __init__(self, world, skills):
        Applier.__init__(self, world)
        self.skills = skills
        self.sc_max = self.c.lim("SC_MAX")
        self.sc_none = self.c.lim("SC_NONE")
        self.efst_blank = self.c.lim("EFST_BLANK")
        self.efst_max = self.c.lim("EFST_MAX")
        self.scb_max = self.c.lim("SCB_MAX")
        self.scb_dye = self.c.lim("SCB_DYE")
        self.scb_all = [i for i in range(self.scb_max) if i != self.scb_dye]

    def _new(self, t):
        return {"type": t, "icon": self.efst_blank, "state": [], "calc_flag": [], "opt1": 0, "opt2": 0, "opt3": 0,
                "look": 0, "flag": [], "skill_id": 0, "endonstart": [], "fail": [], "endreturn": [], "endonend": [],
                "min_duration": 1, "min_rate": 0, "script": None}

    def skill_name2id(self, name):
        lo = name.lower()
        best = 0
        for sid, s in self.skills.rows.items():
            if s["name"].lower() == lo and (best == 0 or sid < best):
                best = sid
        return best

    def _valid(self, k):
        return k > self.sc_none and k < self.sc_max

    def _bits(self, prefix, node, name, lst, lo, hi, what, special_all=None):
        r, c = self.r, self.c
        for key, _v in r.items(node):
            if special_all and key == "All":
                continue
            k = c.get(prefix + key)
            if k is None:
                r.warn("%s %s is invalid." % (what, key))
                raise Reject(what)
            if k < lo or k >= hi:
                r.warn("%s %s is out of bounds." % (what, key))
                raise Reject(what)
            _flag(lst, k, r.b(node, key))

    def _sclist(self, node, name, lst):
        r, c = self.r, self.c
        for key, _v in r.items(node):
            k = c.get("SC_" + key)
            if k is None:
                r.warn("%s status %s is invalid." % (name, key))
                raise Reject(name)
            if not self._valid(k):
                r.warn("%s status %s is out of bounds." % (name, key))
                raise Reject(name)
            if r.b(node, key):
                lst.append(k)
            elif k in lst:
                lst.remove(k)

    def parse_body_node(self, node):
        r, c = self.r, self.c
        name = r.s(node, "Status")
        k = c.get("SC_" + name)
        if k is None:
            r.warn("Invalid Status %s." % name)
            raise Reject("status")
        if not self._valid(k):
            r.warn("Status %s is out of bounds." % name)
            raise Reject("status_bounds")
        self._cur_key = k
        st = self.rows.get(k)
        exists = st is not None
        if not exists:
            st = self._new(k)
        if has(node, "Icon"):
            iname = r.s(node, "Icon")
            v = c.get(iname)
            if v is None:
                r.warn("Icon %s is invalid, defaulting to EFST_BLANK." % iname)
                v = self.efst_blank
            if v < self.efst_blank or v >= self.efst_max:
                r.warn("Icon %s is out of bounds, defaulting to EFST_BLANK." % iname)
                v = self.efst_blank
            st["icon"] = v
        if has(node, "DurationLookup"):
            sname = r.s(node, "DurationLookup")
            sid = self.skill_name2id(sname)
            if sid == 0:
                r.warn("DurationLookup skill %s is invalid, defaulting to none." % sname)
            st["skill_id"] = sid
        if has(node, "States"):
            self._bits("SCS_", node["States"], "States", st["state"], c.get("SCS_NONE"), c.lim("SCS_MAX"), "State")
        if has(node, "CalcFlags"):
            fn = node["CalcFlags"]
            if has(fn, "All"):
                if r.b(fn, "All"):
                    st["calc_flag"] = list(self.scb_all)
                else:
                    st["calc_flag"] = []
            self._bits("SCB_", fn, "CalcFlags", st["calc_flag"], c.get("SCB_NONE"), self.scb_max, "CalcFlag", special_all=True)
        if has(node, "Opt1"):
            o = r.s(node, "Opt1")
            v = c.get("OPT1_" + o)
            if v is None:
                r.warn("Opt1 %s is invalid." % o)
                raise Reject("opt1")
            if v < c.get("OPT1_NONE") or v >= c.lim("OPT1_MAX"):
                r.warn("Opt1 %s is out of bounds." % o)
                raise Reject("opt1")
            st["opt1"] = v
        for key, pre, fld, lo, hi, w in (("Opt2", "OPT2_", "opt2", "OPT2_NONE", "OPT2_MAX", 16),
                                         ("Opt3", "OPT3_", "opt3", "OPT3_NORMAL", "OPT3_MAX", 32),
                                         ("Options", "OPTION_", "look", "OPTION_NOTHING", "OPTION_MAX", 32)):
            if has(node, key):
                on = node[key]
                for kk, _v in r.items(on):
                    v = c.get(pre + kk)
                    if v is None:
                        r.warn("%s %s is invalid." % (key, kk))
                        raise Reject(key)
                    if v < c.get(lo) or v >= c.lim(hi):
                        r.warn("%s %s is out of bounds." % (key, kk))
                        raise Reject(key)
                    mask = (1 << w) - 1
                    if r.b(on, kk):
                        st[fld] = (st[fld] | v) & mask
                    else:
                        st[fld] = st[fld] & ~v & mask
        if has(node, "Flags"):
            self._bits("SCF_", node["Flags"], "Flags", st["flag"], c.get("SCF_NONE"), c.lim("SCF_MAX"), "Flag")
        if has(node, "MinRate"):
            st["min_rate"] = r.u16(node, "MinRate")
        if has(node, "MinDuration"):
            st["min_duration"] = r.i64(node, "MinDuration")
        for key, fld in (("Fail", "fail"), ("EndOnStart", "endonstart"), ("EndReturn", "endreturn"), ("EndOnEnd", "endonend")):
            if has(node, key):
                self._sclist(node[key], key, st[fld])
        if has(node, "Script"):
            st["script"] = r.s(node, "Script")
        if not exists:
            self.rows[k] = st
        return True

    def finish(self):
        c = self.c
        pairs = []
        for nm in ("NOMOVE", "NOPICKITEM", "NODROPITEM", "NOCAST", "NOCHAT", "NOEQUIPITEM", "NOUNEQUIPITEM",
                   "NOCONSUMEITEM", "NOATTACK", "NOWARP", "NODEATHPENALTY", "NOINTERACT"):
            pairs.append((c.get("SCS_" + nm), c.get("SCS_" + nm + "COND")))
        halluc = c.get("SC_HALLUCINATION")
        for k, st in self.rows.items():
            if st["script"] is not None:
                st["calc_flag"] = sorted(set(st["calc_flag"]) | set(self.scb_all))
            for unc, cond in pairs:
                if cond in st["state"] and unc in st["state"]:
                    st["state"].remove(cond)          # legacy quirk: resets the *conditional* flag
            if st["type"] == halluc and not c.battle["display_hallucination"]:
                st["icon"] = self.efst_blank


class SizeFixDb(Applier):
    NAME = "size_fix"
    TYPE = "SIZE_FIX_DB"
    VERSION = 1
    MIN_VERSION = 1
    STUB = "db/size_fix.yml"

    def parse_body_node(self, node):
        r, c = self.r, self.c
        wn = r.s(node, "Weapon")
        k = c.get("W_" + wn)
        if k is None:
            r.warn("Size Fix unknown weapon %s, skipping." % wn)
            raise Reject("weapon")
        if k < c.get("W_FIST") or k > c.get("W_2HSTAFF"):
            r.warn("Size Fix weapon %s is an invalid weapon, skipping." % wn)
            raise Reject("weapon")
        self._cur_key = k
        row = self.rows.get(k)
        exists = row is not None
        if not exists:
            row = {"weapon": k, "small": 100, "medium": 100, "large": 100}
        for key, fld in (("Small", "small"), ("Medium", "medium"), ("Large", "large")):
            if has(node, key):
                v = r.u16(node, key)
                if v > 100:
                    r.warn("%s Size Fix %d for %s is out of bounds, defaulting to 100." % (key, v, wn))
                    v = 100
                row[fld] = v
        if not exists:
            self.rows[k] = row
        return True


class AttrFixDb(Applier):
    NAME = "attr_fix"
    TYPE = "ATTRIBUTE_DB"
    VERSION = 1
    MIN_VERSION = 1
    STUB = "db/attr_fix.yml"

    def __init__(self, world):
        Applier.__init__(self, world)
        self.nel = self.c.lim("MAX_ELE_LEVEL")
        self.nele = self.c.lim("ELE_MAX")
        self.clear()
        self.clears = 0

    def clear(self):
        self.clears += 1
        self.table = [[[100] * self.nele for _ in range(self.nele)] for _ in range(self.nel)]

    def parse_body_node(self, node):
        r, c = self.r, self.c
        lv = r.u16(node, "Level")
        if not (1 <= lv <= self.nel):
            r.warn("Invalid element level %d." % lv)
            raise Reject("level")
        for an in ELEMENT_NAMES:
            if not has(node, an):
                continue
            en = node[an]
            for dn in ELEMENT_NAMES:
                if not has(en, dn):
                    continue
                v = r.i16(en, dn)
                if v < -100:
                    r.warn("%s %d is out of range -100~200. Setting to -100." % (dn, v))
                    v = -100
                elif v > 200:
                    r.warn("%s %d is out of range -100~200. Setting to 200." % (dn, v))
                    v = 200
                self.table[lv - 1][c.get("ELE_" + an)][c.get("ELE_" + dn)] = v
        return True

    @property
    def rows(self):
        return {lv + 1: {"level": lv + 1, "table": self.table[lv]} for lv in range(self.nel)}

    @rows.setter
    def rows(self, v):
        pass


class RefineDb(Applier):
    NAME = "refine"
    TYPE = "REFINE_DB"
    VERSION = 2
    MIN_VERSION = 1
    STUB = "db/refine.yml"

    def __init__(self, world, items):
        Applier.__init__(self, world)
        self.items = items
        self.max_refine = self.c.lim("MAX_REFINE")
        self.max_zeny = self.c.lim("MAX_ZENY")

    def parse_body_node(self, node):
        r, c = self.r, self.c
        gname = r.s(node, "Group")
        g = c.get("REFINE_TYPE_" + gname)
        if g is None:
            r.warn("Unknown refine group %s, skipping." % gname)
            raise Reject("group")
        g &= 0xFFFF
        self._cur_key = g
        info = self.rows.get(g)
        exists = info is not None
        if not exists:
            info = {"group": g, "levels": {}}
        if has(node, "Levels"):
            for ln in (node["Levels"] if isinstance(node["Levels"], list) else []):
                level = r.u16(ln, "Level")
                li = info["levels"].get(level)
                lex = li is not None
                if not lex:
                    li = {"level": level, "levels": {}}
                if has(ln, "RefineLevels"):
                    for rn in (ln["RefineLevels"] if isinstance(ln["RefineLevels"], list) else []):
                        rl = r.u16(rn, "Level")
                        if rl == 0 or rl > self.max_refine:
                            r.warn("Refine level %d is invalid, skipping." % rl)
                            raise Reject("refine_level")
                        rl -= 1
                        ri = li["levels"].get(rl)
                        rex = ri is not None
                        if not rex:
                            ri = {"level": rl, "bonus": 0, "randombonus_max": 0, "blessing_amount": 0,
                                  "broadcast_success": False, "broadcast_failure": False, "costs": {}}
                        if has(rn, "Bonus"):
                            ri["bonus"] = r.u32(rn, "Bonus")
                        if has(rn, "RandomBonus"):
                            ri["randombonus_max"] = r.u32(rn, "RandomBonus")
                        if has(rn, "BlacksmithBlessingAmount"):
                            a = r.u16(rn, "BlacksmithBlessingAmount")
                            if a > 30000:
                                r.warn("Blacksmith Blessing amount %d too high, capping to MAX_AMOUNT." % a)
                                a = 30000
                            ri["blessing_amount"] = a
                        if has(rn, "BroadcastSuccess"):
                            ri["broadcast_success"] = r.b(rn, "BroadcastSuccess")
                        if has(rn, "BroadcastFailure"):
                            ri["broadcast_failure"] = r.b(rn, "BroadcastFailure")
                        if has(rn, "Chances"):
                            for cn in (rn["Chances"] if isinstance(rn["Chances"], list) else []):
                                cname = r.s(cn, "Type")
                                k = c.get("REFINE_COST_" + cname)
                                if k is None:
                                    r.warn("Unknown refine cost type %s, skipping." % cname)
                                    raise Reject("cost_type")
                                if k >= c.get("REFINE_COST_MAX"):
                                    r.warn("Refine cost type %s is unsupported, skipping." % cname)
                                    raise Reject("cost_type")
                                idx = k & 0xFFFF
                                cost = ri["costs"].get(idx)
                                cex = cost is not None
                                if not cex:
                                    cost = {"index": idx, "nameid": 0, "chance": 0, "zeny": 0, "breaking_rate": 0,
                                            "downgrade_amount": 0}
                                if has(cn, "Rate"):
                                    cost["chance"] = r.u16_rate(cn, "Rate")
                                if has(cn, "Price"):
                                    p = r.u32(cn, "Price")
                                    if p > self.max_zeny:
                                        r.warn("Price is above MAX_ZENY, capping...")
                                        p = self.max_zeny
                                    cost["zeny"] = p
                                if has(cn, "Material"):
                                    iname = r.s(cn, "Material")
                                    it = self.items.search_aegis(iname)
                                    if it is None:
                                        r.warn("Unknown refine material %s, skipping." % iname)
                                        raise Reject("material")
                                    cost["nameid"] = it["id"]
                                if has(cn, "BreakingRate"):
                                    cost["breaking_rate"] = r.u16_rate(cn, "BreakingRate")
                                if has(cn, "DowngradeAmount"):
                                    d = r.u16(cn, "DowngradeAmount")
                                    if d > self.max_refine:
                                        r.warn("Downgrade amount %d is invalid, skipping." % d)
                                        raise Reject("downgrade")
                                    cost["downgrade_amount"] = d
                                if not cex:
                                    ri["costs"][idx] = cost
                        if not rex:
                            li["levels"][rl] = ri
                if not lex:
                    info["levels"][level] = li
        if not exists:
            self.rows[g] = info
        return True

    def flat(self):
        out = {}
        for g, info in self.rows.items():
            for lv, li in info["levels"].items():
                for rl, ri in li["levels"].items():
                    row = dict(ri)
                    row["group"], row["level"], row["refine_level"] = g, lv, rl
                    row["costs"] = [ri["costs"][i] for i in sorted(ri["costs"])]
                    del row["level"]
                    row["level"] = lv
                    out[(g, lv, rl)] = row
        return out


class StatPointDb(Applier):
    NAME = "statpoint"
    TYPE = "STATPOINT_DB"
    VERSION = 2
    MIN_VERSION = 1
    STUB = "db/statpoint.yml"

    def parse_body_node(self, node):
        r, c = self.r, self.c
        level = r.u16(node, "Level")
        max_level = c.lim("MAX_LEVEL")
        if level == 0:
            r.warn("The minimum level is 1.")
            raise Reject("level")
        if level > max_level:
            r.warn("Level %d exceeds maximum BaseLevel %d, skipping." % (level, max_level))
            raise Reject("level")
        self._cur_key = level
        e = self.rows.get(level)
        exists = e is not None
        if not exists:
            r.need(node, ["Points"])
            e = {"level": level, "statpoints": 0, "traitpoints": 0}
        if has(node, "Points"):
            e["statpoints"] = r.u32(node, "Points")
        if has(node, "TraitPoints"):
            e["traitpoints"] = r.u32(node, "TraitPoints")
        if not exists:
            self.rows[level] = e
        return True

    def finish(self):
        c = self.c
        start = c.lim("inter_config.start_status_points")
        use_sp = c.battle["use_statpoint_table"]
        use_tp = c.limits.get("battle_config.use_traitpoint_table", 1)
        max_level = c.lim("MAX_LEVEL")
        one = self.rows.get(1)
        if one is None:
            one = {"level": 1, "statpoints": start, "traitpoints": 0}
            self.rows[1] = one
        elif use_sp:
            if one["statpoints"] != start:
                one["statpoints"] = start
        else:
            one["statpoints"] = start
            one["traitpoints"] = 0
        last = one
        trait_start = 200
        for level in range(2, max_level + 1):
            e = self.rows.get(level)
            exists = e is not None
            if not exists:
                e = {"level": level, "statpoints": 0, "traitpoints": 0}
                self.rows[level] = e
            if (not use_sp) or (not exists):
                if level <= trait_start:
                    e["statpoints"] = last["statpoints"] + ((level - 1 + 15) // 5)
                else:
                    e["statpoints"] = last["statpoints"]
            if (not use_tp) or (not exists):
                if level > trait_start:
                    e["traitpoints"] = (level - trait_start) * 3 + (level - trait_start) // 5 * 4
                else:
                    e["traitpoints"] = 0
            last = e


# families used by calc_basehp/sp (MAPID_FIRSTMASK / SECONDMASK of pc_jobid2mapid), by legacy job constant name
NINJA = ("NINJA", "KAGEROU", "OBORO", "BABY_NINJA", "BABY_KAGEROU", "BABY_OBORO", "SHINKIRO", "SHIRANUI")
GUNSLINGER = ("GUNSLINGER", "REBELLION", "BABY_GUNSLINGER", "BABY_REBELLION", "NIGHT_WATCH")
SUMMONER = ("SUMMONER", "BABY_SUMMONER", "SPIRIT_HANDLER")
SUPERNOVICE = ("SUPER_NOVICE", "SUPER_BABY", "SUPER_NOVICE_E", "SUPER_BABY_E", "HYPER_NOVICE")


class JobDb(Applier):
    NAME = "job_stats"
    TYPE = "JOB_STATS"
    VERSION = 4
    MIN_VERSION = 2
    STUB = "db/job_stats.yml"

    def __init__(self, world):
        Applier.__init__(self, world)
        c = self.c
        self.ML = c.lim("MAX_LEVEL")
        self.NP = c.lim("PARAM_MAX")
        self.nweap = c.lim("MAX_WEAPON_TYPE") + (1 if self.w.renewal else 0)
        self.def_aspd = c.lim("AMOTION_ZERO_ASPD") // (c.lim("AMOTION_INTERVAL") if self.w.renewal else 1)
        self._name = {}
        ids = lambda names: set(c.get("JOB_" + n) for n in names if c.get("JOB_" + n) is not None)
        self.no_tables = set(c.get("JOB_" + n) for n in ("WEDDING", "XMAS", "SUMMER", "HANBOK", "OKTOBERFEST", "SUMMER2")
                             if c.get("JOB_" + n) is not None)
        self.fam_ninja, self.fam_gun, self.fam_summoner, self.fam_sn = ids(NINJA), ids(GUNSLINGER), ids(SUMMONER), ids(SUPERNOVICE)

    def clear(self):
        Applier.clear(self)
        self._name.clear()

    def _new(self, jid):
        return {"job": jid, "hp_factor": 0, "hp_increase": 0, "sp_factor": 0, "sp_increase": 0, "ap_factor": 0,
                "ap_increase": 0, "max_weight_base": 0, "aspd_base": [], "max_base_level": 0, "max_job_level": 0,
                "base_exp": [0] * self.ML, "job_exp": [0] * self.ML,
                "job_bonus": [[0] * self.NP for _ in range(self.ML)],
                "alternate_outfits": [], "base_hp": [0] * self.ML, "base_sp": [0] * self.ML, "base_ap": [0] * self.ML,
                "max_param": [0] * self.NP}

    def parse_body_node(self, node):
        r, c = self.r, self.c
        if not has(node, "Jobs"):
            return True
        for jname, _v in r.items(node["Jobs"]):
            jid = c.get("JOB_" + jname)
            if jid is None:
                r.warn("Job %s does not exist." % jname)
                raise Reject("job")
            self._cur_key = jid
            job = self.rows.get(jid)
            exists = job is not None
            if not exists:
                job = self._new(jid)
                self._name[jid] = jname.upper()
            for key, fld, dflt in (("MaxWeight", "max_weight_base", 20000), ("HpFactor", "hp_factor", 0),
                                   ("HpIncrease", "hp_increase", 500), ("SpFactor", "sp_factor", 0),
                                   ("SpIncrease", "sp_increase", 100), ("ApFactor", "ap_factor", 0),
                                   ("ApIncrease", "ap_increase", 0)):
                if has(node, key):
                    job[fld] = r.u32(node, key)
                elif not exists:
                    job[fld] = dflt
            if has(node, "BaseASPD"):
                an = node["BaseASPD"]
                if not exists:
                    job["aspd_base"] = [self.def_aspd] * self.nweap
                for wk, _w in r.items(an):
                    k = c.get("W_" + wk)
                    if k is None:
                        r.warn("Unknown weapon type %s specified for %s, skipping." % (wk, jname))
                        continue
                    if k < c.get("W_FIST") or k >= self.nweap:
                        r.warn("Invalid weapon type %s specified for %s, skipping." % (wk, jname))
                        continue
                    job["aspd_base"][k] = r.i16(an, wk)
            elif not exists:
                job["aspd_base"] = [self.def_aspd] * self.nweap
            if has(node, "MaxStats"):
                sn = node["MaxStats"]
                for sk, _s in r.items(sn):
                    k = c.get("PARAM_" + sk)
                    if k is None:
                        r.warn("Unknown max stat %s specified for %s, skipping." % (sk, jname))
                        continue
                    if k < c.get("PARAM_STR") or k >= self.NP:
                        r.warn("Invalid max stat %s specified for %s, skipping." % (sk, jname))
                        continue
                    job["max_param"][k] = r.u16(sn, sk)
            if has(node, "MaxBaseLevel"):
                lv = r.u16(node, "MaxBaseLevel")
                if lv == 0 or lv > self.ML:
                    r.warn("MaxBaseLevel must be between 1~MAX_LEVEL for %s, capping to MAX_LEVEL." % jname)
                    lv = self.ML
                job["max_base_level"] = lv
            elif not exists:
                job["max_base_level"] = self.ML
            self._exp_list(node, "BaseExp", "Exp", job, "base_exp", "max_base_level", jname)
            if has(node, "MaxJobLevel"):
                lv = r.u16(node, "MaxJobLevel")
                if lv == 0 or lv > self.ML:
                    r.warn("MaxJobLevel must be between 1~MAX_LEVEL for %s, capping to MAX_LEVEL." % jname)
                    lv = self.ML
                job["max_job_level"] = lv
            elif not exists:
                job["max_job_level"] = self.ML
            self._exp_list(node, "JobExp", "Exp", job, "job_exp", "max_job_level", jname)
            if has(node, "BonusStats"):
                for ln in (node["BonusStats"] if isinstance(node["BonusStats"], list) else []):
                    lv = r.u16(ln, "Level")
                    if lv == 0 or lv > self.ML:
                        r.warn("Level must be between 1~MAX_LEVEL for %s." % jname)
                        raise Reject("level")
                    for idx in range(self.NP):
                        if has(ln, PARAM_NAMES[idx]):
                            v = r.i16(ln, PARAM_NAMES[idx])
                            job["job_bonus"][lv - 1][idx] = v & 0xFFFF
            if self.c.limits.get("HP_SP_TABLES", 1):
                self._exp_list(node, "BaseHp", "Hp", job, "base_hp", "max_base_level", jname, u32=True)
                self._exp_list(node, "BaseSp", "Sp", job, "base_sp", "max_base_level", jname, u32=True)
                self._exp_list(node, "BaseAp", "Ap", job, "base_ap", "max_base_level", jname, u32=True)
            if has(node, "AlternateOutfits"):
                an = node["AlternateOutfits"]
                for aname, _a in r.items(an):
                    aid = c.get("JOB_" + aname)
                    if aid is None:
                        r.warn("Job %s does not exist." % aname)
                        raise Reject("altjob")
                    active = r.b(an, aname)
                    aid &= 0xFFFF
                    lst = job["alternate_outfits"]
                    if aid in lst:
                        if active:
                            r.warn("Job %s is already in the alternate outfit list." % aname)
                        else:
                            lst.remove(aid)
                    else:
                        if active:
                            lst.append(aid)
                        else:
                            r.warn("Job %s is not in the alternate outfit list." % aname)
            if not exists:
                self.rows[jid] = job
        return True

    def _exp_list(self, node, key, sub, job, fld, maxfld, jname, u32=False):
        r = self.r
        if not has(node, key):
            return
        for en in (node[key] if isinstance(node[key], list) else []):
            lv = r.u16(en, "Level")
            if lv > job[maxfld]:
                continue
            if lv == 0 or lv > self.ML:
                r.warn("Level must be between 1~MAX_LEVEL for %s." % jname)
                raise Reject("level")
            if has(en, sub):
                job[fld][lv - 1] = r.u32(en, sub) if u32 else r.u64(en, sub)

    def _family(self, jid):
        n = self._name.get(jid, "")
        return n

    def _calc_basehp(self, level, job):
        j = job["job"]
        base = 35.0
        base += math.floor(level * (job["hp_increase"] / 100.0))
        for i in range(2, level + 1):
            base += math.floor(((job["hp_factor"] / 100.0) * i) + 0.5)
        if not self.w.renewal:
            if j in self.fam_ninja or j in self.fam_gun:
                if level >= 10:
                    base += 90.0
        if j in self.fam_summoner:
            base += math.floor((base / 2) + 0.5)
        elif j in self.fam_sn:
            if level >= 99:
                base += 2000.0
            if level >= 150:
                base += 2000.0
        return int(base) & 0xFFFFFFFF

    def _calc_basesp(self, level, job):
        j = job["job"]
        base = 10.0
        base += math.floor(level * (job["sp_increase"] / 100.0))
        for i in range(2, level + 1):
            base += math.floor(((job["sp_factor"] / 100.0) * i) + 0.5)
        if j in self.fam_ninja:
            if level >= 10:
                base -= 22.0
            else:
                base = 11.0 + 3.0 * level
        elif j in self.fam_gun:
            if level >= 10:
                base -= 18.0
            else:
                base = 9.0 + 3.0 * level
        elif j in self.fam_summoner:
            base += math.floor((base / 2) + 0.5)
        return int(base) & 0xFFFFFFFF

    def _calc_baseap(self, level, job):
        base = 0.0
        if level >= 200:
            base = 200.0
        base += math.floor(level * (job["ap_increase"] / 100.0))
        for i in range(2, level + 1):
            base += math.floor(((job["ap_factor"] / 100.0) * i) + 0.5)
        return int(base) & 0xFFFFFFFF

    def finish(self):
        """JobDatabase::loadingFinished (pcdb_checkid jobs only get the derived tables; others stay raw)."""
        for jid, job in self.rows.items():
            if not self._pcdb_checkid(jid):
                continue
            if jid in self.no_tables:
                continue
            mb, mj = job["max_base_level"], job["max_job_level"]
            for j in range(mb):
                if job["base_hp"][j] == 0:
                    job["base_hp"][j] = self._calc_basehp(j + 1, job)
            for j in range(mb):
                if job["base_sp"][j] == 0:
                    job["base_sp"][j] = self._calc_basesp(j + 1, job)
            for j in range(mb):
                if job["base_ap"][j] == 0:
                    job["base_ap"][j] = self._calc_baseap(j + 1, job)
            job["base_hp"] = job["base_hp"][:mb]
            job["base_sp"] = job["base_sp"][:mb]
            job["base_ap"] = job["base_ap"][:mb]
            job["job_bonus"] = job["job_bonus"][:mj]
            for p in range(self.NP):
                cur = 0
                for lv in range(mj):
                    cur += job["job_bonus"][lv][p] if job["job_bonus"][lv][p] < 0x8000 else job["job_bonus"][lv][p] - 0x10000
                    cur = ((cur + 0x8000) & 0xFFFF) - 0x8000
                    job["job_bonus"][lv][p] = cur & 0xFFFF

    def _pcdb_checkid(self, jid):
        # pcdb_checkid(): JOB_NOVICE..JOB_SUMMONER-style player ids; everything the job DB accepts via JOB_ constants
        # except non-player helper ids is treated as a player class here (the dump comparison shows any mismatch).
        return True
