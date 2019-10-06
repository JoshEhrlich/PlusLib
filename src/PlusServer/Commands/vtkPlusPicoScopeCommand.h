/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#ifndef __vtkPlusPicoScopeCommand_h
#define __vtkPlusPicoScopeCommand_h

#include "vtkPlusServerExport.h"
#include "vtkPlusCommand.h"

class vtkPlusPicoScopeDataSource;

/*!
  \class vtkPlusPicoScopeCommand
  \brief This class enables the remote control of the vtkPlusPicoScopeDataSource
  device over OpenIGTLink.
  \ingroup PlusLibPlusServer
 */
class vtkPlusServerExport vtkPlusPicoScopeCommand : public vtkPlusCommand
{
public:

  static vtkPlusPicoScopeCommand* New();
  vtkTypeMacro(vtkPlusPicoScopeCommand, vtkPlusCommand);
  virtual void PrintSelf(ostream& os, vtkIndent indent);
  virtual vtkPlusCommand* Clone() { return New(); }

  /*! Executes the command  */
  virtual PlusStatus Execute();

  /*! Read command parameters from XML */
  virtual PlusStatus ReadConfiguration(vtkXMLDataElement* aConfig);

  /*! Write command parameters to XML */
  virtual PlusStatus WriteConfiguration(vtkXMLDataElement* aConfig);

  /*! Get all the command names that this class can execute */
  virtual void GetCommandNames(std::list<std::string>& cmdNames);

  /*! Gets the description for the specified command name. */
  virtual std::string GetDescription(const std::string& commandName);

  /*! Id of the ultrasound device to change the parameters of at the next Execute */
  vtkGetStdStringMacro(PicoScopeDeviceId);
  vtkSetStdStringMacro(PicoScopeDeviceId);

protected:
  vtkPlusPicoScopeDataSource* GetPicoScopeDevice();

  vtkPlusPicoScopeCommand();
  virtual ~vtkPlusPicoScopeCommand();

protected:
  std::string PicoScopeDeviceId;
  vtkPlusPicoScopeCommand(const vtkPlusPicoScopeCommand&);
  
  void operator=(const vtkPlusPicoScopeCommand&);

  // list of commands to execute
  std::map<std::string, std::string> CommandList;

  // helper to convert string to boolean
  PlusStatus StringToBool(std::string strVal, bool& boolVal);
};

#endif
