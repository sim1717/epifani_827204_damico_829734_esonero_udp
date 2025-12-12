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

int is_valid_city_chars(const char *city) {
    if (city == NULL || city[0] == '\0') {
        return 0;
    }
    for (int i = 0; city[i] != '\0'; i++) {
        char c = city[i];
        if (c == '@' || c == '#' || c == '$' || c == '%' || c == '&' ||
            c == '*' || c == '\t' || c == ';' || c == '|' || c == '\\') {
            return 0;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == ' ' || c == '\'')) {

        }
    }
    return 1;
}

int is_city_supported(const char *city) {
    if (city == NULL) return 0;

    char citylower[64];
    strcpy(citylower, city);
    for(int i = 0; citylower[i]; i++) {
        citylower[i] = tolower(citylower[i]);
    }

    int len = strlen(citylower);
    while (len > 0 && citylower[len-1] == ' ') {
        citylower[len-1] = '\0';
        len--;
    }

    for(int i = 0; i < NUM_CITIES; i++) {
        if(strcmp(citylower, SUPPORTED_CITIES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}


void log_request(struct sockaddr_in *client_addr, weather_request_t *req) {
    char ip_str[16];
    struct in_addr addr;
    addr.s_addr = client_addr->sin_addr.s_addr;
    strcpy(ip_str, inet_ntoa(addr));

    char hostname[256];
    struct hostent *host = gethostbyaddr((char *)&addr, 4, AF_INET);

    if (host && host->h_name) {
        strncpy(hostname, host->h_name, 255);
    } else {
        strcpy(hostname, ip_str);
    }
    hostname[255] = '\0';

    printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n",
           hostname, ip_str, req->type, req->city);
}

int deserialize_request_with_validation(const char *buffer, int len, weather_request_t *req) {
    if (buffer == NULL || req == NULL || len < 2) {
        return -1;
    }

    req->type = buffer[0];

    int city_len = len - 1;
    if (city_len >= (int)sizeof(req->city)) {
        city_len = sizeof(req->city) - 1;
    }

    if (city_len <= 0) {
        return -1;
    }

    memcpy(req->city, buffer + 1, city_len);
    req->city[city_len] = '\0';

    if (!is_valid_city_chars(req->city)) {
        return -1;
    }

    return 0;
}


void deserialize_request(const char *buffer, weather_request_t *req) {
    int offset = 0;
    req->type = buffer[offset];
    offset += 1;
    memcpy(req->city, buffer + offset, sizeof(req->city));
}

int serialize_response(const weather_response_t *resp, char *buffer) {
    int offset = 0;
    uint32_t net_status = htonl(resp->status);
    memcpy(buffer + offset, &net_status, 4);
    offset += 4;
    buffer[offset] = resp->type;
    offset += 1;
    uint32_t value_bits;
    memcpy(&value_bits, &resp->value, 4);
    uint32_t net_value_bits = htonl(value_bits);
    memcpy(buffer + offset, &net_value_bits, 4);
    offset += 4;
    return offset;
}



int main(void) {
    srand(time(NULL));

#if defined WIN32
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
    if (result != 0) {
        printf("Error at WSAStartup()\n");
        return 0;
    }
#endif


    int server_socket;
    server_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_socket < 0) {
        errorhandler("socket creation failed.\n");
        clearwinsock();
        return -1;
    }


    int optval = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval));


    struct sockaddr_in sad;
    memset(&sad, 0, sizeof(sad));
    sad.sin_family = AF_INET;
    sad.sin_addr.s_addr = htonl(INADDR_ANY);
    sad.sin_port = htons(PROTO_PORT);

    if (bind(server_socket, (struct sockaddr*) &sad, sizeof(sad)) < 0) {
        errorhandler("bind() failed.\n");
        closesocket(server_socket);
        clearwinsock();
        return -1;
    }

    printf("Server UDP in ascolto sulla porta %d...\n", PROTO_PORT);
    printf("Premi Ctrl+C per terminare.\n\n");


    while (1) {
        weather_request_t richiesta;
        struct sockaddr_in client_addr;
        unsigned int client_len = sizeof(client_addr);
        char recv_buffer[sizeof(richiesta.type) + sizeof(richiesta.city)];

        int bytes_received = recvfrom(server_socket, recv_buffer, sizeof(recv_buffer), 0,
                                     (struct sockaddr*)&client_addr, &client_len);


        if (bytes_received < 0) {

            printf("Errore rete in recvfrom(), continuo...\n");
            fflush(stdout);
            continue;
        }

        if (bytes_received == 0) {

            continue;
        }

        if (bytes_received < 2) {
            printf("Messaggio troppo corto (%d bytes)\n", bytes_received);
            weather_response_t error_resp;
            error_resp.status = 2;
            error_resp.type = '\0';
            error_resp.value = 0.0;

            char send_buffer[4 + 1 + 4];
            int send_len = serialize_response(&error_resp, send_buffer);
            sendto(server_socket, send_buffer, send_len, 0,
                   (struct sockaddr*)&client_addr, sizeof(client_addr));
            continue;
        }
        int valid = deserialize_request_with_validation(recv_buffer, bytes_received, &richiesta);

        log_request(&client_addr, &richiesta);

        weather_response_t risposta;
        memset(&risposta, 0, sizeof(risposta));


        if (valid != 0) {

            risposta.status = 2;
            risposta.type = '\0';
            risposta.value = 0.0;
        }
        else if (richiesta.type != 't' && richiesta.type != 'h' &&
                 richiesta.type != 'w' && richiesta.type != 'p') {

            risposta.status = 2;
            risposta.type = '\0';
            risposta.value = 0.0;
        }
        else if (!is_city_supported(richiesta.city)) {

            risposta.status = 1;
            risposta.type = '\0';
            risposta.value = 0.0;
        }
        else {

            risposta.status = 0;
            risposta.type = richiesta.type;

            switch (richiesta.type) {
                case 't':
                    risposta.value = get_temperature();
                    break;
                case 'h':
                    risposta.value = get_humidity();
                    break;
                case 'w':
                    risposta.value = get_wind();
                    break;
                case 'p':
                    risposta.value = get_pressure();
                    break;
                default:

                    risposta.status = 2;
                    risposta.type = '\0';
                    risposta.value = 0.0;
                    break;
            }
        }

        // Invia risposta
        char send_buffer[4 + 1 + 4];
        int send_len = serialize_response(&risposta, send_buffer);

        int bytes_sent = sendto(server_socket, send_buffer, send_len, 0,
                               (struct sockaddr*)&client_addr, sizeof(client_addr));

        if (bytes_sent != send_len) {

            printf("Errore parziale in sendto() (inviati %d/%d bytes)\n",
                   bytes_sent, send_len);
            fflush(stdout);

        }
    }


    closesocket(server_socket);
    clearwinsock();
    return 0;
}
