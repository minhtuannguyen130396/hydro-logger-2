# Architecture

Layers:
- App: tasks and orchestration (scheduler, measure, sync, notify, ota)
- Middleware: MessageBus + publish APIs (queues)
- Services: connectivity manager, HTTP client, JSON packer, logging service, power manager, OTA service
- Modules: sensors, rtc, IO controller (singletons)
- Board: low-level drivers (GPIO/UART/I2C/ADC)

Concurrency:
- SchedulerTask triggers measure/sync/ota based on RTC time.
- NotifyTask runs continuously, reads AppState (event bits).
- Message queues connect MeasureTask -> SyncTask for measurement & logs.
