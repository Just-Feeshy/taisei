/*
 * This software is licensed under the terms of the MIT License.
 * See COPYING for further information.
 * ---
 * Copyright (c) 2011-2019, Lukas Weber <laochailan@web.de>.
 * Copyright (c) 2012-2019, Andrei Alexeyev <akari@taisei-project.org>.
 */

#include "taisei.h"

#include "rwops_autobuf.h"
#include "rwops_segment.h"

typedef struct Buffer {
	SDL_IOStream *memio;
	void *data;
	void **ptr;
	size_t size;
} Buffer;

static void auto_realloc(Buffer *b, size_t newsize) {
	size_t pos = SDL_TellIO(b->memio);
	SDL_CloseIO(b->memio);

	b->size = newsize;
	b->data = mem_realloc(b->data, b->size);
	b->memio = SDL_IOFromMem(b->data, b->size);

	if(b->ptr) {
		*b->ptr = b->data;
	}

	if(pos > 0) {
		SDL_SeekIO(b->memio, pos, SDL_IO_SEEK_SET);
	}
}

static int auto_close(void *ctx) {
	Buffer *b = ctx;
	SDL_CloseIO(b->memio);
	mem_free(b->data);
	mem_free(b);
	return 0;
}

static int64_t auto_seek(void *ctx, int64_t offset, int whence) {
	Buffer *b = ctx;
	return SDL_SeekIO(b->memio, offset, whence);
}

static int64_t auto_size(void *ctx) {
	Buffer *b = ctx;
	// return SDL_GetIOSize(b->memrw);
	return b->size;
}

static size_t auto_read(void *ctx, void *ptr, size_t size, SDL_IOStatus *status) {
	Buffer *b = ctx;
	size = SDL_ReadIO(b->memio, ptr, size);
	*status = SDL_GetIOStatus(b->memio);
	return size;
}

static size_t auto_write(void *ctx, const void *ptr, size_t size, SDL_IOStatus *status) {
	Buffer *b = ctx;
	size_t newsize = b->size;

	while(size > newsize - SDL_TellIO(b->memio)) {
		newsize *= 2;
	}

	if(newsize > b->size) {
		auto_realloc(b, newsize);
	}

	size = SDL_WriteIO(b->memio, ptr, size);
	*status = SDL_GetIOStatus(b->memio);
	return size;
}

SDL_IOStream *SDL_RWAutoBuffer(void **ptr, size_t initsize) {
	SDL_IOStreamInterface iface = {
		.close = auto_close,
		.read = auto_read,
		.seek = auto_seek,
		.size = auto_size,
		.write = auto_write,
	};

	auto b = ALLOC(Buffer, {
		.size = initsize,
		.ptr = ptr,
	});

	SDL_IOStream *io = SDL_OpenIO(&iface, b);

	if(UNLIKELY(!io)) {
		mem_free(b);
		return NULL;
	}

	b->memio = NOT_NULL(SDL_IOFromMem(b->data, b->size));
	b->data = mem_alloc(initsize);

	if(ptr) {
		*ptr = b->data;
	}

	return io;
}
