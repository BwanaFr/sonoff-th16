#include <SonoffI7021.h>

uint32_t SonoffI7021::_pulseMaxCycles = microsecondsToClockCycles(1000);  // 1 millisecond timeout for reading pulses from DHT sensor.

/**
 * Constructor
 * @param pin Pin number to use (GPIO14 on Sonoff TH)
 */
SonoffI7021::SonoffI7021(uint8_t pin) : _pin(pin), _temperature(NAN), _humidity(NAN) {
}

/**
 * Destructor
 */
SonoffI7021::~SonoffI7021() {
    pinMode(_pin, INPUT);    
}

/**
 * Waits for a pulse
 * @return number of pulses or -1 if timeout (no pulse detected within 1ms)
 */
int32_t SonoffI7021::expectPulse(bool level) {
    int32_t count = 0;
    while (digitalRead(_pin) == level) {
        if (count++ >= (int32_t)_pulseMaxCycles) {
            return -1;  // Timeout
        }
    }
    return count;
}

float SonoffI7021::getTemperature() {
    return _temperature;
}

float SonoffI7021::getHumidity() {
    return _humidity;
}

bool SonoffI7021::read() {
    int32_t cycles[80];
    uint8_t error = 0;
    uint8_t data[5];
    data[0] = data[1] = data[2] = data[3] = data[4] = 0;

    //Go low for 500us
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
    delayMicroseconds(500);
    noInterrupts();
    //Go high for 40us
    digitalWrite(_pin, HIGH);
    delayMicroseconds(40);
    //Get result
    pinMode(_pin, INPUT_PULLUP);
    delayMicroseconds(10);
    //Test start bit
    if (expectPulse(LOW) == -1) {
        error = 1;
    }else if (expectPulse(HIGH) == -1) {
        error = 1;
    }else{
        //Read data bits
        for (uint32_t i = 0; i < 80; i += 2) {
            cycles[i]   = expectPulse(LOW);
            cycles[i+1] = expectPulse(HIGH);
        }
    }
    interrupts();
    if(error) {
        return false;
    }
    //Decode data
    for (uint32_t i = 0; i < 40; ++i) {
        int32_t lowCycles  = cycles[2*i];
        int32_t highCycles = cycles[2*i+1];
        if ((-1 == lowCycles) || (-1 == highCycles)) {
            return false;
        }
        data[i/8] <<= 1;
        if (highCycles > lowCycles) {
            data[i / 8] |= 1;
        }
    }
    //Compute checksum
    uint8_t checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    if (data[4] != checksum) {
        return false;    
    }
    _humidity = ((data[0] << 8) | data[1]) * 0.1;
    _temperature = (((data[2] & 0x7F) << 8 ) | data[3]) * 0.1;
    if (data[2] & 0x80) {
        _temperature *= -1;
    }
    return true;
}