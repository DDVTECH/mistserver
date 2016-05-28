#!/bin/bash
bestand=/etc/systemd/system/mistserver.service
echo "[Unit]" > $bestand
echo "Description=MistServer" >> $bestand
echo "After=networkmanager.service" >> $bestand
echo "" >> $bestand
echo "[Service]" >> $bestand
echo "Type=simple" >> $bestand
echo "User=$USER" >> $bestand
echo "ExecStart=/usr/bin/MistController -nc "`pwd`"/config.json" >> $bestand
echo "Restart=always" >> $bestand
echo "RestartSec=5" >> $bestand
echo "" >> $bestand
echo "[Install]" >> $bestand
echo "WantedBy=multi-user.target" >> $bestand


