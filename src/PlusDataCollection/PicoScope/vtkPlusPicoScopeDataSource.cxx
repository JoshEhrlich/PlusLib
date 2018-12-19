/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

// Local includes
#include "vtkIGSIOAccurateTimer.h"
#include "vtkPlusPicoScopeDataSource.h"

// VTK includes

// System includes


// PicoScope includes
#include "ps2000.h"

vtkStandardNewMacro(vtkPlusPicoScopeDataSource);

// constants
#define BUFFER_SIZE 	1024 // pico scope buffer size

//----------------------------------------------------------------------------
class vtkPlusPicoScopeDataSource::vtkInternal
{
public:
  vtkPlusPicoScopeDataSource* External;

  vtkInternal(vtkPlusPicoScopeDataSource* external)
    : External(external)
  {
  }

  virtual ~vtkInternal()
  {
  }

  // METHODS

  // 
  void GetPicoScopeInfo(void);

  //
  void SetPicoScopeDefaults(void);

  // PICO Scope internals
  typedef enum {
    MODEL_NONE = 0,
    MODEL_PS2104 = 2104,
    MODEL_PS2105 = 2105,
    MODEL_PS2202 = 2202,
    MODEL_PS2203 = 2203,
    MODEL_PS2204 = 2204,
    MODEL_PS2205 = 2205,
    MODEL_PS2204A = 0xA204,
    MODEL_PS2205A = 0xA205
  } MODEL_TYPE;

  typedef struct
  {
    PS2000_THRESHOLD_DIRECTION	channelA;
    PS2000_THRESHOLD_DIRECTION	channelB;
    PS2000_THRESHOLD_DIRECTION	channelC;
    PS2000_THRESHOLD_DIRECTION	channelD;
    PS2000_THRESHOLD_DIRECTION	ext;
  } DIRECTIONS;

  typedef struct
  {
    PS2000_PWQ_CONDITIONS			*	conditions;
    int16_t							nConditions;
    PS2000_THRESHOLD_DIRECTION		direction;
    uint32_t						lower;
    uint32_t						upper;
    PS2000_PULSE_WIDTH_TYPE			type;
  } PULSE_WIDTH_QUALIFIER;


  typedef struct
  {
    PS2000_CHANNEL channel;
    float threshold;
    int16_t direction;
    float delay;
  } SIMPLE;

  typedef struct
  {
    int16_t hysteresis;
    DIRECTIONS directions;
    int16_t nProperties;
    PS2000_TRIGGER_CONDITIONS * conditions;
    PS2000_TRIGGER_CHANNEL_PROPERTIES * channelProperties;
    PULSE_WIDTH_QUALIFIER pwq;
    uint32_t totalSamples;
    int16_t autoStop;
    int16_t triggered;
  } ADVANCED;

  typedef struct
  {
    SIMPLE simple;
    ADVANCED advanced;
  } TRIGGER_CHANNEL;

  typedef struct {
    int16_t DCcoupled;
    int16_t range;
    int16_t enabled;
    int16_t values[BUFFER_SIZE];
  } CHANNEL_SETTINGS;

  // struct to hold scope handle
  typedef struct {
    int16_t			handle;
    MODEL_TYPE		model;
    PS2000_RANGE	firstRange;
    PS2000_RANGE	lastRange;
    TRIGGER_CHANNEL trigger;
    int16_t			maxTimebase;
    int16_t			timebases;
    int16_t			noOfChannels;
    CHANNEL_SETTINGS channelSettings[PS2000_MAX_CHANNELS];
    int16_t			hasAdvancedTriggering;
    int16_t			hasFastStreaming;
    int16_t			hasEts;
    int16_t			hasSignalGenerator;
    int16_t			awgBufferSize;
  } UNIT_MODEL;

  // MEMBER VARIABLES
  UNIT_MODEL Scope;  // scope handle
};

//----------------------------------------------------------------------------
void vtkPlusPicoScopeDataSource::vtkInternal::GetPicoScopeInfo()
{
  int8_t description[8][25] = { "Driver Version   ",
                "USB Version      ",
                "Hardware Version ",
                "Variant Info     ",
                "Serial           ",
                "Cal Date         ",
                "Error Code       ",
                "Kernel Driver    "
  };
  int16_t 	i;
  int8_t		line[80];
  int32_t		variant;

  if (this->Scope.handle)
  {
    for (i = 0; i < 8; i++)
    {
      ps2000_get_unit_info(this->Scope.handle, line, sizeof(line), i);

      if (i == 3)
      {
        variant = atoi((const char*)line);

        if (strlen((const char*)line) == 5) // Identify if 2204A or 2205A
        {
          line[4] = toupper(line[4]);

          if (line[1] == '2' && line[4] == 'A')		// i.e 2204A -> 0xA204
          {
            variant += 0x9968;
          }
        }
      }

      if (i != 6) // No need to print error code
      {
        printf("%s: %s\n", description[i], line);
      }
    }

    switch (variant)
    {
    case MODEL_PS2104:
      this->Scope.model = MODEL_PS2104;
      this->Scope.firstRange = PS2000_100MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2104_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 1;
      this->Scope.hasAdvancedTriggering = FALSE;
      this->Scope.hasSignalGenerator = FALSE;
      this->Scope.hasEts = TRUE;
      this->Scope.hasFastStreaming = FALSE;
      break;

    case MODEL_PS2105:
      this->Scope.model = MODEL_PS2105;
      this->Scope.firstRange = PS2000_100MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2105_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 1;
      this->Scope.hasAdvancedTriggering = FALSE;
      this->Scope.hasSignalGenerator = FALSE;
      this->Scope.hasEts = TRUE;
      this->Scope.hasFastStreaming = FALSE;
      break;

    case MODEL_PS2202:
      this->Scope.model = MODEL_PS2202;
      this->Scope.firstRange = PS2000_100MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = TRUE;
      this->Scope.hasSignalGenerator = FALSE;
      this->Scope.hasEts = FALSE;
      this->Scope.hasFastStreaming = TRUE;
      break;

    case MODEL_PS2203:
      this->Scope.model = MODEL_PS2203;
      this->Scope.firstRange = PS2000_50MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = FALSE;
      this->Scope.hasSignalGenerator = TRUE;
      this->Scope.hasEts = TRUE;
      this->Scope.hasFastStreaming = TRUE;
      break;

    case MODEL_PS2204:
      this->Scope.model = MODEL_PS2204;
      this->Scope.firstRange = PS2000_50MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = TRUE;
      this->Scope.hasSignalGenerator = TRUE;
      this->Scope.hasEts = TRUE;
      this->Scope.hasFastStreaming = TRUE;
      break;

    case MODEL_PS2204A:
      this->Scope.model = MODEL_PS2204A;
      this->Scope.firstRange = PS2000_50MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = TRUE;
      this->Scope.hasSignalGenerator = TRUE;
      this->Scope.hasEts = TRUE;
      this->Scope.hasFastStreaming = TRUE;
      this->Scope.awgBufferSize = 4096;
      break;

    case MODEL_PS2205:
      this->Scope.model = MODEL_PS2205;
      this->Scope.firstRange = PS2000_50MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = TRUE;
      this->Scope.hasSignalGenerator = TRUE;
      this->Scope.hasEts = TRUE;
      this->Scope.hasFastStreaming = TRUE;
      break;

    case MODEL_PS2205A:
      this->Scope.model = MODEL_PS2205A;
      this->Scope.firstRange = PS2000_50MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = TRUE;
      this->Scope.hasSignalGenerator = TRUE;
      this->Scope.hasEts = TRUE;
      this->Scope.hasFastStreaming = TRUE;
      this->Scope.awgBufferSize = 4096;
      break;

    default:
      printf("Unit not supported");
    }

    this->Scope.channelSettings[PS2000_CHANNEL_A].enabled = 1;
    this->Scope.channelSettings[PS2000_CHANNEL_A].DCcoupled = 1;
    this->Scope.channelSettings[PS2000_CHANNEL_A].range = PS2000_5V;

    if (this->Scope.noOfChannels == 2)
    {
      this->Scope.channelSettings[PS2000_CHANNEL_B].enabled = 1;
    }
    else
    {
      this->Scope.channelSettings[PS2000_CHANNEL_B].enabled = 0;
    }

    this->Scope.channelSettings[PS2000_CHANNEL_B].DCcoupled = 1;
    this->Scope.channelSettings[PS2000_CHANNEL_B].range = PS2000_5V;

    this->SetPicoScopeDefaults();

  }
  else
  {
    printf("Unit Not Opened\n");

    ps2000_get_unit_info(this->Scope.handle, line, sizeof(line), 5);

    printf("%s: %s\n", description[5], line);
    this->Scope.model = MODEL_NONE;
    this->Scope.firstRange = PS2000_100MV;
    this->Scope.lastRange = PS2000_20V;
    this->Scope.timebases = PS2105_MAX_TIMEBASE;
    this->Scope.noOfChannels = 1;
  }
}

void vtkPlusPicoScopeDataSource::vtkInternal::SetPicoScopeDefaults(void)
{
  int16_t ch = 0;
  ps2000_set_ets(this->Scope.handle, PS2000_ETS_OFF, 0, 0);

  for (ch = 0; ch < this->Scope.noOfChannels; ch++)
  {
    ps2000_set_channel(this->Scope.handle,
      ch,
      this->Scope.channelSettings[ch].enabled,
      this->Scope.channelSettings[ch].DCcoupled,
      this->Scope.channelSettings[ch].range);
  }
}
//----------------------------------------------------------------------------
vtkPlusPicoScopeDataSource::vtkPlusPicoScopeDataSource()
  : vtkPlusDevice()
  , Internal(new vtkInternal(this))
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::vtkPlusPicoScopeDataSource()");

  this->FrameNumber = 0;
  this->StartThreadForInternalUpdates = true;
  this->InternalUpdateRate = 30;
}

//----------------------------------------------------------------------------
vtkPlusPicoScopeDataSource::~vtkPlusPicoScopeDataSource()
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::~vtkPlusPicoScopeDataSource()");

  delete Internal;
  Internal = nullptr;
}

//----------------------------------------------------------------------------
void vtkPlusPicoScopeDataSource::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::ReadConfiguration");

  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::WriteConfiguration");

  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::Probe()
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::Probe");

  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::InternalConnect()
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::InternalConnect");

  // Connect to scope
  this->Internal->Scope.handle = ps2000_open_unit();
  
  if (!this->Internal->Scope.handle)
  {
    // scope is not connected
    LOG_INFO("Failed to connect to PicoScope PS2000.");
    return PLUS_FAIL;
  }

  this->Internal->GetPicoScopeInfo();

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::InternalDisconnect()
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::InternalDisconnect");
  ps2000_close_unit(this->Internal->Scope.handle);
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::InternalStartRecording()
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::InternalStartRecording");

  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::InternalStopRecording()
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::InternalStopRecording");

  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::InternalUpdate()
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::InternalUpdate");
  
  this->FrameNumber++;
 
  return PLUS_FAIL;
}