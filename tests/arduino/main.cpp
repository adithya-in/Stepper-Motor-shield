#include <Arduino.h>

const int PUL_PIN = 3;
const int DIR_PIN = 4;
const int ENA_PIN = 5;

void setup() {
  pinMode(PUL_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(PUL_PIN, LOW);
  digitalWrite(DIR_PIN, HIGH);
  digitalWrite(ENA_PIN, LOW);
}

void loop() {
  // 2.5 revs CW at 1/32 microstep (6400 steps/rev)
  for (int i = 0; i < 16000; i++) {
    digitalWrite(PUL_PIN, HIGH);
    delayMicroseconds(50);
    digitalWrite(PUL_PIN, LOW);
    delayMicroseconds(50);
  }
  delay(2000);

  // 5 revs CCW
  digitalWrite(DIR_PIN, LOW);
  for (int i = 0; i < 16000; i++) {
    digitalWrite(PUL_PIN, HIGH);
    delayMicroseconds(50);
    digitalWrite(PUL_PIN, LOW);
    delayMicroseconds(50);
  }
  digitalWrite(DIR_PIN, HIGH);
  delay(2000);
}
