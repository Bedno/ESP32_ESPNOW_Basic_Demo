// ESP-Now transceiver test for TTGO "T-Display" ESP32 module with 1.14" color screen features.
// Andrew Bedno - AndrewBedno.com
// Compile options: Board: ESP32 Dev Module.

const String Version = "0.15";
const int Analyze_None = 0;  // Fastest for production compile.
const int Analyze_Basic = 1;  // Enable basic analysis messages.
const int Analyze_Verbose = 2;  // Show somewhat more details.
const int Analyze_Flags = Analyze_Basic;

uint32_t CPU_MHz = 240;  // Configured clock speed.  240 max, divide to reduce power use.
uint32_t APB_Freq = 80000000;  // Timer clock speed.  Usually 80MHz but read from system in setup to confirm.

// Display library.
// REQUIRES custom install (in IDE library manager) from TTGO git zip (rather than Arduino stock) ...
//   to set SPI pins specific to board: TFT_MOSI=19, TFT_SCLK=18, TFT_CS=5, TFT_DC=16, TFT_RST=23
//   and define constants like TFT_DISPOFF
// https://github.com/Xinyuan-LilyGO/TTGO-T-Display
#include "TFT_eSPI.h"
const int TFT_Wid = 240;  const int TFT_Hgt = 135;
const int Bottom_Y = TFT_Hgt - 15;  // Vertical position of bottom screen row.
TFT_eSPI tft = TFT_eSPI(TFT_Hgt, TFT_Wid); // Start custom screen library at given resolution.

#include <SPI.h>  // SPI communications library.
#include <esp_adc_cal.h>  // ADC calibration for (battery level testing).

// WI-FI
#include <esp_now.h>  // ESP Now Wi-Fi protocol library.
#include <WiFi.h>  // WiFi library.
#include <esp_wifi.h>  // Additional WiFi functions.
long MsgNum_Rcvd = 0;  // Packet number received from transmitter.
long MsgNum_Rcvd_Total = 0;  // How many packets actually received.
long MsgNum_Rcvd_ShouldBe = 0;  // How many receives should have happened in the elapsed time.
const long MsgNum_Rcvd_Timer_DeciDur = 15;  // Deci-Seconds until receive consider missed.
long MsgNum_Rcvd_Timer_DeciCnt = MsgNum_Rcvd_Timer_DeciDur;  // Deci-Seconds until receive consider missed.
long MsgNum_Missed = 0;  // Incremental count of missed packets.
long MsgNum_out = 0;  // Packet number sent by transmitter.
typedef struct ESPNow_Packet_struct {
  long MsgNum_Data;
};
ESPNow_Packet_struct ESPNow_Packet;
// Configure MAC addresses for ESPNow devices using an artificial range.
uint8_t ESPNow_MAC_Master[6] = {0xB0, 0xB2, 0x1C, 0x4F, 0x00, 0x01};
uint8_t ESPNow_MAC_Client[6] = {0xB0, 0xB2, 0x1C, 0x4F, 0x00, 0x02};
esp_now_peer_info_t peerInfo;
char ESPNow_Msg[800];

// Configure hardware options specific to this board model.
// Routes battery level input to ADC:
#define BATTERY_ADC_ENABLE_PIN GPIO_NUM_14
// Battery level analog input pin.  ADC1:32-39. ADC2 disabled when WiFi used.
#define BATTERY_ADC_INPUT_PIN GPIO_NUM_34

// Right/Upper button.
#define BUTTON_1_PIN GPIO_NUM_35
// Left/Lower button.
#define BUTTON_2_PIN GPIO_NUM_0

// Globals for non-volatile memory.
#include <Preferences.h>
Preferences Memory;
bool Sleep_Flag = false;

// Globals for battery level read,
char Battery_Msg[20];
int Battery_VRef = 1100;
uint16_t Battery_Read;
float Battery_Voltage;
float Battery_Read_prev = 0.0;
int Battery_Y = int(Bottom_Y / 2);  // Vertical screen position of power readout.  Middle at boot, bottom in RxTx modes.

long UpSeconds = 0;
long UpSeconds_prev = 0;
long SubTime = 0;
bool Busy = false;

const int Mode_StartMenu = 0;
const int Mode_Receive = 1;
const int Mode_Receiving = -1;
const int Mode_Send = 2;
const int Mode_Sending= -2;
const int Mode_Error = 99;
int Main_Mode = Mode_StartMenu;

// === WI-FI ===

// Callback function when ESPNow packet sent.
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (! Busy) {
    Busy = true;
    tft.fillRect(0, 38, TFT_Wid, 70, TFT_BLACK);  // Clear rectangle for new value
    tft.setCursor(0, 40);
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(3);  // Medium
    sprintf(ESPNow_Msg,"#%ld", MsgNum_out);
    tft.println(ESPNow_Msg);
    if (status == ESP_NOW_SEND_SUCCESS) {
      tft.setTextColor(TFT_GREEN);
      tft.print(" OK");
    } else {
      tft.setTextColor(TFT_RED);
      tft.print(" FAIL");
    }
    Busy = false;
  }    
}
 
void RcvdScreen () {
  tft.fillRect(0, 38, TFT_Wid, 70, TFT_BLACK);  // Clear rectangle for new value
  tft.setCursor(0, 40);
  if (MsgNum_Missed > 1) {
    tft.setTextColor(TFT_RED);
  } else {
    tft.setTextColor(TFT_CYAN);
  }    
  tft.setTextSize(3);  // Medium
  if (MsgNum_Rcvd > 0) {
    sprintf(ESPNow_Msg,"\"#%ld\"", MsgNum_Rcvd);
  } else {
    sprintf(ESPNow_Msg,"Waiting...");
  }    
  tft.println(ESPNow_Msg);
  tft.setTextColor(TFT_BLUE);
  if ( (MsgNum_Rcvd_Total>0) || (MsgNum_Rcvd_ShouldBe>0) ) {
    sprintf(ESPNow_Msg," %ld/%ld", MsgNum_Rcvd_Total, MsgNum_Rcvd_ShouldBe);
    tft.print(ESPNow_Msg);
  }    
}

// Callback function when ESPNow packet received.
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (! Busy) {
    Busy = true;
    memcpy(&ESPNow_Packet, incomingData, sizeof(ESPNow_Packet));
    MsgNum_Rcvd = ESPNow_Packet.MsgNum_Data;
    if ( (MsgNum_Rcvd_Total < 1) && (ESPNow_Packet.MsgNum_Data > 0) ) { MsgNum_Rcvd_Total = ESPNow_Packet.MsgNum_Data; } else { MsgNum_Rcvd_Total++; }; 
    if ( (MsgNum_Rcvd_ShouldBe < 1) && (ESPNow_Packet.MsgNum_Data > 0) ) { MsgNum_Rcvd_ShouldBe = ESPNow_Packet.MsgNum_Data; } else { MsgNum_Rcvd_ShouldBe++; }; 
    MsgNum_Missed = 0;  MsgNum_Rcvd_Timer_DeciCnt = MsgNum_Rcvd_Timer_DeciDur;
    RcvdScreen();
    Busy = false;
  }  
}

String MACtoStr(uint8_t MAC_a[]) {
  String MAC_s;  char MAC_b[3];
  for (int MAC_i = 0; MAC_i < 6; ++MAC_i) {
    sprintf(MAC_b, "%02X", MAC_a[MAC_i]);
    MAC_s += MAC_b;  if (MAC_i < 5) MAC_s += ':';
  }
  return MAC_s;
}

// === BOOT STARTUP ===
void setup()
{
    // Highest clock speeds set in var defs for best service.
    setCpuFrequencyMhz(CPU_MHz);  CPU_MHz = getCpuFrequencyMhz();  APB_Freq = getApbFrequency();

    // Setup battery voltage input.
    pinMode(BATTERY_ADC_ENABLE_PIN, OUTPUT);
    digitalWrite(BATTERY_ADC_ENABLE_PIN, HIGH);

    // Setup buttons, connects to ground (logic low) when pressed.
    pinMode(BUTTON_1_PIN, INPUT_PULLUP);
    pinMode(BUTTON_2_PIN, INPUT_PULLUP);

    // Alternating non-volatile flag implements use of reset button for power off/on.
    Memory.begin("cyfi", false);
    Sleep_Flag = Memory.getBool("sleep", false);
    if (Sleep_Flag) {
      Memory.putBool("sleep", false);
      delay(200);  // Tiny delay assures flash write finishes.
      // Turn off screen.
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.writecommand(TFT_DISPOFF);
      tft.writecommand(TFT_SLPIN);
      digitalWrite(TFT_BL, 0);
      delay(200);  // Tiny delay to let screen finish.
      // Turn off rtos wakes.  May not be necessary, generates a warning.
      // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
      // esp_sleep_enable_ext0_wakeup(BUTTON_1_PIN, 0);  // Enable wakeup on press of right button (or top reset button)
      delay(200);  // Tiny delay for RTOS settling.
      esp_deep_sleep_start();  // Halts.
    } else {
      Memory.putBool("sleep", true);  // Flag to sleep on next boot.
    }
    UpSeconds = Memory.getLong("UpSeconds", 0);
    UpSeconds_prev = UpSeconds;

    // Initialize screen.
    tft.init();
    // Set backlight brightness (uses PWM of LED).
    pinMode(TFT_BL, OUTPUT);
    ledcSetup(0, 5000, 8); // 0-15, 5000, 8
    ledcAttachPin(TFT_BL, 0); // TFT_BL, 0 - 15
    ledcWrite(0, 60);  // 0-15, 0-255 (with 8 bit resolution); 0=dark, 255=brightest

    // Calibrate the battery level sense.
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);    // Check type of calibration value used to characterize ADC
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) { Battery_VRef = adc_chars.vref; }

    tft.setRotation(1);  // 0=portrait, 1=landscape (2=180,3=270)
    tft.fillScreen(TFT_BLACK);  // Clear screen.
    tft.setTextSize(2);  // Small
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("RECEIVE >>>", 110, 0 );
    tft.drawString("   SEND >>>", 110, Bottom_Y );

    tft.setTextSize(2);  // small
    Battery_Read = analogRead(BATTERY_ADC_INPUT_PIN);
    Battery_Voltage = ((float)Battery_Read / 4095.0) * 2.0 * 3.3 * (Battery_VRef / 1000.0);
    Battery_Voltage = round(Battery_Voltage*100) / 100;  // Truncate to two decimal places.
    tft.setTextColor(TFT_GREEN);
    sprintf(Battery_Msg, "%1.2fv ", Battery_Voltage);
    tft.drawString(Battery_Msg, 0, Battery_Y );
    tft.setTextColor(TFT_WHITE);
    tft.drawString("v"+Version, 0, Battery_Y+16 );
    if (Analyze_Flags > Analyze_None) {
      Serial.begin(115200);
      Serial.printf("\n\n\n\n\nBooting v%s\n", Version);
      Serial.printf("Freqs: CPU=%d APB=%d Xtal=%d\n", CPU_MHz, int(APB_Freq/1000000), getXtalFrequencyMhz());
      Serial.printf("UpSeconds=%ld\n", UpSeconds);
    }
}

// === MAIN LOOP ===
void loop() {

   // Set device as receiver.
  if (Main_Mode == Mode_Receive) {
    Serial.printf("Receiving...\n");
    tft.fillScreen(TFT_BLACK);  // Clear screen.
    tft.fillRect(0, 0, TFT_Wid, 32, TFT_YELLOW);  // Head bar.
    tft.setTextColor(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);  // Centered.
    tft.setTextSize(4);  // BIG
    tft.drawString("RECEIVING", 120, 3 );
    tft.setTextSize(3);  // Medium
    Battery_Y = Bottom_Y;
    WiFi.mode(WIFI_STA);
    esp_wifi_set_mac(WIFI_IF_STA, &ESPNow_MAC_Client[0]);
    if (esp_now_init() != ESP_OK) { // Init ESP-NOW
      tft.setCursor(0, 25);  tft.setTextColor(TFT_RED);
      tft.println("ESPNow init failed");
      Main_Mode = Mode_Error;
    } else {
      // register callback to get recv packer info
      esp_now_register_recv_cb(OnDataRecv);
      Main_Mode = Mode_Receiving;
    }
  }
  
  // Set device as transmitter.
  if (Main_Mode == Mode_Send) {
    Serial.printf("Sending...\n");
    tft.fillScreen(TFT_BLACK);  // Clear screen.
    tft.fillRect(0, 0, TFT_Wid, 32, TFT_YELLOW);  // Head bar.
    tft.setTextColor(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);  // Centered.
    tft.setTextSize(4);  // big
    tft.drawString("SENDING", 120, 3 );    
    tft.setTextSize(3);  // Medium
    Battery_Y = Bottom_Y;
    WiFi.mode(WIFI_STA);
    esp_wifi_set_mac(WIFI_IF_STA, &ESPNow_MAC_Master[0]);
    if (esp_now_init() != ESP_OK) { // Init ESP-NOW
      tft.setCursor(0, 25);  tft.setTextColor(TFT_RED);
      tft.println("ESPNow init failed");
      Main_Mode = Mode_Error;
    } else {
      // register callback to get recv packer info
      esp_now_register_send_cb(OnDataSent);
      // register peer
      peerInfo.channel = 0;  
      peerInfo.encrypt = false;
      // register first peer  
      memcpy(peerInfo.peer_addr, ESPNow_MAC_Client, 6);
      if (esp_now_add_peer(&peerInfo) != ESP_OK){
        tft.setCursor(0, 25);  tft.setTextColor(TFT_RED);
        tft.println("ESPNow peer failed");
        Main_Mode = Mode_Error;
      } else {
        Main_Mode = Mode_Sending;
      }
    }
  }

  // Service transmitter.
  if ( (Main_Mode == Mode_Sending) && (! Busy) ) {
    MsgNum_out++;
    ESPNow_Packet.MsgNum_Data = MsgNum_out;
    esp_err_t result = esp_now_send(0, (uint8_t *) &ESPNow_Packet, sizeof(ESPNow_Packet_struct));
  }

  // Log uptime once a minute.
  if ( ((Main_Mode == Mode_Receiving) || (Main_Mode == Mode_Sending) ) && (! Busy) ) {
    UpSeconds++;
    if ( (UpSeconds - UpSeconds_prev) > 60) {
      Memory.putLong("UpSeconds", UpSeconds);
      UpSeconds_prev = UpSeconds;
    }
  }

  for (SubTime=0; SubTime < 10; SubTime++) {
    if (! Busy) {
      // Service receiver.
      if (Main_Mode == Mode_Receiving) {
        if (MsgNum_Rcvd_Timer_DeciCnt < 0) {  // Detect packet receive timeouts.
          MsgNum_Rcvd_ShouldBe++;
          MsgNum_Missed++;
          RcvdScreen();
          MsgNum_Rcvd_Timer_DeciCnt = MsgNum_Rcvd_Timer_DeciDur;
        } else {
          MsgNum_Rcvd_Timer_DeciCnt--;  
        }
      }
      // Wait for button pressed.
      if (Main_Mode == Mode_StartMenu) {
        if (digitalRead(BUTTON_2_PIN) < 1) { Main_Mode = Mode_Send; }
        if (digitalRead(BUTTON_1_PIN) < 1) { Main_Mode = Mode_Receive; }
        if (Main_Mode != Mode_StartMenu) { tft.fillScreen(TFT_BLACK); }
      }
      // Both buttons clears uptime.
      if ( (digitalRead(BUTTON_2_PIN) < 1) && (digitalRead(BUTTON_1_PIN) < 1) ) {
        UpSeconds = 0;  UpSeconds_prev = 0;
      }
    }
    delay(100);  // Main cycle frequency is once a second (10x 1/10).
  }

}
