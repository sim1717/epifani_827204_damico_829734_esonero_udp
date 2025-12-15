/*
 * protocol.h
 *
 * Server header file
 * Definitions, constants and function prototypes for the server
 */
#ifndef PROTOCOL_H_
#define PROTOCOL_H_
#include <stdlib.h>
#include <time.h>
// Shared application parameters
#define PROTO_PORT 56700  // Server port (change if needed)
#define BUFFER_SIZE 512    // Buffer size for messages
#define QLEN 5       // Size of pending connections queue
#define NUM_CITIES 10
typedef struct {
    char type;        // Weather data type: 't', 'h', 'w', 'p'
    char city[64];    // City name (null-terminated string)
} weather_request_t;

typedef struct {
    unsigned int status;  // Response status code
    char type;            // Echo of request type
    float value;          // Weather data value
} weather_response_t;

static const char *SUPPORTED_CITIES[NUM_CITIES] = {
    "bari",
    "roma",
    "milano",
    "napoli",
    "torino",
    "palermo",
    "genova",
    "bologna",
    "firenze",
    "venezia"
};

// Prototipi funzioni di generazione dati
float get_temperature(void);
float get_humidity(void);
float get_wind(void);
float get_pressure(void);


float get_temperature(void) {
    return (float)(rand() % 500 - 100) / 10.0f; // da -10.0 a 40.0 °C
}

float get_humidity(void) {
    return (float)(rand() % 800 + 200) / 10.0f; //da 20.0 a 100.0 %
}

float get_wind(void) {
    return (float)(rand() % 1000) / 10.0f; //da 0.0 a 100.0 km/h
}

float get_pressure(void) {
    return (float)(rand() % 10000 + 95000) / 100.0f; // da 950.0 a 1050.0 hPa
}
#endif /* PROTOCOL_H_ */
