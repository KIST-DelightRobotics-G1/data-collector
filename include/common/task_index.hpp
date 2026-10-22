#pragma once
// Task naming shared by session_organize and session_stats: <root>/tasks.txt
// holds one line per task, "<name>: <task text>", where <name> is the short
// folder name the operator chose (e.g. "align_pos"). Names never change
// once written, so align_pos/ on disk and "[align_pos]" in the stats mean
// the same task across runs. The file can be edited by hand beforehand.
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace kist {

inline constexpr const char* kTaskIndexFile = "tasks.txt";

// Folder-safe short name: [A-Za-z0-9_-], non-empty.
inline bool valid_task_name(const std::string& n) {
    if (n.empty()) return false;
    for (unsigned char c : n)
        if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}

class TaskIndex {
public:
    struct Entry { std::string name, text; };

    explicit TaskIndex(const std::filesystem::path& root) : path_(root / kTaskIndexFile) {
        std::ifstream in(path_);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto sep = line.find(": ");
            if (line.empty() || line[0] == '#' || sep == std::string::npos) continue;
            Entry e{line.substr(0, sep), line.substr(sep + 2)};
            if (valid_task_name(e.name)) entries_.push_back(std::move(e));
        }
    }

    // Name of `text`, or "" when it is not listed.
    std::string lookup(const std::string& text) const {
        for (const auto& e : entries_)
            if (e.text == text) return e.name;
        return "";
    }

    bool has_name(const std::string& name) const {
        for (const auto& e : entries_)
            if (e.name == name) return true;
        return false;
    }

    // Registers a new task under `name` (caller validated + unique).
    void add(const std::string& name, const std::string& text) {
        entries_.push_back({name, text});
        dirty_ = true;
    }

    // Default name for the next new task: task_<n>, skipping taken names.
    std::string next_default_name() const {
        for (size_t n = entries_.size() + 1;; ++n) {
            const std::string c = "task_" + std::to_string(n);
            if (!has_name(c)) return c;
        }
    }

    const std::vector<Entry>& entries() const { return entries_; }
    bool dirty() const { return dirty_; }

    // Rewrites tasks.txt in full (a hand-edited file keeps its order).
    bool save() {
        std::ofstream out(path_);
        if (!out) return false;
        for (const auto& e : entries_) out << e.name << ": " << e.text << "\n";
        dirty_ = false;
        return true;
    }

private:
    std::filesystem::path path_;
    std::vector<Entry>    entries_;
    bool                  dirty_ = false;
};

}  // namespace kist
