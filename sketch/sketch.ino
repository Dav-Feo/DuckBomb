unsigned long lastHeartbeat = 0;

void setup() {
    Serial.begin(9600);
    Serial1.begin(9600);
}

void loop() {

    // Messaggio periodico per confermare che lo sketch è vivo
    if (millis() - lastHeartbeat >= 2000) {
        Serial.println("[UNO Q] In attesa di dati HC-12...");
        lastHeartbeat = millis();
    }

    // Legge quello che arriva dall'HC-12 su D0/RX
    while (Serial1.available() > 0) {

        int b = Serial1.read();

        Serial.print("RICEVUTO -> DEC: ");
        Serial.print(b);

        Serial.print("  HEX: 0x");

        if (b < 16) {
            Serial.print("0");
        }

        Serial.print(b, HEX);

        Serial.print("  CHAR: ");

        if (b >= 32 && b <= 126) {
            Serial.write((char)b);
        } else {
            Serial.print(".");
        }

        Serial.println();
    }

    delay(5);
}