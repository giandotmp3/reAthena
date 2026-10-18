--!nonstrict
-- PROVISIONAL partial tables; replaced by rathena-migrate output.
return {
	-- attack element -> defender element -> level-1 percent (fragment only)
	element_matrix = { water = { fire = 150 }, fire = { water = 50, earth = 150 } },
	size_matrix = {},
	cardfix_buckets = { "race", "element", "size", "race2", "class", "add_dmg", "range_rate" },
	status_modifiers = {},
}
