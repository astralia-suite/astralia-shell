#pragma once

namespace astralia::overview {

struct Grid {
    int columns;
    int rows;

    int per_page() const { return columns * rows; }
};

int workspace_at(Grid grid, int page, int row, int col);
int page_of(Grid grid, int workspace);
int step(Grid grid, int page, int workspace, int d_col, int d_row);

} // namespace astralia::overview
