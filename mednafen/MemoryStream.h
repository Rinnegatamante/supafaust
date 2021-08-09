/******************************************************************************/
/* Mednafen - Multi-system Emulator                                           */
/******************************************************************************/
/* MemoryStream.h:
**  Copyright (C) 2012-2016 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
 Notes:
	For performance reasons(like in the state rewinding code), we should try to make sure map()
	returns a pointer that is aligned to at least what malloc()/realloc() provides.
	(And maybe forcefully align it to at least 16 bytes in the future)
*/

#ifndef __MDFN_MEMORYSTREAM_H
#define __MDFN_MEMORYSTREAM_H

#include "Stream.h"

namespace Mednafen
{

class MemoryStream : public Stream
{
 public:

 MemoryStream();
 MemoryStream(uint64 alloc_hint, int alloc_hint_is_size = false);	// Pass -1 instead of 1 for alloc_hint_is_size to skip initialization of the memory.
 MemoryStream(Stream *stream, uint64 size_limit = ~(uint64)0);
				// Will create a MemoryStream equivalent of the contents of "stream", and then "delete stream".
				// Will only work if stream->tell() == 0, or if "stream" is seekable.
				// stream will be deleted even if this constructor throws.
				//
				// Will throw an exception if the initial size() of the MemoryStream would be greater than size_limit(useful for when passing
				// in GZFileStream streams).

 MemoryStream(const MemoryStream &zs);
 MemoryStream & operator=(const MemoryStream &zs);

 virtual ~MemoryStream();

 virtual uint64 attributes(void);

 virtual uint8 *map(void);
 virtual uint64 map_size(void);
 virtual void unmap(void);

 virtual uint64 read(void *data, uint64 count, bool error_on_eos = true);
 virtual void write(const void *data, uint64 count);
 virtual void seek(int64 offset, int whence);
 virtual uint64 tell(void);
 virtual uint64 size(void);
 virtual void flush(void);
 virtual void close(void);

 virtual int get_line(std::string &str);

 void shrink_to_fit(void);	// Minimizes alloced memory.

 // No methods on the object may be called externally(other than the destructor) after steal_malloced_ptr()
 INLINE void* steal_malloced_ptr(void)
 {
  void* ret = data_buffer;

  data_buffer = nullptr;
  data_buffer_size = 0;
  data_buffer_alloced = 0;
  position = 0;

  return ret;
 }

 private:
 uint8 *data_buffer;
 uint64 data_buffer_size;
 uint64 data_buffer_alloced;

 uint64 position;

 void grow_if_necessary(uint64 new_required_size, uint64 hole_end);
};

}
#endif
