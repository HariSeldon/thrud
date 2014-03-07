#include <string>
#include <vector>

class NDRangePoint {
public:
  NDRangePoint(unsigned int localX, unsigned int localY, unsigned int localZ,
               unsigned int groupX, unsigned int groupY, unsigned int groupZ);
  unsigned int getLocalX() const;
  unsigned int getLocalY() const;
  unsigned int getLocalZ() const;
  unsigned int getGroupX() const;
  unsigned int getGroupY() const;
  unsigned int getGroupZ() const;
  unsigned int getLocal(unsigned int direction) const;
  unsigned int getGroup(unsigned int direction) const;
  unsigned int getCoordinate(const std::string &name, unsigned int direction) const;

private:
  std::vector<unsigned int> local;
  std::vector<unsigned int> group;
};
