// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_LOG_NET_LOG_H_
#define NET_LOG_NET_LOG_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/atomicops.h"
#include "base/compiler_specific.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"
#include "base/types/pass_key.h"
#include "base/values.h"
#include "build/build_config.h"
#include "net/base/net_export.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_entry.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_source_type.h"

namespace net {

class NetLogWithSource;

// NetLog is the destination for log messages generated by the network stack.
// Each log message has a "source" field which identifies the specific entity
// that generated the message (for example, which URLRequest or which
// SpdySession).
//
// To avoid needing to pass in the "source ID" to the logging functions, NetLog
// is usually accessed through a NetLogWithSource, which will always pass in a
// specific source ID.
//
// All methods on NetLog are thread safe, with the exception that no NetLog or
// NetLog::ThreadSafeObserver functions may be called by an observer's
// OnAddEntry() method, as doing so will result in a deadlock.
//
// For a broader introduction see the design document:
// https://sites.google.com/a/chromium.org/dev/developers/design-documents/network-stack/netlog
//
// ==================================
// Materializing parameters
// ==================================
//
// Events can contain a JSON serializable base::Value [1] referred to as its
// "parameters".
//
// Functions for emitting events have overloads that take a |get_params|
// argument for this purpose.
//
// |get_params| is essentially a block of code to conditionally execute when
// the parameters need to be materialized. It is most easily specified as a C++
// lambda.
//
// This idiom for specifying parameters avoids spending time building the
// base::Value when capturing is off. For instance when specified as a lambda
// that takes 0 arguments, the inlined code from template expansion roughly
// does:
//
//   if (net_log->IsCapturing()) {
//     base::Value params = get_params();
//     net_log->EmitEventToAllObsevers(type, source, phase, std::move(params));
//   }
//
// Alternately, the |get_params| argument could be an invocable that takes a
// NetLogCaptureMode parameter:
//
//   base::Value params = get_params(capture_mode);
//
// In this case, |get_params| depends on the logging granularity and would be
// called once per observed NetLogCaptureMode.
//
// [1] Being "JSON serializable" means you cannot use
//     base::Value::Type::BINARY. Instead use NetLogBinaryValue() to repackage
//     it as a base::Value::Type::STRING.
class NET_EXPORT NetLog {
 public:

  // An observer that is notified of entries added to the NetLog. The
  // "ThreadSafe" prefix of the name emphasizes that this observer may be
  // called from different threads then the one which added it as an observer.
  class NET_EXPORT ThreadSafeObserver {
   public:
    // Constructs an observer that wants to see network events, with
    // the specified minimum event granularity.  A ThreadSafeObserver can only
    // observe a single NetLog at a time.
    //
    // Observers will be called on the same thread an entry is added on,
    // and are responsible for ensuring their own thread safety.
    //
    // Observers must stop watching a NetLog before either the observer or the
    // NetLog is destroyed.
    ThreadSafeObserver();

    ThreadSafeObserver(const ThreadSafeObserver&) = delete;
    ThreadSafeObserver& operator=(const ThreadSafeObserver&) = delete;

    // Returns the capture mode for events this observer wants to
    // receive. It is only valid to call this while observing a NetLog.
    NetLogCaptureMode capture_mode() const;

    // Returns the NetLog being watched, or nullptr if there is none.
    NetLog* net_log() const;

    // This method is called whenever an entry (event) was added to the NetLog
    // being watched.
    //
    // OnAddEntry() is invoked on the thread which generated the NetLog entry,
    // which may be different from the thread that added this observer.
    //
    // Whenever OnAddEntry() is invoked, the NetLog's mutex is held. The
    // consequences of this are:
    //
    //   * OnAddEntry() will never be called concurrently -- implementations
    //     can rely on this to avoid needing their own synchronization.
    //
    //   * It is illegal for an observer to call back into the NetLog, or the
    //     observer itself, as this can result in deadlock or violating
    //     expectations of non re-entrancy into ThreadSafeObserver.
    virtual void OnAddEntry(const NetLogEntry& entry) = 0;

   protected:
    virtual ~ThreadSafeObserver();

   private:
    friend class NetLog;

    // Both of these values are only modified by the NetLog.
    NetLogCaptureMode capture_mode_ = NetLogCaptureMode::kDefault;
    raw_ptr<NetLog> net_log_ = nullptr;
  };

  // An observer that is notified of changes in the capture mode set, and has
  // the ability to add NetLog entries with materialized params.
  class NET_EXPORT ThreadSafeCaptureModeObserver {
   public:
    ThreadSafeCaptureModeObserver();

    ThreadSafeCaptureModeObserver(const ThreadSafeCaptureModeObserver&) =
        delete;
    ThreadSafeCaptureModeObserver& operator=(
        const ThreadSafeCaptureModeObserver&) = delete;

    virtual void OnCaptureModeUpdated(NetLogCaptureModeSet modes) = 0;

   protected:
    virtual ~ThreadSafeCaptureModeObserver();

    NetLogCaptureModeSet GetObserverCaptureModes() const;

    // Add event to the observed NetLog. Must only be called while observing is
    // active, and the caller is responsible for ensuring the materialized
    // params are suitable for the current capture mode.
    void AddEntryAtTimeWithMaterializedParams(NetLogEventType type,
                                              const NetLogSource& source,
                                              NetLogEventPhase phase,
                                              base::TimeTicks time,
                                              base::Value::Dict params);

   private:
    // Friend NetLog so that AddCaptureModeObserver/RemoveCaptureModeObserver
    // can update the |net_log_| member.
    friend class NetLog;

    // This value is only modified by the NetLog.
    raw_ptr<NetLog> net_log_ = nullptr;
  };

  // Returns the singleton NetLog object, which is never destructed and which
  // may be used on any thread.
  static NetLog* Get();

  // NetLog should only be used through the singleton returned by Get(), the
  // constructor takes a PassKey to ensure that additional NetLog objects
  // cannot be created.
  explicit NetLog(base::PassKey<NetLog>);

  // NetLogWithSource creates a dummy NetLog as an internal optimization.
  explicit NetLog(base::PassKey<NetLogWithSource>);

  NetLog(const NetLog&) = delete;
  NetLog& operator=(const NetLog&) = delete;

  ~NetLog() = delete;

  // Configure the source IDs returned by NextID() to use a different starting
  // position, so that NetLog events generated by this process will not conflict
  // with those generated by another NetLog in a different process. This
  // should only be called once, before any NetLogSource could be created in
  // the current process.
  //
  // Currently only a single additional source id partition is supported.
  void InitializeSourceIdPartition();

  void AddEntry(NetLogEventType type,
                const NetLogSource& source,
                NetLogEventPhase phase);

  // NetLog parameter generators (lambdas) come in two flavors -- those that
  // take no arguments, and those that take a single NetLogCaptureMode. This
  // code allows differentiating between the two.
  template <typename T, typename = void>
  struct ExpectsCaptureMode : std::false_type {};
  template <typename T>
  struct ExpectsCaptureMode<T,
                            decltype(void(std::declval<T>()(
                                NetLogCaptureMode::kDefault)))>
      : std::true_type {};

  // Adds an entry for the given source, phase, and type, whose parameters are
  // obtained by invoking |get_params()| with no arguments.
  //
  // See "Materializing parameters" for details.
  template <typename ParametersCallback>
  inline typename std::enable_if<!ExpectsCaptureMode<ParametersCallback>::value,
                                 void>::type
  AddEntry(NetLogEventType type,
           const NetLogSource& source,
           NetLogEventPhase phase,
           const ParametersCallback& get_params) {
    if (LIKELY(!IsCapturing()))
      return;

    AddEntryWithMaterializedParams(type, source, phase, get_params());
  }

  // Adds an entry for the given source, phase, and type, whose parameters are
  // obtained by invoking |get_params(capture_mode)| with a NetLogCaptureMode.
  //
  // See "Materializing parameters" for details.
  template <typename ParametersCallback>
  inline typename std::enable_if<ExpectsCaptureMode<ParametersCallback>::value,
                                 void>::type
  AddEntry(NetLogEventType type,
           const NetLogSource& source,
           NetLogEventPhase phase,
           const ParametersCallback& get_params) {
    if (LIKELY(!IsCapturing()))
      return;

    // Indirect through virtual dispatch to reduce code bloat, as this is
    // inlined in a number of places.
    class GetParamsImpl : public GetParamsInterface {
     public:
      explicit GetParamsImpl(const ParametersCallback& get_params)
          : get_params_(get_params) {}
      base::Value::Dict GetParams(NetLogCaptureMode mode) const override {
        return (*get_params_)(mode);
      }

     private:
      const raw_ref<const ParametersCallback> get_params_;
    };

    GetParamsImpl wrapper(get_params);
    AddEntryInternal(type, source, phase, &wrapper);
  }

  // Emits a global event to the log stream, with its own unique source ID.
  void AddGlobalEntry(NetLogEventType type);

  // Overload of AddGlobalEntry() that includes parameters.
  //
  // See "Materializing parameters" for details on |get_params|.
  template <typename ParametersCallback>
  void AddGlobalEntry(NetLogEventType type,
                      const ParametersCallback& get_params) {
    AddEntry(type, NetLogSource(NetLogSourceType::NONE, NextID()),
             NetLogEventPhase::NONE, get_params);
  }

  void AddGlobalEntryWithStringParams(NetLogEventType type,
                                      base::StringPiece name,
                                      base::StringPiece value);

  // Returns a unique ID which can be used as a source ID.  All returned IDs
  // will be unique and not equal to 0.
  uint32_t NextID();

  // Returns true if there are any observers attached to the NetLog.
  //
  // TODO(eroman): Survey current callsites; most are probably not necessary,
  // and may even be harmful.
  bool IsCapturing() const {
    return GetObserverCaptureModes() != 0;
  }

  // Adds an observer and sets its log capture mode.  The observer must not be
  // watching any NetLog, including this one, when this is called.
  //
  // CAUTION: Think carefully before introducing a dependency on the
  // NetLog. The order, format, and parameters in NetLog events are NOT
  // guaranteed to be stable. As such, building a production feature that works
  // by observing the NetLog is likely inappropriate. Just as you wouldn't build
  // a feature by scraping the text output from LOG(INFO), you shouldn't do
  // the same by scraping the logging data emitted to NetLog. Support for
  // observers is an internal detail mainly used for testing and to write events
  // to a file. Please consult a //net OWNER before using this outside of
  // testing or serialization.
  void AddObserver(ThreadSafeObserver* observer,
                   NetLogCaptureMode capture_mode);

  // Removes an observer.
  //
  // For thread safety reasons, it is recommended that this not be called in
  // an object's destructor.
  void RemoveObserver(ThreadSafeObserver* observer);

  // Adds an observer that is notified of changes in the capture mode set.
  void AddCaptureModeObserver(ThreadSafeCaptureModeObserver* observer);

  // Removes a capture mode observer.
  void RemoveCaptureModeObserver(ThreadSafeCaptureModeObserver* observer);

  // Converts a time to the string format that the NetLog uses to represent
  // times.  Strings are used since integers may overflow.
  // The resulting string contains the number of milliseconds since the origin
  // or "zero" point of the TimeTicks class, which can vary each time the
  // application is restarted. This number is related to an actual time via the
  // timeTickOffset recorded in GetNetConstants().
  static std::string TickCountToString(const base::TimeTicks& time);

  // Same as above but takes a base::Time. Should not be used if precise
  // timestamps are desired, but is suitable for e.g. expiration times.
  static std::string TimeToString(const base::Time& time);

  // Returns a dictionary that maps event type symbolic names to their enum
  // values.
  static base::Value GetEventTypesAsValue();

  // Returns a C-String symbolic name for |source_type|.
  static const char* SourceTypeToString(NetLogSourceType source_type);

  // Returns a dictionary that maps source type symbolic names to their enum
  // values.
  static base::Value GetSourceTypesAsValue();

  // Returns a C-String symbolic name for |event_phase|.
  static const char* EventPhaseToString(NetLogEventPhase event_phase);

 private:
  class GetParamsInterface {
   public:
    virtual base::Value::Dict GetParams(NetLogCaptureMode mode) const = 0;
    virtual ~GetParamsInterface() = default;
  };

  // Helper for implementing AddEntry() that indirects parameter getting through
  // virtual dispatch.
  void AddEntryInternal(NetLogEventType type,
                        const NetLogSource& source,
                        NetLogEventPhase phase,
                        const GetParamsInterface* get_params);

  // Returns the set of all capture modes being observed.
  NetLogCaptureModeSet GetObserverCaptureModes() const {
    return base::subtle::NoBarrier_Load(&observer_capture_modes_);
  }

  // Adds an entry using already materialized parameters, when it is already
  // known that the log is capturing (goes straight to acquiring observer lock).
  //
  // TODO(eroman): Drop the rvalue-ref on |params| unless can show it improves
  // the generated code (initial testing suggests it makes no difference in
  // clang).
  void AddEntryWithMaterializedParams(NetLogEventType type,
                                      const NetLogSource& source,
                                      NetLogEventPhase phase,
                                      base::Value::Dict params);

  // Adds an entry at a certain time, using already materialized parameters,
  // when it is already known that the log is capturing (goes straight to
  // acquiring observer lock).
  void AddEntryAtTimeWithMaterializedParams(NetLogEventType type,
                                            const NetLogSource& source,
                                            NetLogEventPhase phase,
                                            base::TimeTicks time,
                                            base::Value::Dict params);

  // Called whenever an observer is added or removed, to update
  // |observer_capture_modes_|. Must have acquired |lock_| prior to calling.
  void UpdateObserverCaptureModes();

  // Returns true if |observer| is watching this NetLog. Must
  // be called while |lock_| is already held.
  bool HasObserver(ThreadSafeObserver* observer);
  bool HasCaptureModeObserver(ThreadSafeCaptureModeObserver* observer);

  // |lock_| protects access to |observers_|.
  base::Lock lock_;

  // Last assigned source ID.  Incremented to get the next one.
  base::subtle::Atomic32 last_id_ = 0;

  // Holds the set of all capture modes that observers are watching the log at.
  //
  // Is 0 when there are no observers. Stored as an Atomic32 so it can be
  // accessed and updated more efficiently.
  base::subtle::Atomic32 observer_capture_modes_ = 0;

  // |observers_| is a list of observers, ordered by when they were added.
  // Pointers contained in |observers_| are non-owned, and must
  // remain valid.
  //
  // |lock_| must be acquired whenever reading or writing to this.
  //
  // In practice |observers_| will be very small (<5) so O(n)
  // operations on it are fine.
  std::vector<raw_ptr<ThreadSafeObserver, VectorExperimental>> observers_;

  std::vector<raw_ptr<ThreadSafeCaptureModeObserver, VectorExperimental>>
      capture_mode_observers_;
};

}  // namespace net

#endif  // NET_LOG_NET_LOG_H_
