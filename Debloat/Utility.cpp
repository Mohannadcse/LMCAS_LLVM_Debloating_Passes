/*
 * Utility.cpp
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#include "Utility.h"

vector<std::string> splitString(string &str, char delim) {
	vector<std::string> strToVec;

	std::size_t current, previous = 0;
	current = str.find(delim);
	while (current != std::string::npos) {
		strToVec.push_back(str.substr(previous, current - previous));
		previous = current + 1;
		current = str.find(delim, previous);
	}
	strToVec.push_back(str.substr(previous, current - previous));
	return strToVec;
}


