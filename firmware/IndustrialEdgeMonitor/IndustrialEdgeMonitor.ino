/*
 * Industrial Edge Monitor v5.0
 * ESP32 edge node for machine condition monitoring.
 *
 * Sensors : DHT11 (temperature/humidity), digital vibration sensor, potentiometer (sensitivity)
 * Actuators: buzzer (graded alarms), SG90 servo (cooling actuation)
 * Storage : micro-SD card (CSV log every 5 s)
 * Network : Wi-Fi soft access point serving a live dashboard and a JSON API
 *
 * Author : Sai Siddhanth (VIT Vellore) - Embedded Systems project
 */
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <SPI.h>
#include <SD.h>
#include <ESP32Servo.h>

#define DHTPIN 4
#define DHTTYPE DHT11
#define VIBRATION_PIN 17
#define BUZZER 16
#define POT_PIN 34
#define SD_CS 5
#define SERVO_PIN 33

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
Servo coolingServo;

// Soft-AP credentials - change the password before deploying on a real shop floor
const char* ssid = "IndustrialMonitor";
const char* password = "12345678";

float temperature = 0;
float humidity = 0;
bool vibrationDetected = false;

int riskScore = 0;
String faultType = "NORMAL";
String alarmLevel = "NORMAL";

unsigned long lastSampleTime = 0;
unsigned long lastBeepTime = 0;
unsigned long lastLogTime = 0;
unsigned long bootTime = 0;
unsigned long lastServoMove = 0;

int vibIntegrator = 0;
int dynamicThreshold = 4;
int potPercent = 0;

bool sdReady = false;

// ===== SERVO VARIABLES =====
int servoAngle = 0;
bool servoDirection = true;

// ================= SD LOGGING =================
void logToSD() {
  if (!sdReady) return;

  File file = SD.open("/log.csv", FILE_APPEND);
  if (!file) return;

  file.print((millis()-bootTime)/1000);
  file.print(",");
  file.print(temperature);
  file.print(",");
  file.print(humidity);
  file.print(",");
  file.print(vibIntegrator);
  file.print(",");
  file.print(riskScore);
  file.print(",");
  file.println(alarmLevel);

  file.close();
}

void handleDownload() {
  File file = SD.open("/log.csv");
  if (!file) {
    server.send(404, "text/plain", "No log file");
    return;
  }
  server.streamFile(file, "text/csv");
  file.close();
}

void handleClear() {
  SD.remove("/log.csv");
  File file = SD.open("/log.csv", FILE_WRITE);
  file.println("Time,Temp,Humidity,VibrationIntensity,Risk,Level");
  file.close();
  server.send(200, "text/plain", "Logs Cleared");
}

// ================= DATA API =================
void handleData() {
  String json = "{";
  json += "\"temperature\":" + String(temperature) + ",";
  json += "\"humidity\":" + String(humidity) + ",";
  json += "\"vibration\":" + String(vibrationDetected ? 1 : 0) + ",";
  json += "\"vibIntensity\":" + String(vibIntegrator) + ",";
  json += "\"risk\":" + String(riskScore) + ",";
  json += "\"fault\":\"" + faultType + "\",";
  json += "\"level\":\"" + alarmLevel + "\",";
  json += "\"sensitivity\":" + String(potPercent) + ",";
  json += "\"uptime\":" + String((millis()-bootTime)/1000);
  json += "}";

  server.send(200, "application/json", json);
}

// ================= UI =================
void handleRoot(){
 String page = R"rawliteral(
 <!DOCTYPE html>
 <html>
 <head>
 <meta charset="utf-8">
 <meta name="viewport" content="width=device-width, initial-scale=1">
 <title>Industrial Edge Monitor v5.0</title>
 <style>
 body{margin:0;background:#000;font-family:Segoe UI;color:#fff;}
 .header{padding:20px;text-align:center;font-size:22px;background:#050505;border-bottom:1px solid #222;}
 .statusBanner{text-align:center;padding:12px;font-weight:bold;}
 .normalBanner{background:#002200;color:#00ff88;}
 .warningBanner{background:#332200;color:#ffaa00;}
 .criticalBanner{background:#330000;color:#ff4444;}
 .topbar{display:flex;justify-content:space-around;padding:10px;background:#111;flex-wrap:wrap;}
 .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px;padding:20px;}
 .card{background:#111;border:1px solid #222;border-radius:8px;padding:20px;}
 .value{font-size:22px;font-weight:bold;}
 canvas{background:#0a0a0a;border-radius:6px;margin-top:10px;}
 .bar{height:10px;background:#222;border-radius:5px;margin-top:5px;}
 .fill{height:100%;background:#00ff88;border-radius:5px;width:0%;}
 button{margin-top:10px;padding:8px;background:#111;border:1px solid #444;color:#fff;}
 </style>
 </head>
 <body>
 <div class="header">INDUSTRIAL EDGE MONITOR v5.0</div>
 <div id="statusBanner" class="statusBanner normalBanner">SYSTEM NORMAL</div>

 <div class="topbar">
 <div>Uptime: <span id="uptime">0</span>s</div>
 <div>Sensitivity: <span id="sens">0</span>%</div>
 <div>Fault: <span id="fault">NORMAL</span></div>
 </div>

 <div class="grid">

 <div class="card">
 Temperature
 <div class="value" id="temp">--</div>
 <canvas id="tempChart" width="300" height="120"></canvas>
 </div>

 <div class="card">
 Risk
 <div class="value" id="risk">0%</div>
 <canvas id="riskChart" width="300" height="120"></canvas>
 </div>

 <div class="card">
 Vibration Intensity
 <div class="value" id="vib">--</div>
 <div class="bar"><div class="fill" id="vibBar"></div></div>
 </div>

 <div class="card">
 <button onclick="window.location='/download'">Download CSV</button>
 <button onclick="fetch('/clear')">Clear Logs</button>
 </div>

 </div>

 <script>
 let tempHistory=[];
 let riskHistory=[];

 let tempCanvas=document.getElementById("tempChart");
 let riskCanvas=document.getElementById("riskChart");

 let tempCtx=tempCanvas.getContext("2d");
 let riskCtx=riskCanvas.getContext("2d");

 function drawGraph(ctx,canvas,data,color){
 ctx.clearRect(0,0,canvas.width,canvas.height);
 ctx.beginPath();
 ctx.strokeStyle=color;
 ctx.lineWidth=2;

 for(let i=0;i<data.length;i++){
 let x=i*(canvas.width/50);
 let y=canvas.height-(data[i]*3);
 if(i==0) ctx.moveTo(x,y);
 else ctx.lineTo(x,y);
 }

 ctx.stroke();
 }

 setInterval(()=>{
 fetch('/data')
 .then(res=>res.json())
 .then(data=>{

 document.getElementById("temp").innerText=data.temperature+" °C";
 document.getElementById("risk").innerText=data.risk+"%";
 document.getElementById("uptime").innerText=data.uptime;
 document.getElementById("fault").innerText=data.fault;
 document.getElementById("sens").innerText=data.sensitivity;

 let vibPercent=(data.vibIntensity/20)*100;
 document.getElementById("vib").innerText=data.vibIntensity;
 document.getElementById("vibBar").style.width=vibPercent+"%";

 let banner=document.getElementById("statusBanner");

 if(data.level=="CRITICAL"){
 banner.className="statusBanner criticalBanner";
 banner.innerText="SYSTEM CRITICAL";
 }
 else if(data.level=="WARNING"){
 banner.className="statusBanner warningBanner";
 banner.innerText="SYSTEM WARNING";
 }
 else{
 banner.className="statusBanner normalBanner";
 banner.innerText="SYSTEM NORMAL";
 }

 tempHistory.push(data.temperature);
 riskHistory.push(data.risk);

 if(tempHistory.length>50) tempHistory.shift();
 if(riskHistory.length>50) riskHistory.shift();

 drawGraph(tempCtx,tempCanvas,tempHistory,"#00ff88");
 drawGraph(riskCtx,riskCanvas,riskHistory,"#ffaa00");
 });
 },800);
 </script>

 </body>
 </html>
 )rawliteral";

 server.send(200,"text/html; charset=utf-8",page);
}

// ================= SETUP =================
void setup(){
 Serial.begin(115200);

 bootTime = millis();

 pinMode(VIBRATION_PIN, INPUT_PULLUP);
 pinMode(BUZZER, OUTPUT);
 digitalWrite(BUZZER, LOW);

 dht.begin();

 coolingServo.attach(SERVO_PIN);
 coolingServo.write(0);

 if (SD.begin(SD_CS)) {
 sdReady = true;

 if (!SD.exists("/log.csv")) {
 File file = SD.open("/log.csv", FILE_WRITE);
 file.println("Time,Temp,Humidity,VibrationIntensity,Risk,Level");
 file.close();
 }
 }

 WiFi.softAP(ssid,password);

 server.on("/",handleRoot);
 server.on("/data",handleData);
 server.on("/download",handleDownload);
 server.on("/clear",handleClear);

 server.begin();
}

// ================= LOOP =================
void loop(){

 if(millis()-lastSampleTime >= 100){
 lastSampleTime = millis();

 int potValue = analogRead(POT_PIN);
 potPercent = map(potValue, 0, 4095, 0, 100);
 dynamicThreshold = map(potValue, 0, 4095, 18, 2);

 float newTemp = dht.readTemperature();
 float newHum = dht.readHumidity();

 if(!isnan(newTemp)) temperature = newTemp;
 if(!isnan(newHum)) humidity = newHum;

 int vibrationState = digitalRead(VIBRATION_PIN);

 if(vibrationState == HIGH) vibIntegrator += 2;
 else vibIntegrator -= 3;

 vibIntegrator = constrain(vibIntegrator,0,20);

 vibrationDetected = (vibIntegrator > dynamicThreshold);

 riskScore = 0;

 if (temperature > 25)
 riskScore += min((int)((temperature - 25)*6),80);

 if (vibrationDetected)
 riskScore += min(vibIntegrator*4,80);

 riskScore = constrain(riskScore,0,100);

 if (temperature >= 35 && vibrationDetected)
 faultType = "COMBINED FAULT";
 else if (temperature >= 35)
 faultType = "OVERHEAT";
 else if (vibrationDetected)
 faultType = "MECHANICAL FAULT";
 else
 faultType = "NORMAL";

 if (temperature >= 35 || vibrationDetected) {
 tone(BUZZER, 2000);
 alarmLevel = "CRITICAL";
 }
 else if (temperature >= 30) {
 if (millis() - lastBeepTime > 1000) {
 lastBeepTime = millis();
 tone(BUZZER, 2000);
 delay(100);
 noTone(BUZZER);
 }
 alarmLevel = "WARNING";
 }
 else {
 noTone(BUZZER);
 alarmLevel = "NORMAL";
 }

 if (temperature > 30) {
 if (millis() - lastServoMove > 20) {
 lastServoMove = millis();

 if (servoDirection) {
 servoAngle += 2;
 if (servoAngle >= 180) servoDirection = false;
 } else {
 servoAngle -= 2;
 if (servoAngle <= 0) servoDirection = true;
 }

 coolingServo.write(servoAngle);
 }
 } else {
 coolingServo.write(0);
 }
 }

 if(millis() - lastLogTime > 5000){
 lastLogTime = millis();
 logToSD();
 }

 server.handleClient();
}