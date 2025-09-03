#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "EoUpdaterProtocol.h"

void print_discover_reply(const eOuprot_cmd_DISCOVER_REPLY_t* reply) {
    if (reply->reply.res != uprot_RES_OK) {
        std::cerr << "Received a non-OK result: " << (int)reply->reply.res << std::endl;
        return;
    }

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             reply->mac48[0], reply->mac48[1], reply->mac48[2],
             reply->mac48[3], reply->mac48[4], reply->mac48[5]);

    std::cout << "--- Board Info ---" << std::endl;
    std::cout << "Board Type: " << (int)reply->boardtype << std::endl;
    std::cout << "MAC Address: " << mac_str << std::endl;
    std::cout << "Protocol Version: " << (int)reply->reply.protversion << std::endl;
    std::cout << "Running Process: " << (int)reply->processes.runningnow << " (1:Updater, 2:Application)" << std::endl;
    std::cout << "Default to Run: " << (int)reply->processes.def2run << std::endl;
    std::cout << "------------------" << std::endl;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <board_ip> <command>" << std::endl;
        std::cerr << "Commands: discover, restart, jump2updater" << std::endl;
        return 1;
    }

    const char* board_ip = argv[1];
    std::string command = argv[2];
    const int board_port = 3333;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Set a 1-second receive timeout
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt");
        close(sock);
        return 1;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(board_port);
    if (inet_pton(AF_INET, board_ip, &dest_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    if (command == "discover") {
        eOuprot_cmd_DISCOVER_t discover_cmd;
        memset(&discover_cmd, 0xFF, sizeof(discover_cmd)); // Fill with 0xFF
        discover_cmd.opc = uprot_OPC_LEGACY_SCAN;
        discover_cmd.opc2 = uprot_OPC_DISCOVER;
        discover_cmd.jump2updater = 0; // Set to 1 to force maintenance

        std::cout << "Sending DISCOVER to " << board_ip << std::endl;
        sendto(sock, &discover_cmd, sizeof(discover_cmd), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));

        char buffer[256];
        struct sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&src_addr, &src_len);

        if (len > 0) {
            if (len >= sizeof(eOuprot_cmd_DISCOVER_REPLY_t)) {
                print_discover_reply((eOuprot_cmd_DISCOVER_REPLY_t*)buffer);
            } else {
                std::cerr << "Received a packet, but it's too small to be a DISCOVER_REPLY. Size: " << len << std::endl;
            }
        } else {
            perror("recvfrom (maybe timeout)");
        }

    } else if (command == "restart") {
        eOuprot_cmd_RESTART_t restart_cmd;
        memset(&restart_cmd, 0xFF, sizeof(restart_cmd));
        restart_cmd.opc = uprot_OPC_RESTART;

        std::cout << "Sending RESTART to " << board_ip << std::endl;
        sendto(sock, &restart_cmd, sizeof(restart_cmd), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        std::cout << "Command sent." << std::endl;

    } else if (command == "jump2updater") {
        eOuprot_cmd_JUMP2UPDATER_t jump_cmd;
        memset(&jump_cmd, 0xFF, sizeof(jump_cmd));
        jump_cmd.opc = uprot_OPC_JUMP2UPDATER;

        std::cout << "Sending JUMP2UPDATER to " << board_ip << std::endl;
        sendto(sock, &jump_cmd, sizeof(jump_cmd), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        std::cout << "Command sent." << std::endl;
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
    }

    close(sock);
    return 0;
}