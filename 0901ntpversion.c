#include <OneWire.h>
#include <Time.h>
#include <stdio.h>
#include <Ethernet.h>
#include <SPI.h>
#include <EthernetUDP.h>
#include <utility/w5100.h>
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>  

#include <avr/wdt.h>

//#define LCMIIC1
//#define LCMIIC2
#define LCMIIC3

#define APIKEY         "uwrncAdPgoqbv8xjXok4pUyzNTejxxGi0nBPCNdZEvX3bKfW" // replace your xively api key here
#define FEEDID         1091751654 // replace your feed ID
#define USERAGENT      "icebox" // user agent is the project name

/*-----( Declare Constants )-----*/
#if defined(LCMIIC1)
#define I2C_ADDR    0x20  // Define I2C Address 
#define BACKLIGHT_PIN  7
#define En_pin  4
#define Rw_pin  5
#define Rs_pin  6
#define D4_pin  0
#define D5_pin  1
#define D6_pin  2
#define D7_pin  3
#define BACKLIGHT_FLAG  NEGATIVE
#elif defined(LCMIIC2)
#define I2C_ADDR    0x27  // Define I2C Address 
#define BACKLIGHT_PIN  3
#define En_pin  2
#define Rw_pin  1
#define Rs_pin  0
#define D4_pin  4
#define D5_pin  5
#define D6_pin  6
#define D7_pin  7
#define BACKLIGHT_FLAG  POSITIVE
#elif defined(LCMIIC3) 
#define I2C_ADDR    0x20  // Define I2C Address 
#define BACKLIGHT_PIN  3
#define En_pin  2
#define Rw_pin  1
#define Rs_pin  0
#define D4_pin  4
#define D5_pin  5
#define D6_pin  6
#define D7_pin  7
#define BACKLIGHT_FLAG  POSITIVE
#else // error
#error LCM not defined
#endif

#define  LED_OFF  0
#define  LED_ON  1

/*-----( Declare objects )-----*/  
LiquidCrystal_I2C  lcd(I2C_ADDR,En_pin,Rw_pin,Rs_pin,D4_pin,D5_pin,D6_pin,D7_pin);


#define ONE_WIRE_BUS 6

OneWire ds(ONE_WIRE_BUS);

int previousmode = 0;//Exceed Temperature Mode 1 Lower Temperature Mode 0
int relay = 7;//Icebox Temperature Control
//Pin 11 = LED
int relay1 = 11;


// initialize the library with the numbers of the interface pins
byte addr[8];

byte mac[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
char server[] = "api.xively.com";  
EthernetClient client;
int networkfail = 0;//flag network fail , 1 = nonetwork, 0 = get network

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield

unsigned int localPort = 8888;      // local port to listen for UDP packets

IPAddress timeServer(132, 163, 4, 101); // time-a.timefreq.bldrdoc.gov NTP server
// IPAddress timeServer(132, 163, 4, 102); // time-b.timefreq.bldrdoc.gov NTP server
// IPAddress timeServer(132, 163, 4, 103); // time-c.timefreq.bldrdoc.gov NTP server

const int NTP_PACKET_SIZE= 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets 

// A UDP instance to let us send and receive packets over UDP
EthernetUDP Udp;

int lightmode = 0;//indicate current light status in arduino

void setup() 
{
	int retrydhcp = 0;
  	lcd.begin(16, 2);
	lcd.setBacklightPin(BACKLIGHT_PIN,BACKLIGHT_FLAG);
	lcd.setBacklight(LED_ON);
	lcd.print("===v140902-Ex===");
	pinMode(relay, OUTPUT);
	pinMode(relay1, OUTPUT);
	digitalWrite(relay1, HIGH);//default no light
	
	while(Ethernet.begin(mac)==0&&retrydhcp<1)//Fail
	{
//    Serial.println("connecting...");
		retrydhcp++;
	}
        W5100.setRetransmissionTime(0x07D0);
        W5100.setRetransmissionCount(1);

	Udp.begin(localPort);
	setTime(9,0,0,1,1,11); 
	if(retrydhcp >=2)
	{
//    Serial.println("Fail to DHCP , No network mode");
		networkfail=1;
	}
	else
	{//sometimes ntp packet may corrupted
		sendNTPpacket(timeServer); // send an NTP packet to a time server

		// wait to see if a reply is available
		delay(1000);  
		if ( Udp.parsePacket() ) 
		{  
			// We've received a packet, read the data from it
			Udp.read(packetBuffer,NTP_PACKET_SIZE);  // read the packet into the buffer

			//the timestamp starts at byte 40 of the received packet and is four bytes,
			// or two words, long. First, esxtract the two words:

			unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
			unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);  
			// combine the four bytes (two words) into a long integer
			// this is NTP time (seconds since Jan 1 1900):
			unsigned long secsSince1900 = highWord << 16 | lowWord;  

			// now convert NTP time into every
			 time:
			// Unix time starts on Jan 1 1970. In seconds, that's 2208988800: 8 x 60 x 60
			const unsigned long seventyYears = 2208988800UL;     
			// subtract seventy years:
			unsigned long epoch = secsSince1900 - seventyYears + 28800;  

			setTime(epoch);
			//setTime((epoch  % 86400L) / 3600+8,(epoch  % 3600) / 60,epoch %60,14,7,2014); 
		}
  
	}
	lcd.clear();
    wdt_enable(WDTO_8S);
}

int icebox_count = 0;
int notemp_count = 0;
int count = 0;
int reportct = 0;
int notemp = 0;

void loop() 
{
    wdt_reset();
	
  	if(icebox_count>100)
		icebox_count=100;
//Exceed 100 , limit to 100 , the value will be reset later
	if(notemp_count>100)
		notemp_count=100;
	
	int retrysearch = 0;
	int found = 0;
	
	while (retrysearch < 3 && found==0)
	{
		if(!ds.search(addr))
		{
			char bufx[30];
			snprintf(bufx,sizeof(bufx),"add Error %d",count);
			if(count%20==0)
			{
				lcd.clear();
				lcd.setCursor(0, 0);
				lcd.print(bufx);
				if(count>1000) count = 0;
			}
			count++;
			ds.reset_search();
			delay(350);
		}
		else 
		{
			found = 1;
		}
		retrysearch++;
	}
	
	if(found==0)
	{
		notemp=1;
	}
	else
	{
		notemp=0;
		notemp_count=0;
	}
	if(notemp==0)
	{
		//part 1 temperature capture
		byte i;
		byte present = 0;
		byte data[12];
		if ( OneWire::crc8( addr, 7) != addr[7]) 
		{
			lcd.print("CRC Error");
			return;
		}
		ds.reset();
		ds.select(addr);
		ds.write(0x44,1);         // start conversion, with parasite power on at the end

		delay(750);     // maybe 750ms is enough, maybe not
		wdt_reset();
		// we might do a ds.depower() here, but the reset will take care of it.

		ds.reset();
		ds.select(addr);    
		ds.write(0xBE);         // Read Scratchpad

		for ( i = 0; i < 9; i++) 
		{           // we need 9 bytes
			data[i] = ds.read();
		}
		int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract;
		LowByte = data[0];
		HighByte = data[1];
		TReading = (HighByte << 8) + LowByte;
		SignBit = TReading & 0x8000;  // test most sig bit
		if (SignBit) // negative
		{
			TReading = (TReading ^ 0xffff) + 1; // 2's comp
		}
		Tc_100 = (6 * TReading) + TReading / 4;    // multiply by (100 * 0.0625) or 6.25

		Whole = Tc_100 / 100;  // separate off the whole and fractional portions
		Fract = Tc_100 % 100;

		//================
		lcd.setCursor(0, 1);
		if(hour()<23&& hour()>8)//day
		{
			lcd.setBacklight(LED_ON);
			if(Whole<30)
			{
				lightmode = 1;
				digitalWrite(relay1, LOW);
			}
			else
			{
				lightmode = 0;
				digitalWrite(relay1, HIGH);		
			}
			char str[30];
			snprintf(str,sizeof(str),"D %d:%d:%d_%d.%d=============",hour(),minute(),second(),Whole,Fract);
			lcd.print(str);
			int ret = controlRelay(25,data);
			if(ret==0)
			{
				lcd.setCursor(0, 0);
				char strice[30];
/*
		Indicator Iceoff And Cout_Down
		Count_Down to 10 The state will change to IceOn or IceOff
		The Count_Down is to prevent frequent change. Something like 1 ->0 -> 1 ->0
		This will damage the hardware
*/
				snprintf(strice,sizeof(strice),"IcOff:%d.%d",month(),day());
				lcd.print(strice);
			}
			else
			{
				lcd.setCursor(0, 0);
				char strice[30];
				snprintf(strice,sizeof(strice),"IcOn:%d.%d",month(),day());
				lcd.print(strice);
			}
		}
		else//night
		{
			lcd.setBacklight(LED_OFF);
			lightmode=0;
			digitalWrite(relay1, HIGH); 
			char str[30];
			snprintf(str,sizeof(str),"Ng %d:%d:%d_%d.%d$$$$$$$$$$$$$$$$$$",hour(),minute(),second(),Whole,Fract);
			lcd.print(str);
			int ret = controlRelay(17,data);
			if(ret==0)
			{
				lcd.setCursor(0, 0);
				char strice[30];
				snprintf(strice,sizeof(strice),"IcOff%d.%d",month(),day());
				lcd.print(strice);
			}
			else
			{
				lcd.setCursor(0, 0);
				char strice[30];
				snprintf(strice,sizeof(strice),"IcOn=%d.%d",month(),day());
				lcd.print(strice);
			}
		}
	  //Do ethernet
		if(networkfail==0)
		{
			if(reportct%10==0)
			{
				reportct=0;
				doreport(Whole,Fract);
			}
		}
		reportct++;
		//End ethernet
	}
	else
	{
		lcd.setCursor(0, 0);
		lcd.clear();
		lcd.print("NOTEMP");
		notemp_count++;
		if(notemp_count>10)
		{ //trigger ice box on
			digitalWrite(relay1, HIGH); //light off
			lightmode = 0;
			digitalWrite(relay, LOW); 
		}
	}
}
void doreport(int Whole,int Fract)
{
//  Serial.println("connecting...");
	if (client.connect(server, 80)) 
	{
//    Serial.println("connecting...");
    // send the HTTP PUT request:
		client.print("PUT /v2/feeds/");
		client.print(FEEDID);
		client.println(".csv HTTP/1.1");
		client.println("Host: api.xively.com");
		client.print("X-ApiKey: ");
		client.println(APIKEY);
		client.print("User-Agent: ");
		client.println(USERAGENT);
		client.print("Content-Length: ");

		// calculate the length of the sensor reading in bytes:
		// 8 bytes for "sensor1," + number of digits of the data:
		int thisLength = 8 + getLength(Whole)+1+getLength(Fract) +2;
		thisLength += 6 + 1+2;		//"light,"

		if(minute()<10)
			thisLength += 1;		//"artime,"
		thisLength += 7 + getLength(hour()) + 2+getLength(minute())+2;		//"artime,"
		thisLength += 9 + 1 + 2;		//"iceonoff,"

		client.println(thisLength);

		// last pieces of the HTTP PUT request:
		client.println("Content-Type: text/csv");
		client.println("Connection: close");
		client.println();

		// here's the actual content of the PUT request:
		client.print("sensor1,");
		client.print(Whole);
		client.print(".");
		client.println(Fract);

		client.print("light,");
		client.println(lightmode);
		client.print("artime,");

		client.print(hour());//hour
		client.print("00");//00
		if(minute()<10)
			client.println("0");//minutes
		
		client.println(minute());//minutes
		
		client.print("iceonoff,");
		if(previousmode==0)
			client.println(1);
		if(previousmode==1)
			client.println(0);
		
	} 
	else 
	{
	lcd.print("ConF");
    Serial.println("connection failed");
	}

	if (client.available()) 
	{
		char c = client.read();
		lcd.print("Read");
//lcd.print(c);
//    Serial.print(c);
	}

	if (client.connected()) 
	{
//    Serial.println("disconnecting.");
		client.stop();
	}
  
}
int controlRelay(int degreetrigger,  byte* data)
{
	int HighByte, LowByte, TReading, SignBit, Tc_100, Whole;
	LowByte = data[0];
	HighByte = data[1];
	TReading = (HighByte << 8) + LowByte;
	SignBit = TReading & 0x8000;  // test most sig bit
	if (SignBit) // negative
	{
		TReading = (TReading ^ 0xffff) + 1; // 2's comp
	}
	Tc_100 = (6 * TReading) + TReading / 4;    // multiply by (100 * 0.0625) or 6.25

	Whole = Tc_100 / 100;  // separate off the whole and fractional portions

	if(Whole>degreetrigger)
	{
		if(previousmode==1)
			icebox_count++;
		else
			icebox_count=0;  		
		previousmode=1;
		if(icebox_count>10)
		{
			//trigger on
			digitalWrite(relay, LOW);
			return 1;
		}
		else
			return 0;	  
	}
	else
	{
		if(previousmode==0)
		{
			icebox_count++;
		}
		else
			icebox_count=0;  		
		previousmode=0;
		if(icebox_count>10)
		{
			//trigger off
			digitalWrite(relay, HIGH); 
			return 0;
		}
		else
			return 1;
	}	
}

int getLength(int someValue) 
{
	// there's at least one byte:
	int digits = 1;
	// continually divide the value by ten, 
	// adding one to the digit count for each
	// time you divide, until you're at 0:
	int dividend = someValue /10;
	while (dividend > 0) 
	{
		dividend = dividend /10;
		digits++;
	}
	// return the number of digits:
	return digits;
}
// send an NTP request to the time server at the given address 
unsigned long sendNTPpacket(IPAddress& address)
{
	// set all bytes in the buffer to 0
	memset(packetBuffer, 0, NTP_PACKET_SIZE); 
	// Initialize values needed to form NTP request
	// (see URL above for details on the packets)
	packetBuffer[0] = 0b11100011;   // LI, Version, Mode
	packetBuffer[1] = 0;     // Stratum, or type of clock
	packetBuffer[2] = 6;     // Polling Interval
	packetBuffer[3] = 0xEC;  // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12]  = 49; 
	packetBuffer[13]  = 0x4E;
	packetBuffer[14]  = 49;
	packetBuffer[15]  = 52;

	// all NTP fields have been given values, now
	// you can send a packet requesting a timestamp:         
	Udp.beginPacket(address, 123); //NTP requests are to port 123
	Udp.write(packetBuffer,NTP_PACKET_SIZE);
	Udp.endPacket(); 
}
