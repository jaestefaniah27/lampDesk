#include <WiFi.h>
#include <PubSubClient.h>
#include "time.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HTTPClient.h>

// 📌 Configuración del OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1  // No se usa el pin de reset
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// 📌 Estado de la pantalla (0 = Hora/Fecha, 1 = Precio BTC)
int modoPantalla = 0;
bool pantallaEncendida = true;
unsigned long tiempoInicioOscuridad = 0;
const int tiempoApagado = 3000;  // 30 segundos en ms
const int umbralOscuridad = 4000; // Si luz > 4000, el cuarto está oscuro
const int umbralEncendido = 3500; // Si luz < 3500 o vibración, se reactiva la pantalla


// 🌍 Servidor NTP y zona horaria de España
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;    // UTC+1 (Horario estándar)
const int daylightOffset_sec = 3600; // UTC+2 en horario de verano
const int clockDelay = 1000;
int lastTimeClock = 0;
// 🗓 Array con los días de la semana en español
const char* diasSemanaESP[] = {"DOM", "LUN", "MAR", "MIE", "JUE", "VIE", "SAB"};

// Credenciales Wi-Fi
const char* ssid = "DIGIFIBRA-UQGR";
const char* password = "F7sKyyKSSs";

// Pines para la lámpara
const int pwmPinWhite = 25;  // Barra azulada (luz fría)
const int pwmPinYellow = 26; // Barra amarillenta (luz cálida)

// Pines de sensores
const int botonPin = 34;       // Botón
const int fotoresistorPin = 35; // Fotoresistor (luz ambiente)
const int vibracionPin = 23;    // Sensor de vibración

// 📌 Debounce para sensores
bool lastButtonState = true;
bool lastVibrationState = false;
unsigned long lastButtonDebounceTime = 0;
unsigned long lastVibrationDebounceTime = 0;
const int debounceButtonDelay = 50;
const int debounceVibrationDelay = 100;

// Configuración PWM para control de brillo
const int frecuenciaPWM = 40000;  // Frecuencia PWM en Hz
const int resolucionPWM = 8;      // Resolución de 8 bits (0-255)

// Estado de la lámpara
bool lampOn = false;  
int lampBrightness = 128;  // Nivel objetivo de brillo (0-255)
int currentLampBrightness = 128; // Nivel actual del brillo (para transición)
int currentPWMWhite = 0;  // PWM actual del LED blanco
int currentPWMYellow = 0; // PWM actual del LED amarillo
int targetPWMWhite = 0;   // PWM objetivo del LED blanco
int targetPWMYellow = 0;  // PWM objetivo del LED amarillo

const int stepBrightness = 1;  // Tamaño del paso para transición suave
const int stepColor = 2;       // Tamaño del paso para transición de color
unsigned long lastUpdate = 0; // Control de tiempo para transición
const int updateInterval = 3; // Intervalo de actualización en ms

// Estado de la temperatura de color
int lampColorTemp = 191;  // Valor inicial de temperatura (rango 148-260)

// Control de lectura del fotoresistor
unsigned long lastPhotoresistorRead = 0;
const int photoresistorInterval = 1000; // 1 segundo
int luzAmbiente = 0;
// Configuración del broker MQTT
const char* mqtt_server = "192.168.1.166";

WiFiClient espClient;
PubSubClient client(espClient);

// 📌 Obtener el precio de Bitcoin
String obtenerPrecioBitcoin() {
    HTTPClient http;
    http.begin("https://api.coindesk.com/v1/bpi/currentprice/EUR.json");
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String payload = http.getString();
        int index = payload.indexOf("\"rate\":\"");
        if (index > 0) {
            int endIndex = payload.indexOf("\"", index + 8);
            String precio = payload.substring(index + 8, endIndex);
            
            // Extraer el cambio en 7 días en porcentaje
            int indexChange = payload.indexOf("\"eur_7d_change\":");
            int endChange = payload.indexOf("}", indexChange);
            String cambioStr = payload.substring(indexChange + 17, endChange);

            // 🛠️ Eliminar comas y convertir a número flotante
            precio.replace(",", "");  
            float precioFloat = precio.toFloat();

            // 🎯 Si el precio es mayor a 100,000€, mostrar solo los 4 primeros dígitos
            if (precioFloat >= 100000) {
                String precioReducido = precio.substring(0, 3) + "." + precio.substring(4, 5); // Primeros 4 dígitos
                return precioReducido + "k";  // Agrega "K" para representar miles
            }
            
            http.end();
            return precio;
        }
    }
    http.end();
    return "Error!";
}

// 📌 Función para obtener la hora y fecha desde Internet
void actualizarPantalla() {
  unsigned long now = millis();
  if (now - lastTimeClock >= clockDelay) {
    lastTimeClock = now;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("⚠️ Error obteniendo la hora");
        return;
    }
  if (modoPantalla == 0) {
    // Obtener hora en formato HH:MM:SS
        char horaStr[10];
        strftime(horaStr, sizeof(horaStr), "%H:%M:%S", &timeinfo);

         // Obtener día de la semana en español y fecha en formato DD/MM
        char fechaStr[10];
        snprintf(fechaStr, sizeof(fechaStr), "%s %02d/%02d", diasSemanaESP[timeinfo.tm_wday], timeinfo.tm_mday, timeinfo.tm_mon + 1);

        // Convertir el nombre del día a mayúsculas
        for (int i = 0; i < 3; i++) {
            fechaStr[i] = toupper(fechaStr[i]);
        }

        // 📌 Mostrar en el OLED
        display.clearDisplay();

        // ⏰ Mostrar hora (grande)
        display.setTextSize(2);  // Tamaño grande
        display.setTextColor(SSD1306_WHITE);
        int xHora = 18; // Centramos la hora (6 píxeles por char)
        display.setCursor(xHora, 4);
        display.print(horaStr);

        // 📅 Mostrar fecha (pequeña)
        display.setTextSize(1);  // Tamaño pequeño
        int xFecha = (SCREEN_WIDTH - (6 * strlen(fechaStr))) / 2; // Centramos la fecha
        display.setCursor(xFecha, 24);
        display.print(fechaStr);
  } else {
        display.clearDisplay();
        String precioBTC = obtenerPrecioBitcoin();
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.print("BTC:");
        display.print(precioBTC);
  }

  if(pantallaEncendida) display.display();
  }
}


// Función para leer sensores
void leerSensores() {
    unsigned long now = millis();

    // Leer fotoresistor una vez por segundo
    if (now - lastPhotoresistorRead >= photoresistorInterval) {
      lastPhotoresistorRead = now;
      luzAmbiente = analogRead(fotoresistorPin);
      Serial.print("Fotoresistor: ");
      Serial.println(luzAmbiente);
      // Si la luz es mayor que el umbralOscuridad, inicia el temporizador
    if (luzAmbiente > umbralOscuridad) {
      if (tiempoInicioOscuridad == 0) {
          tiempoInicioOscuridad = millis();  // Inicia el temporizador
      }
      // Si han pasado 30s y la pantalla está encendida, la apagamos
      else if ((millis() - tiempoInicioOscuridad) >= tiempoApagado && pantallaEncendida) {
          pantallaEncendida = false;
          display.clearDisplay();
          display.display();  // Apagar pantalla
          Serial.println("🌙 Pantalla apagada (cuarto oscuro)");
      }
    } 
  // Si la luz es menor que umbralEncendido o hay vibración, encendemos la pantalla
  else if (luzAmbiente < umbralEncendido) {
      if (!pantallaEncendida) {
          pantallaEncendida = true;
          Serial.println("☀️ Pantalla encendida (luz o vibración detectada)");
          actualizarPantalla();  // Refrescar pantalla
      }
      tiempoInicioOscuridad = 0;  // Reiniciar temporizador
  }
  }

    // Leer botón solo cuando cambia de estado
    if (now - lastButtonDebounceTime > debounceButtonDelay) {
      bool buttonState = !digitalRead(botonPin);
      if (lastButtonState != buttonState){
        lastButtonDebounceTime = now;
        Serial.print("Botón: ");
        Serial.println(buttonState ? "Pulsado" : "Liberado");
        if (buttonState) {
          modoPantalla = (modoPantalla + 1) % 2;
          Serial.println(modoPantalla == 0 ? "⏰ Mostrando Hora/Fecha" : "₿ Mostrando Bitcoin");
          actualizarPantalla();
        }
        lastButtonState = buttonState;                
      }
    }

    // Leer sensor de vibración solo cuando cambia de estado
    if (now - lastVibrationDebounceTime > debounceVibrationDelay) {
      bool vibrationState = digitalRead(vibracionPin);
      if (vibrationState != lastVibrationState) {
        lastVibrationDebounceTime = now;
        Serial.println("Vibración detectada");
        lastVibrationState = vibrationState;
        if (!pantallaEncendida) {
          pantallaEncendida = true;
          Serial.println("☀️ Pantalla encendida (luz o vibración detectada)");
          actualizarPantalla();  // Refrescar pantalla
        }
        tiempoInicioOscuridad = 0;  // Reiniciar temporizador
      }
    }
}

void setup_wifi() {
    Serial.println("\nConectando a WiFi...");
    WiFi.begin(ssid, password);
    int intentos = 0;

    while (WiFi.status() != WL_CONNECTED && intentos < 10) {
        delay(1000);
        Serial.print(".");
        intentos++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi conectado!");
        Serial.print("Dirección IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nNo se pudo conectar a WiFi, reiniciando ESP32...");
        delay(5000);
        ESP.restart();
    }
}

// Función para calcular los valores PWM objetivo en función de la temperatura de color
void calcularPWMObjetivo() {
    if (lampColorTemp <= 162) {
        targetPWMWhite = 0;
        targetPWMYellow = 255;
    } else if (lampColorTemp >= 163 && lampColorTemp <= 190) {
        targetPWMWhite = 128;
        targetPWMYellow = 255;
    } else if (lampColorTemp >= 191 && lampColorTemp <= 218) {
        targetPWMWhite = 255;
        targetPWMYellow = 255;
    } else if (lampColorTemp >= 219 && lampColorTemp <= 246) {
        targetPWMWhite = 255;
        targetPWMYellow = 128;
    } else {
        targetPWMWhite = 255;
        targetPWMYellow = 0;
    }

    // Escalar los valores de PWM según el brillo actual
    targetPWMWhite = (targetPWMWhite * lampBrightness) / 255;
    targetPWMYellow = (targetPWMYellow * lampBrightness) / 255;
}

// Función para actualizar la lámpara con transición suave
void actualizarLampara() {
    unsigned long now = millis();
    
    if (now - lastUpdate >= updateInterval) {
        lastUpdate = now;

        int targetBrightness = lampOn ? lampBrightness : 0; // Si está apagado, reducir brillo a 0

        // Transición de brillo
        if (currentLampBrightness < targetBrightness) {
            currentLampBrightness += stepBrightness;
            if (currentLampBrightness > targetBrightness) currentLampBrightness = targetBrightness;
        } 
        else if (currentLampBrightness > targetBrightness) {
            currentLampBrightness -= stepBrightness;
            if (currentLampBrightness < targetBrightness) currentLampBrightness = targetBrightness;
        }

        // Transición de temperatura de color (PWM de LEDs)
        if (currentPWMWhite < targetPWMWhite) {
            currentPWMWhite += stepColor;
            if (currentPWMWhite > targetPWMWhite) currentPWMWhite = targetPWMWhite;
        } 
        else if (currentPWMWhite > targetPWMWhite) {
            currentPWMWhite -= stepColor;
            if (currentPWMWhite < targetPWMWhite) currentPWMWhite = targetPWMWhite;
        }

        if (currentPWMYellow < targetPWMYellow) {
            currentPWMYellow += stepColor;
            if (currentPWMYellow > targetPWMYellow) currentPWMYellow = targetPWMYellow;
        } 
        else if (currentPWMYellow > targetPWMYellow) {
            currentPWMYellow -= stepColor;
            if (currentPWMYellow < targetPWMYellow) currentPWMYellow = targetPWMYellow;
        }

        // Aplicar los valores PWM actuales a los LEDs
        ledcWrite(pwmPinWhite, currentPWMWhite);
        ledcWrite(pwmPinYellow, currentPWMYellow);
    }
}

// Callback para manejar mensajes MQTT
void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Mensaje en [");
    Serial.print(topic);
    Serial.print("]: ");

    String mensaje = "";  
    for (int i = 0; i < length; i++) {
        mensaje += (char)payload[i];  // Convertir el mensaje a String
    }
    Serial.println(mensaje);

    if (strcmp(topic, "home/lights/lamp/setOn") == 0) {
        if (mensaje == "true") {
            lampOn = true;
        } else if (mensaje == "false") {
            lampOn = false;
        }

        // Si la lámpara se apaga, establecer valores PWM objetivo en 0
        if (!lampOn) {
            targetPWMWhite = 0;
            targetPWMYellow = 0;
        } else {
            calcularPWMObjetivo();
        }
    } 
    
    else if (strcmp(topic, "home/lights/lamp/setBrightness") == 0) {
        int brillo = mensaje.toInt();
        if (brillo >= 0 && brillo <= 100) {
            lampBrightness = map(brillo, 0, 100, 0, 255);
            calcularPWMObjetivo();  // Actualizar valores objetivo
        }
    }

    else if (strcmp(topic, "home/lights/lamp/setColorTemp") == 0) {
        int temp = mensaje.toInt();
        if (temp >= 148 && temp <= 260) {
            lampColorTemp = temp;
            calcularPWMObjetivo();  // Actualizar valores objetivo
        }
    }
}

// Intentar conectar con el broker MQTT
void reconnect_mqtt() {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Conectando a HomeKit...");
    display.display();
    while (!client.connected()) {
        Serial.print("Intentando conexión MQTT... ");
        
        if (client.connect("ESP32Client")) {
            Serial.println("Conectado!");
            client.subscribe("home/lights/lamp/setOn");
            client.subscribe("home/lights/lamp/setBrightness");
            client.subscribe("home/lights/lamp/setColorTemp");
            display.clearDisplay();
            display.setCursor(0, 0);
            display.print("Conectado a HomeKit!!");
            display.display();            
        } else {
            Serial.print("Error (");
            Serial.print(client.state());
            Serial.println("), reintentando en 5 segundos...");
            display.clearDisplay();
            display.setCursor(0, 0);
            display.print("Conectando a HomeKit");
            display.display();
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
        // ✅ Inicializar el OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  
        Serial.println("⚠️ Error al inicializar el OLED");
        while (true);
    }
    // ✅ Invertir la pantalla horizontalmente
    display.setRotation(2);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 10);
    display.print("Conectando WiFi...");
    display.display();

    setup_wifi();
    Serial.println("\n✅ WiFi Conectado!");
    display.clearDisplay();
    display.setCursor(10, 10);
    display.print("WiFi Conectado!");
    display.display();
    delay(1000);

    // ✅ Configurar NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // Configurar PWM para los LEDs
    ledcAttach(pwmPinWhite, frecuenciaPWM, resolucionPWM);
    ledcAttach(pwmPinYellow, frecuenciaPWM, resolucionPWM);

    pinMode(botonPin, INPUT_PULLUP);
    pinMode(fotoresistorPin, INPUT);
    pinMode(vibracionPin, INPUT);

    actualizarLampara();
    
    display.clearDisplay();
    display.setCursor(10, 10);
    display.print("PWM Activado!");
    display.display();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

void loop() {
    if (!client.connected()) {
        reconnect_mqtt();
    }
    client.loop();
    
    actualizarPantalla();  // Actualizar pantalla con fecha y hora
    actualizarLampara();
    leerSensores();
}
