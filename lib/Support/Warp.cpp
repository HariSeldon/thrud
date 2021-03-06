#include "thrud/Support/Warp.h"

#include "thrud/Support/OCLEnv.h"

Warp::Warp(int groupX, int groupY, int groupZ, int warpIndex,
           const NDRangeSpace &ndrSpace) {

  points.reserve(OCLEnv::WARP_SIZE);

//  int numberOfGroupsX = ndrSpace.getNumberOfGroupsX();
//  int numberOfGroupsY = ndrSpace.getNumberOfGroupsY();
  int localSizeX = ndrSpace.getLocalSizeX();
  int localSizeY = ndrSpace.getLocalSizeY();
  int localSizeZ = ndrSpace.getLocalSizeZ();
  int localArea = localSizeX * localSizeY;
//  int localVolume = localArea * localSizeZ;

  // Compute global_id of first thread in the work group.
//  int firstThreadInGroup =
//      groupZ * numberOfGroupsX * numberOfGroupsY * localVolume +
//      groupY * numberOfGroupsX * localArea + groupX * localSizeX;

  int firstThreadLocalPosition = warpIndex * OCLEnv::WARP_SIZE;

  for (int index = 0; index < OCLEnv::WARP_SIZE; ++index) {
    int threadPosition = firstThreadLocalPosition + index;

    // Compute local coordinates of the first warp in the
    int localZ = threadPosition / localArea;
    int tmpPosition = threadPosition % localArea;
    int localY = tmpPosition / localSizeX;
    int localX = tmpPosition % localSizeX;

//    int firstLocalThreadInWarp =
//        localZ * localArea + localY * localSizeX + localX;
//    int globalfirstGlobalThreadInWarp = firstThreadInGroup + firstLocalThreadInWarp;

    NDRangePoint point(localX, localY, localZ, groupX, groupY, groupZ, ndrSpace);
    points.push_back(point);
  }
}

Warp::iterator Warp::begin() {
  return Warp::iterator(this);
}

Warp::iterator Warp::end() {
  return Warp::iterator::end();
}

//-----------------------------------------------------------------------------
Warp::iterator::iterator() { currentPoint = 0; }
Warp::iterator::iterator(const Warp* warp) {
  points = warp->points;
  currentPoint = (points.size() == 0) ? -1 : 0;
}
Warp::iterator::iterator(const iterator& original) {
  this->points = original.points;
  this->currentPoint = original.currentPoint;
}

// Pre-increment.
Warp::iterator& Warp::iterator::operator++() {
  toNext();
  return *this;
}
// Post-increment.
Warp::iterator Warp::iterator::operator++(int) {
  iterator old(*this);
  ++*this;
  return old;
}

NDRangePoint Warp::iterator::operator*() const {
  return points.at(currentPoint);
}
bool Warp::iterator::operator!=(const iterator& iter) const {
  return iter.currentPoint != this->currentPoint;
}

void Warp::iterator::toNext() {
  ++currentPoint;
  if (currentPoint == points.size()) currentPoint = -1;
}

Warp::iterator Warp::iterator::end() {
  iterator endIterator;
  endIterator.currentPoint = -1;
  return endIterator;
}
