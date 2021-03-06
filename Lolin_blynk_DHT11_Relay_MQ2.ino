/*****************************
  = Система умный дом v.1.4 =
  Полочкин Александр Андреевич
  sunches@yandex.ru
******************************
Пины Blynk
V0 - Терминал
V1 - Поддержание температуры
V2 - Для отправки значения температуры
V3 - Для отправки значения влажности
V4 - Сигнализация 
V5 - Для отправки значения примесей в воздухе
******************************/

/* TODO

*/

#define BLYNK_PRINT Serial
#define PIN_DHT 16   // Подключаем DHT к D0 = GPIO16 - Зелёный
#define PIN_PIR 5    // Подключаем датчик движения к D1 = GPIO5 - Оранжевый
#define PIN_WDT 14   // Подключаем сторожевой таймер к D5 = GPIO14 -
#define PIN_RELAY 15 // Подключаем реле к D8 = GPIO15 -
#define PIN_MQ2 A0   // Подключаем MQ2 к A0 - Жёлтый
#define NUMBER_OF_SSID 4   // Количество точек доступа
#define MAX_TEMPERATURE 30 // Максимальная температура в комнате

#define PIR_DELAY 20000 // Задержка срабатывания датчика движения 20 сек
#define OVERHEATING_DELAY 300000 // Задержка оповещения от перегреве 5 мин

#define EMAIL "arhiv.foto2017@yandex.ru" // Почта для отправки сообщений

// Для WiFi
#include <ESP8266WiFi.h>
// Для Blynk
#include <BlynkSimpleEsp8266.h>
// Для функций времени
#include <TimeLib.h>
#include <WidgetRTC.h>
// Для датчика температуры
#include "DHTesp.h"
//Для OTA
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Secrets.h> // Пароли

/*
char auth[] = "*********************************";  //Ключ авторизации Blynk

// Варианты сетей
char* net[NUMBER_OF_SSID] = {"AlexP", "FreeWifi", "MGTS_GPON_3107", "Pressa"};  // Названия сетей
char* pass[NUMBER_OF_SSID] = {"***********", "**********", "*********", "**********"};  // Пароли к сетям
*/

float t; // Температура -5.0 - 50.0
uint8_t h; // Влажность 0 - 100
uint16_t gas; // Газ 0 - 1024

uint8_t pinV1; // Виртуальный пин для опорной температуры
bool secureOn; // Флаг включения сигнализации

bool relayOn = false; // Устанавливаем флаг состояния реле в ОТКЛ

uint32_t detectTime = 0; // Время обнаружения движения
uint32_t overheatingTime = 0; // Время обнаружения перегрева

DHTesp dht; // Создаем объект класса DHTesp - получение температуры и влажности с DHT

WidgetTerminal terminal(V0); // Создаем объект класса WidgetTerminal - вывод сообщений в терминал телефона

BlynkTimer timer; // Создаем объект класса BlynkTimer - запуск процессов по таймеру

WidgetRTC rtc; // Создаем объект класса WidgetRTC - получение точного времени с сервера

void setup() // Действия при запуске ESP8266
{
  pinMode(PIN_WDT, OUTPUT);
  digitalWrite(PIN_WDT, LOW);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW); // выключаем реле

  Serial.begin(115200);

  controlWiFi(); // Подключаемся к Wifi и серверу Blynk

  dht.setup(PIN_DHT, DHTesp::DHT11);

  // Главный цикл выполнения каждую третью секунду
  timer.setInterval(5000L, mainLoop);

  // Проверяем WiFi и пробуем подключится, если нет соединения каждые 10 секунд
  timer.setInterval(10000L, controlWiFi);

  // Синхронизация времени раз в 10 мин
  setSyncInterval(10 * 60);

  // Перезагрузка каждый час
  timer.setInterval(3600000L, Reset);

  // Обновление по воздуху
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();

  terminal.println("Система мониторинга и управления запущена");
  terminal.flush();
}

BLYNK_CONNECTED() {
  //Blynk.syncAll();

  Blynk.syncVirtual(V1, V4); // Получаем с сервера состояние

  //terminal.println("3 2 1 ...");
  Blynk.email(EMAIL, "Кондрово", getDateTime() + " Умный дом запущен");

  if (digitalRead(PIN_RELAY)) relayOn = false;
  else relayOn = true;

  controlTemp();

  rtc.begin();
}

BLYNK_WRITE(V1) // Пороговая температура
{
  pinV1 = param.asInt();

  if (pinV1 != 0)
  {
    Serial.print("Держим температуру: ");
    Serial.println(pinV1);

    terminal.print("\nДержим температуру: ");
    terminal.println(pinV1);
  }
  terminal.flush();

  controlTemp();
}

BLYNK_WRITE(V4) // Включение сигнализации
{
  secureOn = param.asInt();

  if (secureOn)
  {
    Serial.println("Сигнализация ВКЛючена");
    terminal.println("\nСигнализация ВКЛючена");
    terminal.flush();
  }
  else
  {
    Serial.println("Сигнализация ОТКЛючена");
    terminal.println("\nСигнализация ОТКЛючена");
    terminal.flush();
  }
}

String getDateTime() // Определяем дату и время
{
  return (String(day()) + "." + month() + "." + year() + " " + hour() + ":" + minute() + ":" + second());
}

void(* resetFunc) (void) = 0; // Объявляем функцию reset

void Reset(){
  resetFunc();
}

void mainLoop() { // Главный цикл
  Send();
  controlPIR();
  controlTemp();
  watchdog();
}

void Send() {// Замеряем климат и отправляем в Blynk
  t = dht.getTemperature();
  h = dht.getHumidity();
  gas = analogRead(PIN_MQ2);

  Serial.print("t = ");
  Serial.print(t);
  Serial.print(", h = ");
  Serial.print(h);
  Serial.print(", gas = ");
  Serial.println(gas);

  Blynk.virtualWrite(V2, t);
  Blynk.virtualWrite(V3, h);
  Blynk.virtualWrite(V5, gas);

  terminal.print('.');
  terminal.flush();
}

void controlPIR() { // Контроль присутствия
  if (secureOn and (digitalRead(PIN_PIR)) and ((millis() - detectTime) > PIR_DELAY)) {
    detectTime = millis(); // записываем время обнаружения движения

    Blynk.notify("Обнаружено движение!");

    Blynk.email(EMAIL, "Кондрово", getDateTime() + " - Обнаружено движение!");

    Serial.println("Обнаружено движение!");
    terminal.println("\n" + getDateTime() + " - Обнаружено движение!");
    terminal.flush();
  }
}

void controlTemp() { // Управление температурой
  if (t >= MAX_TEMPERATURE) // При достижении MAX_TEMPERATURE принудительно отключать обогреватель
  {
    relayOn = false;
    digitalWrite(PIN_RELAY, LOW); // выключаем реле
    if ((millis() - overheatingTime) > OVERHEATING_DELAY)
    {
      overheatingTime = millis();
      Blynk.email(EMAIL, "Кондрово", getDateTime() + " Перегрев: Конвектор ОТКЛючен");
      terminal.println("\nПерегрев: Конвектор ОТКЛючен");
    }
  }
    else
  {
    if ((pinV1 == 0) and (relayOn))
    {
      relayOn = false;
      digitalWrite(PIN_RELAY, LOW); // выключаем реле
      Blynk.email(EMAIL, "Кондрово", getDateTime() + " Конвектор ОТКЛючен");
      terminal.println("\nКонвектор ОТКЛючен");
    }

    if (pinV1 > 0) // Если Поддержание включено
    {
      if ((t > (pinV1 + 1)) and (relayOn))
      {
        relayOn = false;
        digitalWrite(PIN_RELAY, LOW); // Если перегрев выключаем реле
        Blynk.email(EMAIL, "Кондрово", getDateTime() + " Конвектор ОТКЛючен");
        terminal.println("\nКонвектор ОТКЛючен");
      }

      if ((t < pinV1) and (!relayOn))
      {
        relayOn = true;
        digitalWrite(PIN_RELAY, HIGH); // Если недогрев включаем реле
        Blynk.email(EMAIL, "Кондрово", getDateTime() + " Конвектор ВКЛючен");
        terminal.println("\nКонвектор ВКЛючен");
      }
    }
  }

  Serial.println("-----------------------------------------");
  Serial.print("pinV1=");
  Serial.print(pinV1);
  Serial.print(" relayOn=");
  Serial.println(relayOn);
}

void controlWiFi() // Управление соединением
{
  if (WiFi.status() != WL_CONNECTED)
  {
    // Перебираем знакомые сети и подключаемся
    int i = 0;
    while (!Blynk.begin(auth, net[i], pass[i]))
    {
      (i == NUMBER_OF_SSID - 1) ? i = 0 : i++;
    }
  }
}

// Формируем импульс на входе сторожевого таймера
void watchdog()
{
    Serial.println("Формируем импульс на Watchdog");
    digitalWrite(PIN_WDT, HIGH);
    digitalWrite(PIN_WDT, LOW);
}

void loop()
{
  ArduinoOTA.handle(); // Обновление по воздуху
  Blynk.run(); // Запуск Blynk
  timer.run(); // Запуск таймера
}