/**
 * @file logger.c
 * @brief 日志记录模块实现
 * @details 实现多级别日志记录、文件轮转、线程安全等功能
 */

#include "logger.h"
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/* 全局日志记录器实例 */
Logger *g_logger = NULL;

void logger_config_init(LoggerConfig *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->min_level = LOG_LEVEL_INFO;
    config->target = LOG_TARGET_CONSOLE;
    config->max_file_size = 10 * 1024 * 1024;
    config->max_backup = 5;
    config->show_timestamp = 1;
    config->show_location = 1;
    config->show_thread_id = 1;
}

/**
 * @brief 获取日志级别字符串
 */
const char *logger_level_to_string(LogLevel level) {
    static const char *level_strings[] = {
        "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
    };
    
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_FATAL) {
        return level_strings[level];
    }
    return "UNKNOWN";
}

/**
 * @brief 获取日志级别颜色代码（ANSI）
 */
static const char *logger_level_to_color(LogLevel level) {
    static const char *color_codes[] = {
        "\033[36m",  /* DEBUG - 青色 */
        "\033[32m",  /* INFO - 绿色 */
        "\033[33m",  /* WARN - 黄色 */
        "\033[31m",  /* ERROR - 红色 */
        "\033[35m"   /* FATAL - 紫色 */
    };
    
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_FATAL) {
        return color_codes[level];
    }
    return "\033[0m";
}

/**
 * @brief 获取当前时间戳字符串
 */
void logger_get_timestamp(char *buffer, size_t size) {
    time_t now;
    struct tm *tm_info;
    struct timespec ts;
    
    clock_gettime(CLOCK_REALTIME, &ts);
    now = ts.tv_sec;
    tm_info = localtime(&now);
    
    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec,
             ts.tv_nsec / 1000000);
}

/**
 * @brief 获取日志文件大小
 */
long logger_get_file_size(void) {
    if (g_logger == NULL || g_logger->log_fp == NULL) {
        return 0;
    }
    
    long current_pos = ftell(g_logger->log_fp);
    fseek(g_logger->log_fp, 0, SEEK_END);
    long size = ftell(g_logger->log_fp);
    fseek(g_logger->log_fp, current_pos, SEEK_SET);
    
    return size;
}

/**
 * @brief 日志文件轮转
 */
int logger_rotate(void) {
    if (g_logger == NULL || g_logger->log_fp == NULL) {
        return -1;
    }
    
    char old_file[512];
    char new_file[512];
    int i;
    
    /* 删除最旧的备份文件 */
    snprintf(old_file, sizeof(old_file), "%s.%d", 
             g_logger->config.log_file, g_logger->config.max_backup);
    if (access(old_file, F_OK) == 0) {
        remove(old_file);
    }
    
    /* 重命名现有备份文件 */
    for (i = g_logger->config.max_backup - 1; i >= 1; i--) {
        snprintf(old_file, sizeof(old_file), "%s.%d", 
                 g_logger->config.log_file, i);
        snprintf(new_file, sizeof(new_file), "%s.%d", 
                 g_logger->config.log_file, i + 1);
        if (access(old_file, F_OK) == 0) {
            rename(old_file, new_file);
        }
    }
    
    /* 关闭当前日志文件 */
    fclose(g_logger->log_fp);
    
    /* 重命名当前日志文件 */
    snprintf(new_file, sizeof(new_file), "%s.1", g_logger->config.log_file);
    rename(g_logger->config.log_file, new_file);
    
    /* 重新打开日志文件 */
    g_logger->log_fp = fopen(g_logger->config.log_file, "a");
    if (g_logger->log_fp == NULL) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief 初始化日志记录器
 */
int logger_init(const LoggerConfig *config) {
    /* 分配日志记录器内存 */
    if (g_logger == NULL) {
        g_logger = (Logger *)malloc(sizeof(Logger));
        if (g_logger == NULL) {
            return -1;
        }
        memset(g_logger, 0, sizeof(Logger));
    }
    
    /* 复制配置 */
    if (config != NULL) {
        memcpy(&g_logger->config, config, sizeof(LoggerConfig));
    } else {
        logger_config_init(&g_logger->config);
    }
    
    /* 设置模块名称 */
    strcpy(g_logger->module_name, "MEDIA_FW");
    
    /* 初始化互斥锁 */
    pthread_mutex_init(&g_logger->mutex, NULL);
    
    /* 打开日志文件 */
    if (g_logger->config.target == LOG_TARGET_FILE || 
        g_logger->config.target == LOG_TARGET_BOTH) {
        if (strlen(g_logger->config.log_file) > 0) {
            g_logger->log_fp = fopen(g_logger->config.log_file, "a");
            if (g_logger->log_fp == NULL) {
                fprintf(stderr, "Failed to open log file: %s\n", 
                        g_logger->config.log_file);
                /* 回退到控制台输出 */
                g_logger->config.target = LOG_TARGET_CONSOLE;
            }
        }
    }
    
    g_logger->initialized = 1;
    
    return 0;
}

/**
 * @brief 关闭日志记录器
 */
void logger_shutdown(void) {
    if (g_logger == NULL) {
        return;
    }
    
    pthread_mutex_lock(&g_logger->mutex);
    
    if (g_logger->log_fp != NULL) {
        fflush(g_logger->log_fp);
        fclose(g_logger->log_fp);
        g_logger->log_fp = NULL;
    }
    
    g_logger->initialized = 0;
    
    pthread_mutex_unlock(&g_logger->mutex);
    pthread_mutex_destroy(&g_logger->mutex);
    
    free(g_logger);
    g_logger = NULL;
}

/**
 * @brief 设置日志级别
 */
void logger_set_level(LogLevel level) {
    if (g_logger != NULL) {
        g_logger->config.min_level = level;
    }
}

/**
 * @brief 设置日志输出目标
 */
void logger_set_target(LogTarget target) {
    if (g_logger != NULL) {
        g_logger->config.target = target;
    }
}

/**
 * @brief 设置日志文件
 */
int logger_set_file(const char *file_path) {
    if (g_logger == NULL || file_path == NULL) {
        return -1;
    }
    
    pthread_mutex_lock(&g_logger->mutex);
    
    /* 关闭现有日志文件 */
    if (g_logger->log_fp != NULL) {
        fclose(g_logger->log_fp);
        g_logger->log_fp = NULL;
    }
    
    /* 设置新的日志文件路径 */
    strncpy(g_logger->config.log_file, file_path, 
            sizeof(g_logger->config.log_file) - 1);
    
    /* 打开新的日志文件 */
    g_logger->log_fp = fopen(file_path, "a");
    if (g_logger->log_fp == NULL) {
        pthread_mutex_unlock(&g_logger->mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&g_logger->mutex);
    return 0;
}

/**
 * @brief 核心日志记录函数
 */
void logger_log(LogLevel level, const char *file, int line, 
                const char *func, const char *fmt, ...) {
    if (g_logger == NULL || !g_logger->initialized) {
        /* 如果日志记录器未初始化，输出到stderr */
        va_list args;
        va_start(args, fmt);
        fprintf(stderr, "[%s] ", logger_level_to_string(level));
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
        return;
    }
    
    /* 检查日志级别 */
    if (level < g_logger->config.min_level) {
        return;
    }
    
    pthread_mutex_lock(&g_logger->mutex);
    
    /* 准备日志消息 */
    char timestamp[64] = {0};
    char message[4096] = {0};
    char log_line[8192] = {0};
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    /* 获取时间戳 */
    if (g_logger->config.show_timestamp) {
        logger_get_timestamp(timestamp, sizeof(timestamp));
    }
    
    /* 提取文件名（不含路径） */
    const char *filename = strrchr(file, '/');
    filename = (filename != NULL) ? filename + 1 : file;
    
    /* 构建日志行 */
    int offset = 0;
    
    if (g_logger->config.show_timestamp) {
        offset += snprintf(log_line + offset, sizeof(log_line) - offset,
                          "[%s] ", timestamp);
    }
    
    offset += snprintf(log_line + offset, sizeof(log_line) - offset,
                      "[%s] [%s] ", g_logger->module_name, 
                      logger_level_to_string(level));
    
    if (g_logger->config.show_thread_id) {
        offset += snprintf(log_line + offset, sizeof(log_line) - offset,
                          "[%lu] ", (unsigned long)pthread_self());
    }
    
    if (g_logger->config.show_location) {
        offset += snprintf(log_line + offset, sizeof(log_line) - offset,
                          "[%s:%d %s] ", filename, line, func);
    }
    
    offset += snprintf(log_line + offset, sizeof(log_line) - offset,
                      "%s\n", message);
    
    /* 输出到控制台 */
    if (g_logger->config.target == LOG_TARGET_CONSOLE || 
        g_logger->config.target == LOG_TARGET_BOTH) {
        fprintf(stderr, "%s%s%s", logger_level_to_color(level), 
                log_line, "\033[0m");
    }
    
    /* 输出到文件 */
    if ((g_logger->config.target == LOG_TARGET_FILE || 
         g_logger->config.target == LOG_TARGET_BOTH) && 
        g_logger->log_fp != NULL) {
        fprintf(g_logger->log_fp, "%s", log_line);
        fflush(g_logger->log_fp);
        
        /* 检查文件大小并轮转 */
        if (g_logger->config.max_file_size > 0 &&
            logger_get_file_size() > g_logger->config.max_file_size) {
            logger_rotate();
        }
    }
    
    pthread_mutex_unlock(&g_logger->mutex);
    
    /* 如果是致命错误，终止程序 */
    if (level == LOG_LEVEL_FATAL) {
        abort();
    }
}
