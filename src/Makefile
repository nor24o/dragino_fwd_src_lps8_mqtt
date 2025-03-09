### Environment constants

ARCH ?= mips
CROSS_COMPILE ?= mips-openwrt-linux-musl-


LDEF = -DCFG_bdate='"$(shell date -u '+%Y-%m-%d %H:%M:%S')"'
export LDEF += -DCFG_version='"$(shell if [ -f VERSION ]; then cat VERSION; else V1.0.1; fi)"'

### general build targets

.PHONY: all clean install sx1302hal sx1301hal fwd_sx1301 fwd_sx1302 libmqtt  prepare tools

all: fwd_sx1302 fwd_sx1301  tools

prepare: prep.sh
	./prep.sh

sx1301hal: 
	$(MAKE) all -e -C $@

sx1302hal: 
	$(MAKE) all -e -C $@

libmqtt: 
	$(MAKE) all -e -C $@

libmbedtls:
	$(MAKE) all -e -C libmbedtls ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)


fwd_sx1301: sx1301hal libmqtt prepare
	$(MAKE) all -e -C build_fwd_sx1301 SX1301MOD=1

fwd_sx1302: sx1302hal libmqtt prepare
	$(MAKE) all -e -C build_fwd_sx1302 SX1302MOD=1


tools:
	$(MAKE) all -e -C tools

clean:
	$(MAKE) clean -e -C sx1301hal
	$(MAKE) clean -e -C sx1302hal
	$(MAKE) clean -e -C libmqtt
	$(MAKE) clean -e -C libmpsse
	$(MAKE) clean -e -C tools
	rm -rf build_*_sx* draginofwd*.deb

### EOF
