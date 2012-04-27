default: client
.PHONY: client client-debug client-clean clean release-install debug-install docs

prepare:
	mkdir -p ./bin
	cp -f *.html ./bin/
client-debug: prepare
	cd Connector_HTTP; $(MAKE)
	cd Connector_RTMP; $(MAKE)
	cd Connector_RAW; $(MAKE)
	cd Buffer; $(MAKE)
	cd Controller; $(MAKE)
client: client-debug
client-clean:
	cd Connector_HTTP; $(MAKE) clean
	cd Connector_RTMP; $(MAKE) clean
	cd Connector_RAW; $(MAKE) clean
	cd Buffer; $(MAKE) clean
	cd Controller; $(MAKE) clean
clean: client-clean
	rm -rf ./bin
client-release: prepare
	cd Connector_HTTP; $(MAKE) DEBUG=0 OPTIMIZE=-O2
	cd Connector_RTMP; $(MAKE) DEBUG=0 OPTIMIZE=-O2
	cd Connector_RAW; $(MAKE) DEBUG=0 OPTIMIZE=-O2
	cd Buffer; $(MAKE) DEBUG=0 OPTIMIZE=-O2
	cd Controller; $(MAKE) DEBUG=0 OPTIMIZE=-O2
release: client-release
release-install: client-clean client-release
	cd Connector_RTMP; $(MAKE) install
	cd Connector_HTTP; $(MAKE) install
	cd Connector_RAW; $(MAKE) install
	cd Buffer; $(MAKE) install
	cd Controller; $(MAKE) install
debug-install: client-clean client-debug
	cd Connector_RTMP; $(MAKE) install
	cd Connector_HTTP; $(MAKE) install
	cd Connector_RAW; $(MAKE) install
	cd Buffer; $(MAKE) install
	cd Controller; $(MAKE) install
docs:
	doxygen ./Doxyfile > /dev/null

