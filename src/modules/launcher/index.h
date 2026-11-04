#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "config/launcher_config.h"

namespace astralia::launcher {

struct FileIndex {
    struct Node {
        uint32_t parent;
        uint32_t name_offset;
        uint16_t name_length;
        bool is_dir;
    };

    static constexpr uint32_t no_parent = UINT32_MAX;

    std::string root;
    std::vector<Node> nodes;
    std::string names;
    std::string lower;
};

std::shared_ptr<FileIndex> file_index_build(const std::string &root,
                                            const std::atomic<bool> &cancel);
std::string file_index_path(const FileIndex &index, uint32_t node);
std::vector<FileEntry> file_index_search(const FileIndex &index, const std::string &query,
                                         const VisitStore &visits, int max_results);

} // namespace astralia::launcher
