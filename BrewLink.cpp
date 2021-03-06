#include "BrewLink.h"
#include "Brewpi.h"


/***********************************************************************
**
** Note: Currently this object only allows a single TCP connection.  No
** new connections will be processed while that connection is active.
** This is due to the TCPServer code in wiring using a static TCPClient
** object.  This means that TCPServer does not support multiple concurrent
** connections->
**
***********************************************************************/

String charToString(char *cstr){
	if(cstr!=NULL) {
		char buf[40];
		char *pos=cstr;
		int i=0;
		while (i<38 && pos!=NULL && *pos!='\0') {
			buf[i]=*pos;
			pos++;
			i++;
		}
		buf[i]='\0';
		String str=String(buf);
	}
	return String("");
}



/***********************************************************************
**
** Function: BrewLink
** Purpose: BrewLink object constructor
**
***********************************************************************/
BrewLink::BrewLink(){
	Serial.begin(9600);
	//wifiserver = new TCPServer(8080);
	wifiserver=NULL;
	//clients=NULL;
	serialBufPos=0;
	//singleClientBufPos=0;
}

/***********************************************************************
**
** Function: ~BrewLink
** Purpose: BrewLink object destructor
**
***********************************************************************/
BrewLink::~BrewLink(){
	if (wifiserver!=NULL)
		delete wifiserver;
	Serial.end();
	//BrewLinkClient *currclient=clients;
	//BrewLinkClient *prevclient;
	//while (currclient!=NULL) { // clean up any still attached client structs
	//	currclient->client->flush();
	//	currclient->client->stop();  //gracefully disconnect
	//	prevclient=currclient;
	//	currclient=currclient->next;
	//	free(prevclient);
	//}
}

/***********************************************************************
**
** Function: BrewLink::begin
** Purpose: Start BrewLink TCP server
**
***********************************************************************/
void BrewLink::begin(){
	wifiserver = new TCPServer(8080);
	wifiserver->begin();
}

template<typename C>
bool makeDisconnected(C& connection)
{
	//unsigned int deltaTime = millis() - connection.lastHeard;
	//if (deltaTime>TCPTIMEOUT) {
		IPAddress clientIP = connection.client.remoteIP();
		bLink->printDebug("Disconnecting client %d.%d.%d.%d. Exceeded timeout of %d seconds",clientIP[0],clientIP[1],clientIP[2],clientIP[3], TCPTIMEOUT);
		connection.client.flush();
		connection.client.stop();  //gracefully disconnect
	//}
	return !connection.client.connected();
}

void BrewLink::stop(){
    clients.erase(std::remove_if(clients.begin(), clients.end(), makeDisconnected<struct BrewLinkClient>), clients.end());
	bLink->printDebug("stopping wifiserver(%x)",wifiserver);
	delay(1000);
    wifiserver->stop();
	bLink->printDebug("wifiserver stopped");
	delay(1000);
	delete wifiserver;
	wifiserver=NULL;
}


template<typename C>
bool isDisconnected(C& connection)
{
	unsigned int deltaTime = millis() - connection.lastHeard;
	if (deltaTime>TCPTIMEOUT) {
		IPAddress clientIP = connection.client.remoteIP();
		bLink->printDebug("Disconnecting client %d.%d.%d.%d. Exceeded timeout of %d seconds",clientIP[0],clientIP[1],clientIP[2],clientIP[3], TCPTIMEOUT);
		connection.client.flush();
		connection.client.stop();  //gracefully disconnect
	}
	return !connection.client.connected();
}



/***********************************************************************
**
** Function: BrewLink::receive
** Purpose: checks for waiting input from any client that needs to
** be processed.
**
***********************************************************************/
void BrewLink::receive(){
	//first check the serial connection for input
	//printDebug("recieve started");
	char *replybuf=NULL;
	while (Serial.available()) {
		char inByte = Serial.read();
		Serial.write(inByte); //echo on
		if (inByte=='\n' || inByte=='\r') {  //Commands terminate with a newline or carrage return
			serialCmdBuf[serialBufPos]='\0'; //terminate C string
			replybuf=processCmd(serialCmdBuf);
			if (replybuf!=NULL) {
				Serial.println(replybuf);
				free(replybuf);
				replybuf=NULL;
			}
			serialBufPos=0;
			while (inByte=='\n'|| inByte=='\r') { //flush the buffer to the start of the next command
				inByte = Serial.read();
			}
		} else {
			serialCmdBuf[serialBufPos]=inByte;
			serialBufPos++;
		}
	}


	/* If TCPServer supported concurent connections we'd uncomment this
	//now loop though all connected TCP clients
	BrewLinkClient *currclient=clients;
	while (currclient!=NULL) {
		//printDebug("Checking for data from client %x",currclient);
		checkClient(currclient);
		currclient=currclient->next;
	}
	//check for new connections
	TCPClient newcl=wifiserver->available();  //TCPServer.available always returns an object, so we have to grab it and check if its a valid connection
	if (newcl.connected()) {
		printDebug("TCP Connection recieved from %x.", (int) &newcl);
		BrewLinkClient *newclient=(BrewLinkClient *)malloc(sizeof(BrewLinkClient));
		if (newclient!=NULL) { // ensure successful malloc
			newclient->lastHeard=millis();
			newclient->client=newcl;
			newclient->next=clients;
			clients=newclient;
		}
	}
	*/

	if (wifiserver) { // only process wifi clients if wifi server is active.
	// check for new clients
	struct BrewLinkClient bpiclient;
	bpiclient.client = wifiserver->available();
	if (bpiclient.client.connected()) {
		IPAddress clientIP = bpiclient.client.remoteIP();
		bpiclient.bufPos=0;
		bpiclient.lastHeard=millis();
		clients.push_back(bpiclient);
		printDebug("New client connected from %d.%d.%d.%d.  %d total clients connected.",clientIP[0],clientIP[1],clientIP[2],clientIP[3],clients.size());
	}
	// remove disconnected clients
    clients.erase(std::remove_if(clients.begin(), clients.end(), isDisconnected<struct BrewLinkClient>), clients.end());


	// process client streams
	for(std::vector<BrewLinkClient>::size_type i = 0; i != clients.size(); i++) {
		receiveTCPClient(&clients[i]);
	}
	}

}

void BrewLink::receiveTCPClient(BrewLinkClient *client){
	TCPClient *singleClient=&(client->client);
	char *singleClientCmdBuf=client->cmdBuf;
	unsigned int singleClientBufPos=client->bufPos;
	char *replybuf=NULL;


	if (singleClient->connected()) {
		int bytesRead=0;
		while (singleClient->available()) {
			char inByte = singleClient->read();
			//printDebug("read %c",inByte);
			IPAddress clientIP = singleClient->remoteIP();
			printDebug("Processing char %c from client %d.%d.%d.%d",inByte,clientIP[0],clientIP[1],clientIP[2],clientIP[3]);
			if (inByte=='\n'|| inByte=='\r') {  //Commands terminate with a newline
				singleClientCmdBuf[singleClientBufPos]='\0'; //terminate C string
				replybuf=processCmd(singleClientCmdBuf);
				if (replybuf!=NULL) {
					singleClient->println(replybuf);
					free(replybuf);
					replybuf=NULL;
				}
				singleClientBufPos=0;
				while (inByte=='\n'|| inByte=='\r') { //flush the buffer to the start of the next command
					inByte = singleClient->read();
				}
			} else {
				singleClientCmdBuf[singleClientBufPos]=inByte;
				singleClientBufPos++;
			}
			bytesRead++;
		}
		if (bytesRead>0) {
			client->lastHeard=millis();
		}
	}

	client->bufPos=singleClientBufPos;
}



/***********************************************************************
**
** Function: BrewLink::printDebug
** Purpose: prints debug messages to serial.  We wrap this call to make
** future modifications to how we print debug messages easier.
**
***********************************************************************/
void BrewLink::printDebug(const char *format, ...) {
	char buf[2048];
	va_list args;
	va_start(args,format);
	int sizeneeded=vsnprintf(buf,2048,format,args);
	Serial.println(buf);
	piNet.log(buf);
	Serial.flush(); //wait for buffers to flush.  We want to make sure debug gets printed before a possible crash
	if (sizeneeded>2047){
		printDebug("Error: Serial Buffer Overflow.  Buffer is %d, String was %d characters",2048,sizeneeded);
	}
	va_end(args);
}

/***********************************************************************
**
** Function: BrewLink::processCmd
** Purpose: parse and act on incoming command strings
**
***********************************************************************/
char *BrewLink::processCmd(char *cmd) {
	char *reply;
	printDebug("Recieved command: %s",cmd);
	IPAddress localIP;
	byte mac[6];
	//JSONObj *json;
	//JSONElement *jsonel;
	switch (*cmd) {
	case 'i':
		/*localIP = WiFi.localIP();
		WiFi.macAddress(mac);
		reply=(char *)malloc(sizeof(char)*(strlen(cmd)+45));
//			           0         1         2         3         4
//			           01234567890123456789012345678901234567890
//		               IP:255.255.255.255 MAC: ff:ff:ff:ff:ff:ff
		sprintf(reply,"IP:%d.%d.%d.%d MAC: %0x:%0x:%0x:%0x:%0x:%0x",localIP[0],localIP[1],localIP[2],localIP[3],mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
*/

		reply=cmdNetwork(cmd);

		break;
	case 'd':  //perform device search or update devices
		reply=cmdDeviceSearch(cmd);
		break;
	case 'p': // list all PIDs or update PIDs
		reply=cmdPIDs(cmd);
		break;
	case 'c': // list all connections
		reply=cmdConn(cmd);
		break;
	case 'l': //status command (LCD command)
		reply=cmdStatus();
		break;
	case 's': //set PID set point
		reply=cmdSet(cmd);
		break;
	case 't': // toggle GPIO
		reply=cmdToggle(cmd);
		break;
	case 'w':
		//EEPROM.clear();  //not avail in 0.4.6
		storage->clear();
		reply=storage->write();
		break;
	case 'r':
		reply=storage->read();
		break;
	case 'e':
		reply=NULL;
		cmd++;
		if (*cmd=='c') {
			storage->clear();
		} else {
			storage->dump();
		}
		break;
	case 'x':
	case 'f':
		System.dfu();
		break;
		//singleClient.flush();
		//singleClient.stop();
		break;
	default:
		reply=(char *)malloc(sizeof(char)*(strlen(cmd)+20));
//			           0         1         2         3         4
//			           01234567890123456789012345678901234567890
		sprintf(reply,"Unknown Cmd: %s",cmd);
	}
	return reply;
}

char *BrewLink::cmdDeviceSearch(char *cmd){
		JSONObj *json;
		char *reply=NULL;
		char *jsontxt=cmd;
		jsontxt++;
		if(*jsontxt!='\0') {
		json=new JSONObj(jsontxt);
		// {"Devices":["28a1da7f08000081":{"Name":"Ferm1","CorF":1}]}

		if (json!=NULL) { // then we got some change orders to process
			JSONElement *jsonel=json->getFirstElement();
			if (jsonel!=NULL) {
				JSONArray *jsonarray=jsonel->getValueAsArray();
				if(jsonarray!=NULL) {
					JSONArrayElement *arrayel=NULL;
					bLink->printDebug("Looping Through Device Array :%s", jsonarray->jstringify());
					while(jsonarray->getNextElement(&arrayel)) {
						bLink->printDebug("Looping...:%s",arrayel->jstringify());
						delay(1000);
						JSONObj *conncmd=arrayel->getValueAsObj();
						if (conncmd!=NULL) {
						bLink->printDebug("conncmd:%s",conncmd->jstringify());
						delay(1000);
						jsonel=conncmd->getFirstElement();
						if (jsonel!=NULL) {
						bLink->printDebug("Getting device %s", jsonel->getName());
						delay(1000);
						Device *dev=deviceManager->getDevice(jsonel->getName());
						if(dev!=NULL) {
							JSONObj * devupdate=jsonel->getValueAsObj();
							if (devupdate!=NULL) {
								reply=(char *)malloc(sizeof(char)*(strlen(jsonel->getName())+16));
								//			   0         1         2         3         4
								//			   01234567890123456789012345678901234567890
								sprintf(reply,"Device Updated: %s",jsonel->getName());
								bLink->printDebug("Setting Device Name");
								jsonel=devupdate->getElement("Name");
								if (jsonel!=NULL) {
									dev->setName(jsonel->getValue());
									dev->setStrName(String(jsonel->getValue()));
								}
								switch(dev->getDeviceHardware()) {
									case DEVICE_HARDWARE_ONEWIRE_TEMP:
										//CorF
										bLink->printDebug("Setting Device CorF");

										jsonel=devupdate->getElement("CorF");
										if (jsonel!=NULL) {
											((OneWireTempSensor *)dev)->setCorF((TempFormat)strtod(jsonel->getValue(),NULL));
										}
										break;
									case DEVICE_HARDWARE_PIN:
										//gpioMode
										bLink->printDebug("Setting Device gpioMode (HW)");

										jsonel=devupdate->getElement("gpioMode");
										if (jsonel!=NULL) {
											((HardwareGPIO *)dev)->setGPIOMode((PinMode)strtod(jsonel->getValue(),NULL));
										}
										break;
									case DEVICE_HARDWARE_ONEWIRE_2413:
										//gpioMode
										bLink->printDebug("Setting Device gpioMode (OneWire)");

										jsonel=devupdate->getElement("gpioMode");
										if (jsonel!=NULL) {
											((OneWireGPIO *)dev)->setGPIOMode((PinMode)strtod(jsonel->getValue(),NULL));
										}
										break;
								}

							}
						}
						}
						}
					}
				}else {
					reply=(char *)malloc(sizeof(char)*(strlen(cmd)+30));
			//			           0         1         2         3         4
			//			           01234567890123456789012345678901234567890
					sprintf(reply,"Unable to parse command: %s",cmd);
				}
			}else {
				reply=(char *)malloc(sizeof(char)*(strlen(cmd)+30));
		//			           0         1         2         3         4
		//			           01234567890123456789012345678901234567890
				sprintf(reply,"Unable to parse command: %s",cmd);
			}
		}
		delete json;
		}
		if(reply==NULL) {
			reply=deviceManager->deviceSearch();
			if (reply==NULL) {
				reply=(char *)malloc(sizeof(char)*40);
		//			               0         1         2         3         4
		//			               01234567890123456789012345678901234567890
				sprintf(reply,"Device manager returned no results");

			}
		}
		return reply;

}

char *BrewLink::cmdStatus(){
	JSONObj *json=deviceManager->status();
	if (json!=NULL) {
		char *reply=json->jstringify();
		delete json;
		if (reply!=NULL) {
			printDebug("reply: %s",reply);
		} else {
			printDebug("cmdStatus reply returned NULL!!!");
		}
		return reply;
	} else {
		printDebug("cmdStatus JSON returned NULL!!!");
		return NULL;
	}
}

char *BrewLink::cmdToggle(char *cmd){
	char * reply;
	char *jsontxt=cmd;
	Device *gpio=NULL;

	jsontxt++;
	JSONObj *json=new JSONObj(jsontxt);
	if (json!=NULL) {
		JSONElement *jsonel=json->getElement("dev");
		//printDebug("Getting device %s",jsonel->getValue());
		gpio=deviceManager->getDevice(jsonel->getValue());
		if (gpio!=NULL) {
			if(gpio->getValue()==LOW) {
				gpio->setOutput(HIGH);
				reply=(char *)malloc(sizeof(char)*30);
				sprintf(reply,"Set %s HIGH",jsonel->getValue());
			} else {
				gpio->setOutput(LOW);
				reply=(char *)malloc(sizeof(char)*30);
				//			   0         1         2         3         4
				//			   01234567890123456789012345678901234567890
				//             Set 3af51321000000cb1 HI/?
				sprintf(reply,"Set %s LOW",jsonel->getValue());				}
		} else {
			reply=(char *)malloc(sizeof(char)*(strlen(cmd)+20));
	//			           0         1         2         3         4
	//			           01234567890123456789012345678901234567890
			sprintf(reply,"Unknown Device: %s",cmd);
			//printDebug("reply: %s",reply);
		}
	} else {
		reply=(char *)malloc(sizeof(char)*(strlen(cmd)+30));
//			           0         1         2         3         4
//			           01234567890123456789012345678901234567890
		sprintf(reply,"Unable to parse command: %s",cmd);
		//printDebug("reply: %s",reply);
	}
	delete json;
	return reply;
}

char *BrewLink::cmdSet(char *cmd){
	char * reply;
	char *jsontxt=cmd;
	PID *temppid=NULL;

	jsontxt++;
	JSONObj *json=new JSONObj(jsontxt);
	if (json!=NULL) {
		//printDebug("parsed JSON");
		//delay(1000);
		JSONElement *jsonel=json->getFirstElement();
		if (jsonel!=NULL) {
			//printDebug("got first element 0x%x", (int) jsonel);
			//delay(1000);
			temppid=pids->getPID(jsonel->getName());
			//printDebug("got pid 0x%x", (int) temppid);
			//delay(1000);
			if (temppid!=NULL) {
				temppid->setSetPoint(strtod(jsonel->getValue(),NULL));
				/* old hard coded in EEPROM config writes
				if (!strcmp(jsonel->getName(),"Fermenter1")) {
					EEPROM.write(0,(uint8_t)strtod(jsonel->getValue(),NULL));
				}
				if (!strcmp(jsonel->getName(),"Fermenter2")) {
					EEPROM.write(1,(uint8_t)strtod(jsonel->getValue(),NULL));
				}
				if (!strcmp(jsonel->getName(),"Glycol")) {
					EEPROM.write(2,(uint8_t)strtod(jsonel->getValue(),NULL));
				}
				*/
				//printDebug("setSetpoint");
				//delay(1000);
				reply=(char *)malloc(sizeof(char)*(15+strlen(jsonel->getName())+strlen(jsonel->getValue())));
					//			   0         1         2         3         4
					//			   01234567890123456789012345678901234567890
					sprintf(reply,"Set PID %s=%s",jsonel->getName(),jsonel->getValue());
			} else {
				reply=(char *)malloc(sizeof(char)*(strlen(cmd)+20));
	//			           0         1         2         3         4
	//			           01234567890123456789012345678901234567890
				sprintf(reply,"Unknown PID: %s",cmd);
				//printDebug("reply: %s",reply);
			}
		} else {
			reply=(char *)malloc(sizeof(char)*(strlen(cmd)+30));
	//			           0         1         2         3         4
	//			           01234567890123456789012345678901234567890
			sprintf(reply,"Unable to parse command: %s",cmd);
			//printDebug("reply: %s",reply);
		}
	} else {
		reply=(char *)malloc(sizeof(char)*(strlen(cmd)+30));
//			           0         1         2         3         4
//			           01234567890123456789012345678901234567890
		sprintf(reply,"Unable to parse command: %s",cmd);
		//printDebug("reply: %s",reply);
	}
	delete json;
	return reply;
}

char *BrewLink::cmdConn(char *cmd) {
	JSONObj *json;
	char *reply=NULL;
	char *jsontxt=cmd;
	jsontxt++;
	if(*jsontxt!='\0') {
	json=new JSONObj(jsontxt);
	if (json!=NULL) { // then we got some change orders to process
		JSONElement *jsonel=json->getFirstElement();
		if (jsonel!=NULL) {
			JSONArray *jsonarray=jsonel->getValueAsArray();
			if(jsonarray!=NULL) {
				JSONArrayElement *arrayel=NULL;
				while(jsonarray->getNextElement(&arrayel)) {
					JSONObj *conncmd=arrayel->getValueAsObj();
					jsonel=conncmd->getFirstElement();
					if(!strcmp("Remove",jsonel->getName())) {
						connections->delConnection(jsonel->getValueAsObj());
					} else {
						connections->addConnection(jsonel->getValueAsObj());
					}
				}
			}else {
				reply=(char *)malloc(sizeof(char)*(strlen(cmd)+30));
		//			           0         1         2         3         4
		//			           01234567890123456789012345678901234567890
				sprintf(reply,"Unable to parse command: %s",cmd);
			}
		}else {
			reply=(char *)malloc(sizeof(char)*(strlen(cmd)+30));
	//			           0         1         2         3         4
	//			           01234567890123456789012345678901234567890
			sprintf(reply,"Unable to parse command: %s",cmd);
		}
	}
	delete json;
	}
	if(reply==NULL) {
		json=connections->jsonify();
		reply=json->jstringify();
		delete json;
	}
	return reply;
}


char *BrewLink::cmdPIDs(char *cmd) {
	JSONObj *json;
	char *reply=NULL;
	char *jsontxt=cmd;
	jsontxt++;
	if(*jsontxt!='\0') {
	json=new JSONObj(jsontxt);
	if (json!=NULL) { // then we got some change orders to process
		JSONElement *jsonel=json->getFirstElement();
		if (jsonel!=NULL) {
			JSONArray *jsonarray=jsonel->getValueAsArray();
			if(jsonarray!=NULL) {
				JSONArrayElement *arrayel=NULL;
				while(jsonarray->getNextElement(&arrayel)) {
					JSONObj *conncmd=arrayel->getValueAsObj();
					jsonel=conncmd->getFirstElement();
					PID *pid=pids->getPID(jsonel->getName());
					//"Ferm1":{"Kp":5.00,"Ki":0.25,"Kd":1.50,"setPoint":100.00,"minStateTime":120,"deadBand":0.5,"PWMScale":1.00},
					if(pid!=NULL) {
						JSONObj * pidupdate=jsonel->getValueAsObj();
						if (pidupdate!=NULL) {
							bLink->printDebug("pidupdate:%s", pidupdate->jstringify());
							jsonel=pidupdate->getElement("Kp");
							if (jsonel!=NULL) {
								pid->setP(strtod(jsonel->getValue(),NULL));
							}
							jsonel=pidupdate->getElement("Ki");
							if (jsonel!=NULL) {
								bLink->printDebug("setI:%s", jsonel->jstringify());
								pid->setI(strtod(jsonel->getValue(),NULL));
							}
							jsonel=pidupdate->getElement("Kd");
							if (jsonel!=NULL) {
								pid->setD(strtod(jsonel->getValue(),NULL));
							}
							jsonel=pidupdate->getElement("setPoint");
							if (jsonel!=NULL) {
								pid->setSetPoint(strtod(jsonel->getValue(),NULL));
							}
							jsonel=pidupdate->getElement("minStateTime");
							if (jsonel!=NULL) {
								pid->setMinStateTime(strtod(jsonel->getValue(),NULL));
							}
							jsonel=pidupdate->getElement("deadBand");
							if (jsonel!=NULL) {
								pid->setDeadBand(strtod(jsonel->getValue(),NULL));
							}
							jsonel=pidupdate->getElement("PWMScale");
							if (jsonel!=NULL) {
								pid->setPWMScale(strtod(jsonel->getValue(),NULL));
							}
						}
					}
				}
			}else {
				reply=(char *)malloc(sizeof(char)*(strlen(cmd)+30));
		//			           0         1         2         3         4
		//			           01234567890123456789012345678901234567890
				sprintf(reply,"Unable to parse command: %s",cmd);
			}
		}else {
			reply=(char *)malloc(sizeof(char)*(strlen(cmd)+30));
	//			           0         1         2         3         4
	//			           01234567890123456789012345678901234567890
			sprintf(reply,"Unable to parse command: %s",cmd);
		}
	}
	delete json;
	}
	if(reply==NULL) {
		json=pids->jsonify();
		if (json!=NULL) {
			reply=json->jstringify();
			delete json;
		}
	}
	return reply;
}

char *BrewLink::cmdNetwork(char *cmd){
	char *reply;
	JSONObj *json;
	char *jsontxt=cmd;
	jsontxt++;
	if(*jsontxt!='\0') {
		json=new JSONObj(jsontxt);
		if (json!=NULL) {
			piNet.setConfig(json);
		}
		delete json;
	}
	json=piNet.jsonify();
	reply=json->jstringify();
	delete json;
	return reply;
}
