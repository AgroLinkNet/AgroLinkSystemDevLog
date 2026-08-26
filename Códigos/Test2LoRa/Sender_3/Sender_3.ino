/*
  ================================================================
   SENDER — ESP32-C3 SuperMini + DX-LR02 + SSD1306 OLED + Button
  ================================================================

  BEHAVIOUR
  ---------
  - OLED always shows "WAITING..." with a "Press to send" hint.
  - Button press → sends a LoRa packet, OLED shows the packet
    number (#1, #2 …) in large text for 2 seconds, then returns
    to the waiting screen automatically.

  WIRING
  ------
  DX-LR02               ESP32-C3 SuperMini
  --------              ------------------
  VCC            -----> 3V3
  GND            -----> GND
  UART_TX        -----> GPIO7    (ESP receives here)
  UART_RX        -----> GPIO6    (ESP transmits here)
  M0, M1, AUX         leave unconnected

  SSD1306 OLED (I2C)    ESP32-C3 SuperMini
  ------------------    ------------------
  VCC            -----> 3V3
  GND            -----> GND
  SDA            -----> GPIO4
  SCL            -----> GPIO5

  Button                ESP32-C3 SuperMini
  ------                ------------------
  Pin A          -----> GPIO3
  Pin B          -----> GND
  (no external resistor — internal pull-up enabled in software)

  WHY THESE PINS?
  ---------------
  On the ESP32-C3 SuperMini, GPIO8 and GPIO9 are strapping pins the
  chip reads at boot to decide its startup mode, and GPIO8 doubles
  as the onboard blue LED. An I2C device connected to those pins can
  pull a line LOW at exactly the wrong moment during reset and stop
  the board from booting — which makes the OLED show nothing.
  GPIO4/5/6/7/3 carry none of these restrictions and are the safest
  choices on this board.

  ARDUINO IDE SETTINGS
  --------------------
  Board            : ESP32C3 Dev Module  (or "Nologo ESP32C3 Super Mini")
  USB CDC On Boot  : Enabled             ← required for Serial.print()

  LIBRARIES NEEDED  (Tools → Manage Libraries)
  --------------------------------------------
  - Adafruit SSD1306
  - Adafruit GFX Library
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── Pin assignments ────────────────────────────────────────────────
#define LORA_RX_PIN  7    // ESP RX ← DX-LR02 UART_TX
#define LORA_TX_PIN  6    // ESP TX → DX-LR02 UART_RX
#define OLED_SDA     4
#define OLED_SCL     5
#define BUTTON_PIN   3    // active-LOW with internal pull-up

// ── Configuration ─────────────────────────────────────────────────
#define LORA_BAUD       9600
#define SCREEN_W        128
#define SCREEN_H        64
#define OLED_ADDR       0x3C   // try 0x3D if the screen stays blank
#define PACKET_SHOW_MS  2000   // ms to show the packet # before returning to Waiting

// ── Globals ───────────────────────────────────────────────────────
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
HardwareSerial   LoRaSerial(1);

uint32_t packetCount   = 0;
bool     showingPacket = false;
uint32_t packetShownAt = 0;

// Button debounce
bool     lastRaw      = HIGH;
bool     btnState     = HIGH;
uint32_t lastDebounce = 0;
const uint32_t DEBOUNCE_MS = 50;

// ── Display helpers ───────────────────────────────────────────────
void drawHeader() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  // "LoRa Sender" centred (11 chars × 6 px = 66 px)
  display.setCursor((SCREEN_W - 66) / 2, 2);
  display.print("LoRa Sender");
  display.drawFastHLine(0, 12, SCREEN_W, SSD1306_WHITE);
}

void showWaiting() {
  display.clearDisplay();
  drawHeader();

  // "WAITING..." at textSize 2  (10 chars × 12 px = 120 px)
  display.setTextSize(2);
  display.setCursor(4, 20);
  display.print("WAITING...");

  // Hint at bottom
  display.setTextSize(1);
  // "Press to send" = 13 chars × 6 px = 78 px
  display.setCursor((SCREEN_W - 78) / 2, 52);
  display.print("Press to send");

  display.display();
}

void showPacketSent(uint32_t num) {
  display.clearDisplay();
  drawHeader();

  // "Sent!" label
  display.setTextSize(1);
  // "Sent!" = 5 chars × 6 px = 30 px
  display.setCursor((SCREEN_W - 30) / 2, 17);
  display.print("Sent!");

  // Packet number — auto-size so long numbers still fit on screen
  String  label = "#" + String(num);
  int8_t  sz    = (label.length() <= 4) ? 3 : 2;
  int16_t charW = 6 * sz;
  int16_t x     = (SCREEN_W - (int16_t)label.length() * charW) / 2;
  display.setTextSize(sz);
  display.setCursor(x > 0 ? x : 0, 32);
  display.print(label);

  display.display();
}

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  LoRaSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found! Double-check GPIO4 (SDA) and GPIO5 (SCL).");
    Serial.println("If wiring is correct, try changing OLED_ADDR to 0x3D.");
    while (true) delay(1000);
  }

  showWaiting();
  Serial.println("Sender ready — press the button to send a packet.");
}

// ── Loop ──────────────────────────────────────────────────────────
void loop() {
  // --- debounced button read ---
  bool raw = digitalRead(BUTTON_PIN);
  if (raw != lastRaw) lastDebounce = millis();
  if (millis() - lastDebounce > DEBOUNCE_MS) {
    if (raw != btnState) {
      btnState = raw;
      if (btnState == LOW) {   // active-low: LOW = pressed
        packetCount++;
        String pkt = "PKT:" + String(packetCount);
        LoRaSerial.println(pkt);
        Serial.println("Sent: " + pkt);
        showPacketSent(packetCount);
        showingPacket = true;
        packetShownAt = millis();
      }
    }
  }
  lastRaw = raw;

  // --- auto-return to Waiting screen after PACKET_SHOW_MS ---
  if (showingPacket && millis() - packetShownAt >= PACKET_SHOW_MS) {
    showWaiting();
    showingPacket = false;
  }
}
