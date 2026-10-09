#include <array>
#include <chrono>

#include "check.h"
#include "render/recording_canvas.h"

#include "core/animation.h"
#include "core/poll_reactor.h"

#include "modules/bar/panel/battery_panel.h"
#include "modules/bar/panel/clock_panel.h"
#include "modules/bar/panel/network_panel.h"
#include "modules/bar/panel/panel.h"
#include "modules/bar/panel/panel_set.h"
#include "modules/bar/panel/tray_panel.h"

namespace {

using astralia::Panel;
using astralia::PanelContent;
using astralia::PanelPaint;
using astralia::PanelRegion;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

class FakeContent final : public PanelContent {
  public:
    float height = 100.0f;
    float dialog = 0.0f;
    int activated = 0;
    int last_id = 0;
    int dragged = 0;
    int dropped = 0;
    int opened_count = 0;
    int closed_count = 0;
    int dismissed = 0;
    bool header = true;
    float popup = 0.0f;
    int hover_id = -1;
    int body_hover = -2;
    int hover_a = -1;

    std::string_view title() const override { return "Fake"; }
    bool has_header() const override { return header; }
    astralia::ui::Box popup_anchor() const override { return {10, 20, 40, 40}; }
    float popup_width() const override { return popup > 0.0f ? 220.0f : 0.0f; }
    float popup_height() const override { return popup; }
    bool hover(int id, int) override {
        bool changed = id != body_hover;
        body_hover = id;
        return changed;
    }
    bool popup_hover(int id, int a) override {
        bool changed = id != hover_id || a != hover_a;
        hover_id = id;
        hover_a = a;
        return changed;
    }
    void paint_popup(astralia::ui::Canvas &, const astralia::ui::Box &box, PanelPaint &paint) override { paint.region({box.x + 4, box.y + 4, 100, 28}, 9, 1); }
    void opened() override { ++opened_count; }
    void closed() override { ++closed_count; }
    float content_height(astralia::ui::Canvas &) override { return height; }
    void paint(astralia::ui::Canvas &, const astralia::ui::Box &view, float scroll, PanelPaint &paint) override {
        paint.region({view.x, view.y - scroll, 50, 20}, 7, 3, 4, false);
        paint.region({view.x, view.y - scroll + 30, 100, 20}, 8, 0, 0, true);
    }
    bool activate(const PanelRegion &region, double, double) override {
        ++activated;
        last_id = region.id;
        return true;
    }
    bool drag(const PanelRegion &, double, double) override {
        ++dragged;
        return true;
    }
    void drop(const PanelRegion &) override { ++dropped; }
    float dialog_height() override { return dialog; }
    bool dismiss_dialog() override {
        if (dialog <= 0.0f) {
            return false;
        }
        dialog = 0.0f;
        ++dismissed;
        return true;
    }
};

struct Harness {
    astralia::PollReactor reactor = [] {
        auto made = astralia::PollReactor::create();
        return std::move(*made);
    }();
    FakeContent *content = nullptr;
    std::unique_ptr<Panel> panel;
    test::RecordingCanvas canvas;
    int closed = 0;

    Harness() {
        auto owned = std::make_unique<FakeContent>();
        content = owned.get();
        panel = std::make_unique<Panel>(std::move(owned), reactor);
        panel->on_closed = [this] { ++closed; };
    }
};

void check_panel_framework() {
    Instant instant;
    Harness h;
    test::check(!h.panel->is_open() && h.panel->paint(h.canvas, 1000, 50).w == 0, "a closed panel paints nothing");
    h.panel->open();
    test::check(h.panel->is_open() && h.content->opened_count == 1, "open calls the content");
    astralia::ui::Box card = h.panel->paint(h.canvas, 1000, 50);
    test::check(card.w == 400 && card.x == 580 && card.y == 50, "the card sits at the right margin under the bar");
    float chrome = 20 + 32 + 4 + 1 + 8 + 20;
    test::check(card.h == chrome + 100, "the card height is chrome plus content");
    test::check(h.panel->contains(600, 60) && !h.panel->contains(10, 10), "contains covers the card only");

    double bx = card.x + 20 + 5;
    double by = card.y + 20 + 32 + 4 + 1 + 8 + 5;
    test::check(h.panel->clickable(bx, by) && !h.panel->clickable(card.x + 1, card.y + 1), "clickable follows regions");
    test::check(h.panel->press(bx, by) && h.content->activated == 1 && h.content->last_id == 7, "press activates the region under the pointer");

    test::check(h.panel->hover(bx, by) && h.content->body_hover == 7, "hovering a card region reports its id");
    test::check(!h.panel->hover(bx + 1, by + 1), "hovering the same region changes nothing");
    test::check(h.panel->hover(card.x + 1, card.y + 1) && h.content->body_hover == -1, "hovering empty card space clears the hover");
    double dx = card.x + 20 + 5;
    double dy = by + 30;
    h.panel->press(dx, dy);
    test::check(h.panel->dragging() && h.content->last_id == 8, "a drag region starts a drag");
    h.panel->move(dx + 10, dy);
    test::check(h.content->dragged == 1, "move reaches the content while dragging");
    h.panel->release();
    test::check(!h.panel->dragging() && h.content->dropped == 1, "release drops");
    test::check(!h.panel->move(0, 0) && !h.panel->release(), "no drag, no move");

    test::check(!h.panel->press(card.x - 200, card.y + 2000, astralia::input::Button::Middle), "middle clicks are ignored");
    h.content->dialog = 40.0f;
    h.panel->paint(h.canvas, 1000, 50);
    test::check(h.panel->contains(card.x + 5, card.y + card.h + 8 + 5), "contains covers the dialog card");
    h.panel->press(5, 5);
    test::check(h.content->dismissed == 1 && h.panel->is_open(), "an outside press dismisses the dialog first");
    h.panel->paint(h.canvas, 1000, 50);
    h.panel->press(5, 5);
    test::check(!h.panel->is_open() && h.closed == 1 && h.content->closed_count == 1, "an outside press closes the panel");

    h.panel->open();
    h.panel->paint(h.canvas, 1000, 50);
    astralia::input::KeyEvent escape;
    escape.kind = astralia::input::KeyKind::Escape;
    test::check(h.panel->key(escape) && !h.panel->is_open(), "escape closes");

    h.panel->open();
    card = h.panel->paint(h.canvas, 1000, 50);
    h.panel->press(card.x + card.w - 20 - 11, card.y + 20 + 16);
    test::check(!h.panel->is_open(), "the close button closes");
}

void check_panel_headerless() {
    Instant instant;
    Harness h;
    h.content->header = false;
    h.panel->open();
    astralia::ui::Box card = h.panel->paint(h.canvas, 1000, 50);
    test::check(card.h == 20 + 100 + 20, "a headerless card is the content between two paddings");
    double bx = card.x + 20 + 5;
    double by = card.y + 20 + 5;
    test::check(h.panel->press(bx, by) && h.content->last_id == 7, "headerless content starts at the top padding");
    astralia::input::KeyEvent escape;
    escape.kind = astralia::input::KeyKind::Escape;
    test::check(h.panel->key(escape) && !h.panel->is_open(), "escape closes a headerless card");
}

void check_panel_popup() {
    Instant instant;
    Harness h;
    h.panel->open();
    test::check(!h.panel->popup_wanted(), "no popup without content for it");
    astralia::ui::Box card = h.panel->paint(h.canvas, 1000, 50);
    h.content->popup = 90.0f;
    test::check(h.panel->popup_wanted() && h.panel->popup_width() == 220 && h.panel->popup_height() == 90, "the popup follows the content");
    astralia::ui::Box anchor = h.panel->popup_anchor();
    test::check(anchor.x == card.x + 10 && anchor.y == card.y + 20 && anchor.w == 40, "the anchor is moved to surface coordinates");
    h.panel->paint_popup(h.canvas);
    test::check(h.panel->popup_clickable(10, 10) && !h.panel->popup_clickable(150, 10), "popup regions are clickable");
    test::check(h.panel->press_popup(10, 10) && h.content->activated == 1 && h.content->last_id == 9, "a popup press activates its region");
    test::check(h.panel->hover_popup(10, 10) && h.content->hover_id == 9 && h.content->hover_a == 1, "hovering a popup region reports its id");
    test::check(!h.panel->hover_popup(11, 11), "hovering the same region changes nothing");
    test::check(h.panel->hover_popup(200, 80) && h.content->hover_id == -1, "leaving the regions clears the hover");
    int before = h.content->activated;
    test::check(h.panel->press_popup(200, 80) && h.content->activated == before, "a popup press outside any region does nothing");
    h.panel->close();
    test::check(!h.panel->popup_wanted(), "a closing panel has no popup");
}

void check_panel_set_lookup() {
    Harness h;
    astralia::PanelSet set(h.reactor);
    test::check(set.find(astralia::PanelId::count) == nullptr && set.find(set.active_id()) == nullptr, "an empty set has no active panel to find");
    set.add(astralia::PanelId::clock, std::make_unique<FakeContent>());
    test::check(set.find(astralia::PanelId::clock) != nullptr && set.find(astralia::PanelId::tray) == nullptr, "find returns only the panels that were added");
}

void check_dialog_heights() {
    test::check(astralia::panel_config::confirm_dialog_height == 114.0f, "the confirm dialog is 114 high");
    test::check(astralia::panel_config::field_dialog_height == 122.0f, "the password dialog is 122 high");
}

void check_panel_scroll() {
    Instant instant;
    Harness h;
    h.content->height = 900.0f;
    h.panel->open();
    astralia::ui::Box card = h.panel->paint(h.canvas, 1000, 50);
    test::check(card.h == 520, "tall content stops at the maximum height");
    test::check(h.panel->wheel(card.x + 10, card.y + 100, 80), "wheel scrolls a tall panel");
    test::check(h.panel->wheel(card.x + 10, card.y + 100, 5000), "wheel scrolls again");
    h.panel->paint(h.canvas, 1000, 50);
    test::check(!h.panel->wheel(card.x + 10, card.y + 100, 5000), "scroll clamps at the end");
    test::check(h.panel->wheel(card.x + 10, card.y + 100, -9000), "scroll returns to the top");
    h.panel->close();
    h.panel->open();
    test::check(!h.panel->wheel(0, 0, -1), "opening resets scroll");
}

void check_panel_animation() {
    Harness h;
    h.panel->open();
    h.panel->paint(h.canvas, 1000, 50);
    test::check(h.panel->animating(), "the card reveal animates");
    h.panel->tick(std::chrono::steady_clock::now() + std::chrono::seconds(1));
    test::check(!h.panel->animating(), "the reveal finishes");
    h.panel->close();
    test::check(h.panel->is_open() && h.panel->animating(), "closing keeps the card until the reveal ends");
    h.panel->tick(std::chrono::steady_clock::now() + std::chrono::seconds(2));
    test::check(!h.panel->is_open() && h.closed == 1, "the panel closes after the reveal");
}

void check_calendar() {
    using astralia::CalendarDay;
    std::array<CalendarDay, 42> oct = astralia::clock_panel_cells(2026, 9);
    test::check(oct[0].year == 2026 && oct[0].month == 8 && oct[0].day == 28 && !oct[0].in_month, "October 2026 grid starts on Monday 28 September");
    test::check(oct[3].month == 9 && oct[3].day == 1 && oct[3].in_month, "1 October 2026 is a Thursday");
    test::check(oct[41].month == 10 && oct[41].day == 8, "grid ends on 8 November");
    std::array<CalendarDay, 42> jun = astralia::clock_panel_cells(2026, 5);
    test::check(jun[0].day == 1 && jun[0].in_month, "a month starting on Monday has no leading days");
    test::check(astralia::clock_panel_same_day(oct[3], 2026, 9, 1), "same day matches");
    test::check(!astralia::clock_panel_same_day(oct[0], 2026, 9, 28), "same day needs the month");
    astralia::CalendarMonth back = astralia::clock_panel_month_shifted(2026, 0, -1);
    test::check(back.year == 2025 && back.month == 11, "January minus one is last December");
    astralia::CalendarMonth ahead = astralia::clock_panel_month_shifted(2026, 11, 13);
    test::check(ahead.year == 2028 && ahead.month == 0, "December plus 13 is January two years on");
    test::check(astralia::clock_panel_iso_week(2026, 0, 1) == 1, "1 January 2026 is week 1");
    test::check(astralia::clock_panel_iso_week(2027, 0, 1) == 53, "1 January 2027 is week 53 of 2026");
    test::check(astralia::clock_panel_iso_week(2024, 11, 30) == 1, "30 December 2024 is week 1 of 2025");
    test::check(astralia::clock_panel_iso_week(2026, 9, 4) == 40, "4 October 2026 is week 40");
}

void check_battery_text() {
    test::check(astralia::battery_time_text(0).empty(), "unknown time is blank");
    test::check(astralia::battery_time_text(45 * 60) == "45m", "minutes only");
    test::check(astralia::battery_time_text(2 * 3600 + 5 * 60) == "2h 5m", "hours and minutes");
}

void check_tray_menu() {
    using astralia::TrayMenuEntry;
    using astralia::TrayMenuRow;
    TrayMenuEntry open;
    open.id = 1;
    open.label = "Open";
    TrayMenuEntry gap;
    gap.id = 2;
    gap.separator = true;
    TrayMenuEntry hidden;
    hidden.id = 3;
    hidden.visible = false;
    TrayMenuEntry child;
    child.id = 5;
    child.label = "Child";
    TrayMenuEntry more;
    more.id = 4;
    more.label = "More";
    more.children = {child};
    std::vector<TrayMenuEntry> root{open, gap, hidden, more};

    test::check(astralia::tray_menu_level(&root, {}) == &root, "the empty path is the root");
    const std::vector<TrayMenuEntry> *deep = astralia::tray_menu_level(&root, {4});
    test::check(deep != nullptr && deep->size() == 1 && (*deep)[0].id == 5, "a path walks into a submenu");
    test::check(astralia::tray_menu_level(&root, {99}) == nullptr && astralia::tray_menu_level(nullptr, {}) == nullptr, "a stale path has no level");

    std::vector<TrayMenuRow> rows = astralia::tray_menu_rows(&root, false);
    test::check(rows.size() == 3 && rows[0].kind == TrayMenuRow::Kind::entry && rows[1].kind == TrayMenuRow::Kind::separator && rows[1].height < rows[0].height, "hidden entries are skipped and separators are thin");
    rows = astralia::tray_menu_rows(deep, true);
    test::check(rows.size() == 2 && rows[0].kind == TrayMenuRow::Kind::back, "a submenu starts with a back row");
    rows = astralia::tray_menu_rows(nullptr, false);
    test::check(rows.size() == 1 && rows[0].kind == TrayMenuRow::Kind::loading, "a missing menu is loading");
    test::check(astralia::tray_columns(400) == 8, "the tray grid fits eight columns");
}

void check_network_rows() {
    using astralia::NetworkInfo;
    using astralia::NetworkRow;
    astralia::NetworkMap map;
    auto add = [&](const char *ssid, int signal, bool connected, bool existing, bool in_range) {
        NetworkInfo info;
        info.ssid = ssid;
        info.signal = signal;
        info.connected = connected;
        info.existing = existing;
        info.in_range = in_range;
        info.security = "WPA2";
        map[ssid] = info;
    };
    add("Home", 80, true, true, true);
    add("Cafe", 60, false, false, true);
    add("Hotel", 90, false, false, true);
    add("Far", 20, false, true, false);
    add("Office", 40, false, true, true);

    std::vector<NetworkRow> rows = astralia::network_rows({map, false, false, true, true});
    std::vector<std::string> order;
    for (const NetworkRow &row : rows) {
        if (row.kind == NetworkRow::Kind::network) {
            order.push_back(row.info->ssid);
        }
    }
    test::check(order == std::vector<std::string>({"Home", "Office", "Hotel", "Cafe"}), "connected, then known in range, then available by signal; out of range saved ones are hidden");
    test::check(rows.front().kind == NetworkRow::Kind::section_connected && rows.back().kind == NetworkRow::Kind::spacer, "sections open the list and a spacer closes it");

    astralia::NetworkMap empty;
    rows = astralia::network_rows({empty, true, true, true, true});
    test::check(rows[0].kind == NetworkRow::Kind::error && rows[1].kind == NetworkRow::Kind::ethernet && rows[2].kind == NetworkRow::Kind::scanning, "banners come first, then the scanning state");
    rows = astralia::network_rows({empty, false, false, false, false});
    test::check(rows[0].kind == NetworkRow::Kind::no_adapter, "no adapter");
    rows = astralia::network_rows({empty, false, false, true, false});
    test::check(rows[0].kind == NetworkRow::Kind::disabled, "wifi disabled");
    test::check(std::string(astralia::network_signal_glyph(90)) != astralia::network_signal_glyph(10), "signal glyphs differ by band");
}

} // namespace

void check_panel() {
    check_panel_framework();
    check_panel_headerless();
    check_panel_popup();
    check_panel_set_lookup();
    check_dialog_heights();
    check_panel_scroll();
    check_panel_animation();
    check_calendar();
    check_battery_text();
    check_tray_menu();
    check_network_rows();
}
