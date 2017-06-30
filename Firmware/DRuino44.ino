/* 
* Project DRuino44
* Description: DR44 Alternator PWM Control
* Author: Kevin Carlborg
* Rev 1.0
* Target Device: Arduino Nano using ATmega328
* 
* This sketch controls a DR44 alternator. The DR44 needs a 128Hz PWM signal to set 
* the desired voltage output. The voltage output of the DR44 is adjusted by varying 
* the PWM duty cycle from 5 to 95%. Duty Cycles 0-5% and 95-100% are used for diagnostic purposes. 
* See PWM/Voltage scale below. If the DR44 does not see a PWM signal, it defaults to 13.8V output.
* The sketch is setup as a control loop - The PWM duty cycle is adjusted based off of
* the desired target voltage - set by the DEFAULT_VOLTAGE define below. Set this value
* to what voltage you want your DR44 to output.
* The control loop is setup to take 10 voltage measurements, average them, then round 
* to the tenths decimal place as a means of digital filtering. Then we compare the 
* averaged voltage to the target voltage and increase or decrease the PWM duty cycle 
* accordingly.
* 
******* Important Note #1 *******
* This setup requires you to run a sense wire from the output of the alterntor to the Arduino. The 
* ATmega328 chip on the Aruindo runs off of 5V, so we need to step down the alterntor output 
* down to that level. I used a simple voltage divider circuit using a 22k and a 11k Ohm resistor. 
* The Vout of the divider should be 4V with an input of 12 from the battery and 5V with an input of 
* 15V from the DR44. To more accurately calculate the divided voltage, I hand measured these two 
* resistors with a DMM (digital multi meter) and recorded the values as voltageR1 and voltageR2 in 
* this sketch. Even using actual resistor values, the measured votlage from the Arduino did not jive 
* with the voltage measured from a DMM. So I had to add the voltage calibration offset (CAL_OFFSET) define. 
* To set the CAL_OFFSET value to your own needs, you'll need to do the following:
* 1. Set the DEBUG define to 1. 
* 2. Hook up your Arduino to your DR44. Arduino pin 9 to DR44, and Arduino GND to vehicle chassis
* 3. Connect your Arduino to your PC through a USB cable. 
* 4. Start the engine of your vehicle. 
* 5. Open up the Serial Monitor in the Arduino IDE and set the baud to 9600.
* 6. Connect a DMM to the same points (DR44 output and GND) as the Arduino. 
* 7. Compare the DMM measurement to the Voltage measurent in the Serial Monitor.
* 8. Set the CAL_OFFSET define equal to the difference between the DMM measurement
*    and the Arduino measurement - We are only looking for tenths digit resolution here.
*    i.e. CAL_OFFSET = [DMM Measurement] - [Arduino Measurement]
*    If the Arduino is measuring higher than the DMM, this value will be negative.
******************************
*
******** Important Note #2 *******
* Using the voltage divider resistors mentioned above, you will most likely have a combined 
* 33k Ohm resistive path to GND. Since the output of the alternator should be connected to your
* battery - through a substantial fuse, of course. When the engine is off, key off, etc. There 
* will be approximately a 0.4mA draw on your battery through these resistors. This isn't a lot,
* but I didn't want to take a chance on a dead battery. So I ran the voltage sense wire from the 
* output of the alternator through a relay. The relay is driven off the 5V of the Arduino (thus a 
* 5V coil relay was used). And the Arduino 5V isn't present until the key is switched On.
******************************
*
***** DR44 PWM/Voltage scale *****
* Commanded Duty Cycle Generator Output Voltage
* ref: https://ls1tech.com/forums/conversions-hybrids/1333228-2-wire-truck-alternator-wiring.html#post14432728
* 10% 11 V
* 20% 11.56 V
* 30% 12.12 V
* 40% 12.68 V
* 50% 13.25 V
* 60% 13.81 V
* 70% 14.37 V
* 80% 14.94 V
* 90% 15.5 V
* 
***** PWM Frequency Notes *****
* The Required PWM frequency for DR44 is 128Hz
* The Arduino PWM frequency is set to 122.5 Hz 
* This is as close as we can get to the required frequency and it works quite well.
* This sketch is setup to use Ardunio pin 9 to drive the DR44. Alternatively, pin 10
* could be used. The setPwmFrequency function called within setup() sets timer1 to the
* base 122.5 Hz frequency. Pins 9 and 10 are controlled by timer1. See the header above 
* the setPwmFrequency function below for more info.
*/

/******* User Customized Defines  *******/
#define DEBUG             0        //Turns on printing to serial monitor
#define DEFAULT_VOLTAGE   14.5     //Target Start Up Voltage
#define voltageR1         21890.0  //Manually Measured Value for Voltage Sense divider
#define voltageR2         10920.0  //Manually Measured Value for Voltage Sense divider 
#define CAL_OFFSET        0.2      //Voltage Measurement Correction - for some reason the ADC is off a little.

// Arduino Pin Defines
#define ENGINE_TEMP_PIN   A0  //Temp Sensor in thermostat housing, it's a renix with an HO housing so it's not used for anything else
#define TEMP_SENSOR_PIN   A6  //TMP36 Temp sensor
#define VOLTAGE_SENSE_PIN A7  //Measure output of the alternator through a voltage divider
#define DR44_PWN_PIN      9   //Pin to drive the DR44 PWM

/*********  Time Defines  *********/
#define VOLTAGE_SAMPLE_INTERVAL     30

/*********  Global Defines  *********/
#define MAX_SAMPLES           10      //Num voltage measurements to average
#define DEFAULT_PWM           178     //Starting PWM, equivalent to 14.37V
#define ADC_RESOLUTION        1023.0  

/**
 * Use PWM_INCREMENT_FACTOR to find the PWM increment
 * This will speed up the the transition when changing the target set point
 * To find the increment we'll first find the difference between the current measured voltage
 * and the current target Voltage. Then we'll multiply the difference by 10 to find out how many tenths 
 * of volts difference there is. We do this because we are only concerned with tenths place resolution 
 * as a sort of digital filtering. There is approximately 4.5 PWM bits per tenth of a volt output change 
 * from the DR44. So mulitply the number of tenths by 4.5. Finally divide by 4 to give a finer step 
 * increment so that we gradually fade into the new target voltage.
 * Mathmatically: number_of_tenths * pwm_per_tenth / 4 = 10 * 4.5 / 4 = 11.25
 * i.e. Votlage difference of 0.1V = 1.125 PWM step increment
 * i.e. Voltage difference of 1.0V = 11.25 PWM step increment
 * 
 * To change the duty cycle on the Arduino we do an analogWrite with a value of 0 to 255.
 * The value 0 is equal to 0 VDC output. The value 255 is equal to 5 VDC output.
 * The value 128 is (approximately) 50% duty cycle. At 122.5 HZ, the signal is ON for 4ms and OFF for 4ms
 * 
 * Here are some calculated values based on the DR44 PWM/Voltage scale 
 * 0.0563   Volt per 1 PWM %
 * 0.39215     % per PWM bit
 * 0.022078 Volt per PWM bit
 * 45.293    PWM per Volt
 * 4.5293    PWM per Tenth Volt
 */
#define PWM_INCREMENT_FACTOR  11.25

/** Constant Variables
 *  These were chosen as Variables to force compiler to use the preferred data type
 */
const int MIN_PWM = 255*0.20;     //equivalent to 11.56V
const int MAX_PWM = 255*0.85;     //equivalent to 15.22V
const float REF_VOLTAGE = 5.0;   

// Misc Variables
uint8_t sampleCount;
int16_t currentPWM = DEFAULT_PWM;
unsigned long last_sample_time;
float targetVoltage = DEFAULT_VOLTAGE;
float samples[MAX_SAMPLES];
float avgVoltage;  //one significant digit resolution to keep Alternator drive PWM stable

// Function Prototypes
void setPwmFrequency(int pin, int divisor);
float measureVoltage(void);
void initVariables(void);
void clampPWM(void);
float findAVG(void);
float roundToTenths(float value);

void setup() {         
  pinMode(VOLTAGE_SENSE_PIN, INPUT);
  pinMode(DR44_PWN_PIN, OUTPUT);  
  setPwmFrequency(DR44_PWN_PIN, 256); 
  analogWrite(DR44_PWN_PIN, currentPWM ); // Start off with 60% duty cycle
  Serial.begin(9600);
  initVariables();
  delay(3000);  //wait for engine to start
}


void loop() {
  
  unsigned long currentMillis = millis();
  if (currentMillis - last_sample_time >= VOLTAGE_SAMPLE_INTERVAL) {
    last_sample_time = currentMillis;
    samples[sampleCount] = measureVoltage();
    sampleCount++;
  }
  if(sampleCount >= MAX_SAMPLES) {
    sampleCount = 0;
    float avgVoltgeRaw = findAVG();
    avgVoltage = roundToTenths(avgVoltgeRaw); //digital filtering

    if(avgVoltage != targetVoltage) {
      float tempVDiff = targetVoltage - avgVoltage;
      int16_t pwmIncrement = tempVDiff * PWM_INCREMENT_FACTOR;
      currentPWM += pwmIncrement;
      clampPWM();
      analogWrite(DR44_PWN_PIN, currentPWM );
    }
     /* 
      *  old way of dithering the PWM
      if(avgVoltage < targetVoltage) {
        currentPWM += 2;
        clampPWM();
        analogWrite(DR44_PWN_PIN, currentPWM );
      }
      else if (avgVoltage > targetVoltage) {
        currentPWM--;
        clampPWM();
        analogWrite(DR44_PWN_PIN, currentPWM );
      }*/

    if(DEBUG){
      Serial.print("findAVG= ");
      Serial.println(avgVoltgeRaw);
      Serial.print("Voltage= ");
      Serial.println(avgVoltage);
      Serial.print("PWM= ");
      Serial.println(currentPWM);
      delay(1000);
    }
  }
  delay(10);   
}

void initVariables(void) {
  sampleCount = 0;
  avgVoltage = 0.0;
  last_sample_time = 0;
  for(int i=0;i<MAX_SAMPLES;i++)
    samples[i] = 0.0;
}

void clampPWM(void) {
  if(currentPWM > MAX_PWM) 
    currentPWM = MAX_PWM;
  else if(currentPWM < MIN_PWM) 
    currentPWM = MIN_PWM;
}

float measureVoltage(void) {
  return ((REF_VOLTAGE * analogRead(VOLTAGE_SENSE_PIN) / ADC_RESOLUTION) *(voltageR1+voltageR2)/voltageR2)+CAL_OFFSET;
}

float findAVG(void) {
  float avg = 0.0;
  for(int i=0;i<MAX_SAMPLES;i++) {
    avg += samples[i];
  }
  return avg/MAX_SAMPLES;
}

float roundToTenths(float value) {
  int temp = value * 10;
  return temp/10.0;
}


/**
 * Divides a given PWM pin frequency by a divisor.
 * 
 * The resulting frequency is equal to the base frequency divided by
 * the given divisor:
 *   - Base frequencies:
 *      o The base frequency for pins 3, 9, 10, and 11 is 31250 Hz.
 *      o The base frequency for pins 5 and 6 is 62500 Hz.
 *   - Divisors:
 *      o The divisors available on pins 5, 6, 9 and 10 are: 1, 8, 64,
 *        256, and 1024.
 *      o The divisors available on pins 3 and 11 are: 1, 8, 32, 64,
 *        128, 256, and 1024.
 * 
 * PWM frequencies are tied together in pairs of pins. If one in a
 * pair is changed, the other is also changed to match:
 *   - Pins 5 and 6 are paired on timer0
 *   - Pins 9 and 10 are paired on timer1
 *   - Pins 3 and 11 are paired on timer2
 * 
 * Note that this function will have side effects on anything else
 * that uses timers:
 *   - Changes on pins 3, 5, 6, or 11 may cause the delay() and
 *     millis() functions to stop working. Other timing-related
 *     functions may also be affected.
 *   - Changes on pins 9 or 10 will cause the Servo library to function
 *     incorrectly.
 * 
 * Thanks to macegr of the Arduino forums for his documentation of the
 * PWM frequency divisors. His post can be viewed at:
 *   http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1235060559/0#4
 
Pins 5 and 6: controlled by Timer 0 in fast PWM mode (cycle length = 256)
    Setting   Divisor   Frequency
    0x01    1     62500
    0x02      8     7812.5
    0x03      64    976.5625   <--DEFAULT
    0x04    256     244.140625
    0x05    1024    61.03515625
    
    TCCR0B = (TCCR0B & 0b11111000) | <setting>;

Pins 9 and 10: controlled by timer 1 in phase-correct PWM mode (cycle length = 510)
    Setting   Divisor   Frequency
    0x01    1     31372.55
    0x02    8     3921.16
    0x03      64    490.20   <--DEFAULT
    0x04      256     122.55
    0x05    1024    30.64
    
    TCCR1B = (TCCR1B & 0b11111000) | <setting>;

Pins 11 and 3: controlled by timer 2 in phase-correct PWM mode (cycle length = 510)
    Setting   Divisor   Frequency
    0x01    1     31372.55
    0x02    8     3921.16
    0x03      32      980.39
    0x04    64    490.20   <--DEFAULT
    0x05    128     245.10
    0x06      256     122.55
    0x07    1024      30.64
    
    TCCR2B = (TCCR2B & 0b11111000) | <setting>;

All frequencies are in Hz and assume a 16000000 Hz system clock.
  
 */
void setPwmFrequency(int pin, int divisor) {
  byte mode;
  if(pin == 5 || pin == 6 || pin == 9 || pin == 10) {
    switch(divisor) {
      case 1: mode = 0x01; break;
      case 8: mode = 0x02; break;
      case 64: mode = 0x03; break;
      case 256: mode = 0x04; break;
      case 1024: mode = 0x05; break;
      default: return;
    }
    if(pin == 5 || pin == 6) {
      TCCR0B = TCCR0B & 0b11111000 | mode;
    } else {
      TCCR1B = TCCR1B & 0b11111000 | mode;
    }
  } else if(pin == 3 || pin == 11) {
    switch(divisor) {
      case 1: mode = 0x01; break;
      case 8: mode = 0x02; break;
      case 32: mode = 0x03; break;
      case 64: mode = 0x04; break;
      case 128: mode = 0x05; break;
      case 256: mode = 0x06; break;
      case 1024: mode = 0x7; break;
      default: return;
    }
    TCCR2B = TCCR2B & 0b11111000 | mode;
  }
}






