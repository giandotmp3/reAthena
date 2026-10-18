--!strict-compatible
-- PRE resistance descriptors (BR-RULESET-053). Stat names only, formulas live in scripts.
-- PROVISIONAL starter set; migrator supplies the full table.
return {
	stun = { sc_def = "vit", sc_def2 = "luk" },
	poison = { sc_def = "vit", sc_def2 = "luk" },
	silence = { sc_def = "vit", sc_def2 = "luk" },
	bleeding = { sc_def = "vit", sc_def2 = "luk" },
	sleep = { sc_def = "int", sc_def2 = "luk" },
	curse = { sc_def = "luk", sc_def2 = "luk" },
	stone = { sc_def = "mdef", sc_def2 = "luk" },
	freeze = { sc_def = "mdef", sc_def2 = "luk" },
	confusion = { sc_def = "str+int", sc_def2 = "luk" },
}
