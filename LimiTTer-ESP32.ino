/* LimiTTer sketch for the Arduino UNO/Pro-Mini.
   It scans the Freestyle Libre Sensor every 5 minutes
   and sends the data to the xDrip Android app. You can
   see the data in the serial monitor of Arduino IDE, too.
   If you want another scan interval, simply change the
   SLEEP_TIME value. 
     
   This sketch is based on a sample sketch for the BM019 module
   from Solutions Cubed.
   Wiring for UNO / Pro-Mini:
   Arduino          ESP32  WemosD1          BM019           BLE-HM11
   IRQ: Pin 9       IO26     IO16 (D0)    DIN: pin 2
   SS: pin 10       IO5      IO15 (D8)    SS: pin 3
   MOSI: pin 11     IO23     IO13 (D7)    MOSI: pin 5 
   MISO: pin 12     IO19     IO12 (D6)    MISO: pin4
   SCK: pin 13      IO18     IO14 (D5)    SCK: pin 6
               IO16,17,21,22              VIN : pin 9
                                          GND : Pin 10
   I/O: pin 2                                  BLE_CHK: pin 15 
   I/O: pin 3                                  VCC: pin 9 
   I/O: pin 5                                  TX: pin 2
   I/O: pin 6                                  RX: pin 4
*/

#define ESP32
#define BUG_SPI
#define PRINTMEM
#include <SPI.h>
#include "ArduinoSort.h"

#ifdef ESP32
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#define ADC_PIN 34              //Pino 34 adicionado dois resitores para queda de tensão e posterior leitura da tensão no pino referido.
#define SERVICE_UART_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#endif

#define MAX_BLE_WAIT 90 // Maximum bluetooth re-connect time in seconds 
#define SLEEP_TIME 300000000 // SleepTime in µs 5 min = 300 s      //
#define MAX_NFC_READTRIES 10 // Amount of tries for every nfc block-scan
#define NFCTIMEOUT 500  // Timeout for NFC response
#define RXBUFSIZE 24
#define NFCMEMSIZE 320  // 40 blocs of 8 bytes
#define NFC15MINPOINTER 26  // 0x1A
#define NFC8HOURPOINTER 27  // 0x1B
#define NFC15MINADDRESS 28  // 0x1C
#define NFC8HOURADDRESS 124 // 0x64
#define NFCSENSORTIMEPOINTER  316 // 0x13C et 0x13D
#define NBEXRAW  5 // Exclus les NBEXRAW valeur dont l'écart type est maximal par rapport à la droite des moindres carré
byte RXBuffer[RXBUFSIZE];
byte NfcMem[NFCMEMSIZE];
byte NFCReady = 0;  // used to track NFC state
int batteryPcnt;
long batteryMv;
int noDiffCount = 0;
int sensorMinutesElapse;
float trend[16];
float A = 0;
float B = 0;

#ifdef ESP32
const int SSPin = 5;  // Slave Select pin
const int IRQPin = 26;  // Sends wake-up pulse for BM019
const int NFCPin1 = 16; // Power pin BM019
const int NFCPin2 = 17; // Power pin BM019                  //ESP32 --> TTGO T7 V1.3 Mini
const int NFCPin3 = 21; // Power pin BM019
const int NFCPin4 = 22; // Power pin BM019
const int MOSIPin = 23;
const int SCKPin = 19;
unsigned long sleeptime;
byte batteryLow;
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR byte FirstRun = 1;
RTC_DATA_ATTR float lastGlucose;
BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL;
BLECharacteristic* pRxCharacteristic = NULL;
bool estConnecte = false;
bool etaitConnecte = false;
//===================================================================================================================

class EtatServeur : public BLEServerCallbacks 
{
    void onConnect(BLEServer* pServer) 
    {
      estConnecte = true;
    }

    void onDisconnect(BLEServer* pServer) 
    {
      estConnecte = false;
    }
};

class CharacteristicUART : public BLECharacteristicCallbacks 
{
    void onWrite(BLECharacteristic *pCharacteristique) 
    {
      std::string rxValue = pCharacteristique->getValue();

      if (rxValue.length() > 0) 
      {
        Serial.println("*********");
        Serial.print("Received Value: ");
        for (int i = 0; i < rxValue.length(); i++)
          Serial.print(rxValue[i]);
        Serial.println();
        Serial.println("*********");
      }
    }
};

//===================================================================================================================

#endif

void setup() {
    BM19PowerOn();
    Serial.begin(9600);
   
#ifdef ESP32
  pinMode(LED_BUILTIN, OUTPUT);
  BLEDevice::init("LimiTTer");
  //BLEDevice::getAddress(); // Retrieve our own local BD BLEAddress
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new EtatServeur());
  BLEService *pServiceUART = pServer->createService(SERVICE_UART_UUID);
  pTxCharacteristic = pServiceUART->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  // Create a BLE Descriptor : Client Characteristic Configuration (for indications/notifications)
  pTxCharacteristic->addDescriptor(new BLE2902());
  pRxCharacteristic = pServiceUART->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new CharacteristicUART());
  pServiceUART->start();
  pServer->getAdvertising()->start();
#endif
}

#ifdef BUG_SPI
void Shift_RXBuf(int LastLSb) {
  int NewMSB = LastLSb * 0x80;
  for(int i= 0; i< RXBUFSIZE;i++) {
    LastLSb = RXBuffer[i] & 0x01;
    RXBuffer[i] = (RXBuffer[i]>> 1) + NewMSB;
    NewMSB = LastLSb * 0x80;
  }
}
#endif

void BM19PowerOn()
{
    pinMode(IRQPin, OUTPUT);
    digitalWrite(IRQPin, HIGH); 
    pinMode(SSPin, OUTPUT);
    digitalWrite(SSPin, HIGH);
    pinMode(NFCPin1, OUTPUT);
    pinMode(NFCPin2, OUTPUT);
    pinMode(NFCPin3, OUTPUT);
    pinMode(NFCPin4, OUTPUT);
    digitalWrite(NFCPin1, HIGH);
    digitalWrite(NFCPin2, HIGH);
    digitalWrite(NFCPin3, HIGH);
    digitalWrite(NFCPin4, HIGH);
    pinMode(MOSIPin, OUTPUT);
    pinMode(SCKPin, OUTPUT);
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    SPI.setClockDivider(SPI_CLOCK_DIV128);
    SPI.begin();
    delay(10);                      // send a wake up
    digitalWrite(IRQPin, LOW);      // pulse to put the 
    delayMicroseconds(100);         // BM019 into SPI
    digitalWrite(IRQPin, HIGH);     // mode 
    delay(10);
    digitalWrite(IRQPin, LOW);
}

void BM19PowerOff()
{
 SPI.end();
 digitalWrite(MOSIPin, LOW);
 digitalWrite(SCKPin, LOW);
 digitalWrite(NFCPin1, LOW);         // Turn off all power sources completely
 digitalWrite(NFCPin2, LOW);         // for maximum power save on BM019.
 digitalWrite(NFCPin3, LOW);
 digitalWrite(NFCPin4, LOW);
 digitalWrite(IRQPin, LOW);
}

void poll_NFC_UntilResponsIsReady()
{
  unsigned long ms = millis();
  byte rb;
//  print_state("poll_NFC_UntilResponsIsReady() - ");
  digitalWrite(SSPin , LOW);
  while ( (RXBuffer[0] != 8) && ((millis() - ms) < NFCTIMEOUT) )
  {
#ifdef BUG_SPI  
    rb = RXBuffer[0] = SPI.transfer(0x03)>>1;  // Write 3 until
#else
    rb = RXBuffer[0] = SPI.transfer(0x03);  // Write 3 until
#endif
//    Serial.printf("SPI polling response byte:%x\r\n", RXBuffer[0]);
    RXBuffer[0] = RXBuffer[0] & 0x08;  // bit 3 is set
  }
  digitalWrite(SSPin, HIGH);
  delay(1);
  if ( millis() - ms > NFCTIMEOUT ) {
    Serial.print("\r\n *** poll timeout *** -> response ");
    Serial.print(rb);
  }
}

void receive_NFC_Response()
{
//  print_state("receive_NFC_Response()");
  byte datalength;
  digitalWrite(SSPin, LOW);
  SPI.transfer(0x02);
  RXBuffer[0] = SPI.transfer(0);
  datalength = RXBuffer[1] = SPI.transfer(0);
#ifdef BUG_SPI  
  datalength=datalength/2;
#endif  
  for (byte i = 0; i < datalength; i++) RXBuffer[i + 2] = SPI.transfer(0);
  digitalWrite(SSPin, HIGH);
  delay(1);
#ifdef BUG_SPI  
  Shift_RXBuf(1);
#endif
}

void SetProtocol_Command() {
  digitalWrite(SSPin, LOW);
  SPI.transfer(0x00);  // SPI control byte to send command to CR95HF
  SPI.transfer(0x02);  // Set protocol command
  SPI.transfer(0x02);  // length of data to follow
  SPI.transfer(0x01);  // code for ISO/IEC 15693
  SPI.transfer(0x0D);  // Wait for SOF, 10% modulation, append CRC
  digitalWrite(SSPin, HIGH);
  delay(10);
 
  poll_NFC_UntilResponsIsReady();

  digitalWrite(SSPin, LOW);
  SPI.transfer(0x02);   // SPI control byte for read         
  RXBuffer[0] = SPI.transfer(0);  // response code
  RXBuffer[1] = SPI.transfer(0);  // length of data
  digitalWrite(SSPin, HIGH);

  if ((RXBuffer[0] == 0) & (RXBuffer[1] == 0)) {  // is response code good?
    Serial.println("Protocol Set Command OK");
    NFCReady = 1; // NFC is ready
  } else {
    Serial.println("Protocol Set Command FAIL");
    NFCReady = 0; // NFC not ready
  }
}

void Inventory_Command() {
  Serial.println("Inventory Command");
  digitalWrite(SSPin, LOW);
  SPI.transfer(0x00);  // SPI control byte to send command to CR95HF
  SPI.transfer(0x04);  // Send Receive CR95HF command
  SPI.transfer(0x03);  // length of data that follows is 0
  SPI.transfer(0x26);  // request Flags byte
  SPI.transfer(0x01);  // Inventory Command for ISO/IEC 15693
  SPI.transfer(0x00);  // mask length for inventory command
  digitalWrite(SSPin, HIGH);
  delay(1);

  poll_NFC_UntilResponsIsReady();
  receive_NFC_Response();
//  Serial.println("RxBuf 0 : " + String(RXBuffer[0]));
//  Serial.println("RxBuf 1 : " + String(RXBuffer[1]));
  

  if (RXBuffer[0] == 0x80 )  // is response code good?
    {

#ifdef PRINTMEM  
 byte oneBlock[8];
 for (int i = 0; i < 8; i++)
   oneBlock[i] = RXBuffer[i+3];
  
  char str[24];
  unsigned char * pin = oneBlock;
  const char * hex = "0123456789ABCDEF";
  char * pout = str;
  for(; pin < oneBlock+8; pout+=2, pin++) {
      pout[0] = hex[(*pin>>4) & 0xF];
      pout[1] = hex[ *pin     & 0xF];
  }
  pout[0] = 0;
  Serial.println("Inventory : " + String(str));
 
#endif
      
    Serial.println("Sensor in range ... OK");
    NFCReady = 2;
    }
  else
    {
    Serial.println("Sensor out of range");
    NFCReady = 1;
    }
 }
 
float Read_Memory() {
Serial.println("Read Memory");
 float currentGlucose;
 float shownGlucose;
 int glucosePointer;
 int histoPointer;
 int raw;
 byte readError = 0;
 int readTry;
 int valididx[16];
 
 for ( int b = 3; b < 40; b++) {
  readTry = 0;
  do {
    readError = 0;   
    digitalWrite(SSPin, LOW);
    SPI.transfer(0x00);           // SPI control byte to send command to CR95HF
    SPI.transfer(0x04);           // Send Receive CR95HF command
    SPI.transfer(0x03);           // length of data that follows
    SPI.transfer(0x02);           // request Flags byte
    SPI.transfer(0x20);           // Read Single Block command for ISO/IEC 15693
    SPI.transfer(b);              // memory block address
    digitalWrite(SSPin, HIGH);
    delay(10);
   
    poll_NFC_UntilResponsIsReady();
    receive_NFC_Response();
     
   if (RXBuffer[0] != 0x80)
       readError = 1;  
    
   for (int i = 0; i < 8; i++) {
     NfcMem[8*b+i]=RXBuffer[i+3];
   }
    readTry++;
  } while( (readError) && (readTry < MAX_NFC_READTRIES) );
  
 }
      
  if (!readError)
    {
      sensorMinutesElapse = (NfcMem[NFCSENSORTIMEPOINTER+1]<<8) + NfcMem[NFCSENSORTIMEPOINTER];
      glucosePointer = NfcMem[NFC15MINPOINTER];
      histoPointer=NfcMem[NFC8HOURPOINTER];
#ifdef PRINTMEM
    Serial.println("Glucose Pointer  : " + String(glucosePointer));
    Serial.println("Histo Pointer  : " + String(histoPointer));
#endif
    float SigmaX=0;
    float SigmaY=0;
    float SigmaX2=0;
    float SigmaXY=0;
    int n = 0;

     for (int j=0; j<16; j++) {     
         raw = (NfcMem[NFC15MINADDRESS + 1 + ((glucosePointer+15-j)%16)*6]<<8) + NfcMem[NFC15MINADDRESS + ((glucosePointer+15-j)%16)*6];
        trend[j] = Glucose_Reading(raw) ;
        valididx[j] = j;
        SigmaX+=j;
        SigmaXY+=j*trend[j];
        SigmaX2+=j*j;
        SigmaY+=trend[j];
        n++;
#ifdef PRINTMEM
        Serial.println("Tendance " + String((j+1)) + "minutes : " + String(trend[j]) + " Raw : " + String(raw));
#endif        
     }

#ifdef PRINTMEM
    for (int j=0; j<32;j++) {
      raw = (NfcMem[NFC8HOURADDRESS + 1 + ((histoPointer+31-j)%32)*6]<<8) + NfcMem[NFC8HOURADDRESS + ((histoPointer+31-j)%32)*6];
      Serial.println("Tendance " + String((j+1)/4) + "h"+ String((j*15+15)%60) + "min : " + String(Glucose_Reading(raw)) + " Raw : " + String(raw));
    }
#endif        
       
   // Valeur lissée par la droite des moindres carrés en considérant les 15 dernière minutes comme linéaire
   // y = A x + B avec A = (n*SigmaXY - SigmaX*SigmaY)/(n*SigmaX2 - SigmaX*SigmaX) et B =MoyenneY - A MoyenneX = (SigmaY - A*SigmaX)/n
   // Valeur renvoyée correspond à l'estimation pour x = 0, la valeur courante lue est remplacée par son estimation selon la droite des moindres carrés

    A = (n*SigmaXY - SigmaX*SigmaY)/(n*SigmaX2 - SigmaX*SigmaX);
    B = (SigmaY - A*SigmaX)/n;

    sortArray(valididx, 16, firstIsLarger);
    /*
    for (int j=0; j<16; j++) {    
      Serial.println(String(valididx[j]) + " " + String((trend[valididx[j]] - A * valididx[j] -B)*(trend[valididx[j]] - A * valididx[j] -B)));
      delay(20);
    }
  */
    Serial.println("Projection moindre carrés 1 : " + String(B));
    Serial.println("Pente 1 : " + String(-A) + " Moyenne : " + String(SigmaY/n));
    Serial.println("Ecart 1 : " + String((B-trend[0])));

    Serial.print("Valeur exclues : ");
    for(int j=16-NBEXRAW; j<16;j++)
      Serial.print(String(valididx[j]+1) + String(" "));
    Serial.println();
    
    SigmaX=0;
    SigmaY=0;
    SigmaX2=0;
    SigmaXY=0;
    n = 0;
     for (int j=0; j<(16-NBEXRAW); j++) {     
        SigmaX+=valididx[j];
        SigmaXY+=valididx[j]*trend[valididx[j]];
        SigmaX2+=valididx[j]*valididx[j];
        SigmaY+=trend[valididx[j]];
        n++;   
     }

    A = (n*SigmaXY - SigmaX*SigmaY)/(n*SigmaX2 - SigmaX*SigmaX);
    B = (SigmaY - A*SigmaX)/n;
    shownGlucose = B;

    currentGlucose = trend[0];

    Serial.println("Projection moindre carrés : " + String(shownGlucose));
    Serial.println("Pente 2 : " + String(-A) + " Moyenne : " + String(SigmaY/n));
    Serial.println("Ecart 2 : " + String((shownGlucose-trend[0])));
   
    if (FirstRun == 1)
       lastGlucose = trend[0];
       
    if ((lastGlucose == currentGlucose) && (sensorMinutesElapse > 21000)) // Expired sensor check
      noDiffCount++;

    if (lastGlucose != currentGlucose) // Reset the counter
      noDiffCount = 0;
    
    
    NFCReady = 2;
    FirstRun = 0;

    if (noDiffCount > 5)
      return 0;
    else  
      return shownGlucose;
    
    }
  else
    {
    Serial.print("Read Memory Block Command FAIL");
    NFCReady = 0;
    readError = 0;
    }
    return 0;
 }

float Glucose_Reading(unsigned int val) {
        int bitmask = 0x0FFF;
        return ((val & bitmask) / 8.5);
}


bool firstIsLarger(int first, int second) {
  if ((trend[first] - A * first -B)*(trend[first] - A * first -B) > (trend[second] - A * second -B)*(trend[second] - A * second -B)) {
    return true;
  } else {
    return false;
  }
}

String Build_Packet(float glucose) {
  Serial.println("Build Packet");
  
// Let's build a String which xDrip accepts as a BTWixel packet

      unsigned long raw = glucose*1000; // raw_value
      String packet = "";
      packet = String(raw);
      packet += ' ';
      packet += "216";
      packet += ' ';
      packet += String(batteryPcnt);
      packet += ' ';
      packet += String(sensorMinutesElapse);
      Serial.print("Glucose level: ");
      Serial.println(glucose);
      delay(100);
      Serial.print("Sensor lifetime: ");
      Serial.print(sensorMinutesElapse);
      Serial.println(" minutes elapsed");
      return packet;
}

void Send_Packet(String packet) {
   if ((packet.substring(0,1) != "0"))
    {
#ifdef ESP32
      Serial.println("UART Over BLE wait connection");
      for (int i=0; ( (i < MAX_BLE_WAIT) && !estConnecte ); i++)
      {
        delay(1000);
        Serial.println("Waiting for BLE connection ...");
      }
      if(estConnecte) {
        int Packet_Size = packet.length() + 1;
        char BlePacket[Packet_Size];
        packet.toCharArray(BlePacket, Packet_Size);
        delay(100); // small delay before sending first packet
        pTxCharacteristic->setValue(BlePacket);
        pTxCharacteristic->notify();  
        Serial.println("Packet sent : " + packet);
        delay(1000); // Send packet twice to avoid missing packet
        pTxCharacteristic->setValue(BlePacket);
        pTxCharacteristic->notify();  
        Serial.println("Packet sent : " + packet);
      } else {
        Serial.println("Pb BLE connection, Packet not sent : " + packet);
      }
      
#endif      
      delay(100);
    }
   else
    {
      Serial.println("");
      Serial.print("Packet not sent! Maybe a corrupt scan or an expired sensor.");
      Serial.println("");
      delay(100);
    }
  }

void goToSleep() {

#ifdef ESP32
 sleeptime = millis();
 esp_sleep_enable_timer_wakeup(SLEEP_TIME - sleeptime*1000+1598000);
 esp_deep_sleep_start();
#else
  delay(30000);

  digitalWrite(NFCPin1, HIGH);
  digitalWrite(NFCPin2, HIGH);
  digitalWrite(NFCPin3, HIGH);
  digitalWrite(NFCPin4, HIGH);

  BM19PowerOn();
  NFCReady = 0;
#endif

}

int read_adc(){
  int adc_value = 0;
  int adc_value_new = 0;
  for (int i=0; i<21; i++) {
      adc_value_new = analogRead(ADC_PIN);
      adc_value = adc_value + adc_value_new;
    }
  adc_value = adc_value/20;
  return adc_value;
}

int read_battery_level(){
  int battery_level = map(read_adc(), 1500, 2150, 0, 100);
  if (battery_level > 100){
    battery_level = 100;
  }
  if (battery_level < 1){
    battery_level = 1;
  }
  return battery_level;
}

void lowBatterySleep() {
 
 SPI.end();
 digitalWrite(MOSIPin, LOW);
 digitalWrite(SCKPin, LOW);
 digitalWrite(NFCPin1, LOW); // Turn off all power sources completely
 digitalWrite(NFCPin2, LOW); // for maximum power save on BM019.
 digitalWrite(NFCPin3, LOW);
 digitalWrite(NFCPin4, LOW);
 digitalWrite(IRQPin, LOW);

 Serial.print("Battery low! LEVEL: ");
 Serial.print(batteryPcnt);
 Serial.print("%");
 Serial.println("");
 delay(100);

 // Switch LED on and then off shortly
    for (int i=0; i<10; i++) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(50);
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
    } 
}

void loop() {

  batteryPcnt = read_battery_level();
  if (batteryPcnt < 1)
    batteryLow = 1;
  while (batteryLow == 1)
  {
    lowBatterySleep();
    batteryPcnt = read_battery_level();
    if (batteryPcnt > 10)
    {
      batteryLow = 0;
      //wakeUp();
      delay(100);
    }
  }
  
  if (NFCReady == 0)
  {
    SetProtocol_Command(); // ISO 15693 settings
    delay(100);
  }
  else if (NFCReady == 1)
  {
    for (int i=0; i<3; i++) {
      Inventory_Command(); // sensor in range?
      if (NFCReady == 2)
        break;
      delay(100);
    }
    if (NFCReady == 1) {
      BM19PowerOff();
      goToSleep();
    }
  }
  else
  {
    String xdripPacket = Build_Packet(Read_Memory());
    BM19PowerOff();
    Send_Packet(xdripPacket);
    goToSleep();
  }
}
