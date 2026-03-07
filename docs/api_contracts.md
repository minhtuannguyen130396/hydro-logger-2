# Middleware contracts

## publishMeasurement(MeasurementMsg)
- Called by MeasureTask
- Enqueues data for SyncTask to send

## publishLog(LogMsg)
- Called by MeasureTask and SyncTask
- Enqueues logs for SyncTask to send
