// Repository: Retrovue-playout
// Component: Event Bus Stub
// Purpose: Test adapter for event bus for contract tests.
// Copyright (c) 2025 RetroVue

#ifndef RETROVUE_TESTS_FIXTURES_EVENT_BUS_STUB_H_
#define RETROVUE_TESTS_FIXTURES_EVENT_BUS_STUB_H_

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace retrovue::tests::fixtures
{

  // Event types for test contract
  enum class TestEventType
  {
    READY,
    CHILD_EXIT,
    ERROR,
    STDERR
  };

  // Event data structure
  struct TestEvent
  {
    TestEventType type;
    std::string message;
    int exit_code;

    TestEvent(TestEventType t, const std::string &msg = "", int code = 0)
        : type(t), message(msg), exit_code(code) {}
  };

  // EventBusStub provides a test adapter for event bus functionality.
  // It captures events for test verification.
  class EventBusStub
  {
  public:
    EventBusStub() = default;

    // Clears all captured events.
    void Clear()
    {
      std::lock_guard<std::mutex> lock(mutex_);
      events_.clear();
    }

    // Returns all captured events.
    std::vector<TestEvent> GetEvents() const
    {
      std::lock_guard<std::mutex> lock(mutex_);
      return events_;
    }

    // Returns the number of events of a specific type.
    size_t GetEventCount(TestEventType type) const
    {
      std::lock_guard<std::mutex> lock(mutex_);
      size_t count = 0;
      for (const auto &event : events_)
      {
        if (event.type == type)
        {
          count++;
        }
      }
      return count;
    }

    // Returns true if a specific event type was emitted.
    bool HasEvent(TestEventType type) const
    {
      return GetEventCount(type) > 0;
    }

    // Emits an event (called by VideoFileProducer callback).
    void Emit(TestEventType type, const std::string &message = "", int exit_code = 0)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      events_.emplace_back(type, message, exit_code);
    }

    // Converts string event type to TestEventType.
    static TestEventType ToEventType(const std::string &event_type)
    {
      if (event_type == "ready")
        return TestEventType::READY;
      if (event_type == "child_exit")
        return TestEventType::CHILD_EXIT;
      if (event_type == "error")
        return TestEventType::ERROR;
      if (event_type == "stderr")
        return TestEventType::STDERR;
      return TestEventType::ERROR; // Default
    }

  private:
    mutable std::mutex mutex_;
    std::vector<TestEvent> events_;
  };

} // namespace retrovue::tests::fixtures

#endif // RETROVUE_TESTS_FIXTURES_EVENT_BUS_STUB_H_

