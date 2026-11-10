#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::sungrow_flow {

class HistoryCard;

// The energy counters the card reads its days from, in the order of __init__.py's COUNTERS.
enum Counter : uint8_t { PV, IMPORT, EXPORT, CHARGE, DISCHARGE, COUNTERS };

// The days the card shows: the start of each (seconds since 1970) and the change of every counter on it, in kWh.
// The oldest day first. A counter without a value on a day is NAN (CHARGE and DISCHARGE: 0, a house without a battery).
struct History {
  static constexpr int DAYS = 30;
  int count = 0;
  uint32_t start[DAYS] = {};
  float change[COUNTERS][DAYS] = {};
};

// The plugin: a tile ("flow") with the power of the house now, and a card ("history") with its days. The plugin asks
// Home Assistant for the days, one counter per question, and keeps the answers a quarter of an hour.
class SungrowFlow : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override;
  void on_interval(uint32_t now_ms) override;

  void set_counter(int index, const char *entity) {
    if (index >= 0 && index < COUNTERS) counter_[index] = entity;
  }

  // The card asks for the days: nothing happens while a question is out or the answers are fresh.
  void want_history(uint32_t epoch);
  bool ready() const { return ready_; }
  bool asking() const { return step_ >= 0; }
  bool failed() const { return failed_; }
  const History &history() const { return history_; }

  HistoryCard *card_ = nullptr;  // the card that is open, to tell it its days are there

 protected:
  void ask_next();
  void done(bool ok);

  const char *counter_[COUNTERS] = {"", "", "", "", ""};
  History history_, incoming_;
  int step_ = -1;              // the counter whose answer is awaited, -1 for none
  uint32_t asked_ = 0;         // the number of that question
  uint32_t asked_ms_ = 0;      // when it was sent, to give up on an answer that never comes
  uint32_t now_ms_ = 0;
  uint32_t fetched_ = 0;       // when the last answers were whole (the screen's clock)
  char since_[24] = "";        // the start of the period asked, ISO 8601 in UTC
  bool ready_ = false, failed_ = false;
};

}  // namespace esphome::sungrow_flow
