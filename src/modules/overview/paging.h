#pragma once

namespace astralia {

struct OverviewGrid {
    int columns;
    int rows;

    int per_page() const { return columns * rows; }
};

int overview_workspace_at(OverviewGrid grid, int page, int row, int col);
int overview_page_of(OverviewGrid grid, int workspace);
int overview_step(OverviewGrid grid, int page, int workspace, int d_col, int d_row);

} // namespace astralia
