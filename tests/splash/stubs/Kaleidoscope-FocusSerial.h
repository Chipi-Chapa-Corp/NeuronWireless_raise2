#pragma once
#include <stdint.h>
#include <vector>
struct MockFocus {
 bool eol = false;
 uint8_t input = 0;
 uint32_t output = 255;
 std::vector<uint32_t> values;
 bool handleHelp(const char *,const char *) { return false; }
 bool isEOL() const { return eol; }
 void read(uint8_t &out) { out=input; }
 template<typename T> void send(T value) { output=value;values.push_back(value); }
};
extern MockFocus Focus;
