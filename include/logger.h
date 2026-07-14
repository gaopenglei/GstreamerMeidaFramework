/**
 * @file logger.h
 * @brief 日志记录模块头文件
 * @details 提供多级别日志记录功能，支持文件和控制台输出
 * @author GStreamer Media Framework
 * @version 1.0.0
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 日志级别枚举定义
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,    /**< 调试级别，用于详细的调试信息 */
    LOG_LEVEL_INFO  = 1,    /**< 信息级别，用于一般运行信息 */
    LOG_LEVEL_WARN  = 2,    /**< 警告级别，用于潜在问题提示 */
    LOG_LEVEL_ERROR = 3,    /**< 错误级别，用于错误信息 */
    LOG_LEVEL_FATAL = 4     /**< 致命级别，用于严重错误 */
} LogLevel;

/**
 * @brief 日志输出目标枚举定义
 */
typedef enum {
    LOG_TARGET_CONSOLE = 0, /**< 输出到控制台 */
    LOG_TARGET_FILE    = 1, /**< 输出到文件 */
    LOG_TARGET_BOTH    = 2  /**< 同时输出到控制台和文件 */
} LogTarget;

/**
 * @brief 日志配置结构体
 */
typedef struct {
    LogLevel    min_level;      /**< 最小日志级别 */
    LogTarget   target;         /**< 输出目标 */
    char        log_file[256];  /**< 日志文件路径 */
    int         max_file_size;  /**< 最大文件大小（字节） */
    int         max_backup;     /**< 最大备份文件数 */
    int         show_timestamp; /**< 是否显示时间戳 */
    int         show_location;  /**< 是否显示代码位置 */
    int         show_thread_id; /**< 是否显示线程ID */
} LoggerConfig;

/**
 * @brief 日志记录器结构体
 */
typedef struct {
    LoggerConfig    config;         /**< 日志配置 */
    FILE           *log_fp;         /**< 日志文件指针 */
    pthread_mutex_t mutex;          /**< 互斥锁 */
    int             initialized;    /**< 初始化标志 */
    char            module_name[64];/**< 模块名称 */
} Logger;

/**
 * @brief 全局日志记录器实例
 */
extern Logger *g_logger;

/**
 * @brief 初始化日志配置为默认值
 * @param config 配置结构体指针
 */
void logger_config_init(LoggerConfig *config);

/**
 * @brief 初始化日志记录器
 * @param config 日志配置
 * @return 成功返回0，失败返回-1
 */
int logger_init(const LoggerConfig *config);

/**
 * @brief 关闭日志记录器
 */
void logger_shutdown(void);

/**
 * @brief 设置日志级别
 * @param level 日志级别
 */
void logger_set_level(LogLevel level);

/**
 * @brief 设置日志输出目标
 * @param target 输出目标
 */
void logger_set_target(LogTarget target);

/**
 * @brief 设置日志文件
 * @param file_path 日志文件路径
 * @return 成功返回0，失败返回-1
 */
int logger_set_file(const char *file_path);

/**
 * @brief 核心日志记录函数
 * @param level 日志级别
 * @param file 源文件名
 * @param line 行号
 * @param func 函数名
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
void logger_log(LogLevel level, const char *file, int line, 
                const char *func, const char *fmt, ...);

/**
 * @brief 日志记录宏 - 调试级别
 */
#define LOG_DEBUG(fmt, ...) \
    logger_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

/**
 * @brief 日志记录宏 - 信息级别
 */
#define LOG_INFO(fmt, ...) \
    logger_log(LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

/**
 * @brief 日志记录宏 - 警告级别
 */
#define LOG_WARN(fmt, ...) \
    logger_log(LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

/**
 * @brief 日志记录宏 - 错误级别
 */
#define LOG_ERROR(fmt, ...) \
    logger_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

/**
 * @brief 日志记录宏 - 致命级别
 */
#define LOG_FATAL(fmt, ...) \
    logger_log(LOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

/**
 * @brief 获取日志级别字符串
 * @param level 日志级别
 * @return 日志级别字符串
 */
const char *logger_level_to_string(LogLevel level);

/**
 * @brief 获取当前时间戳字符串
 * @param buffer 时间戳缓冲区
 * @param size 缓冲区大小
 */
void logger_get_timestamp(char *buffer, size_t size);

/**
 * @brief 日志文件轮转
 * @return 成功返回0，失败返回-1
 */
int logger_rotate(void);

/**
 * @brief 获取日志文件大小
 * @return 文件大小（字节）
 */
long logger_get_file_size(void);

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */
