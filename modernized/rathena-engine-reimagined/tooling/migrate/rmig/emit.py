"""Normalise the merged legacy state into the Luau data model (namespaced string ids, readable enum names)."""
import re
from . import luau
from .db_misc import PARAM_NAMES, ELEMENT_NAMES

U64 = 0xFFFFFFFFFFFFFFFF


def slug(s):
    t = re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")
    return t or "unnamed"


class Ids(object):
    """legacy numeric id -> 'mod:kind/slug'. Slug collisions inside a kind get '_<legacy id>' (reported)."""

    def __init__(self, mod, loaded):
        self.mod = mod
        self.collisions = []
        self.maps = {}
        c = loaded.w.c
        self._build("item", [(i, r["aegis"]) for i, r in loaded.dbs["item_db"].rows.items()])
        self._build("mob", [(i, r["sprite"]) for i, r in loaded.dbs["mob_db"].rows.items()])
        self._build("skill", [(i, r["name"]) for i, r in loaded.dbs["skill_db"].rows.items()])
        st = []
        for k in loaded.dbs["status"].rows:
            n = c.name_of("SC_", k)
            st.append((k, n if n else "sc_%d" % k))
        self._build("status", st)
        jb = []
        for k in loaded.dbs["job_stats"].rows:
            n = c.name_of("JOB_", k)
            jb.append((k, n if n else "job_%d" % k))
        self._build("job", jb)

    def _build(self, kind, pairs):
        used = {}
        m = {}
        for lid, name in sorted(pairs):
            s = slug(name)
            if s in used:
                self.collisions.append((kind, name, lid, used[s]))
                s = "%s_%d" % (s, lid)
            used[s] = lid
            m[lid] = "%s:%s/%s" % (self.mod, kind, s)
        self.maps[kind] = m

    def get(self, kind, lid):
        return self.maps[kind].get(lid)


class Emitter(object):
    def __init__(self, loaded, mode):
        self.L = loaded
        self.c = loaded.w.c
        self.mode = mode
        self.mod = "ruleset.prerenewal" if mode == "prerenewal" else "ruleset.renewal"
        self.ids = Ids(self.mod, loaded)
        self.notes = []
        self._eaj = None
        self.max_level = self.c.lim("MAX_LEVEL")

    # ---- helpers
    def nm(self, prefix, v, default=None):
        n = self.c.name_of(prefix, v)
        return n if n is not None else (default if default is not None else str(v))

    def bits(self, prefix, mask):
        names, rest = self.c.bit_names(prefix, mask)
        return names, rest

    def _eaj_table(self):
        if self._eaj is None:
            t = []
            seen = set()
            fm = self.c.lim("MAPID_FIRSTMASK")
            j21, j22 = self.c.lim("JOBL_2_1"), self.c.lim("JOBL_2_2")
            for k, v in self.c.names.items():
                if not k.startswith("EAJ_") or k.endswith("MASK"):
                    continue
                job = 1 << (v & fm)
                idx = 1 if (v & j21) else (2 if (v & j22) else 0)
                if (idx, job) in seen:
                    continue
                seen.add((idx, job))
                t.append((k[4:], idx, job))
            self._eaj = t
        return self._eaj

    def jobs(self, class_base):
        if all(x == U64 for x in class_base):
            return ["All"]
        out = []
        for name, idx, bit in self._eaj_table():
            if class_base[idx] & bit:
                out.append(name)
        return out

    def script_fields(self, d, row):
        any_ = False
        for src, dst in (("script", "script"), ("equip_script", "equip_script"), ("unequip_script", "unequip_script")):
            if row.get(src) is not None:
                d[dst] = row[src]
                any_ = True
        if any_:
            d["script_lang"] = "rathena"

    # ---- item_db
    def items(self):
        c = self.c
        out = {"usable": [], "equip": [], "etc": []}
        app = self.L.dbs["item_db"]
        t_usable = set(c.get("IT_" + n) for n in ("HEALING", "USABLE", "DELAYCONSUME", "CASH") if c.get("IT_" + n) is not None)
        t_equip = set(c.get("IT_" + n) for n in ("WEAPON", "ARMOR", "AMMO", "SHADOWGEAR", "PETARMOR") if c.get("IT_" + n) is not None)
        for iid in sorted(app.rows):
            r = app.rows[iid]
            first = (app.touched.get(iid) or [""])[0]
            if "item_db_usable" in first:
                cat = "usable"
            elif "item_db_equip" in first:
                cat = "equip"
            elif "item_db_etc" in first:
                cat = "etc"
            else:
                cat = "usable" if r["type"] in t_usable else ("equip" if r["type"] in t_equip else "etc")
            d = {"id": self.ids.get("item", iid), "client_id": iid, "aegis_name": r["aegis"], "name": r["name"],
                 "type": self.nm("IT_", r["type"])}
            if r["subtype"]:
                pre = "W_" if r["type"] == c.get("IT_WEAPON") else ("AMMO_" if r["type"] == c.get("IT_AMMO") else "CARD_")
                d["subtype"] = self.nm(pre, r["subtype"])
            d["buy"], d["sell"], d["weight"], d["attack"] = r["buy"], r["sell"], r["weight"], r["atk"]
            if "matk" in r:
                d["magic_attack"] = r["matk"]
            d["defense"], d["range"], d["slots"] = r["def"], r["range"], r["slots"]
            d["jobs"] = self.jobs(r["class_base"])
            d["job_mask"] = ["0x%016X" % x for x in r["class_base"]]
            names, rest = self.bits("ITEMJ_", r["class_upper"])
            d["classes"] = names
            d["gender"] = self.nm("SEX_", r["sex"])
            names, rest = self.bits("EQP_", r["equip"])
            d["locations"] = names
            if rest:
                self.notes.append("item %d: equip bits %#x have no EQP_ name" % (iid, rest))
            d["equip_mask"] = r["equip"]
            d["weapon_level"], d["armor_level"] = r["wlv"], r["alv"]
            d["equip_level_min"], d["equip_level_max"] = r["elv"], r["elvmax"]
            d["refineable"] = not r["no_refine"]
            d["gradable"] = r["gradable"]
            d["view"] = r["look"]
            if r["view_id"]:
                d["alias_of"] = self.ids.get("item", r["view_id"]) or r["view_id"]
            fl = {}
            for k, f in (("buying_store", "buyingstore"), ("dead_branch", "dead_branch"), ("container", "group"),
                         ("unique_id", "guid"), ("bind_on_equip", "bindOnEquip"), ("drop_announce", "broadcast")):
                if r[f]:
                    fl[k] = True
            if r["delay_consume"] & 2:
                fl["no_consume"] = True
            if r["delay_consume"] & 1:
                fl["delay_consume"] = True
            if r["dropEffect"]:
                fl["drop_effect"] = self.nm("DROPEFFECT_", r["dropEffect"])
            if fl:
                d["flags"] = fl
            if r["delay_duration"] or r["delay_sc"] != self.c.lim("SC_NONE"):
                dl = {"duration": r["delay_duration"]}
                if r["delay_sc"] != self.c.lim("SC_NONE"):
                    dl["status"] = self.nm("SC_", r["delay_sc"])
                d["delay"] = dl
            if r["stack_amount"] or r["stack_inventory"] or r["stack_cart"] or r["stack_storage"] or r["stack_guild_storage"]:
                d["stack"] = {"amount": r["stack_amount"], "inventory": r["stack_inventory"], "cart": r["stack_cart"],
                              "storage": r["stack_storage"], "guild_storage": r["stack_guild_storage"]}
            if r["nouse_sitting"] or r["nouse_override"] != 100:
                d["no_use"] = {"override": r["nouse_override"], "sitting": r["nouse_sitting"]}
            tr = {}
            for k, f in (("no_drop", "tr_drop"), ("no_trade", "tr_trade"), ("trade_partner", "tr_trade_partner"),
                         ("no_sell", "tr_sell"), ("no_cart", "tr_cart"), ("no_storage", "tr_storage"),
                         ("no_guild_storage", "tr_guild_storage"), ("no_mail", "tr_mail"), ("no_auction", "tr_auction")):
                if r[f]:
                    tr[k] = True
            if tr or r["trade_override"] != 100:
                tr["override"] = r["trade_override"]
                d["trade"] = tr
            self.script_fields(d, r)
            if r.get("_synthetic"):
                d["synthetic"] = True
            out[cat].append((d["id"], d))
        return out

    # ---- mob_db
    def mobs(self):
        c = self.c
        out = []
        app = self.L.dbs["mob_db"]
        for mid in sorted(app.rows):
            r = app.rows[mid]
            d = {"id": self.ids.get("mob", mid), "client_id": mid, "aegis_name": r["sprite"], "name": r["name"],
                 "japanese_name": r["jname"], "level": r["lv"], "hp": r["max_hp"], "sp": r["max_sp"],
                 "base_exp": r["base_exp"], "job_exp": r["job_exp"], "mvp_exp": r["mexp"],
                 "attack": r.get("_attack", r["atk"])}
            if "matk" in r:
                d["magic_attack"] = r["matk"]
            else:
                d["attack2"] = r["atk2"]
            d["defense"], d["magic_defense"], d["resistance"], d["magic_resistance"] = r["def"], r["mdef"], r["res"], r["mres"]
            for k in ("str", "agi", "vit", "int", "dex", "luk"):
                d[k] = r[k]
            d["attack_range"], d["skill_range"], d["chase_range"] = r["range"], r["range2"], r["range3"]
            d["size"] = self.nm("Size_", r["size"])
            d["race"] = self.nm("RC_", r["race"])
            if r["race2"]:
                d["race_groups"] = [self.nm("RC2_", x) for x in r["race2"]]
            d["element"], d["element_level"] = self.nm("ELE_", r["def_ele"]), r["ele_lv"]
            d["walk_speed"] = r["speed"]
            d["attack_delay"], d["attack_motion"] = r["adelay"], r["amotion"]
            d["client_attack_motion"], d["damage_motion"] = r["clientamotion"], r["dmotion"]
            d["damage_taken"] = r["damagetaken"]
            if r["group_id"]:
                d["group_id"] = r["group_id"]
            if r["title"]:
                d["title"] = r["title"]
            names, rest = self.bits("MD_", r["mode"])
            d["modes"] = names
            d["mode_mask"] = r["mode"]
            d["class"] = self.nm("CLASS_", r["class"])
            for key, f in (("mvp_drops", "mvpdrops"), ("drops", "drops")):
                if r[f]:
                    d[key] = [self._drop(x) for x in r[f]]
            out.append((d["id"], d))
        return out

    def _drop(self, x):
        d = {"item": self.ids.get("item", x["nameid"]) or x["nameid"], "rate": x["rate"]}
        if x["steal"]:
            d["steal_protected"] = True
        if x["rndopt"]:
            d["random_option_group"] = x["rndopt"]
        return d

    # ---- skill_db
    def skills(self):
        c = self.c
        out = []
        app = self.L.dbs["skill_db"]
        for sid in sorted(app.rows):
            r = app.rows[sid]
            n = r["max"]
            tr = lambda a: a[:n]
            d = {"id": self.ids.get("skill", sid), "client_id": sid, "name": r["name"], "description": r["desc"], "max_level": n}
            d["type"] = self.nm("BF_", r["skill_type"], "NONE")
            nm = c.name_of("INF_", r["inf"])
            d["target_type"] = (nm[:-6] if nm and nm.endswith("_SKILL") else nm) if nm else r["inf"]
            if r["nk"]:
                d["damage_flags"] = [self.nm("NK_", x) for x in r["nk"]]
            if r["inf2"]:
                d["flags"] = [self.nm("INF2_", x) for x in r["inf2"]]

            def arr(key, a, force=False):
                if force or any(a):
                    d[key] = tr(a)
            arr("range", r["range"], True)
            d["hit"] = self.nm("DMG_", r["hit"])
            arr("hit_count", r["num"])
            d["element"] = [self.nm("ELE_", x) for x in tr(r["element"])]
            arr("splash_area", r["splash"])
            arr("active_instance", r["maxcount"])
            arr("knockback", r["blewcount"])
            if r["copy_option"] or r["copy_req_opt"]:
                d["copy_flags"] = {"option": r["copy_option"], "remove_requirement_mask": r["copy_req_opt"]}
            if r["nonear_range"] or r["nonear_type"]:
                d["no_near_npc"] = {"additional_range": r["nonear_range"], "type_mask": r["nonear_type"]}
            d["cast_cancel"] = r["castcancel"]
            if r["cast_def_rate"]:
                d["cast_defense_reduction"] = r["cast_def_rate"]
            arr("cast_time", r["cast"])
            arr("fixed_cast_time", r["fixed_cast"])
            arr("after_cast_act_delay", r["delay"])
            arr("after_cast_walk_delay", r["walkdelay"])
            arr("duration1", r["upkeep_time"])
            arr("duration2", r["upkeep_time2"])
            arr("cooldown", r["cooldown"])
            if r["castnodex"]:
                d["cast_time_flags_mask"] = r["castnodex"]
            if r["delaynodex"]:
                d["cast_delay_flags_mask"] = r["delaynodex"]
            rq = {}
            for key, f in (("hp_cost", "req_hp"), ("sp_cost", "req_sp"), ("ap_cost", "req_ap"), ("hp_rate_cost", "req_hp_rate"),
                           ("sp_rate_cost", "req_sp_rate"), ("ap_rate_cost", "req_ap_rate"), ("max_hp_trigger", "req_mhp"),
                           ("zeny_cost", "req_zeny"), ("ammo_amount", "req_ammo_qty"), ("spirit_sphere_cost", "req_spiritball")):
                if any(r[f]):
                    rq[key] = tr(r[f])
            if r["req_weapon"]:
                rq["weapon_mask"] = r["req_weapon"]
                rq["weapon"] = [self.nm("W_", b) for b in range(32) if r["req_weapon"] >> b & 1]
            if r["req_ammo"]:
                rq["ammo_mask"] = r["req_ammo"]
                rq["ammo"] = [self.nm("AMMO_", b) for b in range(32) if r["req_ammo"] >> b & 1]
            if r["req_state"]:
                rq["state"] = self.nm("ST_", r["req_state"])
            if r["req_status"]:
                rq["status"] = [self.ids.get("status", x) or self.nm("SC_", x) for x in r["req_status"]]
            ic = [{"item": self.ids.get("item", r["req_itemid"][i]) or r["req_itemid"][i], "amount": r["req_amount"][i], "slot": i + 1}
                  for i in range(len(r["req_itemid"])) if r["req_itemid"][i]]
            if ic:
                rq["item_cost"] = ic
                if r["req_item_level_dep"]:
                    rq["item_cost_level_dependent"] = True
            if r["req_eqitem"]:
                rq["equipment"] = [self.ids.get("item", x) or x for x in r["req_eqitem"]]
            if rq:
                d["requires"] = rq
            arr("give_ap", r["giveap"])
            if r["unit_id"]:
                u = {"id": self.nm("UNT_", r["unit_id"])}
                if r["unit_id2"]:
                    u["alternate_id"] = self.nm("UNT_", r["unit_id2"])
                u["layout"] = tr(r["unit_layout"])
                u["range"] = tr(r["unit_range"])
                u["interval"] = r["unit_interval"]
                u["target_mask"] = r["unit_target"]
                if r["unit_flag"]:
                    u["flags"] = [self.nm("UF_", x) for x in r["unit_flag"]]
                d["unit"] = u
            if r["sc"] != c.lim("SC_NONE"):
                d["status"] = self.ids.get("status", r["sc"]) or self.nm("SC_", r["sc"])
            out.append((d["id"], d))
        return out

    # ---- status
    def statuses(self):
        c = self.c
        out = []
        app = self.L.dbs["status"]
        for sc in sorted(app.rows):
            r = app.rows[sc]
            d = {"id": self.ids.get("status", sc), "legacy_sc": sc, "name": self.nm("SC_", sc, str(sc))}
            if r["icon"] != c.lim("EFST_BLANK"):
                d["client_id"] = r["icon"]
                d["icon"] = self.nm("EFST_", r["icon"])
            if r["skill_id"]:
                d["duration_lookup"] = self.ids.get("skill", r["skill_id"]) or r["skill_id"]
            if r["state"]:
                d["states"] = [self.nm("SCS_", x) for x in r["state"]]
            if r["calc_flag"]:
                d["calc_flags"] = [self.nm("SCB_", x) for x in r["calc_flag"]]
            if r["opt1"]:
                d["opt1"] = self.nm("OPT1_", r["opt1"])
            if r["opt2"]:
                d["opt2"] = self.bits("OPT2_", r["opt2"])[0]
            if r["opt3"]:
                d["opt3"] = self.bits("OPT3_", r["opt3"])[0]
            if r["look"]:
                d["options"] = self.bits("OPTION_", r["look"])[0]
            if r["flag"]:
                d["flags"] = [self.nm("SCF_", x) for x in r["flag"]]
            d["min_rate"], d["min_duration"] = r["min_rate"], r["min_duration"]
            for key, f in (("fail", "fail"), ("end_on_start", "endonstart"), ("end_return", "endreturn"), ("end_on_end", "endonend")):
                if r[f]:
                    d[key] = [self.ids.get("status", x) or x for x in r[f]]
            if r["script"] is not None:
                d["script"] = r["script"]
                d["script_lang"] = "rathena"
            out.append((d["id"], d))
        return out

    # ---- jobs
    def jobs_tables(self):
        c = self.c
        app = self.L.dbs["job_stats"]
        stats, aspd, exp, bp = [], [], [], []
        pnames = [p.lower() for p in PARAM_NAMES]
        for jid in sorted(app.rows):
            r = app.rows[jid]
            jn = self.ids.get("job", jid)
            jname = self.nm("JOB_", jid, str(jid))
            s = {"id": jn, "client_id": jid, "name": jname, "max_weight": r["max_weight_base"],
                 "hp_factor": r["hp_factor"], "hp_increase": r["hp_increase"], "sp_factor": r["sp_factor"],
                 "sp_increase": r["sp_increase"], "ap_factor": r["ap_factor"], "ap_increase": r["ap_increase"],
                 "max_base_level": r["max_base_level"], "max_job_level": r["max_job_level"]}
            mp = {pnames[i]: r["max_param"][i] for i in range(len(pnames)) if r["max_param"][i]}
            if mp:
                s["max_stats_explicit_or_default"] = mp
            jb = r["job_bonus"]
            cols = {}
            for i, p in enumerate(pnames):
                col = [(row[i] if row[i] < 0x8000 else row[i] - 0x10000) for row in jb]
                if any(col):
                    cols[p] = col
            if cols:
                s["job_bonus"] = cols
            if r["alternate_outfits"]:
                s["alternate_outfits"] = [self.ids.get("job", x) or x for x in r["alternate_outfits"]]
            stats.append((jn, s))
            aspd.append((jn, {"id": jn, "client_id": jid, "name": jname,
                              "aspd_base": {self.nm("W_", i, "w%d" % i).lower(): v for i, v in enumerate(r["aspd_base"])}}))
            exp.append((jn, {"id": jn, "client_id": jid, "name": jname, "max_base_level": r["max_base_level"],
                             "max_job_level": r["max_job_level"], "base_exp": r["base_exp"][:r["max_base_level"]],
                             "job_exp": r["job_exp"][:r["max_job_level"]]}))
            bp.append((jn, {"id": jn, "client_id": jid, "name": jname, "base_hp": r["base_hp"], "base_sp": r["base_sp"],
                            "base_ap": r["base_ap"]}))
        return {"job_stats": stats, "job_aspd": aspd, "job_exp": exp, "job_basepoints": bp}

    # ---- small tables
    def size_fix(self):
        out = []
        for k in sorted(self.L.dbs["size_fix"].rows):
            r = self.L.dbs["size_fix"].rows[k]
            w = self.nm("W_", k, str(k))
            i = "%s:size_fix/%s" % (self.mod, slug(w))
            out.append((i, {"id": i, "weapon": w, "client_id": k, "small": r["small"], "medium": r["medium"], "large": r["large"]}))
        return out

    def attr_fix(self):
        out = []
        c = self.c
        rows = self.L.dbs["attr_fix"].rows
        for lv in sorted(rows):
            t = rows[lv]["table"]
            tbl = {}
            for a, an in enumerate(ELEMENT_NAMES):
                ai = c.get("ELE_" + an)
                tbl[an.lower()] = {dn.lower(): t[ai][c.get("ELE_" + dn)] for dn in ELEMENT_NAMES}
            i = "%s:attr_fix/level_%d" % (self.mod, lv)
            out.append((i, {"id": i, "level": lv, "table": tbl}))
        return out

    def refine(self):
        c = self.c
        out = []
        app = self.L.dbs["refine"]
        for g in sorted(app.rows):
            info = app.rows[g]
            gn = self.nm("REFINE_TYPE_", g, str(g))
            lv = {}
            for level in sorted(info["levels"]):
                li = info["levels"][level]
                rl = {}
                for r in sorted(li["levels"]):
                    ri = li["levels"][r]
                    e = {"bonus": ri["bonus"], "random_bonus": ri["randombonus_max"], "blessing_amount": ri["blessing_amount"]}
                    if ri["broadcast_success"]:
                        e["broadcast_success"] = True
                    if ri["broadcast_failure"]:
                        e["broadcast_failure"] = True
                    ch = {}
                    for ci in sorted(ri["costs"]):
                        co = ri["costs"][ci]
                        cc = {"rate": co["chance"], "price": co["zeny"], "breaking_rate": co["breaking_rate"],
                              "downgrade_amount": co["downgrade_amount"]}
                        if co["nameid"]:
                            cc["material"] = self.ids.get("item", co["nameid"]) or co["nameid"]
                        ch[self.nm("REFINE_COST_", ci, str(ci)).lower()] = cc
                    if ch:
                        e["chances"] = ch
                    rl[r + 1] = e
                lv[level] = rl
            i = "%s:refine/%s" % (self.mod, slug(gn))
            out.append((i, {"id": i, "group": gn, "levels": lv}))
        return out

    def statpoint(self):
        out = []
        for lv in sorted(self.L.dbs["statpoint"].rows):
            r = self.L.dbs["statpoint"].rows[lv]
            i = "%s:statpoint/level_%d" % (self.mod, lv)
            out.append((i, {"id": i, "level": lv, "points": r["statpoints"], "trait_points": r["traitpoints"]}))
        return out

    # ---- raw tables
    def raw(self, name):
        app = self.L.dbs[name]
        scripts = set(app.SCRIPTS)
        out = []
        n = 0
        for key in sorted(app.rows, key=lambda k: (isinstance(k, str), k)):
            row = app.rows[key]
            d, has_script = {}, False
            for k, v in row.items():
                cv = raw_value(v)
                if cv is None:
                    continue
                d[k] = cv
                if k in scripts and isinstance(cv, str):
                    has_script = True
            n += 1
            ident = "%s:%s/%s" % (self.mod, name, slug(str(key)) if app.KEY else "row_%d" % key)
            e = {"id": ident}
            e.update(d)
            if has_script:
                e["script_lang"] = "rathena"
            out.append((ident, e))
        return out


_INT = re.compile(r"^-?(0|[1-9][0-9]*)$")


def raw_value(v):
    from .yamlio import NULL
    if v is NULL:
        return None
    if isinstance(v, dict):
        pairs = getattr(v, "pairs", None) or list(v.items())
        out = {}
        for k, x in pairs:
            cv = raw_value(x)
            if cv is not None and k not in out:
                out[k] = cv
        return out
    if isinstance(v, list):
        return [raw_value(x) if raw_value(x) is not None else "" for x in v]
    if isinstance(v, str):
        if _INT.match(v) and abs(int(v)) < (1 << 53):
            return int(v)
        if v == "true":
            return True
        if v == "false":
            return False
        return v
    return v
