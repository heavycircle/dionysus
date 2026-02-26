#ifndef MODULES_FRONTEND_LOGGING_LOGGING_H
#define MODULES_FRONTEND_LOGGING_LOGGING_H

void logging_print(char level, const char *fmt, ...);

#ifndef NDEBUG

#define LOG_DEBUG(fmt, ...) logging_print('D', fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  logging_print('I', fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  logging_print('W', fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) logging_print('E', fmt, ##__VA_ARGS__)

#else

#define LOG_DEBUG(fmt, ...)
#define LOG_INFO(fmt, ...)
#define LOG_WARN(fmt, ...)
#define LOG_ERROR(fmt, ...)

#endif

#endif // MODULES_FRONTEND_LOGGING_LOGGING_H
