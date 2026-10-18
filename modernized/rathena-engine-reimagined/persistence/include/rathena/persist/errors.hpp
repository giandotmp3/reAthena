#pragma once
// Typed errors of the persistence service. Messages never contain credentials, URLs or row
// payloads (log redaction rule); identifiers such as mod id and kind are safe to include.
#include <stdexcept>
#include <string>

namespace rathena::persist {

struct PersistError : std::runtime_error { using std::runtime_error::runtime_error; };

/// Scaffold marker: the operation is declared by the interface contract but not built yet.
struct NotImplementedError : PersistError { using PersistError::PersistError; };
struct DbConfigError : PersistError { using PersistError::PersistError; };
struct RepositoryError : PersistError { using PersistError::PersistError; };
/// Database outage and the on-disk WAL reached its byte limit: new writes are refused, never
/// dropped silently (architecture 3.1).
struct WalFullError : PersistError { using PersistError::PersistError; };
struct QuotaExceededError : PersistError { using PersistError::PersistError; };
struct CollectionCapExceededError : PersistError { using PersistError::PersistError; };
/// Typed read error for a quarantined (corrupt) row; other rows and mods are unaffected.
struct QuarantinedRowError : PersistError { using PersistError::PersistError; };
struct MigrationError : PersistError { using PersistError::PersistError; };
/// Two definitions claim the same client-visible number; the message names both.
struct IdCollisionError : PersistError { using PersistError::PersistError; };

} // namespace rathena::persist
