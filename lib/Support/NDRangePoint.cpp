#include "thrud/Support/NDRangePoint.h"

#include "thrud/Support/NDRange.h"

NDRangePoint::NDRangePoint(unsigned int localX, unsigned int localY,
                           unsigned int localZ, unsigned int groupX,
                           unsigned int groupY, unsigned int groupZ,
                           unsigned int localSizeX, unsigned int localSizeY,
                           unsigned int localSizeZ, unsigned int groupNumberX,
                           unsigned int groupNumberY,
                           unsigned int groupNumberZ) {

  unsigned int tmpLocal[] = { localX, localY, localZ };
  unsigned int tmpGroup[] = { groupX, groupY, groupZ };
  unsigned int tmpLocalSize[] = { localSizeX, localSizeY, localSizeZ };
  unsigned int tmpGroupNumber[] = { groupNumberX, groupNumberY, groupNumberZ };

  local.assign(tmpLocal, tmpLocal + NDRange::DIRECTION_NUMBER);
  group.assign(tmpGroup, tmpGroup + NDRange::DIRECTION_NUMBER);
  localSize.assign(tmpLocalSize, tmpLocalSize + NDRange::DIRECTION_NUMBER);
  groupNumber.assign(tmpGroupNumber,
                     tmpGroupNumber + NDRange::DIRECTION_NUMBER);
}

unsigned int NDRangePoint::getLocalX() const { return local[0]; }
unsigned int NDRangePoint::getLocalY() const { return local[1]; }
unsigned int NDRangePoint::getLocalZ() const { return local[2]; }
unsigned int NDRangePoint::getGroupX() const { return group[0]; }
unsigned int NDRangePoint::getGroupY() const { return group[1]; }
unsigned int NDRangePoint::getGroupZ() const { return group[2]; }
unsigned int NDRangePoint::getLocalSizeX() const { return localSize[0]; }
unsigned int NDRangePoint::getLocalSizeY() const { return localSize[1]; }
unsigned int NDRangePoint::getLocalSizeZ() const { return localSize[2]; }
unsigned int NDRangePoint::getGroupNumberX() const { return groupNumber[0]; }
unsigned int NDRangePoint::getGroupNumberY() const { return groupNumber[1]; }
unsigned int NDRangePoint::getGroupNumberZ() const { return groupNumber[2]; }

unsigned int NDRangePoint::getLocal(unsigned int direction) const {
  return local[direction];
}

unsigned int NDRangePoint::getGlobal(unsigned int direction) const {
  return local[direction] + group[direction] * localSize[direction];
}

unsigned int NDRangePoint::getGroup(unsigned int direction) const {
  return group[direction];
}

unsigned int NDRangePoint::getLocalSize(unsigned int direction) const {
  return localSize[direction];
}

unsigned int NDRangePoint::getGroupNumber(unsigned int direction) const {
  return groupNumber[direction];
}

unsigned int NDRangePoint::getGlobalSize(unsigned int direction) const {
  return localSize[direction] * groupNumber[direction];
}

unsigned int NDRangePoint::getCoordinate(const std::string &name,
                                         unsigned int direction) const {
  if (name == NDRange::GET_LOCAL_ID)
    return getLocal(direction);
  if (name == NDRange::GET_GROUP_ID)
    return getGroup(direction);
  if (name == NDRange::GET_LOCAL_SIZE)
    return getLocalSize(direction);
  if (name == NDRange::GET_GLOBAL_SIZE)
    return getLocalSize(direction);
  if (name == NDRange::GET_GLOBAL_ID)
    return getGlobal(direction); 

  return 0;
}
