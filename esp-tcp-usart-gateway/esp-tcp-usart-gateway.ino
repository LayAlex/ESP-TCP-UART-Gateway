#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>

#define STATUS_LED_PIN D4 // Встроенный светодиод (обычно инверсный: LOW = включен)

const char *AP_SSID = "ESP-TCP-Gateway";
const char *AP_PASSWORD = "12345678";
const IPAddress LOCAL_IP(192, 168, 0, 108);
const IPAddress GATEWAY(192, 168, 0, 1);
const IPAddress SUBNET(255, 255, 255, 0);
const uint16_t TCP_PORT = 502;

const unsigned long CLIENT_TIMEOUT = 30000;
const unsigned long RECONNECT_DELAY = 1000;

WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpClient;

unsigned long lastActivityTime = 0;
bool clientConnected = false;

// Переменные для мигания
unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 1000;
bool ledState = false;

// Импульс активности
bool activityPulse = false;
unsigned long activityPulseStart = 0;
const unsigned long PULSE_DURATION = 100; // длительность импульса

void setup()
{
  Serial.begin(115200);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); // Светодиод выключен (инверсный логике)

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  tcpServer.begin();
  tcpServer.setNoDelay(true);
}

void loop()
{
  handleTcpClient();
  updateLedStatus();
}

void updateLedStatus()
{
  if (clientConnected)
  {
    if (activityPulse)
    {
      // Светодиод коротко выключен
      digitalWrite(STATUS_LED_PIN, HIGH); // выключен
      if (millis() - activityPulseStart >= PULSE_DURATION)
      {
        activityPulse = false;
        digitalWrite(STATUS_LED_PIN, LOW); // обратно включен
      }
    }
    else
    {
      // Постоянно включён (инверсный — значит LOW)
      digitalWrite(STATUS_LED_PIN, LOW);
    }
  }
  else
  {
    if (millis() - lastBlinkTime >= BLINK_INTERVAL)
    {
      lastBlinkTime = millis();
      ledState = !ledState;
      digitalWrite(STATUS_LED_PIN, ledState ? LOW : HIGH); // мигаем (LOW = включен)
    }
  }
}

void handleTcpClient()
{
  if (!tcpClient || !tcpClient.connected())
  {
    if (clientConnected)
    {
      tcpClient.stop();
      clientConnected = false;
    }

    static unsigned long lastConnectAttempt = 0;
    if (millis() - lastConnectAttempt > RECONNECT_DELAY)
    {
      lastConnectAttempt = millis();
      WiFiClient newClient = tcpServer.available();

      if (newClient)
      {
        if (tcpClient)
          tcpClient.stop();

        tcpClient = newClient;
        tcpClient.setNoDelay(true);
        tcpClient.setTimeout(100);
        clientConnected = true;
        lastActivityTime = millis();
      }
    }
  }
  else
  {
    bool hadExchange = false;

    if (tcpClient.available())
    {
      uint8_t buffer[64];
      size_t len = tcpClient.read(buffer, sizeof(buffer));
      if (len > 0)
      {
        while (Serial.available())
        {
          Serial.read();
        }
        Serial.write(buffer, len);
        lastActivityTime = millis();
        hadExchange = true;
      }
    }

    uint8_t buffer[64];
    size_t len = 0;
    while (Serial.available() && len < sizeof(buffer))
    {
      buffer[len++] = Serial.read();
    }
    if (len > 0)
    {
      if (tcpClient.connected())
      {
        tcpClient.write(buffer, len);
      }
      lastActivityTime = millis();
      hadExchange = true;
    }

    if (hadExchange)
    {
      activityPulse = true;
      activityPulseStart = millis();
    }

    if (!hadExchange && millis() - lastActivityTime > CLIENT_TIMEOUT)
    {
      tcpClient.stop();
      clientConnected = false;
    }
  }
}
