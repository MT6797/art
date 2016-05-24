/*
 * Copyright 2014 The Android Open Source Project
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

#include "jit_instrumentation.h"

#include "art_method-inl.h"
#include "jit.h"
#include "jit_code_cache.h"
#include "scoped_thread_state_change.h"

namespace art {
namespace jit {

#ifdef MTK_ARTSLIM_ENABLE
__attribute__((weak))
MTK_JitInstrumentationCache* GenMTKJITInstrument() {
  return nullptr;
}

__attribute__((weak))
size_t MTKAddSamples1(std::unordered_map<jmethodID, size_t>* samples,
                      size_t hot_method_threshold,
                      size_t it_second,
                      size_t count)
       SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  UNUSED(samples);
  UNUSED(hot_method_threshold);
  return it_second + count;
}

__attribute__((weak))
size_t MTKAddSamples2(std::unordered_map<jmethodID, size_t>* samples,
                      size_t hot_method_threshold,
                      size_t count,
                      ArtMethod* method)
       SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  UNUSED(samples);
  UNUSED(hot_method_threshold);
  UNUSED(method);
  return count;
}

__attribute__((weak))
bool MTKAddSamples3()
       SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  return false;
}

__attribute__((weak))
void MTKAddSamples4(ArtMethod* method, JitInstrumentationCache* jit_instrument)
       SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  UNUSED(method);
  UNUSED(jit_instrument);
}
#endif

class JitCompileTask : public Task {
 public:
  explicit JitCompileTask(ArtMethod* method, JitInstrumentationCache* cache)
      : method_(method), cache_(cache) {
  }

  virtual void Run(Thread* self) OVERRIDE {
    ScopedObjectAccess soa(self);
    VLOG(jit) << "JitCompileTask compiling method " << PrettyMethod(method_);
    if (Runtime::Current()->GetJit()->CompileMethod(method_, self)) {
      cache_->SignalCompiled(self, method_);
    } else {
      VLOG(jit) << "Failed to compile method " << PrettyMethod(method_);
    }
  }

  virtual void Finalize() OVERRIDE {
    delete this;
  }

 private:
  ArtMethod* const method_;
  JitInstrumentationCache* const cache_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(JitCompileTask);
};

JitInstrumentationCache::JitInstrumentationCache(size_t hot_method_threshold)
#ifdef MTK_ARTSLIM_ENABLE
    : lock_("jit instrumentation lock"),
      mtk_jit_instrument_(GenMTKJITInstrument()),
      hot_method_threshold_(hot_method_threshold) {
#else
    : lock_("jit instrumentation lock"), hot_method_threshold_(hot_method_threshold) {
#endif
}

void JitInstrumentationCache::CreateThreadPool() {
  thread_pool_.reset(new ThreadPool("Jit thread pool", 1));
}

void JitInstrumentationCache::DeleteThreadPool() {
  thread_pool_.reset();
}

void JitInstrumentationCache::SignalCompiled(Thread* self, ArtMethod* method) {
  ScopedObjectAccessUnchecked soa(self);
  jmethodID method_id = soa.EncodeMethod(method);
  MutexLock mu(self, lock_);
  auto it = samples_.find(method_id);
  if (it != samples_.end()) {
    samples_.erase(it);
  }
}

void JitInstrumentationCache::AddSamples(Thread* self, ArtMethod* method, size_t count) {
  ScopedObjectAccessUnchecked soa(self);
  // Since we don't have on-stack replacement, some methods can remain in the interpreter longer
  // than we want resulting in samples even after the method is compiled.
  if (method->IsClassInitializer() || method->IsNative() ||
      Runtime::Current()->GetJit()->GetCodeCache()->ContainsMethod(method)) {
    return;
  }

  if (thread_pool_.get() == nullptr) {
    // DCHECK(Runtime::Current()->IsShuttingDown(self));
    return;
  }
  // should not happen
  if (Runtime::Current()->IsShuttingDown(self))  {
    return;
  }

  jmethodID method_id = soa.EncodeMethod(method);
  bool is_hot = false;
  {
    MutexLock mu(self, lock_);
    size_t sample_count = 0;
    auto it = samples_.find(method_id);
    if (it != samples_.end()) {
      #ifdef MTK_ARTSLIM_ENABLE
      it->second = MTKAddSamples1(&samples_,
                                  hot_method_threshold_,
                                  it->second,
                                  count);
      #else
      it->second += count;
      #endif
      sample_count = it->second;
    } else {
      #ifdef MTK_ARTSLIM_ENABLE
      sample_count = MTKAddSamples2(&samples_,
                                    hot_method_threshold_,
                                    count,
                                    method);
      #else
      sample_count = count;
      #endif
      samples_.insert(std::make_pair(method_id, count));
    }
    // If we have enough samples, mark as hot and request Jit compilation.
    if (sample_count >= hot_method_threshold_ && sample_count - count < hot_method_threshold_) {
      is_hot = true;
    }
  }
  if (is_hot) {
    #ifdef MTK_ARTSLIM_ENABLE
    if (MTKAddSamples3()) {
      MTKAddSamples4(method, this);
    } else {
    #endif
    if (thread_pool_.get() != nullptr) {
      if (Runtime::Current()->IsShuttingDown(self))  {
        return;
      }
      thread_pool_->AddTask(self, new JitCompileTask(
          method->GetInterfaceMethodIfProxy(sizeof(void*)), this));
      thread_pool_->StartWorkers(self);
    } else {
      VLOG(jit) << "Compiling hot method " << PrettyMethod(method);
      Runtime::Current()->GetJit()->CompileMethod(
          method->GetInterfaceMethodIfProxy(sizeof(void*)), self);
    }
    #ifdef MTK_ARTSLIM_ENABLE
    }
    #endif
  }
}

JitInstrumentationListener::JitInstrumentationListener(JitInstrumentationCache* cache)
    : instrumentation_cache_(cache) {
  CHECK(instrumentation_cache_ != nullptr);
}

}  // namespace jit
}  // namespace art
