/*
 * This software is licensed under the terms of the MIT License.
 * See COPYING for further information.
 * ---
 * Copyright (c) 2011-2019, Lukas Weber <laochailan@web.de>.
 * Copyright (c) 2012-2019, Andrei Alexeyev <akari@taisei-project.org>.
 */

#include "taisei.h"

#include "rwops_segment.h"
#include "util.h"

typedef struct Segment {
	SDL_IOStream *wrapped;
	size_t start;
	size_t end;
	int64_t pos; // fallback for non-seekable streams
	bool autoclose;
} Segment;

#define SEGMENT(rw) ((Segment*)((rw)->hidden.unknown.data1))

static int64_t segment_seek(SDL_IOStream *rw, int64_t offset, int whence) {
	Segment *s = SEGMENT(rw);

	switch(whence) {
		case SDL_IO_SEEK_CUR : {
			if(offset) {
				int64_t pos = SDL_TellIO(s->wrapped);

				if(pos < 0) {
					return pos;
				}

				if(pos + offset > s->end) {
					offset = s->end - pos;
				} else if(pos + offset < s->start) {
					offset = s->start - pos;
				}
			}

			break;
		}

		case SDL_IO_SEEK_SET : {
			offset += s->start;

			if(offset > s->end) {
				offset = s->end;
			}

			break;
		}

		case SDL_IO_SEEK_END : {
			int64_t size = SDL_SizeIO(s->wrapped);

			if(size < 0) {
				return size;
			}

			if(size > s->end) {
				offset -= size - s->end;
			}

			if(size + offset < s->start) {
				offset += s->start - (size + offset);
			}

			break;
		}

		default: {
			SDL_SetError("Bad whence value %i", whence);
			return -1;
		}
	}

	int64_t result = SDL_SeekIO(s->wrapped, offset, whence);

	if(result > 0) {
		if(s->start > result) {
			result = 0;
		} else {
			result -= s->start;
		}
	}

	return result;
}

static int64_t segment_size(SDL_IOStream *rw) {
	Segment *s = SEGMENT(rw);
	int64_t size = SDL_SizeIO(s->wrapped);

	if(size < 0) {
		return size;
	}

	if(size > s->end) {
		size = s->end;
	}

	return size - s->start;
}

static size_t segment_readwrite(SDL_IOStream *rw, void *ptr, size_t size,
				size_t maxnum, bool write) {
	Segment *s = SEGMENT(rw);
	int64_t pos = SDL_TellIO(s->wrapped);
	size_t onum;

	if(pos < 0) {
		log_debug("SDL_TellIO failed (%i): %s", (int)pos, SDL_GetError());
		SDL_SetError("segment_readwrite: SDL_TellIO failed (%i) %s", (int)pos, SDL_GetError());

		// this could be a non-seekable stream, like /dev/stdin...
		// let's assume nothing else uses the wrapped stream and try to guess the current position
		// this only works if the actual positon in the stream at the time of segment creation matched s->start...
		pos = s->pos;
	} else {
		s->pos = pos;
	}

	if(pos < s->start || pos > s->end) {
		log_warn("Segment range violation");
		SDL_SetError("segment_readwrite: segment range violation");
		return 0;
	}

	int64_t maxsize = s->end - pos;

	while(size * maxnum > maxsize) {
		if(!--maxnum) {
			return 0;
		}
	}

	if(write) {
		onum = SDL_WriteIO(s->wrapped, ptr, size * maxnum);
		onum = (onum <= 0) ? 0 : onum / size;
	} else {
		onum = SDL_ReadIO(s->wrapped, ptr, size * maxnum);
		onum = (onum <= 0) ? 0 : onum / size;
	}

	s->pos += onum / size;
	assert(s->pos <= s->end);

	return onum;
}

static size_t segment_read(SDL_IOStream *rw, void *ptr, size_t size,
			   size_t maxnum) {
	return segment_readwrite(rw, ptr, size, maxnum, false);
}

static size_t segment_write(SDL_IOStream *rw, const void *ptr, size_t size,
			    size_t maxnum) {
	return segment_readwrite(rw, (void*)ptr, size, maxnum, true);
}

static int segment_close(SDL_IOStream *rw) {
	if(rw) {
		Segment *s = SEGMENT(rw);

		if(s->autoclose) {
			SDL_CloseIO(s->wrapped);
		}

		mem_free(s);
		SDL_FreeRW(rw);
	}

	return 0;
}

SDL_IOStream *SDL_RWWrapSegment(SDL_IOStream *src, size_t start, size_t end,
				bool autoclose) {
	if(UNLIKELY(!src)) {
		return NULL;
	}

	assert(end > start);

	SDL_IOStream *rw = SDL_AllocRW();

	if(UNLIKELY(!rw)) {
		return NULL;
	}

	memset(rw, 0, sizeof(SDL_IOStream));

	rw->type = SDL_RWOPS_UNKNOWN;
	rw->seek = segment_seek;
	rw->size = segment_size;
	rw->read = segment_read;
	rw->write = segment_write;
	rw->close = segment_close;
	rw->hidden.unknown.data1 = ALLOC(Segment, {
		.wrapped = src,
		.start = start,
		.end = end,
		.autoclose = autoclose,

		// fallback for non-seekable streams
		.pos = start,
	});

	return rw;
}
