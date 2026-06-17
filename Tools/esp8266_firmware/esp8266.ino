#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ESP8266WebServer.h>

// --- Configuration ---
const int TCP_PORT = 8080;
const int RAM_BUFFER_LIMIT = 1000; 
const char* DATA_FILE = "/telemetry.bin";
const char* WIFI_FILE = "/wifi.txt";

// --- Data Structure ---
#pragma pack(push, 1)
struct SensorRecord {
    uint32_t index;
    float voltage;
    float current;
    float fanTemp;
    float temp1;
    float temp2;
};
#pragma pack(pop)

// --- Globals ---
WiFiServer tcpServer(TCP_PORT);
WiFiClient activeClient;
ESP8266WebServer webServer(80);

SensorRecord ramBuffer[RAM_BUFFER_LIMIT];
uint16_t bufferCount = 0;
uint32_t globalIndex = 0;

// --- State Tracking ---
bool inAPMode = false;
String currentSSID = "";
String currentPASS = "";

void setup() {
    Serial.begin(115200); 
    
    // Initialize LittleFS
    if (!LittleFS.begin()) {
        LittleFS.format(); 
        LittleFS.begin();
    }

    // Load WiFi Credentials
    if (LittleFS.exists(WIFI_FILE)) {
        File f = LittleFS.open(WIFI_FILE, "r");
        currentSSID = f.readStringUntil('\n');
        currentPASS = f.readStringUntil('\n');
        currentSSID.trim();
        currentPASS.trim();
        f.close();
    }

    if (currentSSID.length() > 0) {
        // --- Station Mode (Connecting to Router) ---
        inAPMode = false;
        WiFi.mode(WIFI_STA);
        WiFi.begin(currentSSID.c_str(), currentPASS.c_str());
        tcpServer.begin(); 
    } else {
        // --- Access Point Mode (Setup Network) ---
        inAPMode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP("DC_LOAD_SETUP"); 
        
        // Setup Simple Web Page
        webServer.on("/", HTTP_GET, []() {
            String html = "<html><body style='font-family: Arial; text-align: center; margin-top: 50px;'>";
            html += "<h2>DC Load WiFi Setup</h2>";
            html += "<form action='/save' method='POST'>";
            html += "<b>WiFi SSID:</b> <input type='text' name='s'><br><br>";
            html += "<b>Password:</b> <input type='password' name='p'><br><br>";
            html += "<input type='submit' value='Save & Connect' style='padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>";
            html += "</form></body></html>";
            webServer.send(200, "text/html", html);
        });

        // Handle Save Button
        webServer.on("/save", HTTP_POST, []() {
            String s = webServer.arg("s");
            String p = webServer.arg("p");
            if(s.length() > 0) {
                File f = LittleFS.open(WIFI_FILE, "w");
                f.println(s);
                f.println(p);
                f.close();
                webServer.send(200, "text/html", "<h2 style='color:green; text-align:center;'>Saved! DC Load is restarting...</h2>");
                delay(1000);
                ESP.restart(); 
            } else {
                webServer.send(400, "text/html", "<h2 style='color:red;'>Error: SSID cannot be empty.</h2> <a href='/'>Go Back</a>");
            }
        });
        
        webServer.begin();
    }
}

void loop() {
    // 1. Handle Web Server (Only in AP mode)
    if (inAPMode) {
        webServer.handleClient();
    }
    
    // 2. ALWAYS Handle UART Data & Commands
    handleUART();
    
    // 3. Handle TCP Client (Only in STA mode)
    if (!inAPMode) {
        handleTCP();
    }
}

// --- Subroutines ---

void handleUART() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) return;

        
        if (line == "CMD:GET_IP") {
            if (inAPMode) {
                Serial.print("IP:");
                Serial.println(WiFi.softAPIP().toString());
            } else if (WiFi.status() == WL_CONNECTED) {
                Serial.print("IP:");
                Serial.println(WiFi.localIP().toString());
            } else {
                Serial.println("IP:Connecting...");
            }
            return;
        }

     
        if (line == "CMD:RESET_WIFI") {
            LittleFS.remove(WIFI_FILE); 
            Serial.println("ACK:WIFI_CLEARED_RESTARTING");
            delay(500);
            ESP.restart(); 
            return;
        }

        int c1 = line.indexOf(',');
        int c2 = line.indexOf(',', c1 + 1);
        int c3 = line.indexOf(',', c2 + 1);
        int c4 = line.indexOf(',', c3 + 1);

        if (c1 > 0 && c2 > 0 && c3 > 0 && c4 > 0) {
            SensorRecord rec;
            rec.index = globalIndex++;
            rec.voltage = line.substring(0, c1).toFloat();
            rec.current = line.substring(c1 + 1, c2).toFloat();
            rec.fanTemp = line.substring(c2 + 1, c3).toFloat();
            rec.temp1 = line.substring(c3 + 1, c4).toFloat();
            rec.temp2 = line.substring(c4 + 1).toFloat();

            ramBuffer[bufferCount++] = rec;

            if (activeClient && activeClient.connected()) {
                sendRecordAsCSV(activeClient, rec);
            }

            if (bufferCount >= RAM_BUFFER_LIMIT) {
                flushBufferToFlash();
            }
        }
    }
}

void flushBufferToFlash() {
    File f = LittleFS.open(DATA_FILE, "a");
    if (f) {
        f.write((uint8_t*)ramBuffer, bufferCount * sizeof(SensorRecord));
        f.close();
    }
    bufferCount = 0;
}

void handleTCP() {
    if (tcpServer.hasClient()) {
        if (!activeClient || !activeClient.connected()) {
            if (activeClient) activeClient.stop();
            activeClient = tcpServer.available();
        } else {
            tcpServer.available().stop();
        }
    }

    if (activeClient && activeClient.available()) {
        String cmd = activeClient.readStringUntil('\n');
        cmd.trim();

        if (cmd.startsWith("SYNC:")) {
            uint32_t syncIndex = cmd.substring(5).toInt();
            syncData(syncIndex);
        } 
        else if (cmd == "RESET") {
            LittleFS.remove(DATA_FILE);
            bufferCount = 0;
            globalIndex = 0;
        }
    }
}

void syncData(uint32_t startIndex) {
    File f = LittleFS.open(DATA_FILE, "r");
    if (f) {
        SensorRecord rec;
        while (f.read((uint8_t*)&rec, sizeof(SensorRecord)) == sizeof(SensorRecord)) {
            if (rec.index >= startIndex) {
                sendRecordAsCSV(activeClient, rec);
            }
        }
        f.close();
    }
    for (uint16_t i = 0; i < bufferCount; i++) {
        if (ramBuffer[i].index >= startIndex) {
            sendRecordAsCSV(activeClient, ramBuffer[i]);
        }
    }
}

void sendRecordAsCSV(WiFiClient& client, const SensorRecord& rec) {
    client.printf("%u,%.3f,%.3f,%.2f,%.2f,%.2f\n", 
                  rec.index, rec.voltage, rec.current, rec.fanTemp, rec.temp1, rec.temp2);
}