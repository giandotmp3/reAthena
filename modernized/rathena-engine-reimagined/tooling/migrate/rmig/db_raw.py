"""Databases without a dedicated port yet: merged by their identity key with per-top-level-field last-wins patching.

This is a documented APPROXIMATION of each DB's parseBodyNode (see README Q-D): nested nodes are replaced wholesale,
list nodes of unkeyed DBs are appended. They are emitted as raw normalized tables and are NOT verified against the
legacy loader (TODO list in the report).
"""
from .base import Applier
from .yamlio import NULL, has

# name -> (stub, key field or None, script-carrying field names)
RAW_DBS = {
    "quest_db": ("db/quest_db.yml", "Id", ()),
    "pet_db": ("db/pet_db.yml", "Mob", ("PetScript", "PlayerScript")),
    "item_group_db": ("db/item_group_db.yml", "Group", ()),
    "item_combos": ("db/item_combos.yml", None, ("Script",)),
    "achievement_db": ("db/achievement_db.yml", "Id", ("Condition", "Script")),
    "achievement_level_db": ("db/achievement_level_db.yml", "Level", ()),
    "castle_db": ("db/castle_db.yml", "Id", ()),
    "exp_guild": ("db/exp_guild.yml", "Level", ()),
    "exp_homun": ("db/exp_homun.yml", "Level", ()),
    "guild_skill_tree": ("db/guild_skill_tree.yml", "Id", ()),
    "homunculus_db": ("db/homunculus_db.yml", "Class", ()),
    "instance_db": ("db/instance_db.yml", "Id", ()),
    "mercenary_db": ("db/mercenary_db.yml", "Id", ()),
    "mob_summon": ("db/mob_summon.yml", "Group", ()),
    "skill_tree": ("db/skill_tree.yml", "Job", ()),
    "abra_db": ("db/abra_db.yml", "Skill", ()),
    "attendance": ("db/attendance.yml", None, ()),
    "item_randomopt_db": ("db/item_randomopt_db.yml", "Id", ("Script",)),
    "item_randomopt_group": ("db/item_randomopt_group.yml", "Id", ()),
    "enchantgrade": ("db/enchantgrade.yml", "Type", ()),
    "item_enchant": ("db/item_enchant.yml", "Id", ()),
    "elemental_db": ("db/elemental_db.yml", "Id", ()),
}


class RawDb(Applier):
    def __init__(self, world, name, stub, key, script_fields, type_, version):
        Applier.__init__(self, world)
        self.NAME = name
        self.STUB = stub
        self.KEY = key
        self.SCRIPTS = script_fields
        self.TYPE = type_
        self.VERSION = version
        self.MIN_VERSION = 1
        self._seq = 0

    def clear(self):
        Applier.clear(self)

    def parse_body_node(self, node):
        if not isinstance(node, dict):
            return False
        if self.KEY is None:
            self._seq += 1
            self.rows[self._seq] = node
            return True
        k = node.get(self.KEY)
        if k is None or k is NULL or isinstance(k, (dict, list)):
            self.r.warn("raw db %s: entry without key %s" % (self.NAME, self.KEY))
            return False
        row = self.rows.get(k)
        if row is None:
            self.rows[k] = dict(node)
        else:
            row.update(node)
        self._cur_key = k
        return True
