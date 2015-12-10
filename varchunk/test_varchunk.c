/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <varchunk.h>

static void *
producer_main(void *arg)
{
	varchunk_t *varchunk = arg;
	void *ptr;
	size_t written;
	uint32_t cnt = 0;

	while(cnt < 10e6)
	{
		written = rand() * 1024.f / RAND_MAX;

		if( (ptr = varchunk_write_request(varchunk, written)) )
		{
			uint64_t val = cnt;
			*(uint64_t *)ptr = val;
			varchunk_write_advance(varchunk, written);
			//fprintf(stdout, "P %u %lu %zu\n", cnt, val, written);
			cnt++;
		}
		else
		{
			// buffer full
		}
	}

	return NULL;
}

static void *
consumer_main(void *arg)
{
	varchunk_t *varchunk = arg;
	const void *ptr;
	size_t toread;
	uint32_t cnt = 0;

	while(cnt < 10e6)
	{
		if( (ptr = varchunk_read_request(varchunk, &toread)) )
		{
			uint64_t val = *(uint64_t *)ptr;
			if(val != cnt)
				exit(-1); // TEST FAILED
			varchunk_read_advance(varchunk);
			//fprintf(stdout, "C %u %lu %zu\n", cnt, val, toread);
			cnt++;
		}
		else
		{
			// buffer empty
		}
	}

	return NULL;
}

int
main(int argc, char **argv)
{
	pthread_t producer;
	pthread_t consumer;
	varchunk_t *varchunk = varchunk_new(8192);

	if(!varchunk_is_lock_free())
		return -1; // TEST FAILED

	pthread_create(&consumer, NULL, consumer_main, varchunk);
	pthread_create(&producer, NULL, producer_main, varchunk);

	pthread_join(producer, NULL);
	pthread_join(consumer, NULL);

	varchunk_free(varchunk);

	return 0;
}
