#ifndef __TEST_H__
#define __TEST_H__

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "EoUpdaterProtocol.h"


class simpleEthClient
{
public:
    simpleEthClient(): sock_(-1) {}
    ~simpleEthClient() { closeSocket(); } 
    bool open(const char *ip, uint16_t port = 3333, double rx_timeout_sec = 1.0);
    void closeSocket();
    bool sendRaw(const void *buf, size_t len);
    bool discover();
    bool jump2updater();
    bool def2run_application();
    bool restart();
    bool blink();               
private:
    int sock_;
    struct sockaddr_in dest_;
    struct sockaddr_in src_;
    static void print_discover_reply(const eOuprot_cmd_DISCOVER_REPLY_t *reply, const char *srcip);
};
#endif // __TEST_H__