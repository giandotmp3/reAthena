"""skill_db: port of SkillDatabase::parseBodyNode / parseNode (src/map/skill.cpp)."""
from .base import Applier
from .yamlio import Reject, has

ARR_FIELDS = ("range", "num", "element", "splash", "maxcount", "blewcount", "cast", "delay", "walkdelay", "upkeep_time",
              "upkeep_time2", "cooldown", "req_hp", "req_mhp", "req_sp", "req_ap", "req_hp_rate", "req_sp_rate",
              "req_ap_rate", "req_zeny", "req_ammo_qty", "req_spiritball", "giveap", "unit_layout", "unit_range")


class SkillDb(Applier):
    NAME = "skill_db"
    TYPE = "SKILL_DB"
    VERSION = 4
    MIN_VERSION = 4
    STUB = "db/skill_db.yml"

    def __init__(self, world, items):
        Applier.__init__(self, world)
        self.items = items
        c = self.c
        self.L = c.lim("MAX_SKILL_LEVEL")
        self.NI = c.lim("MAX_SKILL_ITEM_REQUIRE")
        self.sc_max = c.lim("SC_MAX")
        self.sc_none = c.lim("SC_NONE")

    def _new(self, sid):
        L = self.L
        s = {"id": sid, "name": "", "desc": "", "max": 0, "skill_type": 0, "inf": 0, "nk": [], "inf2": [],
             "hit": 0, "copy_option": 0, "copy_req_opt": 0, "nonear_range": 0, "nonear_type": 0, "castcancel": False,
             "cast_def_rate": 0, "castnodex": 0, "delaynodex": 0, "req_weapon": 0, "req_ammo": 0, "req_state": 0,
             "req_itemid": [0] * self.NI, "req_amount": [0] * self.NI, "req_eqitem": [], "req_status": [],
             "req_item_level_dep": False, "unit_id": 0, "unit_id2": 0, "unit_interval": 0, "unit_target": 0,
             "unit_flag": [], "sc": 0}
        for f in ARR_FIELDS:
            s[f] = [0] * L
        s["fixed_cast"] = [0] * L          # RENEWAL_CAST is a separate define, on in both shipped builds
        return s

    # --- parseNode ---
    def _arr(self, name, sub, node, arr):
        r = self.r
        L = len(arr)
        sk = node[name]
        if not isinstance(sk, list):
            v = r.i32(node, name)
            for i in range(L):
                arr[i] = v
            return
        max_level = 0
        for it in sk:
            try:
                lv = r.u16(it, "Level")
            except Reject:
                continue
            if lv > self.L:
                r.warn("%s Level exceeds the maximum skill level of %d, skipping." % (name, self.L))
                raise Reject("level")
            try:
                v = r.i32(it, sub)
            except Reject:
                continue
            if lv == 0:
                raise Reject("level0")
            arr[lv - 1] = v
            max_level = max(max_level, lv)
        i = max_level
        step = 1
        while step <= i // 2:
            diff = arr[i - 1] - arr[i - step - 1]
            j = i - 1
            while j >= step:
                if (arr[j] - arr[j - step]) != diff:
                    break
                j -= 1
            if j >= step:
                step += 1
                continue
            while i < self.L:
                arr[i] = arr[i - step] + diff
                if arr[i] < 1 and arr[i - 1] >= 0:
                    arr[i] = 1
                    diff = 0
                    step = 1
                i += 1
            return
        if max_level == 0:
            return
        while i < L:
            arr[i] = arr[max_level - 1]
            i += 1

    def _flagset(self, lst, idx, active):
        if active:
            if idx not in lst:
                lst.append(idx)
                lst.sort()
        else:
            if idx in lst:
                lst.remove(idx)

    def parse_body_node(self, node):
        r, c = self.r, self.c
        sid = r.u16(node, "Id")
        self._cur_key = sid
        sk = self.rows.get(sid)
        exists = sk is not None
        if not exists:
            r.need(node, ["Name", "Description", "MaxLevel"])
            sk = self._new(sid)
        if has(node, "Name"):
            n = r.s(node, "Name")
            if len(n) > 40:
                r.warn('Name "%s" exceeds maximum length of 40.' % n)
                raise Reject("name_len")
            sk["name"] = n
        if has(node, "Description"):
            n = r.s(node, "Description")
            if len(n) > 40:
                r.warn('Description "%s" exceeds maximum length of 40.' % n)
                raise Reject("desc_len")
            sk["desc"] = n
        if has(node, "MaxLevel"):
            v = r.u16(node, "MaxLevel")
            if v == 0 or v > self.L:
                r.warn("MaxLevel %d does not meet the bounds of 1~%d." % (v, self.L))
                raise Reject("maxlevel")
            sk["max"] = v
        if has(node, "Type"):
            t = r.s(node, "Type")
            k = c.get("BF_" + t)
            if k is None:
                r.warn("Type %s is invalid." % t)
                raise Reject("type")
            if k < c.get("BF_NONE") or k > c.get("BF_MISC"):
                r.warn("Constant Type %s is not a supported skill type." % t)
                raise Reject("type")
            sk["skill_type"] = k
        elif not exists:
            sk["skill_type"] = c.get("BF_NONE")
        if has(node, "TargetType"):
            t = r.s(node, "TargetType")
            k = c.get("INF_" + t + "_SKILL")
            if k is None:
                r.warn("TargetType %s is invalid." % t)
                raise Reject("targettype")
            sk["inf"] = k & 0xFFFF
        if has(node, "DamageFlags"):
            dn = node["DamageFlags"]
            for key, _v in r.items(dn):
                k = c.get("NK_" + key)
                if k is None:
                    r.warn("DamageFlags %s is invalid." % key)
                    raise Reject("damageflag")
                self._flagset(sk["nk"], k & 0xFF, r.b(dn, key))
        if has(node, "Flags"):
            fn = node["Flags"]
            for key, _v in r.items(fn):
                k = c.get("INF2_" + key)
                if k is None:
                    r.warn("Flag %s is invalid, skipping." % key)
                    continue
                self._flagset(sk["inf2"], k & 0xFF, r.b(fn, key))
        for key, fld in (("Range", "range"),):
            if has(node, key):
                self._arr("Range", "Size", node, sk["range"])
        if has(node, "Hit"):
            t = r.s(node, "Hit")
            k = c.get("DMG_" + t)
            if k is None:
                r.warn("Hit %s is invalid." % t)
                raise Reject("hit")
            sk["hit"] = k
        elif not exists:
            sk["hit"] = c.get("DMG_NORMAL")
        if has(node, "HitCount"):
            self._arr("HitCount", "Count", node, sk["num"])
        if has(node, "Element"):
            en = node["Element"]
            if not isinstance(en, list):
                e = r.s(node, "Element")
                k = c.get("ELE_" + e)
                if k is None:
                    r.warn("Element %s is invalid." % e)
                    raise Reject("element")
                if k == c.get("ELE_NONE"):
                    r.warn("%s is not a valid element." % e)
                    raise Reject("element")
                sk["element"] = [k] * self.L
            else:
                for it in en:
                    try:
                        lv = r.u16(it, "Level")
                    except Reject:
                        continue
                    if lv > self.L:
                        r.warn("Element Level exceeds the maximum skill level of %d, skipping." % self.L)
                        raise Reject("level")
                    try:
                        e = r.s(it, "Element")
                    except Reject:
                        continue
                    k = c.get("ELE_" + e)
                    if k is None:
                        r.warn("Element %s is invalid." % e)
                        raise Reject("element")
                    if k == c.get("ELE_NONE"):
                        r.warn("%s is not a valid element." % e)
                        raise Reject("element")
                    if lv == 0:
                        raise Reject("level0")
                    sk["element"][lv - 1] = k
        elif not exists:
            sk["element"] = [c.get("ELE_NEUTRAL")] * self.L
        for key, sub, fld in (("SplashArea", "Area", "splash"), ("ActiveInstance", "Max", "maxcount"),
                              ("Knockback", "Amount", "blewcount")):
            if has(node, key):
                self._arr(key, sub, node, sk[fld])
        if has(node, "CopyFlags"):
            cn = node["CopyFlags"]
            plag = c.get("SKILL_COPY_PLAGIARISM")
            repro = c.get("SKILL_COPY_REPRODUCE")
            if has(cn, "Skill"):
                csn = cn["Skill"]
                if has(csn, "Plagiarism"):
                    a = r.b(csn, "Plagiarism")
                    if a:
                        sk["copy_option"] |= plag
                    else:
                        sk["copy_option"] &= plag        # legacy quirk: '&=' without '~'
                if has(csn, "Reproduce"):
                    a = r.b(csn, "Reproduce")
                    if a:
                        sk["copy_option"] |= repro
                    else:
                        sk["copy_option"] &= repro       # legacy quirk
            else:
                r.warn("CopyFlags requires a Skill copy type.")
                raise Reject("copyflags")
            if has(cn, "RemoveRequirement"):
                rn = cn["RemoveRequirement"]
                for key, _v in r.items(rn):
                    k = c.get("SKILL_REQ_" + key)
                    if k is None:
                        r.warn("CopyFlags RemoveRequirement %s is invalid." % key)
                        raise Reject("copyreq")
                    sk["copy_req_opt"] |= k              # legacy: the boolean value is not read
            elif not exists:
                sk["copy_req_opt"] = 0
        if has(node, "NoNearNPC"):
            nn = node["NoNearNPC"]
            if has(nn, "AdditionalRange"):
                sk["nonear_range"] = r.u16(nn, "AdditionalRange")
            if has(nn, "Type"):
                tn = nn["Type"]
                for key, _v in r.items(tn):
                    k = c.get("SKILL_NONEAR_" + key)
                    if k is None:
                        r.warn("NoNearNPC Type %s is invalid." % key)
                        raise Reject("nonear")
                    a = r.b(tn, key)
                    if a:
                        sk["nonear_type"] |= k
                    else:
                        sk["nonear_type"] &= ~k & 0xFFFF
        if has(node, "CastCancel"):
            sk["castcancel"] = r.b(node, "CastCancel")
        elif not exists:
            sk["castcancel"] = True
        if has(node, "CastDefenseReduction"):
            sk["cast_def_rate"] = r.u16(node, "CastDefenseReduction")
        for key, fld in (("CastTime", "cast"), ("AfterCastActDelay", "delay"), ("AfterCastWalkDelay", "walkdelay"),
                         ("Duration1", "upkeep_time"), ("Duration2", "upkeep_time2"), ("Cooldown", "cooldown")):
            if has(node, key):
                self._arr(key, "Time", node, sk[fld])
        if has(node, "FixedCastTime"):
            self._arr("FixedCastTime", "Time", node, sk["fixed_cast"])
        for key, fld in (("CastTimeFlags", "castnodex"), ("CastDelayFlags", "delaynodex")):
            if has(node, key):
                fn = node[key]
                for k2, _v in r.items(fn):
                    k = c.get("SKILL_CAST_" + k2)
                    if k is None:
                        r.warn("%s %s option is invalid." % (key, k2))
                        raise Reject("castflag")
                    a = r.b(fn, k2)
                    if a:
                        sk[fld] = (sk[fld] | k) & 0xFF
                    else:
                        sk[fld] = sk[fld] & ~k & 0xFF
        if has(node, "Requires"):
            self._requires(node["Requires"], sk, exists)
        if has(node, "GiveAp"):
            self._arr("GiveAp", "Amount", node, sk["giveap"])
        if has(node, "Unit"):
            self._unit(node["Unit"], sk, exists)
        if has(node, "Status"):
            t = r.s(node, "Status")
            k = c.get("SC_" + t)
            if k is None:
                r.warn("Status %s is invalid." % t)
                raise Reject("status")
            if k < self.sc_none or k >= self.sc_max:
                r.warn("Status %s is unknown. Defaulting to SC_NONE." % t)
                k = self.sc_none
            sk["sc"] = k
        elif not exists:
            sk["sc"] = self.sc_none
        if not exists:
            self.rows[sid] = sk
        return True

    def _requires(self, rn, sk, exists):
        r, c = self.r, self.c
        for key, fld in (("HpCost", "req_hp"), ("SpCost", "req_sp"), ("ApCost", "req_ap"), ("HpRateCost", "req_hp_rate"),
                         ("SpRateCost", "req_sp_rate"), ("ApRateCost", "req_ap_rate"), ("MaxHpTrigger", "req_mhp"),
                         ("ZenyCost", "req_zeny")):
            if has(rn, key):
                self._arr(key, "Amount", rn, sk[fld])
        if has(rn, "Weapon"):
            wn = rn["Weapon"]
            if has(wn, "All"):
                if r.b(wn, "All"):
                    sk["req_weapon"] = 0
            else:
                for key, _v in r.items(wn):
                    k = c.get("W_" + key)
                    if k is None:
                        r.warn("Requires Weapon %s is invalid." % key)
                        raise Reject("req_weapon")
                    a = r.b(wn, key)
                    if a:
                        sk["req_weapon"] |= 1 << k
                    else:
                        sk["req_weapon"] &= ~(1 << k)
        if has(rn, "Ammo"):
            an = rn["Ammo"]
            if has(an, "None"):
                if r.b(an, "None"):
                    sk["req_ammo"] = 0
            else:
                for key, _v in r.items(an):
                    k = c.get("AMMO_" + key)
                    if k is None:
                        r.warn("Requires Ammo %s is invalid." % key)
                        raise Reject("req_ammo")
                    a = r.b(an, key)
                    if a:
                        sk["req_ammo"] |= 1 << k
                    else:
                        sk["req_ammo"] &= ~(1 << k)
        if has(rn, "AmmoAmount"):
            if sk["req_ammo"] == 0:
                r.warn("An ammo type is required before specifying ammo amount.")
                raise Reject("ammo_amount")
            self._arr("AmmoAmount", "Amount", rn, sk["req_ammo_qty"])
        if has(rn, "State"):
            t = r.s(rn, "State")
            k = c.get("ST_" + t)
            if k is None:
                r.warn("Requires State %s is invalid." % t)
                raise Reject("req_state")
            sk["req_state"] = k
        if has(rn, "Status"):
            stn = rn["Status"]
            for key, _v in r.items(stn):
                k = c.get("SC_" + key)
                if k is None:
                    r.warn("Requires Status %s is invalid." % key)
                    raise Reject("req_status")
                a = r.b(stn, key)
                if a and k not in sk["req_status"]:
                    sk["req_status"].append(k)
                elif (not a) and k in sk["req_status"]:
                    sk["req_status"].remove(k)
        if has(rn, "SpiritSphereCost"):
            self._arr("SpiritSphereCost", "Amount", rn, sk["req_spiritball"])
        if has(rn, "ItemCost"):
            count = 0
            ic = rn["ItemCost"]
            for it in (ic if isinstance(ic, list) else []):
                try:
                    iname = r.s(it, "Item")
                except Reject:
                    continue
                item = self.items.search_aegis(iname)
                if item is None:
                    r.warn("Requires ItemCost Item %s does not exist." % iname)
                    raise Reject("itemcost")
                try:
                    amount = r.i32(it, "Amount")
                except Reject:
                    continue
                if has(it, "Level"):
                    try:
                        lv = r.u16(it, "Level")
                    except Reject:
                        continue
                    if lv < 1 or lv > sk["max"]:
                        r.warn("Requires ItemCost Level %d is not within the level range of 1~%d." % (lv, sk["max"]))
                        raise Reject("itemcost_level")
                    count = lv - 1
                    if not sk["req_item_level_dep"]:
                        sk["req_item_level_dep"] = True
                if count >= self.NI:
                    r.warn("ItemCost index %d exceeds MAX_SKILL_ITEM_REQUIRE (legacy would overflow)." % count)
                    raise Reject("itemcost_overflow")
                sk["req_itemid"][count] = item["id"]
                sk["req_amount"][count] = amount
                count += 1
        if has(rn, "Equipment"):
            en = rn["Equipment"]
            for key, _v in r.items(en):
                item = self.items.search_aegis(key)
                if item is None:
                    r.warn("Requires Equipment %s does not exist." % key)
                    raise Reject("req_equip")
                a = r.b(en, key)
                if a and item["id"] not in sk["req_eqitem"]:
                    sk["req_eqitem"].append(item["id"])
                elif (not a) and item["id"] in sk["req_eqitem"]:
                    sk["req_eqitem"].remove(item["id"])

    def _unit(self, un, sk, exists):
        r, c = self.r, self.c
        if has(un, "Id"):
            t = r.s(un, "Id")
            k = c.get("UNT_" + t)
            if k is None:
                r.warn("Unit Id %s is invalid." % t)
                raise Reject("unit_id")
            sk["unit_id"] = k & 0xFFFF
        else:
            r.warn("Unit requires an Id.")
            raise Reject("unit_id")
        if has(un, "AlternateId"):
            t = r.s(un, "AlternateId")
            k = c.get("UNT_" + t)
            if k is None:
                r.warn("Alternate Unit Id %s is invalid." % t)
                raise Reject("unit_id2")
            sk["unit_id2"] = k & 0xFFFF
        if has(un, "Layout"):
            self._arr("Layout", "Size", un, sk["unit_layout"])
        if has(un, "Range"):
            self._arr("Range", "Size", un, sk["unit_range"])
        if has(un, "Interval"):
            sk["unit_interval"] = r.i16(un, "Interval")
        if has(un, "Target"):
            t = r.s(un, "Target")
            k = c.get("BCT_" + t)
            if k is None:
                r.warn("Unit Target %s is invalid." % t)
                raise Reject("unit_target")
            sk["unit_target"] = k
        elif not exists:
            sk["unit_target"] = c.get("BCT_ALL")
        bl_char, bl_pc, bl_mob, bl_skill = c.get("BL_CHAR"), c.get("BL_PC"), c.get("BL_MOB"), c.get("BL_SKILL")
        if has(un, "Flag"):
            fn = un["Flag"]
            for key, _v in r.items(fn):
                k = c.get("UF_" + key)
                if k is None:
                    r.warn("Skill Unit Flag %s is invalid." % key)
                    raise Reject("unit_flag")
                self._flagset(sk["unit_flag"], k & 0xFF, r.b(fn, key))
            uf = lambda n: c.get("UF_" + n) in sk["unit_flag"]
            if uf("NOENEMY") and c.battle["defnotenemy"]:
                sk["unit_target"] = c.get("BCT_NOENEMY")
            sk["unit_target"] |= bl_char
            if uf("NOPC"):
                sk["unit_target"] &= ~bl_pc
            if uf("NOMOB"):
                sk["unit_target"] &= ~bl_mob
            if uf("SKILL"):
                sk["unit_target"] |= bl_skill
        elif not exists:
            sk["unit_flag"] = []
            sk["unit_target"] |= bl_char
