// Copyright 2014 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RCLCPP__EXECUTORS__SINGLE_THREADED_EXECUTOR_HPP_
#define RCLCPP__EXECUTORS__SINGLE_THREADED_EXECUTOR_HPP_

#include <rmw/rmw.h>

#include <cassert>
#include <cstdlib>
#include <memory>
#include <vector>

#include "rclcpp/executor.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/memory_strategies.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/utilities.hpp"
#include "rclcpp/rate.hpp"
#include "rclcpp/visibility_control.hpp"
#include "rclcpp/tcl_node_interfaces/node_timing_interface.hpp"

namespace rclcpp
{
namespace executors
{
//
// Timing Coordination Library
//
typedef struct subscription_object_t
{
  rclcpp::SubscriptionBase::SharedPtr subscription;
  std::shared_ptr<void> message;
  rclcpp::MessageInfo message_info;
  
  subscription_object_t(
    rclcpp::SubscriptionBase::SharedPtr & _subscription,
    std::shared_ptr<void> _message,
    rclcpp::MessageInfo _message_info
  ) : subscription(_subscription), message(_message), message_info(_message_info) {}

} subscription_object_t;

/// Single-threaded executor implementation.
/**
 * This is the default executor created by rclcpp::spin.
 */
class SingleThreadedExecutor : public rclcpp::Executor
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(SingleThreadedExecutor)

  /// Default constructor. See the default constructor for Executor.
  RCLCPP_PUBLIC
  explicit SingleThreadedExecutor(
    const rclcpp::ExecutorOptions & options = rclcpp::ExecutorOptions());

  /// Default destructor.
  RCLCPP_PUBLIC
  virtual ~SingleThreadedExecutor();

  /// Single-threaded implementation of spin.
  /**
   * This function will block until work comes in, execute it, and then repeat
   * the process until canceled.
   * It may be interrupt by a call to rclcpp::Executor::cancel() or by ctrl-c
   * if the associated context is configured to shutdown on SIGINT.
   * \throws std::runtime_error when spin() called while already spinning
   */

  //
  //Timing Coordination Library
  //

  RCLCPP_PUBLIC
  void
  spin() override;

  RCLCPP_PUBLIC
  void
  spin_some(std::chrono::nanoseconds max_duration = std::chrono::nanoseconds(0)) override;

  RCLCPP_PUBLIC
  void
  add_node(rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify = true) override;

  RCLCPP_PUBLIC
  void
  add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify = true) override;

  RCLCPP_PUBLIC
  void
  sleep(bool second = false);

  RCLCPP_PUBLIC
  void
  add_node_timing_interface(rclcpp::tcl_node_interfaces::NodeTimingInterface::SharedPtr node_timing) override;

  RCLCPP_PUBLIC
  void
  spin_some_impl(std::chrono::nanoseconds max_duration, bool exhaustive);

  RCLCPP_PUBLIC
  void
  execute_any_executable_for_tcl(AnyExecutable & any_exec);

  RCLCPP_PUBLIC
  void
  add_subscription_to_buffer(rclcpp::SubscriptionBase::SharedPtr subscription);

  RCLCPP_PUBLIC
  void
  add_timer_to_buffer(rclcpp::TimerBase::SharedPtr timer);

  RCLCPP_PUBLIC
  void
  handle_buffer();

  RCLCPP_PUBLIC
  void
  handle_subscription_buffer();

  RCLCPP_PUBLIC
  void
  handle_timer_buffer();

  RCLCPP_PUBLIC
  bool
  check_blocking_condition();

  RCLCPP_PUBLIC
  void
  update_blocking_condition_map(const char * topic_name);

  RCLCPP_PUBLIC
  void
  reset_blocking_condition_map();

  RCLCPP_PUBLIC
  void
  publish_profile_data(int64_t mod_release_time = 0);

  RCLCPP_PUBLIC
  void spin_some_impl_blocking_();

  RCLCPP_PUBLIC
  void get_src_ref_time_arr(std::vector<std::pair<std::string, int64_t>>& crt_times);


private:
  RCLCPP_DISABLE_COPY(SingleThreadedExecutor)

  rclcpp::tcl_node_interfaces::NodeReleaseTimerInterface::SharedPtr tcl_timer_{nullptr};
  rclcpp::tcl_node_interfaces::NodeProfileInterface::SharedPtr tcl_profiler_{nullptr};
  rclcpp::tcl_node_interfaces::NodeTimingPropagateInterface::SharedPtr tcl_timing_propagator_{nullptr};

  std::vector<subscription_object_t> subscription_objects_; // callback 실행 가능한 객체
  std::unordered_map<std::string, bool> blocking_condition_map_; // blocking topic 도착 여부를 담는 map
  std::vector<rclcpp::TimerBase::SharedPtr> timer_objects_; // timercallback 실행 가능한 객체 벡터

  bool use_tcl_timer_{false}; // NodeReleaseTimer 를 사용할 경우 true
  bool use_net_{false};  //
  bool use_blocking_io_{false}; // Blocking I/O 인 노드인 경우 true
  bool is_condition_satisfied_{false}; // Blocking topic 이 모두 수신된 경우 true
  bool profile_enabled_{false}; // Timing Propagation 기능을 사용할 경우 true
  bool received_{false}; // 노드의 처음 실행 후, 첫번째 Blocking topic 을 수신할 경우 true
  int64_t timeout_{-1}; //get_next_executable 의 인자로 전달되는 timeout 값, timeout 만큼 middleware 에서 수신된 객체를 기다림
  std::vector<std::string> src_tasks_; // 현재 태스크가 포함된 경로의 시작 태스크 이름
  std::string networking_topic_{std::string("")}; // 통신시간을 프로파일링하고자 하는 토픽 이름
  int64_t task_skip_cnt_{-1}; // skip count
  int64_t period_{-1}; // 현재 노드의 주기
  int64_t global_{-1}; // global reference time
  int64_t phase_{-1}; // 현재 노드의 위상값

  std::vector<std::pair<std::string, int64_t>>  crt_times_; // 수신한 blocking topic 들의 creation_time(을 기반으로 계산한 reference time)을 담은 벡터
  bool skip_timeout_{false}; // skip count 를 기반으로 timeout 기능을 사용할 경우 true
  int64_t release_after_{0}; // sleep 함수 호출 후 계산한 release time
  int64_t release_before_{0}; // sleep 함수 호출 전 계산한 release time
  int64_t communication_start_{0}; // 통신 시간의 시작 시점
  int64_t communication_end_{0}; // 통신 시간의 종료 시점
  
  rclcpp::Time execution_start_time_; // 실행 시작 시점
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base_{nullptr}; 
  std::function<rclcpp::Time()> get_now = [&]()
  {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return rclcpp::Time(static_cast<int32_t>(ts.tv_sec), static_cast<uint32_t>(ts.tv_nsec), RCL_STEADY_TIME);
  };
};

}  // namespace executors
}  // namespace rclcpp

#endif  // RCLCPP__EXECUTORS__SINGLE_THREADED_EXECUTOR_HPP_
