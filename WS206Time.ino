#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <time.h>
#include <sys/time.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>

// --- Waveshare ESP32-S3-Touch-AMOLED-2.06 Pin Configuration ---
// Display uses QSPI interface with CO5300 driver
#define LCD_CS    17
#define LCD_SCLK  2
#define LCD_SDIO0 1   // MOSI
#define LCD_SDIO1 3   // MISO
#define LCD_SDIO2 4
#define LCD_SDIO3 5
#define LCD_RST   -1  // No reset pin

// I2C pins for touch and sensors
#define IIC_SDA 6
#define IIC_SCL 7

// Create QSPI bus and display object
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_GFX *gfx = new Arduino_CO5300(bus, LCD_RST, 0, false, 410, 502);

// --- BLE UUIDs ---
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-cba987654321"

BLECharacteristic *pCharacteristic;
bool timeUpdated = false;

// --- LVGL UI Objects ---
lv_obj_t *time_label;
lv_obj_t *date_label;

// --- LVGL v9 Flush Callback ---
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    // Draw the LVGL buffer to the CO5300 AMOLED display
    gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
    
    // Tell LVGL the flush is complete
    lv_display_flush_ready(disp);
}

// --- BLE Callback ---
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        String value = pCharacteristic->getValue().c_str();
        if (value.length() == 4) {
            uint32_t epoch = 0;
            epoch |= (uint32_t)value[0] << 24;
            epoch |= (uint32_t)value[1] << 16;
            epoch |= (uint32_t)value[2] << 8;
            epoch |= (uint32_t)value[3];
            
            struct timeval tv;
            tv.tv_sec = epoch;
            tv.tv_usec = 0;
            settimeofday(&tv, NULL);
            timeUpdated = true;
            Serial.println("Time updated via BLE!");
        }
    }
};

void update_time_display() {
    time_t now;
    time(&now);
    struct tm * timeinfo = localtime(&now);

    if (timeinfo->tm_year > 70) {
        char timeStr[10];
        sprintf(timeStr, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        lv_label_set_text(time_label, timeStr);
        
        char dateStr[12];
        sprintf(dateStr, "%04d-%02d-%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
        lv_label_set_text(date_label, dateStr);
    } else {
        lv_label_set_text(time_label, "Wait...");
        lv_label_set_text(date_label, "Sync Time");
    }
}

void setup() {
    Serial.begin(115200);
    
    // 1. Display Initialization
    gfx->begin();
    gfx->fillScreen(0x0000);
    gfx->Display_Brightness(200); // Set brightness (0-255)
    
    // 2. LVGL v9 Initialization
    lv_init();
    
    // Create display object (410x502)
    lv_display_t *disp = lv_display_create(410, 502);
    
    // Allocate draw buffer (1/10th of the screen to save RAM)
    uint32_t buf_size = 410 * 502 / 10 * (LV_COLOR_DEPTH / 8);
    uint8_t *buf = new uint8_t[buf_size];
    
    // Set buffers and render mode
    lv_display_set_buffers(disp, buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    // Attach the flush callback
    lv_display_set_flush_cb(disp, my_disp_flush);

    // 3. Create LVGL UI with AMOLED-optimized design
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    
    // Decorative Arc (cyan color)
    lv_obj_t *arc1 = lv_arc_create(lv_scr_act());
    lv_obj_set_size(arc1, 380, 380);
    lv_arc_set_bg_angles(arc1, 0, 360);
    lv_obj_set_style_arc_width(arc1, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc1, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc1, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc1, lv_color_hex(0x00E5FF), LV_PART_INDICATOR);
    lv_obj_center(arc1);
    
    // Time Label (large, white)
    time_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_32, 0);
    lv_label_set_text(time_label, "00:00:00");
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -20);
    
    // Date Label (smaller, cyan)
    date_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(date_label, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(date_label, "0000-00-00");
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 40);

    // 4. Set Timezone to Indian Standard Time (UTC + 5:30)
    setenv("TZ", "IST-5:30", 1);
    tzset();

    // 5. BLE Initialization
    BLEDevice::init("Waveshare_Clock");
    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    pCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_WRITE
                      );
    pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
    pService->start();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
    Serial.println("BLE Ready. Waiting for connection...");
}

void loop() {
    // LVGL Tick (Required for internal timing)
    static unsigned long previousMillis = 0;
    unsigned long currentMillis = millis();
    lv_tick_inc(currentMillis - previousMillis);
    previousMillis = currentMillis;
    
    // Update Time every second or immediately if BLE synced
    static unsigned long lastTimeUpdate = 0;
    if (currentMillis - lastTimeUpdate >= 1000 || timeUpdated) {
        lastTimeUpdate = currentMillis;
        timeUpdated = false;
        update_time_display();
    }

    // LVGL Handler (Processes UI rendering)
    lv_timer_handler();
    delay(5);
}
