#include "rclcpp/tcl_node_interfaces/node_profile.hpp"

using rclcpp::tcl_node_interfaces::NodeProfile;

NodeProfile::NodeProfile(
    const char * node_name,
    rclcpp::node_interfaces::NodeParametersInterface::SharedPtr node_parameters,
    rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr node_topics)
{
    node_name_ = std::string(node_name);
    const std::string suffix = std::string("/tcl_profile_data");
    const std::string topic_name = node_name + suffix;

    profile_data_publisher_ = rclcpp::detail::create_publisher<tcl_std_msgs::msg::ProfileData>(
        node_parameters,
        node_topics,
        topic_name,
        rclcpp::QoS(rclcpp::KeepLast(10)));
}

void NodeProfile::publish(
    std::vector<tcl_std_msgs::msg::TimingHeader> &timing_header_arr,
    int64_t release_start,
    int64_t release_end,
    int64_t execution_start,
    int64_t execution_end,
    int64_t networking_start,
    int64_t networking_end)
{
    if(profile_data_publisher_)
    {
        tcl_std_msgs::msg::ProfileData::SharedPtr msg(new tcl_std_msgs::msg::ProfileData());
        if(!timing_header_arr.empty())
            msg->timing_header = timing_header_arr;
        // else
        //     msg->timing_header = tcl_std_msgs::msg::TimingHeader();
        msg->release_time.start   = release_start;
        msg->release_time.end     = release_end;
        msg->execution_time.start = execution_start;
        msg->execution_time.end   = execution_end;
        msg->networking_time.start = networking_start;
        msg->networking_time.end = networking_end;
        profile_data_publisher_->publish(*msg);
    }
}