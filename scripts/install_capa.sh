#!/bin/bash
g++ -lmist -Wall -o capa_service capa_service.cpp
if [ $? -eq 0 ]; then
bestand=/etc/systemd/system/capa_service.service
echo "[Unit]" > $bestand
echo "Description=capa_service" >> $bestand
echo "After=networkmanager.service" >> $bestand
echo "" >> $bestand
echo "[Service]" >> $bestand
echo "Type=simple" >> $bestand
echo "User=$USER" >> $bestand
echo "ExecStart="`pwd`"/capa_service" `pwd`/log.csv >> $bestand
echo "Restart=always" >> $bestand
echo "RestartSec=5" >> $bestand
echo "" >> $bestand
echo "[Install]" >> $bestand
echo "WantedBy=multi-user.target" >> $bestand

systemctl daemon-reload
fi
