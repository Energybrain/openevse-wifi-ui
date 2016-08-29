/*
 * Copyright (c) 2015 Chris Howell
 * August 2016 modified by Bobby Chung to support the following features
 * a. Using the EVSE without a display - complete control via this WiFi a simple html interface
 * b. Networkless operation
 * c. WiFi and server configuration fully customizable by user
 * d. Additional back up server entry
 * e. Hyperlink to external dashboard
 * f. Charge and time limits
 * g. Plug in indicator
 * h. Plug in and charging notifications via Emoncms for home automation systems
 * This file is part of Open EVSE.
 * Open EVSE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * Open EVSE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with Open EVSE; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>
#include <EEPROM.h>

#define VERSION "1.1"

// for EEPROM map
#define EEPROM_SIZE 512
#define SSID_START 0
#define SSID_MAX_LENGTH 32
#define PASS_START 32
#define PASS_MAX_LENGTH 64
#define KEY_START 96
#define KEY_MAX_LENGTH 50
#define KEY2_START 146
#define KEY2_MAX_LENGTH 50
#define NODE_START 196
#define NODE_MAX_LENGTH 1
#define HOST_START 197
#define HOST_MAX_LENGTH 32
#define HOST2_START 229
#define HOST2_MAX_LENGTH 32
#define DIRECTORY_START 261
#define DIRECTORY_MAX_LENGTH 32
#define DIRECTORY2_START 293
#define DIRECTORY2_MAX_LENGTH 32
#define STAT_START 325
#define STAT_MAX_LENGTH 101
#define PNOTIFY_START 426
#define PNOTIFY_MAX_LENGTH 1
#define PHOUR_START 427
#define PHOUR_MAX_LENGTH 2
#define PMIN_START 429
#define PMIN_MAX_LENGTH 2
#define PREPEAT_START 431
#define PREPEAT_MAX_LENGTH 1
#define CNOTIFY_START 432
#define CNOTIFY_MAX_LENGTH 1
#define CHOUR_START 433
#define CHOUR_MAX_LENGTH 2
#define CMIN_START 435
#define CMIN_MAX_LENGTH 2
#define CREPEAT_START 437
#define CREPEAT_MAX_LENGTH 1
#define NOTIFY_FLAGS_START 438
#define NOTIFY_FLAGS_MAX_LENGTH 2
#define REMINDERS_START 440
#define REMINDERS_MAX_LENGTH 3

#define SERVER_UPDATE_RATE 30000 //update emoncms server ever 30 seconds
#define CHARGE_LIMIT_MAX  40     // x2 kWh
#define TIME_LIMIT_MAX  48       // x30 minutes
#define MAX_REMINDERS 8          // 16 max
#define P_NOTIFY_SHIFT 0
#define P_READY_SHIFT 1
#define C_NOTIFY_SHIFT 2
#define C_READY_SHIFT 3
#define P_REMINDERS_SHIFT 0
#define C_REMINDERS_SHIFT 4
#define REMINDERS_MASK 0x0f
#define NOTIFY_MASK 0x01
#define NOTIFICATION_POLLING_RATE 295000  //check the time every 295 seconds
#define PLUG_IN_MASK 0x08

ESP8266WebServer server(80);

//Default SSID and PASSWORD for AP Access Point Mode
const char* ssid = "OpenEVSE";
//const char* ssid = "test";
const char* password = "openevse";
String st = "not_scanned";
String privateKey = "";
String privateKey2 = "";
String node = "0";
String esid = "";
String epass = "";

//SERVER variables for Energy Monotoring and backup 
String host = "";
String host2 =  "";
String directory = "";
String directory2 = "";
String status_path = "";
const char* e_url = "input/post.json?node=";

const char* inputID_AMP  = "OpenEVSE_AMP:";
const char* inputID_VOLT  = "OpenEVSE_VOLT:";
const char* inputID_TEMP1  = "OpenEVSE_TEMP1:";
const char* inputID_TEMP2  = "OpenEVSE_TEMP2:";
const char* inputID_TEMP3  = "OpenEVSE_TEMP3:";
const char* inputID_PILOT  = "OpenEVSE_PILOT:";
const char* inputID_NOTIFY1 = "OpenEVSE_P_NOTIFY:";
const char* inputID_NOTIFY2 = "OpenEVSE_C_NOTIFY:";

// for EVSE basic info
int amp = 0;
int volt = 0;
int temp1 = 0;
int temp2 = 0;
int temp3 = 0;
int pilot = 0;
int month = 0;
int day = 0;
int year = 0;
int hour = 0;
int minutes = 0;
int seconds = 0;
int evse_flag = 0;

// for sending out notifications
int plug_notify = 0;
int plug_hour = 0;
int plug_min= 0;
int plug_repeat = 0;
int charge_notify = 0;
int charge_hour = 0;
int charge_min= 0;
int charge_repeat = 0;
int minutes_charging = 0;
int p_notify_sent = 0;
int c_notify_sent = 0;
int p_ready = 0;
int c_ready = 0;
int p_reminders = 0;
int c_reminders = 0;
int notify_flags;
int reminders;

// determines server state
int server_down = 0;
int server2_down = 0; 

int wifi_mode = 0; 
int buttonState = 0;
int clientTimeout = 0;
unsigned long Timer;
unsigned long Timer5 = millis();
unsigned long start_sent_timer_p = millis();
unsigned long start_sent_timer_c = millis();
int count = 5;

String readEEPROM(int start_byte, int allocated_size) {
  String variable;
  for (int i = start_byte; i < (start_byte + allocated_size); ++i) {
    variable += char(EEPROM.read(i));
  }
  delay(10);
  return variable;
}

void update_and_save_notify_flags() {
  notify_flags = (p_notify_sent << P_NOTIFY_SHIFT) + (p_ready << P_READY_SHIFT) + (c_notify_sent << C_NOTIFY_SHIFT) + (c_ready << C_READY_SHIFT);
  reminders = (p_reminders << P_REMINDERS_SHIFT) + (c_reminders << C_REMINDERS_SHIFT);
  writeEEPROM(NOTIFY_FLAGS_START, NOTIFY_FLAGS_MAX_LENGTH, String(notify_flags));
  writeEEPROM(REMINDERS_START, REMINDERS_MAX_LENGTH, String(reminders));
}

void writeEEPROM(int start_byte, int allocated_size, String contents) {
  int length_of_contents = contents.length();
  if (length_of_contents > allocated_size)
    length_of_contents = allocated_size;
  for (int i = 0; i < length_of_contents; ++i) {
    EEPROM.write(start_byte + i, contents[i]);
  }
  for (int i = (start_byte + length_of_contents); i < (start_byte + allocated_size); ++i)
    EEPROM.write(i,0);
  EEPROM.commit();
  delay(10);
}

void ResetEEPROM(int start_byte, int end_byte) {
  Serial.println("Erasing EEPROM");
  for (int i = start_byte; i < end_byte; ++i) {
   EEPROM.write(i, 0);
    //Serial.print("#"); 
  }
  EEPROM.commit();   
}

void send_notify_data(int p_type, int c_type) {
  String url;     
  String url2;   
  String tmp;  
  String p_url = inputID_NOTIFY1;
  String c_url = inputID_NOTIFY2;
  //Serial.println("inside notify");
  p_url += p_type;
  c_url += c_type;
  tmp = e_url;
  tmp += node; 
  tmp += "&json={"; 
  if (p_type)
    tmp += p_url;
  else if (c_type)
    tmp += c_url;  
  tmp += "}&";     
  url = directory.c_str();   //needs to be constant character to filter out control characters padding when read from memory
  url += tmp;
  url2 = directory2.c_str();    //needs to be constant character to filter out control characters padding when read from memory
  url2 += tmp;
  url += privateKey.c_str();    //needs to be constant character to filter out control characters padding when read from memory
  url2 += privateKey2.c_str();  //needs to be constant character to filter out control characters padding when read from memory
    
  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!server2_down) {   
    // This will send the request to the server
    if (client.connect(host2.c_str(), httpPort)) {
      client.print(String("GET ") + url2 + " HTTP/1.1\r\n" + "Host: " + host2.c_str() + "\r\n" + "Connection: close\r\n\r\n");
      delay(10);
      while (client.available()) {
      String line = client.readStringUntil('\r');
      }
    }
  }
  if (!server_down) {  
    if (client.connect(host.c_str(), httpPort)) {
      client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + host.c_str() + "\r\n" + "Connection: close\r\n\r\n"); 
      delay(10);
      while (client.available()) {
         String line = client.readStringUntil('\r');
      }
    }
  }
  //Serial.println(host);
  //Serial.println(url);
}

void handleRescan() {
  String s;
  s = "<HTML><FONT size='20'><FONT color=006666>Open</FONT><B>EVSE</B></FONT><P><B><Open Source Hardware</P></B><P><FONT FACE='Arial'><FONT SIZE=4>Rescanning...</P>";
  s += "<P>Note.  You may need to manually reconnect to the access point after rescanning.</P>";  
  s += "<P><B>Please wait at least 30 seconds before continuing.</B></P>";
  s += "<FORM ACTION='.'>";  
  s += "<P><INPUT TYPE=SUBMIT VALUE='Continue'></P>";
  s += "</FORM></P>";
  s += "</FONT></FONT></HTML>\r\n\r\n";
  server.send(200, "text/html", s);
  WiFi.disconnect();
  delay(2000);  
  ESP.reset();
}

void handleCfm() {
  String s;
  s = "<HTML><FONT size='20'><FONT color=006666>Open</FONT><B>EVSE</B></FONT><P><B>Open Source Hardware</P></B><P><FONT FACE='Arial'><FONT SIZE=4>Confirmation</P>";
  s += "<P>You are about to erase the WiFi and notification settings and external link to your dashboard!</P>";
  s += "<FORM ACTION='reset'>";
  s += "&nbsp;<TABLE><TR>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Continue    '></TD>";
  s += "</FORM><FORM ACTION='.'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Cancel    '></TD>";
  s += "</FORM>";
  s += "</TR></TABLE>";
  s += "</FONT></FONT></HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleRoot() {
  String s;
  String sTmp;
  //Serial.println("inside root");
  s = "<HTML><FONT size='20'><FONT color=006666>Open</FONT><b>EVSE</B></FONT><P><B>Open Source Hardware</P></B>";
  s += "<P><FONT FACE='Arial'><FONT SIZE=5><B>WiFi Configuration</B></FONT></P>";
  s += "<P><FONT SIZE=2>WiFi FW v";
  s += VERSION;
  s += "</FONT></P><FONT SIZE=4>";
  s += "<P>================================</P>";
  s += "<P><B>NETWORK CONNECT</B></P>";
  s += "<FORM ACTION='rescan'>";  
  s += "<INPUT TYPE=SUBMIT VALUE='     Rescan     '>";
  s += "</FORM>";
  s += "<FORM method='get' action='a'>";
  if (wifi_mode == 0)
    s += "<B><I>Connected to </B></I>";
  else
    s += "<B><I>Choose a network </B></I>";
  s += st;
  s += "<P><LABEL><B><I>&nbsp;&nbsp;&nbsp;&nbsp;or enter SSID manually:</B></I></LABEL><INPUT name='hssid' maxlength='32' value='empty'></P>";
  s += "<P><LABEL><B><I>Password:</B></I></LABEL><INPUT type='password' size = '25' name='pass' maxlength='32' value='";
  sTmp = "";
  for (int i = 0; i < epass.length(); ++i) {     // this is to allow single quote entries to be displayed
      if (epass[i] == '\'')
        sTmp += "&#39;";
      else
        sTmp += epass[i];
    }
  s += sTmp.c_str();       //needs to be constant character to filter out control characters padding when read from memory
  s += "'></P>";
  s += "<P>================================</P>";
  s += "<P><B>DATABASE SERVER</B></P>";
  if (wifi_mode != 0) {
    s += "<P>Note. You are not connected to any network so no data will be sent</P>";
    s += "<P>out. However, you can still control your OpenEVSE by selecting (Home Page).</P>";
    s += "<P>If you do want to send data, then please fill in the info above and (Submit).</P>";
    s += "<P>After you successfully connected to your network,</P>";
    s += "<P>please select (WiFi Configuration) and fill in</P>";
    s += "<P>the appropiate information about the database server.</P>";
  }
  else {
    s += "<P>Fill in the appropriate information about the</P>";
    s += "<P>Emoncms server you want use.</P>";
  }
  s += "<P>______________</P>";
  if (wifi_mode == 0) {
    s += "<P><B><I>Primary Server</B></I></P>";
    s += "<P><LABEL><I>Write key (devicekey=1..32):</I></LABEL><INPUT name='ekey' maxlength='50' value='";
    sTmp = "";
    for (int i = 0; i < privateKey.length(); ++i) {     // this is to allow single quote entries to be displayed
      if (privateKey[i] == '\'')
        sTmp += "&#39;";
      else
        sTmp += privateKey[i]; 
    }
    s += sTmp.c_str();     //needs to be constant character to filter out control characters padding when read from memory
    s += "'></P>";
    s += "<P><LABEL><I>Server address (example.com):</I></LABEL><INPUT name='host' maxlength='32' value='";
    sTmp = "";
    for (int i = 0; i < host.length(); ++i) {     // this is to allow single quote entries to be displayed
      if (host[i] == '\'')
        sTmp += "&#39;";
      else
        sTmp += host[i];
    }
    s += sTmp.c_str();    //needs to be constant character to filter out control characters padding when read from memory
    s += "'></P>";
    s += "<P><LABEL><I>Database directory (/emoncms/):</I></LABEL><INPUT name='dir' maxlength='32' value='";
    sTmp = "";
    for (int i = 0; i < directory.length(); ++i) {     // this is to allow single quote entries to be displayed
      if (directory[i] == '\'')
        sTmp += "&#39;";
      else
        sTmp += directory[i];
    }
    s += sTmp.c_str();    //needs to be constant character to filter out control characters padding when read from memory
    s += "'></P>";
    s += "<P>_______________________</P>";   
    s += "<P><B><I>Backup Server (optional)</B></I></P>";
    s += "<P><LABEL><I> Write key (apikey=1..32):</I></LABEL><INPUT name='ekey2' maxlength='50' value='";
    sTmp = "";
    for (int i = 0; i < privateKey2.length(); ++i) {     // this is to allow single quote entries to be displayed
      if (privateKey2[i] == '\'')
        sTmp += "&#39;";
      else
        sTmp += privateKey2[i];
    }
    s += sTmp.c_str();    //needs to be constant character to filter out control characters padding when read from memory
    s += "'></P>";
    s += "<P><LABEL><I>Server address (example2.com):</I></LABEL><INPUT name='host2' mzxlength='31' value='";
    sTmp = "";
    for (int i = 0; i < host2.length(); ++i) {     // this is to allow single quote entries to be displayed
      if (host2[i] == '\'')
        sTmp += "&#39;";
      else
        sTmp += host2[i];
    }
    s += sTmp.c_str();    //needs to be constant character to filter out control characters padding when read from memory
    s += "'></P>";  
    s +=  "<P><LABEL><I>Database directory (/):</I></LABEL><INPUT name='dir2' maxlength='32' value='";
    sTmp = "";
    for (int i = 0; i < directory2.length(); ++i) {     // this is to allow single quote entries to be displayed
      if (directory2[i] == '\'')
        sTmp += "&#39;";
      else
        sTmp += directory2[i];
    }
    s += sTmp.c_str();    //needs to be constant character to filter out control characters padding when read from memory
    s += "'></P>";
    s += "<P>--------</P>";
    s += "<P><LABEL><I>Node for both servers (default is 0):</I></LABEL><SELECT name='node'>"; 
    for (int i = 0; i <= 8; ++i) {
      s += "<OPTION value='" + String(i) + "'";
      if (node == String(i))
      s += "SELECTED";
      s += ">" + String(i) + "</OPTION>";
    }
    s += "</SELECT></P>";
  }  
  s += "&nbsp;<TABLE><TR>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Submit    '></TD>";
  s += "</FORM><FORM ACTION='home'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Home Page   '></TD>";
  s += "</FORM>";
  s += "</TR></TABLE>";
  s += "<FORM ACTION='confirm'>";  
  s += "<P>&nbsp;<INPUT TYPE=SUBMIT VALUE='Erase Settings'></P>";
  s += "</FORM></FONT></FONT></P>";
  s += "</HTML>\r\n\r\n";
	server.send(200, "text/html", s);
}

void handleRapi() {
  String s;
  s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Send RAPI Command<p>Common Commands:<p>Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
        s += "<p>";
        s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label><input name='rapi' maxlength='32'><p><input type='submit'></form>";
        s += "</html>\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleRapiR() {
  String s;
  String rapiString;
  String rapi = server.arg("rapi");
  rapi.replace("%24", "$");
  rapi.replace("+", " "); 
  Serial.flush();
  Serial.println(rapi);
  delay(100);
       while (Serial.available()) {
         rapiString = Serial.readStringUntil('\r');
       }    
   s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>RAPI Command Sent<p>Common Commands:<p>Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
   s += "<p>";
   s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label><input name='rapi' maxlength='32'><p><input type='submit'></form>";
   s += rapi;
   s += "<p>>";
   s += rapiString;
   s += "<p></html>\r\n\r\n";
   server.send(200, "text/html", s);
}

void handleCfg() {
  String s;
  String qsid = server.arg("ssid");
  String qhsid = server.arg("hssid");
  if (qhsid != "empty")
    qsid = qhsid;
  String qpass = server.arg("pass");   
  String qkey = server.arg("ekey"); 
  String qkey2 = server.arg("ekey2");
  String qnode = server.arg("node");
  String qhost = server.arg("host");       
  String qhost2 = server.arg("host2");  
  String qdirectory = server.arg("dir");
  String qdirectory2 = server.arg("dir2");  

  writeEEPROM(PASS_START, PASS_MAX_LENGTH, qpass);
  if (wifi_mode == 0) {
    if (privateKey != qkey) {
      writeEEPROM(KEY_START, KEY_MAX_LENGTH, qkey);
      privateKey = qkey;
    }
    if (privateKey2 != qkey2) {
      writeEEPROM(KEY2_START, KEY2_MAX_LENGTH, qkey2);
      privateKey2 = qkey2;
    }
    if (node != qnode) {    
      writeEEPROM(NODE_START, NODE_MAX_LENGTH, qnode);
      node = qnode;
    }
    if (host != qhost) {
      writeEEPROM(HOST_START, HOST_MAX_LENGTH, qhost);
      host = qhost;
    }
    if (host2 != qhost2) {
      writeEEPROM(HOST2_START, HOST2_MAX_LENGTH, qhost2);
      host2 = qhost2;
    }
    if (directory != qdirectory) {
      writeEEPROM(DIRECTORY_START, DIRECTORY_MAX_LENGTH, qdirectory);
      directory = qdirectory;
    }
    if (directory2 != qdirectory2) {
    writeEEPROM(DIRECTORY2_START, DIRECTORY2_MAX_LENGTH, qdirectory2);
    directory2 = qdirectory2;
    }
  }
  if (qsid != "not chosen") {
    writeEEPROM(SSID_START, SSID_MAX_LENGTH, qsid);
    s = "<HTML><FONT size='20'><FONT color=006666>Open</FONT><B>EVSE</B></FONT><P><B>Open Source Hardware</P></B><P><FONT FACE='Arial'><FONT SIZE=4>Updating Settings...</P>";
    if (qsid != esid.c_str() || qpass != epass.c_str()) {
      s += "<P>Saved to Memory...</P>";
      s += "<P>The OpenEVSE will reset and try to join " + qsid + "</P>";
      s += "<P>After about 30 seconds, if successful, please use the IP address</P>";
      s += "<P>assigned by your DHCP server to the OpenEVSE in your Browser</P>";
      s += "<P>in order to re-access the OpenEVSE WiFi Configuration page.</P>";
      s += "<P>---------------------</P>";
      s += "<P>If unsuccessful after 90 seconds, the OpenEVSE will go back to the";
      s += "<P>default access point at SSID:OpenEVSE.</P>";
      s += "</FONT></FONT></HTML>\r\n\r\n";
      server.send(200, "text/html", s);
      WiFi.disconnect();
      delay(2000);
      ESP.reset();  
    }
    else {
      s += "<FORM ACTION='home'>";
      s += "<P>Saved to Memory...</P>";
      s += "<P><INPUT TYPE=SUBMIT VALUE='Continue'></P>";
    }
  }
  else {
     s = "<HTML><FONT size='20'><FONT color=006666>Open</FONT><B>EVSE</B></FONT><P><B>Open Source Hardware</P></B><P><FONT FACE='Arial'><FONT SIZE=5>Warning. No network selected.";
     s += "<P>All functions except data logging will continue to work.</P>";
     s += "<FORM ACTION='home'>";
     s += "<P><INPUT TYPE=SUBMIT VALUE='     OK     '></P>";
  }
  s += "</FORM></FONT></FONT></HTML>\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleRst() {
  String s;
  s = "<HTML><FONT size='20'><FONT color=006666>Open</FONT><B>EVSE</B></FONT><B>Open Source Hardware</B><P><FONT FACE='Arial'><FONT SIZE=4>WiFi Configuration</P><P>Reset to Defaults:</P>";
  s += "<P>Clearing the EEPROM...</P>";
  s += "<P>The OpenEVSE will reset and have an IP address of 192.168.4.1</P>";
  s += "<P>After about 30 seconds, the OpenEVSE will activate the access point</P>";
  s += "<P>SSID:OpenEVSE and password:openevse</P>";
  s += "</FONT></FONT></HTML>\r\n\r\n";
  ResetEEPROM(0, EEPROM_SIZE);
  EEPROM.commit();
  server.send(200, "text/html", s);
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
}

void handleStartImmediatelyR() {
  String s;
  s = "<HTML>";
  String sCommand = "$ST 0 0 0 0^23"; 
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  sCommand = "$FE^27"; 
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  s += "<FORM ACTION='home'>";
  s += "<P><FONT SIZE=4><FONT FACE='Arial'>Success!</P>";
  s += "<P><INPUT TYPE=SUBMIT VALUE='     OK     '></FONT></FONT></P>";
  s += "</FORM>";
  s += "</HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleDelayTimer() {
  String s;
  // variables for command responses
  String sFirst = "0";
  String sSecond = "0";
  String sThird = "0";
  String sFourth = "0";
  String sFifth = "0";
  s = "<HTML>";
  s += "<FONT SIZE=6><FONT color=006666>Open</FONT><B>EVSE </B></FONT><FONT FACE='Arial'><FONT SIZE=5>Set Delay Start Timer</FONT></FONT>";  
 //get delay start timer
  Serial.flush();
  Serial.println("$GD^27");
  delay(100);
  int start_hour = 0;
  int start_min = 0;
  int stop_hour = 0;
  int stop_min = 0;
  int index;
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      int first_blank_index = rapiString.indexOf(' ');
      int second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sFirst = rapiString.substring(first_blank_index + 1, second_blank_index);  // start hour
      start_hour = sFirst.toInt();
      first_blank_index = rapiString.indexOf(' ',second_blank_index + 1);
      sSecond = rapiString.substring(second_blank_index + 1, first_blank_index); // start min
      start_min = sSecond.toInt();
      second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sThird = rapiString.substring(first_blank_index + 1, second_blank_index);  // stop hour
      stop_hour = sThird.toInt();
      first_blank_index = rapiString.indexOf(' ',second_blank_index + 1);
      sFourth = rapiString.substring(second_blank_index + 1, first_blank_index); // stop min
      stop_min = sFourth.toInt();
      sFifth = rapiString.substring(rapiString.lastIndexOf(' ') + 1,rapiString.indexOf('^'));  // timer enabled - not used 
    }
  }
  s += "<FORM METHOD='get' ACTION='delaytimerR'>";
  s += "<P><FONT FACE='Arial'><FONT SIZE=4>Start Time (hh:mm) - ";
  s += " <SELECT name='starthour'>";
  for (index = 0; index <= 9; ++index) {
     if (index == start_hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
    for (index = 10; index <= 23; ++index) {
     if (index == start_hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT>:";
  s += "<SELECT name='startmin'>";
  for (index = 0; index <= 9; ++index) {
     if (index == start_min)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
  for (index = 10; index <= 59; ++index) {
     if (index == start_min)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT></P>";
  s += "<P>Stop Timer (hh:mm) - ";
  s += "<SELECT name='stophour'>";
  for (index = 0; index <= 9; ++index) {
     if (index == stop_hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
  for (index = 10; index <= 23; ++index) {
     if (index == stop_hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT>:";
  s += "<SELECT name='stopmin'>";
  for (index = 0; index <= 9; ++index) {
     if (index == stop_min)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
  for (index = 10; index <= 59; ++index)  {
     if (index == stop_min)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT></P>";
  s += "<P>Note. All zeros will turn OFF delay timer</P>";
  s += "&nbsp;<TABLE><TR>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Submit    '></TD>";
  s += "</FORM><FORM ACTION='home'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Cancel    '></TD>";
  s += "</FORM>";
  s += "</TR></TABLE>";
  s += "</HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleDelayTimerR() {
  String s;
  String sStart_hour = server.arg("starthour");   
  String sStart_min = server.arg("startmin");
  String sStop_hour = server.arg("stophour");
  String sStop_min = server.arg("stopmin");
  s = "<HTML>";
  String sCommand = "$ST " + sStart_hour + " "+ sStart_min + " " + sStop_hour + " " + sStop_min;
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  if ( (sStart_hour != "00") || (sStart_min != "00") || (sStop_hour != "00") || (sStop_min != "00")) //turn off timer
    sCommand = "$FS^31";
  else
    sCommand = "$FE^27";
  Serial.flush();
  Serial.println(sCommand);
  delay(100);  
  s += "<FORM ACTION='home'>";
  s += "<P><FONT SIZE=4><FONT FACE='Arial'>Success!</P>";
  s += "<P><INPUT TYPE=SUBMIT VALUE='     OK     '></FONT></FONT></P>";
  s += "</FORM>";
  s += "</HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleAdvanced() {
  String s;
  String sTmp;
  String sFirst;
  String sSecond;
  s = "<HTML>";
  s += "<FONT SIZE=6><FONT color=006666>Open</FONT><B>EVSE </B></FONT><FONT FACE='Arial'><FONT SIZE=6>Advanced Menu</FONT>";
  //get EVSE flag and pilot
  int pilotamp = getRapiFlag();
  s += "<FORM METHOD='get' ACTION='advancedR'>";
  s += "<P><FONT SIZE=4>Service level is set to <INPUT TYPE='radio' NAME='service_level' VALUE='auto'";
  if (!(evse_flag & 0x0020)) // auto detect disabled flag
    s += " CHECKED";
  s += ">Auto Detect  <INPUT TYPE='radio' NAME='service_level' VALUE='1'";
  if ((evse_flag & 0x0021) == 0x0020)
    s += " CHECKED";
  s += ">1  <INPUT TYPE='radio' NAME='service_level' VALUE='2'";
  if ((evse_flag & 0x0021) == 0x0021)
    s += " CHECKED";
  s += ">2</P>";
  
  s += "<P>Diode check is set to <INPUT TYPE='radio' NAME='diode_check' VALUE='1'";
  if (!(evse_flag & 0x0002)) // yes
    s += " CHECKED";
  s += ">YES  <INPUT TYPE='radio' NAME='diode_check' VALUE='0'";
  if ((evse_flag & 0x0002) == 0x0002) //no
    s += " CHECKED";
  s += ">NO</P>";

  s += "<P>Vent required state is set to <INPUT TYPE='radio' NAME='vent_check' VALUE='1'";
  if (!(evse_flag & 0x0004)) // yes
    s += " CHECKED";
  s += ">YES  <INPUT TYPE='radio' NAME='vent_check' VALUE='0'";
  if ((evse_flag & 0x0004) == 0x0004) //no
    s += " CHECKED";
  s += ">NO</P>";

  s += "<P>Ground check is set to <INPUT TYPE='radio' NAME='ground_check' VALUE='1'";
  if (!(evse_flag & 0x0008)) // yes
    s += " CHECKED";
  s += ">YES  <INPUT TYPE='radio' NAME='ground_check' VALUE='0'";
  if ((evse_flag & 0x0008) == 0x0008) //no
    s += " CHECKED";
  s += ">NO</P>";

  s += "<P>Stuck relay is set to <INPUT TYPE='radio' NAME='stuck_relay' VALUE='1'";
  if (!(evse_flag & 0x0010)) // yes
    s += " CHECKED";
  s += ">YES  <INPUT TYPE='radio' NAME='stuck_relay' VALUE='0'";
  if ((evse_flag & 0x0010) == 0x0010) //no
    s += " CHECKED";
  s += ">NO</P>";

  /*s += "<P>Auto start charging is set to <INPUT TYPE='radio' NAME='auto_start' VALUE='1'";
  if (!(evse_flag & 0x0040)) // yes
    s += " CHECKED";
  s += ">YES  <INPUT TYPE='radio' NAME='auto_start' VALUE='0'";
  if ((evse_flag & 0x0040) == 0x0040) //no
    s += " CHECKED";
  s += ">NO</P>";*/

  s += "<P>GFI self test is set to <INPUT TYPE='radio' NAME='gfi_check' VALUE='1'";
  if (!(evse_flag & 0x0200)) // yes
    s += " CHECKED";
  s += ">YES  <INPUT TYPE='radio' NAME='gfi_check' VALUE='0'";
  if ((evse_flag & 0x0200) == 0x0200) //no
    s += " CHECKED";
  s += ">NO</P>";
  
  s += "<P>LCD backlight is set to <INPUT TYPE='radio' NAME='lcd' VALUE='1'";
  if (!(evse_flag & 0x0100)) // yes
    s += " CHECKED";
  s += ">RGB  <INPUT TYPE='radio' NAME='lcd' VALUE='0'";
  if ((evse_flag & 0x0100) == 0x0100) //no
    s += " CHECKED";
  s += ">Monochrome</P>";
  s += "<P>-------------------</P>";
  s += "<P><LABEL>External address of dashboard:</LABEL><INPUT name='stat_path' maxlength='101' value='";
  sTmp = "";
  for (int i = 0; i < status_path.length(); ++i) {     // this is to allow single quote entries to be displayed
      if (status_path[i] == '\'')
        sTmp += "&#39;";
      else
        sTmp += status_path[i];
    }
  s += sTmp.c_str();
  s += "'></P>";
  if (wifi_mode == 0) {
    s += "<P>Notification options:</P>";
    s += "<P>&nbsp;&nbsp;<INPUT TYPE='checkbox' NAME='plug_notify' VALUE='1' ";
    if (plug_notify == 1) 
      s += "CHECKED ";
    s += "> When not plugged in by ";
    s += " <SELECT name='plug_hour'>";
    for (int index = 0; index <= 9; ++index) {
       if (index == plug_hour)
         s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
       else
         s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
    }
    for (int index = 10; index <= 23; ++index) {
       if (index == plug_hour)
         s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
       else
         s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
    }
    s += "</SELECT>:";
    s += "<SELECT name='plug_min'>";
    for (int index = 0; index < 1; ++index) {
       if (index == plug_min)
         s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index*10) + "</OPTION>";
       else
         s += "<OPTION value='" + String(index) + "'>" + "0" + String(index*10) + "</OPTION>";
    }
    for (int index = 1; index < 6; ++index) {
       if (index == plug_min)
         s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index*10) + "</OPTION>";
       else
         s += "<OPTION value='" + String(index) + "'>" + String(index*10) + "</OPTION>";
    }
    s += "</SELECT></P><P>&nbsp; and remind every ";
    s += "<SELECT name='plug_repeat'>";
    for (int index = 0; index <= 3; ++index) {
       if (plug_repeat == index)
         s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index*5) + "</OPTION>";
       else
         s += "<OPTION value='" + String(index) + "'>" + String(index*5) + "</OPTION>";
    }
    s += "</SELECT> minutes (0 is no reminders)</P>";
    s += "<P>&nbsp;&nbsp;<INPUT TYPE='checkbox' NAME='charge_notify' VALUE='1' ";
    if (charge_notify == 1) 
      s += "CHECKED ";
    s += "> When not charging by ";
      s += " <SELECT name='charge_hour'>";
    for (int index = 0; index <= 9; ++index) {
       if (index == charge_hour)
         s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
       else
         s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
    }
    for (int index = 10; index <= 23; ++index) {
       if (index == charge_hour)
         s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
       else
         s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
    }
    s += "</SELECT>:";
    s += "<SELECT name='charge_min'>";
    for (int index = 0; index < 1; ++index) {
       if (index == charge_min)
         s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index*10) + "</OPTION>";
       else
         s += "<OPTION value='" + String(index) + "'>" + "0" + String(index*10) + "</OPTION>";
    }
    for (int index = 1; index < 6; ++index) {
       if (index == charge_min)
         s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index*10) + "</OPTION>";
       else
         s += "<OPTION value='" + String(index) + "'>" + String(index*10) + "</OPTION>";
    }
    s += "</SELECT></P><P>&nbsp; and remind every ";
    s += "<SELECT name='charge_repeat'>";
    for (int index = 0; index <= 3; ++index) {
       if (charge_repeat == index)
         s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index*5) + "</OPTION>";
       else
         s += "<OPTION value='" + String(index) + "'>" + String(index*5) + "</OPTION>";
    }
    s += "</SELECT> minutes (0 is no reminders)</P>";
  }
  s += "</FONT></FONT>";
  s += "&nbsp;<TABLE><TR>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Submit    '></TD>";
  s += "</FORM><FORM ACTION='home'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Cancel    '></TD>";
  s += "</FORM>";
  s += "</TR></TABLE>";
  s += "</HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleAdvancedR() {
  String s;
  String sCommand;
  String sService = server.arg("service_level");
  String sDiode = server.arg("diode_check");
  String sStuck_relay = server.arg("stuck_relay");
  String sGround = server.arg("ground_check");
  String sVent = server.arg("vent_check");
  String sLCD = server.arg("lcd");
  String sGFI = server.arg("gfi_check");
  String sStat = server.arg("stat_path");  
  String sPnotify = server.arg("plug_notify");
  String sPHour = server.arg("plug_hour");
  String sPMin = server.arg("plug_min");
  String sPRepeat = server.arg("plug_repeat");
  String sCnotify = server.arg("charge_notify");
  String sCHour = server.arg("charge_hour");
  String sCMin = server.arg("charge_min");
  String sCRepeat = server.arg("charge_repeat");
  count = 0;
  s = "<HTML>";
  if (sDiode == "1")
    sCommand = "$SD 1^22";   // diode check
  else {
    count++;
    sCommand = "$SD 0^23";
  }
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  if (sGFI == "1")
    sCommand = "$SF 1^20";   // GFI self test
  else {
    count++;
    sCommand = "$SF 0^21";
  }
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  if (sGround == "1")
    sCommand = "$SG 1^21";   // ground check
  else   {
    count++;
    sCommand = "$SG 0^20";
  }
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  if (sStuck_relay == "1")
    sCommand = "$SR 1^34";   // stuck relay check
  else {
    count++;
    sCommand = "$SR 0^35";
  }
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  if (sLCD == "1")
    sCommand = "$S0 1^56";   // RGB
  else
    sCommand = "$S0 0^57";     // mono
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  if (sVent == "1")
    sCommand = "$SV 1^30";   // vent check
  else {
    count++;
    sCommand = "$SV 0^31";
  }
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  if (sService == "1")
    sCommand = "$SL 1^2A";   // level 1
  else if (sService == "2")
    sCommand = "$SL 2^29";     // level 2
  else if (sService == "auto")
    sCommand = "$SL A^5A";     // Auto detect
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  sCommand = "$FR^30";
  Serial.flush();
  Serial.println(sCommand);
  delay(100); 
  //store changes only
  if (status_path != sStat) {
    writeEEPROM(STAT_START, STAT_MAX_LENGTH, sStat);
    status_path = sStat;
  }
  int plug_changed = 0;
  if (plug_notify != sPnotify.toInt()) {
    writeEEPROM(PNOTIFY_START, PNOTIFY_MAX_LENGTH, sPnotify);
    plug_notify = sPnotify.toInt();
    plug_changed = 1;
  }
  if (plug_hour != sPHour.toInt()) {
    writeEEPROM(PHOUR_START, PHOUR_MAX_LENGTH, sPHour);
    plug_hour = sPHour.toInt();
    plug_changed = 1;
  }
  if (plug_min != sPMin.toInt()) {
    writeEEPROM(PMIN_START, PMIN_MAX_LENGTH, sPMin);
    plug_min = sPMin.toInt();
    plug_changed = 1;
  }
  if (plug_repeat != sPRepeat.toInt()) {
    writeEEPROM(PREPEAT_START, PREPEAT_MAX_LENGTH, sPRepeat);
    plug_repeat = sPRepeat.toInt();
    p_reminders = 0;
    update_and_save_notify_flags();
  }
  int charge_changed = 0;
  if (charge_notify != sCnotify.toInt()) {
    writeEEPROM(CNOTIFY_START, CNOTIFY_MAX_LENGTH, sCnotify);
    charge_notify = sCnotify.toInt();
    charge_changed = 1;
  }
  if (charge_hour != sCHour.toInt()) {
    writeEEPROM(CHOUR_START, CHOUR_MAX_LENGTH, sCHour);
    charge_hour = sCHour.toInt();
    charge_changed = 1;
  }
  if (charge_min != sCMin.toInt()) {
    writeEEPROM(CMIN_START, CMIN_MAX_LENGTH, sCMin);
    charge_min = sCMin.toInt();
    charge_changed = 1;
  }
  if (charge_repeat != sCRepeat.toInt()) {
    writeEEPROM(CREPEAT_START, CREPEAT_MAX_LENGTH, sCRepeat);
    charge_repeat = sCRepeat.toInt();
    c_reminders = 0;
    update_and_save_notify_flags();
  }
  if (plug_changed) {
    p_ready = 1;
    p_notify_sent = 0;
    p_reminders = 0;
    update_and_save_notify_flags();
  }
  if (charge_changed) {
    c_ready = 1;
    c_notify_sent = 0;
    c_reminders = 0;
    update_and_save_notify_flags();
  } 
  delay(3000 + count*1000);
  s += "<FORM ACTION='home'>";  
  s += "<P><FONT SIZE=4><FONT FACE='Arial'>Success!</P>";
  s += "<P><INPUT TYPE=SUBMIT VALUE='     OK     '></FONT></FONT></P>";
  s += "</FORM>";
  s += "</HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}

int getRapiEVSEState() {
  String sFirst = "0";
  String sSecond = "0";
  Serial.flush();
  Serial.println("$GS^30");
  delay(100);
  int state = 0;
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      sFirst = rapiString.substring(rapiString.indexOf(' '));    // state
      state = sFirst.toInt();
      if (state == 3) {
        sSecond = rapiString.substring(rapiString.lastIndexOf(' ') + 1,rapiString.indexOf('^'));  // seconds charging
        minutes_charging = sSecond.toInt()/60; //convert to minutes
      }
    }
  }
  return state;
}

int getRapiFlag() {
  String sFirst = "0";
  String sSecond = "0";
  int pilotamp = 0;
  
  Serial.flush();
  Serial.println("$GE^26");
  delay(100); 
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      sFirst = rapiString.substring(rapiString.indexOf(' '));           // pilot setting in Amps 
      pilotamp = sFirst.toInt();
      sSecond = rapiString.substring(rapiString.lastIndexOf(' ') + 1,rapiString.indexOf('^'));  // flag in hex
      evse_flag = (int)strtol(&sSecond[1], NULL, 16);                   // flag as integer    
    }
  }
  return pilotamp;
}
  
int getRapiVolatile() {
  String sFirst = "0";
  int vflag;
 
  Serial.flush();
  Serial.println("$GL^2F");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      sFirst = rapiString.substring(rapiString.indexOf(' '));    // volatile flag
       vflag = (int)strtol(&sFirst[1], NULL, 16);   
    }
  } 
  return vflag;
}

void getRapiDateTime() {
  String sFirst = "0";
  String sSecond = "0";
  String sThird = "0";
  String sFourth = "0";
  String sFifth = "0";
  String sSixth = "0";
  
  Serial.flush();
  Serial.println("$GT^37");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      int first_blank_index = rapiString.indexOf(' ');
      int second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sFirst = rapiString.substring(first_blank_index + 1, second_blank_index);  // 2 digit year
      year = sFirst.toInt();
      first_blank_index = rapiString.indexOf(' ',second_blank_index + 1);
      sSecond = rapiString.substring(second_blank_index + 1, first_blank_index); // month
      month = sSecond.toInt();
      second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sThird = rapiString.substring(first_blank_index + 1, second_blank_index);  // day  
      day = sThird.toInt();
      first_blank_index = rapiString.indexOf(' ',second_blank_index + 1);
      sFourth = rapiString.substring(second_blank_index + 1, first_blank_index); // hour 
      hour = sFourth.toInt();
      second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sFifth = rapiString.substring(first_blank_index + 1, second_blank_index);  // min
      minutes = sFifth.toInt();
      sSixth = rapiString.substring(rapiString.lastIndexOf(' '),rapiString.indexOf('^'));    // sec 
      seconds = sSixth.toInt();
    }
  }
}

void handleDateTime() {
  String s;
  int index = 0;
  s = "<HTML>";
  s +="<FONT SIZE=6><FONT color=006666>Open</FONT><B>EVSE </B></FONT><FONT FACE='Arial'><FONT SIZE=5>Edit Date and Time</FONT>";  
 //get date and time
  getRapiDateTime();
  s += "<FORM METHOD='get' ACTION='datetimeR'>";
  s += "<P><FONT SIZE=4>Current Date is ";
  s += "<SELECT name='month'><OPTION value='1'";
  if (month == 1)
    s += " SELECTED";
  s += " >January</OPTION><OPTION value='2'";
  if (month == 2)
   s += " SELECTED";
  s += " >February</OPTION><OPTION value='3'";
  if (month == 3)
   s += " SELECTED";
  s += " >March</OPTION><OPTION value='4'";
  if (month == 4)
   s += " SELECTED";
  s += " >April</OPTION><OPTION value='5'";
  if (month == 5)
   s += " SELECTED";
  s += " >May</OPTION><OPTION value='6'";
  if (month == 6)
   s += " SELECTED";
  s += " >June</OPTION><OPTION value='7'";
  if (month == 7)
   s += " SELECTED";
  s += " >July</OPTION><OPTION value='8'";
  if (month == 8)
   s += " SELECTED";
  s += " >August</OPTION><OPTION value='9'";
  if (month == 9)
   s += " SELECTED";
  s += " >September</OPTION><OPTION value='10'";
  if (month == 10)
   s += " SELECTED";
  s += " >October</OPTION><OPTION value='11'";
  if (month == 11)
   s += " SELECTED";
  s += " >November</OPTION><OPTION value='12'";
  if (month ==12)
   s += " SELECTED";
  s += " >December</OPTION></SELECT>";
  s += " <SELECT name='day'>";
  for (index = 1; index <= 31; ++index) {
     if (index == day)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT>, 20";
  s += "<SELECT name='year'>";
  for (index = 16; index <= 99; ++index) {
     if (index == year)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT></P>";
  s += "<P> Current Time (hh:mm) is ";
  s += "<SELECT name='hour'>";
  for (index = 0; index <= 9; ++index) {
     if (index == hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
  for (index = 10; index <= 23; ++index) {
     if (index == hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT>:";
  s += "<SELECT name='minutes'>";
  for (index = 0; index <= 9; ++index) {
     if (index == minutes)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
  for (index = 10; index <= 59; ++index) {
     if (index == minutes)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT></P>";
  s += "&nbsp;<TABLE><TR>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Submit    '></TD>";
  s += "</FORM><FORM ACTION='home'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Cancel    '></TD>";
  s += "</FORM>";
  s += "</TR></FONT></FONT></TABLE>";
  s += "</HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleDateTimeR() {
  String s;
  String sMonth = server.arg("month");
  String sDay = server.arg("day");
  String sYear = server.arg("year");
  String sHour = server.arg("hour");
  String sMinutes = server.arg("minutes");
  int month = 0;
  int day = 0;
  int year = 0;

  month = sMonth.toInt();
  year = sYear.toInt();
  switch (month) {
    case 1:
      day = 31;
      break;
    case 2:
      if (year%4 == 0)
        day = 29;
      else
        day = 28;
      break;
    case 3:
      day = 31;
      break;
    case 4:
      day = 30;
      break;
    case 5:
      day = 31;
      break;
    case 6:
      day = 30;
      break;
    case 7:
      day = 31;
      break;
    case 8:
      day = 31;
      break;
    case 9:
      day = 30;
      break;
    case 10:
      day = 31;
      break;
    case 11:
      day = 30;
      break;
    case 12:
      day = 31;
      break;
    default:
      day = 0;
  }
  s = "<HTML><FONT SIZE=4><FONT FACE='Arial'>";
  if (sDay.toInt() <= day) {
    String sCommand = "$S1 " + sYear + " " + sMonth + " " + sDay + " " + sHour + " " + sMinutes + " 0";
    Serial.flush();
    Serial.println(sCommand);
    delay(100);
    s += "Success!<P><FORM ACTION='home'>";
  }
  else
    s += "Invalid Date. Please try again.<P><FORM ACTION='datetime'>";
  s += "<INPUT TYPE=SUBMIT VALUE='     OK     '></FORM></P></FONT></FONT></HTML>\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleHomeR() {
  String s;
  String sCurrent = server.arg("maxcurrent");
  String sEvse = server.arg("evse");   
  String sTime_limit = server.arg("timelimit");
  String sCharge_limit = server.arg("chargelimit");        
  s = "<HTML>";
  String sCommand = "$SC " + sCurrent;
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  if (sEvse == "enable") {
    sCommand = "$FE^27";
    Serial.flush();
    Serial.println(sCommand);
    delay(100);
  }
  if (sEvse == "disable")  {
    sCommand = "$FD^26";
    Serial.flush();
    Serial.println(sCommand);
    delay(100);
  }
  if (sEvse == "sleep") {
    sCommand = "$FS^31"; 
    Serial.flush();
    Serial.println(sCommand);
    delay(100);
  }
  if (sEvse == "reset") {
    sCommand = "$FR^30";
    Serial.flush();
    Serial.println(sCommand);
    delay(3000 + count*1000);
  }
  sCommand = "$S3 " + sTime_limit;
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  sCommand = "$SH " + sCharge_limit;
  Serial.flush();
  Serial.println(sCommand);
  delay(100);
  s += "<FORM ACTION='home'>"; 
  s += "<P><FONT SIZE=4><FONT FACE='Arial'>Success!</P>";
  s += "<P><INPUT TYPE=SUBMIT VALUE='     OK     '></FONT></FONT></P>";
  s += "</FORM>";
  s += "</HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleHome() {
  String s;
  char tmpStr[20];
  // variables for command responses
  String sFirst = "0";
  String sSecond = "0";
  String sThird = "0";
  String  sFourth = "0";
  String sFifth = "0";
  String sSixth = "0";
  int first = 0;
  int second = 0;

  IPAddress myAddress = WiFi.localIP();
  //get firmware version
  Serial.flush();
  Serial.println("$GV^35");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      int first_blank_index = rapiString.indexOf(' ');
      int second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sFirst = rapiString.substring(first_blank_index + 1,second_blank_index);   //firmware version
      sSecond = rapiString.substring(rapiString.lastIndexOf(' ') + 1,rapiString.indexOf('^'));    //RAPI version 
    }
  }
  s = "<HTML>";
  s += "<FONT SIZE=6><FONT color=006666>Open</FONT><B>EVSE </B></FONT><FONT FACE='Arial'><FONT SIZE=6>Home</FONT>";
  s += "<P><FONT SIZE=2>EVSE FW v" + sFirst + ",    RAPI v" + sSecond;
  s += ",   WiFi FW v";
  s += VERSION;
  s += "</P>";  // both firmware (controller and wifi) and RAPI version compatibility
  s += "<P>Connected to ";
  if (wifi_mode == 0 ) {
    sprintf(tmpStr,"%d.%d.%d.%d",myAddress[0],myAddress[1],myAddress[2],myAddress[3]);
    s += esid;
    s += " at ";
    s += tmpStr;
  }
  else
    s += "OpenEVSE at 192.168.4.1";
  s += "</FONT></P>";
  //get delay timer settings
  String sStart_hour = "?";
  String sStart_min = "?";
  String sStop_hour = "?";
  String sStop_min = "?";
  int timer_enabled = 0;
  Serial.flush();
  Serial.println("$GD^27");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      int first_blank_index = rapiString.indexOf(' ');
      int second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sStart_hour = rapiString.substring(first_blank_index + 1, second_blank_index);  // start hour
      if (sStart_hour.toInt() <= 9)
        sStart_hour = "0" + sStart_hour;
      first_blank_index = rapiString.indexOf(' ',second_blank_index + 1);
      sStart_min = rapiString.substring(second_blank_index + 1, first_blank_index);   // start min
      if (sStart_min.toInt() <= 9)
        sStart_min = "0" + sStart_min;
      second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sStop_hour = rapiString.substring(first_blank_index + 1, second_blank_index);   // stop hour
      if (sStop_hour.toInt() <= 9)
        sStop_hour = "0" + sStop_hour;
      first_blank_index = rapiString.indexOf(' ',second_blank_index + 1);
      sStop_min = rapiString.substring(second_blank_index + 1, first_blank_index);    // stop min
      if (sStop_min.toInt() <= 9)
        sStop_min = "0" + sStop_min;
      sFifth = rapiString.substring(rapiString.lastIndexOf(' ') + 1,rapiString.indexOf('^'));  // timer enabled
      timer_enabled = sFifth.toInt();
    }
  }
  //get date and time
  getRapiDateTime();
  if (year <= 9) {
    sFirst = "0";
    sFirst += year;
  }
  else
    sFirst = year;  
  sThird = day;   
  if (hour <= 9) {
    sFourth = "0";
    sFourth += hour;
  }
  else 
    sFourth = hour;       
  if (minutes <= 9) {
    sFifth = "0";
    sFifth += minutes;
  }
  else
    sFifth = minutes;
  if (seconds <= 9) {
    sSixth = "0";
    sSixth += seconds;
  }
  else
    sSixth = seconds;       

  switch (month) {
    case 1:
      sSecond = "January ";
      break;
    case 2:
      sSecond = "February ";
      break;
    case 3:
      sSecond = "March ";
      break;
    case 4:
      sSecond = "April ";
      break;
    case 5:
      sSecond = "May ";
      break;
    case 6:
      sSecond = "June ";
      break;
    case 7:
      sSecond = "July ";
      break;
    case 8:
      sSecond = "August ";
      break;
    case 9:
      sSecond = "September ";
      break;
    case 10:
      sSecond = "October ";
      break;
    case 11:
      sSecond = "November ";
      break;
    case 12:
      sSecond = "December ";
      break;
    default:
      sSecond = "not set ";
  }
  s += "<FORM ACTION='datetime'>";
  s += "<P><FONT SIZE=4>Today is " + sSecond + sThird + ", 20" + sFirst + ",&nbsp;" + sFourth + ":" + sFifth + ":" + sSixth + "  <INPUT TYPE=SUBMIT VALUE='  Change  '></P>"; // format June 3, 2016, 14:20:34
  s += "</FORM>";
  
  // get energy usage
  Serial.flush();
  Serial.println("$GU^36");
  delay(100);
  float session_kwh;
  float lifetime_kwh;
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      sFirst = rapiString.substring(rapiString.indexOf(' '));      // this session in Ws
      int convert = sFirst.toInt();
      first = convert/36000; //convert to 100's of kWh
      session_kwh = float(first/100.00); //used to display 2 decimal place
      sSecond = rapiString.substring(rapiString.lastIndexOf(' ') + 1,rapiString.indexOf('^')); // lifetime in kWh
      convert = sSecond.toInt();
      second = convert/10;  //convert to 100's of kWh
      lifetime_kwh = float(second/100.00); //used to display 2 decimal place
    }
  }
  s += "<P>Energy usage:</P>";
  s += "<P>&nbsp;&nbsp;&nbsp;this session is " + String(session_kwh) + " kWh</P>";
  s += "<P>&nbsp;&nbsp;&nbsp;lifetime is " + String(lifetime_kwh) + " kWh</P>";
  
  // get state
  int evse_state = getRapiEVSEState();
  int vflag = getRapiVolatile();
  int sleep = 0;
  int display_connected_indicator = 0;
  int evse_disabled = 0;
  int no_current_display = 0;
  switch (evse_state) {
    case 1:
      sFirst = "<SPAN STYLE='background-color: #00ff26'> not connected&#44; ready&nbsp;</SPAN>";    // vehicle state A - green
      no_current_display = 1;
      break;
    case 2:
      sFirst = "<SPAN STYLE='background-color: #FFFF80'> connected&#44; ready&nbsp;</SPAN>";       // vehicle state B - yellow
      no_current_display = 1;
      break;
    case 3:
      sFirst = "<SPAN STYLE='background-color: #0032FF'><FONT color=FFFFFF> charging&nbsp;</FONT></SPAN>&nbsp;for ";    // vehicle state C - blue
      sFirst += minutes_charging;
      if (minutes_charging == 1)
        sFirst += " minute";
      else
        sFirst += " minutes";
      break;
    case 4:
      sFirst = "<SPAN STYLE='background-color: #FFB900' venting required&nbsp;</SPAN>";          // vehicle state D - amber
      display_connected_indicator = 1;
      break;
    case 5:
      sFirst = "<SPAN STYLE='background-color: #FF0000'><FONT color=FFFFFF> ERROR, diode check failed&nbsp;</FONT></SPAN>";  // red for erros
      display_connected_indicator = 1;
      break;
    case 6:
      sFirst = "<SPAN STYLE='background-color: #FF0000'><FONT color=FFFFFF> ERROR, GFCI fault&nbsp;</FONT></SPAN>";
      display_connected_indicator = 1;
      break;
    case 7:
      sFirst = "<SPAN STYLE='background-color: #FF0000'><FONT color=FFFFFF> ERROR, bad ground&nbsp;</FONT></SPAN>";
      display_connected_indicator = 1;
      break;
    case 8:
      sFirst = "<SPAN STYLE='background-color: #FF0000'><FONT color=FFFFFF> ERROR, stuck contactor&nbsp;</FONT></SPAN>";
      display_connected_indicator = 1;
      break;
    case 9:
      sFirst = "<SPAN STYLE='background-color: #FF0000'><FONT color=FFFFFF> ERROR, GFI self&#45;test failure&nbsp;</FONT></SPAN>";
      display_connected_indicator = 1;
      break;
    case 10:
      sFirst = "<SPAN STYLE='background-color: #FF0000'><FONT color=FFFFFF> ERROR, over temp shutdown&nbsp;</FONT></SPAN>";
      display_connected_indicator = 1;
      break;
    case 254:
      if (vflag & 0x04) //SetLimitSleep state
         sFirst = "<SPAN STYLE='background-color: #FFA0FF'> charge/time limit reached&nbsp;</SPAN>";  // pink purple 
      else if (timer_enabled)
         sFirst = "<SPAN STYLE='background-color: #C880FF'> waiting for start time&nbsp;</SPAN>";  // purple
      else
         sFirst = "<SPAN STYLE='background-color: #9680FF'> sleeping&nbsp;</SPAN>";  // purplish
      sleep = 1;
      display_connected_indicator = 1;
      break;
    case 255:
      sFirst = "<SPAN STYLE='background-color: #FF80FF'> disabled&nbsp;</SPAN>";  // violet
      evse_disabled = 1;
      no_current_display = 1;
      break;
    default:
      sFirst = "<SPAN STYLE='background-color: #FFB900'> unknown&nbsp;</SPAN>";  // amber
      display_connected_indicator = 1;
  }
  s += "<P><FONT SIZE=5>Status is&nbsp;" + sFirst + "</P>";  
  // get current reading
  int current_reading = 0;
  int voltage_reading = -1;
  float current_read;
  Serial.flush();
  Serial.println("$GG^24");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      sFirst = rapiString.substring(rapiString.indexOf(' '));      // this session in mA
      sSecond = rapiString.substring(rapiString.lastIndexOf(' ') + 1,rapiString.indexOf('^'));  // voltage - not used
      int convert = sFirst.toInt();
      current_reading = convert/10; //convert to 100's of Amps
      current_read = current_reading/100.0;
      convert = sSecond.toInt();
      if (convert != -1)
        voltage_reading = convert/1000; //convert to Volts
    }
  }
  
  //get pilot settign and EVSE flag
  int pilotamp = getRapiFlag();
  String sLevel;
  if (evse_flag & 0x0001) {                                  // service level flag 1 = level 2, 0 - level 1
    if (voltage_reading == -1)
      sLevel = "2 (240 V)";
    else
      sLevel = "2 (" + String(voltage_reading) + " V)";
  }
  else {
    if (voltage_reading == -1)
      sLevel = "1 (120 V)";
    else
      sLevel = "1 (" + String(voltage_reading) + " V)";
  }
  s += "<P>";
  if (display_connected_indicator == 1) {
    if (vflag & PLUG_IN_MASK) //connected flag
      s+= "&nbsp;&nbsp;and&nbsp;<SPAN STYLE='background-color: #00ff26'> plugged in </SPAN>";  //green plugged in
    else
      s+= "&nbsp;&nbsp;and&nbsp;<SPAN STYLE='background-color: #FFB900'> NOT plugged in </SPAN>";  //amber not plugged in
  }
  else if (!no_current_display)
    s += "&nbsp;&nbsp;using <B>" + String(current_read) + " A</B>";
  else
    s += "&nbsp;";
  s += "&nbsp;at Level " + sLevel + "</FONT></P>";
  
  // get temperatures
  int evsetemp1 = 0;
  int evsetemp2 = 0;
  int evsetemp3 = 0;
  Serial.flush();
  Serial.println("$GP^33");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if (rapiString.startsWith("$OK") ) {
      sFirst = rapiString.substring(rapiString.indexOf(' '));
      evsetemp1 = sFirst.toInt();
      if (evsetemp1 != 0)
        evsetemp1 = (evsetemp1*9 + 25)/50 + 32;        // convert to F  //reorder operations and added correct rounding for int math
      int firstRapiCmd = rapiString.indexOf(' ');
      sSecond = rapiString.substring(rapiString.indexOf(' ', firstRapiCmd + 1 ));
      evsetemp2 = sSecond.toInt();
      if (evsetemp2 != 0)
        evsetemp2 = (evsetemp2*9 + 25)/50 + 32;        // convert to F  //reorder operations and added correct rounding for int math
      sThird = rapiString.substring(rapiString.lastIndexOf(' ') + 1,rapiString.indexOf('^'));
      evsetemp3 = sThird.toInt();
      if (evsetemp3 != 0)
        evsetemp3 = (evsetemp3*9 + 25)/50 + 32;        // convert to F  //reorder operations and added correct rounding for int math
    }
  }
  s += "<P>Internal temp(s)";
  if (evsetemp1 != 0) {
    s += ":  ";
    s += evsetemp1;
    s += "&deg;F ";
  }
  if (evsetemp2 != 0)  {
    s += ":  ";
    s += evsetemp2;
    s += "&deg;F ";
  }  
  if (evsetemp3 != 0) {
    s += ":  ";
    s += evsetemp3;
    s += "&deg;F";
  }
  s += "</P>";

  // delay start timer
  s += "<FORM ACTION='delaytimer'>";
  s += "<P>Delay start timer is ";
  if (timer_enabled) {
    s += "ON</P>";
    s += "<P>&nbsp;&nbsp;&nbsp;start at " + sStart_hour + ":" + sStart_min + "</P>";
    s += "<P>&nbsp;&nbsp;&nbsp;stop at " + sStop_hour + ":" + sStop_min + "</P>";
    s += "<TABLE><TR>";
    s += "<TD><INPUT TYPE=SUBMIT VALUE='      Change      '></TD>";
    s += "</FORM><FORM ACTION='startimmediatelyR'>";
    s += "<TD><INPUT TYPE=SUBMIT VALUE='Start Now/Turn OFF'></TD>";
    s += "</FORM>";
    s += "</TR></TABLE>";
  }
  else {
    s += "OFF&nbsp;&nbsp;<INPUT TYPE=SUBMIT VALUE='   Turn ON   '></P>";
    s += "</FORM>";
  }
  //get time limit
  first = 0;
  Serial.flush();
  Serial.println("$G3^50");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      sFirst = rapiString.substring(rapiString.indexOf(' '),rapiString.indexOf('^'));      //  in 15 minutes increments
      first = sFirst.toInt();
    }
  }
  s += "<P>Limits for this session (first to reach):</P>";
  s += "<FORM METHOD='get' ACTION='homeR'>";
  s += "<P>&nbsp;&nbsp;&nbsp;time limit is set to ";
  s += "<SELECT name='timelimit'>";
  int found_default = 0;  //used if current default setting doesn't match drop down for cases if changed via RAPI command
  for (int index = 0; index <= TIME_LIMIT_MAX; ++index) {   // drop down at 30 min increments
     if (index == 0 ) {
       if (first == 0) {
         s += "<OPTION value='" + String(index) + "'SELECTED>" + "no limit</OPTION>";
         found_default = 1;
       }
       else
         s += "<OPTION value='" + String(index) + "'>" +  "no limit</OPTION>";
     }
     else {
       if (first == (index*2)) {
         s += "<OPTION value='" + String(index*2) + "'SELECTED>";
         found_default = 1;
       }
       else
         s += "<OPTION value='" + String(index*2) + "'>";
       s += String(index * 30) + "</OPTION>";
     }
  }
  if (!found_default)
    s += "<OPTION value='" + sFirst + "'SELECTED>" + String(first*15) + "</OPTION>";
  s += "</SELECT> minutes</P>";
  
  // get energy limit
  first = 0;
  Serial.flush();
  Serial.println("$GH^2B");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK")) {
      sFirst = rapiString.substring(rapiString.indexOf(' '),rapiString.indexOf('^'));      //  in kWh units
      first = sFirst.toInt();
    }
  }
  s += "<P>&nbsp;&nbsp;&nbsp;charge limit is set to ";
  s += "<SELECT name='chargelimit'>";
  found_default = 0;  //used if current default setting doesn't match drop down for cases if changed via RAPI command
  for (int index = 0; index <= CHARGE_LIMIT_MAX; ++index) {
     if (index == 0 ) {
       if (first == 0) {
         found_default = 1;
         s += "<OPTION value='" + String(index) + "'SELECTED>" + "no limit</OPTION>";
       }
       else
         s += "<OPTION value='" + String(index) + "'>" +  "no limit</OPTION>";
     }
     else
       if (first == (index*2))  {
         found_default = 1;
         s += "<OPTION value='" + String(index*2) + "'SELECTED>" + String(index*2) + "</OPTION>";
       }
       else
         s += "<OPTION value='" + String(index*2) + "'>" + String(index*2) + "</OPTION>";
  }
  if (!found_default)
    s += "<OPTION value='" + sFirst + "'SELECTED>" + sFirst + "</OPTION>";
  s += "</SELECT> kWh</P>";

  //get min max current allowable
  Serial.flush();
  Serial.println("$GC^20");
  delay(100);
  int minamp = 6;
  int maxamp = 7;
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      sFirst = rapiString.substring(rapiString.indexOf(' '));           // min amp
      minamp = sFirst.toInt();
      sSecond = rapiString.substring(rapiString.lastIndexOf(' ') + 1,rapiString.indexOf('^'));  // max amp
      maxamp = sSecond.toInt();
    }
  }
  //get pilot setting
  s += "<P>Max current is ";
  s += "<SELECT name='maxcurrent'>";
  found_default = 0;  //used if current default setting doesn't match drop down for cases if changed via RAPI command
  if (evse_flag & 0x0001) {                                  // service level flag 1 = level 2, 0 - level 1
   for (int index = minamp; index <= maxamp; index+=2) {
     if (index == pilotamp) {
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
       found_default = 1;
     }
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
    }
  }
  else {
    for (int index = minamp; index <= maxamp; ++index)  {
     if (index == pilotamp) {
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
       found_default = 1;
     }
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
    }
  }
  if (!found_default)
    s += "<OPTION value='" + String(pilotamp) + "'SELECTED>" + String(pilotamp) + "</OPTION>";
  s += "</SELECT>&nbsp;A</P>";
  s += "<P>EVSE: <INPUT TYPE='radio' NAME='evse' VALUE='enable' ";
  if (!evse_disabled && !sleep)
    s += "CHECKED ";
  s += ">enable <INPUT TYPE='radio' NAME='evse' VALUE='disable' ";
  if ((evse_disabled == 1) && !sleep)
    s += "CHECKED ";
  s += ">disable <INPUT TYPE='radio' NAME='evse' VALUE='sleep' ";
  if (sleep)
    s += "CHECKED ";
  s += ">sleep <INPUT TYPE='radio' NAME='evse' VALUE='reset' ";
  s += ">reset </FONT></FONT></P>";
  s += "&nbsp;<TABLE><TR>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Submit    '></TD>";
  s += "</FORM>";
  s += "<FORM ACTION='advanced'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='   Advanced   '></TD>";
  s += "</FORM>";
  s += "<FORM ACTION='.'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='WiFi Configuration'></TD>";
  s += "</FORM>";
  s += "<FORM ACTION='rapi'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='     RAPI     '></TD>";
  s += "</FORM>";
  s += "<FORM ACTION='home'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Cancel    '></TD>";
  s += "</FORM>";
  s += "</TR></TABLE>";
  if (status_path != 0 && wifi_mode == 0) {
    s += "<P>-------</P>";
    s += "<P><A href='";
    s += status_path;
    s += "'>Dashboard</A></P>";
  }
  s += "</HTML>";
  s += "\r\n\r\n";
  //Serial.println("sending page...");
  server.send(200, "text/html", s);
  //Serial.println("page sent!");
}

void setup() {
	delay(1000);
	Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(0, INPUT);
  char tmpStr[40];

  esid = readEEPROM(SSID_START, SSID_MAX_LENGTH);
  epass = readEEPROM(PASS_START, PASS_MAX_LENGTH);
  privateKey  = readEEPROM(KEY_START, KEY_MAX_LENGTH);
  privateKey2  = readEEPROM(KEY2_START, KEY2_MAX_LENGTH);
  node = readEEPROM(NODE_START, NODE_MAX_LENGTH);
  host = readEEPROM(HOST_START, HOST_MAX_LENGTH);
  host2 = readEEPROM(HOST2_START, HOST2_MAX_LENGTH);
  directory = readEEPROM(DIRECTORY_START, DIRECTORY_MAX_LENGTH);
  directory2 = readEEPROM(DIRECTORY2_START, DIRECTORY2_MAX_LENGTH);
  status_path = readEEPROM(STAT_START, STAT_MAX_LENGTH);
  String sTmpStr = "0";
  sTmpStr = readEEPROM(PNOTIFY_START, PNOTIFY_MAX_LENGTH);
  plug_notify = sTmpStr.toInt();
  sTmpStr = "0";
  sTmpStr = readEEPROM(PHOUR_START, PHOUR_MAX_LENGTH);
  plug_hour = sTmpStr.toInt();
  sTmpStr = "0";
  sTmpStr = readEEPROM(PMIN_START, PMIN_MAX_LENGTH);
  plug_min = sTmpStr.toInt();
  sTmpStr = "0";
  sTmpStr = readEEPROM(PREPEAT_START, PREPEAT_MAX_LENGTH);
  plug_repeat = sTmpStr.toInt();
  sTmpStr = "0";
  sTmpStr = readEEPROM(CNOTIFY_START, CNOTIFY_MAX_LENGTH);
  charge_notify = sTmpStr.toInt();
  sTmpStr = "0";
  sTmpStr += readEEPROM(CHOUR_START, CHOUR_MAX_LENGTH);
  charge_hour = sTmpStr.toInt();
  sTmpStr = "0";
  sTmpStr = readEEPROM(CMIN_START, CMIN_MAX_LENGTH);
  charge_min = sTmpStr.toInt();
  sTmpStr = "0";
  sTmpStr = readEEPROM(CREPEAT_START, CREPEAT_MAX_LENGTH);
  charge_repeat = sTmpStr.toInt();
  sTmpStr = readEEPROM(NOTIFY_FLAGS_START, NOTIFY_FLAGS_MAX_LENGTH);
  notify_flags = sTmpStr.toInt();
  p_notify_sent = (notify_flags >> P_NOTIFY_SHIFT) & NOTIFY_MASK;
  p_ready = (notify_flags >> P_READY_SHIFT) & NOTIFY_MASK;
  c_notify_sent = (notify_flags >> C_NOTIFY_SHIFT) & NOTIFY_MASK;
  c_ready = (notify_flags >> C_READY_SHIFT) & NOTIFY_MASK;
  sTmpStr = readEEPROM(REMINDERS_START, REMINDERS_MAX_LENGTH);
  reminders = sTmpStr.toInt();
  p_reminders = (reminders >> P_REMINDERS_SHIFT) & REMINDERS_MASK;
  c_reminders = (reminders >> C_REMINDERS_SHIFT) & REMINDERS_MASK;
  
  // go ahead and make a list of networks in case user needs to change it
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(1000);
  int n = WiFi.scanNetworks();
  Serial.print(n);
  Serial.println(" networks found");
  st = "<SELECT name='ssid'>";
  int found_match = 0;
  delay(1000);
  for (int i = 0; i < n; ++i) {
    st += "<OPTION value='";
    st += String(WiFi.SSID(i)) + "'";
    if (String(WiFi.SSID(i)) == esid.c_str()) {
      found_match = 1;
      Serial.println("found match");
      st += "SELECTED";
    }
    st += "> " + String(WiFi.SSID(i));
    st += " </OPTION>";
  }
  if (!found_match)
    if (esid != 0) {
      st += "<OPTION value='" + esid + "'SELECTED>" + esid + "</OPTION>";
    }
    else {
      if (!n)
        st += "<OPTION value='not chosen'SELECTED> No Networks Found!  Select Rescan or Manually Enter SSID</OPTION>";
      else
        st += "<OPTION value='not chosen'SELECTED> Choose One </OPTION>";
    }
  st += "</SELECT>";
  delay(100);
     
  if ( esid != 0 ) { 
    //Serial.println(" ");
    //Serial.print("Connecting as Wifi Client to: ");
    //Serial.println(esid);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(esid.c_str(), epass.c_str());
    delay(50);
    int t = 0;
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED) {
      // test esid
      //Serial.print("#");
      delay(500);
      t++;
      if (t >= 20) {
        //Serial.println(" ");
        //Serial.println("Trying Again...");
        delay(2000);
        WiFi.disconnect();
        WiFi.begin(esid.c_str(), epass.c_str());
        t = 0;
        attempt++;
        if (attempt >= 5) {
          //Serial.println();
          //Serial.print("Configuring access point...");
          WiFi.mode(WIFI_STA);
          WiFi.disconnect();
          delay(100);
          int n = WiFi.scanNetworks();
          //Serial.print(n);
          //Serial.println(" networks found");
          delay(1000);
          st = "<SELECT name='ssid'><OPTION value='not chosen'SELECTED> Try again </OPTION>";
          esid = ""; // clears out esid in case only the password is incorrect-used only to display the right instructions to user
          for (int i = 0; i < n; ++i) {
            st += "<OPTION value='";
            st += String(WiFi.SSID(i)) + "'> " + String(WiFi.SSID(i));
            st += " </OPTION>";
          }
          st += "</SELECT>";
          delay(100);
          WiFi.softAP(ssid, password);
          IPAddress myIP = WiFi.softAPIP();
          //Serial.print("AP IP address: ");
          //Serial.println(myIP);
          Serial.println("$FP 0 0 SSID...OpenEVSE.");
          delay(100);
          Serial.println("$FP 0 1 PASS...openevse.");
          delay(5000);
          Serial.println("$FP 0 0 IP_Address......");
          delay(100);
          sprintf(tmpStr,"$FP 0 1 %d.%d.%d.%d",myIP[0],myIP[1],myIP[2],myIP[3]);
          Serial.println(tmpStr);
          wifi_mode = 1;
          break;
        }
      }
    }
  }
  else {
    //Serial.println();
    //Serial.print("Configuring access point...");
    delay(100);
    WiFi.softAP(ssid, password);
    IPAddress myIP = WiFi.softAPIP();    
    //Serial.print("AP IP address: ");
    //Serial.println(myIP);
    Serial.println("$FP 0 0 SSID...OpenEVSE.");
    delay(100);
    Serial.println("$FP 0 1 PASS...openevse.");
    delay(5000);
    Serial.println("$FP 0 0 IP_Address......");
    delay(100);
    sprintf(tmpStr,"$FP 0 1 %d.%d.%d.%d",myIP[0],myIP[1],myIP[2],myIP[3]);
    Serial.println(tmpStr);     
    wifi_mode = 2; //AP mode with no SSID in EEPROM
  }
	
	if (wifi_mode == 0) {
    //Serial.println(" ");
    //Serial.println("Connected as a Client");
    IPAddress myAddress = WiFi.localIP();
    //Serial.println(myAddress);
    Serial.println("$FP 0 0 Client-IP.......");
    delay(100);
    sprintf(tmpStr,"$FP 0 1 %d.%d.%d.%d",myAddress[0],myAddress[1],myAddress[2],myAddress[3]);
    Serial.println(tmpStr);
  }
  server.on("/confirm", handleCfm);
  server.on("/rescan", handleRescan);
	server.on("/", handleRoot);
  server.on("/a", handleCfg);
  server.on("/r", handleRapiR);
  server.on("/reset", handleRst);
  server.on("/rapi", handleRapi);
  server.on("/home", handleHome);
  server.on("/homeR", handleHomeR);
  server.on("/advanced", handleAdvanced);
  server.on("/advancedR", handleAdvancedR);
  server.on("/datetime", handleDateTime);
  server.on("/datetimeR", handleDateTimeR);
  server.on("/delaytimer", handleDelayTimer);
  server.on("/delaytimerR", handleDelayTimerR);
  server.on("/startimmediatelyR", handleStartImmediatelyR);
	server.begin();
	Serial.println("HTTP server started");
  Timer = millis();
  Timer5 = millis();
}


void loop() {
 server.handleClient();
 int erase = 0;
 long reset_timer;
  long reset_timer2;

  buttonState = digitalRead(0);
  while (buttonState == LOW) {
    buttonState = digitalRead(0);
    erase++;
    if (erase >= 15000) {  //increased the hold down time before erase
      ResetEEPROM(0, SSID_MAX_LENGTH + PASS_MAX_LENGTH); // only want to erase ssid and password
      int erase = 0;
      WiFi.disconnect();
      Serial.print("Finished...");
      delay(2000);
      ESP.reset();
    } 
  }
  if (wifi_mode == 0 && (millis() - Timer5 >= NOTIFICATION_POLLING_RATE)) {
    Timer5 = millis();
    //Serial.println("inside notify check");
    if (plug_notify || charge_notify)  {
      // get current state
      int vflag = getRapiVolatile();
      int evse_state = getRapiEVSEState();
      // calculate current time
      getRapiDateTime();
      int total_minutes = hour*60 + minutes;
      if (total_minutes > (23*60 + 55)) {
        p_ready = 1;
        c_ready = 1;
        update_and_save_notify_flags();
      }
      if (plug_notify && p_ready) {
        // determine if current time is passed the plug in notification time
        int plug_in_notification_time = plug_hour*60 + plug_min*10;
        if (!(vflag & PLUG_IN_MASK)) {   
          if ((total_minutes >= plug_in_notification_time) && !p_notify_sent) {
            p_notify_sent = 1;
            start_sent_timer_p = millis();
            //send notify flag;
            send_notify_data(1, 0);
            if (!plug_repeat){
              p_ready = 0;
              p_notify_sent = 0;
            }
            update_and_save_notify_flags();
          }
          if (p_notify_sent && ((millis()- start_sent_timer_p) >(plug_repeat * 5 * 60000)) && (plug_repeat != 0)) {
            start_sent_timer_p = millis();
            p_reminders++;
            send_notify_data(p_reminders + 1, 0);
            if (p_reminders >= MAX_REMINDERS) {   // send max reminders
              p_ready = 0;
              p_notify_sent = 0;
              p_reminders = 0;
            }
            // send reminder notify flag
            update_and_save_notify_flags();
          }
        }
        else {
          p_notify_sent = 0;
          p_ready = 0;
          p_reminders = 0;
          update_and_save_notify_flags();
        }
      }
      if (charge_notify && c_ready) {
        // determine if current time is passed the charging notification time
        int charge_notification_time = charge_hour*60 + charge_min*10;
        if (evse_state != 3) {
          if ((total_minutes >= charge_notification_time) && !c_notify_sent) {
            c_notify_sent = 1;
            start_sent_timer_c = millis();
            //send notify flag;
            send_notify_data(0, 1);
            if (!charge_repeat){
              c_ready = 0;
              c_notify_sent = 0;
            }
            update_and_save_notify_flags();
          }
          if (c_notify_sent && (millis()- start_sent_timer_c >(charge_repeat * 5 * 60000)) && (charge_repeat != 0)) {
            start_sent_timer_c = millis();
            c_reminders++;
            send_notify_data(0, c_reminders + 1);
            if (c_reminders >= MAX_REMINDERS) {   // send max reminders
              c_ready = 0;
              c_notify_sent = 0;
              c_reminders = 0;
            }
            // send reminder notify flag
            update_and_save_notify_flags();
          }
        }
        else {
          c_notify_sent = 0;
          c_ready = 0;
          c_reminders = 0;
          update_and_save_notify_flags();
        }
      }
    }
  }
  if (wifi_mode == 0 && privateKey != 0) { 
     if ((millis() - Timer) >= SERVER_UPDATE_RATE) {
       Timer = millis();
       Serial.flush();
       Serial.println("$GE*B0");
       delay(100);
       while (Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
         if ( rapiString.startsWith("$OK ") ) {
           String qrapi; 
           qrapi = rapiString.substring(rapiString.indexOf(' '));
           pilot = qrapi.toInt();
         }
       }
       Serial.flush();
       Serial.println("$GG*B2");
       delay(100);
       while (Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
         if ( rapiString.startsWith("$OK") ) {
           String qrapi;
           qrapi = rapiString.substring(rapiString.indexOf(' '));
           amp = qrapi.toInt();
           String qrapi1;
           qrapi1 = rapiString.substring(rapiString.lastIndexOf(' '));
           volt = qrapi1.toInt();
         }
      }
      delay(100);
      Serial.flush();
      Serial.println("$GP*BB");
      delay(100);
      while (Serial.available()) {
        String rapiString = Serial.readStringUntil('\r');
        if (rapiString.startsWith("$OK") ) {
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          temp1 = qrapi.toInt();
          String qrapi1;
          int firstRapiCmd = rapiString.indexOf(' ');
          qrapi1 = rapiString.substring(rapiString.indexOf(' ', firstRapiCmd + 1 ));
          temp2 = qrapi1.toInt();
          String qrapi2;
          qrapi2 = rapiString.substring(rapiString.lastIndexOf(' '));
          temp3 = qrapi2.toInt();
        }
      }  
      // We now create a URL for OpenEVSE RAPI data upload request
      String url;
      String url2;
      String tmp;
      String url_amp = inputID_AMP;
      url_amp += amp;
      url_amp += ",";
      String url_volt = inputID_VOLT;
      url_volt += volt;
      url_volt += ",";
      String url_temp1 = inputID_TEMP1;
      url_temp1 += temp1;
      url_temp1 += ",";
      String url_temp2 = inputID_TEMP2;
      url_temp2 += temp2;
      url_temp2 += ","; 
      String url_temp3 = inputID_TEMP3;
      url_temp3 += temp3;
      url_temp3 += ","; 
      String url_pilot = inputID_PILOT;
      url_pilot += pilot;
      tmp = e_url;
      tmp += node; 
      tmp += "&json={"; 
      tmp += url_amp;
      if (volt <= 0) {
        tmp += url_volt;
      }
      if (temp1 != 0) {
        tmp += url_temp1;
      }
      if (temp2 != 0) {
        tmp += url_temp2;
      }
      if (temp3 != 0) {
        tmp += url_temp3;
      }
      tmp += url_pilot;
    
      tmp += "}&"; 
      url = directory.c_str();   //needs to be constant character to filter out control characters padding when read from memory
      url += tmp;
      url2 = directory2.c_str();    //needs to be constant character to filter out control characters padding when read from memory
      url2 += tmp;
      url += privateKey.c_str();    //needs to be constant character to filter out control characters padding when read from memory
      url2 += privateKey2.c_str();  //needs to be constant character to filter out control characters padding when read from memory
    
      // Use WiFiClient class to create TCP connections
      WiFiClient client;
      const int httpPort = 80;  
      if (server2_down && (millis()-reset_timer2) > 600000)   //retry in 10 minutes
        server2_down = 0;  
      if (!server2_down) {  
        if (!client.connect(host2.c_str(), httpPort)) { //needs to be constant character to filter out control characters padding when read from memory
          server2_down = 1;  
          reset_timer2 = millis();
        }
        else { 
          client.print(String("GET ") + url2 + " HTTP/1.1\r\n" + "Host: " + host2.c_str() + "\r\n" + "Connection: close\r\n\r\n");
          delay(10);
          while (client.available()) {
            String line = client.readStringUntil('\r');
          }
        }
      }    
      if (server_down && (millis()-reset_timer) > 600000)   //retry in 10 minutes 
        server_down = 0;  
      if (!server_down) { 
        if (!client.connect(host.c_str(), httpPort)) { //needs to be constant character to filter out control characters padding when read from memory    
          server_down = 1; 
          reset_timer = millis();
        }
        else {
          // This will send the request to the server
          client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + host.c_str() + "\r\n" + "Connection: close\r\n\r\n");
          delay(10);
          while (client.available()) {
            String line = client.readStringUntil('\r');
          }
        }
      }
      //Serial.println(host);
      //Serial.println(url);
    }
  }
}
