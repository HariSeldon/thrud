#include "thrud/Support/NDRangePoint.h"

#include "thrud/Support/NDRange.h";

NDRangePoint::NDRangePoint(unsigned int localX, unsigned int localY,
                           unsigned int localZ, unsigned int groupX,
                           unsigned int groupY, unsigned int groupZ) {
  unsigned int tmpLocal[] = {localX, localY, localZ};
  unsigned int tmpGroup[] = {groupX, groupY, groupZ};
  
  local.assign(tmpLocal, tmpLocal + NDRange::DIRECTION_NUMBER);
  group.assign(tmpGroup, tmpGroup + NDRange::DIRECTION_NUMBER);
}

unsigned int NDRangePoint::getLocalX() const { return local[0]; }
unsigned int NDRangePoint::getLocalY() const { return local[1]; }
unsigned int NDRangePoint::getLocalZ() const { return local[2]; }
unsigned int NDRangePoint::getGroupX() const { return group[0]; }
unsigned int NDRangePoint::getGroupY() const { return group[1]; }
unsigned int NDRangePoint::getGroupZ() const { return group[2]; }

unsigned int NDRangePoint::getLocal(unsigned int direction) const {
  return local[direction];
}

unsigned int NDRangePoint::getGroup(unsigned int direction) const {
  return group[direction];
}

unsigned int NDRangePoint::getCoordinate(const std::string &name, unsigned int direction) const {
  if(name == NDRange::GET_LOCAL_ID) 
    return local[direction];
  if(name == NDRange::GET_GROUP_ID)
    return group[direction];
  return 0;
}
