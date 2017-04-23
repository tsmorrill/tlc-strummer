/////////////////////////////////////////////////////////////////////////
//                                                                     //
//   Teensy 3.2 chord strummer with built in audio output              //
//   by Johan Berglund, April 2017                                     //
//                                                                     //
/////////////////////////////////////////////////////////////////////////

#include <Audio.h>
#include <Wire.h>
#include <SD.h>
#include <SPI.h>
#include <SerialFlash.h>

#define MIDI_CH 1            // MIDI channel
#define VELOCITY 64          // MIDI note velocity (64 for medium velocity, 127 for maximum)
#define START_NOTE 60        // MIDI start note (60 is middle C)
#define PADS 8               // number of touch electrodes
#define LED_PIN 13           // LED to indicate midi activity
#define BRIGHT_LED 1         // LED brightness, 0 is low, 1 is high
#define TOUCH_THR 1200       // threshold level for capacitive touch (lower is more sensitive)

#define CHECK_INTERVAL 4     // interval in ms for sensor check

unsigned long currentMillis = 0L;
unsigned long statusPreviousMillis = 0L;

byte colPin[12]          = {15,20,21,5,6,7,8,9,10,11,12,14};// teensy digital input pins for keyboard columns (just leave unused ones empty)
byte colNote[12]         = {1,8,3,10,5,0,7,2,9,4,11,6};     // column to note number                                            
                                                            // column setup for omnichord style (circle of fifths)
                                                            // chord    Db, Ab, Eb, Bb,  F,  C,  G,  D,  A,  E,  B, F#
                                                            // col/note  1,  8,  3, 10,  5,  0,  7,  2,  9,  4, 11,  6
                                                            // for chromatic order, C to B, straight order 0 to 11
                                                            // original tlc strummer design only use pins 5 through 12 (chords Bb to B)

byte rowPin[3]           = {4,3,2};                         // teensy output pins for keyboard rows

                                                            // chord type   maj, min, 7th
                                                            // row            0,   1,   2
                                                            
                                                            // chordType 0 to 7, from binary row combinations
                                                            // 0 0 0 silent
                                                            // 0 0 1 maj
                                                            // 0 1 0 min
                                                            // 0 1 1 dim  (maj+min keys)
                                                            // 1 0 0 7th
                                                            // 1 0 1 maj7 (maj+7th keys)
                                                            // 1 1 0 m7   (min+7th keys)
                                                            // 1 1 1 aug  (maj+min+7th)

byte sensorPin[8]       = {1,0,23,22,19,18,17,16};          // teensy lc touch input pins
byte activeNote[8]      = {0,0,0,0,0,0,0,0};                // keeps track of active notes

byte sensedNote;               // current reading
int noteNumber;                // calculated midi note number
int chord = 0;                 // chord key setting (base note of chord)
int chordType = 0;             // chord type (maj, min, 7th...)

int chordNote[8][8] = {        // chord notes for each pad/string
  {-1,-1,-1,-1,-1,-1,-1,-1 },  // silent
  { 0, 4, 7,12,16,19,24,28 },  // maj 
  { 0, 3, 7,12,15,19,24,27 },  // min 
  { 0, 3, 6, 9,12,15,18,21 },  // dim 
  { 0, 4, 7,10,12,16,19,22 },  // 7th 
  { 0, 4, 7,11,12,16,19,23 },  // maj7
  { 0, 3, 7,10,12,15,19,22 },  // m7  
  { 0, 4, 8,12,16,20,24,28 }   // aug  
};

float midiToFreq[128];         // for storing pre calculated frequencies for note numbers

// GUItool: begin automatically generated code
AudioSynthKarplusStrong  string8;        //xy=251,405
AudioSynthKarplusStrong  string6;        //xy=252,328
AudioSynthKarplusStrong  string1;        //xy=253,141
AudioSynthKarplusStrong  string2;        //xy=254,176
AudioSynthKarplusStrong  string3;        //xy=254,211
AudioSynthKarplusStrong  string5;        //xy=254,289
AudioSynthKarplusStrong  string7;        //xy=254,367
AudioSynthKarplusStrong  string4;        //xy=256,246
AudioMixer4              mixer1;         //xy=637,229
AudioMixer4              mixer2;         //xy=637,332
AudioMixer4              mixer3;         //xy=821,284
AudioOutputAnalog        dac1;           //xy=1010,284
AudioConnection          patchCord1(string8, 0, mixer2, 3);
AudioConnection          patchCord2(string6, 0, mixer2, 1);
AudioConnection          patchCord3(string1, 0, mixer1, 0);
AudioConnection          patchCord4(string2, 0, mixer1, 1);
AudioConnection          patchCord5(string3, 0, mixer1, 2);
AudioConnection          patchCord6(string5, 0, mixer2, 0);
AudioConnection          patchCord7(string7, 0, mixer2, 2);
AudioConnection          patchCord8(string4, 0, mixer1, 3);
AudioConnection          patchCord9(mixer1, 0, mixer3, 0);
AudioConnection          patchCord10(mixer2, 0, mixer3, 1);
AudioConnection          patchCord11(mixer3, dac1);
// GUItool: end automatically generated code

// SETUP
void setup() {
  for (int i = 0; i < 12; i++) {
     pinMode(colPin[i],INPUT_PULLUP);
  }
    for (int i = 0; i < 3; i++) {
     pinMode(rowPin[i],OUTPUT);
     digitalWrite(rowPin[i],LOW);
  }
  pinMode(LED_PIN, OUTPUT);
  for(int i=0;i<128;i++) {  // set up table, midi note number to frequency
      midiToFreq[i] = numToFreq(i);
  }
  AudioMemory(20);
  dac1.analogReference(INTERNAL);   // normal volume
  //dac1.analogReference(EXTERNAL); // louder
  mixer1.gain(0, 0.27);
  mixer1.gain(1, 0.27);
  mixer1.gain(2, 0.27);
  mixer1.gain(3, 0.27);
  mixer2.gain(0, 0.27);
  mixer2.gain(1, 0.27);
  mixer2.gain(2, 0.27);
  mixer2.gain(3, 0.27);
  mixer3.gain(0, 0.27);
  mixer3.gain(1, 0.27);
  delay(100);
}

// MAIN LOOP
void loop() {
  currentMillis = millis();
  if ((unsigned long)(currentMillis - statusPreviousMillis) >= CHECK_INTERVAL) {
    if (BRIGHT_LED) digitalWrite(LED_PIN, LOW);                          // led off for high brightness
    readKeyboard();                                                      // read keyboard input and replay active notes (if any) with new chording
    for (int scanSensors = 0; scanSensors < PADS; scanSensors++) {       // scan sensors for changes and send note on/off accordingly
      sensedNote = (touchRead(sensorPin[scanSensors]) > TOUCH_THR);      // read touch pad/pin/electrode/string/whatever
      if (sensedNote != activeNote[scanSensors]) {
        noteNumber = START_NOTE + chord + chordNote[chordType][scanSensors];
        if ((noteNumber < 128) && (noteNumber > -1) && (chordNote[chordType][scanSensors] > -1)) {    // we don't want to send midi out of range or play silent notes
          digitalWrite(LED_PIN, HIGH);                                // sending midi, so light up led
          if (sensedNote){
              usbMIDI.sendNoteOn(noteNumber, VELOCITY, MIDI_CH);      // send Note On, USB MIDI
              internalNoteOn(noteNumber-12, scanSensors);             // play note with teensy audio (built in DAC)
          } else {
              usbMIDI.sendNoteOff(noteNumber, VELOCITY, MIDI_CH);     // send note Off, USB MIDI
          }
          if (!BRIGHT_LED) digitalWrite(LED_PIN, LOW);                // led off for low brightness
        }  
        activeNote[scanSensors] = sensedNote;         
      }  
    }
    statusPreviousMillis = currentMillis;                             // reset interval timing
  }
}
// END MAIN LOOP

// Check chord keyboard, if changed shut off any active notes and replay with new chord
void readKeyboard() {
  int readChord = 0;
  int readChordType = 0;
  for (int row = 0; row < 3; row++) {     // scan keyboard rows
    enableRow(row);                       // set current row low
    for (int col = 0; col < 12; col++) {  // scan keyboard columns
      if (!digitalRead(colPin[col])) {    // is scanned pin low (active)?
        readChord = colNote[col];         // set chord base note
        readChordType |= (1 << row);      // set row bit in chord type
      }
    }
  }  
  if ((readChord != chord) || (readChordType != chordType)) { // have the values changed since last scan?
    for (int i = 0; i < PADS; i++) {
       noteNumber = START_NOTE + chord + chordNote[chordType][i];
       if ((noteNumber < 128) && (noteNumber > -1) && (chordNote[chordType][i] > -1)) {      // we don't want to send midi out of range or play silent notes
         if (activeNote[i]) {
          digitalWrite(LED_PIN, HIGH);                        // sending midi, so light up led
          usbMIDI.sendNoteOff(noteNumber, VELOCITY, MIDI_CH); // send Note Off, USB MIDI
          if (!BRIGHT_LED) digitalWrite(LED_PIN, LOW);        // led off for low brightness
         }
       }
    }
    for (int i = 0; i < PADS; i++) {
      noteNumber = START_NOTE + readChord + chordNote[readChordType][i];
      if ((noteNumber < 128) && (noteNumber > -1) && (chordNote[readChordType][i] > -1)) {    // we don't want to send midi out of range or play silent notes
        if (activeNote[i]) {
          digitalWrite(LED_PIN, HIGH);                        // sending midi, so light up led
          usbMIDI.sendNoteOn(noteNumber, VELOCITY, MIDI_CH);  // send Note On, USB MIDI
          internalNoteOn(noteNumber-12, i);                   // play note with teensy audio (built in DAC)
          if (!BRIGHT_LED) digitalWrite(LED_PIN, LOW);        // led off for low brightness
        }
      }
    }
    chord = readChord;
    chordType = readChordType;
  }
}

// Set selected row low (active), others to Hi-Z
void enableRow(int row) {
  for (int rc = 0; rc < 3; rc++) {
    if (row == rc) {
      pinMode(rowPin[rc], OUTPUT);
      digitalWrite(rowPin[rc], LOW);
    } else {
      digitalWrite(rowPin[rc], HIGH);
      pinMode(rowPin[rc], INPUT); // Put to Hi-Z for safety against shorts
    }
  }
  delayMicroseconds(30); // wait before reading ports (let ports settle after changing)
}

// MIDI note number to frequency calculation
float numToFreq(int input) {
    int number = input - 21; // set to midi note numbers = start with 21 at A0 
    number = number - 48; // A0 is 48 steps below A4 = 440hz
    return 440*(pow (1.059463094359,number));
}

// play a string pluck sound using the teensy audio library Karplus-Strong algorithm
void internalNoteOn(int note, int num) {
  if (num == 0) {
    if (midiToFreq[note] > 20.0) string1.noteOn(midiToFreq[note], 1.0);
  } else if (num == 1) {
    if (midiToFreq[note] > 20.0) string2.noteOn(midiToFreq[note], 1.0);
  } else if (num == 2) {
    if (midiToFreq[note] > 20.0) string3.noteOn(midiToFreq[note], 1.0);
  } else if (num == 3) {
    if (midiToFreq[note] > 20.0) string4.noteOn(midiToFreq[note], 1.0);
  } else if (num == 4) {
    if (midiToFreq[note] > 20.0) string5.noteOn(midiToFreq[note], 1.0);
  } else if (num == 5) {
    if (midiToFreq[note] > 20.0) string6.noteOn(midiToFreq[note], 1.0);
  } else if (num == 6) {
    if (midiToFreq[note] > 20.0) string5.noteOn(midiToFreq[note], 1.0);
  } else if (num == 7) {
    if (midiToFreq[note] > 20.0) string6.noteOn(midiToFreq[note], 1.0);
  }
}

