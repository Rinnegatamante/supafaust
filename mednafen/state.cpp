/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "mednafen.h"
#include "state.h"
#include "MemoryStream.h"

namespace Mednafen
{

struct StateMem
{
 StateMem(Stream*s) : st(s) { };
 ~StateMem();

 Stream* st = nullptr;
};

//
// Fast raw chunk reader/writer.
//
template<bool load>
static void FastRWChunk(Stream *st, const SFORMAT *sf)
{
 for(; sf->size != ~0U; sf++)
 {
  if(!sf->size || !sf->data)
   continue;

  int32 bytesize = sf->size;
  uintptr_t p = (uintptr_t)sf->data;
  uint32 repcount = sf->repcount;
  const size_t repstride = sf->repstride; 

  //
  // Align large variables(e.g. RAM) to a 16-byte boundary for potentially faster memory copying, before we read/write it.
  //
  if(bytesize >= 65536)
   st->seek((st->tell() + 15) &~ 15, SEEK_SET);

  do
  {
   if(load)
    st->read((void*)p, bytesize);
   else
    st->write((void*)p, bytesize);
  } while(p += repstride, repcount--);
 }
}

//
// When updating this function make sure to adhere to the guarantees in state.h.
//
bool MDFNSS_StateAction(StateMem *sm, const unsigned load, const bool data_only, const SFORMAT *sf, const char *sname)
{
   Stream* st = sm->st;
   static const uint8 SSFastCanary[8] = { 0x42, 0xA3, 0x10, 0x87, 0xBC, 0x6D, 0xF2, 0x79 };
   char sname_canary[32 + 8];

   if(load)
   {
      st->read(sname_canary, 32 + 8);
      FastRWChunk<true>(st, sf);
   }
   else
   {
      memset(sname_canary, 0, sizeof(sname_canary));
      strncpy(sname_canary, sname, 32);
      memcpy(sname_canary + 32, SSFastCanary, 8);
      st->write(sname_canary, 32 + 8);

      FastRWChunk<false>(st, sf);
   }
   return true;
}

StateMem::~StateMem(void)
{

}

void MDFNSS_SaveSM(Stream* s, bool data_only)
{
 s->put_NE<uint32>(MEDNAFEN_VERSION_NUMERIC);
 StateMem sm(s);
 MDFN_StateAction(&sm, 0, true);
}

void MDFNSS_LoadSM(Stream* s, bool data_only)
{
 const uint32 version = s->get_NE<uint32>();
 StateMem sm(s);
 MDFN_StateAction(&sm, version, true);
}

}
