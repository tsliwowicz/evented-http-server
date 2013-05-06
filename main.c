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
#include "http-server.h"

#define json_output "{\"message\":\"Hello, World!\"}"
#define xml_output "<message>hello, world!</message>"


static void json_handler(struct evhttp_request *req);
static void xml_handler(struct evhttp_request *req);

//TODO: add - generic handler
//TODO: add - files handler
static http_handler_t handlers[] = {
		{json_handler, "/json"},
		{xml_handler, "/xml"}
};

#define NUM_HANDLERS sizeof(handlers)/sizeof(handlers[0])

void xml_handler(struct evhttp_request *req)
{
	struct evbuffer *rep_buf = evbuffer_new();

	evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type",
			"text/xml ; charset=UTF-8");

	evhttp_add_header(evhttp_request_get_output_headers(req), "Server",
			"Example");

	evbuffer_add(rep_buf, xml_output, strlen(xml_output));

	evhttp_send_reply(req, 200, "OK", rep_buf);

	evbuffer_free(rep_buf);
}

void json_handler(struct evhttp_request *req)
{
	struct evbuffer *rep_buf = evbuffer_new();

	evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type",
			"text/json ; charset=UTF-8");

	evhttp_add_header(evhttp_request_get_output_headers(req), "Server",
			"Example");

	evbuffer_add(rep_buf, json_output, strlen(json_output));

	evhttp_send_reply(req, 200, "OK", rep_buf);

	evbuffer_free(rep_buf);
}

int main(int c, char **v)
{
	http_server_start(8080, handlers, NUM_HANDLERS);
	return(0);
}
