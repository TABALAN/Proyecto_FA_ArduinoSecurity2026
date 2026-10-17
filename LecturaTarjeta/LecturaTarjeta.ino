/*
 ==============================================================
             ARQUITECTURA DEL COMPUTADOR
        PROYECTO: SISTEMA DE CONTROL DE ACCESO
 ==============================================================
  Primera versión que contiene la inicialización y aceptación
  de la tarjeta RFID

  Claudia Mejía - 1127224
  Tony Balán - 1202124
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


const byte RFID_RST = 6;
const byte RFID_SS = 10;


// ==============================================================
// 2. DISPOSITIVOS
// ==============================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo puerta;

MFRC522 rfid(RFID_SS, RFID_RST);


// ==============================================================
// 5. VARIABLES DEL SISTEMA
// ==============================================================

// UID obtenido del RC522

String UIDLeido = "";


// PIN introducido mediante el keypad

char pinIngresado[5] = "";

byte posicionPIN = 0;

const byte TAMANO_UID = 4;

//Para almacenar lo que leemos
byte uidLeido[4];

// Número de intentos incorrectos
byte intentosFallidos = 0;

// ==============================================================
// 3. REGISTRO DE USUARIOS AUTORIZADOS
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
  {{0x45, 0x7A, 0xD7, 0x06}, "1234"}    // Usuario 1 - Claudia
};

const byte NUM_USUARIOS = sizeof(usuarios) / sizeof(usuarios[0]);

// Índice del usuario identificado (se llena al validar la tarjeta)
byte usuarioActual = 0;


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
// 11. LECTURA DEL RFID
// ==============================================================

void mostrarUID()
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
// 12. VALIDACIÓN DE TARJETA
// ==============================================================

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
// 20. REINICIAR CICLO DE AUTENTICACIÓN
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
}


// ==============================================================
// 21. SETUP
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
  // RFID
  // ----------------------------------------------------------

  SPI.begin();

  rfid.PCD_Init();


  // ----------------------------------------------------------
  // INICIO
  // ----------------------------------------------------------

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SISTEMA ACCESO");

  lcd.setCursor(0, 1);
  lcd.print("INICIANDO...");

  delay(1500);

  reiniciarSistema();
}


// ==============================================================
// 22. LOOP
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
        if (tarjetaAutorizada(UIDLeido))
        {
          lcd.clear();

          lcd.setCursor(0, 0);
          lcd.print("TARJETA VALIDA");

          lcd.setCursor(0, 1);
          lcd.print("INGRESE PIN");

          estado = ESPERANDO_PIN;
        }
        else
        {
          estado = ACCESO_DENEGADO;
        }
      }

      break;


    // ========================================================
    // ESTADO 4 - ACCESO DENEGADO
    // ========================================================

    /*
     Se llega a este estado cuando la tarjeta presentada
     no coincide con ningún usuario registrado.

     Muestra el mensaje de rechazo durante 2 segundos
     y luego regresa a la pantalla principal.
    */

    case ACCESO_DENEGADO:

      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("TARJETA INVALIDA");

      lcd.setCursor(0, 1);
      lcd.print("ACCESO DENEGADO");

      delay(2000);

      reiniciarSistema();

      break;
  }
}