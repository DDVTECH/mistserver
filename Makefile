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
	mkdir /tmp/cores
	chmod 777 /tmp/cores
	echo "/tmp/cores/%e.%s.%p" > /proc/sys/kernel/core_pattern
	service xinetd stop
	cp -f ./Connector_HTTP/Connector_HTTP /usr/bin/
	cd Connector_RTMP; $(MAKE) install
	cp -f ./Connector_RAW/Connector_RAW /usr/bin/
	#cp -f ./Connector_RTSP/Connector_RTSP /usr/bin/
	cp -f ./Buffer/Buffer /usr/bin/
	cp -f ./PLS /etc/xinetd.d/
	service xinetd start
