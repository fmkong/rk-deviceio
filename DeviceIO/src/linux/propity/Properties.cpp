/*
 * Copyright (c) 2014 Fredy Wijaya
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdlib.h>
#include "DeviceIo/Properties.h"

namespace DeviceIOFramework {

class PropertiesParser {
public:
	PropertiesParser(){};
	virtual ~PropertiesParser(){};

	/**
	 * Parse a string (like "key = value") to pair
	 */
	std::pair<std::string, std::string> parse(const std::string& str);

	/**
	 * Reads a properties file and returns a Properties object.
	 */
	int read(const std::string& file, Properties& prop);

	/**
	 * Writes Properties object to a file.
	 */
	int write(const std::string& file, const Properties& props);
};

const std::string TRIM_DELIMITERS = " \f\n\r\t\v";
Properties* Properties::m_instance;
PropertiesParser* m_parser;

Properties* Properties::getInstance() {
	if (m_instance == NULL) {
		static std::mutex mt;
		mt.lock();
		if (m_instance == NULL)
			m_instance = new Properties();
		mt.unlock();
	}

	return m_instance;
}

Properties::Properties() {
	m_parser = new PropertiesParser();
}

int Properties::init() {
	if (NULL == m_parser)
		return -1;

	return m_parser->read("/data/local.prop", *m_instance);
}

std::string Properties::get(const std::string& key) const {
	if (properties.find(key) == properties.end()) {
		return "";
	}
	return properties.at(key);
}

std::string Properties::get(const std::string& key, const std::string& defaultValue) const {
	if (properties.find(key) == properties.end()) {
		return defaultValue;
	}
	return properties.at(key);
}

std::vector<std::string> Properties::getPropertyNames() const {
	return keys;
}

void Properties::set(const std::string& key, const std::string& value) {
	if (properties.find(key) == properties.end()) {
		keys.push_back(key);
	}
	properties[key] = value;
	if (NULL != m_parser)
		m_parser->write("/data/local.prop", *m_instance);
}

void Properties::remove(const std::string& key) {
	if (properties.find(key) == properties.end()) {
		return;
	}
	keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
	properties.erase(key);
}

static std::string ltrim(const std::string& str) {
	std::string::size_type s = str.find_first_not_of(TRIM_DELIMITERS);
	if (s == std::string::npos) {
		return "";
	}
	return str.substr(s);
}

static std::string rtrim(const std::string& str) {
	std::string::size_type s = str.find_last_not_of(TRIM_DELIMITERS);
	if (s == std::string::npos) {
		return "";
	}
	return str.substr(0, s+1);
}

static std::string trim(const std::string& str) {
	return rtrim(ltrim(str));
}

static bool isProperty(const std::string& str) {
	std::string trimmedStr = ltrim(str);
	std::string::size_type s = trimmedStr.find_first_of("=");
	if (s == std::string::npos) {
		return false;
	}
	std::string key = trim(trimmedStr.substr(0, s));
	// key can't be empty
	if (key == "") {
		return false;
	}
	return true;
}

static bool isEmptyLine(const std::string& str) {
	std::string trimmedStr = ltrim(str);
	return trimmedStr == "";
}

static bool isComment(const std::string& str) {
	std::string trimmedStr = ltrim(str);
	return trimmedStr[0] == '#';
}

std::pair<std::string, std::string> PropertiesParser::parse(const std::string& str) {
	std::string trimmedStr = trim(str);
	std::string::size_type s = trimmedStr.find_first_of("=");
	std::string key = rtrim(trimmedStr.substr(0, s));
	std::string value = ltrim(trimmedStr.substr(s + 1));

	return std::pair<std::string, std::string>(key, value);
}

int PropertiesParser::read(const std::string& file, Properties& props) {
	std::ifstream is;
	is.open(file.c_str());
	if (!is.is_open()) {
		return -1;
	}

	try {
		size_t linenr = 0;
		std::string line;
		while (getline(is, line)) {
			if (isEmptyLine(line) || isComment(line)) {
				// ignore it
			} else if (isProperty(line)) {
				std::pair<std::string, std::string> prop = parse(line);
				props.set(prop.first, prop.second);
			}
			++linenr;
		}
		is.close();
	} catch (...) {
		// don't forget to close the ifstream
		is.close();
		return -2;
	}

	return 0;
}

int PropertiesParser::write(const std::string& file, const Properties& props) {
	std::ofstream os;
	os.open(file.c_str());
	if (!os.is_open()) {
		return -1;
	}

	try {
		const std::vector<std::string>& keys = props.getPropertyNames();
		std::vector<std::string>::const_iterator itr;
		for (itr = keys.begin(); itr != keys.end(); ++itr) {
			os << *itr << " = " << props.get(*itr, "") << std::endl;
		}
		os.close();
		system("sync");
	} catch (...) {
		// don't forget to close the ofstream
		os.close();
		return -2;
	}

	return 0;
}

Properties::~Properties() {
	if (NULL != m_parser)
		delete m_parser;
}

} // namespace framework
