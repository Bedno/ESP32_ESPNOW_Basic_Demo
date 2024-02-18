# ESP32_ESPNOW_Basic_Demo
Basic demo of Wi-Fi communication between ESP32s using their "ESPNOW" protocol.
Sepcifically targetted for the "TTGO" type module, which includes a tiny color screen.
Compile and install using the Arduino IDE of course.

Sends / Receives packets between two devices with various error checking and display.
Demonstrates use of ESP-NOW protocol which implements low-latency Wi-Fi communications without need for Hub or SSID,
as well as full use of the tiny color display, and power on/off control in software.

Posted in part so I can keep a frozen copy of this working demo which sends only one packet a second,
before I start coding  a streaming test which sends several hundred sequential packets.
