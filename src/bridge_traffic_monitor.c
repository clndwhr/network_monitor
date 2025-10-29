#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

// gcc -o bridge_traffic_monitor bridge_traffic_monitor.c

#define MAX_INTERFACES 20
#define MAX_REQUIRED_INTERFACES 10
#define UCI_PACKAGE "quecmanager"
#define UCI_SECTION "bridge_monitor"
#define CONFIG_FILE "/etc/quecmanager/settings/bridge_traffic_monitor.conf"
#define DEFAULT_OUTPUT_DIR "/tmp/quecmanager"
#define PID_FILE "/tmp/quecmanager/bridge_traffic_monitor.pid"
#define DEFAULT_REFRESH_RATE_MS 100

// Configuration structure
struct config {
    char output_path[512];
    int minimal_mode;
    int json_mode;
    char config_source[64]; // Track where config came from
    char channel[64]; // Channel name used in output (e.g., JSON "channel")
    char required_interfaces[MAX_REQUIRED_INTERFACES][32]; // List of required interfaces
    int required_interface_count;
    int refresh_rate_ms; // Refresh rate in milliseconds
    int websocat_enabled; // Send output to websocat
    char websocat_url[256]; // WebSocket URL (host:port or full ws:// URL)
};

struct interface_stats {
    char name[32];
    unsigned long long rx_bytes;
    unsigned long long tx_bytes;
    unsigned long long rx_packets;
    unsigned long long tx_packets;
    unsigned long long rx_errors;
    unsigned long long tx_errors;
    unsigned long long rx_dropped;
    unsigned long long tx_dropped;
    int active;
};

// Read a stat value from sysfs
unsigned long long read_stat(const char *interface, const char *stat_name) {
    char path[256];
    FILE *f;
    unsigned long long value = 0;

    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s", interface, stat_name);
    f = fopen(path, "r");
    if (f) {
        fscanf(f, "%llu", &value);
        fclose(f);
    }
    return value;
}

// Check if interface exists in system
int interface_exists(const char *name) {
    char path[256];
    struct stat st;
    snprintf(path, sizeof(path), "/sys/class/net/%s", name);
    return (stat(path, &st) == 0);
}

// Read interface operational state
int is_interface_up(const char *interface) {
    char path[256];
    char state[32];
    FILE *f;

    // Check if interface exists first
    if (!interface_exists(interface)) {
        return 0;
    }

    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", interface);
    f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%s", state) == 1) {
            fclose(f);
            return (strcmp(state, "up") == 0 || strcmp(state, "unknown") == 0);
        }
        fclose(f);
    }
    return 0;
}

// Check if interface is a required interface (always show)
int is_required_interface(const char *name, struct config *cfg) {
    for (int i = 0; i < cfg->required_interface_count; i++) {
        if (strcmp(name, cfg->required_interfaces[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Discover all network interfaces
int discover_interfaces(struct interface_stats *interfaces, struct config *cfg) {
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    dir = opendir("/sys/class/net");
    if (!dir) {
        perror("Cannot open /sys/class/net");
        return 0;
    }

    // First, add all required interfaces (even if down or with zero stats)
    for (int i = 0; i < cfg->required_interface_count && count < MAX_INTERFACES; i++) {
        if (interface_exists(cfg->required_interfaces[i])) {
            strncpy(interfaces[count].name, cfg->required_interfaces[i], sizeof(interfaces[count].name) - 1);
            interfaces[count].active = 1;
            count++;
        }
    }

    // Then add any other interfaces discovered (excluding loopback and already added)
    while ((entry = readdir(dir)) != NULL && count < MAX_INTERFACES) {
        if (entry->d_name[0] == '.') continue;

        // Skip loopback
        if (strcmp(entry->d_name, "lo") == 0) continue;

        // Check if this interface is already in the list (required interfaces)
        int already_added = 0;
        for (int i = 0; i < count; i++) {
            if (strcmp(interfaces[i].name, entry->d_name) == 0) {
                already_added = 1;
                break;
            }
        }

        if (!already_added) {
            strncpy(interfaces[count].name, entry->d_name, sizeof(interfaces[count].name) - 1);
            interfaces[count].active = 1;
            count++;
        }
    }

    closedir(dir);
    return count;
}

// Read all stats for an interface
void read_interface_stats(struct interface_stats *iface) {
    // Check if interface exists before reading stats
    if (!interface_exists(iface->name)) {
        // Interface doesn't exist - set all stats to 0
        iface->rx_bytes = 0;
        iface->tx_bytes = 0;
        iface->rx_packets = 0;
        iface->tx_packets = 0;
        iface->rx_errors = 0;
        iface->tx_errors = 0;
        iface->rx_dropped = 0;
        iface->tx_dropped = 0;
        return;
    }

    iface->rx_bytes = read_stat(iface->name, "rx_bytes");
    iface->tx_bytes = read_stat(iface->name, "tx_bytes");
    iface->rx_packets = read_stat(iface->name, "rx_packets");
    iface->tx_packets = read_stat(iface->name, "tx_packets");
    iface->rx_errors = read_stat(iface->name, "rx_errors");
    iface->tx_errors = read_stat(iface->name, "tx_errors");
    iface->rx_dropped = read_stat(iface->name, "rx_dropped");
    iface->tx_dropped = read_stat(iface->name, "tx_dropped");
}

// Read iptables byte counters
unsigned long long read_iptables_counters(const char *chain, const char *direction) {
    FILE *fp;
    char cmd[256];
    char line[512];
    unsigned long long total_bytes = 0;

    snprintf(cmd, sizeof(cmd), "iptables -L %s -v -n -x 2>/dev/null | tail -n +3", chain);
    fp = popen(cmd, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            unsigned long long pkts, bytes;
            if (sscanf(line, "%llu %llu", &pkts, &bytes) == 2) {
                total_bytes += bytes;
            }
        }
        pclose(fp);
    }
    return total_bytes;
}

// Create directory structure recursively
int create_directory_recursive(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

// Write PID file
int write_pid_file(const char *pid_path) {
    FILE *f;
    char pid_dir[512];
    char *last_slash;

    // Extract directory from PID file path
    strncpy(pid_dir, pid_path, sizeof(pid_dir) - 1);
    last_slash = strrchr(pid_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (create_directory_recursive(pid_dir) != 0) {
            fprintf(stderr, "Warning: Could not create directory for PID file: %s\n", pid_dir);
            return -1;
        }
    }

    f = fopen(pid_path, "w");
    if (!f) {
        perror("Cannot create PID file");
        return -1;
    }
    fprintf(f, "%d\n", getpid());
    fclose(f);
    return 0;
}

// Remove PID file
void remove_pid_file(const char *pid_path) {
    unlink(pid_path);
}

// Send data to websocat
int send_to_websocat(const char *data, const char *ws_url) {
    FILE *fp;
    char cmd[512];
    int result;

    // Ensure URL has ws:// prefix
    char full_url[256];
    if (strncmp(ws_url, "ws://", 5) != 0) {
        snprintf(full_url, sizeof(full_url), "ws://%s", ws_url);
    } else {
        snprintf(full_url, sizeof(full_url), "%s", ws_url);
    }

    // Create command: echo "data" | websocat --one-message ws://url
    snprintf(cmd, sizeof(cmd), "echo '%s' | websocat --one-message '%s' 2>/dev/null", data, full_url);
    
    fp = popen(cmd, "r");
    if (!fp) {
        return -1;
    }
    
    result = pclose(fp);
    return (result == 0) ? 0 : -1;
}

// Send file content to websocat
int send_file_to_websocat(const char *filepath, const char *ws_url, int compact_json) {
    FILE *file_fp, *ws_fp;
    char cmd[512];
    char line[4096];
    int result;
    int first_line = 1;

    // Ensure URL has ws:// prefix
    char full_url[256];
    if (strncmp(ws_url, "ws://", 5) != 0) {
        snprintf(full_url, sizeof(full_url), "ws://%s", ws_url);
    } else {
        snprintf(full_url, sizeof(full_url), "%s", ws_url);
    }

    // Open the file to read
    file_fp = fopen(filepath, "r");
    if (!file_fp) {
        return -1;
    }

    // Create command: websocat --one-message ws://url
    snprintf(cmd, sizeof(cmd), "websocat --one-message '%s' 2>/dev/null", full_url);
    ws_fp = popen(cmd, "w");
    if (!ws_fp) {
        fclose(file_fp);
        return -1;
    }

    // Read file and send to websocat
    while (fgets(line, sizeof(line), file_fp)) {
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        if (len > 1 && line[len-2] == '\r') {
            line[len-2] = '\0';
        }

        if (compact_json) {
            // JSON mode: remove all leading/trailing whitespace and send as compact JSON
            char *src = line;
            char *dst = line;
            int in_string = 0;

            // Remove whitespace outside of JSON strings
            while (*src) {
                if (*src == '"' && (src == line || *(src-1) != '\\')) {
                    in_string = !in_string;
                    *dst++ = *src++;
                } else if (in_string) {
                    *dst++ = *src++;
                } else if (*src != ' ' && *src != '\t' && *src != '\r' && *src != '\n') {
                    *dst++ = *src++;
                } else {
                    src++;
                }
            }
            *dst = '\0';

            fprintf(ws_fp, "%s", line);
        } else {
            // Non-JSON mode: replace newlines with \n literal
            if (!first_line) {
                fprintf(ws_fp, "\\n");
            }
            fprintf(ws_fp, "%s", line);
            first_line = 0;
        }
    }

    fclose(file_fp);
    result = pclose(ws_fp);
    return (result == 0) ? 0 : -1;
}

// Read UCI configuration value
char* read_uci_value(const char *package, const char *section, const char *option) {
    static char value[512];
    char cmd[256];
    FILE *fp;

    value[0] = '\0';
    snprintf(cmd, sizeof(cmd), "uci -q get %s.%s.%s 2>/dev/null", package, section, option);

    fp = popen(cmd, "r");
    if (fp) {
        if (fgets(value, sizeof(value), fp)) {
            // Remove trailing newline
            size_t len = strlen(value);
            if (len > 0 && value[len-1] == '\n') {
                value[len-1] = '\0';
            }
        }
        pclose(fp);
    }

    return (value[0] != '\0') ? value : NULL;
}

// Check if UCI package exists
int uci_config_exists(const char *package, const char *section) {
    char cmd[256];
    int result;

    snprintf(cmd, sizeof(cmd), "uci -q show %s.%s >/dev/null 2>&1", package, section);
    result = system(cmd);

    return (result == 0);
}

// Load configuration from UCI
int load_uci_config(struct config *cfg) {
    char *value;
    int found = 0;

    // Check if UCI config exists
    if (!uci_config_exists(UCI_PACKAGE, UCI_SECTION)) {
        return 0;
    }

    // Read output_path
    value = read_uci_value(UCI_PACKAGE, UCI_SECTION, "output_path");
    if (value) {
        snprintf(cfg->output_path, sizeof(cfg->output_path), "%s", value);
        found = 1;
    }

    // Read minimal_mode
    value = read_uci_value(UCI_PACKAGE, UCI_SECTION, "minimal_mode");
    if (value) {
        cfg->minimal_mode = (strcmp(value, "1") == 0 || 
                            strcmp(value, "true") == 0 || 
                            strcmp(value, "yes") == 0 ||
                            strcmp(value, "enabled") == 0);
        found = 1;
    }

    // Read json_mode
    value = read_uci_value(UCI_PACKAGE, UCI_SECTION, "json_mode");
    if (value) {
        cfg->json_mode = (strcmp(value, "1") == 0 || 
                         strcmp(value, "true") == 0 || 
                         strcmp(value, "yes") == 0 ||
                         strcmp(value, "enabled") == 0);
        found = 1;
    }

    // Read refresh_rate_ms
    value = read_uci_value(UCI_PACKAGE, UCI_SECTION, "refresh_rate_ms");
    if (value) {
        int rate = atoi(value);
        if (rate > 0 && rate <= 10000) { // Between 1ms and 10s
            cfg->refresh_rate_ms = rate;
            found = 1;
        }
    }

    // Read websocat_enabled
    value = read_uci_value(UCI_PACKAGE, UCI_SECTION, "websocat_enabled");
    if (value) {
        cfg->websocat_enabled = (strcmp(value, "1") == 0 || 
                                strcmp(value, "true") == 0 || 
                                strcmp(value, "yes") == 0 ||
                                strcmp(value, "enabled") == 0);
        found = 1;
    }

    // Read websocat_url
    value = read_uci_value(UCI_PACKAGE, UCI_SECTION, "websocat_url");
    if (value) {
        snprintf(cfg->websocat_url, sizeof(cfg->websocat_url), "%s", value);
        found = 1;
    }

    // Read channel name
    value = read_uci_value(UCI_PACKAGE, UCI_SECTION, "channel");
    if (value) {
        snprintf(cfg->channel, sizeof(cfg->channel), "%s", value);
        found = 1;
    }

    // Read required_interfaces (comma-separated list)
    value = read_uci_value(UCI_PACKAGE, UCI_SECTION, "required_interfaces");
    if (value) {
        cfg->required_interface_count = 0;
        char *token = strtok(value, ",");
        while (token != NULL && cfg->required_interface_count < MAX_REQUIRED_INTERFACES) {
            // Trim whitespace
            while (*token == ' ' || *token == '\t') token++;
            strncpy(cfg->required_interfaces[cfg->required_interface_count], token, 
                    sizeof(cfg->required_interfaces[0]) - 1);
            cfg->required_interface_count++;
            token = strtok(NULL, ",");
        }
        found = 1;
    }

    if (found) {
        snprintf(cfg->config_source, sizeof(cfg->config_source), "UCI (%s.%s)", UCI_PACKAGE, UCI_SECTION);
    }

    return found;
}

// Load configuration from file
int load_file_config(struct config *cfg) {
    FILE *f;
    char line[512];
    char key[128], value[384];
    int found = 0;

    f = fopen(CONFIG_FILE, "r");
    if (!f) {
        return 0; // Config file is optional
    }

    while (fgets(line, sizeof(line), f)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        // Parse key=value
        if (sscanf(line, "%127[^=]=%383[^\n\r]", key, value) == 2) {
            // Trim whitespace
            char *key_trim = key;
            char *value_trim = value;
            while (*key_trim == ' ' || *key_trim == '\t') key_trim++;
            while (*value_trim == ' ' || *value_trim == '\t') value_trim++;

            if (strcmp(key_trim, "output_path") == 0) {
                snprintf(cfg->output_path, sizeof(cfg->output_path), "%s", value_trim);
                found = 1;
            } else if (strcmp(key_trim, "channel") == 0) {
                snprintf(cfg->channel, sizeof(cfg->channel), "%s", value_trim);
                found = 1;
            } else if (strcmp(key_trim, "minimal_mode") == 0) {
                cfg->minimal_mode = (strcmp(value_trim, "1") == 0 || 
                                    strcmp(value_trim, "true") == 0 || 
                                    strcmp(value_trim, "yes") == 0);
                found = 1;
            } else if (strcmp(key_trim, "json_mode") == 0) {
                cfg->json_mode = (strcmp(value_trim, "1") == 0 || 
                                 strcmp(value_trim, "true") == 0 || 
                                 strcmp(value_trim, "yes") == 0);
                found = 1;
            } else if (strcmp(key_trim, "refresh_rate_ms") == 0) {
                int rate = atoi(value_trim);
                if (rate > 0 && rate <= 10000) {
                    cfg->refresh_rate_ms = rate;
                    found = 1;
                }
            } else if (strcmp(key_trim, "websocat_enabled") == 0) {
                cfg->websocat_enabled = (strcmp(value_trim, "1") == 0 || 
                                        strcmp(value_trim, "true") == 0 || 
                                        strcmp(value_trim, "yes") == 0);
                found = 1;
            } else if (strcmp(key_trim, "websocat_url") == 0) {
                snprintf(cfg->websocat_url, sizeof(cfg->websocat_url), "%s", value_trim);
                found = 1;
            } else if (strcmp(key_trim, "required_interfaces") == 0) {
                cfg->required_interface_count = 0;
                char *token = strtok(value_trim, ",");
                while (token != NULL && cfg->required_interface_count < MAX_REQUIRED_INTERFACES) {
                    // Trim whitespace
                    while (*token == ' ' || *token == '\t') token++;
                    strncpy(cfg->required_interfaces[cfg->required_interface_count], token, 
                            sizeof(cfg->required_interfaces[0]) - 1);
                    cfg->required_interface_count++;
                    token = strtok(NULL, ",");
                }
                found = 1;
            }
        }
    }

    fclose(f);

    if (found) {
        snprintf(cfg->config_source, sizeof(cfg->config_source), "Config file (%s)", CONFIG_FILE);
    }

    return found;
}

// Load configuration with priority: defaults -> file -> UCI -> command line
void load_config(struct config *cfg) {
    // Set defaults first
    snprintf(cfg->output_path, sizeof(cfg->output_path), "%s/bridge_traffic_monitor", DEFAULT_OUTPUT_DIR);
    cfg->minimal_mode = 0;
    cfg->json_mode = 0;
    cfg->refresh_rate_ms = DEFAULT_REFRESH_RATE_MS;
    cfg->websocat_enabled = 0;
    snprintf(cfg->websocat_url, sizeof(cfg->websocat_url), "ws://localhost:8838");
    snprintf(cfg->channel, sizeof(cfg->channel), "network-monitor");
    snprintf(cfg->config_source, sizeof(cfg->config_source), "Defaults");

    // Set default required interfaces
    const char *default_required[] = {"rmnet_data0", "rmnet_ipa0", "eth0", "br-lan", "rmnet_data1"};
    cfg->required_interface_count = 5;
    for (int i = 0; i < 5; i++) {
        strncpy(cfg->required_interfaces[i], default_required[i], sizeof(cfg->required_interfaces[i]) - 1);
    }

    // Try to load from file (lower priority)
    if (load_file_config(cfg)) {
        // File config loaded
    }

    // Try to load from UCI (higher priority - overrides file)
    if (load_uci_config(cfg)) {
        // UCI config loaded and will override file settings
    }
}

void monitor_traffic(struct config *cfg) {
    struct interface_stats current[MAX_INTERFACES];
    struct interface_stats previous[MAX_INTERFACES];
    struct timespec current_time, prev_time;
    double time_diff;
    int interface_count;
    int first_run = 1;
    char output_dir[512];
    char *last_slash;

    // Extract and create output directory
    strncpy(output_dir, cfg->output_path, sizeof(output_dir) - 1);
    last_slash = strrchr(output_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (create_directory_recursive(output_dir) != 0) {
            fprintf(stderr, "Error: Could not create output directory: %s\n", output_dir);
            exit(1);
        }
    }

    // Discover interfaces
    interface_count = discover_interfaces(current, cfg);
    memcpy(previous, current, sizeof(current));

    printf("Discovered %d network interfaces\n", interface_count);
    printf("Starting comprehensive bridge traffic monitoring...\n");

    const char *mode_str = cfg->json_mode ? "JSON" : (cfg->minimal_mode ? "MINIMAL (rates only)" : "EXTENDED (full details)");
    printf("Mode: %s\n", mode_str);
    printf("Refresh Rate: %d ms (%d Hz)\n", cfg->refresh_rate_ms, 1000 / cfg->refresh_rate_ms);
    printf("Output: %s\n\n", cfg->output_path);

    clock_gettime(CLOCK_MONOTONIC, &prev_time);

    // Initial read
    for (int i = 0; i < interface_count; i++) {
        read_interface_stats(&previous[i]);
    }

    while (1) {
        usleep(cfg->refresh_rate_ms * 1000); // Convert ms to microseconds

        clock_gettime(CLOCK_MONOTONIC, &current_time);
        time_diff = (current_time.tv_sec - prev_time.tv_sec) + 
                   (current_time.tv_nsec - prev_time.tv_nsec) / 1000000000.0;

        // Get real wall-clock time for human-readable timestamp
        struct timespec realtime;
        clock_gettime(CLOCK_REALTIME, &realtime);
        time_t now = realtime.tv_sec;
        struct tm *tm_info = localtime(&now);
        char timestamp_str[32];
        strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d %H:%M:%S", tm_info);

        FILE *output = fopen(cfg->output_path, "w");
        if (!output) {
            sleep(1);
            continue;
        }

        if (cfg->json_mode) {
            // JSON MODE - Machine-readable format
            fprintf(output, "{\n");
            fprintf(output, "  \"channel\": \"%s\",\n", cfg->channel);
            fprintf(output, "  \"monitor_name\": \"Bridge Traffic Monitor\",\n");
            fprintf(output, "  \"timestamp\": \"%s\",\n", timestamp_str);
            fprintf(output, "  \"interval_seconds\": %.3f,\n", time_diff);
            fprintf(output, "  \"interfaces\": [\n");

            int first_interface = 1;
            for (int i = 0; i < interface_count; i++) {
                read_interface_stats(&current[i]);

                unsigned long long rx_bytes_diff = current[i].rx_bytes - previous[i].rx_bytes;
                unsigned long long tx_bytes_diff = current[i].tx_bytes - previous[i].tx_bytes;
                unsigned long long rx_packets_diff = current[i].rx_packets - previous[i].rx_packets;
                unsigned long long tx_packets_diff = current[i].tx_packets - previous[i].tx_packets;

                double rx_rate_bps = (time_diff > 0) ? ((double)rx_bytes_diff * 8.0 / time_diff) : 0;
                double tx_rate_bps = (time_diff > 0) ? ((double)tx_bytes_diff * 8.0 / time_diff) : 0;
                double rx_pps = (time_diff > 0) ? ((double)rx_packets_diff / time_diff) : 0;
                double tx_pps = (time_diff > 0) ? ((double)tx_packets_diff / time_diff) : 0;

                int is_up = is_interface_up(current[i].name);
                int is_required = is_required_interface(current[i].name, cfg);

                // Always include required interfaces, or interfaces with activity
                if (!first_run && (is_required || rx_bytes_diff > 0 || tx_bytes_diff > 0 || is_up)) {
                    if (!first_interface) fprintf(output, ",\n");
                    first_interface = 0;

                    fprintf(output, "    {\n");
                    fprintf(output, "      \"name\": \"%s\",\n", current[i].name);
                    fprintf(output, "      \"state\": \"%s\",\n", is_up ? "up" : "down");
                    fprintf(output, "      \"rx\": {\n");
                    fprintf(output, "        \"bytes_total\": %llu,\n", current[i].rx_bytes);
                    fprintf(output, "        \"packets_total\": %llu,\n", current[i].rx_packets);
                    fprintf(output, "        \"bps\": %.2f,\n", rx_rate_bps);
                    fprintf(output, "        \"packets_per_sec\": %.2f\n", rx_pps);
                    fprintf(output, "      },\n");
                    fprintf(output, "      \"tx\": {\n");
                    fprintf(output, "        \"bytes_total\": %llu,\n", current[i].tx_bytes);
                    fprintf(output, "        \"packets_total\": %llu,\n", current[i].tx_packets);
                    fprintf(output, "        \"bps\": %.2f,\n", tx_rate_bps);
                    fprintf(output, "        \"packets_per_sec\": %.2f\n", tx_pps);
                    fprintf(output, "      }\n");
                    fprintf(output, "    }");
                }

                previous[i] = current[i];
            }

            fprintf(output, "\n  ]\n");
            fprintf(output, "}\n");

        } else if (cfg->minimal_mode) {
            // MINIMAL MODE - Only rates
            fprintf(output, "REAL-TIME NETWORK RATES\n");
            fprintf(output, "═══════════════════════════════════════════════════════════════\n");
            fprintf(output, "Timestamp: %s | Interval: %.3fs\n\n", 
                    timestamp_str, time_diff);
        } else {
            // EXTENDED MODE - Full details
            fprintf(output, "╔═══════════════════════════════════════════════════════════════╗\n");
            fprintf(output, "║       COMPREHENSIVE BRIDGE TRAFFIC MONITOR                    ║\n");
            fprintf(output, "╚═══════════════════════════════════════════════════════════════╝\n\n");
            fprintf(output, "Timestamp: %s\n", timestamp_str);
            fprintf(output, "Sample Interval: %.3f seconds\n", time_diff);
            fprintf(output, "Interfaces Monitored: %d\n\n", interface_count);

            // Read iptables counters (only in extended mode)
            unsigned long long iptables_input = read_iptables_counters("INPUT", "rx");
            unsigned long long iptables_output = read_iptables_counters("OUTPUT", "tx");
            unsigned long long iptables_forward = read_iptables_counters("FORWARD", "forward");

            fprintf(output, "═══════════════════════════════════════════════════════════════\n");
            fprintf(output, "IPTABLES COUNTERS (Total Traffic)\n");
            fprintf(output, "═══════════════════════════════════════════════════════════════\n");
            fprintf(output, "  INPUT Chain:   %llu bytes\n", iptables_input);
            fprintf(output, "  OUTPUT Chain:  %llu bytes\n", iptables_output);
            fprintf(output, "  FORWARD Chain: %llu bytes (← BRIDGED TRAFFIC)\n\n", iptables_forward);
            fprintf(output, "═══════════════════════════════════════════════════════════════\n");
            fprintf(output, "INTERFACE STATISTICS\n");
            fprintf(output, "═══════════════════════════════════════════════════════════════\n\n");
        }

        unsigned long long total_rx = 0, total_tx = 0;

        // Only process interface details for non-JSON modes
        if (!cfg->json_mode) {

        for (int i = 0; i < interface_count; i++) {
            read_interface_stats(&current[i]);

            unsigned long long rx_bytes_diff = current[i].rx_bytes - previous[i].rx_bytes;
            unsigned long long tx_bytes_diff = current[i].tx_bytes - previous[i].tx_bytes;
            unsigned long long rx_packets_diff = current[i].rx_packets - previous[i].rx_packets;
            unsigned long long tx_packets_diff = current[i].tx_packets - previous[i].tx_packets;

            double rx_rate_bps = (time_diff > 0) ? ((double)rx_bytes_diff * 8.0 / time_diff) : 0;
            double tx_rate_bps = (time_diff > 0) ? ((double)tx_bytes_diff * 8.0 / time_diff) : 0;
            double rx_pps = (time_diff > 0) ? ((double)rx_packets_diff / time_diff) : 0;
            double tx_pps = (time_diff > 0) ? ((double)tx_packets_diff / time_diff) : 0;

            int is_up = is_interface_up(current[i].name);
            int is_required = is_required_interface(current[i].name, cfg);

            // Always show required interfaces, or interfaces with activity or that are up
            if (is_required || rx_bytes_diff > 0 || tx_bytes_diff > 0 || is_up) {

                if (cfg->minimal_mode) {
                    // MINIMAL MODE - Always show required interfaces, show others only with activity
                    if (!first_run && (is_required || rx_bytes_diff > 0 || tx_bytes_diff > 0)) {
                        fprintf(output, "[%s]%s\n", current[i].name, is_up ? " UP" : " DOWN");
                        fprintf(output, "  RX: %.2f Mbps (%.2f Kbps | %.0f bps) | %.0f pps\n", 
                                rx_rate_bps / (1024 * 1024), rx_rate_bps / 1024, rx_rate_bps, rx_pps);
                        fprintf(output, "  TX: %.2f Mbps (%.2f Kbps | %.0f bps) | %.0f pps\n\n", 
                                tx_rate_bps / (1024 * 1024), tx_rate_bps / 1024, tx_rate_bps, tx_pps);
                    }
                } else {
                    // EXTENDED MODE - Full details
                    fprintf(output, "┌─ Interface: %s %s\n", current[i].name, is_up ? "[UP]" : "[DOWN]");
                    fprintf(output, "│  Cumulative:\n");
                    fprintf(output, "│    RX: %llu bytes (%llu packets)\n", current[i].rx_bytes, current[i].rx_packets);
                    fprintf(output, "│    TX: %llu bytes (%llu packets)\n", current[i].tx_bytes, current[i].tx_packets);

                    if (!first_run && (rx_bytes_diff > 0 || tx_bytes_diff > 0)) {
                        fprintf(output, "│  Real-Time Rates:\n");
                        fprintf(output, "│    RX: %.2f bps | %.2f Kbps | %.2f Mbps (%.2f pps)\n", 
                                rx_rate_bps, rx_rate_bps / 1024, rx_rate_bps / (1024 * 1024), rx_pps);
                        fprintf(output, "│    TX: %.2f bps | %.2f Kbps | %.2f Mbps (%.2f pps)\n", 
                                tx_rate_bps, tx_rate_bps / 1024, tx_rate_bps / (1024 * 1024), tx_pps);
                        fprintf(output, "│    Activity: %llu bytes/s RX, %llu bytes/s TX\n", 
                                (unsigned long long)(rx_bytes_diff / time_diff),
                                (unsigned long long)(tx_bytes_diff / time_diff));
                    }

                    if (current[i].rx_errors > 0 || current[i].tx_errors > 0 || 
                        current[i].rx_dropped > 0 || current[i].tx_dropped > 0) {
                        fprintf(output, "│  Errors: RX=%llu TX=%llu, Dropped: RX=%llu TX=%llu\n",
                                current[i].rx_errors, current[i].tx_errors,
                                current[i].rx_dropped, current[i].tx_dropped);
                    }
                    fprintf(output, "└─\n\n");
                }

                total_rx += current[i].rx_bytes;
                total_tx += current[i].tx_bytes;
            }

            // Update previous values
            previous[i] = current[i];
        }

        } // end if (!cfg->json_mode)

        if (!cfg->minimal_mode && !cfg->json_mode) {
            // Extended mode summary
            fprintf(output, "═══════════════════════════════════════════════════════════════\n");
            fprintf(output, "SUMMARY\n");
            fprintf(output, "═══════════════════════════════════════════════════════════════\n");
            fprintf(output, "Total Interface RX: %llu bytes\n", total_rx);
            fprintf(output, "Total Interface TX: %llu bytes\n", total_tx);
            fprintf(output, "\nNote: For bridged traffic, check FORWARD chain counters above.\n");
            fprintf(output, "      Interface stats may not reflect forwarded packets due to\n");
            fprintf(output, "      hardware acceleration (IPA) or bridge optimization.\n");

            fprintf(output, "\nData Sources:\n");
            fprintf(output, "  • Interface stats: /sys/class/net/*/statistics/\n");
            fprintf(output, "  • iptables counters: iptables -L -v -n -x\n");
            fprintf(output, "  • Update Rate: 10 Hz (100ms intervals)\n");
        }

        fclose(output);

        // Send to websocat if enabled
        if (cfg->websocat_enabled) {
            send_file_to_websocat(cfg->output_path, cfg->websocat_url, cfg->json_mode);
        }

        prev_time = current_time;
        first_run = 0;
    }
}

int main(int argc, char *argv[]) {
    struct config cfg;
    int config_overridden = 0;

    // Load configuration with priority: defaults -> file -> UCI
    load_config(&cfg);

    // Parse command line arguments (highest priority - override everything)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--minimal") == 0) {
            cfg.minimal_mode = 1;
            config_overridden = 1;
        } else if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--json") == 0) {
            cfg.json_mode = 1;
            config_overridden = 1;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                snprintf(cfg.output_path, sizeof(cfg.output_path), "%s", argv[++i]);
                config_overridden = 1;
            } else {
                fprintf(stderr, "Error: --output requires a path argument\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--refresh") == 0) {
            if (i + 1 < argc) {
                int rate = atoi(argv[++i]);
                if (rate > 0 && rate <= 10000) {
                    cfg.refresh_rate_ms = rate;
                    config_overridden = 1;
                } else {
                    fprintf(stderr, "Error: refresh rate must be between 1 and 10000 ms\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: --refresh requires a milliseconds argument\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--websocat") == 0) {
            if (i + 1 < argc) {
                cfg.websocat_enabled = 1;
                snprintf(cfg.websocat_url, sizeof(cfg.websocat_url), "%s", argv[++i]);
                config_overridden = 1;
            } else {
                fprintf(stderr, "Error: --websocat requires a URL argument (host:port or ws://host:port)\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--channel") == 0) {
            if (i + 1 < argc) {
                snprintf(cfg.channel, sizeof(cfg.channel), "%s", argv[++i]);
                config_overridden = 1;
            } else {
                fprintf(stderr, "Error: --channel requires a channel name argument\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            goto show_help;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    // Update config source if overridden by command line
    if (config_overridden) {
        char temp[64];
        snprintf(temp, sizeof(temp), "%s", cfg.config_source);
        snprintf(cfg.config_source, sizeof(cfg.config_source), "%s + CLI args", temp);
    }

    // Write PID file
    if (write_pid_file(PID_FILE) != 0) {
        fprintf(stderr, "Warning: Could not write PID file\n");
    }

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  BRIDGE TRAFFIC MONITOR - Multi-Source Network Statistics\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    printf("This monitor combines:\n");
    printf("  1. Interface statistics (/sys/class/net/)\n");
    printf("  2. iptables packet counters (FORWARD chain for bridge)\n");
    printf("  3. All discovered network interfaces\n\n");
    printf("Configuration:\n");
    printf("  Source:         %s\n", cfg.config_source);
    printf("  Output file:    %s\n", cfg.output_path);
    printf("  PID file:       %s\n", PID_FILE);
    printf("  Mode:           %s\n", cfg.json_mode ? "JSON" : (cfg.minimal_mode ? "MINIMAL" : "EXTENDED"));
    printf("  WebSocket:      %s\n", cfg.websocat_enabled ? cfg.websocat_url : "Disabled");
    printf("  Channel:        %s\n", cfg.channel);
    printf("\n");

    monitor_traffic(&cfg);

    // Cleanup (only reached on exit)
    remove_pid_file(PID_FILE);

    return 0;

show_help:
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  BRIDGE TRAFFIC MONITOR - Multi-Source Network Statistics\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    printf("This monitor combines:\n");
    printf("  1. Interface statistics (/sys/class/net/)\n");
    printf("  2. iptables packet counters (FORWARD chain for bridge)\n");
    printf("  3. All discovered network interfaces\n\n");

    printf("Configuration Priority (highest to lowest):\n");
    printf("  1. Command line arguments (override all)\n");
    printf("  2. UCI configuration (%s.%s)\n", UCI_PACKAGE, UCI_SECTION);
    printf("  3. Config file (%s)\n", CONFIG_FILE);
    printf("  4. Built-in defaults\n\n");

    printf("UCI Configuration:\n");
    printf("  uci set %s.%s=bridge_monitor\n", UCI_PACKAGE, UCI_SECTION);
    printf("  uci set %s.%s.output_path='/path/to/output'\n", UCI_PACKAGE, UCI_SECTION);
    printf("  uci set %s.%s.minimal_mode='yes'\n", UCI_PACKAGE, UCI_SECTION);
    printf("  uci set %s.%s.json_mode='yes'\n", UCI_PACKAGE, UCI_SECTION);
    printf("  uci set %s.%s.refresh_rate_ms='100'\n", UCI_PACKAGE, UCI_SECTION);
    printf("  uci set %s.%s.websocat_enabled='yes'\n", UCI_PACKAGE, UCI_SECTION);
    printf("  uci set %s.%s.websocat_url='ws://192.168.1.100:8838'\n", UCI_PACKAGE, UCI_SECTION);
    printf("  uci set %s.%s.required_interfaces='rmnet_data0,eth0,br-lan'\n", UCI_PACKAGE, UCI_SECTION);
    printf("  uci commit %s\n\n", UCI_PACKAGE);

    printf("  uci set %s.%s.channel='network-monitor'\n", UCI_PACKAGE, UCI_SECTION);

    printf("Config File Format: %s\n", CONFIG_FILE);
    printf("  output_path=/path/to/output              (default: %s/bridge_traffic_monitor)\n", DEFAULT_OUTPUT_DIR);
    printf("  minimal_mode=yes|no                      (default: no)\n");
    printf("  json_mode=yes|no                         (default: no)\n");
    printf("  refresh_rate_ms=100                      (default: %d, range: 1-10000)\n", DEFAULT_REFRESH_RATE_MS);
    printf("  websocat_enabled=yes|no                  (default: no)\n");
    printf("  websocat_url=ws://host:port              (default: ws://localhost:8838)\n");
    printf("  channel=network-monitor                   (default: network-monitor)\n");
    printf("  required_interfaces=eth0,br-lan,wlan0    (comma-separated list)\n\n");

    printf("Default Files:\n");
    printf("  Output:  %s/bridge_traffic_monitor\n", DEFAULT_OUTPUT_DIR);
    printf("  PID:     %s\n\n", PID_FILE);

    printf("Usage:\n");
    printf("  Extended mode:   ./bridge_traffic_monitor\n");
    printf("  Minimal mode:    ./bridge_traffic_monitor -m (or --minimal)\n");
    printf("  JSON mode:       ./bridge_traffic_monitor -j (or --json)\n");
    printf("  Custom output:   ./bridge_traffic_monitor -o /path/to/output\n");
    printf("  Custom refresh:  ./bridge_traffic_monitor -r 500 (500ms refresh)\n");
    printf("  WebSocket mode:  ./bridge_traffic_monitor -w ws://192.168.1.100:8838\n");
    printf("  WebSocket mode:  ./bridge_traffic_monitor -w localhost:8838 (ws:// is optional)\n");
    printf("  Combined:        ./bridge_traffic_monitor -j -r 1000 -w 192.168.1.100:9001\n\n");

    printf("View Output:\n");
    printf("  cat %s/bridge_traffic_monitor\n", DEFAULT_OUTPUT_DIR);
    printf("  watch -n 0.5 cat %s/bridge_traffic_monitor\n", DEFAULT_OUTPUT_DIR);
    printf("  cat /path/to/output.json | jq .\n\n");

    printf("Note: Command line arguments override config file settings.\n\n");

    return 0;
}
