#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>

#define CONFIG_FILE "/etc/netmon.conf"

void print_json(const char *iface, unsigned long rx, unsigned long tx) {
    time_t now = time(NULL);
    printf("{\"interface\":\"%s\",\"timestamp\":%ld,\"rx_bytes\":%lu,\"tx_bytes\":%lu}\n",iface, now, rx, tx);
    fflush(stdout);
}

void get_stats(const char *iface, unsigned long *rx, unsigned long *tx) {
    char buf[512];
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return;
    while (fgets(buf, sizeof(buf), f)) {
        char *p = strstr(buf, iface);
        if (p && (p == buf || *(p-1) == ' ')) {
            sscanf(p + strlen(iface) + 1, "%lu", rx);
            char *tx_ptr = strchr(p, ':');
            if (tx_ptr) {
                int i;
                tx_ptr++;
                for (i = 0; i < 8; i++) {
                    strtok(i == 0 ? tx_ptr : NULL, " ");
                }
                *tx = strtoul(strtok(NULL, " "), NULL, 10);
            }
            break;
        }
    }
    fclose(f);
}

// Read interface from config file
int get_iface_from_config(char *iface, size_t len) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return 0;
    if (fgets(iface, len, f)) {
        // Remove trailing newline
        iface[strcspn(iface, "\r\n")] = 0;
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

// Read interface from UCI (OpenWrt)
int get_iface_from_uci(char *iface, size_t len) {
    FILE *fp = popen("uci get netmon.interface 2>/dev/null", "r");
    if (!fp) return 0;
    if (fgets(iface, len, fp)) {
        iface[strcspn(iface, "\r\n")] = 0;
        pclose(fp);
        return 1;
    }
    pclose(fp);
    return 0;
}

// List all network interfaces except lo
int get_all_ifaces(char ifaces[][32], int max_ifaces) {
    DIR *d = opendir("/sys/class/net");
    struct dirent *dir;
    int count = 0;
    if (!d) return 0;
    while ((dir = readdir(d)) != NULL && count < max_ifaces) {
        if (dir->d_name[0] == '.') continue;
        if (strcmp(dir->d_name, "lo") == 0) continue;
        strncpy(ifaces[count++], dir->d_name, 31);
        ifaces[count-1][31] = 0;
    }
    closedir(d);
    return count;
}

int main(int argc, char *argv[]) {
    char iface[32] = "";
    int monitor_all = 0;
    int interval = 1;
    int i;

    // Priority: CLI arg > config file > UCI > all
    if (argc > 1) {
        if (strcmp(argv[1], "all") == 0) {
            monitor_all = 1;
        } else {
            strncpy(iface, argv[1], 31);
            iface[31] = 0;
        }
    } else if (get_iface_from_config(iface, sizeof(iface))) {
        // got iface from config
    } else if (get_iface_from_uci(iface, sizeof(iface))) {
        // got iface from uci
    } else {
        monitor_all = 1;
    }

    while (1) {
        if (monitor_all) {
            char ifaces[16][32];
            int n = get_all_ifaces(ifaces, 16);
            for (i = 0; i < n; i++) {
                unsigned long rx = 0, tx = 0;
                get_stats(ifaces[i], &rx, &tx);
                print_json(ifaces[i], rx, tx);
            }
        } else {
            unsigned long rx = 0, tx = 0;
            get_stats(iface, &rx, &tx);
            print_json(iface, rx, tx);
        }
        sleep(interval);
    }
    return 0;
}