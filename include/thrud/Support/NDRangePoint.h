#include <string>
#include <vector>

class NDRangePoint {
public:
  NDRangePoint(unsigned int localX, unsigned int localY, unsigned int localZ,
               unsigned int groupX, unsigned int groupY, unsigned int groupZ,
               unsigned int localSizeX, unsigned int localSizeY,
               unsigned int localSizeZ, unsigned int groupNumberX,
               unsigned int groupNumberY, unsigned int groupNumberZ);
  unsigned int getLocalX() const;
  unsigned int getLocalY() const;
  unsigned int getLocalZ() const;
  unsigned int getGroupX() const;
  unsigned int getGroupY() const;
  unsigned int getGroupZ() const;
  unsigned int getLocalSizeX() const;
  unsigned int getLocalSizeY() const;
  unsigned int getLocalSizeZ() const;
  unsigned int getGroupNumberX() const;
  unsigned int getGroupNumberY() const;
  unsigned int getGroupNumberZ() const;
  unsigned int getLocal(unsigned int direction) const;
  unsigned int getGlobal(unsigned int direction) const;
  unsigned int getGroup(unsigned int direction) const;
  unsigned int getLocalSize(unsigned int direction) const;
  unsigned int getGlobalSize(unsigned int direction) const;
  unsigned int getGroupNumber(unsigned int direction) const;
  unsigned int getCoordinate(const std::string &name,
                             unsigned int direction) const;

private:
  std::vector<unsigned int> local;
  std::vector<unsigned int> group;
  std::vector<unsigned int> localSize;
  std::vector<unsigned int> groupNumber;
};
