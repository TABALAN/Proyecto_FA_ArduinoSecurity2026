void setup() {
  // Inicializa la comunicación serial a 9600 baudios
  Serial.begin(9600);
}

void loop() {
  // Envía el mensaje al monitor serial
  Serial.println("Hola Mundo");
  
  // Espera 1 segundo antes de repetir
  delay(1000);
}