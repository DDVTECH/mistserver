default: client
.PHONY: client client-debug client-clean clean release-install debug-install docs

client-debug:
	cd Connector_HTTP; $(MAKE)
	cd Connector_RTMP; $(MAKE)
	cd Connector_RAW; $(MAKE)
	cd Buffer; $(MAKE)
	cd DDV_Controller; $(MAKE)
client: client-debug
client-clean:
	cd Connector_HTTP; $(MAKE) clean
	cd Connector_RTMP; $(MAKE) clean
	cd Connector_RAW; $(MAKE) clean
	cd Buffer; $(MAKE) clean
	cd DDV_Controller; $(MAKE) clean
clean: client-clean
client-release:
	cd Connector_HTTP; $(MAKE) DEBUG=0 OPTIMIZE=-O2
	cd Connector_RTMP; $(MAKE) DEBUG=0 OPTIMIZE=-O2
	cd Connector_RAW; $(MAKE) DEBUG=0 OPTIMIZE=-O2
	cd Buffer; $(MAKE) DEBUG=0 OPTIMIZE=-O2
	cd DDV_Controller; $(MAKE) DEBUG=0 OPTIMIZE=-O2
release: client-release
release-install: client-clean client-release
	cd Connector_RTMP; $(MAKE) install
	cd Connector_HTTP; $(MAKE) install
	cd Connector_RAW; $(MAKE) install
	cd Buffer; $(MAKE) install
	cd DDV_Controller; $(MAKE) install
debug-install: client-clean client-debug
	cd Connector_RTMP; $(MAKE) install
	cd Connector_HTTP; $(MAKE) install
	cd Connector_RAW; $(MAKE) install
	cd Buffer; $(MAKE) install
	cd DDV_Controller; $(MAKE) install
docs:
	doxygen ./Doxyfile > /dev/null

