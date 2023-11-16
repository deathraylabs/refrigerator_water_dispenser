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
const int incrementPin = 3; // increment selected value
const int decrementPin = 4; // decrement selected value
const int dispensePin  = 2; // start dispensing
const int relayPin     = 5; // relay trigger

/******* constants ********/

const int HEIGHTOFFSET = 2;    // dist lower than rim
// const int UNCAL_HEIGHT = 20;   // starting height
const int MAX_RANGE    = 22;   // max dist in cm b/t sensor and empty surface
const int AVE_FACTOR   = 20;   // number of cycles to average before stopping
const int DELAY        = 1000; // flow water for DELAY MS after stop height
const int BUTTON_DELAY = 250;  // ms button press interval
const int ACTIVE_DELAY = 250;  // how often to check sensor data when dispensing
const int IDLE_DELAY   = 1000; // how often to check sensor when idle
const int DEBOUNCE     = 25;   // how long to wait before proceeding

/******* variables ********/

volatile long loops = 0;    // number of loops

int aveArray[AVE_FACTOR];         // array to store distances to average over
int average = MAX_RANGE;          // will start dispensing right away
int stopHeight = EEPROM.read(0);  // get last stored value for stop height
int dispenseStartTime = 0;        // ms value at start of dispensing
int dispenseElapsedTime = 0;      // container for keeping track of time taken to dispense
bool firstMeasurement = true;     // first measurements are averaged differently

volatile bool dispensing = false;  // dispensing toggle state
volatile bool full       = false;  // has the cup reached full point this cyle
// volatile bool idle       = true;   // have we entered into idle state

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


int waterLevel(int distFromSensor){
  // convert from range to water level

  return MAX_RANGE - distFromSensor;
}


void recordElapsedTime(int ref_ms){
  /* record number of seconds since the reference
      time given in ms. */

  dispenseElapsedTime = (millis() - ref_ms) / 1000;
}


void insertSetPoint(int col, int row){
    // inserts the current set point at location specified
    
    // // water level from base
    // int waterLevel = MAX_RANGE - stopHeight;

    clearRange(col, row, 2);
    lcd.setCursor(col, row);
    lcd.print(waterLevel(stopHeight));
}


void staticLine (int line) {
  // prints static line of text on LCD

  switch (line) {
    
    case 0:
      // static text while dispensing
      lcd.clear();
      lcd.print(" XX/YYcm  (TTTs)");
      //         0123456789ABCDEF
      // lcd.setCursor(0, 1);
      // lcd.print("-- ZZcm to go --");
      break;
    case 1:
      // static text for idle state
      lcd.clear();
      lcd.print(" fill to: XXcm");
      //         0123456789ABCDEF
      // lcd.setCursor(0, 1);
      // lcd.print("(currently YYcm)");
      // //         0123456789ABCDEF
      break;
  }

}


void stopMessage(int counter){
  /* Message displayed after dispensing has stopped
  
    args:  counter - set by external program loop and
                     serves to animate the sequence
  */
  if (counter == 0){
    // only write the top line once
    lcd.clear();
    lcd.print("FINISHED! (XXXs)");
    //         0123456789ABCDEF
    // add the elapsed time to message
    clearRange(11, 0, 3);
    lcd.setCursor(11, 0);
    lcd.print(dispenseElapsedTime);
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
  // using the correct formatting

  // convert distance to sensor to fill height
  int fillHeight = MAX_RANGE - average;

  // display the current fill height
  clearRange(1, 0, 2);
  lcd.setCursor(1, 0);
  lcd.print(fillHeight);

  // display stop distance
  clearRange(4, 0, 2);
  lcd.setCursor(4, 0);
  lcd.print(waterLevel(stopHeight));

  // // display remaining fill distance
  // int remaining = average - stopHeight;
  // clearRange(3, 1 , 2);
  // lcd.setCursor(3, 1);
  // lcd.print(remaining);

  // display elapsed time
  clearRange(11, 0, 3);
  lcd.setCursor(11, 0);
  lcd.print(dispenseElapsedTime);
}


void resetAveArray() {
  // initialize array to store readings for averaging
  for (int i = 0; i < AVE_FACTOR; i++) {
    aveArray[i] = MAX_RANGE;  // allows start right away
    // Serial.println(aveArray[i]);
  }
}


void idleState()
{
  // write the static display text to screen
  staticLine(1);

  // displaySetPoint();
  insertSetPoint(10, 0);

  while (dispensing == false){

    if (digitalRead(incrementPin) == LOW){
      // decreases water height (increases distance)
      stopHeight++;
      insertSetPoint(10, 0); 
      delay(BUTTON_DELAY);
    } else if (digitalRead(decrementPin) == LOW) {
      // increases water height (decreases distance)
      stopHeight--;
      insertSetPoint(10, 0);
      delay(BUTTON_DELAY);
    } else if (digitalRead(dispensePin) == HIGH){
      // hop out to dispense again
      dispensing = true;
      firstMeasurement = true;
      // allow time to debounce to prevent state flipping
      delay(DEBOUNCE);
      // reset timer
      dispenseStartTime = millis();
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

 /********************* averaging routine ***********************/

  if (firstMeasurement){
    // take AVE_FACTOR number of measurements first to
    // set the baseline water height
    for (int i = 0; i < AVE_FACTOR; i++) {
      // measure range to water surface
      range = sr04.Distance();

      // populate array with values
      aveArray[i] = range;
      delay(20);
    }

    // no longer first measurement
    firstMeasurement = false;

    // reset dispense timer
    dispenseStartTime = millis();

  } else{
    // measure range to water surface
    range = sr04.Distance();

    if (range >= (MAX_RANGE + 1)){
      // skip this loop calculation due to errant data
      // we'll essentially recalculate same thing from last cycle
      loops--;
    } else {
      // add current range to average
      aveArray[loops % AVE_FACTOR] = range;
    }
  }

  int sum = 0;

  // sum the last AVE_FACTOR number of datapoints
  for (int i = 0; i < AVE_FACTOR; i++) {
    sum += aveArray[i];
  }

  // average range, excluding spurrious readings that were
  // larger than largest possible cup size
  average = sum / AVE_FACTOR;
  
  /****************** end of averaging routine ***********************/

  // update LCD display with new data
  printVariables();

  // check to see if water is a desired height and keep filling
  // if it's not
  if ((average >= stopHeight) && (dispensing == true)) {
    /* keep dispensing, water hasn't reached stop height yet
        - distance is measured from above the cup and from the sensor
        - water level is higher when the range to sensor is smaller
        -> if ave range > stopHeight, the water level is lower than
           where we want it to stop (reverse of what you might expect)
    */

    digitalWrite(relayPin, HIGH);  // keep switch on

    // record the elapsed time
    recordElapsedTime(dispenseStartTime);
  } else if ((average < stopHeight) && (dispensing == true)) {
    /* water level is at stop height and should continue
        to flow a bit longer to ensure average value remains
        below stopHeight to avoid erratic water dispensing
    */
    // dispensing = false;
    full = true;      // cup is now full
    loops = 0;        // to get "full" animated message to work
    delay(DELAY);

    // record the elapsed time
    recordElapsedTime(dispenseStartTime);

    // turn off fridge water flow using relay
    digitalWrite(relayPin, LOW);

    // save this current fill level to non-volatile memory
    byte value = stopHeight;
    EEPROM.update(0, value);  // (address, byte value)

    stopMessage(loops);
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

  dispensing = false;
}

void loop() {  
  // check to see if toggle state has changed
  if ((digitalRead(dispensePin) == HIGH) && (dispensing == false)){
    // if toggle is on and we aren't dispensing, start dispensing
    dispensing = true;

    // start the elapsed time counter
    dispenseStartTime = millis();

    // allow time to debounce to prevent state flipping
    delay(DEBOUNCE);
  } else if (digitalRead(dispensePin) == LOW){
    // stop dispensing if the toggle switch is flipped low
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
  // also animates the "finished" display
  if (loops <= 10000) {
    loops++;
  } else {
    loops = 0;
  }

}