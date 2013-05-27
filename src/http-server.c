/*
Copyright 2013 Tal Sliwowicz

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>
#include <event2/thread.h>

#include <apr-1.0/apr_pools.h>
#include <apr-1.0/apr_thread_pool.h>
#include <apr-1.0/apr_atomic.h>
#include <apr-1.0/apr_strings.h>

#ifdef _EVENT_HAVE_NETINET_IN_H
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#endif

#include "common.h"
#include "http-server.h"

static const char *logger = "http-server";

static void *http_server(apr_thread_t* t, void* d);

typedef struct http_server_data_struct {
	http_handler_t *handlers;
	int num_handlers;
	const char *static_root;
	evutil_socket_t sock;
	int server_index;
} http_server_data_t;

typedef struct callback_data_struct {
	struct evhttp_request *req;
	http_handler_cb cb;
} callback_data_t;

static apr_pool_t *global_pool;
static apr_thread_pool_t *thread_pool;
static  struct event_base **server_base = NULL;
static apr_atomic_t num_servers = 0;

/*
 * content_type_table[] and guess_content_type() were taken (with minor changes) from https://github.com/libevent/libevent/blob/master/sample/http-server.c
 * TODO: should be replaced with something better
 */
static const struct table_entry {
    const char *extension;
    const char *content_type;
} content_type_table[] = {
    { "txt", "text/plain" },
    { "c", "text/plain" },
    { "h", "text/plain" },
    { "html", "text/html" },
    { "htm", "text/htm" },
    { "css", "text/css" },
    { "gif", "image/gif" },
    { "jpg", "image/jpeg" },
    { "jpeg", "image/jpeg" },
    { "png", "image/png" },
    { "pdf", "application/pdf" },
    { "ps", "application/postsript" },
    { "json", "text/json" },
    { "xml", "text/xml" },
    { NULL, NULL },
};

/* Try to guess a good content-type for 'path' */
static const char *guess_mime_type(const char *path)
{
    char buf[12] = {0};
    char *pos = NULL;
    const char *last_period, *extension;
    const struct table_entry *ent;
    last_period = strrchr(path, '.');
    if (!last_period || strchr(last_period, '/'))
        goto not_found; /* no exension */
    extension = last_period + 1;
    strncpy(buf, extension, sizeof(buf)-1);
    pos = strpbrk(buf, "?#");
    if (pos)
        *pos = '\0';

    for (ent = &content_type_table[0]; ent->extension; ++ent)
    {
        if (!evutil_ascii_strcasecmp(ent->extension, buf))
            return ent->content_type;
    }

not_found:
    return "application/octet-stream";
}

static const char *get_path_from_uri(apr_pool_t *parent_pool, const char *uri)
{
    char *path = apr_pstrdup(parent_pool, uri);
    char *pos = strpbrk(path, "?#");
    if (pos)
        *pos = '\0';

    return path;
}

static void *internal_cb(apr_thread_t* t, void* d)
{
	callback_data_t *data = d;
	data->cb(data->req);
	free(d);
	return NULL;
}

static void http_request_cb(struct evhttp_request *req, void *arg)
{
	http_handler_t *handler = arg;
	callback_data_t *data = calloc(sizeof(callback_data_t), 1);

	http_serverlog_request(req);

	data->cb = handler->cb;
	data->req = req;

	apr_thread_pool_push(thread_pool, internal_cb, data, APR_THREAD_TASK_PRIORITY_HIGHEST, NULL);
}

static void static_files_cb(struct evhttp_request *req, void *arg)
{
    http_serverlog_request(req);
    const char *static_root = arg;
    apr_pool_t *local_pool;
    const char *full_name;
    struct stat64 file_stat;
    const char *mime_type;

    if(!static_root)
    {
        LOG4C_ERROR(logger, "static root not configured");
        evhttp_send_error(req, HTTP_NOTFOUND, "Static file server not configured");
        return;
    }

    const char *uri = evhttp_request_get_uri(req);

    if (strstr(uri, "..") != NULL)
    {
        LOG4C_ERROR(logger, "illegal URL");
        evhttp_send_error(req, HTTP_BADREQUEST, "Illegal URL");
        return;
    }

    CREATE_POOL(local_pool, NULL);

    const char *path = get_path_from_uri(local_pool, uri);

    mime_type = guess_mime_type(uri);

    LOG4C_DEBUG(logger, "mime type is %s", mime_type);

    full_name = apr_pstrcat(local_pool, static_root, "/", path, NULL);

    if (lstat64(full_name, &file_stat) < 0)
    {
        LOG4C_ERROR(logger, "file not found");
        evhttp_send_error(req, HTTP_NOTFOUND, NULL);
        return;
    }

    int fd = open(full_name, O_RDONLY);
    if (fd < 0)
    {
        LOG4C_ERROR(logger, "open failed");
        evhttp_send_error(req, HTTP_NOTFOUND, NULL);
        return;
    }

    struct evbuffer *rep_buf = evbuffer_new();

    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", mime_type);
    evbuffer_set_flags(rep_buf, EVBUFFER_FLAG_DRAINS_TO_FD);
    //TODO: LIBEVENT DOES NOT SUPPORT LARGE FILES - USES off_t BUT _FILE_OFFSET_BITS=64 IS NOT DEFINED!
    evbuffer_add_file(rep_buf, fd, 0, file_stat.st_size); //
    evhttp_send_reply(req, HTTP_OK, "OK", rep_buf);

    evbuffer_free(rep_buf);

    RELEASE_POOL(local_pool);
}

int get_num_cpus()
{
	int nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	if (nprocs < 1)
	{
		LOG4C_WARN(logger, "Could not determine number of CPUs online:\n%s\n", strerror (errno));
		return 1;
	}
	return nprocs;
}

void http_server_init()
{
    apr_status_t st;

    evthread_use_pthreads(); //ok if called from other places

    apr_initialize(); //ok if called from other places

    CREATE_POOL(global_pool, NULL);

    st = apr_thread_pool_create(&thread_pool, 20, 1000, global_pool);
    if (st != APR_SUCCESS) {
        LOG4C_FATAL(logger, "error creating pool\n");
        exit(1);
    }

    if (signal(SIGPIPE, SIG_IGN ) == SIG_ERR )
        exit(1);
}

void http_server_cleanup()
{
    apr_thread_pool_destroy(thread_pool);
    RELEASE_POOL(global_pool);
    apr_terminate(); //refcounted, so ok if other places do it too
}

int http_server_start(unsigned short port, http_handler_t http_handlers[], int num_handlers, const char *static_root)
{
	int rt;
	int num_cpus = get_num_cpus();
    http_server_data_t server_data = {0}, *data_tmp = NULL;

	evutil_socket_t sock =  socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		perror("could not create socket");
		return 1;
	}

	rt = evutil_make_listen_socket_reuseable(sock);
	if (rt < 0)
	{
		perror("cannot make socket reuseable");
		return 1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	rt = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
	if (rt < 0)
	{
		perror("cannot bind socket");
		return -1;
	}

	rt = listen(sock, 1024);
	if (rt < 0)
	{
		perror("cannot listen");
		return -1;
	}

	rt = evutil_make_socket_nonblocking(sock);
	if (rt < 0)
	{
		perror("cannot make socket non blocking");
		return -1;
	}

	server_data.handlers = http_handlers;
	server_data.num_handlers = num_handlers;
	server_data.sock = sock;
	server_data.static_root = static_root;

	int i;

	num_servers = num_cpus;

	server_base = apr_pcalloc(global_pool, num_servers * sizeof(struct event_base *));

	for(i= 0; i < num_servers; i++)
	{
	    data_tmp = calloc(1, sizeof(http_server_data_t));
	    *data_tmp = server_data;
	    data_tmp->server_index = i;
		apr_thread_pool_push(thread_pool, http_server, data_tmp, APR_THREAD_TASK_PRIORITY_HIGHEST, NULL);
	}

	return 0;
}

void http_server_stop(int to_wait)
{
    int i;
    for(i= 0; i < num_servers; i++)
    {
        if (server_base[i])
            event_base_loopbreak(server_base[i]);
    }

    if (to_wait)
    {
        while (http_server_is_active())
        {
            apr_sleep(10000); //10 ms
        }

    }
}

int http_server_is_active()
{
    int rt = apr_atomic_read32(&num_servers);
    return rt;
}

void *http_server(apr_thread_t* t, void* d)
{
	http_server_data_t *server_data = d;
	struct event_config *cfg = event_config_new();
	struct event_base *base = NULL;
	struct evhttp *http = NULL;
	int i = 0;

	event_config_set_flag(cfg, EVENT_BASE_FLAG_EPOLL_USE_CHANGELIST);

	base = event_base_new_with_config(cfg);
	if (!base)
	{
		LOG4C_ERROR(logger, "Couldn't create an event_base: exiting");
		goto done;
	}

	event_config_free(cfg);

	/* Create a new evhttp object to handle requests. */
	http = evhttp_new(base);
	if (!http)
	{
	    LOG4C_ERROR(logger, "couldn't create evhttp. Exiting.");
		goto done;
	}

	evhttp_add_server_alias(http, "TVersity-Virtual-STB");

	for (i = 0; i < server_data->num_handlers; i++)
	{
		evhttp_set_cb(http, server_data->handlers[i].uri, http_request_cb, &(server_data->handlers[i]));
	}

	//set up static file handler
	if (server_data->static_root)
	{
	    evhttp_set_gencb(http, static_files_cb, (void *) server_data->static_root);
	}

	struct evhttp_bound_socket *handle = evhttp_accept_socket_with_handle(http, server_data->sock);
	if (!handle)
	{
        LOG4C_ERROR(logger, "could not accept on socket");
		goto done;
	}

	server_base[server_data->server_index] = base;

	event_base_dispatch(base);

done:
    if(server_data)
        free(server_data);
    if(http)
    {
        evhttp_free(http);
    }
	if (base)
	{
	    event_base_free(base);
	    server_base[server_data->server_index] = NULL;
	}

	apr_atomic_dec32(&num_servers);

	return NULL;
}

const char* http_server_cmd_type_to_str(enum evhttp_cmd_type cmd)
{
    switch (cmd)
    {
        case EVHTTP_REQ_GET:
            return "GET";
        case EVHTTP_REQ_POST:
            return "POST";
        case EVHTTP_REQ_HEAD:
            return "HEAD";
        case EVHTTP_REQ_PUT:
            return "PUT";
        case EVHTTP_REQ_DELETE:
            return "DELETE";
        case EVHTTP_REQ_OPTIONS:
            return "OPTIONS";
        case EVHTTP_REQ_TRACE:
            return "TRACE";
        case EVHTTP_REQ_CONNECT:
            return "CONNECT";
        case EVHTTP_REQ_PATCH:
            return "PATCH";
        default:
            return NULL;
    }

};
