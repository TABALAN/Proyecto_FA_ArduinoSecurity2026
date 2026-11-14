# Sistema de Control de Acceso — Arduino Security

## Proyecto Final — Arquitectura del Computador

Sistema de control de acceso físico construido sobre Arduino Uno que integra identificación por tarjeta RFID, autenticación por PIN, control de puerta mediante servomotor e indicadores visuales y sonoros.

---

## Integrantes

| Nombre         | Carné   |
|----------------|---------|
| Claudia Mejía  | 1127224 |
| Tony Balán     | 1202124 |

---

## Dependencias

Antes de compilar el proyecto, se deben instalar las siguientes librerías desde el gestor de librerías del IDE de Arduino (`Herramientas → Administrar bibliotecas`):

| Librería              | Versión probada | Uso                          |
|-----------------------|-----------------|------------------------------|
| `LiquidCrystal_I2C`   | 1.1.2           | Control de la pantalla LCD   |
| `MFRC522`             | 1.4.11          | Lectura del módulo RFID RC522 |
| `Servo`               | 1.2.1           | Control del servomotor       |

Las librerías `Wire`, `SPI` y `EEPROM` son parte del núcleo de Arduino y no requieren instalación adicional.

---

## Componentes

| Componente            | Descripción                                      |
|-----------------------|--------------------------------------------------|
| Arduino Uno           | Unidad de procesamiento central                  |
| RFID RC522            | Lector de tarjetas y llaveros RFID               |
| Teclado matricial 4×4 | Entrada del PIN de autenticación                 |
| Pantalla LCD 16×2 I2C | Interfaz visual del sistema                      |
| Servomotor            | Mecanismo de apertura y cierre de puerta         |
| 74HC595               | Registro de desplazamiento para control de LEDs  |
| 8 LEDs de estado      | Indicadores visuales del estado del sistema      |
| Buzzer activo         | Indicadores sonoros y alarma                     |

---

## Mapa de pines

| Pin Arduino | Conexión         |
|-------------|------------------|
| D0          | Keypad — Fila 1  |
| D1          | Keypad — Fila 2  |
| D2          | Keypad — Fila 3  |
| D3          | Keypad — Fila 4  |
| D4          | Keypad — Col 1   |
| D5          | Keypad — Col 2   |
| D6          | Keypad — Col 3   |
| D7          | Keypad — Col 4   |
| D8          | Servomotor       |
| D9          | RFID RST         |
| D10         | RFID SDA/SS      |
| D11         | RFID MOSI        |
| D12         | RFID MISO        |
| D13         | RFID SCK         |
| A0          | Buzzer           |
| A1          | 74HC595 DATA     |
| A2          | 74HC595 CLOCK    |
| A3          | 74HC595 LATCH    |
| A4          | LCD SDA          |
| A5          | LCD SCL          |

> **Nota sobre D0 y D1:** Estos pines son compartidos con el puerto Serial/USB del Arduino. Al cargar código con el cable USB, se deben desconectar las filas F1 y F2 del teclado para evitar conflictos. Una vez cargado el programa, se debe desconectar el USB y alimentar el sistema con fuente externa para que el teclado funcione correctamente.

> **Nota sobre el RC522:** El módulo opera a 3.3 V. Conectarlo a 5 V puede dañarlo de forma permanente. Se debe verificar el voltaje de alimentación antes de encender el sistema.

---

## Indicadores LED — 74HC595

El 74HC595 controla 8 LEDs que representan el estado actual del sistema. Solo un LED estará encendido a la vez.

| Estado          | Valor binario | LED |
|-----------------|---------------|-----|
| Inicio          | `00000001`    | 1   |
| Esperando RFID  | `00000010`    | 2   |
| Validando RFID  | `00000100`    | 3   |
| Esperando PIN   | `00001000`    | 4   |
| Validando PIN   | `00010000`    | 5   |
| Autorizado      | `00100000`    | 6   |
| Denegado        | `01000000`    | 7   |
| Alarma          | `10000000`    | 8   |

---

## Almacenamiento EEPROM

Los usuarios se almacenan en la EEPROM del Arduino. Cada registro ocupa 9 bytes:

```
Dirección base = índice × 9

[base + 0 .. base + 3] → UID  (4 bytes)
[base + 4 .. base + 8] → PIN  (4 chars + null)
```

La dirección 511 se usa como bandera de inicialización (`0xAA` = ya inicializada). Los datos persisten sin alimentación.

> **Importante:** Si se modifica el arreglo `usuarios[]` y se vuelve a cargar el programa, la EEPROM no se actualizará automáticamente porque la bandera `0xAA` indica que ya fue inicializada. Para forzar la reescritura de los usuarios, se debe cambiar temporalmente el valor de la bandera en el código de `0xAA` a cualquier otro valor, cargar el programa una vez y luego restaurarlo.

Para agregar un usuario, se debe editar el arreglo `usuarios[]` en `base.ino`:

```cpp
Usuario usuarios[] = {
  {{0x45, 0x7A, 0xD7, 0x06}, "1234"}, // Usuario 1 - Tarjeta Blanca
};
```

Para obtener el UID de una tarjeta o llavero, se debe utilizar el sketch `diagnostics/diagnostics.ino`.

---

## Estructura del repositorio

```
Proyecto_FA_ArduinoSecurity2026/
│
├── base/
│   └── base.ino
│
├── diagnostics/
│   └── diagnostics.ino
│
├── 3d-printing/
│   ├── front.stl
│   ├── back.stl
│   ├── door.stl
│   ├── servo_bracket.stl
│   ├── U_rectangular_servo.stl
│   ├── soporte_esquina_M4.stl
│   └── 4_soportes_esquina_M4.stl
│
├── pinout.pdf
└── README.md
```

### `base/`
Programa principal del sistema de control de acceso. Contiene toda la lógica de estados, validación RFID, captura de PIN, control del servo, LEDs y alarma.

### `diagnostics/`
Sketch de diagnóstico de hardware. Prueba automáticamente LCD, LEDs, buzzer y servo al encender. También permite leer el UID de tarjetas RFID por el monitor serial y probar el teclado matricial de forma interactiva. Útil para verificar conexiones antes de usar el programa principal.

### `3d-printing/`
Modelos STL para imprimir la estructura física del proyecto: carcasa frontal y trasera, puerta, soporte del servo y soportes de esquina.

### `pinout.pdf`
Diagrama de referencia de todas las conexiones del sistema.

---

## Manual de usuario

### Encendido

Al conectar la alimentación, el LCD mostrará:

```
SISTEMA ACCESO
INICIANDO...
```

Tras la inicialización, el sistema queda en espera:

```
CONTROL ACCESO
PRESENTE TARJETA
```

El LED 2 estará encendido indicando que el sistema espera una tarjeta RFID.

---

### Acceso al sistema

**Paso 1 — Presentar tarjeta RFID**

Acerque su tarjeta o llavero al lector RC522. El sistema validará si está registrada.

- Si la tarjeta es válida, el LCD mostrará:
  ```
  INGRESE SU PIN
  ____
  ```
- Si la tarjeta no está registrada:
  ```
  TARJETA INVALIDA
  ACCESO DENEGADO
  ```
  El sistema regresará automáticamente a la pantalla principal.

**Paso 2 — Ingresar PIN**

Ingrese los 4 dígitos del PIN con el teclado. Por cada dígito ingresado aparecerá un `*` en el LCD.

| Tecla | Función                                  |
|-------|------------------------------------------|
| `0-9` | Ingresa un dígito                        |
| `*`   | Borra el último dígito ingresado         |
| `#`   | Confirma el PIN (solo con 4 dígitos)     |

- Si el PIN es correcto:
  ```
  ACCESO
  AUTORIZADO
  ```
  El servomotor abrirá la puerta y el LED 6 se encenderá.

- Si el PIN es incorrecto:
  ```
  PIN INCORRECTO
  ACCESO DENEGADO
  ```
  Se registra un intento fallido y el sistema regresa para presentar tarjeta nuevamente.

---

### Puerta abierta

Con la puerta abierta, el LCD mostrará:

```
PUERTA ABIERTA
# PARA CERRAR
```

Presione `#` para cerrar la puerta. El sistema regresará al estado de espera.

---

### Bloqueo por intentos fallidos

Tras **3 intentos incorrectos de PIN**, el sistema se bloquea:

```
ACCESO BLOQUEADO
ALARMA ACTIVADA
```

El buzzer se activará de forma continua y el LED 8 se encenderá. Para desactivar la alarma y desbloquear el sistema, presione la tecla `D`. El sistema regresará al estado de espera.

---

### Diagnóstico de hardware

Para verificar el funcionamiento de los componentes, se debe cargar `diagnostics/diagnostics.ino` en el Arduino. Al encender, ejecutará automáticamente pruebas de LCD, LEDs, buzzer y servo. El diagnóstico puede relanzarse en cualquier momento presionando el botón conectado a D3.

Para obtener el UID de una tarjeta RFID, abra el monitor serial a **9600 baudios** y acerque la tarjeta al lector. El UID aparecerá en el formato `XX:XX:XX:XX`.
