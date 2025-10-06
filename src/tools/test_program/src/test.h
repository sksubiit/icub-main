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
#include <algorithm> 
#include <fstream>
#include <sstream>
#include <cctype>
#include <limits>
#include <sys/stat.h>

#include "EoBoards.h"  
#include "EoUpdaterProtocol.h"


class simpleEthClient
{
public:

    simpleEthClient(): sock_(-1) {}
    ~simpleEthClient() { closeSocket(); } 
    //bool open(const char *ip, uint16_t port = 7777, double rx_timeout_sec = 1.0);
    bool open(const char *ip, double rx_timeout_sec = 1.0);
    void closeSocket();
    bool sendRaw(const void *buf, size_t len);
    bool discover();
    bool jump2updater();
    bool def2run_application();
    bool restart();
    bool blink();
    bool program(); 

private:
    int sock_;
    struct sockaddr_in dest_;
    struct sockaddr_in src_;

    // helpers
    bool recvReplyForIP(uint8_t expected_opc, int timeout_ms, eOuprot_result_t &out_res);
    bool findFirmwareForBoard(const std::string &boardname, std::string &out_hexpath);
    bool sendPROG_START(eOuprot_partition2prog_t partition, eOuprot_result_t &out_res);
    bool sendPROG_DATA_chunk(uint32_t address, const uint8_t *data, size_t len, eOuprot_result_t &out_res);
    bool sendPROG_END(uint16_t numberofpkts, eOuprot_result_t &out_res);

    static void print_discover_reply(const eOuprot_cmd_DISCOVER_REPLY_t *reply, const char *srcip);
    static void print_legacy_scan_reply(const eOuprot_cmd_LEGACY_SCAN_REPLY_t *scan, const char *srcip);  // <--- NEW
};
#endif // __TEST_H__