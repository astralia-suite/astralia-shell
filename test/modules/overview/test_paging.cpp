#include "check.h"

#include "modules/overview/paging.h"

using test::check;

void check_overview_paging() {
    constexpr astralia::OverviewGrid grid{5, 2};
    check(grid.per_page() == 10, "a page holds columns times rows");
    check(astralia::overview_workspace_at(grid, 0, 0, 0) == 1, "first cell is workspace 1");
    check(astralia::overview_workspace_at(grid, 0, 1, 4) == 10, "last cell of the first page");
    check(astralia::overview_workspace_at(grid, 2, 0, 3) == 24, "pages continue the numbering");
    check(astralia::overview_page_of(grid, 1) == 0 && astralia::overview_page_of(grid, 10) == 0 && astralia::overview_page_of(grid, 11) == 1, "page of a workspace");
    check(astralia::overview_page_of(grid, 0) == 0 && astralia::overview_page_of(grid, -3) == 0, "invalid workspaces map to the first page");
    check(astralia::overview_step(grid, 0, 1, 1, 0) == 2, "step right");
    check(astralia::overview_step(grid, 0, 1, -1, 0) == 5, "step left wraps within the row");
    check(astralia::overview_step(grid, 0, 5, 1, 0) == 1, "step right wraps within the row");
    check(astralia::overview_step(grid, 0, 3, 0, 1) == 8, "step down");
    check(astralia::overview_step(grid, 0, 3, 0, -1) == 8, "step up wraps to the last row");
    check(astralia::overview_step(grid, 1, 13, 1, 0) == 14, "step stays on the given page");
    check(astralia::overview_step(grid, 1, 3, 1, 0) == 14, "the given page wins over the workspace's own page");
    check(astralia::overview_step(grid, 0, 1, 7, 0) == 3, "large steps wrap");
}
