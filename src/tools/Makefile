### get external defined data

### constant symbols

ARCH ?=
CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

LCFLAGS = ${CFLAGS} -I. -DBIGENDIAN 

### linking options

### general build targets

all: rssh_client

clean:
	rm -f rssh_client
	rm -f *.o

install:
ifneq ($(strip $(TARGET_IP)),)
 ifneq ($(strip $(TARGET_DIR)),)
  ifneq ($(strip $(TARGET_USR)),)
	@echo "---- Copying payload tools files to $(TARGET_IP):$(TARGET_DIR)"
	@ssh $(TARGET_USR)@$(TARGET_IP) "mkdir -p $(TARGET_DIR)"
	@scp payload_crc $(TARGET_USR)@$(TARGET_IP):$(TARGET_DIR)
	@scp payload_diff $(TARGET_USR)@$(TARGET_IP):$(TARGET_DIR)
	@scp payload_gen $(TARGET_USR)@$(TARGET_IP):$(TARGET_DIR)
  else
	@echo "ERROR: TARGET_USR is not configured in target.cfg"
  endif
 else
	@echo "ERROR: TARGET_DIR is not configured in target.cfg"
 endif
else
	@echo "ERROR: TARGET_IP is not configured in target.cfg"
endif

### rules

### test programs

payload_crc: payload_crc.o
	$(CC) $(CFLAGS) -o $@ $^

payload_diff: payload_diff.o
	$(CC) $(CFLAGS) -o $@ $^

payload_gen: payload_gen.o
	$(CC) $(CFLAGS) -L../../libtools -o $@ $^ $(LIBS)

rssh_client: rssh_client.c
	$(CC) $(LCFLAGS) $^ -o $@ -lpthread

### EOF
