# Distributed Systems Project

## 📦 Overview

This project implements a **distributed web service infrastructure** designed to scale in response to client load. It currently features a fixed architecture with:

* 🛍️ **1 Load Balancer**
* 🔁 **2 Reverse Proxies**
* 🖥️ **3 Backend Servers per Reverse Proxy** (total of 6 servers)

The system is modular and built to be extended. It lays the groundwork for **horizontal scalability**, **inter-load-balancer communication**, and **load-aware routing**.

---

## 🧹 System Architecture

```
                    +---------------------+
                    |     Client(s)       |
                    +----------+----------+
                               |
                        Unix Domain Socket
                               |
                      +--------v--------+
                      |  Load Balancer  |
                      +--------+--------+
                               |
               +---------------+---------------+
               |                               |
      +--------v--------+             +--------v--------+
      | Reverse Proxy 0 |             | Reverse Proxy 1 |
      +--------+--------+             +--------+--------+
               |                               |
     +---------+---------+         +-----------+-----------+
     |         |         |         |           |           |
+----v--+  +---v---+  +--v---+  +---v---+   +---v---+   +---v---+
|Server0|  |Server1|  |Server2|  |Server3|   |Server4|   |Server5|
+-------+  +-------+  +-------+  +-------+   +-------+   +-------+
```

Each component runs as its own Unix process and communicates via **UNIX domain sockets** or **pipes**.

---

## 🚀 Current Features

### 🔄 Load Balancer

* Acts as the **entry point** for all client requests.
* Forwards requests to one of the two reverse proxies in a round-robin or fixed fashion.
* Handles IPC setup using Unix domain sockets.

### 🔁 Reverse Proxies

* Receive requests from the load balancer.
* Forward them to one of their three assigned backend servers.
* Also manage responses back to the load balancer.

### 🖥️ Backend Servers

* Process application-level logic (simulated for now).
* Return responses back to the proxy.

---

## 🧱 Technologies Used

* **C (GCC)** – System-level programming.
* **UNIX Domain Sockets & Pipes** – For IPC.
* **POSIX Process Management** – `fork`, `exec`, `kill`, etc.
* **Modular Architecture** – Easy to extend and scale.

---

## 🛠️ How to Build

Make sure you have a Unix-like system with GCC installed. Then:

```bash
make all
```

Or manually compile with:

```bash
gcc -o load_balancer load_balancer.c
gcc -o reverse_proxy reverse_proxy.c
gcc -o server server.c
gcc -o watchdog watchdog.c
```

---

## ▶️ How to Run

```bash
./watchdog
```

The watchdog will spawn:

* 1 load balancer
* 2 reverse proxies
* 6 servers (3 per proxy)

Each process communicates via predefined socket paths or file descriptors.

---

## 📈 Planned Features

### 🔮 Dynamic Horizontal Scaling

* Monitor **real-time load metrics** on reverse proxies and servers.
* Dynamically **add/remove servers** per reverse proxy based on CPU/connection load.
* Enable autoscaling logic controlled by a central orchestrator.

### 🔗 Multi Load Balancer Support

* Add **multiple load balancers** for high availability.
* Implement **load balancer communication** to exchange metrics and distribute traffic collaboratively.

### 🧠 Smarter Routing Logic

* Replace round-robin with **load-aware scheduling**.
* Implement **failover strategies** in case of proxy or server failure.

---

## 📁 Directory Structure

```
.
├── load_balancer.c
├── reverse_proxy.c
├── server.c
├── watchdog.c
├── Makefile
└── README.md
```

---

## 📌 Notes

* All paths and instance counts are currently **hardcoded** for testing simplicity.
* Designed for experimentation and learning; ideal for extending into a full microservices simulation environment.

---

## 📞 Contact / Contribution

Feel free to fork, extend, and experiment with this project. If you'd like to contribute or report issues, open a pull request or contact me directly.

