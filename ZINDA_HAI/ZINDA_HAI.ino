#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HX711.h>

// LCD Setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Pins
const int loadCellDoutPin = A0;
const int loadCellSckPin = A1;
const int pumpPin = 9;
const int buttonMonitoring = 2;
const int buttonFlowControl = 3;
const int flowButtons[5] = {4, 5, 6, 7, 8};
const int redPin = 10, greenPin = 11, bluePin = 12;
const int buzzerPin = 13;

// Flow settings
const int flowRates[5] = {10, 20, 30, 40, 50};
// ON duration for 1 ml (6 seconds)
const unsigned long pumpOnDuration = 6000;
const unsigned long offDurations[5] = {
  354000,  // 10 ml/hr → (3600s/10)*1000 - 6000 ≈ 354000 ms
  174000,  // 20 ml/hr → (3600s/20)*1000 - 6000 ≈ 174000 ms
  114000,  // 30 ml/hr → 114000 ms
  84000,   // 40 ml/hr →  84000 ms
  66000    // 50 ml/hr →  66000 ms
};
int selectedFlowRate = 0;
unsigned long offDuration = 0;
unsigned long nextPumpTime = 0;
bool pumpState = false;
unsigned long pumpStartTime = 0;

// State flags
HX711 scale;
float calibration_factor = 205.0;
float initialBottleWeight = 0;
bool bottleAttached = false;
bool isMonitoringMode = true;
bool pumpAllowed = false;

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System Starting");

  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, HIGH); // Pump OFF

  scale.begin(loadCellDoutPin, loadCellSckPin);
  scale.set_scale(calibration_factor);
  scale.tare();

  pinMode(buttonMonitoring, INPUT_PULLUP);
  pinMode(buttonFlowControl, INPUT_PULLUP);
  for (int i = 0; i < 5; i++) pinMode(flowButtons[i], INPUT_PULLUP);

  pinMode(buzzerPin, OUTPUT);
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  setColor(0, 0, 0);

  delay(2000);
  lcd.clear();
  lcd.print("Attach Bottle");
}

void loop() {
  float weight = getFilteredWeight();

  // Bottle attach
  if (!bottleAttached && weight > 50.0) {
    attachBottle(weight);
  }

  // Mode select
  if (digitalRead(buttonMonitoring) == LOW) {
    setMonitoringMode();
    delay(300);
  }
  if (digitalRead(buttonFlowControl) == LOW) {
    setFlowControlMode();
    delay(300);
  }

  if (!bottleAttached) return;

  if (isMonitoringMode) {
    runMonitoringMode(weight);
  } else {
    runFlowControlMode(weight);
  }
}

// Weight read
float getFilteredWeight() {
  float w = scale.get_units(10);
  return (w < 0) ? 0 : w;
}

// On attach
void attachBottle(float w) {
  bottleAttached = true;
  initialBottleWeight = w;
  lcd.clear();
  lcd.print("Stabilizing...");
  delay(5000);
  lcd.clear();
  lcd.print("Select Mode");
  Serial.println("Bottle Attached: " + String(w) + "g");
}

void setMonitoringMode() {
  isMonitoringMode = true;
  selectedFlowRate = 0;
  stopPump();
  lcd.clear();
  lcd.print("Monitoring Mode");
}

void setFlowControlMode() {
  isMonitoringMode = false;
  nextPumpTime = millis();
  lcd.clear();
  lcd.print("Flow Control Mode");
}

void runMonitoringMode(float w) {
  updateLevelStatus(w);
  digitalWrite(pumpPin, pumpAllowed ? LOW : HIGH); // Optional relay test in monitoring
  delay(300);
}

void runFlowControlMode(float w) {
  checkFlowRateButtons();
  updateLevelStatus(w);
  controlPump();
}

void checkFlowRateButtons() {
  for (int i = 0; i < 5; i++) {
    if (digitalRead(flowButtons[i]) == LOW) {
      setFlowRate(flowRates[i]);
      delay(300);
    }
  }
}

void setFlowRate(int rate) {
  selectedFlowRate = rate;
  offDuration = offDurations[rate / 10 - 1]; // Selecting off duration based on flow rate
  nextPumpTime = millis();

  lcd.clear();
  lcd.print("Rate: ");
  lcd.print(rate);
  lcd.print("ml/hr");
  lcd.setCursor(0, 1);
  lcd.print("OFF: ");
  lcd.print(offDuration / 1000);
  lcd.print("s");

  Serial.print("Rate Set: ");
  Serial.print(rate);
  Serial.print(" ml/hr, OFF: ");
  Serial.print(offDuration / 1000);
  Serial.println(" sec");
}

void controlPump() {
  unsigned long currentTime = millis();

  if (selectedFlowRate == 0) {
    lcd.setCursor(0, 1);
    lcd.print("Select Rate     ");
    stopPump();
    return;
  }

  if (!pumpAllowed) {
    lcd.setCursor(0, 1);
    lcd.print("Low Level!      ");
    stopPump();
    return;
  }

  if (pumpState) {
    if (currentTime - pumpStartTime >= pumpOnDuration) {
      stopPump();
      nextPumpTime = millis() + offDuration;

     // Corrected delay block based on selectedFlowRate value
if (selectedFlowRate == 10) {
  delay(351000);
} else if (selectedFlowRate == 20) {
  delay(171000);
} else if (selectedFlowRate == 30) {
  delay(111000);
} else if (selectedFlowRate == 40) {
  delay(81000);
} else if (selectedFlowRate == 50) {
  delay(63000);
}

      float remain = getFilteredWeight();
      lcd.setCursor(0, 1);
      lcd.print("1ml Passed      ");
      delay(1000);
      lcd.setCursor(0, 1);
      lcd.print("Remain: ");
      lcd.print(remain, 1);
      lcd.print("g     ");
      Serial.println("Pump OFF");
    }
  } else {
    if (currentTime >= nextPumpTime) {
      startPump();
    } else {
      unsigned long timeLeft = (nextPumpTime - currentTime) / 1000;
      lcd.setCursor(0, 1);
      lcd.print("Next in ");
      lcd.print(timeLeft);
      lcd.print("s     ");
    }
  }
}

void startPump() {
  pumpState = true;
  pumpStartTime = millis();
  digitalWrite(pumpPin, LOW);
  lcd.clear();
  lcd.print("Delivering 1ml");
  lcd.setCursor(0, 1);
  lcd.print("ON Time: 6s");
  Serial.println("Pump ON");
}

void stopPump() {
  if (pumpState) {
    pumpState = false;
    digitalWrite(pumpPin, HIGH);
  }
}
void updateLevelStatus(float w) {
  // Check if bottle was removed
  if (w < 20.0) {  // Or whatever threshold you want (safe low limit)
    bottleAttached = false;
    initialBottleWeight = 0;
    isMonitoringMode = true;
    selectedFlowRate = 0;
    stopPump();
    setColor(1, 0, 0);
    lcd.clear();
    lcd.print("Attach Bottle");
    tone(buzzerPin, 1000, 2000);
    delay(1000);
    return;
  }

  // Continue with normal level detection
  float full = initialBottleWeight * 0.60;
  float half = initialBottleWeight * 0.40;
  float low = initialBottleWeight * 0.25;

  lcd.setCursor(0, 0);
  if (w >= full) {
    setColor(0, 1, 0);
    lcd.print("Status: FULL     ");
    pumpAllowed = true;
  } else if (w >= half) {
    setColor(1, 1, 0);
    lcd.print("Status: HALF     ");
    pumpAllowed = true;
  } else if (w < low) {
    setColor(1, 0, 0);
    lcd.print("Status: EMPTY    ");
    pumpAllowed = false;
    stopPump();
    tone(buzzerPin, 1000, 3000);
    lcd.clear();
    lcd.print("Attach Bottle");
    delay(1000);
    bottleAttached = false;
    initialBottleWeight = 0;
    isMonitoringMode = true;
    selectedFlowRate = 0;
  }
}


void setColor(int r, int g, int b) {
  digitalWrite(redPin, r);
  digitalWrite(greenPin, g);
  digitalWrite(bluePin, b);
}  