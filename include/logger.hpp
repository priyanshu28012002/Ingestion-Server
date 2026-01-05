#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <mutex>
#include <fstream>
#include <memory>

class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        CRITICAL
    };

    // 获取单例实例
    static Logger* get_instance();

    // 删除拷贝构造函数和赋值运算符
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 日志方法
    void debug(const std::string& message, 
               const std::string& file = "", int line = 0);
    void info(const std::string& message, 
              const std::string& file = "", int line = 0);
    void warning(const std::string& message, 
                 const std::string& file = "", int line = 0);
    void error(const std::string& message, 
               const std::string& file = "", int line = 0);
    void critical(const std::string& message, 
                  const std::string& file = "", int line = 0);

    // 配置方法
    void set_log_level(Level level);
    void set_log_to_console(bool enable);
    void set_log_file(const std::string& filename);

private:
    // 私有构造函数
    Logger();
    ~Logger();

    // 内部日志实现
    void log(Level level, const std::string& message, 
             const std::string& file = "", int line = 0);
    
    std::string get_current_timestamp();
    std::string level_to_string(Level level);

    // 单例实例指针
    static Logger* instance_;
    static std::mutex mtx_;

    // 成员变量
    std::ofstream log_file_;
    Level current_level_;
    bool log_to_console_;
};

// 便捷宏定义
#define LOG_DEBUG(msg) Logger::get_instance()->debug(msg, __FILE__, __LINE__)
#define LOG_INFO(msg) Logger::get_instance()->info(msg, __FILE__, __LINE__)
#define LOG_WARNING(msg) Logger::get_instance()->warning(msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::get_instance()->error(msg, __FILE__, __LINE__)
#define LOG_CRITICAL(msg) Logger::get_instance()->critical(msg, __FILE__, __LINE__)

#endif // LOGGER_H