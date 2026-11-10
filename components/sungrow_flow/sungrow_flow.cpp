#include "sungrow_flow.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace esphome::sungrow_flow {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

// Icons of Tessera's set.
static const uint32_t ICON_SOLAR = 0xF0A72, ICON_GRID = 0xF0D3E, ICON_HOME = 0xF02DC, ICON_WAVE = 0xF095B;
static const uint32_t ICON_BATTERY = 0xF0079, ICON_BATTERY_10 = 0xF007A, ICON_BATTERY_EMPTY = 0xF008E;

// Below this many watts nothing flows: no dots, and the battery rests.
static const float FLOWS_FROM = 10.0f;

// ---- Numbers as the screen's language writes them ----

static float or_zero(float v) { return std::isnan(v) ? 0.0f : v; }

static std::string number(const tessera::Plugin *plugin, float value, int decimals) {
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%.*f", decimals, static_cast<double>(value));
  const char *comma = plugin->text("decimal");
  if (comma[0] && comma[0] != '.')
    for (char *c = buffer; *c; ++c)
      if (*c == '.') *c = comma[0];
  return buffer;
}

// "32 W", "2,12 kW", "12,4 kW"; "-" for a sensor without a value.
static std::string power_text(const tessera::Plugin *plugin, float watts) {
  if (std::isnan(watts)) return "-";
  const float size = std::fabs(watts);
  if (size < 1000.0f) return number(plugin, std::round(size), 0) + " W";
  return number(plugin, size / 1000.0f, size < 10000.0f ? 2 : 1) + " kW";
}

static std::string energy_text(const tessera::Plugin *plugin, float kwh) {
  if (std::isnan(kwh)) return "-";
  return number(plugin, kwh, kwh < 100.0f ? 1 : 0) + " kWh";
}

static std::string percent_text(const tessera::Plugin *plugin, float percent) {
  if (std::isnan(percent)) return "-";
  return tessera::format(plugin->text("percent"), std::lround(percent));
}

// An entity the person chose for an option, as a number; NAN while it has none. A power comes in watts.
static float reading(JsonObjectConst entities, const char *option, bool power) {
  JsonObjectConst entity = entities[option].as<JsonObjectConst>();
  if (entity.isNull()) return NAN;
  const char *state = entity["state"] | "";
  char *end = nullptr;
  float value = strtof(state, &end);
  if (end == state) return NAN;
  if (power) {
    const char *unit = entity["attributes"]["unit_of_measurement"] | "W";
    if (!strcmp(unit, "kW")) value *= 1000.0f;
    else if (!strcmp(unit, "MW")) value *= 1000000.0f;
  }
  return value;
}

static void hide(lv_obj_t *part, bool hidden) {
  if (!part || lv_obj_has_flag(part, LV_OBJ_FLAG_HIDDEN) == hidden) return;
  if (hidden) lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(part, LV_OBJ_FLAG_HIDDEN);
}

// A label of one line, at its place, with its words left, in the middle or right.
static lv_obj_t *line_label(lv_obj_t *parent, Font font, theme::Role role, int x, int y, int width, lv_text_align_t align) {
  lv_obj_t *label = ui::label(parent, font, role);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_width(label, std::max(1, width));
  lv_obj_set_style_text_align(label, align, 0);
  return label;
}

// ---- The tile: solar, grid, house and battery around the inverter ----
class FlowTile : public tessera::Tile {
 public:
  explicit FlowTile(SungrowFlow *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    invert_grid_ = c.options["invert_grid"] | false;
    invert_battery_ = c.options["invert_battery"] | false;
    static const char *const STRINGS[4] = {"string1", "string2", "string3", "string4"};
    for (int i = 0; i < 4; ++i) has_string_[i] = strlen(c.options[STRINGS[i]] | "") > 0;

    // Below 2 x 2 cells the diagram has no room: the four values as a list instead.
    compact_ = c.columns < 2 || c.rows < 2;
    if (compact_) {
      create_compact(c.parent);
      return;
    }

    border_ = std::max(2, ui::px(4));
    dot_ = std::max(4, ui::px(7));
    diameter_ = std::max(ui::px(40), std::min(height_ * 30 / 100, width_ * 22 / 100));
    hub_ = diameter_ / 2;
    const int cx = width_ / 2, cy = height_ / 2, half = diameter_ / 2;

    // Everything of the diagram is in one stage, hidden until a sensor is chosen; the note says what to do till then.
    stage_ = ui::block(c.parent, theme::CARD);
    lv_obj_set_style_bg_opa(stage_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(stage_, 0, 0);
    lv_obj_set_pos(stage_, 0, 0);
    lv_obj_set_size(stage_, width_, height_);
    note_ = line_label(c.parent, Font::BODY_LARGE, theme::MUTED, 0, (height_ - ui::line_height(Font::BODY_LARGE)) / 2,
                       width_, LV_TEXT_ALIGN_CENTER);

    // The lines, from each circle's edge to the inverter's, and the dots that run along them.
    arm_[SOLAR] = {cx, diameter_, cx, cy - hub_ / 2};
    arm_[GRID] = {diameter_, cy, cx - hub_ / 2, cy};
    arm_[HOUSE] = {width_ - diameter_, cy, cx + hub_ / 2, cy};
    arm_[BATTERY] = {cx, height_ - diameter_, cx, cy + hub_ / 2};
    const int thick = std::max(2, ui::px(3));
    for (int a = 0; a < NODES; ++a) {
      const Arm &arm = arm_[a];
      line_[a] = ui::block(stage_, theme::LINE);
      lv_obj_set_style_radius(line_[a], 0, 0);
      const int left = std::min(arm.x0, arm.x1), top = std::min(arm.y0, arm.y1);
      const int across = std::abs(arm.x1 - arm.x0), down = std::abs(arm.y1 - arm.y0);
      if (arm.x0 == arm.x1) {
        lv_obj_set_pos(line_[a], left - thick / 2, top);
        lv_obj_set_size(line_[a], thick, std::max(1, down));
      } else {
        lv_obj_set_pos(line_[a], left, top - thick / 2);
        lv_obj_set_size(line_[a], std::max(1, across), thick);
      }
      hide(line_[a], across + down < dot_);
    }
    for (int a = 0; a < NODES; ++a)
      for (int i = 0; i < DOTS; ++i) {
        dots_[a][i] = ui::block(stage_, theme::LINE);
        lv_obj_set_style_radius(dots_[a][i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(dots_[a][i], dot_, dot_);
        hide(dots_[a][i], true);
      }

    // The inverter in the middle.
    hub_ring_ = ring(cx, cy, hub_, std::max(1, border_ / 2));
    const int wave = ui::line_height(Font::ICON_SMALL);
    hub_icon_ = line_label(stage_, Font::ICON_SMALL, theme::MUTED, cx - hub_ / 2, cy - wave / 2, hub_, LV_TEXT_ALIGN_CENTER);
    ui::set_text(hub_icon_, ui::icon(ICON_WAVE));
    hide(hub_icon_, wave > hub_ - 2 * border_);

    // The four circles: an icon, the value and a name, as many of them as the circle holds.
    const int room = diameter_ - 2 * border_ - ui::px(6);
    value_font_ = Font::BODY;
    for (Font f : {Font::TITLE, Font::BODY_LARGE}) {
      if (ui::text_width("8,88 kW", f) <= room) { value_font_ = f; break; }
    }
    const int icon_h = ui::line_height(Font::ICON_SMALL), value_h = ui::line_height(value_font_);
    const int name_h = ui::line_height(Font::BODY), inner = diameter_ - 2 * border_ - ui::px(4);
    const bool names = icon_h + value_h + name_h <= inner;
    const bool icons = icon_h + value_h + (names ? name_h : 0) <= inner;
    const int centre_x[NODES] = {cx, half, width_ - half, cx};
    const int centre_y[NODES] = {half, cy, cy, height_ - half};
    static const uint32_t ICONS[NODES] = {ICON_SOLAR, ICON_GRID, ICON_HOME, ICON_BATTERY};
    static const char *const NAMES[NODES] = {"solar", "grid", "house", "battery"};
    for (int n = 0; n < NODES; ++n) {
      Node &node = node_[n];
      node.ring = ring(centre_x[n], centre_y[n], diameter_, border_);
      int y = centre_y[n] - ((icons ? icon_h : 0) + value_h + (names ? name_h : 0)) / 2;
      const int x = centre_x[n] - room / 2;
      node.icon = line_label(stage_, Font::ICON_SMALL, theme::INK_SOFT, x, y, room, LV_TEXT_ALIGN_CENTER);
      ui::set_text(node.icon, ui::icon(ICONS[n]));
      hide(node.icon, !icons);
      if (icons) y += icon_h;
      node.value = line_label(stage_, value_font_, theme::INK, x, y, room, LV_TEXT_ALIGN_CENTER);
      y += value_h;
      node.name = line_label(stage_, Font::BODY, theme::MUTED, x, y, room, LV_TEXT_ALIGN_CENTER);
      ui::set_text(node.name, plugin_->text(NAMES[n]));
      hide(node.name, !names);
    }

    // Beside the solar circle: the power of each string that was chosen, as many as stand above the house's circle.
    const int body = ui::line_height(Font::BODY), side_x = cx + half + ui::px(10), side_w = width_ - side_x;
    int chosen = 0;
    for (bool has : has_string_) chosen += has ? 1 : 0;
    int y = std::max(0, (diameter_ - chosen * body) / 2);
    for (int i = 0; i < 4; ++i) {
      string_label_[i] = line_label(stage_, Font::BODY, theme::INK_SOFT, side_x, y, side_w, LV_TEXT_ALIGN_LEFT);
      const bool fits = has_string_[i] && side_w > ui::px(40) && y + body <= cy - half - ui::px(2);
      hide(string_label_[i], !fits);
      has_string_[i] = fits;
      if (fits) y += body;
    }

    // Beside the battery: what it does, and how warm it is.
    const int battery_y = height_ - half;
    battery_line_[0] = line_label(stage_, Font::BODY, theme::INK_SOFT, side_x, battery_y - body, side_w, LV_TEXT_ALIGN_LEFT);
    battery_line_[1] = line_label(stage_, Font::BODY, theme::MUTED, side_x, battery_y, side_w, LV_TEXT_ALIGN_LEFT);
    hide(battery_line_[0], side_w <= ui::px(40));
    hide(battery_line_[1], side_w <= ui::px(40));

    // Under the grid's circle: import or export. Under the house's: its largest source, and that source's share.
    const int under_y = cy + half + ui::px(4), under_w = std::max(1, cx - half - ui::px(8));
    under_fits_ = under_y + body <= battery_y - body - ui::px(2);
    grid_note_ = line_label(stage_, Font::BODY, theme::MUTED, ui::px(2), under_y, under_w, LV_TEXT_ALIGN_LEFT);
    share_ = line_label(stage_, Font::BODY, theme::INK_SOFT, width_ - under_w, under_y, under_w, LV_TEXT_ALIGN_RIGHT);
    share_dot_ = ui::block(stage_, theme::LINE);
    lv_obj_set_style_radius(share_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(share_dot_, dot_, dot_);
    under_y_ = under_y;
    hide(grid_note_, !under_fits_);
    hide(share_, !under_fits_);
    hide(share_dot_, true);
  }

  void on_state(JsonObjectConst data) override {
    JsonObjectConst entities = data["entities"].as<JsonObjectConst>();
    solar_ = reading(entities, "solar", true);
    grid_ = reading(entities, "grid", true);
    battery_ = reading(entities, "battery", true);
    house_ = reading(entities, "house", true);
    soc_ = reading(entities, "soc", false);
    temperature_ = reading(entities, "battery_temp", false);
    static const char *const STRINGS[4] = {"string1", "string2", "string3", "string4"};
    for (int i = 0; i < 4; ++i) string_[i] = reading(entities, STRINGS[i], true);
    // On the tile the grid is positive while the house imports, the battery while it discharges.
    if (invert_grid_) grid_ = -grid_;
    if (invert_battery_) battery_ = -battery_;
    any_ = !(std::isnan(solar_) && std::isnan(grid_) && std::isnan(battery_) && std::isnan(house_) && std::isnan(soc_));
    // A house without a sensor of its own uses what comes in and is not sent away.
    if (std::isnan(house_) && !(std::isnan(solar_) && std::isnan(grid_) && std::isnan(battery_)))
      house_ = std::max(0.0f, std::max(0.0f, or_zero(solar_)) + or_zero(grid_) + or_zero(battery_));
    else if (!std::isnan(house_))
      house_ = std::fabs(house_);
  }

  void on_tick(uint32_t) override {
    paint();
    if (!any_ || compact_) return;
    // The dots step along their line once a second, further the more power flows.
    const float flow[NODES] = {std::max(0.0f, or_zero(solar_)), or_zero(grid_), or_zero(house_), or_zero(battery_)};
    const bool to_hub[NODES] = {true, flow[GRID] >= 0, false, flow[BATTERY] >= 0};
    const theme::Role colour[NODES] = {theme::ENERGY_SOLAR, flow[GRID] >= 0 ? theme::ENERGY_GRID : theme::ENERGY_RETURN,
                                       theme::MARK_BLUE,
                                       flow[BATTERY] >= 0 ? theme::ENERGY_BATTERY_OUT : theme::ENERGY_BATTERY_IN};
    for (int a = 0; a < NODES; ++a) {
      const float size = std::fabs(flow[a]);
      const bool runs = size >= FLOWS_FROM && !lv_obj_has_flag(line_[a], LV_OBJ_FLAG_HIDDEN);
      if (runs) phase_[a] = std::fmod(phase_[a] + 0.10f + 0.20f * std::min(1.0f, size / 3000.0f), 1.0f);
      const Arm &arm = arm_[a];
      for (int i = 0; i < DOTS; ++i) {
        hide(dots_[a][i], !runs);
        if (!runs) continue;
        float at = std::fmod(phase_[a] + static_cast<float>(i) / DOTS, 1.0f);
        if (!to_hub[a]) at = 1.0f - at;
        lv_obj_set_pos(dots_[a][i], arm.x0 + static_cast<int>((arm.x1 - arm.x0) * at) - dot_ / 2,
                       arm.y0 + static_cast<int>((arm.y1 - arm.y0) * at) - dot_ / 2);
        ui::set_fill(dots_[a][i], Tone::NORMAL, colour[a]);
      }
    }
  }

  void on_theme() override {
    if (compact_) {
      ui::set_color(note_, theme::MUTED);
      for (lv_obj_t *value : list_value_) ui::set_color(value, theme::INK);
      paint();
      return;
    }
    for (int n = 0; n < NODES; ++n) {
      ui::set_color(node_[n].icon, theme::INK_SOFT);
      ui::set_color(node_[n].value, theme::INK);
      ui::set_color(node_[n].name, theme::MUTED);
      ui::set_fill(line_[n], Tone::NORMAL, theme::LINE);
      ring_role_[n] = -1;
    }
    for (lv_obj_t *label : string_label_) ui::set_color(label, theme::INK_SOFT);
    ui::set_color(battery_line_[0], theme::INK_SOFT);
    ui::set_color(battery_line_[1], theme::MUTED);
    ui::set_color(grid_note_, theme::MUTED);
    ui::set_color(share_, theme::INK_SOFT);
    ui::set_color(hub_icon_, theme::MUTED);
    ui::set_color(note_, theme::MUTED);
    lv_obj_set_style_border_color(hub_ring_, ui::color(theme::LINE), 0);
    paint();
  }

  void on_tap() override { tessera::open_card(plugin_->plugin_id(), "history", "", tile_); }

 private:
  enum { SOLAR, GRID, HOUSE, BATTERY, NODES };
  static constexpr int DOTS = 3;
  struct Arm { int x0, y0, x1, y1; };  // from the circle's edge to the inverter's
  struct Node { lv_obj_t *ring{}, *icon{}, *value{}, *name{}; };

  // An open circle: only its border shows.
  lv_obj_t *ring(int cx, int cy, int diameter, int border) {
    lv_obj_t *r = ui::block(stage_, theme::CARD);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r, border, 0);
    lv_obj_set_style_border_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(r, ui::color(theme::LINE), 0);
    lv_obj_set_size(r, diameter, diameter);
    lv_obj_set_pos(r, cx - diameter / 2, cy - diameter / 2);
    return r;
  }

  // A circle's border in its colour, set only when it is another one (or the look changed).
  void ring_colour(int n, theme::Role role, Tone tone = Tone::NORMAL) {
    const int key = static_cast<int>(role) * 8 + static_cast<int>(tone);
    if (ring_role_[n] == key) return;
    ring_role_[n] = key;
    lv_obj_set_style_border_color(node_[n].ring, ui::tone_color(tone, role), 0);
  }

  // The list of a tile smaller than 2 x 2: an icon in its energy colour and the value, in one or two columns, in the
  // largest font that holds all four.
  void create_compact(lv_obj_t *parent) {
    note_ = line_label(parent, Font::BODY_LARGE, theme::MUTED, 0, (height_ - ui::line_height(Font::BODY_LARGE)) / 2,
                       width_, LV_TEXT_ALIGN_CENTER);
    const int gap = ui::px(8), row_gap = ui::px(4), icon_w = ui::line_height(Font::ICON_SMALL), beside = ui::px(6);
    Font face = Font::BODY;
    int columns = 1;
    bool found = false;
    for (Font f : {Font::TITLE, Font::BODY_LARGE, Font::BODY}) {
      const int line = std::max(ui::line_height(f), icon_w), entry = icon_w + beside + ui::text_width("8,88 kW", f);
      for (int cols : {2, 1}) {
        const int lines = (NODES + cols - 1) / cols;
        if (cols * entry + (cols - 1) * gap <= width_ && lines * line + (lines - 1) * row_gap <= height_) {
          face = f;
          columns = cols;
          found = true;
          break;
        }
      }
      if (found) break;
    }
    const int line = std::max(ui::line_height(face), icon_w);
    const int lines = std::min((NODES + columns - 1) / columns, std::max(1, (height_ + row_gap) / (line + row_gap)));
    const int column_w = (width_ - (columns - 1) * gap) / columns;
    const int top = std::max(0, (height_ - lines * line - (lines - 1) * row_gap) / 2);
    static const uint32_t ICONS[NODES] = {ICON_SOLAR, ICON_GRID, ICON_HOME, ICON_BATTERY};
    for (int n = 0; n < NODES; ++n) {
      const int row = n / columns, x = (n % columns) * (column_w + gap), y = top + row * (line + row_gap);
      list_icon_[n] = line_label(parent, Font::ICON_SMALL, theme::INK_SOFT, x, y + (line - icon_w) / 2, icon_w,
                                 LV_TEXT_ALIGN_CENTER);
      ui::set_text(list_icon_[n], ui::icon(ICONS[n]));
      list_value_[n] = line_label(parent, face, theme::INK, x + icon_w + beside, y + (line - ui::line_height(face)) / 2,
                                  column_w - icon_w - beside, LV_TEXT_ALIGN_RIGHT);
      list_fits_[n] = row < lines;
    }
  }

  void paint_compact() {
    hide(note_, any_);
    for (int n = 0; n < NODES; ++n) {
      hide(list_icon_[n], !any_ || !list_fits_[n]);
      hide(list_value_[n], !any_ || !list_fits_[n]);
    }
    if (!any_) {
      ui::set_text(note_, plugin_->text("choose"));
      return;
    }
    const bool exports = !std::isnan(grid_) && grid_ <= -FLOWS_FROM;
    const bool has_soc = !std::isnan(soc_), low = has_soc && soc_ < 15.0f;
    ui::set_text(list_value_[SOLAR], power_text(plugin_, std::isnan(solar_) ? NAN : std::max(0.0f, solar_)));
    ui::set_text(list_value_[GRID], power_text(plugin_, grid_));
    ui::set_text(list_value_[HOUSE], power_text(plugin_, house_));
    ui::set_text(list_value_[BATTERY], has_soc ? percent_text(plugin_, soc_) : power_text(plugin_, battery_));
    ui::set_tone(list_icon_[SOLAR], Tone::NORMAL, theme::ENERGY_SOLAR);
    ui::set_tone(list_icon_[GRID], Tone::NORMAL, exports ? theme::ENERGY_RETURN : theme::ENERGY_GRID);
    ui::set_tone(list_icon_[HOUSE], Tone::NORMAL, theme::MARK_BLUE);
    ui::set_tone(list_icon_[BATTERY], low ? Tone::ALERT : Tone::NORMAL, theme::ENERGY_BATTERY_OUT);
    ui::set_text(list_icon_[BATTERY], battery_icon(has_soc));
  }

  // The battery's icon, filled as far as it is charged.
  std::string battery_icon(bool has_soc) const {
    uint32_t glyph = ICON_BATTERY;
    if (has_soc && soc_ < 95.0f) {
      const int tens = static_cast<int>(soc_ / 10.0f);
      glyph = tens < 1 ? ICON_BATTERY_EMPTY : ICON_BATTERY_10 + static_cast<uint32_t>(tens - 1);
    }
    std::string icon = ui::icon(glyph);
    return ui::covers(icon, Font::ICON_SMALL) ? icon : ui::icon(ICON_BATTERY);
  }

  void paint() {
    if (compact_) {
      paint_compact();
      return;
    }
    hide(stage_, !any_);
    hide(note_, any_);
    if (!any_) {
      ui::set_text(note_, plugin_->text("choose"));
      return;
    }
    ui::set_text(node_[SOLAR].value, power_text(plugin_, std::isnan(solar_) ? NAN : std::max(0.0f, solar_)));
    ring_colour(SOLAR, theme::ENERGY_SOLAR);
    for (int i = 0; i < 4; ++i)
      if (has_string_[i])
        ui::set_text(string_label_[i], tessera::format(plugin_->text("string"), i + 1) + "  " + power_text(plugin_, string_[i]));

    const bool exports = !std::isnan(grid_) && grid_ <= -FLOWS_FROM, imports = !std::isnan(grid_) && grid_ >= FLOWS_FROM;
    ui::set_text(node_[GRID].value, power_text(plugin_, grid_));
    ring_colour(GRID, exports ? theme::ENERGY_RETURN : theme::ENERGY_GRID);
    ui::set_text(grid_note_, imports || exports ? std::string(plugin_->text(imports ? "import" : "export")) : std::string());

    ui::set_text(node_[HOUSE].value, power_text(plugin_, house_));
    ring_colour(HOUSE, theme::MARK_BLUE);
    paint_share();

    // The battery's circle says its charge; its power stands beside it.
    const bool has_soc = !std::isnan(soc_);
    ui::set_text(node_[BATTERY].value, has_soc ? percent_text(plugin_, soc_) : power_text(plugin_, battery_));
    const bool low = has_soc && soc_ < 15.0f;
    ring_colour(BATTERY, theme::MARK_GREEN, low ? Tone::ALERT : Tone::NORMAL);
    ui::set_tone(node_[BATTERY].icon, low ? Tone::ALERT : Tone::NORMAL, theme::INK_SOFT);
    ui::set_text(node_[BATTERY].icon, battery_icon(has_soc));
    std::string doing;
    if (!std::isnan(battery_)) {
      const char *what = battery_ >= FLOWS_FROM ? "discharging" : battery_ <= -FLOWS_FROM ? "charging" : "idle";
      doing = (has_soc ? power_text(plugin_, battery_) + " " : std::string()) + plugin_->text(what);
    }
    ui::set_text(battery_line_[0], doing);
    const char *degrees = ui::covers("°", Font::BODY) ? " °C" : " C";
    ui::set_text(battery_line_[1], std::isnan(temperature_) ? std::string() : number(plugin_, temperature_, 1) + degrees);
  }

  // The house's largest source as a dot in its colour, and the share of the house's power it gives.
  void paint_share() {
    const bool shown = under_fits_ && !std::isnan(house_) && house_ >= FLOWS_FROM;
    if (!shown) {
      ui::set_text(share_, "");
      hide(share_dot_, true);
      return;
    }
    const float from_grid = std::min(house_, std::max(0.0f, or_zero(grid_)));
    const float from_battery = std::min(house_ - from_grid, std::max(0.0f, or_zero(battery_)));
    const float from_solar = std::max(0.0f, house_ - from_grid - from_battery);
    float most = from_solar;
    theme::Role role = theme::ENERGY_SOLAR;
    if (from_battery > most) { most = from_battery; role = theme::ENERGY_BATTERY_OUT; }
    if (from_grid > most) { most = from_grid; role = theme::ENERGY_GRID; }
    const std::string words = percent_text(plugin_, most / house_ * 100.0f);
    ui::set_text(share_, words);
    const int body = ui::line_height(Font::BODY);
    lv_obj_set_pos(share_dot_, width_ - ui::text_width(words, Font::BODY) - ui::px(6) - dot_, under_y_ + (body - dot_) / 2);
    ui::set_fill(share_dot_, Tone::NORMAL, role);
    hide(share_dot_, false);
  }

  SungrowFlow *plugin_;
  int width_ = 0, height_ = 0, tile_ = -1;
  int diameter_ = 0, hub_ = 0, border_ = 0, dot_ = 0, under_y_ = 0;
  bool invert_grid_ = false, invert_battery_ = false, any_ = false, under_fits_ = false, compact_ = false;
  lv_obj_t *list_icon_[4] = {}, *list_value_[4] = {};
  bool list_fits_[4] = {};
  bool has_string_[4] = {};
  float solar_ = NAN, grid_ = NAN, battery_ = NAN, house_ = NAN, soc_ = NAN, temperature_ = NAN;
  float string_[4] = {NAN, NAN, NAN, NAN};
  float phase_[NODES] = {};
  int ring_role_[NODES] = {-1, -1, -1, -1};
  Font value_font_ = Font::BODY;
  Arm arm_[NODES] = {};
  Node node_[NODES];
  lv_obj_t *stage_{}, *note_{}, *hub_ring_{}, *hub_icon_{}, *grid_note_{}, *share_{}, *share_dot_{};
  lv_obj_t *line_[NODES] = {}, *dots_[NODES][DOTS] = {}, *string_label_[4] = {}, *battery_line_[2] = {};
};

// ---- The card: production and consumption per day, for 7, 14 or 30 days ----

// What the house used on a day: what it imported, what of its own production it kept, and what the battery gave.
static float consumed(const History &h, int day) {
  const float imported = h.change[IMPORT][day];
  if (std::isnan(imported)) return NAN;
  const float pv = h.change[PV][day];
  const float kept = std::isnan(pv) ? 0.0f : std::max(0.0f, pv - or_zero(h.change[EXPORT][day]) - h.change[CHARGE][day]);
  return imported + kept + h.change[DISCHARGE][day];
}

static float percent_of(float part, float whole) {
  if (std::isnan(part) || std::isnan(whole) || whole < 0.05f) return NAN;
  return std::min(100.0f, std::max(0.0f, part / whole * 100.0f));
}

class HistoryCard : public tessera::Card {
 public:
  explicit HistoryCard(SungrowFlow *plugin) : plugin_(plugin) {}
  ~HistoryCard() override {
    if (plugin_->card_ == this) plugin_->card_ = nullptr;
  }

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    height_ = c.height;
    gap_ = ui::px(8);
    const int body = ui::line_height(Font::BODY), large = ui::line_height(Font::BODY_LARGE);

    // The three keys along the top.
    key_h_ = std::max(ui::mm(8), large + ui::px(10));
    key_w_ = (width_ - 2 * gap_) / 3;
    for (int i = 0; i < 3; ++i) {
      key_[i] = ui::block(c.parent, theme::RAISED);
      lv_obj_set_pos(key_[i], i * (key_w_ + gap_), 0);
      lv_obj_set_size(key_[i], key_w_, key_h_);
      key_label_[i] = line_label(c.parent, Font::BODY_LARGE, theme::INK, i * (key_w_ + gap_), (key_h_ - large) / 2, key_w_,
                                 LV_TEXT_ALIGN_CENTER);
      ui::set_text(key_label_[i], tessera::format(plugin_->text("days"), RANGES[i]));
    }

    // The day that is chosen: its date, and what was produced and used on it.
    int y = key_h_ + ui::px(10);
    date_ = line_label(c.parent, Font::BODY_LARGE, theme::INK, 0, y, width_, LV_TEXT_ALIGN_LEFT);
    y += large + ui::px(4);
    const int swatch = std::max(6, ui::px(10)), half = width_ / 2;
    static const theme::Role COLOURS[2] = {theme::ENERGY_SOLAR, theme::MARK_BLUE};
    for (int k = 0; k < 2; ++k) {
      swatch_[k] = ui::block(c.parent, COLOURS[k]);
      lv_obj_set_style_radius(swatch_[k], std::max(1, ui::px(2)), 0);
      lv_obj_set_size(swatch_[k], swatch, swatch);
      lv_obj_set_pos(swatch_[k], k * half, y + (body - swatch) / 2);
      value_[k] = line_label(c.parent, Font::BODY, theme::INK_SOFT, k * half + swatch + ui::px(6), y,
                             half - swatch - ui::px(10), LV_TEXT_ALIGN_LEFT);
    }
    y += body + ui::px(10);

    // From the bottom up: self-consumption, self-sufficiency, the letters of the days, and the bars in what is left.
    stat_[1] = line_label(c.parent, Font::BODY, theme::MUTED, 0, height_ - body, width_, LV_TEXT_ALIGN_LEFT);
    stat_[0] = line_label(c.parent, Font::BODY, theme::MUTED, 0, height_ - 2 * body - ui::px(2), width_, LV_TEXT_ALIGN_LEFT);
    letters_y_ = height_ - 3 * body - ui::px(10);
    chart_top_ = y;
    chart_h_ = std::max(ui::px(20), letters_y_ - ui::px(4) - chart_top_);
    for (int i = 0; i < History::DAYS; ++i)
      for (int k = 0; k < 2; ++k) {
        bar_[i][k] = ui::block(c.parent, COLOURS[k]);
        lv_obj_set_style_radius(bar_[i][k], std::max(1, ui::px(2)), 0);
        hide(bar_[i][k], true);
      }
    for (int i = 0; i < LETTERS; ++i) {
      letter_[i] = line_label(c.parent, Font::BODY, theme::MUTED, 0, letters_y_, 1, LV_TEXT_ALIGN_CENTER);
      hide(letter_[i], true);
    }
    note_ = line_label(c.parent, Font::BODY_LARGE, theme::MUTED, 0, chart_top_ + (chart_h_ - large) / 2, width_,
                       LV_TEXT_ALIGN_CENTER);

    plugin_->card_ = this;
    plugin_->want_history(tessera::epoch());
    paint_keys();
    show();
  }

  // The clock may come after the card: ask again until the days are there.
  void on_tick(uint32_t epoch) override { plugin_->want_history(epoch); }

  void on_theme() override {
    ui::set_color(date_, theme::INK);
    for (int k = 0; k < 2; ++k) {
      ui::set_color(value_[k], theme::INK_SOFT);
      ui::set_color(stat_[k], theme::MUTED);
    }
    ui::set_color(note_, theme::MUTED);
    paint_keys();
    show();
  }

  void on_tap() override {
    const tessera::Point at = tessera::tap_point();
    if (at.x < 0 || at.y < 0) return;
    if (at.y <= key_h_ + gap_ / 2) {
      range_ = RANGES[std::min(2, at.x / std::max(1, key_w_ + gap_))];
      chosen_ = -1;
      paint_keys();
      show();
      return;
    }
    const History &h = plugin_->history();
    if (!plugin_->ready() || h.count == 0) return;
    if (at.y < chart_top_ - gap_ || at.y > letters_y_ + ui::line_height(Font::BODY)) return;
    const int days = std::min(range_, h.count), first = h.count - days;
    const int slot = static_cast<int>(at.x * range_ / std::max(1, width_)) - (range_ - days);
    if (slot < 0 || slot >= days) return;
    chosen_ = first + slot;
    show();
  }

  // Draw the days the plugin has, or say what it waits for.
  void show() {
    const History &h = plugin_->history();
    const bool there = plugin_->ready() && h.count > 0;
    hide(note_, there);
    hide(date_, !there);
    for (int k = 0; k < 2; ++k) {
      hide(swatch_[k], !there);
      hide(value_[k], !there);
      hide(stat_[k], !there);
    }
    if (!there) {
      for (auto &pair : bar_)
        for (lv_obj_t *bar : pair) hide(bar, true);
      for (lv_obj_t *letter : letter_) hide(letter, true);
      ui::set_text(note_, plugin_->text(plugin_->failed() ? "failed" : plugin_->ready() ? "no_data" : "waiting"));
      return;
    }
    const int days = std::min(range_, h.count), first = h.count - days;
    if (chosen_ < first || chosen_ >= h.count) chosen_ = h.count - 1;

    // The bars: every day a pair, the newest day at the right, the highest bar as high as the chart.
    float top = 0.0f, sum[4] = {};  // production, import, export, consumption of the period
    for (int d = first; d < h.count; ++d) {
      const float pv = h.change[PV][d], used = consumed(h, d);
      top = std::max(top, std::max(or_zero(pv), or_zero(used)));
      sum[0] += or_zero(pv);
      sum[1] += or_zero(h.change[IMPORT][d]);
      sum[2] += or_zero(h.change[EXPORT][d]);
      sum[3] += or_zero(used);
    }
    const float slot = static_cast<float>(width_) / range_;
    const int between = std::max(1, static_cast<int>(slot * 0.06f));
    const int bar_w = std::max(2, static_cast<int>(slot * 0.36f));
    for (int i = 0; i < History::DAYS; ++i) {
      const int d = first + i;
      const float value[2] = {i < days ? h.change[PV][d] : NAN, i < days ? consumed(h, d) : NAN};
      const int x = static_cast<int>((range_ - days + i) * slot + (slot - 2 * bar_w - between) / 2);
      for (int k = 0; k < 2; ++k) {
        const bool drawn = i < days && !std::isnan(value[k]) && value[k] > 0.0f && top > 0.0f;
        hide(bar_[i][k], !drawn);
        if (!drawn) continue;
        const int high = std::max(2, static_cast<int>(value[k] / top * chart_h_));
        lv_obj_set_size(bar_[i][k], bar_w, high);
        lv_obj_set_pos(bar_[i][k], x + k * (bar_w + between), chart_top_ + chart_h_ - high);
        lv_obj_set_style_bg_opa(bar_[i][k], d == chosen_ ? LV_OPA_COVER : LV_OPA_60, 0);
      }
    }

    // The first letter of each day's name under its bars, while the days are few enough to read them.
    const bool lettered = range_ <= LETTERS && slot >= ui::text_width("M", Font::BODY);
    for (int i = 0; i < LETTERS; ++i) {
      const bool drawn = lettered && i < days;
      hide(letter_[i], !drawn);
      if (!drawn) continue;
      const int d = first + i;
      const std::string day = tessera::date_text(h.start[d] + 43200u);
      size_t bytes = 1;  // the first character, which may take several bytes
      while (bytes < day.size() && (static_cast<unsigned char>(day[bytes]) & 0xC0) == 0x80) ++bytes;
      lv_obj_set_pos(letter_[i], static_cast<int>((range_ - days + i) * slot), letters_y_);
      lv_obj_set_width(letter_[i], std::max(1, static_cast<int>(slot)));
      ui::set_text(letter_[i], day.substr(0, std::min(bytes, day.size())));
      ui::set_color(letter_[i], d == chosen_ ? theme::INK : theme::MUTED);
    }

    // The chosen day in words, and under the bars how much of the house ran on its own power.
    const float pv = h.change[PV][chosen_], used = consumed(h, chosen_);
    const float imported = h.change[IMPORT][chosen_], exported = h.change[EXPORT][chosen_];
    ui::set_text(date_, tessera::date_text(h.start[chosen_] + 43200u));
    ui::set_text(value_[0], std::string(plugin_->text("production")) + " " + energy_text(plugin_, pv));
    ui::set_text(value_[1], std::string(plugin_->text("consumption")) + " " + energy_text(plugin_, used));
    const std::string period = tessera::format(plugin_->text("days"), range_);
    const char *dot = ui::covers("·", Font::BODY) ? " · " : " - ";
    ui::set_text(stat_[0], std::string(plugin_->text("autarky")) + " " + percent_text(plugin_, percent_of(used - imported, used)) +
                               dot + period + " " + percent_text(plugin_, percent_of(sum[3] - sum[1], sum[3])));
    ui::set_text(stat_[1], std::string(plugin_->text("self_use")) + " " + percent_text(plugin_, percent_of(pv - or_zero(exported), pv)) +
                               dot + period + " " + percent_text(plugin_, percent_of(sum[0] - sum[2], sum[0])));
  }

 private:
  static constexpr int LETTERS = 14;
  static constexpr int RANGES[3] = {7, 14, 30};

  void paint_keys() {
    for (int i = 0; i < 3; ++i) {
      const bool on = RANGES[i] == range_;
      ui::set_fill(key_[i], Tone::NORMAL, on ? theme::ACCENT : theme::RAISED);
      ui::set_color(key_label_[i], on ? theme::ON_ACCENT : theme::INK);
    }
  }

  SungrowFlow *plugin_;
  int width_ = 0, height_ = 0, gap_ = 0, key_w_ = 0, key_h_ = 0, chart_top_ = 0, chart_h_ = 0, letters_y_ = 0;
  int range_ = 14, chosen_ = -1;
  lv_obj_t *key_[3] = {}, *key_label_[3] = {}, *date_{}, *swatch_[2] = {}, *value_[2] = {}, *stat_[2] = {}, *note_{};
  lv_obj_t *bar_[History::DAYS][2] = {}, *letter_[LETTERS] = {};
};

// ---- The plugin ----

void SungrowFlow::setup() {
  add_tile("flow", [this]() { return new FlowTile(this); });
  add_card("history", [this]() { return new HistoryCard(this); }, true);
}

void SungrowFlow::want_history(uint32_t epoch) {
  if (!epoch || step_ >= 0) return;
  if (ready_ && epoch - fetched_ < 900u) return;
  if (failed_ && now_ms_ - asked_ms_ < 30000u) return;  // a question that failed is asked again half a minute later
  // The days of a month and a day back, so thirty whole days are among them wherever the screen's midnight falls.
  const time_t since = static_cast<time_t>(epoch) - 31 * 86400;
  struct tm utc;
  gmtime_r(&since, &utc);
  strftime(since_, sizeof(since_), "%Y-%m-%dT%H:%M:%SZ", &utc);
  incoming_ = History();
  for (int k = 0; k < COUNTERS; ++k)
    for (float &value : incoming_.change[k]) value = k >= CHARGE ? 0.0f : NAN;
  failed_ = false;
  step_ = 0;
  ask_next();
}

// Ask for the next counter that was chosen; with none left, the days are whole.
void SungrowFlow::ask_next() {
  while (step_ < COUNTERS && !counter_[step_][0]) ++step_;
  if (step_ >= COUNTERS) {
    done(incoming_.count > 0);
    return;
  }
  JsonDocument request;
  request["ask"] = "recorder/statistics_during_period";
  JsonObject data = request["data"].to<JsonObject>();
  data["start_time"] = static_cast<const char *>(since_);
  data["statistic_ids"][0] = counter_[step_];
  data["period"] = "day";
  data["types"][0] = "change";
  data["units"]["energy"] = "kWh";
  asked_ms_ = now_ms_;
  asked_ = tessera::send(this, request.as<JsonObjectConst>());
  if (!asked_) done(false);
}

void SungrowFlow::done(bool ok) {
  step_ = -1;
  failed_ = !ok;
  if (ok) {
    history_ = incoming_;
    ready_ = true;
    fetched_ = tessera::epoch();
  }
  if (card_) card_->show();
}

// The answer for one counter: {"re", "ok", "result": {"change": [kWh per day], "start": [each day's start in ms]}}.
void SungrowFlow::on_message(JsonObjectConst message) {
  if (step_ < 0 || (message["re"] | 0u) != asked_) return;
  if (message["ok"] | false) {
    JsonArrayConst change = message["result"]["change"].as<JsonArrayConst>();
    JsonArrayConst start = message["result"]["start"].as<JsonArrayConst>();
    const int rows = static_cast<int>(std::min(change.size(), start.size()));
    // The first counter with days sets which days there are: its last thirty.
    if (incoming_.count == 0 && rows > 0) {
      incoming_.count = std::min(rows, static_cast<int>(History::DAYS));
      for (int i = 0; i < incoming_.count; ++i)
        incoming_.start[i] = static_cast<uint32_t>(start[rows - incoming_.count + i].as<double>() / 1000.0);
    }
    for (int row = 0; row < rows; ++row) {
      if (change[row].isNull()) continue;
      const int64_t at = static_cast<int64_t>(start[row].as<double>() / 1000.0);
      for (int day = 0; day < incoming_.count; ++day) {
        const int64_t apart = at - static_cast<int64_t>(incoming_.start[day]);
        if (apart > -3600 && apart < 3600) {
          incoming_.change[step_][day] = std::max(0.0f, change[row].as<float>());
          break;
        }
      }
    }
  } else if (step_ < CHARGE) {
    // Without production, import or export there is nothing to show; a battery's counters may be missing.
    done(false);
    return;
  }
  ++step_;
  ask_next();
}

// Give up on an answer that did not come within twenty seconds.
void SungrowFlow::on_interval(uint32_t now_ms) {
  now_ms_ = now_ms;
  if (step_ >= 0 && now_ms - asked_ms_ > 20000u) done(false);
}

}  // namespace esphome::sungrow_flow
