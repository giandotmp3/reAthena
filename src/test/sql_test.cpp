// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

// Tests for the database layer (src/common/sql.cpp + sql_dialect.cpp) on the engines that replaced
// MySQL. SQLite always runs (in memory); PostgreSQL runs when RATHENA_TEST_PG is set to
// "host:port:user:password:dbname" for a DISPOSABLE database.
//
//   ctest -R sql_test            (or run ./sql-test directly)

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <common/sql.hpp>
#include <common/sql_dialect.hpp>

static constexpr size_t NAME_LENGTH = 24; // as in common/mmo.hpp

namespace dialect = rathena::sqldialect;

void Sql_inter_server_read( const char* cfgName, bool first );

// Defined by each server; the test links the common library without one.
void display_helpscreen( bool ){}
int32 parse_console( const char* ){ return 0; }

static int failures = 0;
static int checks = 0;

#define CHECK( cond ) do { ++checks; if( !( cond ) ){ ++failures; fprintf( stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond ); } } while( 0 )
#define CHECK_EQ( a, b ) do { ++checks; auto _a = ( a ); auto _b = ( b ); if( !( _a == _b ) ){ ++failures; fprintf( stderr, "%s:%d: CHECK_EQ failed: %s\n", __FILE__, __LINE__, #a " == " #b ); } } while( 0 )

static void test_dialect(){
	using dialect::Dialect;
	auto t = []( const char* q, Dialect d ){ return dialect::translate( q, d ).sql; };

	CHECK_EQ( t( "SELECT `a` FROM `char` WHERE `name` = 'x`y'", Dialect::Sqlite ), std::string( "SELECT \"a\" FROM \"char\" WHERE \"name\" = 'x`y'" ) );
	CHECK_EQ( t( "SELECT 1 FROM t WHERE a = 'it''s' LIMIT 0,10", Dialect::Postgres ), std::string( "SELECT 1 FROM t WHERE a = 'it''s' LIMIT 10 OFFSET 0" ) );
	CHECK_EQ( t( "SELECT 1 FROM `char` WHERE BINARY `name` = 'Bob'", Dialect::Postgres ), std::string( "SELECT 1 FROM \"char\" WHERE CAST(\"name\" AS text) = 'Bob'" ) );
	CHECK_EQ( t( "SELECT 1 FROM `char` WHERE BINARY `name` = 'Bob'", Dialect::Sqlite ), std::string( "SELECT 1 FROM \"char\" WHERE \"name\" COLLATE BINARY = 'Bob'" ) );
	CHECK_EQ( t( "START TRANSACTION", Dialect::Sqlite ), std::string( "BEGIN IMMEDIATE" ) );
	CHECK_EQ( t( "START TRANSACTION", Dialect::Postgres ), std::string( "START TRANSACTION" ) );
	CHECK_EQ( t( "SELECT 'BINARY `x` LIMIT 1,2'", Dialect::Postgres ), std::string( "SELECT 'BINARY `x` LIMIT 1,2'" ) ); // literals untouched

	auto pk = []( const std::string& table ) -> std::optional<std::vector<std::string>> {
		if( table == "acc_reg_num" ) return std::vector<std::string>{ "account_id", "key", "index" };
		if( table == "only_keys" ) return std::vector<std::string>{ "a", "b" };
		return std::nullopt;
	};
	CHECK_EQ( dialect::translate( "REPLACE INTO `acc_reg_num` (`account_id`,`key`,`index`,`value`) VALUES ('1','k','0','5')", Dialect::Postgres, pk ).sql,
		std::string( "INSERT INTO \"acc_reg_num\" (\"account_id\", \"key\", \"index\", \"value\") VALUES ('1','k','0','5') ON CONFLICT (\"account_id\", \"key\", \"index\") DO UPDATE SET \"value\" = EXCLUDED.\"value\"" ) );
	CHECK_EQ( dialect::translate( "REPLACE INTO `only_keys` (`a`,`b`) VALUES (1,2)", Dialect::Postgres, pk ).sql,
		std::string( "INSERT INTO \"only_keys\" (\"a\", \"b\") VALUES (1,2) ON CONFLICT (\"a\", \"b\") DO NOTHING" ) );
	CHECK( !dialect::translate( "REPLACE INTO `unknown` (`a`) VALUES (1)", Dialect::Postgres, pk ).error.empty() );
	CHECK_EQ( t( "REPLACE INTO `t` (`a`) VALUES (1)", Dialect::Sqlite ), std::string( "REPLACE INTO \"t\" (\"a\") VALUES (1)" ) );

	CHECK_EQ( dialect::count_placeholders( "SELECT ? FROM t WHERE a = '?' AND \"b?\" = ?" ), (size_t)2 );
	CHECK_EQ( dialect::number_placeholders( "UPDATE t SET a = ?, b = '?' WHERE c = ?" ), std::string( "UPDATE t SET a = $1, b = '?' WHERE c = $2" ) );
	CHECK( dialect::insert_target( "INSERT INTO `login` (a) VALUES (1)" ) == std::optional<std::string>( "login" ) );
	CHECK( dialect::insert_target( "SELECT 1" ) == std::nullopt );

	char out[64];
	CHECK_EQ( dialect::escape_string( out, "O'Brien\\", 8 ), (size_t)9 );
	CHECK_EQ( std::string( out ), std::string( "O''Brien\\" ) );
}

/// The Sql/SqlStmt contract every server relies on, run against one engine.
static void test_engine( const char* label, std::function<int32( Sql* )> connect ){
	fprintf( stderr, "-- %s\n", label );
	Sql* sql = Sql_Malloc();
	if( connect( sql ) != SQL_SUCCESS ){
		CHECK( !"connect failed" );
		Sql_Free( sql );
		return;
	}

	// The schema was created automatically; the seeded rows are present.
	CHECK( Sql_Query( sql, "SELECT `name` FROM `clan` WHERE `clan_id` = '1'" ) == SQL_SUCCESS );
	char* data = nullptr;
	CHECK( Sql_NextRow( sql ) == SQL_SUCCESS );
	CHECK( Sql_GetData( sql, 0, &data, nullptr ) == SQL_SUCCESS && data != nullptr && strcmp( data, "Swordman Clan" ) == 0 );
	CHECK( Sql_NextRow( sql ) == SQL_NO_DATA );

	// Auto-increment starts where the legacy schema did, and LastInsertId reports it.
	CHECK( Sql_Query( sql, "INSERT INTO `login` (`userid`, `user_pass`, `sex`, `email`) VALUES ('alpha01', 'pw', 'M', 'a@a.com')" ) == SQL_SUCCESS );
	CHECK_EQ( Sql_LastInsertId( sql ), (uint64)2000000 );
	CHECK( Sql_Query( sql, "INSERT INTO `char` (`account_id`, `char_num`, `name`) VALUES ('2000000', '0', 'Hero')" ) == SQL_SUCCESS );
	CHECK_EQ( Sql_LastInsertId( sql ), (uint64)150000 );

	// Names stay case-insensitive, as they were on MySQL, unless BINARY is asked for.
	CHECK( Sql_Query( sql, "SELECT `char_id` FROM `char` WHERE `name` = 'hero'" ) == SQL_SUCCESS );
	CHECK_EQ( Sql_NumRows( sql ), (uint64)1 );
	CHECK( Sql_Query( sql, "SELECT 1 FROM `char` WHERE BINARY `name` = 'hero' LIMIT 1" ) == SQL_SUCCESS );
	CHECK_EQ( Sql_NumRows( sql ), (uint64)0 );
	CHECK( Sql_Query( sql, "INSERT INTO `char` (`account_id`, `char_num`, `name`) VALUES ('2000000', '1', 'HERO')" ) == SQL_ERROR );
	CHECK_EQ( Sql_GetError( sql ), (uint32)SQL_ERRNO_DUPLICATE );

	// REPLACE INTO upserts on the primary key.
	CHECK( Sql_Query( sql, "REPLACE INTO `acc_reg_num` (`account_id`,`key`,`index`,`value`) VALUES ('2000000','#k','0','5')" ) == SQL_SUCCESS );
	CHECK( Sql_Query( sql, "REPLACE INTO `acc_reg_num` (`account_id`,`key`,`index`,`value`) VALUES ('2000000','#k','0','7')" ) == SQL_SUCCESS );
	CHECK( Sql_Query( sql, "SELECT `value` FROM `acc_reg_num` WHERE `account_id` = '2000000'" ) == SQL_SUCCESS );
	CHECK_EQ( Sql_NumRows( sql ), (uint64)1 );
	CHECK( Sql_NextRow( sql ) == SQL_SUCCESS && Sql_GetData( sql, 0, &data, nullptr ) == SQL_SUCCESS && data && strcmp( data, "7" ) == 0 );

	// Escaping keeps quotes as data.
	char esc[64];
	Sql_EscapeString( sql, esc, "it's" );
	CHECK( Sql_Query( sql, "UPDATE `login` SET `email` = '%s' WHERE `account_id` = '2000000'", esc ) == SQL_SUCCESS );
	CHECK_EQ( Sql_NumRowsAffected( sql ), (uint64)1 );
	CHECK( Sql_Query( sql, "SELECT `email` FROM `login` WHERE `account_id` = '2000000'" ) == SQL_SUCCESS );
	CHECK( Sql_NextRow( sql ) == SQL_SUCCESS && Sql_GetData( sql, 0, &data, nullptr ) == SQL_SUCCESS && data && strcmp( data, "it's" ) == 0 );

	// NULL comes back as a null pointer, like the MySQL client.
	CHECK( Sql_Query( sql, "SELECT `lastlogin` FROM `login` WHERE `account_id` = '2000000'" ) == SQL_SUCCESS );
	CHECK( Sql_NextRow( sql ) == SQL_SUCCESS && Sql_GetData( sql, 0, &data, nullptr ) == SQL_SUCCESS && data == nullptr );

	// Time helpers round-trip local time.
	CHECK( Sql_Query( sql, "UPDATE `login` SET `lastlogin` = FROM_UNIXTIME(1700000000) WHERE `account_id` = '2000000'" ) == SQL_SUCCESS );
	CHECK( Sql_Query( sql, "SELECT UNIX_TIMESTAMP(`lastlogin`) FROM `login` WHERE `account_id` = '2000000'" ) == SQL_SUCCESS );
	CHECK( Sql_NextRow( sql ) == SQL_SUCCESS && Sql_GetData( sql, 0, &data, nullptr ) == SQL_SUCCESS && data && strcmp( data, "1700000000" ) == 0 );
	CHECK( Sql_Query( sql, "SELECT COUNT(*) FROM `login` WHERE `lastlogin` < NOW()" ) == SQL_SUCCESS );

	// Transactions roll back.
	CHECK( Sql_QueryStr( sql, "START TRANSACTION" ) == SQL_SUCCESS );
	CHECK( Sql_Query( sql, "DELETE FROM `char` WHERE `char_id` = '150000'" ) == SQL_SUCCESS );
	CHECK( Sql_QueryStr( sql, "ROLLBACK" ) == SQL_SUCCESS );
	CHECK( Sql_Query( sql, "SELECT 1 FROM `char` WHERE `char_id` = '150000'" ) == SQL_SUCCESS );
	CHECK_EQ( Sql_NumRows( sql ), (uint64)1 );

	// Prepared statements: typed parameters, typed columns, truncation is an error.
	{
		SqlStmt stmt( *sql );
		int32 account_id = 2000000;
		char name[NAME_LENGTH] = "Mage";
		CHECK( stmt.Prepare( "INSERT INTO `char` (`account_id`, `char_num`, `name`, `zeny`) VALUES (?, '2', ?, '1234')" ) == SQL_SUCCESS );
		CHECK_EQ( stmt.NumParams(), (size_t)2 );
		CHECK( stmt.BindParam( 0, SQLDT_INT32, &account_id, sizeof( account_id ) ) == SQL_SUCCESS );
		CHECK( stmt.BindParam( 1, SQLDT_STRING, name, strlen( name ) ) == SQL_SUCCESS );
		CHECK( stmt.Execute() == SQL_SUCCESS );
		// The failed duplicate insert above may have consumed an id (sequences are not transactional
		// on PostgreSQL, as with MySQL InnoDB), so only require a fresh id past the first character.
		const uint64 mage_id = stmt.LastInsertId();
		CHECK( mage_id > 150000 );

		uint32 char_id = 0;
		int32 zeny = 0;
		char got[NAME_LENGTH] = {};
		char tiny[3] = {};
		uint32 len = 0;
		int8 is_null = 0;
		CHECK( stmt.Prepare( "SELECT `char_id`, `zeny`, `name`, `name` FROM `char` WHERE `account_id` = ? ORDER BY `char_id`" ) == SQL_SUCCESS );
		CHECK( stmt.BindParam( 0, SQLDT_INT32, &account_id, sizeof( account_id ) ) == SQL_SUCCESS );
		CHECK( stmt.Execute() == SQL_SUCCESS );
		CHECK_EQ( stmt.NumRows(), (uint64)2 );
		CHECK( stmt.BindColumn( 0, SQLDT_UINT32, &char_id ) == SQL_SUCCESS );
		CHECK( stmt.BindColumn( 1, SQLDT_INT32, &zeny ) == SQL_SUCCESS );
		CHECK( stmt.BindColumn( 2, SQLDT_STRING, got, sizeof( got ), &len, &is_null ) == SQL_SUCCESS );
		CHECK( stmt.BindColumn( 3, SQLDT_STRING, tiny, sizeof( tiny ) ) == SQL_SUCCESS );
		CHECK( stmt.NextRow() == SQL_ERROR ); // "Hero" does not fit in 2 chars + terminator
		CHECK( stmt.Prepare( "SELECT `char_id`, `zeny`, `name` FROM `char` WHERE `char_id` = '%" PRIu64 "'", mage_id ) == SQL_SUCCESS );
		CHECK( stmt.Execute() == SQL_SUCCESS );
		CHECK( stmt.BindColumn( 0, SQLDT_UINT32, &char_id ) == SQL_SUCCESS );
		CHECK( stmt.BindColumn( 1, SQLDT_INT32, &zeny ) == SQL_SUCCESS );
		CHECK( stmt.BindColumn( 2, SQLDT_STRING, got, sizeof( got ), &len, &is_null ) == SQL_SUCCESS );
		CHECK( stmt.NextRow() == SQL_SUCCESS );
		CHECK_EQ( (uint64)char_id, mage_id );
		CHECK_EQ( zeny, 1234 );
		CHECK( strcmp( got, "Mage" ) == 0 && len == 4 && is_null == 0 );
		CHECK( stmt.NextRow() == SQL_NO_DATA );
	}

	// Binary data survives a round trip (guild emblems).
	{
		SqlStmt stmt( *sql );
		const char blob_in[] = { 'B', 'M', '\0', '\x01', '\xff', '\'' };
		char blob_out[16] = {};
		uint32 blob_len = 0;
		int32 version = 1, guild_id = 7;
		char world[] = "World";
		char type[] = "BMP";
		CHECK( stmt.Prepare( "REPLACE INTO `guild_emblems` (`version`, `file_type`, `guild_id`, `world_name`, `file_data`) VALUES (?, ?, ?, ?, ?)" ) == SQL_SUCCESS );
		stmt.BindParam( 0, SQLDT_INT32, &version, sizeof( version ) );
		stmt.BindParam( 1, SQLDT_STRING, type, strlen( type ) );
		stmt.BindParam( 2, SQLDT_INT32, &guild_id, sizeof( guild_id ) );
		stmt.BindParam( 3, SQLDT_STRING, world, strlen( world ) );
		stmt.BindParam( 4, SQLDT_BLOB, (void*)blob_in, sizeof( blob_in ) );
		CHECK( stmt.Execute() == SQL_SUCCESS );
		CHECK( stmt.Prepare( "SELECT `file_data` FROM `guild_emblems` WHERE `guild_id` = 7" ) == SQL_SUCCESS );
		CHECK( stmt.Execute() == SQL_SUCCESS );
		CHECK( stmt.BindColumn( 0, SQLDT_BLOB, blob_out, sizeof( blob_out ), &blob_len ) == SQL_SUCCESS );
		CHECK( stmt.NextRow() == SQL_SUCCESS );
		CHECK( blob_len == sizeof( blob_in ) && memcmp( blob_in, blob_out, sizeof( blob_in ) ) == 0 );
	}

	Sql_Free( sql );
}

int main( int argc, char** argv ){
	test_dialect();

	// SQLite: a private in-memory database, schema created from sql-files/sqlite/.
	const std::string conf_dir_s = std::filesystem::temp_directory_path().string();
	const char* conf_dir = conf_dir_s.c_str();
	const std::string schema_dir = std::string( RATHENA_SOURCE_DIR ) + "/sql-files";
	{
		const std::string conf = std::string( conf_dir ) + "/rathena_sql_test_sqlite.conf";
		std::ofstream( conf ) << "db_backend: sqlite\ndb_schema_directory: " << schema_dir << "\n";
		Sql_inter_server_read( conf.c_str(), true );
		test_engine( "sqlite", []( Sql* s ){ return Sql_Connect( s, "", "", "", 0, ":memory:" ); } );
	}

#ifdef HAVE_POSTGRESQL
	if( const char* pg = std::getenv( "RATHENA_TEST_PG" ); pg != nullptr && *pg != '\0' ){
		// host:port:user:password:dbname
		std::vector<std::string> f;
		std::string cur;
		for( const char* p = pg; *p; ++p ){
			if( *p == ':' && f.size() < 4 ){ f.push_back( cur ); cur.clear(); } else cur += *p;
		}
		f.push_back( cur );
		if( f.size() == 5 ){
			const std::string conf = std::string( conf_dir ) + "/rathena_sql_test_pg.conf";
			std::ofstream( conf ) << "db_backend: postgresql\ndb_schema_directory: " << schema_dir << "\n";
			Sql_inter_server_read( conf.c_str(), true );
			// Start from an empty schema so the automatic schema creation is exercised.
			Sql* reset = Sql_Malloc();
			if( Sql_Connect( reset, f[2].c_str(), f[3].c_str(), f[0].c_str(), (uint16)atoi( f[1].c_str() ), f[4].c_str() ) == SQL_SUCCESS ){
				Sql_QueryStr( reset, "DROP SCHEMA public CASCADE" );
				Sql_QueryStr( reset, "CREATE SCHEMA public" );
			}
			Sql_Free( reset );
			test_engine( "postgresql", [&f]( Sql* s ){ return Sql_Connect( s, f[2].c_str(), f[3].c_str(), f[0].c_str(), (uint16)atoi( f[1].c_str() ), f[4].c_str() ); } );
		}else{
			fprintf( stderr, "RATHENA_TEST_PG must be host:port:user:password:dbname\n" );
			++failures;
		}
	}else{
		fprintf( stderr, "-- postgresql: skipped (set RATHENA_TEST_PG to a disposable database)\n" );
	}
#endif

	fprintf( stderr, "%d checks, %d failures\n", checks, failures );
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
