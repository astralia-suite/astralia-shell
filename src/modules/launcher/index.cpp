#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <unordered_set>
#include <utility>

#include "modules/launcher/files.h"
#include "modules/launcher/index.h"
#include "modules/launcher/visits.h"

namespace astralia::launcher {

namespace {

struct Candidate {
    uint32_t node;
    int visits;
    float score;
};

std::string_view node_name(const FileIndex &index, const FileIndex::Node &node) {
    return std::string_view(index.names).substr(node.name_offset, node.name_length);
}

bool lower_less(std::string_view a, std::string_view b) {
    return std::ranges::lexicographical_compare(
        a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
}

unsigned char entry_type(int dir_fd, const dirent *ent) {
    if (ent->d_type != DT_UNKNOWN) {
        return ent->d_type;
    }
    struct stat st{};
    if (fstatat(dir_fd, ent->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        return DT_UNKNOWN;
    }
    return S_ISDIR(st.st_mode) ? DT_DIR : S_ISREG(st.st_mode) ? DT_REG : DT_UNKNOWN;
}

std::unordered_set<std::string> visited_names(const VisitStore &visits) {
    std::string prefix = visit_store_file_key("");
    std::unordered_set<std::string> names;
    for (const auto &[key, count] : visits.counts) {
        if (key.starts_with(prefix)) {
            names.insert(basename_of(key.substr(prefix.size())));
        }
    }
    return names;
}

} // namespace

std::shared_ptr<FileIndex> file_index_build(const std::string &root,
                                            const std::atomic<bool> &cancel) {
    auto index = std::make_shared<FileIndex>();
    index->root = root;
    while (index->root.size() > 1 && index->root.back() == '/') {
        index->root.pop_back();
    }
    std::vector<std::pair<std::string, uint32_t>> pending{{index->root, FileIndex::no_parent}};
    while (!pending.empty() && !cancel.load(std::memory_order_relaxed)) {
        auto [dir, parent] = std::move(pending.back());
        pending.pop_back();
        DIR *handle = opendir(dir.c_str());
        if (handle == nullptr) {
            continue;
        }
        while (const dirent *ent = readdir(handle)) {
            std::string_view name = ent->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            unsigned char type = entry_type(dirfd(handle), ent);
            if ((type != DT_DIR && type != DT_REG) || index->nodes.size() >= FileIndex::no_parent) {
                continue;
            }
            auto id = static_cast<uint32_t>(index->nodes.size());
            index->nodes.push_back({parent, static_cast<uint32_t>(index->names.size()),
                                    static_cast<uint16_t>(name.size()), type == DT_DIR});
            index->names.append(name);
            index->names.push_back('/');
            if (type == DT_DIR) {
                pending.emplace_back((dir == "/" ? "" : dir) + "/" + std::string(name), id);
            }
        }
        closedir(handle);
    }
    index->lower = index->names;
    std::ranges::transform(index->lower, index->lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    index->nodes.shrink_to_fit();
    index->names.shrink_to_fit();
    return index;
}

std::string file_index_path(const FileIndex &index, uint32_t node) {
    std::vector<std::string_view> chain;
    for (uint32_t at = node; at != FileIndex::no_parent; at = index.nodes[at].parent) {
        chain.push_back(node_name(index, index.nodes[at]));
    }
    std::string path = index.root == "/" ? "" : index.root;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        path += '/';
        path += *it;
    }
    if (index.nodes[node].is_dir) {
        path += '/';
    }
    return path;
}

std::vector<FileEntry> file_index_search(const FileIndex &index, const std::string &query,
                                         const VisitStore &visits, int max_results) {
    std::vector<std::string> parts = split_query_parts(query);
    if (parts.empty() || max_results <= 0) {
        return {};
    }
    std::unordered_set<std::string> visited = visited_names(visits);
    std::vector<Candidate> dirs;
    std::vector<Candidate> files;
    std::string_view lower = index.lower;
    for (std::size_t at = lower.find(parts[0]); at != std::string_view::npos;) {
        auto it = std::ranges::upper_bound(index.nodes, at, {}, &FileIndex::Node::name_offset);
        auto i = static_cast<uint32_t>(it - index.nodes.begin() - 1);
        const FileIndex::Node &node = index.nodes[i];
        float score = score_parts(lower.substr(node.name_offset, node.name_length), parts);
        if (score >= 0.0f) {
            std::string_view name = node_name(index, node);
            int count =
                !visited.empty() && visited.contains(std::string(name))
                    ? visit_store_get(visits, visit_store_file_key(file_index_path(index, i)))
                    : 0;
            (node.is_dir ? dirs : files).push_back({i, count, score});
        }
        at = lower.find(parts[0], node.name_offset + node.name_length);
    }

    auto better = [&index](const Candidate &a, const Candidate &b) {
        if (a.visits != b.visits) {
            return a.visits > b.visits;
        }
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return lower_less(node_name(index, index.nodes[a.node]),
                          node_name(index, index.nodes[b.node]));
    };
    std::vector<FileEntry> out;
    for (std::vector<Candidate> *kind : {&dirs, &files}) {
        auto keep = std::min(kind->size(), static_cast<std::size_t>(max_results));
        std::partial_sort(kind->begin(), kind->begin() + static_cast<std::ptrdiff_t>(keep),
                          kind->end(), better);
        for (std::size_t i = 0; i < keep; ++i) {
            const Candidate &c = (*kind)[i];
            const FileIndex::Node &node = index.nodes[c.node];
            out.push_back({std::string(node_name(index, node)), file_index_path(index, c.node),
                           node.is_dir, c.score});
        }
    }
    return out;
}

} // namespace astralia::launcher
