//! Grid growth of culms, nodes, branches and leaves, rendered as glyphs.

use pangocairo::cairo;

use super::Frame;
use super::random::{random_range, random01};
use crate::config::bamboo_config::*;
use crate::modules::rain::{argb_buffer, with_cairo};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    Growing,
    Holding,
    Fading,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Ink {
    None,
    Ground,
    Shoot,
    Culm,
    Node,
    Branch,
    Leaf,
}

#[derive(Clone, Copy)]
struct Cell {
    glyph: char,
    ink: Ink,
}

const EMPTY: Cell = Cell { glyph: ' ', ink: Ink::None };

#[derive(Clone, Copy)]
struct Leaf {
    x: i32,
    y: i32,
    glyph: char,
}

#[derive(Clone)]
struct Tip {
    x: i32,
    y: i32,
    side: i32,
    length: i32,
    target: i32,
    since_node: i32,
    next_node: i32,
    branch: bool,
    grown: bool,
    done: bool,
    next_leaf: usize,
    seeds: Vec<(i32, i32)>,
    path: Vec<(i32, i32)>,
    leaves: Vec<Leaf>,
}

impl Default for Tip {
    fn default() -> Self {
        Tip {
            x: 0,
            y: 0,
            side: 1,
            length: 0,
            target: 1,
            since_node: 0,
            next_node: 0,
            branch: false,
            grown: false,
            done: false,
            next_leaf: 0,
            seeds: Vec::new(),
            path: Vec::new(),
            leaves: Vec::new(),
        }
    }
}

fn random_int(lo: i32, hi: i32) -> i32 {
    let span = hi - lo + 1;
    lo + (span - 1).min((random01() * span as f32) as i32)
}

#[derive(Default)]
struct Grid {
    cells: Vec<Cell>,
    cols: i32,
    rows: i32,
}

impl Grid {
    fn at(&self, x: i32, y: i32) -> Option<&Cell> {
        (x >= 0 && x < self.cols && y >= 0 && y < self.rows).then(|| &self.cells[(y * self.cols + x) as usize])
    }

    fn put(&mut self, x: i32, y: i32, glyph: char, ink: Ink) {
        if x >= 0 && x < self.cols && y >= 0 && y < self.rows {
            self.cells[(y * self.cols + x) as usize] = Cell { glyph, ink };
        }
    }

    fn put_leaf(&mut self, x: i32, y: i32, glyph: char) {
        if self.at(x, y).is_some_and(|c| c.ink == Ink::None) {
            self.put(x, y, glyph, Ink::Leaf);
        }
    }

    fn advance_culm(&mut self, tip: &mut Tip, spawned: &mut Vec<Tip>) {
        tip.y -= 1;
        tip.length += 1;
        tip.since_node += 1;
        if tip.y < 0 {
            tip.done = true;
            spawn_branches(tip, spawned);
            return;
        }
        if tip.since_node >= tip.next_node {
            self.put(tip.x, tip.y, '=', Ink::Node);
            self.put(tip.x + 1, tip.y, '=', Ink::Node);
            tip.since_node = 0;
            tip.next_node = random_int(NODE_SPACING_MIN_ROWS, NODE_SPACING_MAX_ROWS);
            if tip.length as f32 >= (1.0 - BRANCH_ZONE_RATIO) * tip.target as f32 {
                tip.seeds.push((tip.x, tip.y));
            }
        } else {
            self.put(tip.x, tip.y, CULM_GLYPH, Ink::Culm);
            self.put(tip.x + 1, tip.y, CULM_GLYPH, Ink::Culm);
        }
        if tip.length >= tip.target {
            tip.done = true;
            spawn_branches(tip, spawned);
        }
    }

    fn advance_branch(&mut self, tip: &mut Tip) {
        if !tip.grown {
            let flat = random01() < BRANCH_FLAT_CHANCE;
            tip.x += tip.side;
            if !flat {
                tip.y -= 1;
            }
            tip.length += 1;
            let inside = tip.x >= 0 && tip.x < self.cols && tip.y >= 0;
            if inside {
                let glyph = if flat { '-' } else if tip.side > 0 { '/' } else { '\\' };
                self.put(tip.x, tip.y, glyph, Ink::Branch);
                tip.path.push((tip.x, tip.y));
            }
            if !inside || tip.length >= tip.target {
                tip.grown = true;
                plan_leaves(tip);
            }
            return;
        }
        if let Some(leaf) = tip.leaves.get(tip.next_leaf).copied() {
            tip.next_leaf += 1;
            self.put_leaf(leaf.x, leaf.y, leaf.glyph);
        }
        tip.done = tip.next_leaf >= tip.leaves.len();
    }
}

fn spawn_branches(culm: &Tip, spawned: &mut Vec<Tip>) {
    let mut side = culm.side;
    for &(x, y) in &culm.seeds {
        if random01() >= BRANCH_CHANCE {
            continue;
        }
        spawned.push(Tip {
            branch: true,
            side,
            x: if side > 0 { x + 1 } else { x },
            y,
            target: random_int(BRANCH_MIN_LENGTH, BRANCH_MAX_LENGTH),
            ..Tip::default()
        });
        side = -side;
    }
}

fn plan_leaves(tip: &mut Tip) {
    let glyph = if tip.side > 0 { LEAF_GLYPH_RIGHT } else { LEAF_GLYPH_LEFT };
    let mut above = 0;
    for (i, &(x, y)) in tip.path.iter().enumerate() {
        if (i as i32 + 1) % LEAF_SPACING != 0 {
            continue;
        }
        let dy = if above % 2 == 0 { -1 } else { 1 };
        above += 1;
        tip.leaves.push(Leaf { x, y: y + dy, glyph });
    }
    if let Some(&(x, y)) = tip.path.last() {
        tip.leaves.push(Leaf { x: x + tip.side, y, glyph });
    }
}

fn ink_color(ink: Ink) -> u32 {
    match ink {
        Ink::Ground => GROUND_COLOR,
        Ink::Shoot => SHOOT_COLOR,
        Ink::Culm => CULM_COLOR,
        Ink::Node => NODE_COLOR,
        Ink::Branch => BRANCH_COLOR,
        Ink::Leaf => LEAF_COLOR,
        Ink::None => 0,
    }
}

pub struct Grow {
    width: i32,
    height: i32,
    stride: i32,
    hold_left: i32,
    cycles: i32,
    fade: f32,
    phase: Phase,
    grid: Grid,
    buffer: Vec<u8>,
    tips: Vec<Tip>,
}

impl Default for Grow {
    fn default() -> Self {
        Grow {
            width: 0,
            height: 0,
            stride: 0,
            hold_left: 0,
            cycles: 0,
            fade: 1.0,
            phase: Phase::Growing,
            grid: Grid::default(),
            buffer: Vec::new(),
            tips: Vec::new(),
        }
    }
}

impl Grow {
    pub fn rebuild(&mut self, width: i32, height: i32) {
        self.width = width.max(1);
        self.height = height.max(1);
        (self.buffer, self.stride) = argb_buffer(self.width, self.height);
        self.cycles = 0;
        self.start_cycle();
    }

    pub fn phase(&self) -> Phase {
        self.phase
    }

    pub fn cycles(&self) -> i32 {
        self.cycles
    }

    pub fn frame(&self) -> Option<Frame<'_>> {
        (!self.buffer.is_empty()).then_some(Frame { pixels: &self.buffer, width: self.width, height: self.height, stride: self.stride })
    }

    pub fn text(&self) -> String {
        let mut out = String::new();
        for y in 0..self.grid.rows {
            if y > 0 {
                out.push('\n');
            }
            out.extend(self.grid.cells[(y * self.grid.cols) as usize..][..self.grid.cols as usize].iter().map(|c| c.glyph));
        }
        out
    }

    fn start_cycle(&mut self) {
        self.phase = Phase::Growing;
        self.fade = 1.0;
        self.tips.clear();
        let cols = ((self.width as f32 / CELL_WIDTH_PX) as i32).max(1);
        let rows = ((self.height as f32 / CELL_HEIGHT_PX) as i32).max(1);
        self.grid = Grid { cells: vec![EMPTY; (cols * rows) as usize], cols, rows };

        let ground = rows - 1 - GROUND_MARGIN_ROWS;
        for x in GROUND_INSET_COLS..cols - GROUND_INSET_COLS {
            self.grid.put(x, ground, '=', Ink::Ground);
        }

        let usable = cols - 2 * GROUND_INSET_COLS;
        let slot = (usable / SHOOT_COUNT).max(1);
        let mut xs = Vec::with_capacity(SHOOT_COUNT as usize);
        let mut heights = Vec::with_capacity(SHOOT_COUNT as usize);
        for i in 0..SHOOT_COUNT {
            let jitter = if slot > 2 { random_int(0, slot - 2) } else { 0 };
            let x = GROUND_INSET_COLS + slot * i + jitter;
            let h = random_int(1, SHOOT_MAX_ROWS);
            xs.push(x);
            heights.push(h);
            if h >= 2 {
                self.grid.put(x, ground - 2, '/', Ink::Shoot);
                self.grid.put(x + 1, ground - 2, '\\', Ink::Shoot);
                self.grid.put(x, ground - 1, '|', Ink::Shoot);
                self.grid.put(x + 1, ground - 1, '|', Ink::Shoot);
            } else {
                self.grid.put(x, ground - 1, '/', Ink::Shoot);
                self.grid.put(x + 1, ground - 1, '\\', Ink::Shoot);
            }
        }

        let mut order: Vec<usize> = (0..SHOOT_COUNT as usize).collect();
        let culms = CULM_COUNT.min(SHOOT_COUNT);
        for i in 0..culms {
            order.swap(i as usize, random_int(i, SHOOT_COUNT - 1) as usize);
        }
        for &shoot in &order[..culms as usize] {
            let side = if random01() < 0.5 { 1 } else { -1 };
            let target = ((random_range(CULM_MIN_HEIGHT_RATIO, CULM_MAX_HEIGHT_RATIO) * ground.max(1) as f32) as i32).max(1);
            let next_node = random_int(NODE_SPACING_MIN_ROWS, NODE_SPACING_MAX_ROWS);
            self.tips.push(Tip { x: xs[shoot], y: ground - heights[shoot], side, target, next_node, ..Tip::default() });
        }
        self.render(1.0);
    }

    pub fn tick(&mut self) {
        if self.buffer.is_empty() {
            return;
        }
        match self.phase {
            Phase::Growing => {
                self.grow_tips();
                self.render(1.0);
                if self.tips.iter().all(|t| t.done) {
                    self.phase = Phase::Holding;
                    self.hold_left = HOLD_TICKS;
                }
            }
            Phase::Holding => {
                self.hold_left -= 1;
                if self.hold_left <= 0 {
                    self.phase = Phase::Fading;
                }
            }
            Phase::Fading => {
                self.fade -= FADE_STEP;
                if self.fade <= 0.0 {
                    self.cycles += 1;
                    self.start_cycle();
                } else {
                    self.render(self.fade);
                }
            }
        }
    }

    fn grow_tips(&mut self) {
        let mut spawned = Vec::new();
        for tip in self.tips.iter_mut().filter(|t| !t.done) {
            if tip.branch {
                self.grid.advance_branch(tip);
            } else {
                self.grid.advance_culm(tip, &mut spawned);
            }
        }
        self.tips.extend(spawned);
    }

    fn render(&mut self, alpha: f32) {
        self.buffer.fill(0);
        let (width, height, grid) = (self.width as f32, self.height as f32, &self.grid);
        with_cairo(&mut self.buffer, self.width, self.height, self.stride, |cr| {
            if let Ok(mut options) = cairo::FontOptions::new() {
                options.set_antialias(cairo::Antialias::Gray);
                cr.set_font_options(&options);
            }
            cr.select_font_face("monospace", cairo::FontSlant::Normal, cairo::FontWeight::Bold);
            cr.set_font_size(f64::from(FONT_PX));
            let (Ok(font), Ok(probe)) = (cr.font_extents(), cr.text_extents("M")) else { return };
            let origin_x = (width - grid.cols as f32 * CELL_WIDTH_PX) / 2.0;
            let origin_y = (height - grid.rows as f32 * CELL_HEIGHT_PX) / 2.0;
            let inset_x = (CELL_WIDTH_PX - probe.x_advance() as f32) / 2.0;
            let inset_y = (CELL_HEIGHT_PX - (font.ascent() + font.descent()) as f32) / 2.0 + font.ascent() as f32;
            let mut utf8 = [0u8; 4];
            for y in 0..grid.rows {
                for x in 0..grid.cols {
                    let cell = grid.cells[(y * grid.cols + x) as usize];
                    if cell.ink == Ink::None {
                        continue;
                    }
                    let argb = ink_color(cell.ink);
                    cr.set_source_rgba(
                        f64::from((argb >> 16) & 0xFF) / 255.0,
                        f64::from((argb >> 8) & 0xFF) / 255.0,
                        f64::from(argb & 0xFF) / 255.0,
                        f64::from(((argb >> 24) & 0xFF) as f32 * alpha) / 255.0,
                    );
                    cr.move_to(
                        f64::from(origin_x + x as f32 * CELL_WIDTH_PX + inset_x),
                        f64::from(origin_y + y as f32 * CELL_HEIGHT_PX + inset_y),
                    );
                    let _ = cr.show_text(cell.glyph.encode_utf8(&mut utf8));
                }
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::{Grow, Phase};
    use crate::modules::bamboo::random::seed;

    fn ink(pixels: &[u8]) -> usize {
        pixels.iter().skip(3).step_by(4).filter(|&&a| a != 0).count()
    }

    fn glyphs(text: &str) -> usize {
        text.chars().filter(|&c| c != ' ' && c != '\n').count()
    }

    fn ascii_or_bar(text: &str) -> bool {
        text.chars().all(|c| (c as u32) < 0x7F || c == '│')
    }

    fn rows(text: &str) -> Vec<Vec<char>> {
        text.split('\n').map(|r| r.chars().collect()).collect()
    }

    fn is_leaf(c: char) -> bool {
        c == '>' || c == '<'
    }

    fn is_branch(c: char) -> bool {
        c == '-' || c == '/' || c == '\\'
    }

    fn count_leaves(text: &str) -> usize {
        text.chars().filter(|&c| is_leaf(c)).count()
    }

    fn leaves_touch_branches(text: &str) -> bool {
        let grid = rows(text);
        for y in 0..grid.len() {
            for x in 0..grid[y].len() {
                if !is_leaf(grid[y][x]) {
                    continue;
                }
                let mut touches = false;
                for dy in -1i32..=1 {
                    for dx in -1i32..=1 {
                        let (ny, nx) = (y as i32 + dy, x as i32 + dx);
                        if ny < 0 || ny >= grid.len() as i32 || nx < 0 || nx >= grid[ny as usize].len() as i32 {
                            continue;
                        }
                        let near = grid[ny as usize][nx as usize];
                        touches = touches || is_branch(near) || is_leaf(near);
                    }
                }
                if !touches {
                    return false;
                }
            }
        }
        true
    }

    fn pixels(grow: &Grow) -> Vec<u8> {
        grow.frame().map(|f| f.pixels.to_vec()).unwrap_or_default()
    }

    #[test]
    fn check_bamboo_grow() {
        let mut grow = Grow::default();
        assert!(grow.frame().is_none() && grow.text().is_empty(), "a bamboo with no rebuild has no frame");
        grow.tick();
        assert!(grow.frame().is_none(), "ticking without a rebuild does nothing");

        seed(7);
        grow.rebuild(480, 600);
        let frame = grow.frame().expect("frame");
        assert!(frame.width == 480 && frame.height == 600 && frame.stride >= 480 * 4, "the bamboo buffer has the requested size");
        let fresh = grow.text();
        let shoots = glyphs(&fresh);
        assert!(shoots > 0 && grow.phase() == Phase::Growing && ink(&pixels(&grow)) > 0, "a fresh scene draws the ground and the shoots as glyphs");
        assert!(fresh.contains("==") && fresh.contains("/\\"), "the ground and the shoots are ascii art");

        for _ in 0..10 {
            grow.tick();
        }
        let grown = grow.text();
        assert_eq!(count_leaves(&grown), 0, "a culm stays leafless while it grows");
        assert!(glyphs(&grown) > shoots && grown.contains("││"), "ticking grows the culms upward as vertical bars");
        assert!(ascii_or_bar(&grown), "the scene holds ascii and the vertical bar only");
        let first = pixels(&grow);

        seed(7);
        grow.rebuild(480, 600);
        for _ in 0..10 {
            grow.tick();
        }
        assert!(grow.text() == grown && pixels(&grow) == first, "the same seed gives the same frames");

        for _ in 0..500 {
            if grow.phase() != Phase::Growing {
                break;
            }
            grow.tick();
        }
        let full = grow.text();
        assert!(grow.phase() == Phase::Holding && count_leaves(&full) > 0, "the leaves unfurl on the branches before the hold");
        assert!(full.contains('>') && full.contains('<'), "leaves point away from the stem on both sides");
        assert!(leaves_touch_branches(&full), "every leaf sits on a branch");

        let (mut held, mut faded) = (false, false);
        for _ in 0..2000 {
            if grow.cycles() != 0 {
                break;
            }
            grow.tick();
            held = held || grow.phase() == Phase::Holding;
            faded = faded || grow.phase() == Phase::Fading;
        }
        assert!(held && faded && grow.cycles() == 1, "a cycle grows, holds, fades and regrows");
        assert!(grow.phase() == Phase::Growing && glyphs(&grow.text()) > 0, "a new cycle starts with fresh shoots");

        grow.rebuild(200, 100);
        let f = grow.frame().expect("frame");
        assert!(f.width == 200 && f.height == 100 && grow.cycles() == 0, "a rebuild resizes the buffer and restarts the count");
        for _ in 0..50 {
            grow.tick();
        }
        grow.rebuild(0, 0);
        let f = grow.frame().expect("frame");
        assert!(f.width == 1 && f.height == 1, "a degenerate size is clamped");
        for _ in 0..50 {
            grow.tick();
        }
    }
}
