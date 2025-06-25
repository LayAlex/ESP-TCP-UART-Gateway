#include <ESP8266WiFi.h>
#include <ModbusTCP.h>
#include <ModbusRTU.h>

// --- Константы и настройки
#define STATUS_LED_PIN D4

const char *AP_SSID = "ESP-TCP-Gateway";
const char *AP_PASSWORD = "12345678";

IPAddress LOCAL_IP(192, 168, 0, 108);
IPAddress GATEWAY(192, 168, 0, 1);
IPAddress SUBNET(255, 255, 255, 0);

const unsigned long BLINK_INTERVAL = 1000;

// --- Modbus переменные
ModbusRTU rtu;
ModbusTCP tcp;

IPAddress clientIp;
uint16_t transRunning = 0;
uint16_t lastTransactionId = 0;
uint8_t slaveRunning = 0;

unsigned long lastBlinkTime = 0;
bool ledState = false;

// --- Обработка входящего TCP запроса ---
Modbus::ResultCode cbTcpRaw(uint8_t *data, uint8_t len, void *custom)
{
  auto src = (Modbus::frame_arg_t *)custom;
  IPAddress srcIp = IPAddress(src->ipaddr);

  if (clientIp == (uint32_t)0)
  {
    clientIp = srcIp;
  }
  else if (clientIp != srcIp)
  {
    return Modbus::EX_PASSTHROUGH;
  }

  if (len < 1)
  {
    return Modbus::EX_PASSTHROUGH;
  }

  uint8_t reqSlaveId = src->unitId;

  if (!src->to_server && transRunning == src->transactionId)
  {
    // RTU ответ пришел, пробрасываем назад
    rtu.rawResponce(slaveRunning, data, len);
  }
  else
  {
    slaveRunning = reqSlaveId;
    transRunning = src->transactionId;
    lastTransactionId = src->transactionId;

    // Отправляем запрос в RTU
    rtu.rawRequest(slaveRunning, data, len);
  }

  return Modbus::EX_SUCCESS;
}

// --- Обработка ответа от RTU ---
Modbus::ResultCode cbRtuRaw(uint8_t *data, uint8_t len, void *custom)
{
  // Устанавливаем идентификатор транзакции перед отправкой TCP ответа
  tcp.setTransactionId(lastTransactionId);

  // Отправляем TCP-ответ
  bool sent = tcp.rawResponce(clientIp, data, len, slaveRunning);
  if (!sent)
  {
    return Modbus::EX_DEVICE_FAILED_TO_RESPOND;
  }

  // Сброс состояния
  transRunning = 0;
  slaveRunning = 0;
  lastTransactionId = 0;

  return Modbus::EX_SUCCESS;
}

// --- Настройка точки доступа WiFi ---
void setupWiFi()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
}

// --- Индикация подключения ---
void updateLedStatus(bool clientConnected)
{
  unsigned long now = millis();
  if (clientConnected)
  {
    digitalWrite(STATUS_LED_PIN, LOW);
  }
  else
  {
    if (now - lastBlinkTime >= BLINK_INTERVAL)
    {
      lastBlinkTime = now;
      ledState = !ledState;
      digitalWrite(STATUS_LED_PIN, ledState ? LOW : HIGH);
    }
  }
}

// --- setup() ---
void setup()
{
  Serial.begin(115200);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); // LED off initially

  setupWiFi();

  tcp.server();
  tcp.onRaw(cbTcpRaw);

  rtu.begin(&Serial);
  rtu.onRaw(cbRtuRaw);
}

// --- loop() ---
void loop()
{
  rtu.task();
  tcp.task();

  updateLedStatus(transRunning > 0);
  yield();
}
