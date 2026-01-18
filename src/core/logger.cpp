#include "logger.hpp"

#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>


Logger* Logger::instance_ = nullptr;
std::mutex Logger::mtx_;

Logger::Logger() 
    : current_level_(Level::INFO),
      log_to_console_(true)
{
    
    log_file_.open("application.log", std::ios::app);
    
    if (!log_file_.is_open()) {
        std::cerr << "Warning: Could not open log file" << std::endl;
    }
    std::cout << "Logger initialized - Appending to: application.log" << std::endl;
}

Logger::~Logger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

Logger* Logger::get_instance() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (instance_ == nullptr) {
        instance_ = new Logger();
    }
    return instance_;
}

void Logger::set_log_level(Level level) {
    std::lock_guard<std::mutex> lock(mtx_);
    current_level_ = level;
}

void Logger::set_log_to_console(bool enable) {
    std::lock_guard<std::mutex> lock(mtx_);
    log_to_console_ = enable;
}

void Logger::set_log_file(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    if (log_file_.is_open()) {
        log_file_.close();
    }
    

    log_file_.open(filename, std::ios::app);
    
    if (!log_file_.is_open()) {
        std::cerr << "Warning: Could not open log file: " << filename << std::endl;
    } else {
        std::cout << "Log file set to: " << filename << " (append mode)" << std::endl;
    }
}

std::string Logger::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm local_tm = *std::localtime(&now_c);
    
    std::stringstream ss;
    ss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

std::string Logger::level_to_string(Level level) {
    switch (level) {
        case Level::DEBUG:    return "DEBUG";
        case Level::INFO:     return "INFO";
        case Level::WARNING:  return "WARNING";
        case Level::ERROR:    return "ERROR";
        case Level::CRITICAL: return "CRITICAL";
        default:              return "UNKNOWN";
    }
}

void Logger::log(Level level, const std::string& message, 
                 const std::string& file, int line) {
    std::lock_guard<std::mutex> lock(mtx_);
    

    if (level < current_level_) {
        return;
    }
    

    std::stringstream ss;
    ss << get_current_timestamp() 
       << " [" << level_to_string(level) << "] ";
    

    if (!file.empty()) {
        size_t pos = file.find_last_of("/\\");
        std::string filename = (pos != std::string::npos) ? 
                              file.substr(pos + 1) : file;
        ss << "[" << filename << ":" << line << "] ";
    }
    
    ss << message;
    
    std::string log_entry = ss.str();
    

    if (log_to_console_) {
        if (level == Level::ERROR || level == Level::CRITICAL) {
            std::cerr << "\033[1;31m" << log_entry << "\033[0m" << std::endl;
        } else if (level == Level::WARNING) {
            std::cerr << "\033[1;33m" << log_entry << "\033[0m" << std::endl;
        } else {
            std::cout << log_entry << std::endl;
        }
    }
    

    if (log_file_.is_open()) {
        log_file_ << log_entry << std::endl;
        log_file_.flush(); 
    }
}


void Logger::debug(const std::string& message, 
                   const std::string& file, int line) {
    log(Level::DEBUG, message, file, line);
}

void Logger::info(const std::string& message, 
                  const std::string& file, int line) {
    log(Level::INFO, message, file, line);
}

void Logger::warning(const std::string& message, 
                     const std::string& file, int line) {
    log(Level::WARNING, message, file, line);
}

void Logger::error(const std::string& message, 
                   const std::string& file, int line) {
    log(Level::ERROR, message, file, line);
}

void Logger::critical(const std::string& message, 
                      const std::string& file, int line) {
    log(Level::CRITICAL, message, file, line);
}
