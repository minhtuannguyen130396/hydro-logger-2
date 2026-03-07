# State machines (high-level)

## SensorManager
- TRY_PREFERRED -> (warmup ok) READY
- TRY_PREFERRED -> (warmup fail) TRY_OTHER
- TRY_OTHER -> (warmup ok) READY + update last_working_sensor
- TRY_OTHER -> (warmup fail) BOTH_FAIL

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
