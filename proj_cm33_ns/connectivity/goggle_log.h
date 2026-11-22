/*******************************************************************************
* File Name        : goggle_log.h
*
* Description      : Logging for the CM33 connectivity code. The CM55 owns the
*                    debug UART (retarget-io), so the CM33 writes its log lines
*                    into the shared-memory ring and the CM55 graphics task
*                    prints them with the prefix "[CM33] ".
*
*******************************************************************************/
#ifndef GOGGLE_LOG_H
#define GOGGLE_LOG_H

#if defined(__cplusplus)
extern "C" {
#endif

/* printf-style, one line per call ("\r\n" is appended), max 159 chars */
void gl_log_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#if !defined(GL_LOG_ENABLE)
#define GL_LOG_ENABLE   (1)
#endif

#if (GL_LOG_ENABLE)
#define GL_LOG(...)     gl_log_printf(__VA_ARGS__)
#else
#define GL_LOG(...)     do { } while (0)
#endif

#if defined(__cplusplus)
}
#endif

#endif /* GOGGLE_LOG_H */
