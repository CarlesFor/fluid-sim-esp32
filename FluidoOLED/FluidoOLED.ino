/*
  ESP32 clasico + OLED SSD1306 I2C 128x64 + joystick.
  Librerias: Adafruit SSD1306 y Adafruit GFX Library (con sus dependencias).

  OLED: SDA=21, SCL=22. Joystick: VRX=35, VRY=34, SW=27.
  Alimentar el joystick a 3.3 V. Todas las masas deben estar conectadas.
  Dejar el joystick centrado al encender para calibrarlo.
  Mover: orientar la gravedad. Pulsar: reiniciar el liquido.
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <string.h>

constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;
constexpr int JOY_X = 35;
constexpr int JOY_Y = 34;
constexpr int JOY_SW = 27;
constexpr int WIDTH = 128;
constexpr int HEIGHT = 64;

// Cambiar a true si un eje del joystick se mueve al reves.
constexpr bool INVERT_X = false;
constexpr bool INVERT_Y = false;
constexpr int DEAD_ZONE = 180;

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

Particle particles[PARTICLE_COUNT];
float pressure[PARTICLE_COUNT];
float nearPressure[PARTICLE_COUNT];
float correctionX[PARTICLE_COUNT];
float correctionY[PARTICLE_COUNT];
uint16_t ink[WIDTH * HEIGHT];
int centerX = 2048;
int centerY = 2048;
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

float readAxis(int pin, int center, bool invert) {
  int sum = 0;
  for (int i = 0; i < 4; ++i) sum += analogRead(pin);
  int offset = sum / 4 - center;
  int distance = abs(offset);
  if (distance <= DEAD_ZONE) return 0.0f;
  int span = (offset > 0 ? 4095 - center : center) - DEAD_ZONE;
  float result = limitFloat(float(distance - DEAD_ZONE) / (span > 0 ? span : 1),
                            0.0f, 1.0f);
  if (offset < 0) result = -result;
  return invert ? -result : result;
}

void readControls() {
  stickX += 0.3f * (readAxis(JOY_X, centerX, INVERT_X) - stickX);
  stickY += 0.3f * (readAxis(JOY_Y, centerY, INVERT_Y) - stickY);

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
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(7, 15);
  display.println("SIMULACION DE FLUIDO");
  display.setCursor(7, 35);
  display.println("Centra el joystick");
  display.display();
  delay(1000);

  uint32_t sumX = 0;
  uint32_t sumY = 0;
  for (int i = 0; i < 128; ++i) {
    sumX += analogRead(JOY_X);
    sumY += analogRead(JOY_Y);
    delay(2);
  }
  centerX = int(sumX / 128);
  centerY = int(sumY / 128);
  // Evitar una calibracion inutil si se enciende con la palanca al extremo.
  if (centerX < 700 || centerX > 3395) centerX = 2048;
  if (centerY < 700 || centerY > 3395) centerY = 2048;
  Serial.printf("OLED: 0x%02X. Centro joystick: X=%d, Y=%d\n", address, centerX, centerY);

  resetFluid();
  previousTick = lastFrame = micros();
}

void loop() {
  uint32_t now = micros();
  uint32_t elapsed = now - previousTick;
  previousTick = now;
  accumulator += elapsed > 50000 ? 50000 : elapsed;
  readControls();

  // En reposo cae hacia abajo; el joystick orienta la gravedad en 360 grados.
  float strength = fmaxf(fabsf(stickX), fabsf(stickY));
  float gx = 2.0f * stickX;
  float gy = 1.0f - strength + 2.0f * stickY;
  float length = sqrtf(gx * gx + gy * gy);
  if (length > 1.0f) {
    gx /= length;
    gy /= length;
  }

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
