#include <ESP8266WiFi.h>
#include <Firebase.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Configuração Wi-Fi e Firebase
#define FIREBASE_HOST "https://controle-fermentacao-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH ""
const char *ssid = "free";
const char *password = "Beer@2019";

// Pinos e instâncias
#define ONE_WIRE_BUS D4 // Pino do sensor DS18B20
#define RELAY_PIN D5    // Pino do módulo relé
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

// Configuração do NTP para obter o horário
WiFiUDP ntpUDP;
const long utcOffsetInSeconds = -3 * 3600;
NTPClient timeClient(ntpUDP, "a.st1.ntp.br", utcOffsetInSeconds, 60000);

// Variáveis de controle de temperatura
float targetTemp = 18.0;
float tempHysteresis = 0.5;
unsigned long lastRelaySwitch = 0;
unsigned long relayDelay = 300000;
bool relayOn = false;

// Timer para envio ao Firebase
unsigned long lastFirebaseUpdate = 0;
const unsigned long firebaseInterval = 300000; // Atualização a cada 5 minuto

// Declarações de funções
void updateFirebase();
void updateFirebaseRelay();
void getFirebaseSettings();
void updateFirebaseIfNeeded();
void controlTemperature();

void setup()
{
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    
    Serial.println("Connected to WiFi");

    sensors.begin();
    timeClient.begin();

    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); // Relé desligado inicialmente

    getFirebaseSettings();
    updateFirebase();
}

void loop()
{
    controlTemperature();
    updateFirebaseIfNeeded();
}

void updateFirebase()
{
    sensors.requestTemperatures();
    float currentTemp = round(sensors.getTempCByIndex(0) * 10.0) / 10.0;

    if (!timeClient.update())
    {
        Serial.println("Falha ao sincronizar com o servidor NTP.");
        timeClient.forceUpdate();
    }

    unsigned long timestamp = timeClient.getEpochTime();

    FirebaseJson json;
    json.set("timestamp", timestamp);
    json.set("temperature", currentTemp);
    json.set("relayOn", relayOn);

    Firebase.pushJSON(firebaseData, "/records", json);
    Firebase.setFloat(firebaseData, "/config/currentTemp", currentTemp);

    lastFirebaseUpdate = millis();

    Serial.println("Dados enviados ao Firebase!");
}

void updateFirebaseRelay()
{
    Firebase.setBool(firebaseData, "/config/relayOn", relayOn);
}

void getFirebaseSettings()
{
    if (Firebase.getFloat(firebaseData, "/config/targetTemp"))
    {
        targetTemp = firebaseData.floatData();
    }

    if (Firebase.getFloat(firebaseData, "/config/tempHysteresis"))
    {
        tempHysteresis = firebaseData.floatData();
    }

    if (Firebase.getFloat(firebaseData, "/config/relayDelay"))
    {
        relayDelay = firebaseData.floatData();
    }

    Serial.println("Configurações carregadas do Firebase!");
}

void updateFirebaseIfNeeded()
{
    if (millis() - lastFirebaseUpdate >= firebaseInterval)
    {
        updateFirebase();
        getFirebaseSettings();
    }
}

void controlTemperature()
{
    if (millis() - lastRelaySwitch > relayDelay)
    {
        sensors.requestTemperatures();
        float currentTemp = sensors.getTempCByIndex(0);

        if (currentTemp > targetTemp + tempHysteresis && !relayOn)
        {
            // Liga a geladeira
            digitalWrite(RELAY_PIN, HIGH); 
            relayOn = true;
            updateFirebaseRelay();
            lastRelaySwitch = millis();
            Serial.println("Geladeira ligada!");
        }
        else if (currentTemp < targetTemp && relayOn)
        {
            // Desliga a geladeira
            digitalWrite(RELAY_PIN, LOW); 
            relayOn = false;
            updateFirebaseRelay();
            lastRelaySwitch = millis();
            Serial.println("Geladeira desligada!");
        }
    }
}
