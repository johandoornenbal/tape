#include <set>
#include <string>
#include <cmath>
#include "shim/audioeffectx.h"
#define private public
#include "../third_party/airwindows/ToTape8/ToTape8.h"
#undef private
#include "../third_party/airwindows/ToTape8/ToTape8.cpp"
#include "../third_party/airwindows/ToTape8/ToTape8Proc.cpp"
void seedReference(ToTape8& e) { e.fpdL=0x12345678u; e.fpdR=0x87654321u; }
