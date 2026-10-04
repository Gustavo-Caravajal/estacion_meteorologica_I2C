// ============================================================================
// 1. LIBRERIAS Y DEFINICIONES
// ============================================================================
#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include "time.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- Configuración BMP280 por I²C ---
#define BMP_SDA 21
#define BMP_SCL 22
Adafruit_BMP280 bmp; 

// --- Configuración DS18B20 (OneWire) ---
#define ONE_WIRE_BUS 15 // GPIO 15 para liberar pines SPI
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

// --- Configuración DHT22 ---
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// --- Configuración Wi-Fi ---
const char* ssid       = "_SSID_";
const char* password   = "_CLAVE_";

// --- Configuración NTP (Hora) ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -3 * 3600; // GMT-3 (Argentina)
const int   daylightOffset_sec = 0;

// --- Configuración Firebase Realtime Database ---
const char* FIREBASE_HOST = "https://tp2arquitectura-default-rtdb.firebaseio.com";
const char* FIREBASE_AUTH = "AIzaSyBCQW9mAshEh9bTstyTuZvU9BTHyh_EAlM";

#define RUTA_BUFFER_OFFLINE "/buffer.csv"
#define CAPACIDAD_RAM_LOCAL 20

// ============================================================================
// 2. OBJETOS GLOBALES Y ESTRUCTURAS
// ============================================================================
WebServer server(80);
WiFiClientSecure clientSecure;

struct Medicion {
  float temperatura;
  float humedad;
  float presion;
  time_t timestamp;
};

Medicion medicionActual = {0.0, 0.0, 0.0, 0};
Medicion bufferRAM[CAPACIDAD_RAM_LOCAL];
int totalRAM = 0;

unsigned long ultimoMuestreo = 0;
const unsigned long INTERVALO_MUESTREO = 2000; // Muestreo de sensores y web LAN cada 2 seg

unsigned long ultimoGuardado = 0;
const unsigned long INTERVALO_MINIMO_GUARDADO = 30000; // Ventana mínima de 30 segundos
const float DELTA_TEMPERATURA_MINIMO = 1.0;            // Variación mínima de 1 °C
float ultimaTemperaturaGuardada = -999.0;              // Valor centinela inicial

// ============================================================================
// 3. PAGINA WEB LOCAL EMBEBIDA EN MEMORIA FLASH (SVG NATIVO)
// ============================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Estación Meteorológica - Panel Local</title>
  <style>
    :root {
      --bg-dark: #0f141c;
      --card-bg: #151b26;
      --border-color: #242c3d;
      --text-muted: #8b949e;
      --text-light: #e6edf3;
      --color-temp: #ff9800;
      --color-pressure: #00e5ff;
      --color-humidity: #00e676;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background-color: var(--bg-dark); color: var(--text-light); padding: 1.5rem; display: flex; justify-content: center; }
    .dashboard-container { width: 100%; max-width: 1100px; display: flex; flex-direction: column; gap: 1.25rem; }
    .header-bar { display: flex; justify-content: space-between; align-items: center; padding: 0 0.5rem; }
    .live-status { display: flex; align-items: center; gap: 0.5rem; font-size: 0.85rem; font-weight: 700; letter-spacing: 0.05em; }
    .status-dot { width: 9px; height: 9px; border-radius: 50%; background-color: var(--color-humidity); box-shadow: 0 0 8px var(--color-humidity); animation: pulse 2s infinite ease-in-out; }
    .last-update { font-size: 0.85rem; color: var(--text-muted); }
    @keyframes pulse { 0%, 100% { opacity: 1; transform: scale(1); } 50% { opacity: 0.4; transform: scale(0.9); } }
    .metric-card { background-color: var(--card-bg); border: 1px solid var(--border-color); border-radius: 8px; padding: 1.25rem 1.5rem 1rem 1.5rem; display: flex; flex-direction: column; gap: 0.5rem; box-shadow: 0 4px 15px rgba(0, 0, 0, 0.3); }
    .card-header { display: flex; justify-content: space-between; align-items: center; }
    .card-title { font-size: 0.9rem; font-weight: 600; letter-spacing: 0.05em; color: #cbd5e1; }
    .metric-value { font-size: 1.05rem; font-weight: 600; }
    .val-temp { color: var(--color-temp); }
    .val-pressure { color: var(--color-pressure); }
    .val-humidity { color: var(--color-humidity); }
    .svg-container { width: 100%; height: 180px; }
    svg { width: 100%; height: 100%; overflow: visible; }
    .grid-line { stroke: rgba(255, 255, 255, 0.06); stroke-width: 1; }
    .axis-label { fill: #64748b; font-size: 10px; }
  </style>
</head>
<body>
  <div class="dashboard-container">
    <header class="header-bar">
      <div class="live-status"><span class="status-dot"></span><span>LIVE DATA (LAN)</span></div>
      <div class="last-update" id="lbl-update">updated at --:--</div>
    </header>
    <section class="metric-card">
      <div class="card-header"><span class="card-title">TEMPERATURE (°C)</span><span class="metric-value val-temp" id="lbl-temp">Temperature: -- °C</span></div>
      <div class="svg-container"><svg id="chart-temp" viewBox="0 0 800 180"></svg></div>
    </section>
    <section class="metric-card">
      <div class="card-header"><span class="card-title">PRESSURE (hPa)</span><span class="metric-value val-pressure" id="lbl-pressure">Pressure: -- hPa</span></div>
      <div class="svg-container"><svg id="chart-pressure" viewBox="0 0 800 180"></svg></div>
    </section>
    <section class="metric-card">
      <div class="card-header"><span class="card-title">HUMIDITY (%)</span><span class="metric-value val-humidity" id="lbl-humidity">Humidity: -- %</span></div>
      <div class="svg-container"><svg id="chart-humidity" viewBox="0 0 800 180"></svg></div>
    </section>
  </div>
  <script>
    const PAD = { TOP: 20, RIGHT: 30, BOTTOM: 30, LEFT: 45 };
    const WIDTH = 800, HEIGHT = 180;
    const GRAPH_W = WIDTH - PAD.LEFT - PAD.RIGHT;
    const GRAPH_H = HEIGHT - PAD.TOP - PAD.BOTTOM;

    function generarBezierPath(puntos) {
      if (puntos.length === 0) return "";
      if (puntos.length === 1) return `M ${puntos[0].x} ${puntos[0].y}`;
      let d = `M ${puntos[0].x} ${puntos[0].y}`;
      for (let i = 0; i < puntos.length - 1; i++) {
        const p0 = (i > 0) ? puntos[i - 1] : puntos[i];
        const p1 = puntos[i];
        const p2 = puntos[i + 1];
        const p3 = (i != puntos.length - 2) ? puntos[i + 2] : p2;
        const cp1x = p1.x + (p2.x - p0.x) / 6;
        const cp1y = p1.y + (p2.y - p0.y) / 6;
        const cp2x = p2.x - (p3.x - p1.x) / 6;
        const cp2y = p2.y - (p3.y - p1.y) / 6;
        d += ` C ${cp1x} ${cp1y}, ${cp2x} ${cp2y}, ${p2.x} ${p2.y}`;
      }
      return d;
    }

    function renderizarGraficoSVG(svgId, datos, yMin, yMax, gradId, colorHex) {
      const svg = document.getElementById(svgId);
      if (!svg) return;
      const yRange = yMax - yMin;
      const numPuntos = datos.length;

      let contenidoSVG = `
        <defs>
          <linearGradient id="${gradId}" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color="${colorHex}" stop-opacity="0.35"/>
            <stop offset="100%" stop-color="${colorHex}" stop-opacity="0.0"/>
          </linearGradient>
        </defs>
      `;

      const pasosY = 4;
      for (let i = 0; i <= pasosY; i++) {
        const yVal = yMin + (yRange / pasosY) * i;
        const yPos = PAD.TOP + GRAPH_H - (GRAPH_H / pasosY) * i;
        contenidoSVG += `
          <line class="grid-line" x1="${PAD.LEFT}" y1="${yPos}" x2="${WIDTH - PAD.RIGHT}" y2="${yPos}" />
          <text class="axis-label" x="${PAD.LEFT - 8}" y="${yPos + 3}" text-anchor="end">${Math.round(yVal)}</text>
        `;
      }

      if (numPuntos === 0) { svg.innerHTML = contenidoSVG; return; }

      const puntos = datos.map((item, idx) => {
        const xPos = PAD.LEFT + (numPuntos > 1 ? (GRAPH_W / (numPuntos - 1)) * idx : GRAPH_W / 2);
        const yClamped = Math.max(yMin, Math.min(yMax, item.val));
        const yPos = PAD.TOP + GRAPH_H - ((yClamped - yMin) / yRange) * GRAPH_H;
        return { x: xPos, y: yPos, label: item.hora };
      });

      puntos.forEach(p => {
        contenidoSVG += `
          <line class="grid-line" x1="${p.x}" y1="${PAD.TOP}" x2="${p.x}" y2="${PAD.TOP + GRAPH_H}" />
          <text class="axis-label" x="${p.x}" y="${PAD.TOP + GRAPH_H + 18}" text-anchor="middle">${p.label}</text>
        `;
      });
      contenidoSVG += `<text class="axis-label" x="${PAD.LEFT + GRAPH_W / 2}" y="${HEIGHT - 2}" text-anchor="middle">Time (HH:MM)</text>`;

      const curvaPath = generarBezierPath(puntos);
      const primerPunto = puntos[0];
      const ultimoPunto = puntos[puntos.length - 1];
      const areaPath = `${curvaPath} L ${ultimoPunto.x} ${PAD.TOP + GRAPH_H} L ${primerPunto.x} ${PAD.TOP + GRAPH_H} Z`;

      contenidoSVG += `
        <path d="${areaPath}" fill="url(#${gradId})" />
        <path d="${curvaPath}" fill="none" stroke="${colorHex}" stroke-width="2.5" stroke-linecap="round" />
      `;

      puntos.forEach(p => {
        contenidoSVG += `<circle cx="${p.x}" cy="${p.y}" r="4.5" fill="${colorHex}" stroke="#151b26" stroke-width="2" />`;
      });

      svg.innerHTML = contenidoSVG;
    }

    async function actualizarDatosLocales() {
      try {
        const res = await fetch('/api/data');
        if (!res.ok) return;
        const datos = await res.json();

        if (datos.actual) {
          document.getElementById('lbl-temp').textContent = `Temperature: ${Number(datos.actual.temperatura).toFixed(1)}°C`;
          document.getElementById('lbl-pressure').textContent = `Pressure: ${Number(datos.actual.presion).toFixed(1)} hPa`;
          document.getElementById('lbl-humidity').textContent = `Humidity: ${Math.round(Number(datos.actual.humedad))}%`;

          if (datos.actual.timestamp) {
            const f = new Date(Number(datos.actual.timestamp) * 1000);
            const hh = String(f.getHours()).padStart(2, '0');
            const mm = String(f.getMinutes()).padStart(2, '0');
            document.getElementById('lbl-update').textContent = `updated at ${hh}.${mm}`;
          }
        }

        if (Array.isArray(datos.historico)) {
          const listTemp = [], listPres = [], listHum = [];
          datos.historico.forEach(item => {
            const f = new Date(Number(item.timestamp) * 1000);
            const hora = `${String(f.getHours()).padStart(2, '0')}.${String(f.getMinutes()).padStart(2, '0')}`;
            listTemp.push({ val: Number(item.temperatura), hora: hora });
            listPres.push({ val: Number(item.presion), hora: hora });
            listHum.push({ val: Number(item.humedad), hora: hora });
          });

          renderizarGraficoSVG('chart-temp', listTemp, 15, 30, 'gradTemp', '#ff9800');
          renderizarGraficoSVG('chart-pressure', listPres, 900, 1020, 'gradPressure', '#00e5ff');
          renderizarGraficoSVG('chart-humidity', listHum, 40, 100, 'gradHumidity', '#00e676');
        }
      } catch (err) {
        console.error("Error al actualizar datos locales:", err);
      }
    }

    window.addEventListener('DOMContentLoaded', () => {
      actualizarDatosLocales();
      setInterval(actualizarDatosLocales, 3000);
    });
  </script>
</body>
</html>
)rawliteral";

// ============================================================================
// 4. FUNCIONES DE TIEMPO, RESILIENCIA OFFLINE Y RED
// ============================================================================

// nombrefuncion : getTimestamp - Retorna la fecha formateada y opcionalmente el epoch por referencia
String getTimestamp(time_t* epochOut = nullptr) {
  struct tm timeinfo;
  time_t now;
  time(&now);
  if (epochOut) *epochOut = now;
  if (!getLocalTime(&timeinfo)) {
    return "Error fecha/hora";
  }
  char timeStringBuff[30];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

// nombrefuncion : guardarMedicionEnFlash - Almacena las muestras no enviadas a un archivo CSV en LittleFS
void guardarMedicionEnFlash(Medicion m) {
  File archivo = LittleFS.open(RUTA_BUFFER_OFFLINE, FILE_APPEND);
  if (!archivo) {
    Serial.println("ERROR: No se pudo abrir LittleFS para escritura");
    return;
  }
  archivo.printf("%lu,%.2f,%.2f,%.2f\n", (unsigned long)m.timestamp, m.temperatura, m.humedad, m.presion);
  archivo.close();
  Serial.println("Resiliencia: Medicion guardada localmente en LittleFS");
}

// nombrefuncion : enviarFirebase - Ejecuta las peticiones HTTPS REST hacia Firebase Realtime Database
bool enviarFirebase(Medicion m) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  String urlBase = String(FIREBASE_HOST) + "/estacion_meteorologica";
  
  // 1. Actualizar nodo actual
  String urlActual = urlBase + "/actual.json?auth=" + String(FIREBASE_AUTH);
  String payloadActual = "{\"temperatura\":" + String(m.temperatura, 2) +
                         ",\"humedad\":" + String(m.humedad, 2) +
                         ",\"presion\":" + String(m.presion, 2) +
                         ",\"timestamp\":" + String((unsigned long)m.timestamp) + "}";

  http.begin(clientSecure, urlActual);
  http.addHeader("Content-Type", "application/json");
  int codeActual = http.PUT(payloadActual);
  http.end();

  if (codeActual != 200 && codeActual != 204) {
    return false;
  }

  // 2. Insertar nodo histórico con clave timestamp
  String urlHistorico = urlBase + "/historico/" + String((unsigned long)m.timestamp) + ".json?auth=" + String(FIREBASE_AUTH);
  String payloadHistorico = "{\"temperatura\":" + String(m.temperatura, 2) +
                            ",\"humedad\":" + String(m.humedad, 2) +
                            ",\"presion\":" + String(m.presion, 2) + "}";

  http.begin(clientSecure, urlHistorico);
  http.addHeader("Content-Type", "application/json");
  int codeHist = http.PUT(payloadHistorico);
  http.end();

  // 3. Actualizar estado y red del dispositivo
  String urlDispositivo = urlBase + "/dispositivo.json?auth=" + String(FIREBASE_AUTH);
  String payloadDispositivo = "{\"ip_lan\":\"" + WiFi.localIP().toString() + "\"" +
                              ",\"rssi\":" + String(WiFi.RSSI()) +
                              ",\"ultimo_sync\":" + String((unsigned long)m.timestamp) +
                              ",\"estado\":\"online\"}";

  http.begin(clientSecure, urlDispositivo);
  http.addHeader("Content-Type", "application/json");
  http.PUT(payloadDispositivo);
  http.end();

  return (codeHist == 200 || codeHist == 204);
}

// nombrefuncion : sincronizarBufferOffline - Transmite el lote acumulado en LittleFS cuando retorna la conexión
void sincronizarBufferOffline() {
  if (WiFi.status() != WL_CONNECTED || !LittleFS.exists(RUTA_BUFFER_OFFLINE)) {
    return;
  }

  File archivo = LittleFS.open(RUTA_BUFFER_OFFLINE, FILE_READ);
  if (!archivo || archivo.size() == 0) {
    if (archivo) archivo.close();
    LittleFS.remove(RUTA_BUFFER_OFFLINE);
    return;
  }

  Serial.println("Sincronizando lote acumulado offline...");

  String batchJson = "{";
  bool primerRegistro = true;

  while (archivo.available()) {
    String linea = archivo.readStringUntil('\n');
    linea.trim();
    if (linea.length() == 0) continue;

    int idx1 = linea.indexOf(',');
    int idx2 = linea.indexOf(',', idx1 + 1);
    int idx3 = linea.indexOf(',', idx2 + 1);

    if (idx1 > 0 && idx2 > 0 && idx3 > 0) {
      String ts = linea.substring(0, idx1);
      String temp = linea.substring(idx1 + 1, idx2);
      String hum = linea.substring(idx2 + 1, idx3);
      String pres = linea.substring(idx3 + 1);

      if (!primerRegistro) {
        batchJson += ",";
      }
      batchJson += "\"" + ts + "\":{\"temperatura\":" + temp + ",\"humedad\":" + hum + ",\"presion\":" + pres + "}";
      primerRegistro = false;
    }
  }
  archivo.close();
  batchJson += "}";

  if (!primerRegistro) {
    HTTPClient http;
    String urlHistoricoBatch = String(FIREBASE_HOST) + "/estacion_meteorologica/historico.json?auth=" + String(FIREBASE_AUTH);
    http.begin(clientSecure, urlHistoricoBatch);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.PATCH(batchJson);
    http.end();

    if (httpCode == 200 || httpCode == 204) {
      LittleFS.remove(RUTA_BUFFER_OFFLINE);
      Serial.println("Sincronizacion completada con exito. Buffer LittleFS limpiado.");
    } else {
      Serial.print("Error al sincronizar en Firebase. Codigo HTTP: ");
      Serial.println(httpCode);
    }
  }
}

// nombrefuncion : manejarRaiz - Despacha la vista HTML embebida
void manejarRaiz() {
  server.send_P(200, "text/html", INDEX_HTML);
}

// nombrefuncion : manejarApiData - Retorna el payload JSON para la página web LAN
void manejarApiData() {
  String json = "{";
  json += "\"actual\":{";
  json += "\"temperatura\":" + String(medicionActual.temperatura, 1) + ",";
  json += "\"humedad\":" + String(medicionActual.humedad, 1) + ",";
  json += "\"presion\":" + String(medicionActual.presion, 1) + ",";
  json += "\"timestamp\":" + String((unsigned long)medicionActual.timestamp);
  json += "},";

  json += "\"historico\":[";
  for (int i = 0; i < totalRAM; i++) {
    json += "{";
    json += "\"temperatura\":" + String(bufferRAM[i].temperatura, 1) + ",";
    json += "\"humedad\":" + String(bufferRAM[i].humedad, 1) + ",";
    json += "\"presion\":" + String(bufferRAM[i].presion, 1) + ",";
    json += "\"timestamp\":" + String((unsigned long)bufferRAM[i].timestamp);
    json += "}";
    if (i < totalRAM - 1) {
      json += ",";
    }
  }
  json += "]";
  json += "}";

  server.send(200, "application/json", json);
}

// ============================================================================
// 5. SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);

  // Inicializar almacenamiento para resiliencia offline
  if (!LittleFS.begin(true)) {
    Serial.println("ERROR: Fallo al montar LittleFS");
  } else {
    Serial.println("LittleFS montado correctamente.");
  }

  dht.begin();
  ds18b20.begin();

  // Inicializar bus I²C y BMP280
  Wire.begin(BMP_SDA, BMP_SCL);

  if (!bmp.begin(0x76)) {
    Serial.println(F("No se encontró el BMP280 en 0x76. Probando 0x77..."));
    if (!bmp.begin(0x77)) {
      Serial.println(F("ERROR: No se encontró el BMP280 por I²C. Revisá SDA, SCL, VCC y GND."));
    } else {
      Serial.println(F("BMP280 inicializado correctamente por I²C en 0x77."));
    }
  } else {
    Serial.println(F("BMP280 inicializado correctamente por I²C en 0x76."));
  }

  // Configuración de muestreo recomendada para BMP280
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */

  // Conexión Wi-Fi
  Serial.print("Conectando a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi conectado.");
  Serial.print("IP Asignada (LAN): ");
  Serial.println(WiFi.localIP());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Asegurar hora NTP válida antes de comenzar registros
  Serial.print("Sincronizando hora NTP");
  struct tm timeinfo;
  int reintentosNTP = 0;
  while (!getLocalTime(&timeinfo) && reintentosNTP < 20) {
    delay(500);
    Serial.print(".");
    reintentosNTP++;
  }
  if (reintentosNTP < 20) {
    Serial.println("\nHora NTP sincronizada correctamente.");
  } else {
    Serial.println("\nAdvertencia: Servidor NTP demorado. Se sincronizara en segundo plano.");
  }

  // Certificados TLS para Firebase
  clientSecure.setInsecure();

  // Rutas del Servidor Web
  server.on("/", HTTP_GET, manejarRaiz);
  server.on("/api/data", HTTP_GET, manejarApiData);
  server.begin();
  Serial.println("Servidor Web Local activo en puerto 80.");
}

// ============================================================================
// 6. LOOP
// ============================================================================
void loop() {
  server.handleClient();

  unsigned long ahora = millis();
  if (ahora - ultimoMuestreo >= INTERVALO_MUESTREO) {
    ultimoMuestreo = ahora;

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    ds18b20.requestTemperatures();
    float tempDS18B20 = ds18b20.getTempCByIndex(0);

    float tempBMP = bmp.readTemperature();
    float pressBMP = bmp.readPressure() / 100.0F; // Pa a hPa

    time_t epochActual = 0;
    String timestamp = getTimestamp(&epochActual);

    Serial.print("[");
    Serial.print(timestamp);
    Serial.print("] DHT22 Hum: ");
    Serial.print(h);
    Serial.print("% | Temp: ");
    Serial.print(t);
    Serial.println(" °C");

    if (tempDS18B20 == DEVICE_DISCONNECTED_C) {
      Serial.print("DS18B20: Error | ");
    } else {
      Serial.print("DS18B20 Temp: ");
      Serial.print(tempDS18B20);
      Serial.print(" °C | ");
    }

    Serial.print("BMP280 (I2C) Temp: ");
    Serial.print(tempBMP);
    Serial.print(" °C - Presion: ");
    Serial.print(pressBMP);
    Serial.println(" hPa");

    // Asignar lecturas prioritarias según consigna
    if (tempDS18B20 == DEVICE_DISCONNECTED_C) {
      medicionActual.temperatura = 0.0;
      Serial.println("ADVERTENCIA: DS18B20 desconectado; temperatura invalida.");
    } else {
      medicionActual.temperatura = tempDS18B20;
    }
    medicionActual.humedad = (!isnan(h)) ? h : 0.0;
    medicionActual.presion = (!isnan(pressBMP) && pressBMP > 0.0) ? pressBMP : 0.0;
    medicionActual.timestamp = epochActual;

    // Actualizar búfer RAM para visualización web LAN (en tiempo real continuo)
    if (totalRAM < CAPACIDAD_RAM_LOCAL) {
      bufferRAM[totalRAM] = medicionActual;
      totalRAM++;
    } else {
      for (int i = 0; i < CAPACIDAD_RAM_LOCAL - 1; i++) {
        bufferRAM[i] = bufferRAM[i + 1];
      }
      bufferRAM[CAPACIDAD_RAM_LOCAL - 1] = medicionActual;
    }

    // Regla de guardado: Al menos 30 segundos y delta de temperatura >= 1 grado
    bool ventanaTiempoCumplida = (ahora - ultimoGuardado >= INTERVALO_MINIMO_GUARDADO);
    bool esPrimerGuardado = (ultimaTemperaturaGuardada == -999.0);
    bool deltaTemperaturaCumplido = (fabs(medicionActual.temperatura - ultimaTemperaturaGuardada) >= DELTA_TEMPERATURA_MINIMO);

    if (ventanaTiempoCumplida && (esPrimerGuardado || deltaTemperaturaCumplido) && epochActual > 1700000000) {
      ultimoGuardado = ahora;
      ultimaTemperaturaGuardada = medicionActual.temperatura;

      Serial.println("Condicion cumplida: Guardando en historico (Firebase / Flash)...");

      bool enviadoCloud = enviarFirebase(medicionActual);
      if (!enviadoCloud) {
        guardarMedicionEnFlash(medicionActual);
      } else {
        sincronizarBufferOffline();
      }
    }
  }
}