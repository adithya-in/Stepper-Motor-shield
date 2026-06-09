// TB6600 + NEMA17 test — CW 5s, CCW 5s, loop
//
// TB6600 wiring (common-cathode: CLK-/CW-/EN- are signal inputs):
//   TB6600 CLK-  → Arduino pin 3  (STEP, active-low pulse)
//   TB6600 CW-   → Arduino pin 4  (DIR,  active-low: LOW=CW)
//   TB6600 EN-   → Arduino pin 5  (ENA,  active-low: LOW=enabled)
//   TB6600 CLK+  → Arduino 5V
//   TB6600 CW+   → Arduino 5V
//   TB6600 EN+   → Arduino 5V
//   TB6600 GND   → Arduino GND
//   TB6600 V+    → 9-42V DC supply
//   TB6600 A+/A- → NEMA17 coil 1
//   TB6600 B+/B- → NEMA17 coil 2
//
// DIP switches: set current to NEMA17 rating, microstep as desired.

#define STEP_PIN  3
#define DIR_PIN   4
#define ENA_PIN   5

#define STEP_RATE 1000
#define CW        LOW    // active-low
#define CCW       HIGH

void setup() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN,  OUTPUT);
  pinMode(ENA_PIN, OUTPUT);

  digitalWrite(STEP_PIN, HIGH);  // idle = no step
  digitalWrite(DIR_PIN,  CW);    // start CW
  digitalWrite(ENA_PIN, HIGH);   // disabled initially

  Serial.begin(9600);
}

void loop() {
  digitalWrite(ENA_PIN, LOW);    // enable driver
  delay(100);

  Serial.println(">> CW 5s");
  digitalWrite(DIR_PIN, CW);
  for (int i = 0; i < 5; i++) {
    for (int s = 0; s < STEP_RATE; s++) {
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(500);
      digitalWrite(STEP_PIN, LOW);
      delayMicroseconds(500);
    }
    Serial.print("CW sec "); Serial.println(i + 1);
  }

  Serial.println(">> CCW 5s");
  digitalWrite(DIR_PIN, CCW);
  for (int i = 0; i < 5; i++) {
    for (int s = 0; s < STEP_RATE; s++) {
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(500);
      digitalWrite(STEP_PIN, LOW);
      delayMicroseconds(500);
    }
    Serial.print("CCW sec "); Serial.println(i + 1);
  }

  digitalWrite(ENA_PIN, HIGH);   // disable driver
  delay(2000);
}
