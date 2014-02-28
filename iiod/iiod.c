/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

#include "../debug.h"
#include "../iio.h"
#include "ops.h"

#include <endian.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#define IIOD_VERSION "0.1"

#define IIOD_PORT 30431

struct client_data {
	int fd;
	struct iio_context *ctx;
};

static struct sockaddr_in sockaddr = {
	.sin_family = AF_INET,
#if __BYTE_ORDER == __LITTLE_ENDIAN
	.sin_addr.s_addr = __bswap_constant_32(INADDR_ANY),
	.sin_port = __bswap_constant_16(IIOD_PORT),
#else
	.sin_addr.s_addr = INADDR_ANY,
	.sin_port = IIOD_PORT,
#endif
};

static void * client_thd(void *d)
{
	struct client_data *cdata = d;
	FILE *f = fdopen(cdata->fd, "r+b");
	if (!f) {
		ERROR("Unable to reopen socket\n");
		return NULL;
	}

	interpreter(cdata->ctx, f, f);

	INFO("Client exited\n");
	fclose(f);
	close(cdata->fd);
	return NULL;
}

int main(int argc, char **argv)
{
	int fd;
	struct iio_context *ctx;
	char *backend = getenv("LIBIIO_BACKEND");

	if (backend && !strcmp(backend, "xml")) {
		if (argc < 2) {
			ERROR("The XML backend requires the XML file to be "
					"passed as argument\n");
			return EXIT_FAILURE;
		}

		DEBUG("Creating XML IIO context\n");
		ctx = iio_create_xml_context(argv[1]);
	} else {
		DEBUG("Creating local IIO context\n");
		ctx = iio_create_local_context();
	}
	if (!ctx) {
		ERROR("Unable to create local context\n");
		return EXIT_FAILURE;
	}

	DEBUG("Starting IIO Daemon version " IIOD_VERSION "\n");

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		ERROR("Unable to create socket: %s\n", strerror(errno));
		goto err_close_ctx;
	}

	if (bind(fd, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) < 0) {
		ERROR("Bind failed: %s\n", strerror(errno));
		goto err_close_socket;
	}

	if (listen(fd, 16) < 0) {
		ERROR("Unable to mark as passive socket: %s\n",
				strerror(errno));
		goto err_close_socket;
	}

	while (true) {
		pthread_t thd;
		pthread_attr_t attr;
		struct client_data *cdata;
		int new = accept(fd, NULL, NULL);
		if (new == -1) {
			ERROR("Failed to create connection socket: %s\n",
					strerror(errno));
			goto err_close_socket;
		}

		cdata = malloc(sizeof(*cdata));
		if (!cdata) {
			WARNING("Unable to allocate memory for client\n");
			close(new);
			continue;
		}

		cdata->fd = new;
		cdata->ctx = ctx;

		INFO("New client connected\n");
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_create(&thd, &attr, client_thd, cdata);
	}

	close(fd);
	iio_context_destroy(ctx);
	return EXIT_SUCCESS;

err_close_socket:
	close(fd);
err_close_ctx:
	iio_context_destroy(ctx);
	return EXIT_FAILURE;
}