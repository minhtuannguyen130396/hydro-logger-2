#pragma once

class PowerManager {
public:
  void enterSafeMode();
  void exitSafeMode();

private:
  bool in_safe_mode_{false};
};
