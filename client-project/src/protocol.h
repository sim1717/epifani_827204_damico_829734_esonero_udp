/*
 * protocol.h
 *
 * Client header file
 * Definitions, constants and function prototypes for the client
 */
#ifndef PROTOCOL_H_
#define PROTOCOL_H_

// Shared application parameters
#define PROTO_PORT 56700  // Server port (change if needed)
#define BUFFER_SIZE 512    // Buffer size for messages

typedef struct {
    char type;        // Weather data type: 't', 'h', 'w', 'p'
    char city[64];    // City name (null-terminated string)
} weather_request_t;

typedef struct {
    unsigned int status;  // Response status code - ATTENZIONE: unsigned int, non uint32_t!
    char type;            // Echo of request type
    float value;          // Weather data value
} weather_response_t;

#endif /* PROTOCOL_H_ */
