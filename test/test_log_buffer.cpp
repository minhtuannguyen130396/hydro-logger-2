// Placeholder unity test (enable if you configure unity component tests)
#include <cstring>
#include "services/logging/log_buffer.hpp"

extern "C" void app_main(void);

void test_log_buffer_basic() {
  LogBuffer b;
  b.appendf("hello %d\n", 1);
  // simple sanity
  (void)std::strstr(b.c_str(), "hello");
}
