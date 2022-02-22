//Pins
// 4,  5, 12 (TDI), 13(TCK), 14(TMS), 15(TD0), 16, 17, 
//18, 19, 21, 22, 23, 25, 26, 27, 
// 0, 2, 32
//33 EN + /STANDBY

//!!! set arduino and events to core 0
#include <driver/adc.h>
//#include <driver/i2s.h>

const long baud = 921600;
//const long baud = 1000000;
//const long baud = 2000000;
const unsigned long cyclesPerSecond = 240000000;
const unsigned long cyclesPerMilliSecond = cyclesPerSecond / 1000;
const unsigned long cyclesPerMicroSecond = cyclesPerSecond / 1000000;
const unsigned long cyclesPerPhase = cyclesPerSecond / 40000;
const unsigned long cyclesPerPhaseHalf = cyclesPerPhase / 2;

const float meterPerSecond = 344.f;
const float arrayWidthMm = 10 * 8;
const float mmPerUs = meterPerSecond * 0.001f;
const long arrayWidthUs = long(arrayWidthMm / mmPerUs);
const float startMm = 200;
const long startUs = long(startMm / mmPerUs);

const int analogBias = 0;

const int enablePin = 33;
const int pinCount = 8;
const int pulseLength = 20;
const int pins[] = {
  4,  5, 12, 13, 14, 15, 16, 17,}; 
 //18, 19, 21, 22, 23, 25, 26, 27,};

int phaseShifts[64][pinCount];
volatile int phaseShift = 0;
const int samplingRate = 20000;
volatile int depth = 256;
volatile int width = 64;
volatile int currentRec = 0;
volatile bool ready2send = false;
int distance[2];
int start[2];
int recShift[2];
short rec[2][64][256];

void print4(unsigned long v)
{
	const char *hex = "0123456789abcdef";
	Serial.write(hex[v & 15]);
}

void print8(unsigned long v)
{
	print4(v >> 4);
	print4(v);
}

void print16(unsigned long v)
{
	print8(v >> 8);
	print8(v);
}


void comTask(void *data)
{
	while(true)
	{
		do
		{
			delay(1);
		} 
		while(!ready2send);
		//Serial.print("bitluni");
		//Serial.write();
		print16(width);
		print16(depth);
		print16(start[currentRec ^ 1]);
		print16(distance[currentRec ^ 1]);
		for(int j = 0; j < width; j++)
		{
			for(int i = 0; i < depth; i++)
			{
				print4(min((rec[currentRec ^ 1][j][i] * (i + 8)) / 64, 255) >> 4);
//				Serial.printf("%02x", min((rec[currentRec ^ 1][j][i] * (i + 8)) / 64, 255));
			}
		}
		Serial.println();
		static String s = "";
		while(Serial.available() > 0) 
		{
		char ch = Serial.read();
		if(ch == '\n')
		{
			sscanf(s.c_str(), "%d %d", &width, &depth);
			s = "";
			Serial.println("OK");
		}
		else
			s += ch;
		}
		ready2send = false;
	}
}

void waveTask(void *param)
{
	long sum = 0;
	long avg = 2048;
	while(true)
	{
		//send pulse 400µs
		digitalWrite(enablePin, 1);	//enable output
		unsigned long t = 0;
		unsigned long ot = ESP.getCycleCount();
		while(true)
		{
			unsigned long ct = ESP.getCycleCount();
			unsigned long dt = ct - ot;
			ot = ct;
			t += dt;
			if(t >= 400 * cyclesPerMicroSecond) break;
			unsigned long phase = t % cyclesPerPhase;
			unsigned long parallel1 = 0;
			unsigned long parallel0 = 0;
			for(int i = 0; i < pinCount; i++)
			{
				//digitalWrite(pins[i], wave[phase][i]);
				if((phaseShifts[phaseShift][i] + phase) % cyclesPerPhase > cyclesPerPhaseHalf)
					parallel1 |= 1 << pins[i];
				else
					parallel0 |= 1 << pins[i];/**/
			}
			*(unsigned long*)GPIO_OUT_W1TS_REG = parallel1;
			*(unsigned long*)GPIO_OUT_W1TC_REG = parallel0;
			//using set 0 and set 1 registers doesn't mess with tx, rx etc.
			//maybe GPIO_BT_SELECT_REG and GPIO_OUT_REG are an option    
		}
		digitalWrite(enablePin, 0);	//disable output
		
		//read echo
		//wait 800µs
		delayMicroseconds(startUs);//arrayWidthUs);
		adc1_config_width(ADC_WIDTH_BIT_12);	//9, 10, 11, 12
		adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11); //ADC_ATTEN_DB_0, ADC_ATTEN_DB_2_5, ADC_ATTEN_DB_6 ADC_ATTEN_DB_11	  
		recShift[currentRec] = phaseShift;
		unsigned long t0 = ESP.getCycleCount();
		for(int i = 0; i < depth; i++)
		{
			//int a = analogRead(34);
			int a = adc1_get_raw(ADC1_CHANNEL_6);
			sum += a;
			rec[currentRec][phaseShift][i] = abs(a - avg);
		}
		distance[currentRec] = (int)((ESP.getCycleCount() - t0) * mmPerUs / cyclesPerMicroSecond);
		start[currentRec] = startMm;//(int)arrayWidthMm;
		phaseShift = (phaseShift + 1) % width;
		if(phaseShift == 0)
		{
			do{
				delay(0);
			}while(ready2send);
			currentRec ^= 1;
			ready2send = true;
			while (ready2send) delay(1);
			avg = (sum / depth) / width;
			sum = 0;
		}
  	}
}

void setup() 
{
	Serial.begin(baud);
	Serial.println("Phased array setup!"); 
	for(int i = 0; i < pinCount; i++)
		pinMode(pins[i], OUTPUT);
	pinMode(enablePin, OUTPUT);
	calculateWave();
	TaskHandle_t xHandle = NULL;
	xTaskCreatePinnedToCore(comTask, "Communication", 10000, 0, 2, &xHandle, 0);
	xTaskCreatePinnedToCore(waveTask, "Wave", 10000, 0,  ( 2 | portPRIVILEGE_BIT ), &xHandle, 1);
}

void calculateWave()
{
	for(int i = 0; i < 64; i++)
	{
		float shift = (31 - i) / 64.0f;
		for(int j = 0; j < pinCount; j++)
		{
			float phase = j * shift;
			while(shift < 0) shift += 1;
			phaseShifts[i][j] = (int)(phase * cyclesPerPhase) % cyclesPerPhase;
		}
	}
}

void loop() 
{
	delay(1000);
}