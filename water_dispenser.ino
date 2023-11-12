// arduino-based automatic water dispenser control`

#include <EEPROM.h>  // non-volatile storage
#include <LiquidCrystal.h>
// #include <Wire.h>
#include "SR04.h"

/******** ultrasonic sensor setup *********/

#define TRIG_PIN 7
#define ECHO_PIN 6
SR04 sr04 = SR04(ECHO_PIN,TRIG_PIN);
int range;     // measured distance to ultrasonic sensor

/******* digital pins *********/

// const int calButtonPin = 2; // calibration button
const int incrementPin = 4; // increment selected value
const int decrementPin = 3; // decrement selected value
const int dispensePin  = 2; // start dispensing
const int relayPin     = 5; // relay trigger

/******* constants ********/

const int HEIGHTOFFSET = 2;    // dist lower than rim
const int UNCAL_HEIGHT = 20;   // starting height
const int MAX_RANGE    = 22;   // max dist in cm b/t sensor and empty surface
const int AVE_FACTOR   = 10;   // number of cycles to average before stopping
const int DELAY        = 2000; // flow water for DELAY MS after stop height
const int BUTTON_DELAY = 250;  // ms button press interval
const int ACTIVE_DELAY = 250;  // how often to check sensor data when dispensing
const int IDLE_DELAY   = 1000; // how often to check sensor when idle
const int DEBOUNCE     = 25;   // how long to wait before proceeding

/******* variables ********/

volatile long loops = 0;    // number of loops

int aveArray[AVE_FACTOR];         // array to store distances to average over
int average = 20;                 // will start dispensing right away
int stopHeight = EEPROM.read(0);  // get last stored value for stop height
int start_ms = 0;                 // time for non-blocking timing

volatile bool dispensing = false;  // dispensing toggle state
volatile bool full       = false;  // has the cup reached full point this cyle
volatile bool idle       = true;   // have we entered into idle state

///////////////////////// LCD display //////////////////////////////////////////////////////

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(9, 8, 13, 12, 11, 10);

void clearRange(int col, int row, int num){
  // clear [num] number of characters from starting row/col
  lcd.setCursor(col, row);
  for (int i = 0; i < num; i++){
    lcd.print(" ");
  }
}


void displaySetPoint(){
    // display stop distance set point
    clearRange(10, 1, 3);
    lcd.setCursor(10, 1);
    lcd.print(stopHeight);
}


void insertSetPoint(int col, int row){
    // inserts the current set point at location specified
    clearRange(col, row, 3);
    lcd.setCursor(col, row);
    lcd.print(stopHeight);
}


void staticLine (int line) {
  // prints static line of text on LCD

  switch (line) {
    
    case 0:
      // static text while dispensing
      lcd.clear();
      lcd.print(" XX/YYcm  (TTTs)");
      lcd.setCursor(0, 1);
      lcd.print("---ZZcm to go---");
      break;
    case 1:
      // static text for idle state
      lcd.clear();
      lcd.print("-fill to: XXcm -");
      //         0123456789ABCDEF
      lcd.setCursor(0, 1);
      lcd.print("(currently YYcm)");
      //         0123456789ABCDEF
      break;
  }

}


void stopMessage(int counter){
  if (counter == 0){
    lcd.clear();
    lcd.print("cup is full");
  }
  int col = counter % 32;
  int prevCol = (16 + (col)) % 32;
  lcd.setCursor(prevCol, 1);
  lcd.print(" ");
  lcd.setCursor(col, 1);
  lcd.print("*");
  delay(20);
}


void printVariables(){
  // updates LCD display with calculated variables
  // and with the correct formatting

  // convert distance to sensor to fill height
  int fillHeight = stopHeight - range;

  // display the current fill height
  clearRange(1, 0, 2);
  lcd.setCursor(1, 0);
  lcd.print(fillHeight);

  // display stop distance
  clearRange(4, 0, 2);
  lcd.setCursor(4, 0);
  lcd.print(stopHeight);

  // // display average value
  // clearRange(5, 0, 3);
  // lcd.setCursor(5, 0);
  // lcd.print(average);

  // display remaining fill distance
  int remaining = stopHeight - fillHeight;
  clearRange(3, 1 , 2);
  lcd.setCursor(3, 1);
  lcd.print(remaining);

  // display elapsed time
  // TBA
}


void resetAveArray() {
  // initialize array to store readings for averaging
  for (int i = 0; i < AVE_FACTOR; i++) {
    aveArray[i] = UNCAL_HEIGHT;  // allows start right away
    // Serial.println(aveArray[i]);
  }
}


void idleState()
{
  // lcd.clear();
  // lcd.print("STOPPED");

  // write the static display text to screen
  staticLine(1);

  // displaySetPoint();
  insertSetPoint(10, 1);

  while (dispensing == false){

    if (digitalRead(incrementPin) == LOW){
      // decreases water height (increases distance)
      stopHeight++;
      insertSetPoint(10, 1); 
      delay(BUTTON_DELAY);
    } else if (digitalRead(decrementPin) == LOW) {
      // increases water height (decreases distance)
      stopHeight--;
      insertSetPoint(10, 1);
      delay(BUTTON_DELAY);
    } else if (digitalRead(dispensePin) == HIGH){
      // hop out to dispense again
      dispensing = true;
      // allow time to debounce to prevent state flipping
      delay(DEBOUNCE);
    }

  }

  // allows for flow to start right away
  resetAveArray();

  // reset LCD display with static info
  staticLine(0);

}


void dispensingState()
{
  // determines behavior when water is dispensing

  // measure range to water surface
  range = sr04.Distance();

  /*** averaging routine ***/

  if (range >= MAX_RANGE){
    // skip this loop calculation due to errant data
    // we'll essentially recalculate same thing from last cycle
    loops--;
  } else {
    // add current range to average
    aveArray[loops % AVE_FACTOR] = range;
  }

  int sum = 0;

  // sum the last five datapoints
  for (int i = 0; i < AVE_FACTOR; i++) {
    sum += aveArray[i];
  }

  // average range, excluding spurrious readings that were
  // larger than largest possible cup size
  average = sum / AVE_FACTOR;
  
  // update LCD display with new data
  printVariables();

  // check to see if water is a desired height and keep filling
  // if it's not
  if ((average >= stopHeight) && (dispensing == true)) {
    digitalWrite(relayPin, HIGH);
    
    // // set flowing tag to handle final fill
    // dispensing = true;

    // // for debugging
    // clearRange(0, 1, 3);
    // lcd.setCursor(0, 1);
    // lcd.print("on");
  } else if ((average < stopHeight) && (dispensing == true)) {
    /* water level is at stop height and should continue
        to flow a bit longer to ensure average value remains
        below stopHeight to avoid erratic water dispensing
    */
    // dispensing = false;
    full = true;      // cup is now full
    loops = 0;        // to get "full" message to work
    delay(DELAY);

    // switch off flow and reset flag
    digitalWrite(relayPin, LOW);

    // save this current fill level to non-volatile memory
    byte value = stopHeight;
    EEPROM.update(0, value);  // (address, byte value)

    stopMessage(loops);

    // // display relay state on LCD
    // clearRange(0, 1, 3);
    // lcd.setCursor(0, 1);
    // lcd.print("off ");

  }

  // sensor needs time to reset
  delay(100);

}


void setup() {
  // Serial.begin(115200);

  // // wait for serial port to open on native usb devices
  // while (!Serial) {
  //   delay(1);
  // }

  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  //         0123456789ABCDEF

  // clear LCD and display static text
  staticLine(0);

  //***************** button setup **************************//

  // pinMode(calButtonPin, INPUT_PULLUP);
  pinMode(incrementPin, INPUT_PULLUP);
  pinMode(decrementPin, INPUT_PULLUP);
  pinMode(dispensePin, INPUT_PULLUP);
  // controls water flow
  pinMode(relayPin, OUTPUT);

  // make sure water isn't flowing until we want it to flow
  digitalWrite(relayPin, LOW);

  // // interrupt when calibration button is pressed
  // attachInterrupt(digitalPinToInterrupt(dispensePin), dispenserButton, LOW);

  resetAveArray();

  // get start time for for loop timing
  start_ms = millis();

  dispensing = false;
}

void loop() {  
  // check to see if toggle state has changed
  if ((digitalRead(dispensePin) == HIGH) && (dispensing == false)){
    // if toggle is on and we aren't dispensing, start dispensing
    dispensing = true;
    // allow time to debounce to prevent state flipping
    delay(DEBOUNCE);
  } else if (digitalRead(dispensePin) == LOW){
    // stop dispensing if we are and the toggle is flipped low
    dispensing = false;
    full = false;  // reset to fill again
    delay(DEBOUNCE);
  }

  // check to see if dispense toggle is on and switch to
  // an idle state if we are no longer dispensing
  if (full == true){
    stopMessage(loops);
    delay(100);
  } else if (dispensing == false){
    // make sure water isn't flowing until we want it to
    digitalWrite(relayPin, LOW);
    idleState();
  } else if (dispensing == true){
    dispensingState();
  }

  // loop count required for averaging routine above
  if (loops <= 10000) {
    loops++;
  } else {
    loops = 0;
  }

}