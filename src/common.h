/*
 * common.h
 *
 *  Created on: May 27, 2013
 *      Author: tal
 */

#ifndef COMMON_H_
#define COMMON_H_

#include <stdio.h>
#include <apr-1.0/apr_pools.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef apr_uint32_t apr_atomic_t;

static inline int apr_abortfunc(int retcode)
{
    fprintf(stderr, "could not allocate more memory: %d", retcode);
    exit(1);
}


#define CREATE_POOL(pool, parent) \
{ \
    apr_status_t st = apr_pool_create_ex(&pool, parent, apr_abortfunc, NULL);\
    if (st != APR_SUCCESS) \
    { \
        apr_abortfunc(7000);\
    }\
}

#define RELEASE_POOL(pool) apr_pool_destroy(pool)

#define LOG4C(level, logger, msg, ...) \
{ \
    char buf[512] = {0}; \
    snprintf(buf, sizeof(buf)-1, "%s: %s (%s %s() line: %d) %s\n", level, logger, __FILE__, __FUNCTION__, __LINE__, msg); \
    fprintf(stderr, buf, ##__VA_ARGS__); \
}\

#define LOG4C_ERROR(logger, msg, ...) LOG4C("Error", logger, msg, ##__VA_ARGS__)
#define LOG4C_WARN(logger, msg, ...) LOG4C("Warn", logger, msg, ##__VA_ARGS__)
#define LOG4C_INFO(logger, msg, ...) LOG4C("Info", logger, msg, ##__VA_ARGS__)
#define LOG4C_DEBUG(logger, msg, ...) LOG4C("Debug", logger, msg, ##__VA_ARGS__)
#define LOG4C_FATAL(logger, msg, ...) {LOG4C("Debug", logger, msg, ##__VA_ARGS__); exit(1); }

#define http_serverlog_request(req)\
{\
    char *remote_ip;\
    ev_uint16_t port; \
    const char *uri = evhttp_request_get_uri(req); \
    const char *cmd = http_server_cmd_type_to_str(evhttp_request_get_command(req)); \
    struct evhttp_connection *conn = evhttp_request_get_connection(req);\
    evhttp_connection_get_peer(conn, &remote_ip, &port);\
    LOG4C_INFO(logger, "HTTP REQUEST - %s - %s - %s", remote_ip, cmd, uri);\
}


#ifdef __cplusplus
}
#endif


#endif /* COMMON_H_ */
