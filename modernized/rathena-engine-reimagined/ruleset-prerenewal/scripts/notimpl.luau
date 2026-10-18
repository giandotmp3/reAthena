--!strict-compatible
-- Raises the typed NOT_IMPLEMENTED error used by every unimplemented rule function.
return function(rule_id)
	error({ code = "NOT_IMPLEMENTED", rule = rule_id }, 2)
end
