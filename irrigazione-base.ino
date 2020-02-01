// Date and time functions using a DS3231 RTC connected via I2C and Wire lib
//#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"
//#include "QueueList.h"
#include "CircularBuffer.h"
#include <Adafruit_SSD1306.h>
// include the library code:
const byte VER=1;
const byte MAX_PROG=3;
const byte MAX_STN=4;
const byte BUFFERSIZE=100;
RTC_DS3231 rtc;
Adafruit_SSD1306 display(128, 32, &Wire, -1);
char strOra[6];
char daysOfTheWeek[7][4] = {"Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"};
char buffer[BUFFERSIZE] = "";
DateTime endTime;

//definisce il tipo stazione con durata nome e valvola
typedef struct{
  char name[20];
  byte valvePin; //numero della valvola
  uint8_t duration; //durata in minuti
}station;  

typedef struct{
  char name[20];  
  uint8_t hour;
  uint8_t minute;
  station* sList[MAX_STN];
}program;

program* pList[MAX_PROG]; //lista di programmi (partenze con l'elenco delle stazioni)
CircularBuffer<station*,16> wateringQueue; //buffer di stazioni da far partire

boolean watering = false;
uint8_t currMinute;

station* activeStation;
uint32_t activeStationEnd;


//**test**//
  station s1={"Piscina Grande", 2, 10};
  station s2={"Piscina Tetto", 3, 5};
  station s3={"Gocc. orto", 4, 20};
  station s4={"Prato casacolline", 5, 5};
//**end test**//
void setup () {

#ifndef ESP8266
  while (!Serial); // for Leonardo/Micro/Zero
#endif

  Serial.begin(9600);

  delay(3000); // wait for console opening

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  if (rtc.lostPower()) {
//  if (true) { /** forzo il settaggio dell'ora, DA TOGLIERE */
    Serial.println("RTC lost power, lets set the time!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  // inizializzo al minuto corrente per non avere eccezzioni nel test 
  activeStationEnd = rtc.now().unixtime();
  /** inserisco la programmazione a mano */


 /* program p1={"Prati Mattina", 10, 00, {s1,s2}};
  program p2={"Prati pomeriggio", 16, 00, {s1,s2}};*/
  program p1={"Prati Mattina", 10, 0, {&s1,&s2,&s4}};
  program p2={"Prati Pome", 16, 0, {&s1,&s2,&s4}};
  program p3={"Orto", 7, 0, {&s3}};

  
  pList[0]=&p1;
  pList[1]=&p2;
  pList[2]=&p3;

//potrei fare un ciclo sulle stazioni inserite nei programmi...
pinMode(2,OUTPUT);
pinMode(3,OUTPUT);
pinMode(4,OUTPUT);
pinMode(5,OUTPUT);
digitalWrite(2,HIGH);
digitalWrite(3,HIGH);
digitalWrite(4,HIGH);
digitalWrite(5,HIGH);


  
  for (int i=0;i<MAX_PROG;i++){
    if (pList){//se non e' null
      sprintf(buffer,"programma p%d: %d:%d - %s",i,pList[i]->hour,pList[i]->minute,pList[i]->name);
      Serial.println(buffer);
    }
  }
  display.clearDisplay();
  sprintf(buffer,"Irrigazione V.%d       Vittorio Sestito",VER);
  printInfoLcd(buffer, &display);

}




void loop () {
  DateTime now = rtc.now();

  //esegue il ciclo solo una volta al minuto
  //if (now.second() == 0){
  if (now.minute() != currMinute){
  currMinute = now.minute();
  

// Scrive l'ora sul LCD
    printTimeLcd(now, &display); 

//    printDateTime(now);
  //  Serial.println();
    int p=0,s=0;
    for (p=0; p<MAX_PROG;p++){

      /**debug**/
//    sprintf(buffer, "loop su programmi p%d -- %d:%d",p,pList[p]->hour,pList[p]->minute);
//    Serial.println(buffer);

      
      //controlla se non null e c'e' un programma da far partire
      if (pList[p]&&pList[p]->hour == now.hour() && pList[p]->minute == now.minute()){
        
         Serial.print("Inizio programma: ");  
         Serial.print(pList[p]->name);
         Serial.print(" con numero di stazioni: ");
        for (s=0; s<MAX_STN; s++){
          // se la stazione non e' null
          if(pList[p]->sList[s]){      
            printDateTime(now);
            Serial.print(" - inserisco nella coda di irrigazione: ");
            Serial.print(pList[p]->sList[s]->name);
            Serial.print(" durata: ");
            Serial.print(pList[p]->sList[s]->duration);
            wateringQueue.push(pList[p]->sList[s]);
            Serial.print(" coda irrigazione: ");
            Serial.println(wateringQueue.size());
          }
        }
      }
    }// end for over programs

    //controlla se deve fermare una station
    if(watering && activeStationEnd <= now.unixtime()){
      
        printDateTime(now);
        stopStation(activeStation);
        sprintf(buffer, "last:%s at:%d:%d", activeStation->name, now.hour(),now.minute());    
        printInfoLcd(buffer, &display);

        
        watering=false;
    }
    //processa la coda di irrigazione se non sta gia innaffiando e se ci sono delle station in coda

    if (!watering && !wateringQueue.isEmpty()){
      activeStation = wateringQueue.shift();           // non ho usato pop altrimenti mi faceva un lifo invece del fifo
 
      activeStationEnd = now.unixtime() + (activeStation->duration*60); //number of seconds to add

      
      printDateTime(now);
      startStation(activeStation, now);

      sprintf(buffer, "watering:%s for:%d", activeStation->name, activeStation->duration);    
      printInfoLcd(buffer, &display);

      watering=true;
    }

/*

    lcd.setCursor(0,0);
    lcd.print(now.year(), DEC);
    lcd.print('/');
    lcd.print(now.month(), DEC);
    lcd.print('/');
    lcd.print(now.day(), DEC);
    lcd.print(" (");
    lcd.print(daysOfTheWeek[now.dayOfTheWeek()]);
    lcd.print(") ");
    
    lcd.setCursor(0,1);
    lcd.print(now.hour(), DEC);
    lcd.print(':');
    lcd.print(now.minute(), DEC);
    lcd.print(':');
    lcd.print(now.second(), DEC);

*/
/*    startProg1(now);
    lcd.setCursor(0,3);
    lcd.print("zona numero: ");
    lcd.print(zoneNumber);
    
    // calculate a date which is 7 days and 30 seconds into the future
 /*   DateTime future (now + TimeSpan(7,12,30,6));
    
    Serial.print(" now + 7d + 30s: ");
    Serial.print(future.year(), DEC);
    Serial.print('/');
    Serial.print(future.month(), DEC);
    Serial.print('/');
    Serial.print(future.day(), DEC);
    Serial.print(' ');
    Serial.print(future.hour(), DEC);
    Serial.print(':');
    Serial.print(future.minute(), DEC);
    Serial.print(':');
    Serial.print(future.second(), DEC);
    Serial.println();
    
    Serial.println();*/

    }; //fine ciclo una volta al minuto
}

void printDateTime ( DateTime dateTimeToDisplay ) {
 
//    lcd.clear();
 
    sprintf(buffer,  "%02d:%02d %s %02d/%02d/%d", dateTimeToDisplay.hour(), dateTimeToDisplay.minute(), daysOfTheWeek[dateTimeToDisplay.dayOfTheWeek()], dateTimeToDisplay.day(), dateTimeToDisplay.month(), dateTimeToDisplay.year());
    Serial.print( buffer );

}

void startStation(station* stn, DateTime _now){
    sprintf(buffer,  "start Station: %s valvePin: %d duration: %d remaining minutes: %02d", stn->name,stn->valvePin,stn->duration, (activeStationEnd - _now.unixtime())/60);
    Serial.println( buffer );
    digitalWrite(stn->valvePin,LOW);
}

void stopStation(station* stn){
    sprintf(buffer,  "stop Station: %s valvePin: %d - coda irrigazione:%d",  stn->name,stn->valvePin,wateringQueue.size());
    Serial.println( buffer );
    digitalWrite(stn->valvePin,HIGH);
}

void printTimeLcd(DateTime timeToPrint, Adafruit_SSD1306* targetDisplay){
  //display.clearDisplay();
  targetDisplay->setTextSize(2);             // Normal 1:1 pixel scale
  targetDisplay->setTextColor(WHITE, BLACK);        // Draw white text with black background so overwrites what's underneath
  targetDisplay->setCursor(0,18);             // Start at top-left corner
  sprintf(strOra,"%02d:%02d",timeToPrint.hour(),timeToPrint.minute());
  targetDisplay->println(strOra);
  targetDisplay->display();
}

void printInfoLcd(char info[BUFFERSIZE], Adafruit_SSD1306* targetDisplay){
  targetDisplay->fillRect(0,0,128,17, BLACK);  //draws a black rectangle to erase the lines
  targetDisplay->setTextSize(1);             // Normal 1:1 pixel scale
  targetDisplay->setTextColor(WHITE, BLACK);        // Draw white text with black background so overwrites what's underneath
  targetDisplay->setCursor(0,0);             // Start at top-left corner
  targetDisplay->println(info);
  targetDisplay->display();
  
}
/*
void startProg1(DateTime timeToCompare){
  if(!progRunning1){
    if(progHour1==timeToCompare.hour() && progMin1==timeToCompare.minute()){
      progRunning1=true;
      zoneNumber=0;
      endTime= timeToCompare + TimeSpan(0,0,prog1Durata[zoneNumber],0);
    }
  }else {
    if(timeToCompare.unixtime()>=endTime.unixtime()){
      zoneNumber++;  
      endTime= timeToCompare + TimeSpan(0,0,prog1Durata[zoneNumber],0);
      }else if (zoneNumber >= 2){
        progRunning1=false;  
      }
    }
  
  lcd.setCursor(0,1);
  if(progRunning1){
    lcd.print(zoneName[zoneNumber]);
    lcd.setCursor(0,2);
    sprintf(buffer, "Remaining: %7d", endTime.unixtime()-timeToCompare.unixtime());
    lcd.print(buffer);
  }else{
    lcd.print("  *** All  OFF ***  ");
    lcd.setCursor(0,2);
    lcd.print("                    ");
  }
}*/
