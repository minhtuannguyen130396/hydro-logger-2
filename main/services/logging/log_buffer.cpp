#include "services/logging/log_buffer.hpp"

LogBuffer::LogBuffer() { clear(); }

void LogBuffer::clear() {
  len_ = 0;
  buf_[0] = '\0';
}

void LogBuffer::appendf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vappendf(fmt, ap);
  va_end(ap);
}

void LogBuffer::vappendf(const char* fmt, va_list ap) {
  if (!fmt) return;
  if (len_ >= cfg::kSessionLogSize - 1) return;

  int remain = (int)cfg::kSessionLogSize - 1 - (int)len_;
  int n = vsnprintf(buf_ + len_, remain, fmt, ap);
  if (n < 0) return;
  if (n > remain) n = remain;
  len_ = (uint16_t)(len_ + n);
  buf_[len_] = '\0';
}
