#ifndef RCLCPP__TCL_NODE_INTERFACES__NODE_PROFILE_INTERFACE_HPP_
#define RCLCPP__TCL_NODE_INTERFACES__NODE_PROFILE_INTERFACE_HPP_

#include "rclcpp/macros.hpp"
#include "rclcpp/visibility_control.hpp"
#include "tcl_std_msgs/msg/profile_data.hpp"
#include "rclcpp/time.hpp"
#include "rclcpp/create_publisher.hpp"
#include "rclcpp/node_interfaces/node_parameters_interface.hpp"
#include "rclcpp/node_interfaces/node_topics_interface.hpp"
#include "rclcpp/node_interfaces/get_node_topics_interface.hpp"


namespace rclcpp
{
namespace tcl_node_interfaces
{   
    class NodeProfileInterface
    {
    public:
        RCLCPP_SMART_PTR_ALIASES_ONLY(NodeProfileInterface)

        RCLCPP_PUBLIC
        virtual
        ~NodeProfileInterface() = default;
        
        RCLCPP_PUBLIC
        virtual void 
        publish(
            std::vector<tcl_std_msgs::msg::TimingHeader> &timing_header_arr,
            int64_t release_start,
            int64_t release_end,
            int64_t execution_start,
            int64_t execution_end,
            int64_t networking_start,
            int64_t networking_end = 0 ) = 0;
    };

}
}
#endif