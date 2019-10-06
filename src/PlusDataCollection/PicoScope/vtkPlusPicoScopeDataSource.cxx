/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

// Local includes
#include "vtkIGSIOAccurateTimer.h"
#include "vtkPlusPicoScopeDataSource.h"
#include "vtkPlusDataSource.h"

// VTK includes
#include <vtkMatrix4x4.h>
#include <vtkSmartPointer.h>

// STL includes
#include <string>
#include <sstream>

// PicoScope includes
#include "ps2000.h"

vtkStandardNewMacro(vtkPlusPicoScopeDataSource);

// constants
#define BUFFER_SIZE  1024 // pico scope buffer size

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
    PS2000_THRESHOLD_DIRECTION channelA;
    PS2000_THRESHOLD_DIRECTION channelB;
    PS2000_THRESHOLD_DIRECTION channelC;
    PS2000_THRESHOLD_DIRECTION channelD;
    PS2000_THRESHOLD_DIRECTION ext;
  } DIRECTIONS;

  typedef struct
  {
    PS2000_PWQ_CONDITIONS   * conditions;
    int16_t       nConditions;
    PS2000_THRESHOLD_DIRECTION  direction;
    uint32_t      lower;
    uint32_t      upper;
    PS2000_PULSE_WIDTH_TYPE   type;
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

  // struct to hold scope handle
  typedef struct {
    int16_t   handle;
    MODEL_TYPE  model;
    PS2000_RANGE firstRange;
    PS2000_RANGE lastRange;
    TRIGGER_CHANNEL trigger;
    int16_t   maxTimebase;
    int16_t   timebases;
    int16_t   noOfChannels;
    int16_t   hasAdvancedTriggering;
    int16_t   hasFastStreaming;
    int16_t   hasEts;
    int16_t   hasSignalGenerator;
    int16_t   awgBufferSize;
  } UNIT_MODEL;

  // PLUS structs
  typedef enum {
    DC = 0,
    AC = 1
  } COUPLING;

  typedef struct {
    COUPLING coupling;
    PS2000_RANGE voltageRange; 
    int16_t enabled;

    std::string PlusSourceId;
    vtkPlusDataSource* PlusSource;
  } CHANNEL_SETTINGS;

  // METHODS

  // 
  std::string GetPicoScopeInfo(void);

  // 
  enPS2000Range ConvertRangeIntToEnum(int voltageRangeMV);

  //
  std::string GetAdcUnits(int16_t time_units);
  
  //
  int32_t AdcToMv(int32_t raw, int32_t ch);

  //
  PlusStatus SetupPicoScopeChannel(CHANNEL_SETUP channel);

  //
  PlusStatus SetPicoScopeTimebase(int timebase, int numSamples);

  // MEMBER VARIABLES
  UNIT_MODEL Scope;  // scope handle

  int PSInputRanges[PS2000_MAX_RANGES] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000 };

  CHANNEL_SETTINGS ChannelA = CHANNEL_SETUP(false, -1);
  CHANNEL_SETTINGS ChannelB = CHANNEL_SETUP(false, -1);

  int32_t scale_to_mv = 1;

  vtkSmartPointer<vtkImageData> SignalImage = vtkSmartPointer<vtkImageData>::New();
};

//----------------------------------------------------------------------------
std::string vtkPlusPicoScopeDataSource::vtkInternal::GetPicoScopeInfo()
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
  int16_t i;
  int8_t line[80];
  int32_t variant;

  // stringstream to contain device info
  std::stringstream deviceInfoSS;

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

          if (line[1] == '2' && line[4] == 'A')  // i.e 2204A -> 0xA204
          {
            variant += 0x9968;
          }
        }
      }

      if (i != 6) // No need to print error code
      {
        deviceInfoSS << description[i] << " " << line << std::endl;
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
      this->Scope.hasAdvancedTriggering = false;
      this->Scope.hasSignalGenerator = false;
      this->Scope.hasEts = true;
      this->Scope.hasFastStreaming = false;
      break;

    case MODEL_PS2105:
      this->Scope.model = MODEL_PS2105;
      this->Scope.firstRange = PS2000_100MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2105_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 1;
      this->Scope.hasAdvancedTriggering = false;
      this->Scope.hasSignalGenerator = false;
      this->Scope.hasEts = true;
      this->Scope.hasFastStreaming = false;
      break;

    case MODEL_PS2202:
      this->Scope.model = MODEL_PS2202;
      this->Scope.firstRange = PS2000_100MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = true;
      this->Scope.hasSignalGenerator = false;
      this->Scope.hasEts = false;
      this->Scope.hasFastStreaming = true;
      break;

    case MODEL_PS2203:
      this->Scope.model = MODEL_PS2203;
      this->Scope.firstRange = PS2000_50MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = false;
      this->Scope.hasSignalGenerator = true;
      this->Scope.hasEts = true;
      this->Scope.hasFastStreaming = true;
      break;

    case MODEL_PS2204:
      this->Scope.model = MODEL_PS2204;
      this->Scope.firstRange = PS2000_50MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = true;
      this->Scope.hasSignalGenerator = true;
      this->Scope.hasEts = true;
      this->Scope.hasFastStreaming = true;
      break;

    case MODEL_PS2204A:
      this->Scope.model = MODEL_PS2204A;
      this->Scope.firstRange = PS2000_50MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = true;
      this->Scope.hasSignalGenerator = true;
      this->Scope.hasEts = true;
      this->Scope.hasFastStreaming = true;
      this->Scope.awgBufferSize = 4096;
      break;

    case MODEL_PS2205:
      this->Scope.model = MODEL_PS2205;
      this->Scope.firstRange = PS2000_50MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = true;
      this->Scope.hasSignalGenerator = true;
      this->Scope.hasEts = true;
      this->Scope.hasFastStreaming = true;
      break;

    case MODEL_PS2205A:
      this->Scope.model = MODEL_PS2205A;
      this->Scope.firstRange = PS2000_50MV;
      this->Scope.lastRange = PS2000_20V;
      this->Scope.maxTimebase = PS2200_MAX_TIMEBASE;
      this->Scope.timebases = this->Scope.maxTimebase;
      this->Scope.noOfChannels = 2;
      this->Scope.hasAdvancedTriggering = true;
      this->Scope.hasSignalGenerator = true;
      this->Scope.hasEts = true;
      this->Scope.hasFastStreaming = true;
      this->Scope.awgBufferSize = 4096;
      break;

    default:
      LOG_ERROR("PicoScope model not supported.");
    }
  }
  else
  {
    LOG_ERROR("Failed to open PicoScope device.");

    ps2000_get_unit_info(this->Scope.handle, line, sizeof(line), 5);

    deviceInfoSS << description[5] << " " << line);
    this->Scope.model = MODEL_NONE;
    this->Scope.firstRange = PS2000_100MV;
    this->Scope.lastRange = PS2000_20V;
    this->Scope.timebases = PS2105_MAX_TIMEBASE;
    this->Scope.noOfChannels = 1;
  }

  return deviceInfoSS.str();
}

//----------------------------------------------------------------------------
PS2000_RANGE vtkPlusPicoScopeDataSource::vtkInternal::ConvertRangeIntToEnum(int voltageRangeMV)
{
  switch (voltageRangeMV)
  {
  case 10:
    return PS2000_10MV;
  case 20:
    return PS2000_20MV;
  case 50:
    return PS2000_50MV;
  case 100:
    return PS2000_100MV;
  case 200:
    return PS2000_200MV;
  case 500:
    return PS2000_500MV;
  case 1000:
    return PS2000_1V;
  case 2000:
    return PS2000_2V;
  case 5000:
    return PS2000_5V;
  case 10000:
    return PS2000_10V;
  case 20000:
    return PS2000_20V;
  case 50000:
    return PS2000_50V;
  default:
    LOG_WARNING("Invalid voltage range: " << voltageRangeMV << ". Using default range of 5V.");
  }
}

//----------------------------------------------------------------------------

std::string vtkPlusPicoScopeDataSource::vtkInternal::GetAdcUnits(int16_t time_units)
{
  time_units++;
  //printf ( "time unit:  %d\n", time_units ) ;
  switch (time_units)
  {
  case 0:
    return "ADC";
  case 1:
    return "fs";
  case 2:
    return "ps";
  case 3:
    return "ns";
  case 4:
    return "us";
  case 5:
    return "ms";
  }

  return "Not Known";
}

int32_t vtkPlusPicoScopeDataSource::vtkInternal::AdcToMv(int32_t raw, int32_t ch)
{
  return (scale_to_mv) ? (raw * PSInputRanges[ch]) / 32767 : raw;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::vtkInternal::SetupPicoScopeChannel(CHANNEL_SETUP channel)
{
  // check this channel is enabled
  if (!channel.enabled)
  {
    return PLUS_SUCCESS;
  }

  // check channel is valid
  if (!(channel.channelNum == 0 || (channel.channelNum == 1 && this->Scope.noOfChannels == 2)))
  {
    LOG_ERROR("Unsupported channel number: " << channel.channelNum << ". Check the number of channels your oscilloscope supports. Note: channel numbers start at 0.");
    return PLUS_FAIL;
  }

  // check voltage is valid
  bool voltageValid(false);
  for (int i = this->Scope.firstRange; i <= this->Scope.lastRange; i++)
  {
    if (this->PSInputRanges[i] == channel.voltageRangeMV)
    {
      voltageValid = true;
    }
  }

  if (!voltageValid)
  {
    std::string validVoltages;
    for (int i = this->Scope.firstRange; i <= this->Scope.lastRange; i++)
    {
      validVoltages = validVoltages + " " + std::to_string(PSInputRanges[i]) + "mV,";
    }
    LOG_ERROR("Invalid voltage range. Please choose from: " << validVoltages);
    return PLUS_FAIL;
  }

  // convert voltage into PS SDK compatible format
  enPS2000Range voltageRangeEnum = this->ConvertRangeIntToEnum(channel.voltageRangeMV);

  // convert coupling into PS SDK compatible format
  bool couplingBool;
  (channel.coupling == DC) ? couplingBool = true : couplingBool = false;
  
  // set channel values
  if (!ps2000_set_channel(this->Scope.handle, channel.channelNum, true, couplingBool, voltageRangeEnum))
  {
    LOG_ERROR("Failed to set PicoScope channel settings.");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus SetPicoScopeTimebase(int timebase, int numSamples)
{
  return PLUS_SUCCESS;
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

  XML_FIND_NESTED_ELEMENT_REQUIRED(dataSourcesElement, deviceConfig, "DataSources");
  for (int nestedElementIndex = 0; nestedElementIndex < dataSourcesElement->GetNumberOfNestedElements(); nestedElementIndex++)
  {
    vtkXMLDataElement* channelDataElement = dataSourcesElement->GetNestedElement(nestedElementIndex);
    if (STRCASECMP(channelDataElement->GetName(), "DataSource") != 0)
    {
      // if this is not a data source element, skip it
      continue;
    }
    if (channelDataElement->GetAttribute("Type") != NULL && STRCASECMP(channelDataElement->GetAttribute("Type"), "Tool") != 0)
    {
      // if this is not a Tool element, skip it
      continue;
    }
    std::string toolId(channelDataElement->GetAttribute("Id"));
    if (toolId.empty())
    {
      // Channel doesn't have ID needed to generate signal output
      LOG_ERROR("Failed to initialize PicoScope channel: DataSource Id is missing.");
      continue;
    }

    int channelNum;
    XML_READ_SCALAR_ATTRIBUTE_NONMEMBER_REQUIRED(int, Channel, channelNum, channelDataElement);
    int voltageRangeMV;
    XML_READ_SCALAR_ATTRIBUTE_NONMEMBER_REQUIRED(int, VoltageRangeMV, voltageRangeMV, channelDataElement);
    vtkInternal::COUPLING coupling = vtkInternal::DC;
    XML_READ_ENUM2_ATTRIBUTE_NONMEMBER_OPTIONAL(Coupling, coupling, channelDataElement, "AC", vtkInternal::AC, "DC", vtkInternal::DC);

    LOG_INFO(channelNum);
    if (channelNum == 0)
    {
      this->Internal->ChannelA.enabled = true;
      this->Internal->ChannelA.channelNum = 0;
      this->Internal->ChannelA.coupling = coupling;
      this->Internal->ChannelA.voltageRangeMV = voltageRangeMV;
      this->Internal->ChannelA.toolId = toolId;
    }
    else if (channelNum == 1)
    {
      this->Internal->ChannelB.enabled = true;
      this->Internal->ChannelB.channelNum = 1;
      this->Internal->ChannelB.coupling = coupling;
      this->Internal->ChannelB.voltageRangeMV = voltageRangeMV;
      this->Internal->ChannelB.toolId = toolId;
    }
    else
    {
      LOG_ERROR("Invalid channel number: " << channelNum << ". Options are 0 (Channel A), and 1 (Channel B, if supported by your hardware).");
    }
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::WriteConfiguration");

  return PLUS_SUCCESS;
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
    LOG_ERROR("Failed to connect to PicoScope PS2000.");
    return PLUS_FAIL;
  }

  this->Internal->GetPicoScopeInfo();

  this->Internal->SetupPicoScopeChannel(this->Internal->ChannelA);
  this->Internal->SetupPicoScopeChannel(this->Internal->ChannelB);

  this->Internal->SignalImage->SetDimensions(BUFFER_SIZE, 1, 1);
  this->Internal->SignalImage->AllocateScalars(VTK_INT, 1);
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

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::InternalStopRecording()
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::InternalStopRecording");

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::InternalUpdate()
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::InternalUpdate");
  
  int32_t  i;
  int32_t  time_interval;
  int16_t  time_units;
  int16_t  oversample;
  int32_t  no_of_samples = BUFFER_SIZE;
  FILE *  fp;
  int16_t  auto_trigger_ms = 0;
  int32_t  time_indisposed_ms;
  int16_t  overflow;
  int32_t  max_samples;
  int16_t  ch = 0;

  /* Trigger disabled */
  ps2000_set_trigger(this->Internal->Scope.handle, PS2000_NONE, 0, PS2000_RISING, 0, auto_trigger_ms);

  /*  Find the maximum number of samples, the time interval (in time_units),
  *   the most suitable time units, and the maximum oversample at the current timebase
  */
  oversample = 1;
  int timebase = 10;
  while (!ps2000_get_timebase(this->Internal->Scope.handle,
    timebase,
    no_of_samples,
    &time_interval,
    &time_units,
    oversample,
    &max_samples))
    timebase++;


  /* Start it collecting,
  *  then wait for completion
  */

  ps2000_run_block(this->Internal->Scope.handle, no_of_samples, timebase, oversample, &time_indisposed_ms);

  while (!ps2000_ready(this->Internal->Scope.handle))
  {
    vtkIGSIOAccurateTimer::Delay(10);
  }

  ps2000_stop(this->Internal->Scope.handle);

  int32_t times[BUFFER_SIZE];

  ps2000_get_times_and_values(
    this->Internal->Scope.handle,
    times,
    this->Internal->Scope.channelSettings[PS2000_CHANNEL_A].values,
    this->Internal->Scope.channelSettings[PS2000_CHANNEL_B].values,
    NULL,
    NULL,
    &overflow, time_units, no_of_samples);

  if (this->Internal->ChannelA.enabled == true)
  {
    int signal;
    int max = -1000000;
    int mean = 0;
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
      signal = (int)this->Internal->Scope.channelSettings[PS2000_CHANNEL_A].values[i];
      if (signal > max)
      {
        max = signal;
      }
      mean += (abs(signal));
    }
    mean = mean / BUFFER_SIZE;
    
    // put max/mean in transorm and add to buffer
    vtkNew<vtkMatrix4x4> sigmaxTransform;
    sigmaxTransform->Identity();
    sigmaxTransform->SetElement(0, 3, max);
    sigmaxTransform->SetElement(1, 3, mean);
    igsioTransformName toolTransformName(this->Internal->ChannelA.toolId, this->GetToolReferenceFrameName());
    std::string toolSourceId = toolTransformName.GetTransformName();
    const double unfilteredTimestamp = vtkIGSIOAccurateTimer::GetSystemTime();
    ToolTimeStampedUpdate(toolSourceId, sigmaxTransform.Get(), TOOL_OK, this->FrameNumber, unfilteredTimestamp);
  }
  
  if (this->Internal->ChannelB.enabled == true)
  {
    int signal;
    int max = -1000000;
    int mean = 0;
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
      signal = (int)this->Internal->Scope.channelSettings[PS2000_CHANNEL_B].values[i];
      if (signal > max)
      {
        max = signal;
      }
      mean += (abs(signal));
    }
    mean = mean / BUFFER_SIZE;

    // put max/mean in transorm and add to buffer
    vtkNew<vtkMatrix4x4> sigmaxTransform;
    sigmaxTransform->Identity();
    sigmaxTransform->SetElement(0, 3, max);
    sigmaxTransform->SetElement(1, 3, mean);
    igsioTransformName toolTransformName(this->Internal->ChannelB.toolId, this->GetToolReferenceFrameName());
    std::string toolSourceId = toolTransformName.GetTransformName();
    const double unfilteredTimestamp = vtkIGSIOAccurateTimer::GetSystemTime();
    ToolTimeStampedUpdate(toolSourceId, sigmaxTransform.Get(), TOOL_OK, this->FrameNumber, unfilteredTimestamp);
  }

  // TODO: Send signal vector as ndarray over openigtlink
  //// transfer data into image
  //vtkPlusDataSource* aSource(NULL);
  //if (this->GetFirstVideoSource(aSource) != PLUS_SUCCESS)
  //{
  //  LOG_ERROR("Unable to retrieve the video source in the PicoScope device.");
  //  return PLUS_FAIL;
  //}

  //FrameSizeType frameSizeInPx = { BUFFER_SIZE, 1, 1 };

  //if (aSource->GetNumberOfItems() == 0)
  //{
  //  // initialize the video source
  //  aSource->SetImageType(US_IMG_TYPE_XX);
  //  aSource->SetPixelType(VTK_INT);
  //  aSource->SetNumberOfScalarComponents(1);
  //  aSource->SetInputFrameSize(frameSizeInPx);
  //  aSource->SetInputImageOrientation(US_IMG_ORIENT_MF);
  //}

  //
  //aSource->AddItem(this->Internal->SignalImage, US_IMG_ORIENT_MF, US_IMG_TYPE_XX, this->FrameNumber);
  //this->Modified();

  this->FrameNumber++;
 
  return PLUS_FAIL;
}