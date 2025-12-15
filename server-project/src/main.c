#if defined WIN32
#include <winsock.h>
#else
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#define closesocket close
#endif
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include "protocol.h"

///////////SERVER
void clearwinsock() {
#if defined WIN32
    WSACleanup();
#endif
}

void errorhandler(char *errorMessage) {
    printf("%s", errorMessage);
}

// ============ FUNZIONE DNS SERVER  ============
// Funzione per risolvere hostname dal client 
void get_client_hostname(struct sockaddr_in *client_addr, char *hostname_buf, char *ip_buf) {
    struct hostent *host;
    struct in_addr addr;

    // Copia l'indirizzo IP dal socket 
    addr.s_addr = client_addr->sin_addr.s_addr;

    // 1. Converti IP in stringa 
    strcpy(ip_buf, inet_ntoa(addr));

    // 2. Reverse DNS lookup 
    //    Nel nostro caso addr_len_in_bytes è sempre 4 e addr_family_type è sempre AF_INET
    host = gethostbyaddr((char *)&addr, 4, AF_INET);

    if (host && host->h_name) {
        // Usa il nome canonico 
        strncpy(hostname_buf, host->h_name, 255);
    } else {
        // Se non risolve, usa l'IP come hostname
        strcpy(hostname_buf, ip_buf);
    }
    hostname_buf[255] = '\0';  // Terminazione sicura
}
// ============ FINE FUNZIONE DNS SERVER ============

// Deserializza la richiesta dal buffer
void deserialize_request(const char *buffer, weather_request_t *req) {
    int offset = 0;

    // type: 1 byte, nessuna conversione
    req->type = buffer[offset];
    offset += 1;

    // city: array di char, nessuna conversione
    memcpy(req->city, buffer + offset, sizeof(req->city));
}

// Serializza la risposta in un buffer separato
int serialize_response(const weather_response_t *resp, char *buffer) {
    int offset = 0;

    // Serializza status (4 byte con network byte order)
    uint32_t net_status = htonl(resp->status);
    memcpy(buffer + offset, &net_status, 4);
    offset += 4;

    // Serializza type (1 byte, nessuna conversione)
    buffer[offset] = resp->type;
    offset += 1;

    // Serializza value (float come 4 byte con network byte order)
    
    uint32_t value_bits;
    memcpy(&value_bits, &resp->value, 4);
    uint32_t net_value_bits = htonl(value_bits);
    memcpy(buffer + offset, &net_value_bits, 4);
    offset += 4;

    return offset;
}

int main(int argc, char *argv[]) {
    srand(time(NULL));

#if defined WIN32
    // Initialize Winsock
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
    if (result != 0) {
        printf("Error at WSAStartup()\n");
        return 0;
    }
#endif
    int port = PROTO_PORT;  // default

        // Parsing degli argomenti del server
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
                port = atoi(argv[++i]);
                if (port <= 0 || port > 65535) {
                    printf("Errore: porta non valida. Usa un valore tra 1 e 65535\n");
                    clearwinsock();
                    return -1;
                }
            } else {
                printf("Usage: %s [-p port]\n", argv[0]);
                printf("Esempio: %s\n", argv[0]);
                printf("       %s -p 56700\n", argv[0]);
                clearwinsock();
                return -1;
            }
        }
    // create UDP socket
    int server_socket;
    server_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_socket < 0) {
        errorhandler("socket creation failed.\n");
        clearwinsock();
        return -1;
    }

    // set connection settings
    struct sockaddr_in sad;
    memset(&sad, 0, sizeof(sad));
    sad.sin_family = AF_INET;
    sad.sin_addr.s_addr = htonl(INADDR_ANY);  // Accetta connessioni da qualsiasi interfaccia
    sad.sin_port = htons(port);

    // Bind della socket UDP
    if (bind(server_socket, (struct sockaddr*) &sad, sizeof(sad)) < 0) {
        errorhandler("bind() failed.\n");
        closesocket(server_socket);
        clearwinsock();
        return -1;
    }

    printf("Server UDP in ascolto sulla porta %d...\n", PROTO_PORT);
    printf("Premi Ctrl+C per terminare.\n\n");

    // Loop principale UDP (nessun listen/accept)
    while (1) {
        weather_request_t richiesta;
        struct sockaddr_in client_addr;  // Indirizzo del client
        unsigned int client_len = sizeof(client_addr);

        // Buffer per ricevere il datagram
        char recv_buffer[sizeof(richiesta.type) + sizeof(richiesta.city)];

        // Ricevi datagram dal client (acquisisce anche l'indirizzo)
        int bytes_received = recvfrom(server_socket, recv_buffer, sizeof(recv_buffer), 0,(struct sockaddr*)&client_addr, &client_len);

        if (bytes_received <= 0) {
            errorhandler("recvfrom() failed\n");
            continue;  // Continua a ricevere altre richieste
        }

        // Deserializza la richiesta dal buffer
        deserialize_request(recv_buffer, &richiesta);

        // ============ QUI USIAMO LA FUNZIONE DNS  ============
        // Ottieni hostname e IP del client per il logging
        char client_hostname[256];
        char client_ip[16];
        get_client_hostname(&client_addr, client_hostname, client_ip);
        // ============ FINE DNS SERVER ============

        // Log della richiesta
        printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n",
               client_hostname, client_ip, richiesta.type, richiesta.city);

        // Processa la richiesta
        weather_response_t risposta;

        // Converti città in lowercase per confronto case-insensitive
        char citylower[64];
        strcpy(citylower, richiesta.city);
        for(int i = 0; citylower[i]; i++) {
            citylower[i] = tolower(citylower[i]);
        }

        // Rimuovi eventuali spazi extra alla fine
        int len = strlen(citylower);
        while (len > 0 && citylower[len-1] == ' ') {
            citylower[len-1] = '\0';
            len--;
        }

        int city_found = 0;
        for(int i = 0; i < NUM_CITIES; i++) {
            if(strcmp(citylower, SUPPORTED_CITIES[i]) == 0) {
                city_found = 1;
                break;
            }
        }

        if (city_found == 0) {
            // Città non trovata
            risposta.status = 1;
            risposta.type = '\0';
            risposta.value = 0.0;
        } else {
            // Città trovata, controlla tipo richiesta
            switch (richiesta.type) {
                case 't':
                    risposta.type = 't';
                    risposta.status = 0;
                    risposta.value = get_temperature();
                    break;
                case 'h':
                    risposta.type = 'h';
                    risposta.status = 0;
                    risposta.value = get_humidity();
                    break;
                case 'w':
                    risposta.type = 'w';
                    risposta.status = 0;
                    risposta.value = get_wind();
                    break;
                case 'p':
                    risposta.type = 'p';
                    risposta.status = 0;
                    risposta.value = get_pressure();
                    break;
                default:
                    // Tipo non valido
                    risposta.status = 2;
                    risposta.type = '\0';
                    risposta.value = 0.0;
                    break;
            }
        }

        // Serializza la risposta in un buffer separato
        char send_buffer[4 + 1 + 4];  // status(4) + type(1) + value(4)
        int send_len = serialize_response(&risposta, send_buffer);

        // Invia risposta al client (usando l'indirizzo acquisito)
        int bytes_sent = sendto(server_socket, send_buffer, send_len, 0,
                               (struct sockaddr*)&client_addr, sizeof(client_addr));

        if (bytes_sent != send_len) {
            errorhandler("sendto() sent different number of bytes\n");
            // Continua comunque, non interrompere il server
        }
    }

    // Questo codice non viene mai raggiunto perché il loop è infinito
    closesocket(server_socket);
    clearwinsock();
    return 0;
}
