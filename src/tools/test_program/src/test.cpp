#include "test.h"

// helper: receive a 4-byte eOuprot_cmdREPLY_t for dest_ ip and check opc
bool simpleEthClient::recvReplyForIP(uint8_t expected_opc, int timeout_ms, eOuprot_result_t &out_res)
{
    (void)timeout_ms; // socket timeout already set in open()
    unsigned char buf[1500];
    socklen_t sl = sizeof(src_);

    // loop until timeout or until we receive an expected reply from dest_
    while (true) {
        ssize_t r = recvfrom(sock_, buf, sizeof(buf), 0, (struct sockaddr*)&src_, &sl);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // socket read timed out -> no reply
                return false;
            }
            perror("recvfrom");
            return false;
        }
        // ignore packets from other senders, keep waiting until timeout
        if (src_.sin_addr.s_addr != dest_.sin_addr.s_addr) {
            continue;
        }
        if ((size_t)r < sizeof(eOuprot_cmdREPLY_t)) continue;
        auto *reply = reinterpret_cast<eOuprot_cmdREPLY_t*>(buf);
        if (reply->opc != expected_opc) continue;
        out_res = static_cast<eOuprot_result_t>(reply->res);
        return true;
    }
}

// search firmware.info.xml for <board type="boardname"> and return the file path (relative resolved)
bool simpleEthClient::findFirmwareForBoard(const std::string &boardname, std::string &out_hexpath)
{
    // path hardcoded relative to repo; adapt if needed
    const char *xmlpath = "/home/sk/development/robotology-superbuild/src/icub-firmware-build/info/firmware.info.xml";
    std::ifstream f(xmlpath);
    if (!f.is_open()) return false;
    std::string line;
    bool inboard = false;
    std::string foundfile;
    while (std::getline(f, line)) {
        // trim whitespace
        std::string s = line;
        // lowercase for safer matching (board types in xml are lowercase)
        auto tolower_copy = [](std::string t){ for(char &c: t) c = static_cast<char>(std::tolower((unsigned char)c)); return t; };
        std::string tl = tolower_copy(s);
        std::string key = "board type=\"";
        size_t pos = tl.find(key);
        if (!inboard && pos != std::string::npos) {
            size_t start = pos + key.size();
            size_t end = tl.find('"', start);
            if (end != std::string::npos) {
                std::string bt = tl.substr(start, end - start);
                if (bt == tolower_copy(boardname)) {
                    inboard = true;
                    continue;
                }
            }
        }
        if (inboard) {
            size_t p1 = s.find("<file>");
            if (p1 != std::string::npos) {
                size_t p2 = s.find("</file>", p1);
                if (p2 != std::string::npos) {
                    foundfile = s.substr(p1 + strlen("<file>"), p2 - (p1 + strlen("<file>")));
                    break;
                }
            }
            // board close without file -> stop
            if (s.find("</board>") != std::string::npos) break;
        }
    }
    f.close();
    if (foundfile.empty()) return false;
    // resolve relative path: xml path parent + foundfile
    std::string xmls(xmlpath);
    size_t slash = xmls.rfind('/');
    std::string dir = (slash == std::string::npos) ? std::string(".") : xmls.substr(0, slash+1);
    std::string candidate = dir + foundfile;
    // normalize simple ../
    // try candidate as-is
    struct stat st;
    if (stat(candidate.c_str(), &st) == 0) {
        out_hexpath = candidate;
        return true;
    }
    // fallback: try the raw foundfile as absolute or relative to cwd
    if (stat(foundfile.c_str(), &st) == 0) {
        out_hexpath = foundfile;
        return true;
    }
    return false;
}

bool simpleEthClient::sendPROG_START(eOuprot_partition2prog_t partition, eOuprot_result_t &out_res)
{
    eOuprot_cmd_PROG_START_t cmd;
    memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
    cmd.opc = uprot_OPC_PROG_START;
    cmd.partition = static_cast<uint8_t>(partition);
    if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto PROG_START"); return false; }
    if (!recvReplyForIP(uprot_OPC_PROG_START, 0, out_res)) return false;
    return true;
}

bool simpleEthClient::sendPROG_DATA_chunk(uint32_t address, const uint8_t *data, size_t len, eOuprot_result_t &out_res)
{
    if (len == 0 || len > uprot_PROGmaxsize) return false;
    // prepare command in a dynamic buffer to send only HEAD+data
    const size_t HEAD_SIZE = 7; // opc (1) + address(4) + size(2)
    std::vector<uint8_t> packet(HEAD_SIZE + len, EOUPROT_VALUE_OF_UNUSED_BYTE);
    packet[0] = static_cast<uint8_t>(uprot_OPC_PROG_DATA);
    // address little-endian
    packet[1] = static_cast<uint8_t>(address & 0xFF);
    packet[2] = static_cast<uint8_t>((address >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>((address >> 16) & 0xFF);
    packet[4] = static_cast<uint8_t>((address >> 24) & 0xFF);
    // size little-endian (2 bytes)
    packet[5] = static_cast<uint8_t>(len & 0xFF);
    packet[6] = static_cast<uint8_t>((len >> 8) & 0xFF);
    // data
    memcpy(&packet[HEAD_SIZE], data, len);
    if (!sendRaw(packet.data(), packet.size())) { perror("sendto PROG_DATA"); return false; }
    if (!recvReplyForIP(uprot_OPC_PROG_DATA, 0, out_res)) return false;
    return true;
}

bool simpleEthClient::sendPROG_END(uint16_t numberofpkts, eOuprot_result_t &out_res)
{
    eOuprot_cmd_PROG_END_t end_cmd;
    memset(&end_cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(end_cmd));
    end_cmd.opc = uprot_OPC_PROG_END;
    end_cmd.numberofpkts[0] = numberofpkts & 0xFF;
    end_cmd.numberofpkts[1] = (numberofpkts >> 8) & 0xFF;
    if (!sendRaw(&end_cmd, sizeof(end_cmd))) { perror("sendto PROG_END"); return false; }
    if (!recvReplyForIP(uprot_OPC_PROG_END, 0, out_res)) return false;
    return true;
}

// program workflow: discover -> xml lookup -> jump2updater if needed -> parse intel hex -> PROG_START/DATA/END -> def2run + restart
bool simpleEthClient::program()
{
    if (sock_ < 0) return false;

    // 1) discover the board (unicast to dest_)
    eOuprot_cmd_DISCOVER_t cmd;
    memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
    cmd.opc = uprot_OPC_LEGACY_SCAN;
    cmd.opc2 = uprot_OPC_DISCOVER;
    cmd.jump2updater = 0;
    if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto discover"); return false; }

    // wait for reply from dest_
    unsigned char buf[1500];
    bool got = false;
    eOuprot_cmd_DISCOVER_REPLY_t discovered = {0};
    socklen_t sl;
    while (true)
    {
        sl = sizeof(src_);
        ssize_t r = recvfrom(sock_, buf, sizeof(buf), 0, (struct sockaddr*)&src_, &sl);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("recvfrom");
            return false;
        }
        if (src_.sin_addr.s_addr != dest_.sin_addr.s_addr) continue;
        // pick the best available discover info
        if ((size_t)r >= sizeof(eOuprot_cmd_DISCOVER_REPLY2_t)) {
            auto *rep2 = reinterpret_cast<eOuprot_cmd_DISCOVER_REPLY2_t*>(buf);
            discovered = rep2->discoveryreply;
            got = true;
            break;
        } else if ((size_t)r >= sizeof(eOuprot_cmd_MOREINFO_REPLY_t)) {
            auto *m = reinterpret_cast<eOuprot_cmd_MOREINFO_REPLY_t*>(buf);
            discovered = m->discover;
            got = true;
            break;
        } else if ((size_t)r >= sizeof(eOuprot_cmd_DISCOVER_REPLY_t)) {
            auto *rep = reinterpret_cast<eOuprot_cmd_DISCOVER_REPLY_t*>(buf);
            discovered = *rep;
            got = true;
            break;
        } else {
            // ignore legacy limited replies for programming (we need boardtype/process info)
        }
    }
    if (!got) {
        std::cerr << "No discover reply from target IP" << std::endl;
        return false;
    }

    // derive board name used in firmware.info.xml (strip eobrd_ prefix if present)
    const char *raw_board_name = eoboards_type2string2(static_cast<eObrd_type_t>(discovered.boardtype), static_cast<eObool_t>(0));
    std::string board_name;
    if (raw_board_name) {
        board_name = raw_board_name;
        const char prefix[] = "eobrd_";
        if (board_name.rfind(prefix, 0) == 0) {
            board_name = board_name.substr(strlen(prefix));
        }
    } else {
        std::cerr << "Cannot resolve board type string from reply" << std::endl;
        return false;
    }
    std::cout << "Discovered board type: " << board_name << std::endl;

    // 2) find firmware file from firmware.info.xml
    std::string hexpath;
    if (!findFirmwareForBoard(board_name, hexpath)) {
        std::cerr << "Firmware entry not found for board '" << board_name << "' in firmware.info.xml" << std::endl;
        return false;
    }
    std::cout << "Found firmware file: " << hexpath << std::endl;

    // 3) ensure maintenance mode
    bool inmaintenance = (discovered.processes.runningnow == eUpdater);
    if (!inmaintenance) {
        std::cout << "Board not in maintenance, requesting jump2updater..." << std::endl;
        if (!jump2updater()) {
            std::cerr << "Failed to send jump2updater" << std::endl;
            return false;
        }
        // wait and re-discover
        sleep(2);
        // re-run discover and verify
        eOuprot_cmd_DISCOVER_t cmd2;
        memset(&cmd2, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd2));
        cmd2.opc = uprot_OPC_LEGACY_SCAN; cmd2.opc2 = uprot_OPC_DISCOVER; cmd2.jump2updater = 0;
        if (!sendRaw(&cmd2, sizeof(cmd2))) { perror("sendto discover2"); return false; }
        bool got2 = false;
        while (true) {
            sl = sizeof(src_);
            ssize_t r = recvfrom(sock_, buf, sizeof(buf), 0, (struct sockaddr*)&src_, &sl);
            if (r < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                perror("recvfrom");
                return false;
            }
            if (src_.sin_addr.s_addr != dest_.sin_addr.s_addr) continue;
            if ((size_t)r >= sizeof(eOuprot_cmd_DISCOVER_REPLY_t)) {
                auto *rep = reinterpret_cast<eOuprot_cmd_DISCOVER_REPLY_t*>(buf);
                discovered = *rep;
                got2 = true;
                break;
            }
        }
        if (!got2) {
            std::cerr << "No discover reply after requesting maintenance" << std::endl;
            return false;
        }
        if (discovered.processes.runningnow != eUpdater) {
            std::cerr << "Board did not enter eUpdater (maintenance) mode" << std::endl;
            return false;
        }
        std::cout << "Board entered maintenance mode" << std::endl;
    } else {
        std::cout << "Board already in maintenance" << std::endl;
    }

    // 4) open hex file and stream intel-hex
    std::ifstream hexf(hexpath);
    if (!hexf.is_open()) {
        std::cerr << "Cannot open hex file: " << hexpath << std::endl;
        return false;
    }

    // send PROG_START (partition = APPLICATION)
    eOuprot_result_t resa;
    if (!sendPROG_START(uprot_partitionAPPLICATION, resa)) {
        std::cerr << "PROG_START no reply or failed" << std::endl;
        return false;
    }
    if (resa != uprot_RES_OK) {
        std::cerr << "PROG_START returned error: " << (int)resa << std::endl;
        return false;
    }
    std::cout << "PROG_START acknowledged" << std::endl;

    // parse intel hex: accumulate contiguous data up to uprot_PROGmaxsize, send chunks
    std::string line;
    uint32_t upper16 = 0;
    std::vector<uint8_t> chunk;
    uint32_t chunk_base = 0;
    int chunks_sent = 0;
    auto flush_chunk = [&](bool force)->bool {
        if (chunk.empty()) return true;
        // send with retry
        const int MAX_RETRIES = 3;
        eOuprot_result_t rr;
        bool ok = false;
        for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
            if (sendPROG_DATA_chunk(chunk_base, chunk.data(), chunk.size(), rr)) {
                if (rr == uprot_RES_OK) { ok = true; break; }
                else if (rr == uprot_RES_ERR_TRYAGAIN) { usleep(200000); continue; }
                else { break; }
            } else {
                usleep(200000);
            }
        }
        if (!ok) return false;
        ++chunks_sent;
        chunk.clear();
        return true;
    };

    while (std::getline(hexf, line)) {
        if (line.empty()) continue;
        if (line[0] != ':') continue;
        // parse
        auto hexbyte = [](char hi, char lo)->int {
            auto val = [](char c)->int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return 0;
            };
            return (val(hi) << 4) | val(lo);
        };
        size_t idx = 1;
        int bytecount = hexbyte(line[idx], line[idx+1]); idx += 2;
        int addr16 = (hexbyte(line[idx], line[idx+1]) << 8) | hexbyte(line[idx+2], line[idx+3]); idx += 4;
        int rectype = hexbyte(line[idx], line[idx+1]); idx += 2;
        std::vector<uint8_t> data;
        for (int i=0;i<bytecount;i++) {
            int b = hexbyte(line[idx], line[idx+1]); idx += 2;
            data.push_back(static_cast<uint8_t>(b));
        }
        // checksum ignored here (could be validated)
        if (rectype == 0x00) { // data
            uint32_t absaddr = (upper16 << 16) | static_cast<uint32_t>(addr16);

            // Skip records that target RAM (non-flash). The board will drop these;
            // avoid sending them from host (prevents extra "non flash chunk" packet).
            // so it follows the same logic as in the case of GUI by removing non flash chunk
            if ((absaddr & 0xFF000000u) == 0x20000000u) {
                // flush any pending flash chunk before skipping this RAM record
                if (!chunk.empty()) {
                    if (!flush_chunk(true)) { std::cerr << "Failed to send data chunk\n"; return false; }
                }
                std::cout << "Skipping Intel HEX record targeted to RAM at 0x"
                          << std::hex << absaddr << std::dec << " size=" << data.size() << std::endl;
                continue;
            }

            // if chunk empty initialize
            if (chunk.empty()) {
                chunk_base = absaddr;
            }
            // if not contiguous or would overflow, flush
            if (absaddr != chunk_base + chunk.size() || (chunk.size() + data.size() > uprot_PROGmaxsize)) {
                if (!flush_chunk(true)) { std::cerr << "Failed to send data chunk\n"; return false; }
                chunk_base = absaddr;
            }
            // append
            for (uint8_t b : data) chunk.push_back(b);
            // if chunk full flush
            if (chunk.size() >= uprot_PROGmaxsize) {
                if (!flush_chunk(true)) { std::cerr << "Failed to send data chunk\n"; return false; }
            }
        } else if (rectype == 0x01) { // EOF
            // flush any pending data
            if (!flush_chunk(true)) { std::cerr << "Failed to send final data chunk\n"; return false; }
            break;
        } else if (rectype == 0x04) { // extended linear address
            // value is two bytes in data
            if (data.size() >= 2) {
                uint32_t newUpper = (static_cast<uint32_t>(data[0]) << 8) | static_cast<uint32_t>(data[1]);
                // flush pending chunk before changing upper
                if (!flush_chunk(true)) { std::cerr << "Failed to send chunk before extended linear\n"; return false; }
                upper16 = newUpper;
            }
        } else {
            // other record types: treat as boundary -> flush
            if (!flush_chunk(true)) { std::cerr << "Failed to send chunk at record boundary\n"; return false; }
        }
    }

    // in case file ended without EOF record, flush
    if (!chunk.empty()) {
        if (!flush_chunk(true)) { std::cerr << "Failed to send trailing chunk\n"; return false; }
    }

    hexf.close();

    // send PROG_END: numberofpkts = chunks_sent + 1  (protocol requirement)
    uint16_t numberofpkts = static_cast<uint16_t>(chunks_sent + 2);
    std::cout << "Sending PROG_END, chunks_sent=" << chunks_sent << " numberofpkts=" << numberofpkts << std::endl;

    // retry PROG_END a few times in case of transient packet loss
    const int MAX_END_RETRIES = 3;
    eOuprot_result_t r_end = uprot_RES_ERR_TRYAGAIN;
    bool end_ok = false;
    for (int attempt = 0; attempt < MAX_END_RETRIES; ++attempt) {
        if (!sendPROG_END(numberofpkts, r_end)) {
            std::cerr << "PROG_END send/recv attempt " << attempt << " failed (no reply)" << std::endl;
            usleep(200000);
            continue;
        }
        if (r_end == uprot_RES_OK) { end_ok = true; break; }
        if (r_end == uprot_RES_ERR_TRYAGAIN) {
            std::cerr << "PROG_END returned TRYAGAIN, retrying..." << std::endl;
            usleep(200000);
            continue;
        }
        std::cerr << "PROG_END returned error " << (int)r_end << std::endl;
        break;
    }
    if (!end_ok) {
        std::cerr << "PROG_END no reply or not acknowledged after retries" << std::endl;
        return false;
    }
    std::cout << "PROG_END acknowledged, chunks sent: " << chunks_sent << std::endl;

    // restart the board to run the new application
    if (!def2run_application()) {
        std::cerr << "Warning: def2run_application failed" << std::endl;
    }
    sleep(1);
    if (!restart()) {
        std::cerr << "Warning: restart failed" << std::endl;
    }

    return true;
}

bool simpleEthClient::open(const char *ip, uint16_t port, double rx_timeout_sec)
{
    closeSocket();
    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) { perror("socket"); return false; }

    int on = 1;
    if (setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
    }
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    const char *local_ip_env = getenv("LOCAL_IP");
    if (local_ip_env && inet_pton(AF_INET, local_ip_env, &local.sin_addr) <= 0) {
        perror("inet_pton(LOCAL_IP)");
        closeSocket();
        return false;
    }
    if (!local_ip_env) local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (bind(sock_, (struct sockaddr*)&local, sizeof(local)) < 0) {
        perror("bind");
        closeSocket();
        return false;
    }

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

void simpleEthClient::closeSocket()
{
    if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
}

bool simpleEthClient::sendRaw(const void *buf, size_t len)
{
    if (sock_ < 0) return false;
    ssize_t s = sendto(sock_, buf, len, 0, (struct sockaddr*)&dest_, sizeof(dest_));
    return (s == (ssize_t)len);
}

bool simpleEthClient::discover()
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
        if ((size_t)r >= sizeof(eOuprot_cmd_DISCOVER_REPLY2_t))
        {
            auto *rep2 = reinterpret_cast<eOuprot_cmd_DISCOVER_REPLY2_t*>(buf);
            simpleEthClient::print_discover_reply(&rep2->discoveryreply, srcip);
            for (int i = 0; i < 2; ++i)
            {
                const eOuprot_procinfo_t &p = rep2->extraprocs[i];
                std::cout << " ExtraProc[" << i << "] type=" << (int)p.type
                          << " ver=" << (int)p.version.major << "." << (int)p.version.minor
                          << " rom_addr_kb=" << p.rom_addr_kb << " rom_size_kb=" << p.rom_size_kb << std::endl;
            }
        }
        else if ((size_t)r >= sizeof(eOuprot_cmd_MOREINFO_REPLY_t))
        {
            auto *more = reinterpret_cast<eOuprot_cmd_MOREINFO_REPLY_t*>(buf);
            simpleEthClient::print_discover_reply(&more->discover, srcip);
        }
        else if ((size_t)r >= sizeof(eOuprot_cmd_DISCOVER_REPLY_t))
        {
            auto *rep = reinterpret_cast<eOuprot_cmd_DISCOVER_REPLY_t*>(buf);
            simpleEthClient::print_discover_reply(rep, srcip);
        }
        else if ((size_t)r >= sizeof(eOuprot_cmd_LEGACY_SCAN_REPLY_t))
        {
            auto *scan = reinterpret_cast<eOuprot_cmd_LEGACY_SCAN_REPLY_t*>(buf);
            print_legacy_scan_reply(scan, srcip);
        }
        else
        {
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

bool simpleEthClient::jump2updater()
{
    eOuprot_cmd_DISCOVER_t cmd;
    memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
    cmd.opc  = uprot_OPC_LEGACY_SCAN;
    cmd.opc2 = uprot_OPC_DISCOVER;
    cmd.jump2updater = 1;

    if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto"); return false; }
    std::cout << "DISCOVER (jump2updater=1) sent to request maintenance mode." << std::endl;
    return true;
}

bool simpleEthClient::def2run_application()
{
    eOuprot_cmd_DEF2RUN_t cmd;
    memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
    cmd.opc = uprot_OPC_DEF2RUN;
    cmd.proc = static_cast<uint8_t>(eApplication);
    if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto"); return false; }
    std::cout << "DEF2RUN -> Application sent." << std::endl;
    return true;
}

bool simpleEthClient::restart()
{
    eOuprot_cmd_RESTART_t cmd;
    memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
    cmd.opc = uprot_OPC_RESTART;
    if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto"); return false; }
    std::cout << "RESTART sent." << std::endl;
    return true;
}

bool simpleEthClient::blink()
{
    eOuprot_cmd_BLINK_t cmd;
    memset(&cmd, EOUPROT_VALUE_OF_UNUSED_BYTE, sizeof(cmd));
    cmd.opc = uprot_OPC_BLINK;
    if (!sendRaw(&cmd, sizeof(cmd))) { perror("sendto"); return false; }
    std::cout << "BLINK sent." << std::endl;
    return true;
}

void simpleEthClient::print_discover_reply(const eOuprot_cmd_DISCOVER_REPLY_t *reply, const char *srcip)
{
    if (!reply) return;
    std::cout << "---- Discover reply from " << srcip << " ----" << std::endl;
    std::cout << "Result: " << (int)reply->reply.res
              << "  ProtVer: " << (int)reply->reply.protversion
              << "  sizeofextra: " << (int)reply->reply.sizeofextra << std::endl;

    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             reply->mac48[5], reply->mac48[4], reply->mac48[3],
             reply->mac48[2], reply->mac48[1], reply->mac48[0]);

    // try to obtain a human readable board name from the boardtype value
    const char *raw_board_name = eoboards_type2string2(static_cast<eObrd_type_t>(reply->boardtype), static_cast<eObool_t>(0));
    std::string board_name;
    if (raw_board_name) {
        board_name = raw_board_name;
        const char prefix[] = "eobrd_";
        if (board_name.rfind(prefix, 0) == 0) { // startswith
            board_name = board_name.substr(strlen(prefix));
        }
    }

    std::cout << "MAC: " << mac << "  Type: " << (int)reply->boardtype;
    if (!board_name.empty())
    {
        std::cout << " ( Board Info: " << board_name << " )";
    }
    std::cout << std::endl;

    std::cout << "Running: " << (int)reply->processes.runningnow
              << "  Def2run: " << (int)reply->processes.def2run
              << "  Startup: " << (int)reply->processes.startup
              << "  NumProcesses: " << (int)reply->processes.numberofthem << std::endl;

    std::cout << "Capabilities mask: 0x" << std::hex << reply->capabilities << std::dec << std::endl;

    int num = std::min<int>(reply->processes.numberofthem, 3);
    for (int i = 0; i < num; ++i)
    {
        const eOuprot_procinfo_t &p = reply->processes.info[i];
        std::cout << " Process[" << i << "] type=" << (int)p.type;
        std::cout << "  ver=";
        std::cout << (int)p.version.major << "." << (int)p.version.minor;
        std::cout << "  rom_addr_kb=" << p.rom_addr_kb << "  rom_size_kb=" << p.rom_size_kb << std::endl;
    }

    // if eoboards mapping did not give a name, fall back to boardinfo32 string if present
    if ((!board_name.empty()) && reply->boardinfo32[0] != EOUPROT_VALUE_OF_UNUSED_BYTE)
    {
        uint8_t len = reply->boardinfo32[0];
        std::string info;
        if (len > 0)
        {
            size_t copylen = std::min<size_t>(len, sizeof(reply->boardinfo32)-1);
            info.assign(reinterpret_cast<const char*>(&reply->boardinfo32[1]), copylen);
        }
        if (!info.empty())
        {
            std::cout << "Board info: " << info << std::endl;
        }
    }

    std::cout << "----------------------------------------" << std::endl;
}


void simpleEthClient::print_legacy_scan_reply(const eOuprot_cmd_LEGACY_SCAN_REPLY_t *scan, const char *srcip)
{
    if (!scan) return;
    std::cout << "---- Legacy scan reply from " << srcip << " ----" << std::endl;
    std::cout << "opc: " << (int)scan->opc
              << "  version: " << (int)scan->version.major << "." << (int)scan->version.minor << std::endl;
    char mac[18] = {0};
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             scan->mac48[5], scan->mac48[4], scan->mac48[3],
             scan->mac48[2], scan->mac48[1], scan->mac48[0]);
    std::cout << "MAC: " << mac << std::endl;

    uint32_t mask = 0;
    memcpy(&mask, scan->ipmask, sizeof(mask));
    mask = ntohl(mask);
    uint8_t b0 = (mask >> 24) & 0xFF;
    uint8_t b1 = (mask >> 16) & 0xFF;
    uint8_t b2 = (mask >> 8) & 0xFF;
    uint8_t b3 = (mask >> 0) & 0xFF;
    std::cout << "IP mask (dotted): " << (int)b0 << "." << (int)b1 << "." << (int)b2 << "." << (int)b3
              << "  (raw 0x" << std::hex << mask << std::dec << ")" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
}


int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <board_ip> <command>\n"
                  << "Commands: discover, maintenance|jump2updater, application|def2run_application, restart, blink, program\n";
        return 1;
    }

    const char *ip = argv[1];
    std::string cmd = argv[2];

    simpleEthClient client;
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
    else if (cmd == "program")
    {
        if (!client.program()) {
            std::cerr << "Programming failed\n";
            return 1;
        }
    }
    else
    {
        std::cerr << "Unknown command: " << cmd << std::endl;
        return 1;
    }

    return 0;
}