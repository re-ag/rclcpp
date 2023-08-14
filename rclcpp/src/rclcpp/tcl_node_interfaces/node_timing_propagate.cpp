#include "rclcpp/tcl_node_interfaces/node_timing_propagate.hpp"

using rclcpp::tcl_node_interfaces::NodeTimingPropagate;

NodeTimingPropagate::NodeTimingPropagate(
    const char * node_name,
    std::vector<std::string>& sub_timing_topics,
    rclcpp::node_interfaces::NodeParametersInterface::SharedPtr node_parameters,
    rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr node_topics
)
{
    (void)node_parameters;
    (void)node_topics;
    (void)sub_timing_topics;
    node_name_ = std::string(node_name);

    timing_header_msg_ = std::make_shared<tcl_std_msgs::msg::TimingHeader>();
}

void 
NodeTimingPropagate::receive_timing_header(tcl_std_msgs::msg::TimingHeader::SharedPtr msg)
{   
    if(timing_header_msg_)
    {
        auto msg_infos = msg->msg_infos;
        std::for_each(msg_infos.begin(), msg_infos.end(), [&](auto& msg_info){
            msg_info.task_history.push_back(node_name_);
        });
    
        std::for_each(msg_infos.begin(), msg_infos.end(), [&](auto& msg_info)
        {
            if(timing_header_msg_->msg_infos.size() > 0)
            {
                auto ret = std::find_if(timing_header_msg_->msg_infos.begin(), timing_header_msg_->msg_infos.end(), [&](auto& m_msg_info)
                {
                    return msg_info.task_history == m_msg_info.task_history;
                });
    
                if(ret == timing_header_msg_->msg_infos.end())
                {
                    timing_header_msg_->msg_infos.push_back(msg_info);
                }
                else
                {
                    ret->msg_id = msg_info.msg_id;
                    ret->creation_time = msg_info.creation_time;
                }
            }
               else
            {
                timing_header_msg_->msg_infos.push_back(msg_info);
            }
        });
    
        timing_header_msg_->task_name = node_name_;
        timing_header_msg_->published = false;
        timing_header_arr_.push_back(*timing_header_msg_);
    }
}

void 
NodeTimingPropagate::receive_timing_header(tcl_std_msgs::msg::TimingHeader& msg)
{
    if(timing_header_msg_)
    {
        auto msg_infos = msg.msg_infos;
        std::for_each(msg_infos.begin(), msg_infos.end(), [&](auto& msg_info){
            msg_info.task_history.push_back(node_name_);
        });

        std::for_each(msg_infos.begin(), msg_infos.end(), [&](auto& msg_info)
        {
            if(timing_header_msg_->msg_infos.size() > 0)
            {
                auto ret = std::find_if(timing_header_msg_->msg_infos.begin(), timing_header_msg_->msg_infos.end(), [&](auto& m_msg_info)
                {
                    return msg_info.task_history == m_msg_info.task_history;
                });

                if(ret == timing_header_msg_->msg_infos.end())
                {
                    timing_header_msg_->msg_infos.push_back(msg_info);
                }
                else
                {
                    ret->msg_id = msg_info.msg_id;
                    ret->creation_time = msg_info.creation_time;
                }
            }
            else
            {
                timing_header_msg_->msg_infos.push_back(msg_info);
            }
        });
    
        timing_header_msg_->task_name = node_name_;
        timing_header_msg_->published = false;
        timing_header_arr_.push_back(*timing_header_msg_);
    }
}

void 
NodeTimingPropagate::receive_timing_header(const tcl_std_msgs::msg::TimingHeader& msg)
{
    if(timing_header_msg_)
    {
        auto msg_infos = msg.msg_infos;
        std::for_each(msg_infos.begin(), msg_infos.end(), [&](auto& msg_info){
            msg_info.task_history.push_back(node_name_);
        });

        std::for_each(msg_infos.begin(), msg_infos.end(), [&](auto& msg_info)
        {
            if(timing_header_msg_->msg_infos.size() > 0)
            {
                auto ret = std::find_if(timing_header_msg_->msg_infos.begin(), timing_header_msg_->msg_infos.end(), [&](auto& m_msg_info)
                {
                    return msg_info.task_history == m_msg_info.task_history;
                });

                if(ret == timing_header_msg_->msg_infos.end())
                {
                    timing_header_msg_->msg_infos.push_back(msg_info);
                }
                else
                {
                    ret->msg_id = msg_info.msg_id;
                    ret->creation_time = msg_info.creation_time;
                }
            }
            else
            {
                timing_header_msg_->msg_infos.push_back(msg_info);
            }
        });
        timing_header_msg_->task_name = node_name_;
        timing_header_msg_->published = false;
        timing_header_arr_.push_back(*timing_header_msg_);
}
}

void
NodeTimingPropagate::create_timing_header()
{
    if(timing_header_msg_)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        auto now = rclcpp::Time(static_cast<int32_t>(ts.tv_sec), static_cast<uint32_t>(ts.tv_nsec));

        int64_t create_time = now.nanoseconds();
        uint64_t msg_id = create_time / 10000;

        tcl_std_msgs::msg::TimingHeader timing_header;
        tcl_std_msgs::msg::MessageInfo msg_info;

        msg_info.msg_id = msg_id;
        msg_info.creation_time = create_time;
        msg_info.task_history.push_back(node_name_);

        timing_header.msg_infos.push_back(msg_info);
        timing_header.task_name = node_name_;
        timing_header.published = false;
        *timing_header_msg_ = timing_header;

        timing_header_arr_.push_back(*timing_header_msg_);
    }
}


void
NodeTimingPropagate::create_timing_headers() // for source task in middle path like bpp
{
    if(timing_header_msg_)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        auto now = rclcpp::Time(static_cast<int32_t>(ts.tv_sec), static_cast<uint32_t>(ts.tv_nsec));

        int64_t create_time = now.nanoseconds();
        uint64_t msg_id = create_time / 10000;

        bool found = false;
        for(auto& minfo: timing_header_msg_->msg_infos){
            if(!minfo.task_history.empty()){
                if(minfo.task_history[0] == node_name_){
                    minfo.msg_id = msg_id;
                    minfo.creation_time = create_time;
                    found = true;
                }
            }
        }
        if(!found){
            tcl_std_msgs::msg::MessageInfo msg_info;
            msg_info.msg_id = msg_id;
            msg_info.creation_time = create_time;
            msg_info.task_history.push_back(node_name_);
            timing_header_msg_->msg_infos.push_back(msg_info);
        }
        

        timing_header_msg_->task_name = node_name_;
        timing_header_msg_->published = false;
        timing_header_arr_.push_back(*timing_header_msg_);
    }
}

void
NodeTimingPropagate::publish_timing_message()
{
    if(timing_publisher_)
        timing_publisher_->publish(*timing_header_msg_);
}

tcl_std_msgs::msg::TimingHeader::SharedPtr
NodeTimingPropagate::get_timing_header() const
{
    return this->timing_header_msg_;
}

std::vector<tcl_std_msgs::msg::TimingHeader>&
NodeTimingPropagate::get_timing_header_arr() 
{
    return this->timing_header_arr_;
}