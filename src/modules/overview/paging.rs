//! Workspace numbering on pages of `columns` by `rows` cells.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Grid {
    pub columns: i32,
    pub rows: i32,
}

impl Grid {
    pub fn per_page(self) -> i32 {
        self.columns * self.rows
    }
}

/// The workspace in cell `(row, col)` of `page`, counted from 1.
pub fn workspace_at(grid: Grid, page: i32, row: i32, col: i32) -> i32 {
    page * grid.per_page() + row * grid.columns + col + 1
}

/// The page showing `workspace`; workspaces below 1 are on the first page.
pub fn page_of(grid: Grid, workspace: i32) -> i32 {
    if workspace > 0 { (workspace - 1) / grid.per_page() } else { 0 }
}

/// The workspace `d_col` columns and `d_row` rows from `workspace`, wrapping within `page`.
pub fn step(grid: Grid, page: i32, workspace: i32, d_col: i32, d_row: i32) -> i32 {
    let index = (workspace - 1).rem_euclid(grid.per_page());
    let col = (index % grid.columns + d_col % grid.columns + grid.columns) % grid.columns;
    let row = (index / grid.columns + d_row % grid.rows + grid.rows) % grid.rows;
    workspace_at(grid, page, row, col)
}
