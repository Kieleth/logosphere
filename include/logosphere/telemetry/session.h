// Session + Instrument — engine-level telemetry primitives.
//
// Every Logosphere process opens ONE Session at startup. A Session
// is a folder on disk, tagged with the build SHA and a launch UTC
// timestamp, that collects append-only JSONL records from any
// number of game-defined Instruments. Games register the Instruments
// they care about (AI decisions, physics events, whatever); the
// Session takes care of directory layout, file handles, and thread
// safety.
//
// Format: JSONL. One record per line, self-describing. Readable with
// `cat`, diffable, streamable, doesn't need a codec dependency.
//
// Directory layout:
//     ~/.logosphere/sessions/
//       <build_sha>/
//         <launch_utc>_<session_num>/
//           session.json           ← one-shot manifest (build info,
//                                    start time, end time on close)
//           <instrument_name>/
//             <arbitrary files>    ← instrument decides its own sub-
//                                    layout (one big file, one file
//                                    per round, etc.)
//
// Games extending this: subclass Instrument, register an instance on
// the Session right after it opens. The engine will call open() on
// the instance with the instrument's session-scoped directory. The
// instrument is responsible for whatever file(s) it wants to write
// there. `append_jsonl()` is provided for the common case.
//
// Thread safety: Instrument::append_jsonl is internally locked. If a
// game wants fancier concurrency (per-channel locks, SPSC queues,
// etc.) it can override and implement its own writer. For v1 a
// shared mutex is fine — telemetry volume is low.

#pragma once

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace logosphere::telemetry {

// Identifier for one process launch. Deterministic so two processes
// started in different seconds on the same commit have distinct
// SessionIds.
struct SessionId {
    std::string build_sha;        // LOGOSPHERE_BUILD_SHA
    std::string build_describe;   // LOGOSPHERE_BUILD_DESCRIBE
    std::string launch_utc;       // "2026-04-21T14:32:01Z"
    int         session_num = 0;  // 1 for first process in a second,
                                  // 2 for second, etc. — breaks ties
                                  // if multiple launches happen the
                                  // same UTC second.
};

class Instrument {
public:
    virtual ~Instrument() = default;

    // Short name used as the instrument's subdirectory inside the
    // session folder. Must be filesystem-safe (no slashes, no spaces).
    virtual const char* name() const = 0;

    // Called once by Session::register_instrument. `dir` is absolute
    // path to the already-created instrument subdirectory. Default
    // impl stores it and opens no files — subclasses override to set
    // up their own output streams.
    virtual void open(const std::string& dir) { dir_ = dir; }

    // Called once by Session on destruction. Default impl closes the
    // shared JSONL file if open. Subclasses can flush their own.
    virtual void close();

    // Convenience: append a single JSONL record to
    // `<instrument_dir>/<filename>`. File is opened on first append
    // and re-used. `record_json` should NOT include a trailing newline
    // (we add it). Thread-safe.
    void append_jsonl(const std::string& filename,
                      const std::string& record_json);

protected:
    std::string dir_;

private:
    // Shared map of filename → FILE*; simple v1 implementation. A
    // game that wants per-file locks can override append_jsonl.
    struct Sink { std::FILE* fp = nullptr; };
    std::mutex                     mu_;
    std::vector<std::pair<std::string, Sink>> sinks_;
    std::FILE* get_or_open_(const std::string& filename);
};

// Opens a session rooted at ~/.logosphere/sessions/<sha>/<ts>_<n>/.
// Writes session.json with the build info + start time. Destructor
// writes the end_time_utc to session.json and closes all registered
// instruments.
//
// One Session per process is the intended usage; nothing in the API
// prevents multiple Sessions coexisting but there's no reason to.
class Session {
public:
    Session();
    ~Session();

    // Disable copy; Session owns file handles.
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    const SessionId&    id()  const { return id_; }
    const std::string&  dir() const { return dir_; }

    // Games hand an Instrument to the Session; Session stores the
    // unique_ptr and calls open() on it with the session-scoped
    // directory. Returns a raw pointer so the caller can emit records
    // without looking the instrument up again. The pointer's lifetime
    // equals the Session's.
    Instrument* register_instrument(std::unique_ptr<Instrument> instr);

private:
    SessionId                                 id_;
    std::string                               dir_;
    std::vector<std::unique_ptr<Instrument>>  instruments_;
};

} // namespace logosphere::telemetry
