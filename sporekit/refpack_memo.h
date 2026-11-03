#pragma once
#include <string>
namespace sk {
void refpack_memo_init(); // opt-in, supported code only; hook belongs to process-lifetime Kit
std::string refpack_memo_info();
}
