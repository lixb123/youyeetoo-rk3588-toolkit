#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <poll.h>
#include <signal.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_FRAME 65536
#define PROTO_PORT 47000

static volatile sig_atomic_t running = 1;
static FILE *log_file = NULL;

static void log_vprintf(FILE *console, const char *format, va_list arguments) {
    va_list copy;
    va_copy(copy, arguments);
    vfprintf(console, format, arguments);
    fflush(console);
    if (log_file != NULL) {
        vfprintf(log_file, format, copy);
        fflush(log_file);
    }
    va_end(copy);
}

static void log_printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    log_vprintf(stdout, format, arguments);
    va_end(arguments);
}

static void log_errorf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    log_vprintf(stderr, format, arguments);
    va_end(arguments);
}

static void log_errno(const char *operation) {
    int error_number = errno;
    log_errorf("%s: %s\n", operation, strerror(error_number));
}

static void open_log_file(const char *program) {
    char executable[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length <= 0 || (size_t)length >= sizeof(executable)) {
        if (realpath(program, executable) == NULL) {
            strncpy(executable, program, sizeof(executable) - 1);
            executable[sizeof(executable) - 1] = '\0';
        }
    } else {
        executable[length] = '\0';
    }
    char *separator = strrchr(executable, '/');
    if (separator == NULL) {
        strncpy(executable, ".", sizeof(executable) - 1);
        executable[sizeof(executable) - 1] = '\0';
    } else {
        *separator = '\0';
    }
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/youyeetoo_eth_gateway.log", executable);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        fprintf(stderr, "log path is too long\n");
        return;
    }
    log_file = fopen(path, "a");
    if (log_file == NULL) {
        fprintf(stderr, "cannot open log %s: %s\n", path, strerror(errno));
        return;
    }
    setvbuf(log_file, NULL, _IOLBF, 0);
    time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);
    fprintf(log_file, "\n[%04d-%02d-%02d %02d:%02d:%02d] gateway start\n",
            local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
            local_time.tm_hour, local_time.tm_min, local_time.tm_sec);
    fflush(log_file);
    log_printf("log_file=%s\n", path);
}

struct options {
    const char *payload_iface;
    const char *x_iface;
    uint16_t udp_port;
    int promiscuous;
};

struct monitor_socket {
    int fd;
    int ifindex;
    const char *name;
};

static void stop_handler(int signal_number) {
    (void)signal_number;
    running = 0;
}

static void usage(const char *program) {
    log_errorf(
            "Usage: %s [--payload-iface IFACE] [--x-iface IFACE] "
            "[--port PORT] [--promisc]\n",
            program);
}

static int parse_options(int argc, char **argv, struct options *options) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--payload-iface") == 0 && i + 1 < argc) {
            options->payload_iface = argv[++i];
        } else if (strcmp(argv[i], "--x-iface") == 0 && i + 1 < argc) {
            options->x_iface = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long value = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || value > 65535) return -1;
            options->udp_port = (uint16_t)value;
        } else if (strcmp(argv[i], "--no-promisc") == 0) {
            options->promiscuous = 0;
        } else if (strcmp(argv[i], "--promisc") == 0) {
            options->promiscuous = 1;
        } else {
            return -1;
        }
    }
    return 0;
}

static uint32_t ip_value(const char *text) {
    struct in_addr address;
    if (inet_pton(AF_INET, text, &address) != 1) return 0;
    return ntohl(address.s_addr);
}

static int ip_in_10_2_1(uint32_t ip) {
    return (ip & 0xffff0000U) == 0x0a020000U;
}

static int is_pair(uint32_t a, uint32_t b, uint32_t x, uint32_t y) {
    return (a == x && b == y) || (a == y && b == x);
}

static int flow_priority(uint32_t src, uint32_t dst, const char **name) {
    const uint32_t ip35 = ip_value("10.240.1.35");
    const uint32_t ip36 = ip_value("10.240.1.36");
    const uint32_t ip37 = ip_value("10.240.1.37");
    const uint32_t ip38 = ip_value("10.240.1.38");
    const uint32_t ip39 = ip_value("10.240.1.39");
    const uint32_t ip40 = ip_value("10.240.1.40");
    const uint32_t ip50 = ip_value("10.240.1.50");
    const uint32_t ip51 = ip_value("10.240.1.51");
    const uint32_t ip52 = ip_value("10.240.1.52");

    if (src == ip36 || dst == ip36) {
        *name = "upload/platform-interaction";
        return 0;
    }
    if (is_pair(src, dst, ip38, ip50) || ip_in_10_2_1(src) || ip_in_10_2_1(dst)) {
        *name = "business";
        return 1;
    }
    if (is_pair(src, dst, ip35, ip51) || is_pair(src, dst, ip39, ip52)) {
        *name = "management/log";
        return 2;
    }
    if (is_pair(src, dst, ip37, ip40)) {
        *name = "S1-C/S1-U";
        return 1;
    }
    *name = "other";
    return 3;
}

static uint16_t protocol_checksum(const unsigned char *packet, size_t data_length) {
    uint32_t sum = 0;
    for (size_t i = 2; i < 8; ++i) sum += packet[i];
    for (size_t i = 0; i < data_length; ++i) sum += packet[8 + i];
    return (uint16_t)(~sum & 0xffffU);
}

static void print_hex(const unsigned char *data, size_t length, size_t limit) {
    size_t count = length < limit ? length : limit;
    for (size_t i = 0; i < count; ++i) log_printf("%02X", data[i]);
    if (count < length) log_printf("...");
}

static void print_payload(const unsigned char *data, size_t length) {
    log_printf(" data_hex=");
    print_hex(data, length, MAX_FRAME);
}

static void decode_protocol(const unsigned char *data, size_t length) {
    if (length < 10) {
        log_printf(" protocol=short bytes=%zu", length);
        return;
    }
    uint16_t marker = (uint16_t)((data[0] << 8) | data[1]);
    if (marker != 0xeb90 && marker != 0x1acf) {
        log_printf(" protocol=opaque");
        print_payload(data, length);
        return;
    }
    uint16_t word1 = (uint16_t)((data[2] << 8) | data[3]);
    uint16_t word2 = (uint16_t)((data[4] << 8) | data[5]);
    uint16_t data_field_minus_one = (uint16_t)((data[6] << 8) | data[7]);
    size_t data_length = (size_t)data_field_minus_one + 1U;
    size_t expected = 8U + data_length + 2U;
    unsigned int version = (word1 >> 13) & 0x7U;
    unsigned int type = (word1 >> 12) & 0x1U;
    unsigned int secondary = (word1 >> 11) & 0x1U;
    unsigned int apid = word1 & 0x7ffU;
    unsigned int grouping = (word2 >> 14) & 0x3U;
    unsigned int sequence = word2 & 0x3fffU;
    log_printf(" protocol=0x%04X version=%u type=%u secondary=%u apid=0x%03X"
           " grouping=%u seq=%u data_len=%zu",
           marker, version, type, secondary, apid, grouping, sequence, data_length);
    if (expected > length) {
        log_printf(" checksum=truncated expected=%zu", expected);
        return;
    }
    uint16_t received = (uint16_t)((data[8 + data_length] << 8) |
                                   data[8 + data_length + 1]);
    uint16_t calculated = protocol_checksum(data, data_length);
    log_printf(" checksum=%s(0x%04X/0x%04X)",
           received == calculated ? "ok" : "BAD", received, calculated);
    print_payload(data, expected <= length ? expected : length);
    if (marker == 0xeb90 && data_length >= 2 && (apid & 0x0fU) == 0x0fU) {
        uint16_t block = (uint16_t)((data[8] << 8) | data[9]);
        log_printf(" file_block=%u file_bytes=%zu", block, data_length - 2U);
    } else if (data_length >= 2) {
        uint16_t opcode = (uint16_t)((data[8] << 8) | data[9]);
        log_printf(" opcode=0x%04X", opcode);
    }
}

static int open_monitor_socket(const char *iface, int promiscuous) {
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) {
        log_errorf("socket(AF_PACKET,%s): %s\n", iface, strerror(errno));
        return -1;
    }
    int ifindex = (int)if_nametoindex(iface);
    if (ifindex == 0) {
        log_errorf("interface %s not found\n", iface);
        close(fd);
        return -1;
    }
    struct sockaddr_ll address;
    memset(&address, 0, sizeof(address));
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(ETH_P_ALL);
    address.sll_ifindex = ifindex;
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        log_errorf("bind(AF_PACKET,%s): %s\n", iface, strerror(errno));
        close(fd);
        return -1;
    }
    if (promiscuous) {
        struct packet_mreq membership;
        memset(&membership, 0, sizeof(membership));
        membership.mr_ifindex = ifindex;
        membership.mr_type = PACKET_MR_PROMISC;
        if (setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
                       &membership, sizeof(membership)) < 0) {
            log_errorf("warning: promiscuous mode %s: %s\n",
                    iface, strerror(errno));
        }
    }
    return fd;
}

static void inspect_frame(const char *iface, const unsigned char *frame, size_t length,
                          uint16_t protocol_port) {
    size_t l3 = 14;
    if (length < l3) return;
    uint16_t ethertype = (uint16_t)((frame[12] << 8) | frame[13]);
    if (ethertype == 0x8100 && length >= 18) {
        ethertype = (uint16_t)((frame[16] << 8) | frame[17]);
        l3 = 18;
    }
    if (ethertype != ETH_P_IP || length < l3 + sizeof(struct iphdr)) {
        return;
    }
    const struct iphdr *ip = (const struct iphdr *)(frame + l3);
    size_t ip_header_length = (size_t)ip->ihl * 4U;
    if (ip->version != 4 || ip_header_length < 20 || length < l3 + ip_header_length) return;
    char source[INET_ADDRSTRLEN];
    char destination[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip->saddr, source, sizeof(source));
    inet_ntop(AF_INET, &ip->daddr, destination, sizeof(destination));
    uint32_t source_value = ntohl(ip->saddr);
    uint32_t destination_value = ntohl(ip->daddr);
    const char *flow = NULL;
    int priority = flow_priority(source_value, destination_value, &flow);
    log_printf("RX iface=%s src=%s dst=%s proto=%u bytes=%zu priority=%d flow=%s",
           iface, source, destination, ip->protocol, length, priority, flow);
    if (ip->protocol == IPPROTO_UDP && length >= l3 + ip_header_length + sizeof(struct udphdr)) {
        const struct udphdr *udp = (const struct udphdr *)(frame + l3 + ip_header_length);
        size_t udp_offset = l3 + ip_header_length;
        size_t udp_length = ntohs(udp->len);
        if (udp_length >= sizeof(struct udphdr) && udp_offset + udp_length <= length) {
            size_t payload_length = udp_length - sizeof(struct udphdr);
            log_printf(" sport=%u dport=%u", ntohs(udp->source), ntohs(udp->dest));
            if (ntohs(udp->source) == protocol_port || ntohs(udp->dest) == protocol_port) {
                decode_protocol(frame + udp_offset + sizeof(struct udphdr), payload_length);
            }
        }
    }
    log_printf("\n");
}

int main(int argc, char **argv) {
    struct options options = {
        .payload_iface = "eth0",
        .x_iface = "eth1",
        .udp_port = PROTO_PORT,
        .promiscuous = 0,
    };
    open_log_file(argv[0]);
    if (parse_options(argc, argv, &options) != 0) {
        usage(argv[0]);
        return 2;
    }
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    struct monitor_socket monitors[2];
    monitors[0].name = options.payload_iface;
    monitors[1].name = options.x_iface;
    for (size_t i = 0; i < 2; ++i) {
        monitors[i].fd = open_monitor_socket(monitors[i].name, options.promiscuous);
        if (monitors[i].fd < 0) {
            if (i > 0) close(monitors[0].fd);
            return 1;
        }
        monitors[i].ifindex = (int)if_nametoindex(monitors[i].name);
    }
    log_printf("youyeetoo gateway monitor payload=%s x=%s port=%u\n",
           options.payload_iface, options.x_iface, options.udp_port);
    log_printf("priority: 0 upload, 1 business/S1, 2 management-log, 3 other\n");
    struct pollfd pollfds[2];
    for (size_t i = 0; i < 2; ++i) {
        pollfds[i].fd = monitors[i].fd;
        pollfds[i].events = POLLIN;
    }
    unsigned char frame[MAX_FRAME];
    while (running) {
        int result = poll(pollfds, 2, 1000);
        if (result < 0) {
            if (errno == EINTR) continue;
            log_errno("poll");
            break;
        }
        if (result == 0) continue;
        for (size_t i = 0; i < 2; ++i) {
            if ((pollfds[i].revents & POLLIN) == 0) continue;
            struct sockaddr_ll source;
            socklen_t source_length = sizeof(source);
            ssize_t length = recvfrom(monitors[i].fd, frame, sizeof(frame), 0,
                                      (struct sockaddr *)&source, &source_length);
            if (length < 0) {
                if (errno != EINTR) log_errno("recvfrom");
                continue;
            }
            if (source.sll_pkttype == PACKET_OUTGOING) continue;
            inspect_frame(monitors[i].name, frame, (size_t)length, options.udp_port);
        }
    }
    close(monitors[0].fd);
    close(monitors[1].fd);
    log_printf("gateway monitor stopped\n");
    if (log_file != NULL) fclose(log_file);
    return 0;
}
