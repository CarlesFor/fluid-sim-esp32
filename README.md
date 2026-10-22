# Simulación de fluidos con ESP32, OLED y joystick

Una simulación de líquido en una pantalla OLED de **0,96 pulgadas**, controlada
con un joystick. El ESP32 calcula en tiempo real el movimiento de **240 partículas**
con presión y cohesión, y las dibuja como gotas y masas de líquido.

Mueve el joystick para cambiar la dirección de la gravedad: el líquido puede caer
al suelo, hacia las paredes o hacia el techo. En reposo cae hacia abajo. Pulsa el
joystick para reiniciar la simulación.

**[Abrir el código completo](FluidoOLED/FluidoOLED.ino)** ·
**[Descargar el proyecto en ZIP](https://github.com/CarlesFor/fluid-sim-esp32/archive/refs/heads/main.zip)**

## Material necesario

- Un **ESP32 clásico**, por ejemplo un DevKit con ESP32-WROOM-32.
- Una pantalla **OLED SSD1306 I²C de 128 × 64 píxeles**, de 0,96 pulgadas.
- Un módulo joystick analógico con pines `VRX`, `VRY` y `SW`.
- Cables de conexión, un cable USB de datos y, opcionalmente, una protoboard.

El tamaño de la pantalla por sí solo no identifica su controlador ni su
resolución. Este programa está preparado para **SSD1306 de 128 × 64**. Las placas
ESP32-C3, ESP32-S3 y otras variantes pueden requerir un cableado distinto.

## Conexiones

Los números del programa son números **GPIO**. En muchas placas se indican
también como D21, D22, etc.

| Módulo | Pin del módulo | ESP32 |
| --- | --- | --- |
| OLED | SDA | GPIO21 / D21 |
| OLED | SCL | GPIO22 / D22 |
| OLED | GND | GND |
| OLED | VCC | 3V3, si el módulo admite alimentación a 3,3 V |
| Joystick | VRX | GPIO35 / D35 |
| Joystick | VRY | GPIO34 / D34 |
| Joystick | SW | GPIO27 / D27 |
| Joystick | GND | GND |
| Joystick | VCC o +5V | **3V3** |

**Alimenta el joystick con 3,3 V, aunque su placa indique «+5V».** Así VRX y VRY
no entregarán 5 V a las entradas del ESP32. Todas las masas deben estar
conectadas entre sí. El pulsador utiliza la resistencia interna `INPUT_PULLUP`
de GPIO27: en reposo se lee `HIGH` y al pulsar se lee `LOW`.

## Instalación con Arduino IDE

1. Instala [Arduino IDE](https://www.arduino.cc/en/software).
2. En **Archivo → Preferencias → Gestor de URLs adicionales de tarjetas**, añade
   esta URL de la versión estable de Espressif:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

   Consulta la [guía oficial de instalación](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
   si necesitas localizar esa opción en tu sistema.
3. Abre el gestor de placas, busca **esp32** e instala el paquete
   **esp32 by Espressif Systems**.
4. En el gestor de bibliotecas instala:
   - **Adafruit SSD1306**.
   - **Adafruit GFX Library**.
   - Acepta la instalación de sus dependencias, incluida **Adafruit BusIO**.
5. Descarga este repositorio con **Code → Download ZIP** y descomprímelo.
6. Abre **`FluidoOLED/FluidoOLED.ino`**. Mantén el archivo dentro de la carpeta
   `FluidoOLED`, con el mismo nombre que el sketch.
7. Conecta el ESP32 por USB. En **Herramientas → Placa**, selecciona tu modelo;
   para un DevKit clásico normalmente corresponde **ESP32 Dev Module**.
8. Selecciona su puerto en **Herramientas → Puerto** y pulsa **Subir**.
9. Al arrancar, **deja el joystick centrado durante unos dos segundos** para que
   el programa calibre su posición de reposo.

También puedes clonar el repositorio:

```sh
git clone https://github.com/CarlesFor/fluid-sim-esp32.git
```

## Cómo usarlo

| Acción | Resultado |
| --- | --- |
| Dejar el joystick centrado | El líquido cae hacia abajo. |
| Mover la palanca | Cambia la dirección de la gravedad. |
| Moverla en círculos | Lleva el líquido por las paredes y el techo. |
| Pulsar el joystick | Reinicia las partículas en el centro. |

La dirección de los ejes depende de cómo esté orientado el módulo. Si la respuesta
queda invertida, ajusta `INVERT_X` o `INVERT_Y` y vuelve a subir el programa.

## Ajustes del código

Todos estos valores están al principio de
[`FluidoOLED.ino`](FluidoOLED/FluidoOLED.ino):

| Constante | Valor inicial | Función |
| --- | --- | --- |
| `INVERT_X` | `false` | Invierte el eje horizontal al cambiarlo a `true`. |
| `INVERT_Y` | `false` | Invierte el eje vertical al cambiarlo a `true`. |
| `DEAD_ZONE` | `180` | Ignora pequeñas variaciones del joystick en reposo. |
| `PARTICLE_COUNT` | `240` | Cantidad de partículas de líquido. |
| `GRAVITY` | `180.0f` | Intensidad de la gravedad. |
| `REST_DENSITY` | `3.0f` | Densidad de referencia del líquido. |
| `STIFFNESS` | `3600.0f` | Respuesta de la presión. |
| `NEAR_STIFFNESS` | `9000.0f` | Separación a corta distancia. |

Para empezar, cambia únicamente los ejes, la zona muerta o la gravedad. Reducir
`PARTICLE_COUNT` a `180` aligera el cálculo y deja menos líquido en pantalla.
La posición inicial está definida en `resetFluid()`.

La simulación usa pasos fijos de **1/120 de segundo** e intenta actualizar la
pantalla a **30 FPS**. La velocidad real depende de la placa y del bus I²C.
Es un modelo visual de líquido, no un cálculo de precisión para ingeniería.

## Solución de problemas

### La pantalla no muestra nada

- Comprueba alimentación, GND y las conexiones **SDA = 21, SCL = 22**.
- Abre el monitor serie a **115200 baudios**. El programa informa de la dirección
  detectada o de un error al iniciar la pantalla.
- El código busca automáticamente las direcciones **`0x3C` y `0x3D`**.
- La detección I²C no identifica el controlador. Si tu OLED es **SH1106**, hace
  falta cambiar la biblioteca y adaptar su inicialización; cambiar únicamente
  la dirección no basta.
- Si I²C resulta inestable, sustituye los **tres valores `400000` por `100000`**
  en el sketch. La actualización de la pantalla será más lenta.

### El líquido se mueve hacia un lado con la palanca centrada

Reinicia el ESP32 sin tocar la palanca durante la calibración. Si persiste,
aumenta `DEAD_ZONE` de `180` a `250` y vuelve a subir el código.

### El joystick responde al revés

Cambia `INVERT_X` o `INVERT_Y` a `true`, según el eje afectado.

### No aparece el puerto o falla la subida

Comprueba que el cable USB permita transferir datos y que la placa y el puerto
seleccionados sean correctos. Algunos DevKit requieren mantener pulsado **BOOT**
durante el inicio de la conexión de subida. El controlador USB necesario depende
del adaptador de tu placa.

## Archivos

```text
FluidoOLED/
  FluidoOLED.ino    # Programa completo: simulación, dibujo, joystick y calibración
README.md          # Cableado e instrucciones
.gitignore         # Exclusiones de archivos locales y de compilación
```

## Referencias

- [ADC de Arduino-ESP32](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html)
- [Biblioteca Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [Biblioteca Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library)
