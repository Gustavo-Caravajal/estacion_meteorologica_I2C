# Estación Meteorológica IoT

Sistema de estación meteorológica desarrollado sobre **ESP32** para la medición, visualización y almacenamiento de variables ambientales.

El sistema permite obtener **temperatura, humedad y presión atmosférica**, visualizar los datos desde una interfaz web local y almacenarlos en la nube mediante Firebase. Además, incorpora un mecanismo de almacenamiento local para mantener los datos cuando la conexión con Internet o Firebase no se encuentra disponible.

## Características

* Medición de:

  * 🌡️ Temperatura mediante **DS18B20**.
  * 💧 Humedad mediante **DHT22**.
  * 🌬️ Presión atmosférica mediante **BMP280**.
* Servidor web local accesible desde la red Wi-Fi.
* Visualización de los valores actuales.
* Gráficos de temperatura, humedad y presión.
* Almacenamiento histórico en **Firebase Realtime Database**.
* Almacenamiento local mediante **LittleFS** cuando no es posible comunicarse con Firebase.
* Sincronización automática de los datos almacenados localmente cuando vuelve la conexión.
* Obtención de fecha y hora mediante **NTP**.
* Historial temporal de las mediciones en memoria RAM.

## Hardware utilizado

| Componente | Función                      | ESP32   |
| ---------- | ---------------------------- | ------- |
| DHT22      | Humedad                      | GPIO 4  |
| DS18B20    | Temperatura                  | GPIO 15 |
| BMP280     | Presión atmosférica          | I2C     |
| ESP32      | Procesamiento y comunicación | —       |

El BMP280 utiliza comunicación **I2C**, con:

* SDA → GPIO 21
* SCL → GPIO 22

El DS18B20 utiliza comunicación **OneWire**.

El DHT22 utiliza comunicación digital mediante su línea de datos.

## Funcionamiento

El ESP32 realiza una nueva lectura de los sensores aproximadamente cada **2 segundos**.

En cada ciclo se obtienen:

```text
DS18B20 → Temperatura
DHT22   → Humedad
BMP280  → Presión
```

Los valores obtenidos se mantienen en memoria RAM y son utilizados por la interfaz web local para mostrar el estado actual de la estación.

El almacenamiento persistente se realiza con una frecuencia menor para evitar escrituras y comunicaciones innecesarias.

### Almacenamiento

Las mediciones destinadas al almacenamiento se procesan aproximadamente cada **30 segundos** y se utiliza además un cambio mínimo de temperatura como criterio adicional.

Cuando es posible comunicarse con Firebase:

```text
ESP32
  │
  ├── Lectura de sensores
  │
  ├── Actualización de datos
  │
  └── Firebase Realtime Database
```

Si Firebase no está disponible:

```text
ESP32
  │
  ├── Lectura de sensores
  │
  └── LittleFS
        │
        └── buffer.csv
```

Cuando la conexión vuelve a estar disponible, las mediciones almacenadas en `buffer.csv` son enviadas a Firebase y, si la sincronización es exitosa, el archivo local es eliminado.

## Visualización local

El ESP32 funciona como servidor web y proporciona una interfaz accesible desde un navegador dentro de la misma red.

La interfaz permite consultar:

* Temperatura actual.
* Humedad actual.
* Presión atmosférica actual.
* Historial reciente de mediciones.
* Gráficos de las variables ambientales.

La información de la interfaz se obtiene mediante el endpoint:

```text
/api/data
```

## Persistencia offline

Una de las características principales del proyecto es la tolerancia a la pérdida de conexión con el servicio en la nube.

Los datos que no pueden enviarse a Firebase se almacenan en el sistema de archivos **LittleFS**:

```text
/buffer.csv
```

El archivo contiene las mediciones pendientes de sincronización.

De esta manera, una interrupción temporal de la conexión no provoca directamente la pérdida de los datos que debían almacenarse en la nube.

## Tecnologías y librerías

El proyecto utiliza:

* **ESP32**
* **Arduino**
* **Wi-Fi**
* **DHT22**
* **DS18B20**
* **BMP280**
* **LittleFS**
* **Firebase Realtime Database**
* **HTTP/HTTPS**
* **NTP**
* **HTML + JavaScript + SVG**

Principales librerías utilizadas:

```cpp
WiFi.h
WebServer.h
HTTPClient.h
WiFiClientSecure.h
LittleFS.h
Adafruit_BMP280.h
OneWire.h
DallasTemperature.h
DHT.h
```

## Estructura general

El funcionamiento del sistema puede resumirse de la siguiente manera:

```text
             ┌──────────────┐
             │    ESP32     │
             └──────┬───────┘
                    │
       ┌────────────┼────────────┐
       │            │            │
       ▼            ▼            ▼
    DHT22        DS18B20      BMP280
  Humedad       Temperatura   Presión
       │            │            │
       └────────────┼────────────┘
                    ▼
             Procesamiento
                    │
          ┌─────────┴─────────┐
          │                   │
          ▼                   ▼
     Web local             Firebase
          │                   │
          │             Si no disponible
          │                   ▼
          │                LittleFS
          │                   │
          │             Cuando vuelve
          │               la conexión
          │                   ▼
          └──────────── Firebase
```

## Configuración

Antes de utilizar el programa es necesario configurar los parámetros de conexión Wi-Fi y Firebase en el código.

## Ejecución

1. Conectar los sensores al ESP32 según el esquema de conexiones.
2. Configurar las credenciales de Wi-Fi.
3. Configurar los parámetros de Firebase.
4. Cargar el programa en el ESP32.
5. Conectar el dispositivo a la red Wi-Fi.
6. Acceder desde un navegador a la dirección IP obtenida por el ESP32.
7. Observar las mediciones y su evolución mediante la interfaz web.

## Trabajo de Laboratorio

Este proyecto corresponde al **Trabajo de Laboratorio N.º 2 de Arquitectura de Sistemas de Elaboración de Datos II — Segundo Cuatrimestre 2026**.

El objetivo principal es implementar una estación meteorológica capaz de medir variables ambientales, visualizarlas localmente y almacenarlas en la nube, incorporando además un mecanismo de almacenamiento local que permita continuar registrando información ante una interrupción temporal de la conectividad.
