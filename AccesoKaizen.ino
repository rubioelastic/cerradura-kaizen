/*
 * --------------------------------------------------------------------------------------------------------------------
 * Example sketch/program showing how to read new NUID from a PICC to serial.
 * --------------------------------------------------------------------------------------------------------------------
 * This is a MFRC522 library example; for further details and other examples see: https://github.com/miguelbalboa/rfid
 * 
 * Example sketch/program showing how to the read data from a PICC (that is: a RFID Tag or Card) using a MFRC522 based RFID
 * Reader on the Arduino SPI interface.
 * 
 * When the Arduino and the MFRC522 module are connected (see the pin layout beLOW), load this sketch into Arduino IDE
 * then verify/compile and upload it. To see the output: use Tools, Serial Monitor of the IDE (hit Ctrl+Shft+M). When
 * you present a PICC (that is: a RFID Tag or Card) at reading distance of the MFRC522 Reader/PCD, the serial output
 * will show the type, and the NUID if a new card has been detected. Note: you may see "Timeout in communication" messages
 * when removing the PICC from reading distance too early.
 * 
 * @license Released into the public domain.
 * 
 * Typical pin layout used:
 * -----------------------------------------------------------------------------------------
 *             MFRC522      Arduino       Arduino   Arduino    Arduino          Arduino
 *             Reader/PCD   Uno/101       Mega      Nano v3    Leonardo/Micro   Pro Micro
 * Signal      Pin          Pin           Pin       Pin        Pin              Pin
 * -----------------------------------------------------------------------------------------
 * RST/Reset   RST          9             5         D9         RESET/ICSP-5     RST
 * SPI SS      SDA(SS)      10            53        D10        10               10
 * SPI MOSI    MOSI         11 / ICSP-4   51        D11        ICSP-4           16
 * SPI MISO    MISO         12 / ICSP-1   50        D12        ICSP-1           14
 * SPI SCK     SCK          13 / ICSP-3   52        D13        ICSP-3           15
 */

#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>

#define SS_PIN 21
#define RST_PIN 22
#define PULSADOR_PUERTA 4

#define RELE 5

MFRC522 mfrc522(SS_PIN, RST_PIN); // Instance of the class

MFRC522::MIFARE_Key key; 

#define MAX_MATRICULAS 255


bool SonIguales(char *m1, char *s2);
void ReadEmpleado(char *empleado);
bool Habilitado(char *matricula);
bool Es0(char *matricula);
int indice = 0;


char master[9] = {'0','0','4','0','5','1','0','6','\0'};
char master2[9] = {'8','8','0','4','0','2','2','0','\0'};

void habilitarMatricula(String m){
	int i = 0;
	unsigned char nMatriculas = GetNMatriculasHabilitadas();
	while (i < 8){
		EEPROM.write((nMatriculas*8)+i+1,m[i]);
		EEPROM.commit();
		i++;
	}
	 
	nMatriculas++;
	EEPROM.write(0,nMatriculas);
	EEPROM.commit();
}
unsigned char GetNMatriculasHabilitadas(){
	return (unsigned char)EEPROM.read(0);
}
void MostrarMatriculasHabilitadas(){
	unsigned char nMatriculas = GetNMatriculasHabilitadas();
	int i = 0;
	while (i < nMatriculas){
		Serial.print("Matricula: ");
		int j = 0;
		while (j < 8){
			Serial.print((char)EEPROM.read((i*8)+j+1));
			j++;
		}
		i++;
		Serial.println();
	}
}
void setup() { 
	Serial.begin(115200);

  pinMode(RELE,OUTPUT);
  digitalWrite(RELE,LOW);
  
	pinMode(PULSADOR_PUERTA, INPUT_PULLUP);
  while(digitalRead(PULSADOR_PUERTA)==LOW);


	/*EEPROM.begin((255*8)+1);*/
	
	if (!EEPROM.begin((255*8)+1)){
		//Serial.println("ERROR AL INICIALIZAR LA EEPROM");
	}


	
	SPI.begin(); // Init SPI bus

  

 // Chapuza primer arranque
  habilitarMatricula("88040220");
  RemoveMatricula("88040220");
  habilitarMatricula("88040220");
  /*habilitarMatricula("00405106");*/
  int i = 0;
  unsigned char nMatriculas = GetNMatriculasHabilitadas();
  while (i < nMatriculas){
    char m[9];
    GetMatricula(m, i);
    Serial.printf("Matricula habilitada %u: %s\n",i,m);
    i++;
  }

}

void AbrirCerradura(){
	digitalWrite(RELE, HIGH); 
	delay(3000);
	digitalWrite(RELE, LOW); 
}
void Pulsos_AddMode(){
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW);  
	delay(100);	
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW); 
}
void Pulsos_RemoveMode(){
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW);  
	delay(100);	
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW); 
	delay(100);	
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW); 
}
void Pulsos_RemoveAllMode(){
	EEPROM.write(0,0);
	EEPROM.commit();
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW);  
	delay(100);	
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW); 
	delay(100);	
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW); 
	delay(100);	
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW); 
	delay(100);	
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW); 
	delay(100);	
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW); 
	delay(100);	
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW); 
	delay(100);	
	digitalWrite(RELE, HIGH); 
	delay(100);
	digitalWrite(RELE, LOW); 
}
void Pulso(){
	digitalWrite(RELE, HIGH); 
	delay(50);
	digitalWrite(RELE, LOW); 
}
void AddMatricula(char *matricula){
	if (!Habilitado(matricula)){
		int i = 0;
		unsigned char nMatriculas = GetNMatriculasHabilitadas();
		while (i < 8){
			EEPROM.write((nMatriculas*8)+i+1,matricula[i]);
			EEPROM.commit();
			i++;
		}
		nMatriculas++;
		EEPROM.write(0,nMatriculas);
		EEPROM.commit();
	}
	//Serial.printf("Matricula añadida %s\n", matricula);
	Pulso();
}
void RemoveMatricula(char *matricula){
	int i = Indice(matricula);
	while (i != -1){
		unsigned char nMatriculas = GetNMatriculasHabilitadas();
		while (i < nMatriculas){
			int z = 0;
			while (z < 8){

				EEPROM.write((i*8)+z+1,EEPROM.read(((i+1)*8)+z+1));
				EEPROM.commit();
				z++;
			}
			i++;
		}

		nMatriculas--;
		EEPROM.write(0,nMatriculas);
		EEPROM.commit();

		i = Indice(matricula);
	}

	//Serial.printf("Matricula eliminada %s\n", matricula);
	Pulso();
}
void RemoveAllMatriculas(){
	//Serial.println("Todas las matriculas eliminadas");
}
void CopiarMatricula(char *origen, char *destino){
	int i = 0;
	while (i<8){
		destino[i] = origen[i];
		i++;
	}
}
/*void delay(unsigned long tiempo){
	unsigned long aux = millis() + tiempo;
	while (aux > millis()) Serial.print(".");
	Serial.println("");
}*/
void loop() {
	//Serial.println("\n\n\nEstado inicial");
	MostrarMatriculasHabilitadas();
	char matricula[9] = {'0','0','0','0','0','0','0','0','\0'};
	ReadEmpleado(matricula);
	while(Es0(matricula)){
		ReadEmpleado(matricula);
		if (digitalRead(PULSADOR_PUERTA) == LOW) {/*Serial.println("Abrir");*/AbrirCerradura();}
	}

	//delay(2000);
	//Serial.printf("Matricula leida: %s\n",matricula);
	//delay(2000);
	if (SonIguales(matricula, master)){
		//Serial.println("Es el master, abrir");
		AbrirCerradura();
	}else if (SonIguales(matricula, master2)){
		//Serial.println("Es el master, abrir");
		AbrirCerradura();
	}else if (Habilitado(matricula)){
		//Serial.println("Abrir");
		AbrirCerradura();
	}else{
		//Serial.println("No abre");
	}

	ReadEmpleado(matricula);
	if (SonIguales(matricula,master)){
		Pulsos_AddMode();

		//Damos tiempo a que el master retire la tarjeta
		unsigned long tiempo = millis() + 3000;
		ReadEmpleado(matricula);
		while (SonIguales(matricula,master) && tiempo > millis()) ReadEmpleado(matricula);

		if (tiempo < millis()){
			Pulsos_RemoveMode();

			//Damos tiempo a que el master retire la tarjeta

      
			tiempo = millis() + 3000;
			ReadEmpleado(matricula);
			while (SonIguales(matricula,master) && tiempo > millis())ReadEmpleado(matricula);
			if (tiempo < millis()){
				Pulsos_RemoveAllMode();
				RemoveAllMatriculas();
			}else{
				//Aqui eliminamos tarjetas individualmente
				//Serial.println("Comienza tiempo de eliminar matriculas.");
				tiempo = millis() + 3000;
				char ult[9] = {'0','0','0','0','0','0','0','0','\0'};
				while (tiempo > millis()){
					if (!Es0(matricula) && !SonIguales(ult,matricula)){
						RemoveMatricula(matricula);
						tiempo = millis() + 3000;
						CopiarMatricula(matricula, ult);
					}	
					ReadEmpleado(matricula);
				}
				Pulso();
				//Serial.println("FIN");
			}


		}else{
			//Aqui añadimos tarjetas individualmente
			//Serial.println("Comienza tiempo de añadir matriculas.");
			tiempo = millis() + 3000;
			char ult[9] = {'0','0','0','0','0','0','0','0','\0'};
			while (tiempo > millis()){
				if (!Es0(matricula) && !SonIguales(ult,matricula)){
					AddMatricula(matricula);
					tiempo = millis() + 3000;
					CopiarMatricula(matricula, ult);
				}	
				ReadEmpleado(matricula);
			}			
			Pulso();
			//Serial.println("FIN");
		}


		
	}


}

bool Es0(char *matricula){
	char m[9]  = {'0','0','0','0','0','0','0','0','\0'};
	
	if (SonIguales(matricula,m)) return true;

	return false;
}
void GetMatricula(char *matricula, unsigned char i){
	int j = 0;
	while (j < 8){
		matricula[j] = (char)EEPROM.read((i*8)+j+1);
		j++;
	}
	matricula[j] = '\0';
}
int Indice(char *matricula){
	int i = 0;
	unsigned char nMatriculas = GetNMatriculasHabilitadas();
	while (i < nMatriculas){
		char m[9];
		GetMatricula(m, i);
		if (SonIguales(matricula,m)) return i;
		i++;
	}
	return -1;
}
bool Habilitado(char *matricula){
	return Indice(matricula) != -1;
}
bool SonIguales(char *m1, char *m2){
	int i = 0;
	//Serial.printf("Comparar %s con %s\n",m1,m2);
	while (i < 8){
		if (m1[i] != m2[i]) return false;
		i++;
	}
	return true;
}
	
void p(int n){
	Serial.println(n);
}

void ReadEmpleado(char *empleado){
	delay(300);
	//Serial.println("Buscando tarjeta...");
p(0);
	//empleado  = {'0','0','0','0','0','0','0','0','\0'};
	//empleado = "00000000";
	unsigned char z = 0;
	while (z < 8) {empleado[z] = '0';z++;}empleado[z] = '\0';
	
p(1);
	//Hacemos el init y reset del lector de tarjetas en cada iteracion porque es la forma que hay de detectar que quiten la tarjeta
	mfrc522.PCD_Init();
p(2); 
	if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()){
p(3);
		//Serial.println("TARJETA DETECTADA-------------------------------");
		MFRC522::MIFARE_Key key;
		for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
		
		byte buffer1[18];
		byte block = 4;
		byte len = 18;
	
		MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 4, &key, &(mfrc522.uid)); //line 834 of MFRC522.cpp file
p(4);
		if (status != MFRC522::STATUS_OK) {
p(5);
			/*Serial.print(F("Authentication failed: "));
			Serial.println(mfrc522.GetStatusCodeName(status));*/
			mfrc522.PICC_HaltA();
p(6);
			mfrc522.PCD_StopCrypto1();
p(7);
			return;
		}
p(8);
	
		status = mfrc522.MIFARE_Read(block, buffer1, &len);
p(9);
		if (status != MFRC522::STATUS_OK) {
p(10);
			/*Serial.print(F("Reading failed: "));
			Serial.println(mfrc522.GetStatusCodeName(status));*/
			mfrc522.PICC_HaltA();
p(11);
			mfrc522.PCD_StopCrypto1();
p(12);
			return;
		}
p(13);
		mfrc522.PICC_HaltA();
p(14);
		mfrc522.PCD_StopCrypto1();
p(15);

		int i = 0;
		while (i < 8){
			Serial.printf("%c",(char)buffer1[8+i]);
			empleado[i]= (char)buffer1[8+i];
			i++;
		}
p(16);
			
	}
p(17);
	mfrc522.PCD_Reset();
p(18);
}
