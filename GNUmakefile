CLIENT_DIR=client
SERVER_DIR=server
BUILD_DIR=build
TIMEOUT=10
VALGRIND=valgrind --leak-check=yes --error-exitcode=1
GDB=gdb --args
CC=gcc
# Nowadays, `pthread` is not just a library, so we can't just pass `-lpthread`
# but we need to use the `-pthread` option.
CFLAGS=-pthread -g -std=c99 -Wall -pedantic
LDFLAGS=-pthread

CLT_O = main.o client_thread.o
SRV_O = main.o server_thread.o

# <port-nb> 
CLIENT_ARGS=2017 5

.PHONY: default all clean format client server test run-server-valgrind run-client-valgrind

default: all

all: $(BUILD_DIR)/tp2_client $(BUILD_DIR)/tp2_server rapport.pdf

$(BUILD_DIR)/tp2_client: $(patsubst %.o,$(BUILD_DIR)/client/%.o, $(CLT_O))
	$(CC) $(LDFLAGS) -o $@ $(patsubst %.o,$(BUILD_DIR)/client/%.o, $(CLT_O))

$(BUILD_DIR)/tp2_server: $(patsubst %.o,$(BUILD_DIR)/server/%.o, $(SRV_O))
	$(CC) $(LDFLAGS) -o $@ $(patsubst %.o,$(BUILD_DIR)/server/%.o, $(SRV_O))

$(BUILD_DIR)/%.o: %.c
	@[ -d "$$(dirname "$@")" ] || mkdir -p "$$(dirname "$@")"
	$(CC) $(CFLAGS) -c -o $@ $<



# Lancer le client et le serveur, utilisant le port 2017 pour communiquer.
# Ici on utilise par défaut 3 threads du côté du serveur et 5 du côté
# du client.  Chaque client envoie 50 requêtes.  Il a 5 types de resources
# à gérer avec quantités respectivement 10, 4, 23, 1, et 2.
run: all
	$(BUILD_DIR)/tp2_server 2017 5 & 		  \
	$(BUILD_DIR)/tp2_client 2017 5 50   10 4 23 1 2 & \
	wait

run-client-valgrind: all
	$(VALGRIND) $(BUILD_DIR)/tp2_client 2017 5 50 10 4 23 1 2

run-server-valgrind: all
	$(VALGRIND) $(BUILD_DIR)/tp2_server 2017 5

run-client-gdb: all
	$(GDB) $(BUILD_DIR)/tp2_client 2017 5 50 10 4 23 1 2

run-server-gdb: all
	$(GDB) $(BUILD_DIR)/tp2_server 2017 5 50 10 4 23 1 2
	
clean:
	$(RM) -r $(BUILD_DIR) *.aux *.log

%.pdf: %.tex
	pdflatex $<

release:
	tar -czv -f tp2.tar.gz --transform 's|^|tp2/|' \
	    */*.[ch] *.tex *.md GNUmakefile

$(BUILD_DIR)/server/main.o $(BUILD_DIR)/server/server_thread.o: \
    server/server_thread.h
$(BUILD_DIR)/client/main.o $(BUILD_DIR)/client/client_thread.o: \
    client/client_thread.h
