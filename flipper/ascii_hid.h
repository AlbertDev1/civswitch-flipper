#pragma once
#include <stdbool.h>
#include <stdint.h>
bool civ_ascii_to_hid(char ascii, uint8_t* modifier, uint8_t* key);
