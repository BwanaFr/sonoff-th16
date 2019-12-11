#ifndef SONOFFI7021
#define SONOFFI7021
#include <Arduino.h>

class SonoffI7021{

public:
    /**
     * Constructor
     * @param pin Pin number to use (GPIO14 on Sonoff TH)
     */
    SonoffI7021(uint8_t pin);

    /**
     * Destructor
     */
    ~SonoffI7021();

    /**
     * Perform reading of the sensor
     * @return true on success
     */
    bool read();

    /**
     * Gets the latest temperature read
     * @return Latest temperature read or NAN if failed/no previous read() call
     */
    float getTemperature();
    
    /**
     * Gets the latest humidity read
     * @return Latest humidity read or NAN if failed/no previous read() call
     */
    float getHumidity();
    
private:
    static uint32_t _pulseMaxCycles;
    uint8_t _pin;
    float _temperature;
    float _humidity;
    int32_t expectPulse(bool level);
};

#endif