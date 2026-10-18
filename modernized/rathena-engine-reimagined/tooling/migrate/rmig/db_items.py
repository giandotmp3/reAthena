"""item_db: port of ItemDatabase::parseBodyNode / loadingFinished (src/map/itemdb.cpp)."""
from .base import Applier, U64
from .yamlio import Reject, NULL, has

# item types with their own non-stackable semantic (item_data::isStackable)
_NONSTACK = ("WEAPON", "ARMOR", "PETEGG", "PETARMOR", "SHADOWGEAR")
_EQUIP = ("WEAPON", "ARMOR", "AMMO", "SHADOWGEAR")


class ItemDb(Applier):
    NAME = "item_db"
    TYPE = "ITEM_DB"
    VERSION = 3
    MIN_VERSION = 1
    STUB = "db/item_db.yml"

    def __init__(self, world):
        Applier.__init__(self, world)
        self.aegis = {}          # lower aegis name -> row
        self._price = {}         # id -> (has_buy, has_sell)
        c = self.c
        self.IT = lambda n: c.get("IT_" + n)
        self.t_etc = c.get("IT_ETC")
        self.t_weapon = c.get("IT_WEAPON")
        self.t_armor = c.get("IT_ARMOR")
        self.t_ammo = c.get("IT_AMMO")
        self.t_card = c.get("IT_CARD")
        self.t_shadow = c.get("IT_SHADOWGEAR")
        self.t_delay = c.get("IT_DELAYCONSUME")
        self.t_usable = c.get("IT_USABLE")
        self.t_petegg = c.get("IT_PETEGG")
        self.t_petarmor = c.get("IT_PETARMOR")
        self.nonstack = set(c.get("IT_" + n) for n in _NONSTACK)
        self.equiptypes = set(c.get("IT_" + n) for n in _EQUIP)
        self.itemj_all = c.lim("ITEMJ_ALL")
        self.shadow_gear = c.lim("EQP_SHADOW_GEAR")
        self.max_level = c.lim("MAX_LEVEL")
        self.max_zeny = c.lim("MAX_ZENY")
        self.max_wlv = c.lim("MAX_WEAPON_LEVEL")
        self.max_alv = c.lim("MAX_ARMOR_LEVEL")
        self.deftype_max = c.lim("DEFTYPE_MAX")
        self.area_size = c.battle["area_size"]
        self.max_slots = c.lim("MAX_SLOTS")
        self.sc_max = c.lim("SC_MAX")
        self.sc_none = c.lim("SC_NONE")
        self.jobl_21 = c.lim("JOBL_2_1")
        self.jobl_22 = c.lim("JOBL_2_2")
        self.firstmask = c.lim("MAPID_FIRSTMASK")
        self.dummy = c.lim("ITEMID_DUMMY")
        self.dropeffect_none = c.get("DROPEFFECT_NONE")
        self.dropeffect_max = c.lim("DROPEFFECT_MAX")

    # --- state ---
    def clear(self):
        Applier.clear(self)
        self.aegis.clear()
        self._price.clear()

    def search_aegis(self, name):
        return self.aegis.get(name.lower())

    def _base_fields(self, nameid):
        d = {
            "id": nameid, "aegis": "", "name": "", "type": 0, "subtype": 0, "buy": 0, "sell": 0, "weight": 0, "atk": 0,
            "def": 0, "range": 0, "slots": 0, "class_base": [0, 0, 0], "class_upper": 0, "sex": 0, "equip": 0,
            "wlv": 0, "alv": 0, "elv": 0, "elvmax": 0, "no_refine": False, "gradable": False, "look": 0, "view_id": 0,
            "buyingstore": False, "dead_branch": False, "group": False, "guid": False, "bindOnEquip": False,
            "broadcast": False, "delay_consume": 0, "dropEffect": 0, "delay_duration": 0, "delay_sc": 0,
            "stack_amount": 0, "stack_inventory": False, "stack_cart": False, "stack_storage": False,
            "stack_guild_storage": False, "nouse_override": 0, "nouse_sitting": False, "trade_override": 0,
            "tr_drop": False, "tr_trade": False, "tr_trade_partner": False, "tr_sell": False, "tr_cart": False,
            "tr_storage": False, "tr_guild_storage": False, "tr_mail": False, "tr_auction": False,
            "script": None, "equip_script": None, "unequip_script": None,
        }
        return d

    def _jobid2mapid(self, bclass, jobmask, active):
        """itemdb_jobid2mapid; jobmask None = MAPID_ALL."""
        temp = [0, 0, 0]
        if jobmask is not None:
            job = 1 << (jobmask & self.firstmask)
            if jobmask & self.jobl_21:
                temp[1] |= job
            elif jobmask & self.jobl_22:
                temp[2] |= job
            else:
                temp[0] |= job
        else:
            temp = [U64, U64, U64]
        for i in range(3):
            if temp[i] == 0:
                continue
            if active:
                bclass[i] |= temp[i]
            else:
                bclass[i] &= ~temp[i] & U64

    def _default_gender(self, node, it):
        c = self.c
        if it["id"] == c.lim("WEDDING_RING_M"):
            return c.get("SEX_MALE")
        if it["id"] == c.lim("WEDDING_RING_F"):
            return c.get("SEX_FEMALE")
        if it["type"] == self.t_weapon:
            if it["subtype"] == c.get("W_MUSICAL"):
                if it["sex"] != c.get("SEX_MALE"):
                    self.r.warn("Musical instruments are always male-only, defaulting to SEX_MALE.")
                return c.get("SEX_MALE")
            if it["subtype"] == c.get("W_WHIP"):
                if it["sex"] != c.get("SEX_FEMALE"):
                    self.r.warn("Whips are always female-only, defaulting to SEX_FEMALE.")
                return c.get("SEX_FEMALE")
        return it["sex"]

    def _stackable(self, it):
        return it["type"] not in self.nonstack

    def parse_body_node(self, node):
        r, c = self.r, self.c
        nameid = r.u32(node, "Id")
        self._cur_key = nameid
        item = self.rows.get(nameid)
        exists = item is not None
        if not exists:
            r.need(node, ["AegisName", "Name"])
            item = self._base_fields(nameid) if not self.w.renewal else dict(self._base_fields(nameid), matk=0)
        if has(node, "AegisName"):
            name = r.s(node, "AegisName")
            if len(name) > 50:
                r.warn('AegisName "%s" exceeds maximum of 49 characters, capping...' % name)
            other = self.search_aegis(name)
            if other is not None and other["id"] != nameid:
                r.warn("Found duplicate item Aegis name for %s, skipping." % name)
                raise Reject("dup_aegis")
            if exists:
                self.aegis.pop(item["aegis"].lower(), None)
            item["aegis"] = name
            self.aegis[name.lower()] = item
        if has(node, "Name"):
            name = r.s(node, "Name")
            if len(name) > 50:
                r.warn('Name "%s" exceeds maximum of 49 characters, capping...' % name)
            item["name"] = name
        if has(node, "Type"):
            t = r.s(node, "Type")
            k = c.get("IT_" + t)
            if k is None or k < 0 or k >= c.lim("IT_MAX"):
                r.warn("Invalid item type %s, defaulting to IT_ETC." % t)
                k = self.t_etc
            item["type"] = k
        elif not exists:
            item["type"] = self.t_etc
        if has(node, "SubType"):
            t = r.s(node, "SubType")
            if item["type"] == self.t_weapon:
                k = c.get("W_" + t)
                if k is None or k < 0 or k >= c.lim("MAX_WEAPON_TYPE"):
                    r.warn("Invalid weapon type %s, defaulting to W_FIST." % t)
                    # legacy quirk: the fallback is overwritten by the unvalidated constant right after
                    item["subtype"] = (k & 0xFF) if k is not None else 0
                else:
                    item["subtype"] = k
            elif item["type"] == self.t_ammo:
                k = c.get("AMMO_" + t)
                if k is None or k <= 0 or k >= c.lim("MAX_AMMO_TYPE"):
                    r.warn("Invalid ammo type %s, defaulting to AMMO_NONE." % t)
                    item["subtype"] = (k & 0xFF) if k is not None else 0
                else:
                    item["subtype"] = k
            elif item["type"] == self.t_card:
                k = c.get("CARD_" + t)
                if k is None or k < 0 or k >= c.lim("MAX_CARD_TYPE"):
                    r.warn("Invalid card type %s, defaulting to CARD_NORMAL." % t)
                    item["subtype"] = (k & 0xFF) if k is not None else 0
                else:
                    item["subtype"] = k
            else:
                r.warn("Item sub type is not supported for this item type.")
                raise Reject("subtype")
        has_buy = has_sell = False
        if has(node, "Buy"):
            v = r.u32(node, "Buy")
            if v > self.max_zeny:
                r.warn("Buying price exceeds MAX_ZENY. Capping...")
                v = self.max_zeny
            has_buy = True
            item["buy"] = v
        if has(node, "Sell"):
            v = r.u32(node, "Sell")
            if v > self.max_zeny:
                r.warn("Sell price exceeds MAX_ZENY. Capping...")
                v = self.max_zeny
            has_sell = True
            item["sell"] = v
        self._price[nameid] = (has_buy, has_sell)
        if has(node, "Weight"):
            item["weight"] = r.u32(node, "Weight")
        if has(node, "Attack"):
            item["atk"] = r.u32(node, "Attack")
        if self.w.renewal and has(node, "MagicAttack"):
            item["matk"] = r.u32(node, "MagicAttack")
        if has(node, "Defense"):
            v = r.u32(node, "Defense")
            if v > self.deftype_max:
                r.warn("Item defense %d exceeds DEFTYPE_MAX, capping." % v)
                v = self.deftype_max
            item["def"] = v
        if has(node, "Range"):
            v = r.u16(node, "Range")
            if v > self.area_size:
                r.warn("Item attack range %d exceeds AREA_SIZE, capping." % v)
                v = self.area_size
            item["range"] = v
        if has(node, "Slots"):
            v = r.u16(node, "Slots")
            if v > self.max_slots:
                r.warn("Item slots %d exceeds MAX_SLOTS, capping." % v)
                v = self.max_slots
            item["slots"] = v
        if has(node, "Jobs"):
            jn = node["Jobs"]
            item["class_base"] = [0, 0, 0]
            if has(jn, "All"):
                active = r.b(jn, "All")
                self._jobid2mapid(item["class_base"], None, active)
            for k, _v in r.items(jn):
                if k == "All":
                    continue
                kc = c.get("EAJ_" + k)
                if kc is None:
                    r.warn("Invalid item job %s, defaulting to All." % k)
                    self._jobid2mapid(item["class_base"], None, True)
                    break
                active = r.b(jn, k)
                self._jobid2mapid(item["class_base"], kc, active)
        elif not exists:
            item["class_base"] = [0, 0, 0]
            self._jobid2mapid(item["class_base"], None, True)
        if has(node, "Classes"):
            cn = node["Classes"]
            if has(cn, "All"):
                active = r.b(cn, "All")
                if active:
                    item["class_upper"] |= self.itemj_all
                else:
                    item["class_upper"] &= ~self.itemj_all & 0xFFFF
            for k, _v in r.items(cn):
                if k == "All":
                    continue
                kc = c.get("ITEMJ_" + k)
                if kc is None:
                    r.warn("Invalid class upper %s, defaulting to All." % k)
                    item["class_upper"] |= self.itemj_all
                    break
                active = r.b(cn, k)
                if active:
                    item["class_upper"] |= kc
                else:
                    item["class_upper"] &= ~kc & 0xFFFF
        elif not exists:
            item["class_upper"] = self.itemj_all
        if has(node, "Gender"):
            g = r.s(node, "Gender")
            k = c.get("SEX_" + g)
            if k is None or k < c.get("SEX_FEMALE") or k > c.get("SEX_BOTH"):
                r.warn("Invalid item gender %s, defaulting to SEX_BOTH." % g)
                k = c.get("SEX_BOTH")
            item["sex"] = k
            item["sex"] = self._default_gender(node, item)
        elif not exists:
            item["sex"] = c.get("SEX_BOTH")
            item["sex"] = self._default_gender(node, item)
        if has(node, "Locations"):
            ln = node["Locations"]
            for k, _v in r.items(ln):
                kc = c.get("EQP_" + k)
                if kc is None:
                    r.warn("Invalid equip location %s, defaulting to IT_ETC." % k)
                    item["type"] = self.t_etc
                    break
                active = r.b(ln, k)
                if active:
                    if (kc & self.shadow_gear) and item["type"] != self.t_shadow:
                        r.warn("Invalid item equip location %s as it's not a Shadow Gear item type, defaulting to IT_ETC." % k)
                        item["type"] = self.t_etc
                    item["equip"] = (item["equip"] | kc) & 0xFFFFFFFF
                else:
                    item["equip"] = item["equip"] & ~kc & 0xFFFFFFFF
        elif not exists:
            if item["type"] in self.equiptypes:
                r.warn("Invalid item equip location as it has no equip location, defaulting to IT_ETC.")
                item["type"] = self.t_etc
            else:
                item["equip"] = 0
        if has(node, "WeaponLevel"):
            lv = r.u16(node, "WeaponLevel")
            if lv > self.max_wlv:
                r.warn("Invalid weapon level %d, defaulting to 0." % lv)
                lv = 0
            if item["type"] != self.t_weapon:
                r.warn("Item type is not a weapon, defaulting to 0.")
                lv = 0
            item["wlv"] = lv
        if has(node, "ArmorLevel"):
            lv = r.u16(node, "ArmorLevel")
            if lv > self.max_alv:
                r.warn("Invalid armor level %d, defaulting to 0." % lv)
                lv = 0
            if item["type"] != self.t_armor:
                r.warn("Item type is not an armor, defaulting to 0.")
                lv = 0
            item["alv"] = lv
        if has(node, "EquipLevelMin"):
            lv = r.u16(node, "EquipLevelMin")
            if lv > self.max_level:
                r.warn("Minimum equip level %d exceeds MAX_LEVEL, capping." % lv)
                lv = self.max_level
            item["elv"] = lv
        elif not exists:
            item["elv"] = 0
        if has(node, "EquipLevelMax"):
            lv = r.u16(node, "EquipLevelMax")
            if lv < item["elv"]:
                r.warn("Maximum equip level %d is less than minimum equip level, capping." % lv)
                lv = item["elv"]
            if lv > self.max_level:
                r.warn("Maximum equip level %d exceeds MAX_LEVEL, capping." % lv)
                lv = self.max_level
            item["elvmax"] = lv
        elif not exists:
            item["elvmax"] = self.max_level
        if has(node, "Refineable"):
            item["no_refine"] = not r.b(node, "Refineable")
        elif not exists:
            item["no_refine"] = True
        if has(node, "Gradable"):
            item["gradable"] = r.b(node, "Gradable")
        if has(node, "View"):
            item["look"] = r.u32(node, "View")
        if has(node, "AliasName"):
            v = r.s(node, "AliasName")
            vd = self.search_aegis(v)
            if vd is None:
                r.warn("Unable to change the alias because %s is an unknown item." % v)
                raise Reject("alias")
            item["view_id"] = vd["id"]
        if has(node, "Flags"):
            fn = node["Flags"]
            if has(fn, "BuyingStore"):
                a = r.b(fn, "BuyingStore")
                if not self._stackable(item) and a:
                    r.warn("Non-stackable item cannot be enabled for buying store.")
                    a = False
                item["buyingstore"] = a
            if has(fn, "DeadBranch"):
                item["dead_branch"] = r.b(fn, "DeadBranch")
            if has(fn, "Container"):
                item["group"] = r.b(fn, "Container")
            if has(fn, "UniqueId"):
                a = r.b(fn, "UniqueId")
                if not self._stackable(item) and a:
                    r.warn("Non-stackable item cannot be enabled for UniqueId.")
                    a = False
                item["guid"] = a
            if has(fn, "BindOnEquip"):
                item["bindOnEquip"] = r.b(fn, "BindOnEquip")
            if has(fn, "DropAnnounce"):
                item["broadcast"] = r.b(fn, "DropAnnounce")
            tmp = c.lim("DELAYCONSUME_TEMP")
            noc = c.lim("DELAYCONSUME_NOCONSUME")
            if has(fn, "NoConsume"):
                a = r.b(fn, "NoConsume")
                if a:
                    item["delay_consume"] |= noc
                else:
                    item["delay_consume"] &= ~noc & 0xFFFFFFFF
            elif not exists:
                if not (item["delay_consume"] & tmp):
                    item["delay_consume"] = 0
            if has(fn, "DropEffect"):
                e = r.s(fn, "DropEffect")
                k = c.get("DROPEFFECT_" + e)
                if k is None or k < self.dropeffect_none or k > self.dropeffect_max:
                    r.warn("Invalid item drop effect %s, defaulting to DROPEFFECT_NONE." % e)
                    k = self.dropeffect_none
                item["dropEffect"] = k
        # (new item without Flags: all defaults are already zero)
        if has(node, "Delay"):
            dn = node["Delay"]
            if has(dn, "Duration"):
                item["delay_duration"] = r.u32(dn, "Duration")
            if has(dn, "Status"):
                st = r.s(dn, "Status")
                k = c.get("SC_" + st)
                if k is None or k < self.sc_none or k >= self.sc_max:
                    r.warn("Invalid item delay status %s, defaulting to SC_NONE." % st)
                    k = self.sc_none
                item["delay_sc"] = k
            elif not exists:
                item["delay_sc"] = self.sc_none
        elif not exists:
            item["delay_duration"] = 0
            item["delay_sc"] = self.sc_none
        if has(node, "Stack"):
            sn = node["Stack"]
            if has(sn, "Amount"):
                a = r.u16(sn, "Amount")
                if not self._stackable(item):
                    r.warn("Non-stackable item cannot be enabled for stacking.")
                    a = 0
                item["stack_amount"] = a
            if has(sn, "Inventory"):
                item["stack_inventory"] = r.b(sn, "Inventory")
            if has(sn, "Cart"):
                item["stack_cart"] = r.b(sn, "Cart")
            if has(sn, "Storage"):
                item["stack_storage"] = r.b(sn, "Storage")
            if has(sn, "GuildStorage"):
                item["stack_guild_storage"] = r.b(sn, "GuildStorage")
        if has(node, "NoUse"):
            nn = node["NoUse"]
            if has(nn, "Override"):
                v = r.u16(nn, "Override")
                if v > 100:
                    r.warn("Item no use override level %d exceeds 100, capping to 100." % v)
                    v = 100
                item["nouse_override"] = v
            elif not exists:
                item["nouse_override"] = 100
            if has(nn, "Sitting"):
                item["nouse_sitting"] = r.b(nn, "Sitting")
        elif not exists:
            item["nouse_override"] = 100
            item["nouse_sitting"] = False
        if has(node, "Trade"):
            tn = node["Trade"]
            if has(tn, "Override"):
                v = r.u16(tn, "Override")
                if v > 100:
                    r.warn("Item trade override level %d exceeds 100, capping to 100." % v)
                    v = 100
                item["trade_override"] = v
            elif not exists:
                item["trade_override"] = 100
            for key, fld in (("NoDrop", "tr_drop"), ("NoTrade", "tr_trade"), ("TradePartner", "tr_trade_partner"),
                             ("NoSell", "tr_sell"), ("NoCart", "tr_cart"), ("NoStorage", "tr_storage"),
                             ("NoGuildStorage", "tr_guild_storage"), ("NoMail", "tr_mail"), ("NoAuction", "tr_auction")):
                if has(tn, key):
                    item[fld] = r.b(tn, key)
        elif not exists:
            item["trade_override"] = 100
        for key, fld in (("Script", "script"), ("EquipScript", "equip_script"), ("UnEquipScript", "unequip_script")):
            if has(node, key):
                item[fld] = r.s(node, key)
        if not exists:
            self.rows[nameid] = item
        return True

    def finish(self):
        """ItemDatabase::loadingFinished"""
        c = self.c
        tmp = c.lim("DELAYCONSUME_TEMP")
        shield = c.get("EQP_SHIELD")
        for nameid, it in self.rows.items():
            if it["type"] == self.t_delay:
                it["type"] = self.t_usable
                it["delay_consume"] |= tmp
            else:
                it["delay_consume"] &= ~tmp & 0xFFFFFFFF
            if it["type"] == self.t_weapon:
                if it["wlv"] == 0:
                    it["wlv"] = 1
                if it["alv"] != 0:
                    it["alv"] = 0
            elif it["type"] == self.t_armor:
                if it["alv"] == 0:
                    it["alv"] = 1
                if it["wlv"] != 0:
                    it["wlv"] = 0
            else:
                it["wlv"] = 0
                it["alv"] = 0
            if it["type"] != self.t_armor and it["type"] != self.t_shadow and it["def"] > 0:
                it["def"] = 0
            if it["type"] != self.t_weapon and it["type"] != self.t_ammo and it["atk"] > 0:
                it["atk"] = 0
            if it["type"] != self.t_weapon:
                if self.w.renewal and it.get("matk", 0) > 0:
                    it["matk"] = 0
                if it["range"] > 0:
                    it["range"] = 0
            hb, hs = self._price.get(nameid, (False, False))
            if not hb and hs:
                it["buy"] = (it["sell"] * 2) & 0xFFFFFFFF
            elif hb and not hs:
                it["sell"] = it["buy"] // 2
            if it["buy"] / 124.0 < it["sell"] / 75.0:
                it["sell"] = 1
            if it["type"] == self.t_armor and (it["equip"] & shield) != 0 and it["look"] == 0:
                it["look"] = 1
        if self.dummy not in self.rows:
            d = self._base_fields(self.dummy)
            if self.w.renewal:
                d["matk"] = 0
            d.update({"weight": 1, "sell": 1, "type": self.t_etc, "aegis": "UNKNOWN_ITEM", "name": "Unknown Item",
                      "view_id": c.lim("UNKNOWN_ITEM_ID")})
            self.rows[self.dummy] = d
            self.aegis["unknown_item"] = d
            d["_synthetic"] = True
        self._price.clear()
