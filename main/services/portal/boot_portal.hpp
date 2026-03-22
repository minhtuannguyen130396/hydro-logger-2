#pragma once
#include <cstdint>

/// Diagnostic results passed from connectivity test to the portal.
struct PortalDiagResult {
  bool dcom_ok      = false;
  bool sim_ok       = false;
  bool dcom_power   = false;  // powerOn succeeded
  bool sim_power    = false;  // powerOn succeeded
};

/// Boot-time Wi-Fi AP + HTTP configuration portal.
///
/// Usage from diagnostic_task:
///   BootPortal::start(diag);       // starts AP "TRAM_DO_NUOC" + HTTP server
///   while (BootPortal::isActive()) { ... }
///   BootPortal::stop();
class BootPortal {
public:
  /// Start the Wi-Fi AP and HTTP server with the given diagnostic results.
  static bool start(const PortalDiagResult& diag);

  /// Stop the HTTP server and Wi-Fi AP.
  static void stop();

  /// Milliseconds since the last HTTP request was received.
  /// Returns UINT32_MAX if no request has ever been received.
  static uint32_t msSinceLastRequest();

  /// True if portal was started and has not been stopped.
  static bool isActive();

  /// Touch the activity timer (called from HTTP handlers).
  static void touch();
};
