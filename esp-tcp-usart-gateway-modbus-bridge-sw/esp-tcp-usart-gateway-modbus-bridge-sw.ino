#include <ESP8266WiFi.h>
#include <ModbusTCP.h>
#include <ModbusRTU.h>
#include <SoftwareSerial.h>

// --- RTU Pins
#define RTU_RX_PIN D5
#define RTU_TX_PIN D6
#define STATUS_LED_PIN D4

const char *AP_SSID = "ESP-TCP-Gateway";
const char *AP_PASSWORD = "12345678";

IPAddress LOCAL_IP(192, 168, 0, 108);
IPAddress GATEWAY(192, 168, 0, 1);
IPAddress SUBNET(255, 255, 255, 0);

const unsigned long BLINK_INTERVAL = 1000;

// --- Таймауты и состояния
const unsigned long RTU_RESPONSE_TIMEOUT = 2000; // Таймаут ответа RTU в мс
const unsigned long CLIENT_TIMEOUT = 10000;      // Таймаут неактивности клиента в мс

unsigned long lastTransactionStart = 0;
unsigned long lastClientActivity = 0;
bool isTransactionActive = false;

// --- Modbus
ModbusRTU rtu;
ModbusTCP tcp;

SoftwareSerial modbusSerial(RTU_RX_PIN, RTU_TX_PIN); // RX, TX

IPAddress clientIp = IPAddress(0, 0, 0, 0);
uint16_t transRunning = 0xFFFF; // 0xFFFF как невалидное transactionId
uint16_t lastTransactionId = 0;
uint8_t slaveRunning = 0;

unsigned long lastBlinkTime = 0;
bool ledState = false;

// --- Utility: Print bytes as HEX
void printHex(const char *label, uint8_t *data, size_t len)
{
  Serial.print(label);
  for (size_t i = 0; i < len; i++)
  {
    if (data[i] < 0x10)
      Serial.print('0');
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

// --- Сброс состояния транзакции
void resetTransactionState()
{
  transRunning = 0xFFFF;
  slaveRunning = 0;
  lastTransactionId = 0;
  clientIp = IPAddress(0, 0, 0, 0);
  isTransactionActive = false;
}

// --- Быстрое мигание светодиода 5 раз при старте
void blinkLedOnStartup()
{
  pinMode(STATUS_LED_PIN, OUTPUT);
  for (int i = 0; i < 5; i++)
  {
    digitalWrite(STATUS_LED_PIN, LOW); // LED вкл
    delay(100);
    digitalWrite(STATUS_LED_PIN, HIGH); // LED выкл
    delay(100);
  }
  // Выключаем светодиод после мигания
  digitalWrite(STATUS_LED_PIN, HIGH);
}

// --- TCP request handler
Modbus::ResultCode cbTcpRaw(uint8_t *data, uint8_t len, void *custom)
{
  auto src = (Modbus::frame_arg_t *)custom;
  IPAddress srcIp = IPAddress(src->ipaddr);

  Serial.print("[TCP] Request from ");
  Serial.println(srcIp);
  printHex("       TCP → RTU: ", data, len);

  // Включаем светодиод при приёме любых байт по TCP
  digitalWrite(STATUS_LED_PIN, LOW); // LED включён (LOW - вкл)

  if (clientIp == IPAddress(0, 0, 0, 0) || clientIp == srcIp)
  {
    clientIp = srcIp;
    lastClientActivity = millis();
  }
  else
  {
    Serial.println("[TCP] Ignoring request from another client");
    return Modbus::EX_PASSTHROUGH;
  }

  if (len < 1 || len > 252)
  { // Ограничение Modbus RTU
    Serial.println("[TCP] Invalid request length");
    return Modbus::EX_ILLEGAL_VALUE;
  }

  uint8_t reqSlaveId = src->unitId;

  if (!src->to_server && transRunning == src->transactionId)
  {
    Serial.println("[TCP] Duplicate response received, forwarding to RTU");
    rtu.rawResponce(slaveRunning, data, len);
  }
  else
  {
    slaveRunning = reqSlaveId;
    transRunning = src->transactionId;
    lastTransactionId = src->transactionId;
    lastTransactionStart = millis();
    isTransactionActive = true;

    Serial.print("[RTU] Sending request to slave ");
    Serial.println(slaveRunning);

    bool success = rtu.rawRequest(slaveRunning, data, len);
    if (!success)
    {
      Serial.println("[RTU] Failed to send request");
      resetTransactionState();
      return Modbus::EX_DEVICE_FAILED_TO_RESPOND;
    }
  }

  return Modbus::EX_SUCCESS;
}

// --- RTU response handler
Modbus::ResultCode cbRtuRaw(uint8_t *data, uint8_t len, void *custom)
{
  Serial.println("[RTU] Response received");
  printHex("       RTU → TCP: ", data, len);

  tcp.setTransactionId(lastTransactionId);
  bool sent = tcp.rawResponce(clientIp, data, len, slaveRunning);

  if (!sent)
  {
    Serial.println("[TCP] Failed to send response to client");
    resetTransactionState();
    return Modbus::EX_DEVICE_FAILED_TO_RESPOND;
  }

  Serial.println("[TCP] Response sent to client");

  resetTransactionState();
  return Modbus::EX_SUCCESS;
}

// --- Wi-Fi setup
void setupWiFi()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  Serial.print("[WiFi] Access point started: ");
  Serial.println(AP_SSID);
  Serial.print("[WiFi] IP address: ");
  Serial.println(WiFi.softAPIP());
}

// --- setup()
void setup()
{
  Serial.begin(115200);

  // Быстрое мигание светодиода при старте
  blinkLedOnStartup();

  Serial.println("\n[BOOT] Starting Modbus TCP ↔ RTU Gateway");

  setupWiFi();

  tcp.server();
  tcp.onRaw(cbTcpRaw);

  modbusSerial.begin(19200); // Подстройте скорость под ваше устройство
  rtu.begin(&modbusSerial);
  rtu.onRaw(cbRtuRaw);

  resetTransactionState();

  Serial.println("[INIT] Gateway initialized and ready");
}

// --- loop()
void loop()
{
  rtu.task();
  tcp.task();

  // Таймаут ожидания ответа RTU
  if (isTransactionActive && millis() - lastTransactionStart >= RTU_RESPONSE_TIMEOUT)
  {
    Serial.println("[TIMEOUT] No RTU response received in time");
    resetTransactionState();
  }

  // Таймаут клиента
  if (millis() - lastClientActivity >= CLIENT_TIMEOUT)
  {
    //  Serial.println("[TIMEOUT] Client inactive, resetting");
    digitalWrite(STATUS_LED_PIN, HIGH);
  }

  yield();
}
