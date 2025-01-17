/*
 * Copyright 2018 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FIREBASE_APP_CLIENT_UNITY_SRC_CPP_INSTANCE_MANAGER_H_
#define FIREBASE_APP_CLIENT_UNITY_SRC_CPP_INSTANCE_MANAGER_H_

#include <functional>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>

#include "app/src/assert.h"
#include "app/src/log.h"
#include "app/src/include/firebase/internal/mutex.h"

namespace firebase {

/// @brief A class to manage reference counts of C++ instances by a search key.
///
/// This class manages the reference count of C++ instances. When the reference
/// count of the managed instance drops to 0, the manager deletes the instance.
///
/// This is to solve a race condition introduced by SWIG Proxy and GetInstance()
/// pattern, ex.
///   Database::GetInstance(url)
///
/// This kind of C++ static library usually keeps one or a collection of global
/// pointers to already-created instance for future reference.  And the
/// reference is removed/reset once the instance is deleted.  However, a race
/// condition can be introduced with the following sequence.
/// * C# GetInstance() is called.  (This function is auto-generated by SWIG)
///   - Call C++ GetInstance()/CreateInstance() and it creates C++ object A
///   - Create C# proxy PA1 which references to A.
/// * C# proxy PA1 is dereferenced and GC marks it as unreachable, but finalizer
///   is not started yet.
/// * C# GetInstance()/CreateInstance() is called again.
///   - Call C++ GetInstance() and it returns existing C++ object A.
///   - Create C# Proxy PA2 which also references to A.
/// * GC thread starts to finalize C# proxy PA1 and the C++ object A is deleted.
/// * The program tries to do something with C# proxy PA2 and crashes due to bad
///   memory access to deleted C++ object A.
///
/// This class makes sure the C++ instance is deleted only if no C# proxy is
/// referencing to it.  The C# proxy is responsible to dereference when it is
/// disposed.
///
/// @tparam InstanceClass The class for C++ instance.
template <typename InstanceClass>
class CppInstanceManager {
 public:
  CppInstanceManager() {}

  virtual ~CppInstanceManager() {
    // Make sure mutex is locked before destroying all instance.
    // TODO(b/121161177): Need to find the root cause of the double deletion.
    //                    Disable the clean up here for now.
    // MutexLock lock(manager_mutex_);
    // for (auto& it : container_) {
    //   LogWarning(
    //       "Reference to %p is not released (count: %d) when "
    //       "CppInstanceManager<%s> is deleted.  Deleting it anyway.",
    //       it.first, it.second, typeid(InstanceClass).name());
    //   delete it.first;
    // }
    // container_.empty();
  }

  /// @brief CppInstanceManager is neither copyable nor movable.
  CppInstanceManager(const CppInstanceManager&) = delete;
  CppInstanceManager& operator=(const CppInstanceManager&) = delete;
  CppInstanceManager(CppInstanceManager&&) = delete;
  CppInstanceManager& operator=(CppInstanceManager&&) = delete;

  /// @brief Increase the reference count by 1.
  /// @return Reference count after increment.
  int AddReference(InstanceClass* instance) {
    FIREBASE_DEV_ASSERT_MESSAGE(instance,
                                "Null pointer is passed to AddReference<%s>().",
                                typeid(InstanceClass).name());
    MutexLock lock(manager_mutex_);
    auto it = container_.find(instance);
    if (it != container_.end()) {
      ++it->second;
      return it->second;
    } else {
      auto result = container_.emplace(instance, 1);
      FIREBASE_DEV_ASSERT(result.second);
      return result.first->second;
    }
  }

  /// @brief Decrease reference count by 1. Delete the instance when the count
  /// drops to 0.
  /// @return Reference count after decrement. Return -1 if instance not found.
  int ReleaseReference(InstanceClass* instance) {
    if (!instance) return -1;
    MutexLock lock(manager_mutex_);
    auto it = container_.find(instance);
    if (it != container_.end()) {
      int count = (--it->second);
      if (count == 0) {
        delete it->first;
        container_.erase(it);
      }
      return count;
    }
    return -1;
  }

  /// @brief Access the mutex lock.  This can be used to protect race condition
  /// after the instance is created and before the reference count increments.
  Mutex& mutex() { return manager_mutex_; }

 private:
  /// @brief Mutex to lock whenever container_ is used.
  Mutex manager_mutex_;

  /// @brief Map from the instance address to reference count.
  std::unordered_map<InstanceClass*, int32_t> container_;
};

}  // namespace firebase

#endif  // FIREBASE_APP_CLIENT_UNITY_SRC_CPP_INSTANCE_MANAGER_H_
