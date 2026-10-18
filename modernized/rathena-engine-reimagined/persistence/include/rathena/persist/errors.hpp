#pragma once
// Typed errors of the persistence service. Messages never contain credentials, URLs or row
// payloads (log redaction rule); identifiers such as mod id and kind are safe to include.
#include <stdexcept>
#include <string>

namespace rathena::persist {

struct PersistError : std::runtime_error { using std::runtime_error::runtime_error; };

/// Marker for an operation that is declared but not built (kept for interface compatibility).
struct NotImplementedError : PersistError { using PersistError::PersistError; };
struct DbConfigError : PersistError { using PersistError::PersistError; };
struct RepositoryError : PersistError { using PersistError::PersistError; };
/// The backend rejected one row's content (constraint, type mismatch, too big). The database
/// itself is healthy: this is a poison row, not an outage (writer isolates and quarantines it).
struct RowRejectedError : RepositoryError { using RepositoryError::RepositoryError; };
/// The backend (for example PostgreSQL without libpq) is not available in this build.
struct BackendUnavailableError : PersistError { using PersistError::PersistError; };
/// Database outage and the on-disk WAL reached its byte limit: new writes are refused, never
/// dropped silently (architecture 3.1).
struct WalFullError : PersistError { using PersistError::PersistError; };
/// The WAL directory could not be written (I/O error, directory locked, unreadable).
struct WalIoError : PersistError { using PersistError::PersistError; };
/// The write-behind queue is full because the writer cannot make progress; writes are refused.
struct WriteBacklogError : PersistError { using PersistError::PersistError; };
/// The service was shut down; no more writes are accepted.
struct ServiceClosedError : PersistError { using PersistError::PersistError; };
struct QuotaExceededError : PersistError { using PersistError::PersistError; };
struct CollectionCapExceededError : PersistError { using PersistError::PersistError; };
/// Typed read error for a quarantined (corrupt) row; other rows and mods are unaffected.
struct QuarantinedRowError : PersistError { using PersistError::PersistError; };
struct MigrationError : PersistError { using PersistError::PersistError; };
/// Two definitions claim the same client-visible number; the message names both.
struct IdCollisionError : PersistError { using PersistError::PersistError; };
struct IdRangeExhaustedError : PersistError { using PersistError::PersistError; };
/// Stored bytes do not decode (internal; surfaces as quarantine + QuarantinedRowError).
struct DecodeError : PersistError { using PersistError::PersistError; };

} // namespace rathena::persist
