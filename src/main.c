/*
+-----------------------------------------------------------------------------------+
|  thumby                                                                           |
|  Copyright (c) 2013, Mikko Koppanen <mikko.koppanen@gmail.com>                    |
|  All rights reserved.                                                             |
+-----------------------------------------------------------------------------------+
|  Redistribution and use in source and binary forms, with or without               |
|  modification, are permitted provided that the following conditions are met:      |
|     * Redistributions of source code must retain the above copyright              |
|       notice, this list of conditions and the following disclaimer.               |
|     * Redistributions in binary form must reproduce the above copyright           |
|       notice, this list of conditions and the following disclaimer in the         |
|       documentation and/or other materials provided with the distribution.        |
|     * Neither the name of the copyright holder nor the                            |
|       names of its contributors may be used to endorse or promote products        |
|       derived from this software without specific prior written permission.       |
+-----------------------------------------------------------------------------------+
|  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND  |
|  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED    |
|  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           |
|  DISCLAIMED. IN NO EVENT SHALL MIKKO KOPPANEN BE LIABLE FOR ANY                   |
|  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES       |
|  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;     |
|  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND      |
|  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT       |
|  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS    |
|  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                     |
+-----------------------------------------------------------------------------------+
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>

#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <stdbool.h>
#include <unistd.h>

#include <wand/MagickWand.h>

#define THREAD_POOL_SIZE 4

#define MAX_THUMBNAIL_WIDTH 1920
#define MAX_THUMBNAIL_HEIGHT 1080

#define DEFAULT_PORT 8800

#define HANDLER_PATH "/thumb/"

static
int s_bind_socket (uint16_t port, int backlog)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert (fd >= 0);

    int value = 1;
    int rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &value, sizeof (value));
    assert (rc == 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons (port);

    rc = bind (fd, (struct sockaddr*)&addr, sizeof(addr));
    assert (rc == 0);

    rc = listen(fd, backlog);
    assert (rc == 0);

    int flags = fcntl(fd, F_GETFL, 0);
    assert (flags != -1);

    rc = fcntl (fd, F_SETFL, flags | O_NONBLOCK);
    assert (rc != -1);

    return fd;
}

static
size_t s_count_chr (const char *str, char chr)
{
    size_t count = 0;
    const char *p = str;
    while (*p)
        if (*p++ == chr)
            count++;

    return count;
}

static
long s_to_long (const char *input)
{
    if (!input)
        return -1;

    char *end;
    long value = strtol (input, &end, 10);

    if ((errno == ERANGE && (value == LONG_MAX || value == LONG_MIN))
       || (errno != 0 && value == 0)) {
           return -1;
    }
    return value;
}

static
char *s_get_mimetype (MagickWand *magick_wand)
{
	char *format = MagickGetImageFormat (magick_wand);

    if (!format)
        return NULL;

	char *mime_type = MagickToMime (format);
    MagickRelinquishMemory (format);

    return mime_type;
}

static
char *s_get_filename (const char *uri)
{
    struct evhttp_uri *decoded = evhttp_uri_parse (uri);

    if (!decoded)
        return NULL;

    const char *path = evhttp_uri_get_path (decoded);

    if (!path) {
        evhttp_uri_free (decoded);
        return NULL;
    }

    char *decoded_path = evhttp_uridecode (path, 0, NULL);
    if (!decoded_path) {
        evhttp_uri_free (decoded);
        return NULL;
    }
    evhttp_uri_free (decoded);

    // Now parse filename
    char *pch = strrchr (decoded_path, '/');
    if (!pch) {
        free (decoded_path);
        return NULL;
    }
    char *filename = strdup (pch + 1);
    free (decoded_path);

    return filename;
}

static
long s_find_header_long (struct evkeyvalq *values, const char *key)
{
    const char *str = evhttp_find_header (values, key);

    if (!str)
        return -1;

    return s_to_long (str);
}

static
bool s_resize_image (MagickWand *magick_wand, long width, long height)
{
    if (width || height) {
        if (width == 0 || height == 0) {
            double ratio;
            long image_width = MagickGetImageWidth (magick_wand);
            long image_height = MagickGetImageHeight (magick_wand);

            if (!width) {
                ratio  = (double) image_height / (double) height;
                width  = (double) image_width / ratio;
            } else {
                ratio  = (double) image_width / (double) width;
                height = (double) image_height / ratio;
            }
        }
        if (MagickThumbnailImage (magick_wand, width, height) != MagickTrue) {
            return false;
        }
    }
    return true;
}

static
void s_set_mimetype (MagickWand *magick_wand, struct evhttp_request *req)
{
    char *mime_type = s_get_mimetype (magick_wand);

    if (mime_type) {
        evhttp_add_header (
            evhttp_request_get_output_headers (req), "Content-Type", mime_type
        );
        MagickRelinquishMemory (mime_type);
    } else {
        evhttp_add_header (
            evhttp_request_get_output_headers (req), "Content-Type", "application/octet-stream"
        );
    }
}

static
void s_set_headers (struct evhttp_request *req, size_t data_len)
{
    evhttp_add_header (
        evhttp_request_get_output_headers (req), "Connection", "close"
    );

    char data_len_str [48];
    snprintf (data_len_str, 48, "%ld", data_len);

    evhttp_add_header (
        evhttp_request_get_output_headers (req), "Content-Length", data_len_str
    );
}

static void
s_create_thumbnail_cb (struct evhttp_request *req, void *arg)
{
    MagickWand *magick_wand = (MagickWand *) arg;

    const char *req_uri = evhttp_request_get_uri (req);

    char *filename = NULL;
    size_t data_len;
    unsigned char *image_data = NULL;

    int code = HTTP_INTERNAL;
    const char *message = "Internal Server Error";

    // whether it's a thumbnail request
    if ((strncmp (req_uri, HANDLER_PATH, sizeof (HANDLER_PATH) - 1) == 0) && s_count_chr (req_uri, '/') == 2) {

        filename = s_get_filename (req_uri);

        if (!filename) {
            evhttp_send_error (req, HTTP_BADREQUEST, 0);
            return;
	    }

        struct evkeyvalq values;
        if (evhttp_parse_query (req_uri, &values) == 0) {
            long width = 0, height = 0;

            if ((width = s_find_header_long (&values, "w")) < 0 || width > MAX_THUMBNAIL_WIDTH)
                width = 0;

            if ((height = s_find_header_long (&values, "h")) < 0 || height > MAX_THUMBNAIL_HEIGHT)
                height = 0;

            if (MagickReadImage (magick_wand, filename) != MagickTrue) {
                code = HTTP_NOTFOUND;
                message = "Document was not found";

                goto error_reply;
            }

            if (s_resize_image (magick_wand, width, height) == false) {
                goto error_reply;
            }

            s_set_mimetype (magick_wand, req);

            // Response
            image_data = MagickGetImageBlob (magick_wand, &data_len);
            if (!image_data) {
                goto error_reply;
            }

            struct evbuffer *evb = evbuffer_new ();
            if (!evb) {
                goto error_reply;
            }

            evbuffer_add (evb, image_data, data_len);

            s_set_headers (req, data_len);

            evhttp_send_reply (req, HTTP_OK, "OK", evb);
            evbuffer_free (evb);

            MagickRelinquishMemory (image_data);
            ClearMagickWand (magick_wand);
        }
        free (filename);
    }
    evhttp_send_error (req, HTTP_NOTFOUND, "Document was not found");
    return;

error_reply:
    if (filename)
        free (filename);

    if (image_data)
        MagickRelinquishMemory (image_data);

    ClearMagickWand (magick_wand);
    evhttp_send_error (req, code, message);
    return;
}

static
void *s_dispatch (void *arg)
{
    event_base_dispatch ((struct event_base *) arg);
    return NULL;
}

static
void s_time_to_go (evutil_socket_t sig, short events, void *user_data)
{
    struct event_base *base = user_data;
    struct timeval delay = { 2, 0 };

    event_base_loopexit(base, &delay);
}

static
bool s_daemonize ()
{
    signal(SIGINT, SIG_IGN);
    signal(SIGKILL, SIG_IGN);

    int rc = fork ();
    if (rc == -1) {
        fprintf (stderr, "Failed to fork: %s\n", strerror (errno));
        return false;
    }

    if (rc == 0) {
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);
    } else {
        // parent
        signal(SIGINT, SIG_DFL);
        signal(SIGKILL, SIG_DFL);
        exit(0);
    }
    return true;
}

int main (int argc, char **argv)
{
    MagickWandGenesis ();
    int port = DEFAULT_PORT;
    const char *images_dir = "./";

    if (argc > 1) {
        images_dir = argv [1];

        if (chdir (images_dir) == -1) {
            fprintf (stderr, "Failed to change directory to %s: %s", images_dir, strerror (errno));
            return 1;
        }

        if (argc > 2) {
            port = atoi (argv [2]);
            if (port == 0) {
                fprintf (stderr, "Invalid port: %s\n", argv [2]);
                return 1;
            }
        }
    }

    fprintf (stderr, "Serving images from %s on 0.0.0.0:%d\n", images_dir, port);

    if (s_daemonize () == false)
        return 1;

    int fd = s_bind_socket (port, 1024);
    size_t thread_count = THREAD_POOL_SIZE;

    pthread_t ths [thread_count];

    int i;
    for (i = 0; i < thread_count; i++) {
        struct event_base *base = event_base_new ();
        assert (base);

        struct evhttp *http = evhttp_new (base);
        assert (http);

        int rc = evhttp_accept_socket (http, fd);
        assert (rc == 0);

        struct event *signal_event = evsignal_new (base, SIGINT, s_time_to_go, base);
        event_add (signal_event, NULL);

        signal_event = evsignal_new (base, SIGHUP, s_time_to_go, base);
        event_add (signal_event, NULL);

        signal_event = evsignal_new (base, SIGTERM, s_time_to_go, base);
        event_add (signal_event, NULL);

        MagickWand *magick_wand = NewMagickWand ();
        evhttp_set_gencb (http, s_create_thumbnail_cb, magick_wand);

        rc = pthread_create (&ths[i], NULL, s_dispatch, base);
        assert (rc == 0);
    }

    for (i = 0; i < thread_count; i++) {
        pthread_join (ths[i], NULL);
    }

    MagickWandTerminus ();
    return 0;
}
