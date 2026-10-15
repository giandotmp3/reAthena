// Acceptance tests: CAP-26 config with `import:` overlays and ${ENV} placeholders (S1).
#include <gtest/gtest.h>

#include <functional>

#include "rathena/core/config.hpp"

using namespace rathena::core;

namespace {
ConfigErrc error_of(const std::function<void()>& fn) {
	try {
		fn();
	} catch (const ConfigError& e) {
		return e.code();
	}
	ADD_FAILURE() << "expected ConfigError";
	return ConfigErrc::Syntax;
}
} // namespace

TEST(CAP_26_ConfigLoader, CAP_26_ImportOverlayOverridesEarlierDefinitions) {
	const MapEnvironment env;
	const auto cfg = ConfigLoader(env).load_file(RATHENA_CORE_FIXTURES_DIR "/battle_base.conf");
	EXPECT_EQ(cfg.get("base_exp_rate").value(), "500");
	EXPECT_EQ(cfg.get("multi_level_up").value(), "yes");
	EXPECT_EQ(cfg.get("vending_tax").value(), "500");
	const auto* e = cfg.find("base_exp_rate");
	ASSERT_NE(e, nullptr);
	EXPECT_EQ(e->file.filename(), "battle_overlay.conf");
}

TEST(CAP_26_ConfigLoader, CAP_26_DefinitionAfterImportWins) {
	const MapEnvironment env;
	const auto cfg = ConfigLoader(env).load_file(RATHENA_CORE_FIXTURES_DIR "/battle_base.conf");
	EXPECT_EQ(cfg.get("job_exp_rate").value(), "300");
}

TEST(CAP_26_ConfigLoader, CAP_26_MissingImportIsWarningByDefault) {
	const MapEnvironment env;
	const auto cfg = ConfigLoader(env).load_file(RATHENA_CORE_FIXTURES_DIR "/battle_base.conf");
	ASSERT_EQ(cfg.warnings().size(), 1u);
	EXPECT_NE(cfg.warnings()[0].find("does_not_exist.conf"), std::string::npos);
}

TEST(CAP_26_ConfigLoader, CAP_26_MissingImportIsErrorWhenStrict) {
	const MapEnvironment env;
	ConfigLoader::Options o;
	o.missing_import_is_error = true;
	EXPECT_EQ(error_of([&] { (void)ConfigLoader(env, o).load_file(RATHENA_CORE_FIXTURES_DIR "/battle_base.conf"); }),
	          ConfigErrc::FileNotFound);
}

TEST(CAP_26_ConfigLoader, CAP_26_LegacyImportBaseIsWorkingDirectory) {
	// Legacy files say `import: conf/import/x.txt` relative to the server's working directory.
	const MapEnvironment env;
	ConfigLoader::Options o;
	o.import_base = RATHENA_CORE_FIXTURES_DIR;
	const auto cfg = ConfigLoader(env, o).load_string("import: imports/battle_overlay.conf\n", "/nonexistent/root.conf");
	EXPECT_EQ(cfg.get("job_exp_rate").value(), "200");
}

TEST(CAP_26_ConfigLoader, CAP_26_ImportCycleRefused) {
	const MapEnvironment env;
	EXPECT_EQ(error_of([&] { (void)ConfigLoader(env).load_file(RATHENA_CORE_FIXTURES_DIR "/cycle_a.conf"); }),
	          ConfigErrc::ImportCycle);
}

TEST(CAP_26_ConfigLoader, CAP_26_MissingRootFileRefused) {
	const MapEnvironment env;
	EXPECT_EQ(error_of([&] { (void)ConfigLoader(env).load_file(RATHENA_CORE_FIXTURES_DIR "/nope.conf"); }),
	          ConfigErrc::FileNotFound);
}

TEST(CAP_26_ConfigLoader, CAP_26_CommentsAndUrlValues) {
	const MapEnvironment env;
	const auto cfg = ConfigLoader(env).load_string(
		"// full-line comment\n"
		"server_name: rAthena   // trailing comment\n"
		"\n"
		"web_url: http://example.invalid/path\n",
		"inline.conf");
	EXPECT_EQ(cfg.get("server_name").value(), "rAthena");
	EXPECT_EQ(cfg.get("web_url").value(), "http://example.invalid/path");
	EXPECT_EQ(cfg.entries().size(), 2u);
}

TEST(CAP_26_ConfigLoader, CAP_26_SyntaxErrorReportsLocation) {
	const MapEnvironment env;
	try {
		(void)ConfigLoader(env).load_string("ok: 1\nthis line has no colon\n", "inline.conf");
		FAIL();
	} catch (const ConfigError& e) {
		EXPECT_EQ(e.code(), ConfigErrc::Syntax);
		EXPECT_NE(std::string(e.what()).find("inline.conf:2"), std::string::npos) << e.what();
	}
}

TEST(CAP_26_ConfigLoader, CAP_26_MergedWithAppliesOverlay) {
	const MapEnvironment env;
	const ConfigLoader l(env);
	const auto base = l.load_string("a: 1\nb: 2\n", "base.conf");
	const auto merged = base.merged_with(l.load_string("b: 3\nc: 4\n", "over.conf"));
	EXPECT_EQ(merged.get("a").value(), "1");
	EXPECT_EQ(merged.get("b").value(), "3");
	EXPECT_EQ(merged.get("c").value(), "4");
	EXPECT_EQ(base.get("b").value(), "2"); // original untouched
}

// ---------------------------------------------------------------- ${ENV} placeholders (S1)

TEST(S1_EnvPlaceholders, S1_PlaceholderExpandedFromInjectedEnvironment) {
	MapEnvironment env;
	env.set("CHAR_BIND", "127.0.0.1");
	const auto cfg = ConfigLoader(env).load_string("bind_ip: ${CHAR_BIND}\n", "inline.conf");
	const auto* e = cfg.find("bind_ip");
	ASSERT_NE(e, nullptr);
	EXPECT_EQ(e->value, "127.0.0.1");
	EXPECT_EQ(e->raw, "${CHAR_BIND}");
	EXPECT_TRUE(e->from_env);
}

TEST(S1_EnvPlaceholders, S1_MissingEnvRefusedAndNamesVariableOnly) {
	const MapEnvironment env;
	try {
		(void)ConfigLoader(env).load_string("passwd: ${INTER_SERVER_PASS}\n", "inline.conf");
		FAIL();
	} catch (const ConfigError& e) {
		EXPECT_EQ(e.code(), ConfigErrc::MissingEnv);
		EXPECT_NE(std::string(e.what()).find("INTER_SERVER_PASS"), std::string::npos);
	}
}

TEST(S1_EnvPlaceholders, S1_EscapedPlaceholderIsLiteral) {
	const MapEnvironment env;
	const auto cfg = ConfigLoader(env).load_string("msg: cost $${X}\n", "inline.conf");
	EXPECT_EQ(cfg.get("msg").value(), "cost ${X}");
	EXPECT_FALSE(cfg.find("msg")->from_env);
}

TEST(S1_EnvPlaceholders, S1_MalformedPlaceholderRefused) {
	const MapEnvironment env;
	EXPECT_EQ(error_of([&] { (void)ConfigLoader(env).load_string("a: ${UNCLOSED\n", "i.conf"); }), ConfigErrc::Syntax);
	EXPECT_EQ(error_of([&] { (void)ConfigLoader(env).load_string("a: ${}\n", "i.conf"); }), ConfigErrc::Syntax);
	EXPECT_EQ(error_of([&] { (void)ConfigLoader(env).load_string("a: ${BAD-NAME}\n", "i.conf"); }), ConfigErrc::Syntax);
}

TEST(S1_EnvPlaceholders, S1_ProcessEnvironmentReadsRealEnv) {
	const ProcessEnvironment env;
	EXPECT_FALSE(env.get("RATHENA_CORE_TEST_DEFINITELY_UNSET_9f3a").has_value());
}
