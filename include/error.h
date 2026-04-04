/**
 * @file error.h
 * @brief 错误处理模块头文件
 * @details 定义错误码、错误处理函数和错误回调机制
 */

#ifndef ERROR_H
#define ERROR_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 错误码定义
 * @details 错误码采用分段设计：
 *          - 0: 成功
 *          - -1 ~ -99: 通用错误
 *          - -100 ~ -199: 播放模块错误
 *          - -200 ~ -299: 录制模块错误
 *          - -300 ~ -399: 转码模块错误
 *          - -400 ~ -499: 编解码模块错误
 *          - -500 ~ -599: 输入输出模块错误
 */
typedef enum {
    /* 成功 */
    MEDIA_OK = 0,
    
    /* 通用错误 (-1 ~ -99) */
    MEDIA_ERROR_UNKNOWN = -1,              /**< 未知错误 */
    MEDIA_ERROR_INVALID_PARAM = -2,        /**< 无效参数 */
    MEDIA_ERROR_NO_MEMORY = -3,            /**< 内存不足 */
    MEDIA_ERROR_NOT_INITIALIZED = -4,      /**< 未初始化 */
    MEDIA_ERROR_ALREADY_INITIALIZED = -5,  /**< 已初始化 */
    MEDIA_ERROR_TIMEOUT = -6,              /**< 超时 */
    MEDIA_ERROR_NOT_SUPPORTED = -7,        /**< 不支持的操作 */
    MEDIA_ERROR_BUSY = -8,                 /**< 资源忙 */
    MEDIA_ERROR_NOT_FOUND = -9,            /**< 资源未找到 */
    MEDIA_ERROR_PERMISSION_DENIED = -10,   /**< 权限拒绝 */
    
    /* 播放模块错误 (-100 ~ -199) */
    MEDIA_ERROR_PLAYER_INIT_FAILED = -100,     /**< 播放器初始化失败 */
    MEDIA_ERROR_PLAYER_CREATE_PIPELINE = -101, /**< 创建播放管道失败 */
    MEDIA_ERROR_PLAYER_INVALID_URI = -102,     /**< 无效的URI */
    MEDIA_ERROR_PLAYER_NO_DECODER = -103,      /**< 找不到解码器 */
    MEDIA_ERROR_PLAYER_NO_SINK = -104,         /**< 找不到输出设备 */
    MEDIA_ERROR_PLAYER_STATE_CHANGE = -105,    /**< 状态切换失败 */
    MEDIA_ERROR_PLAYER_EOS = -106,             /**< 播放结束 */
    MEDIA_ERROR_PLAYER_BUFFER_UNDERFLOW = -107,/**< 缓冲区欠载 */
    MEDIA_ERROR_PLAYER_SYNC_ERROR = -108,      /**< 同步错误 */
    
    /* 录制模块错误 (-200 ~ -299) */
    MEDIA_ERROR_RECORDER_INIT_FAILED = -200,       /**< 录制器初始化失败 */
    MEDIA_ERROR_RECORDER_CREATE_PIPELINE = -201,   /**< 创建录制管道失败 */
    MEDIA_ERROR_RECORDER_NO_DEVICE = -202,         /**< 找不到设备 */
    MEDIA_ERROR_RECORDER_DEVICE_BUSY = -203,       /**< 设备忙 */
    MEDIA_ERROR_RECORDER_NO_ENCODER = -204,        /**< 找不到编码器 */
    MEDIA_ERROR_RECORDER_MUX_ERROR = -205,         /**< 复用错误 */
    MEDIA_ERROR_RECORDER_FILE_ERROR = -206,        /**< 文件错误 */
    MEDIA_ERROR_RECORDER_BUFFER_OVERFLOW = -207,   /**< 缓冲区溢出 */
    
    /* 转码模块错误 (-300 ~ -399) */
    MEDIA_ERROR_TRANSCODER_INIT_FAILED = -300,     /**< 转码器初始化失败 */
    MEDIA_ERROR_TRANSCODER_CREATE_PIPELINE = -301, /**< 创建转码管道失败 */
    MEDIA_ERROR_TRANSCODER_INVALID_INPUT = -302,   /**< 无效输入文件 */
    MEDIA_ERROR_TRANSCODER_INVALID_OUTPUT = -303,  /**< 无效输出格式 */
    MEDIA_ERROR_TRANSCODER_NO_DECODER = -304,      /**< 找不到解码器 */
    MEDIA_ERROR_TRANSCODER_NO_ENCODER = -305,      /**< 找不到编码器 */
    MEDIA_ERROR_TRANSCODER_FORMAT_ERROR = -306,    /**< 格式错误 */
    
    /* 编解码模块错误 (-400 ~ -499) */
    MEDIA_ERROR_CODEC_INIT_FAILED = -400,      /**< 编解码器初始化失败 */
    MEDIA_ERROR_CODEC_ENCODE_FAILED = -401,    /**< 编码失败 */
    MEDIA_ERROR_CODEC_DECODE_FAILED = -402,    /**< 解码失败 */
    MEDIA_ERROR_CODEC_NOT_SUPPORTED = -403,    /**< 不支持的编解码格式 */
    MEDIA_ERROR_CODEC_PARAM_ERROR = -404,      /**< 编解码参数错误 */
    MEDIA_ERROR_CODEC_RESOURCE_ERROR = -405,   /**< 编解码资源错误 */
    
    /* 输入输出模块错误 (-500 ~ -599) */
    MEDIA_ERROR_IO_OPEN_FAILED = -500,     /**< 打开失败 */
    MEDIA_ERROR_IO_READ_FAILED = -501,     /**< 读取失败 */
    MEDIA_ERROR_IO_WRITE_FAILED = -502,    /**< 写入失败 */
    MEDIA_ERROR_IO_CLOSE_FAILED = -503,    /**< 关闭失败 */
    MEDIA_ERROR_IO_SEEK_FAILED = -504,     /**< 定位失败 */
    MEDIA_ERROR_IO_NETWORK_ERROR = -505,   /**< 网络错误 */
    
    /* GStreamer 特定错误 (-600 ~ -699) */
    MEDIA_ERROR_GST_INIT_FAILED = -600,        /**< GStreamer初始化失败 */
    MEDIA_ERROR_GST_ELEMENT_CREATE = -601,     /**< 创建元素失败 */
    MEDIA_ERROR_GST_ELEMENT_LINK = -602,       /**< 链接元素失败 */
    MEDIA_ERROR_GST_PIPELINE_ERROR = -603,     /**< 管道错误 */
    MEDIA_ERROR_GST_STATE_CHANGE = -604,       /**< 状态切换错误 */
    MEDIA_ERROR_GST_MESSAGE_ERROR = -605,      /**< 消息错误 */
    MEDIA_ERROR_GST_PLUGIN_MISSING = -606      /**< 缺少插件 */
    
} MediaErrorCode;

/**
 * @brief 错误严重程度
 */
typedef enum {
    ERROR_SEVERITY_LOW = 0,     /**< 低严重性，可忽略 */
    ERROR_SEVERITY_MEDIUM = 1,  /**< 中等严重性，需要处理 */
    ERROR_SEVERITY_HIGH = 2,    /**< 高严重性，影响功能 */
    ERROR_SEVERITY_CRITICAL = 3 /**< 致命严重性，需要终止 */
} ErrorSeverity;

/**
 * @brief 错误信息结构体
 */
typedef struct {
    MediaErrorCode code;        /**< 错误码 */
    ErrorSeverity severity;     /**< 严重程度 */
    char message[512];          /**< 错误消息 */
    char file[256];             /**< 发生错误的源文件 */
    int line;                   /**< 发生错误的行号 */
    char function[128];         /**< 发生错误的函数 */
    uint64_t timestamp;         /**< 错误发生时间戳 */
    void *user_data;            /**< 用户数据 */
} ErrorInfo;

/**
 * @brief 错误回调函数类型
 */
typedef void (*ErrorCallback)(const ErrorInfo *error, void *user_data);

/**
 * @brief 错误处理上下文
 */
typedef struct {
    ErrorCallback callback;     /**< 错误回调函数 */
    void *user_data;            /**< 用户数据 */
    int error_count;            /**< 错误计数 */
    ErrorInfo last_error;       /**< 最后一个错误 */
} ErrorContext;

/**
 * @brief 初始化错误处理模块
 * @return 成功返回0，失败返回-1
 */
int error_init(void);

/**
 * @brief 关闭错误处理模块
 */
void error_shutdown(void);

/**
 * @brief 设置错误回调函数
 * @param callback 回调函数
 * @param user_data 用户数据
 */
void error_set_callback(ErrorCallback callback, void *user_data);

/**
 * @brief 设置错误
 * @param code 错误码
 * @param file 源文件
 * @param line 行号
 * @param func 函数名
 * @param fmt 格式化消息
 * @param ... 可变参数
 */
void error_set(MediaErrorCode code, const char *file, int line,
               const char *func, const char *fmt, ...);

/**
 * @brief 获取最后一个错误
 * @return 错误信息指针
 */
const ErrorInfo *error_get_last(void);

/**
 * @brief 获取错误码描述
 * @param code 错误码
 * @return 错误描述字符串
 */
const char *error_code_to_string(MediaErrorCode code);

/**
 * @brief 获取错误严重程度描述
 * @param severity 严重程度
 * @return 严重程度描述字符串
 */
const char *error_severity_to_string(ErrorSeverity severity);

/**
 * @brief 清除错误
 */
void error_clear(void);

/**
 * @brief 检查是否成功
 * @param code 错误码
 * @return 成功返回1，失败返回0
 */
#define IS_SUCCESS(code) ((code) == MEDIA_OK)

/**
 * @brief 检查是否失败
 * @param code 错误码
 * @return 失败返回1，成功返回0
 */
#define IS_ERROR(code) ((code) != MEDIA_OK)

/**
 * @brief 设置错误宏
 */
#define SET_ERROR(code, fmt, ...) \
    error_set((code), __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

/**
 * @brief 条件检查宏
 */
#define CHECK_NULL_RETURN(ptr, code, msg) \
    do { \
        if ((ptr) == NULL) { \
            SET_ERROR((code), "%s: NULL pointer", (msg)); \
            return (code); \
        } \
    } while (0)

/**
 * @brief 条件检查宏（带返回值）
 */
#define CHECK_NULL_RETURN_VAL(ptr, code, val, msg) \
    do { \
        if ((ptr) == NULL) { \
            SET_ERROR((code), "%s: NULL pointer", (msg)); \
            return (val); \
        } \
    } while (0)

/**
 * @brief 条件检查宏（跳转到标签）
 */
#define CHECK_NULL_GOTO(ptr, code, label, msg) \
    do { \
        if ((ptr) == NULL) { \
            SET_ERROR((code), "%s: NULL pointer", (msg)); \
            goto label; \
        } \
    } while (0)

/**
 * @brief 错误码检查宏
 */
#define CHECK_ERROR_RETURN(expr) \
    do { \
        MediaErrorCode __err = (expr); \
        if (IS_ERROR(__err)) { \
            return __err; \
        } \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* ERROR_H */
