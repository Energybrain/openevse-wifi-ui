# OpenEVSE-WiFi-esp8266-ui
A simple HTML only user interface for the OpenEVSE project based on the ESP8266 and RAPI commands.
This repository contains the source for both the OpenEVSE controller and ESP8266 boards.  This is based off of the development version 4.3.2 from lincomatic/open_evse code (only included the modified files here, you will need download the rest from lincomatic's repository) and the master version of chris1howell/OpenEVSE_RAPI_WIFI_ESP8266.
Used development version because it has a nice feature for solar charging and control i.e. the pilot adjust has the option of not saving to EEPROM.
Added a command to query the delay timer settings via RAPI in development version 4.3.2.  All UI code is done on the ESP8266 side.
Version is left as is with a 'b' attached to it indicating my modifications.
Update: OpenEVSE Version with a 'c' suffix - fixes charge and time limits to work properly when using start delay timer.  Also included the use case where a delay timer is set and charges from the grid (TOU), stops when a limit is reached, and without unplugging from the car, charging can be restarted for solar power charging the next day.  As a consequence to having this use case, the kWh recording will not work properly due to no state A transitions so this include a fix that moves the kWh recording to be independant of state A transitions and only in chargingOff and chargingOn functions.
Update: OpenEVSE-WiFi-esp8266-ui - fixed time limit drop down menu bug and changed the choices to something more usable - every 30 minutes increments.  Also, will display custom allowed limits if set via the RAPI commands manually.
