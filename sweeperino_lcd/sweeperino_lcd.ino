#include <Wire.h>
#include <SPI.h>
#include <Si570.h>
#include <si5351.h>  /* Assumes Etherkit Si5351 library from https://github.com/etherkit/Si5351Arduino    */
#include <LiquidCrystal.h>

LiquidCrystal lcd(12, 11, 10, 9, 8, 7);

/* #include <ssd1306_tiny.h> 

ssd1306_tiny display; */

Si5351 si5351;
Si570 *si570=NULL;
#define SI570_I2C_ADDRESS 0x55
char printBuff[20];

#define LOG_AMP A3
//#define WB_POWER_CALIBERATION (-112)
#define WB_POWER_CALIBERATION (-92)
int  dbm_reading = 100;
int power_caliberation = WB_POWER_CALIBERATION;


char serial_in[32], c[30], b[30];
unsigned char serial_in_count = 0;

long frequency, fromFrequency=14150000, toFrequency=30000000, stepSize=100000;

#define TUNING  A2
int tune, previous = 500;
int count = 0;
int  i, pulse;
unsigned long baseTune = 14200000;
boolean sweepBusy = false;

/* display routines */
void printLine1(char *c){
  if (strcmp(c, printBuff)){
    lcd.setCursor(0, 0);
    lcd.print(c);
    strcpy(printBuff, c);
    count++;
  }
}

void printLine2(char *c){
  lcd.setCursor(0, 1);
  lcd.print(c);
}

char *readNumber(char *p, long *number){
  *number = 0;

  sprintf(c, "#%s", p);
  while (*p){
    char c = *p;
    if ('0' <= c && c <= '9')
      *number = (*number * 10) + c - '0';
    else 
      break;
     p++;
  }
  return p;
}

char *skipWhitespace(char *p){
  while (*p && (*p == ' ' || *p == ','))
    p++;
  return p;
} 

/* command 'h' */
void sendStatus(){
  Serial.write("helo v1\n");
  sprintf(c, "from %ld\n", fromFrequency);
  Serial.write(c);
   
  sprintf(c, "to %ld\n", toFrequency);
  Serial.write(c);

  sprintf(c, "step %ld\n", stepSize);
  Serial.write(c);
}

void setFrequency(unsigned long f){
   if (si570 != NULL)
     si570->setFrequency(f);
   else
     si5351.set_freq(f*100ULL, SI5351_CLK0); /* Change this to suit pin out on Si5351 */
   frequency = f;
}

/* command 'g' to begin sweep 
  each response begins with an 'r' followed by the frequency and the raw reading from ad8703 via the adc */
void doSweep(){
  unsigned long x;
  int a;
  
  sweepBusy = 1;
  Serial.write("begin\n");
  printLine1("Sweeping...");
  for (x = fromFrequency; x < toFrequency; x = x + stepSize){
    setFrequency(x);
    delay(10);
    a = analogRead(LOG_AMP) * 2 + (power_caliberation * 10);
    sprintf(c, "r%ld:%d\n", x, a);
    Serial.write(c);
  }
  Serial.write("end\n");

  sweepBusy = 0;
}

/* command 'e' to end sweep */
void endSweep(){
  //to be done
}

void readDetector(){
  int i = analogRead(3);
  sprintf(c, "d%d\n", i);
  Serial.write(c);
}


void parseCommand(char *line){
  char *p = line;
  char command;

  while (*p){
    p = skipWhitespace(p);
    command = *p++;
    
    switch (command){
      case 'f' : //from - start frequency
        p = readNumber(p, &fromFrequency);
        setFrequency(fromFrequency);
        break;
      case 't':
        p = readNumber(p, &toFrequency);
        break;
      case 's':
        p = readNumber(p, &stepSize);     
        break;
      case 'v':
        sendStatus();
        break;
      case 'g':
         sendStatus();
         doSweep();
         break;
      case 'r':
         readDetector();
         break;   
      case 'o':     
      case 'w':
      case 'n':
          break;
      case 'i': /* identifies itself */
        Serial.write("*iSweeperino 2.0\n");
        break;
    }
  } /* end of the while loop */
}

void acceptCommand(){
  int inbyte = 0;
  inbyte = Serial.read();
  
  if (inbyte == '\n'){
    parseCommand(serial_in);    
    serial_in_count = 0;    
    return;
  }
  
  if (serial_in_count < sizeof(serial_in)){
    serial_in[serial_in_count] = inbyte;
    serial_in_count++;
    serial_in[serial_in_count] = 0;
  }
}


void setup()
{  
  lcd.begin(16, 2);
  printBuff[0] = 0;
  printLine1("Sweeperino v0.02");
  // Start serial and initialize the Si5351
  Serial.begin(9600);
  analogReference(DEFAULT);

  Serial.println("*Sweeperino v0.02\n");
  Serial.println("*Testing for Si570\n");

  si570 = new Si570(SI570_I2C_ADDRESS, 56320000);
  if (si570->status == SI570_ERROR) {
    printLine1("Si570 not found");
    Serial.println("*Si570 Not found\n");   
    si570 = NULL;
    
    si5351.init(SI5351_CRYSTAL_LOAD_10PF, 27000000, 0); // Needed for use with latest library  Assumes 27MHz crystal
    Serial.println("*Si5350 ON");       
    printLine2("Si5351 ON");    
    delay(10);
  }
  else {
    Serial.println("*Si570 ON");
     printLine2("Si570 ON");    
  }

  setFrequency(14500000);
  previous  = analogRead(TUNING)/2;
}

void updateDisplay(){
  int j;
  char sign[3];
  sprintf(b, "%9ld", frequency);
  sprintf(c, "%.3s.%.3s.%3s MHz ",  b, b+3, b+6);
  printLine1(c);  
//Following from PA3CMO to correct negative value between 0 and -1dBm
  if (dbm_reading < 0 and dbm_reading/10 == 0) {
    sprintf(sign, "-");
  } else {
    sprintf(sign, "");
  }
  sprintf(c,  " %s%d.%d dBm  ", sign, dbm_reading/10, abs(dbm_reading % 10));
  printLine2(c);
}

void doReading(){
  int new_reading = analogRead(LOG_AMP) * 2 + (power_caliberation * 10);
  if (abs(new_reading - dbm_reading) > 4){
    dbm_reading = new_reading;
    updateDisplay();
  }
}

void doTuning(){
  tune = analogRead(TUNING);    

  if (tune < 20){
    count++;
    if (count < 20){
      baseTune -= 1000;
      setFrequency(baseTune);
      updateDisplay();                   
      delay(100);
    }
    else if (count < 60) {
      baseTune -= 10000;
      setFrequency(baseTune);      
      updateDisplay();                   
      delay(100);
    }
    else {
      baseTune -= 500000;    
      setFrequency(baseTune);
      updateDisplay();             
      delay(500);
    }
    return;
  }
  
  if (tune > 1000){
    count++;
    if (count < 20){
      baseTune += 1000;
      setFrequency(baseTune + 100000);      
      updateDisplay();                   
      delay(100);
    }
    else if (count < 60) {
      baseTune += 10000;
      setFrequency(baseTune + 100000);      
      updateDisplay();             
      delay(100);
    }
    else {
      baseTune += 500000;    
      setFrequency(baseTune + 100000);      
      updateDisplay();             
      delay(500);
    }
    return;
  }
  
  count = 0;
  if (previous != tune){
    setFrequency(baseTune + (100L * (unsigned long)(tune-20)));
    updateDisplay();
    previous = tune;
  }
}

void loop(){
  if (Serial.available()>0)
    acceptCommand();    
  if (!sweepBusy){
    doReading();
    doTuning();
  }
  delay(100);
}

