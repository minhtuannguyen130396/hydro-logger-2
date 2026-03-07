#pragma once

// Simple singleton helper.
// Thread-safe init is guaranteed by C++11 static local initialization.
template <typename T>
class Singleton {
public:
  static T& instance() {
    static T inst;
    return inst;
  }
protected:
  Singleton() = default;
  ~Singleton() = default;
  Singleton(const Singleton&) = delete;
  Singleton& operator=(const Singleton&) = delete;
};
