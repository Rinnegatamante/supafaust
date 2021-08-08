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

#include "string/string.h"

#include "MemoryStream.h"
#include "state.h"
#include "video/Deinterlacer.h"

using namespace Mednafen;

namespace Mednafen
{

NativeVFS NVFS;

static void SettingChanged(const char* name);

static const MDFNSetting_EnumList Deinterlacer_List[] =
{
 { "weave", Deinterlacer::DEINT_WEAVE, gettext_noop("Good for low-motion video; can be used in conjunction with negative <system>.scanlines setting values.") },
 { "bob", Deinterlacer::DEINT_BOB, gettext_noop("Good for causing a headache.  All glory to Bob.") },
 { "bob_offset", Deinterlacer::DEINT_BOB_OFFSET, gettext_noop("Good for high-motion video, but is a bit flickery; reduces the subjective vertical resolution.") },

 { "blend", Deinterlacer::DEINT_BLEND, gettext_noop("Blend fields together; reduces vertical and temporal resolution.") },
 { "blend_rg", Deinterlacer::DEINT_BLEND_RG, gettext_noop("Like the \"blend\" deinterlacer, but the blending is done in a manner that respects gamma, reducing unwanted brightness changes, at the cost of increased CPU usage.") },

 { NULL, 0 },
};

static const MDFNSetting MednafenSettings[] =
{
 { "video.deinterlacer", MDFNSF_CAT_VIDEO, gettext_noop("Deinterlacer to use for interlaced video."), NULL, MDFNST_ENUM, "weave", NULL, NULL, NULL, SettingChanged, Deinterlacer_List },

 { NULL }
};

static uint32 PortDevice[16];
static uint8* PortData[16];
static uint32 PortDataLen[16];

MDFNGI *MDFNGameInfo = NULL;

static double last_sound_rate;
static MDFN_PixelFormat last_pixel_format;
static bool PrevInterlaced;
static std::unique_ptr<Deinterlacer> deint;

static void SettingChanged(const char* name)
{
 if(!strcmp(name, "video.deinterlacer"))
 {
  deint.reset(nullptr);
  deint.reset(Deinterlacer::Create(MDFN_GetSettingUI(name)));
 }
}

void MDFNI_CloseGame(void)
{
 if(MDFNGameInfo)
 {
  MDFNGameInfo->CloseGame();
  MDFNGameInfo = NULL;
 }

 for(unsigned x = 0; x < 16; x++)
 {
  if(PortData[x])
  {
   free(PortData[x]);
   PortData[x] = NULL;
  }

  PortDevice[x] = ~0U;
  PortDataLen[x] = 0;
 }
}

}

extern MDFNGI EmulatedSNES_Faust;

namespace Mednafen
{
std::vector<MDFNGI *> MDFNSystems;
static std::list<MDFNGI *> MDFNSystemsPrio;

bool MDFNSystemsPrio_CompareFunc(const MDFNGI* first, const MDFNGI* second)
{
 if(first->ModulePriority > second->ModulePriority)
  return true;

 return false;
}

static void AddSystem(MDFNGI *system)
{
 MDFNSystems.push_back(system);
}
static MDFN_COLD void LoadCommonPost(void)
{
        //
        //
        //
	MDFNI_SetLayerEnableMask(~0ULL);

	PrevInterlaced = false;
	SettingChanged("video.deinterlacer");

	last_sound_rate = -1;
	last_pixel_format = MDFN_PixelFormat();
}

static MDFN_COLD MDFNGI* FindCompatibleModule(GameFile* gf)
{
 //for(unsigned pass = 0; pass < 2; pass++)
 //{
	for(std::list<MDFNGI *>::iterator it = MDFNSystemsPrio.begin(); it != MDFNSystemsPrio.end(); it++)  //_unsigned int x = 0; x < MDFNSystems.size(); x++)
	{
	  if(!(*it)->Load || !(*it)->TestMagic)
	   continue;

	  gf->stream->rewind();

	  if((*it)->TestMagic(gf))
	   return(*it);
	}
 //}

 return(NULL);
}

MDFNGI *MDFNI_LoadGame(GameFile* gf)
{
 MDFNI_CloseGame();

 try
 {
	MDFN_AutoIndent aind(1);

	MDFNGameInfo = FindCompatibleModule(gf);

        if(!MDFNGameInfo)
          throw MDFN_Error(0, _("Unrecognized file format."));

	MDFN_printf(_("Using module: %s(%s)\n"), MDFNGameInfo->shortname, MDFNGameInfo->fullname);
	{
	 MDFN_AutoIndent aindentgm(1);

	 assert(MDFNGameInfo->soundchan != 0);

	 MDFNGameInfo->DesiredInput.clear();

	 MDFN_printf("\n");

	 gf->stream->rewind();
         MDFNGameInfo->Load(gf);
	}

	LoadCommonPost();
 }
 catch(...)
 {
  MDFNGameInfo = nullptr;

  throw;
 }

 return MDFNGameInfo;
}

static bool InitializeModules(void)
{
 static MDFNGI *InternalSystems[] =
 {
  &EmulatedSNES_Faust,
 };
 for(unsigned int i = 0; i < sizeof(InternalSystems) / sizeof(MDFNGI *); i++)
  AddSystem(InternalSystems[i]);

 for(unsigned int i = 0; i < MDFNSystems.size(); i++)
  MDFNSystemsPrio.push_back(MDFNSystems[i]);

 MDFNSystemsPrio.sort(MDFNSystemsPrio_CompareFunc);
 //
 //
 //
 std::string modules_string;
 for(auto& m : MDFNSystemsPrio)
 {
  if(modules_string.size())
   modules_string += " ";
  modules_string += std::string(m->shortname);
 }
 MDFNI_printf(_("Emulation modules: %s\n"), modules_string.c_str());

 return(1);
}

void MDFNI_Initialize(void)
{
	InitializeModules();
	//
	//
	//
	for(unsigned x = 0; x < 16; x++)
	{
	 PortDevice[x] = ~0U;
	 PortData[x] = NULL;
	 PortDataLen[x] = 0;
	}

	// First merge all settable settings, then load the settings from the SETTINGS FILE OF DOOOOM
	MDFN_MergeSettings(MednafenSettings);

	for(unsigned int x = 0; x < MDFNSystems.size(); x++)
	{
	 if(MDFNSystems[x]->Settings)
	  MDFN_MergeSettings(MDFNSystems[x]->Settings);
	}

	MDFN_FinalizeSettings();
}

void MDFNI_Kill(void)
{
 MDFN_KillSettings();
}

void MDFN_MidSync(EmulateSpecStruct *espec, const unsigned flags)
{
 MDFND_MidSync(espec, flags);

 espec->SoundBufSizeALMS = espec->SoundBufSize;
 espec->MasterCyclesALMS = espec->MasterCycles;

 if((flags & MIDSYNC_FLAG_UPDATE_INPUT) && MDFNGameInfo->TransformInput)	// Call after MDFND_MidSync, and before MDFNMOV_ProcessInput
  MDFNGameInfo->TransformInput();
}

void MDFN_MidLineUpdate(EmulateSpecStruct *espec, int y)
{
 //MDFND_MidLineUpdate(espec, y);
}

void MDFNI_Emulate(EmulateSpecStruct *espec)
{
 // Initialize some espec member data to zero, to catch some types of bugs.
 espec->DisplayRect.x = 0;
 espec->DisplayRect.w = 0;
 espec->DisplayRect.y = 0;
 espec->DisplayRect.h = 0;

 assert((bool)(espec->SoundBuf != NULL) == (bool)espec->SoundRate && (bool)espec->SoundRate == (bool)espec->SoundBufMaxSize);

 espec->SoundBufSize = 0;

 espec->VideoFormatChanged = false;
 espec->SoundFormatChanged = false;

 if(last_pixel_format != espec->surface->format)
 {
  espec->VideoFormatChanged = true;

  last_pixel_format = espec->surface->format;
 }

 if(fabs(espec->SoundRate - last_sound_rate) >= 0.5)
 {
  //puts("Rate Change");
  espec->SoundFormatChanged = true;
  last_sound_rate = espec->SoundRate;
 }

 if(MDFNGameInfo->TransformInput)
  MDFNGameInfo->TransformInput();

 espec->NeedSoundReverse = false; //MDFNSRW_Frame(espec->NeedRewind);

 MDFNGameInfo->Emulate(espec);

 if(espec->InterlaceOn)
 {
  if(!PrevInterlaced)
   deint->ClearState();

  deint->Process(espec->surface, espec->DisplayRect, espec->LineWidths, espec->InterlaceField);
  PrevInterlaced = true;
 }
 else
  PrevInterlaced = false;
}

static void StateAction_RINP(StateMem* sm, const unsigned load, const bool data_only)
{
 char namebuf[16][2 + 8 + 1];

 if(!data_only)
 {
  for(unsigned x = 0; x < 16; x++)
  {
   snprintf(namebuf[x], sizeof(namebuf[x]), "%02x%08x", x, PortDevice[x]);
  }
 }

 #define SFRIH(x) SFPTR8N(PortData[x], PortDataLen[x], namebuf[x])
 SFORMAT StateRegs[] =
 {
  SFRIH(0), SFRIH(1), SFRIH(2), SFRIH(3), SFRIH(4), SFRIH(5), SFRIH(6), SFRIH(7),
  SFRIH(8), SFRIH(9), SFRIH(10), SFRIH(11), SFRIH(12), SFRIH(13), SFRIH(14), SFRIH(15),
  SFEND
 };
 #undef SFRIH

 MDFNSS_StateAction(sm, load, data_only, StateRegs, "MDFNRINP", true);
}

void MDFN_StateAction(StateMem *sm, const unsigned load, const bool data_only)
{
 StateAction_RINP(sm, load, data_only);

 MDFNGameInfo->StateAction(sm, load, data_only);
}

static int curindent = 0;

void MDFN_indent(int indent)
{
 curindent += indent;
 if(curindent < 0)
 {
  fprintf(stderr, "MDFN_indent negative!\n");
  curindent = 0;
 }
}

static uint8 lastchar = 0;
void MDFN_printf(const char *format, ...)
{
 char *format_temp = NULL;
 char *temp = NULL;
 unsigned int x, newlen;

 va_list ap;
 va_start(ap,format);


 // First, determine how large our format_temp buffer needs to be.
 uint8 lastchar_backup = lastchar; // Save lastchar!
 for(newlen=x=0;x<strlen(format);x++)
 {
  if(lastchar == '\n' && format[x] != '\n')
  {
   int y;
   for(y=0;y<curindent;y++)
    newlen++;
  }
  newlen++;
  lastchar = format[x];
 }

 format_temp = (char *)malloc(newlen + 1); // Length + NULL character, duh
 
 // Now, construct our format_temp string
 lastchar = lastchar_backup; // Restore lastchar
 for(newlen=x=0;x<strlen(format);x++)
 {
  if(lastchar == '\n' && format[x] != '\n')
  {
   int y;
   for(y=0;y<curindent;y++)
    format_temp[newlen++] = ' ';
  }
  format_temp[newlen++] = format[x];
  lastchar = format[x];
 }

 format_temp[newlen] = 0;

 char* ret = NULL;
 if (vasprintf(&ret, format_temp, ap) != -1)
    temp = ret;
 free(format_temp);

 MDFND_OutputInfo(temp);
 free(temp);

 va_end(ap);
}

void MDFN_Notify(MDFN_NoticeType t, const char* format, ...)
{
 va_list ap;
 char*ret = NULL;
 char* s  = NULL;

 va_start(ap, format);

 if(vasprintf(&ret, format, ap) != -1)
   s = ret;

 if(s)
 {
  MDFND_OutputNotice(t, s);
  free(s);
 }

 va_end(ap);
}

void MDFNI_Power(void)
{
 assert(MDFNGameInfo);

 MDFNGameInfo->DoSimpleCommand(MDFN_MSC_POWER);
}

void MDFNI_Reset(void)
{
 assert(MDFNGameInfo);

 MDFNGameInfo->DoSimpleCommand(MDFN_MSC_RESET);
}

void MDFNI_SetLayerEnableMask(uint64 mask)
{
 assert(MDFNGameInfo);

 if(MDFNGameInfo->SetLayerEnableMask)
 {
  MDFNGameInfo->SetLayerEnableMask(mask);
 }
}

uint8* MDFNI_SetInput(const uint32 port, const uint32 type)
{
 assert(MDFNGameInfo);
 assert(port < 16 && port < MDFNGameInfo->PortInfo.size());
 assert(type < MDFNGameInfo->PortInfo[port].DeviceInfo.size());

 if(type != PortDevice[port])
 {
  size_t tmp_len = MDFNGameInfo->PortInfo[port].DeviceInfo[type].IDII.InputByteSize;
  uint8* tmp_ptr;

  tmp_ptr = (uint8*)malloc(tmp_len ? tmp_len : 1);	// Ensure PortData[port], for valid port, is never NULL, for easier handling in regards to stuff like memcpy()
							// (which may have "undefined" behavior when a pointer argument is NULL even when length == 0).
  memset(tmp_ptr, 0, tmp_len);

  if(PortData[port] != NULL)
   free(PortData[port]);

  PortData[port] = tmp_ptr;
  PortDataLen[port] = tmp_len;
  PortDevice[port] = type;

  MDFNGameInfo->SetInput(port, MDFNGameInfo->PortInfo[port].DeviceInfo[type].ShortName, PortData[port]);
  //MDFND_InputSetNotification(port, type, PortData[port]);
 }

 return PortData[port];
}

}
