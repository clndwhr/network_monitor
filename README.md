# Network Monitoring Daemon

This project provides a simple userspace daemon (`netmon`) and a kernel module (`netmon_proc.ko`) for monitoring network interface traffic on Linux systems (including OpenWrt). The daemon can output JSON for use with websocat or other tools, and the kernel module exposes stats via `/proc/netmon`.

---

## Build Instructions

1. **Install build tools**
   Make sure you have `gcc`, `make`, and kernel headers installed.

   On Linux Based:

   ```sh
   gcc -o bridge_traffic_monitor bridge_traffic_monitor.c
   ```

2. **Clone or copy the project files**
   Place all files (`bridge_traffic_monitor.c`, `bridge_traffic_monitor.c`) in the same directory.

3. **Build both userspace and kernel module**

   ```sh
   make
   ```

   This will produce:
   - `bridge_traffic_monitor` (userspace daemon)

---

## Install and Use

### Userspace Daemon (`bridge_traffic_monitor`)

- **Monitor all interfaces with minimum required interfaces to report based on uci or config file:**

  ```sh
  ./bridge_traffic_monitor
  ```

## Notes

- The userspace daemon can be run as a background service or integrated with other monitoring tools.

---

## License

This project is licensed under the MIT or GPL license (choose as appropriate for your codebase).
