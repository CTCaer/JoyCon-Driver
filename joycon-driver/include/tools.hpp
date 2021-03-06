#pragma once
#include <chrono>
#include <thread>
#include <map>
#include <string>


double lowpassFilter(double a, double thresh) {
	if (abs(a) > thresh) {
		return a;
	} else {
		return 0;
	}
}

// sleeps accurately:
void accurateSleep(double durationMS, double sleepThreshold = 1.8) {

	// get current time
	auto tNow = std::chrono::high_resolution_clock::now();

	auto tSleepStart = std::chrono::high_resolution_clock::now();
	
	auto tSleepDuration = std::chrono::duration_cast<std::chrono::microseconds>(tNow - tSleepStart);

	// get the application's runtime duration in ms
	//runningTimeMS = std::chrono::duration_cast<std::chrono::milliseconds>(tNow - tApplicationStart).count();
	//auto tFrameDuration = std::chrono::duration_cast<std::chrono::microseconds>(tNow - tFrameStart);
	//double tFrameDurationMS = tFrameDuration.count() / 1000.0;
	
	
	// time spent sleeping (0):
	double tSleepTimeMS = tSleepDuration.count() / 1000.0;

	//float lowerThres = 0.2;
	//float sleepThreshold = 1.8;//1.4

	// run cpu in circles
	while (tSleepTimeMS < durationMS) {
		// allow cpu to sleep if there is lots of time to kill
		if (tSleepTimeMS < durationMS - sleepThreshold) {
			std::this_thread::sleep_for(std::chrono::microseconds(100));
			//Sleep(1);
		}
		tNow = std::chrono::high_resolution_clock::now();
		tSleepDuration = std::chrono::duration_cast<std::chrono::microseconds>(tNow - tSleepStart);
		tSleepTimeMS = tSleepDuration.count() / 1000.0;
	}

	// done sleeping

}



/* The LoadConfig function loads the configuration file given by filename
It returns a map of key-value pairs stored in the conifuration file */
std::map<std::string, std::string> LoadConfig(std::string filename)
{
	std::ifstream input(filename); //The input stream
	std::map<std::string, std::string> ans; //A map of key-value pairs in the file
	while (input) //Keep on going as long as the file stream is good
	{
		std::string key; //The key
		std::string value; //The value
		std::getline(input, key, ':'); //Read up to the : delimiter into key
		std::getline(input, value, '\n'); //Read up to the newline into value
		std::string::size_type pos1 = value.find_first_of("\""); //Find the first quote in the value
		std::string::size_type pos2 = value.find_last_of("\""); //Find the last quote in the value
		if (pos1 != std::string::npos && pos2 != std::string::npos && pos2 > pos1) //Check if the found positions are all valid
		{
			value = value.substr(pos1 + 1, pos2 - pos1 - 1); //Take a substring of the part between the quotes
			ans[key] = value; //Store the result in the map
		}
	}
	input.close(); //Close the file stream
	return ans; //And return the result
}


void setupConsole(std::string title) {
	// setup console
	AllocConsole();
	freopen("conin$", "r", stdin);
	freopen("conout$", "w", stdout);
	freopen("conout$", "w", stderr);
	printf("Debugging Window:\n");
}


int16_t unsignedToSigned16(uint16_t n) {
	uint16_t A = n;
	uint16_t B = 0xFFFF - A;
	if (A < B) {
		return (int16_t)A;
	} else {
		return (int16_t)(-1 * B);
	}
}

//float filterTerm2 = 0;
//
//float comp_filter(float newAngle, float newRate, float previousAngle) {
//
//	float filterTerm0;
//	float filterTerm1;
//	
//	float timeConstant;
//	float finalAngle;
//	float dt = 0.015;
//
//	timeConstant = 0.5; // default 1.0
//
//	filterTerm0 = (newAngle - previousAngle) * timeConstant * timeConstant;
//	filterTerm2 += filterTerm0 * dt;
//	filterTerm1 = filterTerm2 + ((newAngle - previousAngle) * 2 * timeConstant) + newRate;
//	finalAngle = (filterTerm1 * dt) + previousAngle;
//
//	//return previousAngle; // This is actually the current angle, but is stored for the next iteration
//	return finalAngle;
//}


float clamp(float a, float min, float max) {
	if (a < min) {
		return min;
	} else if (a > max) {
		return max;
	} else {
		return a;
	}
}