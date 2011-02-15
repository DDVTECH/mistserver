default: client-install

client:
	cd Connector_HTTP; $(MAKE)
	cd Connector_RTMP; $(MAKE)
	cd Connector_RAW; $(MAKE)
	#cd Connector_RTSP; $(MAKE)
	cd Buffer; $(MAKE)
client-clean:
	cd Connector_HTTP; $(MAKE) clean
	cd Connector_RTMP; $(MAKE) clean
	cd Connector_RAW; $(MAKE) clean
	#cd Connector_RTSP; $(MAKE) clean
	cd Buffer; $(MAKE) clean
clean: client-clean
client-install: client-clean client
	mkdir -p /tmp/cores
	chmod 777 /tmp/cores
	echo "/tmp/cores/%e.%s.%p" > /proc/sys/kernel/core_pattern
	service xinetd stop
	cd Connector_RTMP; $(MAKE) install
	cd Connector_HTTP; $(MAKE) install
	cd Connector_RAW; $(MAKE) install
	#cp -f ./Connector_RTSP/Connector_RTSP /usr/bin/
	cp -f ./Buffer/Buffer /usr/bin/
	rn -rf /etc/xinetd.d/PLS
	service xinetd start

