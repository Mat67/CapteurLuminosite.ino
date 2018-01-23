/* TSL2591 Digital Light Sensor */
/* Dynamic Range: 600M:1 */
/* Maximum Lux: 88K */

// connect SCL to analog 5
// connect SDA to analog 4
// connect Vin to 3.3-5V DC
// connect GROUND to common ground

#include <Wire.h> 
#include <Adafruit_Sensor.h>
#include "Adafruit_TSL2591.h"
#include <x10rf.h>
#include <JeeLib.h>     // Low power functions library
#define tx 2            // Pin number for the 433mhz OOK transmitter
#define reps 5          // Number of times that a RF command must be repeated.
#define ledPin 8        // Pin for the led that blinks when a command is send. (0 = no blink)
#define sensorPin 0     // Pin pour le capteur de lumière (Ou A0)
//#define DEBUG           // Activation du mode debug si decommente (execution des serial.print)

ISR(WDT_vect) { Sleepy::watchdogEvent(); }
x10rf myx10 = x10rf(tx,ledPin,reps);

// Region constantes 
#ifdef DEBUG
  const long defaultSleepTime = 2000;             // 2 sec
  const float dureeMaxModeNuit = 60000;           // 1 min
#else
  const long defaultSleepTime = 60000;            // Durée du passage en mode veille. Valeur max défini par la librairie JeeLib etant de 65xxx ms. (env1 min)
  const long dureeMaxModeNuit = 25200000;         // Permet de definir la durée maximum du mode nuit. (env 7 heures)
#endif

const float tauxMarge = 0.10;                   // Taux permettant de définir la marge à partir de laquelle on envoie les données
const int seuilLuxNuit = 100;                   // Valeur definissant le seuil pour toute valeur inferieur à celui-ci

int previousSendedValue = 0;
long sleepTime = defaultSleepTime;
bool modeJour = true;
bool modeNuit = false;
long totalSleepedTime = 0;

Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591); // pass in a number for the sensor identifier (for your use later)

// Setup the watchdog
/**************************************************************************/
/*
    Configures the gain and integration time for the TSL2591
*/
/**************************************************************************/
void configureSensor(void)
{
  // You can change the gain on the fly, to adapt to brighter/dimmer light situations
  tsl.setGain(TSL2591_GAIN_LOW);    // 1x gain (bright light)
  //tsl.setGain(TSL2591_GAIN_MED);      // 25x gain
   //tsl.setGain(TSL2591_GAIN_HIGH);   // 428x gain
  
  // Changing the integration time gives you a longer time over which to sense light
  // longer timelines are slower, but are good in very low light situtations!
  tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);  // shortest integration time (bright light)
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_200MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_300MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_400MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_500MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_600MS);  // longest integration time (dim light)

  tsl2591Gain_t gain = tsl.getGain();
}

bool valueIsDifferent(int value1, int value2)
{
  if (value1 == value2) {
    return false;
  }
  else if (value1 > value2) {
    return (value1 * (1 - tauxMarge) > value2);
  }
  else {
    return valueIsDifferent(value2, value1);
  }
}


/**************************************************************************/
/*
    Program entry point for the Arduino sketch
*/
/**************************************************************************/
void setup(void) 
{
  
  #ifdef DEBUG
    Serial.begin(9600);
    #define DEBUG_PRINT(x)     delay(1000); Serial.print (x)
    #define DEBUG_PRINTLN(x)   delay(1000); Serial.println (x)
  #else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
  #endif
  
  DEBUG_PRINTLN(F("Starting Adafruit TSL2591 Test!"));
  
  if (tsl.begin()) 
  {
    DEBUG_PRINTLN(F("Found a TSL2591 sensor"));
  } 
  else 
  {
    DEBUG_PRINTLN(F("No sensor found ... check your wiring?"));
    while (1);
  }
  
  configureSensor(); // COnfiguration du capteur de lumiere

  myx10.begin();
}

void loop(void) 
{ 
  long lux = getLuxValue();
  
  // Si c'est la nuit
  if (lux < seuilLuxNuit) {
    DEBUG_PRINTLN("Lux " + String(lux) + " inferieur au seuil nuit " + String(seuilLuxNuit));
    
    if (modeNuit == true || modeJour == true) 
    {
      if (!modeNuit)
      {
        DEBUG_PRINTLN(F("On passe en mode nuit"));
      }
      else
      {
        DEBUG_PRINTLN(F("Mode nuit"));
      }

      modeJour = false; // Désactivation du mode jour
      modeNuit = true;  // Activation du mode nuit
      
      // Cela signifie qu'on entre dans la nuit
      // Particularite : A partir du moment ou on sort du mode nuit, il faut attendre que le jour ce soit levé pour pouvoir à nouveau rentrer en mode nuit.
      // Cela permet de ne pas rater le levé du soeil, parce qu'on serait à nouveau rentré en mode nuit, et que les durée de sommeil étaient trop important
      
      // Increment de la durée du sommeil
      sleepTime = incrementSleepTime(sleepTime);

      if (totalSleepedTime >= dureeMaxModeNuit)
      {
        DEBUG_PRINTLN("Durée max du mode nuit ("+ String(dureeMaxModeNuit) + ") atteinte : " + String(totalSleepedTime));
        sleepTime = resetSleepTime(sleepTime);
        modeNuit = false; // On désactive le mode nuit (on ne veut pas rater le lever du soleil à cause d'un interval trop important)
      } 
      else 
      {
        totalSleepedTime += sleepTime;
        if (totalSleepedTime > dureeMaxModeNuit)
        {
          DEBUG_PRINTLN("Dépassement de la durée max du mode nuit (" + String(dureeMaxModeNuit) + "). La valeur était " + String(totalSleepedTime));
          long oldSleepTime = sleepTime;
          sleepTime = totalSleepedTime - dureeMaxModeNuit; // On fait en sorte de ne pas dépasser la duréemax du mode nuit
          DEBUG_PRINTLN("Modification du temps de sommeil " + String(oldSleepTime) + " à " + String(sleepTime) + " pourne pas depasser la durée maxdu mode nuit (" + String(dureeMaxModeNuit) + ")");
        }
      }
    }
    else
    {
      DEBUG_PRINTLN(F("Mode nuit désactivé. En attente de la lumiere"));
    }
  } 
  else if (valueIsDifferent(lux, previousSendedValue)) // Si la valeur est suffisament differente (selon le taux de marge)
  {
    if (!modeJour)
    {
      DEBUG_PRINTLN(F("Activation du mode jour"));
      modeJour = true; // On active le mode jour (on ne veut pas rater le lever du soleil à cause d'un interval trop important)
      sleepTime = resetSleepTime(sleepTime);  // Et on restore le temps de veille (En cas de sortie du mode nuit par ex)
    }
    else
    {
      DEBUG_PRINTLN(F("Mode jour"));
    }
      
    sendValue(lux);             // On envoie la valeur
    previousSendedValue = lux;  // On met à jour la valeur envoyée
  }
  else
  {
    DEBUG_PRINTLN(F("Tour pour rien."));
    DEBUG_PRINTLN("La valeur " + String(lux) + "lux est trop peu differente de la valeur précédente : " + String(previousSendedValue));
  }

  sleep(sleepTime);
}

long getLuxValue(void)
{
  // More advanced data read example. Read 32 bits with top 16 bits IR, bottom 16 bits full spectrum
  // That way you can do whatever math and comparisons you want!
  uint32_t lum = tsl.getFullLuminosity();
  uint16_t ir, full;
  ir = lum >> 16;
  full = lum & 0xFFFF;
  long lux = tsl.calculateLux(full, ir);
  DEBUG_PRINT(F("Lux: "));
  DEBUG_PRINTLN(lux);
  return lux;
}

long resetSleepTime(long sleep)
{
  DEBUG_PRINTLN("Reset sleepTime à  " + String(defaultSleepTime));
  totalSleepedTime = 0; // Reinitialisation
  return defaultSleepTime;
}

long incrementSleepTime(long sleep)
{
  DEBUG_PRINT("Incrementation de sleep de " + String(sleep)); 
  sleep *= 1.5;
  DEBUG_PRINTLN(" à " + String(sleep));
  
  return sleep;
}

void sendValue(int value) 
{
  DEBUG_PRINT("Sending ");
  DEBUG_PRINT(String(value) + " Lux ...");
  myx10.RFXmeter(12,0,value);
  DEBUG_PRINTLN("Sended");
}

void sleep(long sleepTime)
{
  DEBUG_PRINT(F("go to sleeping "));
  DEBUG_PRINTLN(String(sleepTime) + " ms");
  long sleepedTime = 0;
  while (sleepedTime < sleepTime)
  {
    DEBUG_PRINT("Sleep... ");
    Sleepy::loseSomeTime(sleepTime);
    sleepedTime += sleepTime;
  }
  
  DEBUG_PRINTLN("Sleeped time : " + String(sleepedTime));
}
