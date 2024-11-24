/**
 * @file main.c
 * @brief Tic-tac-toe game implementation using UDP sockets
 * @date 24/11/2024
 * @author redystum
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>

#include "args.h"
#include "debug.h"
#include "memory.h"

#define MAX_RESPONSE 1024
#define SERVER_PORT 6660
#define CLIENT_PORT 6661
#define CONNECTION_SIGNAL 777

int board[3][3] = {
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 }
};

int wait_to_recive(int fd);
void send_movement(char* ip, int position, bool isHost);
void draw_board();
void add_movement(int position, bool isHost, int* playedIndex, int* played);
int win();
void create_socket(int* fd, bool isHost);
int wait_to_recive_w_ipv4(int fd, char** ip);
char* get_self_ip();

int main(int argc, char* argv[])
{
    struct gengetopt_args_info args_info;

    if (cmdline_parser(argc, argv, &args_info) != 0) {
        return 1;
    }

    char* ip = args_info.ip_arg;

    bool host = args_info.host_flag;
    bool client = args_info.client_flag;

    if (host && client) {
        ERROR(1, "Cannot be host and client at the same time");
    }

    if (!host && !client) {
        int choice = 0;
        while (choice != 1 && choice != 2) {
            printf("What do you want to be?\n 1 - Host\n 2 - Client\n-> ");
            char input[10];
            if (fgets(input, sizeof(input), stdin) == NULL) { // Handle EOF
                printf("\nInput terminated. Exiting program.\n");
                exit(1);
            }
            if (sscanf(input, "%d", &choice) != 1) {
                printf("Invalid input. Please enter a number.\n");
                choice = 0;
            }
        }

        if (choice == 1) {
            host = true;
        } else {
            client = true;
        }
    }

    struct sockaddr_in _addr = { 0 };
    if (!host && args_info.ip_given == 0) {
        while (1) {
            char _ip[INET_ADDRSTRLEN];
            printf("Enter the host IP: ");

            if (fgets(_ip, sizeof(_ip), stdin) == NULL) {
                printf("\nInput terminated. Exiting program.\n");
                exit(1);
            }

            size_t len = strlen(_ip);
            if (len > 0 && _ip[len - 1] == '\n') {
                _ip[len - 1] = '\0';
            }

            if (inet_pton(AF_INET, _ip, &_addr.sin_addr) == 1) {
                ip = strdup(_ip);
                if (ip == NULL) {
                    ERROR(1, "strdup error");
                }
                break;
            } else {
                printf("Invalid IP. Please try again.\n");
            }
        }
    } else if (!host) {
        if (inet_pton(AF_INET, ip, &_addr.sin_addr) <= 0) {
            ERROR(1, "Invalid IP");
        }
    }

    int fd;
    create_socket(&fd, host);

    if (host) {

        char* IPbuffer = get_self_ip();
        if (IPbuffer == NULL) {
            ERROR(1, "Failed to get IP address");
        }

        printf("Host IP: %s\n", IPbuffer);
        printf("Waiting for client...\n");

        if (wait_to_recive_w_ipv4(fd, &ip) == CONNECTION_SIGNAL) {
            printf("Client connected\n");
        } else {
            ERROR(1, "Invalid connection");
        }
    } else {
        printf("Connecting to host...\n");
        send_movement(ip, CONNECTION_SIGNAL, host);
    }

    int played[9] = { 0 };
    int playedIndex = 0;

    int position;
    if (!host) {
        draw_board();
        printf("Waiting for next move...\n");
        position = wait_to_recive(fd);
        add_movement(position, !host, &playedIndex, played);
    }

    bool alreadyPlayed = false;
    while (playedIndex < 9) {
        draw_board();

        if (alreadyPlayed) {
            printf("Position already played\n");
        }

        printf("Choose a position: ");
        char input[10];
        if (fgets(input, sizeof(input), stdin) == NULL) { // Handle Ctrl+D or EOF
            printf("\nInput terminated. Exiting game.\n");
            exit(1);
        }
        if (sscanf(input, "%d", &position) != 1 || position < 1 || position > 9) {
            printf("Invalid position. Please enter a number between 1 and 9.\n");
            continue;
        }

        if (position < 1 || position > 9) {
            printf("Invalid position\n");
            continue;
        }

        for (int i = 0; i < playedIndex; i++) {
            if (played[i] == position) {
                alreadyPlayed = true;
                break;
            } else
                alreadyPlayed = false;
        }

        if (alreadyPlayed) {
            continue;
        }

        add_movement(position, host, &playedIndex, played);

        draw_board();
        send_movement(ip, position, host);

        if (win() != 0)
            break;

        if (playedIndex == 9)
            break;

        printf("Waiting for next move...\n");
        position = wait_to_recive(fd);

        add_movement(position, !host, &playedIndex, played);

        if (win() != 0)
            break;
    }

    draw_board();

    int winner = win();

    if (winner == 0) {
        printf("Draw!\n");
    } else if (winner == 1) {
        if (host) {
            printf("You Win!\n");
        } else {
            printf("You Lose!\n");
        }

    } else {
        if (host) {
            printf("You Lose!\n");
        } else {
            printf("You Win!\n");
        }
    }

    close(fd);
    cmdline_parser_free(&args_info);

    return 0;
}

/**
 * @brief Creates a socket and binds it to the appropriate port.
 *
 * This function creates a UDP socket and binds it to either the server or client port
 * based on whether the player is the host or client.
 *
 * @param fd A pointer to the file descriptor of the socket.
 * @param isHost A boolean indicating whether the player is the host.
 */
void create_socket(int* fd, bool isHost)
{
    *fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (*fd < 0) {
        ERROR(1, "socket() failed");
    }

    int port = isHost ? SERVER_PORT : CLIENT_PORT;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(*fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(*fd);
        ERROR(1, "bind() failed");
    }
}

/**
 * @brief Waits to receive a move from the opponent.
 *
 * This function waits for a move from the opponent, receives it, and sends an acknowledgment.
 *
 * @param fd The file descriptor of the socket.
 * @return The position of the move received.
 */
int wait_to_recive(int fd)
{
    uint16_t request;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if (recvfrom(fd, &request, sizeof(request), 0, (struct sockaddr*)&client_addr, &client_addr_len) < 0) {
        ERROR(1, "recvfrom() failed");
    }

    int position = ntohs(request);

    char buffer[MAX_RESPONSE] = "OK";

    if (sendto(fd, buffer, strlen(buffer), 0, (struct sockaddr*)&client_addr, client_addr_len) < 0) {
        ERROR(1, "sendto() failed");
    }

    return position;
}

/**
 * @brief Waits to receive data from an IPv4 address.
 *
 * This function blocks until data is received on the specified file descriptor.
 * Once data is received, it extracts the IPv4 address and stores it in the provided
 * pointer to a string.
 *
 * @param fd The file descriptor to wait on for incoming data.
 * @param ip A pointer to a string where the received IPv4 address will be stored.
 *           The caller is responsible for freeing the allocated memory.
 * @return An integer indicating the success or failure of the operation.
 *         Returns 0 on success, or a negative value on error.
 */
int wait_to_recive_w_ipv4(int fd, char** ip)
{
    uint16_t request;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if (recvfrom(fd, &request, sizeof(request), 0, (struct sockaddr*)&client_addr, &client_addr_len) < 0) {
        ERROR(1, "recvfrom() failed");
    }

    int position = ntohs(request);

    char buffer[MAX_RESPONSE] = "OK";

    if (sendto(fd, buffer, strlen(buffer), 0, (struct sockaddr*)&client_addr, client_addr_len) < 0) {
        ERROR(1, "sendto() failed");
    }

    *ip = inet_ntoa(client_addr.sin_addr);

    return position;
}

/**
 * @brief Retrieves the IP address of the local machine in a thread-safe manner.
 *
 * This function determines and returns the IP address of the machine
 * on which the code is running. It uses `inet_ntop` for thread safety.
 *
 * @return A dynamically allocated string containing the IP address of the local machine.
 *         The caller is responsible for freeing the allocated memory.
 */
char* get_self_ip()
{
    char hostbuffer[256];
    struct hostent* host_entry;
    char* ip_str = NULL;

    if (gethostname(hostbuffer, sizeof(hostbuffer)) == -1) {
        return NULL;
    }

    host_entry = gethostbyname(hostbuffer);
    if (host_entry == NULL) {
        return NULL;
    }

    struct in_addr** addr_list = (struct in_addr**)host_entry->h_addr_list;
    if (addr_list[0] != NULL) {
        ip_str = malloc(INET_ADDRSTRLEN);
        if (ip_str == NULL) {
            return NULL;
        }
        if (inet_ntop(AF_INET, addr_list[0], ip_str, INET_ADDRSTRLEN) == NULL) {
            free(ip_str);
            return NULL;
        }
    }

    return ip_str;
}

/**
 * @brief Sends a move to the opponent.
 *
 * This function sends the player's move to the opponent and waits for an acknowledgment.
 *
 * @param ip The IP address of the opponent.
 * @param position The position of the move to be sent.
 * @param isHost A boolean indicating whether the player is the host.
 */
void send_movement(char* ip, int position, bool isHost)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        ERROR(1, "socket() failed");
    }

    struct sockaddr_in addr = { 0 };
    switch (inet_pton(AF_INET, ip, &addr.sin_addr)) {
    case -1:
        ERROR(1, "invalid domain.\n");
        break;

    case 0:
        ERROR(1, "invalid IP address.\n");
        break;

    default:
        break;
    }

    int port = isHost ? CLIENT_PORT : SERVER_PORT;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    uint16_t pos = htons(position);

    if (sendto(fd, &pos, sizeof(pos), 0, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ERROR(1, "sendto() failed");
    }

    char buffer[MAX_RESPONSE];
    socklen_t addr_len = sizeof(addr);

    ssize_t nrecv = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&addr, &addr_len);
    if (nrecv < 0) {
        ERROR(1, "recvfrom() failed");
    }

    buffer[nrecv] = '\0';

    if (strcmp(buffer, "OK") != 0) {
        ERROR(1, "Invalid response");
    }

    close(fd);
}

/**
 * @brief Adds a movement to the game board.
 *
 * This function updates the game board with the player's move at the specified position.
 *
 * @param position The position on the game board where the move is to be made.
 * @param isHost A boolean indicating whether the player making the move is the host.
 * @param playedIndex A pointer to the index of the last played move.
 * @param played A pointer to the array representing the game board.
 */
void add_movement(int position, bool isHost, int* playedIndex, int* played)
{
    int row = (position - 1) / 3;
    int col = (position - 1) % 3;

    board[row][col] = isHost ? 1 : 2;

    if (playedIndex != NULL) {
        played[*playedIndex] = position;
        (*playedIndex)++;
    }
}

/**
 * @brief Determines the winner of the game.
 *
 * This function checks the current state of the game and returns the winner.
 *
 * @return int
 *         - 0: No winner
 *         - 1: X (host) winner
 *         - 2: O (client) winner
 */
int win()
{
    // Check rows
    for (int i = 0; i < 3; i++) {
        if (board[i][0] == board[i][1] && board[i][1] == board[i][2]) {
            return board[i][0];
        }
    }

    // Check columns
    for (int i = 0; i < 3; i++) {
        if (board[0][i] == board[1][i] && board[1][i] == board[2][i]) {
            return board[0][i];
        }
    }

    // Check diagonals
    if (board[0][0] == board[1][1] && board[1][1] == board[2][2]) {
        return board[0][0];
    }

    if (board[0][2] == board[1][1] && board[1][1] == board[2][0]) {
        return board[0][2];
    }

    return 0;
}

void print_X()
{
    printf("\033[1;34mX\033[0m");
}

void print_O()
{
    printf("\033[1;32mO\033[0m");
}

/**
 * @brief Draws the tic-tac-toe game board on the screen.
 *
 * This function is responsible for rendering the current state of the
 * tic-tac-toe game board. It visually represents the board with the
 * positions of the players' moves.
 */
void draw_board()
{
    printf("\033[H\033[J"); // Clear the terminal screen
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (board[i][j] == 0) {
                printf(" %d ", i * 3 + j + 1);
            } else if (board[i][j] == 1) {
                printf(" ");
                print_X();
                printf(" ");
            } else {
                printf(" ");
                print_O();
                printf(" ");
            }
            if (j < 2)
                printf("|");
        }
        printf("\n");
        if (i < 2)
            printf("---+---+---\n");
    }
}