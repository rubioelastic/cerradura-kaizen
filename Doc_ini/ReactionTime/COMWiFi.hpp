#ifndef COMWIFIESCOSA
#define COMWIFIESCOSA

#include "COMDefines.hpp"


unsigned long UltimoMensajeRecibido;

bool receiveByte(WiFiClient *_cliente, byte *_b, unsigned long *_tiempo){
  unsigned long _a = millis()+*_tiempo;
  while (!_cliente->available() && _a > millis());
  if (_cliente->available()){
  *_b = _cliente->read();
  *_tiempo = _a - millis();
  UltimoMensajeRecibido = millis();
  return true;
  }
  *_tiempo = 0;
  return false;
}

/*
 * Devuelve true si recive correctamenbte un comando de dos bytes, false en caso contrario.
 * En _command queda guardado el comando en caso de devolver true, basura en caso contrario.
 */
bool receiveCommand(WiFiClient *_cliente,unsigned short *_command, unsigned long *_tiempo){
  byte _b = 0;
  if (receiveByte(_cliente,&_b, _tiempo)){
  *_command = _b << 8; 
  if (receiveByte(_cliente,&_b, _tiempo)){
    *_command = *_command | _b;
    return true;
  }
  }
  return false;
}
/*
 * Devuelve true si ha recibido el comando pasado como parametro en el tiempo especificado
 */
bool receiveCommand(WiFiClient *_cliente,unsigned short _command, unsigned long *_tiempo){
  unsigned short _c = 0;
  if (receiveCommand(_cliente, &_c, _tiempo)){
  if (_command == _c)
    return true;
  }
  return false;
}




/*
 * Envia un comando de 2 bytes
 */
void sendCommand(WiFiClient *_cliente,unsigned short _command){
  _cliente->write((byte)(_command >> 8) & 0xFF);
  _cliente->write((byte)_command & 0xFF);
}
void sendByte(WiFiClient *_cliente,byte b){
  _cliente->write(b);
}
bool receiveCommand(WiFiClient *_cliente,unsigned short _command, unsigned long _tiempo){
  return receiveCommand(_cliente,_command, &_tiempo);
}
bool receiveCommand(WiFiClient *_cliente,unsigned short *_command, unsigned long _tiempo){
  return receiveCommand(_cliente,_command, &_tiempo);
}
boolean readMAC(WiFiClient *_cliente,byte *_mac){
  uint8_t i = 0;
  unsigned long a = 5000;
  while (i < 6 && receiveByte(_cliente,&_mac[i], &a)){
  i++;
  }
  if (i==6)
  return true;
  return false;
}


void enviarMAC(WiFiClient *_client, byte *mac){
  _client->write(mac[0]); //Serial.print(mac[0], HEX); Serial.print(" ");
  _client->write(mac[1]); //Serial.print(mac[1], HEX); Serial.print(" ");
  _client->write(mac[2]); //Serial.print(mac[2], HEX); Serial.print(" ");
  _client->write(mac[3]); //Serial.print(mac[3], HEX); Serial.print(" ");
  _client->write(mac[4]); //Serial.print(mac[4], HEX); Serial.print(" ");
  _client->write(mac[5]); ///Serial.println(mac[5], HEX);
}




















#endif
