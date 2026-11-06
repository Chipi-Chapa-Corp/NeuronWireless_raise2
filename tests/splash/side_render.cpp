// Exercise the actual released side layer renderer, including its one-shot delay.
#include <cassert>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdint>
struct RGBW { uint8_t r,g,b,w; };
namespace KsConfig { enum { MAX_BL_LEDS=36, MAX_UG_LEDS=54 }; }
namespace LEDManagement {
struct Layer { uint8_t keyMap_leds[36]{}, underGlow_leds[54]{}; };
std::vector<Layer> layers(10);
RGBW palette[16]{}, output[36]{}, underglow[54]{};
bool updated=false;
enum class LedBrightnessControlEffect { FADE_EFFECT };
float get_max_ledDriver_brightness() { return 1; }
float get_max_underglow_brightness() { return 1; }
float get_ledDriver_brightness() { return 1; }
float get_underglow_brightness() { return 1; }
void set_ledDriver_brightness(float) {}
void set_underglow_brightness(float) {}
void onMount(LedBrightnessControlEffect) {}
void onDismount(LedBrightnessControlEffect) {}
void set_led_at(RGBW value,uint8_t index) { output[index]=value; }
void set_ug_at(RGBW value,uint8_t index) { underglow[index]=value; }
void set_updated(bool value) { updated=value; }
}
#include "LedModeSerializable-Layer.h"
int main() {
  uint8_t wire[28]{};
  LedModeSerializable_Layer sender(CRC32_STR("LedModeSerializable_Layer"));
  sender.base_settings.step=0;sender.base_settings.brightness=255;sender.base_settings.delay_ms=10;
  sender.layer=0;sender.fade_is_on=0;sender.serialize(wire);
  LedModeSerializable_Layer receiver(0);
  for(unsigned frame=0;frame<10000;++frame) {
    for(unsigned i=0;i<16;++i) LEDManagement::palette[i]={uint8_t(i+frame),uint8_t(i+1),uint8_t(i+2),0};
    for(unsigned i=0;i<36;++i) LEDManagement::layers[0].keyMap_leds[i]=(i+frame)%16;
    receiver.deSerialize(wire);
    assert(receiver.base_settings.delay_ms==10 && receiver.layer==0 && receiver.fade_is_on==0);
    receiver.update();
    assert(receiver.base_settings.delay_ms==0 && LEDManagement::updated);
    for(unsigned i=0;i<36;++i)
      assert(memcmp(&LEDManagement::output[i],&LEDManagement::palette[(i+frame)%16],4)==0);
  }
}
