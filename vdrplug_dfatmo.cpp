/*
 * Copyright (C) 2012 Andreas Auras <yak54@inkennet.de>
 *
 * This file is part of DFAtmo the driver for 'Atmolight' controllers for VDR, XBMC and xinelib based video players.
 *
 * DFAtmo is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * DFAtmo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * This is the vdr plugin native module.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <vdr/plugin.h>


extern "C" {
#include "atmodriver.h"

static int ActualLogLevel = DFLOG_ERROR;
dfatmo_log_level_t dfatmo_log_level = &ActualLogLevel;

static void DriverLog(int level, const char *fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  syslog_with_tid((level - 1), "DFAtmo: %s\n", buf);
  }
dfatmo_log_t dfatmo_log = &DriverLog;


typedef enum { PARM_TYPE_LAST, PARM_TYPE_INT, PARM_TYPE_BOOL, PARM_TYPE_CHAR } parm_type_t;

/* defines a single parameter entry. */
typedef struct {
  parm_type_t      type;             /* PARM_TYPE_xxx             */
  const char      *name;             /* name of this parameter          */
  size_t           size;             /* sizeof(parameter)               */
  int              offset;           /* offset in bytes from struct ptr */
  const char     **enum_values;      /* enumeration (first=0) or NULL   */
  int              range_min;        /* minimum value                   */
  int              range_max;        /* maximum value                   */
  int              readonly;         /* 0 = read/write, 1=read-only     */
  const char      *description;      /* user-friendly description       */
} parm_desc_t;

#define PARM_DESC_BOOL( var, enumv, min, max, readonly, descr ) \
{ PARM_TYPE_BOOL, #var, sizeof(TmpParm.var), \
  (char*)&TmpParm.var-(char*)&TmpParm, enumv, min, max, readonly, descr },

#define PARM_DESC_INT( var, enumv, min, max, readonly, descr ) \
{ PARM_TYPE_INT, #var, sizeof(TmpParm.var), \
  (char*)&TmpParm.var-(char*)&TmpParm, enumv, min, max, readonly, descr },

#define PARM_DESC_CHAR( var, enumv, min, max, readonly, descr ) \
{ PARM_TYPE_CHAR, #var, sizeof(TmpParm.var), \
  (char*)&TmpParm.var-(char*)&TmpParm, enumv, min, max, readonly, descr },

static atmo_parameters_t TmpParm;
static parm_desc_t ParmDesc[] = {
PARM_DESC_LIST
{ PARM_TYPE_LAST, NULL, 0, 0, NULL, 0, 0, 1, NULL }
};

static parm_desc_t *GetParmDesc(const char *Name)
{
  parm_desc_t *pd = ParmDesc;
  while (pd->type != PARM_TYPE_LAST)
  {
    if (!strcasecmp(pd->name, Name))
      return pd;
    ++pd;
  }
  return NULL;
}

enum { NULL_DRIVER_TYPE = 0, FILE_DRIVER_TYPE, SERIAL_DRIVER_TYPE, DF10CH_DRIVER_TYPE, CUSTOM_DRIVER_TYPE, NUM_DRIVER_TYPES };

static const char *DriverTypeList[NUM_DRIVER_TYPES] = { trNOOP("null"), trNOOP("file"), trNOOP("serial"), trNOOP("df10ch"), trNOOP("custom") };

static int GetOutputDriverType(const char *Name)
{
  int i;
  for (i = 0; i < CUSTOM_DRIVER_TYPE; ++i)
  {
    if (!strcmp(Name, DriverTypeList[i]))
      break;
  }
  return i;
}

} // extern "C"


static const char *VERSION        = "0.0.1";
static const char *DESCRIPTION    = trNOOP("The driver for 'Atmolight' controllers");
static const char *MAINMENUENTRY  = "DFAtmo";


class cDFAtmoPlugin;

class cDFAtmoThread : public cThread {
protected:
  cDFAtmoPlugin *plugin;
  cCondWait condWait;
public:
  cDFAtmoThread(const char *Desc, cDFAtmoPlugin *Plugin): cThread(Desc) { plugin = Plugin; };
  void Stop();
};


class cDFAtmoGrabThread : public cDFAtmoThread {
protected:
  virtual void Action(void);
public:
  cDFAtmoGrabThread(cDFAtmoPlugin *Plugin): cDFAtmoThread("DFAtmo grab", Plugin) {};
};


class cDFAtmoOutputThread : public cDFAtmoThread {
protected:
  virtual void Action(void);
public:
  cDFAtmoOutputThread(cDFAtmoPlugin *Plugin): cDFAtmoThread("DFAtmo output", Plugin) {};
};


class cDFAtmoPlugin : public cPlugin {
  friend class cDFAtmoGrabThread;
  friend class cDFAtmoOutputThread;
  friend class cDFAtmoSetupMenu;
  friend class cDFAtmoMainMenu;

private:
  cDFAtmoOutputThread outputThread;
  void Configure(void);
  void StopThreads(void);

protected:
  atmo_driver_t ad;
  atmo_parameters_t SetupParm;
  int SetupDriverType;
  int SetupHideMainMenuEntry;
  cDFAtmoGrabThread grabThread;
  void StoreSetup(void);
  void InstantConfigure(void);

public:
  cDFAtmoPlugin(void);
  virtual ~cDFAtmoPlugin();
  virtual const char *Version(void) { return VERSION; }
  virtual const char *Description(void) { return tr(DESCRIPTION); }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Start(void);
  virtual void Stop(void);
  virtual const char *MainMenuEntry(void) { return SetupHideMainMenuEntry ? NULL: MAINMENUENTRY; }
  virtual cOsdObject *MainMenuAction(void);
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  virtual bool Service(const char *Id, void *Data = NULL);
  virtual const char **SVDRPHelpPages(void);
  virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode);
  };



// ---
// cDFAtmoThread
//
void cDFAtmoThread::Stop(void)
{
  if (Active())
  {
    Cancel(-1);
    condWait.Signal();
    Cancel(1);
  }
}


// ---
// cDFAtmoGrabThread
//
void cDFAtmoGrabThread::Action(void)
{
  atmo_driver_t *ad = &plugin->ad;
  uint64_t startTime = cTimeMs::Now();
  uint64_t grabTime = startTime;
  uint64_t n = 1;

  DFATMO_LOG(DFLOG_INFO, "grab thread running");

  while (Running())
  {
      // loop with analyze rate duration
    uint64_t actTime = cTimeMs::Now();
    if (actTime < grabTime)
    {
      condWait.Wait((int)(grabTime - actTime));
      continue;
    }
    grabTime = actTime + ad->active_parm.analyze_rate;

      // get actual displayed image size
    int vidWidth = 0, vidHeight = 0;
    double vidAspect = 0.0;
    cDevice::PrimaryDevice()->GetVideoSize(vidWidth, vidHeight, vidAspect);
    if (vidWidth < 8 || vidHeight < 8)
    {
      DFATMO_LOG(DFLOG_DEBUG, "illegal video size %dx%d!", vidWidth, vidHeight);
      continue;
    }

      // calculate size of analyze image
    int grabWidth = (ad->active_parm.analyze_size + 1) * 64;
    int grabHeight = (grabWidth * vidHeight) / vidWidth;

      // grab image
    int grabSize = 0;
    uint8_t *grabImg = cDevice::PrimaryDevice()->GrabImage(grabSize, false, 100, grabWidth, grabHeight);
    if (grabImg == NULL)
    {
      DFATMO_LOG(DFLOG_DEBUG, "grab failed!");
      continue;
    }

      // Skip PNM header of grabbed image
    uint8_t *img = grabImg;
    int lf = 0;
    while (grabSize > 0 && lf < 4)
    {
      if (*img == '\n')
        ++lf;
      ++img;
      --grabSize;
    }

    if (grabSize != (grabWidth * grabHeight * 3))
    {
      DFATMO_LOG(DFLOG_ERROR, "grab function returned wrong image size (%d,%d)!", grabSize, (grabWidth * grabHeight * 3));
      free(grabImg);
      break;
    }

      /* calculate size of analyze (sub) window */
    int overscan = ad->active_parm.overscan;
    int analyzeWidth, analyzeHeight;
    if (overscan) {
      int cropWidth = (grabWidth * overscan + 500) / 1000;
      int cropHeight = (grabHeight * overscan + 500) / 1000;
      analyzeWidth = grabWidth - 2 * cropWidth;
      analyzeHeight = grabHeight - 2 * cropHeight;
      img += (cropHeight * grabWidth + cropWidth) * 3;
    } else {
      analyzeWidth = grabWidth;
      analyzeHeight = grabHeight;
    }

    if (analyzeWidth < 8 || analyzeHeight < 8 || analyzeWidth > grabWidth || analyzeHeight > grabHeight) {
      DFATMO_LOG(DFLOG_ERROR, "illegal analyze window size %dx%d of %dx%d", analyzeWidth, analyzeHeight, grabWidth, grabHeight);
      free(grabImg);
      break;
    }

    if (configure_analyze_size(ad, analyzeWidth, analyzeHeight))
    {
      free(grabImg);
      break;
    }

      // calculate HSV image
    hsv_color_t *hsv = ad->hsv_img;
    int pitch = grabWidth * 3;
    while (analyzeHeight--)
    {
      uint8_t *i = img;
      int w = analyzeWidth;
      while (w--) {
        rgb_to_hsv(hsv, i[0], i[1], i[2]);
        ++hsv;
        i += 3;
      }
      img += pitch;
    }
    free(grabImg);

    calc_hue_hist(ad);
    calc_windowed_hue_hist(ad);
    calc_most_used_hue(ad);
    calc_sat_hist(ad);
    calc_windowed_sat_hist(ad);
    calc_most_used_sat(ad);
    if (ad->active_parm.uniform_brightness)
      calc_uniform_average_brightness(ad);
    else
      calc_average_brightness(ad);

    {
      cThreadLock lock(this);
      calc_rgb_values(ad);
    }

    ++n;
  }

  DFATMO_LOG(DFLOG_INFO, "grab thread terminated. average loop time is %d ms", (int)((cTimeMs::Now() - startTime) / n));
}



// ---
// cDFAtmoOutputThread
//
void cDFAtmoOutputThread::Action(void)
{
  atmo_driver_t *ad = &plugin->ad;
  uint64_t startTime = cTimeMs::Now();
  uint64_t outputTime = startTime;
  uint64_t n = 1;

  DFATMO_LOG(DFLOG_INFO, "output thread running");

  reset_filters(ad);

  while (Running())
  {
      // loop with analyze rate duration
    uint64_t actTime = cTimeMs::Now();
    if (actTime < outputTime)
    {
      condWait.Wait((int)(outputTime - actTime));
      continue;
    }
    outputTime = actTime + ad->active_parm.output_rate;

    {
      cThreadLock lock(&plugin->grabThread);
      apply_filters(ad);
    }

    if (actTime >= (startTime + ad->active_parm.start_delay))
    {
      if (apply_delay_filter(ad))
        break;
      apply_gamma_correction(ad);
      apply_white_calibration(ad);
      if (send_output_colors(ad, ad->filtered_output_colors, 0))
        break;
    }

    ++n;
  }

  DFATMO_LOG(DFLOG_INFO, "output thread terminated. average loop time is %d ms", (int)((cTimeMs::Now() - startTime) / n));
}



// ---
// cDFAtmoMainMenu
//
class cDFAtmoMainMenu : public cOsdMenu {
private:
  cDFAtmoPlugin *plugin;
  int brightness;
public:
  cDFAtmoMainMenu(cDFAtmoPlugin *Plugin);
  virtual eOSState ProcessKey(eKeys Key);
};


cDFAtmoMainMenu::cDFAtmoMainMenu(cDFAtmoPlugin *Plugin): cOsdMenu(tr("DFAtmo control"), 25)
{
  plugin = Plugin;
  brightness = plugin->ad.active_parm.brightness;

  if (plugin->ad.active_parm.enabled)
  {
    Add(new cOsdItem(tr("Switch Atmolight off"), osUser1));

    parm_desc_t *pd = GetParmDesc("brightness");
    Add(new cMenuEditIntItem(tr(pd->description), &brightness, pd->range_min, pd->range_max));
  }
  else
    Add(new cOsdItem(tr("Switch Atmolight on"), osUser1));
}


eOSState cDFAtmoMainMenu::ProcessKey(eKeys Key)
{
  int enabled = plugin->ad.active_parm.enabled;

  eOSState state = cOsdMenu::ProcessKey(Key);

  int newBrightness = brightness;
  switch (state)
  {
    case osUser1:
      enabled = !enabled;
      state = osEnd;
      break;
    case osUnknown:
      switch (Key)
      {
        case kRed:
          enabled = 0;
          state = osEnd;
          break;
        case kGreen:
          enabled = 1;
          state = osEnd;
          break;
        case kYellow:
          newBrightness = plugin->ad.active_parm.brightness + 10;
          state = osEnd;
          break;
        case kBlue:
          newBrightness = plugin->ad.active_parm.brightness - 10;
          state = osEnd;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  if (plugin->ad.active_parm.enabled)
  {
    if (newBrightness != plugin->ad.active_parm.brightness)
    {
      parm_desc_t *pd = GetParmDesc("brightness");
      if (newBrightness > pd->range_max)
        newBrightness = pd->range_max;
      else if (newBrightness < pd->range_min)
        newBrightness = pd->range_min;
    }
  }
  else
    newBrightness = plugin->ad.parm.brightness;

  if (enabled != plugin->ad.active_parm.enabled || (plugin->ad.active_parm.enabled && newBrightness != plugin->ad.active_parm.brightness))
  {
    atmo_parameters_t save = plugin->ad.parm;
    plugin->ad.parm.enabled = enabled;
    plugin->ad.parm.brightness = newBrightness;
    plugin->InstantConfigure();
    plugin->ad.parm = save;
  }

  return state;
}



// ---
// cDFAtmoSetupMenu
//
class cDFAtmoSetupMenu : public cMenuSetupPage {
private:
  cDFAtmoPlugin *plugin;
  const char *enumText[25];
  int enumTextIdx;
public:
  cDFAtmoSetupMenu(cDFAtmoPlugin *Plugin);
  virtual eOSState ProcessKey(eKeys Key);
  void AddParm(const char *ParmName);
  void SetSubMenuSection(const char *Text);
  void SetMainMenu(void);
  void SetAreasMenu(void);
  void SetAnalysisMenu(void);
  void SetFiltersMenu(void);
  void SetGeneralMenu(void);
  void SetCalibrationMenu(void);
  virtual void Store(void);
  const char **TrEnumText(int NumItems, const char **List);
  virtual void Clear(void) { cMenuSetupPage::Clear(); enumTextIdx = 0; }
};


cDFAtmoSetupMenu::cDFAtmoSetupMenu(cDFAtmoPlugin *Plugin): cMenuSetupPage()
{
  plugin = Plugin;
  SetCols(25);
}

void cDFAtmoSetupMenu::SetSubMenuSection(const char *Text)
{
  SetSection(cString::sprintf("DFAtmo %s", Text));
}

const char **cDFAtmoSetupMenu::TrEnumText(int NumItems, const char **List)
{
  int start = enumTextIdx;
  while (NumItems--)
    enumText[enumTextIdx++] = tr(*List++);
  return (enumText + start);
}

void cDFAtmoSetupMenu::AddParm(const char *ParmName)
{
  parm_desc_t *pd = GetParmDesc(ParmName);
  char *v = ((char *) &plugin->SetupParm) + pd->offset;
  cOsdItem *it = NULL;

  switch (pd->type)
  {
    case PARM_TYPE_INT:
      if (pd->enum_values)
        it = new cMenuEditStraItem(tr(pd->description), (int *)v, (pd->range_max + 1), TrEnumText((pd->range_max + 1), pd->enum_values));
      else
        it = new cMenuEditIntItem(tr(pd->description), (int *)v, pd->range_min, pd->range_max);
      break;
    case PARM_TYPE_BOOL:
      it = new cMenuEditBoolItem(tr(pd->description), (int *)v);
      break;
    case PARM_TYPE_CHAR:
      it = new cMenuEditStrItem(tr(pd->description), v, (int)(pd->size - 1));
      break;
    default:
      return;
  }
  Add(it);
}

void cDFAtmoSetupMenu::Store(void)
{
  if (plugin->SetupDriverType < CUSTOM_DRIVER_TYPE)
    strcpy(plugin->SetupParm.driver, DriverTypeList[plugin->SetupDriverType]);

  if (memcmp(&plugin->SetupParm, &plugin->ad.parm, sizeof(plugin->SetupParm)))
  {
    plugin->ad.parm = plugin->SetupParm;
    plugin->StoreSetup();
    plugin->InstantConfigure();
  }
}

void cDFAtmoSetupMenu::SetMainMenu(void)
{
  SetHasHotkeys();
  Clear();

  Add(new cOsdItem(hk(tr("General")), osUser1));
  Add(new cOsdItem(hk(tr("Areas")), osUser2));
  Add(new cOsdItem(hk(tr("Analysis")), osUser3));
  Add(new cOsdItem(hk(tr("Filters")), osUser4));
  Add(new cOsdItem(hk(tr("Calibration")), osUser5));
}

void cDFAtmoSetupMenu::SetGeneralMenu(void)
{
  Clear();
  SetSubMenuSection(tr("General"));

  Add(new cMenuEditBoolItem(tr("Hide main menu entry"), &plugin->SetupHideMainMenuEntry));
  AddParm("enabled");
  Add(new cMenuEditStraItem(tr("Output driver"), &plugin->SetupDriverType, NUM_DRIVER_TYPES, TrEnumText(NUM_DRIVER_TYPES, DriverTypeList)));
  if (plugin->SetupDriverType == CUSTOM_DRIVER_TYPE)
    AddParm("driver");
  if (plugin->SetupDriverType == FILE_DRIVER_TYPE || plugin->SetupDriverType == SERIAL_DRIVER_TYPE || plugin->SetupDriverType == CUSTOM_DRIVER_TYPE)
    AddParm("driver_param");
}

void cDFAtmoSetupMenu::SetAreasMenu(void)
{
  Clear();
  SetSubMenuSection(tr("Areas"));

  AddParm("top");
  AddParm("bottom");
  AddParm("left");
  AddParm("right");
  AddParm("top_left");
  AddParm("top_right");
  AddParm("bottom_left");
  AddParm("bottom_right");
  AddParm("center");
}

void cDFAtmoSetupMenu::SetAnalysisMenu(void)
{
  Clear();
  SetSubMenuSection(tr("Analysis"));

  AddParm("uniform_brightness");
  AddParm("analyze_size");
  AddParm("overscan");
  AddParm("edge_weighting");
  AddParm("darkness_limit");
  AddParm("analyze_rate");
  AddParm("hue_win_size");
  AddParm("sat_win_size");
  AddParm("hue_threshold");
}

void cDFAtmoSetupMenu::SetFiltersMenu(void)
{
  Clear();
  SetSubMenuSection(tr("Filters"));

  AddParm("brightness");
  AddParm("filter");
  if (plugin->SetupParm.filter != FILTER_NONE)
    AddParm("filter_smoothness");
  if (plugin->SetupParm.filter == FILTER_COMBINED)
  {
    AddParm("filter_length");
    AddParm("filter_threshold");
  }
  AddParm("start_delay");
  AddParm("filter_delay");
  AddParm("output_rate");
}

void cDFAtmoSetupMenu::SetCalibrationMenu(void)
{
  Clear();
  SetSubMenuSection(tr("Calibration"));

  AddParm("wc_red");
  AddParm("wc_green");
  AddParm("wc_blue");
  AddParm("gamma");
}

eOSState cDFAtmoSetupMenu::ProcessKey(eKeys Key)
{
  if (HasSubMenu())
    return cMenuSetupPage::ProcessKey(Key);

  int prevFilter = plugin->SetupParm.filter;
  int prevDriverType = plugin->SetupDriverType;

  eOSState state = cMenuSetupPage::ProcessKey(Key);

  if (prevDriverType != plugin->SetupDriverType)
  {
    int curItemIdx = Current();
    SetGeneralMenu();
    SetCurrent(Get(curItemIdx));
    Display();
    return osContinue;
  }

  if (prevFilter != plugin->SetupParm.filter)
  {
    int curItemIdx = Current();
    SetFiltersMenu();
    SetCurrent(Get(curItemIdx));
    Display();
    return osContinue;
  }

  cDFAtmoSetupMenu *m = NULL;
  switch (state)
  {
    case osUser1:
      m = new cDFAtmoSetupMenu(plugin);
      m->SetGeneralMenu();
      break;
    case osUser2:
      m = new cDFAtmoSetupMenu(plugin);
      m->SetAreasMenu();
      break;
    case osUser3:
      m = new cDFAtmoSetupMenu(plugin);
      m->SetAnalysisMenu();
      break;
    case osUser4:
      m = new cDFAtmoSetupMenu(plugin);
      m->SetFiltersMenu();
      break;
    case osUser5:
      m = new cDFAtmoSetupMenu(plugin);
      m->SetCalibrationMenu();
      break;
    default:
      return state;
  }

  return AddSubMenu(m);
}


// ---
// cDFAtmoPlugin
//
cDFAtmoPlugin::cDFAtmoPlugin(void): cPlugin(), outputThread(this), grabThread(this)
{
  SetupHideMainMenuEntry = 0;
  init_configuration(&ad);
  ad.parm.enabled = 0;
  ad.parm.analyze_rate = 40;
  reset_filters(&ad);
}

cDFAtmoPlugin::~cDFAtmoPlugin()
{
  free_channels(&ad);
  free_analyze_images(&ad);
}

const char *cDFAtmoPlugin::CommandLineHelp(void)
{
  return "  -l LOG_LEVEL               --log=LOG_LEVEL\n";
}

bool cDFAtmoPlugin::ProcessArgs(int argc, char *argv[])
{
  static struct option long_options[] = {
      { "log",  required_argument, NULL, 'l' },
      { NULL }
  };

  int c;
  while ((c = getopt_long(argc, argv, "l:", long_options, NULL)) != -1)
  {
    switch (c)
    {
      case 'l':
        ActualLogLevel = atoi(optarg);
        break;
      default:
        return false;
    }
  }
  return true;
}

bool cDFAtmoPlugin::Start(void)
{
  Configure();
  return true;
}

void cDFAtmoPlugin::Stop(void)
{
  StopThreads();
  close_output_driver(&ad);
  unload_output_driver(&ad);
}

cOsdObject *cDFAtmoPlugin::MainMenuAction(void)
{
  return new cDFAtmoMainMenu(this);
}

cMenuSetupPage *cDFAtmoPlugin::SetupMenu(void)
{
  SetupParm = ad.parm;
  SetupDriverType = GetOutputDriverType(SetupParm.driver);

  cDFAtmoSetupMenu *m = new cDFAtmoSetupMenu(this);
  m->SetMainMenu();
  return m;
}

bool cDFAtmoPlugin::SetupParse(const char *Name, const char *Value)
{
  if (!strcasecmp(Name, "hide_main_menu_entry"))
  {
    SetupHideMainMenuEntry = atoi(Value);
    return true;
  }

  parm_desc_t *pd = GetParmDesc(Name);
  if (pd == NULL)
    return false;

  char *p = ((char *) &ad.parm) + pd->offset;
  int v;

  switch (pd->type)
  {
    case PARM_TYPE_BOOL:
    case PARM_TYPE_INT:
      v = atoi(Value);
      if (v >= pd->range_min && v <= pd->range_max)
      {
        *((int *)p) = v;
        return true;
      }
      break;
    case PARM_TYPE_CHAR:
      if (strlen(Value) < pd->size)
      {
        strcpy(p, Value);
        return true;
      }
      break;
    default:
      break;
  }

  DFATMO_LOG(DFLOG_ERROR, "parameter '%s': illegal value '%s'", Name, Value);
  return true;
}

bool cDFAtmoPlugin::Service(const char *Id, void *Data)
{
  // Handle custom service requests from other plugins
  return false;
}

const char **cDFAtmoPlugin::SVDRPHelpPages(void)
{
  // Return help text for SVDRP commands this plugin implements
  return NULL;
}

cString cDFAtmoPlugin::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  // Process SVDRP commands this plugin implements
  return NULL;
}

void cDFAtmoPlugin::StopThreads(void)
{
  outputThread.Stop();
  grabThread.Stop();
  ad.active_parm.enabled = 0;
}

void cDFAtmoPlugin::InstantConfigure(void)
{
  if (ad.parm.enabled)
  {
    if (!ad.active_parm.enabled)
      Configure();
    else
      instant_configure(&ad);
  }
  else if (ad.active_parm.enabled)
  {
    StopThreads();
    close_output_driver(&ad);
  }
}

void cDFAtmoPlugin::Configure(void)
{
  if (!ad.parm.enabled ||
        strcmp(ad.active_parm.driver, ad.parm.driver) ||
        strcmp(ad.active_parm.driver_path, ad.parm.driver_path) ||
        strcmp(ad.active_parm.driver_param, ad.parm.driver_param))
  {
    StopThreads();
    close_output_driver(&ad);
    unload_output_driver(&ad);
  }

  if (ad.parm.enabled)
  {
    atmo_parameters_t save = ad.parm;

    int send = !ad.driver_opened;
    int start = !open_output_driver(&ad);

    if (memcmp(&save, &ad.parm, sizeof(ad)))
      StoreSetup();

    if (ad.sum_channels < 1 || ad.active_parm.top != ad.parm.top ||
                    ad.active_parm.bottom != ad.parm.bottom ||
                    ad.active_parm.left != ad.parm.left ||
                    ad.active_parm.right != ad.parm.right ||
                    ad.active_parm.center != ad.parm.center ||
                    ad.active_parm.top_left != ad.parm.top_left ||
                    ad.active_parm.top_right != ad.parm.top_right ||
                    ad.active_parm.bottom_left != ad.parm.bottom_left ||
                    ad.active_parm.bottom_right != ad.parm.bottom_right)
    {
      free_channels(&ad);
      if (config_channels(&ad))
        start = 0;
      send = 1;
    }

    ad.active_parm = ad.parm;

      // send first initial color packet
    if (start && send && send_output_colors(&ad, ad.output_colors, 1))
      start = 0;

    if (!start || !grabThread.Start() || !outputThread.Start())
      StopThreads();
  }
  else
    ad.active_parm.enabled = ad.parm.enabled;
}

void cDFAtmoPlugin::StoreSetup(void)
{
  SetupStore("hide_main_menu_entry", SetupHideMainMenuEntry);

  parm_desc_t *pd = ParmDesc;
  char *p = (char *) &ad.parm;

  while (pd->type != PARM_TYPE_LAST)
  {
    switch (pd->type)
    {
      case PARM_TYPE_BOOL:
      case PARM_TYPE_INT:
        SetupStore(pd->name, *((int *)(p + pd->offset)));
        break;
      case PARM_TYPE_CHAR:
        SetupStore(pd->name, (p + pd->offset));
        break;
      default:
        break;
    }
    ++pd;
  }
}


VDRPLUGINCREATOR(cDFAtmoPlugin); // Don't touch this!
