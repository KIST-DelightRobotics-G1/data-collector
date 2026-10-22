// session_organize — files flat session dirs into <date>/<task name>/.
//
//   sessions/20261022_014748/   ->   sessions/20261022/align_pos/20261022_014748/
//
// Only dirs named YYYYMMDD_HHMMSS directly under <root> are moved. The task
// name comes from <root>/tasks.txt ("<name>: <task text>", see
// common/task_index.hpp); the first time a task text is seen the tool prints
// it and asks for a short name on the terminal (Enter = task_<n>; without a
// terminal task_<n> is used). Names are kept forever — edit tasks.txt by
// hand to pre-assign them. A session still being written (no ended_epoch_ns
// in meta.yaml) or without meta.yaml is left where it is; one whose
// meta.yaml carries no task goes to <date>/no_task/.
//
//   ./build/session_organize [root]
//     root       sessions root (default: sessions)
#include "common/task_index.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace {

bool is_session_name(const std::string& n) {      // YYYYMMDD_HHMMSS
    if (n.size() != 15 || n[8] != '_') return false;
    for (size_t i = 0; i < n.size(); ++i)
        if (i != 8 && !std::isdigit(static_cast<unsigned char>(n[i]))) return false;
    return true;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

// task text ("" when none) and whether the session has ended.
bool read_meta(const fs::path& meta, std::string& task, bool& ended) {
    std::ifstream in(meta);
    if (!in) return false;
    task.clear();
    ended = false;
    std::string line;
    bool have_task = false;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (!have_task && t.rfind("task:", 0) == 0) {
            std::string v = trim(t.substr(5));
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            task = v;
            have_task = true;
        } else if (line.rfind("ended_epoch_ns:", 0) == 0) {
            ended = true;
        }
    }
    return true;
}

// Short name for a task text not yet in tasks.txt: asked on the terminal,
// task_<n> when there is none (or on Enter / an unusable answer).
std::string ask_name(kist::TaskIndex& index, const std::string& text) {
    const std::string fallback = index.next_default_name();
    if (!isatty(STDIN_FILENO)) return fallback;
    for (;;) {
        std::printf("\nnew task:\n  %s\nfolder name [%s]: ", text.c_str(), fallback.c_str());
        std::fflush(stdout);
        std::string in;
        if (!std::getline(std::cin, in)) return fallback;
        if (in.empty()) return fallback;
        if (!kist::valid_task_name(in)) { std::printf("  use letters, digits, _ or - only\n"); continue; }
        if (index.has_name(in))         { std::printf("  '%s' is already another task\n", in.c_str()); continue; }
        return in;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string root = "sessions";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") { std::printf("usage: session_organize [root]\n"); return 0; }
        root = a;
    }
    const fs::path root_path(root);
    std::error_code ec;
    if (!fs::is_directory(root_path, ec)) { std::fprintf(stderr, "not a directory: %s\n", root.c_str()); return 1; }

    kist::TaskIndex index(root_path);

    std::vector<fs::path> sessions;
    for (const auto& e : fs::directory_iterator(root_path, ec))
        if (e.is_directory(ec) && is_session_name(e.path().filename().string()))
            sessions.push_back(e.path());
    std::sort(sessions.begin(), sessions.end());

    int moved = 0, skipped = 0;
    for (const auto& src : sessions) {
        const std::string name = src.filename().string();
        std::string task;
        bool ended = false;
        if (!read_meta(src / "meta.yaml", task, ended)) {
            std::printf("skip  %s  (no meta.yaml)\n", name.c_str());
            ++skipped;
            continue;
        }
        if (!ended) {
            std::printf("skip  %s  (still recording: no ended_epoch_ns)\n", name.c_str());
            ++skipped;
            continue;
        }
        std::string bucket = task.empty() ? "no_task" : index.lookup(task);
        if (bucket.empty()) {
            bucket = ask_name(index, task);
            index.add(bucket, task);
        }
        const fs::path dst_dir = root_path / name.substr(0, 8) / bucket;
        const fs::path dst     = dst_dir / name;
        if (fs::exists(dst, ec)) {
            std::printf("skip  %s  (exists: %s)\n", name.c_str(), fs::relative(dst, root_path).c_str());
            ++skipped;
            continue;
        }
        std::printf("move  %s  ->  %s\n", name.c_str(), fs::relative(dst, root_path).c_str());
        fs::create_directories(dst_dir, ec);
        if (ec) { std::fprintf(stderr, "cannot create %s: %s\n", dst_dir.c_str(), ec.message().c_str()); return 1; }
        fs::rename(src, dst, ec);
        if (ec) { std::fprintf(stderr, "cannot move %s: %s\n", name.c_str(), ec.message().c_str()); return 1; }
        ++moved;
    }

    if (index.dirty() && !index.save()) {
        std::fprintf(stderr, "cannot write %s\n", (root_path / kist::kTaskIndexFile).c_str());
        return 1;
    }
    for (const auto& e : index.entries()) std::printf("%s: %s\n", e.name.c_str(), e.text.c_str());
    std::printf("moved %d, skipped %d\n", moved, skipped);
    return 0;
}
