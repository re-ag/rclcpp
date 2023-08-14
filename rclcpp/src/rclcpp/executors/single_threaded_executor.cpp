// Copyright 2015 Open Source Robotics Foundation, Inc.
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

#include "rcpputils/scope_exit.hpp"

#include "rclcpp/executors/single_threaded_executor.hpp"
#include "rclcpp/any_executable.hpp"
#include "rclcpp/tcl_node_interfaces/node_release_timer.hpp"

static
void
take_and_do_error_handling(
  const char * action_description,
  const char * topic_or_service_name,
  std::function<bool()> take_action,
  std::function<void()> handle_action)
{
  bool taken = false;
  try {
    taken = take_action();
  } catch (const rclcpp::exceptions::RCLError & rcl_error) {
    RCLCPP_ERROR(
      rclcpp::get_logger("rclcpp"),
      "executor %s '%s' unexpectedly failed: %s",
      action_description,
      topic_or_service_name,
      rcl_error.what());
  }
  if (taken) {
    handle_action();
  } else {
    // Message or Service was not taken for some reason.
    // Note that this can be normal, if the underlying middleware needs to
    // interrupt wait spuriously it is allowed.
    // So in that case the executor cannot tell the difference in a
    // spurious wake up and an entity actually having data until trying
    // to take the data.
    RCLCPP_DEBUG(
      rclcpp::get_logger("rclcpp"),
      "executor %s '%s' failed to take anything",
      action_description,
      topic_or_service_name);
  }
}

using rclcpp::executors::SingleThreadedExecutor;

SingleThreadedExecutor::SingleThreadedExecutor(const rclcpp::ExecutorOptions & options)
: rclcpp::Executor(options) {}

SingleThreadedExecutor::~SingleThreadedExecutor() {}

/**
 * @brief 프로그램이 종료될 때 까지 이벤트 루프를 실행하며, 새로운 메시지나 이벤트가 발생할 때마다 적절한 콜백함수를 호출하여 처리함
 * 
 */
void
SingleThreadedExecutor::spin()
{
  bool ret = false;
  int64_t margin; // timeout 기능 사용시, sleep 호출을 결정하는 margin
  int64_t curr_offset = 0;
  bool is_expired = false;

  if(use_tcl_timer_){
    if (period_ < 50000000)
      margin = 700000; // 주기 50ms 이하인 노드의 경우 700us margin 설정
    else
      margin = 1000000; // 주기 100ms 인 노드의 경우 1ms margin 설정
  }
  std::cout << "first spin() now : " << get_now().nanoseconds() << std::endl;
  
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin() called while already spinning");
  }
  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );

  while (rclcpp::ok(this->context_) && spinning.load()) {
    rclcpp::AnyExecutable any_executable;
    /*
      첫번째 blocking topic 을 수신하기 전(received==false)까지 무한대로 기다리지 않고, (다음 도착 시간 - margin) 까지만 기다리고 sleep 호출하여 위상을 맞춤
    */
  
    if(use_tcl_timer_ && (!received_ || skip_timeout_) ) 
    {
      curr_offset = (this->get_now().nanoseconds() - global_) % period_;
      timeout_ = period_ - curr_offset - margin;
      int64_t release = tcl_timer_->get_release_start_time_point();
      if (timeout_ < 0) // margin 을 지난 경우 sleep 호출 필요 
      {
        is_expired = true;
      }
    }

    if (use_tcl_timer_ && use_blocking_io_ && is_expired && (!received_ || skip_timeout_))
    {
      is_expired = false;
      skip_timeout_ = false;

      reset_blocking_condition_map();
      is_condition_satisfied_ = false;
      
      std::cout << "before timeout sleep" << std::endl;
      task_skip_cnt_ = tcl_timer_->sleep(); 
    } 

    if (get_next_executable(any_executable, std::chrono::nanoseconds(timeout_)))
    {
      execute_any_executable_for_tcl(any_executable);
      if(use_tcl_timer_ && use_blocking_io_ && is_condition_satisfied_) // blocking topic 처리가 모두 완료된 경우
      {
        reset_blocking_condition_map(); // blocking_condition_map_ 을 false 로 모두 초기화
        is_condition_satisfied_ = false; 

        std::cout << "crt_times_[0].second = " << crt_times_[0].second<< std::endl; // 처리한 blocking_topic 메시지의 creation_time
        release_before_ = tcl_timer_->get_release_start_time_point() - phase_; 
        if(crt_times_[0].second >= release_before_){ 
          skip_timeout_ = false;
          this->sleep();
          if(task_skip_cnt_ > 0){ // 현행 태스크의 실행 시간이 커서 timer expired 된 경우
            if(crt_times_[0].second >= release_after_){ 
              std::cout << "No waiting..." << std::endl;
              task_skip_cnt_ = tcl_timer_->sleep(); 
            }
            else{ // blocking topic 메시지의 creation time 이 현행 태스크의 sleep 이후의 시간 이전이라면, 다음 도착시간 전까지 blocking topic 을 더 수신하여 처리할 필요가 있음
              std::cout << "Waiting for timeout..." << std::endl;
              skip_timeout_ = true; // timeout 기능을 위해 skip_timeout_ = true
            }
          }
        }
        else{ // skip_timeout 동안 기다려서 받은 메시지 처리...(현재 태스크의 도착시간 이전의 creation_time 을 가지는 blocking topic 메시지를 처리한 경우 blocking topic 을 더 처리해야 함)
          if(task_skip_cnt_ > 0){ // queue size >1 인 동시에 실행시간이 큰 경우는 연쇄적으로 점점 밀리기 때문에, task_skip_cnt <0 일 때가 생김 이럴 때 publish 안하면 안됨
            std::cout << "task_skip_cnt_ = " << task_skip_cnt_ << std::endl;
            int64_t modified_release = release_after_ - (task_skip_cnt_ -1) * period_ + phase_;
            std::cout << "modified_release = " << modified_release  << std::endl;
            publish_profile_data(modified_release);
            task_skip_cnt_--;
            // skip_timeout_ = true;
          }
          else{ 
            std::cout << "Waiting for timeout2..." << std::endl;
            publish_profile_data(); // 현재 메시지 처리한 것에 대한 Profie.msg 발행
            skip_timeout_ = true; // 이전 주기 태스크 인스턴스의 메시지를 처리한 것이기 때문에 timeout 기능으로 메시지 더 처리해야 함
          }
        }
      }
      if(!use_tcl_timer_ && profile_enabled_ && is_condition_satisfied_)
      {
        reset_blocking_condition_map();
        is_condition_satisfied_ = false;
        if(tcl_profiler_)// publish profile message
        {
          auto execution_end = get_now().nanoseconds();
          // communication_start_ = node_base_->get_network_start();
          communication_start_ = execution_end;
          std::cout << "(SPIN) communication_start_ : " << communication_start_ << std::endl;
          std::vector<tcl_std_msgs::msg::TimingHeader> &timing_header_arr = tcl_timing_propagator_->get_timing_header_arr();
          tcl_profiler_->publish(
            timing_header_arr,
            0,
            0,
            execution_start_time_.nanoseconds(),
            execution_end,
            communication_start_,
            communication_end_
          );
          timing_header_arr.clear();
        }
      }
    }
    else
    {
      if(!use_blocking_io_){
        this->sleep();
      }
    }
  }
}

void
SingleThreadedExecutor::spin_some(std::chrono::nanoseconds max_duration)
{
  if(profile_enabled_)
  {
    execution_start_time_ = get_now();
    std::cout << "\nrelease_start_time(spin_some) : " << (tcl_timer_->get_release_start_time_point() % (int)1e9 )*1e-6 << std::endl;
    std::cout << "execution_start_time(spin_some) : " << (execution_start_time_.nanoseconds() % (int)1e9) * 1e-6<< std::endl;
  }
  if(use_tcl_timer_ && use_blocking_io_){
    return this->spin_some_impl_blocking_();
  }
  return this->spin_some_impl(max_duration, false);
}

/**
 * @brief concatenate_filter(CCF) 노드에서 사용하기 위해 정의한 spin_some_impl 의 blocking I/O 버전 (CCF 를 위한 함수라 하드코딩이 많음)
 * 
 * 
 */
void
SingleThreadedExecutor::spin_some_impl_blocking_()
{
  int64_t margin = 1000000; // 100ms 주기의 노드라 margin = 1ms
  int64_t curr_offset = 0;
  bool is_expired = false;
  std::cout << "first spin_some_blocking() now : " << get_now().nanoseconds() << std::endl;
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin() called while already spinning");
  }
  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );

  while (rclcpp::ok(this->context_) && spinning.load()) {
    rclcpp::AnyExecutable any_executable;
    if(use_tcl_timer_ && !received_)
    {
      curr_offset = (this->get_now().nanoseconds() - global_) % period_;
      timeout_ = period_ - curr_offset - margin;
      int64_t release = tcl_timer_->get_release_start_time_point();
      if (timeout_ < 0)
      {
        is_expired = true;
      }
    }

    if (use_tcl_timer_ && use_blocking_io_ && is_expired && !received_)
    {
      is_expired = false;
      reset_blocking_condition_map();
      is_condition_satisfied_ = false;
      if(tcl_timer_)
      {
        std::cout << "expired sleep / now : " << this->get_now().nanoseconds() << " release : " << tcl_timer_->get_release_start_time_point() << std::endl;
        task_skip_cnt_ = tcl_timer_->sleep(); 
        std::cout << "after   sleep / now : " << this->get_now().nanoseconds() << " release : " << tcl_timer_->get_release_start_time_point() << std::endl;
      }
    }
    else if (get_next_executable(any_executable, std::chrono::nanoseconds(timeout_)))
    {
      execute_any_executable_for_tcl(any_executable);

      if(use_tcl_timer_ && use_blocking_io_ && is_condition_satisfied_)
      {
        int64_t curr_offset = (execution_start_time_.nanoseconds() - global_) % period_;
        release_before_ = execution_start_time_.nanoseconds() - curr_offset ; // sleep 호출 전 release_time

        std::cout << "release_before_ = " << release_before_ << std::endl;
        int64_t front_crt;
        int64_t rear_crt;
        for(auto crt_time: crt_times_){
          std::cout << crt_time.first << " 's creation time = " << crt_time.second << std::endl;
          if(crt_time.first.find("front") != std::string::npos){ // FLD 토픽의 creation _time
            front_crt = crt_time.second;
          }
          if(crt_time.first.find("rear") != std::string::npos){ // RLD 토픽의 creation_time
            rear_crt = crt_time.second;
          }
        }
        if(front_crt == release_before_ && rear_crt == release_before_){ // 현재 노드와 같은 주기 인스턴스에 해당되는 FLD, RLD 의 메시지라면 제대로 수신받은 것
          std::cout << "RESET" << std::endl;
          reset_blocking_condition_map();
          is_condition_satisfied_ = false;
          break; // while loop 를 나가서, CCF 의 timer_callback 수행 후 SingleThreadedExecutor::sleep() 호출
        }
        else if(front_crt < release_before_){ // FLD가 현재 노드의 이전 주기 인스턴스라면 FLD 토픽을 더 수신받아야 함
          std::cout << "FRONT MORE" << std::endl;

          std::for_each(blocking_condition_map_.begin(), blocking_condition_map_.end(), [&](auto& it)
          {
            // FLD 의 토픽을 더 수신받기 위해 false 처리
            if (it.first.find("front") != std::string::npos)
              it.second = false; 
          });
          std::cout << "front creation_time  = " << front_crt << std::endl;
          // not break;
        }
        else if(rear_crt < release_before_){// RLD가 현재 노드의 이전 주기 인스턴스라면 RLD 토픽을 더 수신받아야 함
          std::cout << "REAR MORE" << std::endl;
          std::for_each(blocking_condition_map_.begin(), blocking_condition_map_.end(), [&](auto& it){
            // RLD 의 토픽을 더 수신받기 위해 false 처리
            if(it.first.find("rear") != std::string::npos)
              it.second = false;
          });
          std::cout << "rear creation_time  = " << rear_crt << std::endl;
          // not break
        }
        
      }
    }
  }
}

/**
 * @brief SingleThreadedExecutor 에 노드를 추가하는 함수 (Executor 의 add_node 와 동일)
 * 
 * @param node_ptr 추가할 노드의 NodeBase 포인터
 * @param notify 
 */
void
SingleThreadedExecutor::add_node(rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify)
{

  // If the node already has an executor
  if(!node_base_)
    node_base_ = node_ptr;
  std::atomic_bool & has_executor = node_ptr->get_associated_with_executor_atomic();
  if (has_executor.exchange(true)) {
    throw std::runtime_error(
            std::string("Node '") + node_ptr->get_fully_qualified_name() +
            "' has already been added to an executor.");
  }
  std::lock_guard<std::mutex> guard{mutex_};
  node_ptr->for_each_callback_group(
    [this, node_ptr, notify](rclcpp::CallbackGroup::SharedPtr group_ptr)
    {
      if (!group_ptr->get_associated_with_executor_atomic().load() &&
      group_ptr->automatically_add_to_executor_with_node())
      {
        this->add_callback_group_to_map(
          group_ptr,
          node_ptr,
          weak_groups_to_nodes_associated_with_executor_,
          notify);
      }
    });

  weak_nodes_.push_back(node_ptr);
}

/**
 * @brief SingleThreadedExecutor 에 노드를 추가하는 함수(NodeTiming 까지 추가)
 * 
 * @param node_ptr 추가할 Node 포인터
 * @param notify 
 */
void
SingleThreadedExecutor::add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  if(!node_base_)
    node_base_ = node_ptr->get_node_base_interface();
  add_node(node_ptr->get_node_base_interface(), notify);
  add_node_timing_interface(node_ptr->get_node_timing_interface());
}

/**
 * @brief 노드의 실행 종료 시 호출하는 함수로, Profile.msg 를 발행하며 NodeReleaseTimer 의 sleep 함수를 호출하여 다음 도착 시간까지 대기
 * 
 * @param second NodeRelaseTimer 의 timerfd 만료 시, 다음 도착시간(타이머 이벤트)까지 대기하고자 한다면 true
 */
void
SingleThreadedExecutor::sleep(bool second)
{
  if(profile_enabled_)
    publish_profile_data();
  if(tcl_timer_){
    task_skip_cnt_ = tcl_timer_->sleep(second);
    release_after_ = tcl_timer_->get_release_start_time_point() - phase_; // sleep 호출 후 도착 시간 저장
  }
}

/**
 * @brief NodeTimingInterface 를 추가하는 함수
 * 
 * @param node_timing_ptr SingleThreadedExecutor 에 추가할 노드의 TimingInterface 포인터
 */
void
SingleThreadedExecutor::add_node_timing_interface(rclcpp::tcl_node_interfaces::NodeTimingInterface::SharedPtr node_timing_ptr)
{
  use_tcl_timer_ = node_timing_ptr->use_tcl_timer();
  use_net_ = node_timing_ptr->use_net();
  use_blocking_io_ = node_timing_ptr->use_blocking_io();
  profile_enabled_ = node_timing_ptr->profile_enabled();

  std::cout << "use_tcl_timer_ : " << use_tcl_timer_ << std::endl;
  std::cout << "use_net_ : " << use_net_ << std::endl;
  std::cout << "use_blocking_io_ : " << use_blocking_io_ << std::endl;
  std::cout << "profile_enabled_ : " << profile_enabled_ << std::endl;

  if(use_net_)
  {
    networking_topic_ = node_timing_ptr->get_networking_topic();
  }

  if(profile_enabled_)
  {
    tcl_profiler_ = node_timing_ptr->get_profiler();
    tcl_timing_propagator_ = node_timing_ptr->get_timing_propagator();    
    if(use_blocking_io_)
    {
      auto blocking_topics = node_timing_ptr->get_blocking_topics();

      std::for_each(blocking_topics.begin(), blocking_topics.end(), [&](auto& topic)
      {
        blocking_condition_map_[topic] = false;
      });
    }
    if(use_tcl_timer_)
    {
      src_tasks_ = node_timing_ptr->get_src_tasks(); // 노드의 시작 태스크 이름
      tcl_timer_ = node_timing_ptr->get_timer(); // NodeReleaseTimer 객체
      global_ = tcl_timer_->get_global_ref_time().count(); // global reference time 
      phase_ = tcl_timer_->get_phase_ns().count(); // 위상(ns)
      period_ = tcl_timer_->get_period_ns().count(); // 주기(ns)
    }
  }
}

void
SingleThreadedExecutor::spin_some_impl(std::chrono::nanoseconds max_duration, bool exhaustive)
{
  auto start = std::chrono::steady_clock::now();
  auto max_duration_not_elapsed = [max_duration, start]() {
      if (std::chrono::nanoseconds(0) == max_duration) {
        // told to spin forever if need be
        return true;
      } else if (std::chrono::steady_clock::now() - start < max_duration) {
        // told to spin only for some maximum amount of time
        return true;
      }
      // spun too long
      return false;
    };

  if (spinning.exchange(true)) {
    throw std::runtime_error("spin_some() called while already spinning");
  }
  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );
  bool work_available = false;
  while (rclcpp::ok(context_) && spinning.load() && max_duration_not_elapsed()) {
    AnyExecutable any_exec;
    if (!work_available) {
      wait_for_work(std::chrono::milliseconds::zero());
    }
    if (get_next_ready_executable(any_exec)) {
      execute_any_executable_for_tcl(any_exec);
      work_available = true;
    } else {
      if (!work_available)
      {
        if(!use_blocking_io_)
        {
          if(!exhaustive)
          {
            break;
          }
        }
        else
        {
          if(profile_enabled_ && is_condition_satisfied_)
          {
            reset_blocking_condition_map();
            is_condition_satisfied_ = false;
            break;
          }
        }
      }
      work_available = false;
    }
  }
}

/**
 * @brief blocking I/O 혹은 non-blocking I/O 기능인지에 따라 콜백을 버퍼에 저장하거나 바로 실행함
 * 
 * @param any_exec 실행가능한 subscription 이나 timer 를 가지는 any_exec 객체
 */
void
SingleThreadedExecutor::execute_any_executable_for_tcl(AnyExecutable & any_exec)
{
  if (!spinning.load()) {
    return;
  }
  if (any_exec.subscription) {
    TRACEPOINT(
      rclcpp_executor_execute,
      static_cast<const void *>(any_exec.subscription->get_subscription_handle().get()));
    if(use_tcl_timer_ && use_blocking_io_) // blocking I/O 일 경우 subscription 콜백을 바로 실행하지 않고 버퍼에 저장
    {
      add_subscription_to_buffer(any_exec.subscription); // subscription 를 버퍼에 저장
      update_blocking_condition_map(any_exec.subscription->get_topic_name()); // 현재 수신한 토픽 이름을 통해 blocking_condition_map_ 업데이트
    }
    else
    {
      if(profile_enabled_ && use_blocking_io_) // tcl_timer 를 사용하지 않지만 프로파일링하는 경우
      {
        update_blocking_condition_map(any_exec.subscription->get_topic_name()); // blocking_condition_map_ 업데이트 
        if(check_blocking_condition())
        {
          execution_start_time_ = get_now(); // blocking_topic들을 모두 수신한 경우 실행 시작 시간 저장
          is_condition_satisfied_ = true; 
        }
        if(use_net_)
        {
          if(networking_topic_ == std::string(any_exec.subscription->get_topic_name())) // 현재 수신받은 토픽이름이 networking_topic 이름일 경우
            communication_end_ = get_now().nanoseconds(); // 통신 종료 시점 저장
        }
      }
      execute_subscription(any_exec.subscription); // tcl_timer_ 를 사용하지 않으니 subscription callback 바로 실행
    }
  }
  if (any_exec.timer) {
    TRACEPOINT(
      rclcpp_executor_execute,
      static_cast<const void *>(any_exec.timer->get_timer_handle().get()));
    if(use_tcl_timer_ && use_blocking_io_) // tcl_timer_ 를 사용하고 blocking I/O 일 경우
      add_timer_to_buffer(any_exec.timer); // timer 객체 버퍼에 저장
    else
    {
      execution_start_time_ = get_now(); 
      execute_timer(any_exec.timer);
      if(profile_enabled_ && !use_blocking_io_) // tcl_timer_ 를 사용하지 않고(통신시간 프로파일링 시) 프로파일링 하는 non-blocking I/O 의 노드의 경우
      {
        is_condition_satisfied_ = true; // timercallback 실행 종료 시점이 노드 실행 종료 시점과 동일함 -> Profile.msg 발행을 위해 is_condition_satisfied_ = true
      }
    }
  }
  if (any_exec.service) {
    execute_service(any_exec.service);
  }
  if (any_exec.client) {
    execute_client(any_exec.client);
  }
  if (any_exec.waitable) {
    any_exec.waitable->execute(any_exec.data);
  }

  if(use_tcl_timer_ && use_blocking_io_ && check_blocking_condition() && !subscription_objects_.empty()) // blocking I/O 의 경우, blocking topic 이 모두 도착하고 subscription_objects_ 버퍼가 비어있지 않다면
  {
    handle_buffer(); // 버퍼에 저장된 callback 들 실행
    this->get_src_ref_time_arr(crt_times_); // callback 으로 처리한 blocking topic 들의 creation_time 들을 얻어옴
    is_condition_satisfied_ = true; // blocking topic 들이 모두 수신되었으니 true
    received_ = true; // blocking topic 을 수신받았기 때문에, true 로 변경
    timeout_ = -1; // 초기 timeout 기능을 사용하지 않기 때문에 get_next_executable의 timeout 인자의 디폴트 값(-1) 로 변경
  }

  // Reset the callback_group, regardless of type
  any_exec.callback_group->can_be_taken_from().store(true);
  // Wake the wait, because it may need to be recalculated or work that
  // was previously blocked is now available.
  try {
    interrupt_guard_condition_.trigger();
  } catch (const rclcpp::exceptions::RCLError & ex) {
    throw std::runtime_error(
            std::string(
              "Failed to trigger guard condition from execute_any_executable: ") + ex.what());
  }
}

/**
 * @brief blocking I/O 기능을 사용할 경우 subcrtipion 객체를 버퍼에 저장함
 * 
 * @param subscription 버퍼에 저장할 subscription 객체
 */
void
SingleThreadedExecutor::add_subscription_to_buffer(rclcpp::SubscriptionBase::SharedPtr subscription)
{
  rclcpp::MessageInfo message_info;
  message_info.get_rmw_message_info().from_intra_process = false;

  if (subscription->is_serialized()) {
    // This is the case where a copy of the serialized message is taken from
    // the middleware via inter-process communication.
    std::shared_ptr<SerializedMessage> serialized_msg = subscription->create_serialized_message();
    take_and_do_error_handling(
      "taking a serialized message from topic",
      subscription->get_topic_name(),
      [&]() {return subscription->take_serialized(*serialized_msg.get(), message_info);},
      [&]()
      {
        subscription->handle_serialized_message(serialized_msg, message_info);
      });
    subscription->return_serialized_message(serialized_msg);
  } else if (subscription->can_loan_messages()) {
    // This is the case where a loaned message is taken from the middleware via
    // inter-process communication, given to the user for their callback,
    // and then returned.
    void * loaned_msg = nullptr;
    // TODO(wjwwood): refactor this into methods on subscription when LoanedMessage
    //   is extened to support subscriptions as well.
    take_and_do_error_handling(
      "taking a loaned message from topic",
      subscription->get_topic_name(),
      [&]()
      {
        rcl_ret_t ret = rcl_take_loaned_message(
          subscription->get_subscription_handle().get(),
          &loaned_msg,
          &message_info.get_rmw_message_info(),
          nullptr);
        if (RCL_RET_SUBSCRIPTION_TAKE_FAILED == ret) {
          return false;
        } else if (RCL_RET_OK != ret) {
          rclcpp::exceptions::throw_from_rcl_error(ret);
        }
        return true;
      },
      [&]() {subscription->handle_loaned_message(loaned_msg, message_info);});
    if (nullptr != loaned_msg) {
      rcl_ret_t ret = rcl_return_loaned_message_from_subscription(
        subscription->get_subscription_handle().get(),
        loaned_msg);
      if (RCL_RET_OK != ret) {
        RCLCPP_ERROR(
          rclcpp::get_logger("rclcpp"),
          "rcl_return_loaned_message_from_subscription() failed for subscription on topic '%s': %s",
          subscription->get_topic_name(), rcl_get_error_string().str);
      }
      loaned_msg = nullptr;
    }
  } else {
    // This case is taking a copy of the message data from the middleware via
    // inter-process communication.
    std::shared_ptr<void> message = subscription->create_message();
    std::string topic_name_str = std::string(subscription->get_topic_name());

    if(topic_name_str == "/parameter_events" || topic_name_str == "/tf" || topic_name_str == "/tf_static") // 해당 이름의 토픽들은 버퍼에 저장하지 않고 바로 실행
    {
      take_and_do_error_handling(
      "taking a message from topic",
      subscription->get_topic_name(),
      [&]() {return subscription->take_type_erased(message.get(), message_info);},
      [&]() {subscription->handle_message(message, message_info);});
    }
    else
    {
      take_and_do_error_handling(
      "taking a message from topic",
      subscription->get_topic_name(),
      [&]() {return subscription->take_type_erased(message.get(), message_info);},
      [&]() 
      {
        subscription_objects_.push_back(subscription_object_t(subscription, message, message_info)); // subscription_objects_ 버퍼에 저장
      });
    }
    subscription->return_message(message);
  }
}

/**
 * @brief blocking I/O 기능을 위해 timer 객체를 벡터에 저장함
 * 
 * @param timer : timer_objects_ 에 저장할 TimerBase 객체 포인터
 */
void
SingleThreadedExecutor::add_timer_to_buffer(rclcpp::TimerBase::SharedPtr timer)
{
  timer_objects_.push_back(timer);
}

/**
 * @brief blocking I/O 조건 만족 시 호출하는 함수로서 subscription_objects_ 와 timer_objects 에 쌓인 콜백을 모두 수행 시킴
 *        동시에 노드의 실행 시작 시간을 저장함
 * 
 */
void
SingleThreadedExecutor::handle_buffer()
{
  execution_start_time_ = get_now();
  
  if(use_tcl_timer_)
  {
    std::cout << "\nrelease_start_time : " << (tcl_timer_->get_release_start_time_point() % (int)1e9 )*1e-6 << std::endl;
    std::cout << "execution_start_time : " << (execution_start_time_.nanoseconds() % (int)1e9) * 1e-6<< std::endl;
  }
  
  handle_subscription_buffer();
  handle_timer_buffer();
}

/**
 * @brief subscription_objects_ 에 쌓인 subscription 콜백을 모두 실행 시킴
 * 
 */
void
SingleThreadedExecutor::handle_subscription_buffer()
{
  std::for_each(subscription_objects_.begin(),  subscription_objects_.end(), [&](auto& it)
  {
    // std::cout << it.subscription->get_topic_name() << " callback" << std::endl;
    it.subscription->handle_message(it.message, it.message_info);
  });
  subscription_objects_.clear();
}

/**
 * @brief timer_objects에 쌓인 timer 콜백을 모두 실행시킴 
 * 
 */
void
SingleThreadedExecutor::handle_timer_buffer()
{
  std::for_each(timer_objects_.begin(), timer_objects_.end(), [&](auto& timer)
  {
    execute_timer(timer);
  });
  timer_objects_.clear();
}

/**
 * @brief blocking_condition_map 이 모두 true 인지, 즉 모든 blocking topic 을 수신받았는지 체크함
 * 
 * @return true : 모든 blocking topic 을 수신받았을 경우 true
 * @return false : blocking topic 을 하나라도 수신받지 못한 경우 false
 */
bool
SingleThreadedExecutor::check_blocking_condition()
{
  for(auto& it : blocking_condition_map_)
  {
    if(it.second == false)
      return false;
  }

  return true;
  
}

/**
 * @brief 수신한 topic_name 을 기반으로 blocking_condition_map 을 업데이트 함
 * 
 * @param topic_name 수신한 토픽의 이름
 */
void
SingleThreadedExecutor::update_blocking_condition_map(const char * topic_name)
{
  std::string topic_name_str = std::string(topic_name);

  std::for_each(blocking_condition_map_.begin(), blocking_condition_map_.end(), [&](auto& it)
  {
    if(it.first == topic_name_str)
      it.second = true;
  });
}

/**
 * @brief blocking_condition_map 을 false 로 초기화 하는 함수
 * 
 */
void
SingleThreadedExecutor::reset_blocking_condition_map()
{
  std::for_each(blocking_condition_map_.begin(), blocking_condition_map_.end(), [&](auto& it)
  {
    it.second = false;
  });
}

/**
 * @brief Profile.msg 를 발행하는 함수
 * 
 * @param mod_release_time skip count 를 통해 수정된 노드의 release_time
 */
void
SingleThreadedExecutor::publish_profile_data(int64_t mod_release_time)
{
  if(tcl_profiler_ && tcl_timer_)
  {
    int64_t release_start;
    if(mod_release_time == 0)
      release_start = tcl_timer_->get_release_start_time_point();
    else{
      release_start = mod_release_time;
      std::cout << "release_start_time(mod!) : " << mod_release_time << std::endl;
    }
    auto execution_start = execution_start_time_.nanoseconds();

    auto execution_end = get_now().nanoseconds();
    auto release_end = execution_end;
    
    std::cout << "execution_end_time : " << (execution_end % (int)1e9 )* 1e-6 << std::endl;
    std::cout << "MSG_ID : " ;
    if(tcl_timing_propagator_->get_timing_header()->msg_infos.size() > 0)
    {
      for(int i = 0; i<tcl_timing_propagator_->get_timing_header()->msg_infos.size(); i++)
        std::cout <<  tcl_timing_propagator_->get_timing_header()->msg_infos[i].msg_id << " ";
    }
    std::cout << std::endl;

    auto elapse= (execution_end - execution_start);
    if(elapse > period_)
      std::cout<<"elapse : "<< elapse <<std::endl;

    std::vector<tcl_std_msgs::msg::TimingHeader> &timing_header_arr = tcl_timing_propagator_->get_timing_header_arr();
    tcl_profiler_->publish(
      timing_header_arr,
      release_start,
      release_end,
      execution_start,
      execution_end,
      communication_start_);

    timing_header_arr.clear();
  }

}

/**
 * @brief 수신한 blocking topic 들에 포함된 creation_time 을 가져오는 함수
 * 
 * @param crt_times : creation_time 을 저장할 벡터
 */
void
SingleThreadedExecutor::get_src_ref_time_arr(std::vector<std::pair<std::string, int64_t>>& crt_times)// for node with more than 1 blocking topic
{ 
  if(use_tcl_timer_ && tcl_timing_propagator_ && use_blocking_io_)
  {
    crt_times.clear();
    auto timing_header = tcl_timing_propagator_->get_timing_header();
    if(!timing_header->msg_infos.empty())
    {
      for(auto minfo : timing_header->msg_infos)
      {
        if(!minfo.task_history.empty())
        {
          if(!src_tasks_.empty()){
            for(auto src_task: src_tasks_){
              if(std::string(minfo.task_history[0]) == src_task)
              {
                auto offset = (minfo.creation_time - global_) % period_;
                auto crt_time = minfo.creation_time - offset; // creation_time 과 global reference time 을 사용하여 reference time 을 계산함
                crt_times.push_back(std::make_pair(src_task, crt_time));
              }
            }
          }
        }  
      }
    }
  }
}