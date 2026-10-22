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
9. En el primer arranque, sigue la **calibración guiada de la OLED** que se
   explica a continuación. La biblioteca `Preferences`, usada para guardarla,
   viene incluida en el paquete de placas ESP32.

También puedes clonar el repositorio:

```sh
git clone https://github.com/CarlesFor/fluid-sim-esp32.git
```

## Calibración del joystick

El programa aprende la orientación del joystick a partir de sus lecturas
reales. No necesita fijar signos ni intercambiar ejes manualmente.

1. Cuando aparezca **«Centra la palanca / Pulsa y suelta SW»**, centra la palanca
   y pulsa y suelta el botón del joystick.
2. La pantalla pedirá **DERECHA, IZQUIERDA, ARRIBA y ABAJO**, en ese orden.
   En cada paso, mueve la palanca hasta el extremo indicado y mantenla quieta
   aproximadamente un segundo, hasta que cambie el mensaje.
3. Entre pasos, vuelve al centro cuando lo indique la pantalla. No hace falta
   pulsar el botón mientras mantienes una dirección.
4. Al terminar, suelta la palanca para empezar la simulación.

La calibración se guarda en la memoria del ESP32 para los siguientes arranques.
Para repetirla, **mantén pulsado SW mientras enciendes o reinicias el ESP32**;
suéltalo cuando aparezca la petición de centrar y vuelve a seguir los pasos.
Si giras físicamente el módulo respecto a la pantalla, repite la calibración.

Si aparece **«Revisa VRX / VRY»**, las muestras no son coherentes: comprueba
VRX = GPIO35, VRY = GPIO34, GND común y alimentación a 3,3 V. Pulsa para repetir.
Si habías conectado el joystick a **5V/VIN**, desconecta la alimentación, pasa
su VCC a **3V3** y repite la calibración. Sus salidas pueden superar el rango
de las entradas del ESP32 cuando el módulo se alimenta a 5 V.
El monitor serie a 115200 baudios muestra las lecturas de cada dirección para
ayudar a localizar el problema. Un eje desconectado o dos señales que no puedan
distinguirse requieren revisar el montaje.

## Cómo usarlo

| Acción | Resultado |
| --- | --- |
| Dejar el joystick centrado | El líquido cae hacia abajo. |
| Mover la palanca | Cambia la dirección de la gravedad. |
| Moverla en círculos | Lleva el líquido por las paredes y el techo. |
| Pulsar el joystick | Reinicia las partículas en el centro. |

Con la palanca desplazada, la gravedad sigue su dirección sin añadir una fuerza
hacia abajo. Al volver al centro, vuelve la caída hacia abajo. El líquido tiene
inercia y puede tardar en cambiar su movimiento.

## Ajustes del código

Todos estos valores están al principio de
[`FluidoOLED.ino`](FluidoOLED/FluidoOLED.ino):

| Constante | Valor inicial | Función |
| --- | --- | --- |
| `DEAD_ZONE` | `180` | Ignora pequeñas variaciones del joystick en reposo. |
| `PARTICLE_COUNT` | `240` | Cantidad de partículas de líquido. |
| `GRAVITY` | `180.0f` | Intensidad de la gravedad. |
| `REST_DENSITY` | `3.0f` | Densidad de referencia del líquido. |
| `STIFFNESS` | `3600.0f` | Respuesta de la presión. |
| `NEAR_STIFFNESS` | `9000.0f` | Separación a corta distancia. |

Para empezar, cambia únicamente la zona muerta o la gravedad. Reducir
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

Repite la calibración manteniendo SW al reiniciar. Centra bien la palanca cuando
se solicite. Si persiste, aumenta `DEAD_ZONE` de `180` a `250` y vuelve a subir
el código.

### El joystick responde al revés

Repite la calibración guiada y mueve la palanca en la dirección que pide cada
pantalla. Mantén el módulo en la misma orientación durante su uso.

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
- [Preferences de Arduino-ESP32](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html)
