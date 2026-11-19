//! The overview state: the scroll that carries the grid, one cell per workspace of the page (or of
//! every monitor's page in the global view), a tile per window, and keyboard, click and drag input.

use std::collections::HashMap;
use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::rc::Rc;
use std::time::Instant;

use super::paging::{Grid, page_of, step, workspace_at};
use crate::base::animation::{AnimationManager, Easing, Value, value, writer};
use crate::base::input::{KeyEvent, KeyKind};
use crate::config::overview_config as cfg;
use crate::render::geometry::Rect;
use crate::render::scroll::ScrollMetrics;
use crate::render::scroll_sequence::{ScrollSequence, ScrollTimings};
use crate::service::compositor::{CloseScope, Compositor, CompositorMonitor, CompositorState, WorkArea, compositor_logical_size, compositor_monitor_by_id, compositor_monitor_by_name, compositor_work_area};
use crate::service::icon::window_icon_path;

/// Shows the user a message (summary, body).
pub type Notify = Box<dyn Fn(&str, &str)>;

const OWNER_SEQUENCE: u64 = 5;
const OWNER_INDICATOR_X: u64 = 3;
const OWNER_INDICATOR_Y: u64 = 4;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Cell {
    pub workspace: i32,
    pub rect: Rect,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Layout {
    pub panels: Vec<Rect>,
    pub scale: f32,
    pub cells: Vec<Cell>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Tile {
    pub address: String,
    pub window_class: String,
    pub workspace: i32,
    pub rect: Rect,
}

struct Block {
    group: i32,
    x: f32,
    y: f32,
    cell_w: f32,
    cell_h: f32,
}

fn make_block(work_w: f64, work_h: f64, group: i32, scale: f32, x: f32, y: f32) -> Block {
    Block { group, x, y, cell_w: (work_w.max(1.0) as f32 * scale).round(), cell_h: (work_h.max(1.0) as f32 * scale).round() }
}

/// How many monitors sit wholly before each one along an axis, so blocks of monitors that do not
/// overlap on that axis get a gap between them.
fn separation_steps(monitors: &[CompositorMonitor], position: fn(&CompositorMonitor) -> f64, extent: usize) -> Vec<f32> {
    let mut order: Vec<usize> = (0..monitors.len()).collect();
    order.sort_by(|&a, &b| position(&monitors[a]).total_cmp(&position(&monitors[b])));
    let mut steps = vec![0.0f32; monitors.len()];
    for i in 1..order.len() {
        for j in 0..i {
            let (before, after) = (&monitors[order[j]], &monitors[order[i]]);
            if position(before) + compositor_logical_size(before)[extent] <= position(after) + 0.5 {
                steps[order[i]] = steps[order[i]].max(steps[order[j]] + 1.0);
            }
        }
    }
    steps
}

/// A tween owner per window and rectangle component, above the fixed owners.
fn tile_owner(address: &str, component: u64) -> u64 {
    let mut hasher = DefaultHasher::new();
    address.hash(&mut hasher);
    1000u64.wrapping_add(hasher.finish() << 2).wrapping_add(component)
}

/// The monitor's active workspace, else the focused monitor's, else 1.
fn active_workspace_in(state: &CompositorState, monitor_name: &str) -> i32 {
    let found = state.by_monitor.get(monitor_name).filter(|w| w.active_id >= 1).or_else(|| state.by_monitor.get(&state.focused_monitor));
    found.filter(|w| w.active_id >= 1).map_or(1, |w| w.active_id)
}

fn rect_of(v: &[Value; 4]) -> Rect {
    Rect { x: v[0].get(), y: v[1].get(), w: v[2].get(), h: v[3].get() }
}

struct TileAnim {
    current: [Value; 4],
    target: Rect,
}

pub struct Model {
    compositor: Rc<dyn Compositor>,
    animations: AnimationManager,
    sequence: ScrollSequence,
    /// Set when the sequence's close ends; `settle` finishes the close.
    sequence_done: Rc<std::cell::Cell<bool>>,
    closed: bool,
    close_requested: bool,
    open: bool,
    global_mode: bool,
    output_name: String,
    width: f32,
    height: f32,
    group: i32,
    selected: i32,
    layout: Layout,
    tiles: Vec<Tile>,
    tile_anim: HashMap<String, TileAnim>,
    indicator_x: Value,
    indicator_y: Value,
    indicator_size: (f32, f32),
    indicator_target: Rect,
    indicator_visible: bool,
    indicator_tracking: bool,
    indicator_page: i32,
    dragging: bool,
    press_mods: (bool, bool),
    drag_address: String,
    drag_from: i32,
    drag_target: i32,
    drag_pointer: (f64, f64),
    drag_offset: (f64, f64),
    icon_paths: HashMap<String, String>,
    pub on_notify: Option<Notify>,
}

impl Model {
    pub fn new(compositor: Rc<dyn Compositor>) -> Model {
        Model {
            compositor,
            animations: AnimationManager::default(),
            sequence: ScrollSequence::new(OWNER_SEQUENCE, ScrollTimings::default()),
            sequence_done: Rc::default(),
            closed: false,
            close_requested: false,
            open: false,
            global_mode: false,
            output_name: String::new(),
            width: 0.0,
            height: 0.0,
            group: 0,
            selected: -1,
            layout: Layout::default(),
            tiles: Vec::new(),
            tile_anim: HashMap::new(),
            indicator_x: value(0.0),
            indicator_y: value(0.0),
            indicator_size: (0.0, 0.0),
            indicator_target: Rect::default(),
            indicator_visible: false,
            indicator_tracking: false,
            indicator_page: -1,
            dragging: false,
            press_mods: (false, false),
            drag_address: String::new(),
            drag_from: -1,
            drag_target: -1,
            drag_pointer: (0.0, 0.0),
            drag_offset: (0.0, 0.0),
            icon_paths: HashMap::new(),
            on_notify: None,
        }
    }

    fn state(&self) -> std::cell::Ref<'_, CompositorState> {
        self.compositor.state()
    }

    fn active_workspace(&self, monitor_name: &str) -> i32 {
        active_workspace_in(&self.state(), monitor_name)
    }

    fn grid(&self) -> Grid {
        let per_monitor = self.compositor.workspaces_per_monitor().max(1);
        Grid { columns: cfg::COLUMNS, rows: (per_monitor + cfg::COLUMNS - 1) / cfg::COLUMNS }
    }

    /// The icon file for a window class, looked up once per class.
    pub fn icon_path(&mut self, window_class: &str) -> &str {
        self.icon_paths.entry(window_class.to_string()).or_insert_with(|| window_icon_path(window_class))
    }

    pub fn open(&mut self, output_name: &str, width: f32, height: f32) {
        self.output_name = output_name.to_string();
        self.width = width;
        self.height = height;
        self.selected = self.active_workspace(output_name);
        self.group = page_of(self.grid(), self.selected);
        self.global_mode = false;
        self.dragging = false;
        self.open = true;
        self.closed = false;
        self.indicator_tracking = false;
        self.indicator_visible = false;
        self.sequence_done.set(false);
        self.sequence.open(&mut self.animations);
    }

    /// Plays the grid out and the scroll away; the overview stays open until that ends.
    pub fn close(&mut self) {
        if !self.open || self.sequence.closing() {
            return;
        }
        self.dragging = false;
        let done = self.sequence_done.clone();
        self.sequence.close(&mut self.animations, Some(Box::new(move || done.set(true))));
        self.settle();
    }

    /// Closes at once, without the exit.
    pub fn fast_close(&mut self) {
        if self.open {
            self.animations = AnimationManager::default();
            self.finish_close();
        }
    }

    fn finish_close(&mut self) {
        self.open = false;
        self.dragging = false;
        self.tiles.clear();
        self.tile_anim.clear();
        self.layout = Layout::default();
        self.indicator_visible = false;
        self.closed = true;
    }

    fn settle(&mut self) {
        if self.sequence_done.replace(false) {
            self.finish_close();
        }
    }

    pub fn tick(&mut self, now: Instant) {
        self.animations.tick(now);
        self.settle();
    }

    /// True once after the close has fully ended.
    pub fn take_closed(&mut self) -> bool {
        std::mem::take(&mut self.closed)
    }

    /// True once after `Escape` or a click away asked the host to close.
    pub fn take_close_request(&mut self) -> bool {
        std::mem::take(&mut self.close_requested)
    }

    pub fn is_open(&self) -> bool {
        self.open
    }

    pub fn dragging(&self) -> bool {
        self.dragging
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active()
    }

    #[cfg(test)]
    pub fn global_mode(&self) -> bool {
        self.global_mode
    }

    #[cfg(test)]
    pub fn selected(&self) -> i32 {
        self.selected
    }

    pub fn drag_target(&self) -> i32 {
        self.drag_target
    }

    pub fn slide_y(&self) -> f32 {
        self.sequence.slide() * self.height
    }

    pub fn scroll(&self) -> f32 {
        self.sequence.scroll()
    }

    pub fn grid_alpha(&self) -> f32 {
        self.sequence.grid()
    }

    pub fn fully_open(&self) -> bool {
        self.open && !self.sequence.closing() && self.sequence.settled()
    }

    pub fn layout(&self) -> &Layout {
        &self.layout
    }

    pub fn tiles(&self) -> &[Tile] {
        &self.tiles
    }

    pub fn indicator(&self) -> Option<Rect> {
        self.indicator_visible.then(|| Rect { x: self.indicator_x.get(), y: self.indicator_y.get(), w: self.indicator_size.0, h: self.indicator_size.1 })
    }

    fn find_cell(&self, workspace: i32) -> Option<&Cell> {
        self.layout.cells.iter().find(|c| c.workspace == workspace)
    }

    fn cell_at(&self, x: f64, y: f64) -> Option<&Cell> {
        self.layout.cells.iter().find(|c| c.rect.contains(x, y))
    }

    fn compute_layout(&mut self, width: f32, height: f32) {
        let grid = self.grid();
        let (columns, rows) = (grid.columns as f32, grid.rows as f32);
        let scroll_style = ScrollMetrics::default();
        let spacing = cfg::SPACING.round();
        let pad = cfg::PADDING;
        let mut layout = Layout::default();
        let mut blocks = Vec::new();
        let state = self.compositor.state();

        if self.global_mode && !state.monitors.is_empty() {
            let gap_x = spacing * (columns - 1.0) + pad * 2.0 + cfg::GLOBAL_BLOCK_SPACING + scroll_style.reach() * 2.0;
            let gap_y = spacing * (rows - 1.0) + pad * 2.0 + cfg::GLOBAL_BLOCK_SPACING + scroll_style.overhang * 2.0;
            let frame_x = 2.0 * (pad + cfg::MARGIN + scroll_style.reach());
            let frame_y = 2.0 * (pad + cfg::MARGIN);
            let ms = &state.monitors;
            let steps_x = separation_steps(ms, |m| m.x, 0);
            let steps_y = separation_steps(ms, |m| m.y, 1);
            let min_x = ms.iter().map(|m| m.x).fold(ms[0].x, f64::min);
            let min_y = ms.iter().map(|m| m.y).fold(ms[0].y, f64::min);
            let mut scale = cfg::GLOBAL_SCALE;
            for (i, m) in ms.iter().enumerate() {
                let area = compositor_work_area(m);
                let fixed_x = steps_x[i] * gap_x + spacing * (columns - 1.0) + frame_x;
                let fixed_y = steps_y[i] * gap_y + spacing * (rows - 1.0) + frame_y;
                let per_x = ((m.x - min_x + area.width.max(1.0)) * f64::from(grid.columns)) as f32;
                let per_y = ((m.y - min_y + area.height.max(1.0)) * f64::from(grid.rows)) as f32;
                scale = scale.min((width - fixed_x) / per_x).min((height - fixed_y) / per_y);
            }
            let scale = scale.max(0.01);
            layout.scale = scale;
            for (i, m) in ms.iter().enumerate() {
                let area = compositor_work_area(m);
                let x = ((m.x - min_x) * f64::from(grid.columns) * f64::from(scale)) as f32 + steps_x[i] * gap_x;
                let y = ((m.y - min_y) * f64::from(grid.rows) * f64::from(scale)) as f32 + steps_y[i] * gap_y;
                let group = page_of(grid, active_workspace_in(&state, &m.name));
                blocks.push(make_block(area.width, area.height, group, scale, x, y));
            }
        } else {
            let (mut work_w, mut work_h) = (f64::from(width), f64::from(height));
            if let Some(area) = compositor_monitor_by_name(&state, &self.output_name).map(compositor_work_area)
                && area.width > 0.0
                && area.height > 0.0
            {
                work_w = area.width;
                work_h = area.height;
            }
            let (work_w, work_h) = (work_w.max(1.0), work_h.max(1.0));
            let frame_x = 2.0 * (pad + cfg::MARGIN + scroll_style.reach());
            let frame_y = 2.0 * (pad + cfg::MARGIN);
            let fit_x = (width - frame_x - spacing * (columns - 1.0)) / (work_w as f32 * columns);
            let fit_y = (height - frame_y - spacing * (rows - 1.0)) / (work_h as f32 * rows);
            layout.scale = cfg::SCALE.min(fit_x).min(fit_y).max(0.01);
            blocks.push(make_block(work_w, work_h, self.group, layout.scale, 0.0, 0.0));
        }
        drop(state);

        let (mut grid_w, mut grid_h) = (0.0f32, 0.0f32);
        for b in &blocks {
            grid_w = grid_w.max(b.x + b.cell_w * columns + spacing * (columns - 1.0));
            grid_h = grid_h.max(b.y + b.cell_h * rows + spacing * (rows - 1.0));
        }
        let root_w = grid_w + pad * 2.0 + cfg::MARGIN * 2.0;
        let root_h = grid_h + pad * 2.0 + cfg::MARGIN * 2.0;
        let origin_x = ((width - root_w) / 2.0).round() + cfg::MARGIN;
        let origin_y = ((height - root_h) / 2.0).round() + self.slide_y() + cfg::MARGIN;

        for b in &blocks {
            layout.panels.push(Rect {
                x: origin_x + b.x,
                y: origin_y + b.y,
                w: b.cell_w * columns + spacing * (columns - 1.0) + pad * 2.0,
                h: b.cell_h * rows + spacing * (rows - 1.0) + pad * 2.0,
            });
            for row in 0..grid.rows {
                for col in 0..grid.columns {
                    layout.cells.push(Cell {
                        workspace: workspace_at(grid, b.group, row, col),
                        rect: Rect {
                            x: origin_x + b.x + pad + col as f32 * (b.cell_w + spacing),
                            y: origin_y + b.y + pad + row as f32 * (b.cell_h + spacing),
                            w: b.cell_w,
                            h: b.cell_h,
                        },
                    });
                }
            }
        }
        self.layout = layout;
    }

    fn rebuild_tiles(&mut self) {
        let state = self.compositor.state();
        let mut visible: Vec<_> = state.clients.iter().filter(|c| self.find_cell(c.workspace_id).is_some()).collect();
        visible.sort_by_key(|c| (c.pinned, c.floating, c.fullscreen > 0, c.workspace_id, std::cmp::Reverse(c.focus_history_id)));
        let fallback = compositor_monitor_by_name(&state, &self.output_name);
        let mut tiles = Vec::with_capacity(visible.len());
        for client in visible {
            let source = compositor_monitor_by_id(&state, client.monitor_id).or(fallback);
            let mut area = source.map_or(WorkArea { x: 0.0, y: 0.0, width: f64::from(self.width), height: f64::from(self.height) }, compositor_work_area);
            area.width = area.width.max(1.0);
            area.height = area.height.max(1.0);
            let Some(cell) = self.find_cell(client.workspace_id).map(|c| c.rect) else { continue };
            let (cell_w, cell_h) = (f64::from(cell.w), f64::from(cell.h));
            let scale = (cell_w / area.width).min(cell_h / area.height);
            let w = (client.size[0] * scale).max(1.0).min(cell_w);
            let h = (client.size[1] * scale).max(1.0).min(cell_h);
            let x = ((client.at[0] - area.x) * scale).max(0.0).clamp(0.0, (cell_w - w).max(0.0));
            let y = ((client.at[1] - area.y) * scale).max(0.0).clamp(0.0, (cell_h - h).max(0.0));
            tiles.push(Tile {
                address: client.address.clone(),
                window_class: client.window_class.clone(),
                workspace: client.workspace_id,
                rect: Rect { x: (f64::from(cell.x) + x) as f32, y: (f64::from(cell.y) + y) as f32, w: w as f32, h: h as f32 },
            });
        }
        drop(state);

        for tile in &tiles {
            match self.tile_anim.get_mut(&tile.address) {
                None => {
                    let r = tile.rect;
                    self.tile_anim.insert(tile.address.clone(), TileAnim { current: [value(r.x), value(r.y), value(r.w), value(r.h)], target: r });
                }
                Some(anim) if anim.target != tile.rect => {
                    let (from, to) = (anim.target, tile.rect);
                    let pairs = [(from.x, to.x), (from.y, to.y), (from.w, to.w), (from.h, to.h)];
                    for (component, (a, b)) in pairs.into_iter().enumerate() {
                        let owner = tile_owner(&tile.address, component as u64);
                        self.animations.animate(a, b, cfg::ANIM_MS, Easing::EaseOutCubic, writer(&anim.current[component]), None, owner);
                    }
                    anim.target = to;
                }
                Some(_) => {}
            }
        }
        self.tile_anim.retain(|address, _| tiles.iter().any(|t| &t.address == address));
        if self.dragging {
            let (mut rest, dragged): (Vec<_>, Vec<_>) = tiles.into_iter().partition(|t| t.address != self.drag_address);
            rest.extend(dragged);
            tiles = rest;
        }
        self.tiles = tiles;
    }

    fn update_indicator(&mut self) {
        self.indicator_visible = false;
        let monitor = if self.global_mode { self.state().focused_monitor.clone() } else { self.output_name.clone() };
        let active = self.active_workspace(&monitor);
        let page = page_of(self.grid(), active);
        let Some(rect) = self.find_cell(active).map(|c| c.rect) else {
            self.indicator_tracking = false;
            return;
        };
        if !self.sequence.settled() {
            return;
        }
        if !self.indicator_tracking || self.indicator_page != page {
            self.indicator_x.set(rect.x);
            self.indicator_y.set(rect.y);
            self.indicator_target = rect;
            self.indicator_tracking = true;
            self.indicator_page = page;
        } else if self.indicator_target.x != rect.x || self.indicator_target.y != rect.y {
            let (x, y) = (self.indicator_x.get(), self.indicator_y.get());
            self.animations.animate(x, rect.x, cfg::ANIM_MS, Easing::EaseOutCubic, writer(&self.indicator_x), None, OWNER_INDICATOR_X);
            self.animations.animate(y, rect.y, cfg::ANIM_MS, Easing::EaseOutCubic, writer(&self.indicator_y), None, OWNER_INDICATOR_Y);
            self.indicator_target = rect;
        }
        self.indicator_size = (rect.w, rect.h);
        self.indicator_visible = true;
    }

    /// Lays the grid out for a `width` by `height` surface from the compositor's current state.
    pub fn sync(&mut self, width: f32, height: f32) {
        if !self.open {
            return;
        }
        self.width = width;
        self.height = height;
        self.compute_layout(width, height);
        self.rebuild_tiles();
        self.update_indicator();
    }

    /// Where `tile` draws now: mid-tween, or under the pointer while dragged.
    pub fn tile_rect(&self, tile: &Tile) -> Rect {
        let mut rect = self.tile_anim.get(&tile.address).map_or(tile.rect, |a| rect_of(&a.current));
        if self.dragging && tile.address == self.drag_address {
            rect.x = (self.drag_pointer.0 - self.drag_offset.0) as f32;
            rect.y = (self.drag_pointer.1 - self.drag_offset.1) as f32;
        }
        rect
    }

    fn select(&mut self, workspace: i32, shift: bool, alt: bool) {
        self.selected = workspace;
        self.group = page_of(self.grid(), workspace);
        if shift {
            self.compositor.swap_workspace(workspace, self.global_mode);
        } else if alt {
            self.compositor.move_workspace_in(workspace, self.global_mode);
        } else {
            self.compositor.focus_workspace(workspace, self.global_mode);
        }
    }

    /// Arrows move the selection, digits pick a cell, `d` closes windows, `Tab` switches to the
    /// global view, `Escape` asks to close; true when the key was taken.
    pub fn key(&mut self, event: &KeyEvent) -> bool {
        if !self.open || self.sequence.closing() {
            return false;
        }
        let grid = self.grid();
        if self.global_mode {
            self.group = page_of(grid, self.selected);
        }
        let sym = if event.base_sym != 0 {
            event.base_sym
        } else if event.text.len() == 1 {
            u32::from(event.text.as_bytes()[0])
        } else {
            0
        };
        match event.kind {
            KeyKind::Left | KeyKind::Right | KeyKind::Up | KeyKind::Down => {
                let d_col = match event.kind {
                    KeyKind::Left => -1,
                    KeyKind::Right => 1,
                    _ => 0,
                };
                let d_row = match event.kind {
                    KeyKind::Up => -1,
                    KeyKind::Down => 1,
                    _ => 0,
                };
                self.select(step(grid, self.group, self.selected, d_col, d_row), event.shift, event.alt);
                true
            }
            KeyKind::Tab => {
                if !self.global_mode && self.state().monitors.len() < 2 {
                    if let Some(notify) = &self.on_notify {
                        notify("Overview", "This device only has one display.");
                    }
                    return true;
                }
                self.global_mode = !self.global_mode;
                self.dragging = false;
                self.indicator_tracking = false;
                self.group = page_of(grid, self.selected);
                true
            }
            KeyKind::Escape => {
                self.close_requested = true;
                true
            }
            KeyKind::Text if (u32::from(b'0')..=u32::from(b'9')).contains(&sym) => {
                let position = if sym == u32::from(b'0') { 10 } else { (sym - u32::from(b'0')) as i32 };
                if position <= grid.per_page() {
                    self.select(self.group * grid.per_page() + position, event.shift, event.alt);
                }
                true
            }
            KeyKind::Text if sym == u32::from(b'd') => {
                if event.ctrl {
                    self.compositor.close_windows(CloseScope::All, -1);
                } else if event.shift {
                    let id = compositor_monitor_by_name(&self.state(), &self.output_name).map(|m| m.id);
                    if let Some(id) = id {
                        self.compositor.close_windows(CloseScope::Monitor, id);
                    }
                } else {
                    self.compositor.close_windows(CloseScope::Workspace, self.selected);
                }
                true
            }
            _ => false,
        }
    }

    /// A press on a tile starts a drag, on a cell selects it, and away from every panel asks to close.
    pub fn press(&mut self, x: f64, y: f64, shift: bool, alt: bool) {
        if !self.fully_open() {
            return;
        }
        self.press_mods = (shift, alt);
        let hit = self.tiles.iter().rev().map(|t| (t, self.tile_rect(t))).find(|(_, r)| r.contains(x, y)).map(|(t, r)| (t.address.clone(), t.workspace, r));
        if let Some((address, workspace, drawn)) = hit {
            self.dragging = true;
            self.drag_address = address;
            self.drag_from = workspace;
            self.drag_target = -1;
            self.drag_offset = (x - f64::from(drawn.x), y - f64::from(drawn.y));
            self.drag_pointer = (x, y);
            return;
        }
        if self.layout.cells.is_empty() {
            self.close_requested = true;
            return;
        }
        if let Some(workspace) = self.cell_at(x, y).map(|c| c.workspace) {
            self.select(workspace, shift, alt);
            return;
        }
        let m = cfg::MARGIN;
        let near_panel = self.layout.panels.iter().any(|p| Rect { x: p.x - m, y: p.y - m, w: p.w + 2.0 * m, h: p.h + 2.0 * m }.contains(x, y));
        if !near_panel {
            self.close_requested = true;
        }
    }

    pub fn move_to(&mut self, x: f64, y: f64) {
        if !self.dragging {
            return;
        }
        self.drag_pointer = (x, y);
        self.drag_target = self.cell_at(x, y).map_or(-1, |c| c.workspace);
    }

    /// Drops a dragged window: onto another workspace moves it, onto its own selects that workspace.
    pub fn release(&mut self) {
        if !self.dragging {
            return;
        }
        self.dragging = false;
        let (x, y) = self.drag_pointer;
        let Some(workspace) = self.cell_at(x, y).map(|c| c.workspace) else { return };
        if workspace != self.drag_from {
            self.compositor.move_window(&self.drag_address, workspace, self.global_mode);
        } else {
            self.select(workspace, self.press_mods.0, self.press_mods.1);
        }
    }

    /// Whether a press at `(x, y)` would act on a tile or a cell.
    pub fn clickable(&self, x: f64, y: f64) -> bool {
        self.fully_open() && (self.tiles.iter().any(|t| self.tile_rect(t).contains(x, y)) || self.cell_at(x, y).is_some())
    }
}
