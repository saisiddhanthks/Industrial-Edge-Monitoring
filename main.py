from fastapi import FastAPI
from fastapi.responses import FileResponse
from pydantic import BaseModel
import time
import csv
import os

app = FastAPI(title="Industrial Edge Monitoring API")

data_store = {
    "temperature": 0,
    "humidity": 0,
    "vibration": 0,
    "vibIntensity": 0,
    "risk": 0,
    "fault": "NORMAL",
    "level": "NORMAL",
    "sensitivity": 0,
    "uptime": 0
}

start_time = time.time()
LOG_FILE = "log.csv"

class SensorData(BaseModel):
    temperature: float
    humidity: float
    vibration: int
    vibIntensity: int
    risk: int
    fault: str
    level: str
    sensitivity: int

@app.post("/update")
def update_data(data: SensorData):
    global data_store

    data_store.update(data.dict())
    data_store["uptime"] = int(time.time() - start_time)
    file_exists = os.path.isfile(LOG_FILE)

    with open(LOG_FILE, "a", newline="") as file:
        writer = csv.writer(file)

        if not file_exists:
            writer.writerow(["Time","Temp","Humidity","Vibration","Risk","Level"])

        writer.writerow([
            data_store["uptime"],
            data.temperature,
            data.humidity,
            data.vibIntensity,
            data.risk,
            data.level
        ])

    return {"message": "Data updated"}

@app.get("/data")
def get_data():
    return data_store

@app.get("/download")
def download_logs():
    if not os.path.exists(LOG_FILE):
        return {"error": "No log file"}
    return FileResponse(LOG_FILE, media_type='text/csv', filename="log.csv")

@app.get("/clear")
def clear_logs():
    with open(LOG_FILE, "w") as file:
        file.write("Time,Temp,Humidity,Vibration,Risk,Level\n")
    return {"message": "Logs cleared"}


@app.get("/")
def root():
    return {"message": "Industrial Monitoring Backend Running"}