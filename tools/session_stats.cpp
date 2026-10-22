// session_stats — per-task outcome statistics over recorded sessions.
//
// Walks <root> recursively for every meta.yaml, groups sessions by their
// `task:` string and counts the operator label (`result: success|fail|
// partial`; missing = unlabeled), one block per task, with the mean session
// duration. meta.yaml
// is read line-by-line (no YAML parser: the task text is written verbatim
// and may hold characters a strict parser rejects).
//
//   ./build/session_stats [root] [--list] [--csv FILE]
//     root        directory holding <session>/meta.yaml at any depth
//                 (default: sessions); tasks named in <root>/tasks.txt
//                 (session_organize) are shown under that name
//     --list      also one line per session (dir, label, duration, task)
//     --csv FILE  write the per-task table as CSV
#include "common/task_index.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

enum Label { kSuccess, kFail, kPartial, kUnlabeled, kLabelCount };
const char* const kLabelName[kLabelCount] = {"success", "fail", "partial", "unlabeled"};

struct Session {
    std::string dir;         // path relative to root
    std::string task;
    Label       label = kUnlabeled;
    double      duration_s = -1;   // <0 = unknown
};

struct TaskStats {
    int    count[kLabelCount] = {0, 0, 0, 0};
    int    total   = 0;
    double dur_sum = 0;
    int    dur_n   = 0;
};

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// "key: value" at the given indentation -> value (trimmed), or false.
bool key_value(const std::string& line, const char* key, std::string& value) {
    const std::string t = trim(line);
    const std::string k = std::string(key) + ":";
    if (t.compare(0, k.size(), k) != 0) return false;
    value = trim(t.substr(k.size()));
    return true;
}

Label parse_label(const std::string& v) {
    for (int i = 0; i < kUnlabeled; ++i)
        if (v == kLabelName[i]) return static_cast<Label>(i);
    return kUnlabeled;
}

Session read_meta(const fs::path& meta, const fs::path& root) {
    Session s;
    s.dir  = fs::relative(meta.parent_path(), root).string();
    s.task = "(no task)";
    std::ifstream in(meta);
    std::string line, v;
    long long start = -1, end = -1;
    bool have_task = false;
    while (std::getline(in, line)) {
        if (!have_task && key_value(line, "task", v)) {
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            if (!v.empty()) s.task = v;
            have_task = true;
        } else if (line.rfind("result:", 0) == 0 && key_value(line, "result", v)) {
            s.label = parse_label(v);              // the last label written wins
        } else if (key_value(line, "started_epoch_ns", v)) {
            start = std::atoll(v.c_str());
        } else if (line.rfind("ended_epoch_ns:", 0) == 0 && key_value(line, "ended_epoch_ns", v)) {
            end = std::atoll(v.c_str());
        }
    }
    if (start >= 0 && end >= 0) s.duration_s = double(end - start) / 1e9;
    return s;
}

std::vector<Session> scan(const fs::path& root) {
    std::vector<Session> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && it->path().filename() == "meta.yaml")
            out.push_back(read_meta(it->path(), root));
    }
    std::sort(out.begin(), out.end(), [](const Session& a, const Session& b) { return a.dir < b.dir; });
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::string root = "sessions", csv_path;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") list = true;
        else if (a == "--csv" && i + 1 < argc) csv_path = argv[++i];
        else if (a == "-h" || a == "--help") {
            std::printf("usage: session_stats [root] [--list] [--csv FILE]\n");
            return 0;
        } else root = a;
    }

    const fs::path root_path(root);
    const auto sessions = scan(root_path);
    if (sessions.empty()) {
        std::fprintf(stderr, "no meta.yaml under %s\n", root.c_str());
        return 1;
    }

    // Tasks named in <root>/tasks.txt (session_organize) come first under
    // that name; the rest follow as [n] in first-seen order (dirs = time).
    const kist::TaskIndex index(root_path);
    std::vector<std::string> tasks, names;
    for (const auto& e : index.entries()) { tasks.push_back(e.text); names.push_back(e.name); }
    std::map<std::string, TaskStats> by_task;
    for (const auto& s : sessions) {
        if (!by_task.count(s.task) && index.lookup(s.task).empty()) {
            tasks.push_back(s.task);
            names.push_back(std::to_string(tasks.size()));
        }
        auto& t = by_task[s.task];
        ++t.count[s.label];
        ++t.total;
        if (s.duration_s >= 0) { t.dur_sum += s.duration_s; ++t.dur_n; }
    }

    if (list) {
        for (const auto& s : sessions) {
            if (s.duration_s >= 0) std::printf("%-28s %-9s %6.1fs  %s\n", s.dir.c_str(), kLabelName[s.label], s.duration_s, s.task.c_str());
            else                   std::printf("%-28s %-9s      ?  %s\n", s.dir.c_str(), kLabelName[s.label], s.task.c_str());
        }
        std::printf("\n");
    }

    TaskStats sum;
    for (size_t i = 0; i < tasks.size(); ++i) {
        if (!by_task.count(tasks[i])) continue;        // listed in tasks.txt, no session under root
        const auto& t = by_task[tasks[i]];
        const int labeled = t.count[kSuccess] + t.count[kFail] + t.count[kPartial];
        std::printf("[%s] %s\n", names[i].c_str(), tasks[i].c_str());
        std::printf("    success   %4d\n", t.count[kSuccess]);
        std::printf("    fail      %4d\n", t.count[kFail]);
        std::printf("    partial   %4d\n", t.count[kPartial]);
        if (t.count[kUnlabeled]) std::printf("    unlabeled %4d\n", t.count[kUnlabeled]);
        std::printf("    total     %4d", t.total);
        if (labeled) std::printf("   success rate %.1f%%", 100.0 * t.count[kSuccess] / labeled);
        if (t.dur_n) std::printf("   avg %.1f s", t.dur_sum / t.dur_n);
        std::printf("\n\n");
        for (int l = 0; l < kLabelCount; ++l) sum.count[l] += t.count[l];
        sum.total += t.total;
    }
    if (tasks.size() > 1)
        std::printf("all tasks: %d sessions — success %d, fail %d, partial %d, unlabeled %d\n",
                    sum.total, sum.count[kSuccess], sum.count[kFail], sum.count[kPartial], sum.count[kUnlabeled]);

    if (!csv_path.empty()) {
        std::ofstream f(csv_path);
        if (!f) { std::fprintf(stderr, "cannot write %s\n", csv_path.c_str()); return 1; }
        f << "name,task,total,success,fail,partial,unlabeled,success_rate,avg_duration_s\n";
        for (size_t i = 0; i < tasks.size(); ++i) {
            const auto& task = tasks[i];
            if (!by_task.count(task)) continue;
            const auto& t = by_task[task];
            const int labeled = t.count[kSuccess] + t.count[kFail] + t.count[kPartial];
            std::string q = task;                                      // CSV-quote the task text
            for (size_t p = 0; (p = q.find('"', p)) != std::string::npos; p += 2) q.insert(p, "\"");
            f << names[i] << ",\"" << q << "\"," << t.total << ',' << t.count[kSuccess] << ',' << t.count[kFail] << ','
              << t.count[kPartial] << ',' << t.count[kUnlabeled] << ',';
            if (labeled) f << double(t.count[kSuccess]) / labeled;
            f << ',';
            if (t.dur_n) f << t.dur_sum / t.dur_n;
            f << '\n';
        }
        std::printf("\nwrote %s\n", csv_path.c_str());
    }
    return 0;
}
