# Industrial Edge Monitoring

A lightweight backend for an edge-device machine-health monitor. A microcontroller with environmental and vibration sensors posts readings to this FastAPI service, which keeps the latest state, logs every reading to CSV and serves the data to a dashboard.

## What it does

- **Ingests sensor readings** (`POST /update`): temperature, humidity, vibration, vibration intensity, a risk score, a fault label, an alert level and sensitivity.
- **Serves live state** (`GET /data`): the latest reading plus device uptime, for a dashboard to poll.
- **Logs to CSV**: every reading is appended to `log.csv` with a timestamp, so a session can be analysed afterwards.
- **Exports and resets logs** (`GET /download`, `GET /clear`).

`sample_data/industrial_data.csv` is an example capture from the device (timestamp, temperature, humidity, vibration, risk, alert).

## Run it

```bash
pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000
```

Then point the device at `http://<your-machine-ip>:8000/update`, or test with:

```bash
curl -X POST http://127.0.0.1:8000/update -H "Content-Type: application/json" \
  -d '{"temperature":25.5,"humidity":54.2,"vibration":0,"vibIntensity":0,"risk":0,"fault":"NORMAL","level":"NORMAL","sensitivity":5}'
curl http://127.0.0.1:8000/data
```

## API

| Method | Path | Purpose |
| --- | --- | --- |
| POST | `/update` | Receive a sensor reading and append it to the log |
| GET | `/data` | Latest reading and uptime |
| GET | `/download` | Download the CSV log |
| GET | `/clear` | Reset the CSV log |
| GET | `/` | Health check |

---
Sai Siddhanth K S · B.Tech CSE (Data Science), VIT Vellore
