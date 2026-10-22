#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "modules/bamboo/bamboo_frame.h"

namespace astralia {

enum class BambooPhase { Growing, Holding, Fading };

class BambooGrow {
  public:
    void rebuild(int width, int height);
    void tick();

    BambooFrame frame() const {
        return buffer_.empty() ? BambooFrame{}
                               : BambooFrame{buffer_.data(), width_, height_, stride_};
    }
    BambooPhase phase() const { return phase_; }
    int cycles() const { return cycles_; }
    std::string text() const;

  private:
    enum class Ink : uint8_t { None, Ground, Shoot, Culm, Node, Branch, Leaf };

    struct Cell {
        char32_t glyph = U' ';
        Ink ink = Ink::None;
    };

    struct Seed {
        int x = 0;
        int y = 0;
    };

    struct Leaf {
        int x = 0;
        int y = 0;
        char32_t glyph = U' ';
    };

    struct Tip {
        int x = 0;
        int y = 0;
        int side = 1;
        int length = 0;
        int target = 1;
        int since_node = 0;
        int next_node = 0;
        bool branch = false;
        bool grown = false;
        bool done = false;
        size_t next_leaf = 0;
        std::vector<Seed> seeds;
        std::vector<std::pair<int, int>> path;
        std::vector<Leaf> leaves;
    };

    void start_cycle();
    void grow_tips();
    void advance_culm(Tip &tip, std::vector<Tip> &spawned);
    void spawn_branches(const Tip &culm, std::vector<Tip> &spawned);
    void advance_branch(Tip &tip);
    void plan_leaves(Tip &tip);
    void put(int x, int y, char32_t glyph, Ink ink);
    void put_leaf(int x, int y, char32_t glyph);
    void render(float alpha);

    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;
    int cols_ = 0;
    int rows_ = 0;
    int hold_left_ = 0;
    int cycles_ = 0;
    float fade_ = 1.0f;
    BambooPhase phase_ = BambooPhase::Growing;

    std::vector<Cell> grid_;
    std::vector<uint8_t> buffer_;
    std::vector<Tip> tips_;
};

} // namespace astralia
