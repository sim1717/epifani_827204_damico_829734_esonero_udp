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
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>  // Per uint32_t
#include "protocol.h"

void clearwinsock() {
#if defined WIN32
    WSACleanup();
#endif
}

void errorhandler(char *errorMessage) {
    printf("%s", errorMessage);
}

// ============ FUNZIONE DI RISOLUZIONE DNS (NUOVA) ============
// Funzione per risolvere hostname o IP (con reverse lookup come specificato)
struct hostent* resolve_host_complete(const char *input, char *resolved_ip, char *resolved_name) {
    struct hostent *host;
    struct in_addr addr;

    // Prova prima come indirizzo IP (dotted-decimal)
    addr.s_addr = inet_addr(input);
    if (addr.s_addr != INADDR_NONE) {
        // È un indirizzo IP (es: 127.0.0.1)
        // 1. Reverse lookup: IP -> hostname
        host = gethostbyaddr((char *)&addr, 4, AF_INET);

        // 2. Copia IP
        strcpy(resolved_ip, input);

        // 3. Copia nome host (se trovato), altrimenti usa IP
        if (host && host->h_name) {
            strncpy(resolved_name, host->h_name, 255);
        } else {
            strcpy(resolved_name, resolved_ip);
        }
    } else {
        // È un hostname (es: localhost)
        // 1. Forward lookup: hostname -> IP
        host = gethostbyname(input);

        if (host) {
            // 2. Copia primo IP dalla lista
            struct in_addr* ina = (struct in_addr*) host->h_addr_list[0];
            strcpy(resolved_ip, inet_ntoa(*ina));

            // 3. Copia nome canonico
            strncpy(resolved_name, host->h_name, 255);

            // 4. Reverse lookup opzionale per consistenza
            struct hostent *reverse_host = gethostbyaddr((char *)ina, 4, AF_INET);
            if (reverse_host && reverse_host->h_name) {
                strncpy(resolved_name, reverse_host->h_name, 255);
            }
        } else {
            return NULL;
        }
    }

    // Assicurati che le stringhe siano terminate
    resolved_name[255] = '\0';
    return host;
}
// ============ FINE FUNZIONE DNS ============

// Serializza la richiesta in un buffer separato
int serialize_request(const weather_request_t *req, char *buffer) {
    int offset = 0;

    // type: 1 byte, nessuna conversione
    buffer[offset] = req->type;
    offset += sizeof(char);

    // city: array di char, nessuna conversione
    memcpy(buffer + offset, req->city, sizeof(req->city));
    offset += sizeof(req->city);

    return offset;
}

// Deserializza la risposta dal buffer
void deserialize_response(const char *buffer, weather_response_t *resp) {
    int offset = 0;
    uint32_t net_status;
    uint32_t net_value_bits;

    // Deserializza status (4 byte)
    memcpy(&net_status, buffer + offset, 4);
    resp->status = ntohl(net_status);
    offset += 4;

    // Deserializza type (1 byte)
    resp->type = buffer[offset];
    offset += 1;

    // Deserializza value (4 byte come float)
    memcpy(&net_value_bits, buffer + offset, 4);
    uint32_t host_bits = ntohl(net_value_bits);
    memcpy(&resp->value, &host_bits, 4);
}

int main(int argc, char *argv[]) {
#if defined WIN32
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
    if (result != 0) {
        printf("Error at WSAStartup()\n");
        return 0;
    }
#endif

    // Variabili per gli argomenti
    char *server_host = "localhost";  // default
    int port = PROTO_PORT;            // default
    char *request_str = NULL;
    int found_r = 0;

    // Parsing degli argomenti
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            server_host = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            request_str = argv[++i];
            found_r = 1;
        }
    }

    if (!found_r || !request_str) {
        printf("Usage: %s [-s server] [-p port] -r \"type city\"\n", argv[0]);
        printf("Esempio: %s -r \"t bari\"\n", argv[0]);
        printf("       %s -s localhost -p 56700 -r \"h milano\"\n", argv[0]);
        clearwinsock();
        return -1;
    }

    // Parsing della richiesta "type city"
    weather_request_t wr;
    memset(&wr, 0, sizeof(wr));

    // Trovare il primo spazio nella stringa di richiesta
    char *space_pos = strchr(request_str, ' ');
    if (!space_pos) {
        printf("Errore: formato richiesta non valido. Usa: \"type city\"\n");
        printf("Esempio: \"t roma\" oppure \"p Reggio Calabria\"\n");
        clearwinsock();
        return -1;
    }

    // Il type è il primo carattere
    wr.type = request_str[0];

    // Validazione: type deve essere un singolo carattere
    if (request_str[1] != ' ') {
        printf("Errore: il tipo deve essere un singolo carattere (t, h, w, p)\n");
        clearwinsock();
        return -1;
    }

    // La città è tutto dopo lo spazio (skip multiple spaces)
    char *city_start = space_pos;
    while (*city_start == ' ') city_start++;  // Salta spazi multipli

    // Validazione lunghezza città
    if (strlen(city_start) >= sizeof(wr.city)) {
        printf("Errore: nome città troppo lungo (max %d caratteri)\n", (int)sizeof(wr.city) - 1);
        clearwinsock();
        return -1;
    }

    // Controlla se ci sono tabulazioni
    if (strchr(city_start, '\t')) {
        printf("Errore: caratteri di tabulazione non ammessi nel nome città\n");
        clearwinsock();
        return -1;
    }

    strcpy(wr.city, city_start);

    // Validazione type
    if (wr.type != 't' && wr.type != 'h' && wr.type != 'w' && wr.type != 'p') {
        printf("Errore: tipo richiesta non valido. Usa: t, h, w, p\n");
        printf("t=temperatura, h=umidità, w=vento, p=pressione\n");
        clearwinsock();
        return -1;
    }

    // ============ QUI USIAMO LA NUOVA FUNZIONE DNS ============
    // 1. Risoluzione DNS del server
    char server_ip[16];  // Sufficiente per "255.255.255.255"
    char server_name[256];
    struct hostent *server_hostent = resolve_host_complete(server_host, server_ip, server_name);

    if (!server_hostent) {
        printf("Errore: impossibile risolvere '%s'\n", server_host);
        clearwinsock();
        return -1;
    }
    // ============ FINE RISOLUZIONE DNS ============

    // 2. Crea socket UDP
    int c_socket;
    if ((c_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        errorhandler("socket() failed");
        clearwinsock();
        return -1;
    }

    // 3. Configura indirizzo server
    struct sockaddr_in echoServAddr;
    memset(&echoServAddr, 0, sizeof(echoServAddr));
    echoServAddr.sin_family = AF_INET;
    echoServAddr.sin_port = htons(port);
    echoServAddr.sin_addr.s_addr = inet_addr(server_ip);

    // 4. Serializza la richiesta
    char send_buffer[sizeof(wr.type) + sizeof(wr.city)];
    int send_len = serialize_request(&wr, send_buffer);

    // 5. Invia richiesta al server
    int bytes_sent = sendto(c_socket, send_buffer, send_len, 0,
                           (struct sockaddr*)&echoServAddr, sizeof(echoServAddr));

    if (bytes_sent != send_len) {
        errorhandler("sendto() failed or sent different number of bytes\n");
        closesocket(c_socket);
        clearwinsock();
        return -1;
    }

    // 6. Ricevi risposta dal server
    char recv_buffer[4 + 1 + 4];  // status(4) + type(1) + value(4)
    struct sockaddr_in fromAddr;
    unsigned int fromSize = sizeof(fromAddr);

    int bytes_received = recvfrom(c_socket, recv_buffer, sizeof(recv_buffer), 0,
                                 (struct sockaddr*)&fromAddr, &fromSize);

    if (bytes_received <= 0) {
        errorhandler("recvfrom() failed or connection closed prematurely\n");
        closesocket(c_socket);
        clearwinsock();
        return -1;
    }

    // 7. Deserializza la risposta
    weather_response_t risposta;
    deserialize_response(recv_buffer, &risposta);

    // 8. Formatta output secondo specifiche
    // Capitalizza prima lettera della città
    char city_capitalized[64];
    strcpy(city_capitalized, wr.city);
    if (city_capitalized[0] >= 'a' && city_capitalized[0] <= 'z') {
        city_capitalized[0] = city_capitalized[0] - 'a' + 'A';
    }

    printf("Ricevuto risultato dal server %s (ip %s). ", server_name, server_ip);

    switch(risposta.status) {
        case 0:
            printf("%s: ", city_capitalized);
            switch (risposta.type) {
                case 't': printf("Temperatura = %.1f°C", risposta.value); break;
                case 'h': printf("Umidità = %.1f%%", risposta.value); break;
                case 'w': printf("Vento = %.1f km/h", risposta.value); break;
                case 'p': printf("Pressione = %.1f hPa", risposta.value); break;
            }
            break;
        case 1:
            printf("Città non disponibile");
            break;
        case 2:
            printf("Richiesta non valida");
            break;
    }
    printf("\n");

    closesocket(c_socket);
    clearwinsock();
    return 0;
}
