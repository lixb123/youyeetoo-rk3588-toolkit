CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic

all: youyeetoo_eth_gateway

youyeetoo_eth_gateway: youyeetoo_eth_gateway.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f youyeetoo_eth_gateway
