#pragma once
#include "Hand.h"
void mock_activate_mode();
namespace kaleidoscope {
enum class EventHandlerResult { OK, EVENT_CONSUMED };
class Plugin {};
namespace plugin {
class LEDModeInterface {};
class AccessTransientLEDMode {
 public: void registerLEDModeActivated(uint8_t id) { led_mode_id_ = id; }
 protected: uint8_t led_mode_id_ = 255;
};
class LEDMode { public: virtual void onActivate() {} virtual void update() {} };
}
struct Scanner {
 Communications_protocol::Devices left = Communications_protocol::KEYSCANNER_DEFY_LEFT;
 Communications_protocol::Devices right = Communications_protocol::KEYSCANNER_DEFY_RIGHT;
 Communications_protocol::Devices leftHandDevice() const { return left; }
 Communications_protocol::Devices rightHandDevice() const { return right; }
};
struct MockRuntime {
 enum { led_count = 176 };
 Scanner scanner;
 MockRuntime &device() { return *this; }
 const Scanner &keyScanner() const { return scanner; }
};
extern MockRuntime Runtime;
}
struct MockLEDControl {
 uint8_t mode = 12;
 bool enabled = true;
 unsigned refresh_count = 0;
 uint8_t get_mode_index() const { return mode; }
 bool isEnabled() const { return enabled; }
 void refreshAll() { ++refresh_count; mock_activate_mode(); }
 void set_mode(uint8_t m) { mode = m; }
 void activate(kaleidoscope::plugin::LEDModeInterface *) { mode = 12; }
};
extern MockLEDControl LEDControl;
