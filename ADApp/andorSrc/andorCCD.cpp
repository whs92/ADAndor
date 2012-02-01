/**
 * Area Detector driver for the Andor CCD.
 *
 * @author Matthew Pearson
 * @date June 2009
 *
 * Updated Dec 2011 for Asyn 4-17 and areaDetector 1-7 
 *
 */

#include <iostream>
#include <fstream>

#include "andorCCD.h"

#include <iocsh.h>
#include <epicsExport.h>
#include <epicsExit.h>

using std::cout;
using std::endl;
using std::flush;
using std::ofstream;

//Definitions of static class data members

const epicsInt32 AndorCCD::AImageFastKinetics = ADImageContinuous+1;

const epicsUInt32 AndorCCD::AASingle = 1;
const epicsUInt32 AndorCCD::AAAccumulate = 2;
const epicsUInt32 AndorCCD::AAKinetics = 3;
const epicsUInt32 AndorCCD::AAFastKinetics = 4;
const epicsUInt32 AndorCCD::AARunTillAbort = 5;
const epicsUInt32 AndorCCD::AATimeDelayedInt = 9;

const epicsUInt32 AndorCCD::ATInternal = 0;
const epicsUInt32 AndorCCD::ATExternal = 1;
const epicsUInt32 AndorCCD::ATExternalStart = 6;
const epicsUInt32 AndorCCD::ATExternalExposure = 7;
const epicsUInt32 AndorCCD::ATExternalFVB = 9;
const epicsUInt32 AndorCCD::ATSoftware = 10;

const epicsUInt32 AndorCCD::ASIdle = DRV_IDLE;
const epicsUInt32 AndorCCD::ASTempCycle = DRV_TEMPCYCLE;
const epicsUInt32 AndorCCD::ASAcquiring = DRV_ACQUIRING;
const epicsUInt32 AndorCCD::ASAccumTimeNotMet = DRV_ACCUM_TIME_NOT_MET;
const epicsUInt32 AndorCCD::ASKineticTimeNotMet = DRV_KINETIC_TIME_NOT_MET;
const epicsUInt32 AndorCCD::ASErrorAck = DRV_ERROR_ACK;
const epicsUInt32 AndorCCD::ASAcqBuffer = DRV_ACQ_BUFFER;
const epicsUInt32 AndorCCD::ASSpoolError = DRV_SPOOLERROR;

const epicsInt32 AndorCCD::ARFullVerticalBinning = 0;
const epicsInt32 AndorCCD::ARMultiTrack = 1;
const epicsInt32 AndorCCD::ARRandomTrack = 2;
const epicsInt32 AndorCCD::ARSingleTrack = 3;
const epicsInt32 AndorCCD::ARImage = 4;

const epicsInt32 AndorCCD::AShutterAuto = 0;
const epicsInt32 AndorCCD::AShutterOpen = 1;
const epicsInt32 AndorCCD::AShutterClose = 2;

const epicsInt32 AndorCCD::AFFTIFF = 0;
const epicsInt32 AndorCCD::AFFBMP = 1;
const epicsInt32 AndorCCD::AFFSIF = 2;
const epicsInt32 AndorCCD::AFFEDF = 3;
const epicsInt32 AndorCCD::AFFRAW = 4;
const epicsInt32 AndorCCD::AFFTEXT = 5;

//C Function prototypes to tie in with EPICS
static void andorStatusTaskC(void *drvPvt);
static void andorDataTaskC(void *drvPvt);
static void exitHandler(void *drvPvt);

/**
 * Constructor for AndorCCD::AndorCCD.
 *
 */
AndorCCD::AndorCCD(const char *portName, int maxBuffers, size_t maxMemory, 
                   const char *installPath, int priority, int stackSize)

  : ADDriver(portName, 1, NUM_ANDOR_DET_PARAMS, maxBuffers, maxMemory, priority, stackSize,
             ASYN_CANBLOCK, 1, 0, 0)
{

  int status = asynSuccess;
  int binX=1, binY=1, minX=0, minY=0, sizeX, sizeY;
  char model[256];
  const char *functionName = "AndorCCD::AndorCCD";

  cout << "Constructing AndorCCD driver..." << endl;
  
  if (installPath == NULL)
    strcpy(mInstallPath, "");
  else 
    mInstallPath = epicsStrDup(installPath);

  /* Create an EPICS exit handler */
  epicsAtExit(exitHandler, this);

  createParam(AndorCoolerParamString,             asynParamInt32, &AndorCoolerParam);
  createParam(AndorTempStatusMessageString,       asynParamOctet, &AndorTempStatusMessage);
  createParam(AndorMessageString,                 asynParamOctet, &AndorMessage);
  createParam(AndorShutterModeString,             asynParamInt32, &AndorShutterMode);
  createParam(AndorShutterExTTLString,            asynParamInt32, &AndorShutterExTTL);
  createParam(AndorPalFileNameString,             asynParamOctet, &AndorPalFileName);
  createParam(AndorAccumulatePeriodString,      asynParamFloat64, &AndorAccumulatePeriod);
  createParam(AndorAdcSpeedString,                asynParamInt32, &AndorAdcSpeed);

  //Create the epicsEvent for signaling to the status task when parameters should have changed.
  //This will cause it to do a poll immediately, rather than wait for the poll time period.
  this->statusEvent = epicsEventMustCreate(epicsEventEmpty);
  if (!this->statusEvent) {
    printf("%s:%s epicsEventCreate failure for start event\n", driverName, functionName);
    return;
  }

  //Use this to signal the data acquisition task that acquisition has started.
  this->dataEvent = epicsEventMustCreate(epicsEventEmpty);
  if (!this->dataEvent) {
    printf("%s:%s epicsEventCreate failure for data event\n", driverName, functionName);
    return;
  }

  //Initialize camera
  try {
    cout << "Initializing CCD...\n" << endl;
    checkStatus(Initialize(mInstallPath));
    setStringParam(AndorMessage, "Camera successfully initialized.");
    checkStatus(GetDetector(&sizeX, &sizeY));
    checkStatus(GetHeadModel(model));
    checkStatus(SetReadMode(ARImage));
    checkStatus(SetImage(binX, binY, minX+1, minX+sizeX, minY+1, minY+sizeY));
    callParamCallbacks();
  } catch (const std::string &e) {
    cout << e << endl;
    return;
  }
  
  // Read the model
  

  /* Set some default values for parameters */
  status =  setStringParam(ADManufacturer, "Andor");
  status |= setStringParam(ADModel, model);
  status |= setIntegerParam(ADSizeX, sizeX);
  status |= setIntegerParam(ADSizeY, sizeY);
  status |= setIntegerParam(ADBinX, 1);
  status |= setIntegerParam(ADBinY, 1);
  status |= setIntegerParam(ADMinX, 0);
  status |= setIntegerParam(ADMinY, 0);
  status |= setIntegerParam(ADMaxSizeX, sizeX);
  status |= setIntegerParam(ADMaxSizeY, sizeY);  
  status |= setIntegerParam(ADImageMode, ADImageSingle);
  status |= setIntegerParam(ADTriggerMode, AndorCCD::ATInternal);
  mAcquireTime = 1.0;
  status |= setDoubleParam (ADAcquireTime, mAcquireTime);
  mAcquirePeriod = 5.0;
  status |= setDoubleParam (ADAcquirePeriod, mAcquirePeriod);
  status |= setIntegerParam(ADNumImages, 1);
  status |= setIntegerParam(ADNumExposures, 1);
  status |= setIntegerParam(NDArraySizeX, sizeX);
  status |= setIntegerParam(NDArraySizeY, sizeY);
  status |= setIntegerParam(NDDataType, NDUInt16);
  status |= setIntegerParam(NDArraySize, sizeX*sizeY*sizeof(epicsUInt16)); 
  mAccumulatePeriod = 2.0;
  status |= setDoubleParam(AndorAccumulatePeriod, mAccumulatePeriod); 
  status |= setIntegerParam(AndorAdcSpeed, 0);
  status |= setIntegerParam(AndorShutterExTTL, 1);
  status |= setIntegerParam(AndorShutterMode, AShutterAuto);
  status |= setDoubleParam(ADShutterOpenDelay, 0.);
  status |= setDoubleParam(ADShutterCloseDelay, 0.);
  status |= setupShutter(-1);

  callParamCallbacks();

  /* Send a signal to the poller task which will make it do a poll, and switch to the fast poll rate */
  epicsEventSignal(statusEvent);

  if (status) {
    printf("%s: unable to set camera parameters\n", functionName);
    return;
  }

  //Define the polling periods for the status thread.
  mPollingPeriod = 0.2; //seconds
  mFastPollingPeriod = 0.05; //seconds

  mAcquiringData = 0;
  
  if (stackSize == 0) stackSize = epicsThreadGetStackSize(epicsThreadStackMedium);
  printf("Stack size = %d\n", stackSize);

  /* Create the thread that updates the detector status */
  status = (epicsThreadCreate("AndorStatusTask",
                              epicsThreadPriorityMedium,
                              stackSize,
                              (EPICSTHREADFUNC)andorStatusTaskC,
                              this) == NULL);
  if (status) {
    printf("%s:%s epicsThreadCreate failure for status task\n",
           driverName, functionName);
    return;
  }

  
  /* Create the thread that does data readout */
  status = (epicsThreadCreate("AndorDataTask",
                              epicsThreadPriorityMedium,
                              stackSize,
                              (EPICSTHREADFUNC)andorDataTaskC,
                              this) == NULL);
  if (status) {
    printf("%s:%s epicsThreadCreate failure for data task\n",
           driverName, functionName);
    return;
  }
}

AndorCCD::~AndorCCD() 
{
  try {
    cout << "Shutdown and freeing up memory..." << endl;
    this->lock();
    checkStatus(FreeInternalMemory());
    checkStatus(ShutDown());
    cout << "Camera shutting down as part of IOC exit." << endl;
    this->unlock();
  } catch (const std::string &e) {
    cout << e << endl;
  }
}


/**
 * Exit handler, delete the AndorCCD object.
 */

static void exitHandler(void *drvPvt)
{
  AndorCCD *pAndorCCD = (AndorCCD *)drvPvt;
  delete pAndorCCD;
}


void AndorCCD::report(FILE *fp, int details)
{
  int param1;
  float fParam1;
  int xsize, ysize;
  int i;
  unsigned int uIntParam1;
  unsigned int uIntParam2;
  unsigned int uIntParam3;
  unsigned int uIntParam4;
  unsigned int uIntParam5;
  unsigned int uIntParam6;
  AndorCapabilities capabilities;

  fprintf(fp, "Andor CCD port=%s\n", this->portName);
  if (details > 0) {
    try {
      checkStatus(GetCameraSerialNumber(&param1));
      fprintf(fp, "  serial number: %d\n", param1); 
      checkStatus(GetHardwareVersion(&uIntParam1, &uIntParam2, &uIntParam3, 
                                     &uIntParam4, &uIntParam5, &uIntParam6));
      fprintf(fp, "  PCB Version: %d\n", uIntParam1);
      fprintf(fp, "  Flex File Version: %d\n", uIntParam2);
      fprintf(fp, "  Firmware Version: %d\n", uIntParam5);
      fprintf(fp, "  Firmware Build: %d\n", uIntParam6);
      getIntegerParam(ADMaxSizeX, &xsize);
      getIntegerParam(ADMaxSizeY, &ysize);
      fprintf(fp, "  xpixels: %d\n", xsize);
      fprintf(fp, "  ypixels: %d\n", ysize);
      checkStatus(GetNumberAmp(&param1));
      fprintf(fp, "  Number of amplifier channels: %d\n", param1);
      checkStatus(GetNumberADChannels(&param1));
      fprintf(fp, "  Number of ADC channels: %d\n", param1);
      checkStatus(GetNumberPreAmpGains(&param1));
      fprintf(fp, "  Number of pre-amp gains: %d\n", param1);
      for (i=0; i<param1; i++) {
        checkStatus(GetPreAmpGain(i, &fParam1));
        fprintf(fp, "    Gain[%d]: %f\n", i, fParam1);
      }
      capabilities.ulSize = sizeof(capabilities);
      checkStatus(GetCapabilities(&capabilities));
      fprintf(fp, "  Capabilities\n");
      fprintf(fp, "        AcqModes=0x%lx\n", capabilities.ulAcqModes);
      fprintf(fp, "       ReadModes=0x%lx\n", capabilities.ulReadModes);
      fprintf(fp, "     FTReadModes=0x%lx\n", capabilities.ulFTReadModes);
      fprintf(fp, "    TriggerModes=0x%lx\n", capabilities.ulTriggerModes);
      fprintf(fp, "      CameraType=0x%lx\n", capabilities.ulCameraType);
      fprintf(fp, "      PixelModes=0x%lx\n", capabilities.ulPixelMode);
      fprintf(fp, "    SetFunctions=0x%lx\n", capabilities.ulSetFunctions);
      fprintf(fp, "    GetFunctions=0x%lx\n", capabilities.ulGetFunctions);
      fprintf(fp, "        Features=0x%lx\n", capabilities.ulFeatures);
      fprintf(fp, "         PCI MHz=%ld\n",   capabilities.ulPCICard);
      fprintf(fp, "          EMGain=0x%lx\n", capabilities.ulEMGainCapability);

    } catch (const std::string &e) {
      cout << e << endl;
    }
  }
  // Call the base class method
  ADDriver::report(fp, details);
}


asynStatus AndorCCD::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    int adstatus = 0;
    epicsInt32 oldValue;

    asynStatus status = asynSuccess;
    const char *functionName = "AndorCCD::writeInt32";

    //Set in param lib so the user sees a readback straight away. Save a backup in case of errors.
    getIntegerParam(function, &oldValue);
    status = setIntegerParam(function, value);

    if (function == ADAcquire) {
      getIntegerParam(ADStatus, &adstatus);
      if (value && (adstatus == ADStatusIdle)) {
        try {
          mAcquiringData = 1;
          //We send an event at the bottom of this function.
        } catch (const std::string &e) {
          cout << e << endl;
          status = asynError;
        }
      }
      if (!value && (adstatus != ADStatusIdle)) {
        try {
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
            "%s, AbortAcquisition()\n", 
            functionName);
          checkStatus(AbortAcquisition());
          mAcquiringData = 0;
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
            "%s, FreeInternalMemory()\n", 
            functionName);
          checkStatus(FreeInternalMemory());
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
            "%s, CancelWait()\n", 
            functionName);
          checkStatus(CancelWait());
        } catch (const std::string &e) {
          cout << e << endl;
          status = asynError;
        } 
      }
    }
    else if ((function == ADNumExposures) || (function == ADNumImages) ||
             (function == ADImageMode)                                 ||
             (function == ADBinX)         || (function == ADBinY)      ||
             (function == ADMinX)         || (function == ADMinY)      ||
             (function == ADSizeX)        || (function == ADSizeY)     ||
             (function == ADTriggerMode)                               || 
             (function == AndorAdcSpeed)) {
      status = setupAcquisition();
      if (status != asynSuccess) setIntegerParam(function, oldValue);
    }
    else if (function == AndorCoolerParam) {
      try {
        if (value == 0) {
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, CoolerOFF()\n", functionName);
          checkStatus(CoolerOFF());
        } else if (value == 1) {
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, CoolerON()\n", functionName);
          checkStatus(CoolerON());
        }
      } catch (const std::string &e) {
        cout << e << endl;
        status = asynError;
      }
    }
    else if (function == ADShutterControl) {
      status = setupShutter(value);
    }
    else if ((function == AndorShutterMode) ||
             (function == AndorShutterExTTL)) {
      status = setupShutter(-1);
    }
    else {
      status = ADDriver::writeInt32(pasynUser, value);
    }

    //For a successful write, clear the error message.
    setStringParam(AndorMessage, " ");

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();

    /* Send a signal to the poller task which will make it do a poll, and switch to the fast poll rate */
    epicsEventSignal(statusEvent);

    if (mAcquiringData) {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
        "%s, Sending dataEvent to dataTask ...\n", 
        functionName);
      //Also signal the data readout thread
      epicsEventSignal(dataEvent);
    }

    if (status)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s:%s: error, status=%d function=%d, value=%d\n",
              driverName, functionName, status, function, value);
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
              "%s:%s: function=%d, value=%d\n",
              driverName, functionName, function, value);
    return status;
}

asynStatus AndorCCD::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "AndorCCD::writeFloat64";

    int minTemp = 0;
    int maxTemp = 0;

    /* Set the parameter and readback in the parameter library.  */
    status = setDoubleParam(function, value);

    if (function == ADAcquireTime) {
      mAcquireTime = (float)value;  
      status = setupAcquisition();
    }
    else if (function == ADAcquirePeriod) {
      mAcquirePeriod = (float)value;  
      status = setupAcquisition();
    }
    else if (function == ADGain) {
      try {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetPreAmpGain(%d)\n", functionName, (int)value);
        checkStatus(SetPreAmpGain((int)value));
      } catch (const std::string &e) {
        cout << e << endl;
        status = asynError;
      }
    }
    else if (function == AndorAccumulatePeriod) {
      mAccumulatePeriod = (float)value;  
      status = setupAcquisition();
    }
    else if (function == ADTemperature) {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
        "%s, Setting temperature value %f\n", functionName, value);
      try {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, CCD Min Temp: %d, Max Temp %d\n", functionName, minTemp, maxTemp);
        checkStatus(GetTemperatureRange(&minTemp, &maxTemp));
        if ((static_cast<int>(value) > minTemp) & (static_cast<int>(value) < maxTemp)) {
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
            "%s, SetTemperature(%d)\n", functionName, static_cast<int>(value));
          checkStatus(SetTemperature(static_cast<int>(value)));
        } else {
          setStringParam(AndorMessage, "Temperature is out of range.");
          callParamCallbacks();
          status = asynError;
        }
      } catch (const std::string &e) {
        cout << e << endl;
        status = asynError;
      }
    }
    else if ((function == ADShutterOpenDelay) ||
             (function == ADShutterCloseDelay)) {             
      status = setupShutter(-1);
    }
      
    else {
      status = ADDriver::writeFloat64(pasynUser, value);
    }

    //For a successful write, clear the error message.
    setStringParam(AndorMessage, " ");

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    if (status)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s:%s error, status=%d function=%d, value=%f\n",
              driverName, functionName, status, function, value);
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
              "%s:%s: function=%d, value=%f\n",
              driverName, functionName, function, value);
    return status;
}


// command: 0=close, 1=open, -1=no change, only set other parameters
asynStatus AndorCCD::setupShutter(int command)
{
  double dTemp;
  int openTime, closeTime;
  int shutterExTTL;
  int shutterMode;
  asynStatus status=asynSuccess;
  static const char *functionName = "AndorCCD::setupShutter";
  
  getDoubleParam(ADShutterOpenDelay, &dTemp);
  // Convert to ms
  openTime = (int)(dTemp * 1000.);
  getDoubleParam(ADShutterCloseDelay, &dTemp);
  closeTime = (int)(dTemp * 1000.);
  getIntegerParam(AndorShutterMode, &shutterMode);
  getIntegerParam(AndorShutterExTTL, &shutterExTTL);
  
  if (command == ADShutterClosed) {
    shutterMode = AShutterClose;
    setIntegerParam(ADShutterStatus, ADShutterClosed);
  }
  else if (command == ADShutterOpen) {
    if (shutterMode == AShutterOpen) {
      setIntegerParam(ADShutterStatus, ADShutterOpen);
    }
    // No need to change shutterMode, we leave it alone and it shutter
    // will do correct thing, i.e. auto or open depending shutterMode
  }

  try {
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
      "%s, SetShutter(%d,%d,%d,%d)\n", 
      functionName, shutterExTTL, shutterMode, closeTime, openTime);
    checkStatus(SetShutter(shutterExTTL, shutterMode, closeTime, openTime)); 

  } catch (const std::string &e) {
    cout << e << endl;
    status = asynError;
  }
  return status;
}



/**
 * Function to check the return status of Andor SDK library functions.
 * @param returnStatus The return status of the SDK function
 * @return 0=success. Does not return in case of failure.
 * @throw std::string An exception is thrown in case of failure.
 */
unsigned int AndorCCD::checkStatus(unsigned int returnStatus) throw(std::string)
{
  if (returnStatus == DRV_SUCCESS) {
    return 0;
  } else if (returnStatus == DRV_NOT_INITIALIZED) {
    throw std::string("ERROR: Driver is not initialized.");
  } else if (returnStatus == DRV_ACQUIRING) {
    throw std::string("ERROR: Not allowed. Currently acquiring data.");
  } else if (returnStatus == DRV_P1INVALID) {
    throw std::string("ERROR: Parameter 1 not valid.");
  } else if (returnStatus == DRV_P2INVALID) {
    throw std::string("ERROR: Parameter 2 not valid.");
  } else if (returnStatus == DRV_P3INVALID) {
    throw std::string("ERROR: Parameter 3 not valid.");
  } else if (returnStatus == DRV_P4INVALID) {
    throw std::string("ERROR: Parameter 4 not valid.");
  } else if (returnStatus == DRV_P5INVALID) {
    throw std::string("ERROR: Parameter 5 not valid.");
  } else if (returnStatus == DRV_P6INVALID) {
    throw std::string("ERROR: Parameter 6 not valid.");
  } else if (returnStatus == DRV_P7INVALID) {
    throw std::string("ERROR: Parameter 7 not valid.");
  } else if (returnStatus == DRV_ERROR_ACK) {
    throw std::string("ERROR: Unable to communicate with card.");
  } else if (returnStatus == DRV_TEMP_OFF) {
    setStringParam(AndorTempStatusMessage, "Cooler is OFF");
    return 0;
  } else if (returnStatus == DRV_TEMP_STABILIZED) {
    setStringParam(AndorTempStatusMessage, "Stabilized at set point");
    return 0;
  } else if (returnStatus == DRV_TEMP_NOT_REACHED) {
    setStringParam(AndorTempStatusMessage, "Not reached setpoint");
    return 0;
  } else if (returnStatus == DRV_TEMP_DRIFT) {
    setStringParam(AndorTempStatusMessage, "Stabilized but drifted");
    return 0;
  } else if (returnStatus == DRV_TEMP_NOT_STABILIZED) {
    setStringParam(AndorTempStatusMessage, "Not stabilized at set point");
    return 0;
  } else if (returnStatus == DRV_VXDNOTINSTALLED) {
    throw std::string("ERROR: VxD not loaded.");
  } else if (returnStatus == DRV_INIERROR) {
    throw std::string("ERROR: Unable to load DETECTOR.INI.");
  } else if (returnStatus == DRV_COFERROR) {
    throw std::string("ERROR: Unable to load *.COF.");
  } else if (returnStatus == DRV_FLEXERROR) {
    throw std::string("ERROR: Unable to load *.RBF.");
  } else if (returnStatus == DRV_ERROR_FILELOAD) {
    throw std::string("ERROR: Unable to load *.COF or *.RBF files.");
  } else if (returnStatus == DRV_ERROR_PAGELOCK) {
    throw std::string("ERROR: Unable to acquire lock on requested memory.");
  } else if (returnStatus == DRV_USBERROR) {
    throw std::string("ERROR: Unable to detect USB device or not USB 2.0.");
  } else if (returnStatus == DRV_ERROR_NOCAMERA) {
    throw std::string("ERROR: No camera found.");
  } else if (returnStatus == DRV_GENERAL_ERRORS) {
    throw std::string("ERROR: An error occured while obtaining the number of available cameras.");
  } else if (returnStatus == DRV_INVALID_MODE) {
    throw std::string("ERROR: Invalid mode or mode not available.");
  } else if (returnStatus == DRV_ACQUISITION_ERRORS) {
    throw std::string("ERROR: Acquisition mode are invalid.");
  } else if (returnStatus == DRV_ERROR_PAGELOCK) {
    throw std::string("ERROR: Unable to allocate memory.");
  } else if (returnStatus == DRV_INVALID_FILTER) {
    throw std::string("ERROR: Filter not available for current acquisition.");
  } else if (returnStatus == DRV_IDLE) {
    throw std::string("ERROR: The system is not currently acquiring.");  
  } else if (returnStatus == DRV_NO_NEW_DATA) {
    throw std::string("ERROR: No data to read, or CancelWait() called.");  
  } else if (returnStatus == DRV_ERROR_CODES) {
    throw std::string("ERROR: Problem communicating with camera.");  
  } else {
    throw std::string("ERROR: Unknown error code returned from Andor SDK.");
  }

  return 0;
}


/**
 * Update status of detector. Meant to be run in own thread.
 */
void AndorCCD::statusTask(void)
{
  int value = 0;
  unsigned int uvalue = 0;
  unsigned int status = 0;
  double timeout = 0.0;

  unsigned int forcedFastPolls = 0;

  //const char *functionName = "AndorCCD::statusTask";

  cout << "Status thread started..." << endl;

  while(1) {

    //Read timeout for polling freq.
    this->lock();
    if (forcedFastPolls > 0) {
      timeout = mFastPollingPeriod;
      forcedFastPolls--;
    } else {
      timeout = mPollingPeriod;
    }
    this->unlock();

    if (timeout != 0.0) {
      status = epicsEventWaitWithTimeout(statusEvent, timeout);
    } else {
      status = epicsEventWait(statusEvent);
    }              
    if (status == epicsEventWaitOK) {
      //cout << "Got status event" << endl;
      //We got an event, rather than a timeout.  This is because other software
      //knows that data has arrived, or device should have changed state (parameters changed, etc.).
      //Force a minimum number of fast polls, because the device status
      //might not have changed in the first few polls
      forcedFastPolls = 5;
    }

    this->lock();
    //cout << " Status poll." << endl;

    try {
      //Only read these if we are not acquiring data
      if (!mAcquiringData) {
        //Read cooler status
        checkStatus(IsCoolerOn(&value));
        status = setIntegerParam(AndorCoolerParam, value);
        //Read temperature of CCD
        checkStatus(GetTemperature(&value));
        status = setDoubleParam(ADTemperatureActual, static_cast<double>(value));
      }

      //Read detector status (idle, acquiring, error, etc.)
      checkStatus(GetStatus(&value));
      uvalue = static_cast<unsigned int>(value);
      if (uvalue == ASIdle) {
        setIntegerParam(ADStatus, ADStatusIdle);
        setStringParam(ADStatusMessage, "IDLE. Waiting on instructions.");
      } else if (uvalue == ASTempCycle) {
        setIntegerParam(ADStatus, ADStatusWaiting);
        setStringParam(ADStatusMessage, "Executing temperature cycle.");
      } else if (uvalue == ASAcquiring) {
        setIntegerParam(ADStatus, ADStatusAcquire);
        setStringParam(ADStatusMessage, "Data acquisition in progress.");
      } else if (uvalue == ASAccumTimeNotMet) {
        setIntegerParam(ADStatus, ADStatusError);
        setStringParam(ADStatusMessage, "Unable to meet accumulate time.");
      } else if (uvalue == ASKineticTimeNotMet) {
        setIntegerParam(ADStatus, ADStatusError);
        setStringParam(ADStatusMessage, "Unable to meet kinetic cycle time.");
      } else if (uvalue == ASErrorAck) {
        setIntegerParam(ADStatus, ADStatusError);
        setStringParam(ADStatusMessage, "Unable to communicate with device.");
      } else if (uvalue == ASAcqBuffer) {
        setIntegerParam(ADStatus, ADStatusError);
        setStringParam(ADStatusMessage, "Computer unable to read data from device at required rate.");
      } else if (uvalue == ASSpoolError) {
        setIntegerParam(ADStatus, ADStatusError);
        setStringParam(ADStatusMessage, "Overflow of the spool buffer.");
      }
    } catch (const std::string &e) {
      cout << e << endl;
      setStringParam(AndorMessage, e.c_str());
    }

    /* Call the callbacks to update any changes */
    callParamCallbacks();
    this->unlock();
        
  } //End of loop

}

asynStatus AndorCCD::setupAcquisition()
{
  int numExposures;
  int numImages;
  int imageMode;
  int adcChannel;
  int triggerMode;
  int binX, binY, minX, minY, sizeX, sizeY, maxSizeX, maxSizeY;
  float acquireTimeAct, acquirePeriodAct, accumulatePeriodAct;
  int FKmode = 4;
  static const char *functionName = "AndorCCD::setupAcquisition";
  
  getIntegerParam(ADImageMode, &imageMode);
  getIntegerParam(ADNumExposures, &numExposures);
  if (numExposures <= 0) {
    numExposures = 1;
    setIntegerParam(ADNumExposures, numExposures);
  }
  getIntegerParam(ADNumImages, &numImages);
  if (numImages <= 0) {
    numImages = 1;
    setIntegerParam(ADNumImages, numImages);
  }
  getIntegerParam(ADBinX, &binX);
  if (binX <= 0) {
    binX = 1;
    setIntegerParam(ADBinX, binX);
  }
  getIntegerParam(ADBinY, &binY);
  if (binY <= 0) {
    binY = 1;
    setIntegerParam(ADBinY, binY);
  }
  getIntegerParam(ADMinX, &minX);
  getIntegerParam(ADMinY, &minY);
  getIntegerParam(ADSizeX, &sizeX);
  getIntegerParam(ADSizeY, &sizeY);
  getIntegerParam(ADMaxSizeX, &maxSizeX);
  getIntegerParam(ADMaxSizeY, &maxSizeY);
  if (minX > (maxSizeX - 2*binX)) {
    minX = maxSizeX - 2*binX;
    setIntegerParam(ADMinX, minX);
  }
  if (minY > (maxSizeY - 2*binY)) {
    minY = maxSizeY - 2*binY;
    setIntegerParam(ADMinY, minY);
  }
  if ((minX + sizeX) > maxSizeX) {
    sizeX = maxSizeX - minX;
    setIntegerParam(ADSizeX, sizeX);
  }
  if ((minY + sizeY) > maxSizeY) {
    sizeY = maxSizeY - minY;
    setIntegerParam(ADSizeY, sizeY);
  }
  
  getIntegerParam(ADTriggerMode, &triggerMode);
  getIntegerParam(AndorAdcSpeed, &adcChannel);
  
  // Unfortunately there does not seem to be a way to query the Andor SDK 
  // for the actual size of the image, so we must compute it.
  setIntegerParam(NDArraySizeX, sizeX/binX);
  setIntegerParam(NDArraySizeY, sizeY/binY);
  
  try {
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
      "%s, SetTriggerMode(%d)\n", functionName, triggerMode);
    checkStatus(SetTriggerMode(ATInternal));
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
      "%s, SetADChannel(%d)\n", functionName, adcChannel);
    checkStatus(SetADChannel(adcChannel));
    //Set fastest HS speed.
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
      "%s, SetHSSpeed(0, 0)\n", functionName);
    checkStatus(SetHSSpeed(0, 0));
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
      "%s, SetImage(%d,%d,%d,%d,%d,%d)\n", 
      functionName, binX, binY, minX+1, minX+sizeX, minY+1, minY+sizeY);
    checkStatus(SetImage(binX, binY, minX+1, minX+sizeX, minY+1, minY+sizeY));

    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
      "%s, SetExposureTime(%f)\n", functionName, mAcquireTime);
    checkStatus(SetExposureTime(mAcquireTime));
    
    switch (imageMode) {
      case ADImageSingle:
        if (numExposures == 1) {
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
            "%s, SetAcquisitionMode(AASingle)\n", functionName);
          checkStatus(SetAcquisitionMode(AASingle));
        } else {
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
            "%s, SetAcquisitionMode(AAAccumulate)\n", functionName);
          checkStatus(SetAcquisitionMode(AAAccumulate));
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
            "%s, SetNumberAccumulations(%d)\n", 
            functionName, numExposures);
          checkStatus(SetNumberAccumulations(numExposures));
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
            "%s, SetAccumulationCycleTime(%f)\n", functionName, mAccumulatePeriod);
          checkStatus(SetAccumulationCycleTime(mAccumulatePeriod));
        }
        break;

      case ADImageMultiple:
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetAcquisitionMode(AAKinetics)\n", functionName);
        checkStatus(SetAcquisitionMode(AAKinetics));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetNumberAccumulations(%d)\n", 
          functionName, numExposures);
        checkStatus(SetNumberAccumulations(numExposures));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetAccumulationCycleTime(%f)\n", functionName, mAccumulatePeriod);
        checkStatus(SetAccumulationCycleTime(mAccumulatePeriod));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetNumberKinetics(%d)\n", functionName, numImages);
        checkStatus(SetNumberKinetics(numImages));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetKineticCycleTime(%f)\n", functionName, mAcquirePeriod);
        checkStatus(SetKineticCycleTime(mAcquirePeriod));
        break;

      case ADImageContinuous:
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetAcquisitionMode(AARunTillAbort)\n", functionName);
        checkStatus(SetAcquisitionMode(AARunTillAbort));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetKineticCycleTime(%f)\n", functionName, mAcquirePeriod);
        checkStatus(SetKineticCycleTime(mAcquirePeriod));
        break;

      case AImageFastKinetics:
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetAcquisitionMode(AAFastKinetics)\n", functionName);
        checkStatus(SetAcquisitionMode(AAFastKinetics));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetImage(%d,%d,%d,%d,%d,%d)\n", 
          functionName, binX, binY, 1, maxSizeX, 1, maxSizeY);
        checkStatus(SetImage(binX, binY, 1, maxSizeX, 1, maxSizeY));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, SetFastKineticsEx(%d,%d,%f,%d,%d,%d,%d)\n", 
          functionName, sizeY, numImages, mAcquireTime, FKmode, binX, binY, minY);
        checkStatus(SetFastKineticsEx(sizeY, numImages, mAcquireTime, FKmode, binX, binY, minY));
        setIntegerParam(NDArraySizeX, maxSizeX/binX);
        setIntegerParam(NDArraySizeY, maxSizeY/binY);
        break;
    }
    // Read the actual times
    if (imageMode == AImageFastKinetics) {
      checkStatus(GetFKExposureTime(&acquireTimeAct));
    } else {
      checkStatus(GetAcquisitionTimings(&acquireTimeAct, &accumulatePeriodAct, &acquirePeriodAct));
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
        "%s, GetAcquisitionTimings(exposure=%f, accumulate=%f, kinetic=%f\n",
        functionName, acquireTimeAct, accumulatePeriodAct, acquirePeriodAct);
    }
    setDoubleParam(ADAcquireTime, acquireTimeAct);
    setDoubleParam(ADAcquirePeriod, acquirePeriodAct);
    setDoubleParam(AndorAccumulatePeriod, accumulatePeriodAct);

    
  } catch (const std::string &e) {
    cout << e << endl;
    return asynError;
  }
  return asynSuccess;
}


/**
 * Do data readout from the detector. Meant to be run in own thread.
 */
void AndorCCD::dataTask(void)
{
  epicsUInt32 status = 0;
  int acquireStatus;
  char *errorString = NULL;
  int acquiring = 0;
  epicsInt32 numImagesCounter;
  epicsInt32 numExposuresCounter;
  epicsInt32 imageCounter;
  epicsInt32 arrayCallbacks;
  epicsInt32 sizeX, sizeY;
  NDDataType_t dataType;
  long firstImage, lastImage;
  int dims[2];
  int nDims = 2;
  epicsTimeStamp startTime;
  NDArray *pArray;
  int autoSave;
  
  //long *dP = NULL;

  const char *functionName = "AndorCCD::dataTask";

  cout << "Data thread started..." << endl;

  while(1) {
    
    errorString = NULL;

    //Wait for event from main thread to signal that data acquisition has started.
    this->unlock();
    status = epicsEventWait(dataEvent);
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
      "%s, got data event\n", functionName);
    this->lock();

    //Sanity check that main thread thinks we are acquiring data
    if (mAcquiringData) {
      try {
        status = setupAcquisition();
        if (status != asynSuccess) continue;
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, StartAcquisition()\n", functionName);
        checkStatus(StartAcquisition());
        acquiring = 1;
      } catch (const std::string &e) {
        cout << e << endl;
        continue;
      }
      //Read some parameters
      getIntegerParam(NDDataType, (epicsInt32*)&dataType);
      getIntegerParam(NDAutoSave, &autoSave);
      getIntegerParam(NDArrayCallbacks, &arrayCallbacks);
      getIntegerParam(NDArraySizeX, &sizeX);
      getIntegerParam(NDArraySizeY, &sizeY);
      // Reset the counters
      setIntegerParam(ADNumImagesCounter, 0);
      setIntegerParam(ADNumExposuresCounter, 0);
      callParamCallbacks();
    } else {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, 
        "%s, Data thread is running but main thread thinks we are not acquiring.\n", functionName);
      acquiring = 0;
    }

    while (acquiring) {
      try {
        checkStatus(GetStatus(&acquireStatus));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, GetStatus returned %d\n", functionName, acquireStatus);
        if (acquireStatus != DRV_ACQUIRING) break;
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, WaitForAcquisition().\n", functionName);
        this->unlock();
        checkStatus(WaitForAcquisition());
        this->lock();
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
          "%s, WaitForAcquisition has returned.\n", functionName);
        getIntegerParam(ADNumExposuresCounter, &numExposuresCounter);
        numExposuresCounter++;
        setIntegerParam(ADNumExposuresCounter, numExposuresCounter);
        callParamCallbacks();
        // Is there an image available?
        status = GetNumberNewImages(&firstImage, &lastImage);
        if (status == DRV_SUCCESS) {
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
            "%s, firstImage=%d, lastImage=%d\n", functionName, firstImage, lastImage);
          // Update counters
          getIntegerParam(NDArrayCounter, &imageCounter);
          imageCounter++;
          setIntegerParam(NDArrayCounter, imageCounter);;
          getIntegerParam(ADNumImagesCounter, &numImagesCounter);
          numImagesCounter++;
          setIntegerParam(ADNumImagesCounter, numImagesCounter);
          // If array callbacks are enabled then read data into NDArray, do callbacks
          if (arrayCallbacks) {
            epicsTimeGetCurrent(&startTime);
            // Allocate an NDArray
            dims[0] = sizeX;
            dims[1] = sizeY;
            pArray = this->pNDArrayPool->alloc(nDims, dims, dataType, 0, NULL);
            // Read the oldest array
            // Is there still an image available?
            status = GetNumberNewImages(&firstImage, &lastImage);
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
              "%s, GetNumberNewImages, status=%d, firstImage=%d, lastImage=%d\n", 
              functionName, status, firstImage, lastImage);
            if (dataType == NDUInt32) {
              asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
                "%s, GetOldestImage(%p, %d)\n", functionName, pArray->pData, sizeX*sizeY);
              checkStatus(GetOldestImage((at_32*)pArray->pData, sizeX*sizeY));
              setIntegerParam(NDArraySize, sizeX * sizeY * sizeof(epicsUInt32));
            }
            else if (dataType == NDUInt16) {
              asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
                "%s, GetOldestImage16(%p, %d)\n", functionName, pArray->pData, sizeX*sizeY);
              checkStatus(GetOldestImage16((epicsUInt16*)pArray->pData, sizeX*sizeY));
              setIntegerParam(NDArraySize, sizeX * sizeY * sizeof(epicsUInt16));
            }
            /* Put the frame number and time stamp into the buffer */
            pArray->uniqueId = imageCounter;
            pArray->timeStamp = startTime.secPastEpoch + startTime.nsec / 1.e9;
            /* Get any attributes that have been defined for this driver */        
            this->getAttributes(pArray->pAttributeList);
            /* Call the NDArray callback */
            /* Must release the lock here, or we can get into a deadlock, because we can
             * block on the plugin lock, and the plugin can be calling us */
            this->unlock();
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
                 "%s:, calling array callbacks\n", functionName);
            doCallbacksGenericPointer(pArray, NDArrayData, 0);
            this->lock();
            pArray->release();
          }
          // Save data if autosave is enabled
          if (autoSave) this->saveDataFrame();
          callParamCallbacks();
        }
      } catch (const std::string &e) {
        cout << e << endl;
        errorString = const_cast<char *>(e.c_str());
        setStringParam(AndorMessage, errorString);
      }
    }
      
    //Now clear main thread flag
    mAcquiringData = 0;
    setIntegerParam(ADAcquire, 0);
    //setIntegerParam(ADStatus, 0); //Dont set this as the status thread sets it.

    /* Call the callbacks to update any changes */
    callParamCallbacks();
  } //End of loop

}


/**
 * Save a data frame using the Andor SDK file writing functions.
 * Also has the option to save data as plain text.
 */
void AndorCCD::saveDataFrame() 
{
  char *errorString = NULL;
  int fileFormat;
  char fullFileName[MAX_FILENAME_LEN];
  char palFilePath[MAX_FILENAME_LEN];

  const char *functionName = "AndorCCD::saveDataFrame";

  // Fetch the file format
  getIntegerParam(NDFileFormat, &fileFormat);
      
  this->createFileName(255, fullFileName);
  setStringParam(NDFullFileName, fullFileName);
  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, file name is %s.\n", functionName, fullFileName);
  getStringParam(AndorPalFileName, 255, palFilePath);

  try {
    if (fileFormat == AFFTIFF) {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
        "%s, SaveAsTiffEx(%s, %s, 1, 1, 1)\n", functionName, fullFileName, palFilePath);
      checkStatus(SaveAsTiffEx(fullFileName, palFilePath, 1, 1, 1));
    } else if (fileFormat == AFFBMP) {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
        "%s, SaveAsBmp(%s, %s, 0, 0)\n", functionName, fullFileName, palFilePath);
      checkStatus(SaveAsBmp(fullFileName, palFilePath, 0, 0));
    } else if (fileFormat == AFFSIF) {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
        "%s, SaveAsSif(%s)\n", functionName, fullFileName);
      checkStatus(SaveAsSif(fullFileName));
    } else if (fileFormat == AFFEDF) {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
        "%s, SaveAsEDF(%s, 0)\n", functionName, fullFileName);
      checkStatus(SaveAsEDF(fullFileName, 0));
    } else if (fileFormat == AFFRAW) {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
        "%s, SaveAsRaw(%s, 1)\n", functionName, fullFileName);
      checkStatus(SaveAsRaw(fullFileName, 1));
    }
  } catch (const std::string &e) {
    cout << e << endl;
    errorString = const_cast<char *>(e.c_str());
  }

  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, Saving data.. Done!\n", functionName);

  if (errorString != NULL) {
    setStringParam(AndorMessage, errorString);
    setIntegerParam(ADStatus, ADStatusError);
  }

  this->unlock();
  
}


//C utility functions to tie in with EPICS

static void andorStatusTaskC(void *drvPvt)
{
  AndorCCD *pPvt = (AndorCCD *)drvPvt;

  pPvt->statusTask();
}


static void andorDataTaskC(void *drvPvt)
{
  AndorCCD *pPvt = (AndorCCD *)drvPvt;

  pPvt->dataTask();
}

/**
 * Config function for IOC shell.
 *
 * @param portName The ASYN port.
 * @param maxBuffers The maximum number of data frame buffers for the ADDriver class.
 * @param maxMemory The maximum memory size allowed in the ADDriver class.
 * @param maxSizeX The maximum X dimension of the detector.
 * @param maxSizeY The maximum Y dimension of the detector.
 */
extern "C" {
int andorCCDConfig(const char *portName, int maxBuffers, size_t maxMemory, 
                   const char *installPath, int priority, int stackSize)
{
  /*Instantiate class.*/
  new AndorCCD(portName, maxBuffers, maxMemory, installPath, priority, stackSize);
  return(asynSuccess);
}


/* Code for iocsh registration */

/* andorCCDConfig */
static const iocshArg andorCCDConfigArg0 = {"Port name", iocshArgString};
static const iocshArg andorCCDConfigArg1 = {"maxBuffers", iocshArgInt};
static const iocshArg andorCCDConfigArg2 = {"maxMemory", iocshArgInt};
static const iocshArg andorCCDConfigArg3 = {"installPath", iocshArgString};
static const iocshArg andorCCDConfigArg4 = {"priority", iocshArgInt};
static const iocshArg andorCCDConfigArg5 = {"stackSize", iocshArgInt};
static const iocshArg * const andorCCDConfigArgs[] =  {&andorCCDConfigArg0,
                                                       &andorCCDConfigArg1,
                                                       &andorCCDConfigArg2,
                                                       &andorCCDConfigArg3,
                                                       &andorCCDConfigArg4,
                                                       &andorCCDConfigArg5};

static const iocshFuncDef configAndorCCD = {"andorCCDConfig", 6, andorCCDConfigArgs};
static void configAndorCCDCallFunc(const iocshArgBuf *args)
{
    andorCCDConfig(args[0].sval, args[1].ival, args[2].ival, args[3].sval, 
                   args[4].ival, args[5].ival);
}

static void andorCCDRegister(void)
{

    iocshRegister(&configAndorCCD, configAndorCCDCallFunc);
}

epicsExportRegistrar(andorCCDRegister);
}
