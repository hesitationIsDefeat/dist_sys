# Compiler and flags
CC = gcc
CFLAGS = -Wall -g

# Targets
TARGETS = watchdog load_balancer reverse_proxy server client

# Default rule: builds everything
all: $(TARGETS)

# Build rules for each file 
watchdog: watchdog.c
	$(CC) $(CFLAGS) -o watchdog watchdog.c

load_balancer: load_balancer.c
	$(CC) $(CFLAGS) -o load_balancer load_balancer.c

reverse_proxy: reverse_proxy.c
	$(CC) $(CFLAGS) -o reverse_proxy reverse_proxy.c

server: server.c
	$(CC) $(CFLAGS) -o server server.c -lm

client: client.c
	$(CC) $(CFLAGS) -o client client.c

# Clean rule
clean:
	rm -f $(TARGETS) *.o /tmp/lb-wd /tmp/cl-lb /tmp/lb-rp /tmp/rp-sv*

# .PHONY ensures these aren't treated as actual files
.PHONY: all clean watchdog load_balancer reverse_proxy server client
