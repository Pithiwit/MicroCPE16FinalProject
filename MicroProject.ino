#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <WebServer.h>

// ================= 1. ตั้งค่าเครือข่าย WiFi & MQTT =================
const char* ssid = "@JumboPlusIoT"; 
const char* password = "gzuoy6it";
const char* mqtt_server = "broker.hivemq.com";

const char* topic_pub = "cpe16/spectramatch/sensor_v2";
const char* topic_sub = "cpe16/spectramatch/led_v2";

// ================= 2. Hardware Config =================
#define LED_PIN 19
#define NUM_LEDS 120
#define S0 25
#define S1 26
#define S2 27
#define S3 14
#define OUT 32
#define BTN_RESET 18

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

// ================= 3. ตัวแปรสำหรับอ่านค่าสี =================
bool manualOverride = false;
unsigned long lastMqttPub = 0;

float rSmooth = 0, gSmooth = 0, bSmooth = 0;
float rGain = 1.35, gGain = 1.00, bGain = 0.75; // Gain ตามโค้ดต้นฉบับ



// ================= 4. หน้าเว็บ Dashboard =================
const char webpage_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>LED Control V2</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/mqtt/4.3.7/mqtt.min.js"></script>
    <style>
        body { font-family: sans-serif; background: #1a1a1a; color: white; text-align: center; padding: 20px; margin:0; display: flex; justify-content: center; align-items: center; min-height: 90vh; }
        .card { background: #2a2a2a; border-radius: 15px; padding: 30px; width: 100%; max-width: 350px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); }
        button { padding: 12px; margin: 5px; border-radius: 8px; border: none; font-weight: bold; width: 45%; cursor: pointer; }
        .btn-r { background: #ff4757; color: white; } .btn-g { background: #2ed573; color: white; } .btn-b { background: #1e90ff; color: white; }
        .btn-auto { background: linear-gradient(135deg, #ffa502, #ff7f50); width: 75px; height: 75px; border-radius: 50%; font-size: 1.5em; margin-top: 20px; color: white; border: 4px solid #eccc68; cursor: pointer; transition: 0.2s; }
        .btn-auto:active { transform: scale(0.9); }
        #status { margin-top: 20px; font-size: 0.85em; color: #7bed9f; }
    </style>
</head>
<body>
    <div class="card">
        <h3>🕹️ Manual Control</h3>
        <button class="btn-r" onclick="sendColor(255,0,0)">Red</button>
        <button class="btn-g" onclick="sendColor(0,255,0)">Green</button><br>
        <button class="btn-b" onclick="sendColor(0,0,255)">Blue</button>
        <button style="background:#eee; color:black;" onclick="sendColor(255,255,255)">White</button>
        <h4>🎨 Custom Color:</h4>
        <input type="color" style="width:95%; height:45px; cursor:pointer;" onchange="sendHex(this.value)">
        <button class="btn-auto" onclick="sendAutoMode()">R</button>
        <div id="status">🔌 Connecting...</div>
    </div>
    <script>
        var broker = (location.protocol === 'https:') ? 'wss://broker.hivemq.com:8884/mqtt' : 'ws://broker.hivemq.com:8000/mqtt';
        var client = mqtt.connect(broker, { clientId: 'web_' + Math.floor(Math.random() * 10000) });
        client.on('connect', function() { document.getElementById('status').innerText = '🟢 MQTT Connected'; });
        function sendColor(r,g,b) { client.publish('cpe16/spectramatch/led_v2', JSON.stringify({r:r, g:g, b:b})); }
        function sendHex(h) { sendColor(parseInt(h.substr(1,2),16), parseInt(h.substr(3,2),16), parseInt(h.substr(5,2),16)); }
        function sendAutoMode() { client.publish('cpe16/spectramatch/led_v2', JSON.stringify({mode:'auto'})); }
    </script>
</body>
</html>
)=====";

// ================= 5. Helper Functions =================
int readColor(bool s2, bool s3) {
    digitalWrite(S2, s2);
    digitalWrite(S3, s3);
    delayMicroseconds(200);
    long total = 0;
    for (int i = 0; i < 8; i++) total += pulseIn(OUT, LOW, 30000);
    return total / 8;
}

int gammaFix(int v) {
    return constrain((int)(pow(v / 255.0, 2.2) * 255.0), 0, 255);
}

void callback(char* topic, byte* payload, unsigned int length) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payload, length);
    if (doc.containsKey("mode") && doc["mode"] == "auto") {
        manualOverride = false;
        Serial.println(">>> MODE: AUTO (Sensor Activated) <<<");
    } else {
        manualOverride = true;
        int r = doc["r"], g = doc["g"], b = doc["b"];
        strip.fill(strip.Color(gammaFix(r), gammaFix(g), gammaFix(b)));
        strip.show();
        Serial.println(">>> MODE: MANUAL (Web Command) <<<");
    }
}

void reconnect() {
    while (!client.connected()) {
        String cid = "ESP32_" + String(random(0xffff), HEX);
        if (client.connect(cid.c_str())) {
            client.subscribe(topic_sub);
            Serial.println("MQTT Reconnected: " + cid);
        } else { delay(5000); }
    }
}

// ================= 6. SETUP =================
void setup() {
    Serial.begin(115200);
    pinMode(S0, OUTPUT); pinMode(S1, OUTPUT);
    pinMode(S2, OUTPUT); pinMode(S3, OUTPUT);
    pinMode(OUT, INPUT); pinMode(BTN_RESET, INPUT_PULLUP);
    digitalWrite(S0, HIGH); digitalWrite(S1, LOW); // Scale 20%

    strip.begin(); strip.setBrightness(80); strip.show();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.print("\n>>> IP: "); Serial.println(WiFi.localIP());

    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    server.on("/", []() { server.send(200, "text/html; charset=utf-8", webpage_html); });
    server.begin();
}

// ================= 7. LOOP =================
void loop() {
    server.handleClient();
    if (!client.connected()) reconnect();
    client.loop();

    if (digitalRead(BTN_RESET) == LOW) { manualOverride = false; delay(200); }

    // --- อ่านค่าเซนเซอร์ตลอดเวลาเพื่อส่ง MQTT ---
    int rRaw = readColor(LOW, LOW);
    int bRaw = readColor(LOW, HIGH);
    int gRaw = readColor(HIGH, HIGH);

    // --- ประมวลผลสี (Logic ตามโค้ดต้นฉบับของคุณ) ---
    int rOut, gOut, bOut;

    if (rRaw < 320 && gRaw < 320 && bRaw < 320) { // White
        rOut = 255; gOut = 255; bOut = 255;
    } else if (rRaw > 900 && gRaw > 900 && bRaw > 800) { // Black
        rOut = 0; gOut = 0; bOut = 0;
    } else {
        int r = map(rRaw, 1080, 260, 0, 255);
        int g = map(gRaw, 1085, 260, 0, 255);
        int b = map(bRaw, 920, 260, 0, 255);
        
        rOut = constrain(r * rGain, 0, 255);
        gOut = constrain(g * gGain, 0, 255);
        bOut = constrain(b * bGain, 0, 255);
    }

    // --- ระบบ SMOOTH ---
    int rG = gammaFix(rOut);
    int gG = gammaFix(gOut);
    int bG = gammaFix(bOut);

    rSmooth = rSmooth * 0.8 + rG * 0.2;
    gSmooth = gSmooth * 0.8 + gG * 0.2;
    bSmooth = bSmooth * 0.8 + bG * 0.2;

    // --- แสดงผลไฟเฉพาะโหมด AUTO ---
    if (!manualOverride) {
        strip.fill(strip.Color((int)rSmooth, (int)gSmooth, (int)bSmooth));
        strip.show();
    }
    

    // --- ส่งสถานะสีขึ้น MQTT ทุกๆ 500ms ---
    if (millis() - lastMqttPub > 500) {
        StaticJsonDocument<128> doc;
        doc["r"] = (int)rSmooth; doc["g"] = (int)gSmooth; doc["b"] = (int)bSmooth;
        doc["mode"] = manualOverride ? "manual" : "auto";
        char buffer[128]; serializeJson(doc, buffer);
        client.publish(topic_pub, buffer);
        lastMqttPub = millis();
    }
}