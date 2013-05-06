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


#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <event2/event.h>
#include <event2/http.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>


#ifdef __cplusplus
extern "C" {
#endif

//TODO: consider adding apr_pool_t as a convenience parameter
typedef void (*http_handler_cb)(struct evhttp_request *req);

typedef struct http_handler_struct {
	http_handler_cb cb;
	char *uri;
} http_handler_t;

int http_server_start(unsigned short port, http_handler_t http_handlers[], int num_handlers);

#ifdef __cplusplus
}
#endif



#endif /* HTTP_SERVER_H_ */
