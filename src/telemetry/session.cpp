#include "logosphere/telemetry/session.h"
#include "logosphere/build_info.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace logosphere::telemetry {

namespace {

// "2026-04-21T14:32:01Z" — ISO 8601 UTC, colon-free minute+second
// variant would be filesystem-safer on Windows but macOS/Linux
// handle colons fine, and we already have them elsewhere.
std::string iso_utc_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

std::string home_dir() {
    if (const char* h = std::getenv("HOME")) return h;
    return "/tmp";  // harmless fallback
}

// JSONL-safe escape: double quotes and backslashes. No control-char
// handling for v1 — we control our own producers. Extend if we ever
// take user input into records.
std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

} // namespace

// ---------- Instrument ----------

std::FILE* Instrument::get_or_open_(const std::string& filename) {
    for (auto& e : sinks_) {
        if (e.first == filename) return e.second.fp;
    }
    std::filesystem::create_directories(dir_);
    std::filesystem::path p = std::filesystem::path(dir_) / filename;
    std::FILE* fp = std::fopen(p.string().c_str(), "ab");
    if (!fp) {
        std::cerr << "[telemetry] fopen failed for " << p << std::endl;
        return nullptr;
    }
    sinks_.push_back({filename, {fp}});
    return fp;
}

void Instrument::append_jsonl(const std::string& filename,
                              const std::string& record_json) {
    std::lock_guard<std::mutex> lock(mu_);
    std::FILE* fp = get_or_open_(filename);
    if (!fp) return;
    std::fwrite(record_json.data(), 1, record_json.size(), fp);
    std::fputc('\n', fp);
    std::fflush(fp);  // telemetry is low-volume; flush per line so a
                      // crash doesn't lose the records leading up to
                      // it. Worth the cost here.
}

void Instrument::close() {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& e : sinks_) {
        if (e.second.fp) std::fclose(e.second.fp);
    }
    sinks_.clear();
}

// ---------- Session ----------

Session::Session() {
    id_.build_sha      = kBuildSha;
    id_.build_describe = kBuildDescribe;
    id_.launch_utc     = iso_utc_now();

    // Session number: if another session from the same second already
    // exists in the build_sha subtree, increment. Lets back-to-back
    // launches disambiguate without forcing sub-second precision.
    std::filesystem::path root = std::filesystem::path(home_dir())
        / ".logosphere" / "sessions" / id_.build_sha;
    std::filesystem::create_directories(root);

    int n = 1;
    while (true) {
        std::ostringstream oss;
        oss << id_.launch_utc << "_" << n;
        std::filesystem::path candidate = root / oss.str();
        if (!std::filesystem::exists(candidate)) {
            id_.session_num = n;
            dir_ = candidate.string();
            std::filesystem::create_directories(dir_);
            break;
        }
        ++n;
        if (n > 1000) {  // belt + suspenders
            id_.session_num = n;
            dir_ = (root / (oss.str() + "_fallback")).string();
            std::filesystem::create_directories(dir_);
            break;
        }
    }

    // Write session.json manifest. Minimal JSON hand-rolled to avoid
    // pulling in a dependency just for this.
    std::ostringstream j;
    j << "{"
      << "\"build_sha\":\""      << escape(id_.build_sha)      << "\","
      << "\"build_describe\":\"" << escape(id_.build_describe) << "\","
      << "\"launch_utc\":\""     << escape(id_.launch_utc)     << "\","
      << "\"session_num\":"      << id_.session_num
      << "}";
    std::filesystem::path manifest = std::filesystem::path(dir_) / "session.json";
    if (std::FILE* fp = std::fopen(manifest.string().c_str(), "wb")) {
        std::fputs(j.str().c_str(), fp);
        std::fputc('\n', fp);
        std::fclose(fp);
    }

    std::cerr << "[telemetry] session opened: " << dir_ << std::endl;
}

Session::~Session() {
    for (auto& i : instruments_) {
        i->close();
    }
    // Update manifest with end time — simple rewrite since the file
    // is tiny. If we ever need concurrent readers, switch to an
    // append-only end_marker line.
    std::filesystem::path manifest = std::filesystem::path(dir_) / "session.json";
    std::ostringstream j;
    j << "{"
      << "\"build_sha\":\""      << escape(id_.build_sha)      << "\","
      << "\"build_describe\":\"" << escape(id_.build_describe) << "\","
      << "\"launch_utc\":\""     << escape(id_.launch_utc)     << "\","
      << "\"session_num\":"      << id_.session_num             << ","
      << "\"end_utc\":\""        << escape(iso_utc_now())       << "\""
      << "}";
    if (std::FILE* fp = std::fopen(manifest.string().c_str(), "wb")) {
        std::fputs(j.str().c_str(), fp);
        std::fputc('\n', fp);
        std::fclose(fp);
    }
    std::cerr << "[telemetry] session closed: " << dir_ << std::endl;
}

Instrument* Session::register_instrument(std::unique_ptr<Instrument> instr) {
    Instrument* raw = instr.get();
    std::filesystem::path sub = std::filesystem::path(dir_) / instr->name();
    std::filesystem::create_directories(sub);
    instr->open(sub.string());
    instruments_.push_back(std::move(instr));
    return raw;
}

} // namespace logosphere::telemetry
