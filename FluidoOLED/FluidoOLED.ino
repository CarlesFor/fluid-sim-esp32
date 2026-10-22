/*
  ESP32 clasico + OLED SSD1306 I2C 128x64 + joystick.
  Librerias: Adafruit SSD1306 y Adafruit GFX Library (con sus dependencias).

  OLED: SDA=21, SCL=22. Joystick: VRX=35, VRY=34, SW=27.
  Alimentar el joystick a 3.3 V. Todas las masas deben estar conectadas.
  Primer arranque: seguir la calibracion de centro y cuatro direcciones.
  Mantener SW pulsado al arrancar para repetir la calibracion.
  Mover: orientar la gravedad. Pulsar: reiniciar el liquido.
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;
constexpr int JOY_X = 35;
constexpr int JOY_Y = 34;
constexpr int JOY_SW = 27;
constexpr int WIDTH = 128;
constexpr int HEIGHT = 64;

// La orientacion se aprende del joystick real y se guarda en el ESP32.
constexpr int DEAD_ZONE = 180;
constexpr uint32_t CALIBRATION_MAGIC = 0x4A4F5931;

constexpr int PARTICLE_COUNT = 240;
constexpr float H = 4.6f;             // Radio de interaccion, en pixeles.
constexpr float REST_DENSITY = 3.0f;
constexpr float STIFFNESS = 3600.0f;
constexpr float NEAR_STIFFNESS = 9000.0f;
constexpr float STEP = 1.0f / 120.0f;
constexpr uint32_t STEP_US = 8333;
constexpr uint32_t FRAME_US = 33333;   // Objetivo: 30 imagenes/s.
constexpr float GRAVITY = 180.0f;

Adafruit_SSD1306 display(WIDTH, HEIGHT, &Wire, -1, 400000, 400000);

// Solo tipos estandar en las firmas: compatible con los prototipos de Arduino.
struct Particle {
  float x, y, oldX, oldY, vx, vy;
};

struct JoystickCalibration {
  uint32_t magic;
  int centerX, centerY;
  float rightX, rightY;  // Cambio en los ADC al mover hacia la derecha.
  float upX, upY;        // Cambio en los ADC al mover hacia arriba.
};

JoystickCalibration joystick = {};
Particle particles[PARTICLE_COUNT];
float pressure[PARTICLE_COUNT];
float nearPressure[PARTICLE_COUNT];
float correctionX[PARTICLE_COUNT];
float correctionY[PARTICLE_COUNT];
uint16_t ink[WIDTH * HEIGHT];
float stickX = 0.0f;
float stickY = 0.0f;
uint32_t previousTick = 0;
uint32_t lastFrame = 0;
uint32_t accumulator = 0;

float limitFloat(float value, float low, float high) {
  return fmaxf(low, fminf(value, high));
}

void resetFluid() {
  for (int i = 0; i < PARTICLE_COUNT; ++i) {
    Particle &p = particles[i];
    // Bloque centrado, que cae y se extiende al iniciar.
    p.x = 37.0f + (i % 30) * 1.8f;
    p.y = 25.0f + (i / 30) * 1.8f;
    p.oldX = p.x;
    p.oldY = p.y;
    p.vx = p.vy = 0.0f;
  }
}

int readRawPin(int pin) {
  analogRead(pin);  // Descartar la primera conversion al cambiar de entrada.
  int sum = 0;
  for (int i = 0; i < 8; ++i) sum += analogRead(pin);
  return sum / 8;
}

void readRawJoystick(int &x, int &y) {
  x = readRawPin(JOY_X);
  y = readRawPin(JOY_Y);
}

void calibrationMessage(const char *first, const char *second) {
  display.clearDisplay();
  display.setCursor(0, 4);
  display.println("CALIBRAR JOYSTICK");
  display.setCursor(0, 25);
  display.println(first);
  display.setCursor(0, 45);
  display.println(second);
  display.display();
}

void waitCalibrationButton() {
  // Exigir una pulsacion nueva y estable, seguida de soltar el boton.
  uint32_t releasedAt = millis();
  while (uint32_t(millis() - releasedAt) < 40) {
    if (digitalRead(JOY_SW) == LOW) releasedAt = millis();
    delay(5);
  }
  uint32_t pressedAt = millis();
  while (uint32_t(millis() - pressedAt) < 40) {
    if (digitalRead(JOY_SW) == HIGH) pressedAt = millis();
    delay(5);
  }
  releasedAt = millis();
  while (uint32_t(millis() - releasedAt) < 40) {
    if (digitalRead(JOY_SW) == LOW) releasedAt = millis();
    delay(5);
  }
}

bool validJoystickCalibration() {
  if (joystick.magic != CALIBRATION_MAGIC ||
      joystick.centerX < 500 || joystick.centerX > 3595 ||
      joystick.centerY < 500 || joystick.centerY > 3595) return false;
  float rightLength = sqrtf(joystick.rightX * joystick.rightX +
                            joystick.rightY * joystick.rightY);
  float upLength = sqrtf(joystick.upX * joystick.upX + joystick.upY * joystick.upY);
  float determinant = joystick.rightX * joystick.upY - joystick.rightY * joystick.upX;
  // Dos direcciones independientes, con recorrido suficiente en ambos ejes.
  // Estas comparaciones tambien rechazan NaN o infinito en datos guardados.
  return rightLength >= 900.0f && rightLength <= 4000.0f &&
         upLength >= 900.0f && upLength <= 4000.0f &&
         fabsf(determinant) >= 0.65f * rightLength * upLength;
}

bool loadJoystickCalibration() {
  Preferences storage;
  if (!storage.begin("fluidjoy", false)) return false;
  bool loaded = storage.getBytesLength("calibration") == sizeof(joystick) &&
                storage.getBytes("calibration", &joystick, sizeof(joystick)) == sizeof(joystick);
  storage.end();
  return loaded && validJoystickCalibration();
}

bool saveJoystickCalibration() {
  Preferences storage;
  if (!storage.begin("fluidjoy", false)) return false;
  bool saved = storage.putBytes("calibration", &joystick, sizeof(joystick)) == sizeof(joystick);
  storage.end();
  return saved;
}

bool captureJoystickDirection(const char *direction, int &x, int &y) {
  calibrationMessage("Suelta la palanca", "Vuelve al centro");
  uint32_t centeredAt = millis();
  uint32_t startedAt = millis();
  while (uint32_t(millis() - centeredAt) < 400) {
    readRawJoystick(x, y);
    if (abs(x - joystick.centerX) > DEAD_ZONE ||
        abs(y - joystick.centerY) > DEAD_ZONE) centeredAt = millis();
    if (uint32_t(millis() - startedAt) > 30000) return false;
    delay(10);
  }
  calibrationMessage(direction, "Manten al tope 1 s");
  int anchorX = joystick.centerX;
  int anchorY = joystick.centerY;
  uint32_t stableAt = millis();
  startedAt = millis();
  while (uint32_t(millis() - startedAt) < 30000) {
    readRawJoystick(x, y);
    float dx = x - joystick.centerX;
    float dy = y - joystick.centerY;
    if (dx * dx + dy * dy < 1000.0f * 1000.0f ||
        abs(x - anchorX) > 90 || abs(y - anchorY) > 90) {
      stableAt = millis();
      anchorX = x;
      anchorY = y;
    } else if (uint32_t(millis() - stableAt) >= 1000) {
      int sumX = 0, sumY = 0;
      for (int i = 0; i < 32; ++i) {
        readRawJoystick(x, y);
        sumX += x;
        sumY += y;
        delay(2);
      }
      x = sumX / 32;
      y = sumY / 32;
      Serial.printf("%s: GPIO35=%d GPIO34=%d\n", direction, x, y);
      return true;
    }
    delay(10);
  }
  return false;
}

bool joystickDirectionsOppose(int ax, int ay, int bx, int by) {
  float dx1 = ax - joystick.centerX, dy1 = ay - joystick.centerY;
  float dx2 = bx - joystick.centerX, dy2 = by - joystick.centerY;
  float lengths = sqrtf((dx1 * dx1 + dy1 * dy1) * (dx2 * dx2 + dy2 * dy2));
  return lengths > 0.0f && dx1 * dx2 + dy1 * dy2 <= -0.8f * lengths;
}

bool calibrateJoystickOnce() {
  calibrationMessage("Centra la palanca", "Pulsa y suelta SW");
  waitCalibrationButton();
  delay(200);
  int sumX = 0, sumY = 0;
  int minX = 4095, maxX = 0, minY = 4095, maxY = 0;
  for (int i = 0; i < 64; ++i) {
    int x, y;
    readRawJoystick(x, y);
    sumX += x;
    sumY += y;
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
    if (y < minY) minY = y;
    if (y > maxY) maxY = y;
    delay(2);
  }
  joystick.centerX = sumX / 64;
  joystick.centerY = sumY / 64;
  if (joystick.centerX < 500 || joystick.centerX > 3595 ||
      joystick.centerY < 500 || joystick.centerY > 3595 ||
      maxX - minX > 120 || maxY - minY > 120) {
    Serial.printf("Centro invalido/inestable: GPIO35=%d GPIO34=%d\n",
                  joystick.centerX, joystick.centerY);
    return false;
  }

  int rx, ry, lx, ly, ux, uy, dx, dy;
  if (!captureJoystickDirection("Mueve DERECHA", rx, ry) ||
      !captureJoystickDirection("Mueve IZQUIERDA", lx, ly) ||
      !captureJoystickDirection("Mueve ARRIBA", ux, uy) ||
      !captureJoystickDirection("Mueve ABAJO", dx, dy)) return false;
  if (!joystickDirectionsOppose(rx, ry, lx, ly) ||
      !joystickDirectionsOppose(ux, uy, dx, dy)) return false;
  joystick.magic = CALIBRATION_MAGIC;
  joystick.rightX = 0.5f * (rx - lx);
  joystick.rightY = 0.5f * (ry - ly);
  joystick.upX = 0.5f * (ux - dx);
  joystick.upY = 0.5f * (uy - dy);
  return validJoystickCalibration();
}

void configureJoystick() {
  bool loaded = loadJoystickCalibration();
  calibrationMessage("SW al arrancar:", "recalibrar joystick");
  bool forceCalibration = false;
  for (int i = 0; i < 100; ++i) {
    if (digitalRead(JOY_SW) == LOW) forceCalibration = true;
    delay(10);
  }
  if (!loaded || forceCalibration) {
    while (!calibrateJoystickOnce()) {
      Serial.println("Calibracion incoherente. Revisar VRX=35, VRY=34, GND y 3V3.");
      calibrationMessage("Revisa VRX / VRY", "Pulsa para repetir");
      waitCalibrationButton();
    }
    bool saved = saveJoystickCalibration();
    calibrationMessage(saved ? "Calibracion guardada" : "No se pudo guardar",
                       "Suelta la palanca");
    delay(1000);
  }
  // Esperar el centro para no arrancar con la ultima direccion de calibracion.
  calibrationMessage("Suelta la palanca", "para empezar");
  uint32_t centeredAt = millis();
  while (uint32_t(millis() - centeredAt) < 400) {
    int x, y;
    readRawJoystick(x, y);
    if (abs(x - joystick.centerX) > DEAD_ZONE ||
        abs(y - joystick.centerY) > DEAD_ZONE || digitalRead(JOY_SW) == LOW) {
      centeredAt = millis();
    }
    delay(10);
  }
  stickX = stickY = 0.0f;
}

float joystickDeadZone(float value) {
  constexpr float zone = float(DEAD_ZONE) / 2048.0f;
  float magnitude = fabsf(value);
  if (magnitude <= zone) return 0.0f;
  float result = limitFloat((magnitude - zone) / (1.0f - zone), 0.0f, 1.0f);
  return value < 0.0f ? -result : result;
}

void mapJoystick(int rawX, int rawY, float &x, float &y) {
  float dx = rawX - joystick.centerX, dy = rawY - joystick.centerY;
  float determinant = joystick.rightX * joystick.upY - joystick.rightY * joystick.upX;
  // Invertir la base medida: derecha = +X, arriba = -Y de la pantalla.
  x = joystickDeadZone((dx * joystick.upY - dy * joystick.upX) / determinant);
  y = joystickDeadZone((dx * joystick.rightY - dy * joystick.rightX) / determinant);
}

void readControls() {
  int rawX, rawY;
  readRawJoystick(rawX, rawY);
  float targetX, targetY;
  mapJoystick(rawX, rawY, targetX, targetY);
  stickX += 0.3f * (targetX - stickX);
  stickY += 0.3f * (targetY - stickY);

  // Antirrebote; una pulsacion mantenida produce un solo reinicio.
  static bool lastRaw = HIGH;
  static bool stable = HIGH;
  static uint32_t changedAt = 0;
  bool raw = digitalRead(JOY_SW);
  if (raw != lastRaw) {
    lastRaw = raw;
    changedAt = millis();
  }
  if (raw != stable && uint32_t(millis() - changedAt) >= 30) {
    stable = raw;
    if (stable == LOW) resetFluid();
  }
}

void getGravity(float &gx, float &gy) {
  float length = sqrtf(stickX * stickX + stickY * stickY);
  if (length < 0.02f) {
    gx = 0.0f;
    gy = 1.0f;
    return;
  }
  // Al mover la palanca, la gravedad sigue su direccion exacta.
  // La caida por defecto solo se aplica con la palanca centrada.
  gx = stickX / length;
  gy = stickY / length;
}

void simulateStep(float gx, float gy) {
  for (int i = 0; i < PARTICLE_COUNT; ++i) {
    Particle &p = particles[i];
    p.oldX = p.x;
    p.oldY = p.y;
    p.vx = limitFloat(p.vx * 0.998f + gx * STEP, -150.0f, 150.0f);
    p.vy = limitFloat(p.vy * 0.998f + gy * STEP, -150.0f, 150.0f);
    p.x += p.vx * STEP;
    p.y += p.vy * STEP;
  }

  // Relajacion de doble densidad: presion para separar y cohesion para unir.
  memset(pressure, 0, sizeof(pressure));
  memset(nearPressure, 0, sizeof(nearPressure));
  for (int i = 0; i < PARTICLE_COUNT; ++i) {
    for (int j = i + 1; j < PARTICLE_COUNT; ++j) {
      float dx = particles[j].x - particles[i].x;
      float dy = particles[j].y - particles[i].y;
      float r2 = dx * dx + dy * dy;
      if (r2 >= H * H) continue;
      float q = 1.0f - sqrtf(r2) / H;
      float q2 = q * q;
      pressure[i] += q2;
      pressure[j] += q2;
      nearPressure[i] += q2 * q;
      nearPressure[j] += q2 * q;
    }
  }
  for (int i = 0; i < PARTICLE_COUNT; ++i) {
    pressure[i] = STIFFNESS * fmaxf(-0.5f, pressure[i] - REST_DENSITY);
    nearPressure[i] *= NEAR_STIFFNESS;
    correctionX[i] = correctionY[i] = 0.0f;
  }
  for (int i = 0; i < PARTICLE_COUNT; ++i) {
    for (int j = i + 1; j < PARTICLE_COUNT; ++j) {
      float dx = particles[j].x - particles[i].x;
      float dy = particles[j].y - particles[i].y;
      float r2 = dx * dx + dy * dy;
      if (r2 >= H * H) continue;
      // Direccion determinista si dos particulas coinciden exactamente.
      if (r2 < 0.000001f) {
        dx = ((i + j) & 1) ? 0.001f : -0.001f;
        dy = 0.0f;
        r2 = 0.000001f;
      }
      float r = sqrtf(r2);
      float q = 1.0f - r / H;
      float displacement = 0.5f * STEP * STEP *
          ((pressure[i] + pressure[j]) * q +
           (nearPressure[i] + nearPressure[j]) * q * q);
      displacement = limitFloat(displacement, -0.08f, 0.3f);
      float pushX = 0.5f * displacement * dx / r;
      float pushY = 0.5f * displacement * dy / r;
      correctionX[i] -= pushX;
      correctionY[i] -= pushY;
      correctionX[j] += pushX;
      correctionY[j] += pushY;
    }
  }
  for (int i = 0; i < PARTICLE_COUNT; ++i) {
    Particle &p = particles[i];
    p.x += correctionX[i];
    p.y += correctionY[i];
    p.vx = (p.x - p.oldX) / STEP;
    p.vy = (p.y - p.oldY) / STEP;

    // Recipiente cerrado con un pequeno rebote y rozamiento en las paredes.
    if (p.x < 2.0f || p.x > WIDTH - 3.0f) {
      p.vx *= -0.15f;
      p.vy *= 0.96f;
    }
    if (p.y < 2.0f || p.y > HEIGHT - 3.0f) {
      p.vy *= -0.15f;
      p.vx *= 0.96f;
    }
    p.x = limitFloat(p.x, 2.0f, WIDTH - 3.0f);
    p.y = limitFloat(p.y, 2.0f, HEIGHT - 3.0f);
  }
}

void renderFluid() {
  // Sumar manchas suaves y aplicar un umbral une las particulas en liquido.
  memset(ink, 0, sizeof(ink));
  for (int i = 0; i < PARTICLE_COUNT; ++i) {
    const Particle &p = particles[i];
    int cx = int(p.x);
    int cy = int(p.y);
    for (int y = cy - 3; y <= cy + 3; ++y) {
      if (y <= 0 || y >= HEIGHT - 1) continue;
      for (int x = cx - 3; x <= cx + 3; ++x) {
        if (x <= 0 || x >= WIDTH - 1) continue;
        float dx = x - p.x;
        float dy = y - p.y;
        float weight = 1.0f - (dx * dx + dy * dy) / 9.0f;
        if (weight > 0.0f) ink[y * WIDTH + x] += uint16_t(200 * weight * weight);
      }
    }
  }
  display.clearDisplay();
  uint8_t *buffer = display.getBuffer();
  for (int y = 1; y < HEIGHT - 1; ++y) {
    for (int x = 1; x < WIDTH - 1; ++x) {
      if (ink[y * WIDTH + x] >= 100) buffer[x + (y / 8) * WIDTH] |= 1 << (y & 7);
    }
  }
  display.drawRect(0, 0, WIDTH, HEIGHT, SSD1306_WHITE);
  display.display();
}

uint8_t findOLED() {
  const uint8_t addresses[] = {0x3C, 0x3D};
  for (uint8_t address : addresses) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) return address;
  }
  return 0;
}

void setup() {
  Serial.begin(115200);
  pinMode(JOY_SW, INPUT_PULLUP);
  pinMode(JOY_X, INPUT);
  pinMode(JOY_Y, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(JOY_X, ADC_11db);
  analogSetPinAttenuation(JOY_Y, ADC_11db);

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(50);
  uint8_t address = findOLED();
  // periphBegin=false conserva los pines establecidos por Wire.begin().
  if (!address || !display.begin(SSD1306_SWITCHCAPVCC, address, true, false)) {
    Serial.println("No se pudo iniciar la OLED. Revisar SDA=21, SCL=22 y alimentacion.");
    while (true) delay(1000);
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  configureJoystick();
  Serial.printf("OLED: 0x%02X. Centro joystick: GPIO35=%d GPIO34=%d\n",
                address, joystick.centerX, joystick.centerY);

  resetFluid();
  previousTick = lastFrame = micros();
}

void loop() {
  uint32_t now = micros();
  uint32_t elapsed = now - previousTick;
  previousTick = now;
  accumulator += elapsed > 50000 ? 50000 : elapsed;
  readControls();

  float gx, gy;
  getGravity(gx, gy);

  // Limitar la recuperacion evita bloquearse si la pantalla tarda demasiado.
  int steps = 0;
  while (accumulator >= STEP_US && steps < 6) {
    simulateStep(gx * GRAVITY, gy * GRAVITY);
    accumulator -= STEP_US;
    ++steps;
  }
  if (steps == 6) accumulator %= STEP_US;
  if (uint32_t(now - lastFrame) >= FRAME_US) {
    lastFrame = now;
    renderFluid();
  }
  delay(1);
}
