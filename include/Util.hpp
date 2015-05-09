#ifndef UTIL_HPP
#define UTIL_HPP

#include <iostream>
#include <sstream>

enum LogLevel {ERROR, INFO, DEBUG};

class logIt {
public:
  logIt(const std::string lable, LogLevel log_level = ERROR) {
    _buffer << "[" 
      << (log_level == ERROR
        ? "ERROR"
        : (log_level == INFO ? "INFO" : "DEBUG")) 
      << "::" << lable << "] ";
  }
  
  template <typename T>
  logIt & operator<<(T const & value) {
    _buffer << value;
    return *this;
  }
  
  ~logIt() {
    _buffer << std::endl;
    std::cerr << _buffer.str();
  }

private:
    std::ostringstream _buffer;
};

extern LogLevel _log_level;

#define LOG(level, lable) \
if (level > _log_level) ; \
else logIt(lable, level)

#endif // UTIL_HPP