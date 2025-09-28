# Network Monitoring Daemon and Kernel Module

This project provides a simple userspace daemon (`netmon`) and a kernel module (`netmon_proc.ko`) for monitoring network interface traffic on Linux systems (including OpenWrt). The daemon can output JSON for use with websocat or other tools, and the kernel module exposes stats via `/proc/netmon`.

---

## Build Instructions

1. **Install build tools**
   Make sure you have `gcc`, `make`, and kernel headers installed.

   On Debian/Ubuntu:

   ```sh
   sudo apt-get install build-essential linux-headers-$(uname -r)
   ```

2. **Clone or copy the project files**
   Place all files (`Makefile`, `netmon.c`, `netmon_proc.c`) in the same directory.

3. **Build both userspace and kernel module**

   ```sh
   make
   ```

   This will produce:
   - `netmon` (userspace daemon)
   - `netmon_proc.ko` (kernel module)

---

## Install and Use

### Userspace Daemon (`netmon`)

- **Monitor all interfaces:**

  ```sh
  ./netmon all
  ```

- **Monitor a specific interface:**

  ```sh
  ./netmon eth0
  ```

- **Monitor interface from config file:**
  1. Create `/etc/netmon.conf` with the interface name (e.g., `rmnet_data0`).
  2. Run:

     ```sh
     ./netmon
     ```

- **Monitor interface from UCI (OpenWrt):**

  ```sh
  uci set netmon.interface='wwan0'
  uci commit netmon
  ./netmon
  ```

- **Pipe JSON output to websocat:**

  ```sh
  ./netmon eth0 | websocat ws://localhost:9001
  ```

### Kernel Module (`netmon_proc.ko`)

- **Insert the module for a specific interface:**

  ```sh
  sudo insmod netmon_proc.ko iface=eth0
  ```

- **Insert the module for all interfaces:**

  ```sh
  sudo insmod netmon_proc.ko
  ```

- **View stats:**

  ```sh
  cat /proc/netmon
  ```

- **Remove the module:**

  ```sh
  sudo rmmod netmon_proc
  ```

---

## Clean Up

To remove build artifacts:

```sh
make clean
```

---

## Notes

- The userspace daemon can be run as a background service or integrated with other monitoring tools.
- The kernel module must be loaded with appropriate permissions.
- Only one instance of the kernel module can be loaded at a time.
- For OpenWrt, you may need to adjust build settings for cross-compilation.

---

## License

This project is licensed under the MIT or GPL license (choose as appropriate for your codebase).
