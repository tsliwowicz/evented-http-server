
/*
 * Copyright 2013 Tal Sliwowicz

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
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

#ifdef _EVENT_HAVE_NETINET_IN_H
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#endif

#include "http-server.h"

static apr_pool_t *global_pool;
static apr_thread_pool_t *thread_pool;

static void *http_server(apr_thread_t* t, void* d);

typedef struct http_server_data_struct {
	http_handler_t *handlers;
	int num_handlers;
	evutil_socket_t sock;
} http_server_data_t;

typedef struct callback_data_struct {
	struct evhttp_request *req;
	http_handler_cb cb;
} callback_data_t;



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

	data->cb = handler->cb;
	data->req = req;

	apr_thread_pool_push(thread_pool, internal_cb, data, APR_THREAD_TASK_PRIORITY_HIGHEST, NULL);
}

static int mem_abort(int retcode)
{
	fprintf(stderr, "error allocating\n");
	exit(1);
}

int get_num_cpus()
{
	int nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	if (nprocs < 1)
	{
		fprintf(stderr, "Could not determine number of CPUs online:\n%s\n",
				strerror (errno));
		exit(1);
	}
	return nprocs;
}

int http_server_start(unsigned short port, http_handler_t http_handlers[], int num_handlers)
{
	apr_status_t st;
	int rt;
	int num_cpus = get_num_cpus();
	http_server_data_t server_data = {0};

	evthread_use_pthreads();

	apr_initialize();

	st = apr_pool_create_ex(&global_pool, NULL, mem_abort, NULL );
	if (st != APR_SUCCESS) {
		fprintf(stderr, "error creating pool\n");
		exit(1);
	}

	st = apr_thread_pool_create(&thread_pool, 20, 1000, global_pool);
	if (st != APR_SUCCESS) {
		fprintf(stderr, "error creating pool\n");
		exit(1);
	}

	if (signal(SIGPIPE, SIG_IGN ) == SIG_ERR )
		return (1);

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

	int i;

	for(i= 0; i < num_cpus; i++)
	{
		apr_thread_pool_push(thread_pool, http_server, &server_data, APR_THREAD_TASK_PRIORITY_HIGHEST, NULL);
	}

	//TODO: check number of worker threads + add a thread safe stop_server function
	while(1)
	{
		sleep(10);
	}

	apr_thread_pool_destroy(thread_pool);

	apr_pool_destroy(global_pool);

	apr_terminate();

	return 0;
}


void *http_server(apr_thread_t* t, void* d)
{
	http_server_data_t *server_data = d;
	struct event_config *cfg = event_config_new();
	struct event_base *base;
	struct evhttp *http;
	int i = 0;

	event_config_set_flag(cfg, EVENT_BASE_FLAG_EPOLL_USE_CHANGELIST);

	base = event_base_new_with_config(cfg);
	if (!base)
	{
		fprintf(stderr, "Couldn't create an event_base: exiting\n");
		goto done;
	}

	event_config_free(cfg);

	/* Create a new evhttp object to handle requests. */
	http = evhttp_new(base);
	if (!http)
	{
		fprintf(stderr, "couldn't create evhttp. Exiting.\n");
		goto done;
	}


	for (i = 0; i < server_data->num_handlers; i++)
	{
		evhttp_set_cb(http, server_data->handlers[i].uri, http_request_cb, &(server_data->handlers[i]));
	}

	struct evhttp_bound_socket *handle = evhttp_accept_socket_with_handle(http, server_data->sock);
	if (!handle)
	{
		perror("could not accept on socket");
		goto done;
	}

	event_base_dispatch(base);

done:
	return NULL;
}
