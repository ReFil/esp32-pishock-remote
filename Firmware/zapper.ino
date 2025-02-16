#include "WiFi.h"
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiClientSecure.h>

#include "secrets.h"

//#define DEBUG

#define DISPLAY_POWER_PIN D10

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(OLED_RESET);

#define BATTERY_PIN A0
#define POWER_PIN A1
#define DURATION_PIN A2

#define POTENTIOMETER_MAX 2170
#define REVERSE_POTS

#define BUTTON_PIN D3

#define POWER_MAX 30
#define DURATION_MAX 5

#define OP_BEEP 2
#define OP_VIBE 1
#define OP_SHOCK 0

uint8_t battery_percentage = 0;
uint8_t power = 0;
uint8_t duration = 0;

uint32_t last_active_time;
uint32_t last_button_down;
uint32_t last_button_up;

bool button_state;
bool old_button_state;

bool is_collar_connected;

void try_connect_wifi() {
    WiFi.disconnect();

    // The idea is this cycles through the ssid array until it connects, this has not been tested though
    for(int i=0; i<SSID_ARRAY_LEN; i++) {
        WiFi.begin(ssids[i], passes[i]);
        int timeout=0;
            while ((WiFi.status() != WL_CONNECTED) && (timeout < 60)) {
            delay(500);
            timeout++;
            #ifdef DEBUG
            Serial.print(".");
            #endif
            }
            if(WiFi.status() == WL_CONNECTED) {
                #ifdef DEBUG
                Serial.println("Connected to WiFi");
                Serial.print("SSID: ");
                Serial.println(WiFi.SSID());

                IPAddress ip = WiFi.localIP();
                Serial.print("IP Address: ");
                Serial.println(ip);
            
                long rssi = WiFi.RSSI();
                Serial.print("signal strength (RSSI):");
                Serial.print(rssi);
                Serial.println(" dBm");
                #endif
                return;
            }
    }
    // No connectable networks, let the thing go to sleep and try again in 5 minutes
    
    #ifdef DEBUG
    Serial.println("Failed to connect to a WiFi Network");
    #endif
    return;
}

uint8_t lithium_ion_mv_to_pct(int16_t bat_mv) {
    // Simple linear approximation of a battery based off adafruit's discharge graph:
    // https://learn.adafruit.com/li-ion-and-lipoly-batteries/voltages

    if (bat_mv >= 4200) {
        return 99;
    } else if (bat_mv <= 3450) {
        return 0;
    }

    return bat_mv * 2 / 15 - 459;
}

void poll_analog_inputs() {

    // This doesn't seem accurate, there probably needs to be some correction factor here, my unit read 80% at full battery
    uint32_t rawVbatt = 0;
    for(int i = 0; i < 16; i++) {
       rawVbatt = rawVbatt + analogReadMilliVolts(BATTERY_PIN); // ADC with correction   
    }
    uint32_t Vbattmv = 2 * rawVbatt / 16;     // attenuation ratio 1/2, mV --> V
    battery_percentage = lithium_ion_mv_to_pct(Vbattmv);


    // Read both potentiometers
    uint32_t raw_power = 0;
    for(int i = 0; i < 16; i++) {
        raw_power = raw_power + analogReadMilliVolts(POWER_PIN); // ADC with correction   
    }
    raw_power = raw_power / 16;     // attenuation ratio 1/2, mV --> V


    uint32_t raw_duration = 0;
    for(int i = 0; i < 16; i++) {
        raw_duration = raw_duration + analogReadMilliVolts(DURATION_PIN); // ADC with correction   
    }
    raw_duration = raw_duration / 16;     // attenuation ratio 1/2, mV --> V
    #ifdef REVERSE_POTS
    power = map(raw_power, POTENTIOMETER_MAX, 5, 1, POWER_MAX);
    duration = map(raw_duration, POTENTIOMETER_MAX, 5, 1, DURATION_MAX);
    #else
    power = map(raw_power, 5, POTENTIOMETER_MAX, 1, POWER_MAX);
    duration = map(raw_duration, 5, POTENTIOMETER_MAX, 1, DURATION_MAX);
    #endif

    #ifdef DEBUG
    Serial.print("Vbattmv: ");
    Serial.print(Vbattmv);
    Serial.print(" Battery %: ");
    Serial.print(battery_percentage);
    Serial.print(" Raw Power: ");
    Serial.print(raw_power);
    Serial.print(" Power: ");
    Serial.print(power);
    Serial.print(" Raw Duration: ");
    Serial.print(raw_duration);
    Serial.print(" Duration: ");
    Serial.println(duration);
    #endif

  
}

void drawCentreString(const String &buf, int x, int y)
{
    int16_t x1, y1;
    uint16_t w, h;
    display.setTextSize(2);
    display.getTextBounds(buf, x, y, &x1, &y1, &w, &h); //calc width of new string
    display.setCursor(x - w / 2, y);
    display.print(buf);
}

void update_display() {
    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("WiFi:");
    display.print((WiFi.status() == WL_CONNECTED)?"C":"X");

    display.setCursor(48, 0);
    display.print("Collar:");
    display.print((is_collar_connected)?"C":"X");

    display.setCursor(108, 0);
    display.print(String(battery_percentage) + "%");



    display.setCursor(5, 18);
    drawCentreString(String(power), 27, 18);

    display.setCursor(65, 18);
    drawCentreString(String(duration), 90, 18);

    display.setTextSize(1);

    
    display.setCursor(12, 9);
    display.print("Power");

    display.setCursor(65, 9);
    display.print("Duration");
    
    display.display();
}

const char* api_url = "https://do.pishock.com/api/apioperate";

void send_to_api(int power, int duration, int op){
    if(WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;

        // SSL is hard so we ignore cert validation (it didn't work when i tried to validate the cert)
        client.setInsecure();
        HTTPClient http;
        http.begin(client, api_url);
    
        
        http.addHeader("Content-Type", "application/json");
        String httpRequestData = "{\"Username\":\"";
        httpRequestData += username;
        httpRequestData += "\",\"Name\":\"";
        httpRequestData += dev_name;
        httpRequestData += "\",\"Code\":\"";
        httpRequestData += dev_code;
        if(op != 2) {
        httpRequestData += "\",\"Intensity\":\"";
        httpRequestData += String((int)power);
        }
        httpRequestData += "\",\"Duration\":\"";
        httpRequestData += String((int)duration);
        httpRequestData += "\",\"Apikey\":\"";
        httpRequestData += api_key;
        httpRequestData += "\",\"Op\":\"";
        httpRequestData += String((int)op);
        httpRequestData += "\"}";           
        #ifdef DEBUG
        Serial.print("HTTP Request: ");
        Serial.println(httpRequestData);
        #endif
        int httpResponseCode = http.POST(httpRequestData);
    
        if (httpResponseCode > 0) {
            String response = http.getString();
            #ifdef DEBUG
            Serial.print("Response Code: " + String(httpResponseCode));
            Serial.println(",  Response: " + response);
            #endif
            // Inspect response to determine if collar actually connected
            if(response == "Operation Attempted.")
                is_collar_connected = true;
            else
                is_collar_connected = false;
        } else {
            #ifdef DEBUG
            Serial.println("Error on sending POST: " + String(httpResponseCode));
            #endif
        }
        
        // Free resources
        http.end();
    }
}

void setup() {

    Serial.begin(115200);
    
    // Set pin 10 as output, button pin as input
    pinMode(DISPLAY_POWER_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    
    //Serial.println("bongus");

    // Fire up the wifi and connect
    WiFi.useStaticBuffers(true);
    WiFi.mode(WIFI_STA);
    try_connect_wifi();

    // Turn the display on
    digitalWrite(DISPLAY_POWER_PIN, HIGH);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    delay(500);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    
    display.display();
    delay(500);

    last_active_time = millis();
}

void loop() {

    // Poll analog inputs and update display
    poll_analog_inputs();
    update_display();    
    
    button_state = !digitalRead(BUTTON_PIN);
    // If button has been pressed
    if(button_state && !old_button_state)
        last_button_down = millis();

    // If button has been released
    else if(!button_state && old_button_state){
        last_button_up = millis();

        // Released after more than 1.5s
        if((last_button_up - last_button_down) > 1500) {
            #ifdef DEBUG
            Serial.println("Zapping");
            #endif
            display.clearDisplay();
            drawCentreString("Zapping!", 80, 10);
            display.display();
            send_to_api(power, duration, OP_SHOCK);
            delay(100);
        }

        // It was just tapped
        else  {
            #ifdef DEBUG
            Serial.println("Vibing");
            #endif
            display.clearDisplay();
            display.setTextSize(2);
            drawCentreString("Buzzing!", 80, 10);
            display.display();
            send_to_api(power, duration, OP_VIBE);
            delay(100);
        }
        last_button_down = last_button_up;
        last_active_time = millis();
    }   
    old_button_state = button_state;

    // Every 30s poll API with zero duration and zero power to get collar status
    if((millis() % 30000) < 19)
        send_to_api(0, 0, OP_VIBE);

    // If 5 mins since last button press
    if((millis() - last_active_time ) > 300000) {
        // Turn off display
        digitalWrite(DISPLAY_POWER_PIN, LOW);

        // Shut down I2C pins to stop phantom power
        pinMode(D4, OUTPUT);
        pinMode(D5, OUTPUT);
        digitalWrite(D4, LOW);
        digitalWrite(D5, LOW);

        // Enable wakeup on the button pin
        esp_deep_sleep_enable_gpio_wakeup(BIT(BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);

        // Go to sleep now
        esp_deep_sleep_start();
    }
    delay(10);
    
}