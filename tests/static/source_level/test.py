#! /usr/bin/python

import os;
import subprocess;
import time;

KERNELS_DIRECTORY = "kernels";
CL_EXTENSION = ".cl";
CF = "4";
CD = "0";
ST = "1";
DIV_REGION = "classic";
WD = os.getcwd();
KERNELS_PATH = os.path.join(WD, KERNELS_DIRECTORY);
#COMPILER = os.path.join(WD, "apply_coarsening.sh");
COMPILER = "./apply_coarsening.sh";

#-------------------------------------------------------------------------------
def runCommand(arguments):
  WAITING_TIME = 60;
  runProcess = subprocess.Popen(arguments, stdout=subprocess.PIPE,
                                           stderr=subprocess.PIPE);
  runPid = runProcess.pid;
  counter = 0;
  runReturnCode = None;

  # Manage the case in which the run hangs.
  while(counter < WAITING_TIME and runReturnCode == None):
    counter += 1;
    time.sleep(1);
    runReturnCode = runProcess.poll();

  if runReturnCode != 0:
    print "\033[1;31m%s\033[1;m" % "Failure!";
  else:
    print "\033[1;32m%s\033[1;m" % "Ok!"
    

#  if runReturnCode != 0:
#    if(runReturnCode == None):
#      runProcess.kill();
#      raise TimeOutException("Fail: time expired");
#    else:
#      commandOutput = runProcess.communicate();
#      print("Program return code: " + str(runReturnCode));
#      raise RunFailureException("Fail: %d" % runReturnCode, runReturnCode, \
#                                (commandOutput[0], commandOutput[1]));

  commandOutput = runProcess.communicate();
  return (commandOutput[0], commandOutput[1]);

# ------------------------------------------------------------------------------
def compileKernel(fileName, kernelName):
  command = [COMPILER, fileName, kernelName, CD, CF, ST, DIV_REGION];
  print(" ".join(command));
  output = runCommand(command); 

# ------------------------------------------------------------------------------
def main():
  for fileName in os.listdir(KERNELS_PATH):
    kernelFile = os.path.join(KERNELS_PATH, fileName);
    if os.path.isfile(kernelFile):
      fileName, fileExtension = os.path.splitext(kernelFile);
      if(fileExtension == CL_EXTENSION):
        components = fileName.split(".");
        kernelName = components[len(components) - 2];
        print "Compiling: " + kernelName;
        compileKernel(kernelFile, kernelName); 
      
# ------------------------------------------------------------------------------
main();
