#include <set>
#include <string>
#include <cmath>
#include "shim/audioeffectx.h"
#define private public
#include "../third_party/airwindows/ToTape7/ToTape7.h"
#undef private
#include "../third_party/airwindows/ToTape7/ToTape7.cpp"
#include "../third_party/airwindows/ToTape7/ToTape7Proc.cpp"
void seedReference(ToTape7& e) { e.fpdL=0x12345678u; e.fpdR=0x87654321u; }
