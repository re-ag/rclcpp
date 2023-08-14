#include "rclcpp/tcl_node_interfaces/node_release_timer.hpp"

using rclcpp::tcl_node_interfaces::NodeReleaseTimer;

NodeReleaseTimer::NodeReleaseTimer(
    int64_t rate,
    int64_t phase,
    int64_t ref_time
) : rate_(rate), 
    phase_(phase), 
    global_ref_time_(rclcpp::Time(ref_time)), 
    timerfd_(-1)
{
    auto period_ms = std::chrono::milliseconds(static_cast<uint64_t>(1000.0 / static_cast<double>(rate_)));
    period_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(period_ms);

    local_ref_time_= global_ref_time_ + rclcpp::Duration(std::chrono::nanoseconds(phase_));
    release_start_time_ = local_ref_time_.nanoseconds();
    std::cout<<std::setprecision(15);
    init_timer();
    sleep(); // timerfd 를 초기화한 뒤, 첫번째 태스크 인스턴스의 시작을 위해 sleep 호출을 미리 한번 해야 함
}

/**
 * @brief timerfd 를 생성하고 초기화 하는 함수
 * 
 */
void
NodeReleaseTimer::init_timer()
{   
    if(timerfd_ != -1)
        close(timerfd_);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME,&ts);

    auto now = rclcpp::Time(static_cast<int32_t>(ts.tv_sec), static_cast<uint32_t>(ts.tv_nsec));
    
    int64_t timer_expired_time = local_ref_time_.nanoseconds();

    if(now >= local_ref_time_)
    {
        int64_t curr_offset = (now - global_ref_time_).nanoseconds() % period_ns_.count();
        int64_t offset_diff = phase_ - curr_offset + period_ns_.count();
        timer_expired_time = now.nanoseconds() + offset_diff;
    }

    std::cout << "now : " << now.nanoseconds() << std::endl;
    std::cout << "l_ref : " << local_ref_time_.nanoseconds() << std::endl;
    std::cout << "g_ref : " << global_ref_time_.nanoseconds() << std::endl;
    std::cout << "exp : " << timer_expired_time << std::endl;

    struct itimerspec timeout;

    timeout.it_value.tv_sec = timer_expired_time / (int64_t)1e9;
    timeout.it_value.tv_nsec = timer_expired_time % (int64_t)1e9;
    timeout.it_interval.tv_sec = period_ns_.count() / 1000000000;
    timeout.it_interval.tv_nsec = period_ns_.count() % 1000000000;

    if ((timerfd_ = timerfd_create(CLOCK_REALTIME, 0)) <= 0)
    {
        std::cout << "timerfd_create failed!" << std::endl;
    }

    if (timerfd_settime(timerfd_, TFD_TIMER_ABSTIME, &timeout, NULL) != 0)
    {
        std::cout << "timerfd_settime fail" << std::endl;
    }
}

/**
 * @brief timerfd 의 read 함수를 호출하여 다음 도착 시간까지 대기 후, release_start_time 을 저장함
 * 
 * @param second : true 일 경우, timerfd 가 expired 된 경우(=즉 이미 타이머 이벤트가 발생 된 뒤 read 함수를 호출한 경우) 한번 더 read 함수를 호출함
 * @return int64_t : read 함수 호출 시, timerfd 가 expired 되지 않았다면 -1 을 리턴, expired 된 경우 이미 지나친 타이머 이벤트 발생 수를 리턴
 */
int64_t
NodeReleaseTimer::sleep(bool second)
{   
    bool expired = false;
    int64_t task_skip_cnt;
    unsigned long long missed;
    struct timespec ts;
    struct timespec before_read;
    struct timespec after_read;

    clock_gettime(CLOCK_REALTIME,&before_read);
    auto before = rclcpp::Time(static_cast<int32_t>(before_read.tv_sec), static_cast<uint32_t>(before_read.tv_nsec));
    std::cout << "before read : " << before.nanoseconds() << std::endl;

    if (read(timerfd_, &missed, sizeof(missed)) < 0)
        std::cout << "timer read error" << std::endl;
    
    clock_gettime(CLOCK_REALTIME,&after_read);
    auto after = rclcpp::Time(static_cast<int32_t>(after_read.tv_sec), static_cast<uint32_t>(after_read.tv_nsec));
    int64_t diff = after.nanoseconds() - before.nanoseconds();

    if(diff < 100000) // timerfd 의 이벤트 발생이 이미 발생한 뒤 read 함수를 호출한 경우(read함수 바로 리턴됨)
    {
        std::cout << "timerfd is expired..." << std::endl;
        task_skip_cnt = missed; // read 함수의 리턴값(이미 발생한 이벤트 발생 수)
        if(second)
        {
            if (read(timerfd_, &missed, sizeof(missed)) < 0)
                std::cout << "timer read error" << std::endl;
            task_skip_cnt = missed;
        }
    }
    else{
        if(missed > 1){
            task_skip_cnt = missed;
        }
        else{
            task_skip_cnt = -1;
        }
    }

    std::cout << "task_skip_cnt is " << task_skip_cnt << std::endl;
    clock_gettime(CLOCK_REALTIME,&ts);
    auto now = rclcpp::Time(static_cast<int32_t>(ts.tv_sec), static_cast<uint32_t>(ts.tv_nsec));
    int64_t curr_offset = (now - local_ref_time_).nanoseconds() % period_ns_.count();
    release_start_time_ = now.nanoseconds() - curr_offset;
    return task_skip_cnt;
}

int64_t
NodeReleaseTimer::get_release_start_time_point() const
{
    return release_start_time_;
}

std::chrono::nanoseconds
NodeReleaseTimer::get_period_ns() const
{
    return this->period_ns_;
}

std::chrono::nanoseconds
NodeReleaseTimer::get_phase_ns() const
{
    return std::chrono::nanoseconds(this->phase_);
}
std::chrono::nanoseconds
NodeReleaseTimer::get_global_ref_time() const
{
    return std::chrono::nanoseconds(this->global_ref_time_.nanoseconds());
}
