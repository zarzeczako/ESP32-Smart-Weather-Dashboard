// === 1. NAJPIERW RDZEŃ I INTERNET (Bezkolizyjne ładowanie) ===
#include <FS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// === 2. POTEM GRAFIKA I DOTYK ===
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// === 3. NA KOŃCU CZUJNIKI ===
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h> 

// ================= USTAWIENIA SIECI =================
const char* ssid = "CHANGE_ME";
const char* password = "CHANGE_ME";
const char* timezone = "Europe/Warsaw";

// ================= ZMIENNE GLOBALNE =================
String btc_price = "Czekam...";
String usd_price = "Czekam...";
String eur_price = "Czekam...";
String gbp_price = "Czekam...";

String current_date = "Laczenie...";
String current_time = "Laczenie...";
String current_temp = "Czekam...";
String current_press = "Czekam...";
String current_light = "Czekam...";
String current_light_state = "Analiza..."; // Stan oświetlenia
String current_verdict = "Analiza...";
bool time_synchronized = false;

// Uruchamiamy serwer lokalny
WebServer server(80);

// ================= USTAWIENIA CZUJNIKÓW =================
#define I2C_SDA 27
#define I2C_SCL 22
TwoWire I2CBME = TwoWire(0);
Adafruit_BMP280 bme(&I2CBME); 

#define LDR_PIN 34 // Pin fotorezystora

// ================= USTAWIENIA DOTYKU I EKRANU =================
#define XPT2046_IRQ 36   
#define XPT2046_MOSI 32  
#define XPT2046_MISO 39  
#define XPT2046_CLK 25   
#define XPT2046_CS 33    

SPIClass touchscreenSPI = SPIClass(HSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS);

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
int x, y, z;
lv_color_t theme_color = lv_color_hex(0x2196f3);
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

// ================= LOGIKA SERWERA WWW =================
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Smart Dashboard ESP32</title>";
  html += "<style>";
  html += "body { background: #121212; color: #ffffff; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; text-align: center; margin: 0; padding: 20px; }";
  html += ".container { max-width: 450px; margin: auto; background: #1e1e1e; padding: 25px; border-radius: 15px; box-shadow: 0 10px 25px rgba(0,0,0,0.7); }";
  html += "h1 { color: #2196f3; font-size: 28px; margin-bottom: 5px; }";
  html += "h2 { color: #888; font-size: 14px; font-weight: normal; margin-top: 0; margin-bottom: 25px; }";
  html += ".sensor-box { background: #2a2a2a; padding: 15px; border-radius: 10px; margin-bottom: 15px; }";
  html += ".val { font-size: 26px; font-weight: bold; color: #4caf50; display: block; margin-top: 5px; }";
  html += ".alert { background: #332b00; color: #ffca28; font-size: 18px; font-weight: bold; padding: 15px; border-radius: 10px; border: 1px solid #ffca28; margin-bottom: 20px; }";
  html += ".crypto-box { background: #1a242f; border: 1px solid #2c3e50; padding: 15px; border-radius: 10px; }";
  html += ".footer { margin-top: 25px; font-size: 12px; color: #666; }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>Inteligentna Stacja IoT</h1>";
  html += "<h2>Zbudowano na ESP32</h2>";
  
  html += "<div class='alert'>System radzi: " + current_verdict + "</div>";
  
  html += "<div class='sensor-box'>Temperatura<span class='val'>" + current_temp + "</span></div>";
  html += "<div class='sensor-box'>Ciśnienie Atmosferyczne<span class='val'>" + current_press + "</span></div>";
  html += "<div class='sensor-box'>Natężenie Światła<span class='val'>" + current_light + "</span></div>";
  html += "<div class='sensor-box'>Stan Światła<span class='val'>" + current_light_state + "</span></div>"; // Podmienione z wilgotności!
  
  html += "<div class='crypto-box'>";
  html += "<h3 style='color:#f39c12; margin-top:0;'>Globalne Rynki</h3>";
  html += "<p style='font-size:22px; font-weight:bold; margin:10px 0;'>Bitcoin: " + btc_price + "</p>";
  html += "<p style='margin:5px 0;'>Dolar: " + usd_price + " | Euro: " + eur_price + "</p>";
  html += "<p style='margin:5px 0;'>Funt: " + gbp_price + "</p>";
  html += "</div>";
  
  html += "<div class='footer'>Ostatnia synchronizacja: " + current_date + " | " + current_time + "</div>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

// ================= LVGL I EKRAN =================
void log_print(lv_log_level_t level, const char * buf) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}

void touchscreen_read(lv_indev_t * indev, lv_indev_data_t * data) {
  if(touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();
    if (p.z < 100 || p.z > 4000) {
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
    x = map(p.y, 240, 3800, 1, 320);
    y = map(p.x, 200, 3700, 1, 240);
    x = constrain(x, 0, 319);
    y = constrain(y, 0, 239);
    
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  }
  else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static void reset_button_event_cb(lv_event_t * e) {
  Serial.println("Przycisk kliknięty. Odświeżam system...");
  update_table_values();
}

static lv_obj_t * table;

// ================= ZBIERANIE DANYCH =================
void get_date_and_time() {
  if (WiFi.status() != WL_CONNECTED) {
    current_date = "Brak WiFi";
    current_time = "Brak WiFi";
    return;
  }

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo, 5)){
    current_date = "Synch. NTP...";
    current_time = "Synch. NTP...";
    return;
  }
  
  char char_date[20];
  char char_time[20];
  strftime(char_date, sizeof(char_date), "%Y-%m-%d", &timeinfo);
  strftime(char_time, sizeof(char_time), "%H:%M:%S", &timeinfo);
  
  current_date = String(char_date);
  current_time = String(char_time);
}

void get_bitcoin_price() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://api.binance.com/api/v3/ticker/price?symbol=BTCUSDT");
    int httpCode = http.GET();
    
    if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);
      const char* price = doc["price"];
      btc_price = "$" + String(atof(price), 0); 
    }
    http.end();
  } else {
    btc_price = "Brak WiFi";
  }
}

void get_currency_rates() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://open.er-api.com/v6/latest/PLN");
    int httpCode = http.GET();
    
    if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);
      
      float usd_rate = doc["rates"]["USD"];
      float eur_rate = doc["rates"]["EUR"];
      float gbp_rate = doc["rates"]["GBP"];
      
      if(usd_rate > 0) usd_price = String(1.0 / usd_rate, 2) + " PLN";
      if(eur_rate > 0) eur_price = String(1.0 / eur_rate, 2) + " PLN";
      if(gbp_rate > 0) gbp_price = String(1.0 / gbp_rate, 2) + " PLN";
    }
    http.end();
  } else {
    usd_price = "Brak WiFi";
    eur_price = "Brak WiFi";
    gbp_price = "Brak WiFi";
  }
}

static void update_table_values(void) {
  // 1. Zbieranie z czujników
  float bme_temp = bme.readTemperature();
  
// Przeliczanie fotorezystora
  int ldr_raw = analogRead(LDR_PIN);
  // Odwracanie logiki dzielnika i zawężamy zakres z 0-4095 do realnych wskazań płytki 
  // (mrok: 1500=0%, max światło: 200=100%)
  
  int light_percent = map(ldr_raw, 1500, 200, 0, 100);
  light_percent = constrain(light_percent, 0, 100); 
  current_light = String(light_percent) + " %";

  // Inteligentna ocena oświetlenia (NOWOŚĆ)
  if (light_percent < 10) current_light_state = "Ciemno (Noc)";
  else if (light_percent >= 10 && light_percent < 40) current_light_state = "Szarowka";
  else if (light_percent >= 40 && light_percent < 80) current_light_state = "Dobre oswietlenie";
  else current_light_state = "Oslepiajaco!";

  // Kolorystyka
  if (bme_temp < 10) theme_color = lv_palette_main(LV_PALETTE_BLUE);
  else if (bme_temp >= 10 && bme_temp < 20) theme_color = lv_palette_main(LV_PALETTE_CYAN);
  else if (bme_temp >= 20 && bme_temp < 25) theme_color = lv_palette_main(LV_PALETTE_GREEN);
  else if (bme_temp >= 25 && bme_temp < 30) theme_color = lv_palette_main(LV_PALETTE_ORANGE);
  else theme_color = lv_palette_main(LV_PALETTE_RED);
  
  current_temp = String(bme_temp) + "\u00B0C";
  current_press = String(bme.readPressure() / 100.0F) + " hPa";
  
  if (bme_temp < 0) current_verdict = "Mroz! Zamykaj okna.";
  else if (bme_temp >= 0 && bme_temp < 15) current_verdict = "Chlodno. Zamknij okno!";
  else if (bme_temp >= 15 && bme_temp < 25) current_verdict = "Super pogoda! Nie otwieraj okien!";
  else if (bme_temp >= 25 && bme_temp < 30) current_verdict = "Cieplo. Otworz okno, byleby ostroznie!";
  else current_verdict = "Upal! Wywal okno z zawiasow.";

  String rssi_value = String(WiFi.RSSI()) + " dBm";
  String ram_value = String(ESP.getFreeHeap() / 1024) + " KB";
  
  unsigned long uptime_sec = millis() / 1000;
  String uptime_str = String(uptime_sec / 60) + "m " + String(uptime_sec % 60) + "s";
  
  get_bitcoin_price();
  get_currency_rates();
  get_date_and_time();

  // ================= USZEREGOWANA TABELA =================
  // Główny nagłówek
  lv_table_set_cell_value(table, 0, 0, "Parametr");
  lv_table_set_cell_value(table, 0, 1, "Wartosc");

  // Blok 1: Czas i Data
  lv_table_set_cell_value(table, 1, 0, "Data");
  lv_table_set_cell_value(table, 1, 1, current_date.c_str());
  lv_table_set_cell_value(table, 2, 0, "Czas");
  lv_table_set_cell_value(table, 2, 1, current_time.c_str());

  // Blok 2: Środowisko (Czujniki)
  lv_table_set_cell_value(table, 3, 0, "Temperatura");
  lv_table_set_cell_value(table, 3, 1, current_temp.c_str());
  lv_table_set_cell_value(table, 4, 0, "Cisnienie");
  lv_table_set_cell_value(table, 4, 1, current_press.c_str());
  lv_table_set_cell_value(table, 5, 0, "Swiatlosc");
  lv_table_set_cell_value(table, 5, 1, current_light.c_str());
  lv_table_set_cell_value(table, 6, 0, "Stan Swiatla"); // Nowy element zamiast wilgotności
  lv_table_set_cell_value(table, 6, 1, current_light_state.c_str());
  lv_table_set_cell_value(table, 7, 0, "Werdykt");
  lv_table_set_cell_value(table, 7, 1, current_verdict.c_str());

  // Blok 3: Giełda i Finanse
  lv_table_set_cell_value(table, 8, 0, "Bitcoin (BTC)");
  lv_table_set_cell_value(table, 8, 1, btc_price.c_str());
  lv_table_set_cell_value(table, 9, 0, "Dolar (USD)");
  lv_table_set_cell_value(table, 9, 1, usd_price.c_str());
  lv_table_set_cell_value(table, 10, 0, "Euro (EUR)");
  lv_table_set_cell_value(table, 10, 1, eur_price.c_str());
  lv_table_set_cell_value(table, 11, 0, "Funt (GBP)");
  lv_table_set_cell_value(table, 11, 1, gbp_price.c_str());

  // Blok 4: Telemetria Urządzenia
  lv_table_set_cell_value(table, 12, 0, "Sygnal WiFi");
  lv_table_set_cell_value(table, 12, 1, rssi_value.c_str());
  lv_table_set_cell_value(table, 13, 0, "Adres IP");
  lv_table_set_cell_value(table, 13, 1, WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "Brak");
  lv_table_set_cell_value(table, 14, 0, "Wolny RAM");
  lv_table_set_cell_value(table, 14, 1, ram_value.c_str());
  lv_table_set_cell_value(table, 15, 0, "Czas Pracy");
  lv_table_set_cell_value(table, 15, 1, uptime_str.c_str());
  
  lv_obj_invalidate(table); // Odświeża kolory na żywo
}

static void draw_event_cb(lv_event_t * e) {
  lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
  lv_draw_dsc_base_t * base_dsc = (lv_draw_dsc_base_t*) draw_task->draw_dsc;
  
  if(base_dsc->part == LV_PART_ITEMS) {
    uint32_t row = base_dsc->id1;
    uint32_t col = base_dsc->id2;

    if(row == 0) { // Tylko nagłówek "Parametr | Wartość"
      lv_draw_label_dsc_t * label_draw_dsc = lv_draw_task_get_label_dsc(draw_task);
      if(label_draw_dsc) label_draw_dsc->align = LV_TEXT_ALIGN_CENTER;
      lv_draw_fill_dsc_t * fill_draw_dsc = lv_draw_task_get_fill_dsc(draw_task);
      if(fill_draw_dsc) {
        fill_draw_dsc->color = lv_color_mix(theme_color, fill_draw_dsc->color, LV_OPA_40);
        fill_draw_dsc->opa = LV_OPA_COVER;
      }
    }
    else if(col == 0) { // Wyrównanie dla nazw kategorii do prawej
      lv_draw_label_dsc_t * label_draw_dsc = lv_draw_task_get_label_dsc(draw_task);
      if(label_draw_dsc) label_draw_dsc->align = LV_TEXT_ALIGN_RIGHT;
    }

    if((row != 0 && row % 2) == 0) { // Paski (co drugi wiersz)
      lv_draw_fill_dsc_t * fill_draw_dsc = lv_draw_task_get_fill_dsc(draw_task);
      if(fill_draw_dsc) {
        fill_draw_dsc->color = lv_color_mix(theme_color, fill_draw_dsc->color, LV_OPA_10);
        fill_draw_dsc->opa = LV_OPA_COVER;
      }
    }
  }
}

void lv_create_main_gui(void) {
  table = lv_table_create(lv_screen_active());
  update_table_values();
  
  lv_obj_set_size(table, 320, 180);
  lv_obj_align(table, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_add_event_cb(table, draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
  lv_obj_add_flag(table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

  lv_obj_t * reset_btn = lv_button_create(lv_screen_active());
  lv_obj_set_size(reset_btn, 320, 60); 
  lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
  
  lv_obj_add_event_cb(reset_btn, reset_button_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * label = lv_label_create(reset_btn);
  lv_label_set_text(label, "ODSWIEZ DANE"); //  etykieta guzika
  lv_obj_center(label); 
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  pinMode(LDR_PIN, INPUT); // Inicjalizacja fotorezystora!

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");

  // Rejestracja ścieżki i uruchomienie serwera WWW
  server.on("/", handleRoot);
  server.begin();

  I2CBME.begin(I2C_SDA, I2C_SCL, 100000);
  if (!bme.begin(0x76)) {
    Serial.println("Nie znajduje BME280/BMP280!");
  }

  lv_init();
  lv_log_register_print_cb(log_print);
  
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(2);

  lv_display_t * disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
  
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchscreen_read);

  lv_create_main_gui();
}

void loop() {
  lv_task_handler();
  lv_tick_inc(5);
  
  server.handleClient(); // Nasłuchiwanie wejść na serwer z telefonu
  
  if (WiFi.status() == WL_CONNECTED && !time_synchronized) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5) && timeinfo.tm_year > 70) {
      update_table_values();
      time_synchronized = true;
      Serial.println("Zegar zsynchronizowany w tle. Tabela odswiezona!");
    }
  }
  
  delay(5);
}
