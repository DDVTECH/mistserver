default: client-local-install

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
	cp -f ./Connector_HTTP/Connector_HTTP /usr/bin/
	cp -f ./Connector_RTMP/Connector_RTMP /usr/bin/
	cp -f ./Connector_RAW/Connector_RAW /usr/bin/
	#cp -f ./Connector_RTSP/Connector_RTSP /usr/bin/
	cp -f ./Buffer/Buffer /usr/bin/
	cp -f ./PLS /etc/xinetd.d/
	service xinetd restart
client-local-install: client
	mkdir -p ./bin
	cp -f ./Connector_HTTP/Connector_HTTP ./bin/
	cp -f ./Connector_RTMP/Connector_RTMP ./bin/
	cp -f ./Connector_RAW/Connector_RAW ./bin/
	#cp -f ./Connector_RTSP/Connector_RTSP ./bin/
	cp -f ./Buffer/Buffer ./bin/

