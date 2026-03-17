# State machines (high-level)

## SensorManager
- BUILD_SELECTED -> WARMUP
- WARMUP -> (warmup ok) READY
- WARMUP -> (warmup fail) FAIL

## ConnectivityManager
- SELECT_PREFERRED (from NVS) -> TRY_MODULE
- TRY_MODULE:
  - powerOn -> checkInternet(timeout)
  - success -> ONLINE + update last_success_comm
  - fail -> powerOff -> SWITCH -> TRY_MODULE (other module)
- if both fail -> OFFLINE_FAIL

## Scheduler
- each 10-minute boundary triggers MeasureTask
- minute==0 additionally triggers SyncTask
- outside schedule -> safe energy mode
