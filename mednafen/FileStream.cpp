/******************************************************************************/
/* Mednafen - Multi-system Emulator                                           */
/******************************************************************************/
/* FileStream.cpp:
**  Copyright (C) 2010-2020 Mednafen Team
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

#include <mednafen/types.h>
#include "FileStream.h"
#include <mednafen/mednafen.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef VITA
 #define SSIZE_MAX 2147483647
#endif

#ifdef WIN32
 #include <mednafen/win32-common.h>
#else
 #include <unistd.h>
 #include <sys/file.h>
#endif

#ifdef HAVE_MMAP
 #include <sys/mman.h>
#endif

namespace Mednafen
{

#define STRUCT_STAT struct stat

#if SIZEOF_OFF_T == 4

 #ifdef HAVE_FSEEKO64
  #undef fseeko
  #define fseeko fseeko64
 #endif

 #ifdef HAVE_FSTAT64
  #undef fstat
  #define fstat fstat64
  #undef STRUCT_STAT
  #define STRUCT_STAT struct stat64
 #endif
#endif

FileStream::FileStream(const std::string& path, const uint32 mode, const int do_lock, const uint32 buffer_size)
	: fd(-1), pos(0),
	  buf(buffer_size ? new uint8[buffer_size] : nullptr), buf_size(buffer_size), buf_write_offs(0), buf_read_offs(0), buf_read_avail(0),
	  need_real_seek(false), locked(false), mapping(NULL), mapping_size(0),
	  OpenedMode(mode), path_humesc(MDFN_strhumesc(path))
{
 int open_flags;

  // Only allow locking if the file is open for writing, to ensure compatibility with NFS on Linux.
 if(mode == MODE_READ && do_lock)
  throw MDFN_Error(0, _("FileStream MODE_READ incompatible with file locking."));

 // Only allow locking if the file is open for writing, to ensure compatibility with NFS on Linux.
 if(mode == MODE_READ && do_lock)
  throw MDFN_Error(0, _("FileStream MODE_READ incompatible with file locking."));


 switch(mode)
 {
  default:
	throw MDFN_Error(0, _("Unknown FileStream mode."));

  case MODE_READ:
	open_flags = O_RDONLY;
	break;
 }

 #ifdef O_BINARY
  open_flags |= O_BINARY;
 #elif defined(_O_BINARY)
  open_flags |= _O_BINARY;
 #endif

#ifdef WIN32
 auto perm_mode = _S_IREAD | _S_IWRITE;
#else
 auto perm_mode = S_IRUSR | S_IWUSR;
#endif

 #if defined(S_IRGRP)
 perm_mode |= S_IRGRP;
 #endif

 #if defined(S_IROTH)
 perm_mode |= S_IROTH;
 #endif

 if(path.find('\0') != std::string::npos)
  throw MDFN_Error(EINVAL, _("Error opening file \"%s\": %s"), path_humesc.c_str(), _("Null character in path."));

 #ifdef WIN32
 {
  bool invalid_utf8;
  auto tpath = Win32Common::UTF8_to_T(path, &invalid_utf8, true);

  if(invalid_utf8)
   throw MDFN_Error(EINVAL, _("Error opening file \"%s\": %s"), path_humesc.c_str(), _("Invalid UTF-8 in path."));

  fd = ::_topen((const TCHAR*)tpath.c_str(), open_flags, perm_mode);
 }
 #else
 fd = ::open(path.c_str(), open_flags, perm_mode);
 #endif

 if(fd == -1)
 {
  ErrnoHolder ene(errno);

  throw MDFN_Error(ene.Errno(), _("Error opening file \"%s\": %s"), path_humesc.c_str(), ene.StrError());
 }
}

FileStream::~FileStream()
{
   close();
}

uint64 FileStream::attributes(void)
{
 uint64 ret = ATTRIBUTE_SEEKABLE;

 switch(OpenedMode)
 {
  case MODE_READ:
	ret |= ATTRIBUTE_READABLE;
	break;
 }

 return ret;
}

uint8 *FileStream::map(void)
{
 if(!mapping)
 {
#ifdef HAVE_MMAP
  uint64 length = size();
  int prot = 0;
  int flags = 0;
  void* tptr;

  if(OpenedMode == MODE_READ)
  {
   prot |= PROT_READ; // | PROT_EXEC;
   flags |= MAP_PRIVATE;
  }
  else
  {
   prot |= PROT_WRITE;
   prot |= MAP_SHARED;
  }

  if(length > SIZE_MAX)
   return(NULL);

  tptr = mmap(NULL, length, prot, flags, fd, 0);
  if(tptr != (void*)-1)
  {
   mapping = tptr;
   mapping_size = length;

   #ifdef HAVE_MADVISE
   // Should probably make this controllable via flag or somesuch.
   madvise(mapping, mapping_size, MADV_SEQUENTIAL | MADV_WILLNEED);
   #endif
  }
#endif
 }

 return((uint8*)mapping);
}

uint64 FileStream::map_size(void)
{
 return mapping_size;
}

void FileStream::unmap(void)
{
 if(mapping)
 {
#ifdef HAVE_MMAP
  munmap(mapping, mapping_size);
#endif
  mapping = NULL;
  mapping_size = 0;
 }
}

void FileStream::set_buffer_size(uint32 new_size)
{
 if(new_size != buf_size)
 {
  flush();
  //
  std::unique_ptr<uint8[]> new_buf(new_size ? new uint8[new_size] : nullptr);

  buf = std::move(new_buf);
  buf_size = new_size;
 }
}

uint64 FileStream::read(void *data, uint64 count, bool error_on_eos)
 {
 if(buf_write_offs)
  write_buffered_data();
 //
 //
 uint8* tmp_data = (uint8*)data;
 uint64 tmp_count = count;


 while(tmp_count > 0)
 {
  if(buf_read_offs == buf_read_avail)
  {
   buf_read_offs = 0;
   buf_read_avail = 0;

   if(tmp_count >= buf_size)
   {
    const uint64 dr = read_ub(tmp_data, tmp_count);

    tmp_data += dr;
    tmp_count -= dr;
    pos += dr;

    break;
   }
   else if(!(buf_read_avail = read_ub(buf.get(), buf_size)))
    break;
  }
  //
  const uint64 tmp_copy_count = std::min<uint64>(tmp_count, buf_read_avail - buf_read_offs);

  memcpy(tmp_data, buf.get() + buf_read_offs, tmp_copy_count);
  buf_read_offs += tmp_copy_count;
  tmp_data += tmp_copy_count;
  tmp_count -= tmp_copy_count;
  pos += tmp_copy_count;
 }
 //
 //
 if(tmp_count && error_on_eos)
  throw MDFN_Error(0, _("Error reading from opened file \"%s\": %s"), path_humesc.c_str(), _("Unexpected EOF"));

 return count - tmp_count;
}

uint64 FileStream::read_ub(void* data, uint64 count)
{
 uint8* tmp_data = (uint8*)data;
 uint64 tmp_count = count;

 while(tmp_count > 0)
 {
  const ssize_t rti = std::min<uint64>(SSIZE_MAX, tmp_count);
  ssize_t dr;

  do
  {
   dr = ::read(fd, tmp_data, rti);
  } while(dr < 0 && errno == EINTR);

  if(dr < 0)
  {
   ErrnoHolder ene(errno);

   need_real_seek = true;

   throw MDFN_Error(ene.Errno(), _("Error reading from opened file \"%s\": %s"), path_humesc.c_str(), ene.StrError());
  }
  else if(!dr)
   break;

  tmp_data += dr;
  tmp_count -= dr;
 }

 return count - tmp_count;
}


uint64 FileStream::write_ub(const void* data, uint64 count)
{
 uint8* tmp_data = (uint8*)data;
 uint64 tmp_count = count;

 while(tmp_count > 0)
 {
  const ssize_t wti = std::min<uint64>(SSIZE_MAX, tmp_count);
  ssize_t dw;

  do
  {
   dw = ::write(fd, tmp_data, wti);
  } while(dw < 0 && errno == EINTR);

  if(dw < 0)
  {
   ErrnoHolder ene(errno);

   need_real_seek = true;

   throw MDFN_Error(ene.Errno(), _("Error writing to opened file \"%s\": %s"), path_humesc.c_str(), ene.StrError());
  }
  else if(!dw)
   break;

  tmp_data += dw;
  tmp_count -= dw;
 }

 return count - tmp_count;
}

void FileStream::write_buffered_data(void)
{
 const uint64 tw = buf_write_offs;
 const uint64 dw = write_ub(buf.get(), tw);

 if(dw < tw)
 {
  memmove(buf.get(), buf.get() + dw, tw - dw);
  buf_write_offs -= dw;
  //
  ErrnoHolder ene(ENOSPC);

  throw MDFN_Error(ene.Errno(), _("Error writing to opened file \"%s\": %s"), path_humesc.c_str(), ene.StrError());
 }
 else
  buf_write_offs = 0;
}

void FileStream::write(const void *data, uint64 count)
{
 if(OpenedMode == MODE_READ)
 {
  ErrnoHolder ene(EBADF);

  throw MDFN_Error(ene.Errno(), _("Error writing to opened file \"%s\": %s"), path_humesc.c_str(), ene.StrError());
 }
 //
 //
 if(buf_read_avail)
 {
  need_real_seek = true;
   seek(0, SEEK_CUR);
 }
 //
 //
 const uint8* tmp_data = (const uint8*)data;
 uint64 tmp_count = count;

 while(tmp_count > 0)
  {
  if(tmp_count >= buf_size)
  {
   if(buf_write_offs)
    write_buffered_data();
   //
   const uint64 dw = write_ub(tmp_data, tmp_count);

   if(dw < tmp_count)
   {
    ErrnoHolder ene(ENOSPC);

    throw MDFN_Error(ene.Errno(), _("Error writing to opened file \"%s\": %s"), path_humesc.c_str(), ene.StrError());
   }

   break;
  }
  //
  //
  const uint64 tmp_copy_count = std::min<uint64>(tmp_count, buf_size - buf_write_offs);

  memcpy(buf.get() + buf_write_offs, tmp_data, tmp_copy_count);
  buf_write_offs += tmp_copy_count;
  tmp_data += tmp_copy_count;
  tmp_count -= tmp_copy_count;

  if(buf_write_offs == buf_size)
   write_buffered_data();
  }

 pos += count;
 }

void FileStream::seek(int64 offset, int whence)
{
 if(MDFN_LIKELY(buf_size))
 {
  if(buf_write_offs)
   write_buffered_data();
  //
  if(whence == SEEK_CUR)
  {
   uint64 new_pos = (uint64)pos + offset;

   if((offset < 0 && (new_pos > pos)) || (offset > 0 && (new_pos < pos)))
   {
    ErrnoHolder ene(EINVAL);

    throw MDFN_Error(ene.Errno(), _("Error seeking in opened file \"%s\": %s"), path_humesc.c_str(), ene.StrError());
   }
   //
   whence = SEEK_SET;
   offset = new_pos;
  }
  //
  if(!need_real_seek && whence == SEEK_SET)
  {
   uint64 tmp_bro = (uint64)buf_read_offs + offset - pos;

   if(tmp_bro <= buf_read_avail)
   {
    //printf("Buffered seek; buf_read_offs(%llu -> %llu), pos(%llu -> %llu)\n", (unsigned long long)buf_read_offs, (unsigned long long)tmp_bro, (unsigned long long)pos, (unsigned long long)offset);
    buf_read_offs = tmp_bro;
    pos = offset;
    return;
   }
  }
  //
  buf_write_offs = 0;
  buf_read_offs = 0;
  buf_read_avail = 0;
 }
 //printf("Unbuffered seek; %llu -> %u(%llu)\n", (unsigned long long)pos, whence, (unsigned long long)offset);
#ifdef WIN32
 int64 rv = _lseeki64(fd, offset, whence);
#else
 auto rv = lseek(fd, offset, whence);
#endif
 if(rv == (decltype(rv))-1)
 {
  ErrnoHolder ene(errno);

  throw MDFN_Error(ene.Errno(), _("Error seeking in opened file \"%s\": %s"), path_humesc.c_str(), ene.StrError());
 }
 pos = (std::make_unsigned<decltype(rv)>::type)rv;
 need_real_seek = false;
}

void FileStream::flush(void)
{
 if(buf_read_avail)
 {
  need_real_seek = true;
  seek(0, SEEK_CUR);
 }
 else if(buf_write_offs)
  write_buffered_data();
}

uint64 FileStream::tell(void)
{
 return pos;
}

uint64 FileStream::size(void)
{
 if(buf_write_offs)
  write_buffered_data();

#ifdef WIN32
 DWORD h;
 uint32 rv;

 SetLastError(NO_ERROR);
 rv = GetFileSize((HANDLE)_get_osfhandle(fd), &h);
 if(rv == (uint32)-1 && GetLastError() != NO_ERROR)
 {
  const uint32 ec = GetLastError();

  throw MDFN_Error(0, _("Error getting the size of opened file \"%s\": %s"), path_humesc.c_str(), Win32Common::ErrCodeToString(ec).c_str());
 }

 return ((uint64)h << 32) | rv;
#else
 STRUCT_STAT stbuf;

 if(fstat(fd, &stbuf) == -1)
 {
  ErrnoHolder ene(errno);

  throw MDFN_Error(ene.Errno(), _("Error getting the size of opened file \"%s\": %s"), path_humesc.c_str(), ene.StrError());
 }

 return (std::make_unsigned<decltype(stbuf.st_size)>::type)stbuf.st_size;
#endif
}

void FileStream::close(void)
{
 if(fd == -1)
 {
  unmap();

  write_buffered_data();
  if(::close(fd) == -1)
  {
   ErrnoHolder ene(errno);
   fd = -1;
   throw MDFN_Error(ene.Errno(), _("Error closing opened file \"%s\": %s"), path_humesc.c_str(), ene.StrError());
  }
  fd = -1;
 }
}

int FileStream::get_line(std::string &str)
{
 int c;

 str.clear();

 while((c = get_char()) >= 0)
 {
  if(c == '\r' || c == '\n' || c == 0)
   return(c);

  str.push_back(c);
 }

 return(str.length() ? 256 : -1);
}

}
