#include "modules/overview/paging.h"

namespace astralia {

int overview_workspace_at(OverviewGrid grid, int page, int row, int col) {
    return page * grid.per_page() + row * grid.columns + col + 1;
}

int overview_page_of(OverviewGrid grid, int workspace) {
    return workspace > 0 ? (workspace - 1) / grid.per_page() : 0;
}

int overview_step(OverviewGrid grid, int page, int workspace, int d_col, int d_row) {
    int index = (workspace - 1) % grid.per_page();
    if (index < 0) {
        index += grid.per_page();
    }
    int col = (index % grid.columns + d_col % grid.columns + grid.columns) % grid.columns;
    int row = (index / grid.columns + d_row % grid.rows + grid.rows) % grid.rows;
    return overview_workspace_at(grid, page, row, col);
}

} // namespace astralia
