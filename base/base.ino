/*
 ==============================================================
             ARQUITECTURA DEL COMPUTADOR
        PROYECTO: SISTEMA DE CONTROL DE ACCESO

                 PROGRAMA BASE
 ==============================================================

 Este programa contiene la arquitectura general del proyecto.

 Durante los siguientes laboratorios se implementarán
 progresivamente las funciones marcadas como:

 - Inicialización de pantalla LCD.
 - Lectura de tarjeta RFID.

 Al finalizar los laboratorios, este programa se habrá
 convertido en el sistema completo de control de acceso.
 ==============================================================
*/


#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>


// ==============================================================
// 1. MAPA DE PINES
// ==============================================================

/*
 ARDUINO UNO
 --------------------------------------------------------------

 D0  -> Serial RX
 D1  -> Serial TX

 D2  -> Buzzer pasivo
 D3  -> Botón de diagnóstico
 D4  -> Servo

 D5  -> Libre

 D6  -> RFID RST

 D7  -> 74HC595 DATA
 D8  -> 74HC595 CLOCK
 D9  -> 74HC595 LATCH

 D10 -> RFID SDA / SS
 D11 -> RFID MOSI
 D12 -> RFID MISO
 D13 -> RFID SCK

 A0  -> Keypad C1
 A1  -> Keypad C2
 A2  -> Keypad C3
 A3  -> Keypad C4

 A4  -> LCD SDA
 A5  -> LCD SCL
*/


const byte PIN_BUZZER = 2;
const byte PIN_DIAG   = 3;
const byte PIN_SERVO  = 4;

const byte RFID_RST = 6;

const byte DATA  = 7;
const byte CLOCK = 8;
const byte LATCH = 9;

const byte RFID_SS = 10;

const byte columnas[4] = {
  A0, A1, A2, A3
};


// ==============================================================
// 2. DISPOSITIVOS
// ==============================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo puerta;

MFRC522 rfid(RFID_SS, RFID_RST);


// ==============================================================
// 3. REGISTRO DE DESPLAZAMIENTO 74HC595
// ==============================================================

/*
 Cada bit del registro corresponde a un LED de estado.
 Solo un LED estará encendido a la vez.

                  74HC595

             Q0 ─────► LED 1 - Inicio
             Q1 ─────► LED 2 - Esperando RFID
             Q2 ─────► LED 3 - Validando RFID
             Q3 ─────► LED 4 - Esperando PIN
             Q4 ─────► LED 5 - Validando PIN
             Q5 ─────► LED 6 - Autorizado
             Q6 ─────► LED 7 - Denegado
             Q7 ─────► LED 8 - Alarma

 Bits:

       7   6   5   4   3   2   1   0
     ┌───┬───┬───┬───┬───┬───┬───┬───┐
     │AL │DE │AU │VP │EP │VR │ER │IN │
     └───┴───┴───┴───┴───┴───┴───┴───┘

*/

byte registro = B00000001; // Inicio: LED 1 encendido


// ==============================================================
// 4. MAPA DEL KEYPAD
// ==============================================================

char teclas[4][4] = {

  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}

};


// ==============================================================
// 5. VARIABLES DEL SISTEMA
// ==============================================================

// UID obtenido del RC522
String UIDLeido = "";

// Tamaño del UID y almacenar lo que leemos
const byte TAMANO_UID = 4;
byte uidLeido[4];

// PIN introducido mediante el keypad
char pinIngresado[5] = "";
byte posicionPIN = 0;

// Número de intentos incorrectos
byte intentosFallidos = 0;

// Indica si el acceso fue denegado por tarjeta inválida (true) o PIN incorrecto (false)
bool tarjetaInvalida = false;


// ==============================================================
// 6. ESTADOS DEL SISTEMA
// ==============================================================

/*
 La CPU solamente puede encontrarse en uno de estos
 estados en un momento determinado.

 El estado actual determina qué operación realizará
 el programa.
*/

enum EstadoSistema
{
  ESPERANDO_RFID,

  ESPERANDO_PIN,

  VALIDANDO_PIN,

  ACCESO_AUTORIZADO,

  ACCESO_DENEGADO,

  PUERTA_ABIERTA,

  ALARMA
};


EstadoSistema estado = ESPERANDO_RFID;


// ==============================================================
// 7. ACTUALIZAR 74HC595
// ==============================================================

void actualizarRegistro()
{
  digitalWrite(LATCH, LOW);

  shiftOut(DATA, CLOCK, MSBFIRST, registro);

  digitalWrite(LATCH, HIGH);
}


// ==============================================================
// 8. CONTROL DE LEDs DE ESTADO
// ==============================================================

/*
 Envía al 74HC595 el valor binario correspondiente al estado
 activo, encendiendo únicamente el LED asociado.

     Estado           Valor       LED
     Inicio         00000001      1
     Esperando RFID 00000010      2
     Validando RFID 00000100      3
     Esperando PIN  00001000      4
     Validando PIN  00010000      5
     Autorizado     00100000      6
     Denegado       01000000      7
     Alarma         10000000      8
*/

void indicarEstado(byte valorLED)
{
  registro = valorLED;
  actualizarRegistro();
}


// ==============================================================
// 9. EEPROM - ALMACENAMIENTO DE USUARIOS
// ==============================================================

/*
 Cada usuario tiene:
   - uid : identificador único de su tarjeta/llavero RFID (4 bytes)
   - pin : código numérico de 4 dígitos asociado

 Para agregar un usuario nuevo, añadir una entrada al arreglo
 y actualizar NUM_USUARIOS.
*/

struct Usuario {
  byte uid[TAMANO_UID];
  char pin[5];
};

Usuario usuarios[] = {
  {{0x45, 0x7A, 0xD7, 0x06}, "1234"} //Usuario 1 - Tarjeta Blanca
};

const byte NUM_USUARIOS = sizeof(usuarios) / sizeof(usuarios[0]);

// Índice del usuario identificado (se llena al validar la tarjeta)
byte usuarioActual = 0;

// Prototipos manuales para evitar conflicto con el generador de Arduino
void guardarUsuarioEEPROM(byte indice, byte uid[], const char pin[]);
void cargarUsuarioEEPROM(byte indice, Usuario *u);
void inicializarEEPROM();

/*
 Layout en EEPROM (por usuario):

   Dirección base = u * (TAMANO_UID + 4)

   [base + 0 .. base + 3] -> UID  (4 bytes)
   [base + 4 .. base + 7] -> PIN  (4 chars + null)

   Ejemplo con 1 usuario:
     Dirección 0-3  -> UID
     Dirección 4-7  -> PIN
*/

const byte TAMANO_REGISTRO = TAMANO_UID + 5; // 4 bytes UID + 4 chars PIN + null

void guardarUsuarioEEPROM(byte indice, byte uid[], const char pin[])
{
  // Calcular dirección base del usuario en EEPROM
  int base = indice * TAMANO_REGISTRO;

  // Escribir los 4 bytes del UID
  for (byte i = 0; i < TAMANO_UID; i++)
    EEPROM.write(base + i, uid[i]);

  // Escribir los 4 dígitos del PIN más el terminador nulo
  for (byte i = 0; i < 5; i++)
    EEPROM.write(base + TAMANO_UID + i, pin[i]);
}

void cargarUsuarioEEPROM(byte indice, Usuario *u)
{
  // Calcular dirección base del usuario en EEPROM
  int base = indice * TAMANO_REGISTRO;

  // Leer los 4 bytes del UID
  for (byte i = 0; i < TAMANO_UID; i++)
    u->uid[i] = EEPROM.read(base + i);

  // Leer los 4 dígitos del PIN más el terminador nulo
  for (byte i = 0; i < 5; i++)
    u->pin[i] = EEPROM.read(base + TAMANO_UID + i);
}

void inicializarEEPROM()
{
  /*
   Escribe los usuarios del arreglo usuarios[] en EEPROM
   solo si la EEPROM no ha sido inicializada antes.

   Usa la dirección 511 como bandera:
     0xAA -> ya inicializada, no sobreescribir
     otro -> primera vez, escribir usuarios
  */

  if (EEPROM.read(511) == 0xAA)
  {
    // EEPROM ya tiene datos: cargar usuarios desde ahí
    for (byte u = 0; u < NUM_USUARIOS; u++)
      cargarUsuarioEEPROM(u, &usuarios[u]);

    return;
  }

  // Primera vez: escribir los usuarios definidos en el arreglo
  for (byte u = 0; u < NUM_USUARIOS; u++)
    guardarUsuarioEEPROM(u, usuarios[u].uid, usuarios[u].pin);

  // Marcar EEPROM como inicializada
  EEPROM.write(511, 0xAA);
}


// ==============================================================
// 10. SELECCIÓN DE FILA DEL KEYPAD
// ==============================================================

void seleccionarFila(byte fila)
{
  /*
   TODO - LABORATORIO KEYPAD

   Para detectar una tecla:

   1. Colocar Q0-Q3 en HIGH.

   2. Colocar únicamente la fila que se desea
      consultar en LOW.

   3. Enviar el nuevo valor al 74HC595.

   Pista:

       bitClear()
  */
}


// ==============================================================
// 11. LECTURA DEL KEYPAD
// ==============================================================

char leerTeclado()
{
  /*
   TODO - LABORATORIO KEYPAD

   Implementar el barrido de la matriz 4x4.

   El algoritmo deberá:

   1. Seleccionar una fila.
   2. Leer las cuatro columnas.
   3. Determinar qué tecla fue presionada.
   4. Manejar el rebote de la tecla.
   5. Retornar el carácter correspondiente.

   Si ninguna tecla fue presionada:

       return 0;
  */

  return 0;
}


// ==============================================================
// 12. LECTURA DEL RFID
// ==============================================================
String leerTarjeta()
{
  /*
   Lectura del módulo RC522.

   1. Verifica si hay una nueva tarjeta presente.
   2. Lee el UID de la tarjeta.
   3. Copia los bytes al arreglo global uidLeido[].
   4. Construye y retorna el UID como String hexadecimal.
   5. Detiene la comunicación con la tarjeta.

   Retorna "" si no hay tarjeta presente.
  */

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial())
    return "";

  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++)
  {
    // Guardar cada byte en el arreglo global para comparación posterior
    uidLeido[i] = rfid.uid.uidByte[i];

    // Agregar cero a la izquierda si el byte es menor a 0x10
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";

    uid += String(rfid.uid.uidByte[i], HEX);
  }

  // Detener comunicación con la tarjeta actual
  rfid.PICC_HaltA();

  mostrarUID();

  return uid;
}


// ==============================================================
// 13. VALIDACIÓN DE TARJETA
// ==============================================================

void mostrarUID() //Para ver en la terminal el UID detectado
{
  Serial.print("UID detectado: ");

  for (byte i = 0; i < rfid.uid.size; i++)
  {
    if (rfid.uid.uidByte[i] < 0x10)
    {
      Serial.print("0");
    }

    Serial.print(rfid.uid.uidByte[i], HEX);
    Serial.print(" ");
  }

  Serial.println();
}

bool tarjetaAutorizada(String uid)
{
  /*
   Busca el UID leído en el arreglo de usuarios registrados.

   Recorre cada usuario y compara byte a byte su UID
   con el contenido de uidLeido[].

   Si encuentra coincidencia:
     - Guarda el índice en usuarioActual (para usar su PIN después)
     - Retorna true

   Si ningún usuario coincide, retorna false.
  */

  for (byte u = 0; u < NUM_USUARIOS; u++)
  {
    bool coincide = true;

    for (byte i = 0; i < TAMANO_UID; i++)
    {
      if (uidLeido[i] != usuarios[u].uid[i])
      {
        coincide = false;
        break;
      }
    }

    if (coincide)
    {
      // Guardar qué usuario fue identificado
      usuarioActual = u;
      return true;
    }
  }

  return false;
}


// ==============================================================
// 14. CAPTURA DEL PIN
// ==============================================================

bool leerPIN()
{
  /*
   TODO - LABORATORIO KEYPAD / INTEGRACIÓN

   Utilizar:

       leerTeclado()

   para capturar un PIN de cuatro números.

   Cada número deberá almacenarse en:

       pinIngresado

   Cuando existan cuatro números:

       return true;

   Mientras el usuario continúe escribiendo:

       return false;


   SUGERENCIA:

   En el LCD no mostrar los números.

   Mostrar:

       *
       **
       ***
       ****
  */

  return false;
}


// ==============================================================
// 15. VALIDACIÓN DEL PIN
// ==============================================================

bool validarPIN()
{
  /*
   TODO - LABORATORIO EEPROM

   El PIN autorizado se encuentra almacenado
   en la memoria EEPROM.

   El algoritmo deberá:

   1. Leer el PIN almacenado.
   2. Compararlo con pinIngresado.
   3. Determinar si son iguales.

   Retornar:

       true  -> PIN correcto
       false -> PIN incorrecto
  */

  return false;
}


// ==============================================================
// 16. ABRIR PUERTA
// ==============================================================

void abrirPuerta()
{
  /*
   TODO - LABORATORIO SERVO

   Mover el servo a la posición correspondiente
   a la puerta abierta.
  */
}


// ==============================================================
// 17. CERRAR PUERTA
// ==============================================================

void cerrarPuerta()
{
  /*
   TODO - LABORATORIO SERVO

   Mover el servo a la posición correspondiente
   a la puerta cerrada.
  */
}


// ==============================================================
// 18. SONIDO DE ACCESO CORRECTO
// ==============================================================

void sonidoCorrecto()
{
  /*
   TODO - LABORATORIO BUZZER

   Utilizar el buzzer pasivo para generar
   un sonido que indique acceso autorizado.
  */
}


// ==============================================================
// 19. SONIDO DE ERROR
// ==============================================================

void sonidoError()
{
  /*
   TODO - LABORATORIO BUZZER

   Generar un sonido diferente que permita
   reconocer un acceso incorrecto.
  */
}


// ==============================================================
// 20. ALARMA
// ==============================================================

void activarAlarma()
{
  /*
   TODO - PROYECTO FINAL

   Diseñar el comportamiento de la alarma.

   Puede utilizar:

       - LED rojo
       - Buzzer
       - LCD

   La implementación queda a criterio
   de cada equipo.
  */
}


// ==============================================================
// 21. REINICIAR CICLO DE AUTENTICACIÓN
// ==============================================================

void reiniciarSistema()
{

  // Limpiar variables de sesión
  UIDLeido = "";
  posicionPIN = 0;
  pinIngresado[0] = '\0';
  usuarioActual = 0;


  // Mostrar pantalla principal de espera
  // Línea 0: hora actual (placeholder hasta integrar RTC)
  // Línea 1: instrucción al usuario
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("00:00");

  lcd.setCursor(0, 1);
  lcd.print("PRESENTE TARJETA");

  estado = ESPERANDO_RFID;
  indicarEstado(B00000010); // LED 2: Esperando RFID
}


// ==============================================================
// 22. SETUP
// ==============================================================

void setup()
{

  // ----------------------------------------------------------
  // SERIAL
  // ----------------------------------------------------------

  Serial.begin(9600);


  // ----------------------------------------------------------
  // LCD
  // ----------------------------------------------------------

  lcd.init();

  lcd.backlight();


  // ----------------------------------------------------------
  // 74HC595
  // ----------------------------------------------------------

  pinMode(DATA, OUTPUT);
  pinMode(CLOCK, OUTPUT);
  pinMode(LATCH, OUTPUT);

  actualizarRegistro();


  // ----------------------------------------------------------
  // KEYPAD
  // ----------------------------------------------------------

  for (byte i = 0; i < 4; i++)
  {
    pinMode(columnas[i], INPUT_PULLUP);
  }


  // ----------------------------------------------------------
  // BUZZER
  // ----------------------------------------------------------

  pinMode(PIN_BUZZER, OUTPUT);


  // ----------------------------------------------------------
  // BOTÓN DIAGNÓSTICO
  // ----------------------------------------------------------

  pinMode(PIN_DIAG, INPUT_PULLUP);


  // ----------------------------------------------------------
  // SERVO
  // ----------------------------------------------------------

  puerta.attach(PIN_SERVO);


  // ----------------------------------------------------------
  // RFID
  // ----------------------------------------------------------

  SPI.begin();

  rfid.PCD_Init();


  // ----------------------------------------------------------
  // EEPROM
  // ----------------------------------------------------------

  inicializarEEPROM();


  // ----------------------------------------------------------
  // INICIO
  // ----------------------------------------------------------

  indicarEstado(B00000001); // LED 1: Inicio

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SISTEMA ACCESO");

  lcd.setCursor(0, 1);
  lcd.print("INICIANDO...");

  delay(1500);

  reiniciarSistema();
}


// ==============================================================
// 23. LOOP
// ==============================================================

void loop()
{

  /*
   ============================================================
                       UNIDAD DE CONTROL
   ============================================================

   La variable "estado" representa el estado actual
   del sistema.

   Dependiendo de ese estado, la CPU ejecutará una
   operación diferente.
  */


  switch (estado)
  {

    // ========================================================
    // ESTADO 0 - ESPERANDO RFID
    // ========================================================

    case ESPERANDO_RFID:

      UIDLeido = leerTarjeta();

      if (UIDLeido != "")
      {
        indicarEstado(B00000100); // LED 3: Validando RFID

        if (tarjetaAutorizada(UIDLeido))
        {
          indicarEstado(B00001000); // LED 4: Esperando PIN

          lcd.clear();

          lcd.setCursor(0, 0);
          lcd.print("TARJETA VALIDA");

          lcd.setCursor(0, 1);
          lcd.print("INGRESE PIN");

          estado = ESPERANDO_PIN;
        }
        else
        {
          indicarEstado(B01000000); // LED 7: Denegado
          tarjetaInvalida = true;
          estado = ACCESO_DENEGADO;
        }
      }

      break;


    // ========================================================
    // ESTADO 1 - ESPERANDO PIN
    // ========================================================

    case ESPERANDO_PIN:

      if (leerPIN())
      {
        indicarEstado(B00010000); // LED 5: Validando PIN
        estado = VALIDANDO_PIN;
      }

      break;


    // ========================================================
    // ESTADO 2 - VALIDANDO PIN
    // ========================================================

    case VALIDANDO_PIN:

      if (validarPIN())
      {
        indicarEstado(B00100000); // LED 6: Autorizado
        estado = ACCESO_AUTORIZADO;
      }
      else
      {
        indicarEstado(B01000000); // LED 7: Denegado
        intentosFallidos++;
        tarjetaInvalida = false;
        estado = ACCESO_DENEGADO;
      }

      break;


    // ========================================================
    // ESTADO 3 - ACCESO AUTORIZADO
    // ========================================================

    case ACCESO_AUTORIZADO:

      indicarEstado(B00100000); // LED 6: Autorizado

      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("ACCESO");

      lcd.setCursor(0, 1);
      lcd.print("AUTORIZADO");


      sonidoCorrecto();

      abrirPuerta();


      estado = PUERTA_ABIERTA;

      break;


    // ========================================================
    // ESTADO 4 - ACCESO DENEGADO
    // ========================================================

    case ACCESO_DENEGADO:

      lcd.clear();

      if (tarjetaInvalida)
      {
        // Tarjeta no registrada en el sistema
        lcd.setCursor(0, 0);
        lcd.print("TARJETA INVALIDA");

        lcd.setCursor(0, 1);
        lcd.print("ACCESO DENEGADO");
      }
      else
      {
        // PIN incorrecto
        lcd.setCursor(0, 0);
        lcd.print("PIN INCORRECTO");

        lcd.setCursor(0, 1);
        lcd.print("ACCESO DENEGADO");
      }

      sonidoError();

      delay(1500);


      if (intentosFallidos >= 3)
      {
        estado = ALARMA;
      }
      else
      {
        reiniciarSistema();
      }

      break;


    // ========================================================
    // ESTADO 5 - PUERTA ABIERTA
    // ========================================================

    case PUERTA_ABIERTA:

      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("PUERTA ABIERTA");


      delay(3000);


      cerrarPuerta();

      intentosFallidos = 0;


      reiniciarSistema();

      break;


    // ========================================================
    // ESTADO 6 - ALARMA
    // ========================================================

    case ALARMA:

      indicarEstado(B10000000); // LED 8: Alarma

      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("!!! ALARMA !!!");


      activarAlarma();

      break;
  }
}