"""mob_db: port of MobDatabase::parseBodyNode / parseDropNode / loadingFinished (src/map/mob.cpp)."""
from .base import Applier
from .yamlio import Reject, has

MIN_MOB_DB, MAX_MOB_DB, MIN_MOB_DB2, MAX_MOB_DB2 = 1000, 3999, 20020, 31999


class RandOptGroupNames(Applier):
    """Only what mob_db needs from item_randomopt_group.yml: Id <-> Group name (RandomOptionGroupDatabase::parseBodyNode)."""
    NAME = "item_randomopt_group"
    TYPE = "RANDOM_OPTION_GROUP"
    VERSION = 1
    MIN_VERSION = 1
    STUB = "db/item_randomopt_group.yml"

    def parse_body_node(self, node):
        r = self.r
        gid = r.u16(node, "Id")
        row = self.rows.get(gid)
        exists = row is not None
        if not exists:
            r.need(node, ["Group"])
            row = {"id": gid, "name": ""}
        if has(node, "Group"):
            name = r.s(node, "Group")
            if row["name"] != name and any(x["name"] == name for x in self.rows.values()):
                r.warn("Found duplicate random option group name for %s, skipping." % name)
                raise Reject("dup_group")
            row["name"] = name
        if not exists:
            self.rows[gid] = row
        return True

    def get_id(self, name):
        for g, x in self.rows.items():
            if x["name"] == name:
                return g
        return None


class MobDb(Applier):
    NAME = "mob_db"
    TYPE = "MOB_DB"
    VERSION = 5
    MIN_VERSION = 1
    STUB = "db/mob_db.yml"

    def __init__(self, world, items, randopt):
        Applier.__init__(self, world)
        self.items = items
        self.randopt = randopt
        c = self.c
        self.MAX_DROP = c.lim("MAX_MOB_DROP")
        self.MAX_MVP = c.lim("MAX_MVP_DROP")
        self.deftype_min = c.lim("DEFTYPE_MIN")
        self.deftype_max = c.lim("DEFTYPE_MAX")
        self.max_exp = c.lim("MAX_EXP")

    def _new(self, mob_id):
        c = self.c
        amotion = c.lim("MAX_ASPD_NOPC") // c.lim("AMOTION_DIVIDER_NOPC")
        m = {
            "id": mob_id, "sprite": "", "name": "", "jname": "", "lv": 1, "max_hp": 1, "max_sp": 1,
            "base_exp": 0, "job_exp": 0, "mexp": 0, "atk": 0, "atk2": 0, "def": 0, "mdef": 0, "res": 0, "mres": 0,
            "str": 1, "agi": 1, "vit": 1, "int": 1, "dex": 1, "luk": 1, "range": 0, "range2": 0, "range3": 0,
            "size": 0, "race": 0, "race2": [], "def_ele": 0, "ele_lv": 1, "speed": c.lim("DEFAULT_WALK_SPEED"),
            "adelay": c.lim("MAX_ASPD_NOPC"), "amotion": amotion, "clientamotion": max(1, min(amotion, 65535)),
            "dmotion": 0, "damagetaken": 100, "group_id": 0, "title": "", "mode": c.get("MONSTER_TYPE_06"),
            "class": 0, "mvpdrops": [], "drops": [],
        }
        if self.w.renewal:
            m["matk"] = 0
        return m

    def _cap_name(self, what, name):
        # name.resize(NAME_LENGTH): truncated to 23 + padded with NULs which std::string keeps; c_str comparisons stop
        # at the first NUL, the dump (std::string) keeps the padding. Normalised form is the truncated text.
        if len(name) > 24:
            self.r.warn('%s "%s" exceeds maximum of 23 characters, capping...' % (what, name))
        return name[:24]

    def _drops(self, name, node, maxn, drops):
        r = self.r
        for dn in (node[name] if isinstance(node[name], list) else []):
            if has(dn, "Index"):
                idx = r.u16(dn, "Index")
                if idx >= maxn:
                    r.warn("Invalid monster %s index %d. Must be between 0~%d, skipping." % (name, idx, maxn - 1))
                    continue
                if idx == len(drops):
                    drop = {}
                    exist = False
                elif idx > len(drops):
                    continue
                else:
                    drop = drops[idx]
                    exist = True
            else:
                if len(drops) >= maxn:
                    r.warn("Maximum of %d monster %s met, skipping." % (maxn, name))
                    continue
                drop = {}
                exist = False
            iname = r.s(dn, "Item")
            item = self.items.search_aegis(iname)
            if item is None:
                r.warn("Monster %s item %s does not exist, skipping." % (name, iname))
                continue
            rate = r.u16_rate(dn, "Rate")
            steal = False
            if has(dn, "StealProtected"):
                steal = r.b(dn, "StealProtected")
            group = 0
            if has(dn, "RandomOptionGroup"):
                gname = r.s(dn, "RandomOptionGroup")
                g = self.randopt.get_id(gname)
                if g is None:
                    r.warn("Unknown random option group %s for monster %s, defaulting to no group." % (gname, name))
                else:
                    group = g
            drop["nameid"] = item["id"]
            drop["rate"] = rate
            drop["steal"] = steal
            drop["rndopt"] = group
            if not exist:
                drops.append(drop)

    def parse_body_node(self, node):
        r, c = self.r, self.c
        mob_id = r.u32(node, "Id")
        self._cur_key = mob_id
        if not ((mob_id > MIN_MOB_DB and mob_id < MAX_MOB_DB) or (mob_id > MIN_MOB_DB2 and mob_id < MAX_MOB_DB2)):
            r.warn("Invalid monster ID %d, must be in range %d-%d or %d-%d." % (mob_id, MIN_MOB_DB, MAX_MOB_DB, MIN_MOB_DB2, MAX_MOB_DB2))
            raise Reject("id_range")
        mob = self.rows.get(mob_id)
        exists = mob is not None
        if not exists:
            r.need(node, ["AegisName", "Name"])
            mob = self._new(mob_id)
        bexp = self.c.battle
        if has(node, "AegisName"):
            mob["sprite"] = self._cap_name("AegisName", r.s(node, "AegisName"))
        if has(node, "Name"):
            mob["name"] = self._cap_name("Name", r.s(node, "Name"))
        if has(node, "JapaneseName"):
            mob["jname"] = self._cap_name("JapaneseName", r.s(node, "JapaneseName"))
        elif not exists:
            mob["jname"] = mob["name"]
        if has(node, "Level"):
            mob["lv"] = r.u16(node, "Level")
        if has(node, "Hp"):
            mob["max_hp"] = r.u32(node, "Hp")
        if has(node, "Sp"):
            mob["max_sp"] = r.u32(node, "Sp")
        mx = self.max_exp
        if has(node, "BaseExp"):
            v = r.u64(node, "BaseExp")
            mob["base_exp"] = int(max(0, min(mx, float(v) * float(bexp["base_exp_rate"]) / 100.0)))
        if has(node, "JobExp"):
            v = r.u64(node, "JobExp")
            mob["job_exp"] = int(max(0, min(mx, float(v) * float(bexp["job_exp_rate"]) / 100.0)))
        if has(node, "MvpExp"):
            v = r.u64(node, "MvpExp")
            mob["mexp"] = int(max(0, min(mx, float(v) * float(bexp["mvp_exp_rate"]) / 100.0)))
        if has(node, "Attack"):
            mob["atk"] = r.u16(node, "Attack")
        if has(node, "Attack2"):
            v = r.u16(node, "Attack2")
            if self.w.renewal:
                mob["matk"] = v
            else:
                mob["atk2"] = v
        for key, fld in (("Defense", "def"), ("MagicDefense", "mdef")):
            if has(node, key):
                v = r.u16(node, key)
                if v < self.deftype_min or v > self.deftype_max:
                    r.warn("Invalid monster defense %d, capping..." % v)
                    v = max(self.deftype_min, min(self.deftype_max, v))
                mob[fld] = v
        if has(node, "Resistance"):
            mob["res"] = r.u16(node, "Resistance")
        if has(node, "MagicResistance"):
            mob["mres"] = r.u16(node, "MagicResistance")
        for key, fld in (("Str", "str"), ("Agi", "agi"), ("Vit", "vit"), ("Int", "int"), ("Dex", "dex"), ("Luk", "luk")):
            if has(node, key):
                mob[fld] = r.u16(node, key)      # stored in int16: max(0, uint16) -> wraps above 32767
                if mob[fld] > 0x7FFF:
                    mob[fld] -= 0x10000
        if has(node, "AttackRange"):
            mob["range"] = r.u16(node, "AttackRange")
        if has(node, "SkillRange"):
            mob["range2"] = r.u16(node, "SkillRange")
        if has(node, "ChaseRange"):
            mob["range3"] = r.u16(node, "ChaseRange")
        if has(node, "Size"):
            s = r.s(node, "Size")
            k = c.get("Size_" + s)
            if k is None:
                r.warn("Unknown monster size %s, defaulting to Small." % s)
                k = c.get("SZ_SMALL") if c.get("SZ_SMALL") is not None else 0
            if k < 0 or k > 2:
                r.warn("Invalid monster size %s, defaulting to Small." % s)
                k = 0
            mob["size"] = k
        if has(node, "Race"):
            s = r.s(node, "Race")
            k = c.get("RC_" + s)
            if k is None:
                r.warn("Unknown monster race %s, defaulting to Formless." % s)
                k = c.get("RC_FORMLESS")
            if not (k > c.get("RC_NONE_") and k < c.lim("RC_MAX")):
                r.warn("Invalid monster race %s, defaulting to Formless." % s)
                k = c.get("RC_FORMLESS")
            mob["race"] = k
        if has(node, "RaceGroups"):
            rn = node["RaceGroups"]
            for k, _v in r.items(rn):
                kc = c.get("RC2_" + k)
                if kc is None:
                    r.warn("Unknown monster race group %s, skipping." % k)
                    continue
                if kc <= c.get("RC2_NONE") or kc >= c.lim("RC2_MAX"):
                    r.warn("Invalid monster race group %s, skipping." % k)
                    continue
                a = r.b(rn, k)
                if a:
                    mob["race2"].append(kc)
                else:
                    if kc in mob["race2"]:
                        mob["race2"].remove(kc)   # util::vector_erase_if_exists removes the first match
        if has(node, "Element"):
            s = r.s(node, "Element")
            k = c.get("ELE_" + s)
            if k is None:
                r.warn("Unknown monster element %s, defaulting to Neutral." % s)
                k = c.get("ELE_NEUTRAL")
            if not (k > c.get("ELE_NONE") and k < c.lim("ELE_MAX")):
                r.warn("Invalid monster element %s, defaulting to Neutral." % s)
                k = c.get("ELE_NEUTRAL")
            mob["def_ele"] = k
        if has(node, "ElementLevel"):
            v = r.u16(node, "ElementLevel")
            if not (1 <= v <= c.lim("MAX_ELE_LEVEL")):
                r.warn("Invalid monster element level %d, defaulting to 1." % v)
                v = 1
            mob["ele_lv"] = v
        if has(node, "WalkSpeed"):
            v = r.u16(node, "WalkSpeed")
            lo, hi = c.lim("MIN_WALK_SPEED"), c.lim("MAX_WALK_SPEED")
            if v < lo or v > hi:
                r.warn("Invalid monster walk speed %d, capping..." % v)
                v = max(lo, min(hi, v))
            mob["speed"] = v
        if has(node, "AttackDelay"):
            v = r.u16(node, "AttackDelay")
            # cap_value(speed, MAX_ASPD_NOPC, MIN_ASPD)
            mob["adelay"] = max(c.lim("MAX_ASPD_NOPC"), min(c.lim("MIN_ASPD"), v))
        if has(node, "AttackMotion"):
            v = r.u16(node, "AttackMotion")
            mob["amotion"] = max(1, min(c.lim("MIN_ASPD") // c.lim("AMOTION_DIVIDER_NOPC"), v))
        if has(node, "ClientAttackMotion"):
            v = r.u16(node, "ClientAttackMotion")
            mob["clientamotion"] = max(1, min(65535, v))
        elif not exists:
            mob["clientamotion"] = max(1, min(65535, mob["amotion"]))
        if has(node, "DamageMotion"):
            v = r.u16(node, "DamageMotion")
            rate = bexp["monster_damage_delay_rate"]
            if rate != 100:
                v = v * rate // 100
            mob["dmotion"] = v
        if has(node, "DamageTaken"):
            mob["damagetaken"] = r.u16_rate(node, "DamageTaken", 100)
        if has(node, "GroupId"):
            mob["group_id"] = r.u16_rate(node, "GroupId")
        if has(node, "Title"):
            mob["title"] = self._cap_name("Title", r.s(node, "Title"))
        if has(node, "Ai"):
            s = r.s(node, "Ai")
            k = c.get("MONSTER_TYPE_" + s)
            if k is None:
                r.warn("Unknown monster AI %s, defaulting to 06." % s)
                k = c.get("MONSTER_TYPE_06")
            if k < 0 or k > c.lim("MD_MASK"):
                r.warn("Invalid monster AI %s, defaulting to 06." % s)
                k = c.get("MONSTER_TYPE_06")
            mob["mode"] = k
        if has(node, "Class"):
            s = r.s(node, "Class")
            k = c.get("CLASS_" + s)
            if k is None:
                r.warn("Unknown monster class %s, defaulting to Normal." % s)
                k = c.get("CLASS_NORMAL")
            if k < c.get("CLASS_NORMAL") or k > c.get("CLASS_EVENT"):
                r.warn("Invalid monster class %s, defaulting to Normal." % s)
                k = c.get("CLASS_NORMAL")
            mob["class"] = k
        if has(node, "Modes"):
            mn = node["Modes"]
            for k, _v in r.items(mn):
                kc = c.get("MD_" + k)
                if kc is None:
                    r.warn("Unknown monster mode %s, skipping." % k)
                    continue
                if kc < 0 or kc > c.get("MD_SKILLIMMUNE"):
                    r.warn("Invalid monster mode %s, skipping." % k)
                    continue
                a = r.b(mn, k)
                if a:
                    mob["mode"] |= kc
                else:
                    mob["mode"] &= ~kc
        if has(node, "MvpDrops"):
            self._drops("MvpDrops", node, self.MAX_MVP, mob["mvpdrops"])
        if has(node, "Drops"):
            self._drops("Drops", node, self.MAX_DROP, mob["drops"])
        if not exists:
            self.rows[mob_id] = mob
        return True

    def finish(self):
        """MobDatabase::loadingFinished (+ mob_drop_ratio_adjust with default battle.conf: identity, rate 0 removed)."""
        c = self.c
        cls = {k: c.get("CLASS_" + k) for k in ("BOSS", "GUARDIAN", "BATTLEFIELD", "EVENT")}
        md = lambda n: c.get("MD_" + n)
        mvp_bit = md("MVP")
        for mob in self.rows.values():
            k = mob["class"]
            if k == cls["BOSS"]:
                mob["mode"] |= (md("DETECTOR") | md("STATUSIMMUNE") | md("KNOCKBACKIMMUNE"))
            elif k == cls["GUARDIAN"]:
                mob["mode"] |= md("STATUSIMMUNE")
            elif k == cls["BATTLEFIELD"]:
                mob["mode"] |= (md("STATUSIMMUNE") | md("SKILLIMMUNE"))
            elif k == cls["EVENT"]:
                mob["mode"] |= md("FIXEDITEMDROP")
            b = self.c.battle
            if b["view_range_rate"] != 100:
                mob["range2"] = max(1, mob["range2"] * b["view_range_rate"] // 100)
            if b["chase_range_rate"] != 100:
                mob["range3"] = max(mob["range2"], mob["range3"] * b["chase_range_rate"] // 100)
            mob["adelay"] = max(mob["adelay"], mob["amotion"])
            mob["_attack"] = mob["atk"]                  # raw YAML Attack (renewal rewrites atk/atk2 below)
            if self.w.renewal:                           # status_calc_misc: status_base_atk_max/min for monsters
                a = mob["atk"]
                mob["atk2"] = (a * 120 // 100) & 0xFFFF
                mob["atk"] = (a * 80 // 100) & 0xFFFF
            if not b["monster_active_enable"]:
                mob["mode"] &= ~md("AGGRESSIVE")
            maxhp = float(mob["max_hp"])
            if mob["mode"] & mvp_bit:
                if b["mvp_hp_rate"] != 100:
                    maxhp = maxhp * float(b["mvp_hp_rate"]) / 100.0
            else:
                if b["monster_hp_rate"] != 100:
                    maxhp = maxhp * float(b["monster_hp_rate"]) / 100.0
            mob["max_hp"] = max(1, min(0xFFFFFFFF, int(maxhp)))
            mob["max_sp"] = max(1, min(0xFFFFFFFF, mob["max_sp"]))
            for key in ("mvpdrops", "drops"):
                mob[key] = [d for d in mob[key] if d["nameid"] != 0 and d["rate"] != 0]
