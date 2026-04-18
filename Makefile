CC      ?= gcc
CXX     ?= g++
PROTOC  ?= protoc

CFLAGS   ?= -O2 -g
CXXFLAGS ?= -O2 -g
CFLAGS   += -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc $(EXTRA_CFLAGS)
CXXFLAGS += -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc -Iproto -std=c++17 $(EXTRA_CXXFLAGS)
LDFLAGS  += $(EXTRA_LDFLAGS)
PROTO_LIBS ?= -lprotobuf -lpthread

# GLib/GIO flags for the daemon (override GLIB_CFLAGS / GLIB_LIBS to
# skip pkg-config when cross-compiling).
GLIB_CFLAGS ?= $(shell pkg-config --cflags glib-2.0 gio-2.0 2>/dev/null)
GLIB_LIBS   ?= $(shell pkg-config --libs   glib-2.0 gio-2.0 2>/dev/null)

PROTO_SRC := proto/app.proto
PROTO_CC  := proto/app.pb.cc
PROTO_H   := proto/app.pb.h

C_LIB_SRC   := src/ble_pack.c src/ble_reasm.c
C_LIB_OBJ   := $(C_LIB_SRC:.c=.o)

CXX_LIB_SRC := src/app_dispatch.cc $(PROTO_CC)
CXX_LIB_OBJ := src/app_dispatch.o proto/app.pb.o

DAEMON_SRC  := src/main.c src/gatt_server.c src/ota_handler.c src/firmware_version.c src/casync_runner.c
DAEMON_OBJ  := src/main.o src/gatt_server.o src/ota_handler.o src/firmware_version.o src/casync_runner.o
DAEMON      := rauc-ble-otad

TESTS_C   := tests/test_ble_pack tests/test_ble_reasm
TESTS_CXX := tests/test_app_dispatch
TESTS     := $(TESTS_C) $(TESTS_CXX)

all: $(TESTS) $(DAEMON)

# Generate protobuf C++ sources.
$(PROTO_CC) $(PROTO_H): $(PROTO_SRC)
	$(PROTOC) --cpp_out=proto --proto_path=proto $(PROTO_SRC)

# Pure-C library objects.
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# C++ objects (depend on generated protobuf header).
src/app_dispatch.o: src/app_dispatch.cc $(PROTO_H)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

proto/app.pb.o: $(PROTO_CC) $(PROTO_H)
	$(CXX) $(CXXFLAGS) -c -o $@ $(PROTO_CC)

# Daemon objects need GLib/GIO headers.
src/gatt_server.o: src/gatt_server.c src/gatt_server.h src/ota_handler.h src/firmware_version.h src/casync_runner.h
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) -c -o $@ $<

src/ota_handler.o: src/ota_handler.c src/ota_handler.h
	$(CC) $(CFLAGS) -c -o $@ $<

src/firmware_version.o: src/firmware_version.c src/firmware_version.h
	$(CC) $(CFLAGS) -c -o $@ $<

src/casync_runner.o: src/casync_runner.c src/casync_runner.h
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) -c -o $@ $<

src/main.o: src/main.c src/gatt_server.h
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) -c -o $@ $<

# Daemon binary: C lib + C++ dispatch + GLib + protobuf.
$(DAEMON): $(DAEMON_OBJ) $(C_LIB_OBJ) $(CXX_LIB_OBJ) $(PROTO_H)
	$(CXX) $(CXXFLAGS) -o $@ $(DAEMON_OBJ) $(C_LIB_OBJ) $(CXX_LIB_OBJ) $(LDFLAGS) $(GLIB_LIBS) $(PROTO_LIBS)

# Pure-C tests link only C objects.
tests/test_ble_pack: tests/test_ble_pack.c $(C_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/test_ble_reasm: tests/test_ble_reasm.c $(C_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# C++ test links the dispatch + protobuf objects and the C library.
tests/test_app_dispatch: tests/test_app_dispatch.cc $(C_LIB_OBJ) $(CXX_LIB_OBJ) $(PROTO_H)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_app_dispatch.cc $(C_LIB_OBJ) $(CXX_LIB_OBJ) $(LDFLAGS) $(PROTO_LIBS)

check: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

clean:
	rm -f $(C_LIB_OBJ) $(CXX_LIB_OBJ) $(DAEMON_OBJ) $(TESTS) $(DAEMON) $(PROTO_CC) $(PROTO_H)

.PHONY: all check clean
