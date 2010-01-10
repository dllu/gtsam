/*
 * LieConfig.cpp
 *
 *  Created on: Jan 8, 2010
 *      Author: richard
 */

#include "LieConfig.h"

#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>
#include <utility>
#include <iostream>
#include <stdexcept>

#include "VectorConfig.h"

using namespace std;

#define FOREACH_PAIR( KEY, VAL, COL) BOOST_FOREACH (boost::tie(KEY,VAL),COL)

namespace gtsam {

  template<class T>
  void LieConfig<T>::print(const string &s) const {
       cout << "LieConfig " << s << ", size " << values_.size() << "\n";
       pair<string, T> v;
       BOOST_FOREACH(v, values_)
         gtsam::print(v.second, v.first + ": ");
     }

  template<class T>
  bool LieConfig<T>::equals(const LieConfig<T>& expected, double tol) const {
    if (values_.size() != expected.values_.size()) return false;
    pair<string, T> v;
    BOOST_FOREACH(v, values_) {
      boost::optional<const T&> expval = expected.gettry(v.first);
      if(!expval || !gtsam::equal(v.second, *expval, tol))
        return false;
    }
    return true;
  }

  template<class T>
  const T& LieConfig<T>::get(const std::string& key) const {
    iterator it = values_.find(key);
    if (it == values_.end()) throw std::invalid_argument("invalid key");
    else return it->second;
  }

  template<class T>
  boost::optional<const T&> LieConfig<T>::gettry(const std::string& key) const {
    const_iterator it = values_.find(key);
    if (it == values_.end()) return boost::optional<const T&>();
    else return it->second;
  }

  template<class T>
  void LieConfig<T>::insert(const std::string& name, const T& val) {
    values_.insert(make_pair(name, val));
    dim_ += dim(val);
  }

  template<class T>
  LieConfig<T> expmap(const LieConfig<T>& c, const VectorConfig& delta) {
		LieConfig<T> newConfig;
		string j; T pj;
		FOREACH_PAIR(j, pj, c) {
			if (delta.contains(j)) {
				const Vector& dj = delta[j];
				newConfig.insert(j, expmap(pj,dj));
			} else
			newConfig.insert(j, pj);
		}
		return newConfig;
	}

  // This version just creates a VectorConfig then calls function above
  template<class T>
  LieConfig<T> expmap(const LieConfig<T>& c, const Vector& delta) {
    VectorConfig deltaConfig;
        int delta_offset = 0;
        string j; T pj;
        FOREACH_PAIR(j, pj, c) {
            int cur_dim = dim(pj);
            Vector dj = sub(delta, delta_offset, delta_offset+cur_dim);
            deltaConfig.insert(j,dj);
            delta_offset += cur_dim;
        }
        return expmap(c,deltaConfig);
  }

}
