#include "thrud/Support/MathUtils.h"

#include <algorithm>
#include <functional>
#include <numeric>

#include <iostream>
#include <iterator>


// "Private" support function.
float square(float input) {
  return input * input;
}

//------------------------------------------------------------------------------
template <typename integerType>
float getAverage(const std::vector<integerType> &elements) {
  if(elements.size() == 0)
    return 0;

  integerType sum = std::accumulate(elements.begin(), elements.end(), 0);
  float average = sum / elements.size();

  return average;
}

template float getAverage(const std::vector<unsigned int> &elements);

//------------------------------------------------------------------------------
template <typename integerType>
float getVariance(const std::vector<integerType> &elements) {
  if(elements.size() == 0)
    return 0;

  float average = getAverage(elements);
  std::vector<float> averages(elements.size(), average);
  std::vector<float> differences; 
  differences.reserve(elements.size());

  std::transform(elements.begin(), elements.end(),
                 averages.begin(), differences.begin(),
                 std::minus<float>());

  std::transform(differences.begin(), differences.end(), 
                 differences.begin(), square);   

  float variance = getAverage(differences); 

  return variance;
}

template float getVariance(const std::vector<unsigned int> &elements);

////------------------------------------------------------------------------------
//template <class dataType>                                                     
//std::vector<dataType> getTopologialOrder(
//    std::map<dataType, std::vector<dataType> > &graph) {
//  std::vector<dataType> result;
//  std::vector<dataType> roots = getRoots(graph);
//  std::stack<dataType, std::vector<dataType> > workingSet;
//
//  while(!workingSet.empty()) {
//    dataType node = workingSet.top();
//    workingSet.pop();
//
//    result.push_back(node); 
//    std::vector<dataType> &adjs = graph[node];
//    std::vector<dataType> copy(adjs.size());
//    std::copy(adjs.begin(), adjs.end(), copy.begin());
//
//    for (typename std::vector<dataType>::iterator iter = copy.begin(), 
//      end = copy.end(); iter != end; ++iter) {
//      dataType node2 = *iter;
//      // Remove  
//
//    }
//  }
//
//  return result;
//}
//
//template InstVector getTopologialOrder(InstGraph &graph);
//
////------------------------------------------------------------------------------
//template <class dataType>
//std::vector<dataType> getRoots(
//  std::map<dataType, std::vector<dataType> > &graph) {
//
//}
