void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, 20, 21);
}

void loop() {
  Serial1.println("Hello from Board A");
  Serial.println("sent");
  delay(5000);
}
