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

};

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
  
  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusPicoScopeDataSource::InternalDisconnect()
{
  LOG_TRACE("vtkPlusPicoScopeDataSource::InternalDisconnect");
 
  return PLUS_FAIL;
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