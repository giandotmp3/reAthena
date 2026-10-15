// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

// Database access for every server: SQLite (always built in) and PostgreSQL (HAVE_POSTGRESQL).
// MySQL/MariaDB is no longer supported -- see analysis/rathena/DATABASE_DECISION.md.
//
// The public API (Sql_* and SqlStmt) is unchanged. Queries keep their MySQL spelling and are
// translated by sql_dialect before they reach the engine. Results are buffered as text, which
// matches how the MySQL client delivered them (Sql_GetData) and is converted to the bound C
// types on SqlStmt::NextRow.

#include "sql.hpp"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <sqlite3.h>
#ifdef HAVE_POSTGRESQL
#include <libpq-fe.h>
#endif

#include "cbasetypes.hpp"
#include "cli.hpp"
#include "core.hpp"
#include "malloc.hpp"
#include "showmsg.hpp"
#include "sql_dialect.hpp"
#include "strlib.hpp"

namespace dialect = rathena::sqldialect;

///////////////////////////////////////////////////////////////////////////////
// Configuration (conf/inter_athena.conf)
///////////////////////////////////////////////////////////////////////////////

enum class e_db_backend { sqlite, postgresql };

static e_db_backend db_backend = e_db_backend::sqlite;
static std::string sqlite_directory = "save";
static bool db_auto_create_schema = true;
static std::string db_schema_directory = "sql-files";
int32 db_reconnect_type = 2;
uint32 db_reconnect_count = 1;

///////////////////////////////////////////////////////////////////////////////
// Engine abstraction
///////////////////////////////////////////////////////////////////////////////

namespace {

/// A bound parameter, already in engine-ready form.
struct Param {
	enum class Kind { Null, Int, UInt, Double, Text, Blob } kind = Kind::Null;
	int64 i = 0;
	uint64 u = 0;
	double d = 0;
	std::string bytes; // Text or Blob
};

struct ResultSet {
	std::vector<std::string> names;
	std::vector<std::vector<std::optional<std::string>>> rows;
	bool has_result = false; // statement produced a result set (SELECT-like)
	void clear(){ names.clear(); rows.clear(); has_result = false; }
};

class Engine {
public:
	virtual ~Engine() = default;
	virtual bool connect( const char* user, const char* passwd, const char* host, uint16 port, const char* db ) = 0;
	/// Runs one statement. On failure fills `error` and `errnum` (SQL_ERRNO_DUPLICATE for key violations).
	virtual bool exec( const std::string& sql, const std::vector<Param>& params, ResultSet& out ) = 0;
	virtual uint64 last_insert_id() = 0;
	virtual uint64 affected_rows() = 0;
	virtual bool ping() = 0;
	virtual bool set_encoding( const char* encoding ) = 0;
	virtual dialect::Dialect sql_dialect() const = 0;
	virtual void close() = 0;
	/// Runs a whole schema file (several statements). Only used to create a fresh database.
	virtual bool exec_script( const std::string& script ) = 0;
	virtual bool has_table( const char* table ) = 0;

	std::string error;
	uint32 errnum = 0;
};

// ---------------------------------------------------------------- SQLite

/// local time as 'YYYY-MM-DD HH:MM:SS', the format MySQL used for DATETIME
static std::string format_local_time( time_t t ){
	struct tm lt;
#ifdef _WIN32
	localtime_s( &lt, &t );
#else
	localtime_r( &t, &lt );
#endif
	char buf[32];
	strftime( buf, sizeof( buf ), "%Y-%m-%d %H:%M:%S", &lt );
	return buf;
}

static bool parse_local_time( const char* s, time_t& out ){
	struct tm lt = {};
	if( sscanf( s, "%d-%d-%d %d:%d:%d", &lt.tm_year, &lt.tm_mon, &lt.tm_mday, &lt.tm_hour, &lt.tm_min, &lt.tm_sec ) < 3 ){
		return false;
	}
	lt.tm_year -= 1900;
	lt.tm_mon -= 1;
	lt.tm_isdst = -1;
	out = mktime( &lt );
	return true;
}

static void sqlite_now( sqlite3_context* ctx, int, sqlite3_value** ){
	const std::string s = format_local_time( time( nullptr ) );
	sqlite3_result_text( ctx, s.c_str(), -1, SQLITE_TRANSIENT );
}

static void sqlite_unix_timestamp( sqlite3_context* ctx, int argc, sqlite3_value** argv ){
	if( argc == 0 ){
		sqlite3_result_int64( ctx, (sqlite3_int64)time( nullptr ) );
		return;
	}
	if( sqlite3_value_type( argv[0] ) == SQLITE_NULL ){
		sqlite3_result_null( ctx );
		return;
	}
	time_t t;
	const char* s = (const char*)sqlite3_value_text( argv[0] );
	if( s == nullptr || !parse_local_time( s, t ) ){
		sqlite3_result_int64( ctx, 0 );
		return;
	}
	sqlite3_result_int64( ctx, (sqlite3_int64)t );
}

static void sqlite_from_unixtime( sqlite3_context* ctx, int, sqlite3_value** argv ){
	if( sqlite3_value_type( argv[0] ) == SQLITE_NULL ){
		sqlite3_result_null( ctx );
		return;
	}
	const std::string s = format_local_time( (time_t)sqlite3_value_int64( argv[0] ) );
	sqlite3_result_text( ctx, s.c_str(), -1, SQLITE_TRANSIENT );
}

class SqliteEngine final : public Engine {
	sqlite3* db = nullptr;
public:
	~SqliteEngine() override { this->close(); }

	dialect::Dialect sql_dialect() const override { return dialect::Dialect::Sqlite; }

	bool connect( const char*, const char*, const char*, uint16, const char* dbname ) override {
		std::string path = dbname != nullptr ? dbname : "";
		// A bare name ("ragnarok") is a database name; a path or file name is used as given.
		if( path.empty() ){
			path = "rathena";
		}
		if( path != ":memory:" && path.find( '/' ) == std::string::npos && path.find( '\\' ) == std::string::npos && path.find( '.' ) == std::string::npos ){
			path = sqlite_directory + "/" + path + ".db";
		}
		if( sqlite3_open_v2( path.c_str(), &this->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr ) != SQLITE_OK ){
			this->error = std::string( "cannot open SQLite database '" ) + path + "': " + ( this->db ? sqlite3_errmsg( this->db ) : "out of memory" );
			this->close();
			return false;
		}
		sqlite3_extended_result_codes( this->db, 1 );
		// Several server processes may share the file: wait for the write lock instead of failing.
		sqlite3_busy_timeout( this->db, 10000 );
		sqlite3_create_function( this->db, "now", 0, SQLITE_UTF8, nullptr, sqlite_now, nullptr, nullptr );
		sqlite3_create_function( this->db, "unix_timestamp", 0, SQLITE_UTF8, nullptr, sqlite_unix_timestamp, nullptr, nullptr );
		sqlite3_create_function( this->db, "unix_timestamp", 1, SQLITE_UTF8, nullptr, sqlite_unix_timestamp, nullptr, nullptr );
		sqlite3_create_function( this->db, "from_unixtime", 1, SQLITE_UTF8, nullptr, sqlite_from_unixtime, nullptr, nullptr );
		sqlite3_exec( this->db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr );
		if( path != ":memory:" ){
			sqlite3_exec( this->db, "PRAGMA journal_mode = WAL", nullptr, nullptr, nullptr );
			sqlite3_exec( this->db, "PRAGMA synchronous = NORMAL", nullptr, nullptr, nullptr );
		}
		return true;
	}

	bool exec( const std::string& sql, const std::vector<Param>& params, ResultSet& out ) override {
		out.clear();
		this->errnum = 0;
		sqlite3_stmt* st = nullptr;
		const char* tail = nullptr;
		if( sqlite3_prepare_v2( this->db, sql.c_str(), (int)sql.size(), &st, &tail ) != SQLITE_OK ){
			this->set_error();
			return false;
		}
		if( st == nullptr ){
			return true; // empty statement
		}
		if( tail != nullptr ){
			while( *tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n' || *tail == ';' ) ++tail;
			if( *tail != '\0' ){
				sqlite3_finalize( st );
				this->error = "only one SQL statement per query is allowed";
				return false;
			}
		}
		for( size_t i = 0; i < params.size(); ++i ){
			const Param& p = params[i];
			const int idx = (int)i + 1;
			switch( p.kind ){
				case Param::Kind::Null: sqlite3_bind_null( st, idx ); break;
				case Param::Kind::Int: sqlite3_bind_int64( st, idx, p.i ); break;
				case Param::Kind::UInt: sqlite3_bind_int64( st, idx, (sqlite3_int64)p.u ); break;
				case Param::Kind::Double: sqlite3_bind_double( st, idx, p.d ); break;
				case Param::Kind::Text: sqlite3_bind_text( st, idx, p.bytes.data(), (int)p.bytes.size(), SQLITE_TRANSIENT ); break;
				case Param::Kind::Blob: sqlite3_bind_blob( st, idx, p.bytes.data(), (int)p.bytes.size(), SQLITE_TRANSIENT ); break;
			}
		}
		const int ncols = sqlite3_column_count( st );
		out.has_result = ncols > 0;
		for( int c = 0; c < ncols; ++c ){
			out.names.emplace_back( sqlite3_column_name( st, c ) );
		}
		for( ;; ){
			const int rc = sqlite3_step( st );
			if( rc == SQLITE_DONE ){
				break;
			}
			if( rc != SQLITE_ROW ){
				this->set_error();
				sqlite3_finalize( st );
				return false;
			}
			std::vector<std::optional<std::string>> row( (size_t)ncols );
			for( int c = 0; c < ncols; ++c ){
				switch( sqlite3_column_type( st, c ) ){
					case SQLITE_NULL:
						break;
					case SQLITE_INTEGER:
						row[c] = std::to_string( (long long)sqlite3_column_int64( st, c ) );
						break;
					case SQLITE_FLOAT: {
						char buf[64];
						snprintf( buf, sizeof( buf ), "%.17g", sqlite3_column_double( st, c ) );
						row[c] = buf;
						break;
					}
					default: {
						const void* data = ( sqlite3_column_type( st, c ) == SQLITE_BLOB ) ? sqlite3_column_blob( st, c ) : (const void*)sqlite3_column_text( st, c );
						const int len = sqlite3_column_bytes( st, c );
						row[c] = std::string( data ? (const char*)data : "", (size_t)len );
					}
				}
			}
			out.rows.push_back( std::move( row ) );
		}
		sqlite3_finalize( st );
		return true;
	}

	uint64 last_insert_id() override { return this->db ? (uint64)sqlite3_last_insert_rowid( this->db ) : 0; }
	uint64 affected_rows() override { return this->db ? (uint64)sqlite3_changes( this->db ) : 0; }
	bool ping() override { return this->db != nullptr; }
	bool set_encoding( const char* ) override { return true; } // bytes are stored as given

	void close() override {
		if( this->db != nullptr ){
			sqlite3_close_v2( this->db );
			this->db = nullptr;
		}
	}

	bool exec_script( const std::string& script ) override {
		char* err = nullptr;
		if( sqlite3_exec( this->db, script.c_str(), nullptr, nullptr, &err ) != SQLITE_OK ){
			this->error = err ? err : "unknown error";
			sqlite3_free( err );
			return false;
		}
		return true;
	}

	bool has_table( const char* table ) override {
		ResultSet rs;
		Param p;
		p.kind = Param::Kind::Text;
		p.bytes = table;
		return this->exec( "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?", { p }, rs ) && !rs.rows.empty();
	}

private:
	void set_error(){
		this->error = sqlite3_errmsg( this->db );
		const int ext = sqlite3_extended_errcode( this->db );
		this->errnum = ( ext == SQLITE_CONSTRAINT_UNIQUE || ext == SQLITE_CONSTRAINT_PRIMARYKEY ) ? SQL_ERRNO_DUPLICATE : (uint32)ext;
	}
};

// ---------------------------------------------------------------- PostgreSQL

#ifdef HAVE_POSTGRESQL
/// MySQL character-set names used in rAthena configs -> PostgreSQL client_encoding names.
static std::string pg_encoding_name( std::string enc ){
	for( auto& ch : enc ) ch = (char)tolower( (unsigned char)ch );
	static const std::map<std::string, std::string> names = {
		{ "utf8", "UTF8" }, { "utf8mb4", "UTF8" }, { "latin1", "LATIN1" }, { "euckr", "EUC_KR" },
		{ "euc-kr", "EUC_KR" }, { "cp949", "UHC" }, { "gbk", "GBK" }, { "big5", "BIG5" }, { "sjis", "SJIS" },
		{ "ujis", "EUC_JP" }, { "cp1250", "WIN1250" }, { "cp1251", "WIN1251" }, { "cp1252", "WIN1252" },
		{ "tis620", "WIN874" }, { "greek", "ISO_8859_7" }, { "hebrew", "ISO_8859_8" }, { "koi8r", "KOI8R" },
	};
	const auto it = names.find( enc );
	return it != names.end() ? it->second : enc;
}

class PgEngine final : public Engine {
	PGconn* conn = nullptr;
	uint64 last_id = 0;
	uint64 last_affected = 0;
	std::map<std::string, std::vector<std::string>> pk_cache;
	std::map<std::string, std::string> identity_cache; // table -> identity column ("" if none)

public:
	~PgEngine() override { this->close(); }

	dialect::Dialect sql_dialect() const override { return dialect::Dialect::Postgres; }

	bool connect( const char* user, const char* passwd, const char* host, uint16 port, const char* db ) override {
		const std::string port_s = std::to_string( port ? port : 5432 );
		const char* keys[] = { "host", "port", "user", "password", "dbname", "application_name", "connect_timeout", nullptr };
		const char* vals[] = { host, port_s.c_str(), user, passwd, db, SERVER_NAME ? SERVER_NAME : "rathena", "10", nullptr };
		this->conn = PQconnectdbParams( keys, vals, 0 );
		if( this->conn == nullptr || PQstatus( this->conn ) != CONNECTION_OK ){
			this->error = this->conn ? PQerrorMessage( this->conn ) : "out of memory";
			this->close();
			return false;
		}
		// Server NOTICEs ("relation already exists, skipping", ...) are informational only.
		PQsetNoticeProcessor( this->conn, []( void*, const char* ){}, nullptr );
		return this->session_setup();
	}

	bool exec( const std::string& sql, const std::vector<Param>& params, ResultSet& out ) override {
		out.clear();
		this->errnum = 0;
		this->last_affected = 0;
		auto translated = dialect::translate( sql, dialect::Dialect::Postgres, [this]( const std::string& t ){ return this->primary_key( t ); } );
		if( !translated.error.empty() ){
			this->error = translated.error;
			return false;
		}
		std::string q = dialect::number_placeholders( translated.sql );

		// MySQL's LAST_INSERT_ID(): return the identity value of the inserted row.
		std::string identity;
		if( const auto target = dialect::insert_target( q ); target.has_value() && q.find( "RETURNING" ) == std::string::npos ){
			identity = this->identity_column( *target );
			if( !identity.empty() ){
				while( !q.empty() && ( q.back() == ';' || isspace( (unsigned char)q.back() ) ) ) q.pop_back();
				q += " RETURNING \"" + identity + "\"";
			}
		}

		PGresult* res = this->run( q, params );
		if( res == nullptr && db_reconnect_type != 0 && PQstatus( this->conn ) == CONNECTION_BAD ){
			if( this->reconnect() ){
				res = this->run( q, params );
			}
		}
		if( res == nullptr ){
			return false;
		}

		const char* affected = PQcmdTuples( res );
		this->last_affected = ( affected && *affected ) ? strtoull( affected, nullptr, 10 ) : 0;
		if( !identity.empty() ){
			if( PQntuples( res ) > 0 && !PQgetisnull( res, 0, 0 ) ){
				this->last_id = strtoull( PQgetvalue( res, 0, 0 ), nullptr, 10 );
			}
			PQclear( res );
			return true; // INSERT: no result set, as with MySQL
		}
		if( PQresultStatus( res ) == PGRES_TUPLES_OK ){
			out.has_result = true;
			const int ncols = PQnfields( res );
			for( int c = 0; c < ncols; ++c ){
				out.names.emplace_back( PQfname( res, c ) );
			}
			const int nrows = PQntuples( res );
			out.rows.reserve( (size_t)nrows );
			for( int r = 0; r < nrows; ++r ){
				std::vector<std::optional<std::string>> row( (size_t)ncols );
				for( int c = 0; c < ncols; ++c ){
					if( PQgetisnull( res, r, c ) ){
						continue;
					}
					if( PQftype( res, c ) == 17 ){ // bytea arrives hex-encoded in text mode
						size_t len = 0;
						unsigned char* raw = PQunescapeBytea( (const unsigned char*)PQgetvalue( res, r, c ), &len );
						row[c] = std::string( (const char*)raw, len );
						PQfreemem( raw );
					}else{
						row[c] = std::string( PQgetvalue( res, r, c ), (size_t)PQgetlength( res, r, c ) );
					}
				}
				out.rows.push_back( std::move( row ) );
			}
		}
		PQclear( res );
		return true;
	}

	uint64 last_insert_id() override { return this->last_id; }
	uint64 affected_rows() override { return this->last_affected; }

	bool ping() override {
		if( this->conn == nullptr ) return false;
		PGresult* r = PQexec( this->conn, "SELECT 1" );
		const bool ok = r && PQresultStatus( r ) == PGRES_TUPLES_OK;
		PQclear( r );
		return ok || this->reconnect();
	}

	bool set_encoding( const char* encoding ) override {
		const std::string name = pg_encoding_name( encoding );
		return PQsetClientEncoding( this->conn, name.c_str() ) == 0;
	}

	void close() override {
		if( this->conn != nullptr ){
			PQfinish( this->conn );
			this->conn = nullptr;
		}
	}

	bool exec_script( const std::string& script ) override {
		PGresult* r = PQexec( this->conn, script.c_str() ); // simple protocol: several statements
		const bool ok = r && ( PQresultStatus( r ) == PGRES_COMMAND_OK || PQresultStatus( r ) == PGRES_TUPLES_OK );
		if( !ok ) this->error = PQerrorMessage( this->conn );
		PQclear( r );
		return ok;
	}

	bool has_table( const char* table ) override {
		ResultSet rs;
		Param p;
		p.kind = Param::Kind::Text;
		p.bytes = table;
		return this->exec( "SELECT 1 FROM information_schema.tables WHERE table_schema = current_schema() AND table_name = ?", { p }, rs ) && !rs.rows.empty();
	}

private:
	bool session_setup(){
		// Datetimes are stored as local time (timestamp without time zone), like MySQL's NOW().
		const time_t now = time( nullptr );
		struct tm lt, gt;
#ifdef _WIN32
		localtime_s( &lt, &now );
		gmtime_s( &gt, &now );
#else
		localtime_r( &now, &lt );
		gmtime_r( &now, &gt );
#endif
		lt.tm_isdst = 0;
		gt.tm_isdst = 0;
		const long offset_min = (long)difftime( mktime( &lt ), mktime( &gt ) ) / 60;
		char tz[96];
		snprintf( tz, sizeof( tz ), "SET TIME ZONE INTERVAL '%+03ld:%02ld' HOUR TO MINUTE", offset_min / 60, labs( offset_min % 60 ) );
		PGresult* r = PQexec( this->conn, tz );
		const bool ok = r && PQresultStatus( r ) == PGRES_COMMAND_OK;
		if( !ok ) this->error = PQerrorMessage( this->conn );
		PQclear( r );
		return ok;
	}

	bool reconnect(){
		static uint32 retries = 0;
		PQreset( this->conn );
		if( PQstatus( this->conn ) == CONNECTION_OK && this->session_setup() ){
			retries = 0;
			ShowInfo( "Reconnected to the PostgreSQL server.\n" );
			return true;
		}
		if( db_reconnect_type == 1 && ++retries > db_reconnect_count ){
			ShowFatalError( "The database has been unreachable for too long, %u reconnects were attempted. Shutting down.\n", retries );
			exit( EXIT_FAILURE );
		}
		return false;
	}

	PGresult* run( const std::string& q, const std::vector<Param>& params ){
		std::vector<std::string> text( params.size() );
		std::vector<const char*> values( params.size() );
		std::vector<int> lengths( params.size() ), formats( params.size() );
		std::vector<Oid> types( params.size() );
		for( size_t i = 0; i < params.size(); ++i ){
			const Param& p = params[i];
			switch( p.kind ){
				case Param::Kind::Null: values[i] = nullptr; break;
				case Param::Kind::Int: text[i] = std::to_string( (long long)p.i ); values[i] = text[i].c_str(); break;
				case Param::Kind::UInt: text[i] = std::to_string( (unsigned long long)p.u ); values[i] = text[i].c_str(); break;
				case Param::Kind::Double: { char b[64]; snprintf( b, sizeof( b ), "%.17g", p.d ); text[i] = b; values[i] = text[i].c_str(); break; }
				case Param::Kind::Text: values[i] = p.bytes.c_str(); break;
				case Param::Kind::Blob: values[i] = p.bytes.data(); lengths[i] = (int)p.bytes.size(); formats[i] = 1; types[i] = 17; break;
			}
		}
		PGresult* res = PQexecParams( this->conn, q.c_str(), (int)params.size(), types.data(), values.data(), lengths.data(), formats.data(), 0 );
		const ExecStatusType st = res ? PQresultStatus( res ) : PGRES_FATAL_ERROR;
		if( st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK ){
			return res;
		}
		this->error = PQerrorMessage( this->conn );
		const char* state = res ? PQresultErrorField( res, PG_DIAG_SQLSTATE ) : nullptr;
		this->errnum = ( state && strcmp( state, "23505" ) == 0 ) ? SQL_ERRNO_DUPLICATE : 1;
		PQclear( res );
		return nullptr;
	}

	std::optional<std::vector<std::string>> primary_key( const std::string& table ){
		if( auto it = this->pk_cache.find( table ); it != this->pk_cache.end() ){
			return it->second;
		}
		const char* q = "SELECT a.attname FROM pg_index i JOIN pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = ANY(i.indkey) "
			"WHERE i.indrelid = to_regclass($1) AND i.indisprimary ORDER BY array_position(i.indkey, a.attnum)";
		const std::string quoted = "\"" + table + "\"";
		const char* vals[] = { quoted.c_str() };
		PGresult* r = PQexecParams( this->conn, q, 1, nullptr, vals, nullptr, nullptr, 0 );
		std::vector<std::string> cols;
		if( r && PQresultStatus( r ) == PGRES_TUPLES_OK ){
			for( int i = 0; i < PQntuples( r ); ++i ) cols.emplace_back( PQgetvalue( r, i, 0 ) );
		}
		PQclear( r );
		if( cols.empty() ) return std::nullopt;
		this->pk_cache[table] = cols;
		return cols;
	}

	std::string identity_column( const std::string& table ){
		if( auto it = this->identity_cache.find( table ); it != this->identity_cache.end() ){
			return it->second;
		}
		const char* q = "SELECT attname FROM pg_attribute WHERE attrelid = to_regclass($1) AND attidentity <> '' AND NOT attisdropped LIMIT 1";
		const std::string quoted = "\"" + table + "\"";
		const char* vals[] = { quoted.c_str() };
		PGresult* r = PQexecParams( this->conn, q, 1, nullptr, vals, nullptr, nullptr, 0 );
		std::string col;
		if( r && PQresultStatus( r ) == PGRES_TUPLES_OK && PQntuples( r ) > 0 ){
			col = PQgetvalue( r, 0, 0 );
		}
		PQclear( r );
		this->identity_cache[table] = col;
		return col;
	}
};
#endif

/// Creates the schema in an empty database (db_auto_create_schema).
static bool create_schema_if_empty( Engine& engine ){
	if( !db_auto_create_schema || engine.has_table( "login" ) ){
		return true;
	}
	const char* sub = ( db_backend == e_db_backend::postgresql ) ? "postgresql" : "sqlite";
	ShowStatus( "Database is empty: creating the schema from %s/%s/ ...\n", db_schema_directory.c_str(), sub );
	for( const char* file : { "main.sql", "logs.sql", "web.sql", "roulette_default_data.sql" } ){
		const std::string path = db_schema_directory + "/" + sub + "/" + file;
		std::ifstream in( path, std::ios::binary );
		if( !in ){
			ShowError( "Schema file not found: %s\n", path.c_str() );
			return false;
		}
		std::stringstream ss;
		ss << in.rdbuf();
		if( !engine.exec_script( ss.str() ) ){
			ShowError( "Creating the schema from %s failed: %s\n", path.c_str(), engine.error.c_str() );
			return false;
		}
	}
	ShowStatus( "Database schema created.\n" );
	return true;
}

/// MySQL-typed buffer -> engine parameter
static bool to_param( Param& p, SqlDataType type, const void* buffer, size_t len ){
	p = Param{};
	switch( type ){
		case SQLDT_NULL: p.kind = Param::Kind::Null; return true;
		case SQLDT_INT8: p.kind = Param::Kind::Int; p.i = *(const int8*)buffer; return true;
		case SQLDT_INT16: p.kind = Param::Kind::Int; p.i = *(const int16*)buffer; return true;
		case SQLDT_INT32: p.kind = Param::Kind::Int; p.i = *(const int32*)buffer; return true;
		case SQLDT_INT64: p.kind = Param::Kind::Int; p.i = *(const int64*)buffer; return true;
		case SQLDT_UINT8: p.kind = Param::Kind::UInt; p.u = *(const uint8*)buffer; return true;
		case SQLDT_UINT16: p.kind = Param::Kind::UInt; p.u = *(const uint16*)buffer; return true;
		case SQLDT_UINT32: p.kind = Param::Kind::UInt; p.u = *(const uint32*)buffer; return true;
		case SQLDT_UINT64: p.kind = Param::Kind::UInt; p.u = *(const uint64*)buffer; return true;
		case SQLDT_CHAR: p.kind = Param::Kind::Int; p.i = *(const char*)buffer; return true;
		case SQLDT_UCHAR: p.kind = Param::Kind::UInt; p.u = *(const unsigned char*)buffer; return true;
		case SQLDT_LONG: p.kind = Param::Kind::Int; p.i = *(const long*)buffer; return true;
		case SQLDT_ULONG: p.kind = Param::Kind::UInt; p.u = *(const unsigned long*)buffer; return true;
		case SQLDT_LONGLONG: p.kind = Param::Kind::Int; p.i = *(const int64*)buffer; return true;
		case SQLDT_ULONGLONG: p.kind = Param::Kind::UInt; p.u = *(const uint64*)buffer; return true;
		case SQLDT_FLOAT: p.kind = Param::Kind::Double; p.d = *(const float*)buffer; return true;
		case SQLDT_DOUBLE: p.kind = Param::Kind::Double; p.d = *(const double*)buffer; return true;
		case SQLDT_STRING:
		case SQLDT_ENUM: {
			// The MySQL client sent buffer_len bytes; text columns cannot hold NUL, so stop there.
			const char* s = (const char*)buffer;
			p.kind = Param::Kind::Text;
			p.bytes.assign( s, strnlen( s, len ) );
			return true;
		}
		case SQLDT_BLOB: p.kind = Param::Kind::Blob; p.bytes.assign( (const char*)buffer, len ); return true;
		default:
			ShowDebug( "to_param: unsupported buffer type (%d)\n", type );
			return false;
	}
}

static std::unique_ptr<Engine> make_engine(){
#ifdef HAVE_POSTGRESQL
	if( db_backend == e_db_backend::postgresql ){
		return std::make_unique<PgEngine>();
	}
#endif
	return std::make_unique<SqliteEngine>();
}

} // namespace

/// Sql handle
struct Sql
{
	StringBuf buf;
	std::unique_ptr<Engine> engine;
	ResultSet result;
	size_t next_row = 0;      // index of the row NextRow fetches next
	bool row_valid = false;   // a row is current (after a successful NextRow)
	uint64 affected = 0;
	uint64 insert_id = 0;
};

///////////////////////////////////////////////////////////////////////////////
// Sql Handle
///////////////////////////////////////////////////////////////////////////////

Sql* Sql_Malloc(void)
{
	Sql* self = new Sql();
	StringBuf_Init( &self->buf ); // the constructor does not allocate
	self->engine = make_engine();
	return self;
}

uint32 Sql_GetError( Sql* self ){
	return ( self && self->engine ) ? self->engine->errnum : 0;
}

int32 Sql_Connect(Sql* self, const char* user, const char* passwd, const char* host, uint16 port, const char* db)
{
	if( self == nullptr )
		return SQL_ERROR;

	StringBuf_Clear( &self->buf );
#ifndef HAVE_POSTGRESQL
	if( db_backend == e_db_backend::postgresql ){
		ShowSQL( "db_backend is postgresql, but this server was built without PostgreSQL support (libpq).\n" );
		return SQL_ERROR;
	}
#endif
	if( !self->engine->connect( user, passwd, host, port, db ) ){
		ShowSQL( "%s\n", self->engine->error.c_str() );
		return SQL_ERROR;
	}
	if( !create_schema_if_empty( *self->engine ) ){
		return SQL_ERROR;
	}
	return SQL_SUCCESS;
}

int32 Sql_GetTimeout(Sql* self, uint32* out_timeout)
{
	// No server-side idle timeout to track: SQLite is in-process and PostgreSQL reconnects on demand.
	if( self && out_timeout ){
		*out_timeout = 0;
		return SQL_SUCCESS;
	}
	return SQL_ERROR;
}

int32 Sql_GetColumnNames(Sql* self, const char* table, char* out_buf, size_t buf_len, char sep)
{
	if( self == nullptr || SQL_ERROR == Sql_Query( self, "SELECT * FROM `%s` LIMIT 0", table ) )
		return SQL_ERROR;

	size_t off = 0;
	out_buf[off] = '\0';
	for( const auto& name : self->result.names ){
		if( off + name.size() + 2 > buf_len ){
			ShowDebug( "Sql_GetColumns: output buffer is too small\n" );
			*out_buf = '\0';
			return SQL_ERROR;
		}
		memcpy( out_buf + off, name.data(), name.size() );
		off += name.size();
		out_buf[off++] = sep;
	}
	out_buf[off] = '\0';
	Sql_FreeResult( self );
	return SQL_SUCCESS;
}

int32 Sql_SetEncoding(Sql* self, const char* encoding)
{
	if( self && self->engine->set_encoding( encoding ) )
		return SQL_SUCCESS;
	return SQL_ERROR;
}

int32 Sql_Ping(Sql* self)
{
	if( self && self->engine->ping() )
		return SQL_SUCCESS;
	return SQL_ERROR;
}

size_t Sql_EscapeString(Sql* self, char *out_to, const char *from)
{
	return dialect::escape_string( out_to, from, strlen( from ) );
}

size_t Sql_EscapeStringLen(Sql* self, char *out_to, const char *from, size_t from_len)
{
	return dialect::escape_string( out_to, from, from_len );
}

/// Runs the statement in self->buf.
static int32 Sql_P_Run( Sql* self ){
	const auto translated = dialect::translate( StringBuf_Value( &self->buf ), self->engine->sql_dialect() );
	// PostgreSQL translation needs catalog lookups and happens inside the engine.
	const std::string& sql = ( self->engine->sql_dialect() == dialect::Dialect::Sqlite ) ? translated.sql : std::string( StringBuf_Value( &self->buf ) );
	if( !self->engine->exec( sql, {}, self->result ) ){
		ShowSQL( "DB error - %s\n", self->engine->error.c_str() );
		return SQL_ERROR;
	}
	self->affected = self->engine->affected_rows();
	self->insert_id = self->engine->last_insert_id();
	return SQL_SUCCESS;
}

int32 Sql_Query(Sql* self, const char* query, ...)
{
	int32 res;
	va_list args;

	va_start(args, query);
	res = Sql_QueryV(self, query, args);
	va_end(args);

	return res;
}

int32 Sql_QueryV(Sql* self, const char* query, va_list args)
{
	if( self == nullptr )
		return SQL_ERROR;

	Sql_FreeResult( self );
	StringBuf_Clear( &self->buf );
	StringBuf_Vprintf( &self->buf, query, args );
	return Sql_P_Run( self );
}

int32 Sql_QueryStr(Sql* self, const char* query)
{
	if( self == nullptr )
		return SQL_ERROR;

	Sql_FreeResult( self );
	StringBuf_Clear( &self->buf );
	StringBuf_AppendStr( &self->buf, query );
	return Sql_P_Run( self );
}

uint64 Sql_LastInsertId(Sql* self)
{
	return self ? self->insert_id : 0;
}

uint32 Sql_NumColumns(Sql* self)
{
	if( self && self->result.has_result )
		return (uint32)self->result.names.size();
	return 0;
}

uint64 Sql_NumRows(Sql* self)
{
	if( self && self->result.has_result )
		return (uint64)self->result.rows.size();
	return 0;
}

uint64 Sql_NumRowsAffected(Sql* self)
{
	return self ? self->affected : 0;
}

int32 Sql_NextRow(Sql* self)
{
	if( self && self->result.has_result )
	{
		if( self->next_row < self->result.rows.size() ){
			++self->next_row;
			self->row_valid = true;
			return SQL_SUCCESS;
		}
		self->row_valid = false;
		return SQL_NO_DATA;
	}
	return SQL_ERROR;
}

int32 Sql_GetData(Sql* self, size_t col, char** out_buf, size_t* out_len)
{
	if( self && self->row_valid )
	{
		auto& row = self->result.rows[self->next_row - 1];
		if( col < row.size() && row[col].has_value() )
		{
			if( out_buf ) *out_buf = row[col]->data();
			if( out_len ) *out_len = row[col]->size();
		}
		else
		{// NULL value or out of range
			if( out_buf ) *out_buf = nullptr;
			if( out_len ) *out_len = 0;
		}
		return SQL_SUCCESS;
	}
	return SQL_ERROR;
}

void Sql_FreeResult(Sql* self)
{
	if( self )
	{
		self->result.clear();
		self->next_row = 0;
		self->row_valid = false;
	}
}

void Sql_ShowDebug_(Sql* self, const char* debug_file, const unsigned long debug_line)
{
	if( self == nullptr )
		ShowDebug("at %s:%lu - self is nullptr\n", debug_file, debug_line);
	else if( StringBuf_Length(&self->buf) > 0 )
		ShowDebug("at %s:%lu - %s\n", debug_file, debug_line, StringBuf_Value(&self->buf));
	else
		ShowDebug("at %s:%lu\n", debug_file, debug_line);
}

void Sql_Free(Sql* self)
{
	if( self )
	{
		Sql_FreeResult( self );
		self->engine.reset();
		delete self;
	}
}

///////////////////////////////////////////////////////////////////////////////
// Prepared Statements
///////////////////////////////////////////////////////////////////////////////

namespace {
struct ParamBinding {
	SqlDataType type = SQLDT_NULL;
	void* buffer = nullptr;
	size_t length = 0;
};

struct ColumnBinding {
	SqlDataType type = SQLDT_NULL;
	void* buffer = nullptr;
	size_t length = 0; // capacity for STRING/ENUM (without the terminator) and BLOB
	uint32* out_length = nullptr;
	int8* out_is_null = nullptr;
};
}

struct SqlStmtState {
	std::string query;          // dialect-translated for SQLite; as written for PostgreSQL
	size_t num_params = 0;
	std::vector<ParamBinding> params;
	std::vector<ColumnBinding> columns;
	bool bind_columns = false;
	ResultSet result;
	size_t next_row = 0;
	uint64 insert_id = 0;
};

SqlStmt::SqlStmt( Sql& sql ) : sql( sql ), state( std::make_unique<SqlStmtState>() ){
	StringBuf_Init( &this->buf ); // the constructor does not allocate
}

int32 SqlStmt::Prepare(const char* query, ...){
	int32 res;
	va_list args;

	va_start(args, query);
	res = this->PrepareV(query, args);
	va_end(args);

	return res;
}

static int32 SqlStmt_P_Prepare( SqlStmtState& st, const char* query, dialect::Dialect d ){
	st = SqlStmtState{};
	if( d == dialect::Dialect::Sqlite ){
		const auto t = dialect::translate( query, d );
		if( !t.error.empty() ){
			ShowSQL( "DB error - %s\n", t.error.c_str() );
			return SQL_ERROR;
		}
		st.query = t.sql;
	}else{
		st.query = query;
	}
	st.num_params = dialect::count_placeholders( st.query );
	st.params.assign( st.num_params, ParamBinding{} );
	return SQL_SUCCESS;
}

int32 SqlStmt::PrepareV(const char* query, va_list args){
	StringBuf_Clear( &this->buf );
	StringBuf_Vprintf( &this->buf, query, args );
	return SqlStmt_P_Prepare( *this->state, StringBuf_Value( &this->buf ), this->sql.engine->sql_dialect() );
}

int32 SqlStmt::PrepareStr(const char* query){
	StringBuf_Clear( &this->buf );
	StringBuf_AppendStr( &this->buf, query );
	return SqlStmt_P_Prepare( *this->state, StringBuf_Value( &this->buf ), this->sql.engine->sql_dialect() );
}

size_t SqlStmt::NumParams(){
	return this->state->num_params;
}

int32 SqlStmt::BindParam(size_t idx, enum SqlDataType buffer_type, void* buffer, size_t buffer_len){
	if( idx < this->state->params.size() ){
		this->state->params[idx] = ParamBinding{ buffer_type, buffer, buffer_len };
	}
	return SQL_SUCCESS; // out of range - ignore (as before)
}

int32 SqlStmt::Execute(){
	this->FreeResult();
	std::vector<Param> params( this->state->params.size() );
	for( size_t i = 0; i < params.size(); ++i ){
		const ParamBinding& b = this->state->params[i];
		if( b.type != SQLDT_NULL && b.buffer == nullptr ){
			params[i].kind = Param::Kind::Null;
		}else if( !to_param( params[i], b.type, b.buffer, b.length ) ){
			return SQL_ERROR;
		}
	}
	Engine& engine = *this->sql.engine;
	if( !engine.exec( this->state->query, params, this->state->result ) ){
		ShowSQL( "DB error - %s\n", engine.error.c_str() );
		return SQL_ERROR;
	}
	this->state->insert_id = engine.last_insert_id();
	this->sql.insert_id = this->state->insert_id;
	this->sql.affected = engine.affected_rows();
	this->state->bind_columns = false;
	return SQL_SUCCESS;
}

uint64 SqlStmt::LastInsertId(){
	return this->state->insert_id;
}

size_t SqlStmt::NumColumns(){
	return this->state->result.names.size();
}

int32 SqlStmt::BindColumn(size_t idx, enum SqlDataType buffer_type, void* buffer, size_t buffer_len, uint32* out_length, int8* out_is_null){
	if( buffer_type == SQLDT_STRING || buffer_type == SQLDT_ENUM )
	{
		if( buffer_len < 1 )
		{
			ShowDebug("SqlStmt::BindColumn: buffer_len(%" PRIuPTR ") is too small, no room for the nul-terminator\n", buffer_len);
			return SQL_ERROR;
		}
		--buffer_len;// nul-terminator
	}

	if( !this->state->bind_columns ){
		this->state->columns.assign( this->NumColumns(), ColumnBinding{} );
		this->state->bind_columns = true;
	}

	if( idx < this->state->columns.size() ){
		this->state->columns[idx] = ColumnBinding{ buffer_type, buffer, buffer_len, out_length, out_is_null };
	}
	return SQL_SUCCESS; // out of range - ignore (as before)
}

uint64 SqlStmt::NumRows(){
	return (uint64)this->state->result.rows.size();
}

void SqlStmt::ShowDebugTruncatedColumn( size_t i ){
	const auto& names = this->state->result.names;
	ShowSQL( "DB error - data of field '%s' was truncated.\n", i < names.size() ? names[i].c_str() : "?" );
	ShowDebug( "column - %lu\n", (unsigned long)i );
}

/// Converts one text value into a bound C buffer. Returns false if it does not fit.
static bool store_column( const ColumnBinding& b, const std::optional<std::string>& v ){
	const bool is_null = !v.has_value();
	const char* s = is_null ? "" : v->c_str();
	const size_t len = is_null ? 0 : v->size();
	if( b.out_is_null ) *b.out_is_null = is_null ? 1 : 0;
	if( b.out_length ) *b.out_length = (uint32)len;
	if( b.buffer == nullptr ) return true;

	auto as_signed = [&]() -> int64 { return is_null ? 0 : (int64)strtoll( s, nullptr, 10 ); };
	auto as_unsigned = [&]() -> uint64 {
		if( is_null ) return 0;
		if( *s == '-' ) return (uint64)strtoll( s, nullptr, 10 );
		return (uint64)strtoull( s, nullptr, 10 );
	};

	switch( b.type ){
		case SQLDT_NULL: return true;
		case SQLDT_INT8: *(int8*)b.buffer = (int8)as_signed(); return true;
		case SQLDT_INT16: *(int16*)b.buffer = (int16)as_signed(); return true;
		case SQLDT_INT32: *(int32*)b.buffer = (int32)as_signed(); return true;
		case SQLDT_INT64: *(int64*)b.buffer = as_signed(); return true;
		case SQLDT_UINT8: *(uint8*)b.buffer = (uint8)as_unsigned(); return true;
		case SQLDT_UINT16: *(uint16*)b.buffer = (uint16)as_unsigned(); return true;
		case SQLDT_UINT32: *(uint32*)b.buffer = (uint32)as_unsigned(); return true;
		case SQLDT_UINT64: *(uint64*)b.buffer = as_unsigned(); return true;
		case SQLDT_CHAR: *(char*)b.buffer = (char)as_signed(); return true;
		case SQLDT_UCHAR: *(unsigned char*)b.buffer = (unsigned char)as_unsigned(); return true;
		case SQLDT_LONG: *(long*)b.buffer = (long)as_signed(); return true;
		case SQLDT_ULONG: *(unsigned long*)b.buffer = (unsigned long)as_unsigned(); return true;
		case SQLDT_LONGLONG: *(int64*)b.buffer = as_signed(); return true;
		case SQLDT_ULONGLONG: *(uint64*)b.buffer = as_unsigned(); return true;
		case SQLDT_FLOAT: *(float*)b.buffer = is_null ? 0.f : strtof( s, nullptr ); return true;
		case SQLDT_DOUBLE: *(double*)b.buffer = is_null ? 0. : strtod( s, nullptr ); return true;
		case SQLDT_STRING:
		case SQLDT_ENUM:
			if( len > b.length ) return false;
			memcpy( b.buffer, s, len );
			memset( (char*)b.buffer + len, 0, b.length - len + 1 ); // clear the rest and terminate
			return true;
		case SQLDT_BLOB:
			if( len > b.length ) return false;
			memcpy( b.buffer, s, len );
			if( len < b.length ) memset( (char*)b.buffer + len, 0, b.length - len );
			return true;
		default:
			return true;
	}
}

int32 SqlStmt::NextRow(){
	SqlStmtState& st = *this->state;
	if( st.next_row >= st.result.rows.size() ){
		return SQL_NO_DATA;
	}
	const auto& row = st.result.rows[st.next_row++];
	if( st.bind_columns ){
		for( size_t i = 0; i < st.columns.size() && i < row.size(); ++i ){
			if( !store_column( st.columns[i], row[i] ) ){
				this->ShowDebugTruncatedColumn( i );
				return SQL_ERROR;
			}
		}
	}
	return SQL_SUCCESS;
}

void SqlStmt::FreeResult(){
	this->state->result.clear();
	this->state->next_row = 0;
}

void SqlStmt::ShowDebug_(const char* debug_file, const unsigned long debug_line){
#if !defined(SQL_REMOVE_SHOWDEBUG)
	if( StringBuf_Length( &this->buf ) > 0 ){
		ShowDebug("at %s:%lu - %s\n", debug_file, debug_line, StringBuf_Value(&this->buf));
	}else{
		ShowDebug("at %s:%lu\n", debug_file, debug_line);
	}
#endif
}

SqlStmt::~SqlStmt(){
	this->FreeResult();
}

///////////////////////////////////////////////////////////////////////////////
// Configuration
///////////////////////////////////////////////////////////////////////////////

void Sql_inter_server_read(const char* cfgName, bool first) {
	char line[1024], w1[1024], w2[1024];
	FILE* fp;

	fp = fopen(cfgName, "r");
	if(fp == nullptr) {
		if( first ) {
			ShowFatalError("File not found: %s\n", cfgName);
			exit(EXIT_FAILURE);
		} else
			ShowError("File not found: %s\n", cfgName);
		return;
	}

	while(fgets(line, sizeof(line), fp)) {
		int32 i = sscanf(line, "%1023[^:]: %1023[^\r\n]", w1, w2);
		if(i != 2)
			continue;

		if( !strcmpi( w1, "db_backend" ) ){
			if( !strcmpi( w2, "sqlite" ) )
				db_backend = e_db_backend::sqlite;
			else if( !strcmpi( w2, "postgresql" ) || !strcmpi( w2, "postgres" ) )
				db_backend = e_db_backend::postgresql;
			else
				ShowError( "%s::db_backend '%s' is not supported (sqlite or postgresql); keeping the current backend.\n", cfgName, w2 );
		} else if( !strcmpi( w1, "sqlite_directory" ) ){
			sqlite_directory = w2;
		} else if( !strcmpi( w1, "db_auto_create_schema" ) ){
			db_auto_create_schema = config_switch( w2 ) != 0;
		} else if( !strcmpi( w1, "db_schema_directory" ) ){
			db_schema_directory = w2;
		} else if( !strcmpi( w1, "db_reconnect_type" ) || !strcmpi( w1, "mysql_reconnect_type" ) ){
			db_reconnect_type = atoi(w2);
			switch( db_reconnect_type ) {
				case 1:
				case 2:
					break;
				default:
					ShowError("%s::%s is set to %d which is not valid, defaulting to 1...\n", cfgName, w1, db_reconnect_type);
					db_reconnect_type = 1;
					break;
			}
		} else if( !strcmpi( w1, "db_reconnect_count" ) || !strcmpi( w1, "mysql_reconnect_count" ) ){
			db_reconnect_count = atoi(w2);
			if( db_reconnect_count < 1 )
				db_reconnect_count = 1;
		} else if(!strcmpi(w1,"import"))
			Sql_inter_server_read(w2,false);
	}
	fclose(fp);

	return;
}

void Sql_Init(void) {
	Sql_inter_server_read(INTER_CONF_NAME,true);
}
