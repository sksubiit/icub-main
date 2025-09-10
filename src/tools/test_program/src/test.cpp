#include "test.h"

class SimpleEthClient
{
public:
    SimpleEthClient(): sock_(-1) {}
    ~SimpleEthClient() { closeSocket(); }

    bool open(const char *ip, uint16_t port = 3333, double rx_timeout_sec = 1.0)
    {
        closeSocket();
        sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ < 0) { perror("socket"); return false; }

        // allow quick reuse and bind the local port so replies come back on port 'port' (3333)
        int on = 1;
        if (setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
            perror("setsockopt(SO_REUSEADDR)");
            // non-fatal: continue
        }
        struct sockaddr_in local;
        memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        // Optionally force binding to a specific local IP by exporting LOCAL_IP environment variable
        const char *local_ip_env = getenv("LOCAL_IP");
        if (local_ip_env && inet_pton(AF_INET, local_ip_env, &local.sin_addr) <= 0) {
            perror("inet_pton(LOCAL_IP)");
            closeSocket();
            return false;
        }
        if (!local_ip_env) local.sin_addr.s_addr = htonl(INADDR_ANY); // bind on all local addresses
        local.sin_port = htons(port);
        if (bind(sock_, (struct sockaddr*)&local, sizeof(local)) < 0) {
            perror("bind");
            closeSocket();
            return false;
        }
        // print bound local address:port
        // struct sockaddr_in actual;
        // socklen_t alen = sizeof(actual);
        // if (getsockname(sock_, (struct sockaddr*)&actual, &alen) == 0) {
        //     std::cout << "Socket bound to " << inet_ntoa(actual.sin_addr) << ":" << ntohs(actual.sin_port) << std::endl;
        // }

        memset(&dest_, 0, sizeof(dest_));
        dest_.sin_family = AF_INET;
        dest_.sin_port = htons(port);
        if (inet_pton(AF_INET, ip, &dest_.sin_addr) <= 0) { perror("inet_pton"); closeSocket(); return false; }

        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(rx_timeout_sec);
        tv.tv_usec = static_cast<suseconds_t>((rx_timeout_sec - tv.tv_sec) * 1e6);
        if (setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) { perror("setsockopt"); closeSocket(); return false; }

        return true;
    }

    void closeSocket()
    {
        if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
    }

    bool sendRaw(const void *buf, size_t len)
    {
        if (sock_ < 0) return false;
        ssize_t s = sendto(sock_, buf, len, 0, (struct sockaddr*)&dest_, sizeof(dest_));
        return (s == (ssize_t)len);

        // if (s < 0) { perror("sendto"); return false; }
        // if ((size_t)s != len) {
        //     std::cerr << "send to wrote " << s << " of " << len << " bytes\n";
        //     return false;
        // }
        // struct sockaddr_in actual;
        // socklen_t alen = sizeof(actual);
        // if (getsockname(sock_, (struct sockaddr*)&actual, &alen) == 0) {
        //     std::cout << "Sent from " << inet_ntoa(actual.sin_addr) << ":" << ntohs(actual.sin_port)
        //               << " -> " << inet_ntoa(dest_.sin_addr) << ":" << ntohs(dest_.sin_port) << std::endl;
        // }
        // return true;
    }

    // Discover: broadcast/unicast discover and print replies until timeout
    bool discover()
    {
        if (sock_ < 0) return false;
        eOuprot_cmd_DISCOVER_t cmd;
        memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
        cmd.opc = uprot_OPC_LEGACY_SCAN;
        cmd.opc2 = uprot_OPC_DISCOVER;
        cmd.jump2updater = 0;

        if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto"); return false; }
        std::cout << "DISCOVER sent, awaiting replies..." << std::endl;

        unsigned char buf[1500];
        while (true)
        {
            socklen_t sl = sizeof(src_);
            ssize_t r = recvfrom(sock_, buf, sizeof(buf), 0, (struct sockaddr*)&src_, &sl);
            if (r < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::cout << "Receive timed out, no more replies." << std::endl;
                    break;
                }
                perror("recvfrom");
                return false;
            }

            const char *srcip = inet_ntoa(src_.sin_addr);
            // New-style discover reply
            if ((size_t)r >= sizeof(eOuprot_cmd_DISCOVER_REPLY_t))
            {
                auto *rep = reinterpret_cast<eOuprot_cmd_DISCOVER_REPLY_t*>(buf);
                // sanity: check opcode in the embedded reply field if available
                print_discover_reply(rep, srcip);
            }
            // Legacy scan reply (older boards)
            else if ((size_t)r >= sizeof(eOuprot_cmd_LEGACY_SCAN_REPLY_t))
            {
                auto *scan = reinterpret_cast<eOuprot_cmd_LEGACY_SCAN_REPLY_t*>(buf);
                print_legacy_scan_reply(scan, srcip);
            }
            else
            {
                // Unknown / too small: dump raw bytes to help debugging
                std::cout << "Ignored/unknown packet of size " << r << " from " << srcip << " -- raw:";
                for (ssize_t i = 0; i < r; ++i)
                {
                    printf(" %02X", buf[i]);
                }
                std::cout << std::endl;
            }
        }
        return true;
    }

    bool jump2updater()
    {

        eOuprot_cmd_DISCOVER_t cmd;
        memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
        cmd.opc  = uprot_OPC_LEGACY_SCAN;
        cmd.opc2 = uprot_OPC_DISCOVER;
        cmd.jump2updater = 1; // request board to switch to updater (maintenance)

        if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto"); return false; }
        std::cout << "DISCOVER (jump2updater=1) sent to request maintenance mode." << std::endl;
        return true;
    }

    bool def2run_application()
    {
        eOuprot_cmd_DEF2RUN_t cmd;
        memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
        cmd.opc = uprot_OPC_DEF2RUN;
        cmd.proc = static_cast<uint8_t>(eApplication); // set default-to-run to application
        if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto"); return false; }
        std::cout << "DEF2RUN -> Application sent." << std::endl;
        return true;
    }

    bool restart()
    {
        eOuprot_cmd_RESTART_t cmd;
        memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
        cmd.opc = uprot_OPC_RESTART;
        if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto"); return false; }
        std::cout << "RESTART sent." << std::endl;
        return true;
    }

    bool blink()
    {
        eOuprot_cmd_BLINK_t cmd;
        memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
        cmd.opc = uprot_OPC_BLINK;
        if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto"); return false; }
        std::cout << "BLINK sent." << std::endl;
        return true;
    }

private:
    int sock_;
    struct sockaddr_in dest_;
    struct sockaddr_in src_;

    static void print_discover_reply(const eOuprot_cmd_DISCOVER_REPLY_t *reply, const char *srcip)
    {
        if (!reply) return;
        std::cout << "---- Discover reply from " << srcip << " ----" << std::endl;
        std::cout << "Result: " << (int)reply->reply.res << "  ProtVer: " << (int)reply->reply.protversion << std::endl;
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 reply->mac48[0], reply->mac48[1], reply->mac48[2],
                 reply->mac48[3], reply->mac48[4], reply->mac48[5]);
        std::cout << "MAC: " << mac << "  Type: " << (int)reply->boardtype << std::endl;
        std::cout << "Running: " << (int)reply->processes.runningnow << "  Def2run: " << (int)reply->processes.def2run << std::endl;
        std::cout << "Capabilities mask: 0x" << std::hex << reply->capabilities << std::dec << std::endl;
        std::cout << "----------------------------------------" << std::endl;
    }

    static void print_legacy_scan_reply(const eOuprot_cmd_LEGACY_SCAN_REPLY_t *scan, const char *srcip)
    {
        if (!scan) return;
        std::cout << "---- Legacy scan reply from " << srcip << " ----" << std::endl;
        std::cout << "opc: " << (int)scan->opc
                  << "  version: " << (int)scan->version.major << "." << (int)scan->version.minor << std::endl;
        // print mac if present
        char mac[18] = {0};
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 scan->mac48[0], scan->mac48[1], scan->mac48[2],
                 scan->mac48[3], scan->mac48[4], scan->mac48[5]);
        std::cout << "MAC: " << mac << std::endl;
        // ip mask (if present) - print as hex or dotted
        uint32_t mask = *(uint32_t*)(scan->ipmask);
        std::cout << "IP mask (raw): 0x" << std::hex << mask << std::dec << std::endl;
        std::cout << "----------------------------------------" << std::endl;
    }
};

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <board_ip> <command>\n"
                  << "Commands: discover, maintenance|jump2updater, application|def2run_application, restart, blink\n";
        return 1;
    }

    const char *ip = argv[1];
    std::string cmd = argv[2];

    SimpleEthClient client;
    if (!client.open(ip, 3333, 3.0)) return 1;

    if (cmd == "discover")
    {
        client.discover();
    }
    else if (cmd == "maintenance" || cmd == "jump2updater")
    {
        client.jump2updater();
    }
    else if (cmd == "application" || cmd == "def2run_application")
    {
        client.def2run_application();
        sleep(1);
        client.restart();
    }
    else if (cmd == "restart")
    {
        client.restart();
    }
    else if (cmd == "blink")
    {
        client.blink();
    }
    else
    {
        std::cerr << "Unknown command: " << cmd << std::endl;
        return 1;
    }

    return 0;
}