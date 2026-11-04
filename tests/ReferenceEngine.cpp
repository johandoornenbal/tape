#include <set>
#include <string>
#include <cmath>
#include "shim/audioeffectx.h"
#define private public
#include "../third_party/airwindows/ToTape9.h"
#undef private
#include "../third_party/airwindows/ToTape9.cpp"
#include "../third_party/airwindows/ToTape9Proc.cpp"
void seedReference(ToTape9& e) { e.fpdL=0x12345678u; e.fpdR=0x87654321u; }
