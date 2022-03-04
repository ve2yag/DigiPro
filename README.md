# DigiPro
 Lora APRS digipeater for Arduino/AVR
 
 Atmega 168/328P(B) using internal 8MHz RC oscillator at 3.3 volts.
 
 - Compatible with ASCII packet format [LoRa-APRS-tracker](https://github.com/lora-aprs/LoRa_APRS_Tracker) and binary/AX25 format [sh123/esp32_loraprs](https://github.com/sh123/esp32_loraprs)
 - Digipeat packet in same format received. Digi beaconing and telemetry use the most heard format.
 - Digipeater support WIDEx-x and SSID digipeating for shortest packet.
 - Support ?APRS? and ?APRSS query
 - Support message ACKing, but don't use messaging for now. (remote config?)
 - Support 18650 battery voltage monitoring and sleep mode under 3.5 volts
 - Additional telemetry using DS18B20 and BMP180 for internal/external temperature and pressure.

Digipeater are extremly efficient, current draw is around 10,5ma on receive and 0,5ma when enter sleep mode. Only Lora module are powered and CPU stay in power down mode (few uA) Wake only when incoming packet is ready inside Lora module, also wake each second to check if it time to transmit beacon and telemetry. When all is tuned, I put some Goop glue on feedpoint connection to waterproof them.

Configure radio and digi with project.h file. 

[See schematic and PCB](Board.pdf)

 ![Board](Board.jpg) ![Digi VA2AIG-4](Digi.png)

Antenna is 3D printed EDZ, 2x 5/8 wave. 8.1dbi and easy to tune. Antenna is made from 36" stainless tig welding rod, 3/32 inch. Mast is 1/2 water CPVC pipe, coax RG-174/U. Coax coil at feed point is current balun to stabilize antenna pattern and choke current flow back to Lora module.

Enclosure is 3D printed, using Cubesat shape. Solar panel are 80mmx80mm 160ma@5v, 3 sides. (3D model in 3D_enclosure and antenna folder)

VA2AIG-4, Digipeater wide coverage (Have a bug with V2.0) [Map](https://fr.aprs.fi/#!call=a%2FVA2AIG-4&timerange=3600&tail=3600)
VE2YAG-4, Test digipeater, V2.1. Testing now at home, see telemetry.
[Telemetry](https://fr.aprs.fi/telemetry/a/VE2YAG-4) or see on [Map](https://fr.aprs.fi/info/a/VE2YAG-4)

I use also [MicroBeacon_2021](https://github.com/ve2yag/MicroBeacon_2021) as small testing tracker(same PCB) and Lorakiss as Kiss-over-IP Lora modem, hooked to APRX on a raspberry pi to create a dual-port igate with Direwolf.

