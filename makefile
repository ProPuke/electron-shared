CC      = gcc
CFLAGS  = -O2 -fdata-sections -ffunction-sections
LDFLAGS = -lm -lcurl -lpthread -ldl `pkg-config gtk+-3.0 --libs` -s -Wl,--gc-sections
OBJ_DIR = obj

.PHONY: all
all: electron-shared

.PHONY: test
test: electron-shared
	./electron-shared test/electron-quick-start

electron-shared: obj/main.o obj/jsmn.o obj/semver.o obj/zip.o source/lib/libui/build/out/libui.a
	$(CC) obj/*.o source/lib/libui/build/out/libui.a $(LDFLAGS) -o electron-shared

${OBJ_DIR}:
	mkdir -p ${OBJ_DIR}

obj/main.o: source/main.c | ${OBJ_DIR}
	$(CC) $(CFLAGS) -o obj/main.o -c source/main.c

obj/jsmn.o: source/lib/jsmn/jsmn.c source/lib/jsmn/jsmn.h | ${OBJ_DIR}
	$(CC) $(CFLAGS) -o obj/jsmn.o -c source/lib/jsmn/jsmn.c

obj/semver.o: source/lib/semver.c/semver.c source/lib/semver.c/semver.h | ${OBJ_DIR}
	$(CC) $(CFLAGS) -o obj/semver.o -c source/lib/semver.c/semver.c

obj/zip.o: source/lib/zip/src/zip.c source/lib/zip/src/zip.h source/lib/zip/src/miniz.h | ${OBJ_DIR}
	$(CC) $(CFLAGS) -o obj/zip.o -c source/lib/zip/src/zip.c

source/lib/libui/build/out/libui.a:
	mkdir -p source/lib/libui/build
	cd source/lib/libui/build; cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF; make

.PHONY: clean
clean:
	if [ -d "source/lib/libui/build" ]; then rm -rf source/lib/libui/build; fi
	rm -rf $(OBJ_DIR)
	rm -f electron-shared
