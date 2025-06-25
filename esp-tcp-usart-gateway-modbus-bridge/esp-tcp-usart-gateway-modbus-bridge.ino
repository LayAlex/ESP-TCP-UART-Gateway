#include <ESP8266WiFi.h>
#include <ModbusTCP.h>
#include <ModbusRTU.h>

// Пины и параметры
#define STATUS_LED_PIN D4 // Встроенный светодиод (инверсный)
const char *AP_SSID = "ESP-TCP-Gateway";
const char *AP_PASSWORD = "12345678";

IPAddress LOCAL_IP(192, 168, 0, 108);
IPAddress GATEWAY(192, 168, 0, 1);
IPAddress SUBNET(255, 255, 255, 0);

const unsigned long BLINK_INTERVAL = 1000; // Для индикации при отсутствии клиента

ModbusRTU rtu;
ModbusTCP tcp;

IPAddress clientIp; // IP единственного TCP клиента

uint16_t transRunning = 0; // ID текущей транзакции Modbus TCP
uint8_t slaveRunning = 0;  // Текущий slave ID запроса

unsigned long lastBlinkTime = 0;
bool ledState = false;


Modbus::ResultCode cbTcpRaw(uint8_t *data, uint8_t len, void *custom)
{
  auto src = (Modbus::frame_arg_t *)custom;
  IPAddress srcIp = IPAddress(src->ipaddr);

  Serial.print("cbTcpRaw called. len=");
  Serial.println(len);
  Serial.print("From IP: ");
  Serial.println(srcIp);

  Serial.print("Data bytes: ");
  for (int i = 0; i < len; i++)
  {
    Serial.printf("%02X ", data[i]);
  }
  Serial.println();

  if (clientIp == (uint32_t)0)
  {
    clientIp = srcIp;
    Serial.print("New client connected: ");
    Serial.println(clientIp);
  }
  else if (clientIp != srcIp)
  {
    Serial.println("Different client tried to connect - passthrough");
    return Modbus::EX_PASSTHROUGH;
  }

  if (len < 1)
  {
    Serial.println("Data length < 1 - passthrough");
    return Modbus::EX_PASSTHROUGH;
  }

  // Здесь берём slave id из структуры src, а не из data[0]
  uint8_t reqSlaveId = src->unitId;
  Serial.print("Requested Slave ID: ");
  Serial.println(reqSlaveId);

  if (!src->to_server && transRunning == src->transactionId)
  {
    Serial.println("Response from RTU received, forwarding to TCP client");
    rtu.rawResponce(slaveRunning, data, len);
  }
  else
  {
    Serial.println("New Modbus TCP request, forwarding to RTU");
    slaveRunning = reqSlaveId;

    transRunning = src->transactionId;

    rtu.rawRequest(slaveRunning, data, len);
  }

  return Modbus::EX_SUCCESS;
}



// --- Callback: обработка входящих данных RTU (Modbus RTU ответ) ---
Modbus::ResultCode cbRtuRaw(uint8_t *data, uint8_t len, void *custom)
{
  auto src = (Modbus::frame_arg_t *)custom;

  if (clientIp == (uint32_t)0)
  {

    return Modbus::EX_DEVICE_FAILED_TO_RESPOND;
  }

  // Отправляем ответ Modbus TCP клиенту
  if (!tcp.isConnected(clientIp))
  {
    if (!tcp.connect(clientIp))
    {

      rtu.errorResponce(src->slaveId, (Modbus::FunctionCode)data[0], Modbus::EX_DEVICE_FAILED_TO_RESPOND);
      return Modbus::EX_DEVICE_FAILED_TO_RESPOND;
    }
  }

  // Копируем данные в буфер
  uint8_t data_buf[256];
  if (len > sizeof(data_buf))
  {

    return Modbus::EX_PASSTHROUGH;
  }
  memcpy(data_buf, data, len);

  // Отправляем ответ TCP клиенту (rawRequest используется и для ответов)
  transRunning = tcp.rawRequest(
      clientIp, data_buf, len,
      [](Modbus::ResultCode event, uint16_t transactionId, void *) -> bool
      {
        if (event != Modbus::EX_SUCCESS)
        {
        }
        return true;
      },
      MODBUSIP_UNIT);

  slaveRunning = 0; // Сбрасываем
  return Modbus::EX_SUCCESS;
}

// --- Setup WiFi AP ---
void setupWiFi()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
}

// --- LED update ---
void updateLedStatus(bool clientConnected)
{
  unsigned long now = millis();
  if (clientConnected)
  {
    digitalWrite(STATUS_LED_PIN, LOW); // Светодиод включен (инверсный)
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

void setup()
{
  Serial.begin(115200);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); // Выключен

  setupWiFi();

  tcp.server();
  tcp.onRaw(cbTcpRaw);

  rtu.begin(&Serial);  
  rtu.onRaw(cbRtuRaw);
}

void loop()
{
  rtu.task();
  tcp.task();

  updateLedStatus(transRunning > 0);

  yield();
}
